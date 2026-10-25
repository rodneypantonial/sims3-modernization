// Address-space budget of TS3W.exe (docs/66): rebuilds the menu / BP-live table from files that already exist.
// Reads only: probe-out/queue/*.jsonl (memory before/after + native counters), probe-out/*/memory.csv,
// probe-out/phase0/*/*.csv (ResourceCache and AnimationCache bytes), probe-out/queue/bin-before/ts3native.ini.
// Never starts the game or a queue.
//   node tools/harness/addr-budget.ts [--probe-out <dir>] [--json]
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
const opt = (k: string) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : undefined; };
const repo = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const PO = opt('--probe-out') ?? join(repo, 'probe-out');

export const median = (a: number[]) => {
  const s = a.filter(Number.isFinite).sort((x, y) => x - y);
  if (!s.length) return NaN;
  const m = s.length >> 1;
  return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
};
export function parseCsv(text: string): Record<string, number>[] {
  const [h, ...rows] = text.trim().split(/\r?\n/);
  const keys = h.split(',');
  return rows.map(r => Object.fromEntries(r.split(',').map((v, i) => [keys[i], Number(v)])));
}
// self-check
console.assert(median([3, 1, 2]) === 2 && median([1, 2, 3, 4]) === 2.5 && Number.isNaN(median([])), 'median');
console.assert(parseCsv('a,b\r\n1,2\n3,4')[1].b === 4, 'parseCsv');

const read = (p: string) => { try { return readFileSync(p, 'utf8'); } catch { return ''; } };
const MB = (b: number | undefined) => (b == null || b < 0 ? NaN : b / 1048576);

// 1. Queue jobs: memory before/after (probe /memory) and native counters (Verify.Memory).
type Mem = { commitMB: number; reserveOnlyMB: number; freeMB: number; largestFreeMB: number; imageMB: number; mappedMB: number; privateMB: number };
const jobs: { save: string; job: string; before?: Mem; after?: Mem; rcBefore: number; rcAfter: number }[] = [];
const qdir = join(PO, 'queue');
for (const f of existsSync(qdir) ? readdirSync(qdir).filter(f => f.endsWith('.jsonl')) : []) {
  for (const l of read(join(qdir, f)).split('\n').filter(Boolean)) {
    let o: any; try { o = JSON.parse(l); } catch { continue; }
    if (!o.memory?.before) continue;
    jobs.push({ save: o.save ?? '', job: o.job, before: o.memory.before, after: o.memory.after,
      rcBefore: MB(o.counters?.before?.vfMemResCacheBytes), rcAfter: MB(o.counters?.after?.vfMemResCacheBytes) });
  }
}
const menu = jobs.filter(j => j.save === 'menu');
const bp = jobs.filter(j => j.save === 'Baseline-BP');
const col = (js: typeof jobs, side: 'before' | 'after', k: keyof Mem) => median(js.map(j => j[side]?.[k] as number));

// 2. All memory.csv samples (1 per ~10 s, every probe run): thread count and spread in live mode (commit > 2000 MB).
const csvRows: Record<string, number>[] = [];
for (const d of existsSync(PO) ? readdirSync(PO) : []) {
  const p = join(PO, d, 'memory.csv');
  if (existsSync(p)) csvRows.push(...parseCsv(read(p)));
}
const live = csvRows.filter(r => r.commitMB > 2000);
const pct = (k: string, p: number) => { const s = live.map(r => r[k]).filter(Number.isFinite).sort((a, b) => a - b); return s[Math.floor((s.length - 1) * p)]; };

// 3. Phase 0 CSVs: ResourceCache held and AnimationCache bytes per sample.
const p0: Record<string, number>[] = [];
const p0dir = join(PO, 'phase0');
for (const d of existsSync(p0dir) ? readdirSync(p0dir) : [])
  for (const f of readdirSync(join(p0dir, d)).filter(f => f.endsWith('.csv'))) p0.push(...parseCsv(read(join(p0dir, d, f))));

// 4. Installed ts3native.ini: Boehm initial heap.
const heapMB = Number(/^InitialHeapMB=(\d+)/m.exec(read(join(qdir, 'bin-before', 'ts3native.ini')))?.[1] ?? NaN);

const f0 = (x: number) => (Number.isFinite(x) ? Math.round(x).toString() : '?');
const rcLive = median(bp.flatMap(j => [j.rcBefore, j.rcAfter]));
const threads = median(live.map(r => r.threads));
const S = {
  menu: Object.fromEntries((['commitMB', 'reserveOnlyMB', 'freeMB', 'largestFreeMB', 'imageMB', 'mappedMB', 'privateMB'] as const).map(k => [k, col(menu, 'before', k)])),
  bpStart: Object.fromEntries((['commitMB', 'reserveOnlyMB', 'freeMB', 'largestFreeMB', 'imageMB', 'mappedMB', 'privateMB'] as const).map(k => [k, col(bp, 'before', k)])),
  bpEnd: Object.fromEntries((['commitMB', 'reserveOnlyMB', 'freeMB', 'largestFreeMB', 'imageMB', 'mappedMB', 'privateMB'] as const).map(k => [k, col(bp, 'after', k)])),
};
const stacksMB = threads * 1; // PE SizeOfStackReserve 1 MB (docs/66); shortcut: S3SS 64 KB and ts3native 256 KB stacks counted as 1 MB, upgrade with a per-thread /threads stack scan.
const known = heapMB + 8 /* GC growth over initial, F15 */ + rcLive + median(p0.map(r => r.animBytesMB)) + 18.5 + 8 + 1 + 12.5 + stacksMB;

if (args.includes('--json')) {
  console.log(JSON.stringify({ menuJobs: menu.length, bpJobs: bp.length, csvSamples: csvRows.length, liveSamples: live.length, S, threads, rcLive, heapMB, known }, null, 2));
  process.exit(0);
}
const row = (name: string, k: string, src: string) => `| ${name} | ${f0(S.menu[k])} | ${f0(S.bpStart[k])} | ${f0(S.bpEnd[k])} | ${src} |`;
const out = [
  `Sources: ${menu.length} menu job(s), ${bp.length} BP job(s) in probe-out/queue/*.jsonl; ${csvRows.length} memory.csv samples (${live.length} live); ${p0.length} phase0 samples.`,
  '',
  '| Region (MB) | Main menu | BP live, start | BP live, after 7-30 min | Tag |',
  '|---|---|---|---|---|',
  row('Committed', 'commitMB', '[measured]'),
  row('Reserved, not committed', 'reserveOnlyMB', '[measured]'),
  row('Image (exe + DLLs + asi)', 'imageMB', '[measured]'),
  row('Mapped (file mappings: DXVK, exe mapping, NLS)', 'mappedMB', '[measured]'),
  row('Private (heaps, stacks, GC, driver)', 'privateMB', '[measured]'),
  row('Free', 'freeMB', '[measured]'),
  row('Largest free block', 'largestFreeMB', '[measured]'),
  `| Fragmentation (free - largest free) | ${f0(S.menu.freeMB - S.menu.largestFreeMB)} | ${f0(S.bpStart.freeMB - S.bpStart.largestFreeMB)} | ${f0(S.bpEnd.freeMB - S.bpEnd.largestFreeMB)} | [measured] |`,
  '',
  `Live spread (memory.csv, commit > 2000 MB): largest free p10 ${f0(pct('largestFreeMB', 0.1))} / median ${f0(pct('largestFreeMB', 0.5))}; mapped p10-p90 ${f0(pct('mappedMB', 0.1))}-${f0(pct('mappedMB', 0.9))}; threads median ${f0(threads)}.`,
  '',
  '| Private owner, BP live (MB) | Value | Tag |',
  '|---|---|---|',
  `| Boehm GC heap (ts3native InitialHeapMB) | ${f0(heapMB)} reserved+committed at Mono start, ~${f0(heapMB + 8)} after growth | [measured config; 360 MB measured F15] |`,
  `| ResourceCache held (counter vfMemResCacheBytes) | ${f0(rcLive)} (phase0 median ${f0(median(p0.map(r => r.cacheBytesMB)))}) | [measured] |`,
  `| AnimationCache (budget 15) | ${f0(median(p0.map(r => r.animBytesMB)))} | [measured phase0] |`,
  `| Thread stacks (${f0(threads)} threads x 1 MB reserve) | ~${f0(stacksMB)} | [inferred] |`,
  '| Package indexes (EA 15 + CC 3.5) | ~18.5 | [computed, docs/08] |',
  '| Catalog OBJD decode (14,577 x 576 B) | ~8 | [inferred, docs/39] |',
  '| Shaders kept (SM3 bytecode 8.7 + DBUF 3.8) | ~12.5 + DXVK SPIR-V | [measured file; inferred kept] |',
  '| Glyph cache | <= 1 | [verified bound, docs/54] |',
  `| Unattributed private (EA GeneralAllocator rest, mimalloc arenas, STBL, CAS part cache, SpeedTree, DXVK/driver) | ~${f0(S.bpStart.privateMB - known)} at start, ~${f0(S.bpEnd.privateMB - known)} after | [computed remainder] |`,
];
console.log(out.join('\n'));
