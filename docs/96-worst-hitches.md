# 96. Worst hitches (>= 300 ms) attributed from existing captures

Written 2026-10-18 from existing data only: every `probe-out/profile/*-hitches.jsonl` written 2026-10-16 to 2026-10-18 (55 profiler runs, 587 profiled minutes, 1,065 iterations of 300 ms or more), their `-seconds.csv`, `-simstack.jsonl` (native sim-thread stacks), `-managed.jsonl` (C# stacks), `-activity.jsonl`, `-rng.json`, `-ab.json`, the queue logs `probe-out/queue/<tag>.log` and the ts3native logs. The four `/taskstall` runs (2026-10-13/14) are used as older context only. The game was not run for this doc.

Tool: `node tools/harness/worst-hitches.ts [--min 300] [--since 2026-10-16] [--json out.json]` (set `TS3_REPO=<main checkout>` from a worktree; `--self-check` tests the classifier, the queue-log parser and both clock fits).

Tags: **[measured]** = counted from the captures with the tool; **[verified]** = read in the disassembly or our own source; **[inferred]** = reasoning, still to confirm.

## 1. Answer

The premise holds: most of the worst hitch time has no `Route_*` time in it. Over the three days, route search is **41.7%** of the game's own >= 300 ms time (harness steps left out). The other 58% splits into four causes, and two of them are cheap to fix [measured]:

| # | Cause | n | ms | Share of game-only >= 300 ms time | Rate | Already addressed? |
|---|---|---|---|---|---|---|
| A | Route search inside the iteration (route >= 50% of wall) | 457 | 189,039 | 41.7% | 46.7/h (BP 23, IP 434) | Yes: H6 async routing, refusal fallbacks (PLAN, docs/60 §2.3, docs/64) |
| B | **NRaas Overwatch daily alarm** in TaskObjects (`CleanupInventory` -> localized catalog names) | 207 | 104,099 | **23.0%** | 21.2/h, every ~1,560 sim-minutes | Partly: only the Stuck Check is off (`overwatch-no-stuckcheck`); docs/64 §5 called these cleanups one-off, they are daily |
| C | **First minute after a load** (all stages) | 137 | 74,881 | **16.5%** | ~1.4 s per load | No; never sampled (samplers start at 60 s) |
| D | ThreadedObjects without route time (our `SafeSpot` -> `FindGoodLocation`, catalog product lists, mods) | 107 | 45,859 | 10.1% | 10.9/h (IP 75) | No; half of its native samples are in routing-world code that the profiler's route timer does not cover |
| E | Locomotion stage (sim thread waiting inside ts3native) | 40 | 19,203 | 4.2% | 4.1/h, 39 of 40 in async-routing runs | Yes: steering lock, docs/81, docs/84 S2 |
| F | Mixed route + script (route 20-50%) | 35 | 13,024 | 2.9% | 3.6/h | as A |
| G | Other (no timed stage dominates) | 14 | 7,099 | 1.6% | 1.4/h | - |
| - | Harness steps (repro `save_game`, `place_objects`, `buy_open`, `cas_open`, ...) | 68 | 210,312 | (31.7% of all) | - | Excluded: test actions, not play |

The examples in the request: `ip-ab-routeasync` 2026-10-17 1,225 ms (threaded 1,216, route 0) is class D (no samples: `--ab` runs have no sampler window). `bp-p10` 790/569/904 ms are class C at 7, 19 and 40 s after the first profiler row, the same three bursts every seeded BP run has. `ip-p10` 1,280 ms is class C (43 s); 1,007 ms is class E (494 ms threaded + 508 ms locomotion, 5 native samples all waiting in ts3native).

## 2. Method

**Classes** (first match wins, per iteration; `classify()` in the tool): harness step (a repro or save step in the queue log from 3 s before the iteration to 5 s after, or `steps` 0 and >= 1 s); first minute (under 60 s after the first `-seconds.csv` row); route >= 50% of wall; TaskObjects >= 50%; locomotion >= 40%; ThreadedObjects >= 50% with route under 20%; ThreadedObjects >= 50% (mixed); other. Route is nested in ThreadedObjects/TaskObjects, so it is a share, not a stage.

**Clocks.** The profiler's `t` is QPC ms; simstack `t`, the queue log and the managed sampler start are wall clock. `epoch - QPC` is one constant per Windows boot [measured: fine fits of `ip-p10` (03:17 UTC) and `ip-p1b` (07:55 UTC) on 2026-10-18 differ by 2 ms; the four IP fits of 2026-10-17 agree within 40 ms]. Coarse offset: profile-world start stamp + 3.8 s - first row QPC (the first row lands 3.2-4.6 s after the start in every well-fitted run). Fine offset: for runs with simstack samples, the offset (coarse +-15 s, 10 ms steps) where samples inside route-heavy hitch iterations (route >= 70%) carry a route-search frame (`0x82C6E0`, `0x65E0D0`, `0x676260`, `0x63E340`, `0x61E500`, `0x620560`, `0x61FE10`) and samples inside route-free ones do not. IP runs score 25-40, BP runs 2-7 (few long plans), so a boot takes the median of fits scoring >= 15 and every run of that boot uses it: 39 runs are aligned this way. The 16 runs of the 2026-10-16 boot have no IP fit and keep the coarse offset (+-1 s: enough for queue-log events, too coarse for 200 ms samples; only `seeded-rng` and the two `s3pp-cache` runs among them have simstack samples).

**Samples per hitch.** Native: simstack samples (5 Hz, `secs/2` to `secs/2 + 180 s`) whose aligned time lies inside the iteration. Managed: `stall-split.ts alignRun` placement (uniform 5 Hz from ~57 s after the first row, per-run fit r >= 0.2), +-100 ms around the iteration [inferred placement; the NRaas lift of 246x in class B shows it lands]. Baseline: samples more than 1.5 s from any >= 50 ms iteration. Activity: the last 30 s Sim snapshot before the hitch.

## 3. Shape of the tail

| Wall ms | n (all) | ms (all) | n game-only | ms game-only |
|---|---|---|---|---|
| 300-500 | 774 | 283,989 | 743 | 272,229 |
| 500-1,000 | 244 | 159,492 | 234 | 152,836 |
| 1,000-2,000 | 24 | 30,777 | 18 | 23,043 |
| >= 2,000 | 23 | 189,259 | 2 | 5,097 |

21 of the 23 iterations of 2 s or more are harness steps: `save_game` (12-22 s, TaskObjects, `steps` 0) and repro `place_objects`/`remove_sims` (2-5 s). The two game-only ones are `seeded-rng` 2,981 ms (TaskObjects) and `ip-env-off` 2,116 ms (class D) [measured].

Stage share of the game-only >= 300 ms time [measured]: ThreadedObjects 67.7%, TaskObjects 23.4%, locomotion 4.8%, animation pose 0.7%, script tick 0.4%, GC mark 0.2%, finalizers/one-shot/publish ~0%, outside any timed stage 2.8%; route (nested) 41.0%. **GC is not a cause**: a GC mark runs in 3-14% of these iterations and adds 0.2% of their time.

## 4. Causes

### 4.1 B: NRaas Overwatch daily alarm (TaskObjects burst)

- **Signature** [measured]: TaskObjects 432 ms mean of 503 ms wall; 3-4 per 10-minute run in every run of both worlds; median gap 153 s in BP and 124 s in IP, which is **1,577 and 1,554 sim-minutes** at each run's average sim rate (p25-p75 1,470-1,636). Same period in sim time across worlds with different wall periods, so it runs on the sim clock, about once per sim-day [inferred from average rates; the captures hold no sim clock per hitch].
- **Managed** [measured]: 24 of the 27 bursts with managed samples have NRaas on a sample, 23 of them `NRaas.OverwatchSpace.Alarms.CleanupInventory.PrivatePerformAction` (task root `FunctionTask.Simulate`, under `NRaas.Overwatch.OnTimer` / `AlarmOption.PerformAlarm`). Leaf `ScriptCore.UserToolUtils.UserToolUtils_ProductGenericImpl` in 35% of samples (baseline 0%), reached from `Inventories.CheckInventories` -> `Inventories.GetName` -> `GameObject.GetLocalizedName` -> `GameObject.get_CatalogName` -> `BuildBuyProduct.get_CatalogName` -> `UserToolUtils.GetProductCatalogName` (28 of 31 such samples).
- **Native** [measured]: `IC_UserToolUtils_ProductGenericImpl` 57% inclusive (baseline 0%), `STBL_Find 0x59AF50` and its caller `0x59CB00` 49%/37% (docs/54: one probe per string table on a miss, so heavy store content makes every miss slower), `Lock_Acquire` 38% (baseline 14%).
- This matches docs/07 §2.2 (Overwatch's `OnTimer` runs every alarm option in one `FunctionTask`, once a day at the alarm hour) [verified there] and the 2026-10-14 `/taskstall` lines (NRaas `FunctionTask`s with `get_CatalogName` below the leaf, 384-525 ms). docs/64 §5 filed the inventory cleanup with the one-off load scans; it is daily.
- **Fix B1 (config, no code):** turn Overwatch's inventory cleanup off in our generated Overwatch tuning (`tools/setup/build-overwatch-tuning.ts`, today only the Stuck Check), or document the in-game toggle. Whether V123 exposes a per-option switch for `CleanupInventory` is [inferred]; read it in the V123 decompile (as docs/07 did for the Stuck Check). Effort 0.5 day. Gain cap: up to 23 of 27 sampled bursts carry it, so <= ~9 s/h of >= 300 ms time and ~18 of the 21 events per hour [inferred: other alarm options in the same task still run].
- **Fix B2 (code):** a catalog-name cache (product key -> name) in Ts3Fixes on `GameObject.get_CatalogName`, or native around `GetProductCatalogName`. It also covers the kleptomaniac/product-list hits in class D (§4.3). Effort 1-2 days. S3PP's `LocalizedStringCache` (docs/69 #4) caches `LocStrSvc_GetString_A`, not this product path [inferred from the stacks: `STBL_Find` is reached through `0x59CB00`, not through the S3PP-hooked getter].

### 4.2 C: first minute after a load

- 137 iterations, 74.9 s, ~1.4 s per profiled load (53 runs) [measured]. BP: 47 ThreadedObjects iterations without route time at **6-8 s, 16-19 s and 36-40 s** after the first row (30.6 s), the same three bursts in every seeded BP run, plus 19 TaskObjects ones near 10 s (12.6 s). IP: 38 route-search iterations (16.3 s), 14 ThreadedObjects without route (6.1 s), 8 locomotion (3.3 s), 4 TaskObjects (3.4 s).
- No native or managed sample covers them: the managed sampler starts at 60 s and the native one at `secs/2`. Queue-log neighbours are only the harness's own start lines (`test_env`, `seed_rng`, sampler start).
- Context [inferred]: the 2026-10-14 `/taskstall` run caught NRaas `OnDelayedWorldLoadFinished` work in TaskObjects (Overwatch `CleanupTimers`, `CleanupGenealogy`, ErrorTrap dereference scans, 384-853 ms), which fits the BP TaskObjects group near 10 s. The BP ThreadedObjects triple is unexplained: deterministic with seed 12345, so it is game or mod work queued at load, not noise.
- Player view: once per load, ~1-1.5 s of hitches in the first minute; a player who loads often sees it more than any daily cause.
- **Capture (cheap, settles it):** one BP and one IP `profile-world --seed 12345 --minutes 5 --taskstall 250` from a fresh load (S3PP `SimulatorStageProfiler` off, as `--taskstall` requires). `/taskstall` starts before the run window and records each stage stall >= 250 ms with its task and managed stacks. Fix and effort follow from it.

### 4.3 D: ThreadedObjects without route time

- 107 iterations, 45.9 s, mostly IP (75) [measured]. "No route time" does not mean no routing work: **20 of the 40 native samples are in the routing/world-query code range** `0x610000-0x690000` (planner, A*, height queries; docs/28 N3/N10) reached through ICs the profiler does not time as route (`World_FindGoodLocation`, `World_ArePointsConnectedByLand`) [measured; range inferred from docs/28]. The 2,116 ms `ip-env-off` iteration has 10 of 11 samples there, 3 in the `FindGoodLocation` core (`0xB46B20`/`0xB470C0` family).
- **Managed** [measured]: 11 of the 21 sampled iterations have our **`Ts3Fixes.Fixes.SafeSpot` -> `GlobalFunctions.FindGoodLocation`** on a sample, called from `RabbitHoleHook.RouteNearEntranceAndEnterRabbitHole` (26 samples under `RabbitHoleInteraction.Run`; leaf `World_FindGoodLocation` 40% of class samples, baseline 0%). Others: `simler90Extras` stray-pet check -> `ArePointsConnectedByLand` (3), IP `UnchartedIslandMarker` `Query_CountObjectsOnLot` (4), the catalog product list `IC_UserToolUtils_GetObjectProductListFilteredImpl` with `STBL_Find` (3 iterations, 739/603/309 ms; native 0x59AF50 chain).
- `SafeSpot` (`mods/ts3fixes/src/Fixes.cs`) caches the routability verdict per 0.5 m cell, but it runs `FindGoodLocation` again on every try (up to 2 verified spots + 3 fresh ones per call) [verified, our source]. In IP that search alone can cost hundreds of ms [measured above].
- **Fix D1 (ours):** keep the `FindGoodLocation` result with the verified spot (position + facing per lot and route profile, one sim-day like the verdicts) and reuse it, so a verified spot needs no new search; stop after the first failed fresh search. Effort 0.5-1 day, plus a `verify-shortcut` run. Gain cap: ~half of class D, <= ~2.4 s/h, ~5 events/h, IP [inferred].
- **Fix D2:** B2's catalog-name cache also removes the product-list cases.
- **Tool gap:** the profiler times `Route_*` only. Timing `IC_World_FindGoodLocation 0x78AE70`, `...AtCustomDepth 0x78B0A0` and `ArePointsConnectedByLand` as a separate bucket in `stutter-profile.js` would split class D without samples (one normal run).

### 4.4 E: locomotion waits

- 40 iterations, locomotion 347 ms mean; 13 of 14 native samples have an `ntdll` leaf under ts3native frames (wait), callers `0xE70D30`/`0xEA1400` and once the steering clone `0x85FD10` -> `RoutingWorld_ObstacleQuery 0x82EDB0` [measured]. 39 of 40 are in runs with async routing on for at least part of the run; same build, `h6-m1 ip-env-on` 14 vs `ip-env-off` 0 [measured]. In the `routeasync` A/B: 3 on, 0 off.
- This is the docs/81/docs/84 steering-lock wait (the job holds `+0x918` while the locomotion obstacle query wants it). Addressed by design S2 / steer read-only (docs/84 §3, `[SteerRO]`); the PLAN numbers for S2 Mode 1 showed it slower overall, so this class is the one it should shrink. Gain cap ~2 s/h [measured class size].

### 4.5 A: route search

Known (docs/64: 80% of >= 200 ms ThreadedObjects slices). New numbers here: 434 of 457 are IP; managed samples sit in `RouteBackoffHook.PlanAsync` (39), LynMods `VisitCommunityLotLabLecture` (11), `GoHomeHook` (9), rabbit holes (6). The `routeasync` A/B (31/34 min on/off, first minute and warm-up slices out) cut this class from 24.9 to 8.3 s/h, while class D went from 2.5 to 8.2 s/h and E from 0 to 2.2 s/h [measured, small counts: 8 vs 4 and 3 vs 0 iterations]. So async routing moves part of the tail into waits and untimed world queries; the net is still -8.7 s/h for these three classes (18.7 vs 27.5 s/h).

### 4.6 Harness steps

68 iterations, 210 s, 31.7% of all >= 300 ms time [measured]: the repro steps of `rt-s2b-sweep` and `verify-*` (`save_game`, `place_objects`, `remove_sims`, `buy_open`, `cas_open`, `moodlet`, `thumbnails`). They are real game costs of those actions (save: docs/85 and the save docs), but they are not play stutter; p99 and hitch counts of runs with `--repro` include them, so compare such runs only with the same repro.

## 5. Correlation with events

| Event | Evidence | Verdict |
|---|---|---|
| GC | GC mark in 3-14% of iterations, 0.2% of their time | not a cause [measured] |
| Save | only the harness `save_game` steps (12-22 s each) | separate topic [measured] |
| Autonomy bursts | managed "autonomy search" lift 0.2-0.4 in every class | not a cause of the worst iterations [measured] |
| Time of day | class B on the sim clock, once per ~sim-day | yes, B [measured period, inferred alarm] |
| Load | class C, 16.5% | yes [measured] |
| Lot load / streaming | ts3native lot stream columns are 0 in these runs (counters not active) | no data |
| Travel | no travel step in these runs | no data |
| New Sims spawning | the 30 s activity snapshots change their Sim count around most iterations of every class (91% of route, 86% of B), so the signal does not separate classes | no signal at 30 s resolution |
| Interactions (activity lift) | "Kiss"/"Be Smooched" x206 in class E (7 iterations, one couple), rabbit-hole visits in D; no class-wide interaction | weak [measured] |
| A/B switch | IP class B bursts drift 1-20 s from the 120 s slice switches | not the switch [measured] |
| ts3native watchdog | `watchdogs 0` in every log; no per-line time stamps | no data |

## 6. Fixes ranked

| Rank | Fix | Class | Gain cap [inferred] | Effort (days) | Status |
|---|---|---|---|---|---|
| 1 | B1: Overwatch inventory cleanup off (tuning XML / setting) | B | <= ~9 s/h of >= 300 ms time, ~18 events/h | 0.5 | new |
| 2 | D1: reuse `FindGoodLocation` results in `SafeSpot` | D | <= ~2.4 s/h (IP) | 0.5-1 | new (our code) |
| 3 | Capture for C, then fix | C | up to ~1.4 s per load | 0.5 capture + ? | new |
| 4 | B2: catalog-name cache | B, D | B's remainder + D product-list cases | 1-2 | new |
| 5 | Steering read-only / S2 | E | ~2 s/h | in progress | docs/81, 84 |
| 6 | Sync-fallback refusals in async routing | A | most of the remaining 8.3 s/h with async on | in progress | PLAN, docs/60 |

## 7. Captures that would settle what the data cannot

1. **First minute (C) and the unsampled D/E cases:** BP + IP `--taskstall 250 --minutes 5 --seed 12345` from a fresh load (§4.2). Also gives task classes for the `--ab` runs' class D iterations, which have no samples.
2. **B1 check:** one 10-minute seeded BP run with the cleanup off; pass when TaskObjects >= 300 ms iterations drop from 3-4 to 0-1 per run (`worst-hitches.ts`, class B count).
3. **D split without samples:** a `FindGoodLocation`/`ArePointsConnectedByLand` bucket in `stutter-profile.js` (§4.3), then any normal IP run.
4. **B period:** the agent's `time` at each TaskObjects >= 300 ms iteration (or moving the Overwatch alarm hour) would turn the sim-day inference into a measurement.

## 8. Limits

- Native samples cover only the `secs/2` window (3 min per 10-minute run) and no `--ab`/`--repro` run; 95 of the 997 game-only iterations have one. Managed placement is a per-run fit, not a time stamp.
- 16 runs of 2026-10-16 use the coarse offset (+-1 s); their queue-log matching is fine, their sample matching is not used.
- One tag was reused for two lengths (`verify-2026-10-16-*` at 420 and 480 s); the tool keys those runs as `tag@secs` and matches each to its own `--minutes` line.
- Activity snapshots (30 s) and the run-average sim rate are too coarse for per-hitch game-time or spawn attribution.
