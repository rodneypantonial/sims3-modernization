// Quiet-window length for the autonomy stage-C worker (docs/59 §3.5, docs/70): native-only sim-thread time per iteration,
// after the script work. Reads only stutter-profile.js output (*-seconds.csv with animSacs/locomotion/publish columns, and the
// matching *-hitches.jsonl, which holds per-iteration rows for iterations >= hitchMs).
//   node tools/harness/quiet-window.ts [--fit 1.5] [files...]   default: every probe-out/profile/*-seconds.csv with those columns
//   node tools/harness/quiet-window.ts --self-check
// Per second: script side S = script + taskObjects + oneShot + threaded + gcMark + fin (route nests inside these);
// tail N = animSacs + animPose + locomotion + publish (measured); untimed U = wall - S - N (frame waits, catch-up, other).
// Quiet window per iteration: low = N / iters (measured tail only), high = (wall - S) / iters (tail + untimed).
// shortcut: per-second means, not per-iteration windows (the probe only logs hitch iterations); upgrade = per-iteration ring buffer.
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { basename, join } from 'node:path';

const SCRIPT = ['script', 'taskObjects', 'oneShot', 'threaded', 'gcMark', 'fin'];
const TAIL = ['animSacs', 'animPose', 'locomotion', 'publish'];

type Sec = { iters: number; wall: number; S: number; N: number };

export function parseSeconds(text: string): Sec[] {
  const lines = text.split(/\r?\n/).filter(Boolean);
  const h = lines[0].split(','), ix = (k: string) => h.indexOf(k);
  const out: Sec[] = [];
  let prevT = NaN;
  for (const l of lines.slice(1)) {
    const v = l.split(',').map(Number), t = v[ix('t')];
    const wall = Number.isFinite(prevT) && t - prevT > 0 && t - prevT < 5000 ? t - prevT : 1000;
    prevT = t;
    const iters = v[ix('iters')];
    if (!(iters > 0)) continue; // paused / loading second
    const sum = (ks: string[]) => ks.reduce((a, k) => a + (v[ix(k)] || 0), 0);
    out.push({ iters, wall, S: sum(SCRIPT), N: sum(TAIL) });
  }
  return out;
}

const pct = (xs: number[], p: number) => { const s = [...xs].sort((a, b) => a - b); return s.length ? s[Math.min(s.length - 1, Math.floor(p * s.length))] : NaN; };
const f = (x: number, d = 2) => Number.isFinite(x) ? x.toFixed(d) : '-';

function report(files: string[], fit: number) {
  console.log(`| Run | s | it/s | iter ms | S ms/it | tail ms/it p50/p90/p99/max | tail+untimed ms/it p50/p90/p99/max | tail % | tail+untimed % | s with tail>=${fit} | s with tail+untimed>=${fit} |`);
  console.log('|---|---|---|---|---|---|---|---|---|---|---|');
  for (const file of files) {
    const rows = parseSeconds(readFileSync(file, 'utf8'));
    if (!rows.length) continue;
    const lo = rows.map(r => r.N / r.iters), hi = rows.map(r => Math.max(0, r.wall - r.S) / r.iters);
    const W = rows.reduce((a, r) => a + r.wall, 0), I = rows.reduce((a, r) => a + r.iters, 0);
    const sN = rows.reduce((a, r) => a + r.N, 0), sS = rows.reduce((a, r) => a + r.S, 0);
    const d4 = (xs: number[]) => [0.5, 0.9, 0.99, 1].map(p => f(pct(xs, p))).join(' / ');
    const share = (xs: number[]) => f(100 * xs.filter(x => x >= fit).length / xs.length, 0) + '%';
    console.log(`| ${basename(file).replace(/-\d+-seconds\.csv$/, '')} | ${rows.length} | ${f(I * 1000 / W, 1)} | ${f(W / I)} | ${f(sS / I)} | ${d4(lo)} | ${d4(hi)} | ${f(100 * sN / W, 1)} | ${f(100 * (W - sS) / W, 1)} | ${share(lo)} | ${share(hi)} |`);
  }
  // Hitch iterations: real per-iteration rows (only iterations >= hitchMs).
  console.log('\n| Run | hitch its | wall ms p50 | tail ms p50/p90/max | wall-S ms p50/p90/max |\n|---|---|---|---|---|');
  for (const file of files) {
    const hj = file.replace(/-seconds\.csv$/, '-hitches.jsonl');
    if (!existsSync(hj)) continue;
    const its = readFileSync(hj, 'utf8').split(/\r?\n/).filter(Boolean).map(l => JSON.parse(l));
    if (!its.length) continue;
    const tail = its.map(r => TAIL.reduce((a, k) => a + (r[k] || 0), 0));
    const free = its.map(r => r.wallMs - SCRIPT.reduce((a, k) => a + (r[k] || 0), 0));
    const d3 = (xs: number[]) => [0.5, 0.9, 1].map(p => f(pct(xs, p))).join(' / ');
    console.log(`| ${basename(hj).replace(/-\d+-hitches\.jsonl$/, '')} | ${its.length} | ${f(pct(its.map(r => r.wallMs), 0.5))} | ${d3(tail)} | ${d3(free)} |`);
  }
}

function selfCheck() {
  const r = parseSeconds('iters,script,threaded,animSacs,publish,t\n30,10,600,60,30,1000\n0,0,0,0,0,2000\n30,10,600,90,0,3000\n');
  if (r.length !== 2 || r[0].S !== 610 || r[0].N !== 90 || r[1].wall !== 1000) throw new Error('parseSeconds ' + JSON.stringify(r));
  if (pct([1, 2, 3, 4], 0.5) !== 3 || pct([1, 2, 3, 4], 1) !== 4) throw new Error('pct');
  console.log('self-check ok');
}

const args = process.argv.slice(2);
if (args.includes('--self-check')) selfCheck();
else {
  const fi = args.indexOf('--fit'), fit = fi >= 0 ? Number(args.splice(fi, 2)[1]) : 1.5;
  const dir = 'probe-out/profile';
  const files = args.length ? args : readdirSync(dir).filter(n => n.endsWith('-seconds.csv')).map(n => join(dir, n))
    .filter(p => readFileSync(p, 'utf8').split('\n', 1)[0].includes('animSacs')).sort();
  report(files, fit);
}
