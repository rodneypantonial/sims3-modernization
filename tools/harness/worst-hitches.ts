// Worst hitches (>= 300 ms wall per Simulate iteration) attributed from existing captures (docs/96). Reads only; never talks to the game.
//   node tools/harness/worst-hitches.ts [--min 300] [--since 2026-10-16] [--top 25] [--json out.json]
//   node tools/harness/worst-hitches.ts --self-check
// Inputs per run tag in probe-out/profile: <tag>-<secs>-hitches.jsonl and -seconds.csv (stutter-profile.js, QPC ms clock),
// -simstack.jsonl (native sim-thread stacks, epoch ms), -managed.jsonl (C# stacks, no time stamps), -activity.jsonl (ms since the
// profile-world start), -rng.json; probe-out/queue/<tag>.log (UTC stamps of the profile-world start, repro steps, samplers).
// Clock alignment (QPC ms -> epoch ms): epoch - QPC is one constant per Windows boot. Coarse: profile-world start stamp + 3.8 s (the
// profiler's first seconds row lands 3.2-4.6 s after it in runs with a fine fit) - first row QPC. Runs whose coarse offsets agree
// within 120 s share a boot. Fine: for runs with simstack samples, the offset (coarse +-15 s, 10 ms steps) where samples inside
// route-heavy hitch iterations show a route-search frame and samples inside route-free ones do not; fits with score >= 15 (Isla
// Paradiso runs, many long plans) give the boot offset for every run of that boot (they agree within 2-40 ms across a day).
// Managed samples are placed with stall-split.ts alignRun (uniform 5 Hz from ~57 s after the first row; per-run fit, inferred).
// Set TS3_REPO=<main checkout> when running from a git worktree.
import { existsSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadFunctions, lookup, split } from './sample-split.ts';
import { alignRun, category, rootOf } from './stall-split.ts';

const ROOT = process.env.TS3_REPO ?? join(import.meta.dirname, '..', '..');
const PROFILE = join(ROOT, 'probe-out', 'profile'), QUEUE = join(ROOT, 'probe-out', 'queue');
const LAG_MS = 3800, BOOT_GAP_MS = 120_000, FINE_MIN = 15;
// route search: Route_PlanLocked 0x82C6E0, planner 0x65E0D0, world/lot/cell A*, water JPS (docs/28 N3)
const ROUTE_FN = /FUN_00(82c6e0|65e0d0|676260|63e340|61e500|620560|61fe10)\+/i;

type H = Record<string, any> & { t: number; wallMs: number };
type Ev = { at: number; text: string };
export type Cls = 'harness step' | 'first minute after load' | 'route search' | 'TaskObjects burst' | 'locomotion' | 'ThreadedObjects, no route' | 'mixed route + script' | 'other';

const jsonl = (f: string) => existsSync(f) ? readFileSync(f, 'utf8').split('\n').filter(Boolean).flatMap(l => { try { return [JSON.parse(l)]; } catch { return []; } }) : [];
const csvRows = (text: string) => { const [head, ...ls] = text.trim().split(/\r?\n/); const c = head.split(','); return ls.map(l => Object.fromEntries(l.split(',').map((v, i) => [c[i], Number(v)]))); };
const med = (xs: number[]) => { const s = [...xs].sort((a, b) => a - b); return s.length ? s[s.length >> 1] : NaN; };
const pct = (n: number, d: number) => d ? `${(100 * n / d).toFixed(1)}%` : '-';

// the stage signature of one iteration; rel = s since the first seconds row; step = a harness step logged within the window
export function classify(h: H, rel: number, step: string | null): Cls {
  const w = h.wallMs, f = (k: string) => (h[k] ?? 0) / w;
  if (step || (h.steps === 0 && w >= 1000)) return 'harness step';
  if (rel < 60) return 'first minute after load';
  if (f('route') >= 0.5) return 'route search';
  if (f('taskObjects') >= 0.5) return 'TaskObjects burst';
  if (f('locomotion') >= 0.4) return 'locomotion';
  if (f('threaded') >= 0.5 && f('route') < 0.2) return 'ThreadedObjects, no route';
  if (f('threaded') >= 0.5) return 'mixed route + script';
  return 'other';
}

// queue log: "<ISO> $ ... profile-world.ts --tag <tag> ..." then "HH:MM:SS text" lines (UTC) until the next "$" line
// secs: the profiler length; a tag reused with another --minutes (verify-2026-10-16-*: 420 and 480 s) keeps only its own run
export function queueEvents(log: string, tag: string, secs = 0): { start: number | null; events: Ev[] } {
  let start: number | null = null, day = '', events: Ev[] = [], inRun = false;
  for (const l of log.split(/\r?\n/)) {
    const m = /^(\d{4}-\d\d-\d\d)T([\d:.]+)Z \$ (.*)$/.exec(l);
    if (m) {
      const min = /--minutes (\d+(?:\.\d+)?)/.exec(m[3]);
      inRun = m[3].includes('profile-world.ts') && m[3].includes(`--tag ${tag} `) && (!secs || !min || Math.round(Number(min[1]) * 60) === secs);
      if (inRun) { start = Date.parse(`${m[1]}T${m[2]}Z`); day = m[1]; events = []; }
      continue;
    }
    const e = /^(\d\d:\d\d:\d\d) (.*)$/.exec(l);
    if (inRun && e && start !== null) { let at = Date.parse(`${day}T${e[1]}Z`); if (at < start - 3600_000) at += 86400_000; events.push({ at, text: e[2] }); }
  }
  return { start, events };
}
const isStep = (text: string) => /^repro .* step (?!set_speed|camera_pan|wait)/.test(text) || /save_game|saving/.test(text);

// fine offset (epoch - QPC): samples inside route-heavy iterations should show a route frame, samples inside route-free ones not
export function fineOffset(samples: { t: number; r: boolean }[], hitches: H[], coarse: number, span = 15_000, step = 10) {
  const iv = hitches.filter(h => h.wallMs >= 100).map(h => ({ a: h.t - h.wallMs, b: h.t, rf: (h.route ?? 0) / h.wallMs })).sort((x, y) => x.a - y.a);
  const find = (q: number) => { let lo = 0, hi = iv.length - 1; while (lo <= hi) { const m = (lo + hi) >> 1; if (iv[m].b < q) lo = m + 1; else if (iv[m].a > q) hi = m - 1; else return iv[m]; } return null; };
  let best = { off: coarse, score: -Infinity };
  for (let off = coarse - span; off <= coarse + span; off += step) {
    let s = 0;
    for (const x of samples) { const h = find(x.t - off); if (!h) continue; if (h.rf >= 0.7) s += x.r ? 1 : -1; else if (h.rf === 0) s += x.r ? -1 : 0.1; }
    if (s > best.score) best = { off, score: s };
  }
  return best;
}
// runs whose coarse offsets lie within BOOT_GAP_MS share a boot; a boot takes the median good fine fit, else each run keeps its coarse one
export function bootOffsets(runs: { coarse: number; fine?: { off: number; score: number } }[]) {
  const order = runs.map((r, i) => i).sort((a, b) => runs[a].coarse - runs[b].coarse);
  const out: { off: number; src: string }[] = runs.map(r => ({ off: r.coarse, src: 'start+lag' }));
  let g: number[] = [];
  const flush = () => {
    const good = g.map(i => runs[i].fine).filter(f => f && f.score >= FINE_MIN && Math.abs(f.off - runs[g[0]].coarse) < BOOT_GAP_MS).map(f => f!.off);
    if (good.length) for (const i of g) out[i] = { off: med(good), src: `boot fit (${good.length})` };
    g = [];
  };
  for (const i of order) { if (g.length && runs[i].coarse - runs[g[g.length - 1]].coarse > BOOT_GAP_MS) flush(); g.push(i); }
  flush();
  return out;
}

function main(minMs: number, since: string, topN: number, jsonOut?: string) {
  const sinceMs = Date.parse(`${since}T00:00:00+08:00`);
  const an = join(ROOT, 'game-copy', 'analysis'), symf = join(an, 'ghidra-user-symbols.tsv');
  const fns = existsSync(join(an, 'functions.csv')) ? loadFunctions(readFileSync(join(an, 'functions.csv'), 'utf8'), existsSync(symf) ? readFileSync(symf, 'utf8') : '') : [];
  const files = readdirSync(PROFILE).filter(f => /-\d+-hitches\.jsonl$/.test(f) && statSync(join(PROFILE, f)).mtimeMs >= sinceMs);
  const runs = files.map(hf => {
    const tag = hf.replace(/-\d+-hitches\.jsonl$/, ''), secs = Number(/-(\d+)-hitches/.exec(hf)![1]);
    const rows = csvRows(readFileSync(join(PROFILE, hf.replace('-hitches.jsonl', '-seconds.csv')), 'utf8')), hitches: H[] = jsonl(join(PROFILE, hf));
    const q = existsSync(join(QUEUE, `${tag}.log`)) ? queueEvents(readFileSync(join(QUEUE, `${tag}.log`), 'utf8'), tag, secs) : { start: null, events: [] };
    const sim = jsonl(join(PROFILE, `${tag}-simstack.jsonl`)).map((s: any) => ({ t: s.t as number, stack: s.stack as string[], r: (s.stack as string[]).some(f => ROUTE_FN.test(f)) }));
    const q0 = rows[0]?.t ?? 0, coarse = q.start !== null ? q.start + LAG_MS - q0 : NaN;
    const fine = sim.length && !isNaN(coarse) ? fineOffset(sim, hitches, coarse) : undefined;
    return { tag, id: tag, secs, rows, hitches, q, sim, q0, coarse, fine, world: (/-(bp|ip|sv)-/.exec(tag) ?? [])[1] ?? '?', off: NaN, offSrc: '' };
  }).filter(r => r.rows.length > 30 && !isNaN(r.coarse));
  bootOffsets(runs).forEach((o, i) => { runs[i].off = o.off; runs[i].offSrc = o.src; });
  // a tag reused for two lengths (verify-2026-10-16-*: 420 and 480 s) is shown as tag@secs; files stay keyed by the tag
  runs.forEach(r => { if (runs.filter(x => x.tag === r.tag).length > 1) r.id = `${r.tag}@${r.secs}`; });

  type Rec = { tag: string; world: string; h: H; rel: number; at: number; cls: Cls; step: string | null; events: string[]; native: string[][]; managed: string[][]; acts: string[]; dSims: number };
  const recs: Rec[] = [], baseNative: string[][] = [], baseManaged: string[][] = [];
  const actAll = new Map<string, number>(); let actN = 0;
  for (const r of runs) {
    const act = jsonl(join(PROFILE, `${r.tag}-activity.jsonl`));
    for (const a of act) { actN++; for (const [, i] of a.sims) if (i) actAll.set(i, (actAll.get(i) ?? 0) + 1); }
    const man = jsonl(join(PROFILE, `${r.tag}-managed.jsonl`));
    const ma = man.length ? alignRun(r.tag, man, r.rows, r.hitches, r.secs) : null; // ma.times on the QPC clock
    const near = (q: number, pad: number) => r.hitches.some(h => h.wallMs >= 50 && q >= h.t - h.wallMs - pad && q <= h.t + pad);
    for (const s of r.sim) if (!near(s.t - r.off, 1500)) baseNative.push(s.stack);
    if (ma && ma.r >= 0.2) ma.times.forEach((t, j) => { if (!near(t, 1500)) baseManaged.push(man[j]); });
    for (const h of r.hitches.filter(h => h.wallMs >= minMs)) {
      const at = h.t + r.off, a0 = at - h.wallMs;
      const evs = r.q.events.filter(e => e.at >= a0 - 3000 && e.at <= at + 5000);
      const step = evs.find(e => isStep(e.text))?.text.replace(/^repro \S+ step (\w+).*/, '$1').slice(0, 40) ?? null;
      const rel = (h.t - r.q0) / 1000;
      const native = r.sim.filter(s => s.t - r.off >= h.t - h.wallMs && s.t - r.off <= h.t).map(s => s.stack);
      const managed = ma && ma.r >= 0.2 ? man.filter((_, j) => ma.times[j] >= h.t - h.wallMs - 100 && ma.times[j] <= h.t + 100) : [];
      const sinceStart = at - r.q.start!;
      const ai = act.reduce((b: number, a: any, i: number) => a.t <= sinceStart ? i : b, -1);
      const acts = ai >= 0 ? act[ai].sims.map((x: any) => x[1]).filter(Boolean) : [];
      const dSims = ai >= 1 ? act[ai].sims.length - act[ai - 1].sims.length : 0;
      recs.push({ tag: r.id, world: r.world, h, rel, at, cls: classify(h, rel, step), step, events: evs.map(e => e.text.slice(0, 60)), native, managed, acts, dSims });
    }
  }

  const all = recs.reduce((a, x) => a + x.h.wallMs, 0), game = recs.filter(x => x.cls !== 'harness step'), gameMs = game.reduce((a, x) => a + x.h.wallMs, 0);
  console.log(`## Runs (${runs.length}, profiler files since ${since}; hitch >= ${minMs} ms)\n| Run | World | Iterations >= ${minMs} | ms | Offset source | simstack samples | fine score |\n|---|---|---|---|---|---|---|`);
  for (const r of runs) { const x = recs.filter(y => y.tag === r.id); console.log(`| ${r.id} | ${r.world} | ${x.length} | ${Math.round(x.reduce((a, y) => a + y.h.wallMs, 0))} | ${r.offSrc} | ${r.sim.length} | ${r.fine ? r.fine.score.toFixed(1) : '-'} |`); }

  const buckets = [[300, 500], [500, 1000], [1000, 2000], [2000, Infinity]];
  console.log(`\n## Size buckets (all ${recs.length} iterations, ${Math.round(all)} ms; game-only excludes harness steps)\n| Wall ms | n | ms | n game-only | ms game-only |\n|---|---|---|---|---|`);
  for (const [a, b] of buckets) { const s = recs.filter(x => x.h.wallMs >= a && x.h.wallMs < b), g = s.filter(x => x.cls !== 'harness step'); console.log(`| ${a}-${b === Infinity ? '' : b} | ${s.length} | ${Math.round(s.reduce((p, x) => p + x.h.wallMs, 0))} | ${g.length} | ${Math.round(g.reduce((p, x) => p + x.h.wallMs, 0))} |`); }
  const STAGES = ['threaded', 'taskObjects', 'locomotion', 'animPose', 'animSacs', 'script', 'gcMark', 'fin', 'oneShot', 'publish'];
  const unacc = game.reduce((a, x) => a + x.h.wallMs - STAGES.reduce((s, k) => s + (x.h[k] ?? 0), 0), 0);
  console.log(`\n## Stage share of game-only >= ${minMs} ms time (${game.length} iterations, ${Math.round(gameMs)} ms)\n| Stage | Share |\n|---|---|`);
  for (const k of [...STAGES, 'route']) console.log(`| ${k}${k === 'route' ? ' (nested in threaded/taskObjects)' : ''} | ${pct(game.reduce((a, x) => a + (x.h[k] ?? 0), 0), gameMs)} |`);
  console.log(`| not in a timed stage | ${pct(unacc, gameMs)} |`);

  const classes = [...new Set(recs.map(x => x.cls))];
  const runMin = runs.reduce((a, r) => a + (r.rows.at(-1)!.t - r.q0) / 60000, 0);
  console.log(`\n## Causes (share of all >= ${minMs} ms time; per hour over ${runMin.toFixed(0)} profiled minutes)\n| Class | n | ms | share all | share game-only | median ms | max ms | BP / IP n | per hour | with simstack | with managed |\n|---|---|---|---|---|---|---|---|---|---|---|`);
  for (const c of classes.sort((a, b) => recs.filter(x => x.cls === b).reduce((p, x) => p + x.h.wallMs, 0) - recs.filter(x => x.cls === a).reduce((p, x) => p + x.h.wallMs, 0))) {
    const s = recs.filter(x => x.cls === c), ms = s.reduce((p, x) => p + x.h.wallMs, 0);
    console.log(`| ${c} | ${s.length} | ${Math.round(ms)} | ${pct(ms, all)} | ${c === 'harness step' ? '-' : pct(ms, gameMs)} | ${Math.round(med(s.map(x => x.h.wallMs)))} | ${Math.round(Math.max(...s.map(x => x.h.wallMs)))} | ${s.filter(x => x.world === 'bp').length} / ${s.filter(x => x.world === 'ip').length} | ${(60 * s.length / runMin).toFixed(1)} | ${s.filter(x => x.native.length).length} | ${s.filter(x => x.managed.length).length} |`);
  }

  const name = (e: number) => fns.find(f => f.entry === e)?.name ?? `0x${e.toString(16)}`;
  const nb = split(baseNative, fns, 24);
  for (const c of classes) {
    const s = recs.filter(x => x.cls === c);
    console.log(`\n### ${c}`);
    const gaps = runs.flatMap(r => { const t = s.filter(x => x.tag === r.id).map(x => x.rel).sort((a, b) => a - b); return t.slice(1).map((v, i) => v - t[i]).filter(g => g > 5); });
    console.log(`stage means ms: ${['threaded', 'route', 'taskObjects', 'locomotion', 'gcMark'].map(k => `${k} ${Math.round(s.reduce((a, x) => a + (x.h[k] ?? 0), 0) / s.length)}`).join(', ')}; wall ${Math.round(s.reduce((a, x) => a + x.h.wallMs, 0) / s.length)}; ` +
      `GC mark in iteration ${pct(s.filter(x => x.h.gcMarks > 0).length, s.length)}; median gap between same-class hitches in a run ${gaps.length ? Math.round(med(gaps)) + ' s' : '-'} (${gaps.length} gaps)`);
    const steps = new Map<string, number>(); s.forEach(x => x.step && steps.set(x.step, (steps.get(x.step) ?? 0) + 1));
    if (steps.size) console.log(`harness steps: ${[...steps].map(([k, v]) => `${k} ${v}`).join(', ')}`);
    const nat = s.flatMap(x => x.native);
    if (nat.length) {
      const sp = split(nat, fns, 24);
      console.log(`native samples ${nat.length} (baseline ${baseNative.length}); top inclusive TS3W functions:\n| Function | in class | baseline | lift |\n|---|---|---|---|`);
      for (const [e, v] of [...sp.rows].sort((a, b) => b[1].incl - a[1].incl).slice(0, topN)) { const bi = (nb.rows.get(e)?.incl ?? 0) / Math.max(1, baseNative.length), ci = v.incl / nat.length; console.log(`| ${name(e)} | ${pct(v.incl, nat.length)} | ${pct(bi * 1000, 1000)} | ${bi ? (ci / bi).toFixed(1) : 'new'} |`); }
      const leafMod = new Map<string, number>(); nat.forEach(st => { const m = (/ ([\w.-]+?)(?:!|\+)/.exec(st[0] ?? '') ?? [])[1] ?? '?'; leafMod.set(m, (leafMod.get(m) ?? 0) + 1); });
      console.log(`leaf module: ${[...leafMod].sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k} ${pct(v, nat.length)}`).join(', ')}`);
    }
    const man = s.flatMap(x => x.managed);
    if (man.length) {
      const has = (re: RegExp) => s.filter(x => x.managed.some(m => m.some(f => re.test(f)))).length;
      console.log(`hitches with managed samples ${s.filter(x => x.managed.length).length}; with a sample on: NRaas ${has(/^NRaas\./)}, NRaas Overwatch CleanupInventory ${has(/CleanupInventory/)}, our hooks/Ts3Fixes ${has(/Hook\.|^Ts3Fixes\./)}, Ts3Fixes SafeSpot ${has(/Ts3Fixes\.Fixes\.SafeSpot/)}, a route plan leaf ${s.filter(x => x.managed.some(m => /^ScriptCore\.Route\.Route_/.test(m[0] ?? ''))).length}`);
      const cnt = (xs: string[][], key: (m: string[]) => string) => { const m = new Map<string, number>(); xs.forEach(x => m.set(key(x), (m.get(key(x)) ?? 0) + 1)); return m; };
      for (const [title, key] of [['category', category], ['task root', rootOf], ['leaf', (m: string[]) => m[0] ?? '(none)']] as const) {
        const a = cnt(man, key), b = cnt(baseManaged, key);
        console.log(`managed ${title} (${man.length} samples, baseline ${baseManaged.length}):\n| Key | in class | baseline | lift |\n|---|---|---|---|`);
        for (const [k, v] of [...a].sort((x, y) => y[1] - x[1]).slice(0, Math.min(topN, 12))) { const bb = (b.get(k) ?? 0) / Math.max(1, baseManaged.length); console.log(`| ${k} | ${pct(v, man.length)} | ${pct(b.get(k) ?? 0, baseManaged.length)} | ${bb ? ((v / man.length) / bb).toFixed(1) : 'new'} |`); }
      }
    }
    const ia = new Map<string, number>(); let iaN = 0; s.forEach(x => { if (x.acts.length || x.dSims) iaN++; x.acts.forEach(i => ia.set(i, (ia.get(i) ?? 0) + 1)); });
    if (iaN) {
      const lifts = [...ia].filter(([, v]) => v >= 3).map(([k, v]) => [k, (v / iaN) / ((actAll.get(k) ?? 0) / actN), v] as const).sort((a, b) => b[1] - a[1]).slice(0, 8);
      console.log(`activity (last 30 s snapshot before the hitch, ${iaN} hitches): Sims count changed in ${s.filter(x => x.dSims).length}; interactions with the highest lift: ${lifts.map(([k, l, v]) => `${k} x${l.toFixed(1)} (${v})`).join('; ')}`);
    }
    const evc = new Map<string, number>(); s.forEach(x => x.events.forEach(e => { const k = e.replace(/[\d.:]+/g, '#').slice(0, 45); evc.set(k, (evc.get(k) ?? 0) + 1); }));
    if (evc.size) console.log(`queue log lines within -3..+5 s: ${[...evc].sort((a, b) => b[1] - a[1]).slice(0, 6).map(([k, v]) => `"${k}" ${v}`).join('; ')}`);
  }
  console.log(`\n## Worst ${topN} game-only iterations\n| Run | rel s | wall | threaded | route | taskObjects | locomotion | class | plans (n/ms) | native / managed samples |\n|---|---|---|---|---|---|---|---|---|---|`);
  for (const x of [...game].sort((a, b) => b.h.wallMs - a.h.wallMs).slice(0, topN))
    console.log(`| ${x.tag} | ${Math.round(x.rel)} | ${Math.round(x.h.wallMs)} | ${Math.round(x.h.threaded ?? 0)} | ${Math.round(x.h.route ?? 0)} | ${Math.round(x.h.taskObjects ?? 0)} | ${Math.round(x.h.locomotion ?? 0)} | ${x.cls} | ${Object.entries(x.h.byEntry ?? {}).map(([k, v]: any) => `${k.replace('Route_', '')} ${v.n}/${Math.round(v.ms)}`).join(', ')} | ${x.native.length} / ${x.managed.length} |`);
  // --ab runs: the switched patch on vs off per class (slices from ab.json, s since the profile-world start; warm-up slice left out)
  // first 60 s left out (load class); runs grouped by the switched patch names
  const abRows = new Map<string, [number, number, number, number]>(); // patch | class -> on n, on ms, off n, off ms
  const abSec = new Map<string, [number, number]>();
  for (const r of runs) {
    const f = join(PROFILE, `${r.tag}-ab.json`);
    if (!existsSync(f)) continue;
    const ab = JSON.parse(readFileSync(f, 'utf8')), sl: { from: number; on: boolean; warmup?: boolean }[] = ab.slices ?? [], p = (ab.names ?? []).join(',');
    const end = (r.rows.at(-1)!.t + r.off - r.q.start!) / 1000, sec = abSec.get(p) ?? [0, 0], skip = (r.q0 + r.off - r.q.start!) / 1000 + 60;
    sl.forEach((s, i) => { if (!s.warmup) sec[s.on ? 0 : 1] += Math.max(0, Math.min(sl[i + 1]?.from ?? end, end) - Math.max(s.from, skip)); });
    abSec.set(p, sec);
    for (const x of recs.filter(y => y.tag === r.id && y.cls !== 'harness step' && y.rel >= 60)) {
      const s = (x.at - r.q.start!) / 1000, k = sl.findLastIndex(y => y.from <= s);
      if (k < 0 || sl[k].warmup) continue;
      const key = `${p} | ${x.cls}`, v = abRows.get(key) ?? [0, 0, 0, 0], o = sl[k].on ? 0 : 2; v[o]++; v[o + 1] += x.h.wallMs; abRows.set(key, v);
    }
  }
  if (abRows.size) {
    console.log(`\n## A/B runs: switched patch on vs off (warm-up slices and the first 60 s left out)\n| Patch | Class | on n | on ms/h | off n | off ms/h |\n|---|---|---|---|---|---|`);
    for (const [k, v] of [...abRows].sort()) { const [p, c] = k.split(' | '), sec = abSec.get(p)!; console.log(`| ${p} (${(sec[0] / 60).toFixed(0)}/${(sec[1] / 60).toFixed(0)} min) | ${c} | ${v[0]} | ${Math.round(3600 * v[1] / sec[0])} | ${v[2]} | ${Math.round(3600 * v[3] / sec[1])} |`); }
  }
  if (jsonOut) writeFileSync(jsonOut, JSON.stringify(recs.map(x => ({ ...x, native: x.native.map(s => s.slice(0, 16)), managed: x.managed.map(m => m.slice(0, 12)) })), null, 1));
}

function selfCheck() {
  const assert = (c: unknown, m: string) => { if (!c) throw new Error(`self-check failed: ${m}`); };
  const h = (o: Record<string, number>) => ({ t: 0, steps: 10, ...o }) as H;
  assert(classify(h({ wallMs: 400, route: 300, threaded: 350 }), 100, null) === 'route search', 'route');
  assert(classify(h({ wallMs: 400, taskObjects: 380 }), 100, null) === 'TaskObjects burst', 'task');
  assert(classify(h({ wallMs: 4000, taskObjects: 3900 }), 100, 'save_game') === 'harness step', 'step');
  assert(classify(h({ wallMs: 12000, taskObjects: 11900, steps: 0 }), 100, null) === 'harness step', 'steps 0');
  assert(classify(h({ wallMs: 400, threaded: 380, route: 10 }), 30, null) === 'first minute after load', 'load');
  assert(classify(h({ wallMs: 400, threaded: 380, route: 10 }), 100, null) === 'ThreadedObjects, no route', 'thr');
  assert(classify(h({ wallMs: 400, threaded: 300, route: 120 }), 100, null) === 'mixed route + script', 'mixed');
  assert(classify(h({ wallMs: 400, threaded: 150, locomotion: 200 }), 100, null) === 'locomotion', 'loco');
  const q = queueEvents('2026-10-18T03:17:51.346Z $ node profile-world.ts --tag x-ip --minutes 10\n03:18:52 managed sampler 180 s\n03:20:00 repro r step save_game: {}\n\n2026-10-18T04:00:00.000Z $ node other.ts\n04:00:01 not ours\n', 'x-ip');
  assert(q.start === Date.parse('2026-10-18T03:17:51.346Z') && q.events.length === 2 && isStep(q.events[1].text) && !isStep(q.events[0].text), 'queueEvents');
  // fine fit: samples taken 5 s (epoch) after QPC 10000.. ; route hitch at QPC 10000-10400, route-free one at 20000-20400
  const hs = [{ t: 10400, wallMs: 400, route: 390 }, { t: 20400, wallMs: 400, route: 0 }] as H[];
  const off = 1_000_000;
  const smp = [10100, 10200, 10300].map(t => ({ t: t + off, r: true })).concat([20100, 20200, 20300].map(t => ({ t: t + off, r: false })));
  const f = fineOffset(smp, hs, off + 2000, 3000, 10);
  assert(Math.abs(f.off - off) <= 100 && f.score >= 3, `fineOffset ${f.off - off} ${f.score}`);
  const b = bootOffsets([{ coarse: 5000 }, { coarse: 7000, fine: { off: 6100, score: 30 } }, { coarse: 900_000 }]);
  assert(b[0].off === 6100 && b[1].off === 6100 && b[2].off === 900_000 && b[2].src === 'start+lag', `bootOffsets ${JSON.stringify(b)}`);
  console.log('self-check ok');
}

const isMain = resolve(process.argv[1] ?? '') === fileURLToPath(import.meta.url);
const args = isMain ? process.argv.slice(2) : [];
const opt = (k: string, d: string) => { const i = args.indexOf(k); return i < 0 ? d : args[i + 1]; };
if (!isMain) { /* imported */ }
else if (args.includes('--self-check')) selfCheck();
else main(Number(opt('--min', '300')), opt('--since', '2026-10-16'), Number(opt('--top', '25')), args.includes('--json') ? opt('--json', '') : undefined);
