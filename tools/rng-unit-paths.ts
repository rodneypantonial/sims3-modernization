// docs/125 addendum: every managed path from the [AutShadow] driver's unit roots (docs/122 allow-list v0 + the 7 meta
// implementors) to the script RNG (Random_NextDoubleImpl via RandomGen), to System.Random and to Slots_GetPositionOfSlotImpl.
// One row per (unit root, target, last game frame before the RandomUtil/RandomGen/Slots layer), shortest path to that frame.
// Usage: node tools/rng-unit-paths.ts --asm <repo>/game-copy/assemblies --index <repo>/game-copy/analysis/callgraph/index.json
//        [--depth 14] > docs/data/rng-unit-paths.csv   (summary to stderr). Static only.
// Dispatch: virtual/interface calls expand to every override (managed-callgraph overrideIds), except that a call declared on
// an ancestor of the unit's receiver (the definition or the meta implementor) only reaches that receiver's own chain.
// shortcut: that receiver rule assumes such calls go to `this`; a call on another definition/meta object of the same hierarchy
// is missed (none seen in the allow-listed Test/score bodies). BCL-declared virtuals are not expanded. ldftn edges are followed.
import { readFileSync } from 'node:fs';
import { defRoots, overrideIds, subtypes } from './managed-callgraph.ts';

const arg = (n: string, d: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
type Rec = { k: string; a: number; ic?: string; c: number[] };
const ix: { asms: { name: string }[]; kinds: string[]; methods: Rec[] } =
  JSON.parse(readFileSync(arg('--index', 'game-copy/analysis/callgraph/index.json'), 'utf8'));
const DEPTH = Number(arg('--depth', '12'));
const BCL = new Set(['mscorlib', 'System', 'System.Xml']);
const byPrefix = (p: string) => ix.methods.flatMap((m, i) => (m.k.startsWith(p) ? [i] : []));
const typeOf = (i: number) => ix.methods[i].k.split('::')[0];
const short = (i: number) => ix.methods[i].k.replace(/\(.*$/s, '').replace(/^Sims3\.Gameplay\./, '');
const subMemo = new Map<string, Set<string>>();
const subs = (t: string) => { if (!subMemo.has(t)) subMemo.set(t, new Set((subtypes(t) ?? []).map((x) => x.full))); return subMemo.get(t)!; };

const DEFS = readFileSync('docs/data/autonomy-allow.csv', 'utf8').trim().split(/\r?\n/).slice(1)
  .filter((l) => l.split(',')[5] === 'in').map((l) => l.split(',')[0]);
const IMPL = ['Sims3.Gameplay.Core.Lot', 'Sims3.Gameplay.Abstracts.RabbitHole', 'Sims3.Gameplay.Autonomy.HomeMetaObject',
  'Sims3.Gameplay.Autonomy.WorkMetaObject', 'Sims3.Gameplay.Autonomy.GardeningMetaObject', 'Sims3.Gameplay.Pools.WorldPondHelper',
  'Sims3.Gameplay.Objects.Miscellaneous.BakeSaleTable'];
const hmw = byPrefix('Sims3.Gameplay.Interfaces.IMetaObject::HowMuchItWantsSimToCome(')[0];
// ScorePair's own call: InteractionDefinition.Test(ref parameters, ref tooltip), the generic base body (CommonTests, then the def's Test).
const testRef = byPrefix('Sims3.Gameplay.Interactions.InteractionDefinition::Test(Sims3.Gameplay.Interactions.InteractionInstanceParameters&,')[0];
const masc = byPrefix('Sims3.Gameplay.Interactions.IMetaInteractionDefinition::MetaAdSatisfiesCommodity(')[0];
// Pair unit (AutShadow.ScorePair): Tradeoff getter, commodity filters, parameters ctor, Test, score.
const pairShared = ['Sims3.Gameplay.Autonomy.InteractionObjectPair::get_Tradeoff(', 'Sims3.Gameplay.Autonomy.Tradeoff::SatisfiesCommodity(',
  'Sims3.Gameplay.Interactions.InteractionInstanceParameters::.ctor(', 'Sims3.Gameplay.Autonomy.Autonomy::CalculateScoreForObjectInteraction(']
  .flatMap(byPrefix);
type Unit = { unit: string; root: string; recv: string[]; roots: number[] };
const SIM = 'Sims3.Gameplay.Actors.Sim';
// Pair receivers: the definition, its interaction class (outer type, the CreateInstance product), the meta target, the actor.
const outer = (d: string) => d.replace(/\+[^+]*$/, '');
const units: Unit[] = [
  // Meta unit: the implementor's own HowMuchItWantsSimToCome (RabbitHole subclasses dispatch to theirs).
  ...IMPL.map((t) => ({ unit: 'meta', root: t.replace(/^Sims3\.Gameplay\./, ''), recv: [t, SIM],
    roots: overrideIds(hmw).filter((g) => subs(t).has(typeOf(g)) && (t !== 'Sims3.Gameplay.Abstracts.RabbitHole' || !IMPL.slice(2).includes(typeOf(g)))) })),
  ...DEFS.map((d) => ({ unit: 'pair', root: d.replace(/^Sims3\.Gameplay\./, ''), recv: [d, outer(d), ...IMPL, SIM],
    roots: [...defRoots(ix as never, d), ...pairShared, ...[masc, testRef].flatMap(overrideIds).filter((g) => subs(typeOf(g)).has(d))] })),
];

// Target layer: RandomUtil / RandomGen / ScriptCore.Random / System.Random, and the slot wrappers down to the IC.
const LAYER = /^(Sims3\.Gameplay\.Core\.RandomUtil::|Sims3\.SimIFace\.RandomGen::|Sims3\.SimIFace\.IRandom::|ScriptCore\.Random::|System\.Random::|Sims3\.SimIFace\.Slots::GetPositionOfSlot\(|ScriptCore\.Slots::(GetPositionOfSlot|Slots_GetPositionOfSlotImpl)\()/;
const leaf = (i: number): string | null => {
  const k = ix.methods[i].k;
  if (/^ScriptCore\.Random::Random_NextDoubleImpl\(|^Sims3\.SimIFace\.RandomGen::NextDouble\(/.test(k)) return 'rng';
  if (/^System\.Random::(Sample|Next)/.test(k)) return 'sysrand';
  if (/^ScriptCore\.Slots::Slots_GetPositionOfSlotImpl\(/.test(k)) return 'slotpos';
  return null;
};
const layerMemo = new Map<number, Set<string>>();
function layerTargets(i: number, seen = new Set<number>()): Set<string> {
  if (layerMemo.has(i)) return layerMemo.get(i)!;
  const out = new Set<string>(); const l = leaf(i); if (l) out.add(l);
  seen.add(i);
  const c = ix.methods[i].c;
  if (!l) for (let k = 0; k < c.length; k += 2) if (LAYER.test(ix.methods[c[k]].k) && !seen.has(c[k])) for (const x of layerTargets(c[k], seen)) out.add(x);
  layerMemo.set(i, out); return out;
}
// [AutShadow] worker hooks (ts3fixes AutShadow.ApplyAutShadow): the worker answers there, the sim-thread reference runs the body.
const HOOKS: [RegExp, string][] = [
  [/^Sims3\.Gameplay\.Utilities\.Localization::LocalizeString\(/, 'LocalizeString (worker: empty string)'],
  [/^Sims3\.Gameplay\.Actors\.Sim::IsAllowedInRoom\(ulong,int\)/, 'IsAllowedInRoom (worker: cache read)'],
  [/^Sims3\.Gameplay\.Actors\.Sim::get_NumSimsDoingGardening\(/, 'NumSimsDoingGardening (worker: request copy)'],
  [/^Sims3\.Gameplay\.Utilities\.OnlineDatingManager::DidSimsMeetOnline\(/, 'DidSimsMeetOnline (worker: record read)'],
  [/^Sims3\.Gameplay\.Autonomy\.MetaAutonomyTuning::GetSimCountForVenueWithCustomDesireFunction\(/, 'desire function (worker: abort)'],
  [/^Sims3\.Gameplay\.ChildAndTeenUpdates\.Punishment::GetPunishmentManagerFromSim\(/, 'GetPunishmentManagerFromSim (worker: read or abort)'],
];
// Hand-checked guards of docs/118 §5 / docs/121 §1: lazy create paths, run on the sim thread before the pilot.
const GUARDS: [RegExp, string][] = [
  [/^Sims3\.Gameplay\.Seasons\.SeasonsManager::LazyAllocateManager\(/, 'SeasonsManager.LazyAllocateManager (guard: once per load, sInstance set before meta searches)'],
  [/^Sims3\.Gameplay\.Socializing\.Relationship::(Get|GetInternal)\(/, 'Relationship.Get (guard: create path)'],
];
const tagOf = (tab: [RegExp, string][], i: number) => tab.find(([r]) => r.test(ix.methods[i].k))?.[1];

const ovMemo = new Map<number, number[]>();
const CHA = process.argv.includes('--cha'); // plain class hierarchy analysis, no receiver rule (noise count)
function succ(n: number, recv: string[]): [number, string][] {
  const m = ix.methods[n]; const out: [number, string][] = [];
  for (let k = 0; k < m.c.length; k += 2) {
    const t = m.c[k], kind = ix.kinds[m.c[k + 1]];
    out.push([t, kind]);
    if ((kind === 'virt' || kind === 'iface') && ix.methods[t].a >= 0 && !BCL.has(ix.asms[ix.methods[t].a].name)) {
      if (!ovMemo.has(t)) ovMemo.set(t, overrideIds(t));
      const rs = CHA ? [] : recv.filter((r) => subs(typeOf(t)).has(r));
      for (const o of ovMemo.get(t)!) if (!rs.length || rs.some((r) => subs(typeOf(o)).has(r) || (IMPL.includes(r) && subs(r).has(typeOf(o))))) out.push([o, kind + '>']);
    }
  }
  return out;
}

const q = (s: string) => (/[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s);
type Hit = { tg: string; n: number; t: number; path: number[]; kinds: string[] };
// BFS from roots; a hit is a game frame n calling into the target layer (or, with stopAtHooks, into a worker hook whose body
// reaches a target). One hit per (target, n, callee); the path is the shortest to n. Guards are cut (docs/118 §5).
function search(roots: number[], recv: string[], stopAtHooks: boolean, guardHits: Set<string>): Hit[] {
  const parent = new Map<number, [number, string]>(); const dist = new Map<number, number>(); const hits: Hit[] = []; const seen = new Set<string>();
  let fr = [...new Set(roots)]; fr.forEach((r) => dist.set(r, 0));
  const pathTo = (n: number) => { const p = [n]; const kinds: string[] = []; while (parent.has(p[0])) { const [pp, kd] = parent.get(p[0])!; kinds.unshift(kd); p.unshift(pp); } return { p, kinds }; };
  for (let d = 0; fr.length && d <= DEPTH; d++) {
    const next: number[] = [];
    for (const n of fr) {
      if (LAYER.test(ix.methods[n].k)) continue;
      const g = tagOf(GUARDS, n);
      if (g) continue;
      for (const [t, kind] of succ(n, recv)) {
        const gt = tagOf(GUARDS, t);
        if (gt) guardHits.add(`${gt.split(' (')[0]} from ${short(n)}`); // every caller, to check its create flag by hand
        const tgs = LAYER.test(ix.methods[t].k) ? [...layerTargets(t)] : stopAtHooks && tagOf(HOOKS, t) ? [...hookBody(t).keys()] : null;
        if (tgs) {
          for (const tg of tgs) { const key = `${tg}|${n}|${t}`; if (seen.has(key)) continue; seen.add(key); const { p, kinds } = pathTo(n); hits.push({ tg, n, t, path: p, kinds: [...kinds, kind] }); }
          continue;
        }
        if (d < DEPTH && !dist.has(t)) { dist.set(t, d + 1); parent.set(t, [n, kind]); next.push(t); }
      }
    }
    fr = next;
  }
  return hits;
}
// Shortest inner path per target of a hooked method's own body (what the sim-thread reference runs and the worker skips).
const hookMemo = new Map<number, Map<string, Hit>>();
function hookBody(h: number): Map<string, Hit> {
  if (hookMemo.has(h)) return hookMemo.get(h)!;
  const m = new Map<string, Hit>(); hookMemo.set(h, m);
  for (const x of search([h], [SIM], false, new Set())) if (!m.has(x.tg) || m.get(x.tg)!.path.length > x.path.length) m.set(x.tg, x);
  return m;
}

// Hand review of each last game frame in the decompiled class library (docs/125-addendum §2); key = target|frame.
const REVIEW: Record<string, string> = {
  'rng|Core.VisitCommunityLot::SharedVisitVenueTests': 'tooltip text on a failing branch, then return false: the unit ends (I-125-3 holds)',
  'rng|Autonomy.Autonomy::CheckAvailabilityTooltip': 'snow/pregnant tooltip, then a fail result: the unit ends (I-125-3 holds)',
  'rng|Actors.Sim::LocalizeString': 'snow tooltip in CheckAvailabilityTooltip, then a fail result: the unit ends',
  'rng|Autonomy.Motives::StressInteractionTest': 'failure text, then true = Tuning_TooStressed: the unit ends',
  'rng|ActorSystems.MoodManager::<StressInteractionTest>b__0': 'tooltip lambda: created, never invoked by ScorePair: no draw',
  'rng|ObjectComponents.CleanableComponent::LocalizeString': 'tooltip method group: never invoked by ScorePair: no draw',
  'rng|Interactions.InteractionDefinitionUtilities::ResortTests': 'check-in tooltip, then return false: the unit ends',
  'rng|Core.Lot::CommonTestLocalizeString': 'tooltip lambdas in CommonVisitTest: created, never invoked: no draw',
  'rng|CAS.SimDescription::get_IsFemale': 'Grim Reaper actor only (worker reads "" so IsFemale itself differs); this route needs a box-stall posture (horse): not reachable together [inferred]',
  'rng|Actors.Sim::IsAllowedInRoom': 'stale room cache: the reference recalculates (draw only for Variation-token name strings) and refreshes the cache, so the worker then reads it without abort 10; a later draw in the unit (pet branch) can shift: hole in I-125-2/3',
  'rng|Interactions.InteractionDefinitionUtilities::CommonTests': 'as IsAllowedInRoom: reference-only recalculation before C2, can shift the pet draw',
  'rng|Autonomy.Autonomy::CheckAvailabilityOnLot': 'as IsAllowedInRoom: reference-only recalculation before C2, can shift the pet draw',
  'rng|ChildAndTeenUpdates.Punishment::IsInteractionBanned': 'manager create path only; the worker aborts 16 there (A4)',
  'rng|Socializing.Relationship::GetHighestRelationshipSimForPet': 'pet actors: same draw on both threads, first draw of the pair unit (C2)',
  'rng|Pools.Pool::ReactToSkinnyDippers': 'lazy posture interaction list: first access populates it and adds the skinny dipper (world writes + draws); Mode R reference fills first = one-sided; Mode W worker-first = off-thread writes',
  'rng|Pools.Pool::ShouldReactPositiveToSkinnyDipper': 'as ReactToSkinnyDippers (same lazy fill)',
  'slotpos|Abstracts.GameObject::GetPositionOfSlot': 'TombRoomManager stairs/room-connection branch: only for a Stairs or RoomConnectionObject root target; meta targets are a Lot (early false) or other roots: unreachable for the 7 implementors',
  'slotpos|Actors.Sim::IsAllowedInRoom': 'reference-only room recalculation (worker: cache read or abort 10)',
  'slotpos|Interactions.InteractionDefinitionUtilities::CommonTests': 'reference-only room recalculation (worker: cache read or abort 10)',
  'slotpos|Autonomy.Autonomy::CheckAvailabilityOnLot': 'reference-only room recalculation (worker: cache read or abort 10)',
  'slotpos|ChildAndTeenUpdates.Punishment::IsInteractionBanned': 'manager create path only; the worker aborts 16 there (A4)',
};
const rows = ['unit,root,target,last_game_frame,callee,depth,path,worker_hook,edge_flags,inside_unit,one_sided,review'];
const sum: Record<string, number> = {};
for (const u of units) {
  if (!u.roots.length) { console.error(`no roots: ${u.root}`); continue; }
  const guardHits = new Set<string>();
  const hits = search(u.roots, u.recv, true, guardHits);
  for (const h of hits) {
    const hook = tagOf(HOOKS, h.t);
    const inner = hook ? hookBody(h.t).get(h.tg)! : undefined;
    const path = [...h.path, h.t, ...(inner ? inner.path.slice(1) : [])];
    const flags = [...new Set([...h.kinds, ...(inner?.kinds ?? [])].filter((x) => x === 'dlg' || x.endsWith('>') || x === 'dinv'))].join('/');
    const oneSided = h.tg === 'slotpos' ? (hook ? 'reference only (worker hook answers above it)' : 'both threads; worker answered by the AO_CUT key')
      : hook ? 'yes: reference draws, worker does not' : 'no: same draw on both threads';
    rows.push([u.unit, u.root, h.tg, short(h.n), short(h.t), String(path.length), path.map(short).join(' > '), hook ?? '', flags, `yes (${u.unit} unit)`, oneSided, REVIEW[`${h.tg}|${short(h.n)}`] ?? 'not reviewed'].map(q).join(','));
    const sk = `${u.unit}/${h.tg}/${hook ? 'hooked' : 'plain'}`; sum[sk] = (sum[sk] ?? 0) + 1;
  }
  // Guarded create paths (cut above): one row per caller of the guard; whether the create branch (and its draw) runs is by hand.
  for (const g of guardHits) {
    const [guard, from] = g.split(' from ');
    rows.push([u.unit, u.root, 'rng (guarded create path)', from, guard, '', '', '', '', `yes (${u.unit} unit)`, '-', guard.startsWith('Relationship')
      ? 'draw only in the create branch (CalculateAttractionScore); callers here pass createIfNone false or enumerate: no draw'
      : 'draw only while SeasonsManager.sInstance is null (once per load, before meta searches): no draw after warm-up'].map(q).join(','));
    sum[`${u.unit}/guard`] = (sum[`${u.unit}/guard`] ?? 0) + 1;
  }
}
console.log(rows.join('\n'));
console.error(JSON.stringify({ rows: rows.length - 1, ...sum }));
