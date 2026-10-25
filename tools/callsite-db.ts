// Cumulative call-site coverage database (docs/133 §7): the [ProbeSnap] CallSites=1 lines of ts3native.log copies
// ("vcall SITE -> TARGET n=N [jmp] [out=module]", cumulative counts per game start) merged into one JSON keyed by site:
// runs seen, hits, targets (address + module), plus one row per run with the trap cost ("call sites: ... ns per trap",
// "callsites min N: ... total X ms"). Addresses and counts only, no game bytes. tools/harness/queue.ts merges every job
// whose ini had CallSites=1; tools/virtual-resolution-report.ts --db reads it.
//   node tools/callsite-db.ts merge <ts3native.log> [--job <id>] [--db <file>]   (default probe-out/callsites/db.json)
//   node tools/callsite-db.ts --self-check
// A run is one game start: the log's first line "ts3native X.Y.Z <date> <time>". Copies of one run (session reuse: each job's
// copy = start-up part + its own lines) merge without double counting: per (site, target) the last run and its count are kept
// and only the growth is added.
import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import assert from 'node:assert/strict';
import { parseProbe } from './indirect-live-vs-static.ts';

const ROOT = resolve(import.meta.dirname, '..');
export const DB_DEFAULT = join(ROOT, 'probe-out', 'callsites', 'db.json');
type Tgt = { n: number; out?: string; lastRun: string; lastN: number };
type Site = { runs: number; lastRun: string; hits: number; skipped?: number; targets: Record<string, Tgt> };
type RunRow = { job?: string; mergedAt: string; pairs: number; armed?: number; trapNs?: number; minutes?: number; trapMs?: number; lateMsPerS?: number };
export type Db = { version: 1; runs: Record<string, RunRow>; sites: Record<string, Site> };
const hx = (a: number) => `0x${a.toString(16).toUpperCase()}`;

export const emptyDb = (): Db => ({ version: 1, runs: {}, sites: {} });
export const runIdOf = (text: string) => /^ts3native (\S+ \d{4}-\d\d-\d\d \d\d:\d\d:\d\d)/.exec(text)?.[1] ?? null;

// merges one log copy; null when the text has no run header or no CallSites line (nothing to merge)
export function mergeLog(db: Db, text: string, job?: string, now = new Date().toISOString()) {
  const run = runIdOf(text);
  if (!run || !/OK {3}call sites: /.test(text)) return null;
  let added = 0, newPairs = 0;
  for (const p of parseProbe(text)) {
    const s = (db.sites[hx(p.site)] ??= { runs: 0, lastRun: '', hits: 0, targets: {} });
    if (s.lastRun !== run) { s.runs++; s.lastRun = run; }
    let t = s.targets[hx(p.target)];
    if (!t) { t = s.targets[hx(p.target)] = { n: 0, lastRun: '', lastN: 0 }; newPairs++; }
    if (p.out) t.out = p.out;
    const add = t.lastRun === run ? Math.max(0, p.n - t.lastN) : p.n;
    t.lastN = t.lastRun === run ? Math.max(t.lastN, p.n) : p.n;
    t.lastRun = run;
    t.n += add; s.hits += add; added += add;
  }
  for (const m of new Set([...text.matchAll(/vcall skip 0x([0-9A-F]+)/gi)].map((m) => parseInt(m[1], 16)))) {
    const s = (db.sites[hx(m)] ??= { runs: 0, lastRun: '', hits: 0, targets: {} });
    if (s.lastRun !== run) { s.skipped = (s.skipped ?? 0) + 1; s.lastRun = run; }
  }
  const r: RunRow = db.runs[run] ?? { mergedAt: now, pairs: 0 };
  if (job) r.job = r.job ? (r.job.split(',').includes(job) ? r.job : `${r.job},${job}`) : job;
  r.mergedAt = now;
  r.pairs = Object.values(db.sites).reduce((a, s) => a + Object.values(s.targets).filter((t) => t.lastRun === run).length, 0);
  const arm = /call sites: (\d+) of \d+ sites armed, (\d+) ns per trap/.exec(text);
  if (arm) { r.armed = Number(arm[1]); r.trapNs = Number(arm[2]); }
  const mins = [...text.matchAll(/callsites min (\d+): .*total (\d+) ms/g)].at(-1);
  if (mins && Number(mins[1]) >= (r.minutes ?? 0)) { r.minutes = Number(mins[1]); r.trapMs = Number(mins[2]); }
  db.runs[run] = r;
  return { run, added, newPairs };
}

// the DB as cumulative "vcall" lines (virtual-resolution-report.ts compareNative reads these like a log)
export const dbToLog = (db: Db) => Object.entries(db.sites).flatMap(([s, v]) =>
  Object.entries(v.targets).map(([t, x]) => `vcall ${s} -> ${t} n=${x.n}${x.out ? ` out=${x.out}` : ''}`)).join('\n');

export function loadDb(file = DB_DEFAULT): Db {
  if (!existsSync(file)) return emptyDb();
  const d = JSON.parse(readFileSync(file, 'utf8'));
  if (d.version !== 1) throw new Error(`${file}: unknown version ${d.version}`);
  return d;
}
export function saveDb(db: Db, file = DB_DEFAULT) {
  mkdirSync(dirname(file), { recursive: true });
  writeFileSync(`${file}.tmp`, JSON.stringify(db));
  renameSync(`${file}.tmp`, file);  // never a half-written DB
}
// queue.ts: merge one job's log copy; a one-line result for its log
export function mergeFile(log: string, job?: string, file = DB_DEFAULT): string {
  const db = loadDb(file), r = mergeLog(db, readFileSync(log, 'utf8'), job);
  if (!r) return 'callsites: no CallSites lines in the log (asi without v130, or off)';
  saveDb(db, file);
  const fired = Object.values(db.sites).filter((s) => Object.keys(s.targets).length).length, rr = db.runs[r.run];
  return `callsites: run ${r.run} merged (+${r.added} hits, ${r.newPairs} new pairs${rr.trapMs !== undefined ? `, trap ${rr.trapMs} ms in ${rr.minutes} min` : ''}); DB ${Object.keys(db.runs).length} runs, ${fired} sites fired`;
}

function selfCheck() {
  const head = 'ts3native 0.36.64 2026-10-25 01:00:00\nOK   call sites: 3 indirect call/jmp sites (vcallsites5.h, 0 rejected), armed in 90 s, self-disarming\n';
  const a1 = head + 'call sites: 3 of 3 sites armed, 4100 ns per trap; rule\nvcall 0x00401000 -> 0x00500000 n=10\nvcall 0x00402000 -> 0x10000000 n=2 out=d3d9.dll\ncallsites min 1: hits 12, trap 0.050 ms (0.00% of the minute), armed 2, done 1, poly 0, total 0 ms\n';
  // the next job's copy of the same run: start-up part + its own lines (counts are cumulative since the game start)
  const a2 = head + 'vcall 0x00401000 -> 0x00500000 n=32\nvcall skip 0x00403000: bytes differ from the table\ncallsites min 4: hits 22, trap 0.1 ms, total 3 ms\n';
  const b = 'ts3native 0.36.64 2026-10-25 02:00:00\nOK   call sites: 3 ...\nvcall 0x00401000 -> 0x00500000 n=5\nvcall 0x00401000 -> 0x00500010 n=1\n';
  const db = emptyDb();
  assert.equal(mergeLog(db, 'ts3native 0.36.63 2026-10-25 00:00:00\nprobe snap off (Enabled=0, CallSites=0)\n'), null);
  assert.deepEqual(mergeLog(db, a1, 'j1', 't'), { run: '0.36.64 2026-10-25 01:00:00', added: 12, newPairs: 2 });
  assert.deepEqual(mergeLog(db, a1, 'j1', 't'), { run: '0.36.64 2026-10-25 01:00:00', added: 0, newPairs: 0 }); // idempotent
  assert.deepEqual(mergeLog(db, a2, 'j2', 't'), { run: '0.36.64 2026-10-25 01:00:00', added: 22, newPairs: 0 }); // growth only
  assert.deepEqual(mergeLog(db, b, 'j3', 't'), { run: '0.36.64 2026-10-25 02:00:00', added: 6, newPairs: 1 });
  const s = db.sites['0x401000'];
  assert.equal(s.runs, 2); assert.equal(s.hits, 38); assert.equal(s.targets['0x500000'].n, 37);
  assert.equal(db.sites['0x402000'].targets['0x10000000'].out, 'd3d9.dll');
  assert.equal(db.sites['0x403000'].skipped, 1); assert.equal(Object.keys(db.sites['0x403000'].targets).length, 0);
  const r1 = db.runs['0.36.64 2026-10-25 01:00:00'];
  assert.deepEqual([r1.job, r1.armed, r1.trapNs, r1.minutes, r1.trapMs, r1.pairs], ['j1,j2', 3, 4100, 4, 3, 2]);
  assert.match(dbToLog(db), /^vcall 0x401000 -> 0x500000 n=37$/m);
  assert.deepEqual(parseProbe(dbToLog(db)).map((p) => p.n).sort((x, y) => x - y), [1, 2, 37]);
  console.log('callsite-db self-check ok');
}

if (import.meta.filename === resolve(process.argv[1] ?? '')) {
  const { values, positionals } = parseArgs({ allowPositionals: true, options: { 'self-check': { type: 'boolean' }, job: { type: 'string' }, db: { type: 'string' } } });
  if (values['self-check']) selfCheck();
  else if (positionals[0] === 'merge' && positionals[1]) console.log(mergeFile(positionals[1], values.job, values.db ?? DB_DEFAULT));
  else { console.error('usage: node tools/callsite-db.ts merge <ts3native.log> [--job id] [--db file] | --self-check'); process.exitCode = 2; }
}
