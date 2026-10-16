# 64. ThreadedObjects stalls: which tasks run far over the 5 ms slice (T-TO)

Data only: existing captures in `probe-out/profile/` (managed C# samples, `-seconds.csv`, `-hitches.jsonl`), the verify rerun counters (`probe-out/queue/verify-2026-10-16-*-native.json`) and the four `/taskstall` runs of 2026-10-13/14 (`probe-out/*-taskstall.jsonl`). The game was not run for this doc. Builds on docs/28 G12 (ThreadedObjects is ~62-64% of sim-thread wall time), docs/36 §3.2 (the 5 ms budget is checked only between tasks), docs/59 §1 (autonomy), docs/04a §8 (route plan callers), docs/04 F18 (first `/taskstall` run) and the I-36-1 result in `docs/data/results/2026-10-15-verify-findings.md` (max 885 ms per call).

Tags: **[measured]** = counted from the captures with `tools/harness/stall-split.ts`; **[verified]** = read in the disassembly or decompile; **[inferred]** = reasoning, still to confirm.

## 1. Summary

- **The long tail is route search.** In ThreadedObjects slices of 200 ms or more, native route time (the `Route_*` ICs, timed in the same iteration) is **80.5%** of the slice time (618 slices in 20 runs); at 500 ms or more it is 71.6% (59 slices) [measured]. Managed samples taken inside such slices have a route plan as the innermost frame **44.8%** of the time, against **3.8%** far from any hitch [measured].
- **The 885 ms maximum (I-36-1, Isla Paradiso) is one task and one plan:** that iteration spent 885 ms in ThreadedObjects and 862 ms in a single `Route_Plan` call (1 plan in the iteration) [measured]. The loop only checks its deadline after a task returns (QPC import call at `0xD7FBCA`, after the per-task call) [verified bytes], so any part of a slice beyond 5 ms belongs to one task [inferred from the loop shape]. 8 of the 10 longest slices across all runs are the same pattern: 1-7 plans, 758-911 ms of plan time, some of them all failing (5 of 5 failed in a 900 ms slice) [measured].
- **The tasks are Sims running travel interactions:** "Sim interaction step" is 52-66% of samples in long windows against ~10% in the baseline. The interactions are going home (`GoHome` and our pass-through `GoHomeHook`), visiting community lots and lots, rabbit holes and restaurants, and Go Here With [measured]. The plans inside are `RoutingComponent.DoRoute` replans and **houseboat gangway re-plans** (House Boat Fix `EnterHouseboatThroughGangway` plus the game's `ExitHouseboatThroughGangway`: 33-38% of plan samples in long windows) [measured].
- **Not the cause of long slices:** autonomy search (3-5% of long-window samples vs 7-11% baseline, lift 0.3-0.5), SimUpdate (lift 0.3-0.7), story progression (under 1%), NRaas code inside ThreadedObjects (2-3%) [measured]. NRaas's big stalls are in **TaskObjects** (Overwatch/ErrorTrap `FunctionTask`s, 287-853 ms), not in this stage (docs/04 F18, §5 here).
- **World matters:** in Bridgeport slices of 100 ms or more hold 6-7% of ThreadedObjects time and only 39-40% of that is route time; in Isla Paradiso they hold 27-33%, and 79-89% of that is route time [measured]. Most ThreadedObjects time everywhere is in 20-100 ms slices (mean 19-22 ms per call, 4x the budget), which this data cannot split by task.

## 2. Method

`node tools/harness/stall-split.ts` (new, reads only; set `TS3_REPO=<repo>` when run from a git worktree):

- **`--slices`**: ThreadedObjects ms per `Simulate` iteration from every `-hitches.jsonl` with a `threaded` field (iterations of 50 ms wall or more only), bucketed; route ms and plan counts of the same iterations; per-call totals from the verify native counters `vfThrD7F8B0*` (live window, after - before).
- **`--stalls`**: every `/taskstall` record (stalls of 250 ms or more, docs/04 F18) with task class, category of the sampled stack and the first frame below the leaf that is not ScriptCore/SimIFace/System.
- **default (tasks)**: managed samples (`<tag>-managed.jsonl`, docs/20 §1) have no time stamp. The sampler runs from about 60 s into the run at about 5 Hz for `min(180, secs/2 - 60)` s, one line per sample in order (`profile-world.ts`, `managed-sample.mjs`). The script places sample j at `off + j x window / n` s after the first `-seconds.csv` row and picks `off` between 40 and 90 s where the per-second share of samples with a route plan leaf best follows the profiler's route ms/s (Pearson r). 14 of 16 runs land at 57-58 s with r 0.36-0.85; `sv-mod` (88.5 s, r 0.24) and `bp-nobudget` (43.5 s, r 0.31) are less certain. With `--pad P --long L` a sample is "long" when it falls inside an iteration with ThreadedObjects of L ms or more (P ms margin each side) and "baseline" when no hitch iteration is within 1.5 s. Without `--pad` the split is per second (long: an iteration of L ms or more in that second; normal: none of 50 ms or more).
- **Stage runs** have the `threaded` column (`bp-nobudget`, `bp-worker1`, `ip-jps-verify`, `mainstack-2026-10-16-bp-mainstack`; 1,916 samples). **Proxy runs** (12 older BP/IP/SV runs, 9,991 samples) only have the whole iteration time, which stands in for ThreadedObjects [inferred: ThreadedObjects is 59-88% of hitch wall time in the stage runs, measured].
- Categories (first match wins): no script; harness agent; NRaas; story progression; autonomy search (`AutonomyManager.Simulate` root or `Autonomy.RunAutonomy`/`TryFindBestAction`); Sim interaction step (`InteractionQueue.ProcessOneInteraction` or `Sim.DoInteraction`); other Sim task; `SimUpdate`; `Lot.Simulate` and reactions; alarms and one-shot functions; native event callbacks; else the task root (the method below `ScriptProxy.Simulate`).

Commands behind the numbers: `--slices`; `--stalls`; `--pad 100 --long 200`; `--pad 100 --long 100 <bp tags>`; default (`--long 100`, per second).

## 3. Slice length [measured]

| Live window (verify 2026-10-16, window not in front) | Calls | Mean | Max | Calls >= 100 ms | >= 200 ms | >= 500 ms | Share of stage time in slices >= 100 ms | Route share of those |
|---|---|---|---|---|---|---|---|---|
| BP | 10,145 | 19.2 ms | 211 ms | 112 (1.1%) | 2 | 0 | 7.1% | 39% |
| IP | 8,412 | 22.1 ms | 885 ms | 231 (2.7%) | 95 (1.1%) | 11 | 27.2% | 79% |

Calls, mean and max come from the native counters; the bucket counts from the hitch iterations (at most one ThreadedObjects call per iteration [inferred: 10,145 calls in 30,615 iterations in BP; the 885 ms matches on both sides]). Over all 20 runs with the stage column: 3,948 iterations of 100 ms or more (route 62.1% of their ThreadedObjects ms), 618 of 200 ms or more (80.5%; 489 at least 80% route, 45 under 20%), 59 of 500 ms or more (71.6%; 46 / 10). Plans per long slice: median 2; 259 of the 618 slices of 200 ms or more had exactly one.

Longest slices (all runs): 2,794 ms with no route at all (`t14-spatial`, the same iteration also had 509 ms of GC mark), then 978 ms (2 `Route_ReplanFromPosition`, 892 ms), 930 ms with only 19 ms of route (`bp-wab-routablecache`), 921 ms (7 plans, 875 ms), 918 ms (5 failing `Route_Plan`, 900 ms), **885 ms (1 `Route_Plan`, 862 ms: I-36-1)**, 833, 806, 803 (4 failing plans), 787 ms.

## 4. Top stall contributors [measured]

Share of managed samples. "Window" = inside a ThreadedObjects (stage runs) or whole iteration (proxy runs) of 200 ms or more, 100 ms margin; "per second" = seconds with a 100 ms slice vs seconds with none of 50 ms (stage runs).

| Category | Stage runs, window (117) | Proxy runs, window (1,209) | Proxy baseline, no hitch within 1.5 s (266) | Stage runs, long seconds (558) | Stage runs, normal seconds (286) | Route plan leaf, of the category (proxy window) |
|---|---|---|---|---|---|---|
| Sim interaction step | **65.8%** | **52.2%** | 9.8% | 41.2% | 20.3% | 79.1% (stage: 89.6%) |
| no script running | 8.5% | 14.5% | 49.2% | 20.8% | 29.0% | - |
| `Lot.Simulate` / reactions | 5.1% | 6.5% | 6.4% | 9.5% | 13.3% | 0% |
| `SimBuilderTask` (Sim/outfit builds) | 0.9% | 5.9% | 0.0% | 1.1% | 2.1% | 0% |
| autonomy search | 5.1% | 3.0% | 11.3% | 3.8% | 7.0% | 0% |
| `SimUpdate` (motives, buffs) | 4.3% | 3.5% | 10.2% | 6.5% | 8.7% | 0% |
| native event callbacks | 3.4% | 3.1% | 3.8% | 5.6% | 3.8% | 0% |
| alarms / one-shot functions | under 1% | 2.4% | 0.4% | 1.4% | 1.7% | 51.7% |
| NRaas code | 2.6% | 1.8% | 0.0% | 1.6% | 1.4% | 40.9% |
| our `StuckWatchdog` | 1.7% | under 1% | - | 1.3% | 1.0% | - |
| story progression | - | 0.7% | 0.0% | - | - | 0% |

All samples, pooled stage and proxy runs: route plan leaf **44.8%** inside slices of 200 ms or more (1,319 samples), 28.6% inside slices of 100 ms or more (300 ms margin, 4,262), **3.8%** with no hitch within 1.5 s (235).

**Interactions** in the windows (Sim interaction step only; proxy / stage): going home `GoHomeHook.Run` 17.4% / 42.9% plus `GoHome.Run` 6.3%; `VisitCommunityLot.Run` 10.9% (+ hook 2.5% / 6.5%); rabbit holes (`RabbitHoleInteraction`, `MetaRabbitHoleInteraction`, restaurant) 20.6% / 10.4%; `VisitLot.Run` 5.9% (hook 13.0% in stage runs); `GoToVirtualHomeInternal` 3.5%; Go Here With (`LeaderGoHereWith` 3.5% proxy, `GoHereWithHoldingInteraction` 13.0% stage). The `*Hook.Run` frames are ts3fixes pass-through wrappers that call the game's `Run` when no shortcut applies; the time is the game's plan inside [inferred from the frames below them].

**Plan callers** in the windows (plan leaf only; proxy 523 / stage 70 samples): houseboat gangway `EnterHouseboatThroughGangway` (House Boat Fix) 24.3% / 37.1% plus the game's `ExitHouseboatThroughGangway` 9.0% / 1.4%; `RoutingComponent.DoRoute` 22.8% / 38.6%; `Lot.PlanToLotEx` 9.8% / 5.7%; rabbit hole `RouteOutside` + `RouteNearEntranceAndEnterRabbitHole` 10.9% / 1.4% (+ `EnterOrig` 1.4%); `Lot.PlanRouteIntoLot` 2.7% / 10.0%; EA's 3 AM `RunStuckCheck` 2.7%; `Sim.IsPointInLotSafelyRoutable` 1.9%. These samples come mostly from Isla Paradiso, which has most of the long windows.

**Bridgeport only** (`--pad 100 --long 100`, BP runs): Sim interaction step 37.1% (stage) / 33.8% (proxy, 57% of it plans), `Lot.Simulate` 12.1% / 7.3%, `SimBuilderTask` 8.3% (proxy, 0% baseline), alarms 3.4% / 4.3%. Plan callers spread out (`DoRoute`, `PlanRouteIntoLot`, `RouteToSlot`, rabbit hole entry 10-17% each). So BP has fewer long slices (2 of 200 ms or more in the verify window) and they are only partly route; Sim/outfit builds and lot work fill the rest [measured, small sample: 116 stage samples]. The longest BP slices across all runs still reach 800-930 ms: failing plans (918 ms, 803 ms) and one with almost no route time (930 ms) [measured].

## 5. `/taskstall` records (stalls of 250 ms or more, 2026-10-13/14) [measured]

| Stage | Records | What the sampled stack shows |
|---|---|---|
| ThreadedObjects | 15 (5 ended before a sample) | 8 of 10 sampled end in a plan: `Lot` route via our `RouteBackoffHook` (1,069 ms), EA `RunStuckCheck` alarm (645, 296 ms), `NpcParty.TryGetPositionForSim` from an invitation alarm (433 ms), `DoRoute` in a virtual-home trip (410 ms), `SafeSpot` routability check (362 ms, since cached), Visit Community Lot replan (321 ms), Virtual Home plan (279 ms). Not route: a door `SacsRequest` (268 ms) and the kleptomaniac trait's `GetObjectProductListFiltered` in a rabbit hole (393 ms) |
| TaskObjects | 16 | NRaas `FunctionTask`s: Overwatch nightly Stuck Check (277-368 ms, route plans), Overwatch inventory/genealogy cleanups and ErrorTrap load-time dereference scans (384-853 ms, not route), NRaas reset tasks (345-589 ms); our harness `Ts3Agent.PollTask` writing JSON (1,409 ms, test-only) |

## 6. Is 885 ms one task or many?

One. The deadline is read only after each task returns (`0xD7FBCA`), and the over-budget part of a slice can only come from the task that was running when the 5 ms passed [verified call order, inference on the consequence]. The 885 ms iteration had one plan of 862 ms (97% of the slice) [measured], so it was one task waiting on one native route search. More generally, 42% of the slices of 200 ms or more held exactly one plan and the median is two, so a single search, sometimes with one replan, makes a long slice [measured].

## 7. Candidates

| # | Candidate | Targets | Evidence | Notes |
|---|---|---|---|---|
| S1 | **Cap a single route search** (ts3native route budget, docs/15 Phase 4; or the async plan, docs/18) | the whole long tail: 72-81% of slice time in slices of 200 ms or more is plan time | §3, §6 [measured] | Both exist and are off: the budget stranded Sims (41 stuck), the async plan is paused after three crashes (PLAN.md). A narrower first step: cap only plans that are retried anyway (`DoRoute` replans while following, plans to a lot that has already failed), so a capped search becomes "try again next tick" instead of a failed trip [inferred] |
| S2 | **Houseboat gangway re-plans** (Isla Paradiso) | 33-38% of plan samples in long windows | §4 [measured]; docs/04a §8 row 2 says the first plan is thrown away and redone as gangway legs [verified there] | House Boat Fix is third-party (no license): behaviour spec only. Options: skip the plan that is thrown away when the target is a houseboat lot, or reuse the gangway legs per docked boat; the docked-houseboat shortcut (b2) already avoids some trips |
| S3 | **Yield between plans in one task** (ts3fixes `Route.Plan` hook: after a plan that took more than ~5 ms, `Simulator.Sleep(0)` when `MayYield()` allows) | slices with 2 or more plans (most of the 359 slices of 200 ms or more without exactly one plan) | §3 [measured] | Splits multi-plan slices into one frame each; does not shorten a single long search. The yield guard from docs/04 F18 applies (no yield inside a nested native->managed call). Delays the Sim by a tick per plan |
| S4 | Route-fail work: plans that all fail (900 ms of 5 failing plans; 795 ms of 4) | the failing-plan slices | §3 [measured] | Our route-fail backoff (`RouteBackoffHook`) should already space these; check that it sees these callers |
| S5 | `SimBuilderTask` bursts (BP) | 6-8% of BP long-window samples, 0% baseline | §4 [measured, proxy runs] | Sim/outfit builds (fix #20 territory). That this task runs in ThreadedObjects and not TaskObjects is [inferred] |
| - | Autonomy search splitting (docs/59) | steady cost, not the tail | lift 0.3-0.5 in long windows [measured] | Keep for per-tick cost and the second-thread plan, not for stalls |
| - | NRaas settings | TaskObjects stalls, not ThreadedObjects | §5 [measured] | Existing `overwatch-no-stuckcheck` entry and the `OverwatchStuck` yield hook cover the nightly check; the ErrorTrap/Overwatch load scans are one-off |

No candidate here changes the 20-100 ms slices that hold most of the stage time; those need the per-task timer below.

## 8. Probe and repro to confirm

**Per-task timer at the ThreadedObjects loop** (ts3native counters, not Frida, to keep the cost low; reserve a counter slot in `docs/registry.md` first):

- `ScriptHost_ThreadedObjects` `0xD7F8B0`-`0xD7FC5D`. Calls inside it [verified bytes, `E8` targets]: stopwatch start `0x4F35B0` at `0xD7F91D` and deadline `0x4F34F0` at `0xD7F956`; **resume an existing task** `0xD7F000` at `0xD7FA74` and `0xD7FB53` (return addresses `0xD7FA79`, `0xD7FB58`); first start of a new task: method lookup `0xEA0FB0` at `0xD7FA86`, runtime invoke `0xE52D70` at `0xD7FAEC` (return `0xD7FAF1`); deadline check `call [0xF953B8]` (QueryPerformanceCounter) at `0xD7FBCA`; round-robin cursor `host+0x1700`/`+0x1704`; current task id `host+0xBD0`/`+0xBD4`, context `host+0xBC8` (docs/04 F18 `/taskstall` reads these). Which `0xD7F000` call is which branch, and that its first argument points to the task's 8-byte id, is [inferred from the decompile].
- Per call: QPC delta, task id and managed class (the `/taskstall` reader), route ms and plan count during the call (the route IC timer already in `stutter-profile.js`). Aggregate per class: calls, total, max, and calls over 5 / 50 / 200 ms; plus per ThreadedObjects call the number of tasks run and whether it ended on the deadline or on the cursor.
- Cheaper first look with no new code: `profile-world.ts --taskstall 100` on the IP verify save (the endpoint exists; it gives task and stack for every slice of 100 ms or more, and no managed sampler runs in that mode).

**Confirms the finding if:** in IP, tasks whose call time is at least 80% route time own most ThreadedObjects calls of 200 ms or more, and their classes are Sim tasks in travel interactions; in BP, the per-class table names what fills the 100-200 ms slices that are not route.

**Repro:** `ip-houseboat-gohome` (houseboat household going home; gangway re-plans) and `bp-route-async` (long plans) in `tools/harness/repros.json`. For S2 add a repro that sends a houseboat Sim to a community lot and back while the boat is docked; for S3 a Go Home from a far lot, where the task makes two or more plans.

## 9. Limits

- 5 Hz samples, aligned by correlation, not by clock: a sample can land up to about 0.5 s off. The window split sharpens as the margin shrinks (route leaf 28.6% at 300 ms margin, 44.8% at 100 ms), which supports the alignment [measured], but single small rows (under 2%) are noise.
- Stage runs are 4 runs, 117 samples inside slices of 200 ms or more; proxy runs use the whole iteration, which also contains TaskObjects and GC.
- "No script running" in long windows (8-15%) is native time with no interpreter frame on top, or margin samples just outside the slice.
- The verify runs had the window in the background (fps capped): counts and shares are valid, absolute times are not comparable with foreground runs.
