# 121. Autonomy meta scoring: native closure of the 36 PROXY ICs, the RNG, static collections and open dispatch

The "next static step" of docs/118 §7: for each PROXY internal call (IC) that meta scoring reaches (docs/118 §5), follow the native body and what it calls down to the data it reads, and decide how an off-thread scorer would answer it. The model is docs/94 §17 (routing's non-routing live reads, classified by what a snapshot would need). Static only: Ghidra headless on a copy project of the Steam 1.67 exe, `tools/managed-callgraph.ts --ics`, the decompiled class library for managed reads. Nothing was run in game. Per-IC table: `docs/data/autonomy-proxy-ics.csv`. Open claims: docs/51 §144 (I-121-1 to I-121-10).

Tags: **[verified]** = read in the disassembly, the PE file or the decompiled class library for this note; **[verified, docs/N]** = verified there, not re-read; **[inferred]** = reasoned from names, call shapes or the docs/62 scan, not read to the end.

## Summary

| Class | Meaning | ICs |
|---|---|---|
| **a** snapshot or cache | reads a bounded set of native state that can be copied on the sim thread (per tick, per load or per session) | **26** |
| **a-stub** | result only feeds a greyed-out tooltip string, which autonomy never shows (docs/59 §1.2 C7): a rooted empty string is enough | **6** |
| **b** needs the sim thread | shared mutable state that a copy cannot stand in for, or no bounded copy | **2** (`Random_NextDoubleImpl`, `Slots_GetPositionOfSlotImpl`) |
| **c** safe as is | no native state touched | **1** (`UTFConnectorSimulator::Simulator_GetTaskImpl`: never bound) |
| **d** unclear | needs a live answer first | **1** (`Query_GetObjects`: world-wide, type set unknown) |

The one docs/62 SAFE IC in the closure, `DeviceConfig_GetProcessorLevel` `0x741130`, is safe as is [verified]: it reads a string field of the config object and converts it, with no write.

**Verdict:** **33 of the 36** PROXY ICs can be answered without the sim thread (26 snapshot or cache, 6 stub, 1 never bound), **34** if the live part shows `Query_GetObjects` only asks for a few types. The two that cannot (the RNG and slot positions) both sit on side branches (pets, a scuba/baby branch of `CommonTests`) and can abort the worker search to the sim thread. Snapshot: **~2-5 KB per session or load** plus **~25-90 KB per meta search** (§3) [inferred]. Effort to an off-thread meta-scoring pilot: **~19-34 h** agent build on the shortest path (M5 in game, shim layer and snapshot, shadow), **~35-65 h** following the full docs/118 M1-M6 order, plus 4-9 game jobs and 2-4 clean nights (§5). Mono stays parked behind routing (memory "Mono parked"); this is the ready plan, not a start. **Update §8:** all 120 scoring functions resolve (+5 ICs, 1 of them class b); the six other meta implementors add 2 class-a ICs and one managed shim, so the meta closure is 38 PROXY of which **35** need no sim thread; lot-grid readers take no lock, so a pre-warm alone is not enough for live reads; the depth-4 scan found three more write-on-read sites (handle refcount destroy, terrain reentrancy byte, door lazy cache), all covered by the snapshot.

**Update docs/127 (overrides followed):** this closure entered only declared bodies of virtual calls. With overrides up to 5 hops (receiver types from the IL), **all 12 v0 definitions are blocked**: in rain or hail with Seasons on, `CalculateScoreForObjectInteraction` creates an interaction instance for an outdoor target, and `InteractionInstance.Init` writes the static `sNextId` (a managed write, which the IC guard does not catch). Worker hook H11 (docs/127 §4) removes it; then each v0 definition needs one more row, `Query_GetGlobalObjects` (through `WorldLot.GetObjects<T>`, I-127-3). The 36 PROXY ICs above stay as classified; the other new ICs on the v0 paths are already in the snapshot set. Of the 7 implementors, `Lot` and `BakeSaleTable` need the same new row, the other 5 stay ready.

## 1. Method

1. Roots as docs/118 §5: the five top meta definitions (`VisitCommunityLot`, `VisitLot`, `GoToGarden`, `GoToCommunityLot`, `AutonomouslyGoForMealInRestaurant`; the last one's namespace is `Sims3.Gameplay.Objects.RabbitHoles`, which the docs/118 table shortened), the base path (`Autonomy.CheckAvailability`, `InteractionDefinitionUtilities.CommonTests`) and `Lot.HowMuchItWantsSimToCome`, depth 12, guards `SeasonsManager.LazyAllocateManager` and `Relationship.Get` cut. Union: **36 PROXY + 1 SAFE** [verified: tool output, same count as docs/118]. Root abbreviations in the CSV: VCL, VL, GTG, GTCL, MEAL, CA (`CheckAvailability`), CT (`CommonTests`), LOT (`Lot.HowMuchItWantsSimToCome`).
2. Native side: `DecompileTo.java` at depth 2 over all 35 native bodies (252 functions), depth 0-1 over the script-host virtual targets the IC bodies reach through `0x7692B0` (the host interface, docs/27). Host vtable slots read from the PE file: interface `0x108B690` `+0x17C` = `0xD7CFD0` (returns the host), `+0xC0` = `0xD798F0`, `+0xBC` = `0xD70B50`, `+0x58` = `0xD71000`; host vtable `0x108B818` `+0x18` = `0xD74240`, `+0x28` = `0xD70EE0` [verified].
3. Each IC's verdict uses the docs/59 §3.5 premise: the worker runs in the quiescent window between script ticks, so managed state is still, but **native** state that the engine changes outside script (animation, locomotion, steering, lot streaming) is not.
4. Scoring functions: the 120 `[ScoringFunction]` methods (the `ScoringFunctionDelegate` targets) were run through the same closure (§4.4).

## 2. Per-IC verdicts, grouped by the state they read

Full rows (address, roots, depth, reads, writes on call, writers, size, reason, claim): `docs/data/autonomy-proxy-ics.csv`.

### 2.1 Object rows (class a): 6 ICs

`Objects_GetPosition`, `Slots_GetParentIdImpl`, `World_ObjectGetHiddenFlags`, `ScriptProxy_GetResourceKeyImpl`, `Simulator_GetTaskImpl`, `World_GetObjectTombRoomIdImpl`. The position-keyed ICs of §2.2 are answered from the same rows when the position is an object's.

- **Shared path** [verified]: object manager `[0x11DC350]`, hash lookup `0x939560` (the docs/62 §5 "8-15 entries per lookup" hash), component slot by bit mask `0x935770` (popcount index into the object's component array), transform component bit `[0x11E9158]`.
- Position: world matrix `0x958EE0` reads the local transform and, if the object has a parent (`+0x30`), composes it with the parent chain (`0x958560`); no store into the object was found [verified for `0x958EE0`, inferred for `0x958560`]. Writers: native animation and locomotion every frame, also inside the quiescent window. So: copy, do not read live.
- Parent id: the same lookup plus LOCK-prefixed atomics, a handle reference count taken and dropped [inferred from the instruction shape]. The worker must not touch that count: one more reason to copy.
- Hidden flags: bit 0 of the component byte at `+0x40` (`0x945EC0`) [verified].
- Resource key: `0xD70B50` -> `0x7697A0`: hash lookup, then the 16-byte key at object `+0x18`; constant for the object's life [verified]. `World_ObjectGetProductKey` is bound to the same native function [verified: `internal-calls-map.csv` lists both names at `0x7872E0`].
- Task lookup: `0xD74240` looks the id up in the host's script-object hash (`+0x1664`/`+0x1668`), then a second map (`+0xBE8`/`+0xBEC`, returns the entry's `+0x24`); both change on script object create and destroy [verified]. Answer: the managed object reference from the row; ids outside the snapshot abort.
- Row (docs/59 §2.5 plus this doc): id, position, parent and root parent, lot id, room id, tomb room id, level, resource key, managed object reference, flag word (outside, covered, hidden, destroyed, in world, ...). ~64-80 B per row.

### 2.2 World geometry (class a): 5 ICs

`World_GetLotLocationImpl`, `World_GetRoomIdAtPosImpl`, `World_IsPositionOutsideImpl`, `World_HasAnyRoofOrFloorAbovePosImpl`, `World_GetDoorRoomsImpl` (and the lot part of `World_GetObjectTombRoomIdImpl`).

- `World_GetDoorRoomsImpl`: object lookup, the door's portal component, the lot table `[0x11ECF08]` and room lookups; returns a managed `int[]` [verified to depth 0; rest inferred]. Room ids on both sides of a door change only in build/buy or on lot load: a per-door pair (4 B) for the scored lot's doors.

- All go through the **lot-at-position grid** `0xC688A0` on the WorldManager `[0x11ECBC4]` and then walk the hit cell's lots by bounds (`+0x174`/`+0x178`) [verified].
- **Write on read:** when the grid's dirty byte `+0x1CC` is set, `0xC688A0` first rebuilds it in `0xC68680` under lock `0x10786C8` (the lock docs/62 lists as `Lock_Acquire(... via c68680)`) [verified]. A worker call can therefore run a locked rebuild of a shared table; sim-thread readers of the grid that do not take the lock would race it [inferred: readers not audited]. **Pre-warm:** call one lot-at-position query on the sim thread before the window opens; with the dirty flag clear, the IC is a pure read until a lot is added, removed or edited (I-121-2).
- Room id: lot `+0x298` (room grid), level clamped to 0-3, then `0xB499D0` [verified]. Outside: `0xB046C0` reads the room flags (`0xB96380`, or `0xABB7D0` on the floor/roof path) [verified]. Roof/floor above: `0xABB7D0` -> `0xACFF90` [verified to depth 1].
- Writers of the answers: build/buy, lot load and unload, edit town. None of these run during live-mode scoring, but lot streaming loads lots during play [inferred]. So the answer is "copy per row" (the snapshot flags), and positions that are not a row's position abort (docs/59 §3.4 already does this for `IsPositionOutside`).
- `World_GetLotLocationImpl`: per lot a fixed location; a per-lot table of ~100-200 lots covers every meta lot.

### 2.3 Lot object queries (classes a and d): 3 ICs

- `Query_CountObjectsOnLot`, `Query_GetObjectsOnLot` (a): lot by id (`0xC62D40`, lot vtable `+0x40` type check), then the lot's object vector (lot `+0x14`) walked by `0x7499B0` with no lock; per object a component lookup in `[0x11ECF08]`; `GetObjectsOnLot` allocates the managed result array [verified]. The vector grows and shrinks when objects are added or removed. Snapshot: per scored lot the frozen list of (object id, script class) pairs, 8 B per object; any `T` the managed side asks for is answered by filtering that list, so the type set need not be known in advance.
- `Query_GetObjects` (d): goes to the script host (`0xD798F0`), resolves the Mono class (`0xE70D50`), and walks the host's per-class object maps `+0x1658` and `+0x1678` [verified]. It is world-wide: a frozen copy of every script object's (id, class) would be thousands of rows. Which types meta scoring passes (`Lot.CountObjects<T>` and friends, 118 virtual targets) is a live question (I-121-7). If they are a handful of rare types (rabbit holes, meta objects), a per-type list refreshed on create/destroy is small and this becomes class a.

### 2.4 Constants per load or per search (class a): 7 ICs

| IC | Read | Lifetime |
|---|---|---|
| `GameUtils_GetWorldName` | `0x5A2110`: cached world id at `+0x60`; first call scans the world file list and stores it [verified] | per load (pre-warm) |
| `GameUtils_GetWorldType` | `0x5A22A0`: cached type at `+0x64`, falls back to the name [verified] | per load (pre-warm) |
| `World_GetWorldNameKey` | WorldManager + string service; allocates the string [inferred] | per load, rooted string |
| `World_GetSeaLevelImpl` | terrain object (WorldManager `+0x60`) float `+0x14`, default `[0x1078624]` [verified] | per load |
| `World_GetSunriseTime`, `World_GetSunsetTime` | WorldManager float `+0x12C` and its neighbour; direct field reads [verified] | per sim day |
| `World_GetMoonPhaseImpl` | object at WorldManager `+0x64`: phase table `+0x88C`/`+0x890`, override flag `+0x255`, after a product check `0x594710(0xF)` [verified] | per sim day |

The two world-identity ICs are a lazy fill on first call; after one sim-thread call per load they are pure reads, so a worker could even call them live. The snapshot copy is still simpler (4 B each).

### 2.5 Session caches (class a): 6 ICs

`DeviceConfig_GetOption` (config registry lookup through the host, shared temp string `0x11CB920` written per call), `GameUtils_IsWorldInstalled` (world registry `[0x11EB490]`, lazy fills under lock `0x11D3C98`, 88 unresolved virtual calls), `UserToolUtils_GetProductImpl` (creates a script handle under `g_ScriptHandleLock`), `UserToolUtils_ProductGenericImpl` (a switch over ~60 product getters behind a script handle; 146 unresolved virtual calls), `World_GetLotName`, `World_GetLotNameKey` (lot by id, string build). None is a pure read on the native side, but every answer is fixed for the session (install state, config) or changes only on a player rename (lot names) [inferred]. A cache keyed by the call's arguments, filled on the sim thread on first use (miss -> abort the worker search, sim thread fills the key), answers them; size well under 5 KB including all lot names.

### 2.6 Tooltip text (class a-stub): 6 ICs

`EAText_GetMoneyString`, `EAText_GetNumberString`, `EAText_GetTimeString`, `LocalizedStringService_GetLocalizedStringByString` (ScriptCore and the `Sims3.UI` binding of the same native name), `OnlineFeaturesHost_GetPersonaName`.

All are reached from `VisitCommunityLot.SharedVisitVenueTests` and `Localization.LocalizeString` / `FillInTokens` on **failing** branches that build a greyed-out tooltip (store hours, resort closed) [verified in the decompiled class library]. The tooltip is built whether or not the call is autonomous, and autonomy never shows it (docs/59 §1.2 C7). Natively they write shared EAText globals (`0x11D7508`..., docs/54) and allocate, so they are not safe to call. A worker shim that returns one preallocated, rooted empty string keeps behaviour [inferred: no autonomous caller reads the tooltip text; I-121-4]. Better still, shim `Localization.LocalizeString` itself on the worker: that also removes the RNG draw in `FillInTokens` (§4.1).

### 2.7 Needs the sim thread (class b): 2 ICs

- `Random_NextDoubleImpl`: advances the host Mersenne Twister at host `+0xC30` (624 words + index; docs/27 §1.1) [verified, docs/27]. See §4.1.
- `Slots_GetPositionOfSlotImpl`: object lookup, then the slot position helper `0x76F0A0`, the same helper docs/90 found waiting on the steering `+0x918` state (gate-12 family F-B), with 48 unresolved virtual calls and the lot-table lock `0x10786C8` [verified for the call; inferred for the rest]. Slot positions follow animation; the domain is object x slot, unbounded. Reached only from `CommonTests` (one branch). Answer: abort the worker search, sim thread scores the pair (or a per-row "slot positions" extension for the few object classes the live part names).
- **Update 2026-10-20 (docs/125):** both now have worker answers: the RNG by per-unit Mersenne Twister streams (same values in the reference and on the worker, no abort), slot positions as learned keys answered by the game body at every cut. `Query_GetObjects` (§2.3) the same way, kept up to 256 objects.

### 2.8 Never bound (class c): 1 IC

`ScriptCore.UTFConnectorSimulator::Simulator_GetTaskImpl` has no entry in the EA IC table (`internal-calls-map.csv` lists only `ScriptCore.Simulator::Simulator_GetTaskImpl`) [verified]. The static closure reaches it through `ISimulator` dispatch; in the game the live implementor is `ScriptCore.Simulator` [inferred: UTF connector is EA's test front end]. If it were ever called it would fail in the runtime's IC lookup before any native state is touched. I-121-8.

## 3. Snapshot size [inferred]

| Part | Content | Built | Size |
|---|---|---|---|
| Session caches | config options, installed worlds, products, lot names and keys, persona name, rooted empty string | first use, then on rename | ~2-5 KB |
| Per load | world name, type, name key, sea level | load | < 0.1 KB |
| Per search, world | sunrise, sunset, moon phase, generation counters | each search | < 0.1 KB |
| Per search, lots | location and bounds for every meta-scored lot; per scored community lot the frozen (id, class) object list | each search, or on lot change (incremental) | lot table 3-8 KB; object lists ~30 community lots x 50-300 objects x 8 B = **12-72 KB** |
| Per search, rows | Sims and meta objects the closure touches (actor, other Sims, gardens, rabbit holes, the scored lots' doors): ~100-250 rows x 64-80 B | each search, dirty rows only once change hooks exist (docs/76, docs/79) | **~6-20 KB** |
| **Total** | | | **~2-5 KB fixed + ~25-90 KB per meta search** |

This is the meta-search share; docs/59 §2.5 gives 30-100 KB for a local search on a busy home lot. Build cost: the docs/59 §3.3 native incremental builder (rows) plus a vector copy per scored lot; the object lists only change on add/remove, so a per-lot generation counter keeps most searches at the dirty-row cost [inferred].

## 4. RNG, static collections, open dispatch

### 4.1 RNG

- `RandomGen.gRandomGen` (the docs/118 STATIC_WRITE) is a lazy fill of an `IRandom` reference from `AppDomain.GetData("RandomGen")`; after the first call it is a read of a fixed reference [verified]. Harmless on its own; warm it on the sim thread.
- The real shared state is the native MT behind `Random_NextDoubleImpl` (§2.7). Paths in the closure: (1) the pet branch of the base meta score (`Relationship.GetBFFOrSimWithHighestLikingForPet` -> `RandomUtil.GetRandomObjectFromList`), only for pet actors; (2) `Localization.FillInTokens` in tooltip text (§2.6) [verified: tool paths].
- Options: (a) **abort on draw** (pilot): pets and any draw send the search back to the sim thread; tooltip draws disappear with the `LocalizeString` stub; (b) per-worker MT seeded from the sim MT at snapshot time: keeps the worker going but changes which numbers both threads draw, so outcomes differ from vanilla (acceptable for the offload goal only after a determinism discussion, docs/27); (c) draw-ahead buffer: the sim thread pre-draws N values for the worker; still reorders the sim's own stream. Recommendation: (a) for the pilot, measure how often it fires. **Superseded (docs/125 §2):** per-unit streams seeded from (cut seed, unit), a variant of (b) that keeps Mode R bit-equal and leaves the live stream untouched.
- Determinism note: even with (a), skipping tooltip draws on the worker removes draws the vanilla sim thread made, so the sim RNG stream shifts. Seeded A/B runs (docs/27) must use the same worker setting on both sides.

### 4.2 Static collections

- `GameUtils.WorldNameToType`: filled at `GameUtils` start-up; `GetWorldType(world)` adds one entry only for `WorldName.UserCreated` on its first miss, every other miss returns a value without writing [verified]. Pre-warm: call `GetCurrentWorldType()` once per load on the sim thread; after that the worker only reads a dictionary nobody writes.
- `Situation.sAllSituations`: enumerated by `Situation.GetSituations<T>` (reached via `Lot.GetHoursOfOperation` -> `Bartending.TryGetHoursOfOperationExtended`) [verified]. Writers are situation start and exit, both managed and on the sim thread. In the quiescent window (docs/59 §3.5) no script runs, so an in-place read is safe; if the window premise fails, copy the list (tens of entries, < 1 KB) or bump a generation counter on add/remove and drop stale results.

### 4.3 Delegates and interface dispatch

| Site | Targets | What they touch | Off-thread handling |
|---|---|---|---|
| `ScoringFunctionDelegate` | the 120 `[ScoringFunction]` static methods, bound by reflection over every loaded assembly at start-up (`Autonomy.FindAllScoringFunctions`), so mods add more [verified] | §4.4 | allow-list by method (name + assembly MVID); unknown targets abort |
| `IMetaObject.HowMuchItWantsSimToCome` | 7 implementors: `Lot`, `RabbitHole`, `HomeMetaObject`, `WorkMetaObject`, `GardeningMetaObject`, `WorldPondHelper`, `BakeSaleTable` [verified: `overrides`] | `Lot` is the root above; the other six need their own closure (live part) | allow-list per implementor; mod subclasses abort |
| `Lot+OnAllowedInRoomCheck` | registered per room by `PrivacySituation` only (`RegisterRoomRestrictionCheckCallback`) [verified] | situation state of the privacy situation (managed) | in-place read in the quiescent window; or abort when the lot has any registered callback (rare: privacy situations) |
| `Predicate<T>` | `List.Find` lambdas, e.g. `RealEstateManager.FindProperty` / `FindVenueProperty` (`CheckAvailability`, `Lot.IsCheckedInResort`) [verified: tool paths] | managed property records | in-place read; the lambda bodies are in the same assemblies and on the allow-list |
| `Lot.CountObjects<T>` (virtual, 118 targets) | generic over T | `Query_*` (§2.3) | per-lot object list filter; `Query_GetObjects` stays d |

### 4.4 Scoring functions closure

75 of the 120 `[ScoringFunction]` methods resolve in the call-graph index by the decompiled path (the other 45 are nested types the name rule missed; not chased). Their union adds only **3 ICs** beyond the 37: `Slots_CanAttachToSlotImpl` (docs/62 UNSAFE), `Slots_GetContainedObjectImpl` (PROXY) and `World_StaysAfterEvictImpl` (PROXY), plus two static writes (`ScoringFunctionsForBabiesAndToddlers.sMaxHunger`, a lazy cache; `DesignModeSwap.DesignModeSwapComplete`) [verified: tool output]. Which scoring function each of the five meta definitions names is in its tuning, not in code; the meta definitions score by venue (`MetaInteractionScoredByVenue`), so they may name none (I-121-10). A scoring function that reaches the UNSAFE slot IC must stay on the sim thread.

**Tuning read (2026-10-20, I-121-10 verified for the shipped tuning):** a scan of all 267 installed `.package` files (base game, EP1-11, SP1-9 and their delta packages) finds interaction tuning (ITUN, type `0x03B33DDF`) only in `Game/Bin/Gameplay/GameplayData.package`: 4,128 resources, 224 of which name a scoring function (107 distinct names, `CleaningScoringFunction` 48 times), so the reader sees names where they exist. The five definitions have 7 ITUN rows (`VisitLot`, `VisitCommunityLot` and `GoToCommunityLot` on `Lot`; `GoToGarden` on `GardeningMetaObject`; `AutonomouslyGoForMealInRestaurant` on `Restaurant`; `CarOwnable` twins of `VisitLot` and `VisitCommunityLot` on `IOwnableVehicle`), each with one tradeoff whose `ScoringFunction` name is empty. So no `[ScoringFunction]` body is in the meta closure, and the §8.1 extra ICs (including `Slots_CanAttachToSlotImpl`) are not reached by these five. A mod tuning package could set a name; the worker must check that the definition's tuning has no scoring function and abort otherwise (one compare) [inferred: field not located].

## 5. Effort to an off-thread meta-scoring pilot

Units per CLAUDE.md (wall-clock for Claude with agents). "Pilot" = the worker scores the five meta definitions in shadow (M6: results compared, not used).

| Step | Work | Estimate |
|---|---|---|
| C live | probe: IC hit list per meta definition for one load; answers I-121-3/7/9/10 and the docs/118 GC gate | 1-2 h + 1 job |
| Shim layer | 34 IC shims in ts3native at the binding table (thread check, snapshot answer, abort flag), counters, Verify | 2-4 h |
| Snapshot builder | native rows, per-lot object lists with generation counters, lot table, constants, session caches with miss-abort, pre-warm calls (world type, lot grid, `RandomGen`) | 3-6 h |
| M5 | second Mono thread, docs/120 §6 a-d | 9-14 h + 1 load + 3 clean nights |
| M6 | shadow in game, docs/118 | 4-8 h + 3-5 jobs + 1 clean night |
| **Shortest path** | the five rows above | **~19-34 h**, 5-7 game jobs, 2-4 clean nights |
| Full docs/118 order | adds M1 (6-12 h), M2 (4-8 h), M3 (3-6 h + 2 jobs), M4 (3-5 h) before M5 | **~35-65 h**, 7-9 game jobs, 4 clean nights |

The full order buys an offline GC stress gate (G4) before any second thread in game; the shortest path relies on docs/120's static go and the crash rule. Either way the managed-world problem docs/119 §4.3 names for the offline host does not apply to the in-game shadow.

## 6. Biggest blockers

1. **M5 itself** (docs/120): no IC answer helps until a second managed thread runs safely; this is half of the shortest-path hours.
2. **Native state changes inside the quiescent window**: positions, parents, hidden flags and slot positions are written by native animation and locomotion outside script ticks, so object ICs need copies, not live reads. The window premise (docs/59 §3.5) still has to be measured for managed state.
3. **Write-on-read paths**: the lot-at-position grid rebuild (`0xC68680`, lock `0x10786C8`), the world id lazy fill, the handle refcount atomics in the slot ICs and the EAText globals. Pre-warm or copy, never call.
4. **Open dispatch**: six meta implementors and the tuning-named scoring functions need the live closure; mod types abort by MVID.
5. **RNG**: aborts or a determinism trade-off (§4.1).

## 7. Ledger

Open claims: docs/51 §144 (I-121-1 to I-121-15; I-121-11 to I-121-15 from §8).

## 8. Open static items closed (2026-10-20)

Static only, same sources as §1 plus three new read-only Ghidra scripts on the `ghidra-ac121` copy project: `StoreScan.java` (per-root BFS, non-stack stores, LOCK ops, `Lock_Acquire` calls), `DecompStores.java` (stores left in the decompiler's p-code, with the pointer's origin: parameter, global, call return), `OffsetWrites.java` (every write to one field offset). New CSV root tags: **SF** = the 120 scoring functions, **BST** = `BakeSaleTable.HowMuchItWantsSimToCome`.

### 8.1 The 45 unresolved scoring functions

All 120 `[ScoringFunction]` methods resolve [verified: tool output]. The 45 misses of §4.4 were nested types (`Sims3.Gameplay.Actors.Sim+PetFollow::PetFollowScoringFunction`, `ScoringFunctionsForBabiesAndToddlers::*` in nested classes, ...): the index has them under `Outer+Inner`; the earlier name rule built `Outer.Inner`. No tool change was needed; §8 Reproduce builds the ids from the decompiled source's class nesting.

Union of all 120 (cut list as §1): 28 reach a non-Mono IC; 31 distinct ICs. Beyond the meta closure of §2 they add **5**:

| IC | Reached from | Class | Answer |
|---|---|---|---|
| `Slots_GetContainedObjectImpl` `0x76F820` | `SitHereForEating.EatHereScoringFunction`, `HarvestPlant+EatFromHarvestPlantPet` (and BST, §8.2) | a | per-row slot contents for the few classes that ask; the body takes and drops a handle refcount (§8.4) |
| `Slots_GetContainmentSlotNamesImpl` `0x76F990` | `HarvestPlant+EatFromHarvestPlantPet` | a | session cache by resource key (slot names are fixed per model) [inferred] |
| `CASUtils_GetOutfitInGameObjectImpl` `0x5A9430` | `Sim+GoInside.GoInsideToFixSimTemperature` | a | per-Sim row: current outfit key (16 B) |
| `World_StaysAfterEvictImpl` `0x785C30` | `BedSleep.BedSleepScoringFunction` | a | per-row flag; no store to depth 4 besides out-parameters |
| `Slots_CanAttachToSlotImpl` `0x76E430` | `EatHereScoringFunction` | **b** | docs/62 UNSAFE (fires `World_FireEvent`, writes globals): abort to the sim thread |

Static writes: the same two as §4.4 (`sMaxHunger`, 22 baby/toddler functions; `DesignModeSwapComplete`, `BedSleep` only) plus `RandomGen.gRandomGen` (5 functions, the lazy fill of §4.1). `Random_NextDoubleImpl` is reached by 5 scoring functions (urinal, pet follow, pet lay-down, ...). All of this matters only if the meta definitions' tuning names a scoring function (I-121-10: the shipped tuning names none, §4.4).

### 8.2 The six other `IMetaObject.HowMuchItWantsSimToCome` implementors

| Implementor | Closure | ICs | Blocker |
|---|---|---|---|
| `HomeMetaObject`, `WorkMetaObject` | 1 method: returns 0 [verified] | none | none: allow-list as is |
| `WorldPondHelper` | count of its own skater id list, tuning, base meta score | `GameUtils_GetWorldType`, `DeviceConfig_GetProcessorLevel` (both already rows) | none beyond §2 |
| `RabbitHole` | unique reference count, tuning, base meta score | world name/type, `DeviceConfig_GetOption`, product ICs, `GetResourceKey`, `GetTaskImpl`, `GetParentId`, moon phase (all already rows) | none beyond §2 |
| `GardeningMetaObject` | as `RabbitHole` plus `Sim.NumSimsDoingGardening` | as above | **managed static write**: the getter writes `sLastTimeSimsGardeningWasUpdated` on every call (through `SimClock.ElapsedTimeInMinutes(ref)`, then restores it) and recounts `sNumSimsDoingGardening` over `LotManager.Actors` when more than 30 sim minutes passed [verified]. Worker shim: return the cached count without writing; the sim thread refreshes it (I-121-15) |
| `BakeSaleTable` | lot Sims not in the household, stock check, desired count, Sims in radius | adds **`Query_GetObjectsInRange`** `0x7496E0` (asked for `Sim` within `kMetaAutonomyDesiredRadius` of the table) and **`Slots_GetContainedObjectImpl`** (stock check) | both class a: Sim rows with positions answer the range query; slot contents per row (I-121-12) |

So all seven implementors can be allow-listed: two need nothing, three need only §2's rows, `GardeningMetaObject` needs one managed shim, `BakeSaleTable` two more row-backed IC answers.

### 8.3 Lot-at-position grid: readers and the lock

- **Readers do not take the lock** [verified]. `0xC688A0` checks the dirty byte `WM+0x1CC`, calls the rebuild `0xC68680` if set, then reads the cell size `+0x1D0`, origin `+0xB4`/`+0xB8` and the cell hash `+0x1D8`/`+0x1DC` with no lock. The rebuild has 13 more callers, all in the world-manager code block `0xC68960`-`0xC6C290` [inferred: same check-then-read shape, not each read].
- **Lock `0x10786C8` has two users** [verified: xrefs]: the rebuild itself (taken around the whole rebuild; the dirty byte is cleared last, still under the lock) and `WorldManager_Shutdown` `0xC6B780` (fence when the world shuts down off the main thread). So the lock serializes rebuilds against each other and against shutdown, not against readers.
- **Setters of the dirty byte** [verified: `OffsetWrites.java`; of the program's 81 writes to some `+0x1CC`, five are in world-manager code: four set it, the rebuild clears it]: `0xC5FEC0` (`MarkLotIndexDirty`: lot load, houseboat create, lot remove), `0xC65700`, `0xC65830` (`DetachLot`) and `0xC671E0` (`Init`); only the rebuild clears it. Matches docs/60 and docs/109.
- **Can a worker read after a sim-thread pre-call clears the flag?** Yes, until a setter runs. The main thread rebuilds every frame when the flag is set (`WorldManager_ScoreAndStreamLots`, docs/60), so a lot load or detach during the window followed by a main-thread rebuild resets the cell vectors and the hash while the worker reads them: a torn or stale read. Vanilla already has this race (the routing job reads the grid from its worker the same way, docs/60). Two safe options: (1) the snapshot answers of §2.2 (unchanged recommendation); (2) for a live read, the worker holds `0x10786C8` around the lookup and aborts if the flag is set: every rebuild takes that lock, so none can run while the worker holds it, and the cost is at most one frame's rebuild wait for the main thread (I-121-13).

### 8.4 Depth-4 native closure of the snapshot ICs

Scanned: the 27 class-a/b IC bodies of §2, the 6 new ones, the host targets of §1, to depth 4 (1,031 function visits; the 11 decompiler failures are imports without a body: `GetTickCount`, `memmove`, `memset`, `floor`, `acos`). Every store the decompiler keeps was traced to its pointer's origin; stores into out-parameters, caller locals and fresh allocations were dropped. What is left [verified unless marked]:

| Write | Where | Reached from | Effect for a worker |
|---|---|---|---|
| Handle refcount: `LOCK` inc, `LOCK` dec, and on zero set to 1 and call the deleting destructor | IC bodies of `Slots_GetParentIdImpl`, `World_GetDoorRoomsImpl`, `World_GetObjectTombRoomIdImpl`, `CASUtils_GetOutfitInGameObjectImpl`, `Slots_GetContainedObjectImpl`, `Slots_GetContainmentSlotNamesImpl` (and `0x7460F0`) | 6 ICs | atomic, but if another thread drops the last reference first, the destructor runs on the worker: copy, never call (confirms I-121-1's refcount guess) |
| Reentrancy byte `+0x116` set 1, then 0, on the lot terrain object | `0xA8DF80` (terrain height), only on the out-of-lot-bounds path | `GetLotLocation`, `IsPositionOutside`, `HasAnyRoofOrFloorAbovePos`, `GetObjectTombRoomId`, `GetRoomIdAtPos` at depth 3-4 | a concurrent caller sees the byte set and gets height 0; the routing job already shares this (it calls the same function from `Route_SearchAStar`) |
| Lazy cache: float `+0x28`, clears bit `0x10` of `+0x2C` | `0xC89500` on a portal record | `World_GetDoorRoomsImpl` | write-on-read; the per-door snapshot of §2.2 avoids it |
| Lot-grid rebuild | `0xC68680` (§8.3) | the 5 geometry ICs | as §8.3 |
| Blocking read lock `0x106FB00` | `0xB96380` (room region lookup) | `World_IsPositionOutsideImpl`, `HasAnyRoofOrFloorAbovePos` | not a write; a live worker call could wait on it |
| Allocator and string-buffer state | `0x4E51B0`, `0x4E5920`, `0x4E69D0`, ... (the memory manager, docs/119 I-119-11) | most ICs | thread-safe allocator; not shared sim state |

Nothing else: no global written directly, and the remaining parameter stores go to buffers the caller owns (iterator `0xC33810` into the caller's buffer, out-structs, local vectors) [inferred: caller ownership read for the IC-level calls, not for every depth-3/4 site; indirect calls not followed]. Verdict: every snapshot-class IC stays class **a**, and the "copy, never call live" rule now has three verified reasons (refcount destroy, terrain flag, door cache) besides the grid (I-121-14).

### 8.5 `tools/mono-host-ics.ts` base-class walk

`mono-host-ics.ts` now takes Test and CalculateScore up the base chain (`defRoots` of `managed-callgraph.ts`, now exported; the CLI runs only when that file is the entry point), maps four more base paths (`MetaInteractionDefinitionUtilities.SpecialCaseTests`, `Autonomy.UpdateCacheIfNeeded`, `LiveDragHelperModel.get_CachedTopDraggedObject`, `Autonomy.CheckAvailabilityTooltip`) and resolves "scoring function T.M" rows. Top-20 rows with roots: **12 -> 17** (21 -> 27 roots); still out: two `[unresolved]` rows and `base path: .ctor`. Closure 2,743 -> 2,924 methods; ICs **108 -> 129**: engine-only 12 -> 26 (14 `UIManager` window/event ICs, reached through `LiveDragHelperModel`), reads-world 27 -> 29, writes-world 1 -> 2 (`World_GetWorldPositionImpl`), managed-reentry 5 -> 6 (`Slots_CanAttachToSlotImpl`), static-data 20 -> 21, pure 43 -> 45 [verified: tool output]. `docs/data/mono-host-ics.csv` (docs/119's table) was not regenerated here.

### 8.6 What changes for the pilot

- Meta closure with all seven implementors: **38 PROXY + 1 SAFE**. Without the sim thread: **35 of 38** (28 snapshot or cache, 6 stub, 1 never bound), 36 if `Query_GetObjects` resolves (I-121-7). Still class b: the RNG and slot positions (side branches, abort). Since docs/125 both have worker answers (per-unit RNG streams, slot positions and `Query_GetObjects` as learned cut keys); only `Slots_CanAttachToSlotImpl` still aborts.
- If tuning names a scoring function (I-121-10): +3 class a, +1 class b (`CanAttachToSlot`, eating only). The shipped tuning names none (§4.4), so this applies only to mod tuning: abort when one is set.
- Every object and geometry IC must be answered from the snapshot, never called live (§8.4); the lot grid live-read fallback needs lock `0x10786C8` (§8.3).
- Effort (§5): +2 IC shims and one managed shim (`NumSimsDoingGardening`), about +0.5-1 h on the shim layer; shortest path stays **~19-35 h**.

## 9. Live probe for the open items: `mono-ledger-probe.json` (built 2026-10-20, not run yet)

One 10-minute Bridgeport load closes I-121-5, I-121-7, I-121-9 and I-121-12 (with docs/120 I-120-4 and docs/119 I-119-44) without a second managed thread. It reuses the `[AutShadow]` guard thunks (docs/122 §3.1) and the ts3fixes B13 hook; nothing new is hooked in the game.

| Piece | What it does |
|---|---|
| `[AutShadow] Ledger=1` (`native/ts3native/autledger.cpp`, default 0) | the guard thunks of `Random_NextDoubleImpl`, `Slots_GetPositionOfSlotImpl`, `Query_GetObjects` and `Query_GetObjectsInRange` count each call, then pass through to the game's body unchanged |
| Scope | ts3fixes' `MetaHook` around the game's `Autonomy.ScoreInteractionsForMetaAutonomy` (B13) calls `AutShadowNative.Config(5)` / `Config(6)` (switch `autledger`, default off). B13 may yield, so a scope belongs to the running script task (id at `MonoScriptHost +0xBD0`, docs/119 §11.2); calls from other tasks during a yielded B13 count as outside |
| I-121-5 | `lgIn[0]` / `lgOut[0]`: RNG draws inside and outside meta scoring, plus up to 32 in-scope caller chains (8 interpreter frames each, the frame walk of `tools/probe/agent.js` `interpFrames`, named by the agent). The report flags chains that pass neither `Relationship.GetBFFOrSimWithHighestLikingForPet` nor `Localization.FillInTokens` |
| I-121-7, I-121-12 | `lgCls`: the Mono class of the `System.Type` argument of each in-scope query call (MonoReflectionType `+8` -> MonoType, first word = MonoClass [inferred Mono 1.x layout]), with call counts |
| I-121-9 | `lgIn[1]` per minute and per scope (the IC's body calls the slot helper `0x76F0A0`, §2.7), distinct object ids (`lgSlotIds`, up to 64) and the caller chains, which name the scoring classes that ask |

Reader: `node tools/harness/mono-ledger-report.ts <queue>-<date>-bp-mono-ledger-native.json`; `queue.ts` prints the same lines under the result table. Limits: the scope is the whole B13 (meta `Test`, `CalculateScore` and the implementors' `HowMuchItWantsSimToCome`), not each `CalculateScore` frame; Bridgeport may have no bake sale table, so I-121-12 can come back with no in-scope calls (then rerun on a save with one). Counters: `ts3native_autshadow` v3 (docs/registry.md).

## Reproduce

```sh
# managed closure (same as docs/118 §5), one root
node tools/managed-callgraph.ts def Sims3.Gameplay.Objects.RabbitHoles.AutonomouslyGoForMealInRestaurant+Definition --cut "Sims3.Gameplay.Seasons.SeasonsManager::LazyAllocateManager(,Sims3.Gameplay.Socializing.Relationship::Get(" --ics --index <repo>/game-copy/analysis/callgraph/index.json --asm <repo>/game-copy/assemblies
node tools/managed-callgraph.ts query Sims3.Gameplay.Interactions.InteractionDefinitionUtilities::CommonTests --cut "<as above>" --ics --index ... --asm ...
node tools/managed-callgraph.ts overrides Sims3.Gameplay.Interfaces.IMetaObject::HowMuchItWantsSimToCome --index ... --asm ...
# native bodies (copy project, read-only)
analyzeHeadless <repo>/game-copy/ghidra-ac121 TS3 -process TS3W.exe -noanalysis -readOnly -scriptPath tools/ghidra -postScript DecompileTo.java <out>/ics-d2.c 2 0x741230 0x7419B0 ... 0x741130
analyzeHeadless ... -postScript DecompileTo.java <out>/host-d1.c 1 0xD74240 0xD70B50 0xD71000 0xD798F0 0x7499B0 0x76F0A0 0xB499D0 0xC688A0
# §8.1: the 120 scoring functions as roots (ids "Ns.Outer+Inner::Method" from the decompiled class nesting), then query ... --ics
# §8.2: overrides of IMetaObject::HowMuchItWantsSimToCome, then query <the six implementors> --cut "<as above>" --ics
# §8.3/§8.4 (copy project, read-only)
analyzeHeadless ... -postScript XrefsTo.java <out>/xr.txt 0xC688A0 0xC68680 0x10786C8 -postScript OffsetWrites.java <out>/ow.txt 0x1CC
analyzeHeadless ... -postScript StoreScan.java <out>/ss.tsv 4 <the 33 IC bodies and host targets> -postScript DecompStores.java <out>/ds.tsv 4 <same> '!0xC68680' '!0x4E16F0'
# §8.5
node tools/mono-host-ics.ts --summary --index <repo>/game-copy/analysis/callgraph/index.json --asm <repo>/game-copy/assemblies
```
