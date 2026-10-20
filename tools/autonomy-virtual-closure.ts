// docs/127: autonomy closures with virtual dispatch resolved. For allow-list v0 (docs/121: the 12 "in" definitions and the
// 7 HowMuchItWantsSimToCome implementors) and every docs/126 tier-2 candidate, the docs/126 closure (depth 12, same cuts,
// delegates not followed) is run three times:
//   base:  as docs/126 (virtual calls reach the declared body only);
//   cha:   plus every game override of each virt/iface call that sits fewer than --hops (default 5) calls from a
//          Test/CalculateScore root (class hierarchy analysis, managed-callgraph.ts virtualTargets), with the `this` rule;
//   typed: cha, but at the hand-reviewed sites of RECEIVERS only the overrides the receiver's class family can reach
//          (the definition itself, or its target type from the root Test signature; managed-callgraph.ts familyTargets).
// Per definition: ICs, yields/events and static writes reached only through overrides, their docs/121 class, and the
// docs/126 verdict per run. --mods <dir> adds mod assemblies (NRaas, extracted with tools/extract-assemblies.ts) to the
// hierarchy: their overrides are counted per definition (cha run), not followed.
// Usage: node tools/autonomy-virtual-closure.ts --asm <repo>/game-copy/assemblies [--index ...] [--mods <dir>] [--hops 5] [--what-if <cuts>] > docs/data/autonomy-virtual-closure.csv
//        (summary to stderr). Static only.
import { ix, closure, classify, cands, inV0, IMPL, starts, icClass, pilot, CUT, snap, type Cand } from './autonomy-tier2.ts';
import { defRoots, instanceTypeOf, isA, type Recv } from './managed-callgraph.ts';

const arg = (n: string, d: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const HOPS = Number(arg('--hops', '5'));
// --what-if <prefix,prefix>: extra worker-hook cuts for all three runs (docs/127 §4 H11), to count what a hook would reopen.
CUT.push(...arg('--what-if', '').split(',').filter(Boolean));
// Hand-reviewed receivers, read in the decompiled class library [verified unless marked]: [caller id prefix, declared id
// prefix, producer prefix (the IL receiverTypes 'via': method or field that pushed the receiver), role]; '' = any.
// def = the interaction definition being scored; target / actor = the pair's target / actor, whose class is the root
// Test's target / actor parameter once Test has passed; instance = the definition's TInteraction; sim-routing = SimRoutingComponent when the
// caller's `this` is a Sim (Sim adds that component at construction). Calls that match none use the IL static type.
type Role = 'def' | 'target' | 'actor' | 'instance' | 'sim-routing';
const RECEIVERS: [string, string, string, Role][] = [
  // iop is the scored pair in the Autonomy scoring methods [inferred for callers outside CalculateScore*/CheckAvailability*].
  ['', '', 'Sims3.Gameplay.Autonomy.InteractionObjectPair::get_Target(', 'target'],
  ['', '', 'Sims3.Gameplay.Autonomy.InteractionObjectPair::get_InteractionDefinition(', 'def'],
  // The Test(ref parameters) overloads read actor and target from the parameters the generic base Test casts.
  ['', '', 'Sims3.Gameplay.Interactions.InteractionInstanceParameters::get_Actor(', 'actor'],
  ['', '', 'Sims3.Gameplay.Interactions.InteractionInstanceParameters::get_Target(', 'target'],
  // The venue score casts iop.Target to IMetaObject into a local before HowMuchItWantsSimToCome / GetHotSpotStatus.
  ['Sims3.Gameplay.Interactions.MetaInteractionScoredByVenue`3::CalculateScore(', 'Sims3.Gameplay.Interfaces.IMetaObject::', '', 'target'],
  // CalculateScoreForObjectInteraction keeps iop.Target in a local before GetInUseAutonomyMultiplier.
  ['Sims3.Gameplay.Autonomy.Autonomy::CalculateScoreForObjectInteraction(', 'Sims3.Gameplay.Interfaces.IGameObject::GetInUseAutonomyMultiplier(', '', 'target'],
  // obj is the scored target on the CalculateScoreForObjectInteraction path; other callers pass other objects [inferred].
  ['Sims3.Gameplay.Seasons.SeasonsManager::IsShelteredFromPrecipitation(', 'Sims3.Gameplay.Interfaces.ICanCountAsBeingIndoors::IsInteractionConsideredIndoors(', '', 'target'],
  // CreateInstance news up TInteraction and calls Init on it: the definition's own instance class.
  ['Sims3.Gameplay.Interactions.InteractionDefinition`3::CreateInstance(', 'Sims3.Gameplay.Interactions.InteractionInstance::', '', 'instance'],
  // GameObject.CreateRoute calls CreateRoute on its own routing component.
  ['Sims3.Gameplay.Abstracts.GameObject::CreateRoute(', 'Sims3.Gameplay.ObjectComponents.RoutingComponent::CreateRoute(', '', 'sim-routing'],
];
// Actor (0) / target (1) class from the root Test(actor, target, bool, ref tooltip) signature; a generic parameter gives none.
const split = (s: string): string[] => { const o: string[] = []; let d = 0, st = 0; for (let i = 0; i < s.length; i++) { const ch = s[i]; if (ch === '<') d++; else if (ch === '>') d--; else if (ch === ',' && !d) { o.push(s.slice(st, i)); st = i + 1; } } o.push(s.slice(st)); return o; };
const testParam = (c: Cand, i: number): string | undefined => {
  for (const r of c.roots) { const m = /::Test\((.*)\)$/.exec(ix.methods[r].k); const p = m ? split(m[1]) : []; if (p.length === 4 && p[2] === 'bool' && !p[i].startsWith('!')) return p[i]; }
  return undefined;
};
const v0: Cand[] = [
  ...[...inV0].map((t) => ({ group: 'v0', rank: '', samples: '', share: '', ctx: '', name: t, asm: '', roots: defRoots(ix as never, t), own: t })),
  ...IMPL.map((t) => ({ group: 'v0-implementor', rank: '', samples: '', share: '', ctx: '', name: `${t}::HowMuchItWantsSimToCome`, asm: '', roots: starts(`${t}::HowMuchItWantsSimToCome`) })),
];
const esc = (s: string) => (/[",]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s);
const out = ['group,definition_type,target_type,instance_type,closure_methods,typed_closure_methods,cha_closure_methods,new_ics,new_ic_classes,new_sim_thread_or_blocking_ics,new_yield_event,new_static_writes,mod_overrides,verdict,typed_verdict,cha_verdict,verdict_changed,first_new_reason,first_new_path'];
const icDefs = new Map<string, Set<string>>(); const tally = new Map<string, number>(); const modDecl = new Map<string, number>();
const short = (n: string) => n.replace(/^.*::/, '');
const bump = (m: Map<string, number>, k: string) => m.set(k, (m.get(k) ?? 0) + 1);
for (const c of [...v0, ...cands]) {
  if (!c.roots.length) continue;
  const self = c.own ?? c.name.replace(/::.*$/, ''); const tgt = testParam(c, 1), act = testParam(c, 0); const inst = c.own ? instanceTypeOf(c.own) : undefined;
  const recv = (caller: number, decl: number, fam: string | undefined, rt: Recv | undefined) => {
    const hit = RECEIVERS.find(([cp, dp, vp]) => ix.methods[caller].k.startsWith(cp) && ix.methods[decl].k.startsWith(dp) && (!vp || !!rt?.via?.startsWith(vp)));
    if (!hit) return undefined;
    return hit[3] === 'def' ? c.own : hit[3] === 'target' ? tgt : hit[3] === 'actor' ? act : hit[3] === 'instance' ? inst : fam && isA(fam, 'Sims3.Gameplay.Actors.Sim') ? 'Sims3.Gameplay.Actors.SimRoutingComponent' : undefined;
  };
  const b = closure(c.roots, c.own), h = closure(c.roots, c.own, HOPS), v = closure(c.roots, c.own, HOPS, self, recv);
  const cb = classify(c, b), ch = classify(c, h), cv = classify(c, v);
  // Class from docs/126 icClass; '*' = already in the pilot snapshot set (autonomy-proxy-ics.csv), so not new for the verdict.
  const newIcs = [...v.ics].filter(([n]) => !b.ics.has(n)).map(([n, d]) => [n, icClass(n) + (snap.has(n.slice(n.indexOf('::'))) ? '*' : ''), d] as const);
  for (const [n, cl] of newIcs) icDefs.set(`${n}\t${cl}`, (icDefs.get(`${n}\t${cl}`) ?? new Set()).add(c.name));
  const newTags = [...v.tags.keys()].filter((k) => !b.tags.has(k));
  const ye = newTags.filter((k) => /^(YIELD|EVENT) /.test(k)).map((k) => k.replace(/^(\w+) .*\.([^.]+::)/, '$1 $2'));
  // A static write is new for the verdict only when the pilot union does not already reach it (docs/126 §1 step 3).
  const sw = newTags.filter((k) => /^STATIC/.test(k)).map((k) => (pilot.tags.has(k) ? '(pilot) ' : '') + k.replace(/^(\w+) .*\.([^.]+::)/, '$1 $2'));
  const blocking = newIcs.filter(([, cl]) => /^(b|w|d)$/.test(cl));
  const firstKey = cv.keys.find((k) => !cb.keys.includes(k)) ?? (newIcs[0]?.[0] ?? '');
  const changed = cb.verdict !== cv.verdict;
  bump(tally, `${c.group}: ${cb.verdict} -> typed ${cv.verdict} (cha ${ch.verdict})`);
  for (const m of h.mod) bump(modDecl, m);
  out.push([c.group, c.name, tgt ?? '', inst ?? '', String(b.size), String(v.size), String(h.size), String(newIcs.length), newIcs.map(([n, cl, d]) => `${short(n)}:${cl}@${d}`).join(';'),
    blocking.map(([n, cl]) => `${short(n)}:${cl}`).join(';'), ye.join(';'), sw.slice(0, 8).join(';') + (sw.length > 8 ? `;+${sw.length - 8}` : ''),
    h.mod.size ? `${h.mod.size}: ${[...h.mod].slice(0, 3).join('; ')}` : '', cb.verdict, cv.verdict, ch.verdict, changed ? 'yes' : '',
    changed ? cv.reason : firstKey ? short(firstKey) : '', firstKey ? v.path(firstKey) : ''].map(esc).join(','));
}
console.error(`hops ${HOPS}; verdicts (group: before -> typed (cha)):`);
for (const [k, n] of [...tally].sort()) console.error(`  ${n}\t${k}`);
console.error('ICs reached only through overrides, typed run (definitions reaching each, docs/121 class):');
for (const [k, s] of [...icDefs].sort((a, b) => b[1].size - a[1].size)) console.error(`  ${s.size}\t${k}`);
console.error(`mod overrides seen (cha run): ${modDecl.size}`); for (const [k, n] of [...modDecl].sort((a, b) => b[1] - a[1]).slice(0, 30)) console.error(`  ${n}\t${k}`);
console.log(out.join('\n'));
