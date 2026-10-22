// Visible freezes and camera stutter from one profile run (docs/162). Reads only; never talks to the game.
//   node tools/harness/freeze-report.ts <tag> [<tag> ...] [--min 300] [--top 10] [--repro-causes]   (--repro-causes: causes of
//     the iterations >= --min inside repro firings, which the tables exclude as scenario cost; see reproCauses)
//   node tools/harness/freeze-report.ts --self-check
// Inputs in probe-out/profile (profile-world.ts): <tag>-<secs>-freezes.jsonl (sim publish gaps >= 50 ms), -hitches.jsonl,
// -seconds.csv (stutter-profile.js, QPC ms), -frames.csv (frame-times.js, QPC + epoch ms), <tag>-nativelog.jsonl (sync-plan lines
// of ts3native.log stamped with epoch ms), <tag>-managed.jsonl (C# stacks, minutes 1-4 only, placed by stall-split.ts alignRun).
// Visible freeze = time between two FrameDb_PublishFrame calls (the renderer holds the newest frame until the next one). Camera
// stutter = presented frame time on the main thread. Real only (docs/161 §6, worst-hitches.ts exclusion): first minute, profiler
// edge, repro steps, samplers, agent calls and render-paced gaps are excluded and listed by reason; a noisy/cut/crashed run is
// marked INVALID. Each freeze >= --min ms gets worst-hitches.ts cause() of its longest iteration (class, lead bucket,
// longest route-job wait), the sync plan logged in it (reason) and the managed category sampled in it.
// Set TS3_REPO=<main checkout> when running from a git worktree.
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { baseWins, cause, exclAdd, exclTable, exclusion, firingWins, reproSteps, runContext, SIM_MS, type ExclRow, type RunCtx, type Win } from './worst-hitches.ts';
import { alignRun, category } from './stall-split.ts';
import { parseSyncLines } from './route-phases-report.ts';

const ROOT = process.env.TS3_REPO ?? join(import.meta.dirname, '..', '..');
const PROFILE = join(ROOT, 'probe-out', 'profile'), QUEUE = join(ROOT, 'probe-out', 'queue');
const LOG_MS = 50;
// thresholds shared with hitch-ab.ts (keep the same lists there)
export const FREEZE_MS = [100, 300, 500, 1000, 2000], FRAME_MS = [50, 100, 300];

type R = Record<string, any>;
export type Freeze = { t0: number; t1: number; gapMs: number; prevGapMs: number; n: number; iters: number };
// st: exclusive main-loop stage ms (frameStages); cons: consume counters; both absent before docs/167's frame-times.js
export type Frame = { epoch: number; q: number; ms: number; fg: number; st?: Record<string, number>; cons?: { n: number; tryFail: number; notReady: number; drains: number; msgs: number };
  svc?: { name: string; ms: number }[] };

// Main-loop stages of frames.csv (frame-times.js, docs/167). Exclusive: services and capture lose their nested consume and
// render callback. An unhooked stage (`na`) is left out; its time sits in other.
export const STAGES = ['load state', 'idle sleep', 'services', 'consume', 'video capture', 'render callback', 'begin frame', 'draw', 'present',
  'after present (S3SS limiter, SleepEx)', 'end-frame job wait', 'other'];
export function frameStages(x: R): Pick<Frame, 'st' | 'cons'> {
  if (!('svcMs' in x)) return {};
  const v = (k: string) => Number.isFinite(x[k]) ? x[k] as number : NaN, z = (k: string) => Number.isFinite(x[k]) ? x[k] as number : 0;
  const raw = [v('loadMs'), v('idleMs'), v('svcMs') - z('consumeMs'), v('consumeMs'), v('captureMs') - z('rcbMs'), v('rcbMs'), v('beginMs'), v('drawMs'),
    v('presentMs'), v('postMs'), v('endMs'), v('otherMs')];
  const st: Record<string, number> = {}; raw.forEach((ms, i) => { if (Number.isFinite(ms)) st[STAGES[i]] = Math.max(0, ms); });
  return { st, cons: Number.isFinite(x.consumes) ? { n: x.consumes, tryFail: z('tryFail'), notReady: z('notReady'), drains: z('drains'), msgs: z('msgs') } : undefined };
}
// top-3 main-slot services of the frame (frame-times.js svc1..svc3, docs/167 §7), longest first; absent when not recorded (`na`)
export function frameServices(x: R): Pick<Frame, 'svc'> {
  if (!('svc1' in x) || Number.isNaN(x.svc1)) return {};
  return { svc: [1, 2, 3].flatMap(k => typeof x[`svc${k}`] === 'string' && x[`svc${k}`] && Number.isFinite(x[`svc${k}Ms`]) ? [{ name: x[`svc${k}`] as string, ms: x[`svc${k}Ms`] as number }] : []) };
}
export const leadStage = (st: Record<string, number>) => Object.entries(st).sort((a, b) => b[1] - a[1])[0] ?? ['', 0];
export type Run = { tag: string; secs: number; rows: R[]; hitches: R[]; freezes: Freeze[]; frames: Frame[]; nlog: { t: number; line: string }[]; managed: string[][]; ctx?: RunCtx };

// nearest-rank percentile when only the values >= LOG_MS are known: null = below LOG_MS
export function tailPct(tail: number[], total: number, p: number): number | null {
  if (!total) return null;
  const s = [...tail].sort((a, b) => a - b), i = Math.max(0, Math.ceil(p * total) - 1), lo = total - s.length;
  return i >= lo ? s[i - lo] : null;
}
const nearest = (xs: number[], p: number) => { const s = [...xs].sort((a, b) => a - b); return s.length ? s[Math.max(0, Math.ceil(p * s.length) - 1)] : NaN; };
const per10 = (xs: number[], mins: number, ths: number[], gt = false) => Object.fromEntries(ths.map(m => [m, mins ? xs.filter(x => gt ? x > m : x >= m).length * 10 / mins : 0]));

// the sync plan line (and its script stack) logged in a freeze; t = epoch ms stamped by profile-world within ~1 s after the write
export function syncPlans(nlog: Run['nlog']) {
  const out: { t: number; ms: number; ic: string; reason: string; entry: string; stack: string }[] = [];
  for (const { t, line } of nlog) {
    const m = /^route async: sync plan (\d+) ms, IC (\S+), reason (\S+), entry (\S+?),/.exec(line);
    if (m) out.push({ t, ms: Number(m[1]), ic: m[2], reason: m[3], entry: m[4], stack: '' });
    else if (out.length && /^route async: script stack/.test(line)) out[out.length - 1].stack ||= line.replace(/^.*?:\s*[^:]*:\s*/, '').split(' < ').slice(0, 3).join(' < ');
  }
  return out;
}

// Real vs excluded (worst-hitches.ts exclusion, docs/161 §6): seconds rows inside an excluded window (first minute, profiler
// edge, repro step, sampler, probe call) leave the minutes, publishes and frames; a gap is excluded by its own window, its row's,
// or as render-paced (no sim iteration >= 50 ms inside); counts, percentiles and causes are over real gaps only
export function analyze(r: Run, minMs = 300) {
  const ctx = r.ctx ?? { wins: baseWins(r.rows), foreground: false, invalid: null, offSrc: 'none' };
  const t0 = r.rows[0]?.t ?? r.freezes[0]?.t0 ?? 0;
  const rowWhy = r.rows.map((x, i) => exclusion({ a: i ? r.rows[i - 1].t : x.t - 1000, b: x.t }, ctx)?.why ?? null);
  const excl = new Map<string, ExclRow>(); rowWhy.forEach(w => w && exclAdd(excl, w, 0, 1));
  const rowOf = (t: number) => r.rows.findIndex(x => x.t >= t);
  const rows = r.rows.filter((_, i) => !rowWhy[i]), mins = rows.length / 60;
  const hasPub = rows.some(x => 'publishes' in x);
  let pubs = rows.reduce((s, x) => s + (x.publishes ?? 0), 0);
  const iters = rows.reduce((s, x) => s + (x.iters ?? 0), 0);
  // frames in excluded windows leave the frame stats too: the sampler windows also slow the main thread (render jobs the
  // suspended sim thread's pump runs, docs/41); counted apart by reason
  const frameExcl = new Map<string, ExclRow>();
  const frames = r.frames.filter(f => { const w = exclusion({ a: f.q - f.ms, b: f.q }, ctx)?.why; if (w) exclAdd(frameExcl, w, f.ms); return !w; }), fms = frames.map(f => f.ms);
  // epoch - QPC from the frame log (both stamped in the same call); without frames the sync-plan join is skipped
  const off = r.frames.length ? nearest(r.frames.map(f => f.epoch - f.q), 0.5) : NaN;
  const plans = syncPlans(r.nlog);
  const tl = new Map<number, R>(); for (const h of r.hitches) if (h.n !== undefined) tl.set(h.n, h);
  const ma = r.managed.length && r.rows.length > 30 ? alignRun(r.tag, r.managed, r.rows, r.hitches, r.secs) : null;
  const itersOf = (f: Freeze) => f.n !== undefined && tl.size ? Array.from({ length: f.iters + 1 }, (_, k) => tl.get(f.n - f.iters + k)).filter(Boolean) as R[]
    : r.hitches.filter(h => h.t - h.wallMs < f.t1 && h.t > f.t0); // older hitches without n: by time
  const fz = r.freezes.filter(f => {
    const ri = rowOf(f.t1), longest = Math.max(0, ...itersOf(f).map(h => h.wallMs));
    const why = exclusion({ a: f.t0, b: f.t1, gap: { longestIterMs: longest, iters: f.iters } }, ctx)?.why ?? (ri < 0 ? 'profiler edge' : rowWhy[ri]);
    if (!why) return true;
    exclAdd(excl, why, f.gapMs);
    if (ri >= 0 && !rowWhy[ri]) pubs--; // an excluded gap in a kept row leaves the publish count too
    return false;
  }), gaps = fz.map(f => f.gapMs);
  const detail = fz.filter(f => f.gapMs >= LOG_MS).map(f => {
    const its = itersOf(f), main = its.sort((a, b) => b.wallMs - a.wallMs)[0];
    const c = main ? cause(main as any, (main.t - t0) / 1000, null) : null;
    const plan = isNaN(off) ? null : plans.filter(p => p.t >= f.t0 + off && p.t <= f.t1 + off + 1500 && p.ms <= f.gapMs + 50).sort((a, b) => b.ms - a.ms)[0] ?? null;
    const mIdx = ma && ma.r >= 0.2 ? ma.times.flatMap((t, j) => t >= f.t0 && t <= f.t1 ? [j] : []) : [];
    const cats = new Map<string, number>(); for (const j of mIdx) cats.set(category(r.managed[j]), (cats.get(category(r.managed[j])) ?? 0) + 1);
    return { f, main, longestIterMs: main?.wallMs ?? 0, cls: c ? c.cls : `no iteration >= ${LOG_MS} ms (publish skipped)`, lead: c?.lead ?? '', wait: c?.wait ?? '', plans: c?.plans ?? '', tasks: c?.tasks ?? '', topTask: c?.topTask ?? '',
      plan, managed: [...cats].sort((a, b) => b[1] - a[1])[0]?.[0] ?? '' };
  });
  // slow presented frames by main-loop stage (docs/167): real frames only (the same exclusion as the frame stats above)
  const staged = frames.filter(f => f.st), slow100 = staged.filter(f => f.ms >= 100);
  const stages = staged.length ? {
    n: staged.length, slowN: slow100.length,
    rows: STAGES.map(s => ({ stage: s, lead: Object.fromEntries(FRAME_MS.map(m => [m, staged.filter(f => f.ms > m && leadStage(f.st!)[0] === s).length])),
      ms100: slow100.reduce((t, f) => t + (f.st![s] ?? 0), 0), p50: nearest(staged.flatMap(f => s in f.st! ? [f.st![s]] : []), 0.5) }))
      .filter(r => Number.isFinite(r.p50)),
    ms100: slow100.reduce((t, f) => t + f.ms, 0),
    tryFail: slow100.filter(f => f.cons?.tryFail).length, notReady: slow100.filter(f => f.cons?.notReady).length, noConsume: slow100.filter(f => f.cons && !f.cons.n).length,
    worst: [...slow100].sort((a, b) => b.ms - a.ms).map(f => ({ f, lead: leadStage(f.st!), next: Object.entries(f.st!).sort((a, b) => b[1] - a[1]).slice(1, 3) })),
  } : null;
  // services stage by service (docs/167 §7): lead = the frame's longest service; ms only from each frame's top 3
  // (shortcut: a service outside a frame's top 3 counts 0 there; the rest is svcMs - top 3, shown as "other services")
  const sv = staged.filter(f => f.svc), sv100 = sv.filter(f => f.ms >= 100), svStage = (f: Frame) => (f.st!['services'] ?? 0) + (f.st!['consume'] ?? 0);
  const svNames = [...new Set(sv.flatMap(f => f.svc!.map(x => x.name)))];
  const msOf = (f: Frame, n: string) => f.svc!.find(x => x.name === n)?.ms ?? 0;
  const services = sv.length ? {
    n: sv.length, slowN: sv100.length, ms100: sv100.reduce((t, f) => t + f.ms, 0), svc100: sv100.reduce((t, f) => t + svStage(f), 0),
    rows: [...svNames.map(name => ({ name, lead: Object.fromEntries(FRAME_MS.map(m => [m, sv.filter(f => f.ms > m && f.svc![0]?.name === name).length])),
      ms100: sv100.reduce((t, f) => t + msOf(f, name), 0), top3: sv100.filter(f => msOf(f, name) > 0).length,
      p50: nearest(sv.flatMap(f => f.svc!.some(x => x.name === name) ? [msOf(f, name)] : []), 0.5) })).sort((a, b) => b.ms100 - a.ms100),
    { name: 'other services (outside top 3, list walk)', lead: Object.fromEntries(FRAME_MS.map(m => [m, 0])), top3: 0,
      ms100: sv100.reduce((t, f) => t + Math.max(0, svStage(f) - f.svc!.reduce((u, x) => u + x.ms, 0)), 0), p50: NaN }],
  } : null;
  const big = detail.filter(d => d.f.gapMs >= minMs);
  const causes = new Map<string, typeof big>(); for (const d of big) causes.set(d.cls, [...(causes.get(d.cls) ?? []), d]);
  const over100 = detail.filter(d => d.f.gapMs >= 100);
  return {
    tag: r.tag, t0, mins, minMs, excl, frameExcl, invalid: ctx.invalid, foreground: ctx.foreground, freezes: per10(gaps, mins, FREEZE_MS), hasPub, pubs, iters,
    gap: { p99: hasPub ? tailPct(gaps, pubs, 0.99) : NaN, p999: hasPub ? tailPct(gaps, pubs, 0.999) : NaN, max: gaps.length ? Math.max(...gaps) : 0 },
    frames: { n: frames.length, fps: fms.length ? fms.length / (fms.reduce((a, b) => a + b, 0) / 1000) : 0, p99: nearest(fms, 0.99), p999: nearest(fms, 0.999), max: fms.length ? Math.max(...fms) : NaN,
      over: per10(fms, mins, FRAME_MS, true), focused: frames.length ? frames.filter(f => f.fg).length / frames.length : 0 }, stages, services,
    // gap vs iteration (docs/162 §2): how much longer the screen holds than the longest sim iteration inside
    vsIter: { n: over100.length, extraP50: nearest(over100.map(d => d.f.gapMs - d.longestIterMs), 0.5), extraP90: nearest(over100.map(d => d.f.gapMs - d.longestIterMs), 0.9),
      noLongIter: over100.filter(d => !d.main).length, multiIter: over100.filter(d => d.f.iters >= 2).length, prevGapP50: nearest(fz.map(f => f.prevGapMs), 0.5) },
    causes: [...causes].map(([cls, ds]) => {
      const top = (k: (d: typeof ds[number]) => string) => { const m = new Map<string, number>(); ds.forEach(d => { const v = k(d); if (v) m.set(v, (m.get(v) ?? 0) + 1); }); return [...m].sort((a, b) => b[1] - a[1]).slice(0, 2).map(([v, n]) => `${v} (${n})`).join('; '); };
      return { cls, n: ds.length, ms: ds.reduce((s, d) => s + d.f.gapMs, 0), med: nearest(ds.map(d => d.f.gapMs), 0.5), max: Math.max(...ds.map(d => d.f.gapMs)),
        lead: top(d => d.lead), wait: top(d => d.wait.replace(/ \d+ ms$/, '')), reason: top(d => d.plan ? `${d.plan.reason} ${d.plan.ic}` : ''), managed: top(d => d.managed), task: top(d => d.topTask) };
    }).sort((a, b) => b.ms - a.ms),
    worst: [...big].sort((a, b) => b.f.gapMs - a.f.gapMs),
    syncJoin: isNaN(off) ? 'no frames.csv: sync-plan join skipped' : `${plans.length} sync plan lines`, managedJoin: ma ? `managed r ${ma.r.toFixed(2)}` : 'no managed samples',
  };
}

const f0 = (x: number | null) => x === null ? `< ${LOG_MS}` : Number.isFinite(x) ? String(Math.round(x)) : '-';
const f1 = (x: number) => x.toFixed(1);
export function report(a: ReturnType<typeof analyze>, topN = 10) {
  const L = [`## ${a.tag} (${f1(a.mins)} real min)`, ''];
  if (a.invalid) L.push(`**RUN INVALID: ${a.invalid}** (numbers below shown for reading only; left out of pair means)`, '');
  L.push(`Visible Sim freezes (publish gaps) per 10 min: ${FREEZE_MS.map(m => `>= ${m} ms ${f1(a.freezes[m])}`).join(', ')}`);
  L.push(`Publish gap p99 ${f0(a.gap.p99)} / p99.9 ${f0(a.gap.p999)} / max ${f0(a.gap.max)} ms` + (a.hasPub ? ` (${a.pubs} publishes, ${a.iters} iterations, ${(a.pubs / Math.max(1, a.iters)).toFixed(2)} per iteration)` : ' (no publishes column: profiler older than docs/162, percentiles unknown)'));
  L.push(a.frames.n ? `Presented frames: ${a.frames.n} (${f1(a.frames.fps)} FPS, focused ${Math.round(100 * a.frames.focused)}%), p99 ${f0(a.frames.p99)} / p99.9 ${f0(a.frames.p999)} / max ${f0(a.frames.max)} ms; per 10 min ${FRAME_MS.map(m => `> ${m} ms ${f1(a.frames.over[m])}`).join(', ')}; excluded frames ${[...a.frameExcl].map(([k, x]) => `${k} ${x.n} (${x.n ? f1(x.n / (x.ms / 1000)) : '-'} FPS)`).join(', ') || 'none'}`
    : 'Presented frames: none (no frames.csv)');
  L.push(`Gap vs iteration (gaps >= 100 ms, ${a.vsIter.n}): gap - longest iteration inside p50 ${f0(a.vsIter.extraP50)} / p90 ${f0(a.vsIter.extraP90)} ms; no iteration >= ${LOG_MS} ms inside ${a.vsIter.noLongIter}; spanning >= 2 iteration boundaries ${a.vsIter.multiIter}; previous gap p50 ${f0(a.vsIter.prevGapP50)} ms (interpolation slack, docs/162 §2)`);
  L.push(`Joins: ${a.syncJoin}; ${a.managedJoin}; ForegroundRender ${a.foreground ? 'on' : 'off (gaps with no iteration >= 50 ms = render-paced)'}`, '');
  L.push(exclTable(a.excl, 'publish gaps'), '');
  L.push(`### Top causes, freezes >= ${a.minMs} ms`, '', '| Cause (longest iteration) | n | ms | median | max | lead bucket | longest route-job wait | sync plan reason | slowest script run | managed |', '|---|---|---|---|---|---|---|---|---|---|');
  for (const c of a.causes) L.push(`| ${c.cls} | ${c.n} | ${Math.round(c.ms)} | ${Math.round(c.med)} | ${Math.round(c.max)} | ${c.lead} | ${c.wait} | ${c.reason} | ${c.task} | ${c.managed} |`);
  L.push('', `### Worst ${topN}`, '', '| s | gap ms | iterations | longest iteration ms | cause | lead | wait | plans (n/ms) | sync plan | slowest script runs (ms) | managed |', '|---|---|---|---|---|---|---|---|---|---|---|');
  for (const d of a.worst.slice(0, topN)) L.push(`| ${Math.round((d.f.t1 - a.t0) / 1000)} | ${Math.round(d.f.gapMs)} | ${d.f.iters + 1} | ${Math.round(d.longestIterMs)} | ${d.cls} | ${d.lead} | ${d.wait} | ${d.plans} | ${d.plan ? `${d.plan.ms} ms ${d.plan.reason} ${d.plan.ic} ${d.plan.stack}` : ''} | ${d.tasks} | ${d.managed} |`);
  L.push('', '### Slow frames by stage (main loop, docs/167)', '');
  const S = a.stages;
  if (!S) L.push(a.frames.n ? 'No stage columns in frames.csv (frame-times.js older than docs/167).' : 'No frames.csv.');
  else {
    L.push(`Real frames with stages: ${S.n}; frames >= 100 ms: ${S.slowN} (${Math.round(S.ms100)} ms); of these consume skipped (TryEnter failed or +0xF8) in ${S.tryFail}, no ready sim frame in ${S.notReady}, no consume call in ${S.noConsume}`, '');
    L.push(`| Stage (exclusive) | ${FRAME_MS.map(m => `lead in frames > ${m} ms`).join(' | ')} | ms in frames >= 100 ms (share) | median per frame ms |`, `|---|${FRAME_MS.map(() => '---|').join('')}---|---|`);
    for (const r of S.rows) L.push(`| ${r.stage} | ${FRAME_MS.map(m => r.lead[m]).join(' | ')} | ${Math.round(r.ms100)} (${S.ms100 ? Math.round(100 * r.ms100 / S.ms100) : 0}%) | ${r.p50.toFixed(2)} |`);
    L.push('', `### Slow frames (>= 100 ms), worst ${topN}`, '', '| s | frame ms | lead stage | lead ms | next stages ms | consume n / tryFail / notReady / msgs | fg | top services ms |', '|---|---|---|---|---|---|---|---|');
    for (const w of S.worst.slice(0, topN)) L.push(`| ${Math.round((w.f.q - a.t0) / 1000)} | ${Math.round(w.f.ms)} | ${w.lead[0]} | ${Math.round(w.lead[1])} | ${w.next.map(([k, x]) => `${k} ${Math.round(x)}`).join('; ')} | ${w.f.cons ? `${w.f.cons.n} / ${w.f.cons.tryFail} / ${w.f.cons.notReady} / ${w.f.cons.msgs}` : '-'} | ${w.f.fg} | ${w.f.svc ? w.f.svc.map(x => `${x.name} ${Math.round(x.ms)}`).join('; ') : '-'} |`);
  }
  L.push('', '### Services by service (main slot +0x1C, docs/167 §7)', '');
  const V = a.services;
  if (!V) L.push(S ? 'No service columns in frames.csv (frame-times.js older than docs/167 §7, or ServiceList_Update not hooked).' : 'No stage columns.');
  else {
    L.push(`Frames with the service split: ${V.n}; frames >= 100 ms: ${V.slowN} (${Math.round(V.ms100)} ms, of it services stage incl. consume ${Math.round(V.svc100)} ms). ms per frame come from each frame's top 3 services; median = over frames where the service is in the top 3`, '');
    L.push(`| Service | ${FRAME_MS.map(m => `lead in frames > ${m} ms`).join(' | ')} | ms in frames >= 100 ms (share of frame ms) | in top 3 of frames >= 100 ms | median per frame ms |`, `|---|${FRAME_MS.map(() => '---|').join('')}---|---|---|`);
    for (const r of V.rows) L.push(`| ${r.name} | ${FRAME_MS.map(m => r.lead[m]).join(' | ')} | ${Math.round(r.ms100)} (${V.ms100 ? Math.round(100 * r.ms100 / V.ms100) : 0}%) | ${r.top3} | ${Number.isFinite(r.p50) ? r.p50.toFixed(2) : '-'} |`);
  }
  return L.join('\n');
}

// ---- repro causes (docs/168 §3): the analyze() tables above drop repro-triggered windows as scenario cost, so nothing names
// what a forced repro's own hitches are. Here iterations >= minMs inside the firings (worst-hitches.ts firingWins: arm notes;
// else each repro window, labelled by its step) get the same cause() columns; every other exclusion (first minute, profiler
// edge, sampler, upkeep probe call, 0-step save/load) and an invalid run still leave them out
export type Fire = { a: number; b: number; arm: string };
export function reproFires(wins: Win[], steps: string[] | undefined, rows: { t: number }[]): Fire[] {
  const rw = wins.filter(w => w.why === 'repro-triggered'), fw = firingWins(rw, steps, rows.at(-1)?.t ?? 0);
  return fw.length ? fw.map(({ a, b, arm }) => ({ a, b, arm })) : rw.map(w => ({ a: w.a, b: w.b, arm: w.what }));
}
// sync plans of a ts3native log (no time stamps: joined by duration, as hitch-ab.ts --causes [inferred]): the docs/164
// 'route phases' lines (entry carries IC/reason/job, plus the phase split) when logged, else the 'route async' lines
export function planLines(log: string) {
  const ph = parseSyncLines(log).map(p => ({ ms: p.ms, label: `${p.entry} ${p.res}`, phase: Object.entries(p.phases).filter(([, v]) => v > 0).sort((a, b) => b[1] - a[1]).slice(0, 2).map(([k, v]) => `${k} ${v}`).join(', '), used: false }));
  return ph.length ? ph : [...log.matchAll(/sync plan (\d+) ms, IC (\S+), reason (\S+), entry (\S+?),/g)].map(m => ({ ms: +m[1], label: `IC ${m[2]}/${m[3]}/${m[4]}`, phase: '', used: false }));
}
export const nativeLog = (tag: string) => existsSync(QUEUE) ? readdirSync(QUEUE).filter(f => f.startsWith(`${tag}-`) && /^\d{4}-ts3native\.log$/.test(f.slice(tag.length + 1))).map(f => readFileSync(join(QUEUE, f), 'utf8')).join('\n') : '';
export function reproCauses(r: Pick<Run, 'tag' | 'rows' | 'hitches' | 'frames' | 'ctx'>, fires: Fire[], log = nativeLog(r.tag), minMs = 300, topN = 10) {
  const ctx = r.ctx ?? { wins: baseWins(r.rows), foreground: false, invalid: null, offSrc: 'none' };
  const head = `### Repro causes: ${r.tag}, iterations >= ${minMs} ms inside the firings (scenario cost, NOT natural play; docs/168 §3)`;
  if (ctx.invalid) return `${head}\n\nRUN INVALID (${ctx.invalid}): left out`;
  // the repro's own step commands are agent calls too (calls.jsonl), and so are repro-loop's quiet-wait `ping`s, whose round
  // trip spans the iteration in progress (it waits on the hitch, a no-op on the sim thread): neither excludes here; upkeep calls do
  const own = new Set(ctx.wins.filter(w => w.why === 'repro-triggered').map(w => w.what.split(' ')[0])).add('ping');
  const keep = { wins: ctx.wins.filter(w => w.why !== 'repro-triggered' && !(w.why === 'probe call' && own.has(w.what))), foreground: ctx.foreground }, t0 = r.rows[0]?.t ?? 0;
  const plans = planLines(log), excl = new Map<string, ExclRow>(), arms = new Map<string, R[]>();
  // longest first: each logged plan joins at most one iteration, the closest in duration within max(10 ms, 10%)
  for (const h of r.hitches.filter(x => x.wallMs >= minMs).sort((x, y) => y.wallMs - x.wallMs)) {
    const s0 = h.t - h.wallMs, fire = fires.find(f => f.a <= s0 && s0 < f.b); if (!fire) continue; // by its start, as hitch-ab.ts scenarioRows
    const ex = exclusion({ a: h.t - h.wallMs, b: h.t, wallMs: h.wallMs, steps: h.steps }, keep);
    if (ex) { exclAdd(excl, ex.why === 'repro-triggered' ? 'save/load (0-step >= 1 s)' : ex.why, h.wallMs); continue; }
    const c = cause(h as any, (h.t - t0) / 1000, null), longest = Math.max(0, ...Object.values(h.byEntry ?? {}).map((v: any) => v.ms ?? 0));
    let plan: typeof plans[number] | null = null;
    if (longest >= LOG_MS) for (const p of plans) if (!p.used && Math.abs(p.ms - longest) <= Math.max(10, 0.1 * longest) && (!plan || Math.abs(p.ms - longest) < Math.abs(plan.ms - longest))) plan = p;
    if (plan) plan.used = true;
    const fr = r.frames.filter(f => f.q >= h.t - h.wallMs && f.q <= h.t + 100).sort((x, y) => y.ms - x.ms)[0];
    arms.set(fire.arm, [...(arms.get(fire.arm) ?? []), { h, ...c, plan: plan ? `${plan.ms} ms ${plan.label}${plan.phase ? ` (${plan.phase})` : ''}` : '',
      planKey: plan ? `${plan.label.replace(/ \S+$/, '')}${plan.phase ? ` ${plan.phase.split(' ')[0]}` : ''}` : '',
      frame: fr ? `${Math.round(fr.ms)} ms${fr.svc?.[0] ? `: ${fr.svc[0].name} ${Math.round(fr.svc[0].ms)}` : ''}` : '', svc: fr?.svc?.[0]?.name ?? '' }]);
  }
  const L = [head, '', `Plans joined by duration from ${plans.length} logged sync plans [inferred]; frame = slowest presented frame ending in the iteration (+100 ms) and its longest service.`, '',
    '| Arm | Cause (class) | n | ms | median | max | lead bucket | longest route-job wait | lock holder | sync plan (entry/reason, top phase) | slowest script run | frame service |', `|${'---|'.repeat(12)}`];
  const W = ['', `Worst ${topN} per arm`, '', '| Arm | s | ms | class | lead | wait | lock holder | plans (n/ms) | sync plan | slowest script runs (ms) | frame |', `|${'---|'.repeat(11)}`];
  for (const [arm, ds] of [...arms].sort()) {
    const by = new Map<string, R[]>(); ds.forEach(d => by.set(d.cls, [...(by.get(d.cls) ?? []), d]));
    for (const [cls, xs] of [...by].sort((a, b) => b[1].reduce((s, d) => s + d.h.wallMs, 0) - a[1].reduce((s, d) => s + d.h.wallMs, 0))) {
      // the two most frequent values of a column in this class
      const t = (k: (d: R) => string) => { const m = new Map<string, number>(); xs.forEach(d => { const v = k(d); if (v) m.set(v, (m.get(v) ?? 0) + 1); }); return [...m].sort((a, b) => b[1] - a[1]).slice(0, 2).map(([v, n]) => `${v} (${n})`).join('; '); };
      const ms = xs.map(d => d.h.wallMs as number);
      L.push(`| ${arm} | ${cls} | ${xs.length} | ${Math.round(ms.reduce((s, x) => s + x, 0))} | ${Math.round(nearest(ms, 0.5))} | ${Math.round(Math.max(...ms))} | ${t(d => d.lead)} | ${t(d => d.wait.replace(/ \d+ ms$/, ''))} | ${t(d => d.holder.replace(/^(\w+) waited \d+ ms on /, '$1 on ').replace(/ \d+ from /, ' from '))} | ${t(d => d.planKey)} | ${t(d => d.topTask)} | ${t(d => d.svc)} |`);
    }
    for (const d of ds.slice(0, topN)) W.push(`| ${arm} | ${Math.round((d.h.t - t0) / 1000)} | ${Math.round(d.h.wallMs)} | ${d.cls} | ${d.lead} | ${d.wait} | ${d.holder} | ${d.plans} | ${d.plan} | ${d.tasks} | ${d.frame} |`);
  }
  if (!arms.size) L.push(`| - | no real iteration >= ${minMs} ms inside ${fires.length} firings | | | | | | | | | | |`);
  return [...L, ...W, '', exclTable(excl, `iterations >= ${minMs} ms inside the firings`).replace(` >= ${SIM_MS} ms`, '')].join('\n');
}

const jsonl = (f: string) => existsSync(f) ? readFileSync(f, 'utf8').split('\n').filter(Boolean).flatMap(l => { try { return [JSON.parse(l)]; } catch { return []; } }) : [];
const csv = (f: string) => {
  if (!existsSync(f)) return [] as R[];
  const [head, ...ls] = readFileSync(f, 'utf8').trim().split(/\r?\n/), c = head.split(',');
  // numbers, except text cells (service names, docs/167 §7); `na` and '' stay NaN / 0 as before
  return ls.filter(Boolean).map(l => Object.fromEntries(l.split(',').map((v, i) => [c[i], Number.isNaN(Number(v)) && v !== 'na' ? v : Number(v)])));
};
export function load(tag: string): Run {
  const f = readdirSync(PROFILE).find(n => n.startsWith(`${tag}-`) && /^\d+-freezes\.jsonl$/.test(n.slice(tag.length + 1)));
  if (!f) throw new Error(`no ${tag}-<secs>-freezes.jsonl in ${PROFILE} (profiler older than docs/162, or set TS3_REPO from a worktree)`);
  const base = join(PROFILE, f.replace(/-freezes\.jsonl$/, '')), secs = Number(/-(\d+)-freezes/.exec(f)![1]), rows = csv(`${base}-seconds.csv`);
  const frames = csv(`${base}-frames.csv`).map(x => ({ epoch: x.epochMs, q: x.qpcMs, ms: x.frameMs, fg: x.fg, ...frameStages(x), ...frameServices(x) })).filter(x => Number.isFinite(x.q) && Number.isFinite(x.ms));
  // the frame log stamps epoch and QPC in one call: the exact clock offset for the harness windows
  const ctx = runContext(tag, rows, secs, frames.length ? nearest(frames.map(x => x.epoch - x.q), 0.5) : NaN);
  return { tag, secs, rows, hitches: jsonl(`${base}-hitches.jsonl`), freezes: jsonl(`${base}-freezes.jsonl`), frames, ctx,
    nlog: jsonl(join(PROFILE, `${tag}-nativelog.jsonl`)), managed: jsonl(join(PROFILE, `${tag}-managed.jsonl`)) };
}

function selfCheck() {
  assert.equal(tailPct([60, 400], 1000, 0.99), null); // rank 990 of 1000 lies below the two logged gaps
  assert.equal(tailPct([60, 400], 1000, 0.999), 60); assert.equal(tailPct([60, 400], 1000, 1), 400); assert.equal(tailPct([], 0, 0.99), null);
  const sp = syncPlans([{ t: 5, line: 'route async: sync plan 84 ms, IC Plan, reason Los, entry nojob, route 2579B9B2, caller 41782227 (0 lines rate-limited before)' },
    { t: 5, line: 'route async: script stack of route 2579B9B2 (innermost first): A.B < C.D < E.F < G.H' }]);
  assert.deepEqual(sp, [{ t: 5, ms: 84, ic: 'Plan', reason: 'Los', entry: 'nojob', stack: 'A.B < C.D < E.F' }]);
  // 3 min run: rows every s from QPC 1000; epoch = QPC + 1e6. Freezes: one in the first minute (excluded), a 450 ms route freeze
  // over iterations 200-201 with a sync plan logged 0.6 s later, a 120 ms and a 60 ms gap with no long iteration (render-paced
  // unless ForegroundRender and >= 2 boundaries). Rows 60-179 are real (2.0 min; the last 2 s are the profiler edge)
  const rows = Array.from({ length: 182 }, (_, i) => ({ t: 1000 + i * 1000, iters: 40, publishes: 30 }));
  const run: Run = { tag: 'fx-bp', secs: 180, rows, managed: [],
    freezes: [{ t0: 10_000, t1: 10_900, gapMs: 900, prevGapMs: 30, n: 5, iters: 1 }, { t0: 100_000, t1: 100_450, gapMs: 450, prevGapMs: 33, n: 201, iters: 1 },
      { t0: 120_000, t1: 120_120, gapMs: 120, prevGapMs: 30, n: 300, iters: 2 }, { t0: 130_000, t1: 130_060, gapMs: 60, prevGapMs: 30, n: 400, iters: 0 }],
    hitches: [{ t: 100_430, wallMs: 440, n: 201, steps: 3, threaded: 420, route: 400, rtLock: 10, rtMaxMs: 10, rtMaxKey: '0x00760bff', byEntry: { Route_Plan: { n: 1, ms: 390 } },
        tasks: [{ stage: 'threaded', name: 'ScriptProxy>Sims3.Gameplay.Actors.Sim', ms: 395, to: 'A.DoRoute < B.Run' }] },
      { t: 99_990, wallMs: 20, n: 200 }],
    frames: [...Array.from({ length: 6000 }, (_, i) => ({ epoch: 1_000_000 + 61_000 + i * 16, q: 61_000 + i * 16, ms: 16, fg: 0 })), { epoch: 1_000_000 + 160_000, q: 160_000, ms: 350, fg: 0 }],
    nlog: [{ t: 1_000_000 + 101_050, line: 'route async: sync plan 390 ms, IC Plan, reason NoStart, entry own, route 1, caller 2 (0 lines rate-limited before)' },
      { t: 1_000_000 + 101_050, line: 'route async: sync plan 900 ms, IC Plan, reason Los, entry own, route 1, caller 2 (0 lines rate-limited before)' }] };
  const a = analyze(run, 300);
  assert.equal(a.mins, 2);
  assert.deepEqual(a.freezes, { 100: 5, 300: 5, 500: 0, 1000: 0, 2000: 0 }); // 1 real freeze in 2 min
  assert.equal(a.pubs, 30 * 120 - 2); assert.equal(a.gap.p99, null); assert.equal(a.gap.max, 450); // 1 real logged of 3598: p99 below 50 ms
  assert.deepEqual([...a.excl].map(([k, x]) => [k, x.n, x.ms, x.secs]), [['first minute', 1, 900, 60], ['profiler edge', 0, 0, 2], ['render-paced', 2, 180, 0]]);
  assert.equal(a.frames.max, 350); assert.equal(a.frames.over[300], 5); assert.equal(a.frames.over[50], 5);
  assert.equal(a.vsIter.n, 1); assert.equal(a.vsIter.noLongIter, 0); assert.equal(a.vsIter.multiIter, 0);
  // ForegroundRender=1: the 120 ms gap over 2 publish-less boundaries is sim-side and stays; a sampler window over 95-105 s
  // drops 10 rows and the 450 ms freeze with them
  const fg = analyze({ ...run, ctx: { wins: baseWins(rows), foreground: true, invalid: null, offSrc: 'x' } }, 300);
  assert.deepEqual(fg.freezes, { 100: 10, 300: 5, 500: 0, 1000: 0, 2000: 0 }); assert.equal(fg.excl.get('render-paced')!.n, 1);
  const sm = analyze({ ...run, ctx: { wins: [...baseWins(rows), { a: 95_000, b: 105_000, why: 'sampler', what: 'native' }], foreground: false, invalid: 'noisy machine (x)', offSrc: 'x' } }, 300);
  assert.equal(sm.mins, 110 / 60); assert.deepEqual([sm.excl.get('sampler')!.n, sm.excl.get('sampler')!.secs], [1, 10]); assert.equal(sm.gap.max, 0);
  assert.match(report(sm), /RUN INVALID: noisy machine/);
  assert.equal(sm.frameExcl.get('sampler')!.n, 625); assert.match(report(sm), /excluded frames .*sampler 625 \(62\.5 FPS\)/); // 10 s of 16 ms frames
  assert.equal(a.causes.length, 1); assert.equal(a.causes[0].cls, 'route search'); assert.equal(a.causes[0].lead, 'route (1)');
  assert.equal(a.causes[0].reason, 'NoStart Plan (1)'); // the 900 ms line is longer than the gap: not this freeze's plan
  assert.match(a.causes[0].wait, /^gate 10 goal step/);
  assert.equal(a.causes[0].task, 'ScriptProxy>Sims3.Gameplay.Actors.Sim (1)'); // task attribution (docs/166)
  const txt = report(a);
  assert.match(txt, /p99 < 50 \/ p99\.9 < 50 \/ max 450 ms/); assert.match(txt, /\| route search \| 1 \| 450 \|/); assert.match(txt, /\| ScriptProxy>Sims3\.Gameplay\.Actors\.Sim 395 \(yield A\.DoRoute\) \|/);
  assert.equal(a.stages, null); assert.match(txt, /No stage columns in frames\.csv/);
  // main-loop stages (docs/167): csv rows as frame-times.js writes them; `na` reads as NaN (unhooked: left out). A 500 ms frame
  // in the first minute is excluded; the 350 ms frame is led by the end-frame wait, the 120 ms one by services (100 - its
  // nested 10 ms consume) with TryEnter failed and endMs unhooked
  const row = (o: R) => frameStages({ loadMs: 0, idleMs: 0, svcMs: 0, consumeMs: 0, captureMs: 0, rcbMs: 0, beginMs: 0, drawMs: 0, presentMs: 0, postMs: 0, endMs: 0, otherMs: 0,
    consumes: 1, tryFail: 0, notReady: 0, drains: 1, msgs: 4, ...o });
  assert.deepEqual(frameStages({ frameMs: 16 }), {}); assert.equal(Object.keys(row({ endMs: NaN }).st!).includes('end-frame job wait'), false);
  const sf: Frame[] = [{ epoch: 1_030_000, q: 30_000, ms: 500, fg: 1, ...row({ endMs: 500 }) },
    ...Array.from({ length: 6000 }, (_, i) => ({ epoch: 1_061_000 + i * 16, q: 61_000 + i * 16, ms: 16, fg: 1, ...row({ drawMs: 10, presentMs: 6 }) })),
    { epoch: 1_160_000, q: 160_000, ms: 350, fg: 1, ...row({ svcMs: 20, consumeMs: 5, endMs: 300, presentMs: 5, otherMs: 25, msgs: 40 }) },
    { epoch: 1_158_000, q: 158_000, ms: 120, fg: 1, ...row({ svcMs: 100, consumeMs: 10, otherMs: 10, endMs: NaN, tryFail: 1, drains: 0, msgs: 0 }) }];
  const st = analyze({ ...run, frames: sf }, 300).stages!;
  assert.equal(st.n, 6002); assert.equal(st.slowN, 2);
  const lead = (s: string) => st.rows.find(r => r.stage === s)!.lead;
  assert.deepEqual(lead('end-frame job wait'), { 50: 1, 100: 1, 300: 1 }); assert.deepEqual(lead('services'), { 50: 1, 100: 1, 300: 0 }); assert.deepEqual(lead('draw'), { 50: 0, 100: 0, 300: 0 });
  assert.equal(st.rows.find(r => r.stage === 'services')!.ms100, 105); assert.equal(st.tryFail, 1);
  assert.deepEqual(st.worst.map(w => [w.f.ms, w.lead[0]]), [[350, 'end-frame job wait'], [120, 'services']]);
  const stx = report(analyze({ ...run, frames: sf }, 300));
  assert.match(stx, /\| end-frame job wait \| 1 \| 1 \| 1 \| 300 \(64%\) \| 0\.00 \|/); assert.match(stx, /\| 157 \| 120 \| services \| 90 \| consume 10; other 10 \| 1 \/ 1 \/ 0 \/ 0 \| 1 \| - \|/);
  assert.equal(analyze({ ...run, frames: sf }, 300).services, null); assert.match(stx, /No service columns in frames\.csv/);
  // services by service (docs/167 §7): csv text cells, `na` = not hooked, '' = fewer than 3 services ran
  assert.deepEqual(frameServices({ svc1: 'World/Service', svc1Ms: 80, svc2: 'JobManager', svc2Ms: 5, svc3: 0, svc3Ms: 0 }).svc, [{ name: 'World/Service', ms: 80 }, { name: 'JobManager', ms: 5 }]);
  assert.deepEqual(frameServices({ svc1: NaN, svc1Ms: NaN }), {}); assert.deepEqual(frameServices({ svcMs: 3 }), {});
  const vs = (o: R, svc: R) => ({ ...row(o), ...frameServices(svc) });
  const vf: Frame[] = [...Array.from({ length: 6000 }, (_, i) => ({ epoch: 1_061_000 + i * 16, q: 61_000 + i * 16, ms: 16, fg: 1,
      ...vs({ svcMs: 4, consumeMs: 1, drawMs: 6, presentMs: 6 }, { svc1: 'RenderSystem', svc1Ms: 2, svc2: 'JobManager', svc2Ms: 1, svc3: 'World/Service', svc3Ms: 0.5 }) })),
    { epoch: 1_160_000, q: 160_000, ms: 400, fg: 1, ...vs({ svcMs: 380, consumeMs: 2, otherMs: 20 }, { svc1: 'World/Service', svc1Ms: 300, svc2: 'RenderSystem', svc2Ms: 50, svc3: 'JobManager', svc3Ms: 20 }) },
    { epoch: 1_158_000, q: 158_000, ms: 120, fg: 1, ...vs({ svcMs: 100, otherMs: 20 }, { svc1: 'CAS/SimService', svc1Ms: 90, svc2: 'World/Service', svc2Ms: 5, svc3: '', svc3Ms: 0 }) }];
  const va = analyze({ ...run, frames: vf }, 300), V = va.services!;
  assert.equal(V.n, 6002); assert.equal(V.slowN, 2); assert.equal(V.svc100, 480);
  const vr = (n: string) => V.rows.find(r => r.name === n)!;
  assert.deepEqual(vr('World/Service').lead, { 50: 1, 100: 1, 300: 1 }); assert.deepEqual(vr('CAS/SimService').lead, { 50: 1, 100: 1, 300: 0 });
  assert.equal(vr('World/Service').ms100, 305); assert.equal(vr('World/Service').top3, 2); assert.equal(vr('RenderSystem').p50, 2);
  assert.equal(V.rows[0].name, 'World/Service'); assert.equal(vr('other services (outside top 3, list walk)').ms100, 10 + 5); // 380+2-370, 100-95
  const vtx = report(va);
  assert.match(vtx, /\| World\/Service \| 1 \| 1 \| 1 \| 305 \(59%\) \| 2 \| 0\.50 \|/); assert.match(vtx, /\| 400 \| services \| 378 \| .* \| World\/Service 300; RenderSystem 50; JobManager 20 \|/);
  // repro causes: firing off 55-110 s, on 110-175 s; a repro window at 70-81 s does not exclude, nor does the step's own agent
  // call; the sampler (95-100 s), an upkeep call, the first minute and a 0-step save do; the on route iteration joins the 650 ms
  // phases line (640 ms plan, within 10%)
  const rf = [{ a: 55_000, b: 110_000, arm: 'off' }, { a: 110_000, b: 175_000, arm: 'on' }];
  const rctx = { wins: [...baseWins(rows), { a: 70_000, b: 81_000, why: 'repro-triggered' as const, what: 'stuck_check off' }, { a: 95_000, b: 100_000, why: 'sampler' as const, what: 'native' },
    { a: 74_600, b: 74_900, why: 'probe call' as const, what: 'stuck_check' }, { a: 104_800, b: 105_100, why: 'probe call' as const, what: 'keep_alive' }], foreground: false, invalid: null, offSrc: 'x' };
  const rh = [{ t: 75_000, wallMs: 500, steps: 3, taskObjects: 480, tasks: [{ stage: 'taskObjects', name: 'FunctionTask>X.Stuck', ms: 450 }] }, { t: 97_000, wallMs: 400, steps: 3, taskObjects: 390 },
    { t: 58_000, wallMs: 900, steps: 3 }, { t: 120_000, wallMs: 700, steps: 3, threaded: 680, route: 650, byEntry: { Route_Plan: { n: 1, ms: 640 } } }, { t: 130_000, wallMs: 1500, steps: 0 },
    { t: 40_000, wallMs: 800, steps: 3 }, { t: 105_000, wallMs: 350, steps: 3 }];
  const rlog = 'route phases: sync plan 650 ms failed, entry IC Plan/QueueFull/nojob, route 1, planlocked 1; loops entry=0/0 ground=0/0 prepass=0/0 othercaller=0/0; ms plan=1 cell=500 world=100 other=49\n';
  const rfr = [{ epoch: 0, q: 119_900, ms: 680, fg: 1, svc: [{ name: 'World/Service', ms: 600 }] }];
  const rc = reproCauses({ tag: 'fx', rows, hitches: rh, frames: rfr, ctx: rctx }, rf, rlog);
  const has = (s: string) => assert.ok(rc.includes(s), `repro causes: missing ${s}\n${rc}`);
  has('| off | TaskObjects burst | 1 | 500 | 500 | 500 | script (1) |  |  |  | FunctionTask>X.Stuck (1) |  |');
  has('| on | route search | 1 | 700 | 700 | 700 | route (1) |  |  | IC Plan/QueueFull/nojob cell (1) |  | World/Service (1) |');
  has('| on | 119 | 700 | route search | route |  |  | Plan 1/640 | 650 ms IC Plan/QueueFull/nojob failed (cell 500, world 100) |  | 680 ms: World/Service 600 |');
  has('| first minute | 1 | 900 |'); has('| sampler | 1 | 400 |'); has('| probe call | 1 | 350 |'); has('| save/load (0-step >= 1 s) | 1 | 1500 |');
  assert.match(reproCauses({ tag: 'fx', rows, hitches: rh, frames: [], ctx: { ...rctx, invalid: 'noisy machine (x)' } }, rf, rlog), /RUN INVALID \(noisy machine/);
  assert.deepEqual(planLines('sync plan 84 ms, IC Plan, reason Los, entry nojob, route 1'), [{ ms: 84, label: 'IC Plan/Los/nojob', phase: '', used: false }]);
  // fires: arm notes give one firing per arm note; without step texts every repro window is its own 'arm', named by its step
  const fw = [{ a: 70_000, b: 81_000, why: 'repro-triggered' as const, what: 'a' }, { a: 90_000, b: 101_000, why: 'repro-triggered' as const, what: 'b' }];
  assert.deepEqual(reproFires(fw, ['repro x step a [arm off #1]: {}', 'repro x step b [arm on #2]: {}'], rows), [{ a: 70_000, b: 90_000, arm: 'off' }, { a: 90_000, b: 182_000, arm: 'on' }]);
  assert.deepEqual(reproFires(fw, undefined, rows).map(f => f.arm), ['a', 'b']);
  // setup steps (docs/168 §7): step a is setup until 75 s -> 'off setup' 70-75, its causes in their own rows; the 500 ms
  // TaskObjects iteration at 75 s ends there and lands in 'off setup'
  const sfw = [{ ...fw[0], setupEnd: 75_000 }, fw[1]];
  assert.deepEqual(reproFires(sfw, ['repro x step a [arm off #1] (setup): {}', 'repro x step b [arm on #2]: {}'], rows), [{ a: 70_000, b: 75_000, arm: 'off setup' }, { a: 75_000, b: 90_000, arm: 'off' }, { a: 90_000, b: 182_000, arm: 'on' }]);
  assert.match(reproCauses({ tag: 'fx', rows, hitches: [{ t: 74_000, wallMs: 500, steps: 3, taskObjects: 480 }], frames: [], ctx: rctx }, reproFires(sfw, ['repro x step a [arm off #1]: {}', 'repro x step b [arm on #2]: {}'], rows), rlog), /\| off setup \| TaskObjects burst \| 1 \| 500 \|/);
  console.log('freeze-report self-check ok');
}

if (resolve(process.argv[1] ?? '') === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  if (args.includes('--self-check')) selfCheck();
  else {
    const opt = (k: string, d: string) => { const i = args.indexOf(k); return i < 0 ? d : args[i + 1]; };
    const tags = args.filter((x, i) => !x.startsWith('--') && !['--min', '--top'].includes(args[i - 1]));
    if (!tags.length) { console.error('usage: node tools/harness/freeze-report.ts <tag> [...] [--min 300] [--top 10] | --self-check'); process.exit(2); }
    const min = Number(opt('--min', '300')), top = Number(opt('--top', '10'));
    // --repro-causes: also the causes inside the run's repro firings (the windows the tables above exclude)
    console.log(tags.map(t => { const r = load(t); return report(analyze(r, min), top) + (args.includes('--repro-causes') ? `\n\n${reproCauses(r, reproFires(r.ctx!.wins, reproSteps(t, r.secs), r.rows), undefined, min, top)}` : ''); }).join('\n\n'));
  }
}
