# v0.1.5 release build: what still causes noticeable hitches (2026-10-24)

Read-only analysis of existing runs on `asi.rel015` (ts3native 0.36.37 + the v0.1.5 ts3fixes package). No game runs, builds or edits. Purpose: pick v0.2.0 targets (routing and script side; Mono offload out of scope).

**Single runs, noisy.** IP has only 7.5 real minutes (both IP runs are short and the `-rvb` runs lose most of their time to sampler windows). Every rate below is from 1-3 runs per world; treat counts < 5 as anecdotes.

## Inputs

| Run | World | Real min (freeze-report) | Tools |
|---|---|---|---|
| `release-015-extra-2026-10-24-bp-rvb-rel015` | BP | 2.3 | samplers (HEAVY), no HitchAttr |
| `release-check-2026-10-24-bp-rc-smoke` | BP | 4.1 | HitchAttr, RouteJobPhases, LogFallbackStack |
| `release-check-2026-10-24-bp-rc-soak` | BP | 38.6 | as smoke |
| `release-015-extra-2026-10-24-ip-rvb-rel015` | IP | 2.8 | samplers, no HitchAttr |
| `release-check-2026-10-24-ip-rc-smoke` | IP | 4.7 | HitchAttr etc. |
| `release-015-extra-2026-10-24-sv-rvb-rel015` | SV | 2.5 | samplers, no HitchAttr |
| `release-check-2026-10-24-sv-rc-smoke` | SV | 4.4 | HitchAttr etc. |
| `release-015-extra-2026-10-24-sv-soak-rel015` | SV | 33.2 | samplers, no HitchAttr, `LogFallbackStack=0` |

Commands: `node tools/harness/hitch-ab.ts [--causes] <tags>`, `node tools/harness/freeze-report.ts <tag> --min 300 --top 40` (the BP soak needs `node --stack-size=60000`: `Math.max(...fms)` overflows the stack on 137k frames), `node tools/harness/nostart-report.ts --filter 'rel015|rc-soak|rc-smoke' --jobs`, plus a one-off read of `tasks[]` in `-hitches.jsonl`. Repro-fired, first-minute, sampler and probe-call windows are excluded by the tools (docs/161 §6). Counts below are **visible freezes** (publish gaps, freeze-report.ts) >= 300 ms, classified by the longest iteration's cause, task names and route-job wait key [measured unless marked].

## Per world (visible freezes >= 300 ms)

### BP (45.0 real min): 17 freezes = 3.8 / 10 min, >= 1 s: 0

| Cause class | n | per 10 min | total ms | max ms |
|---|---|---|---|---|
| script / TaskObjects: `FunctionTask>OverwatchSpreadHook.OnTimer` (NRaas Overwatch alarm, one tick, yield at `OverwatchStuckHook.IsPointInLotSafelyRoutable`) | 16 | 3.6 | 5,514 | 463 |
| script: `ScriptCore.ScriptProxy: Dispose` (two disposes 148 + 139 ms in one iteration) | 1 | 0.2 | 303 | 303 |
| sync route plan / +0x918 wait / GC / other | 0 | 0 | 0 | - |

Also: 84 sync plans in the BP soak (5.6 s total, max 171 ms; all < 300 ms): traffic cars `LiteDriver.Simulate` 45 (Foreign), 3 AM stuck check 22 (Foreign), `RouteToSlot` 10 (M1Pending/QueueFull).

### IP (7.5 real min): 14 freezes = 18.7 / 10 min, >= 1 s: 0

| Cause class | n | per 10 min | total ms | max ms |
|---|---|---|---|---|
| +0x918 lock wait, holder = async route job: `ObjectFootprintFromTool` `0x8334CC` 2 (501, 338), gate 12 `LotTable_Contains` 2 (391, 311), gate 12 `GetIfPresent` `0x62FB74` 1 (320), gate 12 F-G placement `0x631F40` 1 (382) | 6 | 8.0 | 2,243 | 501 |
| script / TaskObjects: Overwatch alarm (as BP) | 3 | 4.0 | 946 | 325 |
| script / ThreadedObjects: `ScriptProxy>Sims3.Gameplay.Actors.Sim` run (277-406 ms to the next `RouteBackoffHook.AsyncWait` yield; one with a 145 ms `ReplanFromPosition`) | 2 | 2.7 | 957 | 573 |
| sync route plan, Sim's own: `Replan` 347 ms (`SACS.AcquireDriver`), `ReplanFromPosition` 242 ms (`PreBoatRouteCallback`) | 2 | 2.7 | 694 | 382 |
| script / ThreadedObjects: `ScriptProxy>Sims3.Gameplay.Core.WorldLot` (`Lot.Simulate`, 304 ms run) | 1 | 1.3 | 341 | 341 |

### SV (40.1 real min): 20 freezes = 5.0 / 10 min, >= 1 s: 4 (1.0 / 10 min)

| Cause class | n | per 10 min | total ms | max ms |
|---|---|---|---|---|
| sync route plan `IC Plan`, reason `Foreign`, entry `nojob` (`FallbackAsync=0` in the release); 6 of 9 have a Foreign sync-plan line, 3 (302-317 ms) only plan time; all 11 soak Foreign lines share one caller key `408F2827` (no stack: `LogFallbackStack=0`); the one with a stack (SV smoke, 301 ms) is the 3 AM stuck check | 9 | 2.2 | 8,981 | **3,681** |
| locomotion stage (anim lead, 246-370 ms of `locomotion`; wait key not recorded: HitchAttr off in the soak) | 6 | 1.5 | 2,491 | 588 |
| script / ThreadedObjects: Sim proxy 530 ms (to `RouteBackoffHook.AsyncWait`), `OneShotFunction` 421 ms (`NpcParty.ForcePlaceSim`) | 2 | 0.5 | 967 | 536 |
| script: `ScriptCore.ScriptProxy: Dispose` 497 ms | 1 | 0.2 | 518 | 518 |
| other (no timed stage, 609 ms iteration) | 1 | 0.2 | 614 | 614 |
| script / TaskObjects: Overwatch alarm | 1 | 0.2 | 372 | 372 |

GC: 0 freezes in any run (gcMark <= 2 ms in every iteration >= 300 ms) [measured].

### Render / frame service (camera stutter, not sim freezes)

Presented frames > 300 ms: 0 in every run. Frames >= 100 ms: BP soak 355 (~92 / 10 min), SV soak 222 (~67 / 10 min), max 253 ms; lead stage `services`, top service `CAS/SimService` (~185-207 ms per frame, Sim builds) in the soaks, `SwarmSystem` in the smokes [measured]. Below the 300 ms bar and engine-side (CAS builds = Apex Radiance's area, docs/42/169); listed for completeness.

## Top 10 causes overall (count x mean = total ms, all runs, 92.6 real min)

| # | Cause | Worlds | n | total ms | mean | max | >= 1 s | Existing fix ideas |
|---|---|---|---|---|---|---|---|---|
| 1 | Foreign sync `Route.Plan` (no START accepted, `FallbackAsync=0`); likely the 3 AM stuck check [inferred: same IC/reason/entry as the stacked SV-smoke and BP-soak stuck-check plans] | SV | 9 | 8,981 | 998 | 3,681 | 4 | `FallbackAsync`/`SyncCapMs`/`FbJobDeadline` (docs/134, docs/178 T1: built, no gain shown, not shipped); `stuckchecknear` + `failrepeat` (docs/163 §3-4, built, off); `stuckcheckasync` (shipped, but see #2 note: it did not engage in the soaks) |
| 2 | NRaas Overwatch alarm in one tick (`FunctionTask>OverwatchSpreadHook.OnTimer`, 250-360 ms per run) | BP, IP, SV | 20 | 6,832 | 342 | 463 | 0 | `overwatchchain` (docs/145 §11, **shipped on, but never engaged in natural play**, see below); `overwatchspread` (docs/145 §3, off); NRaas upstream PR #45 (docs/145 §5) |
| 3 | SV locomotion stage (wait key unknown) | SV | 6 | 2,491 | 415 | 588 | 0 | if steering `0x85FE10` waits (docs/178 §2 pattern [inferred]): lock-taker bundle `steerro,avoidro,...` (docs/178 T5, built, off); `[UnseenThrottle]` (docs/143, built, off) |
| 4 | +0x918 waits behind the async route job (`ObjectFootprintFromTool`, gate 12 `LotTable_Contains`/`GetIfPresent`/F-G placement) | IP | 6 | 2,243 | 374 | 501 | 0 | `deferfootprint2` (docs/89 sibling `0x8334A0`), `placementro` (docs/174), gate-12 lot-table: docs/84 / docs/178 T5 bundle; P5 world copy (docs/178 T8) |
| 5 | Sim ThreadedObjects script run (interaction work up to the next yield) | IP, SV | 3 | 1,494 | 498 | 573 | 0 | no fix designed (unnamed interaction body; needs managed sampling of the run) |
| 6 | `ScriptCore.ScriptProxy: Dispose` | BP, SV | 2 | 821 | 411 | 518 | 0 | no fix designed |
| 7 | Sim's own sync plans (`Replan`, boat `ReplanFromPosition`) | IP | 2 | 694 | 347 | 382 | 0 | A10 async `Replan` (docs/87, off), ForeignKinds/`a10foreign` (docs/176, in T5) |
| 8 | Unstaged 609 ms iteration | SV | 1 | 614 | 614 | 614 | 0 | none (unknown) |
| 9 | `NpcParty.ForcePlaceSim` one-shot | SV | 1 | 431 | 431 | 431 | 0 | none (docs/141 c-chain family "event/one-shot" [inferred]) |
| 10 | `WorldLot` `Lot.Simulate` run | IP | 1 | 341 | 341 | 341 | 0 | docs/74 (lot detail priority throttle, design), docs/144 unseen script throttle (built, off) |

## Key finding: two shipped ts3fixes switches did not engage in natural play [measured]

- **`overwatchchain`:** the BP soak has 85 runs of `FunctionTask>OverwatchSpreadHook.OnTimer` (17 >= 100 ms, max 361 ms, the docs/145 §11.1 off-arm shape) and **0** runs of `OneShotFunctionTask>Ts3Fixes.Fixes.OwChainTheirs`; SV soak 89 and 0. In chain mode `TimerHook` only queues `OwChainTheirs` and returns, so the mode at the natural alarm was not `Chain`. The repro that passed the release check (`rc-on-overwatchchain`: `OwChainTheirs` 3 runs, max 46 ms) flips the switch through the agent.
- **`stuckcheckasync`:** both soaks have 0 `StuckCheckDrain` task runs, and the 16 stuck-check stacks in the BP soak log run `RunOrig` inline under `AlarmTimerCallback.Invoke` (the sync path); `rc-on-stuckcheckasync` (switch flipped by the agent) shows `StuckCheckDrain` 10 runs.
- **Likely cause [inferred, not checked]:** both switches read `ShortcutOn(name)` = the runtime switch dictionary, else the tuning default; in natural runs nothing flips them, so the tuning in the package decides. Either the package's `kOverwatchChain`/`kStuckCheckAsync` tuning is not what the game reads, or the release check's agent sets the switches. `errortrapspread` uses the same mechanism and should be checked too. **If the published v0.1.5 package behaves the same, players get none of these three fixes.** Check first: the soak's `ts3fixes` applied log / switch dump (`/fixes` agent call or the `ts3fixes.switches` dictionary) on one natural load, and the ITUN in `ts3fixes.package` against `Instantiator.cs` field names.

## v0.2.0 candidates (ranked)

| # | What to build | Expected gain (freezes >= 300 ms per 10 min) | Confidence |
|---|---|---|---|
| 1 | **Make shipped ts3fixes switches engage without the agent** (find why `overwatchchain`/`stuckcheckasync` stayed off; add a natural-run check: soak pass needs `OwChainTheirs`/`StuckCheckDrain` task lines or the counters `owchain_alarms`/`stuckcheckasync_run` > 0) | BP 3.6 -> ~0, IP ~4 -> ~0, SV 0.2 -> 0 [inferred from the repro arms: chain runs max 46-84 ms]; possibly part of SV #1 (stuck check) | high on the cause (measured 0 engaged runs), medium on the fix; ~1-2 h + one natural BP soak |
| 2 | **SV Foreign sync plans**: first a 10-15 min SV run with `LogFallbackStack=16` to name caller `408F2827`; if it is the stuck check, #1 plus `stuckchecknear` (docs/163) moves it to a task; else `FallbackAsync` + T1 deadline (docs/178) for that caller | SV 2.2 -> ~0.5, >= 1 s 1.0 -> ~0.2 [inferred] | medium (caller unattributed in the soak) |
| 3 | **IP lock-taker bundle** (`deferfootprint2`, `placementro`, gate-12 lot-table read-only; docs/178 T5) natural IP A/B | IP 8.0 -> ~2-3 [inferred from docs/178: 22 of 32 IP freezes covered] | medium; IP sample here is 7.5 min |
| 4 | **SV locomotion attribution**: one SV soak with HitchAttr on, then T5 `steerro`/`avoidro` if the key is steering | SV 1.5 -> ~0.5 [inferred] | low until attributed |
| 5 | `ScriptProxy: Dispose` and Sim ThreadedObjects runs: attribution only (managed sampler on those iterations) | ~0.5-1 per 10 min each | low (no design) |

Not v0.2.0 hitch targets on this data: GC (0), render/frame service (no frame > 300 ms; CAS/SimService belongs to Apex Radiance).
