// P5 lending gain model (docs/158): from a run's native counters (+ the probe P0 wait keys) recompute what P5b / P5c lending
// can cut from the sim's +0x918 wait, per A/B side. Read-only: reads probe-out/profile/<tag>-ab.json + <tag>-ab-native.jsonl,
// or a queue <job>-native.json ({before, after}) with --native.
//   node tools/p5-gain-model.ts [--dir <profile dir>] [--dt lo,hi] <tag> [...]
//   node tools/p5-gain-model.ts --native <file.json> [--secs 600] [--dt lo,hi]
//   node tools/p5-gain-model.ts --self-check
// Model (docs/158 §3): dW = s * (N * dT - q * L)   [ms/s; negative = less sim wait]
//   s   sim wait per ms of job hold (docs/150: 0.67, measured correlation over 293 BP slices; causal reading inferred)
//   N   eligible jobs/s (P5 takes async ReplanFromPosition only: arStartedReplanFromPosition)
//   dT  change of an eligible job's whole hold (asyncUs counts lent time too): + copy, clone run and door-ask retakes,
//       + the game's plan after a refusal, - the game's plan a taken answer skips; measured only by a P5-on vs P5-off A/B
//       (snap-p5c-ab); --dt lo,hi sets the range used otherwise
//   L   lent ms/s (p5bWinUs)
//   q   share of the lent time the sim turns into progress. Upper bound a / (n * (1 - a)), capped at a: a = admitted share of
//       the keyed wait, n = windows per P5 plan (each window the sim gets one more admitted touch with probability a before a
//       non-admitted touch parks it to the job's end: docs/152 conservation). Lower bound 0
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

let DIR = join(resolve(import.meta.dirname, '..'), 'probe-out', 'profile');  // --dir <path> for another checkout's files
const S = 0.67;          // docs/150 §Summary
const FB_SHARE = 0.69;   // gate 12 F-B share of 0x62F782 (docs/94 §22.10.1, bp-p1b)
type C = Record<string, number>;
type Side = { secs: number; c: C; keys: Map<number, number> };

// key classes by what P5b (ReleaseLock + ReleaseGates) and P5c (FreeLock) admit: docs/94 §22.9-22.11, docs/152 §2
const STEER = [0x85FE10, 0x85FE81, 0x85F6CB, 0x846D21, 0x85B50C, 0x85FFC7, 0x631814, 0x631F40];  // + gate 12 F-C, F-G reads
const PLAIN = [0x833593, 0x8334CC, 0x82D296, 0x82A0F6, 0x83156D, 0x2000003];  // P5c: plain acquires; destroys parked
const NEVER = [0x760BFF, 0x62FB74];                                            // gate waits P5c keeps out
export function classify(keys: Map<number, number>, gate0Hit = 1) {
  let tot = 0, b = 0, c = 0, unknown = 0;
  for (const [k, us] of keys) {
    tot += us;
    if (STEER.includes(k)) b += us;
    else if (k === 0x62EF7A) b += us * gate0Hit;
    else if (k === 0x62F782) b += us * FB_SHARE;
    else if (PLAIN.includes(k)) c += us;
    else if (!NEVER.includes(k)) unknown += us;  // unmapped keys (docs/152 G7): counted half admitted by P5c
  }
  return { tot, aB: tot ? b / tot : 0, aC: tot ? (b + c + unknown / 2) / tot : 0 };
}
export const qUpper = (a: number, n: number) => (n > 0 && a < 1 ? Math.min(a, a / (n * (1 - a))) : a);
export const arm = (N: number, dt: [number, number], q: [number, number], L: number): [number, number] =>
  [S * (N * dt[0] - q[1] * L), S * (N * dt[1] - q[0] * L)];

function lastRun<T extends { i: number }>(snaps: T[]) {  // as wait-ab.ts: a reused tag appended an earlier run
  const from = snaps.map(x => x.i).lastIndexOf(0);
  return from > 0 ? snaps.slice(from) : snaps;
}
export function sides(slices: { on: boolean }[], snaps: { i: number; s: number; c: C }[]) {
  const out: Record<'on' | 'off', Side> = { on: { secs: 0, c: {}, keys: new Map() }, off: { secs: 0, c: {}, keys: new Map() } };
  for (let i = 1; i < slices.length; i++) {  // slice 0 = warm-up
    const a = snaps.find(x => x.i === i), b = snaps.find(x => x.i === i + 1);
    if (!a || !b) continue;
    const sd = out[slices[i].on ? 'on' : 'off'];
    sd.secs += b.s - a.s;
    for (const k in b.c) if (typeof b.c[k] === 'number') sd.c[k] = (sd.c[k] ?? 0) + b.c[k] - (a.c[k] ?? 0);
    for (let q = 0; q < 32; q++) {
      const key = b.c[`blockKey_${q}`];
      if (key) sd.keys.set(key, (sd.keys.get(key) ?? 0) + (b.c[`blockUs_${q}`] ?? 0) - (a.c[`blockUs_${q}`] ?? 0));
    }
  }
  return out;
}

export function inputs(sd: Side) {
  const c = sd.c, t = sd.secs || 1, g = (k: string) => c[k] ?? 0;
  const N = g('arStartedReplanFromPosition') / t, plans = g('m2Plans');
  const hits = g('p5bGateAcq_1'), miss = g('p5bGateMiss');
  const cl = classify(sd.keys, hits + miss ? hits / (hits + miss) : 1);
  const L = g('p5bWinUs') / 1000 / t, E = g('arJobUsReplanFromPosition') / 1000 / t, n = plans ? g('p5bWins') / plans : 0;
  return {
    secs: sd.secs, hold: g('asyncUs') / 1000 / t, N, E, perJob: N ? E / N : 0, L, lambda: E ? L / E : 0, n,
    take: plans ? g('m2Taken') / plans : 0, stale: plans ? (g('p5cStale_0') + g('p5cStale_1')) / plans : 0,
    staleEarly: plans ? g('p5cStale_0') / plans : 0, wait: g('haUs_0') / 1000 / t, keyed: cl.tot / 1000 / t,
    aB: cl.aB, aC: cl.aC, qB: qUpper(cl.aB, n || 20), qC: qUpper(cl.aC, n || 20),
    door: g('m2DoorWaitUs') / 1000 / t, heldOutPerWin: g('p5bWins') ? g('p5bHeldOut') / g('p5bWins') : 0,
  };
}
type In = ReturnType<typeof inputs>;

const f = (x: number, d = 1) => (Number.isFinite(x) ? x.toFixed(d) : '-');
const r = (x: [number, number]) => `${f(x[0])}..${f(x[1])}`;
function print(label: string, x: In, dt: [number, number]) {
  console.log(`  ${label}: ${f(x.secs, 0)} s, hold ${f(x.hold)} ms/s, sim +0x918 wait haUs[0] ${f(x.wait)}, keyed ${f(x.keyed)}`);
  console.log(`    eligible (ReplanFromPosition) ${f(x.N, 2)} jobs/s, ${f(x.E)} ms/s = ${f(x.hold ? 100 * x.E / x.hold : 0, 0)}% of hold, ${f(x.perJob)} ms/job`);
  console.log(`    lent L ${f(x.L)} ms/s = ${f(100 * x.lambda, 0)}% of eligible hold, ${f(x.n)} windows/plan, held out per window ${f(x.heldOutPerWin, 3)}; take ${f(100 * x.take, 0)}%, stale ${f(100 * x.stale, 0)}% (early ${f(100 * x.staleEarly, 0)}%), door wait ${f(x.door)} ms/s`);
  console.log(`    admitted share of keyed wait: P5b ${f(100 * x.aB, 0)}%, P5c ${f(100 * x.aC, 0)}%; q upper P5b ${f(x.qB, 3)}, P5c ${f(x.qC, 3)}`);
  if (!x.N) { console.log('    no eligible jobs on this side: every P5 arm cuts 0 (A10 ReplanFromPosition off)'); return; }
  if (!x.L) return;
  console.log(`    model dW ms/s: lending only P5b ${r(arm(x.N, [0, 0], [0, x.qB], x.L))}, P5c ${r(arm(x.N, [0, 0], [0, x.qC], x.L))}; with dT ${dt[0]}..${dt[1]} ms/job P5b ${r(arm(x.N, dt, [0, x.qB], x.L))}, P5c ${r(arm(x.N, dt, [0, x.qC], x.L))}`);
  const L6 = x.lambda * x.hold;  // every job kind lent at this side's lambda (P6 + foreign + Replan family)
  console.log(`    all kinds lent at this lambda: L ${f(L6)} ms/s, lending-only dW upper bound P5b ${f(-S * x.qB * L6)}, P5c ${f(-S * x.qC * L6)}`);
}

function report(tag: string, dt: [number, number]) {
  const { slices } = JSON.parse(readFileSync(join(DIR, `${tag}-ab.json`), 'utf8'));
  const snaps = lastRun(readFileSync(join(DIR, `${tag}-ab-native.jsonl`), 'utf8').trim().split('\n').map(l => JSON.parse(l)));
  const s = sides(slices, snaps), on = inputs(s.on), off = inputs(s.off);
  console.log(tag);
  print('off', off, dt);
  print('on ', on, dt);
  if ((on.L > 0) !== (off.L > 0) && on.N && off.N) {  // a P5 A/B (snap-p5c-ab): measured dT and implied q
    const [P, Z] = on.L > 0 ? [on, off] : [off, on];
    const dT = P.perJob - Z.perJob, dW = P.wait - Z.wait, dH = P.hold - Z.hold;
    // two readings: the eligible jobs' hold change only, or the whole hold change (other kinds' rate differs between sides)
    const qJob = -(dW - S * P.N * dT) / (S * P.L), qAll = -(dW - S * dH) / (S * P.L);
    console.log(`  P5 A/B (lent side minus plain side): dW ${f(dW)} ms/s, keyed ${f(P.keyed - Z.keyed)}; dT ${f(dT)} ms per eligible job, whole hold ${f(dH)} ms/s`);
    console.log(`    implied q ${f(qJob, 2)} (eligible jobs) / ${f(qAll, 2)} (whole hold); one slice pair moves dW by ~5-8 ms/s (docs/136 §2): q is not resolved by one tag`);
  }
}

function native(path: string, secs: number, dt: [number, number]) {
  const j = JSON.parse(readFileSync(path, 'utf8')), a = j.after.c ?? j.after, b = j.before.c ?? j.before;
  const c: C = {};
  for (const k in a) if (typeof a[k] === 'number') c[k] = a[k] - (b[k] ?? 0);
  const keys = new Map<number, number>();
  for (let q = 0; q < 32; q++) if (a[`blockKey_${q}`]) keys.set(a[`blockKey_${q}`], c[`blockUs_${q}`] ?? 0);
  console.log(`${path} (whole run, ${secs} s assumed)`);
  print('run', inputs({ secs, c, keys }), dt);
}

function selfCheck() {
  assert.equal(qUpper(0.5, 20), 0.05);
  assert.equal(qUpper(0.95, 1), 0.95);  // capped at a
  const cl = classify(new Map([[0x85FE10, 50], [0x833593, 30], [0x760BFF, 10], [0x62F782, 10]]));
  assert.equal(cl.tot, 100); assert.equal(Math.round(cl.aB * 1000), 569); assert.equal(Math.round(cl.aC * 1000), 869);
  const [best, worst] = arm(2, [-1, 1], [0, 0.5], 10);
  assert.equal(best.toFixed(3), (S * -7).toFixed(3)); assert.equal(worst.toFixed(3), (S * 2).toFixed(3));
  const c = (t: number, w: number) => ({ asyncUs: t, p5bWinUs: w, blockKey_0: 0x85FE10, blockUs_0: t / 2 });
  const s = sides([{ on: true }, { on: false }, { on: true }], [{ i: 1, s: 10, c: c(0, 0) }, { i: 2, s: 20, c: c(1000, 0) }, { i: 3, s: 30, c: c(3000, 500) }]);
  assert.equal(s.off.secs, 10); assert.equal(s.on.c.p5bWinUs, 500); assert.equal(s.on.keys.get(0x85FE10), 1000);
  const x = inputs({ secs: 10, c: { arStartedReplanFromPosition: 20, arJobUsReplanFromPosition: 400000, p5bWinUs: 100000, m2Plans: 10, p5bWins: 200, m2Taken: 6 }, keys: new Map() });
  assert.equal(x.N, 2); assert.equal(x.perJob, 20); assert.equal(x.lambda, 0.25); assert.equal(x.n, 20); assert.equal(x.take, 0.6);
  console.log('p5-gain-model self-check ok');
}

const argv = process.argv.slice(2);
const opt = (name: string) => { const i = argv.indexOf(name); return i >= 0 ? argv.splice(i, 2)[1] : undefined; };
const dt = (opt('--dt')?.split(',').map(Number) ?? [-1, 9.5]) as [number, number];  // docs/158 §2.4
const dir = opt('--dir'), secs = opt('--secs');
if (dir) DIR = dir;
if (argv[0] === '--self-check') selfCheck();
else if (argv[0] === '--native') native(argv[1], secs ? +secs : 600, dt);
else if (!argv.length) console.log('usage: node tools/p5-gain-model.ts [--dir D] [--dt lo,hi] <tag> [...] | --native <file.json> [--secs N] | --self-check');
else for (const t of argv) {
  if (!existsSync(join(DIR, `${t}-ab.json`))) { console.log(`${t}: no ${t}-ab.json in ${DIR}`); continue; }
  report(t, dt);
}
