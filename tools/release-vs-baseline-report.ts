// README table for tools/harness/queues/release-vs-baseline.json: release v0.1.4 vs no fixes of ours, per world.
//   node tools/release-vs-baseline-report.ts [--date YYYY-MM-DD] [--queue release-vs-baseline] [--probe-out <dir>]
//   node tools/release-vs-baseline-report.ts --set "BP|<base tag>,<base tag>|<rel tag>,<rel tag>" [--set ...]   any profile tags
//   node tools/release-vs-baseline-report.ts --self-check
// Reads probe-out/profile/<tag>-<secs>-seconds.csv + -hitches.jsonl (profile-world.ts, hitchMs 50) and the queue's machine
// load file probe-out/queue/<tag>-*-machine.jsonl. The two runs per side are pooled (equal 10 min runs: the same as averaging
// the per-second rates; p99 by nearest rank over all iterations, as milestone.ts). Prints markdown to stdout. Not imported by
// the harness.
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { summarize } from './harness/machine-load.ts';

const { values } = parseArgs({ options: { date: { type: 'string' }, queue: { type: 'string', default: 'release-vs-baseline' },
  'probe-out': { type: 'string' }, set: { type: 'string', multiple: true }, 'self-check': { type: 'boolean' } } });
const PROBE = values['probe-out'] ? resolve(values['probe-out']) : join(import.meta.dirname, '..', 'probe-out');
const HITCH_MS = 50;

// same rule as tools/harness/milestone.ts iterPct (that file runs the game on import, so not imported)
function iterPct(totalIters: number, hitchMs: number[], p: number): number | string {
  if (!totalIters) return '-';
  const fromTop = totalIters - Math.ceil(p * totalIters) + 1;
  const desc = [...hitchMs].sort((a, b) => b - a);
  return fromTop <= desc.length ? desc[fromTop - 1] : `<${HITCH_MS}`;
}
type Sec = Record<string, number>;
function stats(secs: Sec[], hitchMs: number[]) {
  const n = secs.length, iters = secs.reduce((a, r) => a + (r.iters || 0), 0);
  return { seconds: n, itersPerSec: iters / n, hitchesPerMin: hitchMs.length / (n / 60), p99: iterPct(iters, hitchMs, 0.99),
    routeMsPerSec: secs.reduce((a, r) => a + (r.route || 0), 0) / n, worst: Math.max(0, ...secs.map(r => r.maxIterMs || 0), ...hitchMs) };
}

const lines = (f: string) => existsSync(f) ? readFileSync(f, 'utf8').split('\n').filter(Boolean) : [];
function readTag(tag: string) {
  const dir = join(PROBE, 'profile');
  const csv = existsSync(dir) ? readdirSync(dir).find(f => f.startsWith(`${tag}-`) && /^\d+-seconds\.csv$/.test(f.slice(tag.length + 1))) : undefined;
  if (!csv) return null;
  const [head, ...rows] = lines(join(dir, csv)), cols = head.split(',');
  const secs = rows.map(l => Object.fromEntries(l.split(',').map((v, i) => [cols[i], Number(v)])));
  const hitchMs = lines(join(dir, csv.replace('-seconds.csv', '-hitches.jsonl'))).map(l => JSON.parse(l).wallMs as number);
  const qd = join(PROBE, 'queue'), mf = existsSync(qd) ? readdirSync(qd).filter(f => f.startsWith(`${tag}-`) && /^\d+-machine\.jsonl$/.test(f.slice(tag.length + 1))).sort().pop() : undefined;
  return { tag, secs, hitchMs, machine: mf ? summarize(join(qd, mf)) : null };
}

// queue mode: newest date that has the queue's profile files, unless --date
function queueSets(): [string, string[], string[]][] {
  let date = values.date;
  if (!date) {
    const re = new RegExp(`^${values.queue}-(\\d{4}-\\d\\d-\\d\\d)-`), dir = join(PROBE, 'profile');
    date = (existsSync(dir) ? readdirSync(dir) : []).map(f => f.match(re)?.[1]).filter(Boolean).sort().pop();
    if (!date) throw new Error(`no ${values.queue} profile files in ${dir}; pass --date or --set`);
  }
  const t = (w: string, side: string, n: number) => `${values.queue}-${date}-${w}-${side}-${n}`;
  return [['Bridgeport', 'bp'], ['Sunset Valley', 'sv'], ['Isla Paradiso', 'ip']].map(([name, w]) =>
    [name, [t(w, 'base', 1), t(w, 'base', 2)], [t(w, 'rel', 1), t(w, 'rel', 2)]] as [string, string[], string[]]);
}

const fmt = (v: unknown, d: number) => typeof v === 'number' && Number.isFinite(v) ? v.toFixed(d) : v == null ? '-' : String(v);
const change = (a: unknown, b: unknown) => typeof a === 'number' && typeof b === 'number' && b ? `${a >= b ? '+' : ''}${(100 * (a - b) / b).toFixed(0)}%` : '-';
const METRICS: [string, (s: ReturnType<typeof stats>) => number | string, number][] = [
  ['sim iterations/s (higher is better)', s => s.itersPerSec, 1], ['hitches/min, >= 50 ms (lower is better)', s => s.hitchesPerMin, 1],
  ['p99 iteration ms', s => s.p99, 0], ['sim-thread route ms/s', s => s.routeMsPerSec, 1], ['worst iteration ms', s => s.worst, 0]];

function report(sets: [string, string[], string[]][]) {
  const out = ['| World | Metric | No fixes (baseline) | v0.1.4 | Change |', '|---|---|---|---|---|'], runs: string[] = [];
  for (const [world, baseTags, relTags] of sets) {
    const side = (tags: string[]) => {
      const got = tags.map(t => { const r = readTag(t); runs.push(`| ${t} | ${r ? r.secs.length : 'missing'} | ${r?.machine ? `${r.machine.ok ? 'ok' : 'NOT comparable'}, ${r.machine.short}` : '-'} |`); return r; }).filter(r => r !== null);
      return got.length ? stats(got.flatMap(g => g.secs), got.flatMap(g => g.hitchMs)) : null;
    };
    const b = side(baseTags), r = side(relTags);
    for (const [name, get, d] of METRICS) {
      const vb = b && get(b), vr = r && get(r);
      out.push(`| ${world} | ${name} | ${fmt(vb, d)} | ${fmt(vr, d)} | ${change(vr, vb)} |`);
    }
  }
  console.log(out.join('\n'));
  console.log(`\nRuns (seconds profiled; machine load verdict, tools/harness/machine-load.ts):\n\n| tag | seconds | machine load |\n|---|---|---|\n${runs.join('\n')}`);
}

if (values['self-check']) {
  const assert = (await import('node:assert')).strict;
  assert.equal(iterPct(1000, [60, 300, 80, 55, 70, 90, 51, 52, 53, 54, 400], 0.99), 51);
  assert.equal(iterPct(1000, [300], 0.99), '<50');
  const s = stats([{ iters: 30, maxIterMs: 120, route: 10 }, { iters: 30, maxIterMs: 40, route: 30 }], [120, 260]);
  assert.equal(s.itersPerSec, 30); assert.equal(s.hitchesPerMin, 60); assert.equal(s.routeMsPerSec, 20); assert.equal(s.worst, 260);
  assert.equal(change(110, 100), '+10%'); assert.equal(change('<50', 60), '-');
  console.log('self-check ok');
  process.exit(0);
}
report(values.set?.length ? values.set.map(s => { const [w, b, r] = s.split('|'); return [w, b.split(','), r.split(',')] as [string, string[], string[]]; }) : queueSets());
