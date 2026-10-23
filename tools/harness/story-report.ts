// Story fuzzer reports (docs/171 §9): every real hitch and visible freeze of a story run joined to the story steps that ran in
// it, NEW causes, coverage, seed replay of a segment and a forced-repro draft. Reads only; never talks to the game.
//   node tools/harness/story-report.ts <tag> [--baseline <tag>[,<tag>...]] [--top 10]
//   node tools/harness/story-report.ts --replay <tag> --from <s> --to <s> [--lead 120]
//   node tools/harness/story-report.ts --draft-repro <tag> <hitch s>
//   node tools/harness/story-report.ts --self-check
// Inputs in probe-out/profile: <tag>-story.jsonl (story-fuzzer.ts, docs/171 §5) plus the profile files freeze-report.ts load()
// reads. Times on the command line and in the tables = s since the story log's first line. Iterations and publish gaps >= 300 ms;
// the real-hitch rule of freeze-report.ts / worst-hitches.ts exclusion(): first minute, profiler edge, sampler and render-paced
// are dropped; probe-call and repro-triggered windows are dropped too unless a story step overlaps (story steps are harness-
// triggered = scenario cost, reported per story, docs/171 §5). Causes = worst-hitches.ts cause() of the longest iteration
// (class / lead bucket, slowest task, lock holder, longest route-job wait), the sync plan logged in it (reason, top phase of the
// docs/164 split line) and the longest service of the slowest presented frame inside (docs/167 §7).
// Set TS3_REPO=<main checkout> when running from a git worktree.
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { load, syncPlans, type Run } from './freeze-report.ts';
import { baseWins, cause, exclusion, holderText, STEP_POST_MS, type RunCtx } from './worst-hitches.ts';
import { keyName } from './hitch-attr-report.ts';
import { parseSyncLines } from './route-phases-report.ts';
import { DAY_TYPES, dayFails, lintDays, TOWN_KEYS } from './story-day.ts';
import { householdReport } from './story-households.ts';

const ROOT = process.env.TS3_REPO ?? join(import.meta.dirname, '..', '..');
const PROFILE = join(ROOT, 'probe-out', 'profile');
const MIN_MS = 300, BIG_MS = 1000, LOOKBACK_MS = 120_000;
const SOFT = new Set<string>(['probe call', 'repro-triggered']);

type R = Record<string, any>;
export type Step = { t: number; seed: number; story: string; run: number; step: number; cmd: string; actor?: string; simId?: unknown;
  target?: unknown; lot?: unknown; ok: boolean; ms: number; error?: string; world?: string; save?: string; args?: R };
export type Line = R;
export const parseStory = (text: string): Line[] => text.split('\n').filter(Boolean).flatMap(l => { try { return [JSON.parse(l)]; } catch { return []; } });
export const storyFile = (tag: string) => join(PROFILE, `${tag}-story.jsonl`);
const isStep = (x: Line): x is Step => !x.kind && typeof x.story === 'string' && Number.isFinite(x.t);
// a step's window (epoch ms): the call and the 10 s after it, the same post window as a repro step (worst-hitches.ts runContext)
export const stepWin = (s: Step) => ({ a: s.t, b: s.t + (s.ms ?? 0) + STEP_POST_MS });
const startOf = (lines: Line[]) => lines.find(x => Number.isFinite(x.t))?.t ?? 0;

export type Facets = { cls: string; lead: string; task: string; holder: string; wait: string; sync: string; phase: string; svc: string };
export type Ev = { kind: 'iteration' | 'freeze'; ms: number; a: number; b: number; s: number; steps: Step[]; f: Facets; tasks: string; holderText: string; plan: string };

// every iteration and publish gap >= 300 ms of the run: dropped by reason, or kept with the story steps whose windows overlap it
// (none = natural play). Parallel stories: one event joins every overlapping step
export function joinRun(r: Run, lines: Line[]) {
  const steps = lines.filter(isStep), t0 = startOf(lines), dropped = new Map<string, number>(), events: Ev[] = [];
  const ctx: RunCtx = r.ctx ?? { wins: baseWins(r.rows), foreground: false, invalid: null, offSrc: 'none' };
  const hard = { wins: ctx.wins.filter(w => !SOFT.has(w.why)), foreground: ctx.foreground };
  // shortcut: epoch - QPC only from frames.csv (every run since docs/167); older runs without it get no story join
  const offs = r.frames.map(f => f.epoch - f.q).sort((a, b) => a - b), off = offs.length ? offs[Math.ceil(offs.length / 2) - 1] : NaN;
  if (Number.isNaN(off)) return { events, dropped, t0, steps, off };
  const plans = syncPlans(r.nlog);
  const phases = r.nlog.flatMap(x => parseSyncLines(x.line).map(p => ({ t: x.t, ms: p.ms, top: Object.entries(p.phases).sort((a, b) => b[1] - a[1])[0]?.[0] ?? '' })));
  // a line logged in the event (stamped within ~1.5 s after the write) no longer than it (freeze-report.ts plan join)
  const inEv = <T extends { t: number; ms: number }>(xs: T[], a: number, b: number, ms: number) =>
    xs.filter(p => p.t >= a + off && p.t <= b + off + 1500 && p.ms <= ms + 50).sort((x, y) => y.ms - x.ms)[0];
  const rowT0 = r.rows[0]?.t ?? 0, drop = (why: string) => dropped.set(why, (dropped.get(why) ?? 0) + 1);
  const add = (kind: Ev['kind'], ms: number, a: number, b: number, x: Parameters<typeof exclusion>[0], h: R | undefined) => {
    const hx = exclusion(x, hard);
    if (hx && !SOFT.has(hx.why)) return drop(hx.why);
    const on = steps.filter(s => { const w = stepWin(s); return w.a < b + off && w.b > a + off; }), sx = exclusion(x, ctx);
    if (!on.length && sx) return drop(sx.why);
    const c = h ? cause(h as any, (h.t - rowT0) / 1000, null) : null, plan = inEv(plans, a, b, ms), ph = inEv(phases, a, b, ms);
    const fr = r.frames.filter(f => f.svc?.length && f.q - f.ms < b && f.q > a).sort((p, q) => q.ms - p.ms)[0];
    const ht = h ? holderText(h as any) : '', hm = /on lock (\S+) held by (\S+)/.exec(ht);
    events.push({ kind, ms, a: a + off, b: b + off, s: (b + off - t0) / 1000, steps: on, tasks: c?.tasks ?? '', holderText: ht, plan: plan ? `${plan.ms} ms ${plan.reason} ${plan.ic} ${plan.stack}` : '',
      f: { cls: c?.cls ?? 'no iteration >= 50 ms', lead: c?.lead ?? '', task: c?.topTask ?? '', holder: hm ? `${hm[1]} held by ${hm[2]}` : '',
        wait: h?.rtMaxKey ? keyName(String(h.rtMaxKey)) : '', sync: plan?.reason ?? '', phase: ph?.top ?? '', svc: fr?.svc?.[0]?.name ?? '' } });
  };
  for (const h of r.hitches) if (h.wallMs >= MIN_MS) add('iteration', h.wallMs, h.t - h.wallMs, h.t, { a: h.t - h.wallMs, b: h.t, wallMs: h.wallMs, steps: h.steps, iter: h as any }, h);
  for (const f of r.freezes) {
    if (f.gapMs < MIN_MS) continue;
    // shortcut: longest iteration by time overlap only (freeze-report.ts also joins by iteration number n); fine at >= 300 ms
    const main = r.hitches.filter(h => h.t - h.wallMs < f.t1 && h.t > f.t0).sort((p, q) => q.wallMs - p.wallMs)[0];
    add('freeze', f.gapMs, f.t0, f.t1, { a: f.t0, b: f.t1, gap: { longestIterMs: main?.wallMs ?? 0, iters: f.iters }, iter: main as any }, main);
  }
  return { events: events.sort((p, q) => p.a - q.a), dropped, t0, steps, off };
}

const top = (xs: string[], n = 2) => { const m = new Map<string, number>(); xs.forEach(v => v && m.set(v, (m.get(v) ?? 0) + 1)); return [...m].sort((a, b) => b[1] - a[1]).slice(0, n).map(([v, k]) => `${v} (${k})`).join('; '); };
const NAT = '(no story step: natural)';
function table(groups: Map<string, Ev[]>, keyHead: string[]) {
  const L = [`| ${keyHead.join(' | ')} | iterations >= ${MIN_MS} | >= ${BIG_MS} | max iteration ms | freezes >= ${MIN_MS} | >= ${BIG_MS} | max gap ms | stage (class / lead) | task | lock holder | route-job wait | sync plan (phase) | frame service |`,
    `|${'---|'.repeat(keyHead.length + 12)}`];
  const rows = [...groups].map(([k, es]) => {
    const it = es.filter(e => e.kind === 'iteration').map(e => e.ms), fz = es.filter(e => e.kind === 'freeze').map(e => e.ms), c = (xs: number[], m: number) => xs.filter(x => x >= m).length;
    return { k, w: es.reduce((s, e) => s + e.ms, 0), cells: [c(it, MIN_MS), c(it, BIG_MS), Math.round(Math.max(0, ...it)), c(fz, MIN_MS), c(fz, BIG_MS), Math.round(Math.max(0, ...fz)),
      top(es.map(e => `${e.f.cls} / ${e.f.lead}`)), top(es.map(e => e.f.task)), top(es.map(e => e.f.holder)), top(es.map(e => e.f.wait)),
      top(es.map(e => e.f.sync && `${e.f.sync}${e.f.phase ? ` (${e.f.phase})` : ''}`)), top(es.map(e => e.f.svc))] };
  }).sort((a, b) => b.w - a.w);
  for (const r of rows) L.push(`| ${r.k} | ${r.cells.join(' | ')} |`);
  return L.join('\n');
}
// by template (one row per story id) and by template + step; an event in parallel stories' windows counts in each
export function byStory(events: Ev[]) {
  const t = new Map<string, Ev[]>(), s = new Map<string, Ev[]>(), put = (m: Map<string, Ev[]>, k: string, e: Ev) => m.set(k, [...(m.get(k) ?? []), e]);
  for (const e of events) {
    if (!e.steps.length) { put(t, NAT, e); put(s, `${NAT} | - | -`, e); continue; }
    for (const id of new Set(e.steps.map(x => x.story))) put(t, id, e);
    for (const k of new Set(e.steps.map(x => `${x.story} | ${x.step} | ${x.cmd}`))) put(s, k, e);
  }
  return { byTemplate: table(t, ['story']), byStep: table(s, ['story', 'step', 'cmd']) };
}

// NEW causes: a facet value the baseline never had. Baseline = given tags (every facet, all their events incl. natural) or the
// docs/161 §3 cause list (class, sync reason, route-job wait key only: that table names no tasks, holders, phases or services)
export type Base = Partial<Record<keyof Facets, Set<string>>>;
export const DOCS161: Base = {
  cls: new Set(['harness step', 'first minute after load', 'route search', 'TaskObjects burst', 'locomotion', 'ThreadedObjects, no route', 'mixed route + script', 'other', 'no iteration >= 50 ms']),
  sync: new Set(['NoStart', 'Foreign', 'M1Pending']),
  wait: new Set(['steering obstacle query', 'UpdateSimFootprint', 'gate 12 LotTable_Contains', 'steering avoidance A', 'ObjectFootprintFromTool', 'gate 12 F-G placement',
    '0x0085f6cb', '0x0075a64c', 'destroy (RtWaitJob)', 'gate 10 goal step (sync plan)', 'gate 0 (transform wrapper)', 'UpdateObjectLot', '0x0085fe81']),
};
export function baseFrom(events: Ev[]): Base {
  const b: Base = {}; for (const e of events) for (const [k, v] of Object.entries(e.f) as [keyof Facets, string][]) if (v) (b[k] ??= new Set()).add(v);
  for (const k of ['cls', 'lead', 'task', 'holder', 'wait', 'sync', 'phase', 'svc'] as (keyof Facets)[]) b[k] ??= new Set();
  return b;
}
export function newCauses(events: Ev[], base: Base, label: string) {
  const m = new Map<string, Ev[]>();
  for (const e of events) for (const [k, set] of Object.entries(base) as [keyof Facets, Set<string>][]) { const v = e.f[k]; if (v && !set.has(v)) m.set(`${k} | ${v}`, [...(m.get(`${k} | ${v}`) ?? []), e]); }
  const L = [`NEW causes (not in ${label}; facets judged: ${Object.keys(base).join(', ')})`, ''];
  if (!m.size) return [...L, 'none'].join('\n');
  L.push('| facet | value | n | max ms | stories | first s |', '|---|---|---|---|---|---|');
  for (const [k, es] of [...m].sort((a, b) => b[1].length - a[1].length))
    L.push(`| ${k} | ${es.length} | ${Math.round(Math.max(...es.map(e => e.ms)))} | ${top(es.flatMap(e => e.steps.length ? [...new Set(e.steps.map(x => x.story))] : [NAT]))} | ${Math.round(es[0].s)} |`);
  return L.join('\n');
}

// coverage from the log: runs and steps per story, skips with their missing needs, prepares
export function coverage(lines: Line[]) {
  const ids = new Set<string>(), g = (id: string) => (ids.add(id), id);
  const runs = new Map<string, Set<number>>(), ok = new Map<string, number>(), bad = new Map<string, number>(), skip = new Map<string, string[]>(), prep = new Map<string, number>();
  const inc = (m: Map<string, number>, k: string) => m.set(k, (m.get(k) ?? 0) + 1);
  for (const x of lines) {
    if (isStep(x)) { g(x.story); (runs.get(x.story) ?? runs.set(x.story, new Set()).get(x.story)!).add(x.run); inc(x.ok ? ok : bad, x.story); }
    else if (x.kind === 'skip' && x.story) { const why = x.why /* story-fuzzer.ts */ ?? x.missing ?? x.needs ?? x.need ?? x.reason ?? '?'; (skip.get(g(x.story)) ?? skip.set(x.story, []).get(x.story)!).push(Array.isArray(why) ? why.map(w => typeof w === 'string' ? w : JSON.stringify(w)).join(', ') : typeof why === 'string' ? why : JSON.stringify(why)); }
    else if (x.kind === 'prepare' && x.story) inc(prep, g(x.story));
  }
  const L = ['| story | runs | steps ok | steps failed | skipped | missing needs | prepared |', '|---|---|---|---|---|---|---|'];
  for (const id of [...ids].sort()) L.push(`| ${id} | ${runs.get(id)?.size ?? 0} | ${ok.get(id) ?? 0} | ${bad.get(id) ?? 0} | ${skip.get(id)?.length ?? 0} | ${top(skip.get(id) ?? [], 3)} | ${prep.get(id) ?? 0} |`);
  return L.join('\n');
}

// day planner section (docs/171 §12): pursuits done vs backlog per day type, attendance, rescues per Sim (flagged when the
// lint calls the schedule unbelievable), deaths; empty when the run had no day planner
export function daysReport(lines: Line[]) {
  const s = lines.filter(x => x.kind === 'days').at(-1);
  if (!s) return '';
  const L = ['### Day plan', '', `${s.simDays} sim days (${s.simHours} sim h), deaths ${s.deaths?.unscripted ?? 0} unscripted / ${s.deaths?.scripted ?? 0} scripted / ${s.deaths?.natural ?? 0} old age (natural). Pass rule: ${dayFails(lines).join('; ') || 'pass'}.`, '',
    '| day type | Sim-days | pursuits planned | done | done per Sim-day | carried | backlog at day end |', '|---|---|---|---|---|---|---|'];
  for (const t of DAY_TYPES) { const b = s.byType?.[t]; if (b?.simDays) L.push(`| ${t} | ${b.simDays} | ${b.planned} | ${b.done} | ${(b.done / b.simDays).toFixed(2)} | ${b.carried} | ${b.backlog} |`); }
  L.push('', '| Sim | work/school days due | attended | rescues | flag |', '|---|---|---|---|---|');
  const names = new Set([...Object.keys(s.attendance ?? {}), ...Object.keys(s.rescues ?? {})]), flags = lintDays(lines);
  for (const n of [...names].sort()) { const a = s.attendance?.[n]; L.push(`| ${n} | ${a?.due?.join(', ') ?? '-'} | ${a?.attended?.join(', ') ?? '-'} | ${s.rescues?.[n] ?? 0} | ${flags.filter(f => f.startsWith(`${n}:`)).length ? 'unbelievable schedule' : ''} |`); }
  const g = lines.filter(x => x.kind === 'guard'), d = lines.filter(x => x.kind === 'death');
  if (g.length) L.push('', 'Rescues: ' + g.map(x => `${x.sim} ${x.danger} day ${x.day} ${Math.round(x.hour * 10) / 10} h${x.ok ? '' : ' (FAILED)'}`).join('; '));
  if (d.length) L.push('', 'Deaths: ' + d.map(x => `${x.sim} (${x.how}, ${x.scripted ? `scripted by ${x.story}` : x.natural ? `old age, natural${x.story ? ` (in ${x.story})` : ''}` : 'UNSCRIPTED'}) day ${x.day}`).join('; '));
  return L.join('\n');
}

// off-camera life (docs/180 §4): story-day's per-day `town` diffs (Sims outside the active household), one row per sim day
export function townReport(lines: Line[]) {
  // pool households (EA's stray pets etc.) are one summary line, not life events; shortcut: lines without `pools` (before
  // ts3agent's town_snapshot pool flag) only know EA's pet pool, by its name
  const pet = (n: string) => /\(PetHousehold\)$/.test(n), keep = (a?: string[]) => (a ?? []).filter(n => !pet(n)), n = (a?: string[]) => (a ?? []).filter(pet).length;
  const t = lines.filter(x => x.kind === 'town'), d = t.filter(x => !x.base && x.ok !== false).map(x => x.pools ? x
    : { ...x, births: keep(x.births), arrivals: keep(x.arrivals), gone: keep(x.gone), pools: { PetHousehold: { arrivals: n(x.births) + n(x.arrivals), gone: n(x.gone) } } });
  if (!t.length) return '';
  const sp = [...new Set(t.map(x => x.sp).filter(Boolean))].join(', ') || 'unknown';
  const L = ['### Off-camera life', '', `EA story progression mode: ${sp}. ${d.length} day diffs of Sims outside the active household (gone = disposed: EA's death and emigration both remove the description).`, '',
    `| day | Sims | ${TOWN_KEYS.join(' | ')} |`, `|${'---|'.repeat(TOWN_KEYS.length + 2)}`];
  for (const x of d) L.push(`| ${x.day} | ${x.sims} | ${TOWN_KEYS.map(k => x[k]?.length ? `${x[k].length}: ${x[k].slice(0, 3).join('; ')}${x[k].length > 3 ? '; ...' : ''}` : '0').join(' | ')} |`);
  const tot = Object.fromEntries(TOWN_KEYS.map(k => [k, d.reduce((a, x) => a + (x[k]?.length ?? 0), 0)]));
  L.push(`| total | | ${TOWN_KEYS.map(k => tot[k]).join(' | ')} |`);
  const pl: Record<string, { arrivals: number; gone: number }> = {};
  for (const x of d) for (const [h, v] of Object.entries<any>(x.pools ?? {})) if (v.arrivals || v.gone) { const a = pl[h] ??= { arrivals: 0, gone: 0 }; a.arrivals += v.arrivals; a.gone += v.gone; }
  if (Object.keys(pl).length) L.push('', `Pool households (EA's stray pet and other pools, not counted above): ${Object.entries(pl).map(([h, v]) => `${h} ${v.arrivals} in / ${v.gone} out`).join('; ')}`);
  const rot = lines.filter(x => x.kind === 'rotate');
  if (rot.length) L.push('', 'Household rotation: ' + rot.map(x => `day ${x.day} ${x.ok ? `-> ${x.household ?? x.to} (${(x.sims ?? []).join(', ')})` : `not done (${x.error})`}`).join('; '));
  return L.join('\n');
}

// sim hour at an epoch time from story-day's `clock` lines (linear between two lines); NaN before the first
export function simHourAt(clocks: Line[], t: number) {
  const i = clocks.findLastIndex(c => c.t <= t);
  if (i < 0) return NaN;
  const a = clocks[i], b = clocks[i + 1], abs = (c: Line) => (c.day - 1) * 24 + c.hour;
  return b && b.t > a.t ? abs(a) + (abs(b) - abs(a)) * (t - a.t) / (b.t - a.t) : abs(a);
}
// EA's StoryProgressionService alarm: once a sim day at 1:00 (ScheduleInfrequent, docs/180 §2); its 4-10 actions yield
// between actions, so the run ends within minutes of sim time. Window = 1:00-1:30 [inferred], or a script task named for it
export const SP_HOUR = 1, SP_WIN_H = 0.5;
export function spHitches(events: Ev[], lines: Line[]) {
  const clocks = lines.filter(x => x.kind === 'clock').sort((p, q) => p.t - q.t);
  return events.filter(e => {
    if (/StoryProgression/i.test(`${e.tasks} ${e.f.task}`)) return true;
    const h = simHourAt(clocks, e.a) % 24;
    return h >= SP_HOUR && h < SP_HOUR + SP_WIN_H;
  });
}

export function report(tag: string, r: Run, lines: Line[], base: Base = DOCS161, baseLabel = 'the docs/161 §3 baseline cause list', topN = 10) {
  const j = joinRun(r, lines), b = byStory(j.events), census = lines.find(x => x.kind === 'census'), dr = daysReport(lines), tr = townReport(lines);
  const L = [`## ${tag}: hitches by story (docs/171 §9)`, ''];
  if (r.ctx?.invalid) L.push(`**RUN INVALID: ${r.ctx.invalid}** (read only, not a timing result)`, '');
  if (Number.isNaN(j.off)) return [...L, 'No frames.csv: no epoch-QPC offset, story join skipped.', '', '### Coverage', '', coverage(lines), ...(dr ? ['', dr] : []), ...(tr ? ['', tr] : []), ...(householdReport(lines) ? ['', householdReport(lines)] : [])].join('\n');
  L.push(`Story log: ${j.steps.length} steps, seed ${[...new Set(j.steps.map(s => s.seed))].join(', ') || '-'}, world ${census?.world ?? j.steps[0]?.world ?? '-'}, save ${j.steps[0]?.save ?? '-'}. ` +
    `Kept ${j.events.length} events >= ${MIN_MS} ms (${j.events.filter(e => e.steps.length).length} in story step windows: call + ${STEP_POST_MS / 1000} s); dropped ${[...j.dropped].map(([k, n]) => `${k} ${n}`).join(', ') || 'none'}.`, '');
  L.push('### By story template', '', b.byTemplate, '', '### By story step', '', b.byStep, '', `### ${newCauses(j.events, base, baseLabel)}`, '');
  L.push(`### Worst ${topN}`, '', '| s | kind | ms | stories (step cmd actor -> target @ lot) | cause | slowest script runs | sync plan |', '|---|---|---|---|---|---|---|');
  for (const e of [...j.events].sort((p, q) => q.ms - p.ms).slice(0, topN))
    L.push(`| ${Math.round(e.s)} | ${e.kind} | ${Math.round(e.ms)} | ${e.steps.map(s => `${s.story}#${s.run} ${s.step} ${s.cmd} ${s.actor ?? ''} -> ${fmt(s.target)} @ ${fmt(s.lot)}`).join('; ') || NAT} | ${e.f.cls} / ${e.f.lead}${e.f.wait ? `, wait ${e.f.wait}` : ''}${e.f.holder ? `, ${e.f.holder}` : ''}${e.f.svc ? `, ${e.f.svc}` : ''} | ${e.tasks} | ${e.plan}${e.f.phase ? ` (${e.f.phase})` : ''} |`);
  const hh = householdReport(lines, j.events);
  if (hh) L.push('', hh);
  L.push('', '### Coverage', '', coverage(lines));
  if (dr) L.push('', dr);
  if (tr) {
    const sp = spHitches(j.events, lines), n = (m: number) => sp.filter(e => e.ms >= m).length;
    L.push('', tr, '', `Hitches attributed to story progression (sim time ${SP_HOUR}:00-${SP_HOUR}:${SP_WIN_H * 60} or a StoryProgression script task): ${n(MIN_MS)} >= ${MIN_MS} ms, ${n(BIG_MS)} >= ${BIG_MS} ms, max ${Math.round(Math.max(0, ...sp.map(e => e.ms)))} ms${sp.length ? ` (${sp.slice(0, 5).map(e => `${Math.round(e.s)} s ${Math.round(e.ms)} ms ${e.f.cls}`).join('; ')})` : ''}.`);
  }
  return L.join('\n');
}
const fmt = (x: unknown) => x === undefined || x === null ? '-' : typeof x === 'string' || typeof x === 'number' ? String(x) : JSON.stringify(x);

// story-smoke pass rule (queue.ts, queues/story-smoke.json): the log exists, stories ran with >= 1 ok step, and the join had an
// epoch-QPC offset (frames.csv). Crash and exit code are queue.ts's own checks. [] = pass
export function smokeFails(r: Run | null, lines: Line[]): string[] {
  if (!lines.length) return ['no story log (<tag>-story.jsonl missing or empty)'];
  const steps = lines.filter(isStep), f: string[] = [];
  if (!steps.length) f.push('no story step ran');
  else if (!steps.some(s => s.ok)) f.push(`none of ${steps.length} story steps ok`);
  if (!r) f.push('no profile files: story-report join not run');
  else if (Number.isNaN(joinRun(r, lines).off)) f.push('no frames.csv: story-report could not join hitches');
  return f;
}

// --replay: the seed and the steps of [from, to] s, plus a queue job that replays them through story-fuzzer.ts (docs/171 §9:
// same seed, the scheduler runs from the start and executes only steps in [from - lead, to])
export function replay(tag: string, lines: Line[], from: number, to: number, lead = 120) {
  const t0 = startOf(lines), steps = lines.filter(isStep), seeds = [...new Set(steps.map(s => s.seed))];
  const seg = steps.filter(s => (s.t - t0) / 1000 >= from - lead && (s.t - t0) / 1000 <= to), save = steps[0]?.save;
  const start = Math.max(0, from - lead), job = { id: `story-replay-${tag}-${from}-${to}`,
    note: `Replay of ${tag} story steps ${start}-${to} s (hitch window ${from}-${to} s, lead ${lead} s) from seed ${seeds.join(',')} (story-report.ts --replay, docs/171 §9). Set asi to the build ${tag} ran; scratch save copy (docs/171 safety).`,
    asi: 'tested', save, run: { profile: { minutes: Math.ceil((to - start + 90) / 60), story: { seed: seeds[0], replay: tag, from: start, to } } } };
  const L = [`seed ${seeds.join(', ') || '-'}, save ${save ?? '-'}, world ${steps[0]?.world ?? '-'}; ${seg.length} steps in ${start}-${to} s (lead ${lead} s before ${from})`, '',
    '| s | story | run | step | cmd | actor | target | lot | ok | ms |', '|---|---|---|---|---|---|---|---|---|---|',
    ...seg.map(s => `| ${Math.round((s.t - t0) / 1000)} | ${s.story} | ${s.run} | ${s.step} | ${s.cmd} | ${s.actor ?? '-'} | ${fmt(s.target)} | ${fmt(s.lot)} | ${s.ok} | ${s.ms} |`), '',
    `command: node tools/harness/story-fuzzer.ts --seed ${seeds[0]} --replay ${tag} --from ${start} --to ${to}`, '', 'queue job:', JSON.stringify(job, null, 2)];
  return { seg, job, text: L.join('\n') };
}

// --draft-repro: a repros.json entry (docs/169 loop form) from the story steps of the 120 s before the event nearest <hitch s>
export function draftRepro(tag: string, lines: Line[], events: Ev[], hitchS: number) {
  const e = [...events].sort((p, q) => Math.abs(p.s - hitchS) - Math.abs(q.s - hitchS))[0];
  if (!e || Math.abs(e.s - hitchS) > 5) throw new Error(`no hitch or freeze >= ${MIN_MS} ms within 5 s of ${hitchS} s in ${tag} (times = s since the story log's first line)`);
  const t0 = startOf(lines), runs = new Set(e.steps.map(s => `${s.story}#${s.run}`));
  const pre = lines.filter(isStep).filter(s => s.t <= e.b && s.t >= e.a - LOOKBACK_MS && (!runs.size || runs.has(`${s.story}#${s.run}`))).sort((p, q) => p.t - q.t);
  const lead = e.steps[0] ?? pre.at(-1), clean = (o: R) => Object.fromEntries(Object.entries(o).filter(([, v]) => v !== undefined && v !== null));
  const steps = pre.map((s, i) => clean({ cmd: s.cmd === 'interact' ? 'do_interaction' : s.cmd, args: s.args ?? clean({ sim: s.actor, target: s.target, lot: s.lot }),
    waitS: pre[i + 1] ? Math.max(1, Math.round((pre[i + 1].t - s.t) / 1000)) : 20,
    note: `story ${s.story} run ${s.run} step ${s.step} at ${Math.round((s.t - t0) / 1000)} s${s.ok ? '' : ` (failed in the run: ${s.error ?? '?'})`}${s.cmd === 'any_interaction' ? '; TODO pin the interaction the seed picked' : ''}` }));
  return {
    id: `story-${lead?.story ?? 'natural'}-${lead?.step ?? 0}-draft`,
    bug: `TODO (draft): ${Math.round(e.ms)} ms ${e.kind} at ${Math.round(e.s)} s of ${tag}${lead ? `, story ${lead.story} run ${lead.run} step ${lead.step} (${lead.cmd})` : ', no story step'}; ` +
      `cause ${e.f.cls} / ${e.f.lead}${e.f.task ? `, task ${e.f.task}` : ''}${e.f.holder ? `, ${e.f.holder}` : ''}${e.f.wait ? `, wait ${e.f.wait}` : ''}${e.f.sync ? `, sync plan ${e.f.sync}${e.f.phase ? ` (${e.f.phase})` : ''}` : ''}${e.f.svc ? `, service ${e.f.svc}` : ''}`,
    fix: 'TODO: the switch under test (arm step below)',
    save: lead?.save ?? pre[0]?.save ?? 'TODO',
    note: `DRAFT, UNVERIFIED: story-report.ts --draft-repro ${tag} ${hitchS} (docs/171 §9), seed ${lead?.seed ?? '-'}. Steps = the story steps of the ${LOOKBACK_MS / 1000} s before the hitch, as logged (check args: pie-menu label, object ids). ` +
      'Shortcut (docs/169): once the cause is known, replace them with the direct trigger (one agent command or reflect_call) and re-create the state each firing; stop on a fixed event count per arm (until.perArm), not the clock.',
    steps: [{ cmd: 'time', waitS: 75, note: 'lead-in: the first 60 s of the window are the load minute (excluded)' }, { cmd: 'set_speed', args: { speed: 'normal' }, waitS: 2 }],
    loop: { steps: [{ cmd: 'patches', args: { TODO_switch: '$on' }, waitS: 1, note: 'arm TODO: name the switch under test' }, ...steps], arms: ['off', 'on', 'on', 'off'], until: { perArm: 10 }, maxMinutes: 20 },
    expect: 'TODO: the off arm repeats the hitch >= 300 ms in most firings; the on arm does not',
  };
}

function selfCheck() {
  // 3 min run: rows every s from QPC 1000, epoch = QPC + 1e6 (frames.csv). Story log from epoch 1_069_000 (s 0): kitchen-fire run 1
  // start_fire at 31 s and interact at 43 s, party run 2 interact at 32 s, a skip and a prepare line
  const E = 1_000_000, txt = [{ kind: 'census', t: E + 69_000, world: 'Bridgeport', save: 'Story-BP' },
    { kind: 'prepare', t: E + 69_500, story: 'kitchen-fire', need: 'object stove' },
    { t: E + 100_000, seed: 7, story: 'kitchen-fire', run: 1, step: 0, cmd: 'start_fire', actor: 'Ann', target: 'stove', lot: 12, ok: true, ms: 500, world: 'Bridgeport', save: 'Story-BP' },
    { t: E + 101_000, seed: 7, story: 'party', run: 2, step: 0, cmd: 'interact', actor: 'Bob', target: 'Ann', lot: 12, ok: true, ms: 100, args: { sim: 'Bob', target: 'Ann', label: 'Chat' }, save: 'Story-BP' },
    { t: E + 112_000, seed: 7, story: 'kitchen-fire', run: 1, step: 1, cmd: 'any_interaction', actor: 'Ann', target: 'fire', lot: 12, ok: false, ms: 200, error: 'none enabled', save: 'Story-BP' },
    { kind: 'skip', t: E + 113_000, story: 'vampire-bite', missing: ['ep:LateNight'] }].map(x => JSON.stringify(x)).join('\n') + '\nnot json\n';
  const lines = parseStory(txt);
  assert.equal(lines.length, 6);
  const rows = Array.from({ length: 182 }, (_, i) => ({ t: 1000 + i * 1000, iters: 40, publishes: 30 }));
  const ctx: RunCtx = { wins: [...baseWins(rows), { a: 104_000, b: 105_500, why: 'probe call', what: 'story call' }, { a: 160_000, b: 161_000, why: 'probe call', what: 'keep_alive' }], foreground: false, invalid: null, offSrc: 'x' };
  const run: Run = { tag: 'fx-story', secs: 180, rows, managed: [], ctx,
    hitches: [{ t: 105_000, wallMs: 1200, n: 1, steps: 3, route: 1100, threaded: 1150 }, // route search in the fire + party windows and a story probe call
      { t: 150_000, wallMs: 400, n: 2, steps: 3, threaded: 350, tasks: [{ stage: 'threaded', name: 'ScriptProxy>Sims3.Foo', ms: 340 }] }, // natural
      { t: 160_500, wallMs: 350, n: 3, steps: 3, threaded: 330 }, // non-story probe call: dropped
      { t: 30_000, wallMs: 2000, n: 4, steps: 3, threaded: 1900 }, // first minute: dropped
      { t: 115_000, wallMs: 320, n: 5, steps: 3, rtLock: 300, rtMaxKey: '0x00999999', rtMaxMs: 300, holders: [{ who: 'sim', ms: 300, lock: '+0x918', by: 'job', tid: 9, site: '0x00831568' }] }],
    freezes: [{ t0: 103_700, t1: 105_300, gapMs: 1600, prevGapMs: 30, n: 1, iters: 1 }, { t0: 140_000, t1: 140_400, gapMs: 400, prevGapMs: 30, n: 9, iters: 0 }],
    frames: [...Array.from({ length: 50 }, (_, i) => ({ epoch: E + 61_000 + i * 16, q: 61_000 + i * 16, ms: 16, fg: 0 })), { epoch: E + 105_300, q: 105_300, ms: 1250, fg: 0, svc: [{ name: 'World/Service', ms: 40 }] }],
    nlog: [{ t: E + 105_600, line: 'route async: sync plan 1100 ms, IC Plan, reason Weird, entry own, route 1, caller 2 (0 lines rate-limited before)' },
      { t: E + 105_600, line: 'route phases: sync plan 1100 ms failed, entry own, route 1, planlocked 1; loops entry=1/5; ms plan=10 world=900 cell=50' }] };
  const j = joinRun(run, lines);
  assert.equal(j.off, E);
  assert.deepEqual([...j.dropped].sort(), [['first minute', 1], ['probe call', 1], ['render-paced', 1]]);
  assert.deepEqual(j.events.map(e => [e.kind, e.ms, e.steps.map(s => `${s.story}${s.step}`).join(',')]),
    [['freeze', 1600, 'kitchen-fire0,party0'], ['iteration', 1200, 'kitchen-fire0,party0'], ['iteration', 320, 'kitchen-fire1'], ['iteration', 400, '']]);
  const big = j.events[1].f;
  assert.deepEqual([big.cls, big.lead, big.sync, big.phase, big.svc], ['route search', 'route', 'Weird', 'world', 'World/Service']);
  assert.equal(j.events[0].f.cls, 'route search'); // freeze -> its longest iteration
  assert.equal(j.events[2].f.holder, '+0x918 held by job'); assert.equal(j.events[2].f.wait, '0x00999999'); assert.equal(j.events[3].f.task, 'ScriptProxy>Sims3.Foo');
  assert.equal(Math.round(j.events[2].s), 46);
  const b = byStory(j.events);
  assert.match(b.byTemplate, /\| kitchen-fire \| 2 \| 1 \| 1200 \| 1 \| 1 \| 1600 \| route search \/ route \(2\); other \/ waits \(1\) \|/);
  assert.match(b.byStep, /\| party \| 0 \| interact \| 1 \| 1 \| 1200 \| 1 \| 1 \| 1600 \|/);
  assert.match(b.byStep, /\| \(no story step: natural\) \| - \| - \| 1 \| 0 \| 400 \|/);
  assert.match(b.byTemplate, /Weird \(world\) \(2\)/);
  const nc = newCauses(j.events, DOCS161, 'docs/161');
  assert.match(nc, /\| sync \| Weird \| 2 \| 1600 \| kitchen-fire \(2\); party \(2\) \|/); assert.match(nc, /\| wait \| 0x00999999 \| 1 \| 320 \|/); assert.doesNotMatch(nc, /\| cls \|/);
  assert.match(newCauses(j.events, baseFrom(j.events), 'self'), /none$/);
  const cov = coverage(lines);
  assert.match(cov, /\| kitchen-fire \| 1 \| 1 \| 1 \| 0 \| {2}\| 1 \|/); assert.match(cov, /\| vampire-bite \| 0 \| 0 \| 0 \| 1 \| ep:LateNight \(1\) \| 0 \|/);
  const rp = replay('fx-story', lines, 30, 45, 0);
  assert.deepEqual(rp.seg.map(s => s.step + s.story), ['0kitchen-fire', '0party', '1kitchen-fire']);
  assert.deepEqual(rp.job.run.profile, { minutes: 2, story: { seed: 7, replay: 'fx-story', from: 30, to: 45 } }); assert.equal(rp.job.save, 'Story-BP');
  assert.match(rp.text, /story-fuzzer\.ts --seed 7 --replay fx-story --from 30 --to 45/);
  const d = draftRepro('fx-story', lines, j.events, 46);
  assert.equal(d.id, 'story-kitchen-fire-1-draft'); assert.equal(d.save, 'Story-BP');
  assert.deepEqual(d.loop.steps.map(s => s.cmd), ['patches', 'start_fire', 'any_interaction']); assert.deepEqual(d.loop.arms, ['off', 'on', 'on', 'off']);
  assert.deepEqual(d.loop.steps[1], { cmd: 'start_fire', args: { sim: 'Ann', target: 'stove', lot: 12 }, waitS: 12, note: 'story kitchen-fire run 1 step 0 at 31 s' });
  assert.match(d.loop.steps[2].note, /failed in the run: none enabled.*TODO pin/); assert.match(d.bug, /320 ms iteration at 46 s .*wait 0x00999999/);
  assert.equal(draftRepro('fx-story', lines, j.events, 37.5).loop.steps.length, 3); // the 1600 ms freeze (s 36.3): fire step 0 and party step 0
  assert.throws(() => draftRepro('fx-story', lines, j.events, 90), /no hitch or freeze/);
  const txtR = report('fx-story', run, lines);
  assert.match(txtR, /Kept 4 events >= 300 ms \(3 in story step windows/); assert.match(txtR, /\| 36 \| freeze \| 1600 \| kitchen-fire#1 0 start_fire Ann -> stove @ 12; party#2 0 interact Bob -> Ann @ 12 \|/);
  assert.match(report('fx-story', { ...run, frames: [] }, lines), /story join skipped/);
  assert.deepEqual(smokeFails(run, lines), []);
  assert.deepEqual(smokeFails({ ...run, frames: [] }, lines), ['no frames.csv: story-report could not join hitches']);
  assert.deepEqual(smokeFails(run, []), ['no story log (<tag>-story.jsonl missing or empty)']);
  assert.deepEqual(smokeFails(null, lines.filter(l => l.kind)), ['no story step ran', 'no profile files: story-report join not run']);
  assert.deepEqual(smokeFails(run, lines.map(l => l.kind ? l : { ...l, ok: false })), ['none of 3 story steps ok']);
  // day planner section (docs/171 §12)
  assert.equal(daysReport(lines), '');
  const dl = [...lines, ...Array.from({ length: 7 }, () => ({ kind: 'guard', sim: 'Ann', danger: 'starve', day: 1, hour: 12, ok: true })), { kind: 'death', sim: 'Bob', how: 'dying', scripted: false, day: 2 },
    { kind: 'days', simDays: 2, simHours: 40, deaths: { scripted: 0, unscripted: 1 }, attendance: { Ann: { due: [1], attended: [1] } }, rescues: { Ann: 7 }, byType: { workday: { simDays: 2, planned: 2, done: 1, carried: 1, backlog: 6 }, weekend: { simDays: 1, planned: 3, done: 3, carried: 0, backlog: 4 } } }];
  const dr = daysReport(dl);
  assert.match(dr, /\| workday \| 2 \| 2 \| 1 \| 0\.50 \| 1 \| 6 \|/); assert.match(dr, /\| Ann \| 1 \| 1 \| 7 \| unbelievable schedule \|/);
  assert.match(dr, /1 unscripted death/); assert.match(dr, /Bob \(dying, UNSCRIPTED\)/); assert.match(report('fx-story', run, dl), /### Day plan/);
  // docs/180: off-camera table and story progression hitches (clock 0:54 at 60 s .. 1:36 at 200 s: the 105 s hitch is ~1:08)
  const tl = [...lines, { kind: 'clock', t: E + 60_000, day: 2, hour: 0.9 }, { kind: 'clock', t: E + 200_000, day: 2, hour: 1.6 },
    { kind: 'town', t: E + 61_000, day: 1, base: true, sp: 'Enabled', sims: 40 },
    { kind: 'town', t: E + 62_000, day: 2, sp: 'Enabled', sims: 41, births: ['Kid (Hart)'], arrivals: [], deaths: [], gone: ['Old (Alto)'], moves: [], jobs: ['A: none -> Law'], promotions: [], marriages: ['A + B'], partners: [] },
    { kind: 'rotate', t: E + 63_000, day: 2, ok: true, household: 'Hart', sims: ['Kid', 'C'] },
    { kind: 'town', t: E + 64_000, day: 3, sp: 'Enabled', sims: 41, births: [], arrivals: ['Piper Dog (PetHousehold)'], deaths: [], gone: ['Coco Horse (PetHousehold)', 'Fred Cat (PetHousehold)'], moves: [], jobs: [], promotions: [], marriages: [], partners: [] },
    { kind: 'town', t: E + 65_000, day: 4, sp: 'Enabled', sims: 41, births: [], arrivals: [], deaths: [], gone: [], moves: [], jobs: [], promotions: [], marriages: [], partners: [], pools: { PetHousehold: { arrivals: 2, gone: 0 } } }];
  const tr = report('fx-story', run, tl);
  assert.match(tr, /### Off-camera life/); assert.match(tr, /EA story progression mode: Enabled/); assert.match(tr, /\| 2 \| 41 \| 1: Kid \(Hart\) \| 0 \| 0 \| 1: Old \(Alto\)/);
  assert.match(tr, /day 2 -> Hart \(Kid, C\)/); assert.match(tr, /\| 3 \| 41 \| 0 \| 0 \| 0 \| 0 \|/, 'pet pool not in the table'); assert.match(tr, /Pool households .*PetHousehold 3 in \/ 2 out/); assert.match(tr, /story progression .*: [1-9]\d* >= 300 ms/);
  assert.equal(simHourAt(tl.filter(x => x.kind === 'clock'), E + 130_000), 25.25);
  console.log('story-report self-check ok');
}

if (resolve(process.argv[1] ?? '') === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2), opt = (k: string) => { const i = args.indexOf(k); return i < 0 ? undefined : args[i + 1]; };
  const storyOf = (tag: string) => { const f = storyFile(tag); if (!existsSync(f)) throw new Error(`no ${f} (set TS3_REPO from a worktree)`); return parseStory(readFileSync(f, 'utf8')); };
  if (args.includes('--self-check')) selfCheck();
  else if (opt('--replay')) {
    const tag = opt('--replay')!, from = Number(opt('--from')), to = Number(opt('--to'));
    if (!Number.isFinite(from) || !Number.isFinite(to)) { console.error('usage: --replay <tag> --from <s> --to <s> [--lead 120]'); process.exit(2); }
    console.log(replay(tag, storyOf(tag), from, to, Number(opt('--lead') ?? 120)).text);
  } else if (opt('--draft-repro')) {
    const tag = opt('--draft-repro')!, s = Number(args[args.indexOf('--draft-repro') + 2]), lines = storyOf(tag);
    if (!Number.isFinite(s)) { console.error('usage: --draft-repro <tag> <hitch s>'); process.exit(2); }
    console.log(JSON.stringify(draftRepro(tag, lines, joinRun(load(tag), lines).events, s), null, 2));
  } else {
    const tag = args.find((x, i) => !x.startsWith('--') && !['--baseline', '--top'].includes(args[i - 1]));
    if (!tag) { console.error('usage: node tools/harness/story-report.ts <tag> [--baseline <tag>[,<tag>]] [--top 10] | --replay <tag> --from <s> --to <s> | --draft-repro <tag> <hitch s> | --self-check'); process.exit(2); }
    const bt = opt('--baseline')?.split(',').filter(Boolean);
    // a baseline run without a story log: all its events are natural (empty log, so the join keeps only real ones)
    const base = bt ? baseFrom(bt.flatMap(t => joinRun(load(t), existsSync(storyFile(t)) ? storyOf(t) : []).events)) : DOCS161;
    console.log(report(tag, load(tag), storyOf(tag), base, bt ? `baseline ${bt.join(', ')}` : undefined, Number(opt('--top') ?? 10)));
  }
}
