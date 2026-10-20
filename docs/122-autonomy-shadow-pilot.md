# 122. Autonomy meta-scoring shadow pilot: build design (`[AutShadow]`)

Written 2026-10-20. Static design only: no ts3native build, no game run. It builds on M5 (the second Mono thread, docs/120, docs/30 P1-P4) and turns the docs/121 IC closure into a concrete pilot: a worker thread scores meta autonomy in **shadow** (results compared with the sim thread's, never used). Model: the routing snapshot (docs/94: cut, records, shadow compare classes, phased gates). Mono stays parked behind routing (memory "Mono parked"); this is the ready-to-build design, not a start.

Inputs: docs/118 (plan M0-M6, gates G0-G6), docs/119 (offline host, §8-§9), docs/120 (M5 static check), docs/121 (closure incl. §8) and `docs/data/autonomy-proxy-ics.csv`, docs/59 §2.5 and §3, docs/75, docs/97, docs/30 §5, docs/76 and docs/79 (change hooks), docs/94 §4-§5. Tags: **[verified, docs/N]** = verified there, not re-read; **[inferred]** = reasoning still to confirm (ledger docs/51 §146, I-122-*); **[design]** = a choice made here. Effort in CLAUDE.md units (wall-clock for Claude with agents; game runs and clean nights listed apart).

Naming: the Mono plan already uses M0-M6 for steps, so the shadow match classes here are **A0-A5** (docs/94 calls its routing classes M0-M5). Gates are **G-A0 to G-A6**.

## Summary

- **What runs on the worker:** the meta phase only (80% of manager-task scoring, docs/75 §1), for an allow-list of 13 meta definition types and the 7 `IMetaObject` implementors (docs/121 §8.2). A ts3fixes driver `AutShadow.ScoreMeta` replays the game's B14/C1/C2 loop (docs/59 §1.2) per meta object into its own result arrays: no `BestScores`, no `InteractionCheckTable` static pools, no yield, no stopwatch.
- **Two compare modes [design]:** **Mode R** (reference): at the cut, the sim thread runs the same driver on live data, then the worker runs it on the snapshot in the quiescent window; the managed state is the same for both, so every score must be bit-equal (A0) and any difference is a snapshot or shim bug (the docs/94 option G logic: equal by construction). **Mode W** (worker only): the worker result is compared with the game's own meta search (records from a C1 hook); differences there measure staleness for a later go-live, not bugs.
- **Snapshot (per cut, sim thread):** object rows (~100-250 x 80 B), lot table (~150 lots x 24 B), per-lot (id, class, ref) lists for the scored lots with generation counters, world constants, and a learned-key memo for session-lifetime ICs. Rows are filled by calling **the game's own IC bodies** on the sim thread, so answers are bit-equal by construction. Change tracking reuses routesnap's `World_FireEvent` detour and object-event nodes (docs/76, docs/79) as one more consumer. Budget: cut p95 ≤ 0.3 ms, ≤ 2 ms/s at the sampled rate, snapshot memory ≤ 256 KB.
- **Shims:** every EA IC slot gets a guard thunk (sim thread: tail jump to the game body; worker: the shim or abort). 28 snapshot/memo shims, 6 tooltip stubs (one rooted empty string), aborts for the RNG, slot positions, `Slots_CanAttachToSlotImpl` and `Query_GetObjects` (until I-121-7), abort for every IC not on the list. Three managed worker shims in ts3fixes: `Sim.IsAllowedInRoom` (hit = read, miss = abort, never store), `Sim.NumSimsDoingGardening` (read-only), `Localization.LocalizeString` (empty string, no RNG draw).
- **Gates:** G-A0 static checks, G-A1 offline shim selftest, G-A2 offline two-thread run in `native/monohost`, G-A3 in-game count and audit (Mode 0), G-A4 Mode R bit-equal, G-A5 Mode W stability, G-A6 cost A/B and the measured break-even (§7).
- **Effort:** ~17-29 h for this pilot on top of M5 (docs/120 §6: 9-14 h), so **~26-43 h** from today, **7-10 game jobs and 2-3 clean nights** (§9). docs/121 §5 said 19-35 h; the extra is Mode R and the incremental-vs-full audit, which buy a bug-level signal before any cost run.
- **G-A0 static checks done (2026-10-20, §12):** 78 binder bodies are in the closure (no engine reads; 59 enter the interpreter through `0xD8A8E0`'s global call-record list, one writes `[0x11F2D84]`): new worker path F1 and abort F2. Three more managed W sites (online-dating records, punishment manager, reflection desire function): hooks H4-H6. 7 of the 8 extra definitions in, `VisitWithDefinition` out. No hook owner conflict; mod guard per method, not per session. +4-5 h.
- **Part 1 built offline (2026-10-20, §13), G-A1 passed:** guard thunks on every provider-table slot, a generic snapshot (answers from the game's own IC bodies, A/B versions, learned keys, audit), change tracking as one more consumer of routesnap's event layer, F2 at `0xDA9790`; selftest T1/T2/T4 pass. Part 2 (cut, worker, ts3fixes driver, compare) waits for M5 F1.
- **Part 2 built offline (2026-10-20, §14), G-A2 passed in monohost:** cut and window on `[MonoThread]`'s slot stub, sim-thread reference with the RNG restored, worker dispatch through `MtSubmit`, `AutShadowNative` ICs, rooted answer column and array allocator (part 1's MonoArray length offset was wrong: fixed), `simstale`; ts3fixes driver, H4-H6, worker hooks, per-method mod guard, Mode R/W compare. Host: 200 requests x 50 objects, 8,400 of 8,400 A0 with the main thread interpreting, allocating and collecting. Next: Mode 0 and Mode R in game (`autshadow-first.json`).
- **Sim-thread ICs answered (2026-10-20, docs/125):** RNG by per-unit MT streams (driver `RngUnit` marks, contract Version 3), slot positions and `Query_GetObjects` as learned keys re-asked at every cut; monohost G-A2 now A0 10,000 of 10,000 with 0 `rng` aborts. Only `Slots_CanAttachToSlotImpl` still aborts by design.
- **Biggest risks:** EA native method bodies (the binder's 1,085, docs/119 §9.4) inside the closure bypass IC shims; hidden managed writes from the driver's closure into shared objects; the quiescent-window premise; the cost of guarding every IC and of the M5 lock (§10).

## 1. Scope and flow

### 1.1 What is in and out

| In | Out (stays on the sim thread, unchanged) |
|---|---|
| Meta phase B13/B14 pairs of allow-listed definitions on allow-listed implementors | local lot scoring (B9-B12), socials, posture, side entry points (`FindBestAction*`) |
| `IMetaObject.HowMuchItWantsSimToCome` for the 7 implementors (exact runtime type) | mod subclasses of any implementor or definition (runtime type + assembly MVID miss) |
| Scoring functions on the docs/121 §8.1 allow-list, only if tuning names one (I-121-10) | `Slots_CanAttachToSlotImpl` paths (eating scoring function: abort) |
| Reads of managed state in place, inside the quiescent window (docs/59 §3.5) | rain, hail and deep snow searches (the C8 shelter path builds an `InteractionInstance`, docs/59 §3.2) |

Allow-list v0 [design]: the 5 roots docs/118 §5 closed (`VisitCommunityLot`, `VisitLot`, `GoToGarden`, `GoToCommunityLot`, `AutonomouslyGoForMealInRestaurant`) plus the other 8 of docs/75 §1's 13 (`VisitCommunityLot` date variant, `VisitCommunityLotWith`, `VisitLotWithDate`, the 5 other venue-scored rabbit-hole visits) once static step S2 (§8) shows their closures add no IC outside §3. File: `docs/data/autonomy-allow.csv` (definition type, MVID, closure size, ICs, verdict, first reason), the file docs/59 §3.6 and docs/97 §2 (12e) planned.

### 1.2 One shadow search

| # | Thread | When | Work |
|---|---|---|---|
| 1 | sim (managed) | entry of `ScoreInteractionsForMetaAutonomy` (B13), sampled (1 in `Sample`) | ts3fixes prefix hook builds the **request**: actor, the meta object list B13 would score (public meta objects + known lots, current lot, home lot, career location; the same order), search id, tick. Request lives in a rooted ts3fixes static (`AutShadow.sReq`); `AutShadowNative.Post(id)` tells ts3native |
| 2 | sim (managed) | during the game's own B13 (Mode W, and Mode R for the L report) | C1 hook records the game's (meta object id, definition type, target id, score, pass) for the sampled search into `sGame` |
| 3 | sim (native) | **cut**: return of `MonoScriptHost_Simulate` `0xD81840` (end of the iteration's script work, GC step and finalizers; the point docs/59 §3.2 step 3 names) with a request pending | build or update the snapshot (§2), per-cut pre-warm (§3.5), bump the version; Mode R: invoke `AutShadow.ScoreMeta(sReq, sRef)` through `0xE5A600` on the sim thread with the script RNG state saved before and restored after (§3.4); then open the window flag and wake the worker |
| 4 | worker | quiescent window | `AutShadow.ScoreMeta(sReq, sWork)` through `0xE5A600` (docs/30 §5); IC calls hit the guard thunks and are answered from the snapshot; aborts are per meta object (§4.3) |
| 5 | sim (native) | entry of the next `Simulate` | close the window flag; if the worker is still inside, set its stop flag (no wait) and mark the run **overrun** (A5) |
| 6 | sim (managed) | next request or next tick | `AutShadow.Compare` classifies (§5), bumps ts3fixes counters, appends mismatch records to a ring that ts3native drains to `autshadow.jsonl` |

The worker never yields, never calls `Simulator.*`, never writes the request's game objects (§4.4), and its results are only compared.

## 2. Snapshot

### 2.1 Cut point and versions

- **Cut:** one per pending request, at the return of `0xD81840` [design]. Between this point and the next `Simulate` entry the sim thread runs native animation, locomotion, publish and frame waits (docs/59 §3.5 [inferred, to measure]); the native state keeps moving there (docs/121 §6 item 2), which is why object answers are copies.
- **Versions:** two snapshot buffers (A/B), the worker reads the one published at its cut; the next cut writes the other. A buffer is reused only after the worker released it (worker sets `done` with `Interlocked`). Native memory (`VirtualAlloc`, 2 x 128 KB reserve); the managed-reference column lives in a `GC_malloc_uncollectable` block (root by kind, docs/120 §1.2), never in `malloc` memory.
- **Epoch:** routesnap's world epoch (docs/76 §2: world events 2, 3, 5, 7, 9, 0x0A, lot events 0x0D-0x10, build/buy boundary slot swaps of `World_LotClusterTreesImpl` slot `0x11623B0` and `RouteManager_OnExitBuildBuy` slot `0x1160DA8`) forces a full rebuild and drops any in-flight run (A5).

### 2.2 Rows per IC (from docs/121 §2, §8 and the CSV)

| Part | Fields | Feeds ICs | Filled by (sim thread, at cut) | Lifetime / invalidation |
|---|---|---|---|---|
| **Object row** (~80 B) | id; position (12 B, as `Objects_GetPosition` returns it); parent id; root parent; lot id; room id; tomb room id; level; resource key (16 B); outfit key (Sims, 16 B); flags: hidden (bit 0 of `+0x40`), outside, roof/floor above, stays-after-evict, in world, destroyed; managed ref (index into the uncollectable column) | `Objects_GetPosition`, `Slots_GetParentIdImpl`, `World_ObjectGetHiddenFlags`, `ScriptProxy_GetResourceKeyImpl` (and `World_ObjectGetProductKey`, same body `0x7872E0`), `Simulator_GetTaskImpl`, `World_GetObjectTombRoomIdImpl`, `World_StaysAfterEvictImpl`, `CASUtils_GetOutfitInGameObjectImpl`; position-keyed: `World_GetRoomIdAtPosImpl`, `World_IsPositionOutsideImpl`, `World_HasAnyRoofOrFloorAbovePosImpl` when the asked position is bit-equal to a row's position | the saved original IC bodies, called natively with the row id (and the row's position for the three geometry ICs) | dirty on object events `0x0E`, `0x21`, `0x12`, `6`, `0x18`, `4`, `0x0B`/`0x0C`; Sims and objects whose root parent is a Sim always dirty; grandchildren through our own parent index (docs/79 §1); geometry flags also on the build/buy epoch |
| **Row set** | all Sims in `LotManager.Actors`; every object in the request's meta list; doors of the scored lots; plus a **learned set**: ids the worker asked for and missed (added at the next cut, dropped after 100 cuts unused) | as above | request (managed list -> ids), learned set | per cut |
| **Slot contents** (per row, `BakeSaleTable` and the classes the scoring functions name) | (slot name hash -> contained id) for the containment slots | `Slots_GetContainedObjectImpl`, `Slots_GetContainmentSlotNamesImpl` (names by resource key, session memo) | learned keys re-asked at each cut through the original bodies | per cut; names per session |
| **Lot table** (~24 B x 100-200 lots) | lot id, `World_GetLotLocationImpl` answer, managed lot ref | `World_GetLotLocationImpl` | original body per lot | lot events `0x0D`-`0x10`, `0x46`, epoch |
| **Door pair** (4 B per door) | the two room ids | `World_GetDoorRoomsImpl` (the worker allocates the `int[2]`) | original body per door of each scored lot; avoids the `0xC89500` lazy-cache write on the worker (docs/121 §8.4) | build/buy epoch, lot load |
| **Per-lot object list** (~12 B per object) | (object id, `MonoClass*`, ref) for every object in the lot vector (`lot+0x14`, docs/121 §2.3) | `Query_CountObjectsOnLot`, `Query_GetObjectsOnLot` (filter by class assignability: a read of the already-initialised class hierarchy [inferred]) | walk of the lot vector on the sim thread, class from the managed ref | per-lot generation: object events `4`, `6`, world event `0x11` `OnObjectLotUpdate` (`0xC8A290`), lot events; only dirty lots are re-walked |
| **Sims by position** | Sim rows (above) | `Query_GetObjectsInRange` for `Sim` (`BakeSaleTable` only) | none extra; the shim filters Sim rows by squared distance [design: the boundary compare may differ from the native one; Mode R shows it, I-122-8] | per cut |
| **World constants** | world name, world type, name key (rooted string), sea level; sunrise, sunset, moon phase | `GameUtils_GetWorldName`, `GameUtils_GetWorldType`, `World_GetWorldNameKey`, `World_GetSeaLevelImpl`, `World_GetSunriseTime`, `World_GetSunsetTime`, `World_GetMoonPhaseImpl` | original bodies | load (first 4), every cut (last 3: three field reads) |
| **Session memo** (learned keys) | (IC, argument key) -> answer | `DeviceConfig_GetOption`, `GameUtils_IsWorldInstalled`, `UserToolUtils_GetProductImpl`, `UserToolUtils_ProductGenericImpl`, `World_GetLotName`, `World_GetLotNameKey` (strings rooted), `DeviceConfig_GetProcessorLevel` | **recorded on the sim-thread pass-through** of the guard thunk when the game itself calls these ICs (no extra call); worker miss = abort + key logged | session; lot names also cleared on lot events and loads |

`Query_GetObjects` (class d) is not in the snapshot: the worker aborts and logs the requested class, which answers I-121-7 as a side effect. If the classes are few and rare, a per-class list fed by object events `4`/`6` makes it class a in a later round.
**Update 2026-10-20 (docs/125):** the RNG, slot positions and `Query_GetObjects` are no longer aborts. The RNG draws per-unit streams (owner `AO_RNG`, `RngUnit` marks from the driver, contract Version 3); slot positions and `Query_GetObjects` are learned keys re-asked through the game body at every cut (`AO_CUT`), `Query_GetObjects` answers kept up to 256 objects. `Slots_CanAttachToSlotImpl` still aborts.

### 2.3 Size and cost budget [inferred]

| Part | Size | Cut cost |
|---|---|---|
| Rows: 100-250 x 80 B | 8-20 KB | Sims always dirty: ~50-150 Sims x ~8 IC bodies x ~0.2-0.4 µs = **~80-480 µs** on a full Sim refresh; non-Sim rows only when dirty |
| Per-lot lists: ~30 scored lots x 50-300 objects x 12 B | 18-108 KB | dirty lots only: typically 0-3 lots x ~20-60 µs |
| Lot table, doors, constants, memo | < 10 KB | < 20 µs |
| **Total** | **~30-140 KB per version, 2 versions ≤ 256 KB** | **target p95 ≤ 0.3 ms per cut** |

The Sim refresh dominates. Pilot rule [design]: refresh only the Sims the request can reach (actor, Sims on the scored lots via the per-lot lists, Sims within the `BakeSaleTable` radius) and mark the rest stale-with-abort (a worker read of a stale Sim row aborts with reason `simstale` and adds it to the learned set). At `Sample` = 4 and ~10-30 meta searches/s [inferred; the rate is a G-A3 output], 2.5-7.5 cuts/s x ≤ 0.3 ms = **≤ ~2 ms/s**. docs/94's routing cut has the same 2 ms/s cap (G3).

### 2.4 Invariant and audit (docs/94 §4.2 pattern)

- **Invariant S-A:** at every cut, the incremental snapshot equals a full rebuild from live data (rows, lists, lot table, constants).
- **Audit** (`Audit=N`, default 50): every Nth cut also builds a full snapshot into a scratch buffer and compares field by field; a mismatch logs the part, id, field, and the last 32 change events for that id. Classes as docs/94: S1 missed event (coverage gap), S2 lazy state, S3 off-sim writer, S4 epoch race.

## 3. Shim layer (ts3native `autshadow.cpp`)

### 3.1 Guard thunks at the binding table

- EA ICs are P/Invokes resolved lazily by name through Mono's `icall_hash` `[0x11F436C]`, filled once in `MonoScriptHost_Init` from each provider's `{function, name}` table; native method bodies bound by `0xE3E570` read IC pointers at bind time (docs/119 §9.1, §9.4 [verified there]). So the swap must happen in the provider tables **before** `MonoScriptHost_Init`, as routesnap's 39 slot swaps already do (docs/94 §10) [design: same mechanism, same install point].
- `Guard=all` (pilot default): one 10-byte thunk per exe IC name (`push index; jmp guard`, the docs/119 §9.1 P3 layout, 1,775 names). `guard` compares `fs:[0x24]` (thread id) with the worker id: sim thread -> `jmp original[index]`; worker -> `shim[index]` if set, else **abort** with reason `ic:<index>` and the neutral value for the IC's return type (from `methods.csv` signatures: `float`/`double` via `fldz`, the docs/119 risk 2). Sim-thread cost ~2-3 ns per IC call [inferred; G-A3 measures it]. Fallback `Guard=closure` (only the 129 ICs of `tools/mono-host-ics.ts`, docs/121 §8.5) if the Mode 0 A/B shows > 1 ms/s.
- **One owner per slot** (docs/68 rule 3): slots routesnap already swaps (portal, lock-key, route ICs) are guarded from inside routesnap's stub, not swapped twice. `tools/hook-sites.ts --check` gets the new rows.
- Mono runtime (corlib) ICs are not EA provider entries and stay unguarded; they are covered by the M5 rules (allocation under the P3 lock; no `GCHandle`, weak reference, reflection, `Monitor` reliance; docs/120 §3.3) and by static step S1 (§8).

### 3.2 Worker answers, by docs/121 class

| docs/121 class | ICs | Worker answer |
|---|---|---|
| a, object rows | 6 + `StaysAfterEvict`, `GetOutfitInGameObject` | row lookup by id in our own open-addressing table (not the game's hash, docs/59 §3.4); id not in the row set -> abort `row` + learn |
| a, geometry | `GetRoomIdAtPos`, `IsPositionOutside`, `HasAnyRoofOrFloorAbovePos` | answer only for a position bit-equal to a row's position (plus level); else abort `pos` (counted by caller method name from the interpreter frame, to size a later extension) |
| a, lot | `GetLotLocation`, `GetDoorRooms` | lot table, door pair (allocates the managed `int[]` on the worker under the P3 lock) |
| a, lists | `CountObjectsOnLot`, `GetObjectsOnLot`, `GetObjectsInRange` (Sim) | per-lot list filtered by class; the result array is allocated on the worker (its array class must already exist: Mode R and the sim's own calls create it first) |
| a, slots | `GetContainedObject`, `GetContainmentSlotNames` | slot contents per row; names from the session memo |
| a, constants and memos | 7 world constants, 7 memo ICs | snapshot constants; memo hit, else abort `memo` + key logged |
| a-stub | `EAText_GetMoneyString`, `EAText_GetNumberString`, `EAText_GetTimeString`, `LocalizedStringService_GetLocalizedStringByString` (both bindings), `OnlineFeaturesHost_GetPersonaName` | one preallocated, rooted empty string [inferred safe: I-121-4] |
| b | `Random_NextDoubleImpl`, `Slots_GetPositionOfSlotImpl`, `Slots_CanAttachToSlotImpl` | abort `rng` / `slotpos` / `attach`; since docs/125: per-unit RNG streams, slot positions as learned `AO_CUT` keys, `attach` still aborts |
| c | `UTFConnectorSimulator::Simulator_GetTaskImpl` | not registered in the game (docs/119 §9.1): stays unregistered |
| d | `Query_GetObjects` | abort `query` + Mono class name logged (I-121-7); since docs/125: learned type key (`AO_CUT`), ≤ 256 objects kept, larger types abort `query` |
| not on the list | everything else, incl. `StopWatch_*`, `Simulator_SleepImpl`, `GetCurrentTaskImpl` | abort `ic:<name>` (Verify: should be 0 on the allow-listed path) |

The "lot-at-position grid live read under lock `0x10786C8`" fallback of docs/121 §8.3 is **not** used in the pilot: every geometry answer comes from the copy, so the worker never touches the grid, the terrain reentrancy byte or a handle refcount (docs/121 §8.4).

### 3.3 Abort semantics

The guard sets `abortReason` (first one wins) and returns the neutral value; the managed code keeps going and may throw (null result). The driver's per-object `try/catch` and its check of `AutShadowNative.Aborted()` after each meta object mark that object A4 with the first reason and reset the flag. A stop flag from the sim (window closed) makes the next guard call and the next object boundary abandon the whole run (A5). No wait, no exception thrown from native code [design: avoids `mono_raise_exception` from the worker and game `catch` blocks that would swallow it, docs/119 risk 1].

### 3.4 RNG

The worker never draws: `Random_NextDoubleImpl` aborts (docs/121 §4.1 option a). Mode R's sim-thread reference run copies the Mersenne Twister state at MonoScriptHost `+0xC30` (624 words + index, docs/27 §1.1 [verified there]) before the run and restores it after, so the reference leaves the sim's RNG stream unchanged [design]. Draws inside the reference run are still counted, and the matching worker objects are A4 `rng`.
**Superseded 2026-10-20 (docs/125 §2):** the worker draws from per-unit Mersenne Twister streams seeded from (cut seed, unit) with the game's own `0x4FA990`/`0x4FAA70`; the reference re-seeds the host MT per unit inside the same save and restore, so both sides draw the same values per unit and Mode R stays bit-equal (monohost: A0 10,000 of 10,000, 0 `rng` aborts).

### 3.5 Managed worker shims and pre-warm (ts3fixes `AutShadow.cs`)

| Hook (one owner each; shared with AM-1 where it exists, docs/97 §7) | Sim thread | Worker (`AutShadowNative.IsWorker()`, a ts3native IC registered with `mono_add_internal_call` `0xE82550` before ts3fixes first calls it) |
|---|---|---|
| `Sim.IsAllowedInRoom(ulong, int)` (AM-1 H2's site) | unchanged (or AM-1's memo) | lot has any `OnAllowedInRoomCheck` callback -> abort `privacy`; per-Sim cache hit -> answer; miss -> abort `roomcache` (never `RecalculateAllowedRooms`, a W-class write, docs/97 §4) |
| `Sim.get_NumSimsDoingGardening` | unchanged | return `sNumSimsDoingGardening`, no clock write (I-121-15) |
| `Localization.LocalizeString` (all overloads reached) | unchanged | the rooted empty string; removes `FillInTokens`' RNG draw (docs/121 §2.6) |

**Pre-warm on the sim thread:**

| When | Call | Why (docs/121) |
|---|---|---|
| world event 7 (load finished; routesnap already sees it) | `GameUtils_GetWorldName`/`GetWorldType` IC once each; managed `GameUtils.GetCurrentWorldType()` | lazy fills `+0x60`/`+0x64`; `WorldNameToType` add on first miss (§2.4, §4.2) |
| same | `RandomGen.gRandomGen` getter | lazy `AppDomain.GetData` fill (§4.1) |
| same | one lot-at-position query (any lot's centre) | clears the grid dirty byte so the first cut's builder calls do not pay a rebuild (§8.3); the worker never reads the grid |
| same | allocate the rooted empty string and the result arrays; resolve `AutShadow` methods and the 7 implementor and 13 definition classes (class init, vtables) | docs/30 §5 step 6, docs/120 §3.3 |
| each cut | `Sim.NumSimsDoingGardening` getter once | refreshes the count the worker shim returns |
| first `WarmIn` (default 20) requests of a load | Mode R reference before the worker run, even in Mode W | runs every method and IC signature the worker will run on the sim thread first (transform, thunk hash `0x11F4128`, mscorlib lazy statics: docs/120 §3.3); with equal data the worker takes the same paths |
| always | worker runs only while `[0x11DC18C] == 0` | docs/120 §2.2 tracker-lock rule |

### 3.6 What the driver must not do (static step S1 checks it)

`AutShadow.ScoreMeta` replaces B14/C1: it walks `MetaAds`, applies the commodity filter, and calls C2 (`InteractionObjectPair.TestAndCalculateScore`) on a **pair object it allocates itself** unless S1 shows C2 writes nothing into the pair [design; I-122-2]. It never touches `InteractionCheckTable` (static pools, C1a), `BestScores`, `CacheValuesToOptimizeTestFunctions` (C0 writes the actor), `YieldIfPossible`, `StopWatch`, events or delegates other than the allow-listed `Predicate` lambdas and scoring functions.

## 4. Dispatch

### 4.1 Allow-list check (per meta object and ad)

1. Implementor: exact runtime type in the 7 (`Lot`, `RabbitHole`, `HomeMetaObject`, `WorkMetaObject`, `GardeningMetaObject`, `WorldPondHelper`, `BakeSaleTable`) and its assembly MVID equal to the allow-list file; else the object is `skip` (not compared).
2. Definition of each meta ad: exact runtime type and MVID in `autonomy-allow.csv`; else `skip` for that ad.
3. Scoring function (if tuning names one): method + MVID in the allow-list; else abort `sf`.
4. Session guard (docs/104 mod guard, docs/59 §3.6): game assembly MVIDs differ from the file -> `[AutShadow]` off for the session, reason logged. A loaded run-time patcher (`MonoPatcher.asi` is on the reference install) does **not** switch it off: per-method docs/104 checks over the closure mark the affected definitions `skip` (G-A0 §12.4).

### 4.2 Hand-over

Request and results are managed arrays rooted in ts3fixes statics (`sReq`, `sRef`, `sWork`, `sGame`), allocated on the sim thread at warm-up and reused; the native side only sees the request id and the window/stop/done/abort words (native globals with `Interlocked`, since managed `lock` is a no-op in this build, docs/120 §3.3). Result layout per (meta object, ad): `float score`, `int code` (0 pass, 1 Test failed, 2 skip, 3 abort + reason index), plus per object `float metaScore` (`HowMuchItWantsSimToCome`). One worker, one request in flight; a new request while busy is dropped (`asBusy`).

### 4.3 Modes

| Mode | Worker | Sim-thread reference | Compare |
|---|---|---|---|
| 0 | none | none | counts only: requests, cut cost, audit, IC pass-through hit list per definition (answers I-121-3/7/9/10, the docs/118 "C live" probe) |
| 1 = R | yes | yes (sampled requests) | A-classes against the reference |
| 2 = W | yes | only for `WarmIn` | L report against the game's own search (§5.2) |

## 5. Compare

### 5.1 Mode R classes (bit-level, per (meta object, ad))

| Class | Meaning | Rule |
|---|---|---|
| A0 | same code, scores bit-equal, meta score bit-equal | pass |
| A1 | same code, score differs by ≤ 1 ULP | must be 0; each case explained (same interpreter, same data: it should not happen) |
| A2 | same code (pass), score differs beyond 1 ULP | **0** |
| A3 | code differs (pass vs fail) | **0** |
| A4 | worker aborted (reason) or skipped | counted by reason; target < 20% of allow-listed pairs in the pilot |
| A5 | overrun, epoch bump or guard trip (sim entered managed code in the window) | not a mismatch; counted to size the window |

A record per A1-A3 (JSONL): request (actor id, meta object id, definition type, target), both scores and codes, snapshot version, change events since the cut, and with `DumpOnMismatch=1` (cap 20 per run) the snapshot buffer, so the IC answers can be checked offline against the row builder (§6 T2).

### 5.2 Mode W report (L, informational)

Per sampled search: pairs both scored, equal-score share, Test agreement, and **top-1 agreement** (the worker's best allow-listed pair vs the game's best among the same pairs). The game scores some objects before the cut and some in later ticks (B13 yields every 5 objects and 1 ms, docs/59 §1.3 Y2), so differences mix staleness and bugs; Mode R has already separated out the bugs. L feeds the go-live decision (docs/118: separate, after G6), not a gate.

### 5.3 Counters and logs

- ts3native: reserve one counter version in `docs/registry.md` at build time (not here). Fields: `asReady`, `asMode`, `asReq`, `asBusy`, `asCut`, `asCutUs` (sum, max), `asAuditN`, `asAuditMis[4]` (S1-S4), `asRows`, `asLotsDirty`, `asSnapBytes`, `asWorkUs`, `asWindowUs`, `asOverrun`, `asAbort[16]` (row, pos, memo, rng, slotpos, attach, query, ic, sf, privacy, roomcache, simstale, exc, stop, other), `asGuardSim`, `asGuardWorker`, `asUnguarded` (worker reached an IC with no thunk: must be 0), `asWorkerClassInit` (docs/58 Verify by thread: must be 0).
- ts3fixes counters (reserve a slot range): `asA[6]`, `asL[4]` (pairs, equal, test agree, top-1 agree), `asSkip`, `asWarmIn`.
- Files: `autshadow.jsonl` (mismatch and audit records), abort-key log (IC name, caller method, missing id or key; capped), `autshadow-dump-*.bin` on mismatch.
- Switches (ts3fixes convention): `autshadow` (on/off for `--ab`), `autshadowverify`. Ini `[AutShadow] Enabled=0 Mode=0 Sample=4 Guard=all Audit=50 WarmIn=20 DumpOnMismatch=0`.

## 6. Test plan

### 6.1 Offline (no game; `native/ts3native` selftest and `native/monohost`, docs/119)

| Test | Where | What | Pass |
|---|---|---|---|
| T1 guard thunks | monohost (bare host, all 1,775 names registered as in docs/119 §9.1 P3) | call a sample of ICs per return type from the main thread (tail jump to a fake body) and from a second thread marked worker (shim or abort, neutral value; x87 stack balanced) | 0 failures; FP stack depth 0 after every call |
| T2 snapshot lookups | selftest | row table, per-lot lists with class filter, memo learn/miss, A/B versions, audit compare on synthetic rows; incremental vs full on random event sequences (docs/94 K1 style) | invariant S-A on seeds 1-4 x 3,000 |
| T3 two threads | monohost + M5 P1-P4 (docs/120 §6 d) | a test assembly calls ScriptCore wrappers that reach shimmed ICs (`World.GetLotLocation`, `Queries.GetObjectsOnLot`) on the worker while the main thread allocates and forces collections | 0 mismatches against the snapshot answers, 0 faults over 10k iterations (doubles as docs/118 G5 offline) |
| T4 abort and stop | monohost | worker hits an abort IC and an unlisted IC; main thread sets stop mid-run | run ends at the next boundary; flags reset; no leaked buffer |

Real scoring cannot run offline: the bare host has no world (docs/119 §4.3, §9.4 risk 4). That is what game Mode R is for.

### 6.2 Game jobs (clean-night queue for timing runs; memory: clean night testing, fail-fast tests)

| Job | Mode | World | Length | Pass |
|---|---|---|---|---|
| `bp-autshadow-count`, `ip-autshadow-count` | 0 | BP, IP | 10 min | G-A3 |
| `bp-autshadow-r`, `ip-autshadow-r`, `sv-autshadow-r` | 1 | BP, IP, SV | 10 min | G-A4 (≥ 2,000 compared searches per world over the runs) |
| `bp-autshadow-r-edit` | 1 | BP + repro (enter build, add a wall and a door, exit; Edit Town place a lot) | 5 min | epoch drops in-flight runs; 0 A2/A3 after |
| `bp-autshadow-w`, `ip-autshadow-w` | 2 | BP, IP | 10 min | G-A5 |
| `bp-autshadow-ab`, `ip-autshadow-ab` | 2 vs off, `--ab autshadow` | BP, IP | 18 min ABBA, clean night | G-A6 |

Repro entries in `tools/harness/repros.json` with agent actions (memory: repro actions): `aut-meta-burst` (several idle adult Sims, needs max, unpause so meta searches fire) is the main driver; a pet household for the `rng` abort path.

## 7. Gates

| Gate | After | Pass when | Fail means |
|---|---|---|---|
| **G-A0** | static steps S0-S3 (§8) | the closure from `AutShadow.ScoreMeta` hits no EA native body (or each is listed with its reads), no instance write into an object the driver did not allocate except the three hooked sites, every IC in the 13-definition closure has a §3.2 row, no slot owner conflict | fix the design before building |
| **G-A1** | shim build | T1, T2, T4 pass | fix before game |
| **G-A2** | M5 built (docs/120 a-d) | T3 passes (and docs/118 G5 offline) | M5 not ready; stop |
| **G-A3** | Mode 0, 2 jobs | 0 crashes; `asAuditMis` = 0 over ≥ 5,000 audits per world; cut p95 ≤ 0.3 ms and ≤ 2 ms/s; guard sim-thread cost ≤ 1 ms/s (else `Guard=closure`); live IC hit set ⊆ the static set (docs/118 GC) | add the missing event or IC; widen the closure |
| **G-A4** | Mode R | A2 = A3 = 0 and A1 = 0 or explained over ≥ 2,000 compared searches per world; A4 < 20% with every reason sized; `asUnguarded` = 0; `asWorkerClassInit` = 0; 0 crashes in two clean runs (crash rule) | the first A2/A3 record names the missing input; fix and rerun |
| **G-A5** | Mode W | ≥ 10k sampled searches per world; 0 crashes in two runs; overrun A5 < 5%; worker time p95 ≤ the measured window | window premise fails: record it; go-live needs another design (docs/97 §1's warning) |
| **G-A6** | cost A/B, clean night | sim-thread it/s not down more than ~2% vs off and ThreadedObjects ms/s not up more than ~2% (offload rule); 0 crashes; and the measured `asWorkUs` of non-aborted pairs gives f x T for the docs/59 §3.3 break-even | keep `[AutShadow]` off; write down the break-even numbers |

Go-live (worker results used) stays a separate decision after G-A6 with docs/59 §3.5's staleness rules, the docs/118 G6 thresholds and a determinism note (the worker skips tooltip RNG draws, docs/121 §4.1).

## 8. Static steps before the build (S0-S3)

| Step | Work | Output |
|---|---|---|
| S0 | Binder bodies: list the 1,085 `0xE3E570` keys (docs/119 §9.4: key string `<return> <Class>:<Method>(<args>)`) and intersect with the closure of `AutShadow.ScoreMeta`'s game callees; for each hit, its native reads (docs/62 style) | I-122-1; any engine read there needs a shim at the body or forces `skip` |
| S1 | `tools/managed-callgraph.ts`: tag `stfld` on objects not allocated in the closure (W class, docs/59 §3.6 rule 3) for the C2 closure of the 13 definitions and the 7 implementors | I-122-2, I-122-3; the list of worker hooks beyond §3.5 |
| S2 | `--ics` closure of the 8 definitions not in docs/118 §5 | allow-list v0; I-122-4 |
| S3 | `tools/hook-sites.ts --check` with the guard thunk table, routesnap's slots and the three managed hooks; S3SS/S3PP overlap (docs/68) | no conflict or an owner decision |

## 9. Effort (CLAUDE.md units)

| Step | Work | Estimate |
|---|---|---|
| S0-S3 | static checks and allow-list v0 | 1-2 h |
| M5 | docs/120 §6 a-d (prerequisite, not this doc's work) | 9-14 h + 1 load + 3 clean nights (docs/120) |
| Shim layer | guard thunks for all names, return-type table, ~38 shims, abort/stop words, `IsWorker` IC registration, owner sharing with routesnap | 3-5 h |
| Snapshot builder | rows through original IC bodies, row set and learned set, lot table, door pairs, per-lot lists with generations, constants, memo recording, consumer in routesnap's event layer, A/B versions, audit | 4-6 h |
| ts3fixes | request hook, `ScoreMeta` driver, three worker hooks, C1 record hook, compare, counters, switches | 3-5 h |
| Offline tests | T1-T4 | 2-3 h |
| Game rounds | Mode 0, R, W: build and fix rounds | 4-8 h |
| **Total for the pilot** | | **~17-29 h** (**~26-43 h** with M5) |
| Game runs | 2 count + 3-4 R + 1 edit repro + 2 W | **7-10 jobs** (~8-15 min each) |
| Clean nights | Mode 0 cost (shared with M5's Mode 0 night when possible), Mode W ABBA | **2-3** |

## 10. Risks

| # | Risk | Effect | Mitigation |
|---|---|---|---|
| 1 | EA native bodies (binder) inside the closure read engine state directly and use the IC nesting counter `[0x11F2D84]` | worker races the engine; shims bypassed | S0 before the build; Verify snapshot of `[0x11F2D84]` around worker runs (docs/59 §3.8) |
| 2 | Hidden managed writes from the driver's closure (pair objects, C0 caches, lazy statics) | a "shadow" that changes the game | S1 scan; driver allocates its own pairs; Mode R makes the sim do every lazy fill first |
| 3 | Window premise fails (remote-call pump, clip events, host invokes on the sim thread in the window) | stale reads, managed exceptions on the worker | A5 counts overruns; `InvokeHook`/`HostCallHook` (existing ts3native counters) trip the stop flag; shadow-only, so the effect is a lost compare |
| 4 | M5 stability (docs/120 §5: unlisted lock in a stopped world, mscorlib first-use races, incremental GC with two mutators) | crash or hang | G-A2 offline first; Mode R warm-in; watchdog (docs/59 §3.8); crash rule |
| 5 | Sim-thread cost: guard on every IC call, P3 gate (docs/30 §8: 0.6-2%), cut | the pilot fails G-A6 even if correct | `Guard=closure`; sampling; the M5 cost is shared with any other worker load |
| 6 | Abort rate (positions not equal to a row, `roomcache` misses, Sims stale) too high | few compares, weak signal | A4 by reason and caller decides the next extension (per-cut learned positions, room table precompute) |
| 7 | Mode R reference changes vanilla state (cache fills, gardening recount) | seeded A/B runs drift | RNG restored; R never used in a cost A/B; same switch on both halves for seeded runs |
| 8 | Mods replacing definitions or patching methods | wrong closure on the worker | MVID allow-list, session guard (docs/104) |
| 9 | Value | meta pilot proves correctness but go-live may not break even (docs/97 §1: -16 to +7 ms/s) | G-A6 measures f x T directly; stop there if below break-even |

## 11. Ledger

Open claims: docs/51 §146 (I-122-1 to I-122-11); G-A0 results: docs/51 §150 (I-122-12 to I-122-19); part 1 build: docs/51 §156 (I-122-20 to I-122-28); part 2 build: docs/51 §159 (I-122-29 to I-122-37).; RNG, slot position and query answers (docs/125): docs/51 §166 (I-125-1 to I-125-9).

## 12. G-A0 results (2026-10-20, static)

Static only: no ts3native build, no game run. Sources: `TS3W.exe` copy (`game-copy/bin`) read as bytes, Ghidra headless read-only on `game-copy/ghidra-ac121` (`DecompileTo.java`, `XrefsTo.java`), the managed index (`game-copy/analysis/callgraph`), the decompiled script assemblies (read only), `tools/hook-sites.ts`, docs/68. Closure as docs/118 §5 and docs/121 §8: depth 12, the two hand-checked guards cut, roots = the 13 definitions' `Test` and nearest `CalculateScore`, the 7 implementors' `HowMuchItWantsSimToCome`, C2 `InteractionObjectPair.TestAndCalculateScore`, `Autonomy.CheckAvailability`, `InteractionDefinitionUtilities.CommonTests`, `MetaInteractionDefinitionUtilities.SpecialCaseTests`. Union: **1,294 methods, 70 ICs** (39 EA + 31 Mono runtime).

| Step | Verdict | One line |
|---|---|---|
| S0 binder bodies | **fail as designed, fixable** | 78 of the 1,085 bodies are in the closure; none reads engine state, but 59 can enter the interpreter through `0xD8A8E0` (process-global call-record list) and one writes the IC nesting counter `[0x11F2D84]` |
| S1 managed writes | **fail as designed, fixable** | C2 writes no pair field (I-122-2 holds); 3 W sites beyond §3.5 need worker hooks, the rest are hooked, idempotent lazy fills or fresh objects |
| S2 the 8 other definitions | **pass for 7, 1 out** | all 8 add no IC outside §3.2; `VisitCommunityLotWith+VisitWithDefinition` is out (W site: punishment manager + event listener) |
| S3 hook owners | **pass, with owner notes** | no S3SS/S3PP/MonoPatcher byte overlap with any pilot site; the cut must ride `[MonoThread]`'s slot, the `0xD8A8E0` fix shares `[Verify]`'s detour; the "patcher loaded = off" rule would turn the pilot off on the reference install |

Gate G-A0 as written ("hits no EA native body or each is listed with its reads; no instance write into a non-driver object except the three hooked sites; every IC has a §3.2 row; no slot owner conflict"): **not passed yet**. Every IC has a row and there is no owner conflict; the binder bodies are listed with their reads, and the writes have named fixes (§12.5). G-A0 passes once §12.5 is in the design, which this section does; the build items are new, not open questions.

### 12.1 S0: binder bodies

**Mapping [verified].** `0xE3E570` makes 1,085 calls `0xD9A860(hash, a, body, init)`; the binder `0xD9A7B0` builds the key with `0xD9A530` (`<ret> <Class>:<Method>(<args>)`, Mono reflection type names, `&` for by-ref) and hashes it with `0x7CFC50`: FNV-1 over the lowercased bytes (basis `0x811C9DC5`, prime `0x1000193`, multiply then xor). The table stores the hash, not the string. New `node tools/managed-callgraph.ts bindkeys` prints the key and hash of every managed method; joined with the 1,085 pushed hashes it matches **1,085 of 1,085**, each to exactly one method. Bodies by namespace: `System` 360, `Mono.Xml2` 162, `System.Xml` 153, `ScriptCore` 126, `Sims3.Gameplay.Utilities` 94, `System.Reflection` 78, `System.Text` 70, `Sims3.Collections` 19, `System.Threading` 13, other 10.

**In the closure: 78 bodies** (22 `SimClock`/`DateAndTime`, 51 `String`/`StringBuilder`/`Math`/`Array`, `Type.GetTypeFromHandle`, `Type.IsSubclassOf`, `MethodBase.Invoke`, `Monitor.Enter`/`Exit`) [verified: tool output]. What they read and write (all 78 bodies and their init callbacks decompiled, one call level down) [verified unless marked]:

| State | Who | Effect on a worker | Guard possible? |
|---|---|---|---|
| Engine state (any non-Mono global, singleton or IC) | none of the 78 | - | not needed |
| EA ICs | none: the init callbacks resolve only corlib ICs (`InternalAllocateStr`, `InternalStrcpy`, `InternalJoin`, `InternalSplit`, `get_Chars`, `Array.ClearInternal`/`GetLength`/`GetLowerBound`, `Math.Floor`/`Round2`, `Type.internal_from_handle`/`type_is_subtype_of`, `get_OffsetToStringData`), by method through `mono_lookup_internal_call` | the EA IC set of §3.2 is unchanged | - |
| Binding caches (per-method class, field and method handles in `0x11EE5xx-0x11F18xx`) | the init callbacks, once, at first bind | first bind on the worker would write them | covered by the warm-in rule (Mode R runs the same bodies first; I-122-10) |
| Managed statics and objects (`SimClock`'s current-time statics through the static-field helpers `0xD89CD0`/`0xD89C60`; string and builder buffers) | `SimClock`/`DateAndTime` bodies; string bodies on their own new objects | reads of sim-written statics: constant in the window (I-122-9) | not needed |
| **Call-record list `[0x11EE5A4]`** (docs/56 §2: native-to-managed call records, push in `0xD8A830`, pop at the end of `0xD8A8E0`; no other run-time user, 8 references) | **59 of 78** bodies call `0xD8A8E0` when a callee has no native body (for example `Math.Min(float)` asks `Single.IsNaN` through the interpreter on every call; exception construction in the others) | two threads push and pop one global list: a torn list on the sim thread = crash, not a stale read | **no IC guard**: needs a worker path at `0xD8A8E0` (§12.5 F1) |
| **IC nesting counter `[0x11F2D84]`** | read by 17 of the 78 bodies (picks the `setjmp` path around their corlib IC call); **written** (non-atomic `++`/`--`) only by `String.ParseFormatSpecifier` `0xDA9790`, reached through `String.Format` on exception-message paths (shortest: `Array.Copy` argument errors) [path: inferred, shortest only] | a lost update skews the sim thread's counter for the rest of the session (I-56-1) | abort the worker at `0xDA9790` (§12.5 F2); reads are harmless when the sim is outside managed code |
| Reflection invoke | `MethodBase.Invoke` body: reached only from `MetaAutonomyTuning.GetSimCountForVenueWithCustomDesireFunction` | calls a tuning-named method the closure cannot see (§12.2) | managed abort hook (§12.5 H5) |
| `Monitor.Enter`/`Exit` | bodies are a null check and nothing else: the managed lock is a no-op in this build (docs/120 §3.3) | none | - |

So I-122-1's premise ("no binder body in the closure") is false, but its consequence holds: every **engine** read on the worker still goes through an IC guard thunk. The two real hazards are Mono-host globals, both already named in docs/56 §2, where the M5 worker rule says "no entry through `0xD8A8E0`". That rule cannot hold for any managed worker code: `Math.Min`, `String.Concat` and `StringBuilder` reach it. It becomes an M5 build item (docs/120), not a pilot-only one (I-122-12).

### 12.2 S1: managed writes in the closure

Field writes of every closure method (index `fw`/`sw` columns, 176 instance fields, 11 statics), triaged by owner and by source reading:

| Class | Sites | Verdict |
|---|---|---|
| Pair object (C2) | none: no `InteractionObjectPair` field is written anywhere in the closure; C2 only fills a local `InteractionInstanceParameters` struct [verified: index] | **I-122-2 holds**: the driver may reuse the game's pair objects (the design default of its own pairs stays cheaper to reason about; either works) |
| Already hooked (§3.5) | `Sim.mAllowedRooms` + its queue/dictionary (`RecalculateAllowedRooms`), `Sim.sNumSimsDoingGardening`/`sLastTimeSimsGardeningWasUpdated`, the `LocalizeString` subtree: `FillInTokens` RNG and switch-map statics, `Opportunity+RequiredItem.mNumItemsCollected` (`LocalizeOpportunityToken`), `Lot.mName`, `NodeSubject.mMiniSimDescriptionCache`, `Responder` model getters | covered |
| **New W sites** | **(1)** `OnlineDatingManager.GetRecordForSim` creates and adds a record per Sim; reached from `VisitLot+Definition` via `Lot.CommonVisitTest` > `CelebrityManager.CanSocialize` > `DidSimsMeetOnline`. **(2)** `Punishment.GetPunishmentManagerFromSim` creates the manager, sets `Sim.Punishment` and calls `EventTracker.AddListener` (static `sSimMissedWorkListener`); reached only from `VisitCommunityLotWith+VisitWithDefinition` via `GoHereWith.GetValidFollowers` > `IsSimGrounded`. **(3)** `MetaAutonomyTuning.DesiredSimFunction.Invoke`: a `MethodInfo` that `MetaAutonomyManager` builds from the tuning row's `Time` column ("Type:Method"), reached from `Lot`, `RabbitHole` and `BakeSaleTable` through `DesiredNumSims` > `UncappedDesiredSimsCount` | **I-122-3 is false**: hooks H4-H6 (§12.5) |
| Guarded by the sim | `MetaAutonomyManager.sVenueToPlayerSimAtVenueMap.Add` in `IsSomeActiveHouseholdSimAtTheVenue`: the map is non-null only inside one hourly sim-thread loop (set and nulled in one `try/finally`) [verified source], so a worker always takes the no-cache branch | no hook |
| Idempotent lazy fills (same value whoever writes first) | `GameObject.mProduct`/`mbProductInited`/`mIntPrice` (`SimDescription.IsRich` > `get_Cost`), `CelebrityManager.mOwner`, `Lot`'s two cached anonymous delegates, `RandomGen.gRandomGen`, `GameUtils.WorldNameToType`, mscorlib `CultureInfo.numInfo`/`dateTimeInfo`/`constructed`, `NumberFormatInfo.isReadOnly`, `DateTimeFormatInfo` invariant patterns and `theInvariantDateTimeFormatInfo` | Mode R's reference fills them first; in Mode W only `WarmIn` does, so a first fill can still land on the worker (a write of the same value plus an allocation under P3). Accepted for shadow; listed for go-live |
| Fresh objects | closure display classes, every `List`/`Dictionary`/`Queue` the closure builds locally (`Lot.GetSims`, `FindAllLotsOfType`, `GetContainedObjectList`, `BakeSaleTable.CurrentlyStocked`'s copy, `GetHighestRelationshipSimForPet`), `StringBuilder`, enumerators, `NumberFormatter` stores | none |

### 12.3 S2: the 8 other definitions

| Definition | Closure | ICs (incl. Mono) | New IC vs core | W sites | Verdict |
|---|---|---|---|---|---|
| `VisitCommunityLot+DateDefinition` | 445 | 29 | none | hooked ones only | **in** |
| `VisitLotWithDate+Definition` | 445 | 29 | none | hooked ones only | **in** |
| `CityHall+AutonomouslyGoToCityHall`, `Hospital+AutonomouslyVisitHospital`, `Stadium+AutonomouslyAttendGame`, `ShowVenue+AutonomouslyAttendShow` | 464 each | 30 | none | hooked ones only | **in** |
| `ShoppingRabbitHole+AutonomouslyShopInRabbitHole` | 466 | 30 | none | hooked ones only | **in** |
| `VisitCommunityLotWith+VisitWithDefinition` | 968 | 60 | none | **punishment manager create + `EventTracker.AddListener`** | **out** for v0; in after H6 |

I-122-4 (no new IC) holds for all 8 [verified: tool output]. Allow-list v0 = 12 definitions + 7 implementors: `docs/data/autonomy-allow.csv` (type, assembly MVID from the index, closure size, verdict, reason). Every one of the 39 EA ICs of the union has a §3.2 row (the 38 PROXY + 1 SAFE of docs/121 §8.6 plus the `Sims3.UI` twin of `GetLocalizedStringByString`); the four scoring-function-only ICs (`StaysAfterEvict`, `GetOutfitInGameObject`, `GetContainmentSlotNames`, `CanAttachToSlot`) are not reached without a tuning-named scoring function (I-121-10).

### 12.4 S3: hook owners

`node tools/hook-sites.ts --check` exits 0 on this tree. Checked by address against docs/68 §1 (S3SS 1.6.3, S3PP, MonoPatcher 0.3.0) and §2-§3 (ours, built and planned), window ±0x40 for the 39 EA IC bodies and the 78 binder bodies:

| Pilot site | Third-party | Ours | Decision |
|---|---|---|---|
| Guard thunks (provider-table entries, before `MonoScriptHost_Init`) | MonoPatcher detours `InitializeScriptHost 0xD7E560` and registers its own ICs with `mono_add_internal_call 0xE82550` after the original runs: its ICs are not in the provider tables, so `Guard=all` leaves them unguarded | routesnap owns 39 table slots; none is one of the 39 closure ICs | as §3.1: guard routesnap's slots from inside its stub. MonoPatcher's own ICs are reachable only from a patched method, which the mod guard catches (below) |
| `World_FireEvent 0x78F2B0` | none | routesnap entry detour; `[LoadProf]` rewrites the call site `0x769993` (not the entry) | one more consumer inside routesnap's detour, no second hook |
| Object event nodes (`ObjectEvents_Register 0x936BC0`, lists `0x11DC2A8`) | none | routesnap registers nodes (data, no code bytes) | add our own node (list insert); no owner issue |
| Cut at the return of `0xD81840` | S3SS inline patches inside (`0xD819AA`, `0xD81A1D`, `0xD81A37`, `0xD81FDE`); S3PP seams only in callees | `[MonoThread]` owns the vtable slot `0x108B6B0`; `[LocoWindow]` design owns `0xD81C5C` inside | **ride `[MonoThread]`'s slot stub** (post-call), never a second slot swap or an inline patch (docs/68 #2, #9) |
| `0xD8A8E0` (new, §12.5 F1) | none | `[Verify]` count detour `nativeToManaged` | shared stub, one owner |
| `0xDA9790` (new, §12.5 F2) | none | none | entry bytes to check at build time |
| IC bodies reached through the stubs (`LocalizedStringService_GetLocalizedStringByString` `0x7437E0` -> S3PP `LocalizedStringCache` at `0x59C5B0`) | S3PP detours a callee, not the IC body | none | composes: the sim-thread path tail-jumps into the body (S3PP still runs); the worker returns the stub and never reaches it |
| Managed hooks (`IsAllowedInRoom`, gardening getter, `LocalizeString`, H4-H6) | none native; NRaas or other script mods may replace these methods with MonoPatcher-style swaps | AM-1 shares `IsAllowedInRoom` (docs/97 §7) | the docs/104 integrity layers (L2 bytes, L3 `klass` check), not a session switch |

**Mod guard change:** §4.1 step 4 ("a known run-time patcher assembly is loaded -> off for the session") would turn `[AutShadow]` off on the reference install, which loads `MonoPatcher.asi` (docs/68 §1). Replace it with docs/104's per-method checks over the 1,294 closure methods (L3 `klass == declaring class`) plus the `ForceJIT` and `GenerateCode` byte checks (L2); a hit marks the affected definitions `skip`, not the session.

### 12.5 Design changes

| # | Change | Where | Effort |
|---|---|---|---|
| F1 | **Worker path at `0xD8A8E0`:** on the worker thread the detour runs the same native-to-managed call with a worker-private call-record list (or the same state layout on the worker's stack), never touching `[0x11EE5A4]`; the sim thread passes through unchanged. Selftest: two threads each run 10^5 nested calls through it with the list checked after each. Shared stub with `[Verify]`. Also an M5 item: docs/56's "no entry through `0xD8A8E0`" rule is replaced by this path | ts3native `monothread` (M5) | 1-2 h |
| F2 | **Worker abort at `String.ParseFormatSpecifier` `0xDA9790`** (the only `[0x11F2D84]` writer in the closure): entry check, worker -> abort `fmt` and return through the body's normal error exit; plus the live I-122-1 check (snapshot of `[0x11F2D84]` around worker runs) stays | ts3native `autshadow` | 0.5 h |
| H4 | `OnlineDatingManager.DidSimsMeetOnline` on the worker: read the records without creating any (missing record = no accepted dates, the answer a fresh record gives [inferred, I-122-15]) | ts3fixes | 0.5 h |
| H5 | `MetaAutonomyTuning.GetSimCountForVenueWithCustomDesireFunction` on the worker: abort `desirefn` (reflection target unknown to the closure); Mode 0 logs which venue tunings set it | ts3fixes | 0.5 h |
| H6 | (for `VisitWithDefinition` later) `Punishment.GetPunishmentManagerFromSim` on the worker: existing manager -> read; none -> abort `punish` | ts3fixes | 0.5 h |
| G | Mod guard per method (§12.4) instead of a session switch | ts3fixes + ts3native | 1 h |
| C | `asAbort` gains `fmt`, `desirefn`, `punish` (3 of the 16 slots; `other` keeps the rest) | counters | - |

Effort: **+4-5 h** on the pilot (~21-34 h; ~30-48 h with M5), no extra game job. The rest of the design stands: Mode R still runs the same native bodies on both threads, so equal-by-construction (A0) is unaffected.

### 12.6 Reproduce

```sh
node tools/managed-callgraph.ts bindkeys --asm <repo>/game-copy/assemblies > keys.tsv        # hash, method id, key per method
# 1,085 registrations: in the exe copy, each `call 0xD9A860` inside 0xE3E570 is preceded by push hash; push a; push body; push init (push 0 = 6A 00)
# join keys.tsv column 1 with the pushed hashes; then closures as docs/121 Reproduce (roots above) and intersect
analyzeHeadless <repo>/game-copy/ghidra-ac121 TS3 -process TS3W.exe -noanalysis -readOnly -scriptPath tools/ghidra -postScript DecompileTo.java <out>/bodies.c 1 <the 78 bodies and their inits>
analyzeHeadless ... -postScript XrefsTo.java <out>/xr.txt 0x11EE5A4 0x11944C8 0x11944CC 0x11F2D84 0xD8A8E0 0xD8A830
node tools/hook-sites.ts --check
```

## 13. Part 1 build (2026-10-20, offline, gate G-A1)

Built offline only: no game run. Gate **G-A1 passed** on the selftest (T1, T2, T4 below); T3 (two threads with real Mono) stays G-A2 and needs M5. Files: `native/ts3native/autshadow.cpp` (module), `autshadow-kinds.h` (return kinds, generated), `autshadow-test.h` (selftest). Shared files: one include and one `ApplyAutShadow` call in `ts3native.cpp` (after every other install, so routesnap's slot stubs are already in the tables), two consumer lines in `routesnap.cpp`, the selftest include and call, `[AutShadow] Enabled=0 Guard=all Audit=50` in `ts3native.ini`, a docs/68 row. Counters: export `ts3native_autshadow` v1 (docs/registry.md). Ledger: docs/51 §156 (I-122-20 to I-122-28).

### 13.1 What part 1 does in the game with `Enabled=1`

Only the pass-through layer: guard thunks in every provider-table slot, the F2 detour, change tracking (routesnap's `World_FireEvent` detour feeds `AsWorldEvent`; our own object-event nodes for `4`, `6`, `0x0B`, `0x0C`, `0x0E`, `0x12`, `0x18`, `0x21`). No worker exists, so every thunk tail-jumps to what the slot held, and nothing calls `AsCut`. Memo ICs (§2.2 session memo) take the slow entry on the sim thread and record their answers. Cost to measure at G-A3: one `fs:[0x24]` compare, a counter add and a `push; ret` per IC call.

### 13.2 Design as built (deviations from §2-§3 marked)

| Part | As built |
|---|---|
| Guards (§3.1) | Scan of the exe's initialised data sections for `{fn, name}` pairs (`fn` in `.text` or one of routesnap's 39 slot shims, name `ScriptCore.*::*` / `Sims3.*::*`), one `push index; jmp` thunk each; `Guard=closure` = only the 43 descriptor names. **Deviation:** routesnap's slots are guarded by putting our thunk in front of routesnap's stub (the slot then holds our thunk, routesnap's stub stays its target), not from inside routesnap's stub: routesnap's code and routing path stay unchanged. Neutral values from `autshadow-kinds.h` (60 float, 1 double, 82 struct names, from `managed-callgraph.ts hostdata`): `fldz` for float/double, the hidden result pointer in `eax` for structs, else 0 in `edx:eax` |
| Answers (§2.2, §3.2) | **Deviation (one generic table instead of typed rows):** an entry is (IC, input argument dwords) -> the body's answer (`edx:eax` masked to the managed width, x87 value, struct bytes, out parameters, array contents), filled by calling the IC body found in the slot at install. Owner kinds: object (object rows: position, parent, hidden flags, resource key, tomb room, stays-after-evict, outfit, task, contained object, slot names), list (per-lot queries keyed (type, lot, flags) with the list generation), cut (sunrise, sunset, moon, objects in range), world (geometry, lot location with its 12-byte out, door rooms, world name/type/key, sea level: epoch only), memo (session memo, recorded on the sim thread's own calls; strings keyed by content hash), stub (6 tooltip ICs: the shared empty string), abort (RNG, slot position, attach, `Query_GetObjects`). Per-lot lists are the body's own answer per key, not a lot-vector walk with a worker-side class filter: no read of `lot+0x14` or of the class hierarchy |
| Change tracking (§2.1) | Epoch: world events 2, 3, 5, 7, 9, `0x0A`, `0x0D`-`0x10`, `0x46` and routesnap's `OnExitBuildBuy` / `LotClusterTrees` slots -> full refresh (lot-name memos dropped). Dirty objects from our event nodes; children and grandchildren through our own parent entries (up to 64 levels); Sims via `AsAddRow(id, sim)` always refreshed. List generation from object events 4/6 and world event `0x11`. shortcut: one generation for all lots (any add/remove refreshes every list entry); a per-lot generation needs the object's lot at the event. Without `[RouteSnap] Count=1` every cut is a full refresh |
| Versions (§2.1) | Master on the sim thread, two published copies; `AsWorkerBegin` holds the published one, the cut writes the other (an overrun run keeps its copy across cuts). Learned keys from worker misses arrive through a 64-slot ring and drop after 100 cuts without a hit. One heap block, `VirtualAlloc`'d only when enabled: 1,024 entries x 3 tables + pools + dirty sets ~0.6 MB (over §2.3's 256 KB estimate because entries are generic; trim if address space matters) |
| Audit (§2.4) | `Audit=N`: every Nth cut re-calls the body for every non-memo entry and compares; mismatches count as S1 (the S2-S4 split needs the event history: part 2). Managed references (`string`, `ITask`) are skipped (`auditSkipped`): identity, not content |
| Aborts (§3.3, §12.5 C) | First reason wins, neutral value returned, no exception from native code; reasons as docs/registry.md (`fmt`, `desirefn`, `punish` added, plus `ref` = managed answer not rooted yet, and `list`). `AsManagedAbort(reason)` is the native side of the future `AutShadowNative::Abort` IC for H4-H6 |
| F2 (§12.5) | Detour at `0xDA9790` (`55 8B EC 83 E4 F8`): any thread but the worker passes through; the worker aborts `fmt`, stores an exception in arg 2 (the body's own error contract, I-122-22) and returns without touching `[0x11F2D84]`. The exception is part 2's preallocated one, else `mono_get_exception_null_reference 0xE770E0` on the worker (an allocation: needs the M5 P3 lock) |

### 13.3 Offline tests (selftest `TestAutShadow`)

| Test | What | Result |
|---|---|---|
| T1 guard thunks | fake provider table in the selftest image (15 entries: 12 descriptor ICs incl. struct, ulong, short, bool + out, float, double, array, memo, stub; unlisted float, struct and int; one non-code entry skipped); calls through the slots on the main thread (pass-through, memo recorded) and on a second thread marked worker (snapshot answers per return kind, worker-allocated array, misses, unlisted ICs) | pass; x87 stack depth unchanged over 20 FP rounds |
| T2 snapshot | 64 objects with parents on 4 lots; seeds 1-4 x 3,000 random steps (moves, visibility, reparenting, lot moves via events 4/6 and world event `0x11`, per-cut constants, world epochs, learned row and list keys); a cut every 7 steps with `Audit=1`, the published copy checked against fresh body calls every 10th cut; a clean cut refreshes only the Sim row; a change without its event is caught by the audit; learned keys come back as entries; a held version survives two cuts; unused learned keys drop after 100 cuts | invariant S-A: 0 mismatches over 1,820 audited cuts (max cut 0.12-0.77 ms over two runs with the full audit on, 166 entries) |
| T4 abort and stop | first reason wins (`ic` before `rng`), `fmt` through a detoured fake `0xDA9790` (the sim thread runs the body, the worker gets the exception), stop from the sim aborts the next guard call, `AsWorkerEnd` releases the version | pass |

### 13.4 Open for part 2

- Cut at the return of `0xD81840` on `[MonoThread]`'s slot stub, request and worker dispatch, window and stop flags, `IsWorker` / `Abort` ICs (`mono_add_internal_call`); needs M5 F1 at `0xD8A8E0` first.
- Rooted column for managed answers (`g_asRefsRooted`, `g_asEmptyStr`, F2's `g_asFmtExc`) and the array allocator (`g_asNewArray`: `mono_array_new_specific` under the P3 lock); until then reference and array answers abort `ref`.
- Request rows: `AsAddRow` for actor, Sims (always refreshed), meta objects, doors. The `simstale` rule of §2.3 (refresh only reachable Sims) is not built: every Sim row is refreshed at each cut.
- ~~Check every snapshot body's not-found path for a raise (I-122-26) and the remaining argument layouts (I-122-20) before G-A3.~~ Done: bodies §14.8, shim side and memo bodies §14.9 (one memo keying fix, ts3native 0.36.3).
- ts3fixes: `AutShadow.ScoreMeta`, hooks H4-H6, Mode R/W compare, counters, switches; per-method mod guard (G).
- Counter reader in `tools/probe/agent.js`; `asUnguarded` / `asWorkerClassInit` (export v2).

## 14. Part 2 build (2026-10-20, offline, gate G-A2)

Built offline only: no game run. **G-A2 passed in `native/monohost`** (§14.4): the worker scores a synthetic request from the snapshot through the guard thunks while the host's main thread (the sim thread's stand-in) interprets, allocates and collects, and the sim-thread reference of the same cut matches bit for bit. Files: `native/ts3native/autshadow-run.cpp` (new, included by `autshadow.cpp`), small changes in `autshadow.cpp`, `monothread.cpp` (two hook pointers and the return hook in the Simulate slot stub), `monothread-m5.cpp` (a job hook around the worker's invoke), `ts3native.ini`; ts3fixes `mods/ts3fixes/src/AutShadow.cs` and `AutShadowCore.cs` (new), `Fixes.cs`, `Instantiator.cs`, ts3agent `PatchHandlers.cs`, `tools/setup/build-fixes.ts`, `tools/selftest/autshadow/`; host test in `native/monohost`. Counters: `ts3native_autshadow` v2, ts3fixes slots 106-121 (docs/registry.md). Ledger: docs/51 §159 (I-122-29 to I-122-37). `[AutShadow] Enabled=0` stays the default; the ts3fixes tuning `kAutShadow` is false.

### 14.1 Native flow as built

| Step | Where | As built |
|---|---|---|
| Cut point | `[MonoThread]`'s Simulate slot stub (one slot owner, §12.4) | `MtSimulateStub` swaps the caller's return address for `MtSimReturn` (outer call only) when `g_mtOnLeave` is set; `MtTick` calls `g_mtOnEnter` after the M5 tick. No second slot swap, no inline patch in `0xD81840`. Needs `[MonoThread] Enabled=1` (any Mode); without it `mode` = -1 and `Begin()` refuses |
| Request | ts3fixes B13 hook -> `Begin()`, `AddRow(id, kind)`, `Post(id)` | `Begin` refuses while a request is pending or a run is in flight (`busy`) and clears the per-request reachable set. Kinds: 0 object, 1 reachable Sim (refreshed every cut), 2 other Sim (listed; refreshed only when dirty, else **published stale**: a worker read aborts `simstale` and promotes that Sim to refreshed-every-cut for the rest of the session) |
| Cut | return of Simulate with a request posted | the first cut roots the column (§14.3); `AsCut`; Mode 0 or no methods: finish at once (`LastRun` 3) |
| Reference | same point, sim thread | `0xE5A600` on the `reference` method, with host `+0xC30 .. +0x15F8` (MT state, 0x9C8 bytes [end inferred]) saved before and restored after; `rngDraws` counts runs that moved it |
| Worker | M5 worker, `MtSubmit(work)` | only when the worker is idle; `g_asWorkerTid` = the M5 worker; the job hook holds the published version (stop flag left as the sim set it) and times the run; a submit refusal or an EA memory tracking skip finishes with `LastRun` 4, a managed exception with 5 |
| Window close | top of the next Simulate | an epoch bump since the cut sets stop (`epochDrops`, `LastRun` 2); a worker still inside gets stop (`overrun`, `LastRun` 1). No wait |
| Finish | a later return of Simulate, once the job left the worker | `finish` on the sim thread (compare, counters), then idle |

Managed calls are static methods with no arguments, resolved by ts3fixes (`MethodHandle.Value`) and handed over with `SetMethods`; request and result arrays stay in rooted ts3fixes statics (§4.2).

### 14.2 `AutShadowNative` internal calls

Registered with `mono_add_internal_call` `0xE82550` at the first Simulate entry (before any ts3fixes call inside Simulate; ts3fixes probes `Version()` lazily and treats `MissingMethodException` as "no ts3native"): `Version` (2), `Config(key)` (0 Mode, 1 Sample, 2 WarmIn, 3 DumpOnMismatch, 4 worker idle), `IsWorker`, `Abort(reason)`, `Aborted`, `ResetAbort`, `Stopped`, `Begin`, `AddRow(ulong, int)`, `Post(int)`, `LastRun`, `SetMethods(IntPtr x3)`, `SetRefs(string empty, Exception fmt)`, `Log(string)` (one line to `autshadow.jsonl` next to the ini, cap 100k). `Aborted`, `ResetAbort` and `Stopped` read as clear on any thread but the worker, so the sim-thread reference and the managed hooks never see the worker's run state.

### 14.3 Rooted answers and arrays (§13.4 open items closed)

- **Rooted column:** each table (master, A, B) has a `refs[1024]` column next to its array pool; one `GC_add_roots` range per table plus one for the cut's scratch pool and the two kept objects (`SetRefs`: empty string, F2 exception), added once at the first cut. A managed answer (`AK_REF`) goes into the column as soon as the body returns it (inside the cut, before the next body call can collect), memo answers when recorded; compaction moves the column with the entries and publication copies it. `g_asRefsRooted` turns on with it, so string and `ITask` answers no longer abort `ref`.
- **Arrays:** `AsNewArrayGc` = `GC_malloc` (P3-locked while the worker runs) of 16 + 4n bytes with the vtable and length set. **The MonoArray length is at `+8`, data at `+16`** (`+12` is the bounds pointer, NULL for vectors) [verified in monohost: a host `object[12]` reads `vtable, 0, 0xC, 0, element 0`]. Part 1 read the length at `+12` (its selftest fakes used the same wrong layout, so G-A1 could not see it): every array answer had length 0 until this fix.

### 14.4 G-A2 in `native/monohost` (T3)

The host links `autshadow.cpp` unchanged inside its `mt` namespace (routesnap's pieces as no-ops). Seven host fake bodies with the game's IC layouts (`Objects_GetPosition`, `World_GetLotLocationImpl`, `Query_GetObjectsOnLot`, `World_GetSunriseTime`, `World_GetWorldNameKey`, `EAText_GetMoneyString`, `Random_NextDoubleImpl`) sit in a provider table in the host image; `AsInstallGuards` writes its thunks there and the host registers the thunks under `MonoHostTest.AsIc` names. `AsDriver` (`HostTest.cs`) is the synthetic `ScoreMeta`: 50 meta objects, each scored from a row answer, a position-keyed lot location with its out struct, a per-lot object array (12 managed objects), the sunrise constant, a reference answer (a string whose only host copy is masked), the tooltip stub and, for every 7th object, an RNG draw. Per request the main thread posts, calls the cut (reference + submit), runs its own pass-through scoring passes, a 1,500-node churn and a forced collection until the job is out, then the next Simulate entry and the finish.

| Check | Result | Tag |
|---|---|---|
| `AS-install` | 7 guard thunks, 14 `AutShadowNative` ICs, `Setup` -> `Version` 2, worker idle | verified (run x5) |
| `AS-G-A2` | 4 warm-up requests (53 learned keys), then **200 requests x 50 objects: A0 8,400 of 8,400**, A1-A3 0, A4 1,600 = exactly the `rng` aborts (8 per request), other aborts 0, A5 0; 200 main-thread passes with 0 wrong answers; 200 collections run by the main thread while the worker was inside (P2 pushes >= collections); audit every 5th cut: 0 mismatches; cut max 46-57 us; worker exceptions 0; ~0.8-0.9 s | verified (run x4 after the array fix) |
| `AS-overrun` | the next Simulate entry right after the submit: stop set, the worker leaves at its next check, finish sees `LastRun` 1, A5 counted | verified (run x4) |
| before the array fix | A0 0, A2 8,400 (every score off by the list part: length-0 arrays) | verified (run x2) |

Full host selftest 36/36 (M1.x, M2, M5 and the three AS checks), exit 0. Selftest `TestAutShadowRun` (T5, `autshadow-test.h`, on TestMonoThreadM5's fake worker): Mode 0 finish without reference; Mode 1 reference + worker with equal answers, RNG restored, a stale Sim read aborts `simstale` and is refreshed from the next cut on; overrun; epoch drop; EA tracking refusal; no idle worker; IC registration; `GC_add_roots` ranges; JSONL log; array header. Passes.

**Not proven offline:** that the rooted column is what keeps a managed answer alive. A negative control (rooting off, answers still handed out) also passed: the world-name key string stayed reachable through other conservative references in the host, so this test cannot tell (I-122-32).

### 14.5 ts3fixes (`AutShadow.cs`, as built)

| Part | As built |
|---|---|
| Request | `Autonomy.ScoreInteractionsForMetaAutonomy` hook (B13; the hook returns `int` for the private enum return type [inferred to work in the interpreter, I-122-34]); sampled 1 in `Config(1)` with switch `autshadow`; request = actor, commodity kind, search type, flags, hour, gardening count and the meta object list in B13's order; `AddRow` 1 for actor and reachable Sims, 0 for meta objects, 2 for the other Sims |
| Driver | `ScoreMeta`: our own B14/C1/C2 loop for the 7 implementors (exact type; `RabbitHole` subclasses from the two MVID-checked EA assemblies, since `RabbitHole` is abstract) and the 12 allow-listed definitions; per (meta object, ad) score and code, per object meta score; per-object `try/catch`, `Aborted()` after each object, `Stopped()` at each boundary |
| Worker hooks | `Sim.IsAllowedInRoom`, `Sim.get_NumSimsDoingGardening`, the four `Localization.LocalizeString` overloads, H4 `OnlineDatingManager.DidSimsMeetOnline`, H5 `MetaAutonomyTuning.GetSimCountForVenueWithCustomDesireFunction`, H6 `Punishment.GetPunishmentManagerFromSim`; sim-thread cost one static read; the B13 hook goes in only if every worker hook did |
| Mod guard (§12.4, G) | assembly MVIDs against `autonomy-allow.csv` (mismatch -> off for the session); per method L3 (`MonoMethod+0x08` == the declaring class) over the driver's callees, self-checked on our own patched method (fail -> off for the load); L2 bytes logged only; a hit marks that definition or implementor `skip` |
| Compare | Mode 1: A0-A5 per (meta object, ad) from `LastRun` and the two result arrays, a JSONL record per A1-A3 (cap 200 per load); Modes 1 and 2: L report from a `CalculateScore` wrapper that records the game's own C1 scores for the sampled request while its B13 runs |
| Warm-up | at the first sampled B13 of a load: `Version` probe, finish/reference/work once on the sim thread, `SetRefs`, `SetMethods`; Mode 2 references only the first `WarmIn` requests |

The ts3fixes half compiles clean against the game assemblies and its logic selftest (`node tools/selftest/autshadow/run.ts`: ULP and class rules, L report, MVID format) passes; none of it has run in the game. `tools/setup/build-fixes.ts` needs `game-copy/assemblies` under the checkout it runs from (build from the main checkout).

### 14.6 Open items

- **In game first (G-A3, then G-A4):** queue `tools/harness/queues/autshadow-first.json`, ready to run: §14.7.
- `DumpOnMismatch` is only passed to ts3fixes; the native snapshot dump of §5.1 is not built.
- `unguarded` and `workerClassInit` are reserved v2 fields, not wired (need an icall-resolution hook and a class-init hook).
- ~~Still open from §13.4: every snapshot body's not-found path for a raise (I-122-26) and the remaining argument layouts (I-122-20)~~: closed statically in §14.8; shim side and the 7 memo bodies in §14.9.
- Once a run started, the M5 worker is the shadow worker for the session: a later non-AutShadow job on it would also get snapshot answers. No such job exists.
- ts3fixes items only the game can show (I-122-34 to I-122-37): the `int` stand-in return of the B13 hook, `FieldInfo.GetValue` on the worker for private fields (a corlib IC), boxing in H4 under the P3 lock, the L3 layout assumption, static constructors of implementors and definitions run by the reference first.
- Counter reader for `ts3native_autshadow` v2 in `tools/probe/agent.js`: added (§14.7).

### 14.7 How to run the first in-game jobs (2026-10-20)

No game run yet; this is the run recipe.

- **Switch:** the ts3fixes tuning `kAutShadow` stays `False`, so one ts3fixes build serves every job. A pilot job turns the driver on with `"run": {"profile": {"fixesOn": "autshadow"}}`: `profile-world.ts --fixes-on autshadow` sends the agent's `patches {"autshadow": true}` while paused before the window, reads it back (`fixes on: autshadow (verified)` in the job log) and restores the previous value at the end. The B13 hook reads the switch per call (`ShortcutOn("autshadow")`), so searches before the switch are just not sampled. The ts3agent `patches` command now lists `autshadow` and `autshadowverify` (unset = off, like the tuning). Jobs without `fixesOn` are unchanged: the switch stays unset, `ShortcutOn` returns `kAutShadow` (false), and the hook runs the game's method with no `AutShadowNative` probe (verified by reading `MetaHook`: `Ready()` is reached only through `TryRequest`, behind the switch).
- **asi:** master 0.35.22 (counters v72, `ts3native_autshadow` v2, `ts3native_mtprobe` v2), parked as `game-copy/ts3native.asi.autshadow2`. ts3fixes and ts3agent are rebuilt from the checkout by `cycle.ts` as for every job.
- **Order:** `node tools/harness/queue.ts tools/harness/queues/autshadow-first.json --only bp-autshadow-count,ip-autshadow-count`, read both against the pass rule in each job note; only then `--only bp-autshadow-r`, then `ip-autshadow-r`. Stop on any crash (crash rule).
- **Readings:** `-native.json` carries `autshadow` (all v1/v2 fields, `abort` by reason name) and `mtprobe` (v2 adds `arenaRelNonEmpty`, `arenaDropped`, `n2m*`); the ts3fixes side (`autshadow_a0`..`a5`, L report, requests, busy, guard skips, exceptions, `autshadow_reasons`) is in the agent's `patches` answer. Records: `autshadow.jsonl` next to `ts3native.ini`.
- **Not covered:** the snapshot dump of `DumpOnMismatch` (§14.6); `unguarded` / `workerClassInit` read 0 by construction.

### 14.8 Static pre-run checks (2026-10-20, I-122-20, I-122-26, I-122-34..37)

Static only (capstone over `game-copy/bin/TS3W.exe`, `functions.csv` boundaries, `methods.csv` signatures, the decompiled script DLLs); no build, no game run. The cut (`AsCut` / `AsRefresh`, and the audit) calls a body natively only for the owners `obj`, `world`, `list` and `cut`: the 25 rows 0-24 of `kAsDesc`. Memo answers are recorded on the sim thread's own managed calls, stubs and aborts never call the body.

**Method (I-122-26).** For each body: the direct-call closure (E8 calls and jumps that leave the function) to depth 14, then the host-interface slots the bodies reach through `0x7692B0` resolved from the PE (`0x108B690` `+0x50` = `0xD7BD40`, `+0x84` = `0xD71070`, `+0x8C` = `0xD710D0`, `+0x9C`/`+0xA4` = `0xD711C0`, `+0xBC` = `0xD70B50`, `+0xC0` = `0xD798F0`, `+0xC4` = `0xD74C00`, `+0x17C` = `0xD7CFD0`; `0x108B818` `+0x18` = `0xD74240`) and their closures to depth 8. Markers: `mono_raise_exception 0xEA18F0`, the `mono_get_exception_*` block `0xE77000`-`0xE775FF`, and the imports `RaiseException`, `_CxxThrowException`, `longjmp`, `abort`, `exit`. Controls: `0xD9B130` (calls `mono_get_exception_null_reference`) and `0xE527D0` (calls `mono_raise_exception`) are both flagged [verified].

**Result.** No body reaches a raise on any path except the managed allocator: `mono_array_new 0xEA30F0` -> `mono_array_new_specific 0xEA2770`, which raises an overflow exception (`0xE77080`) for a negative length and out-of-memory (`0xE78610` -> `0xEA18F0`) on a size overflow or a failed allocation [verified in the disassembly]. Only 3 bodies allocate that way (slot names, door rooms, world name key), and their lengths are native container counts, never an argument. `abort` / `exit` are reachable only through Mono and GC assertions (`mono_class_from_mono_type 0xE70D50`, GC fatal paths), which end the process in managed code too. Not followed: component-vtable calls (e.g. `0x798281`, component `+0xC`) and the refcount release (`push 1; call [vtable]` when a handle's count reaches 0, the same as on the game's own calls).

| # | IC | Body | Stack dwords (verified) | Return | Not-found / bad input | Raise reachable |
|---|---|---|---|---|---|---|
| 0 | `Slots_GetParentIdImpl` | `0x76F780` | id 0-1 | u64 `edx:eax` | hash miss -> 0 | no |
| 1 | `Objects_GetPosition` | `0x744DB0` | result ptr 0, id 1-2 | Vector3 | zero vector | no |
| 2 | `World_ObjectGetHiddenFlags` | `0x78D400` | id 0-1 | uint | 0 | no |
| 3 | `ScriptProxy_GetResourceKeyImpl` | `0x7872E0` | result ptr 0, id 1-2 (host `+0xBC` -> `0x7697A0`) | 16 B | key zeroed first | no |
| 4 | `World_GetObjectTombRoomIdImpl` | `0x78AC00` | id 0-1 | short | `or ax, 0xFFFF` (-1) | no |
| 5 | `World_StaysAfterEvictImpl` | `0x785C30` | id 0-1 | bool | 0 | no |
| 6 | `CASUtils_GetOutfitInGameObjectImpl` | `0x5A9430` | result ptr 0, id 1-2 | 16 B | zeroed | no |
| 7 | `Simulator_GetTaskImpl` | `0x76CA60` | id 0-1, bool 2 (host vtable `+0x18` = `0xD74240`, `ret 0xC`) | ITask | 3 hash misses -> null | no |
| 8 | `Slots_GetContainedObjectImpl` | `0x76F820` | id 0-1, slot 2 | u64 | 0 | no |
| 9 | `Slots_GetContainmentSlotNamesImpl` | `0x76F990` | id 0-1 | uint[] | null | allocator only (host `+0x84`) |
| 10 | `World_GetRoomIdAtPosImpl` | `0x78ADF0` | Vector3 0-2, level 3 | short | no WorldManager -> lot 0 into `0x786F40` | no |
| 11 | `World_IsPositionOutsideImpl` | `0x7850B0` | Vector3 0-2 (by address) | bool | `0xB046C0`'s own default | no |
| 12 | `World_HasAnyRoofOrFloorAbovePosImpl` | `0x786E50` | Vector3 0-2 | bool | no WorldManager / no lot -> 0 | no |
| 13 | `World_GetLotLocationImpl` | `0x784F90` | Vector3 0-2, out 3, room definition 4 | ulong | no lot -> 0; out still filled from the position (int +0, int +4, sbyte +8, short +0xA = 12 B) | no |
| 14 | `World_GetDoorRoomsImpl` | `0x7981F0` | id 0-1, room definition 2 | int[] | no object / portal -> null | allocator only (host `+0x8C`) |
| 15 | `Query_CountObjectsOnLot` | `0x749BC0` | Type 0, lot 1-2, flags 3 | uint | unknown lot (`0x7499B0`) -> 0 | no (host `+0xC4`) |
| 16 | `Query_GetObjectsOnLot` | `0x749D30` | as 15 | Array | unknown lot -> null | no (host `+0xC0`) |
| 17 | `Query_GetObjectsInRange` | `0x7496E0` | Type 0, Vector3 1-3, radius 4 | Array | none (spatial query) | no (host `+0xC0`) |
| 18 | `GameUtils_GetWorldName` | `0xEC1030` | none (tail jump `0x5A2110`) | int | no WorldManager -> -1 | no |
| 19 | `GameUtils_GetWorldType` | `0xEC1050` | none (tail jump `0x5A22A0`) | int | -1 | no |
| 20 | `World_GetWorldNameKey` | `0x795320` | none | string | no WorldManager / host / name -> null | allocator only (host `+0x50`) |
| 21 | `World_GetSeaLevelImpl` | `0x786240` | none (tail jump `0xC61670`) | float | `fldz` | no |
| 22 | `World_GetSunriseTime` | `0x784E50` | none | float | constant `[0xFB2F68]` | no |
| 23 | `World_GetSunsetTime` | `0x784E70` | none | float | constant `[0xFB2F68]` | no |
| 24 | `World_GetMoonPhaseImpl` | `0x78AA70` | none (tail jump `0xC10CF0`) | int | `or ax, 0xFFFF` | no |

**What the builder can pass.** Stale ids after a destroy: every object row (0-9, 14) looks the id up in the object hash (`0x939560`) before anything else and returns the default above on a miss, so a destroyed object's row reads as "gone", like the game's own call after the destroy. Learned keys: the worker's own arguments, so the same values the sim-thread reference passes on the same cut. The `Type` dword of rows 15-17 is a `MonoReflectionType*` kept in the key; Mono caches those per domain and the GC does not move them, so a key never points at a freed type [inferred]. A null `Type` would fault in the host slot (not a raise); managed callers always pass `typeof(T)`.

**Guards.** None needed in native code for I-122-26. The one raise left, out of memory in the 3 allocating bodies during a cut, would unwind with no managed frame and end the process; the game's own call would fail the same way, so it is accepted. Optional and cheap if wanted later: do not learn a rows 15-17 key whose dword 0 is 0.

**I-122-20.** All 25 layouts match `kAsDesc` (nArg after the hidden pointer, `sz`, `at`, out index and size); `World_GetLotLocationImpl` returns a `ulong` lot id with the 12-byte out at dword 3. The 17 memo, stub and abort rows match their `methods.csv` signatures' dword counts (ulong / long / double 2, bool 1, refs 1); they are not disassembled because the cut never calls them. Note `Simulator_GetTaskImpl` also has a `ScriptCore.UTFConnectorSimulator` binding with the same signature; it is not a descriptor name, so the snapshot never answers it [whether the closure reaches it: not checked].

**Managed side (static parts of I-122-34 .. I-122-37).**

| Id | Static result |
|---|---|
| I-122-34 | `Autonomy.YieldResult` is `private enum { Continue, SearchInterrupted, ActorDestroyed }` with no base type, so int32. `SwapMethod` swaps the method header (IL, locals), not the signature: B13's callers still see the enum and the hook's `ret` leaves an I4, the same stack value. Consistent; the game run still decides |
| I-122-35 | The 4 worker `FieldInfo.GetValue` reads (`Lot.mRoomRestrictionCheckCallbacks`, `Sim.mAllowedRooms`, `OnlineDatingManager.mActiveRecords`, `OnlineDatingRecord.mAcceptedDates`) are instance fields of reference type: no boxing of the value and no static-field class init in the IC. The IC itself is resolved on the sim thread first (the request's `sNumSimsDoingGardening` read). H4 boxes its `ulong` key twice per `Accepted` (`IDictionary.Contains` and the indexer): P3-locked allocations, as for the G-A2 arrays; the dictionary's key type is `ulong`, so no `ArgumentException` |
| I-122-36 | `MonoType.data` at +0 confirmed (`0xE70D50` reads the type byte at +6 and the class at +0). `MonoMethod.klass` at +8 is Mono 1.2.3's layout (flags, iflags, token, klass) [inferred]; the built self-check is the guard |
| I-122-37 | **Finding:** the warm-in reference does not run the worker-only hook branches: on the sim thread every hook takes its `Orig` path. Code first executed on the worker is H4's `IDictionary.Contains(object)` and `this[object]` on `Dictionary<ulong, OnlineDatingRecord>` (the game uses `ContainsKey` and the typed indexer instead), plus our helpers `Accepted` and `Cached`; `Cached`'s dictionary calls (`TryGetValue`, `GetObjectCacheVersion<IPortalConnectionObject>`) are the game's own `IsAllowedInRoom` calls and are already warm. So the interpreter transforms those methods on the worker (docs/120 §3.3 risk). Fix (ts3fixes, small): at the warm-up, call `Accepted(recs, 0, 0)` (when online dating is active) and `Cached` once on the sim thread. Not built here |

I-122-31 (the return hook in `[MonoThread]`'s slot stub) was not re-checked: native, and the first count job tests it directly.

### 14.9 Shim side of I-122-20 / I-122-26 and the memo bodies (2026-10-20, ts3native 0.36.3)

Static (capstone over `game-copy/bin/TS3W.exe`, managed externs from the decompiled `ScriptCore`) plus the offline selftest; no game run.

**The shim never makes up a default.** `AsWorkerAnswer` returns only what the game's own body returned for the same key (stored at the cut by `AsRefresh`, or on the sim's own call for memo rows); a key that is not in the published version aborts the run (`row`, `pos`, `list`, `memo`, ...) and the struct result memory and outs stay untouched. So every not-found default in the §14.8 table reaches the worker unchanged (null array = `payN` 0 -> null; null ref; short -1 masked to 2 bytes by `AsMaskRet`; Vector3 zero; zeroed 16 B keys). No mismatch there [verified by reading the code; T1 covers hit, miss and unlisted paths].

**The 7 memo rows (the remaining I-122-20 layouts).** They do call the body (on the sim thread, through `AsSimMemo` / `AsCallRaw` with `nArg` dwords), so their layouts matter. All match `kAsDesc` [verified in the disassembly]:

| IC | Body | Stack dwords | Return | Not-found | Outs written |
|---|---|---|---|---|---|
| `DeviceConfig_GetOption(string, out uint)` | `0x741230` | string 0 (host `+0x58` to native), out 1 | bool `al` | option missing (`0x58ED70` loop ends) or null string -> 0 | **only when found** |
| `GameUtils_IsWorldInstalled(uint)` | `0xEC1000` | id 0 | bool (0/1 in `eax`) | 0 | - |
| `UserToolUtils_GetProductImpl(uint, ulong, uint, uint)` | `0xCE6320` | 0, 1-2, 3, 4 | uint handle | no host / manager -> 0 | - |
| `UserToolUtils_ProductGenericImpl(uint, uint, ref uint, ref ulong, ref uint, ref uint, ref float)` | `0xCE99E0` | 0, 1, refs 2-6 (8 B at 3) | string | handle not type `0x45` / no object / funcID > 0x27 -> common exit `0xCEA25B`: the empty local text through host `+0x50` (a new managed string each call; the memo replays the recorded one) | **per funcID** (e.g. one writes only the float, one the ulong + two uints) |
| `World_GetLotName(ulong)` | `0x7955C0` | 0-1 | string | no WorldManager -> null | - |
| `World_GetLotNameKey(ulong)` | `0x7956C0` | 0-1 | string | no WorldManager -> null | - |
| `DeviceConfig_GetProcessorLevel()` | `0x741130` | none | int | no config -> 0 | - |

No direct raise call on these not-found paths (their call closures were not walked); even if one raised, it would be the sim thread's own managed call (the memo thunk holds no lock across the body).

**Mismatch found and fixed.** The memo recorded every out after the call and keyed only on the non-out arguments. When a body leaves an out unwritten (`GetOption` on a missing option; `ProductGenericImpl` for most funcIDs, whose managed wrapper passes the caller's `ResourceKey` fields in by `ref`), the game leaves the caller's own value there, but the worker got the sim caller's value replayed. Fix in `AsKeyOf`: memo rows also key on the in-values of their out/ref parameters, computed before the body runs. A replay is then exact (same in-values -> same outs, written or not); a different in-value is a memo miss and aborts `memo`, never a wrong answer. Keys fit `AS_KEY` = 8 (ProductGeneric: 2 + 6 dwords). Cut rows are unchanged: the only cut row with an out (`World_GetLotLocationImpl`) always writes it (§14.8). Selftest: `AfOption` now writes its out only when found, like `0x741230`; new T1 cases (a missing option recorded with in-value 42 replays 42 to a worker caller holding 42; a worker caller holding 5 aborts `memo` and keeps 5) pass.

**Other residuals.** `ScriptCore.UTFConnectorSimulator::Simulator_GetTaskImpl` (same signature as row 7) has no descriptor, so a worker call gets the guard thunk's unlisted path: abort `ic`, never a snapshot answer [verified in code; whether the closure reaches it stays unchecked, it only costs a run]. A null `Type` in a rows 15-17 key would fault natively at the next cut; managed callers always pass `typeof(T)`, so no guard was added.

### 14.10 Mod guards, docs/125 addendum fixes and H11 (2026-10-20, offline, ts3native 0.36.5, contract Version 4)

- Contract **Version 4**: new IC `RngPause` (16 ICs); ts3fixes wants 4; monohost checks 4 / 16. Counters `ts3native_autshadow` v4 (`abort2` for reasons 20 `rain` and 21 `posture`, `rngPause`, `rngPauseDraws`); ts3fixes counters 122-126.
- docs/128 G-M1..G-M5 built as in docs/128 §5.1: accepted MVID pairs (EA, simler90 UPDATE242), scoring-function skip `sf` (`Def` -3) with `AlwaysChooseBest` recorded, ts3fixes hooks pass through on the worker, closure-wide L3 sweep (688 methods, generated list `mods/ts3fixes/src/AutShadowLists.cs`; ~65-110 ms per sweep under the game's Mono, once per load at the first sampled search), mod override census (63 slots).
- docs/125 addendum §4 built (§4.1 there): reference RNG pause brackets around the `LocalizeString` and `IsAllowedInRoom` hooks, room cache and posture list pre-warm at the request, worker abort `posture` on an unfilled list.
- **H11** (docs/127 §4): in `ScorePair` on the worker, before the score: autonomy search, Seasons on, rain or hail, outdoor non-Sim target = abort `rain` (20). The reference and the game score those pairs as before, so Mode R shows them as A4 with reason `rain`. Condition order and laziness selftested (`AutShadowCore.RainShelter`).
- Not run in the game. Next: the docs/122 §14.7 first jobs with these guards on (expect `sf` 0, G-M4 foreign 0, G-M5 types 0 on an install without NRaas, `rain` only in rainy Seasons saves).