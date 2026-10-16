// Which script tasks own the long ThreadedObjects slices (docs/64). Reads existing captures only; never talks to the game.
//   node tools/harness/stall-split.ts [--long 100] [--norm 50] [--pad 0] [--top 20] [tags...]   managed samples by task, long vs normal
//   node tools/harness/stall-split.ts --slices                            ThreadedObjects time per iteration (hitches) and per call (native counters)
//   node tools/harness/stall-split.ts --stalls                            /taskstall records (probe-out/*-taskstall.jsonl) by task
//   node tools/harness/stall-split.ts --self-check
// Managed samples (`<tag>-managed.jsonl`, tools/probe/managed-sample.mjs) carry no time stamp: one line per sample, ~5 Hz, in order,
// starting 60 s into the run for min(180, secs/2 - 60) s (profile-world.ts). The script places sample j at off + j * window / n seconds
// after the first `<tag>-<secs>-seconds.csv` row and picks `off` (40-90 s) where the per-second share of route-plan samples best
// matches the profiler's route ms/s (Pearson r; runs under 0.2 are left out). A second is "long" when a hitch iteration in it spent
// >= --long ms in ThreadedObjects (`threaded` in `-hitches.jsonl`; older runs without that field use the whole iteration `wallMs` as a
// proxy) and "normal" when its longest one stayed under --norm ms (a second without hitch iterations counts as 0).
// Set TS3_REPO=<main checkout> when running from a git worktree (probe-out/ is not in worktrees).
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = process.env.TS3_REPO ?? join(import.meta.dirname, '..', '..'); // TS3_REPO: read probe-out/ of the main checkout from a worktree
const PROFILE = join(ROOT, 'probe-out', 'profile');
const PLAN = /^ScriptCore\.Route\.Route_(Plan|PlanFromPoint|Replan|ReplanFromPosition)$/;

export const rootOf = (s: string[]) => {
  if (!s.length) return '(no script)';
  const i = s.lastIndexOf('ScriptCore.ScriptProxy.Simulate');
  return i > 0 ? s[i - 1] : s.at(-1)!;
};
// first match wins; mods first so their share is not hidden inside the game task that called them
const CATS: [string, (s: string[], root: string) => boolean][] = [
  ['(no script running)', s => !s.length],
  ['harness agent (Ts3Agent)', s => s.some(m => m.startsWith('Ts3Agent.'))],
  ['NRaas mod code', s => s.some(m => m.startsWith('NRaas.'))],
  ['story progression', s => s.some(m => /StoryProgression/.test(m))],
  ['autonomy search', (s, r) => /AutonomyManager\.Simulate$/.test(r) || s.some(m => /Autonomy\.(RunAutonomy|TryFindBestAction)$/.test(m))],
  ['Sim interaction step', s => s.some(m => /InteractionQueue\.ProcessOneInteraction$|Actors\.Sim\.DoInteraction$/.test(m))],
  ['Sim task, other (idle, LOD, reset)', (_, r) => r === 'Sims3.Gameplay.Actors.Sim.Simulate'],
  ['SimUpdate (motives, buffs)', (_, r) => /SimUpdate\.Simulate$/.test(r)],
  ['Lot.Simulate / reactions', (s, r) => /Core\.Lot\.Simulate$/.test(r) || s.some(m => /ReactionBroadcaster/.test(m))],
  ['alarms / one-shot functions', (_, r) => /AlarmOneShot|OneShotFunction|FunctionTask/.test(r)],
  ['native event callbacks', (_, r) => /OnObjectLotUpdate|OnNativeEventDelegate|OnRoutingEvent|OnPortalLocked|UIEventCallback/.test(r)],
];
export const category = (s: string[]) => { const r = rootOf(s); return CATS.find(([, f]) => f(s, r))?.[0] ?? `other: ${r}`; };
export const isPlan = (s: string[]) => PLAN.test(s[0] ?? '');
const ourHook = (s: string[]) => s.some(m => /Hook\.|^Ts3Fixes\.|StuckWatchdog|RouteBackoff/.test(m));

const pearson = (a: number[], b: number[]) => {
  const n = a.length, ma = a.reduce((x, y) => x + y, 0) / n, mb = b.reduce((x, y) => x + y, 0) / n;
  let c = 0, va = 0, vb = 0;
  for (let i = 0; i < n; i++) { c += (a[i] - ma) * (b[i] - mb); va += (a[i] - ma) ** 2; vb += (b[i] - mb) ** 2; }
  return va && vb ? c / Math.sqrt(va * vb) : 0;
};
const csvRows = (text: string) => {
  const [head, ...lines] = text.trim().split(/\r?\n/);
  const cols = head.split(',');
  return lines.map(l => Object.fromEntries(l.split(',').map((v, i) => [cols[i], Number(v)])) as Record<string, number>);
};
const jsonl = (f: string) => readFileSync(f, 'utf8').split('\n').filter(Boolean).flatMap(l => { try { return [JSON.parse(l)]; } catch { return []; } });

// row k covers (t[k-1], t[k]] in ms on the profiler clock; returns the row index for a time in ms, -1 if outside
export const rowAt = (ts: number[], t: number) => { for (let k = 1; k < ts.length; k++) if (t > ts[k - 1] && t <= ts[k]) return k; return -1; };

type Run = { tag: string; samples: string[][]; rowOf: number[]; times: number[]; hits: { t: number; wall: number; ms: number }[]; peak: Map<number, number>; off: number; r: number; proxy: boolean };
export function alignRun(tag: string, samples: string[][], rows: Record<string, number>[], hitches: any[], secs: number): Run {
  const ts = rows.map(r => r.t), t0 = ts[0], n = samples.length;
  const window = Math.min(180, Math.floor(secs / 2) - 60);
  const proxy = !hitches.some(h => 'threaded' in h);
  const tOf = (j: number, off: number) => t0 + (off + j * window / n) * 1000;
  let best = { off: 60, r: -2 };
  for (let off = 40; off <= 90; off += 0.5) {
    const plan = new Map<number, [number, number]>();
    samples.forEach((s, j) => { const k = rowAt(ts, tOf(j, off)); if (k < 0) return; const p = plan.get(k) ?? [0, 0]; p[0] += +isPlan(s); p[1]++; plan.set(k, p); });
    const ks = [...plan.keys()].filter(k => plan.get(k)![1] >= 2);
    if (ks.length < 10) continue;
    const r = pearson(ks.map(k => plan.get(k)![0] / plan.get(k)![1]), ks.map(k => (rows[k].route || 0) / Math.max(1, ts[k] - ts[k - 1])));
    if (r > best.r) best = { off, r };
  }
  const peak = new Map<number, number>(); // row -> longest hitch iteration in it (ThreadedObjects ms, or wall ms as proxy)
  for (const h of hitches) { const k = rowAt(ts, h.t); if (k >= 0) peak.set(k, Math.max(peak.get(k) ?? 0, proxy ? h.wallMs : h.threaded)); }
  const hits = hitches.map(h => ({ t: h.t, wall: h.wallMs, ms: proxy ? h.wallMs : h.threaded }));
  return { tag, samples, rowOf: samples.map((_, j) => rowAt(ts, tOf(j, best.off))), times: samples.map((_, j) => tOf(j, best.off)), hits, peak, off: best.off, r: best.r, proxy };
}

const pct = (n: number, d: number) => d ? `${(100 * n / d).toFixed(1)}%` : '-';
const interactionOf = (s: string[]) => { const i = s.findIndex(m => /RunInteractionWithoutCleanup$/.test(m)); return i > 0 ? s[i - 1] : '(not in an interaction)'; };
const planCaller = (s: string[]) => s.find(m => !/^ScriptCore\.Route\.|^Sims3\.SimIFace\.Route\.|^RouteBackoffHook\./.test(m)) ?? '?';
function tasks(tags: string[], longMs: number, normMs: number, topN: number, pad: number) {
  const files = readdirSync(PROFILE);
  const all = tags.length ? tags : files.filter(f => /-managed\.jsonl$/.test(f)).map(f => f.replace(/-managed\.jsonl$/, ''));
  const runs: Run[] = [];
  for (const tag of all) {
    const sec = files.find(f => f.startsWith(`${tag}-`) && /^\d+-seconds\.csv$/.test(f.slice(tag.length + 1)));
    if (!sec) continue;
    const secs = Number(/-(\d+)-seconds/.exec(sec)![1]), rows = csvRows(readFileSync(join(PROFILE, sec), 'utf8'));
    const hf = join(PROFILE, sec.replace('-seconds.csv', '-hitches.jsonl'));
    if (rows.length < 30 || !existsSync(hf)) continue;
    runs.push(alignRun(tag, jsonl(join(PROFILE, `${tag}-managed.jsonl`)), rows, jsonl(hf), secs));
  }
  const isLong = (r: Run, k: number) => (r.peak.get(k) ?? 0) >= longMs, isNormal = (r: Run, k: number) => (r.peak.get(k) ?? 0) < normMs;
  // --pad: per sample instead of per second: long = inside a long iteration (+-pad ms), normal = no hitch iteration within 1.5 s
  const longAt = (r: Run, j: number) => pad ? r.hits.some(h => h.ms >= longMs && r.times[j] >= h.t - h.wall - pad && r.times[j] <= h.t + pad) : isLong(r, r.rowOf[j]);
  const normalAt = (r: Run, j: number) => pad ? !r.hits.some(h => r.times[j] >= h.t - h.wall - 1500 && r.times[j] <= h.t + 1500) : isNormal(r, r.rowOf[j]);
  console.log('| Run | Samples | offset s | r (plan share vs route ms) | long s | normal s | long by |\n|---|---|---|---|---|---|---|');
  for (const r of runs) {
    const ks = [...new Set(r.rowOf.filter(k => k > 0))];
    console.log(`| ${r.tag} | ${r.samples.length} | ${r.off} | ${r.r.toFixed(2)} | ${ks.filter(k => isLong(r, k)).length} | ${ks.filter(k => isNormal(r, k)).length} | ${r.proxy ? 'iteration wall (proxy)' : 'ThreadedObjects ms'} |`);
  }
  for (const proxy of [false, true]) {
    const sel = runs.filter(r => r.proxy === proxy && r.r >= 0.2); // r < 0.2: alignment not trusted, run left out
    if (!sel.length) continue;
    const pick = (f: (r: Run, j: number) => boolean) => sel.flatMap(r => r.samples.filter((_, j) => r.rowOf[j] > 0 && f(r, j)));
    const L = pick(longAt), N = pick(normalAt);
    const table = (title: string, key: (s: string[]) => string, only: (s: string[]) => boolean = () => true, extra = true) => {
      const count = (xs: string[][]) => { const c = new Map<string, [number, number, number]>(); for (const s of xs.filter(only)) { const v = c.get(key(s)) ?? [0, 0, 0]; v[0]++; v[1] += +isPlan(s); v[2] += +ourHook(s); c.set(key(s), v); } return c; };
      const lc = count(L), nc = count(N), ln = L.filter(only).length, nn = N.filter(only).length;
      console.log(`\n${title} (samples long ${ln}, normal ${nn})\n| Key | Long s | Normal s | Lift |${extra ? ' Route plan leaf (long) | Our hook on stack (long) |' : ''}\n|---|---|---|---|${extra ? '---|---|' : ''}`);
      for (const k of [...new Set([...lc.keys(), ...nc.keys()])].sort((a, b) => (lc.get(b)?.[0] ?? 0) - (lc.get(a)?.[0] ?? 0)).slice(0, topN)) {
        const l = lc.get(k) ?? [0, 0, 0], m = nc.get(k) ?? [0, 0, 0];
        console.log(`| ${k} | ${pct(l[0], ln)} | ${pct(m[0], nn)} | ${m[0] && ln ? ((l[0] / ln) / (m[0] / nn)).toFixed(2) : '-'} |${extra ? ` ${pct(l[1], l[0])} | ${pct(l[2], l[0])} |` : ''}`);
      }
    };
    console.log(`
### ${proxy ? 'Proxy runs (whole iteration' : 'Stage runs (ThreadedObjects'} >= ${longMs} ms = long, ${pad ? `samples within ${pad} ms of such an iteration vs none within 1.5 s` : `per second; < ${normMs} ms = normal`}): ${sel.map(r => r.tag).join(', ')}`);
    table('By category', category);
    table('Interactions (Sim interaction step only)', interactionOf, s => category(s) === 'Sim interaction step', false);
    table('Route plan callers (plan leaf only)', planCaller, isPlan, false);
  }
}

function slices() {
  const files = readdirSync(PROFILE).filter(f => /-hitches\.jsonl$/.test(f));
  const edges = [5, 20, 50, 100, 200, 500, 1000, Infinity];
  console.log(`| Run | Hitch iterations | ThreadedObjects share of hitch wall | ${edges.slice(0, -1).map((e, i) => `${i ? edges[i - 1] : 0}-${e}`).join(' | ')} | >=1000 | max ms |\n|---|---|---|${edges.map(() => '---|').join('')}---|`);
  const pool: any[] = [];
  for (const f of files) {
    const h = jsonl(join(PROFILE, f)).filter(x => 'threaded' in x);
    if (h.length < 20) continue;
    const b = edges.map(() => 0);
    for (const x of h) { b[edges.findIndex(e => x.threaded < e)]++; pool.push({ ...x, run: f.replace(/-\d+-hitches\.jsonl$/, '') }); }
    const wall = h.reduce((a, x) => a + x.wallMs, 0), thr = h.reduce((a, x) => a + x.threaded, 0);
    console.log(`| ${f.replace(/-\d+-hitches\.jsonl$/, '')} | ${h.length} | ${pct(thr, wall)} | ${b.join(' | ')} | ${Math.max(...h.map(x => x.threaded)).toFixed(0)} |`);
  }
  // route ms is timed inside the same iteration (Route_* ICs), so route / threaded says how much of a long slice is native route search
  for (const lo of [100, 200, 500]) {
    const L = pool.filter(h => h.threaded >= lo), thr = L.reduce((a, h) => a + h.threaded, 0), rt = L.reduce((a, h) => a + Math.min(h.route, h.threaded), 0);
    console.log(`ThreadedObjects >= ${lo} ms: ${L.length} iterations, route ms ${pct(rt, thr)} of their ThreadedObjects ms; route >= 80%: ${L.filter(h => h.route >= 0.8 * h.threaded).length}, route < 20%: ${L.filter(h => h.route < 0.2 * h.threaded).length}`);
  }
  for (const h of pool.sort((a, b) => b.threaded - a.threaded).slice(0, 10))
    console.log(`  ${h.run}: ThreadedObjects ${Math.round(h.threaded)} ms, route ${Math.round(h.route)} ms in ${h.routes} plans ${JSON.stringify(h.byEntry)}`);
  const q = join(ROOT, 'probe-out', 'queue');
  for (const f of existsSync(q) ? readdirSync(q).filter(f => /-native\.json$/.test(f)) : []) {
    const j = JSON.parse(readFileSync(join(q, f), 'utf8')), a = j.after ?? {}, b = j.before ?? {};
    const calls = (a.vfThrD7F8B0Sim ?? 0) - (b.vfThrD7F8B0Sim ?? 0), us = (a.vfThrD7F8B0Us ?? 0) - (b.vfThrD7F8B0Us ?? 0);
    if (calls > 0) console.log(`${f}: ThreadedObjects calls ${calls}, total ${(us / 1e6).toFixed(1)} s, mean ${(us / calls / 1000).toFixed(1)} ms/call, max ${((a.vfThrD7F8B0MaxUs ?? 0) / 1000).toFixed(0)} ms`);
  }
}

function stalls() {
  const dir = join(ROOT, 'probe-out');
  console.log('| File | Stage | ms | Task class | Category (sampled stack) | Plan leaf | Task frame below the leaf |\n|---|---|---|---|---|---|---|');
  for (const f of readdirSync(dir).filter(f => /-taskstall\.jsonl$/.test(f))) for (const r of jsonl(join(dir, f))) {
    const s: string[] = r.samples?.at(-1)?.methods ?? [];
    const own = s.find(m => !/^(ScriptCore|Sims3\.SimIFace)\.|^System\./.test(m)) ?? '';
    console.log(`| ${f.slice(0, 15)} | ${r.stage} | ${Math.round(r.ms)} | ${r.task?.class ?? '(no sample)'} | ${s.length ? category(s) : '-'} | ${s.length ? (isPlan(s) ? 'yes' : 'no') : '-'} | ${own} |`);
  }
}

function selfCheck() {
  const assert = (ok: boolean, m: string) => { if (!ok) throw new Error(`self-check failed: ${m}`); };
  const sim = ['ScriptCore.Route.Route_Plan', 'GoHomeHook.Run', 'Sims3.Gameplay.ActorSystems.InteractionQueue.ProcessOneInteraction', 'Sims3.Gameplay.Actors.Sim.DoInteraction', 'Sims3.Gameplay.Actors.Sim.Simulate', 'ScriptCore.ScriptProxy.Simulate'];
  assert(rootOf(sim) === 'Sims3.Gameplay.Actors.Sim.Simulate' && category(sim) === 'Sim interaction step' && isPlan(sim) && ourHook(sim), 'sim interaction');
  assert(category([]) === '(no script running)' && rootOf(['A.B', 'FunctionTask.Simulate']) === 'FunctionTask.Simulate', 'empty / no proxy');
  assert(category(['NRaas.X.Y', 'FunctionTask.Simulate']) === 'NRaas mod code' && category(['X', 'Sims3.Gameplay.Autonomy.AutonomyManager.Simulate', 'ScriptCore.ScriptProxy.Simulate']) === 'autonomy search', 'mods first, autonomy');
  assert(rowAt([1000, 2000, 3000], 1500) === 1 && rowAt([1000, 2000, 3000], 3000) === 2 && rowAt([1000, 2000], 500) === -1, 'rowAt');
  // synthetic run: route ms in rows 70-79; plan samples at 60 + j/5 in (64, 74], so they line up at off 65
  const rows = Array.from({ length: 300 }, (_, k) => ({ t: 1000 * k, route: k >= 70 && k < 80 ? 500 : 0 }));
  const samples = Array.from({ length: 450 }, (_, j) => { const s = 60 + j / 5; return s > 64 && s <= 74 ? ['ScriptCore.Route.Route_Plan'] : ['X']; });
  const run = alignRun('t', samples, rows, [{ t: 75500, threaded: 300, wallMs: 320 }, { t: 75900, threaded: 40, wallMs: 60 }, { t: 90500, threaded: 20, wallMs: 60 }], 300);
  assert(run.off === 65 && run.peak.get(76) === 300 && run.peak.get(91) === 20 && !run.proxy, `alignRun ${run.off} ${[...run.peak]}`);
  console.log('self-check ok');
}

const isMain = resolve(process.argv[1] ?? '') === fileURLToPath(import.meta.url);
const args = isMain ? process.argv.slice(2) : [];
const opt = (k: string, d: number) => { const i = args.indexOf(k); return i < 0 ? d : Number(args.splice(i, 2)[1]); };
const flag = (k: string) => { const i = args.indexOf(k); if (i >= 0) args.splice(i, 1); return i >= 0; };
if (!isMain) { /* imported */ }
else if (flag('--self-check')) selfCheck();
else if (flag('--slices')) slices();
else if (flag('--stalls')) stalls();
else { const longMs = opt('--long', 100), normMs = opt('--norm', 50), topN = opt('--top', 20), pad = opt('--pad', 0); tasks(args, longMs, normMs, topN, pad); }
