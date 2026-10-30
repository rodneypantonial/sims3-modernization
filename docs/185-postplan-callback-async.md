# 185. Plan callbacks (`AboutToPlanCallback` / `PostPlanCallback`) with `[RouteAsync]`

Written 2026-10-24. **Static only:** ts3fixes `RouteBackoff.cs`, ts3native `ts3native.cpp` (`GetOptionHook`, `AsyncStart`, `IcCollect`), the extracted script assemblies (`ScriptCore.Route`, `SimRoutingComponent`, `TombRoomManager`, read in `game-copy/decompiled`, nothing copied) and the NRaas source clone (`game-copy/nraas-src` @2f57186: GoHere `AncientPortalRoutingHelper`, `DoorPortalComponentEx`; Overwatch `StuckCheck`). No game run, no build. Follows docs/184 §2 (GoHere nested plans in a plan callback).

## Summary

| # | Case | Verdict | Tag |
|---|---|---|---|
| 1 | When `PostPlanCallback` runs with an async plan | sim thread, in the calling script task, inside the original `Route.Plan` (`PlanOrig`) right after the IC collect, before our `finally` CANCEL; same position relative to the result as without us | verified (code) |
| 2 | Nested `Route.Plan` from inside `PostPlanCallback` (GoHere `Reroute`) | **safe**: the outer job is freed at collect, `g_collect` / `g_rfbHandle` are consumed before the callback runs, CANCEL is per handle; the nested plan is own-task async if the interpreter state may yield, else NoYield sync; it never re-enters the outer job | verified (code); the yield state inside a delegate invoke is inferred |
| 3 | Nested `ReplanFromPoint` on the **same** route inside its own callback | **safe** in release (`Kinds=0`: sync); with A10 on, a new job on the same handle after the outer one was freed, CANCELs harmless | verified (code) |
| 4 | Queue depth / deadlock | **safe**: nested plans run one after another in the same task (+0 queued jobs net); a sync nested plan waits for a running job through the existing Lock_Acquire pump, as any sim-thread plan | verified (code, docs/18) |
| 5 | Overwatch StuckCheck V2 chains with GoHere's replacement | **yes**: GoHere removes only EA's `SimRoutingComponent.PostPlanRouteCallback`; Overwatch adds its own; both run on every plan | verified (NRaas source) |
| 6 | **`AboutToPlanCallback` runs after the async job** | **risk**: the job plans without this plan's callback inputs (forbidden portals, entrance / ghost / car options); the late call then clears `LockedDoorsDuringPlan` the job just filled, so a `FailedBecauseDestinationPortalLocked` result loses its door | mechanism verified (code); frequency and player effect inferred |
| 7 | Our path turns a vanilla success into a failure (`r.PlanResult`) | no verified path in the release config (caps off); case 6 mostly makes plans *less* constrained; the backoff skip never fires the callbacks | inferred |

Player report (8,060 plans, 961 failed, 2,549 `Foreign` sync): 11.9% failed is the vanilla Isla Paradiso rate (12%, docs/04, measured before async routing); the `Foreign` plans run the game's own sync path. Case 6 is the one mechanism here that could add Overwatch counts: a Sim whose plan fails at a locked door would, in vanilla, walk to the door (`DoRouteFailForDoor`) and so change position; after an async plan it takes the generic route failure in place, and Overwatch counts failures only while the Sim stands still [inferred]. Fix design in §5, repro `route-aboutplan-order`.

## 1. Order of events

`ScriptCore.Route.Plan` (`Route.cs` 900-925): `IsDestObjectInvalid` -> `AboutToPlanCallback(this, "Plan", destination)` -> IC `Route_Plan` -> start points -> `PostPlanCallback(this, "Plan", result string)` -> return the field `mRoutePlanResult` (read after the callback, so a callback that replans the same route changes the returned result). `PlanFromPoint` passes "Plan" too; `Replan` / `ReplanFromPoint` pass their own names (`Route.cs` 965-1007) [verified].

With ts3fixes (`RouteBackoffHook.PlanHook` -> `PlanBackoff` -> `PlanAsync`, `RouteBackoff.cs` 139-157):

| Step | Vanilla | Async own-task plan (release: `asyncroute` on, `[RouteAsync] Mode=1`) |
|---|---|---|
| a | | START (`GetOption` bit 58) -> `AsyncStart`: the worker runs the whole `IC_Route_Plan 0x765160` on the route **as it is now** |
| b | | `AsyncWait`: `Simulator.Sleep(0)` per tick until READY; READY arms `g_collect = handle` |
| c | `AboutToPlanCallback` | `PlanOrig` = the original `Route.Plan`: `AboutToPlanCallback` (**after the search**) |
| d | IC searches | IC detour `IcCollect`: `g_collect`, `g_rfbHandle` cleared, stored 0x48-byte result copied, job set `J_FREE` |
| e | `PostPlanCallback` | `PostPlanCallback`, same task, sim thread |
| f | | `finally`: CANCEL (`GetOption2` bits 58+62): `JobFor(handle)`, already free |

Sync fallbacks (START refused: NoYield, Foreign with `FallbackAsync=0`, M1*, QueueFull, ...) call `PlanOrig` at once: vanilla order. The backoff / failrepeat skips (`FailFast`) return without `PlanOrig`: **no callback of either kind runs** for those plans (unseen townies, `backoff` on; `failrepeat` off).

## 2. `AboutToPlanCallback` after the job (case 6)

What the callbacks change before the IC, and what the job misses:

| Callback | Inputs it sets | Effect when it comes after the search |
|---|---|---|
| EA `SimRoutingComponent.AboutToPlanRouteCallback` (`SimRoutingComponent.cs` ~1996-2080) | clears `LockedDoorsDuringPlan`; `BeginAsCar` / `BeginAsBoat` from the current routing parent; `ForceUseMainEntrance` for service Sims; `RouteAsGhost` from `CouldUseGhostRouting(sim, current lot, goal lot)`; `EnablePlanningAsCar` off in Egypt for unselectable Sims and on an always-on hoverboard; `ClearForbiddenPortalList` then the closed-venue doors (unless the interaction is `IAllowedOnClosedVenues`, not on GoHome/GoWork) | job ignores this plan's goal-dependent options and closed-venue doors; `CreateRoute`'s `SetRouteOptions` already set car/boat/ghost from the state at creation, so those differ only when the state changed since [verified code; frequency inferred] |
| | the job's door answers (event 59 -> `Sim.OnPortalPathPlan`, served on the sim thread during the job) fill `LockedDoorsDuringPlan` | the late call **clears** them; the `FailedBecauseDestinationPortalLocked` branch of `DoRouteFailureBehavior` (`SimRoutingComponent.cs` ~1328-1370) finds no door, so `DoRouteFailForDoor` never runs [verified code order] |
| EA `TombRoomManager.AboutToPlanRouteCallback` | forbidden doors and stairs of tomb lots | tomb doors open to the job (World Adventures) |
| GoHere `DoorPortalComponentEx.AboutToPlanRouteCallback` | GoHere's per-door rules (non-autonomous routing into rooms) | rules ignored by the job |
| other mods | unknown | unknown |

The options set late stay on the route, so the Sim's later sync replans (`Kinds=0`) do see them; only the first plan of each route is off. Release scope: own-task kind-0 plans only (`FallbackAsync=0`, `Kinds=0`, `drivertaskasync` / `stuckcheckown` off); with `FallbackAsync=1` or A10 kinds every async kind has the same order. docs/104 §3.4 already listed "the async collect changes their order" without a verdict; docs/18's "as without us" was wrong for this callback.

## 3. `PostPlanCallback` and nested plans (cases 1-4)

- **Thread and task:** the callback runs inside `PlanOrig`, i.e. on the sim thread in the task that called `Route.Plan`, after the IC collect. The result it reads (`r.PlanResult`) is the collected job result, the same value the caller gets [verified].
- **Outer state during the callback:** `IcCollect` set the job `J_FREE` and cleared `g_collect` / `g_rfbHandle` before returning; `AsyncStart` clears them again for a nested START. Nothing of the outer plan is pending, so a nested plan cannot steal or strand the outer result. The outer `finally` CANCEL looks the job up by handle and finds none (or, case 3, the nested one already freed) [verified, `ts3native.cpp` `IcCollect`, `GetOptionHook`].
- **Class of a nested plan:** GoHere's `Reroute` plans for `r.Follower` (the same Sim) in the same task: own-task START when the outer plan came from the Sim's task, else foreign (sync with `FallbackAsync=0`). `CanYield` decides on the interpreter state: a managed delegate invoke (`callrun`, docs/130) is expected to stay on the task's root state, so the nested plan sleeps inside the callback; if it is a fresh state, START answers NoYield and the plan runs sync [inferred; both outcomes safe].
- **Sleeping inside the callback:** the outer route is held by the sleeping frame; nobody else follows or replans it meanwhile. A Sim reset unwinds both `finally` blocks (two CANCELs) [inferred from the reset path, docs/18].
- **Queue depth:** each nested plan finishes before the next starts (the task waits), so a callback adds at most one job at a time and the outer job is already free [verified].
- **Deadlock:** a sync nested plan reaches `Route_PlanLocked`, which waits for a running job through the Lock_Acquire pump (runs the job's managed calls), as for any sim-thread plan [verified design, docs/18].
- **Our per-plan hooks:** `NotePlanResult` sees nested results first, then the outer one; `routesrc` (off) times the nested plans inside the outer bucket too (double count, observe only) [verified code].

## 4. Overwatch StuckCheck V2 (case 5, 7)

- `StuckCheck.OnPreLoad` adds `OnPostPlan` to `Route.PostPlanCallback`; GoHere's `AncientPortalRoutingHelper.OnPreLoad` removes EA's `SimRoutingComponent.PostPlanRouteCallback` and adds its own. Different targets, so both stay in the multicast list [verified]. Their order follows `OnPreLoad` order [inferred].
- `OnPostPlan` counts a plan only when the Sim has not moved since its last counted plan and `r.PlanResult` failed; more than `mMinimumRouteFail` (10) over more than `mRouteFailTestMinutesV2` (30) sim-minutes resets the Sim ("Unroutable") [verified source].
- When GoHere reroutes (subway / hover-train planning on), its nested `r.ReplanFromPoint` fires the callbacks again for the same route ("ReplanFromPoint"), so Overwatch sees two results for one outer plan, and if GoHere runs first the outer call reads the replanned result. Vanilla-with-GoHere does the same; we do not change it [verified source].
- Failures where vanilla succeeds: none found in the release config. `SyncCapMs`, `FbJobCapMs`, `PlanDeadlineMs`, `FailMemo`, `ReplanDedupe`, `[RouteBudget]`, `[RouteEarlyOut]` acting are all 0/off in `ts3native.ini`; a skipped or uncollected job is re-planned sync (`IcCollect` -> `SyncIc`); the `backoff` failures never reach Overwatch (no callback). Case 6 is the only async difference in result inputs, and it removes constraints more often than it adds them [inferred].
- More notices than vanilla can still come from case 6's door branch (§2) and from Sims that stand still longer while their task sleeps [inferred]. ts3fixes `overwatchstucknotice` (one notice per 2 sim-hours) already caps the notice spam.

## 5. Fix `preplancb` (built 2026-10-24, offline, not run)

Goal: the job sees the inputs vanilla's search sees, and the door list survives.

1. ts3fixes `PlanAsync` / `ReplanAsync` / `PlanFromPointAsync`: when the plan is eligible for a START (own, owner-task or foreign), invoke `Route.AboutToPlanCallback` with the same arguments the original would (`"Plan"` + destination; `"Replan"` / `"ReplanFromPoint"` + `PlanResult.mDestination`) **before** the START. A refused START then plans sync with the callback run twice (idempotent for EA's callbacks: set options, clear-then-add portals).
2. After `AsyncWait`, copy `LockedDoorsDuringPlan` of the follower Sim; after `PlanOrig` returns, restore it when the second callback emptied it and the copy is not empty (EA reads it only in `DoRouteFailureBehavior`, after the plan; NRaas `ResetSimTask.ResetRouting` reads it on a reset, docs/184 §3; I-150-13-5).
3. Switch `preplancb` (default off until its A/B), counters (reserve in docs/registry.md): pre-calls made, door lists restored.
4. Cost: one extra callback per async plan (`TombRoomManager`'s lot query, docs/74: ~1% of script samples on tomb lots; `spatialcache` dead end, so no cheap cache).

Rejected: nulling the static delegate around `PlanOrig` (nested plans in `PostPlanCallback` and other tasks during their sleeps would lose every callback); wrapping the multicast delegate to skip the second call (mods add to the delegate after us and are not skipped); our own copy of `Route.Plan`'s tail (EA logic duplicated, private fields).

Selftest: a `tools/selftest` case with a fake route whose callback records call order against START (pre-call before START, one restore). Repro `route-aboutplan-order` (§6). ~1.5-2.5 h + 1 game run.

### 5.1 As built

- **Code:** `RouteBackoff.cs` `PreCb` (+ `DestObjectInvalid`, `LockedDoors`, `RestoreDoors`), game-independent parts in `PrePlanCbCore.cs`. Called right before each START once the plan is eligible: `PlanAsync` own START, `OwnerTaskStart` (`drivertaskasync` / `stuckcheckown`), the foreign kind-0 START, and `AsyncKindStart` for kinds 1-3 (own and foreign; `ReplanFromPoint` too, it has the same order). One pre-call per plan even when the owner-task START is refused and the foreign one follows. Tuning `kPrePlanCallback`, switch `preplancb` (default off), counters 295 pre-calls, 296 door lists restored (agent `preplancb_precalls`, `preplancb_doors_restored`). Script only; ts3native unchanged.
- **Arguments, from the IL** (`managed-callgraph.ts callers "Sims3.SimIFace.Route+AboutToPlanDelegate::Invoke"`, same five callers in the installed mod copies of ScriptCore / Sims3GameplaySystems / Sims3GameplayObjects): `ScriptCore.Route::Plan()` and `PlanFromPoint(Vector3)` pass `"Plan"` + the private destination point (= `GetApproximateOriginalDestination()`), `Replan()` `"Replan"` and `ReplanFromPoint(Vector3)` `"ReplanFromPoint"` + `PlanResult.mDestination`; each calls it after `IsDestObjectInvalid` and before its IC. `PreCb` skips the pre-call the same way (the private `IsDestObjectInvalid` by reflection; its side effect is the failed result the original sets next anyway). The fifth caller, `Sims3.SimIFace.Route::PlanToLotCore(ulong)`, invokes the delegate itself with the lot point and then calls `Plan()`: **EA already runs the callback twice per plan there** [verified IL].
- **Subscribers and a second call** (EA assemblies via the call-graph method list; NRaas source @2f57186; installed mods: only the replaced EA DLLs reference the delegate, same four EA subscribers):

| Subscriber | What it does per call | Second call |
|---|---|---|
| EA `SimRoutingComponent.AboutToPlanRouteCallback` (Sims) | clears `LockedDoorsDuringPlan`; sets `BeginAsCar`, `BeginAsBoat`, `UseFollowerStartOrientation`, `RouteAsGhost`, `ForceUseMainEntrance`, `EnablePlanningAsCar` off, `ReplanUsingStroller` (replans), `IgnorePrivateFootprints` (`Terrain.PreModifyRouteForSpecies`); `ClearForbiddenPortalList` then closed-venue / not-greeted doors; `AddObjectToIgnoreForRoute` for fires (fireproof / ghost / Reaper); `sPrePlanProfileCallback` (never assigned in the game) | same values; forbidden list cleared and refilled; only the door list is lost (restored by us). With `SimFlags.IgnoreClearingForbiddenPortals` the doors and the fire ignore list get duplicate entries, as on every replan of the same route in vanilla [inferred harmless: lists of ids] |
| EA `TombRoomManager.AboutToPlanRouteCallback` | adds tomb doors / stairs of the current and goal lot to the forbidden list | re-added after EA's clear (order of `Startup` vs `SimStartup` not checked); duplicates at worst [inferred harmless] |
| EA `CarRoutingComponent.AboutToPlanRouteCallback` (vehicles) | only `sPrePlanProfileCallback` (null) | no effect |
| EA `BunkBedContainer.AboutToPlanCallback` (loft bed mode, child Sims) | `AddObjectToIgnoreForRoute` for the loft footprints | duplicate ignore entries, as on replans [inferred harmless] |
| NRaas GoHere `DoorPortalComponentEx.AboutToPlanRouteCallback` | for a user-directed interaction, `TestPortalObject` per lot door (reads settings only) -> `AddObjectToForbiddenPortalList`, `DoRouteFail = false` | same doors again; no other writes |
| other NRaas mods | none subscribe (ErrorTrap only clears `LockedDoorsDuringPlan` references; `ResetSimTask` reads it) | - |

  **Choice: keep the second call and restore the door list** (as designed). Every subscriber is idempotent per plan apart from that list, EA itself calls the delegate twice in `PlanToLotCore`, and suppressing the second call would need one of the rejected options (null the static delegate, or wrap it and miss mods added later). Unknown non-NRaas mods that keep per-call state (counters, notices) would see one extra call per async plan [inferred, none installed here].
- **Door list:** `AsyncWait` -> copy of `LockedDoorsDuringPlan` (only when it is not empty) -> the original (its callback clears, `IcCollect` hands over the job result, no new door answers) -> the copy goes back when the list is still empty. A job left uncollected and re-planned sync refills the list itself and is not touched. `PostPlanCallback` subscribers run before the restore; none reads the list (EA reads it only in `DoRouteFailureBehavior`, after the plan returns).
- **Rejected options stay rejected:** nothing nulls or wraps `Route.AboutToPlanCallback`, and `Route.Plan`'s tail is not copied; nested plans inside `PostPlanCallback` run their own `PreCb` with their own local flag.
- **Selftest:** `tools/selftest/preplancb` (in `selftest-all.ts`): a fake route with EA's order and a fake job; checks pre-call before START, the second call kept, one restore, names per kind, refused START (callback twice, sync doors), not eligible (no pre-call), uncollected job (no restore), no door, invalid destination object, no subscriber. The hook's order is mirrored in the fake (not the hook itself).
- **Repro:** `route-aboutplan-order` arm C (`routeasync` 1 + `preplancb`), expected to match arm A. Not run.

## 6. Repro `route-aboutplan-order`

BP spring save: lock the home door nearest the active Sim for everyone but the household (`@door.SetLockType SelectedHousehold`), send visitor townies on a lot visit into the home (their own task: own START), once with `asyncroute` off and once on. Expected today: off arm, the visitors walk to the locked door and show the door-locked failure; on arm, they fail in place. With `preplancb`: both arms the same. Unlock in cleanup.

### 6.1 First run invalid: no visit queued (2026-10-24)

`night-1024-new` job `bp-preplancb-order` passed on `preplancb_precalls >= 1`, but all 6 `sim_visit_lot` steps answered `queued:false`, so no visitor planned into the locked home in any arm [measured: job log]. Cause: the agent queued `VisitLot` as autonomous (`isAutonomous` true), so `InteractionQueue.InsertInteraction` ran `VisitLot.Definition.Test` -> `Lot.CommonVisitTest` with its autonomous-only gates; Wogan Hemlock was already on an autonomous visit (sets `LastAutonomousVisitDate`, visit-frequency gate) and Johnny Jones was going home from one (`LastVisitedSomeone` < 1 day gate) [verified source; which gate fired per visit is inferred, the agent did not report it]. The `preplancb_precalls` pass came from other plans.

Fix: `sim_visit_lot {autonomous: false}` queues the visit player-directed (UserDirected priority, `isAutonomous` false: those gates are skipped, the Sim's own queued autonomy is cancelled, so the visit runs now); a refused visit answers `test` + `why` (the Test result and greyed-out tooltip). The repro's `stepsRequire: "sim_visit_lot.queued>=6"` (repros.json, checked by `profile-world.ts` through `fixes-require.ts` `stepTally`) fails the job unless every visit queued. Gates still left: nobody of the household at home, visitor greeted or inside the lot. Queue `tools/harness/queues/verify-preplancb-order.json` (correctness only). Not run.

### 6.2 Second run invalid: celebrity-lot gate (2026-10-25)

`verify-preplancb-order` job `bp-preplancb-order-verify` FAILED on `sim_visit_lot.queued=0 (want >=6)`; `preplancb_precalls >= 1` passed, no crash [measured: job log, chain log `evening-1024b.out`]. All 6 player-directed visits answered `test: Def_TestFailed`, `why: "This is a private residence. Please use the Electronic Gate to ring the household."` [measured]. Cause: the active household's home (Air Spray, BP) has an Electronic Gate, so `Lot.IsCelebrityLot` is set (residential + `IElectronicGate` count != 0) and `Lot.CommonVisitTest` refuses unless `Lot.CanVisitCelebrityLot`: an adult household member at home with `LTR.Liking > ElectronicGate.kVisitRelationshipThreshold` (25) toward the visitor [verified source: `Lot.cs`, `ElectronicGate.cs` in `game-copy/decompiled`]. This gate is not autonomous-only, so it likely also refused §6.1's visits [inferred]. Not a `preplancb` or door-order defect: no visit ever planned. Fix: two `modify_relationship {other: "active", liking: 50}` setup steps (cheat, labelled) after the lock step. Not run.

### 6.3 Third run: 5 of 6 queued, door-order case ran (2026-10-25)

`verify-preplancb-order` job `bp-preplancb-order-verify` (01:39 UTC) FAILED only on `sim_visit_lot.queued=5 (want >=6)`; no crash, `preplancb_precalls >= 1` passed [measured: job log]. Arms A and B queued all four visits and arm C's first (Wogan); arm C's second visit (Johnny, 01:44:15, 30 s after Wogan passed the same test) answered `Def_TestFailed` with the Electronic Gate tooltip [measured]. `CanVisitCelebrityLot` loops the household's actors and needs one adult with `IsAtHome` and `LTR.Liking > 25` toward the visitor [verified source], so in those 30 s either no such adult was at home or Johnny's liking fell [inferred; the agent does not report which]. Not a `preplancb` defect. The order case itself fired: `preplancb` is on only in arm C (~1 min) and its counters went 0 -> 2189 `preplancb_precalls` and 0 -> 146 `preplancb_doors_restored` [measured: repro step answers], so 146 async plans had their `LockedDoorsDuringPlan` emptied by the late callback and put back (I-319-3/4 confirmed live, fix acts). The per-visitor outcome (walk to the locked door vs fail in place) is not in the step answers: the one `sim_details` per arm shows only lot and queue (both visitors still off the home lot after 30 s), so the A/B/C behaviour difference stays unread [gap].

Fix (pass rule, not the game): liking re-set before arm C; `stepsRequire` `sim_visit_lot.queued>=4` (a household adult leaving home must not void the run); the queue requires `preplancb_doors_restored>=1` next to `preplancb_precalls>=1` (the real signal: the order case ran and was undone). Not run.

### 6.4 Fourth run: 3 of 6 queued, why the visits are refused (2026-10-25)

`verify-preplancb-order` 07:30 UTC FAILED on `sim_visit_lot.queued=3 (want >=4)`, no crash [measured: job log, `-repro.jsonl`]. Arm A (Wogan) and arm B (Johnny) queued; 30 s later both visitors were on the home lot (Air Spray) reading the household's books [measured: `sim_details`], so the swap visits (Wogan in B, Johnny in A) answered `Def_TestFailed` with no tooltip: `VisitLot.Definition.Test` refuses a visitor already greeted on the lot (`IsGreetedOnLot`) or inside it (`LotCurrent == target && !IsOutside`), neither sets a tooltip [verified source, which of the two inferred]. Arm C's first visit (Wogan) queued, the second (Johnny) hit the Electronic Gate again, as in §6.3 (two of two runs). `CanVisitCelebrityLot` passes on any household adult at home with liking > 25 [verified source]; the repro gave liking only to the active Sim (Tiara), and the household's other adult (Lala) works and goes out [measured: activity log], so a visit while Tiara is off the lot is refused [inferred: the agent does not report who is at home].

Fix (repro, no game or mod change): `sim_go_home` for both visitors (30 s) before the swap pass and before arm C, so no visitor is on the lot when its next visit is tested; liking 50 with Lala as well as Tiara at both setup points; `stepsRequire` per arm via a new step field `tally` (`profile-world.ts` counts the step under that name): `visit_a.queued>=1,visit_b.queued>=1,visit_c.queued>=1`, so every arm planned at least one visit into the locked home (arm C's is the one `preplancb` acts on); `preplancb_precalls>=1` and `preplancb_doors_restored>=1` stay. Not run. Gap: on a refusal the agent should list the household adults (at home, liking); not added while a queue runs (`mods/` is rebuilt at job start).

### 6.5 Fifth run: PASS, arm C outcome not recorded (2026-10-25 13:47 UTC); check fixed 2026-10-30

`verify-preplancb-order` passed (`visit_a/b/c.queued>=1`, `preplancb_precalls` 0 -> 2,154, `preplancb_doors_restored` 0 -> 93, PLAN.md). Arm A walked to the door (Wogan on the home lot 30 s later). Arm B failed in place (Johnny still at Bridgeport Sports Zone with an empty queue). The repro had `sim_details` only after the first A and B visits, so neither arm C outcome nor the swap outcomes were read. Fix (repro only): every `sim_visit_lot` now has a `sim_details` step for the same Sim right after it, tallied `detail_a` / `detail_b` / `detail_c`, and `stepsRequire` adds `detail_c.in_world>=2`, so the job fails unless both arm C outcomes are recorded. The verdict stays a read of the result rows (`lot`, `queue`): for arm C, expect the arm A outcome (on the home lot or at the door, locked-door reaction queued), not the arm B outcome (off the lot, empty queue). Not run.

## 7. Corrections

- docs/11 §"C# alternatives": EA sets both delegates (`SimRoutingComponent`, `CarRoutingComponent`, `TombRoomManager`, `BunkBedContainer`, docs/93); mods such as GoHere and Overwatch add to them. Fixed in place.
- docs/18 "How it works" result row: `AboutToPlanCallback` runs after the job, not as without us (§2). Note added in place.
