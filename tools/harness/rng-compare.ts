// Seeded vs unseeded runs (PLAN.md seeded RNG lane step 4, docs/27): reads the rows of tools/harness/queues/seeded-rng.json and
// the profile-world.ts files each profile job wrote, prints per metric the mean and spread per group and the variance ratio.
//   node tools/harness/rng-compare.ts [probe-out/queue/seeded-rng-<date>.jsonl ...]   (default: the newest seeded-rng-*.jsonl)
//   node tools/harness/rng-compare.ts --self-check
// Metrics per run: sim minutes reached in the window, iterations/s, route plans, route fail %, hitches (stutter-profile
// <tag>-<secs>-seconds.csv), autonomy picks = interaction changes seen in the 30 s sims_activity samples (<tag>-activity.jsonl),
// and pick overlap = mean pairwise Jaccard of the runs' (Sim, interaction) pick multisets within a group.
// var ratio = seeded variance / unseeded variance: < 1 means seeding cuts the run noise for that metric.
// Default.ini check: menu rows with rngDraws (queue menuMemory.draws) grouped by [Config] SimulatorRandomSeed.
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { join, resolve } from 'node:path';

const ROOT = resolve(import.meta.dirname, '..', '..');
const PROFILE = join(ROOT, 'probe-out', 'profile');

export const mean = (x: number[]) => x.reduce((a, b) => a + b, 0) / x.length;
export const variance = (x: number[]) => (x.length < 2 ? NaN : x.reduce((a, b) => a + (b - mean(x)) ** 2, 0) / (x.length - 1));

// per-second CSV of tools/frida/stutter-profile.js; columns by header (new columns go last)
export function csvTotals(text: string): { seconds: number; iters: number; routes: number; routeFails: number; hitches: number } {
  const [head, ...rows] = text.trim().split(/\r?\n/);
  const cols = head.split(',');
  const sum = (c: string) => { const i = cols.indexOf(c); return i < 0 ? 0 : rows.reduce((a, r) => a + (Number(r.split(',')[i]) || 0), 0); };
  return { seconds: rows.length, iters: sum('iters'), routes: sum('routes'), routeFails: sum('routeFails'), hitches: sum('hitches') };
}

// interaction changes between consecutive samples of the same Sim (to a non-empty interaction), as "sim|interaction"
// shortcut: 30 s samples miss interactions that start and end between two checks; a per-pick agent counter would see them all
export function picks(jsonl: string): string[] {
  const last = new Map<string, string | null>(), out: string[] = [];
  for (const line of jsonl.split(/\r?\n/).filter(Boolean)) {
    for (const [sim, inter] of JSON.parse(line).sims as [string, string | null][]) {
      if (last.has(sim) && inter && last.get(sim) !== inter) out.push(`${sim}|${inter}`);
      last.set(sim, inter);
    }
  }
  return out;
}

// Jaccard of two multisets
export function jaccard(a: string[], b: string[]): number {
  const count = (x: string[]) => x.reduce((m, k) => m.set(k, (m.get(k) ?? 0) + 1), new Map<string, number>());
  const ca = count(a), cb = count(b);
  let inter = 0, union = 0;
  for (const k of new Set([...ca.keys(), ...cb.keys()])) { inter += Math.min(ca.get(k) ?? 0, cb.get(k) ?? 0); union += Math.max(ca.get(k) ?? 0, cb.get(k) ?? 0); }
  return union ? inter / union : 1;
}
const pairMean = (lists: string[][]) => {
  const s: number[] = [];
  for (let i = 0; i < lists.length; i++) for (let j = i + 1; j < lists.length; j++) s.push(jaccard(lists[i], lists[j]));
  return s.length ? mean(s) : NaN;
};

export type Run = { job: string; seeded: boolean; simMinutes: number; itPerS: number; routes: number; routeFailPct: number; hitches: number; picks: string[]; firstDraws?: number[] };
const METRICS: [keyof Run, string][] = [['simMinutes', 'sim minutes'], ['itPerS', 'it/s'], ['routes', 'route plans'], ['routeFailPct', 'route fail %'], ['hitches', 'hitches']];

export function report(runs: Run[]): string[] {
  const g = { seeded: runs.filter(r => r.seeded), unseeded: runs.filter(r => !r.seeded) };
  const f = (n: number, d = 2) => (Number.isFinite(n) ? n.toFixed(d) : '-');
  const out = [`runs: ${g.seeded.length} seeded (${g.seeded.map(r => r.job).join(', ') || '-'}), ${g.unseeded.length} unseeded (${g.unseeded.map(r => r.job).join(', ') || '-'})`,
    '| metric | seeded mean | seeded sd (cv %) | unseeded mean | unseeded sd (cv %) | var ratio s/u |', '|---|---|---|---|---|---|'];
  const row = (name: string, s: number[], u: number[]) => {
    const cell = (x: number[]) => `${f(Math.sqrt(variance(x)))} (${f(100 * Math.sqrt(variance(x)) / mean(x), 1)})`;
    out.push(`| ${name} | ${f(mean(s))} | ${cell(s)} | ${f(mean(u))} | ${cell(u)} | ${f(variance(s) / variance(u))} |`);
  };
  for (const [k, name] of METRICS) row(name, g.seeded.map(r => r[k] as number), g.unseeded.map(r => r[k] as number));
  row('autonomy picks', g.seeded.map(r => r.picks.length), g.unseeded.map(r => r.picks.length));
  out.push(`| pick overlap (Jaccard) | ${f(pairMean(g.seeded.map(r => r.picks)))} | | ${f(pairMean(g.unseeded.map(r => r.picks)))} | | higher = more alike |`);
  const draws = g.seeded.map(r => JSON.stringify(r.firstDraws ?? null));
  if (draws.length) out.push(`seed_rng firstDraws: ${new Set(draws).size === 1 && draws[0] !== 'null' ? 'same in every seeded run (ok)' : `DIFFER or missing: ${[...new Set(draws)].join(' / ')}`}`);
  return out;
}

// Default.ini [Config] SimulatorRandomSeed: key read if the seeded menu runs drew the same numbers and the unseeded one differs
export function iniVerdict(rows: { seed: unknown; draws: unknown[] }[]): string {
  const s = rows.filter(r => r.seed != null).map(r => JSON.stringify(r.draws)), u = rows.filter(r => r.seed == null).map(r => JSON.stringify(r.draws));
  if (s.length < 2) return 'Default.ini check: needs 2 seeded menu rows';
  if (rows.some(r => !r.draws?.length || r.draws.some(d => typeof d !== 'number' || d === 0))) return 'Default.ini check: inconclusive (a draw is missing or 0.0 = gRandomGen not cached)';
  if (new Set(s).size !== 1) return 'Default.ini check: seeded menu draws differ: key not read, or pre-menu draws vary (not shown either way)';
  if (u.some(x => x === s[0])) return 'Default.ini check: unseeded draws equal the seeded ones: inconclusive';
  return 'Default.ini check: SimulatorRandomSeed is read (same draws with the seed, different without)';
}

function load(file: string): { runs: Run[]; ini: { seed: unknown; draws: unknown[] }[]; skipped: string[] } {
  const rows = readFileSync(file, 'utf8').split(/\r?\n/).filter(Boolean).map(l => JSON.parse(l));
  const last = new Map<string, any>(); // a resumed or rerun job: its last row counts
  for (const r of rows) last.set(r.job, r);
  const runs: Run[] = [], ini: { seed: unknown; draws: unknown[] }[] = [], skipped: string[] = [];
  for (const r of last.values()) {
    if (r.rngDraws) { ini.push({ seed: r.defaultIni?.['Config.SimulatorRandomSeed'] ?? null, draws: r.rngDraws }); continue; }
    if (!r.run?.profile) continue;
    if (!r.pass) { skipped.push(`${r.job} (not PASS)`); continue; }
    const p = join(PROFILE, r.profileTag), csv = `${p}-${r.run.profile.minutes * 60}-seconds.csv`;
    if (!existsSync(csv) || !existsSync(`${p}-rng.json`)) { skipped.push(`${r.job} (missing ${existsSync(csv) ? 'rng.json' : 'seconds.csv'})`); continue; }
    const t = csvTotals(readFileSync(csv, 'utf8')), rng = JSON.parse(readFileSync(`${p}-rng.json`, 'utf8'));
    runs.push({ job: r.job, seeded: rng.seed !== null, simMinutes: rng.simMinutes.end - rng.simMinutes.start, itPerS: t.iters / t.seconds,
      routes: t.routes, routeFailPct: t.routes ? (100 * t.routeFails) / t.routes : 0, hitches: t.hitches,
      picks: existsSync(`${p}-activity.jsonl`) ? picks(readFileSync(`${p}-activity.jsonl`, 'utf8')) : [], firstDraws: rng.reply?.firstDraws });
  }
  return { runs, ini, skipped };
}

if (process.argv.includes('--self-check')) {
  const assert = (await import('node:assert')).strict;
  assert.equal(mean([1, 2, 3]), 2);
  assert.equal(variance([1, 2, 3]), 1);
  assert.ok(Number.isNaN(variance([4])));
  assert.deepEqual(csvTotals('iters,maxIterMs,hitches,routes,routeFails,t\n10,5,1,4,1,0\n20,7,0,6,0,1\n'), { seconds: 2, iters: 30, routes: 10, routeFails: 1, hitches: 1 });
  const act = [{ sims: [['A', 'Sleep'], ['B', null]] }, { sims: [['A', 'Sleep'], ['B', 'Chat']] }, { sims: [['A', 'Eat'], ['B', null], ['C', 'Read']] }, { sims: [['B', 'Chat']] }]
    .map(x => JSON.stringify(x)).join('\n');
  assert.deepEqual(picks(act), ['B|Chat', 'A|Eat', 'B|Chat']);
  assert.equal(jaccard(['a', 'a', 'b'], ['a', 'b', 'b']), 0.5);
  assert.equal(jaccard([], []), 1);
  const mk = (job: string, seeded: boolean, sm: number, p: string[]): Run => ({ job, seeded, simMinutes: sm, itPerS: 30, routes: 100, routeFailPct: 1, hitches: 5, picks: p, firstDraws: seeded ? [0.1, 0.2, 0.3] : undefined });
  const rep = report([mk('s1', true, 100, ['a', 'b']), mk('s2', true, 102, ['a', 'b']), mk('u1', false, 90, ['a']), mk('u2', false, 110, ['c'])]);
  assert.match(rep.find(l => l.startsWith('| sim minutes'))!, /\| 101\.00 \| 1\.41 \(1\.4\) \| 100\.00 \| 14\.14 \(14\.1\) \| 0\.01 \|/);
  assert.match(rep.find(l => l.startsWith('| pick overlap'))!, /\| 1\.00 \| \| 0\.00 \|/);
  assert.match(rep.at(-1)!, /same in every seeded run/);
  assert.match(report([{ ...mk('s1', true, 1, []), firstDraws: [0.1] }, mk('s2', true, 1, [])]).at(-1)!, /DIFFER/);
  assert.match(iniVerdict([{ seed: 7, draws: [0.1, 0.2] }, { seed: 7, draws: [0.1, 0.2] }, { seed: null, draws: [0.5, 0.6] }]), /is read/);
  assert.match(iniVerdict([{ seed: 7, draws: [0.1, 0.2] }, { seed: 7, draws: [0.3, 0.2] }, { seed: null, draws: [0.5, 0.6] }]), /differ/);
  assert.match(iniVerdict([{ seed: 7, draws: [0, 0] }, { seed: 7, draws: [0, 0] }]), /inconclusive/);
  assert.match(iniVerdict([{ seed: 7, draws: [0.1] }]), /needs 2/);
  console.log('self-check ok');
  process.exit(0);
}

let files = process.argv.slice(2).filter(a => !a.startsWith('--'));
if (!files.length) {
  const dir = join(ROOT, 'probe-out', 'queue');
  const found = existsSync(dir) ? readdirSync(dir).filter(f => /^seeded-rng-.*\.jsonl$/.test(f)).map(f => join(dir, f)).sort((a, b) => statSync(b).mtimeMs - statSync(a).mtimeMs) : [];
  if (!found.length) { console.error('no probe-out/queue/seeded-rng-*.jsonl; run: node tools/harness/queue.ts tools/harness/queues/seeded-rng.json'); process.exit(2); }
  files = [found[0]];
}
for (const file of files) {
  const { runs, ini, skipped } = load(resolve(file));
  console.log(`# ${file}`);
  if (skipped.length) console.log(`skipped: ${skipped.join(', ')}`);
  if (runs.length) console.log(report(runs).join('\n'));
  if (ini.length) console.log(iniVerdict(ini));
}
