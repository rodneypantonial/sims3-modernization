# 176. Foreign ReplanFromPosition plans: the Isla Paradiso route-search freezes

Written 2026-10-23. Static analysis of existing run data plus a build (`[RouteAsync] ForeignKinds`, ts3native 0.36.29, counters v101; ts3fixes `asyncreplanforeign`; ts3agent `route_replan`). **Not run.** Ledger: docs/51 §286. Builds on docs/87 (design A10), docs/134 (foreign START for `Route.Plan`), docs/141 (plan callers), docs/164 (sync-plan phases).

## 1. What froze (data)

Source: `natural-play-2026-10-22-ip-natural-30` on asi.0332 with the candidate set on (`node tools/harness/freeze-report.ts natural-play-2026-10-22-ip-natural-30 --top 40`; ts3native log `probe-out/queue/natural-play-2026-10-22-ip-natural-30-2247-ts3native.log`, `route-phases-report.ts --sync <log> --min 150`).

The 8 freezes of class "route search" (>= 300 ms) are **all `ReplanFromPosition`** [measured]: one sync plan each, 283-318 ms (s 64, 173, 199, 1251, 1260, 1475, 1632) and 243 ms inside the 452 ms worst (s 531). Two more freezes carry the same caller with a shorter plan (s 1705 "mixed", RFP 139 ms; s 999 "other", RFP 142 ms). No `Replan`, `PlanFromPoint` or IC `Plan` is behind a route-search freeze in this run; the `PlanFromPoint` freezes (s 146, 998, 1239, five `TaskObjects burst`) are waits and Overwatch's stuck check, not search time.

The script task running in each freeze is a Sim's own `ScriptProxy`: 7 of 8 resumed from `ScriptCore.EventQueue.GetNextEvent` (a state-machine wait, e.g. a portal animation) and 1 from `GoToPointState.PreBoatRouteCallback` (a boat route) [measured: freeze-report task column].

Sync-plan phases (`[RouteJobPhases] SyncPlans=1`, 72 sync plans >= 150 ms, 15.5 s) [measured]:

| plan | world A\* | water | post pass | cell A\* | other |
|---|---|---|---|---|---|
| the 283-318 ms RFPs (one world A\* call each, goal-lot entry loop) | 136-245 ms (52-86%) | 16-49 ms | 10-57 ms | 0-19 ms | alloc 3-8 |
| all 72 >= 150 ms | 54.3% | 15.8% | 13.1% | 9.3% | alloc 2.8, plan 2.2, cand 1.0 |

All of them succeeded; `cap: yes` (SyncCapMs 1000 not reached). The one 703 ms failed plan (cell A\* 95%) is a different shape and not in a freeze row. The log names them `Route_PlanLocked (no IC)` because A10 is off in this config (`Kinds=0`): the IC is not hooked, so there is no reason code or script stack (I-218-7).

## 2. Who calls them

`Route.ReplanFromPoint` callers in the Steam scripts (`managed-callgraph.ts callers "Sims3.SimIFace.Route::ReplanFromPoint"`) [verified, index]: `RoutingComponent.DoRoute` (the route loop: after an action returns `ContinueAndPopPathAndReplan` or `...Restart`, it replans from `RoutingParent.PositionOnFloor` when the parent is an `IBoat`, else from the Sim), `VehicleFollowSubPathRouteAction.ForceReplan` (cars, boats), `PathAdaptor.Replan` (doors), `SimRoutingComponent` houseboat gangway/ladder/boat enter and exit and `CheckAndUpdateRouteForStations`, strollers, `GetIn*RouteAction.OnLoad`, social jigs, `LearnToDrive`, `Terrain.ModifyRouteForSpecies`.

Which of them: the A10 runs of 2026-10-18 hooked the IC and logged every sync plan with its reason (`clean-night-2-2026-10-18-ip-ar-ab`, `-ip-ar-ab-wait`, `Kinds=7`) [measured]:

- A10 moved 707 / 722 own-task RFPs (about 20 ms each, `p10-report.ts`), but **every RFP sync plan >= 150 ms (100 plans, 42 of them >= 250 ms) was `NoStart`**: ts3fixes sent no START. They come from **one managed call site per load** (`caller 455C2827` in one run, `45682827` in the other).
- `NoStart` for an A10 kind means `AsyncEligible` was false in `RouteBackoff.cs`: the route's follower is not a Sim (`Follower.Target as Sim` null: boat or car), or the running task is not the follower Sim's, or the context cannot yield.

So the freeze plans are **foreign** replans: a yielding task that is not the route's own Sim [verified for the A10 runs; same build family for the 10-22 run, inferred]. Most likely the boat case of `DoRoute` / `VehicleFollowSubPathRouteAction` (IP only, water time in every long plan, `PreBoatRouteCallback`, the Sim's own task running a route whose follower is the boat) [inferred, I-176-3]. With ForeignKinds the IC is hooked again, so the first verify run prints the script stack of each such plan (`LogFallbackStack=16`, reason `Foreign` on off arms).

## 3. Which fix

| option | covers these plans | result | gain |
|---|---|---|---|
| A10 as built (`Kinds=4`) | no: all long RFPs are NoStart | - | 0 for the freezes; dropped for +7.4 ms/s keyed waits (docs/136 §3.6) |
| worker job with a native completion wait on the sim thread | yes | same | none: the sim thread still stops for the plan |
| **worker job, the calling task sleeps a tick at a time until READY** (A10's handshake from a foreign yielding task) | yes, when the task may yield and the route is not being followed | the game's own IC run whole on the worker (I-87-10) | up to the whole plan time per freeze (~150-320 ms) minus sim waits behind the job |

Built: the third row, as an extension of A10 rather than a new path (all of A10's and docs/134's rules apply: M1, queue, D drains, H2, destroy waits, CANCEL, collect checks).

## 4. Build (ts3native 0.36.29, counters v101; off by default)

- **ini** `[RouteAsync] ForeignKinds` (bits as `Kinds`: 1 Replan, 2 PlanFromPoint, 4 ReplanFromPosition; default 0) and `ForeignKindsMode` (afMode's start, default 1). A kind in ForeignKinds is hooked even with `Kinds=0`, but its **own-task** STARTs are refused (`g_arOwnKinds`), so `Kinds=0 ForeignKinds=4` moves only the foreign RFPs. Runtime switch `afMode` (`native_set a10foreign`, `--ab a10foreign`, +0x4308).
- **Channel:** ts3fixes `AsyncKindStart` sends the kind START with **bit 61** when the own-task check fails but `ForeignEligible()` holds (switch `asyncreplanforeign`, default on, ts3native decides). In ts3native, bit 61 alone with bit 58 is the NoYield query; the A10 channel also needs bit 59, so they never meet. Older ts3native fails the channel test and passes the call to the game, which answers false: sync as before.
- **ts3native:** a foreign kind START is counted (`afSeen`), refused as `Foreign` unless `afMode` 1 and the kind in `afKinds`, then goes through `AsyncStart` (NoYield `afNoYield`, route being followed `afActive`, M1, queue). Its job is flagged `af`, not `fb`: **no FbJobCapMs cap** (the job runs the original IC to the end), `RjpJobBegin` counts it as an A10 job. Collect adds `afCollected` / `afJobUs`; tripwire `afActiveTrip` if the route started being followed during the job (expected 0).
- **Results stay the game's:** the worker runs the whole original IC (`g_arOrig[3]`), as every A10 job; every refusal, mismatch, drop or skip plans synchronously through the game's IC on the sim thread. One known difference from the release config: with the IC hooked, its sync path is A10's `SyncIc`, so `SyncCapMs` (1000 ms world A\* cap) and `PlanDeadlineMs` apply to sync RFPs (as with any `Kinds` value; I-218-7). The measurement settings keep `PlanDeadlineMs=0`.
- **Selftest** `TestA10` case 13: refused while off (reason `Foreign`, sync plan), own STARTs refused for a foreign-only kind, foreign job started with `af=1 fb=0` and collected, `afActive` and `afNoYield` refusals; v100 layout asserts.
- **Agent:** `route_replan {count}` replans `@route` from its follower's position in a one-shot task (a foreign yielding task) and keeps rows (plan type, paths, ms, signature of path starts + destination) for `{result: true}`. ts3fixes counters 246-247 (`a10foreign_sent`, `a10foreign_started`).

## 5. Repro and queue

- Repro `ip-foreign-replan-loop` (`tools/harness/repros.json`, IP): the active Sim plans to the farthest community lot with water + boat planning (kept as `@route`); each firing = 3 foreign `ReplanFromPoint` calls; arms flip `a10foreign` ABBA until 10 firings per arm; rows per loop for result equality. Feature entry `a10foreign` in `feature-repros.json`.
- Queue `tools/harness/queues/foreign-replan.json` (measurement settings of `freeze-measure.json` + `ForeignKinds=4`, `ForeignKindsMode=0`): `ip-foreign-replan-verify` (2 firings per arm, correctness, noisy machine OK) and `ip-foreign-replan-ab` (10 per arm, clean night). No natural-play job. Read with `hitch-ab.ts --scenario <tag>`.

## 6. Open

- Whether the natural callers' routes are being followed at the replan (`afActive`): if the boat route stays active, ts3native refuses and nothing moves; the verify run's off-arm stacks and counters show it (I-176-5).
- Waits moved, not removed: a 150-300 ms job holds `+0x918` on the worker; sim-thread takers (steering, footprints, gate 12) wait behind it unless TailUnlock / P5 lending admit them. The loop A/B reads HitchAttr holders (route worker) in the on windows (I-176-6).
- Estimate: verify + A/B = 2 game runs (~6 + ~15 min, 1 clean night slot); a follow-up for followed boat routes (if `afActive` dominates) is design work, ~2-4 h.
