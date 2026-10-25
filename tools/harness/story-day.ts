// Story fuzzer day planner (docs/171 §12): every driven Sim lives a seeded day around the game's own schedule while the template
// stories run only in its free blocks; a death guard keeps Sims alive unless a death template owns them.
//   node tools/harness/story-day.ts            self-check (planner, backlog carry-over, guard decisions, pass rule, fake run)
// Story config: "days": true | {tickS: 5, nightSkip: true, workPursuits: 1, freePursuits: 3}. Every tick one story_day_state
// call reads the clock and every driven Sim (StoryHandlers.cs), then per Sim:
//   - day type (dayType) from the game's schedule: vacation (GameUtils.IsOnVacation), workday / schoolday (career or school
//     IsWorkDay and not IsDayOff), retired (RetiredCareer, no job), weekend (Saturday/Sunday), else dayoff
//   - a plan per sim day (planDay, seeded from (seed, Sim, day)): sleep by age, work or school [start - 0.5 h, finish] (the game's
//     carpool / bus and its own work alarm take the Sim; go_to_work only when it is late), homework after school, pursuits
//     from the Sim's backlog in free gaps (workday 1, free day 3), the rest free for stories
//   - backlog (buildBacklog): lifetime wish skill, promised wish skills, top unmaxed skills, a seeded hobby; at day end the
//     planned pursuits not done move to the front (carried), done ones to the back
//   - needs: a motive under its threshold outside work/school/sleep -> sim_fix_need (the game's BuffAutoSolve search), 1 sim h
//     cooldown per motive
//   - death guard (guardDecide) on every Sim not owned by a death template -> death_rescue {kind}; every rescue logged
// Log lines (story.jsonl): dayplan, block, work (go_to_work), pursuit, need, guard, death, and a final days summary;
// dayFails = the story-days pass rule, lintDays = the believability rules for day logs (story-lint.ts).
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

export type DayType = 'workday' | 'schoolday' | 'dayoff' | 'weekend' | 'vacation' | 'retired';
export const DAY_TYPES: DayType[] = ['workday', 'schoolday', 'dayoff', 'weekend', 'vacation', 'retired'];
export const WORK_DAYS: DayType[] = ['workday', 'schoolday'];
export type Job = { name: string; school?: boolean; start: number; finish: number; work_day: boolean; day_off: boolean; should_be_at_work: boolean; at_work: boolean; in_rabbit_hole?: boolean; hours_until_work?: number; carpool?: string };
export type Guard = { swimming?: boolean; buffs?: string[]; unlucky?: boolean; interaction?: string | null; aging?: boolean; days_to_age?: number; dying?: boolean; dead?: boolean; ghost?: boolean; death_type?: string };
export type DayState = { sim: string; id: string; age: string; job?: Job; school?: Job; occupation?: string; retired?: boolean; motives?: Record<string, number>; skills?: { skill: string; level: number; max: number }[]; ltw?: { name: string; skill?: string | null }; wishes?: { name: string; skill?: string | null; points?: number }[]; guard?: Guard };
export type Clock = { day: string; hour: number; day_number: number; vacation?: boolean; aging?: boolean };
export type Block = { kind: 'sleep' | 'work' | 'school' | 'homework' | 'pursuit' | 'free'; a: number; b: number; skill?: string };
export type Pursuit = { skill: string; why: 'ltw' | 'wish' | 'skill' | 'hobby' };
// town (default on): one town_snapshot per calendar day, diffed into a `town` line (off-camera life, docs/180 §4);
// rotate: N = hand the game to another household every N calendar days through the game's own switch (docs/180 §5), 0 = off
export type DaysConfig = { tickS?: number; nightSkip?: boolean; workPursuits?: number; freePursuits?: number; town?: boolean; rotate?: number };
type R = { next: () => number; int: (a: number, b: number) => number; pick: <T>(xs: T[]) => T | undefined };

// off-camera changes between two town snapshots (docs/180 §4): Sims outside the active household (in either snapshot).
// births = new Baby; arrivals = new non-Baby (immigration, a new townie); gone = vanished (EA's kill and emigrate both
// dispose the description, so death and leaving town look alike here); deaths = dead flag set; moves = household or home lot
// changed; jobs = job name changed; promotions = same job, level up; marriages = married set; partners = new partner;
// pools = EA's pool households (town_snapshot `pool`: stray pets, aliens, mermaids, travellers) churn by household, not life
// events (story-days 2026-10-23: PetHousehold 48 arrivals / 48 gone in 7 days)
export type TownSim = { id: string; name: string; age: string; household?: string | null; household_name?: string; lot?: string | null; partner?: string; married?: boolean; job?: string; level?: number; dead?: string; pool?: boolean };
export type TownSnap = { day_number: number; hour?: number; active_household?: string | null; story_progression?: string; sims: TownSim[] };
export const TOWN_KEYS = ['births', 'arrivals', 'deaths', 'gone', 'moves', 'jobs', 'promotions', 'marriages', 'partners'] as const;
export function townDiff(prev: TownSnap, cur: TownSnap) {
  const P = new Map(prev.sims.map(s => [s.id, s])), C = new Map(cur.sims.map(s => [s.id, s]));
  const off = (s?: TownSim) => !s || (s.household ?? null) !== (cur.active_household ?? null) && (s.household ?? null) !== (prev.active_household ?? null);
  const out: Record<(typeof TOWN_KEYS)[number], string[]> = Object.fromEntries(TOWN_KEYS.map(k => [k, []])) as any;
  const pools: Record<string, { arrivals: number; gone: number }> = {}, tally = (s: TownSim, k: 'arrivals' | 'gone') => (pools[s.household_name ?? '-'] ??= { arrivals: 0, gone: 0 })[k]++;
  for (const c of cur.sims) {
    const p = P.get(c.id);
    if (!off(c) || p && !off(p)) continue;
    if (c.pool) { if (!p) tally(c, 'arrivals'); continue; } // a pet adopted out of the pool is a move (not skipped)
    if (!p) { out[c.age === 'Baby' ? 'births' : 'arrivals'].push(`${c.name} (${c.household_name ?? '-'})`); continue; }
    if (c.dead && !p.dead) out.deaths.push(`${c.name} (${c.dead})`);
    if (p.household !== c.household || p.lot !== c.lot) out.moves.push(`${c.name}: ${p.household_name ?? '-'} @ ${p.lot ?? 'no lot'} -> ${c.household_name ?? '-'} @ ${c.lot ?? 'no lot'}`);
    if ((p.job ?? '') !== (c.job ?? '')) out.jobs.push(`${c.name}: ${p.job ?? 'none'} -> ${c.job ?? 'none'}`);
    else if (c.job && (c.level ?? 0) > (p.level ?? 0)) out.promotions.push(`${c.name}: ${c.job} ${p.level} -> ${c.level}`);
    if (c.married && !p.married) out.marriages.push(`${c.name} + ${C.get(c.partner!)?.name ?? c.partner}`);
    else if (c.partner && c.partner !== p.partner && !c.married) out.partners.push(`${c.name} + ${C.get(c.partner)?.name ?? c.partner}`);
  }
  for (const p of prev.sims) if (!C.has(p.id) && off(p)) p.pool ? tally(p, 'gone') : out.gone.push(`${p.name} (${p.household_name ?? '-'})`);
  // a couple is one marriage, not two
  out.marriages = [...new Set(out.marriages.map(m => m.split(' + ').sort().join(' + ')))];
  out.partners = [...new Set(out.partners.map(m => m.split(' + ').sort().join(' + ')))];
  return { ...out, pools };
}

const KIDS = ['Child', 'Teen'], NO_PLAN = ['Baby', 'Toddler'];
export function dayType(s: DayState, c: Clock): DayType {
  if (c.vacation) return 'vacation';
  for (const [j, t] of [[s.job, 'workday'], [s.school, 'schoolday']] as const) if (j && j.work_day && !j.day_off) return t;
  if (!s.job && s.retired) return 'retired';
  return /^(Saturday|Sunday)$/.test(c.day) ? 'weekend' : 'dayoff';
}

// hobbies by age (TS3 play knowledge [community], SkillNames enum names [verified: sim_pursue parses them]); sim_pursue fails
// cleanly when the game says the Sim cannot learn one, and the pursuit counts as not done
const HOBBIES: Record<string, string[]> = {
  kid: ['Logic', 'Painting', 'Guitar', 'Athletic'],
  adult: ['Painting', 'Guitar', 'Logic', 'Cooking', 'Gardening', 'Fishing', 'Writing', 'Athletic', 'Handiness', 'Charisma'],
};
// a hobby the home lot has no object for: the object bought for it (catalog queries the templates' object prepares use);
// Charisma (mirror), Gardening (plot + seeds), Fishing (pond) and Photography (player-aimed camera) are never bought.
// shortcut: 'instrument' may buy a piano or drums, the next census decides; upgrade to a guitar query once one is verified live
export const BUY: Record<string, string> = { Painting: 'easel', Logic: 'chess', Guitar: 'instrument', Athletic: 'gym', Writing: 'computer', Cooking: 'stove' };
export const MAX_BUYS = 2; // pursuit buys per run (each one a Buy-mode visit)
// the day's pursuit list from the practice census (sim_practice, docs/171 §16 #8): only skills the home lot supports, topped
// up with supported hobbies to the day type's cap so a free day can fill its 3 blocks; can = undefined (census failed) = as is
export function practicable(backlog: Pursuit[], can: Set<string> | undefined, age: string, cap: number): Pursuit[] {
  if (!can) return backlog;
  const out = backlog.filter(p => can.has(p.skill));
  for (const h of (KIDS.includes(age) ? HOBBIES.kid : HOBBIES.adult).filter(h => can.has(h) && !backlog.some(p => p.skill === h))) {
    if (out.length >= cap) break;
    const p: Pursuit = { skill: h, why: 'hobby' };
    backlog.push(p); out.push(p);
  }
  return out;
}
export function buildBacklog(s: DayState, r: R): Pursuit[] {
  const out: Pursuit[] = [], add = (skill: string | null | undefined, why: Pursuit['why']) => { if (skill && !out.some(p => p.skill === skill)) out.push({ skill, why }); };
  add(s.ltw?.skill, 'ltw');
  for (const w of [...(s.wishes ?? [])].sort((a, b) => (b.points ?? 0) - (a.points ?? 0))) add(w.skill, 'wish');
  for (const k of [...(s.skills ?? [])].filter(k => k.level < k.max).sort((a, b) => b.level - a.level).slice(0, 2)) add(k.skill, 'skill');
  const pool = (KIDS.includes(s.age) ? HOBBIES.kid : HOBBIES.adult).filter(h => !out.some(p => p.skill === h));
  add(r.pick(pool), 'hobby');
  return out;
}

// the plan of one calendar day (hours 0-24): sleep by age, work/school, homework, pursuits from the backlog front, free
// hour: when the plan is made; an overnight shift still running then (the game's ShouldBeAtWork) is a work block [0, finish]
export function planDay(s: DayState, type: DayType, backlog: Pursuit[], r: R, cfg: DaysConfig = {}, hour = 0): { blocks: Block[]; planned: string[] } {
  const kid = KIDS.includes(s.age), j = type === 'workday' ? s.job : type === 'schoolday' ? s.school : undefined;
  const carry = s.job && s.job.finish < s.job.start && s.job.should_be_at_work && hour < s.job.finish ? s.job.finish : 0;
  let wake = (kid ? 6.5 : 7) + r.int(-1, 1) * 0.5, bed = (s.age === 'Child' ? 20.5 : kid ? 22 : s.age === 'Elder' ? 22 : 23) + r.int(-1, 1) * 0.5;
  const fixed: Block[] = [];
  if (j && j.start >= 0 && j.finish >= 0) {
    const a = Math.max(0, j.start - 0.5), b = j.finish > j.start ? j.finish : 24; // shortcut: an overnight shift ends at midnight in this day's plan
    wake = Math.min(wake, Math.max(0, a - 1));
    if (b >= bed) bed = Math.min(24, b + 1);
    fixed.push({ kind: type === 'schoolday' ? 'school' : 'work', a, b });
    if (type === 'schoolday' && b + 1.5 <= bed) fixed.push({ kind: 'homework', a: b + 0.5, b: b + 1.5, skill: 'Homework' });
  }
  // last night's shift: work until it ends, then sleep up to 6 h (before tonight's shift)
  if (carry) { fixed.push({ kind: 'work', a: 0, b: carry }); wake = Math.max(wake, Math.min(carry + 6, j ? Math.max(carry, j.start - 1.5) : 24)); }
  fixed.push({ kind: 'sleep', a: carry, b: wake }, { kind: 'sleep', a: bed, b: 24 });
  fixed.sort((p, q) => p.a - q.a);
  // free gaps, then pursuits (2 h each, 30 min free before) from the backlog front, up to the day type's cap
  const gaps: Block[] = [];
  let at = 0;
  for (const f of fixed) { if (f.a > at) gaps.push({ kind: 'free', a: at, b: f.a }); at = Math.max(at, f.b); }
  if (at < 24) gaps.push({ kind: 'free', a: at, b: 24 });
  const cap = WORK_DAYS.includes(type) ? cfg.workPursuits ?? 1 : cfg.freePursuits ?? 3;
  const planned: string[] = [], blocks: Block[] = [...fixed.filter(f => f.b > f.a)];
  const queue = backlog.slice();
  for (const g of gaps) {
    let x = g.a;
    while (planned.length < cap && queue.length && g.b - x >= 2.5) {
      const p = queue.shift()!;
      blocks.push({ kind: 'free', a: x, b: x + 0.5 }, { kind: 'pursuit', a: x + 0.5, b: x + 2.5, skill: p.skill });
      planned.push(p.skill); x += 2.5;
    }
    if (g.b > x) blocks.push({ kind: 'free', a: Math.min(x, g.b), b: g.b });
  }
  return { blocks: blocks.filter(b => b.b > b.a).sort((p, q) => p.a - q.a), planned };
}
export const blockAt = (blocks: Block[], h: number) => blocks.find(b => h >= b.a && h < b.b) ?? { kind: 'free', a: h, b: h } as Block;

// day end: planned but not done -> backlog front (carried, in plan order); done (planned or a stand-in) -> back (hobbies
// repeat); failed (the game found no object for it, or the Sim cannot learn it) -> the very back, so one skill without an
// object does not hold the front every day (story-days 2026-10-23: 89 of 128 planned pursuits carried)
export function carryOver(backlog: Pursuit[], planned: string[], done: Set<string>, failed: Set<string> = new Set()): { backlog: Pursuit[]; carried: number } {
  const by = (sk: string) => backlog.find(p => p.skill === sk)!;
  const notDone = planned.filter(sk => !done.has(sk) && !failed.has(sk)).map(by), didIt = backlog.filter(p => done.has(p.skill));
  const bad = backlog.filter(p => failed.has(p.skill) && !done.has(p.skill));
  const rest = backlog.filter(p => !planned.includes(p.skill) && !done.has(p.skill) && !failed.has(p.skill));
  return { backlog: [...notDone, ...rest, ...didIt, ...bad], carried: notDone.length };
}

// death guard: what would kill this Sim soon (thresholds [inferred]: the game kills at the motive floor -100, BuffDrowning /
// BuffFatigued timeouts in a pool, BuffOnFire timeout, a failed repair on a Singed or Unlucky Sim, an elder's last aging day)
export type GuardKind = 'starve' | 'drown' | 'burn' | 'electrocution' | 'freeze' | 'old_age';
export function guardDecide(s: DayState, c: Clock): { kind: GuardKind; why: string }[] {
  const g = s.guard ?? {}, m = s.motives ?? {}, buffs = new Set(g.buffs ?? []), out: { kind: GuardKind; why: string }[] = [];
  if (g.dead || g.ghost || g.dying) return out; // too late: KillSim runs at MaxDeath priority (logged as a death, not rescued)
  const low = (k: string, v: number) => m[k] !== undefined && m[k] <= v;
  if (g.swimming && (buffs.has('Drowning') || buffs.has('Fatigued') || low('Energy', -70) || low('Hunger', -70))) out.push({ kind: 'drown', why: `swimming${buffs.has('Drowning') ? ' Drowning' : ''}${buffs.has('Fatigued') ? ' Fatigued' : ''} energy ${m.Energy} hunger ${m.Hunger}` });
  else if (low('Hunger', -85) || low('VampireThirst', -85) || (buffs.has('Starving') && low('Hunger', -70))) out.push({ kind: 'starve', why: `hunger ${m.Hunger}${m.VampireThirst !== undefined ? ` thirst ${m.VampireThirst}` : ''}` });
  if (buffs.has('OnFire')) out.push({ kind: 'burn', why: 'OnFire' });
  if ((buffs.has('SingedElectricity') || g.unlucky) && /repair|upgrade/i.test(g.interaction ?? '')) out.push({ kind: 'electrocution', why: `${buffs.has('SingedElectricity') ? 'Singed' : 'Unlucky'} in ${g.interaction}` });
  if (low('Temperature', -85)) out.push({ kind: 'freeze', why: `temperature ${m.Temperature}` });
  if (s.age === 'Elder' && c.aging && g.aging && (g.days_to_age ?? Infinity) <= 1) out.push({ kind: 'old_age', why: `${g.days_to_age} days to the elder's end` });
  return out;
}

const NEEDS: [string, number][] = [['Hunger', -30], ['Bladder', -40], ['Energy', -40], ['Hygiene', -50], ['Fun', -50], ['Social', -50]];

export type DaySim = { id: string; name: string; age: string; busy?: string; blocked?: string };
type IO = { call: (cmd: string, args?: object, to?: number) => Promise<any>; sleep: (ms: number) => Promise<unknown>; now: () => number; timeUp: () => boolean; log: (l: object) => void; print: (l: string) => void };
// rng factory as story-fuzzer.ts rng (passed in: no import cycle at load time)
export async function runDays(io: IO, o: { cfg: DaysConfig; driven: DaySim[]; deathOwned: Map<string, string>; seed: number; rng: (seed: number, k: number) => R; ctx: object; ready?: () => void; rotate?: (day: number) => Promise<boolean>; buy?: (query: string) => Promise<unknown> }) {
  const { cfg, driven, deathOwned, ctx } = o, tickMs = (cfg.tickS ?? 5) * 1000;
  type St = { day: number; type?: DayType; can?: Set<string>; blocks: Block[]; planned: string[]; done: Set<string>; failed: Set<string>; tried: Set<string>; oldAgeDay?: number; backlog?: Pursuit[]; entered: Set<string>; lastFix: Record<string, number>; workTries: number; dueDays: Set<number>; attended: Set<number>; rescues: number; dead?: boolean; ghostAtStart?: boolean; block?: string; name: string; since?: number };
  const st = new Map<string, St>();
  const byType: Record<string, { simDays: number; planned: number; done: number; carried: number; backlog: number }> = Object.fromEntries(DAY_TYPES.map(t => [t, { simDays: 0, planned: 0, done: 0, carried: 0, backlog: 0 }]));
  const lastGuard = new Map<string, number>(), days = new Set<number>(), bought = new Set<string>();
  let firstAbs = NaN, lastAbs = NaN, skipping = false, deaths = { scripted: 0, unscripted: 0, natural: 0 }, ticks = 0, lastClock = NaN, rotations = 0;
  let town: TownSnap | null = null, rotateDay: number | undefined;
  const closeDay = (s: DaySim, x: St) => {
    if (!x.type || !x.backlog) return;
    const b = byType[x.type], co = carryOver(x.backlog, x.planned, x.done, x.failed);
    b.simDays++; b.planned += x.planned.length; b.done += x.done.size; b.carried += co.carried; b.backlog += co.backlog.length;
    x.backlog = co.backlog;
    io.log({ kind: 'dayend', t: io.now(), sim: s.name, simId: s.id, day: x.day, type: x.type, planned: x.planned, done: [...x.done], failed: [...x.failed], carried: co.carried, backlog: co.backlog.map(p => p.skill), ...ctx });
  };
  const act = async (cmd: string, args: object, line: object) => {
    const at = io.now(), r = await io.call(cmd, args, 20000);
    // line carries the kind; work beats a story (the game queues work after the story's interactions anyway)
    io.log({ t: at, ms: io.now() - at, cmd, args, ok: !!r?.ok, ...(r?.ok ? { result: r.result } : { error: String(r?.error ?? 'no answer').slice(0, 300) }), ...line, ...ctx });
    return r;
  };
  while (!io.timeUp()) {
    const sims = driven.filter(s => !NO_PLAN.includes(s.age));
    const r = await io.call('story_day_state', { sims: sims.map(s => s.name) }, 20000);
    ticks++;
    if (!r?.ok) { io.log({ kind: 'day', t: io.now(), cmd: 'story_day_state', ok: false, error: String(r?.error ?? 'no answer').slice(0, 300), ...ctx }); o.ready?.(); await io.sleep(tickMs); continue; }
    const c: Clock = r.result, abs = (c.day_number - 1) * 24 + c.hour;
    if (Number.isNaN(firstAbs)) firstAbs = abs;
    lastAbs = abs;
    // clock line per sim hour (story-report maps hitches to sim time: story progression's 1 AM alarm, docs/180 §4)
    if (Math.floor(abs) !== lastClock) { lastClock = Math.floor(abs); io.log({ kind: 'clock', t: io.now(), day: c.day_number, hour: c.hour, ...ctx }); }
    if (!days.has(c.day_number)) {
      days.add(c.day_number);
      // off-camera life: one snapshot per calendar day, diffed against the last
      if (cfg.town !== false) {
        const at = io.now(), s = await io.call('town_snapshot', {}, 30000);
        if (s?.ok && Array.isArray(s.result?.sims)) {
          const snap: TownSnap = s.result;
          if (town) io.log({ kind: 'town', t: at, ms: io.now() - at, day: c.day_number, from: town.day_number, sp: snap.story_progression ?? null, sims: snap.sims.length, ...townDiff(town, snap), ...ctx });
          else io.log({ kind: 'town', t: at, ms: io.now() - at, day: c.day_number, base: true, sp: snap.story_progression ?? null, sims: snap.sims.length, ...ctx });
          town = snap;
        } else io.log({ kind: 'town', t: at, ok: false, day: c.day_number, error: String(s?.error ?? 'no answer').slice(0, 300), ...ctx });
      }
      // household rotation between sim days (never mid-story: the engine's rotate waits for running stories), docs/180 §5
      if (cfg.rotate && o.rotate && c.day_number >= (rotateDay ??= c.day_number + cfg.rotate)) {
        const before = [...driven];
        if (await o.rotate(c.day_number)) {
          rotateDay = c.day_number + cfg.rotate; rotations++;
          for (const s of before) if (!driven.includes(s)) { const x = st.get(s.id); if (x && !x.dead) closeDay(s, x); if (x) x.since = undefined; s.blocked = undefined; }
          continue; // re-read the clock with the new household
        } else rotateDay = c.day_number + 1; // try again the next day
      }
    }
    let allAway = sims.length > 0;
    for (const d of (r.result.sims ?? []) as DayState[]) {
      const s = sims.find(x => x.id === String(d.id) || x.name === d.sim);
      if (!s) continue;
      let x = st.get(s.id);
      if (!x) st.set(s.id, x = { name: s.name, day: -1, blocks: [], planned: [], done: new Set(), failed: new Set(), tried: new Set(), entered: new Set(), lastFix: {}, workTries: 0, dueDays: new Set(), attended: new Set(), rescues: 0, ghostAtStart: !!d.guard?.ghost });
      x.since ??= abs; // first tick of this stint in the driven household (reset when a rotation hands the Sim off)
      // deaths: once per Sim; scripted = a death template owns the Sim; natural = old age (R-LIFE-1: an elder's daily chance
      // once aging is on is ordinary play, docs/171 §16), not a failure of the run
      const g = d.guard ?? {};
      if (!x.dead && (g.dead || g.dying || (g.ghost && !x.ghostAtStart))) {
        x.dead = true; const owner = deathOwned.get(s.id), natural = !owner && g.death_type === 'OldAge';
        owner ? deaths.scripted++ : natural ? deaths.natural++ : deaths.unscripted++;
        io.log({ kind: 'death', t: io.now(), sim: s.name, simId: s.id, scripted: !!owner, natural, story: owner ?? s.busy ?? null, how: g.dying ? 'dying' : g.dead ? 'dead' : 'ghost', cause: g.death_type ?? null, day: c.day_number, hour: c.hour, ...ctx });
      }
      if (x.dead) continue;
      // guard (not for a death template's Sim), one rescue per Sim and kind per 30 s; old age only while a story runs on the
      // elder (the death would end that story) and once per sim day (the game's daily roll re-adds the alarm: story-days
      // 2026-10-23 rescued Hyacinth Atkins 32 times on day 18)
      if (!deathOwned.has(s.id)) for (const k of guardDecide(d, c)) {
        const key = `${s.id}:${k.kind}`;
        if (k.kind === 'old_age' && (!s.busy || x.oldAgeDay === c.day_number)) continue;
        if (io.now() - (lastGuard.get(key) ?? -Infinity) < 30000) continue;
        lastGuard.set(key, io.now()); x.rescues++;
        if (k.kind === 'old_age') x.oldAgeDay = c.day_number;
        await act('death_rescue', { sim: s.name, kind: k.kind }, { kind: 'guard', sim: s.name, simId: s.id, danger: k.kind, why: k.why, day: c.day_number, hour: c.hour });
      }
      // a new calendar day: close the old, plan the new
      if (x.day !== c.day_number) {
        closeDay(s, x);
        const rr = o.rng(o.seed, 100000 + c.day_number * 997 + Number(String(s.id).replace(/\D/g, '').slice(-6) || 0));
        x.backlog ??= buildBacklog(d, rr);
        x.type = dayType(d, c);
        // practice census (docs/171 §16 #8): which backlog skills and hobbies the home lot has an object for; the backlog's
        // first unsupported skill with a BUY query is bought (paid, Buy mode) when the run has buys left, then counted again
        const census = async () => {
          const pc = await io.call('sim_practice', { sim: s.name, skills: [...new Set([...x!.backlog!.map(p => p.skill), ...(KIDS.includes(d.age) ? HOBBIES.kid : HOBBIES.adult)])].filter(k => k !== 'Homework') }, 20000);
          return pc?.ok ? new Set(Object.keys(pc.result?.skills ?? {})) : undefined;
        };
        x.can = await census();
        const miss = x.can && o.buy && bought.size < MAX_BUYS ? x.backlog.find(p => !x!.can!.has(p.skill) && BUY[p.skill] && !bought.has(p.skill)) : undefined;
        if (miss) {
          bought.add(miss.skill);
          const at = io.now(), b = await o.buy!(BUY[miss.skill]).then(() => null, (e: Error) => String(e?.message ?? e).slice(0, 300));
          io.log({ kind: 'buy', t: at, ms: io.now() - at, sim: s.name, simId: s.id, skill: miss.skill, query: BUY[miss.skill], ok: !b, ...(b && { error: b }), day: c.day_number, ...ctx });
          if (!b) x.can = await census();
        }
        const cap = WORK_DAYS.includes(x.type) ? cfg.workPursuits ?? 1 : cfg.freePursuits ?? 3;
        Object.assign(x, { day: c.day_number, ...planDay(d, x.type, practicable(x.backlog, x.can, d.age, cap), rr, cfg, c.hour), done: new Set(), failed: new Set(), tried: new Set(), entered: new Set(), workTries: 0 });
        io.log({ kind: 'dayplan', t: io.now(), sim: s.name, simId: s.id, age: d.age, day: c.day_number, weekday: c.day, type: x.type, blocks: x.blocks, planned: x.planned, backlog: x.backlog.map(p => `${p.skill}:${p.why}`), job: d.job?.name ?? d.school?.name ?? d.occupation ?? null, ...ctx });
      }
      const b = blockAt(x.blocks, c.hour), bk = `${b.kind}@${b.a}`;
      if (x.block !== bk) { x.block = bk; io.log({ kind: 'block', t: io.now(), sim: s.name, simId: s.id, block: b.kind, a: b.a, b: b.b, ...(b.skill && { skill: b.skill }), day: c.day_number, hour: c.hour, ...ctx }); }
      s.blocked = b.kind === 'free' ? undefined : `day:${b.kind}`;
      if (!['sleep', 'work', 'school'].includes(b.kind)) allAway = false;
      // work / school: attendance from the game's own state; go_to_work only when late (the carpool and alarm come first)
      const j = b.kind === 'school' ? d.school : b.kind === 'work' ? d.job : undefined;
      if (j && j.should_be_at_work) {
        // an overnight shift belongs to the day it started (its after-midnight part is yesterday's shift)
        const night = j.finish < j.start && c.hour < j.finish, shiftDay = night ? c.day_number - 1 : c.day_number;
        // due only when the planner had the Sim by the carpool (1 h before the shift): story-days 2026-10-23 rotated to
        // Caroline Custard at 12:15, her 13:00 shift's carpool had left at 12:00, and she was counted as missing work
        const owned = x.since! <= (shiftDay - 1) * 24 + j.start - 1;
        if (owned) x.dueDays.add(shiftDay);
        if (owned && (j.at_work || j.in_rabbit_hole)) x.attended.add(shiftDay);
        else if ((night || c.hour >= j.start + 0.25) && x.workTries < 3 && !x.entered.has(`late${Math.floor(c.hour * 2)}`)) {
          x.entered.add(`late${Math.floor(c.hour * 2)}`); x.workTries++;
          await act('go_to_work', { sim: s.name, ...(b.kind === 'school' && { school: true }) }, { kind: 'work', sim: s.name, simId: s.id, day: c.day_number, hour: c.hour });
        }
      }
      if (s.busy) continue; // in a story: the story owns the Sim now
      if (!x.entered.has(bk)) {
        x.entered.add(bk);
        if (b.kind === 'sleep' && (d.motives?.Energy ?? 100) < 60) await act('sim_fix_need', { sim: s.name, motive: 'Energy' }, { kind: 'need', sim: s.name, simId: s.id, motive: 'Energy', why: 'bedtime', day: c.day_number, hour: c.hour });
      }
      // pursuits and homework: the earliest one of today not tried yet whose block has begun, in a free, pursuit or homework
      // block; so one missed while the Sim was in a story, or skipped between ticks, runs late the same day (story-days
      // 2026-10-23: 55 of 128 planned pursuits were never tried). A pursuit the game finds nothing for: up to 2 other backlog
      // skills not planned today (a player picks another hobby when the house has no object for one)
      const due = ['free', 'pursuit', 'homework'].includes(b.kind) ? x.blocks.find(p => (p.kind === 'pursuit' || p.kind === 'homework') && p.skill && p.a <= c.hour && !x.tried.has(`${p.kind}@${p.a}`)) : undefined;
      if (due) {
        x.tried.add(`${due.kind}@${due.a}`);
        const pursue = (skill: string, instead?: string) => act('sim_pursue', { sim: s.name, skill }, { kind: 'pursuit', sim: s.name, simId: s.id, skill, type: x.type, day: c.day_number, hour: c.hour, ...(due.b <= c.hour && { late: true }), ...(instead && { instead }) });
        let pr = await pursue(due.skill!);
        if (due.kind === 'pursuit') {
          if (pr?.ok) x.done.add(due.skill!);
          else {
            x.failed.add(due.skill!);
            for (const alt of (x.backlog ?? []).filter(p => !x.planned.includes(p.skill) && !x.done.has(p.skill) && !x.failed.has(p.skill) && (!x.can || x.can.has(p.skill))).slice(0, 2)) {
              pr = await pursue(alt.skill, due.skill);
              if (pr?.ok) { x.done.add(alt.skill); break; }
              x.failed.add(alt.skill);
            }
          }
        }
      }
      if (b.kind === 'free' || b.kind === 'pursuit' || b.kind === 'homework') for (const [m, th] of NEEDS) {
        const v = d.motives?.[m];
        if (v === undefined || v > th || abs - (x.lastFix[m] ?? -Infinity) < 1) continue;
        x.lastFix[m] = abs;
        await act('sim_fix_need', { sim: s.name, motive: m }, { kind: 'need', sim: s.name, simId: s.id, motive: m, value: v, day: c.day_number, hour: c.hour });
        break; // one need per Sim per tick
      }
    }
    // night skip: every planned Sim asleep or away -> the game's own Skip speed (as its auto speed for sleep/rabbit holes)
    if (cfg.nightSkip !== false && allAway !== skipping) {
      skipping = allAway;
      await act('set_speed', { speed: skipping ? 'skip' : 'ultra' }, { kind: 'between', why: skipping ? 'all asleep or away' : 'someone awake' });
    }
    o.ready?.(); // the first tick has blocked the Sims at work, school or asleep: stories may start now
    await io.sleep(tickMs);
  }
  for (const s of driven) { const x = st.get(s.id); if (x && !x.dead) closeDay(s, x); s.blocked = undefined; }
  const attendance = Object.fromEntries([...st].filter(([, x]) => x.dueDays.size).map(([, x]) => [x.name, { due: [...x.dueDays], attended: [...x.attended] }]));
  const rescues = Object.fromEntries([...st].filter(([, x]) => x.rescues).map(([, x]) => [x.name, x.rescues]));
  const summary = { kind: 'days', t: io.now(), ticks, simDays: days.size, simHours: Number.isNaN(firstAbs) ? 0 : Math.round((lastAbs - firstAbs) * 10) / 10, byType, attendance, rescues, deaths, rotations, ...ctx };
  io.log(summary);
  io.print(`days: ${summary.simDays} sim days (${summary.simHours} h), deaths ${deaths.unscripted} unscripted / ${deaths.scripted} scripted / ${deaths.natural} old age, rescues ${Object.values(rescues).reduce((a, b) => a + b, 0)}, household rotations ${rotations}`);
  for (const t of DAY_TYPES) if (byType[t].simDays) io.print(`  ${t.padEnd(10)} ${byType[t].simDays} sim-days, pursuits planned ${byType[t].planned} done ${byType[t].done} carried ${byType[t].carried}, backlog at day end ${byType[t].backlog}`);
  return summary;
}

// pursuits done per Sim-day on free days vs work/school days, from the days summary
export const perDay = (bt: Record<string, { simDays: number; done: number }>, types: string[]) => {
  const d = types.reduce((a, t) => a + (bt[t]?.simDays ?? 0), 0);
  return { simDays: d, done: types.reduce((a, t) => a + (bt[t]?.done ?? 0), 0), mean: d ? types.reduce((a, t) => a + (bt[t]?.done ?? 0), 0) / d : NaN };
};
const FREE: DayType[] = ['dayoff', 'weekend', 'vacation', 'retired'];
// story-days pass rule (queue.ts): the run spans >= 2 sim days, 0 unscripted deaths, every Sim with a due work/school day was
// at work that day, pursuits per Sim-day on free days > on work/school days. [] = pass
export function dayFails(lines: any[], minDays = 2): string[] {
  const s = lines.filter(l => l.kind === 'days').at(-1);
  if (!s) return ['no days summary (day planner did not run or the run stopped early)'];
  const f: string[] = [];
  if (s.simDays < minDays) f.push(`only ${s.simDays} sim day(s) seen (${s.simHours} sim h); need ${minDays}`);
  if (s.deaths?.unscripted) f.push(`${s.deaths.unscripted} unscripted death(s): ${lines.filter(l => l.kind === 'death' && !l.scripted).map(l => `${l.sim} (${l.how})`).join(', ')}`);
  for (const [sim, a] of Object.entries<any>(s.attendance ?? {})) { const miss = a.due.filter((d: number) => !a.attended.includes(d)); if (miss.length) f.push(`${sim} missed work/school on day ${miss.join(', ')}`); }
  const w = perDay(s.byType, WORK_DAYS), fr = perDay(s.byType, FREE);
  if (!w.simDays || !fr.simDays) f.push(`pursuit comparison needs both: ${w.simDays} work/school Sim-days, ${fr.simDays} free Sim-days`);
  else if (!(fr.mean > w.mean)) f.push(`pursuits per Sim-day: free days ${fr.mean.toFixed(2)} not > work/school days ${w.mean.toFixed(2)}`);
  return f;
}

// believability of a day log (story-lint.ts): many rescues = an unbelievable schedule; a story starting on a Sim that the day
// plan has at work, school or asleep
export const RESCUES_PER_DAY = 3;
export function lintDays(lines: any[]): string[] {
  const f: string[] = [], s = lines.filter(l => l.kind === 'days').at(-1), days = Math.max(1, s?.simDays ?? 1);
  const per = new Map<string, number>();
  for (const l of lines) if (l.kind === 'guard') per.set(l.sim, (per.get(l.sim) ?? 0) + 1);
  for (const [sim, n] of per) if (n / days >= RESCUES_PER_DAY) f.push(`${sim}: ${n} death-guard rescues in ${days} sim day(s) (>= ${RESCUES_PER_DAY} a day: unbelievable schedule)`);
  const blocks = lines.filter(l => l.kind === 'block').sort((a, b) => a.t - b.t);
  for (const st of lines.filter(l => !l.kind && l.step === 0 && l.actor)) {
    // shortcut: step 0's call goes out after list_interactions and target picks, so a block that began within 15 s of it
    // may have begun after the story took the Sim; the scheduler logs no separate start line
    const b = blocks.filter(x => x.sim === st.actor && x.t <= st.t).at(-1);
    if (b && st.t - b.t >= 15000 && ['work', 'school', 'sleep'].includes(b.block)) f.push(`${st.story}#${st.run} started on ${st.actor} during the day plan's ${b.block} block`);
  }
  return f;
}

// ---- fake game for the day planner: wraps story-fuzzer.ts fakeGame (dry run, self-checks). Sim clock = Friday 06:00 + rate sim
// minutes per virtual second; YA/Adult Sims with an even id digit work 9-17 Mon-Fri, Child/Teen go to school 8-15 Mon-Fri,
// Elders retired; every 4th worker rides the carpool by itself, the rest need go_to_work; motives fall and fixes refill them;
// one injected danger per kind on a schedule (fire, starving, drowning, singed repair, freeze) for the guard checks
export function withDays(g: { ag: (cmd: string, args?: any) => Promise<any>; now: () => number }, sims: any[], o: { rate?: number; startHour?: number } = {}) {
  const rate = o.rate ?? 2, start = (4 * 24 + (o.startHour ?? 6)) * 60; // day 1 = Friday (index 4 from Monday)
  const DOW = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'];
  const minutes = () => start + g.now() / 1000 * rate;
  const S = new Map<string, any>(), base = g.ag;
  let stove = false;
  const state = (name: string) => {
    let s = S.get(name);
    if (!s) {
      const c = sims.find(x => x.name === name) ?? { id: `X${S.size}`, name, age: 'Adult' };
      const digit = Number(String(c.id).replace(/\D/g, '').slice(-1) || 0);
      s = { c, digit, motives: { Hunger: 40, Energy: 60, Bladder: 50, Hygiene: 50, Fun: 30, Social: 30 }, went: -1, rescues: [] as string[], skills: [{ skill: 'Cooking', level: 2, max: 10 }] };
      S.set(name, s);
    }
    return s;
  };
  const job = (s: any, school: boolean, m: number) => {
    const day = Math.floor(m / 1440), dow = day % 7, h = (m % 1440) / 60, wd = dow < 5, [a, b] = school ? [8, 15] : [9, 17];
    const should = wd && h >= a && h < b, auto = !school && s.digit % 4 === 0 || school;
    if (should && auto && s.went !== day) s.went = day;
    return { name: school ? 'School' : 'Business', school, start: a, finish: b, work_day: wd, day_off: !wd, should_be_at_work: should, at_work: should && s.went === day, hours_until_work: 0 };
  };
  const ag = async (cmd: string, args?: any): Promise<any> => {
    const m = minutes(), day = Math.floor(m / 1440), h = (m % 1440) / 60;
    switch (cmd) {
      case 'story_day_state': {
        const out = (args.sims as string[]).map(name => {
          const s = state(name), age = s.c.age, kid = ['Child', 'Teen'].includes(age), worker = ['YoungAdult', 'Adult'].includes(age) && s.digit % 2 === 0;
          for (const k of Object.keys(s.motives)) s.motives[k] = Math.max(-100, s.motives[k] - 0.5);
          const inj = (kind: string, hr: number) => m >= start + hr * 60 && !s.rescues.includes(kind) && s.digit === 1;
          const buffs = [inj('burn', 3) && 'OnFire', inj('electrocution', 30) && 'SingedElectricity', inj('drown', 34) && 'Drowning'].filter(Boolean);
          const motives = { ...s.motives, ...(inj('starve', 20) && { Hunger: -90 }), ...(inj('freeze', 26) && { Temperature: -95 }) };
          return { sim: name, id: s.c.id, age, ...(worker && { job: job(s, false, m) }), ...(kid && { school: job(s, true, m) }), retired: age === 'Elder', motives, skills: s.skills,
            ...(s.digit === 3 && { ltw: { name: 'Become a Master Chef', skill: 'Cooking' } }), wishes: s.digit === 5 ? [{ name: 'Learn the Guitar Skill', skill: 'Guitar', points: 500 }] : [],
            guard: { swimming: buffs.includes('Drowning'), buffs, unlucky: false, interaction: buffs.includes('SingedElectricity') ? 'Repair TV' : null, aging: false, dying: false, dead: false, ghost: false } };
        });
        return { ok: true, result: { day: DOW[day % 7], hour: Math.round(h * 100) / 100, day_number: day - 3, vacation: false, aging: false, sims: out } };
      }
      case 'go_to_work': { const s = state(args.sim); s.went = day; return { ok: true, result: { queued: true } }; }
      case 'sim_fix_need': { const s = state(args.sim); s.motives[args.motive] = 80; return { ok: true, result: { interaction: `fix ${args.motive}` } }; }
      // home lots of Sims with id digit 0/3/6/9 have no stove until the day planner buys one; Writing's computer is always in use
      case 'sim_practice': return { ok: true, result: { skills: Object.fromEntries((args.skills as string[]).filter(k => k !== 'Cooking' || state(args.sim).digit % 3 !== 0 || stove).map(k => [k, { object: k }])), missing: [], cannot: [] } };
      case 'buy_object': if (/stove/.test(String(args.product))) stove = true; return base(cmd, args);
      case 'sim_pursue': return args.skill === 'Writing' || args.skill === 'Cooking' && state(args.sim).digit % 3 === 0 ? { ok: false, error: `the game's autonomy found nothing for skill ${args.skill}` } : { ok: true, result: { interaction: `practice ${args.skill}` } };
      case 'death_rescue': { const s = state(args.sim); s.rescues.push(args.kind); if (args.kind === 'starve') s.motives.Hunger = 0; return { ok: true, result: { did: [args.kind] } }; }
      default: return base(cmd, args);
    }
  };
  return { ...g, ag, minutes };
}

async function selfCheck() {
  const { rng, fakeGame, runStories, FIXTURE_TEMPLATES } = await import('./story-fuzzer.ts');
  const { FIXTURE, normalize, fromAgent } = await import('./story-census.ts');
  const r = rng(5, 1), C = (day: string, extra: Partial<Clock> = {}): Clock => ({ day, hour: 8, day_number: 1, ...extra });
  const J = (o: Partial<Job> = {}): Job => ({ name: 'Business', start: 9, finish: 17, work_day: true, day_off: false, should_be_at_work: false, at_work: false, ...o });
  const A = (o: Partial<DayState> = {}): DayState => ({ sim: 'Ann', id: '1', age: 'Adult', ...o });
  // day types from the game's schedule
  assert.equal(dayType(A({ job: J() }), C('Monday')), 'workday');
  assert.equal(dayType(A({ job: J({ work_day: false, day_off: true }) }), C('Saturday')), 'weekend');
  assert.equal(dayType(A({ job: J({ day_off: true }) }), C('Tuesday')), 'dayoff', 'vacation day / leave = day off');
  assert.equal(dayType(A({ age: 'Child', school: J({ start: 8, finish: 15, school: true }) }), C('Monday')), 'schoolday');
  assert.equal(dayType(A({ age: 'Elder', retired: true }), C('Monday')), 'retired');
  assert.equal(dayType(A({ job: J() }), C('Monday', { vacation: true })), 'vacation');
  assert.equal(dayType(A(), C('Wednesday')), 'dayoff');
  // planner: work block from the career hours, sleep, a workday gets 1 pursuit, a free day up to 3; blocks tile the day
  const bl: Pursuit[] = [{ skill: 'Cooking', why: 'ltw' }, { skill: 'Guitar', why: 'wish' }, { skill: 'Logic', why: 'hobby' }, { skill: 'Painting', why: 'hobby' }];
  const w = planDay(A({ job: J() }), 'workday', bl, rng(1, 1));
  assert.ok(w.blocks.some(b => b.kind === 'work' && b.a === 8.5 && b.b === 17));
  assert.equal(w.planned.length, 1); assert.equal(w.planned[0], 'Cooking');
  const tile = (bs: Block[]) => { let at = 0; for (const b of bs) { assert.ok(Math.abs(b.a - at) < 1e-9, `gap/overlap at ${at}: ${JSON.stringify(bs)}`); at = b.b; } assert.equal(at, 24); };
  tile(w.blocks);
  const f = planDay(A(), 'weekend', bl, rng(1, 2));
  assert.equal(f.planned.length, 3); tile(f.blocks); assert.ok(!f.blocks.some(b => b.kind === 'work'));
  const sc = planDay(A({ age: 'Child', school: J({ start: 8, finish: 15, school: true }) }), 'schoolday', bl, rng(1, 3));
  assert.ok(sc.blocks.some(b => b.kind === 'school') && sc.blocks.some(b => b.kind === 'homework' && b.skill === 'Homework')); tile(sc.blocks);
  const night = planDay(A({ job: J({ start: 22, finish: 6 }) }), 'workday', bl, rng(1, 4)); tile(night.blocks);
  assert.ok(night.blocks.some(b => b.kind === 'work' && b.b === 24), 'overnight shift runs to midnight');
  // backlog: ltw first, then wishes by points, top skills, one hobby; carry-over keeps undone pursuits in front
  const b0 = buildBacklog(A({ ltw: { name: 'x', skill: 'Cooking' }, wishes: [{ name: 'a', skill: 'Logic', points: 100 }, { name: 'b', skill: 'Guitar', points: 900 }, { name: 'c', skill: null }], skills: [{ skill: 'Cooking', level: 9, max: 10 }, { skill: 'Fishing', level: 3, max: 10 }, { skill: 'Writing', level: 10, max: 10 }] }), r);
  assert.deepEqual(b0.slice(0, 4).map(p => `${p.skill}:${p.why}`), ['Cooking:ltw', 'Guitar:wish', 'Logic:wish', 'Fishing:skill']);
  assert.equal(b0.at(-1)!.why, 'hobby'); assert.ok(!b0.some(p => p.skill === 'Writing'), 'a maxed skill is no pursuit');
  const co = carryOver(bl, ['Cooking', 'Guitar'], new Set(['Guitar']));
  assert.deepEqual(co.backlog.map(p => p.skill), ['Cooking', 'Logic', 'Painting', 'Guitar']); assert.equal(co.carried, 1);
  // practice census: unsupported skills drop out, supported hobbies top the list up to the cap (free days fill 3 blocks)
  const pb = bl.map(p => ({ ...p })), pr = practicable(pb, new Set(['Guitar', 'Athletic', 'Charisma']), 'Adult', 3);
  assert.deepEqual(pr.map(p => p.skill), ['Guitar', 'Athletic', 'Charisma']); assert.ok(pb.some(p => p.skill === 'Athletic'), 'top-up joins the backlog');
  assert.equal(practicable(bl, undefined, 'Adult', 3), bl, 'no census: backlog as is');
  assert.deepEqual(practicable(bl.map(p => ({ ...p })), new Set(), 'Adult', 3), [], 'nothing at home: no pursuit planned');
  // a carried pursuit is the next day's first one: the workday's undone pursuit runs on the day off
  assert.equal(planDay(A(), 'weekend', co.backlog, rng(1, 5)).planned[0], 'Cooking');
  // guard decisions
  const G = (gd: Guard, m: Record<string, number> = {}, o: Partial<DayState> = {}) => guardDecide(A({ guard: gd, motives: m, ...o }), C('Monday', { aging: true })).map(x => x.kind);
  assert.deepEqual(G({}, { Hunger: -90 }), ['starve']);
  assert.deepEqual(G({}, { Hunger: -60 }), []);
  assert.deepEqual(G({ buffs: ['Starving'] }, { Hunger: -75 }), ['starve']);
  assert.deepEqual(G({ swimming: true, buffs: ['Fatigued'] }, { Hunger: -90 }), ['drown'], 'starving in a pool = drown (Motive.cs), one rescue');
  assert.deepEqual(G({ buffs: ['OnFire'] }), ['burn']);
  assert.deepEqual(G({ buffs: ['SingedElectricity'], interaction: 'Repair TV' }), ['electrocution']);
  assert.deepEqual(G({ buffs: ['SingedElectricity'], interaction: 'Watch TV' }), [], 'singed but not repairing: no rescue');
  assert.deepEqual(G({ unlucky: true, interaction: 'Upgrade Stereo' }), ['electrocution']);
  assert.deepEqual(G({}, { Temperature: -90 }), ['freeze']);
  assert.deepEqual(G({ aging: true, days_to_age: 0.5 }, {}, { age: 'Elder' }), ['old_age']);
  assert.deepEqual(G({ aging: true, days_to_age: 0.5 }, {}, { age: 'Adult' }), []);
  assert.deepEqual(guardDecide(A({ age: 'Elder', guard: { aging: true, days_to_age: 0.5 } }), C('Monday', { aging: false })), [], 'aging off: no old-age rescue');
  assert.deepEqual(G({ dying: true, buffs: ['OnFire'] }), [], 'too late: logged as a death');
  // docs/171 §16: last night's shift is a work block, then sleep; failed pursuits go to the backlog's back
  const ov = planDay(A({ job: J({ start: 23, finish: 4, should_be_at_work: true }) }), 'dayoff', bl, rng(1, 6), {}, 0.3); tile(ov.blocks);
  assert.ok(ov.blocks[0].kind === 'work' && ov.blocks[0].b === 4 && ov.blocks[1].kind === 'sleep' && ov.blocks[1].a === 4, JSON.stringify(ov.blocks));
  assert.ok(!planDay(A({ job: J({ start: 23, finish: 4, should_be_at_work: false }) }), 'dayoff', bl, rng(1, 6), {}, 0.3).blocks.some(b => b.kind === 'work'));
  const cf = carryOver(bl, ['Cooking', 'Guitar'], new Set(['Logic']), new Set(['Cooking']));
  assert.deepEqual(cf.backlog.map(p => p.skill), ['Guitar', 'Painting', 'Logic', 'Cooking']); assert.equal(cf.carried, 1);
  // old age (R-LIFE-1): no rescue while the elder is free; while a story runs on it once per sim day; an old-age death is
  // natural, not a failure
  {
    const old = { id: '7', name: 'Old', age: 'Elder' } as DaySim, ol: any[] = [];
    let t = 0, n = 0;
    const io = { now: () => t, sleep: async (ms: number) => { t += ms; }, timeUp: () => n >= 12, log: (l: any) => ol.push(l), print: () => {},
      call: async (cmd: string) => {
        if (cmd !== 'story_day_state') return { ok: true, result: {} };
        n++; old.busy = n >= 4 ? 'story#1' : undefined;
        return { ok: true, result: { day: 'Monday', hour: 10 + n * 0.1, day_number: 1, aging: true, sims: [{ sim: 'Old', id: '7', age: 'Elder', retired: true, guard: { aging: true, days_to_age: 0.5, ...(n >= 11 && { dying: true, death_type: 'OldAge' }) } }] } };
      } };
    const sm: any = await runDays(io, { cfg: { tickS: 40, nightSkip: false }, driven: [old], deathOwned: new Map(), seed: 1, rng, ctx: {} });
    assert.equal(ol.filter(l => l.kind === 'guard' && l.danger === 'old_age').length, 1, 'one old-age rescue, only while in a story');
    assert.deepEqual(sm.deaths, { scripted: 0, unscripted: 0, natural: 1 });
    assert.ok(ol.some(l => l.kind === 'death' && l.natural && l.cause === 'OldAge'));
    assert.deepEqual(dayFails([...ol, { ...sm, simDays: 2, byType: { workday: { simDays: 1, done: 0 }, weekend: { simDays: 1, done: 1 } } }]), []);
  }
  // pass rule and lint on hand-made summaries
  const sum = (o: any) => ({ kind: 'days', simDays: 2, simHours: 30, deaths: { scripted: 0, unscripted: 0 }, attendance: { Ann: { due: [1], attended: [1] } }, byType: { workday: { simDays: 2, done: 1 }, weekend: { simDays: 2, done: 4 } }, ...o });
  assert.deepEqual(dayFails([sum({})]), []);
  assert.match(dayFails([sum({ simDays: 1 })]).join(), /only 1 sim day/);
  assert.match(dayFails([{ kind: 'death', sim: 'Bob', scripted: false, how: 'dying' }, sum({ deaths: { scripted: 1, unscripted: 1 } })]).join(), /1 unscripted death\(s\): Bob \(dying\)/);
  assert.match(dayFails([sum({ attendance: { Ann: { due: [1, 2], attended: [1] } } })]).join(), /Ann missed work\/school on day 2/);
  assert.match(dayFails([sum({ byType: { workday: { simDays: 2, done: 4 }, weekend: { simDays: 2, done: 4 } } })]).join(), /not > work/);
  assert.match(dayFails([sum({ byType: { workday: { simDays: 2, done: 1 } } })]).join(), /needs both/);
  assert.deepEqual(dayFails([]), ['no days summary (day planner did not run or the run stopped early)']);
  const gl = (n: number) => Array.from({ length: n }, () => ({ kind: 'guard', sim: 'Ann' }));
  assert.deepEqual(lintDays([...gl(5), sum({})]), []);
  assert.match(lintDays([...gl(6), sum({})]).join(), /Ann: 6 death-guard rescues in 2 sim day/);
  assert.match(lintDays([{ kind: 'block', t: 1, sim: 'Ann', block: 'work' }, { t: 20000, story: 's', run: 1, step: 0, actor: 'Ann' }]).join(), /s#1 started on Ann during the day plan's work block/);
  assert.deepEqual(lintDays([{ kind: 'block', t: 1, sim: 'Ann', block: 'work' }, { t: 5000, story: 's', run: 1, step: 0, actor: 'Ann' }]), [], 'a block that began as the story started');
  // end to end on the fake game: 25 virtual min at 2 sim min/s from Friday 06:00 = 50 sim h over Friday, Saturday, Sunday
  const raw = FIXTURE, sims = fromAgent(raw).sims;
  const g = withDays(fakeGame(raw), sims), lines: any[] = [];
  const content = { templates: FIXTURE_TEMPLATES.filter(t => !t.id.startsWith('world-')), personas: [], categories: {}, worlds: {} };
  await (g as any).drive(runStories({ ag: g.ag, sleep: (g as any).sleep, now: g.now, halted: () => false, census: normalize(raw, 'fixture'), log: l => lines.push(l) }, { seed: 3, minutes: 25, days: { tickS: 5 } }, content));
  const days = lines.find(l => l.kind === 'days');
  assert.ok(days.simDays >= 2, `sim days ${days.simDays}`);
  assert.deepEqual(dayFails(lines), [], dayFails(lines).join('; '));
  for (const k of ['burn', 'starve', 'electrocution', 'drown', 'freeze']) assert.ok(lines.some(l => l.kind === 'guard' && l.danger === k && l.ok), `guard rescued ${k}`);
  assert.ok(lines.some(l => l.kind === 'work' && l.cmd === 'go_to_work'), 'late workers sent to work');
  assert.ok(lines.some(l => l.kind === 'dayend' && l.carried > 0) || lines.filter(l => l.kind === 'pursuit' && !l.ok).length === 0, 'a failed pursuit is carried');
  assert.ok(lines.filter(l => l.kind === 'dayplan').every(l => DAY_TYPES.includes(l.type)));
  // a failed planned pursuit (fake: Writing) is followed by a stand-in from the backlog; every planned pursuit is tried
  const spare = (f: any) => { const p = lines.find(l => l.kind === 'dayplan' && l.sim === f.sim && l.day === f.day); return p && p.backlog.some((b: string) => !p.planned.includes(b.split(':')[0])); };
  const fails = lines.filter(l => l.kind === 'pursuit' && !l.ok && !l.instead && l.skill !== 'Homework');
  assert.ok(fails.some(spare), 'fixture has a failed pursuit with a spare backlog skill');
  for (const f of fails.filter(spare)) assert.ok(lines.some(l => l.kind === 'pursuit' && l.instead === f.skill && l.sim === f.sim && l.day === f.day), `stand-in after ${f.sim} ${f.skill}`);
  const tried = lines.filter(l => l.kind === 'pursuit' && !l.instead).length, planned = lines.filter(l => l.kind === 'dayend').reduce((a, l) => a + l.planned.length, 0);
  assert.ok(tried >= planned - lines.filter(l => l.kind === 'dayend' && l.day === days.simDays).length * 3, `planned pursuits tried: ${tried} of ${planned}`);
  assert.deepEqual(lintDays(lines).filter(x => /started on/.test(x)), [], 'no story starts on a Sim at work, school or asleep');
  // work and school end a running story for that Sim (docs/171 §16): no step on a Sim more than one tick into its work block
  const blk = lines.filter(l => l.kind === 'block').sort((p, q) => p.t - q.t);
  const late = lines.filter(l => !l.kind && l.actor).filter(l => { const b = blk.filter(x => x.sim === l.actor && x.t <= l.t).at(-1); return b && ['work', 'school'].includes(b.block) && l.t - b.t > 6000; });
  assert.deepEqual(late.map(l => `${l.story}#${l.run} ${l.actor}`), [], 'story steps on a Sim at work or school');
  assert.ok(lines.some(l => l.kind === 'between' && l.cmd === 'set_speed' && l.args.speed === 'skip'), 'night skip');
  // a Cooking Sim without a stove: one paid Buy-mode buy, then Cooking is planned; no pursuit is planned without an object
  const buys = lines.filter(l => l.kind === 'buy');
  assert.ok(buys.length >= 1 && buys.length <= MAX_BUYS && buys.some(l => l.ok && l.query === 'stove'), JSON.stringify(buys));
  // docs/180: off-camera diff (unit + fake run), clock lines, household rotation between days
  {
    const S = (o: Partial<TownSim>): TownSim => ({ id: '1', name: 'A', age: 'Adult', household: 'h1', household_name: 'H1', lot: 'l1', ...o });
    const p: TownSnap = { day_number: 1, active_household: 'act', sims: [S({}), S({ id: '2', name: 'B', household: 'h2', lot: 'l2' }), S({ id: '3', name: 'Me', household: 'act' }), S({ id: '4', name: 'Old', age: 'Elder' })] };
    const c: TownSnap = { day_number: 2, active_household: 'act', sims: [S({ partner: '2', married: true, job: 'Business', level: 1 }), S({ id: '2', name: 'B', household: 'h1', lot: 'l1', partner: '1', married: true }), S({ id: '3', name: 'Me', household: 'act', job: 'Law' }), S({ id: '5', name: 'Kid', age: 'Baby' }), S({ id: '6', name: 'New', age: 'YoungAdult', household: 'h3' })] };
    const d = townDiff(p, c);
    assert.deepEqual(d.births, ['Kid (H1)']); assert.deepEqual(d.arrivals, ['New (H1)']); assert.deepEqual(d.gone, ['Old (H1)']);
    assert.deepEqual(d.marriages, ['A + B'], 'one marriage per couple'); assert.equal(d.moves.length, 1); assert.deepEqual(d.jobs, ['A: none -> Business']);
    assert.ok(!JSON.stringify(d).includes('Me'), 'the active household is on camera, not in the diff');
    // EA's pet pool: churn is one tally, not arrivals/gone; a pet adopted out of the pool is a move
    const pp = { ...p, sims: [...p.sims, S({ id: '7', name: 'Coco', household: 'pp', household_name: 'PetHousehold', lot: null, pool: true }), S({ id: '8', name: 'Rex', household: 'pp', household_name: 'PetHousehold', lot: null, pool: true })] };
    const pc = { ...c, sims: [...c.sims, S({ id: '9', name: 'Piper', household: 'pp', household_name: 'PetHousehold', lot: null, pool: true }), S({ id: '8', name: 'Rex' })] };
    const dp = townDiff(pp, pc);
    assert.deepEqual([dp.arrivals, dp.gone, dp.pools], [['New (H1)'], ['Old (H1)'], { PetHousehold: { arrivals: 1, gone: 1 } }]);
    assert.ok(dp.moves.some(m => m.startsWith('Rex: PetHousehold')), JSON.stringify(dp.moves));
  }
  // attendance (story-days 2026-10-23): a Sim handed over after its carpool left (12:15, shift 13:00) is not due that day; a
  // Sim driven before it is; names stay in the summary after the Sim left the driven household
  {
    const ann = { id: '11', name: 'Ann', age: 'Adult' } as DaySim, cal = { id: '12', name: 'Cal', age: 'Adult' } as DaySim;
    const drv = [ann], al: any[] = [];
    let t = 0, n = 0;
    const io = { now: () => t, sleep: async (ms: number) => { t += ms; }, timeUp: () => n >= 12, log: (l: any) => al.push(l), print: () => {},
      call: async (cmd: string) => {
        if (cmd !== 'story_day_state') return { ok: true, result: {} };
        n++; const hour = 11 + n * 0.25; // 11:15 .. 14:00
        if (n === 5) drv.push(cal); // handed over at 12:15
        if (n === 12) drv.splice(0); // both rotated out before the summary
        const job = { name: 'Journalism', start: 13, finish: 17, work_day: true, day_off: false, should_be_at_work: hour >= 13, at_work: false };
        return { ok: true, result: { day: 'Wednesday', hour, day_number: 18, sims: drv.map(s => ({ sim: s.name, id: s.id, age: 'Adult', job })) } };
      } };
    const sm: any = await runDays(io, { cfg: { tickS: 5, nightSkip: false, town: false }, driven: drv, deathOwned: new Map(), seed: 1, rng, ctx: {} });
    assert.deepEqual(sm.attendance, { Ann: { due: [18], attended: [] } }, JSON.stringify(sm.attendance));
    assert.match(dayFails([{ ...sm, simDays: 2 }]).join(), /^Ann missed work\/school on day 18/);
  }
  const town = lines.filter(l => l.kind === 'town' && !l.base);
  assert.ok(town.length >= 1 && town.some(l => l.births.includes('Baby Landgraab (Landgraab)')) && town.some(l => l.marriages.length === 1) && town.some(l => l.gone.length === 1), JSON.stringify(town[0]));
  assert.ok(lines.filter(l => l.kind === 'clock').length >= 40, 'a clock line per sim hour');
  {
    const g2 = withDays(fakeGame(raw), sims), l2: any[] = [];
    await (g2 as any).drive(runStories({ ag: g2.ag, sleep: (g2 as any).sleep, now: g2.now, halted: () => false, census: normalize(raw, 'fixture'), log: l => l2.push(l) }, { seed: 3, minutes: 25, days: { tickS: 5, rotate: 1 } }, content));
    const rot = l2.filter(l => l.kind === 'rotate');
    assert.ok(rot.some(l => l.ok && l.from === '9100' && ['9101', '9103'].includes(l.to)), JSON.stringify(rot));
    // never mid-story: no story step between a rotate's start and its end, and every step after it is on the new household
    for (const r of rot.filter(l => l.ok)) {
      assert.ok(!l2.some(l => !l.kind && l.story && l.t > r.t && l.t < r.t + r.ms), 'a story step during the switch');
      const after = l2.filter(l => !l.kind && l.actor && l.t > r.t + r.ms && l.t < (rot.find(x => x.t > r.t)?.t ?? Infinity));
      assert.ok(after.every(l => r.sims.includes(l.actor)), `steps after the switch ordered other Sims: ${[...new Set(after.map(l => l.actor))].join(', ')}`);
    }
    assert.equal(l2.find(l => l.kind === 'days').rotations, rot.filter(l => l.ok).length);
  }
  console.log(`story-day self-check ok (${town.length} town diffs, ${days.simDays} sim days, ${days.simHours} h; free ${perDay(days.byType, FREE).mean.toFixed(2)} vs work ${perDay(days.byType, WORK_DAYS).mean.toFixed(2)} pursuits per Sim-day; ${lines.filter(l => l.kind === 'guard').length} rescues; ${lines.filter(l => l.kind === 'skip' && /leaves the story/.test(l.error ?? '')).length} stories left for work)`);
}

// not awaited: a top-level await here would deadlock the story-fuzzer.ts <-> story-day.ts import cycle
if (resolve(process.argv[1] ?? '') === fileURLToPath(import.meta.url)) selfCheck().catch(e => { console.error(e); process.exit(1); });
