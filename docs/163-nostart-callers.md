# 163. Which script callers are behind the sync fallback plans, and two fixes

Written 2026-10-22. **Log reading plus an offline ts3fixes build:** no game run. Inputs: every `probe-out/queue/*-ts3native.log` with `route async: sync plan` lines (220 logs, 1,296 plans), the jobs' `probe-out/profile/<tag>-*-hitches.jsonl` and `-repro.jsonl`, and the decompiled Steam scripts (`game-copy/decompiled`, not in git). Ledger rows: docs/51 §251. Tags as in AGENTS.md. Builds on docs/134 (the reasons), docs/141 (planner callers by context), docs/151 (plan sources).

## Summary

- **The multi-second sync plans have no managed caller in the logs.** In the release-relevant jobs (freeze-data, release-validate, release-vs-baseline, freeze-fix-ab, fbfix-ab, earlyout-ab, clean-night-0.2.0, replan-defer-ab, candidate-loop-ab, rare-scenarios, fbjobcap-ab, scriptfix-ab; the experimental snap-p* builds left out): 403 sync plans, 104.9 s; **272 carry a script stack, 29.3 s (28% of the time); of the 26 plans >= 1 s only 2 have a stack**, both our test agent [verified, `tools/harness/nostart-report.ts`]. The freeze-data and release-validate jobs ran without `LogFallbackStack`; the later jobs with it rarely hit a long plan. 101 of the 271 stacked plans stopped at depth 8, which ends at the `Route` helper and cannot name the caller (docs/141 §5).
- **Most multi-second plans are our own test commands, not play.** BP and SV `rare-route-bits` (4,060 / 3,924 / 4,095 / 1,984 ms BP, 1,313-2,134 ms SV, all `NoStart`, all failed): the hitch records put them in the `taskObjects` stage (the agent's poll task), and the two stacked ones of later runs (2,099 and 1,815 ms, reason `Foreign`) are `Ts3Agent.Handlers.SimRouteTo` [verified, stacks + hitch stage; the unstacked ones are inferred from the same repro and stage]. The repro plans ghost/flier/car-only routes to the farthest lot from the agent's task by design; a player never sends these.
- **The player-visible multi-second ones are SV big crowd** (freeze-data 7,361 + 7,350 + 1,015 ms, release-validate 1,786 ms, all `NoStart`, all failed `Route_Plan`s in the `threaded` stage, i.e. ThreadedObjects: Sims, lots and their alarms) [verified, hitch fields]. Caller unknown: no stack. The 7.36 / 7.35 s pair ran ~0.3 s apart (hitch iterations at 320.0 s and 327.7 s), 30-45 s after the crowd's third wave and with the clock at ultra speed across several 3 AMs [verified, repro log + hitches]. Candidates [inferred]: the 3 AM stuck check (§3: up to 10 plans to random lot points that include sea), a caller retrying a failed plan (§4), a foreign-task social or Go Here With route.
- **Ranking of the attributed callers by player-visible hitches** (§2): the 3 AM stuck check is first (43 plans, 5.6 s, 4 hitches >= 300 ms, 0 >= 1 s, max 731 ms); by summed time next then view-object `Los` routes (73, 5.5 s, max 172 ms, sync by design, docs/134 §7), then our test agent (7, 4.2 s, max 2,099 ms), `Lot.PlanToLot` from a frame the 8-deep stack hides (17, 2.7 s, max 312 ms) and traffic cars (32, 2.3 s, max 188 ms; `unseencars` exists, docs/151 D4).
- **Built (ts3fixes, both default off, offline, not run):** `stuckchecknear` (§3: the stuck check's same random points, nearest first; same verdict) and `failrepeat` (§4: a Sim's no-path failure repeated within 500 ms from the same spot to the same goal returns the recorded failure). Counters 238-243. Selftest `node tools/selftest/nostart/run.ts`. Queue `tools/harness/queues/nostart-fix-ab.json` (§5), with `LogFallbackStack=16` in every job so the next SV crowd run names the multi-second callers.

## 1. Data and method

`node tools/harness/nostart-report.ts [--filter <regex>] [--jobs]` (new, reads only; `TS3_REPO=<main checkout>` from a worktree) pairs each `sync plan` line with the `script stack of route H` line that follows it, keys the caller (test agent, the docs/141 chain ids by method name, else the first 3 frames above `ScriptCore.Route` / `Sims3.SimIFace.Route` / our hook frames) and sums count, ms, max, worlds and reasons. `--jobs` adds a row per log with frames >= 300 ms and hitches per minute from the profile. The table in §2 is the release-relevant filter `^(freeze-data|release-validate|release-vs-baseline|fbfix|freeze-fix|earlyout|clean-night-0|replan-defer|candidate-loop|rare-scenarios|fbjobcap|scriptfix|stuckcheck)`.

Reasons as docs/134 §1: `NoStart` (no START reached ts3native; with the 2026-10-20 ts3fixes every foreign-task plan was `NoStart`), `Foreign` (ts3fixes 2026-10-21+ sent a foreign START that `FallbackAsync=0` refused), `M1Pending`, `QueueFull`, `Los`, `Skipped`.

## 2. Ranking

A sync plan of N ms is a sim iteration (a player-visible hitch) of at least N ms, so the hitch columns count plans >= 300 / >= 1000 ms.

| caller (docs/141 chain) | count | sum s | hitches >= 300 ms | >= 1000 ms | max ms | worlds | reasons |
|---|---|---|---|---|---|---|---|
| (no stack) | 131 | 75.60 | 45 | 24 | 7,361 | bp, ip, sv | NoStart 63, M1Pending 28, Foreign 22, QueueFull 13, Los 3, Skipped 2 |
| `LotManager.RunStuckCheckHelper` (3 AM stuck check alarm; docs/134 §8, not a docs/141 c chain: alarm = b) | 43 | 5.57 | 4 | 0 | 731 | bp, ip, sv | Foreign 40, NoStart 2, M1Pending 1 |
| `ViewObjects.CreateRouteToObject` (view/photo, own task) | 73 | 5.46 | 0 | 0 | 172 | bp, sv, ip | Los 73 |
| test agent `Ts3Agent.Handlers.SimRouteTo` (repro command) | 7 | 4.22 | 2 | 2 | 2,099 | bp, ip | Foreign 7 |
| `Lot.PlanToLot` (frame above hidden, depth 8) | 17 | 2.65 | 1 | 0 | 312 | bp, ip | Foreign 16, M1Pending 1 |
| stack too short (ends at `Route.PlanToPointRadialRange`) | 20 | 2.55 | 0 | 0 | 262 | bp, ip | M1Pending 9, Foreign 9, NoStart 2 |
| `LiteDriver.Simulate` (traffic car) | 32 | 2.28 | 0 | 0 | 188 | bp | Foreign 31, M1Pending 1 |
| `RoutingComponent.RouteToSlot` | 18 | 1.21 | 0 | 0 | 98 | bp | M1Pending 15, QueueFull 2, Skipped 1 |
| `GoHome.Run` -> `Lot.PlanRouteIntoLot` / `PlanToLot` | 13 | 1.15 | 0 | 0 | 166 | bp, ip, sv | M1Pending 11, Foreign 2 |
| stack too short (ends at `Route.PlanToSlot`) | 14 | 0.91 | 0 | 0 | 97 | bp, sv | M1Pending 12, Foreign 1, Skipped 1 |

**By hitches:** 45 of the 52 plans >= 300 ms and 24 of the 26 >= 1 s have no stack; of the attributed ones only the stuck check (4 hitches >= 300 ms, max 731), our test agent (2 >= 1 s, max 2,099) and `Lot.PlanToLot` (1, 312 ms) cause any. Los routes, traffic cars, slot and GoHome plans cost time but no hitch >= 300 ms.

No docs/141 c chain (#1 reset safe location, #3 `GetFarthestLot`, #5 indoor positions) appears in any stack. #2 (replan callbacks) and #4 (replan all) are invisible here, not absent: every line in this set is `IC Plan`, and with A10 off a sim-thread `Route_Replan` never reaches `IcCollect`, so it logs no sync-plan line (docs/134 §11.1; the BP route-bits hitch at 51.6 s is a 4,443 ms failed `Route_Replan` from the agent's `@route.Replan` step, seen only in the hitch record) [verified]. Their fixes are built already (docs/141 §6-§7: `safespotcap`, `routablecache`, `farthestsorted`, `indoorcap`, `replandefer`, `replanspread`).

Unstacked plans >= 1 s by job (release-relevant set): SV big crowd 7,361 / 7,350 / 1,015 (freeze-data), 1,786 (release-validate); BP route bits 4,060 / 3,924 / 4,095 / 1,984; SV route bits 1,313 / 1,671 / 2,134 / 1,028; SV `clean-night-0.2.0` spreads / slotro 2,702 / 1,955 / 1,483 / 1,476 / 1,300 / 1,274 (`Foreign`: a foreign task, FallbackAsync off); SV release-vs-baseline 1,069-1,253 (x5, `NoStart`); SV scriptfix 1,244 (`Foreign`).

## 3. `stuckchecknear`: the 3 AM stuck check, nearest point first

**What the game does** [verified, decompiled `LotManager.RunStuckCheckHelper`]: at the 3 AM venue cleanup every actor gets an alarm 20-60 sim-minutes later. If the Sim is not on the active household's lot, has not moved (`Position == mLocation`) and is not in a rabbit hole: on the world lot it plans 2 m in 4 directions (selectable Sims only); on any other lot it draws a point with `Lot.GetRandomPosition(false, true, WorldTerrain | WorldSea | LotTerrain)` and plans to it, up to `Sim.kNumberOfPlanAttemptsForStuckSim` (10) times, returning at the first success. All failed: re-arm (attempt + 1) or, at attempt 3, `SetObjectToReset`. `GetRandomPosition` draws with `RandomUtil` [verified]. Sea is in the mask, so a beach lot yields sea points; a failed plan floods the Sim's whole reachable area before it reports no path (docs/142 §2).

**Ours** (`FewerPlans.cs`, `StuckCheckNear`, inside the existing `RunStuckCheck` hook, also on the `stuckcheckasync` drain path): same early return in the same order; draws all 10 points with the same call and arguments, then plans them nearest first (ties in draw order, `NoStartCore.NearestFirst`), stops at the first success, and ends as the game does (`mAttempt` read by reflection; reset, or the private static `CreateAlarm(sim, attempt + 1)`). The world lot branch and a missing field or method run the game's body.

**Same verdict** [verified by construction and selftest]: the game's answer is "not stuck" iff some drawn point routes; the game stops drawing at its first success, so the points it plans are a prefix of ours, drawn from the same stream in the same order, and any success among them is a success among ours; if all 10 fail, both plan all 10. `tools/selftest/nostart`: 100,000 random cases, verdict equal every time. **What differs:** the number of `RandomUtil` draws (always 10 for us; the game stops at its first success), so later random calls in that tick differ; and plans run in another order.

**Expected gain** [inferred]: fewer failed plans before the first success when near points route more often than far ones (selftest model with that shape: 12,473 vs 32,772 failed plans in 100,000 checks; a model, not a measurement). No gain for a truly stuck Sim (all 10 fail either way) or when the first drawn point routes. The stuck check's measured sync plans are <= 731 ms each; whether it is behind the SV 7 s pair is open (I-251-2).

## 4. `failrepeat`: the same failed plan again right away

**Shape**: two failed `Route_Plan`s of 7,361 and 7,350 ms on the sim thread about 0.3 s apart in one SV crowd run [verified, hitches]; the same search length twice suggests the same request [inferred]. ts3native's `ReplanDedupe` (docs/150 §13) serves a repeat of a failed plan within 500 ms for the Replan ICs only; nothing does it for `Plan`.

**Ours** (`RouteBackoff.cs`, `PlanFailRepeat`, at the `ScriptCore.Route.Plan` funnel before the backoff/async path): when the follower is a Sim, the goal is >= 5 m away (`kBackoffMinDistance`, so fail-path hops near the Sim are untouched) and this Sim's last plan to the same 1 m goal cell from within 0.25 m of the same spot failed with `FailedBecauseNoPathToDestination`, blocked by neither a Sim nor a vehicle, and returned at most 500 real ms ago, the plan returns that recorded result (all fields, blocker included, so the caller runs the same failure branch) with the paths cleared and start points invalid as after a real failure (`FailFast`, the backoff's proven path). A success or any other result clears the entry. Map dropped at 256 entries.

**Differs from the game** only when the world opened a path inside the 500 ms (a door unlocked, a blocker moved): the repeat fails once more instead of succeeding. `ReplanDedupe` guards that with the `[RouteSnap]` world version; ts3fixes cannot see it, hence the short real-time window. Interaction with `backoff` (unseen townies, 30 sim-minutes): failrepeat runs first, so a repeat it answers is not counted by the backoff.

## 5. Queue `tools/harness/queues/nostart-fix-ab.json` (not run)

asi `game-copy/ts3native.asi.0325` at release defaults plus `HitchAttr.Enabled=1`, `RouteAsync.LogFallbackMs=20`, `RouteAsync.LogFallbackStack=16`; ts3fixes and ts3agent from this branch. Only the two new switches (intermediate build rule).

| job | world / repro | min | switches | pass |
|---|---|---|---|---|
| `bp-nostart-verify-3am` | BP `stuck-check-3am` | 8 | both on | no crash, 0 watchdog lines, `stuckchecknear_checks >= 1` (8 min: the alarms fire 20-60 sim-minutes after 3 AM) |
| `sv-nostart-verify-crowd` | SV `rare-big-crowd` | 5 | both on | no crash, 0 watchdog lines; counters read |
| `bp-nostart-verify-routebits` | BP `rare-route-bits` | 5 | both on | no crash; agent plan results as in OFF |
| `sv-nostart-crowd-offfirst` / `-onfirst` | SV `rare-big-crowd` | 10 | `--ab stuckchecknear,failrepeat`, 2-min slices | clean night; pair mean only |
| `bp-nostart-routebits-offfirst` / `-onfirst` | BP `rare-route-bits` | 10 | same | clean night; pair mean only |

Report: hitches first, with `tools/harness/hitch-ab.ts` (built by another agent: p99, p99.9, max, counts >= 100/300/500/1000 ms, both-order pair means), then `node tools/harness/nostart-report.ts --filter nostart-fix-ab --jobs` (sync fallback count / sum / max ms and hitches >= 300 / >= 1000 ms by caller, frames >= 300 ms, hitches per minute) plus counters `stuckchecknear_checks/_plans/_allfailed`, `failrepeat_recorded/_answered/_skipped_ms` per slice. The BP route-bits pair mostly checks that nothing breaks: its long plans are the agent's, 2-3 s apart (beyond the 500 ms window). Game time: ~1 h 40 min (3 day jobs, 4 clean-night jobs).

## 6. Effort and next step

Built in ~2 h (log analysis, two switches, selftest, report tool, queue). Next: run the verify jobs; the SV crowd job's 16-frame stacks decide whether either switch targets the multi-second plans at all. If the 7 s callers turn out to be something else, fix that caller (the report keys it) before the release pair.

## 7. Callers under 0.5 s (2026-10-24): owner-task STARTs

Input: `nostart-report.ts` over every `probe-out/queue/*-ts3native.log` on 2026-10-24 (758 logs, 3,424 sync plans). v0.1.5 ships with `[RouteAsync] FallbackAsync=0` (no gain in the pairs, docs/release-notes/v0.1.5-dev.md), so a `Foreign` START stays sync. Sources: IL call lists from `game-copy/analysis/callgraph/index.json` (`managed-callgraph.ts` build; no decompiled bodies read for this section).

| caller (script stack) | plans | max ms | reason | where the plan comes from | verdict | fix |
|---|---|---|---|---|---|---|
| `LotManager.RunStuckCheckHelper` | 130 (task table: 51) | 6,870 (task table: 423) | Foreign | 3 AM stuck check; with `stuckcheckasync` (in v0.1.5) its plans run in our drain task, without it in the world lot's alarm task; neither is the Sim's [verified, §3 + FewerPlans.cs] | the plans are throwaway (`CreateRoute().PlanToPoint(..).Succeeded()`, never followed), so the drain task can sleep on them like an own task [verified, FewerPlans.cs / §3] | **built `stuckcheckown`**: own START from the drain task only (the alarm-task path stays sync: sleeping there holds back every world alarm, docs/134 §8) |
| `LiteDriver.Simulate` (traffic car) | 120 | 224 | Foreign | `CarNpcWithNoDriver.OnStartup` adds `new LiteDriver(car)` as its own task (`Simulator.AddObject`); `LiteDriver.Simulate` calls the car's `RoutingComponent.CreateRoute`, `PlanToPoint`, `DoRoute`, `Simulator.Sleep` [verified, IL call list]. Follower = the car, task = the LiteDriver: ts3fixes sees no Sim and sends a foreign START | the LiteDriver task is the only one driving that car, so sleeping it is the own-task case [inferred from the IL: no other route call in it] | **built `drivertaskasync`** (car branch): own START when the current task's object is a `CarNpcWithNoDriver+LiteDriver` and the follower a `CarNpcWithNoDriver` |
| `Boat.RouteBoatNearHouseboat` | 21 | 243 | Foreign | `GetOutOfBoat.Run` (the Sim's interaction, Sim's task) calls `RouteBoatNearHouseboat(Sim, Houseboat)`: `CreateRoute`, `AddDestination(RadialRangeDestination)`, `Plan`, `DoRoute` [verified, IL]; the receiver of `CreateRoute` is the boat [inferred: the reason is Foreign, so the follower is not the task's Sim] | the boat moves only while its rider's task drives it [inferred] | **built `drivertaskasync`** (rider branch): own START when the follower is a `Vehicle` and the task's Sim has `Parent == vehicle` [the Parent link is inferred: counter `drivertask_rider_sent` decides] |
| `GoHere.RouteActorToDestination` | 70 | 296 | QueueFull (62) | the Sim's own Go Here (`Terrain.GoHere`: `CreateRoute`, `Plan`, `DoRoute`) [verified, IL]: already an own START, refused because the 8 job slots are full (`fbMode 0`) [verified, docs/134 §1.3] | not a task problem; only a deeper queue or a retry helps | not built: (a) native `kJobs` 32 without the rest of `FallbackAsync` (a new native switch), or (b) ts3fixes retries the own START a few ticks after `StepAside` (needs the refusal reason from ts3native, else Los / M1 refusals wait too). `jobyield` (docs/183) already makes the task sleep while a job runs, then the sync plan follows |
| `ServingContainerGroup.RunBodyForGrabAServing` | 12 | 397 | InvokeHook 11, M1Pending 1 | the Sim's own interaction: `PlanToPointRadialRange` then `DoRoute` [verified, IL]; `InvokeHook` = the old asi's broken hook (PLAN.md 2026-10-23, fixed), `M1Pending` = lot update pending (docs/134 §2.6) | one real plan in the window; the fix is native (`M1Retry`, needs `FallbackAsync`) | not built (rare; no ts3fixes lever) |

**What the own START changes:** ts3native treats the plan like a Sim's own plan (worker search, only the asking task sleeps, `NoYield` / `Active` / `M1` / `QueueFull` refusals as usual); it does not check who the follower is [inferred from the docs/134 §2 refusal list: the foreign bit only gates `fbMode`; the counter `ownertask_started` confirms]. Unlike `FallbackAsync=1` (every foreign plan, ~1,500 per SV crowd run, worker hold 142 vs 78 ms/s in the v0.1.5 candidate) these switches move only the plans whose task cannot be blocking anyone else: per hour of play ~tens of car plans, a few boat plans and the night's stuck checks.

**Expected effect** [inferred, not measured]: car and boat plans off the sim thread (sum ~11 s over the 758 logs, none >= 300 ms, so hitch counts barely move; ms/s on BP traffic); stuck check plans off the sim thread including the rare multi-second failed ones (7 >= 300 ms, 2 >= 1 s in the logs). **Risks:** a car or boat waits a few ticks longer before it moves; a stuck-check Sim that moves during the search gets a verdict from its old spot (harmless: moving means not stuck, and the game re-checks); the worker gets more jobs, so own plans of Sims may queue behind them (`QueueFull` up), which is what `FallbackAsync` paid for at scale.

**Built** (ts3fixes, both default off, offline): `RouteBackoff.cs` `OwnerTaskStart` / `OwnerTaskSlot`, tunables `kDriverTaskAsync`, `kStuckCheckOwn`, counters [251] car STARTs sent, [252] rider STARTs sent, [253] stuck check STARTs sent, [254] started (agent `drivertask_car_sent`, `drivertask_rider_sent`, `stuckcheckown_sent`, `ownertask_started`). Compiles with csc against the game assemblies; ts3agent change not compiled. Repro drafts (not run): `route-traffic-car-loop` (BP), `ip-boat-houseboat-return-loop` (IP), `route-stuckcheck-own-loop` (BP). Verify rule: sent > 0 and started ≈ sent on the on arms, no `reason Foreign` line with the caller's stack, cars and both houseboat Sims still arrive.

## Reproduce

```
TS3_REPO=<main checkout> node tools/harness/nostart-report.ts --filter "^(freeze-data|release-validate|release-vs-baseline|fbfix|freeze-fix|earlyout|clean-night-0|replan-defer|candidate-loop|rare-scenarios|fbjobcap|scriptfix|stuckcheck)"
node tools/harness/nostart-report.ts --self-check
node tools/selftest/nostart/run.ts
node tools/setup/build-fixes.ts
```
