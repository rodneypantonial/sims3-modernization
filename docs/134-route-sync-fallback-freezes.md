# 134. Route sync fallback freezes (NoStart, M1Pending, QueueFull, Los)

Written 2026-10-21. **Offline build only:** no game run, no queue started. ts3native 0.36.7 (counters v73), ts3fixes from the same commit. Question (Rodney, top priority, noticeable in play): with `[RouteAsync]` on (release v0.1.4), some route plans fall back to a synchronous plan on the sim thread and freeze the game for seconds. Ledger rows: docs/51 §187. Tags as in AGENTS.md.

## Summary

- **Data (freeze-data 2026-10-20, 7/7 PASS, `LogFallbackMs=20`):** every long fallback is `IC Plan` with `entry nojob`. Largest: SV big crowd 7,361 + 7,350 + 1,015 ms (all `NoStart`), BP route bits 4,060 ms (`NoStart`), SV route bits 1,313 ms (`NoStart`), IP water 278 ms (`QueueFull`), BP play 153 ms (`M1Pending`), 130 ms (`Los`) [verified, `probe-out/queue/freeze-data-2026-10-20-*-ts3native.log`]. The logged native caller (`40742227`, `40AE2227`, ...) differs per run and is the same for every line of a run: it is the Mono-generated wrapper of the `Route.Plan` internal call, not the managed caller [verified: one value per log, 7 values over 7 logs].
- **NoStart is not a refusal.** It means no START reached ts3native for that route: the ts3fixes hook decided the plan was not eligible and called the original `Route.Plan` directly (§1.1). Two different groups hide behind it: plans from a yielding task that is not the route's Sim ("foreign", fixable), and plans with no yielding task at all (cannot yield, only cappable).
- **Fix, behind `[RouteAsync] FallbackAsync=1` (default 0, runtime switch `fbMode`, `--ab fallbackasync`):** (a) foreign-task plans run on the worker while the calling task sleeps; (b) `M1Pending` applies the pending lot updates on the sim thread under one `+0x918` try and then starts the job; (c) the job queue is 32 deep instead of 8. Separate key **`SyncCapMs`** (default 0) caps the world A* of every sim-thread sync plan, whatever its reason, through the existing `[RouteBudget]` stubs (an ordinary no-path failure). Separate key **`LogFallbackStack=N`** (default 0) logs the script stack of every logged fallback plan.
- **Non-task callers stay synchronous** (§3): they cannot yield, a blocking wait on another thread does not unfreeze anything (the sim thread is blocked either way), and a deferred answer would break the `Plan()` contract. For them only the cap and the log apply.
- Selftest `TestFallback` + a CallPlan cap case in `TestBudget`: pass (`selftest ok`). A/B queue `tools/harness/queues/freeze-fix-ab.json` (14 jobs, the 7 freeze-data jobs off vs on, seed 12345), not run.
- **clean-night-0.2.0 finding 2026-10-21 (§10):** with `FallbackAsync` on, mean sim-thread route time drops but the worst hitch grows (SV 328 -> 1,027 ms). Cause [verified mechanism; foreign origin of the long jobs inferred]: a foreign plan now runs as a worker job that holds `+0x918` with **no cap** (SyncCapMs is per sim-thread plan), and the sim thread waits out the rest of that job at its next `+0x918` user (a destroy, `0x02000003`; steering in locomotion, `0x85FE10`). Every `FallbackAsync=0` run since 2026-10-20 kept jobs <= 374 ms; `FallbackAsync=1` runs had 509-4,638 ms jobs. Fix: **`[RouteAsync] FbJobCapMs`** (ts3native 0.36.16, default 0, `--ab fbjobcap`), the SyncCapMs cap for foreign jobs, plus per-kind longest-job counters and a `long job` log line.
- **Follow-up 2026-10-21 (§2.6, §7):** the first A/B (BP play, `FallbackAsync` on) left 4 of 6 sync plans as `M1Pending` (57-90 ms each): the single `+0x918` try failed 34 times against 24 applies (`route fallback: ... pending applied 24 busy 34`). New key **`[RouteAsync] M1Retry=1`** (default 0, counters v75, runtime switch `fbRetryMode`, `--ab m1retry`) parks such a START and retries once per READY tick (max 30), so the Sim's task sleeps instead of the sim thread planning. **Los** stays synchronous (verdict §7): about 1-3% of STARTs, one logged plan >= 20 ms per run (53-130 ms); design only. Queue `tools/harness/queues/freeze-fix-ab2.json` (4 jobs), not run.

## 1. When each reason fires (from the code)

Every `IC_Route_Plan` on the sim thread goes through `IcPlanHook` -> `IcCollect` (`native/ts3native/ts3native.cpp`). With no job for the route it plans synchronously (`SyncIc`, `entry nojob`) and takes the reason the last START or READY of **this route** recorded (`g_rfbHandle == handle`), else `NoStart` [verified, code].

### 1.1 NoStart (most lines, all the multi-second ones)

ts3fixes `RouteBackoffHook.PlanAsync` (`mods/ts3fixes/src/RouteBackoff.cs`) sends START only when `AsyncEligible`: the route's follower is a Sim, `Simulator.CurrentTask == sim.ObjectId`, live mode, not travelling or shutting down, `Simulator.CheckYieldingContext(false)`, switch `asyncroute` on [verified, code]. Otherwise it calls the original `Plan()` without START, and ts3native sees a plan nobody announced: `NoStart`. The same reason comes from `AsyncStart` itself when `asyncMode != 1` or the detours are not all in place (it clears `g_rfbHandle` and returns 0) [verified, code]; with the release ini that branch is not taken.

So `NoStart` = one of [inferred from the code; which one per line is what `LogFallbackStack` answers]:

| Group | Condition in `AsyncEligible` | Can the caller yield? | Examples among the 178 direct callers of `Route.Plan` (`game-copy/analysis/callgraph`, docs/78 index) [inferred from names and signatures] |
|---|---|---|---|
| **Foreign task** | `CurrentTask != sim.ObjectId` (the plan runs in another object's task) or the follower is not a Sim (`sim == null`) | yes, if `CheckYieldingContext` holds | `SocialInteraction.RouteSimsToSocial(actor, target, ...)` (the actor's task plans the target's route), `Sim+GroupChatB.RouteToGroupTalk`, `DanceFloor.RouteOntoADanceFloorWithFollower`, `ElevatorDoors.RouteToElevator(Sim, List<Sim>)`, `Terrain+GoHereWith.RouteActorToDestination`, `SimRoutingComponent.PlanRouteForPush`, `Lot.TryTeleportAndReplan`, vehicle and boat routes (`Vehicle.CreateGetInRoute`, `Boat.RouteBoatNearHouseboat`), situation and one-shot tasks |
| **No yielding task** | `CheckYieldingContext(false)` false | no | plans from event handlers, alarms run inline, callbacks inside a native->managed call |
| Not live | `IsLiveState` false, travel, shutdown | - | edit modes, travel |

Big crowds (many Sims socialising and grouping) and the forced car/UFO/subway plans of `rare-route-bits` are exactly the foreign-task shapes, which fits the data, but the share is unmeasured. The 7 s plans are long searches on the sim thread (a sync plan waits for at most the one running job first; the max job in that run was 237 ms) [inferred: `route async ... max 236642 us` line in the same log].

### 1.2 M1Pending

`AsyncStart` -> `RtM1Allows` (`rtsites.cpp`): world state 3 = the routing world's pending lot update flag `[[world+0x95C]+0x10E]` is set (set by `0x6236D0`, `PathPlanner_Init 0x62E860`, `LotGraph_Build 0x63CB90`; docs/60 §1.4 #5) [verified, docs/60]. The plan would apply them first (`Route_PlanLocked 0x82C6E0` takes `+0x918` with the call at `0x82C733`, then calls `RoutingWorld_ApplyPendingUpdates 0x82C420` at `0x82C73A`) [verified, bytes], and H6 M1 does not let the worker do that, so START is refused and the plan runs on the sim thread, which applies them and searches. Thunderstorm and lot streaming runs show it most (BP thunderstorm 6 of 9 lines).

### 1.3 QueueFull

`AsyncStart` found no free job slot among `kJobs = 8`: every slot QUEUED, RUNNING, or DONE/SKIPPED less than `kDoneKeepMs` (5 s) ago and not yet collected [verified, code]. One worker runs jobs one after another, so a burst of more than 8 plans (big crowd, IP water) fills it.

### 1.4 Los

`AsyncLosRoute`: the route has `ForcePhysicsBasedRaycast` (option bit `0x2000000`); its plan raises the lot display level from inside the search and would race the sim's level writers on the worker (docs/94 §17.9, I-105-3). Stays synchronous by design; only `SyncCapMs` touches it. Rate and options: §7.

## 2. The fix

### 2.1 Foreign-task plans (FallbackAsync, part a)

- **ts3fixes:** when `AsyncEligible` fails, `ForeignEligible()` (switch `asyncroute`, live, not travelling or shutting down, `CheckYieldingContext(false)`) sends a **foreign START**: `GetOption2` with bits 58 + 59 and kind 0 (`0x0C00000000000000`), the A10 channel's unused kind-0 slot. ts3native before 0.36.7 answers 0 there (`!idx` branch), and the game answers false for an option bit it has no name for, so with an older ts3native the plan stays synchronous as before [verified, code of 0.36.6 `GetOptionHook`].
- **ts3native:** `GetOptionHook` counts it (`fbForeign`); with `fbMode 0` it refuses it with the new reason **`Foreign`** (so the log separates foreign plans from true `NoStart` even with the fix off); with `fbMode 1` it calls `AsyncStart(handle, 0, NULL, foreign=true)`: every check of a normal START (NoYield via `CanYield`, M1, Los, queue), plus **route not being followed** (`Route_IsActive 0x74CFC0`, as A10; refusal `Active`). The job carries `fb = 1`; at collect, a foreign job whose route became active meanwhile is a tripwire (`fbActiveTrip`, logged once).
- The calling task then sleeps tick by tick exactly as an own-task async plan (`AsyncWait`, READY, CANCEL in `finally`), and the original `Plan()` takes the worker's result.

### 2.2 M1Pending (part b)

With `fbMode 1`, a START refused for world state 3 calls `FbApplyPending`: one try of `+0x918` through the game's `Lock_Acquire 0x4E16F0` (deadline 0, so our `LockAcquireHook` and the gate's owner tracking see it; no pump, no wait), `RoutingWorld_ApplyPendingUpdates(world)` (thiscall, no stack args, plain `ret` at `0x82C48E`) [verified, bytes], `Lock_Release 0x4E17B0`, then `RtM1Allows` again. A busy lock (the worker holds it) counts `fbPendBusy` and refuses as before. The sim thread pays the update work it would have paid inside the sync plan, and not the search. `asyncRefusedM1[3]` still counts the first refusal.

### 2.3 QueueFull (part c)

`kJobs` is now 32; `AsyncStart` hands out the first 8 slots with `fbMode 0` (today's behaviour, the old selftest's QueueFull case still holds) and all 32 with `fbMode 1` (`fbDeepQueued` counts STARTs past slot 8). A deeper queue makes the sleeping tasks wait longer for the single worker, but they sleep; the sim thread does not. P5c's START-cut ring (`g_p5cCut`) grew from 16 to 64 entries so a 32-deep queue cannot alias two live sequence numbers (an alias would only refuse a clone answer as stale).

### 2.4 SyncCapMs (every sim-thread sync plan)

`SyncIc` (all sync plans that pass `IcCollect`, every reason and entry, kinds 0-3) sets a thread-local cap `t_fbCapUs` for the duration of the IC call while `asyncMode == 1`; `CallPlan` then runs the plan under a budget with that time cap even when `[RouteBudget]` Mode is 0 (the smaller of the two when both are on). Over the cap the world A* sees every further edge as blocked and leaves through its own "open list empty" exit: an ordinary `FailedBecauseNoPathToDestination` [verified, docs/15 phase 4 and `TestBudget`]. The cost-function stubs are patched for this even with `[RouteBudget] Enabled=0` (`budgetMode` stays 0, so no other plan is capped). Worker plans never carry the cap (the variable is per thread). Limits: lot/portal searches `0x63E340` are not capped, and time spent outside the world A* (start points, managed callbacks) is not either; the log line says `capped (SyncCapMs)` when the cap ended a plan, so the A/B shows how much of a long plan was A*. The cap changes the result of a long plan (failure instead of a path), so it is its own key, off by default. One nested sync plan inside another keeps one hit flag (shortcut, commented in code).

### 2.5 Script stack (LogFallbackStack=N)

After each `sync plan` log line (same rate limit, 20 lines/min), one more line: `route async: script stack of route X (innermost first): Ns.Class.Method < ...`, up to N (max 16) interpreter frames of the sim thread. The walk is autledger.cpp's `AsLgFrames` (TEB TLS slot `[0x11F4160]` -> context `+8` frame, `frame[0]` parent, `frame[1]` info, info `+8` `MonoMethod*`; verified 2026-10-12 in docs/04 F6a); names as `tools/probe/agent.js` (`MonoMethod` klass `+8`, name `+0x18`; `MonoClass` name `+0x34`, namespace `+0x38`). It runs while the sim thread is still inside the route IC, so the frames are the plan's caller. Every read is under SEH; an unreadable method prints `?`. Cost: only on logged plans (>= `LogFallbackMs`).

### 2.6 M1Retry (follow-up, `[RouteAsync] M1Retry=1`, needs `FallbackAsync=1`)

**Data** [verified, `probe-out/queue/freeze-fix-ab-2026-10-20-bp-ffx-play-on-2318-ts3native.log`]: with `FallbackAsync=1` the BP play job kept 6 sync plans >= 20 ms: 4 `M1Pending` (57-90 ms, sum 269), 1 `Los` (59), 1 `NoStart` (483). The counter line says `pending applied 24 busy 34`: the one try of §2.2 failed more often than it worked, because the worker holds `+0x918` for each job and the queue is busy in play. Every `M1Pending` script stack starts with `RouteBackoffHook.PlanAsync` (own-task START), so the callers are tasks that may yield [verified, same log, `LogFallbackStack=8`].

**What it does** (`AsyncStart`, `FbRetry`, `JobQueue` in `ts3native.cpp`):
- At START, a world-state-3 refusal whose `FbApplyPending` try failed (lock busy) is **parked** instead of refused when `fbMode 1` and `fbRetryMode 1`: START answers yes, the route takes a job slot in the new state `J_PEND` (sim thread only; the worker, `AsyncBusy`, the lock pump, P4 and P4clone only look at `J_QUEUED`/`J_RUNNING`, so a parked slot is no job); `fbRetryParked` counts it. Every other refusal (`NoYield`, `Los`, other M1 reasons, `Active`, `Running`, `Shadow`, `QueueFull`) is checked before or at the same point and stays as it was.
- The task then sleeps in `AsyncWait` exactly as for a started plan (ts3fixes unchanged). Each READY of a parked route calls `FbRetry` once, after READY's own `DiscPoll`: switched off meanwhile or `Los` -> give up; `RtM1Allows` passes (someone else applied the updates) -> queue; state 3 with D records still queued, or a busy try -> wait another tick (up to `kFbRetryTicks = 30`, then give up); try succeeded -> `RtM1Allows` again -> queue; `Active` (kinds 1-3, foreign) -> give up. Queue = the old START tail, now `JobQueue` (invoke hook, slot fields, `P4OnStart` cut, `J_QUEUED`); READY answers "not yet" and the next ticks behave as for any job (`fbRetryQueued`).
- Give up: slot freed, the reason recorded with `RfbRefuse`, READY answers "done" with nothing armed, the task's `Plan()` runs synchronously and is logged with that reason (`M1Pending` for the tick cap); `fbRetryGaveUp`. A `Plan()` while still parked (C# poll cap, 1000 polls) frees the slot and plans with reason `M1Pending`, entry `pending`. CANCEL frees a parked slot (`DropJob`). A parked slot whose task vanished without CANCEL is reclaimed after `kDoneKeepMs` like an uncollected result (`doneTick` is refreshed on every retry).
- **Non-task callers:** never START (ts3fixes sends START only from a yielding task; `CanYield` is checked before M1), so they keep today's synchronous plan.

**Pending lot updates applied exactly once and in order** [verified in code and selftest; live order inferred]:
1. Only two places apply them: `FbApplyPending` (sim thread, after a successful `+0x918` try, only while `RtWorldState` says 3, i.e. the pending flag `[[world+0x95C]+0x10E]` is set) and the game's own `Route_PlanLocked`. A failed try (deadline 0, `-2`) applies nothing. The apply consumes the flag (`RoutingWorld_ApplyPendingUpdates`, docs/60 §1.4 #5); the next check sees state 0, so the same set is never applied twice (selftest: `fbPendApplied` stays at 2 through the collect and the give-up cases).
2. Whoever comes first applies: another sim-thread sync plan between the ticks (`Route_PlanLocked`), or `FbRetry`. In the first case `FbRetry` finds state 0 and queues without applying.
3. **Order with design D's queue** (deferred discourage footprints, `discourage.cpp`; design U's deferred `UpdateSimFootprint` uses the same queue): in a sync plan, START's `DiscPoll` drains D first, then `Route_PlanLocked` applies the pending updates. `FbRetry` keeps that order: it runs after READY's `DiscPoll` and **waits a tick while D still holds records** (`DiscPending()`), which closes the gap where `DiscPoll` skipped the drain because a job was running and the job ended before the try. So D's records never land after the pending updates. The original `FbApplyPending` at START (§2.2) has no such check; there the gap is a few instructions wide (job ends between `DiscPoll` and the try) [inferred, not observed]; left as is.
4. The job's own `+0x918` comes later (worker); if the flag is set again in between, the worker skips the job (`RtQuietWorld`, `J_SKIPPED`, then a sync plan with reason `Skipped`), as for every job.

**Cost:** the parked Sim waits up to 30 more ticks (in practice the running job's length) instead of the sim thread freezing 57-90 ms. Each failed retry counts in `fbPendBusy` and `asyncRefusedM1[3]` again (they count tries, not STARTs); `fbRetryParked`/`fbRetryQueued`/`fbRetryGaveUp`/`fbRetryTicksMax` count STARTs. Not done: holding the worker back (`g_rgOffSimOpen`) so the next try is sure to win; it would idle the worker for a tick per parked START; only if the A/B shows many give-ups.

## 3. Safety for callers that are not the route's Sim

- **Foreign task callers** (a task that may yield, not the follower's): the yield happens inside `Route.Plan`, a point where EA code already yields routinely (the same plan on the own-task path since 0.5.0; every `DoRoute` after it). What changes for the caller is the same as for own-task async plans since 0.5.0: other tasks run during the plan (a few ticks), so a caller that checked a condition before `Plan()` and relies on it after may see it changed. That risk class was accepted for own-task plans after their A/Bs [community/own test history, docs/18]; foreign plans widen the set of callers, not the kind of risk [inferred].
- **The follower is not idle** (docs/18 §Async plan relied on that for own-task plans): it keeps simulating while its route is planned. Guards: a route being followed is never started (`Active`); a route that becomes followed before the collect is a tripwire (`fbActiveTrip`, expected 0); the follower's own task planning the same route meanwhile meets the existing "plain plan of a route with a job" path (waits for the job, drops it, plans itself; selftest since 0.11.3); a destroy during a running job waits for the job (docs/88, RT_DESTROY), a queued job of a vanished follower plans with a dead follower id (the plan's own `FindById` fails) [inferred, not tested in game]. The start point is the follower's position when the worker starts, a few ticks after the call; a sync plan from another task already hands the route over later than it was planned, so this staleness is of the same kind [inferred].
- **Callers with no yielding task** (event handlers, nested native->managed calls, `CheckYieldingContext` false): **no change** except the cap and the log. They cannot be suspended: a yield there aborts the game (interp.c:3595, docs/18 "Crash: yield in a nested call"); waiting on the sim thread for a worker plan blocks the sim thread exactly as long as the sync plan would (no gain, plus a thread hop); a deferred answer ("pending" result now, the real one at the next call) is not a `Plan()` result any caller expects: they would take their failure branch (route-fail behaviour, fail-path plans, backoff entries). `SyncCapMs` bounds them with a result they already handle (no path).
- **Los** routes stay on the sim thread by design (I-105-3); only the cap applies.

## 4. Selftests (native/ts3native/selftest.cpp, `selftest ok`)

| Test | Checks |
|---|---|
| `TestFallback` 1 | foreign START, `fbMode 0`: refused `Foreign`, sync plan counted under it (`rfbN[2]`, `rfbMax`); `fbMode 1`: started (`fb = 1`), READY, the plan takes the worker's result; route being followed: `Active`, sync; nested native->managed call: `NoYield`; tripwire when the route becomes active before the collect |
| `TestFallback` 2 | `M1Pending`: `fbMode 0` refuses, nothing applied; `fbMode 1` applies on this thread while holding `+0x918` (fake world, real `CRITICAL_SECTION`), releases it (the worker then takes it for the job), job collected; busy lock: one try with deadline 0, nothing applied, refused as before |
| `TestFallback` 2b | `M1Retry`: busy try parks (`J_PEND`, START yes, no job, `AsyncBusy` false); READY with the lock busy = wait; D records queued = wait with the lock free; then applied once under `+0x918` on this thread, queued, the worker's result collected; tick cap gives up (reason `M1Pending`, sync, nothing applied); Plan while parked = sync, slot freed; CANCEL frees; `M1Retry=0` refuses at START as before |
| `TestFallback` 3 | 8 fresh uncollected results: `fbMode 0` QueueFull; `fbMode 1` starts in slot >= 8 (`fbDeepQueued`), collected |
| `TestFallback` 4 | `SyncCapMs`: the sim sync plan runs with the cap set and cleared after, a capped one counts `fbCapped`; a worker plan has no cap; cap 0 = none |
| `TestFallback` 5 | stack text from a fake interpreter context: `ScriptCore.Route.Plan < Sims3.Gameplay.Actors.Sim+GroupChatB.RouteToGroupTalk < ?` (bad `MonoMethod` = `?`), frame limit, `RfbNote` writes the stack line only with `LogFallbackStack` set |
| `TestBudget` | `t_fbCapUs` caps an endless search with `budgetMode 0` (`budgetCapped`, `t_fbCapHit`), no cap = untouched |

## 5. A/B queue (not run)

`tools/harness/queues/freeze-fix-ab.json`: the 7 freeze-data jobs, each off then on, same save, seed 12345 and repro. Both arms `LogFallbackMs=20`, `LogFallbackStack=8`. ON = `FallbackAsync=1` + `SyncCapMs=1000`; OFF = both 0. Before the run: build 0.36.7 (`native/ts3native/build.cmd`), copy `native/ts3native/out/ts3native.asi` to `game-copy/ts3native.asi.0367`, build and install ts3fixes from the same commit (`node tools/setup/build-fixes.ts`, install), since the foreign START is ts3fixes code. `minCounters 73` refuses an older asi. 14 jobs, ~3.5-4 h of game time; noisy-machine rule: correctness only unless run clean.

Read per job: `sync plan` lines (count, max and sum of ms, reasons, `capped`), the script stack lines (which managed callers are `Foreign` vs `NoStart`), the `route fallback:` counter line, `fbActiveTrip` (must be 0), watchdogs (0), crashes (0). Decision rules: ON keeps the max sync plan <= ~1.1 s in every job and cuts the sum; the cap's share is the `capped` lines (if the multi-second plans are mostly `NoStart` + `capped`, the cap is what fixes them and the stack lines say which callers to look at next); `Foreign` lines in OFF that disappear in ON measure part (a).

## 5a. A/B queue 2 (M1Retry, not run)

`tools/harness/queues/freeze-fix-ab2.json`: BP play (12 min) off then on, BP big crowd (8 min) on then off (swapped start), seed 12345. Both arms `FallbackAsync=1`, `SyncCapMs=0`, `LogFallbackMs=20`, `LogFallbackStack=8`; only `M1Retry` differs. asi: build this branch, copy `native/ts3native/out/ts3native.asi` to `game-copy/ts3native.asi.m1retry` (`minCounters 76`; `TS3NATIVE_VERSION` unchanged). ts3fixes unchanged. Read: `M1Pending` sync lines off vs on (on should drop the busy ones; give-ups show as `M1Pending` lines and in `fbRetryGaveUp`), the `route fallback retry:` line, `fbActiveTrip` 0, watchdogs 0, crashes 0. 4 jobs, ~1 h of game time.

## 5b. Replan-family callers that stay sync (docs/141 §6, 2026-10-21)

`FallbackAsync` only moves `Plan` calls; `Replan`/`ReplanFromPoint` from a foreign or task-less context and every route being followed (`Active`) stay sync. Reading the IL (docs/141 §6.1): for a Sim, `FollowRouteAction.ForceReplan` already hands the replan to the Sim's own route loop (`ReplanRouteInterruptAction`, own task, after the follow stopped, so A10 can take it); the sync ones left are the door/elevator action `PortalRouteThroughAction.ForceReplan` (a `ReplanFromPoint` of the followed route inside the callback: `Active`, sync), strollers, and vehicle/boat followers. Two ts3fixes switches, default off (`ReplanDefer.cs`):

- `replandefer`: the door/elevator replan of a followed route goes through the game's own interrupt action instead (the Sim stops and reroutes a tick later, from its own task). Routes not followed yet (elevator approach inside `DoPortal`) keep the sync replan: the caller follows the replanned route right after.
- `replanspread`: `OnBuildBuyExit` / `OnRoutingDataChanged` (build/buy exit, edit town, play flow: every Sim in one loop) are replayed 4 routing Sims per tick, so the interrupt replans do not all START in one tick (`QueueFull` beyond the 8-deep queue).

A/B queue `tools/harness/queues/replan-defer-ab.json` (repro `replan-buildbuy-crowd`, `LogFallbackStack=16`, `FallbackAsync=0`, `SyncCapMs=0`), not run. Read: `IC Replan` sync lines right after each exit (reasons `QueueFull`, `Active`), the `replandefer_*` / `replanspread_*` counters.

## 6. Effort

Built in ~3 h (agent, offline). Game runs: the A/B queue (14 jobs). If it passes: a second clean run before turning `FallbackAsync` on by default (crash rule: 2 clean runs); `SyncCapMs` needs a separate decision (it changes route results).
**SyncCapMs decision (2026-10-21, Claude, delegated by Rodney): `SyncCapMs=1000` in the v0.2.0 release ini, together with `FallbackAsync=1`.** Data: freeze-fix-ab (7 pairs, `SyncCapMs=1000` on the ON arm) never hit the cap (`fbCapped` 0 in every job); with FallbackAsync on, the longest remaining sim-thread sync plan was 483 ms (BP play, NoStart), every other job <= 196 ms, so 1000 ms is ~2x the longest legitimate plan seen and leaves normal routes unchanged. Without the fix the worst freezes were 2,099-7,361 ms (route-bits, SV crowd): the cap bounds those rare cases to ~1 s, at the price of that one plan failing (the Sim retries or routes elsewhere, like any failed route). Lower values (250-500) would start failing real plans in crowds and big lots for little gain, since FallbackAsync already removed the frequent long ones. Check in release validation: `fbCapped` per job and route-fail counts vs v0.1.4.

M1Retry follow-up (§2.6): built in ~1 h (agent, offline). Game runs: queue 2 (4 jobs).

## 7. Los routes (ForcePhysicsBasedRaycast): study, design only

**Why I-105-3 keeps them on the sim thread** [verified, docs/105 §1 and §9, docs/94 §17.9]: goal building of a radial-range destination calls `World_CanSeeObject 0x789DD0` for each candidate goal when the route option `ForcePhysicsBasedRaycast` (`0x2000000`) is set. When the target is above the lot's display level it **raises** the display level (`lot+0x39C`, plain store) and rewrites bit `0x10000` of every level group word (`PhysShape_SetLevelEnableBit 0x681F20`, non-atomic `or`/`and`) before the rays, and **restores** both after, with a camera call on the restore. Floor buttons, picks, camera, scene updates and script ICs read those without `+0x918`. On the worker that is a lost-update race (a player floor change inside the job window is overwritten by the worker's quiet restore), not a crash. The value probe of 2026-10-18 saw 400 such worker calls before the refusal existed.

**How often** [verified, `probe-out/queue/freeze-data-2026-10-20-*` and `freeze-fix-ab-2026-10-20-*` logs and `-native.json`]: `arLosRefused` 228 of 16,800 STARTs in BP play (12 min), 163 / 10,253 BP big crowd, 150 / 4,357 SV big crowd, 30-81 in the 6-min repros: about 1-3% of STARTs. Only **one** Los plan per run reaches the 20 ms log line (53, 54, 59, 130 ms); the rest are short. The logged script stacks are `ViewObjects.RouteToObject < ViewObjects.CreateRouteToObject < RouteBackoffHook.Plan` (a view-object interaction: route to a spot that can see the object), own task. So Los is a small freeze source: about one 50-130 ms plan per 10 minutes of play.

| Option | What | Verdict |
|---|---|---|
| A. Refuse only when a raise will happen | START reads the destination's target (`+0x50/+0x54`), its lot and level, and the lot's `+0x39C`; plan async when target level <= display level (the test is read-only then) | **Not safe alone**: the player can lower the floor during the job, then the worker raises after all; needs B's guard. Since lots start at level 0 (docs/107 §5.2), most upper-floor targets raise anyway. |
| B. Thread-local display level on the worker | Detour `Lot_SetDisplayLevel 0xAC0570` on the worker inside `World_CanSeeObject` (record `{lot, level}` per thread, write nothing; no camera call) and the ray leaf visitor `0x680F80` (with an override, compute the level bit of the shape's group from the lot's level table instead of reading word `+0x2C`) | Correct (no shared write). Needs the group-to-level map per lot (deque `lot+0x378`, docs/105 §9.1 emulator spec) and a detour on a hot physics path. ~6-10 h build + selftests, 2 live jobs. |
| C. Snapshot the visibility answers | Sim thread builds the goals before START, the worker searches with them | Splits `Route_PlanCommon` (goal generation and search are one IC call); A10-scale change. Not worth it at this rate. |
| D. Keep sync, cap | Today; `SyncCapMs` bounds them | **Chosen.** |

**Verdict:** Los stays synchronous (I-105-3 unchanged, nothing trivially safe). The raycast part can only leave the sim thread with option B; at ~1 logged plan of <= 130 ms per 10 min it does not pay before the multi-second `NoStart`/`Foreign` plans and `M1Pending` are gone. Revisit when the freeze logs show Los as the largest remaining reason.
## 8. The 3 AM stuck check in our own task (`stuckcheckasync`, added 2026-10-21)

**Offline build only:** no game run. Ledger rows: docs/51 §199.

### 8.1 Data

freeze-fix-ab 2026-10-20 (`probe-out/queue/freeze-fix-ab-2026-10-20-*-ts3native.log`, 3 jobs had run): the biggest sync plan of the ON arm of BP play was **483 ms, reason `NoStart`**, script stack `... Route.PlanToPoint < LotManager.RunStuckCheckHelper.RunOrig` (the game's `RunStuckCheck` body behind our `stuckcheckbusy` hook). In the OFF arm the same site logged 286, 54 and 50 ms plans with reason **`Foreign`** [verified, logs: each `script stack` line follows its `sync plan` line].

### 8.2 Calling context [verified, decompiled + call graph]

- `RunStuckCheckHelper.Init` (called for every actor by the 3 AM venue cleanup alarm `LotManager.CleanUpLotsAndInventories`, `kVenueCleanupTime = 3`, and by the debug interaction) arms one `AlarmManager.Global` alarm per Sim, 20-60 sim-minutes later, `AlarmType.NeverPersisted`, not `YieldRequired`.
- `AlarmManager.Global` is the world lot's alarm manager (`sWorldAlarmManager`); its only managed caller is `Lot.Simulate` (`managed-callgraph.ts callers`), the lot's task loop. A plain alarm callback runs inline in `AlarmManager.Simulate` (`YieldRequired` alarms get their own `AlarmOneShot` task instead).
- So the check runs **inside the world lot's task**: `CheckYieldingContext` holds, the follower is not the current task, and ts3fixes sends the foreign START. That is why OFF logged `Foreign`. With `FallbackAsync=1` the plan goes to the worker and **the world lot task sleeps inside the alarm** (up to 10 plans per Sim, each a few ticks): every later world alarm and the world lot's own updates (door privacy, detail priority, reactions) wait for it [inferred from the code; EA only yields inside alarms that ask for it].
- The 483 ms `NoStart` line does not fit that context: the same call site sent the foreign START in OFF, and ON started 99.9% of foreign STARTs (`route fallback: ... foreign 5920 started 5913`). `NoStart` there means ts3fixes did not send any START (`ForeignEligible` false: not live, travelling, world shutting down, `YieldingDisabled`) or `AsyncStart` returned before a reason (`asyncMode != 1` or not ready). Which one is open (I-199-3); the log line has no game-state field.

### 8.3 The switch

`stuckcheckasync` (tuning `kStuckCheckAsync`, default off; `mods/ts3fixes/src/FewerPlans.cs`):

- The hooked `RunStuckCheck` (alarm context) first applies `stuckcheckbusy` as before, then, when the switch is on and the game is live (not travelling, not shutting down), queues `(helper, Sim)` and returns. The alarm ends at once.
- One `OneShotFunctionTask` drains the queue (`SliceQueue`, FIFO = alarm order): for each entry it calls the game's own body (`RunOrig` on the same helper object, so the same stored position, attempt count, reset after attempt 3 or re-arm), then `Simulator.Sleep(0)` before the next: **one Sim per tick**. The plans inside come from our task, not the Sim's: foreign START, so `FallbackAsync=1` runs them on the worker and only our task sleeps. With `FallbackAsync=0` they stay synchronous, but one Sim per tick instead of every alarm due that tick.
- Skipped (counted as dropped): the Sim was destroyed, the world is shutting down or travelling, or the body threw (the alarm's caller swallows exceptions too). A task killed by a world unload leaves entries behind; the next queue start drops them.
- What can change: the check runs one or more ticks after its alarm instead of in it. The body reads `sim.LotCurrent`, `Household.ActiveHouseholdLot`, `sim.Position == mLocation` and the rabbit-hole posture at run time; a Sim that starts moving in those ticks is skipped where the alarm would have planned it (the reverse cannot happen). Re-armed alarms and resets happen correspondingly later; `RandomUtil` draws keep their order among stuck checks but interleave differently with other draws. Same 3 AM window, same decisions up to that delay [inferred; the A/B compares resets].
- Counters (`patches`): [127] `stuckcheckasync_queued`, [128] `_run`, [129] `_dropped`, [130] `_maxdepth`, [131] `_maxwait_simsec`.
- Selftest `node tools/selftest/slicequeue/run.ts`: FIFO, depth and wait gauges, a clock that went back, `Clear`, random model (`slicequeue selftest ok`). The task and hook are game code and only testable in game.

### 8.4 The other managed callers in the logs

From the same three logs (stack depth 8, so the frames end at the `Route` helper) and `managed-callgraph.ts callers` [verified for counts; context from names, inferred]:

| Caller (logged) | Reasons logged | Callers in the script DLLs | Calling context | Move into a task? |
|---|---|---|---|---|
| `Route.PlanToSlot` | `M1Pending` (57-97 ms, OFF and ON), `Skipped` (56 ms) | 162 sites: interaction `Run` bodies (jigs, social jigs, bar brawl, sprinklers, wardrobes...), `RoutingComponent` / `SimRoutingComponent`, `RabbitHole`, `PoolPortalObject` | Sim's own interaction task or another Sim's (foreign): a START was sent, the reason proves it | **No gain.** They already run in a yielding task; the refusal is the world state (`M1Pending`: pending lot updates and the `+0x918` try was busy, `fbPendBusy`) or a skipped job. A retry START a tick later (caller sleeps, then asks again) would fit them; not built (needs a reason channel back to ts3fixes). |
| `Route.PlanToPointRadialRange` | `M1Pending` (71-90 ms) | 184 sites: `Lot`, `SimQueue`, `RoutingComponent`, `Boat`, interaction `Run` bodies | same | **No gain**, same as above. |
| `ViewObjects.RouteToObject` / `CreateRouteToObject` | `Los` (53-59 ms) | 10 + 4 sites: view, photograph, critique art, horoscope, ant farm, interior designer | interaction tasks | **No.** Line-of-sight routes stay on the sim thread by design (§1.4, I-105-3); only `SyncCapMs` applies. |
| `LiteDriver.Simulate` | `Foreign` (OFF, 52-79 ms) | car driver task | foreign task | Already handled by `FallbackAsync=1` (no such line in ON). |
| `Ts3Agent.Handlers.SimRouteTo` | `Foreign` | our test agent | agent call | Test harness only. |

None of the logged callers is task-less, so nothing besides the stuck check has a context problem to fix; the remaining sync plans are refusals (`M1Pending`, `Los`). Behavioural risk of a retry design for `M1Pending`: the caller's plan result arrives a tick or two later (same class as every async plan since 0.5.0).

### 8.5 Test (not run)

Repro `stuck-check-3am` (`tools/harness/repros.json`): `test_env {hour: 4}` jumps the clock across 3:00 (`SimClock.AddOneHour`; due alarms then fire, [inferred]), so the game's own cleanup alarm arms the stuck checks; waits 240 s for them, then reads `patches`. No agent action forces a check directly; one would construct `RunStuckCheckHelper` through reflection with `loc = sim.Position` and call `Execute` (not built: the clock jump runs the real path). Queue `tools/harness/queues/stuckcheck-async.json`: `bp-sca-off` / `bp-sca-on`, both `FallbackAsync=1`, `LogFallbackStack=16` (deep enough to show the alarm or our task below the route funnel), ON with `profile.fixesOn: stuckcheckasync`; asi 0.36.7 (`game-copy/ts3native.asi.0368`), ts3fixes and ts3agent (switch list, counters) built from this commit. Pass: no crash, `fbActiveTrip` 0, watchdogs 0, `stuckcheckasync_run` > 0. Read: stuck-check sync-plan lines (fewer, shorter in ON; stacks end in `StuckCheckDrain`), `_maxwait_simsec` (a few sim-minutes at most), Sims reset by the check vs OFF. Effort: built in ~1.5 h (agent, offline); the queue is 2 jobs (~25 min of game time).

## 9. Candidate-loop callers (docs/141 §7, built 2026-10-21, offline, not run)

The planner callers from docs/141 §4 that plan many times per call, fixed at the caller in ts3fixes `CandidateLoops.cs` (switches default off, counters 137-145, ledger docs/51 §205). They cut the number of plans; none changes the context a plan runs in.

| docs/141 # | caller | context of the plans | `FallbackAsync=1` alone | switch | change |
|---|---|---|---|---|---|
| 1 | `Sim.FindRoutablePointInsideNearFrontDoor` (resets, send-home) | c mostly (reset, event, UI, dispose, load), some a/b | no help for c (sync, only `SyncCapMs`) | `safespotcap` | at most 6 routability plans per front-door search (EA's own `IsPointSafelyRoutable` limit 5); same spot when the first safe cell is among the first 6 |
| 3 | `LotManager.GetFarthestLot(lot, actor, true)` | a/b: only `AlienSituation.SendAlienHome` and `TravelUtil` early departure plan | already async (own or foreign task) | `farthestsorted` | farthest first, same lot, plans = 1 + unroutable lots beyond the answer, cap 3, sim-day cache |
| 5 | `Lot.GetRoutableIndoorPositions` | a (fire job setup) / b (`NpcParty.BringNpcGuests` one-shot) | already async (own or foreign task) | `indoorcap` | a room given up after 4 failed plans |

- **Why no task move for #3 and #5:** their routing callers already run in yielding tasks (docs/141 §7.2, §6.3); the c chains docs/141 listed for them are the no-route overload (#3) and a class-hierarchy artefact (#5). For #1 a task move is not possible: every caller needs the spot back in the same call (reset puts the Sim there next).
- **What the A/B reads** (`tools/harness/queues/candidate-loop-ab.json`, BP and SV x off / on / on+`routablecache`, 10 min, seed 12345, repro `candidate-loops`, ts3native 0.36.7, `FallbackAsync=1`, `SyncCapMs=0`, `LogFallbackStack=16`): the probe lines (the farthest lot id must be equal across arms), counters `safespotcap_*`, `farthest_*`, `indoorcap_*`, `routablecache_*`, sync plan lines whose stack shows one of the four methods (count, max ms), stuck Sims vs OFF. 6 jobs, ~1.5-2 h of game time; noisy-machine rule applies (correctness unless run clean).

## 10. Worst hitch grows with FallbackAsync on: long foreign jobs (clean-night-0.2.0, 2026-10-21)

**Static analysis and log reading only; fix built offline (ts3native 0.36.16), not run.** Ledger rows: docs/51 §214. Data: `probe-out/profile/clean-night-0.2.0-2026-10-21-{bp-fb-play-onfirst,sv-fb-play-offfirst,sv-fb-play-onfirst}-*` (hitches, seconds, ab.json, ab-native.jsonl) and `probe-out/queue/clean-night-0.2.0-2026-10-21-*-ts3native.log`; ts3native 0.36.14, `--ab fallbackasync,m1retry`, `SyncCapMs=1000` in both halves, `[HitchAttr]` on. Hitches mapped to slices by the seconds rows (`t`), slice 0 (warm-up) left out.

### 10.1 What the fields mean [verified, code]

`[HitchAttr]` (`hitchattr.cpp`) notes only waits of the job owner (the sim thread) **for the route worker**: `rtLock` = a `+0x918` acquire while a job runs or is queued (`LockAcquireAsync`, keyed by the caller), `rtOtherLock` = another game lock in the same pump, `rtJob` = `RtWaitJob` (a destroy `0x02000003`, drains `0x020000xx`) and the discourage drain, `rtPlanWait` = a sync plan that waited for the job first. So `rtJob 1013` is **not** an inline plan and not the M1 apply: the sim thread sat in `RtWaitJob` -> `WaitAsyncDone` (serving the job's managed calls) until the running job ended. Key `0x02000003` = `RT_DESTROY` (`ObjectService_DestroyById 0x939420`, `rtsites.cpp`; `[Discourage] DestroyPark` is off in the release ini, so the destroy waits).

### 10.2 Every hitch >= 250 ms (counted slices)

| job | side | n | of which route waits (rt >= half the frame) | worst | worst's fields |
|---|---|---|---|---|---|
| BP play on-first | off | 4 | 2 (174 ms `rtLock` at `0x631814`; 134 ms at `0x75A64C`) | 371 | `taskObjects` 345, no route wait |
| BP play on-first | on | 1 | 0 | 500 | `taskObjects` 461, no route wait (script, not routing) |
| SV play off-first | off | 3 | 2 (294 ms at `0x82D296` + 24 ms `rtJob`; 296 ms at `0x75A64C`) | 328 | `rtLock` 294, job max that slice 295 ms |
| SV play off-first | on | 6 | 4 (1,013 `rtJob` `0x02000003`; 335 at `0x82A0F6`; 259 at `0x8334CC`; 258 at `0x82A0F6`) | 1,027 | `threaded` 1,021, `rtJob` 1,013, 1 wait |
| SV play on-first | on | 6 | 3 (514 at `0x85FE10` in locomotion; 233; 243 at `0x833593`) | 541 | `locomotion` 515, `rtLock` 514 |
| SV play on-first | off | 2 | 0 | 272 | `taskObjects` 166 + `threaded` 102 |

The BP change of the worst iteration (371 -> 500) is script time (`taskObjects`), not routing [verified, fields]. Both SV regressions are one wait for one worker job: the 1,013 ms destroy wait (slice 2, on) fell in the slice whose `asyncMaxUs` went 294,977 -> **1,016,822 us**; the 514 ms steering wait (slice 1, on) in the slice whose `asyncMaxUs` went 171,575 -> **521,598 us** [verified, `-ab-native.jsonl` per slice]. In both the wait started within 3-7 ms of the job's start, so the hitch is the job's length. In OFF slices the same mechanism caps at the longest own-task job (295 ms) [verified].

### 10.3 Why it is worse with the fix on

- **Jobs carry no cap** [verified, code]: `SyncCapMs` sets the thread-local `t_fbCapUs` only in `SyncIc` (sim thread); `ServeAsync` ran the job with `t_fbCapUs = 0`, and `budgetCapped` stayed 0 in every log. Before `FallbackAsync`, a foreign plan ran on the sim thread under the 1 s cap; now it runs on the worker, uncapped, holding `+0x918` for the whole IC.
- **The sim thread cannot avoid the wait** [verified, code + data]: every tick takes `+0x918` (steering `0x82EDB0`/`0x82F0B0`, docs/81; `UpdateSimFootprint`, docs/89; destroys, docs/88), and each of those waits for the running job (`LockAcquireAsync`, `RtWaitJob`). So a job of N ms is a hitch of ~N ms wherever the next acquire falls; `FallbackAsync` moves a long foreign plan's freeze from the plan call into `locomotion`/`threaded` and takes its bound away.
- **Long jobs only with `FallbackAsync=1`** [verified, logs, `route async: ... max N us`]: 13 runs with it off (freeze-data 2026-10-20, freeze-fix-ab OFF arms) max 126-374 ms; with it on: freeze-fix-ab BP route-bits **4,638 ms** (its profile has a 4,664 ms hitch that is all `locomotion`), freeze-fix-ab2 BP play on 1,481 ms (1,588 ms hitch, `threaded`), SV crowd 668 ms, IP water 509 ms, clean-night SV 1,017 and 522 ms, IP 348 ms. That the long jobs are the foreign ones is **inferred** (no per-job record existed; the new `long job` line answers it).
- **More jobs, more waits** [verified, per-slice counters]: on adds ~340-490 foreign jobs per 2-min slice (`fbForeignStarted`), worker job time +2-3 s per slice (`asyncUs`), which is the `rtWaits` and locomotion rise (+5-10 ms/s) while sync route time falls (BP 57.7 -> 38.5, SV 18.6 -> 13.0 ms/s).
- **M1Retry is not involved** [verified for the hitches above]: none is apply time (no `rtLock`-free `threaded`/`taskObjects` spike at a START); parks were 0-7 per 2-min slice (`fbRetryParked`).

### 10.4 Fix: `[RouteAsync] FbJobCapMs` (ts3native 0.36.16)

- `ServeAsync` sets the worker's `t_fbCapUs` to `FbJobCapMs` for a job with `fb = 1` while the runtime switch `fbjcMode` is 1 (start value 1 when the key is > 0; `--ab fbjobcap`, `+0x354C`), and clears it after; `CallPlanBudget` then ends the world A* at the cap through the same stubs as SyncCapMs (an ordinary no-path failure; the stubs are patched when either key is set). Own-task and A10 jobs are not capped. Nested plans that the job's managed calls send back to the worker inherit the cap (comment in code).
- Counters v81 at `+0x354C` after v80 (unseen phase 2), 3418 int32: `fbjcMode`, `fbjcCapUs`, `fbjcCapped`, `fbjcMaxUs` (longest foreign job), `ownJobMaxUs` (longest other job), `fbjcLong`, `ownJobLong` (jobs >= 200 ms). Every job >= 200 ms logs `route async: long job N ms, foreign|own START, IC kind K, route H[, capped (FbJobCapMs)]` (worker, 20 lines a minute), and the counter dump adds a `route jobs:` line. Those two answer the open inference whatever the cap is.
- Selftest `TestFallback` 4b: an own job runs uncapped; a foreign job runs with the cap on the worker, the hit counts `fbjcCapped`, a 210 ms foreign job counts `fbjcLong` and `fbjcMaxUs`; switch off = no cap.
- Choice of value: `release-features.ts fbjobcap` sets 300 (about the longest own-task job seen, 295-374 ms, so a foreign job is no worse than what OFF already shows). The cap fails a foreign plan that needs more A* than that; with FallbackAsync off the same plan ran on the sim thread up to SyncCapMs (1 s). Not done: re-running a capped job synchronously (that is the old freeze), splitting the job's lock hold (design work of docs/81-83).
- A/B queue `tools/harness/queues/fbjobcap-ab.json` (SV play off-first/on-first `--ab fbjobcap`, BP route-bits with the cap on; asi `game-copy/ts3native.asi.0316b`), not run.

### 10.5 Ship decision for v0.2.0 (recommendation)

`FallbackAsync=1` (and with it `M1Retry`) **should not ship as built in 0.36.14**: it cuts mean route time by about a third but makes the rare long foreign plan an uncapped job, and the sim thread waits for all of it (seen 0.5-4.6 s), where the off arm keeps it under `SyncCapMs`. Ship it only with `FbJobCapMs` (0.36.17) after `fbjobcap-ab.json` shows no foreign job far past the cap and no route-fail or stuck-Sim rise; otherwise ship `FallbackAsync=0` with `SyncCapMs=1000` (that bounds the sync freezes on its own).

## 11. Why FbJobCapMs did not bound the freezes (fbjobcap-ab 2026-10-22, candidate-loop-ab 2026-10-21) and the deadline fix (ts3native 0.36.20)

**Log reading + code; fix built offline, selftest ok, no game run.** Ledger rows: docs/51 §218. Data: `probe-out/profile/fbjobcap-ab-2026-10-22-*` (hitches, ab.json, ab-native.jsonl), `probe-out/queue/fbjobcap-ab-2026-10-22-*-ts3native.log`, `candidate-loop-ab-2026-10-21-*` (ts3native 0.36.7, `FallbackAsync=1` in every arm).

### 11.1 What the sim thread does in each multi-second frame [verified, hitch fields + log lines]

| frame | job | fields | sim thread | job side |
|---|---|---|---|---|
| SV fjc on-first 7,767 + 7,881 ms | `long job 7755 ms` / `7874 ms, foreign START, IC kind 0` | `locomotion` 7,749 / 7,869, `rtLock` 7,749 / 7,868 at `0x85FE10` | steering's `+0x918` acquire (docs/81) pumping in `LockAcquireAsync` | worker holds `+0x918` (watchdog lines "lock busy for 2/4/6 s during the job of route 257CB664 / 251CCF90", owner = worker, P4 phase 1) |
| SV fjc off-first 1,457 ms | `long job 1432 ms`, foreign | `rtLock` 1,436 at `0x85FE10` | same | same |
| BP fjc route-bits 1,964 ms | `long job 1948 ms`, foreign, `capped (FbJobCapMs)` | `rtLock` 1,944 at `0x8334CC` (UpdateSimFootprint family, docs/89) | same lock, other site | same |
| SV candidate-loop 7,985 / 8,426 / 16,435 / 7,951 ms | `asyncMaxUs` 7,959,816 (cl-on), 8,414,408 (cl-onrc) | `locomotion` 7,958 / 8,411 / 8,168 + `threaded` 8,259 / `threaded` 7,940 (0.36.7 has no `rt*` fields) | frame = job length | foreign [inferred: 0.36.7 has no `long job` line] |

- **The sim thread never waits for a fallback job's result.** The calling task sleeps (`AsyncWait`); what blocks the sim thread is the routing lock: the worker holds `+0x918` for the whole job (H6 S3b) and every tick needs it (steering `0x85FE10`, footprints `0x8334CC`/`0x833593`, destroys `0x02000003`). A foreign job of N ms is a frame of ~N ms at the next acquire, whichever site (§10.3) [verified].
- The BP 1,872 ms frame of the same run is different: a sim-thread `Route_Replan` (1,818 ms, failed, `taskObjects`). The log says `A10 off (Kinds=0)`, so `IC_Route_Replan` is not hooked and **`SyncCapMs` never sees Replan** (it lives in `SyncIc`, reached only from `IcCollect`) [verified, log + code]. Not a FallbackAsync effect; noted for the A10 decision.

### 11.2 Why the cap "never fired" in SV and did not bound the BP job [verified]

1. **SV: every long job fell in a cap-off slice.** `--ab fbjobcap` toggles only the cap; FallbackAsync is on in both halves. Per-slice counters: on-first `fbjcLong` 0 -> 2 in the slice 360-480 s whose snapshot has `fbjcMode 0`; off-first 0 -> 1 in 120-240 s, also `fbjcMode 0`. No on slice of either job had a foreign job >= 200 ms (`fbjcLong` flat; on-slice `fbjcMaxUs` <= 197 ms). The pair says nothing about the cap either way.
2. **BP: the cap is a world A\* node clock.** `BudgetStep` (the cost-function stubs, docs/15 phase 4) starts its clock at the first generated world A\* node, reads it every 16th generated node and acts only by walling world A\* edges. The capped job: `capNodes 32`, request time `capMs 1942`, job 1,948 ms. The clock was read at node 16 (< 300 ms after node 1) and node 32 (> 300 ms); the whole `Route_PlanLocked` took 1,942 ms with 32 generated nodes. World A\* nodes cost microseconds (same run: successful plans up to 10,751 nodes in <= 75 ms, `maxNodesOk`/`maxUsOk`), so >= ~1.6 s of the job ran outside world A\* node generation, in code the cap neither times nor stops [verified arithmetic]. The job's time outside `Route_PlanLocked` was 6 ms.
3. **Where inside `Route_PlanLocked`** is open [inferred candidates]: pending lot updates applied by the worker at its entry (`0x82C420`; `vfRtPendingInJob` 48 in that run), lazy lot routing builds (`LotGraph_Build 0x63CB90`), per-candidate lot legs (`LotPlan 0x6410B0`, portal A\* `0x63E340`), cell A\* `0x61E500`, expansions that evaluate many edges but generate few nodes, and after the cap the rest of the candidate loops (docs/142 §2.2).
4. `SyncCapMs` has the same node clock (A\* only); it never fired in any run (`fbCapped` 0).

### 11.3 Fix (ts3native 0.36.20, counters v84, same switches: `FbJobCapMs` + `--ab fbjobcap`)

- **Deadline from the job's start.** `ServeAsync` sets `t_fbDeadline` = job start + `FbJobCapMs` (QPC) for a foreign job with `fbjcMode 1`; `CallPlanBudget` puts it into the request's budget; `BudgetStep` reads the clock on **every 4th cost call of either kind** (edge or heuristic, the first included), independent of generated nodes. Past the deadline the search ends through its own exit as before (no-path failure). Work before the first node now counts; a request that starts past the deadline is walled at its first edge. Nested plans that the job's managed calls send back inherit the absolute deadline. Own-task and A10 jobs, `SyncCapMs` and `[RouteBudget]` keep their node clock.
- **What it cannot do** [verified, design]: stop code that makes no world A\* cost call (pending apply, lazy lot builds, lot legs, post passes). That part is now measured instead of guessed: each foreign job records `capAt` (job start -> cap fire), `tail` (cap fire -> job end), `plan` (summed `Route_PlanLocked`), `host` (waits for the sim thread to run the job's managed calls); the `long job` line prints them plus `other` (IC time outside both). A capped job whose cap fired > `FbJobCapMs` + 20 ms counts `fbjcCapLate` (work before the first cost call).
- **Sim side:** every sim-thread wait behind a foreign job (`LockAcquireAsync` pump with a foreign job running, `RtWaitJob`) feeds `fbjcSimWaitMaxUs` and a histogram; foreign job lengths another. Dump line `route jobs split: ...`.
- Counters v84 at `+0x3604` (after v83), 3475 int32: `fbjcCapAtMaxUs`, `fbjcCapLate`, `fbjcTailMaxUs`, `fbjcPlanMaxUs`, `fbjcHostMaxUs`, `fbjcSimWaitMaxUs`, `fbjcJobHist[6]`, `fbjcWaitHist[6]` (buckets < 100, < 300, < 500, < 1000, < 2000, >= 2000 ms). `tools/probe/agent.js` reads them.
- Not done (needs the split first): stopping the non-A\* phases (a planner-level abort at the candidate loop or `LotPlan`, a cap on the portal and cell A\*). If the verify job shows long tails, that is the next step, aimed at the phase the split names.

### 11.4 Selftest (`TestFallback` 4c, `selftest ok`)

A fake foreign job sends its world A\* cost calls through the patched stubs like the BP job: (a) 25 ms per node, cap 200 ms: capped after < 16 nodes (the old clock first read the time at node 16, >= 400 ms), `capAt` 200-300 ms, not late; (b) 300 ms before the first node: walled at the first edge (0 nodes), `capAt` >= 300 ms, `fbjcCapLate` 1; (c) 150 ms after the cap: `fbjcTailMaxUs` >= 150 ms; (d) own job: full search, no cap; (e) `fbjcMode 0`: foreign job uncapped, still counted; histogram sums, sim-wait buckets and max.

### 11.5 Queue `tools/harness/queues/fbfix-ab.json`

asi `game-copy/ts3native.asi.fbfix` (sha256 `91a531c25503bf5a8ab1a94cb1e0be576f9c4180823e447b5c42ac0e4b3a92ea`), ts3fixes unchanged. Job 1 **`fbfix-verify`** (day job, 5 min BP route-bits, noisy machine OK, cap on for the whole run, `[RouteEarlyOut] Mode=1` observe-only for the world A\* share of each slow plan): pass = 0.36.20 / v84 lines present, every capped `long job` has `capAt` <= 320 ms or is counted late, `fbjcCapAtMaxUs` <= 320,000 unless `fbjcCapLate` > 0, no crash, `fbActiveTrip` 0; report = tails, split, sim-wait histogram (ship target: `fbjcSimWaitMaxUs` <= ~400 ms, no wait >= 500 ms). Jobs 2-7: SV play, SV crowd, BP route-bits, each off-first + on-first, 10 min, `--ab fbjobcap`, clean night; only pair means count.
