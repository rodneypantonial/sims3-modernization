# 88. Destroy waits: key `0x2000003` (`ObjectService_DestroyById 0x939420`)

Written 2026-10-18. Static only; the game was not run (PLAN "Routing off-thread" item 3, docs/84 §6 step 5). Tools: Ghidra headless decompiles on a private copy of the docs/86 project (`game-copy/ghidra-ds`, copied from `ghidra-g0`, read-only runs), capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67) with a direct-call graph over `game-copy/analysis/functions.csv` (40,260 functions; `E8`/`E9` targets at function entries, register-indirect calls counted per function), forward closures (destroy roots 7 hops, job roots 8 hops) and a reverse walk to roots. Live numbers are re-read from existing runs in `probe-out/queue` and `probe-out/keep` (`*-native.json`, after minus before; 10 min profiles = 600 s). `native/` was read, not changed. Script side: `game-copy/decompiled` (grep). Scratch scripts stay in the session scratchpad.

Tags as in AGENTS.md: **[verified]** = read in the disassembly, decompile, source or a run file (address or file given); **[inferred]** = reasoning, to confirm with the §7 probe or the A/B job.

## 1. Summary

- **What the key is [verified, `native/ts3native/rtsites.cpp`].** `0x2000003` = `0x02000000 | RT_DESTROY` (enum value 3), the key `RtEnter` passes to `RtWaitJob` at the `ObjectService_DestroyById 0x939420` site. It is our own closure **#4 "drain on destroy"** (docs/60 §2.2 row 4, S3b): with `[RouteGate] Enforce=1`, every non-worker destroy publishes `g_rgOffSimOpen` (no queued job starts while it is open) and then waits for a running job; the sim thread pumps the job's managed calls while it waits (`WaitAsyncDone`). It waits for **one** job at most: the published count stops `ServeAsync` from starting the next queued job [verified `ServeAsync` start loop]. The D-X apply (`DiscOnDestroy`, docs/84 §7.2) runs right after the wait and relies on it.
- **Why the closure exists, and what is real [verified paths, effect inferred].** docs/60 put the wait there for the object id hash: the job looks objects up with `ObjectService_FindById 0x939560` (no lock, no AddRef) at 15 sites in its direct closure (§3.2), and `DestroyById` erases the object from the hash and drops its references, so the last `Release` can free the object block (`Object_DeletingDtor 0x935DF0` -> `Object_Destruct 0x935860` -> each component's `+0x2C`) while the worker holds the raw pointer or is about to `lock inc` it. That **lifetime** dependency is real. The rest of the destroy is not a dependency: within 7 direct hops `DestroyById` and its owner callbacks touch **no routing state** (no footprint, lot table, lot quadtree, physics quadtree mutator, room graph or route handle function; §3.1), and the only data both sides touch is the hash itself and object memory (§3.3). The hash erase alone is safe for a concurrent reader as long as the erased object's memory stays valid: `ObjectIdHash_EraseNode 0x938D00` rewrites only the predecessor's `+0x10` (or the bucket head) and the count `+0x1028`, never the erased node's own `+0x10` [verified decompile].
- **So the wait is broader than the dependency.** What must wait is only the free (the last `Release` and the component destructors it runs), not the destroy. The events, the owner callback, the update-list removal, the destroyed flag and the hash erase can run while a job runs; script `Destroy()` already runs the owner callback (`0x798D40`) unwaited during jobs today, before it reaches `DestroyById` (§2).
- **Who destroys during jobs [verified callers, rates partly inferred].** 16 direct callers (§2). The hot ones are reached from the sim stages that run right after the script task stage that STARTs the jobs (docs/82 §8 order: script tasks `0xD81948` -> GC -> finalizers `0xD81A09`/`0xD81A30` -> `Animation_SACSPass 0xD81A70` -> ... -> `Locomotion_Update 0xD81C5C` -> `Animation_UpdateActors`): script `Simulator.DestroyObject` (`VisualEffect.Dispose`, `StateMachine.Destroy`, `FinalizerTask`, props: 223 C# call sites), `Animation_DrainDeferred 0x571A50` (animation props), effects (`IC_World_ObjectDetachAutoDestroyEffects` -> `0xC63E40`, locomotion effect list `0x84CE30`). Effects and state machines have no routing footprint and are never route targets [inferred]; the per-caller split needs probe P-G (§7).
- **Live size [verified run files].** P0 (`h6-m1-2026-10-17-bp-p0-async`): 603 destroys inside a job, every one waited, 5.24 s = **8.7 ms/s = 11.8%** of the 44.4 s sim wait. With design D on (`clean-night-2026-10-17-bp-env-on`): 681 waits, 5.28 s = 8.8 ms/s (9.2% of 57.3 s); `bp-s2-on` 621 / 5.12 s, `bp-s2-shadow` 631 / 5.14 s; IP 92-173 waits, 1.9-2.6 s (3.1-4.3 ms/s). Mean wait 7.8-8.8 ms in BP against a mean job of 5.1-6.1 ms; 14-28 ms in IP (mean job 10.6 ms) (§4). `vfRtDestroyOffSim` = 0 and `vfRtDestroyInInvoke` = 0 in all 16 async runs.
- **Recommendation: design G, a destroy graveyard (§5).** At `RT_DESTROY`, when a job runs and D-X has no queued record for that id: take one reference (`ObjectService_FindByIdAddRef 0x9395A0`), park the pointer, and let the original destroy run **without waiting**; release the parked references at the next IC point where no job runs (the same drain points as design D), with `g_rgOffSimOpen` published so no job starts during the release. Keep the wait for: D-X records queued for the id, a full graveyard, closing/teardown, and non-sim threads. **Gain cap 8.8 ms/s BP (5.2-5.3 s per 10 min), 3.1-4.3 ms/s IP; realistic 3-6 ms/s BP** (the sim reaches its next routing touch of the same iteration sooner and may wait there; gains do not add). **Effort ~1.5 days.** H2 (docs/86) does not cover it: the destroys run outside the `Locomotion_Update` window.

## 2. Callers, rate and stage

Every direct call of `0x939420` [verified caller index; roots from the reverse walk]:

| Caller | Reached from | What it destroys | Stage / rate class | Tag |
|---|---|---|---|---|
| host `ScriptHost_GetWorld` vtable `+0x17C` -> `+0xC` (no direct call; `IC_Simulator_DestroyObjectImpl 0x76D120`) | C# `Simulator.DestroyObject` (223 call sites): `VisualEffect.Dispose`, `StateMachine.Destroy` (every animation state machine), `FinalizerTask` (finalizer-thread disposals replayed on the sim), `ScriptObject.Destroy` (`World.RemoveObjectFromObjectManager` = `0x798D40` first, docs/84 §7.2), props (food, trays, held objects) | effects, state machines, temporary props, any object | script tasks, finalizers: **right after the job STARTs of the same tick** | callers verified (grep, IC body); host slot target not traced (docs/50 G) |
| `Animation_DrainDeferred 0x571A50` | `Animation_SACSPass`, `Animation_UpdateActors` | deferred animation props: `0x798D40` (owner callback, event `0x0C`) first, then the animation component hook `0x941BC0`, then `DestroyById` | SACS pass and actor update, per tick | verified decompile |
| `0x84CE30` (list at `this+0xA4`) | five callers `0x84EA00`, `0x84F2B0`, `0x8504D0`, `0x850E20`, `0x851280` (locomotion family) | objects with an Effect component on a locomotion list | locomotion, per route segment change [inferred] | verified decompile |
| `0xC63E40` | `IC_World_ObjectDetachAutoDestroyEffects 0x797B20` | objects with an Effect component (`g_CompBit_Effect`) | script IC | verified decompile |
| `0xB39770` | `IC_World_ObjectDesigner_SetObjectImpl 0x7875D0` | CAS/design preview object | rare (UI) | verified callers |
| `0xC70150` (owner list removal, with destroy flag) | `0xC71310`, `0xC71A90` (lot unload), and the owner callbacks below | lot objects | lot unload (no jobs: travel/teardown drain) | verified decompile |
| `LotObjects_LoadFromStream 0xC70320`, `WorldManager_Shutdown` callers `0xC63ED0`/`0xD680D0`, `ObjectService_DestroyAll 0x939510`, `0xD68320`/`0xD68540`, `0x5815F0`, `0xCDBD40`, `0xD27F50`, `0xD4C790`, `0xECBE70` | world/lot load and shutdown, build tools (`IC_UserToolUtils_*` at 5 hops), others | placeholders, all objects | load/shutdown (no jobs), rare | verified callers; roles of the last five not read |

**Rates.** Destroys during a job: 1.0-1.2/s in BP, 0.15-0.3/s in IP [verified, `vfRtDestroyInJob`]. Total destroys: ~35/s in an unseeded BP profile (`native-batch3-2026-10-16-bp-native-batch3`: `idcErases` 25,061 in 12 min; `IdcEraseHook` counts every `EraseNode` call whether the cache is on or off [verified `idcache.cpp`]) [verified run file; other build, other save state]. With jobs running 14% of the time (`asyncUs` 85.7 s / 600 s in `bp-env-on`), a uniform rate would put ~5/s inside jobs; the observed 1.1/s says destroys cluster outside job time, or the batch3 run churned more [inferred, open question 2]. No run records which caller a waiting destroy came from; P-G (§7) adds that.

**Managed reach of the destroy.** `DestroyById` reaches `GlobalHandlerChain 0x936C90` at hop 1 for events 5 and 6 (and 10 / `0x0C` in the owner callbacks), a native listener table with no handlers registered for these events (docs/50 G) [verified call, empty table inferred]. No `World_FireEvent`, remote call, `ScriptHost_InvokeDelegate` or runtime invoke within 7 direct hops [verified]. The managed reach is in the **free**: `Object_Destruct` calls each component's `+0x2C`; the script component releases its managed object through host vtable `+0x10C` (`ScriptComponent_ReleaseScriptObject 0x94EE30`, docs/50 H) [verified docs/50]. One `Lock_Acquire` is reached (hop 2, `0xAE00A0` on the free list lock `0x1068820`, a pool allocator for the lot's shell-object message; leaf) [verified].

## 3. What destroy touches, and what the job reads

### 3.1 Destroy, 7 hops

Roots: `DestroyById 0x939420`, its owner callbacks (world object manager `0xAA8FF0`, lot `0xACE060` -> `0xACD7B0`; both end in `0xC70150`), script `RemoveObjectFromObjectManager 0x798D40`, `Object_DeletingDtor 0x935DF0` and the footprint component's `+0x2C` `0x947A70`. 349 functions, 143 register-indirect calls [verified closure].

| Hop | Content | Writes | Tag |
|---|---|---|---|
| 1 | `ObjectId_Lookup 0x939100` (the id), `lock inc` of `obj+4`, event 5, owner `obj+0x30` (`0x68EC50`) vtable `+0x3C`, `ObjectService_RemoveFromUpdateList 0x938CA0` (swap-remove in `svc+0x11D4`, index in `obj+0x28` low 22 bits), `obj+0x28 |= 0x400000`, `ObjectIdHash_EraseNode 0x938D00`, event 6, two `lock dec` with the deleting destructor (slot 0, argument 1) when a count drops from 1 | update list, `obj+0x28`, predecessor `+0x10` or bucket head, `svc+0x1028`, `obj+4` | verified decompile |
| 1-2 (owner) | world manager: event 10, `0xC70150`; lot: render flags `lot+0x425`/`+0x334`, event 10, `0xACD7B0`, `0xC70150` | `0xC70150`: owner vector `+0x14/+0x18` (`0x5C0970`), two owner id maps (`0xC6FF80`, `0xC70030`: chain unlink and `EA_Free`), `obj+0x30 = 0` (`0x935760`), one `lock dec`; `0xACD7B0`: the lot-membership component (`0x11ECF08`) bits `+0x2C` (`0xC899F0`) and bytes `+0x30..+0x35` (`0xC89730`), the lot's per-cell id vectors at `lot+0x2D0` (`0xACCD00`, `memmove`), a handle registry unlink (`0xC0AD80`), the lot's buildable-shell and greenhouse objects under the lot critical section `lot+0xF0` (only when the destroyed object is one of them; posts message `0xA80B189`) | verified decompile |
| 2-7 | allocator, lot render pending lists (`LotRenderer_ErasePending 0xA8F0A0`), render message posts (`RenderMessageBuffer_Post 0x593E60`), lot lookups (`WorldManager_FindLotById 0xC60D30`, `Lot_RoomLookup 0xA8DF80`, `Lot_TileLookup 0xAB7ED0`: reads) | lot render and shell state | verified closure |

**Routing state: none.** The closure contains none of `LotRouting_RemoveObjectFootprints 0x635700`, `RemoveFootprintLocked 0x82A0C0`, `LockedLotUpdate 0x82B450`, `UpdateObjectFootprintCore 0x8321F0`, `RemoveDiscourageFootprint 0x834860`, `RemoveLot 0x829D50`, `UpdateSimFootprint 0x833560`, `Locomotion_Notify 0x947590`, `0x947160`, the quadtree mutators `0x6823E0`/`0x682080`/`0x6819B0`, `LotQuadTree_Insert 0x666C50`, `0x6675B0`, `LotTable_GetOrInsert 0x62EAB0`, `LotTable_Contains 0x628A90`, and no `+0x918` acquire [verified, direct calls]. This matches docs/84 §7.2 (destroy removes no routing footprints) from the other side. The component destructors (`+0x2C`, register-indirect) are not followed here; the footprint component's only releases its polygon objects (docs/84 §7.2) [verified]. Every destructor runs in the free, which design G keeps at a no-job point.

### 3.2 The job's object reads, 8 hops

Roots `IC_Route_Plan 0x765160`, `Route_PlanLocked 0x82C6E0`, `RoutingJob_Execute 0x81B2A0`: 1,104 functions [verified closure, direct calls]. Functions in it that call `FindById` (all on the worker during a job):

| Function (hop) | Called from (in the job) | Object | Pattern | Tag |
|---|---|---|---|---|
| `0x81A340` (1) | `RoutingJob_Execute` | ids from the routing options (ignore/goal lists) | lookup | verified call |
| `Route_PlanCommon 0x7606E0`, `0x750260`, `0x751FF0` (2-3) | the plan | the routing object (the Sim) and its components | lookup, `Object_GetComponent` | verified calls |
| `0x75D920` -> `0x74BE00`, `0x752480` (2-3) | `Route_StartPointsFromPosition` | start / goal objects | lookup, component, `RefPtr_Assign` | verified calls |
| `0x65BC20` (3) | `PathPlanner_PlanHierarchical` | portal objects | lookup | verified call |
| `0xB74A20` (4) | `Route_GoalStep` -> `0xB793F0` -> `0xB74D10` | goal footprint owners | lookup, component, transform `0x958EE0` | verified calls (docs/60 §1.4) |
| `0x636BF0` (6), `0x643010` / `0x6430F0` / `0x643180` (6, from `0x643E60` with `LotQuadTree_QuerySegment`), `0x627C40` (8, from `0x6351A0`, `0x634930`) | lot graph build, segment queries, portal edge cost | ids stored in footprints and portals (any object with a routing footprint) | **lookup, then `lock inc obj+4`, reads (`obj+0x18..+0x24` resource key for "StairsHelper"; lot-membership component `+0x18`/`+0x20`), `lock dec` with the deleting destructor on the last reference** | verified decompile `0x643010`, `0x627C40` |
| `Portal_LockState 0x64F2C0` (7) | `Portal_IsLocked` / `Portal_IsDiscouraged` | portal owner | lookup | verified call |

Plus the footprint records' own id resolution through slot `+0xA8` (`0x80C8C0`, docs/84 §7.2), not in this direct closure.

### 3.3 Overlap

The only functions both closures share are readers or generic helpers: `ObjectId_Lookup`, `FindById`, `ObjectService_Get`, `Object_GetComponent`, the allocator, `RefPtr_Assign`, lot lookups [verified intersection: 117 functions; the ones read are readers or allocator code]. None of the destroy's writes (§3.1) is read in the job's direct closure: `0xACCD00`, `0xC899F0`, `0xC89730`, `0xC89EE0`, `0xC0AD80`, `0x5C0970`, `0xC6FF80`, `0xC70030`, `0x938CA0`, `0x68EC50`, `0x935760`, `0x749660` (the lot-membership component getter) are absent [verified]. `0x627C40` reads the lot-membership component at `+0x18`/`+0x20`, not the `+0x2C..+0x35` the owner callback writes [verified both decompiles]. Readers through vtables are not excluded [inferred none].

**The three job-side hazards a destroy creates, and which survive design G:**

| Hazard | Today without the wait | With G | Tag |
|---|---|---|---|
| H-a: chain walk in `ObjectId_Lookup` while `EraseNode` unlinks | safe while the node's memory is valid (erase does not touch the node's `+0x10`; x86 stores are atomic and ordered) | safe: the parked reference keeps the memory | verified decompile, x86 order |
| H-b: free while the worker holds the pointer (between `FindById` and its `lock inc`, or a lookup without AddRef followed by component reads) | use after free, or a block reused by a new object | removed: the count cannot reach 0 while parked | verified pattern, race inferred |
| H-c: the worker's own `lock dec` is the last one (the sim dropped the hash and owner references meanwhile) | `Object_Destruct` on the worker: component destructors and the managed release run off the sim thread | removed: the parked reference is dropped on the sim at a no-job point | verified pattern |
| H-d: a lookup inside one plan finds the object, a later lookup of the same id misses | possible | still possible: the plan sees the destroy "between" two lookups; the planner treats a missing object as absent (returns 0 at each site) [inferred] | inferred |

H-d is new behaviour versus the waited destroy, where the whole plan saw the object. It matches what a sync plan would see if the destroy had come first, except within one plan. P-G's shadow counter `dgJobMissParked` (§7) measures it.

## 4. Cost split

| Run (build) | Destroys in job | Wait s | ms/s | Share of `blockUs` | Mean wait ms | Mean job ms |
|---|---|---|---|---|---|---|
| `h6-m1 bp-p0-async` (0.11.4, P0) | 603 | 5.24 | 8.7 | 11.8% | 8.7 | 6.1 |
| `h6-m1 bp-env-on` (0.12.x, D on) | 356 | 4.02 | 6.7 | 8.4% | 11.3 | 7.3 |
| `clean-night bp-env-on` | 681 | 5.28 | 8.8 | 9.2% | 7.8 | 5.1 |
| `clean-night bp-s2-on` | 621 | 5.12 | 8.5 | 9.1% | 8.2 | 5.5 |
| `clean-night bp-s2-shadow` | 631 | 5.14 | 8.6 | 9.2% | 8.2 | 5.2 |
| `h6-m1 bp-d-async-3` / `-4` | 747 / 720 | 6.55 / 5.71 | 10.9 / 9.5 | 15.1% / 12.5% | 8.8 / 7.9 | 5.7 / 5.8 |
| `clean-night ip-env-on` | 173 | 2.25 | 3.8 | 4.5% | 13.0 | 10.6 |
| `h6-m1 ip-env-on` | 92 | 2.57 | 4.3 | 5.1% | 27.9 | 15.8 |
| `clean-night ip-ab-routeasync` (2026-10-17 / -18, half the load async) | 94 / 137 | 1.89 / 2.03 | 3.1 / 3.4 | 6.4% / 4.8% | 20.1 / 14.8 | 11.9 / 11.7 |

All runs: `vfRtDestroyInJob` = waits (every destroy inside a job waits), `vfRtDestroyOffSim` = 0, `vfRtDestroyInInvoke` = 0, `g11EndDestroy` 1-11 [verified]. The mean wait exceeds the mean job by 1.3-2.6x: a destroy lands in long jobs more often than in short ones (length-biased sampling), and many destroys come in the stages right after the tick's START, i.e. near the start of a job [inferred, stage order verified docs/82 §8].

Our stub's own cost outside jobs: a few compares and two interlocked operations per destroy (~35/s), negligible [inferred].

## 5. Design options

Rules as in docs/84/86: sync reads the sim needs now cannot move to the worker; managed reach stays on the sim; a lock-free change needs every job-side reader of the written data to be safe; gains do not add.

| # | Option | What | Risk | Effort | Gain cap [inferred] |
|---|---|---|---|---|---|
| Q | **Queue the destroy to the worker in order** (like D's co-location idea) | the worker runs `DestroyById` between jobs | the free reaches managed code (script component release) and the owner callbacks post render and lot messages; the sim and script expect the object gone at once (`ObjectGuid` invalid, `FindById` null). Not possible | - | not applicable |
| DF | **Defer the whole destroy on the sim to the next IC point with no job** (D's pattern) | record the id, return as if destroyed, run the original later | script reads the object after `Destroy()` (it is still in the hash: `FindById` finds it, `IsValid` true, events 5/6 late, update list still ticks it); event order against later creates; a create that reuses the slot or id; D-X ordering with Remove records. High | 2-3 d | 8.8 ms/s, same as G |
| S | **Split destroy: now the non-routing part, routing removal later** | as asked | destroy has no routing part (§3.1); the part that must wait is the free, which is what G defers | - | = G |
| L | **Lock-free skip when the object has no routing presence** | skip the wait when the object has no footprint component (and is no portal / route target) | the job's lookups are not limited to footprint owners (§3.2: route targets, ignore lists, portal owners, the routing Sim); presence cannot be decided from the object alone, and a wrong guess is a use after free. Effects and state machines would pass a class test, but a class allow-list is brittle | 1 d | the effect/state-machine share of 8.8 ms/s (unknown until P-G) |
| P | **Gate policy change: no wait at all** | drop closure #4 | H-b and H-c (use after free, destructors on the worker). Not acceptable | 0.2 d | 8.8 ms/s |
| H2 | **Covered by docs/86 H2?** | - | no: H2 opens its window only around `Locomotion_Update`; the destroys come from script tasks, finalizers, `Animation_SACSPass` and `Animation_UpdateActors` (§2, docs/82 §8 order), and H2's held jobs meet them later (docs/86 §5 note) | - | 0 |
| **G** | **Destroy graveyard (recommended)**: park one reference, destroy now, release when no job runs | §5.1 | §5.2 | 1.5 d | 8.8 ms/s BP, 3.1-4.3 ms/s IP; realistic 3-6 ms/s BP |

### 5.1 Design G

In `RtEnter` `RT_DESTROY` (existing stub, no new hook site; `thiscall(svc; idLo, idHi)`, `ret 8`, id at `slot[1]`/`slot[2]` [verified D-X]):

1. **Flush first** when this is the sim thread, the graveyard is not empty and no job runs (below).
2. **Park** when all hold: `rgEnforce`, `[DestroyGrave] Enabled=1 Mode=1`, this is the owner (sim) thread and not inside a job callback (`g_inInvoke`), `g_async.state == A_RUNNING` (read after `g_rgOffSimOpen` is raised, as today), not `rtClosing`, D-X has **no** queued record for this id in the current world (else keep today's wait and `DiscOnDestroy`), the graveyard has room (cap 512). Then `ObjectService_FindByIdAddRef 0x9395A0 (svc; idLo, idHi, &obj)` (`ret 0xC` [verified D-X]); if found, store `obj` and the job sequence number in the graveyard and skip `RtWaitJob`. The original then runs as today: events, owner, update list, flag, erase, its two `Release`s; the count stays at least 1 (ours).
3. Otherwise (no job, not sim, closing, D-X record, full): today's path unchanged (wait, `DiscOnDestroy`).

**Flush** (sim thread, IC points only): raise `g_rgOffSimOpen`, read `g_async.state`; if a job runs, drop the count and leave the graveyard for the next point; else release every parked object with `RefPtr_Release 0x7460F0` (the game's own `lock dec` and deleting destructor [verified D-X use]) in park order, then drop the count. Flush points: the `RT_DESTROY` stub entry (above), `GetOptionHook` READY/START polls, the D drain points (`DiscPoll` callers), and, waiting for the job first, `RT_SAVE`, `RT_CLOSING`, `RT_TEARDOWN` (travel, quit, world init/teardown) and graveyard overflow. **Never** at `LockAcquireHook`/`RgEnter` gate entries: the free reaches managed code (script component release), and a plain acquire can sit inside a native caller mid-update (docs/84 §7.1 argument). Because every `DestroyById` entry with no job flushes first, `ObjectService_DestroyAll` and the shutdown destroys empty the graveyard before any object service teardown [inferred: jobs are refused while closing, so the first shutdown destroy flushes].

Why the flush may run Object_Destruct there: an extra holder delaying the free is a vanilla state (the discourage ICs, `0x643010`, `0x627C40` and `0x84CE30` all hold a temporary reference across calls that may destroy) [verified patterns], and the deferred `Release` runs on the same thread with no job running, the conditions the free has today after the wait.

### 5.2 Risks

| Risk | Handling | Tag |
|---|---|---|
| Dangling pointer on the worker (H-b) | the parked reference keeps the block until a no-job flush; a job that starts after the destroy cannot find the object (it left the hash before the job started, since starts are blocked during the destroy) | design; start rule verified |
| Object reuse after free | the block is not freed while parked, so no new object can occupy it during the job. Ids: a parked object is out of the hash, so `ObjectService_NewUniqueId` could hand out its id again (64-bit random, negligible), and stream loads (`ReserveId`) run only with no job (docs/84 §7.2); the graveyard keys by pointer, not id | verified id code (docs/84), probability inferred |
| Refcounts | one `FindByIdAddRef` and one `RefPtr_Release` per park, both the game's own atomic code; the worker's own pairs can no longer reach 0 (H-c) | verified functions |
| Visible lag of the free | component destructors (render node, physics shape `Realm_RemoveShape`, effect stop, managed release) run at the next IC point, usually the same tick [inferred]; a destroyed object's physics shape can stay in the quadtree for the rest of the running job, so the job may route around it (stale by one job, as D) | inferred; T-G3 measures age |
| H-d (object vanishes between two lookups of one plan) | counted in shadow (`dgJobMissParked`); the lookup sites return "not found" paths [inferred] | inferred |
| D-X order | ids with queued discourage records keep today's wait and apply, so D-X's guarantee is unchanged | design |
| Memory | up to 512 parked objects for at most one job plus one poll interval | inferred |
| A destroy from inside a job callback (`g_inInvoke`) or off the sim | unchanged (count, no park): 0 in every run | verified counters |

## 6. Offline proof (steerro-test style)

1. **steerro-test section I (new):** map TS3W.exe as the other sections. Build a fake object service in our memory (1,033 bucket heads, the miss sentinel at `+0x1024` and its pointer at `+0x1034`, count `+0x1028`, an empty update vector `+0x11D4/+0x11D8`) and fake objects (vtable whose slot 0 is our deleting destructor: counts the thread, poisons the block with `0xDD`, frees it; `obj+0x30` owner NULL; `obj+0x28` index 0; ids at `+8/+0xC`; a canary at `+0x18`). The game's global handler table for events 5/6 is empty in the mapped image (no init ran) [inferred, assert it]. A "worker" thread loops the game's own `ObjectService_FindById 0x939560` over a set of ids, then `lock inc`, reads the canary, `lock dec` (the `0x643010` pattern) and records any poisoned read and any destructor call on its thread. The main thread sets `g_async.state = A_RUNNING`, runs the game's `DestroyById 0x939420` through our `RT_DESTROY` stub for half the objects, ends the "job", runs a flush. Pass: graveyard on: 0 poisoned reads, 0 worker destructors, destructor count = destroyed count only after the flush, every count 0 at the end, hash count `+0x1028` right; graveyard off (wait removed, the unsafe option P): poisoned reads or worker destructors > 0 over the run as the negative control. Seeds 1, 7, 99, 2026 and the default.
2. **selftest:** `TestDestroyGrave`: park rules (no job, D-X record queued, closing, full, in-invoke, off-sim), flush refused while `A_RUNNING`, flush order, overflow waits, counters; `TestRouteEnforce` and `TestDiscourageDX` unchanged.
3. **tools/emu spec** `destroy_grave.py`: the stub path with a stub `0x9395A0`/`0x7460F0` checking the conventions (`ret 0xC`, `ecx` = service).
4. `node tools/hook-sites.ts --check` (no new site: `0x939420`, `0x9395A0` and `0x7460F0` are called, not hooked), `node tools/check-docs-code.ts`.

## 7. Probe P-G and live jobs

**P-G (count only, same build, `[DestroyGrave] Mode=0` = shadow):** at each `RT_DESTROY` during a job, keep today's wait, and record (a) the caller key (return address `slot[0]`, top 32 like `blockKey`), (b) the object's component bits (footprint `0x11E8FA8`, effect, animation, script; `obj+0x2C` mask [verified `Object_Destruct` reads it]), (c) whether D-X had records. In shadow also park the reference (it is harmless while the wait still runs) and count `dgJobMissParked`: worker `ObjectId_Lookup` misses for an id that is parked (through the existing `[IdCache]` lookup detour, worker thread only).

Counters (reserve in docs/registry.md): `dgParked`, `dgFlushed`, `dgPendingMax`, `dgAgeMaxUs`/`dgAgeSumUs` (park to release), `dgFlushRefused` (job running at a flush point), `dgKeptWaitDisc`, `dgKeptWaitFull`, `dgKeptWaitClosing`, `dgJobMissParked`, `dgKey_0..31`/`dgKeyN_0..31`, `dgKindFootprint`/`Effect`/`Anim`/`Script`. Tripwires: **T-G1** `dgCountLow` (a parked object whose count `obj+4` is below 1 at flush, read before our release) = 0; **T-G2** `dgFlushInJob` = 0; **T-G3** `dgAgeMaxUs` under 100 ms (one job plus one poll).

Live, in `queues/clean-night*.json` only (clean machine), queue + D + D-X on, seeded:
1. **`bp-dg-shadow`** (Mode 0, 10 min): caller and kind split of the 8.8 ms/s; `dgJobMissParked`; pass = game alive, 0 watchdogs, T-G2 = 0.
2. **`bp-dg-ab`** and **`ip-dg-ab`** (in-load `--ab destroygrave`, bundle rule): Mode 1 halves `vfRtDestroyWaitUs` near 0 except kept waits, total `blockUs` and iterations/s against Mode 0, T-G1..T-G3, 0 crashes, stuck Sims logged only; any crash in `0x939xxx`, `0x935xxx` or `0x64xxxx` stops the run (crash-cause rule).
3. **`bp-pet-guard-dx-dg`**: the `pet-guard-cancel` repro with G on: `dgKeptWaitDisc` > 0 and D-X's T-D5 still holds.
4. Repro for churn: `repros.json` `rt-s2b-sweep` already runs an object create/destroy churn twice (both Mode slices see destroys and quadtree writes) while Sims route, and `alloc-dupfree` churns objects, effects and Sims [verified repros.json]; use `rt-s2b-sweep` with `--ab destroygrave` as the targeted repro, no new agent action needed.
Re-run P0 after (gains do not add).

## 8. Open questions

1. The caller and object-kind split of the 8.8 ms/s (P-G, I-88-4); if effects and state machines carry almost all of it, option L for those two classes becomes a cheaper fallback.
2. Why destroys land in jobs at ~1.1/s against ~35/s total (I-88-5): clustering by stage, or a different churn in the batch3 run.
3. H-d frequency and its effect on plans (I-88-6).
4. The host slot behind `IC_Simulator_DestroyObjectImpl` (`ScriptHost_GetWorld` vtable `+0x17C` -> `+0xC`) is assumed to end in `DestroyById` (docs/50) and does not do more first [inferred, I-88-7].
5. Component destructors (`+0x2C`) of other classes than the footprint component, run at flush time: no job runs then, so their reach does not matter for safety, only for the flush points' managed-safety (IC points) [inferred].

## 9. Ledger

Rows `I-88-1` .. `I-88-8` in docs/51 §19.
