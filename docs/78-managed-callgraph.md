# 78. Managed call graph and leaf tags (PLAN gap 12, steps 12a + 12b)

2026-10-16. Static only: no game run, nothing built, `mods/` untouched. Tool: `tools/managed-callgraph.ts`. Input for 12d (virtual dispatch), 12e (allow-list pilot) and any other off-thread idea that needs "what can this method reach".

Tags: [verified] = read in the IL/metadata or the decompiled script DLLs (`game-copy/`, not copied here); [inferred] = reasoning, not checked live.

## 1. Answer

- **12a done.** A TypeScript ECMA-335 reader (PE sections, `#~` tables, signature blobs, IL bodies; no npm dependency, no decompiler) indexes all 10 extracted assemblies in about 5 s: **179,462 methods** (165,766 with an IL body, 0 decode errors), **2,266 native bindings**, **687,001 call sites**, 65,355 distinct fields [verified: tool output].
- **Overloads are told apart** by the full parameter list (and arity for generic methods; the return type is appended only where two methods differ by it alone, e.g. conversion operators). The four `EventTracker.SendEvent` overloads get four ids [verified: self-check].
- **Unresolved share.** 11.7% of all call sites (80,094) cannot be bound to one target: unresolved virtual (38,384 edges), interface with more than 4 implementors or a non-final implementation (15,924), `constrained.` calls (6,423), delegate `Invoke` (1,071). In the six game script assemblies plus `ScriptCore`/`SimIFace`/`UI` it is 11.3% of edges (per assembly 7.3% `UI` to 12.3% `Sims3GameplaySystems`) [verified: tool output]. Only 29 targets stay as stubs, all multi-dimensional array accessors (runtime-provided, no metadata body).
- **12b done.** Each callee is tagged from `docs/data/ic-safety.csv` (SAFE/PROXY/UNSAFE/MAIN; 1,753 of 1,758 rows bind to a method by exact name; 5 rows name a class or method these DLLs do not have; bindings the csv names under another class fall back to a method-name match), yield, event send, static write, static collection write, delegate invoke and unresolved virtual. A query prints the closure of a method or of an interaction definition to depth N, the worst tag and one shortest path per tag.
- **Demo (§4): all five top meta definitions close to UNSAFE at depth 12**, and through the same two guarded paths: `SeasonsManager` lazy allocation (reaches `Simulator_AddObjectImpl`, UNSAFE) and `Relationship.Get` create-on-miss (reaches `EventTracker.SendEvent`). Both are behind a run-time guard the closure cannot see (`sInstance == null`; `bCreateIfNotPresent: false` passed by the callers on these paths) [verified in source]. With those two cut by hand, all five fall to **STATIC_WRITE: the RNG** (`RandomGen.gRandomGen` via `RandomUtil` in the base meta score and in token localization), plus one static collection and the scoring-function delegate. No YIELD within depth 12.

## 2. Method

**Reader.** PE32 header, section table, CLI header, metadata root, `#~` tables with heap-size flags and coded-index widths (all 45 table schemas), `#Strings`, `#Blob`, `#GUID` (assembly MVID kept for 12g). Method bodies: tiny and fat headers; the IL decoder knows every operand size (including `switch` and the `0xFE` prefix) and fails on a length mismatch, so a wrong operand table cannot pass silently.

**Ids.** `Namespace.Outer+Inner::Name`arity(param types)`, types rendered from signatures with full names (generic instances `List`1<Sims3.Gameplay.Actors.Sim>`, `!0`/`!!0` for type and method variables, `&` for by-ref). The same rendering on both sides lets a `MemberRef` in one assembly match the `MethodDef` in another by name and signature string, walking up the base chain when the compiler names a derived type.

**Native bindings.** `ScriptCore` declares its ICs as `extern` methods with the `PInvokeImpl` flag, not `InternalCall`; both count [verified: method flags]. The binding name is `Type::Method`, the key `docs/data/ic-safety.csv` uses. 329 more bindings are Mono runtime ICs in `mscorlib` (tag `MONO_IC`); the rest of the non-csv ones are `UTFConnector*` twins and a few `ScriptCore`/`UI` helpers, matched to the csv by method name where it has one.

**Edges per method** (deduplicated per method; index `c` array):

| Kind | IL | Meaning |
|---|---|---|
| `call` | `call` | direct; also `base.X()` |
| `cv` | `callvirt` | target not virtual, `final`, or declared on a sealed type: resolved |
| `virt` | `callvirt` | unresolved virtual; edge to the declared target (its body is followed, the tag says unresolved) |
| `iface` | `callvirt` | interface with >4 implementing classes or a non-final implementation: unresolved, declared target only |
| `ifaceN` | `callvirt` | interface with 1-4 implementing classes, every implementation final: one edge per implementation (this is how `Simulator.Sleep` reaches `ScriptCore.Simulator.Simulator_SleepImpl` through `ISimulator`) |
| `cons` | `constrained.` + `callvirt` | target depends on a type argument: unresolved |
| `dinv` | `callvirt Invoke` on a delegate type | delegate call: unresolved |
| `newobj` | `newobj` | constructor |
| `dlg` | `ldftn` / `ldvirtftn` | delegate creation; not followed by default (`--delegates`), as in docs/73 §2.1 |

Fields per method: instance reads and writes (`ldfld`/`ldflda`, `stfld`), static reads and writes (`ldsfld`/`ldsflda`, `stsfld`), each as `OwnerType::name`. A method that loads a static `System.Collections.*` field and also calls a mutator (`Add`, `Remove`, `Clear`, `set_Item`, ...) on a collection gets the static collection list [inferred: method level, not data flow].

**Leaf tags** (worst first): `YIELD` (the yield ICs of docs/73 §2.2, `Simulator.Sleep`, `set_YieldingDisabled`, `StateMachineClient.RequestState`), `MAIN`, `UNSAFE`, `IC?` (binding not in the csv), `EVENT` (`EventTracker.SendEvent*`), `STATIC_WRITE`, `STATIC_COLL`, `DELEGATE` (delegate invoke), `VIRTUAL` (`virt`/`iface`/`cons` edge), `UNRESOLVED` (stub), `PROXY`, `LAZY_CACHE` (compiler caches: `<>9__CachedAnonymousMethodDelegate*`, `<PrivateImplementationDetails>` string-switch maps; idempotent), `MONO_IC`, `SAFE`.

**Closure.** Breadth first from the roots over `call`, `cv`, `virt` (declared body), `ifaceN`, `newobj`, to depth N; one shortest path per tag. `def <Type>` takes as roots the type's own `Test` overloads (else the nearest base's) and the nearest `CalculateScore` up the base chain. `--cut a,b` skips calls into methods whose id starts with a prefix, for guards checked by hand (docs/73 §2.1 did the same for two guards).

**Files** (git-ignored, `game-copy/analysis/callgraph/`): `index.json` (~38 MB: methods with edges, field lists and bindings, field table, assembly MVIDs, stats) and `methods.csv` (~22 MB: per method counts of calls, unresolved calls, field reads and writes).

## 3. Coverage and self-check

| Assembly | Methods | Bindings | Call edges | Unresolved |
|---|---|---|---|---|
| Sims3GameplaySystems | 75,877 | 0 | 250,890 | 12.3% |
| Sims3GameplayObjects | 55,580 | 0 | 140,021 | 11.0% |
| UI | 14,926 | 5 | 59,783 | 7.3% |
| Sims3StoreObjects | 6,732 | 0 | 23,622 | 10.7% |
| SimIFace | 5,990 | 0 | 4,729 | 11.9% |
| ScriptCore | 4,625 | 1,932 | 4,160 | 9.2% |
| Sims3Metadata | 959 | 0 | 1,906 | 26.3% |
| mscorlib, System, System.Xml | 14,773 | 329 | 25,083 | 20.9-35.4% |

`--self-check` runs synthetic checks (compressed integers, an IL stream with `switch` and a `0xFE` opcode) and, when the assemblies are present, real ones: no IL decode error; at least 98% of the csv rows bind; `Autonomy.CalculateScore` has `Autonomy.CalculateScoreAndAddToCandidates` (docs/59 C1) among its callers (the three callers found are C1, `Autonomy.RespondToEvent` and `Posture.FindTransitionInteraction`, which matches docs/75's side entry points: posture transitions and reactions); `EventTracker.SendEvent` has four distinct overload ids; `Simulator.Sleep` reaches `Simulator_SleepImpl` through the interface rule [verified: tool output].

## 4. Demo: the docs/75 top 5 meta definitions

Roots: the definition's `Test(actor, target, autonomous, ref tooltip)` and the inherited `MetaInteractionScoredByVenue`3.CalculateScore` (all five inherit it [verified: base chain]). Depth 12. The base path (`CheckAvailability`, `CommonTests`, C2-C11) is not a root here; 12e adds it once for all definitions.

| Definition (docs/75 rank) | Closure | Worst | Worst after cutting the 2 guards | Closure after cut |
|---|---|---|---|---|
| `Core.VisitCommunityLot+Definition` (4) | 1,811 | UNSAFE | STATIC_WRITE (`RandomGen.gRandomGen` via `Localization.FillInTokens` → `RandomUtil.GetInt`) | 915 |
| `Core.VisitLot+Definition` (7) | 1,574 | UNSAFE | STATIC_WRITE (RNG via the base meta score) | 640 |
| `Autonomy.GoToGarden+Definition` (12) | 1,438 | UNSAFE | STATIC_WRITE (RNG via the base meta score) | 460 |
| `Core.GoToCommunityLot+Definition` (venue family) | 1,430 | UNSAFE | STATIC_WRITE (RNG via the base meta score) | 445 |
| `RabbitHoles.AutonomouslyGoForMealInRestaurant+Definition` (rabbit-hole family) | 1,445 | UNSAFE | STATIC_WRITE (RNG via the base meta score) | 466 |

Paths (first hit per tag, uncut):
- **UNSAFE** `Simulator_AddObjectImpl`: `CalculateScore` (venue) → `MetaInteractionDefinition`3.CalculateScore` → `Autonomy.CalculateScoreForObjectInteraction` → `SeasonsManager.get_Enabled` → `LazyAllocateManager` → `OnStartup` → `Simulator.AddObject`. For `VisitCommunityLot` the same tail starts in its own `Test` via `SharedVisitTests` → `Lot.GetMetaAutonomyVenueType`. The allocation runs only while `sInstance` is null [verified in source]; that it is always set before the first meta search in a seasons world is [inferred].
- **EVENT** `EventTracker.SendEvent`: `CalculateScoreForObjectInteraction` (or `Lot.CommonVisitTest` → `CanVisitCelebrityLot` → `Sim.GetRelationship` for `VisitLot`) → `Relationship.Get` → `GetInternal` → `CalculateAttractionScore` → `SendEvent`. `CanVisitCelebrityLot` passes `bCreateIfNotPresent: false` [verified in source]; whether the attraction path runs on lookup without create is for 12e to check.
- **STATIC_WRITE / STATIC_COLL**, uncut: `SeasonsManager.sInstance`, `Relationship.sAllRelationships`; after the cut: `RandomGen.gRandomGen` (RNG state: `MetaInteractionDefinition`3.CalculateScore` → `CalculateScoreForObjectInteraction` → `Relationship.GetBFFOrSimWithHighestLikingForPet` → `GetHighestRelationshipSimForPet` → `RandomUtil.GetRandomObjectFromList` → `RandomGen.NextDouble`), `GameUtils.WorldNameToType` (from `Tradeoff.TimeToRoute` → `GameUtils.IsDowntownWorld`; a lazily filled map [inferred]) and, for `VisitCommunityLot`, `Situation.sAllSituations` (from `Lot.GetHoursOfOperation` → `Bartending.TryGetHoursOfOperationExtended`).
- **DELEGATE**: `ScoringFunctionDelegate.Invoke` in `CalculateScoreForObjectInteraction` (the tuning scoring function, a docs/59 §3.6 root of its own).
- **VIRTUAL**: the venue score's own key call, `IMetaObject.HowMuchItWantsSimToCome`, is an interface call with many implementors, so the closures above do not contain the venue body. Its three implementations queried as roots (depth 12): `Lot.HowMuchItWantsSimToCome` UNSAFE (the same seasons and relationship paths; STATIC_COLL after the cut), `RabbitHole.HowMuchItWantsSimToCome` STATIC_COLL (`WorldNameToType`), `GardeningMetaObject.HowMuchItWantsSimToCome` STATIC_WRITE (`Sim.sLastTimeSimsGardeningWasUpdated`, a per-time memo of the gardening count).

**Reading for 12e** [inferred]: none of the five is allow-listable as is, and the blockers are shared, not per definition: two guarded lazy/create paths (decide by hand once, then `--cut`), the RNG (docs/59 §3.6 rule 3 already rejects `RandomUtil`; the pet-liking branch and token localization would need a seeded per-worker RNG or must stay on the sim thread), the gardening memo, and the scoring-function delegate. Fixing these in the base meta score clears all five at once. Depth 40 closures reach ~10,000 methods and a YIELD (via `Relationship` construction → outfit build → `Simulator.Sleep`), which shows why unguarded closures without argument tracking are too coarse on their own.

## 5. Limits

- No argument or branch tracking: a call behind `if (create)` or a lazy null check counts as reached. docs/73's literal-`false` rule is not reimplemented; use `--cut` for checked guards.
- Virtual dispatch is open (11.7% of sites): `virt` follows only the declared body; interface calls with many implementors are not followed. 12d adds class hierarchy analysis.
- Delegates created in a method are not followed; delegate invokes are tagged, not resolved. Reflection-bound callbacks (wishes, docs/73 §2.3) and tuning scoring functions are invisible.
- Static collection writes are a method-level heuristic; instance writes to shared objects (the W class of docs/59 §2.4) are listed per method (`fw`) but not tagged, since "allocated inside the closure" needs escape analysis.
- Mods are not scanned (docs/59 §3.6: keyed by runtime type and MVID; MVIDs are in the index).

## Reproduce

```sh
node tools/managed-callgraph.ts --self-check --asm <repo>/game-copy/assemblies
node tools/managed-callgraph.ts build --asm <repo>/game-copy/assemblies --out <repo>/game-copy/analysis/callgraph
node tools/managed-callgraph.ts def Sims3.Gameplay.Core.VisitLot+Definition --index <repo>/game-copy/analysis/callgraph/index.json --asm <repo>/game-copy/assemblies
node tools/managed-callgraph.ts def Sims3.Gameplay.Core.VisitLot+Definition --cut "Sims3.Gameplay.Seasons.SeasonsManager::LazyAllocateManager(,Sims3.Gameplay.Socializing.Relationship::Get(" ...
node tools/managed-callgraph.ts callers Sims3.Gameplay.Autonomy.Autonomy::CalculateScore --index ...
```
