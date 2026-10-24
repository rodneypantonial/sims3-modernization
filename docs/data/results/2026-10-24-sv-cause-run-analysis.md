# SV cause run analysis (2026-10-24, sv-cause-run)

Run: 2026-10-24 15:55-16:42 PHT, SV, 45 min seeded 12345, master 0.36.39 = release defaults + `LogFallbackStack=16`, `LogFallbackMs=20`, `OwnJobWait=1`, `HitchAttr.Enabled=1`, `HitchAttr.Holders=1`. Read-only analysis, no game runs.

Inputs: `run-verdict.ts sv-cause-run --date 2026-10-24` (PASS, 47.1 min, 8.6 >= 300 ms and 2.4 >= 1000 ms per 10 min, max 2431 ms, p99 61 ms), `hitch-ab.ts ... --causes`, `nostart-report.ts --filter sv-cause-run --jobs`, `freeze-report.ts ... --min 300/500 --repro-causes`, `probe-out/queue/sv-cause-run-2026-10-24-sv-cause-run-0754-ts3native.log`.

Tags: [measured] = read directly from the logs/tool output; [inferred] = joined by time/duration or from design docs.

## 1. Headline

- **Every natural freeze >= 500 ms (9 of 9) and every >= 1 s freeze (7 natural + 2 in a repro window) is a stuck check plan**: `LotManager.RunStuckCheckHelper` run by ts3fixes `stuckcheckasync` in its `OneShotFunctionTask` drain (`Ts3Fixes.Fixes.StuckCheckDrain`), planned synchronously on the sim thread with `reason Foreign, entry nojob` [measured: script stack + freeze-report slowest script run].
  Stack (innermost first): `ScriptCore.Route.Route_Plan < Route_Plan < PlanOrig < RouteBackoffHook.PlanAsync < PlanBackoffBody < PlanBackoff < Plan < Sims3.SimIFace.Route.PlanToPoint < RunStuckCheckHelper.RunOrig < Ts3Fixes.Fixes.StuckCheckRun < Ts3Fixes.Fixes.StuckCheckDrain < Sims3.Gameplay.Function.Invoke < Sims3.Gameplay.OneShotFunctionTask.Simulate`.
  59 such plans, 20.2 s of sim-thread time, 11 plans >= 1 s, max 2019 ms [measured, nostart-report].
- **No freeze >= 500 ms is an own-job wait.** Only 2 `sim waited N ms behind an own job` lines: 464 ms (route 255EEFE3, job 486 ms) and 313 ms (route 25699209, job 318 ms) [measured]. By duration they match the 499 ms freeze at s 586 (gate 12 F-G placement 464 ms) and the 355 ms freeze at s 2249 (ObjectFootprintFromTool 314 ms) [inferred join].
- **Movement-step freezes (300-500 ms) wait on our own async route jobs** at the steering / footprint / gate-12 sites, not on other game locks. HitchAttr Holders logged only 7 lock waits >= 20 ms in the whole run, all 20-36 ms (main thread or thread 36412 as holder, sites `0x4A94EC`, `0x729E2C`, `0x7345F2`, `0x7366CE`); none sits in a >= 300 ms freeze (lock holder column empty in every freeze row) [measured].
- Own-job sim waits in total: 4,640 waits, 29.4 s, max 288 ms by the T1/T4 counter line; `job yield mode 0` (JobYield off as built) [measured].

## 2. Every freeze >= 500 ms (natural play) [measured]

s = seconds from profile start (15:55 PHT + load).

| s | freeze ms | longest iter ms | sync plan (reason) | script caller | own-job wait | lock holder |
|---|---|---|---|---|---|---|
| 917 | 2433 | 2431 | 3 plans 2411 ms, 1281 ms Foreign | StuckCheckDrain (2420 ms run) | none | none |
| 1139 | 2057 | 2068 | 2019 ms Foreign | StuckCheckDrain (2047) | none | none |
| 802 | 1610 | 1608 | 1524 ms Foreign | StuckCheckDrain (1592) | none | none |
| 1588 | 1261 | 1239 | 1220 ms Foreign | StuckCheckDrain (1227) | none | none |
| 913 | 1205 | 1201 | 1178 ms Foreign | StuckCheckDrain (1189) | RemovePrivateFootprint 3 ms | none |
| 1586 | 1199 | 1213 | 1171 ms Foreign | StuckCheckDrain (1182) | none | none |
| 691 | 1126 | 1125 | 1088 ms Foreign | StuckCheckDrain (1111) | none | none |
| 1024 | 718 | 722 | 707 ms Foreign | StuckCheckDrain (708) | none | none |
| 1949 | 621 | 468 | 2 plans 448 ms, 231 ms Foreign | StuckCheckDrain (456) | none | none |

Excluded by the tools as repro window (`test_env guard: winter or extr`), but the same cause: s 2408 2363 ms and s 2415 1356 ms (1336 ms Foreign, `nojob`), both StuckCheckDrain [measured]. The stuck check is not part of that repro [inferred], so in play these are 2 more >= 1 s freezes.

## 3. Freezes 300-499 ms (natural play) [measured]

| s | freeze ms | class | longest route-job wait | script task in the iteration |
|---|---|---|---|---|
| 586 | 499 | ThreadedObjects | gate 12 F-G placement 464 ms (= own-job wait 464 ms, route 255EEFE3 [inferred join]) | AutonomyManager 486 |
| 1944 | 456 | route search | none; 219 ms Foreign plan | StuckCheckDrain 443 |
| 2380 | 451 | locomotion | steering obstacle query 219 ms | StuckWatchdog, Sim OnAboutToStartFollowingRoute |
| 2180 | 446 | route search | none; 217 ms Foreign plan | StuckCheckDrain 435 |
| 1184 | 439 | locomotion | gate 12 GetIfPresent 228 ms | ScriptProxy 228 |
| 525 | 413 | locomotion (anim) | `0x0085f6cb` 283 ms | none |
| 2064 | 395 | locomotion | steering avoidance A 261 ms | OwChainTheirs 2 (ts3fixes owchain) |
| 587 | 389 | route search | none; sync `ReplanFromPosition` 363 ms | Sim FollowRouteAction.PerformRoute 373 |
| 2249 | 355 | locomotion (anim) | ObjectFootprintFromTool 314 ms (= own-job wait 313 ms, route 25699209 [inferred join]) | StuckWatchdog |
| 2475 | 344 | locomotion | steering obstacle query 263 ms (+ 85 ms Los plan) | Sim PerformRoute 62 |
| 2671 | 333 | ThreadedObjects | RemovePrivateFootprint 257 ms | Sim OnAboutToStartFollowingRoute 260 |
| 364 | 329 | locomotion | steering obstacle query 131 ms | AutonomyManager 42 |
| 283 | 310 | locomotion | steering obstacle query 289 ms | Sim 3 |
| 322 | 305 | ThreadedObjects | RemovePrivateFootprint 254 ms | Sim OnAboutToStartFollowingRoute 257 |
| 1729 | 304 | ThreadedObjects | ObjectFootprintFromTool 274 ms | Sim WaitForExitReason 277 |
| 276 | 301 | ThreadedObjects | gate 12 GetIfPresent 276 ms | WorldLot Lot.Simulate 277 |

## 4. Ranking by count x ms (natural freezes >= 300 ms)

| # | cause | n >= 300 | n >= 1000 | ms | tag |
|---|---|---|---|---|---|
| 1 | Stuck check sync plan in StuckCheckDrain (Foreign, nojob) | 11 (+2 repro window) | 7 (+2) | 13,132 (+3,719) | measured |
| 2 | Steering obstacle query / avoidance A wait (own route job holds `+0x918`) | 5 | 0 | 1,829 | measured |
| 3 | Footprint waits: RemovePrivateFootprint 2, ObjectFootprintFromTool 2 | 4 | 0 | 1,297 | measured |
| 4 | Gate 12 GetIfPresent | 2 | 0 | 740 | measured |
| 5 | Gate 12 F-G placement (own-job wait 464 ms) | 1 | 0 | 499 | measured / join inferred |
| 6 | Anim `0x0085f6cb` wait | 1 | 0 | 413 | measured |
| 7 | Sim sync `ReplanFromPosition` (FollowRouteAction) | 1 | 0 | 389 | measured |

All sync plans of the run (nostart-report): stuck check 59 / 20.21 s / max 2019; ViewObjects `CreateRouteToObject` (Los) 13 / 0.98 s / max 100; `GoForWalkWithDog` PlanRouteIntoLot (Foreign) 9 / 0.96 s / max 208; GoToLot (Skipped/M1Pending) 5 / 0.50 s; 4 single plans < 100 ms [measured]. Reasons: Foreign 69, Los 13, Skipped 5, M1Pending 3.

## 5. Fix mapping per cause

| cause | matching built fix | state | expected effect |
|---|---|---|---|
| 1 Stuck check in drain task | **`stuckcheckown`** (ts3fixes `RouteBackoff.cs` `OwnerTaskStart`, `kStuckCheckOwn`, counters 253/254; docs/163 §7): own START from exactly this drain task, so the plan runs on the worker and only the drain sleeps | built 2026-10-24, off, offline only; repro draft `route-stuckcheck-own-loop` (BP) not run | removes all 9 natural + 2 repro-window >= 1 s freezes of this run and ~20 s of sim-thread plan time [inferred]. Secondary: `stuckchecknear` (fewer plans per check, same verdict) and `FallbackAsync=1` + T1 deadline (covers every Foreign plan but costs worker hold, docs/134/183 §1.1). `failrepeat` does not fit (different points per plan). If the 1-2 s plans are failed floods, docs/142 `[RouteEarlyOut]` acting mode would shorten them too [inferred, not checked]. |
| 2 Steering obstacle query / avoidance A | S2 clone (docs/84), `[AvoidRO]` (docs/137), steer-pass switches (docs/175) | built, off in release defaults as far as docs/175 says; their state was not read in this log | single freezes 300-450 ms; JobYield does not cover these sites (not C#-visible). |
| 3 RemovePrivateFootprint / ObjectFootprintFromTool | DeferFootprint (design U, on: `footprint defer: mode 1`), DeferFootprint2 (docs/89 phase 2) | DeferFootprint on; these sites are outside its deferral [inferred] | **no fix yet for RemovePrivateFootprint / ObjectFootprintFromTool**: idea = add both to the design-U defer queue (apply at the drain points) or a job-side yield of `+0x918` between search phases. |
| 4 Gate 12 GetIfPresent | `[SlotEnabledRO]` / design SH (docs/90) | built, off [inferred from docs] | small. |
| 5 Gate 12 F-G placement | `[PlacementRO]` (docs/174) | built, off | covers the 464 ms own-job wait. |
| 6 Anim `0x0085f6cb` | none | no fix yet | idea: resolve the site (docs/89 lists both animation passes as UpdateSimFootprint callers); likely the same footprint-defer family. |
| 7 Sync `ReplanFromPosition` | A10 async `Replan` (docs/87), `FallbackAsync` | built behind FallbackAsync (off) | one freeze. |
| own-job waits (2, < 500 ms) | `jobyield` (docs/183) | built, off | JobYield reaches only sites 0 and 3; these two are gate/footprint waits (site 1), so JobYield would not help here [inferred]. Long own jobs (486/318 ms) are the real lever: T1 deadline / job cap. |

## 6. ts3fixes fix activity

No ts3fixes counter dump in the run files (`stuckcheckasync_run`, `owchain_alarms` not found in the queue log, probe log, jsonl or native.json). Activity is visible only through task names in the profile: `StuckCheckDrain` in 159 of 3,107 hitch records, `OwChainTheirs` 172 mentions (93 records) [measured]. So both `stuckcheckasync` and `owchain` were active; owchain never led a freeze (2 ms in the 395 ms one). Counter values: not recorded; add a `patches` read at job end if wanted.

## 7. Not checked

- Whether the 1-2 s stuck check plans fail (failed floods, docs/142) or succeed: plan result not in the log line.
- Which own-START plans the 486/318 ms jobs were (own jobs log no script stack).
- Release-default state of S2 / AvoidRO / PlacementRO / SlotEnabledRO in 0.36.39 (not printed as mode lines in this log).
