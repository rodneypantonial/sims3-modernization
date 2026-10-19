// Static script-side checks for docs/105 I-105-6, I-105-7 (input half), I-105-8 and the enum half of docs/100 L9 (docs/105 §9).
// Reads the managed call graph index of tools/managed-callgraph.ts (per method: call edges, instance field reads/writes)
// and two enum files of the decompiled SimIFace. Every check compares an exact expected set with what the game's IL has;
// any new writer, caller, override or enum shift fails the check and names the difference.
// Usage:
//   node tools/portal-answer-check.ts [--index game-copy/analysis/callgraph/index.json] [--decompiled game-copy/decompiled]
//   node tools/portal-answer-check.ts --self-check      (synthetic index and enum: the checks must pass and must catch a planted change)
// Exit 0 = every check passed; 1 = a mismatch; 0 with SKIP when the index (game files) is missing.
import { readFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';

type Rec = { k: string; a: number; c: number[]; fr: number[]; fw: number[] };
type Index = { asms: { name: string; mvid: string }[]; kinds: string[]; methods: Rec[]; fields: string[] };
const arg = (n: string, d: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };

const G = 'Sims3.Gameplay.';
const DOOR = `${G}Abstracts.CommonDoor`;
const SIM = 'Sims3.Gameplay.Actors.Sim';
const strip = (k: string) => k.replace(/\(.*$/s, '');

// ---------- queries over the index ----------
function q(ix: Index) {
  const byPrefix = (p: string) => ix.methods.map((r, i) => [r, i] as const).filter(([r]) => r.k === p || r.k.startsWith(p + '('));
  const fieldIx = (f: string) => { const i = ix.fields.indexOf(f); if (i < 0) throw new Error(`no field ${f} in the index`); return i; };
  return {
    writers: (f: string) => { const i = fieldIx(f); return ix.methods.filter((r) => r.fw.includes(i)).map((r) => r.k); },
    readersCalling: (f: string, callee: RegExp) => {
      const i = fieldIx(f);
      return ix.methods.filter((r) => r.fr.includes(i) && calls(ix, r).some((t) => callee.test(t))).map((r) => r.k);
    },
    callees: (m: string) => { const rs = byPrefix(m); if (!rs.length) throw new Error(`no method ${m}`); return [...new Set(rs.flatMap(([r]) => calls(ix, r)))]; },
    callers: (m: string) => { const t = new Set(byPrefix(m).map(([, i]) => i)); return ix.methods.filter((r) => r.c.some((x, k) => k % 2 === 0 && t.has(x))).map((r) => r.k); },
    named: (suffix: string) => ix.methods.filter((r) => r.a >= 0 && r.k.endsWith(suffix)).map((r) => r.k),
    mvid: (name: string) => ix.asms.find((a) => a.name === name)?.mvid ?? '',
  };
}
const calls = (ix: Index, r: Rec) => r.c.filter((_, k) => k % 2 === 0).map((t) => ix.methods[t].k);

// ---------- expected sets (from the 2026-10-18 read of the Steam 1.67 script DLLs, docs/105 §9) ----------
type Check = { id: string; what: string; got: (ix: Index) => string[]; want: string[]; strip?: boolean };
const CHECKS: Check[] = [
  // I-105-6: handler-list identity = delegate reference
  { id: 'I-105-6a', what: 'writers of Lot.mOnPortalPathPlanHandler', got: (ix) => q(ix).writers(`${G}Core.Lot::mOnPortalPathPlanHandler`), strip: true,
    want: [`${G}Core.Lot::add_PortalPathPlan`, `${G}Core.Lot::remove_PortalPathPlan`] },
  { id: 'I-105-6b', what: 'the add accessor combines, the remove accessor removes', strip: true,
    got: (ix) => [...q(ix).callees(`${G}Core.Lot::add_PortalPathPlan`), ...q(ix).callees(`${G}Core.Lot::remove_PortalPathPlan`)].filter((k) => k.startsWith('System.Delegate::')),
    want: ['System.Delegate::Combine', 'System.Delegate::Remove'] },
  { id: 'I-105-6c', what: 'writers of MulticastDelegate.prev (the handler chain): constructors and the three methods that build fresh clones', strip: true,
    got: (ix) => q(ix).writers('System.MulticastDelegate::prev'),
    want: ['System.MulticastDelegate::.ctor', 'System.MulticastDelegate::.ctor', 'System.MulticastDelegate::GetInvocationList', 'System.MulticastDelegate::CombineImpl', 'System.MulticastDelegate::RemoveImpl'] },
  { id: 'I-105-6d', what: 'CombineImpl, RemoveImpl and GetInvocationList clone before linking', strip: true,
    got: (ix) => ['CombineImpl', 'RemoveImpl', 'GetInvocationList'].map((m) => `${m}:${q(ix).callees(`System.MulticastDelegate::${m}`).some((k) => k.startsWith('System.Delegate::Clone('))}`),
    want: ['CombineImpl:true', 'RemoveImpl:true', 'GetInvocationList:true'] },
  { id: 'I-105-6e', what: 'writers of the Delegate target/method fields (only constructors; method_info is a lazy cache)', strip: true,
    got: (ix) => ['target_type', 'method_ptr', 'm_target', 'method_name', 'method_info'].flatMap((f) => q(ix).writers(`System.Delegate::${f}`).map((w) => `${f}<-${strip(w)}`)),
    want: ['target_type<-System.Delegate::.ctor', 'target_type<-System.Delegate::.ctor', 'method_ptr<-System.Delegate::.ctor', 'method_ptr<-System.Delegate::.ctor',
      'm_target<-System.Delegate::.ctor', 'm_target<-System.Delegate::.ctor', 'method_name<-System.Delegate::.ctor', 'method_name<-System.Delegate::.ctor',
      'method_info<-System.Delegate::DynamicInvokeImpl'] },
  { id: 'I-105-6f', what: 'mscorlib is the game\'s Mono 1.2.3 build (MVID)', got: (ix) => [q(ix).mvid('mscorlib')], want: ['aed601673f714349b8afd3b84087182a'] },
  // I-105-7 (input half): every input the vanilla handlers read is a call listed in docs/105 §3
  { id: 'I-105-7a', what: 'calls of Sim.OnPortalPathPlan (rows 2-10 of §3)', strip: true, got: (ix) => q(ix).callees(`${SIM}::OnPortalPathPlan`),
    want: [`${G}Core.PortalPlanEventArgs::get_Actor`, `${SIM}::get_SimDescription`, `${G}CAS.SimDescription::get_IsTombMummy`, `${G}Core.PortalPlanEventArgs::set_Locked`,
      `${G}Core.PortalPlanEventArgs::get_PortalObject`, `${G}Abstracts.GameObject::get_LotCurrent`, `${G}Roles.Role::get_RoleGivingObject`, `${G}Interfaces.IGameObject::get_LotCurrent`,
      `${G}Core.PortalPlanEventArgs::get_Locked`, `${G}Core.PortalPlanEventArgs::get_RoomFrom`, `${G}Core.PortalPlanEventArgs::get_RoomTo`, `${DOOR}::IsAllowedThrough`,
      `${SIM}::get_SimRoutingComponent`, 'System.Collections.Generic.List`1::Add', `${G}Objects.FirefighterPole::IsAllowedThrough`, `${G}Interfaces.IBoxStall::IsAllowedThrough`,
      `${G}Interfaces.IHorseJump::IsAllowedThrough`, `${G}Objects.Elevator.ElevatorDoors::IsAllowedThrough`, `${G}CAS.SimDescription::get_ToddlerOrBelow`,
      `${G}CAS.SimDescription::get_IsVisuallyPregnant`, `${G}Objects.Ladder::DiscourageSimFromUsing`, `${G}Core.PortalPlanEventArgs::set_Discouraged`] },
  { id: 'I-105-7b', what: 'calls of PrivacySituation.OnPortalPathPlan (row 11)', strip: true, got: (ix) => q(ix).callees(`${G}Situations.PrivacySituation::OnPortalPathPlan`),
    want: [`${G}Core.PortalPlanEventArgs::get_RoomTo`, `${G}Abstracts.GameObject::get_RoomId`, `${G}Core.PortalPlanEventArgs::get_Lot`, `${G}Abstracts.GameObject::get_LotCurrent`,
      `${G}Core.PortalPlanEventArgs::get_Actor`, `${SIM}::get_InteractionQueue`, `${G}ActorSystems.InteractionQueue::GetCurrentInteraction`,
      `${G}ActorSystems.InteractionQueue::get_TransitionInteraction`, `${G}Interactions.InteractionInstance::get_Autonomous`, `${G}Interactions.InteractionInstance::GetPriority`,
      `${G}Core.PortalPlanEventArgs::get_Locked`, `${G}Situations.PrivacySituation::OtherSimCanIgnorePrivacy`, `${G}Core.PortalPlanEventArgs::set_Locked`] },
  { id: 'I-105-7c', what: 'calls of the dispatcher Lot.OnPortalPathPlan (row 1: one args object, every handler, Locked before Discouraged)', strip: true,
    got: (ix) => q(ix).callees(`${G}Core.Lot::OnPortalPathPlan`),
    want: [`${G}Core.PortalPlanEventArgs::.ctor`, 'System.Delegate::GetInvocationList', `${G}Core.PortalPathPlanHandler::Invoke`, `${G}Core.PortalPlanEventArgs::get_Locked`, `${G}Core.PortalPlanEventArgs::get_Discouraged`] },
  { id: 'I-105-7d', what: 'every IsAllowedThrough(int,int,Sim) in the DLLs (door subclasses reached by the virtual call: CommonDoor, DoorSingleGender, Turnstile, NonAnimatedDoor, SecretDoor; the last two fail the handler\'s Door test)',
    strip: true, got: (ix) => q(ix).named('::IsAllowedThrough(int,int,Sims3.Gameplay.Actors.Sim)'),
    want: ['HobbiesSkills.HorseJump', 'Door.SecretDoor', 'Door.SlidingBookcase', 'HobbiesSkills.Trampoline'].map((t) => `${G}Objects.${t}::IsAllowedThrough`)
      .concat([`${G}ObjectComponents.IPortalConnectionObject`, `${G}Pools.WaterSlide`, `${G}Objects.Ladder`, `${G}Objects.Elevator.ElevatorDoors`, DOOR, `${G}Pools.Pool`,
        `${G}Interfaces.IHorseJump`, `${G}Core.Platform`, `${G}Pools.DivingBoard`, `${G}Abstracts.NonAnimatedDoor`, `${G}Abstracts.DoorSingleGender`, `${G}Pools.PoolLadder`,
        `${G}Objects.FirefighterPole`, `${G}Objects.Elevator.ElevatorInterior`, `${G}Objects.TubeElevator`, `${G}Core.Stairs`, `${G}Abstracts.StaircaseSpiral`,
        'Sims3.Store.Objects.Turnstile'].map((t) => `${t}::IsAllowedThrough`)) },
  { id: 'I-105-7e', what: 'every OtherSimCanIgnorePrivacy(Sim) (base + 8 overrides, docs/103 §3) and DiscourageSimFromUsing(Sim)', strip: true,
    got: (ix) => [...q(ix).named('::OtherSimCanIgnorePrivacy(Sims3.Gameplay.Actors.Sim)'), ...q(ix).named('::DiscourageSimFromUsing(Sims3.Gameplay.Actors.Sim)')],
    want: ['Objects.Plumbing.Bathtub+BathtubPrivacySituation', 'Objects.Plumbing.Shower+ShowerPrivacySituation', 'Objects.Plumbing.Sink+SpongeBathPrivacySituation',
      'Objects.Plumbing.Toilet+ToiletSituation', 'Objects.Plumbing.Urinal+UrinalSituation', 'Objects.Beds.WooHoo+WooHooPrivacySituation', 'Situations.PrivacySituation',
      'Situations.GenericPrivacySituation'].map((t) => `${G}${t}::OtherSimCanIgnorePrivacy`)
      .concat(['Sims3.Store.Objects.RoutineMachine+ShowerPrivacySituation::OtherSimCanIgnorePrivacy', `${G}Objects.Ladder::DiscourageSimFromUsing`]) },
  { id: 'I-105-7f', what: 'door fields CommonDoor.IsAllowedThrough reads directly (the door mirror of §4.1)', got: (ix) => {
    const r = ix.methods.find((m) => m.k === `${DOOR}::IsAllowedThrough(int,int,Sims3.Gameplay.Actors.Sim)`)!; return r.fr.map((i) => ix.fields[i]); },
    want: ['mOneWayDestinationRoomId', 'mLockType', 'mLockOwner', 'mOwnerList'].map((f) => `${DOOR}::${f}`) },
  // I-105-8: every change of the door mirror's fields
  { id: 'I-105-8a', what: 'writers of the lock fields', strip: true,
    got: (ix) => ['mLockType', 'mLockOwner', 'mOneWayDestinationRoomId', 'mOwnerList'].flatMap((f) => q(ix).writers(`${DOOR}::${f}`).map((w) => `${f}<-${strip(w)}`))
      .concat(q(ix).writers(`${G}Abstracts.DoorSingleGender::mGender`).map((w) => `mGender<-${strip(w)}`), q(ix).writers('Sims3.Store.Objects.Turnstile::mLocks').map((w) => `mLocks<-${strip(w)}`)),
    want: [`mLockType<-${DOOR}::SetLockTypeAndOwner`, `mLockType<-${DOOR}::ImportContent`, `mLockType<-${DOOR}::.ctor`, `mLockOwner<-${DOOR}::SetLockTypeAndOwner`,
      `mOneWayDestinationRoomId<-${DOOR}::SetLockTypeAndOwner`, `mOneWayDestinationRoomId<-${DOOR}::.ctor`, `mOwnerList<-${DOOR}::.ctor`,
      `mGender<-${G}Abstracts.DoorSingleGender::.ctor`, 'mLocks<-Sims3.Store.Objects.Turnstile::.ctor'] },
  { id: 'I-105-8b', what: 'methods that read mOwnerList and call a List mutator (LockDoor.Run mutates local lists only, then calls SetLockTypeOwnerList)', strip: true,
    got: (ix) => q(ix).readersCalling(`${DOOR}::mOwnerList`, /^System\.Collections\.Generic\.List`1::(Add|AddRange|Remove|RemoveAt|RemoveAll|Clear|Insert|InsertRange|RemoveRange|set_Item|Sort|Reverse)\(/),
    want: [`${DOOR}::SetLockTypeOwnerList`, `${DOOR}::AddToOwnerList`, `${DOOR}::RemoveFromOwnerList`, `${DOOR}::HouseholdSimChanged`, `${DOOR}+LockDoor::Run`] },
  { id: 'I-105-8c', what: 'callers of CommonDoor.get_OwnerList (an alias of the list would be an unhooked mutator)', got: (ix) => q(ix).callers(`${DOOR}::get_OwnerList`), want: [] },
  { id: 'I-105-8d', what: 'callers of World.DoorLockStateChanged (IC 0x788310)', strip: true, got: (ix) => q(ix).callers('Sims3.SimIFace.World::DoorLockStateChanged'),
    want: [`${G}Objects.Door.RotatingWallDoor::OnStartMoving`, `${G}Objects.Door.RotatingWallDoor::OnDoneMoving`, `${DOOR}::OnLockChanged`] },
  { id: 'I-105-8e', what: 'which lock writers raise the lock event (ImportContent does not: the gap of docs/105 §9)', strip: true,
    got: (ix) => ['SetLockTypeAndOwner', 'ImportContent'].map((m) => `${m}:${q(ix).callees(`${DOOR}::${m}`).some((k) => k.startsWith(`${DOOR}::OnLockChanged(`))}`),
    want: ['SetLockTypeAndOwner:true', 'ImportContent:false'] },
];

// ---------- L9: enum values (decompiled SimIFace; no explicit values, so the value is the ordinal) ----------
export function enumValue(src: string, member: string): number {
  const body = src.slice(src.indexOf('{') + 1, src.lastIndexOf('}'));
  let v = -1;
  for (const raw of body.split(',')) {
    const m = raw.trim().match(/^(\w+)\s*(?:=\s*(0x[0-9a-fA-F]+|\d+))?$/);
    if (!m) continue;
    v = m[2] !== undefined ? Number(m[2]) : v + 1;
    if (m[1] === member) return v;
  }
  return NaN;
}

function diff(got: string[], want: string[]): string {
  const g = [...got].sort(), w = [...want].sort();
  const extra = g.filter((x, i) => g.indexOf(x) === i && g.filter((y) => y === x).length > w.filter((y) => y === x).length);
  const missing = w.filter((x, i) => w.indexOf(x) === i && w.filter((y) => y === x).length > g.filter((y) => y === x).length);
  return extra.length || missing.length ? `extra [${extra.join(', ')}] missing [${missing.join(', ')}]` : '';
}

export function runChecks(ix: Index, checks: Check[], enums?: { lotType: string; commercial: string }): string[] {
  const fails: string[] = [];
  for (const c of checks) {
    let got: string[];
    try { got = c.got(ix); } catch (e) { got = [`ERROR ${(e as Error).message}`]; }
    if (c.strip) got = got.map(strip);
    const d = diff(got, c.want);
    console.log(`${d ? 'FAIL' : 'PASS'} ${c.id} ${c.what}${d ? `\n     ${d}` : ` (${got.length})`}`);
    if (d) fails.push(c.id);
  }
  if (enums) {
    const pairs: [string, number, number][] = [['LotType.Commercial', enumValue(enums.lotType, 'Commercial'), 1],
      ['CommercialLotSubType.kEP10_Diving', enumValue(enums.commercial, 'kEP10_Diving'), 0x50]];
    for (const [n, got, want] of pairs) {
      console.log(`${got === want ? 'PASS' : 'FAIL'} L9 ${n} = ${got} (want ${want}: the pair +0x408/+0x40C the lot-outline stamp skips)`);
      if (got !== want) fails.push(`L9 ${n}`);
    }
  }
  return fails;
}

function selfCheck(): void {
  // a tiny index: Lot add/remove write the field; the check must pass, then fail once a third writer is planted
  const ix: Index = { asms: [{ name: 'mscorlib', mvid: 'x' }], kinds: ['call'], fields: ['L::f'], methods: [
    { k: 'L::add(H)', a: 0, c: [2, 0], fr: [0], fw: [0] }, { k: 'L::remove(H)', a: 0, c: [], fr: [0], fw: [0] }, { k: 'System.Delegate::Combine(D,D)', a: 0, c: [], fr: [], fw: [] }] };
  const ck: Check[] = [{ id: 's1', what: 'writers', strip: true, got: (x) => q(x).writers('L::f'), want: ['L::add', 'L::remove'] },
    { id: 's2', what: 'callees', strip: true, got: (x) => q(x).callees('L::add'), want: ['System.Delegate::Combine'] }];
  const ok1 = runChecks(ix, ck).length === 0;
  ix.methods.push({ k: 'Mod::Poke()', a: 0, c: [], fr: [], fw: [0] });
  const caught = runChecks(ix, ck).join() === 's1';
  const e = enumValue('enum E : uint\n{\n\tA,\n\tB = 0x10,\n\tC\n}', 'C') === 0x11 && Number.isNaN(enumValue('enum E { A }', 'Z'));
  const dupe = diff(['a', 'a'], ['a']) !== '';
  console.log(`self-check: clean index passes ${ok1}, planted writer caught ${caught}, enum parser ${e}, duplicate counted ${dupe}`);
  process.exit(ok1 && caught && e && dupe ? 0 : 1);
}

if (process.argv.includes('--self-check')) selfCheck();
else {
  const idx = arg('--index', 'game-copy/analysis/callgraph/index.json');
  const dec = arg('--decompiled', 'game-copy/decompiled');
  if (!existsSync(idx)) { console.log(`SKIP: ${idx} not found (node tools/managed-callgraph.ts build)`); process.exit(0); }
  const ix: Index = JSON.parse(readFileSync(idx, 'utf8'));
  const enumFile = (n: string) => join(dec, 'SimIFace', 'Sims3.SimIFace', `${n}.cs`);
  const enums = existsSync(enumFile('LotType')) ? { lotType: readFileSync(enumFile('LotType'), 'utf8'), commercial: readFileSync(enumFile('CommercialLotSubType'), 'utf8') } : undefined;
  if (!enums) console.log(`note: ${dec} missing, L9 enum check skipped`);
  const fails = runChecks(ix, CHECKS, enums);
  console.log(fails.length ? `RESULT: FAIL (${fails.join(', ')})` : 'RESULT: PASS');
  process.exit(fails.length ? 1 : 0);
}
