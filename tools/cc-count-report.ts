// docs/200 §12: address-space cost per installed CC item at load, by owner (queue tools/harness/queues/cc-count-load.json).
//   node tools/cc-count-report.ts                       newest log of each job in probe-out/queue (cc-count-load-*)
//   node tools/cc-count-report.ts --date 2026-10-27     that queue date only
//   node tools/cc-count-report.ts --none a.log --k1 b.log --k2 c.log --real d.log
//   node tools/cc-count-report.ts --self-check
// Per job, at the last gc-after sample (after load, ~3 min idle and a forced GC): free, largest free, priv, rcN from the
// `diag: mem` line, and the `allocsites:` live rows summed by owner (heap MB / virtual+map MB). Owner = TS3W.exe address range
// of the row's `at` (its `from` when `at` is inside the EA allocator); rows past AllocSitesTop and DLL rows land in `other`
// (= the sum line's totals minus the named owners); `untracked` = usum private + map MB. Then deltas per 1,000 CC items
// (none -> 1k -> 2k) and for the real CC per 1,000 resources.
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import assert from 'node:assert/strict';

const ROOT = resolve(import.meta.dirname, '..');
const JOBS = ['none', 'k1', 'k2', 'real'] as const;
type Job = (typeof JOBS)[number];
const JOB_ID: Record<Job, string> = { none: 'bp-ccn-none', k1: 'bp-ccn-1k', k2: 'bp-ccn-2k', real: 'bp-ccn-real' };

// shortcut: owner by address range of the calling function (docs/190 §2 + §5.3 ranges, docs/201 §13.2 sites); the build drops
// the EA tag strings (docs/190 §8), so catalogue and LocStr ranges are [inferred] around the verified functions
// (Catalog::Init 0xC93000, OBJD loader 0xC922C0; LocStrSvc 0x59BDE0 / 0x59C5B0). Upgrade when a tagged site table exists.
const OWNERS: [string, number, number][] = [
  ['catalogue', 0xC90000, 0xCA0000],
  ['CAS', 0x5A2C00, 0x5F1700],
  ['LocStr', 0x59B000, 0x59D000],
  ['ResourceSystem', 0x4A0000, 0x4B3000], // DBPF packages + indexes
  ['ResourceSystem', 0x731000, 0x739000], // ResourceCache
  ['ResourceLoad', 0x727000, 0x731000],   // AsyncLoader, PackedStream decompression
  ['Mono', 0xD70000, 0xEC0000],           // Mono runtime, Scripting/Simulator sub-heap, Boehm sections
];
const BUCKETS = ['catalogue', 'CAS', 'LocStr', 'ResourceSystem', 'ResourceLoad', 'Mono', 'other', 'untracked'];
const EA_ALLOC = [0x4E3000, 0x4E7000]; // EA heap entries / cores / direct blocks: the user is in `from`

const va = (s: string) => (/^0x[0-9A-Fa-f]{8}$/.test(s) ? parseInt(s, 16) : NaN);
export function ownerOf(at: string, from: string): string {
  let a = va(at);
  if (a >= EA_ALLOC[0] && a < EA_ALLOC[1]) a = va(from);
  return OWNERS.find(([, lo, hi]) => a >= lo && a < hi)?.[0] ?? 'other';
}

type Mb = { heap: number; vm: number };
export type JobSample = { n: number; free: number; big: number; priv: number; rcN: number; owners: Record<string, Mb> };

const num = (line: string, key: string) => {
  const m = new RegExp(`(?:^| )${key}=(-?[\\d.]+)`).exec(line);
  return m ? Number(m[1]) : NaN;
};

export function readSample(text: string): JobSample | null {
  const lines = text.split(/\r?\n/);
  const mem = lines.filter(l => /^diag: mem #\d+ mark ev=gc-after /.test(l)).at(-1);
  if (!mem) return null;
  const n = Number(/#(\d+)/.exec(mem)![1]);
  const pre = `allocsites: #${n} mark ev=gc-after `;
  const as = lines.filter(l => l.startsWith(pre)).map(l => l.slice(pre.length));
  const owners: Record<string, Mb> = Object.fromEntries(BUCKETS.map(b => [b, { heap: 0, vm: 0 }]));
  for (const r of as) {
    const m = /^live r=\d+ k=(\w+) at=(\S+) from=(\S+) MB=([\d.]+)/.exec(r);
    if (!m) continue;
    const o = ownerOf(m[2], m[3]);
    if (o !== 'other') owners[o][m[1] === 'heap' ? 'heap' : 'vm'] += Number(m[4]);
  }
  const sum = as.find(r => r.startsWith('sum '));
  if (sum) {
    const named = BUCKETS.filter(b => b !== 'other' && b !== 'untracked');
    owners.other.heap = num(sum, 'heapMB') - named.reduce((s, b) => s + owners[b].heap, 0);
    owners.other.vm = num(sum, 'virtMB') + num(sum, 'mapMB') - named.reduce((s, b) => s + owners[b].vm, 0);
  }
  const usum = as.find(r => r.startsWith('usum '));
  if (usum) {
    const mb = (k: string) => Number(new RegExp(`${k}=\\d+/([\\d.]+)MB`).exec(usum)?.[1] ?? 0);
    owners.untracked.vm = mb('private') + mb('map');
  }
  return { n, free: num(mem, 'free'), big: num(mem, 'big'), priv: num(mem, 'priv'), rcN: num(mem, 'rcN'), owners };
}

const f1 = (x: number) => (Number.isFinite(x) ? x.toFixed(1) : '-');
const sgn = (x: number) => (Number.isFinite(x) ? (x >= 0 ? '+' : '') + x.toFixed(1) : '-');
const tot = (m: Mb) => m.heap + m.vm;

export function report(s: Partial<Record<Job, JobSample | null>>, items = { k1: 1200, k2: 2200 }, realRes = 209735): string[] {
  const out: string[] = [];
  const have = JOBS.filter(j => s[j]);
  out.push(`job          #   free    big   priv     rcN  ` + BUCKETS.map(b => b.padStart(15)).join(''));
  for (const j of have) {
    const x = s[j]!;
    out.push(`${JOB_ID[j].padEnd(12)} ${String(x.n).padStart(2)} ${f1(x.free).padStart(6)} ${f1(x.big).padStart(6)} ${f1(x.priv).padStart(6)} ${String(x.rcN).padStart(7)}  `
      + BUCKETS.map(b => `${f1(x.owners[b].heap)}/${f1(x.owners[b].vm)}`.padStart(15)).join(''));
  }
  out.push('(owner columns: heap MB / virtual+map MB live at gc-after; other = sum line minus named owners)');
  const delta = (a: Job, b: Job, per: number, label: string) => {
    const x = s[a], y = s[b];
    if (!x || !y) return out.push(`${label}: missing ${!x ? JOB_ID[a] : JOB_ID[b]}`);
    const d = (k: 'free' | 'big' | 'priv') => sgn((y[k] - x[k]) / per);
    out.push(`${label}: free ${d('free')} big ${d('big')} priv ${d('priv')} rcN ${sgn((y.rcN - x.rcN) / per)} | `
      + BUCKETS.map(o => `${o} ${sgn((tot(y.owners[o]) - tot(x.owners[o])) / per)}`).join(' '));
  };
  out.push('', 'Deltas (MB, rcN count) per 1,000 CC items:');
  delta('none', 'k1', items.k1 / 1000, `none -> 1k (${items.k1} items)`);
  delta('k1', 'k2', (items.k2 - items.k1) / 1000, `1k -> 2k (+${items.k2 - items.k1} items)`);
  delta('none', 'k2', items.k2 / 1000, `none -> 2k (${items.k2} items)`);
  out.push('', 'Real CC per 1,000 resources:');
  delta('none', 'real', realRes / 1000, `none -> real (${realRes} resources)`);
  return out;
}

function newestLog(job: Job, date?: string): string | undefined {
  const dir = join(ROOT, 'probe-out', 'queue');
  if (!existsSync(dir)) return undefined;
  const re = new RegExp(`^cc-count-load-${date ?? '\\d{4}-\\d{2}-\\d{2}'}-${JOB_ID[job]}-\\d{4}-ts3native\\.log$`);
  return readdirSync(dir).filter(f => re.test(f)).sort().map(f => join(dir, f)).at(-1);
}

function selfCheck() {
  assert.equal(ownerOf('0x00C922D0', '-'), 'catalogue');
  assert.equal(ownerOf('0x004E4E87', '0x005C7906'), 'CAS');      // EA core: owner from the caller
  assert.equal(ownerOf('0x0072D62B', '0x0'), 'ResourceLoad');
  assert.equal(ownerOf('0x00D70916', '0x00E4EF00'), 'Mono');
  assert.equal(ownerOf('d3d9.dll+0x28FEF2', '-'), 'other');
  const log = (free: number, cas: number, heap: number) => [
    `diag: mem #3 mark ev=gc-before priv=1 free=1 big=1? rcN=1?`,
    `diag: mem #4 mark ev=gc-after priv=${2000 - free} wPriv=1 free=${free} big=${free - 100}? rcN=${150000 + cas * 10}? t=9`,
    `allocsites: #4 mark ev=gc-after sum heapMB=${heap} heapN=1 virtMB=500.0 virtN=1 mapMB=100.0 mapN=1 sites=1`,
    `allocsites: #4 mark ev=gc-after usum private=4/64.0MB map=1/16.0MB image=9/400.0MB new=0 more=0`,
    `allocsites: #4 mark ev=gc-after live r=1 k=heap at=0x005C7906 from=0x005C0000 MB=${cas}.00 n=9 dMB=+0 dN=+0 aMB=1 fMB=0`,
    `allocsites: #4 mark ev=gc-after live r=2 k=virtual at=0x00D70916 from=0x00E4EF00 MB=300.00 n=9 dMB=+0 dN=+0 aMB=1 fMB=0`,
    `allocsites: #4 mark ev=gc-after grow r=1 k=heap at=0x005C7906 from=0x005C0000 MB=99.00 n=9 dMB=+0 dN=+0 aMB=1 fMB=0`,
  ].join('\n');
  const s = { none: readSample(log(1500, 10, 400)), k1: readSample(log(1440, 34, 430)), k2: readSample(log(1400, 54, 450)), real: null };
  assert.equal(s.none!.n, 4);
  assert.equal(s.none!.owners.CAS.heap, 10);                       // grow rows are not counted twice
  assert.equal(s.none!.owners.Mono.vm, 300);
  assert.equal(s.none!.owners.other.heap, 390);
  assert.equal(s.none!.owners.other.vm, 300);                      // 500 + 100 - 300
  assert.equal(s.none!.owners.untracked.vm, 80);
  const r = report(s);
  assert.ok(r.some(l => l.startsWith('none -> 1k (1200 items): free -50.0 big -50.0 priv +50.0 rcN +200.0 | catalogue +0.0 CAS +20.0')), r.join('\n'));
  assert.ok(r.some(l => l.startsWith('1k -> 2k (+1000 items): free -40.0')));
  assert.ok(r.includes('none -> real (209735 resources): missing bp-ccn-real'));
  assert.equal(readSample('nothing'), null);
  console.log('cc-count-report self-check ok');
}

const { values } = parseArgs({ options: {
  'self-check': { type: 'boolean' }, date: { type: 'string' },
  none: { type: 'string' }, k1: { type: 'string' }, k2: { type: 'string' }, real: { type: 'string' },
  'items-1k': { type: 'string', default: '1200' }, 'items-2k': { type: 'string', default: '2200' },
  'real-resources': { type: 'string', default: '209735' }, // docs/08 §1: user CC + Store repacks
} });
if (values['self-check']) selfCheck();
else {
  const s: Partial<Record<Job, JobSample | null>> = {};
  for (const j of JOBS) {
    const path = values[j] ?? newestLog(j, values.date);
    if (!path) { console.log(`${JOB_ID[j]}: no log`); continue; }
    s[j] = readSample(readFileSync(path, 'utf8'));
    console.log(`${JOB_ID[j]}: ${path}${s[j] ? '' : ' (no gc-after sample)'}`);
  }
  console.log(report(s, { k1: Number(values['items-1k']), k2: Number(values['items-2k']) }, Number(values['real-resources'])).join('\n'));
}
