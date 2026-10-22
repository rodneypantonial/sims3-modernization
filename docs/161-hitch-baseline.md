# 161. Hitch baseline: what players notice, per world, and what leads it

Written 2026-10-22. **Data only (existing captures 2026-10-20..22), no game run.** Target = hitches a player notices (one Simulate iteration of 300 ms or more, and the 1 s+ freezes), not iterations per second. This ranking picks the weekend work.

## 1. Tool: `tools/harness/hitch-ab.ts`

- `node tools/harness/hitch-ab.ts <tag or prefix> ...`: per run and arm: iterations, minutes, p99, p99.9, max, counts of iterations >= 100/300/500/1000/2000 ms and the same per 10 min. Arms: an `--ab` run gives `on` / `off` (profile-compare.mjs rules: warm-up slice out, 15 s after each switch out, a last slice under half length out); any other run gives `all` (first 60 s out: load). Pairs: `<base>-offfirst` + `<base>-onfirst` = both orders (pair means, on - off with %); `<base>-off[-N]` + `<base>-on[-N]` = two loads. Runs whose profiler stopped early (< 80% of its seconds rows) are listed and left out.
- p99 / p99.9 use the **total** iteration count (`iters` in `-seconds.csv`): `-hitches.jsonl` holds only iterations >= 50 ms (profile-world.ts starts the profiler with `hitchMs 50`). When the rank falls below the slowest recorded iterations the value prints as `<50` (bounded, not measured). In play runs p99 is usually 50-110 ms and p99.9 100-250 ms; in quiet SV crowd runs p99 is `<50`.
- `--pool <label> [--arms off,all]`: one row with those arms of every given valid run pooled, real hitches only (§6), with the excluded table and the invalid runs (the old `--wh` harness-step filter is replaced by the §6 classifier).
- `--causes [--wh ...] [--arms off,all]`: ranking of >= 300 / >= 1000 ms iterations by the work that leads them (§3).
- `--self-check`: fixture (split, bounded percentiles, slices, causes, plan match) and the real run `clean-night-0.2.0-2026-10-21-bp-fb-play-onfirst` when `TS3_REPO` points at the data. Queue result notes can import `hitchReport(tags)`, `poolReport`, `causeReport`, `armStats`.
- `worst-hitches.ts --json` now also writes `roots` (task root per managed sample, taken before the 12-frame cut).

## 2. Baseline per world (release-default-like, v0.1.4 ini)

Set (all with the master/v0.1.4 ini, every v0.2.0 candidate off): `release-vs-baseline*` `-rel-N` runs (whole run), `release-validate-2026-10-20-*-rel-play`, `freeze-data-2026-10-20-*-frz-play`, and the **off arms** of `clean-night-0.2.0` A/Bs (fb, slotro, spreads, unseen, ut). Crowd/route-bits: `release-validate` and `freeze-data` `big-crowd` / `route-bits` repros, the `-off` load of `clean-night-0.2.0-*-fb-crowd` / `-fb-routebits`, off arms of `*-ut-crowd` / `*-unseen-crowd`. IP has no crowd/route-bits repro in this window. Daytime A/Bs (fbfix-ab, routesrc-ab, unseen-*) are left out: other defaults or a noisy machine.

**Real hitches only (2026-10-22, §6 classifier).** Every slow iteration is real or excluded; the counts, p99/p99.9, max and minutes below are over real ones in valid runs only. Excluded = points removed by reason (§6), with the number of excluded iterations >= 50 ms and, in brackets, >= 300 ms. Runs = valid runs of the set (invalid runs listed under the table). BP play includes `clean-night-0.2.0-2026-10-22-bp-fb-play-offfirst` (17 runs; the first table's 16 had already dropped the cut one).

| World / set | runs (valid of set) | iterations | real min | p99 ms | p99.9 ms | max ms | >= 100 | >= 300 | >= 500 | >= 1000 | >= 2000 | per 10 min (100/300/500/1000/2000) | excluded n (>= 300) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| BP play | 11 of 17 | 75,449 | 24.2 | 85 | 150 | 1,844 | 441 | 13 | 1 | 1 | 0 | 182.2 / 5.4 / 0.4 / 0.4 / 0.0 | 4,120 (16) |
| BP crowd/route-bits | 8 of 9 | 131,399 | 14.3 | 51 | 102 | 362 | 142 | 4 | 0 | 0 | 0 | 99.5 / 2.8 / 0.0 / 0.0 / 0.0 | 2,750 (25) |
| IP play | 9 of 11 | 79,362 | 21.4 | 115 | 252 | 1,190 | 1,134 | 43 | 6 | 1 | 0 | 529.9 / 20.1 / 2.8 / 0.5 / 0.0 | 2,605 (46) |
| SV play | 9 of 13 | 130,500 | 21.3 | <50 | 111 | 627 | 184 | 4 | 2 | 0 | 0 | 86.6 / 1.9 / 0.9 / 0.0 / 0.0 | 1,005 (12) |
| SV crowd/route-bits | 3 of 5 | 83,397 | 7.8 | <50 | 90 | 7,394 | 65 | 7 | 2 | 2 | 1 | 83.5 / 9.0 / 2.6 / 2.6 / 1.3 | 776 (6) |

Excluded by reason, all five sets (iterations >= 50 ms / >= 300 / >= 1000, run minutes dropped):

| Reason | n | >= 300 | >= 1000 | min dropped |
|---|---|---|---|---|
| first minute (load) | 1,613 | 30 | 6 | 12.0 |
| profiler edge (last 2 s) | 81 | 1 | 0 | 1.3 |
| repro-triggered (repro steps, test_env season/moon resets; game cost, not natural play) | 3,416 | 19 | 4 | 39.2 |
| sampler (managed / native windows) | 3,319 | 28 | 0 | 27.8 |
| probe call (agent upkeep round) | 2,827 | 27 | 2 | 25.9 |
| render-paced | - (publish gaps only, freeze-report.ts) | | | |

Invalid runs (15, left out whole): noisy machine (others >= 10% of the game CPUs) 14: `release-vs-baseline` bp-rel-1/3/4, sv-rel-1/2/3/4; `release-validate` bp-rel-play, ip-rel-play, bp/sv big-crowd, sv route-bits; `clean-night-0.2.0` bp-fb-play-onfirst, ip-fb-play-onfirst; profiler cut short 1: `clean-night-0.2.0-2026-10-21-bp-fb-play-offfirst` (89 of 600 s). No crash.

Reading [verified, data]: the ranking holds. IP play still has the most hitches (20.1 >= 300 ms per 10 min, 4x BP, 10x SV); SV crowds still have the worst freezes (max 7.4 s; 2.6 >= 1 s per 10 min, but from 3 valid runs and 7.8 min); SV play drops from 5.7 to 1.9 >= 300 ms per 10 min and its >= 1 s freezes all went out (in noisy `release-vs-baseline` runs, or repro/probe windows). IP's 10.1 s max was a noisy run; real max 1.2 s. The real minutes are ~30% of the profiled ones: the samplers, the agent's 30 s upkeep round and the repro windows take most of the rest, so new baseline runs should run without samplers (`--no-samplers`) when only hitch counts are wanted.

Command: `TS3_REPO=<main checkout> node tools/harness/hitch-ab.ts --pool "<set>" --arms off,all <tags of the set>` per set (the run list is the set above; `--wh` is no longer needed: the classifier decides). §3's cause ranking is still the first table's set and rules.

<details><summary>First table (2026-10-22 morning, before the classifier: every iteration after the first minute, harness steps >= 300 ms dropped)</summary>

| World / set | runs | iterations | min | p99 ms | p99.9 ms | max ms | >= 100 | >= 300 | >= 500 | >= 1000 | >= 2000 | per 10 min (100/300/500/1000/2000) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| BP play | 16 | 274,071 | 90.9 | 85 | 150 | 1,985 | 1,622 | 43 | 3 | 2 | 0 | 178.4 / 4.7 / 0.3 / 0.2 / 0.0 |
| BP crowd/route-bits | 9 | 365,426 | 41.8 | 54 | 111 | 362 | 515 | 10 | 0 | 0 | 0 | 123.2 / 2.4 / 0.0 / 0.0 / 0.0 |
| IP play | 11 | 214,651 | 55.3 | 112 | 252 | 10,106 | 2,868 | 120 | 13 | 3 | 2 | 518.8 / 21.7 / 2.4 / 0.5 / 0.4 |
| SV play | 13 | 421,613 | 73.7 | 52 | 118 | 2,518 | 707 | 42 | 12 | 5 | 1 | 96.0 / 5.7 / 1.6 / 0.7 / 0.1 |
| SV crowd/route-bits | 5 | 288,347 | 30.4 | <50 | 98 | 7,394 | 262 | 24 | 7 | 5 | 2 | 86.3 / 7.9 / 2.3 / 1.6 / 0.7 |

</details>

## 3. Causes of >= 300 / >= 1000 ms iterations (same set, 292 profiled minutes)

Each iteration goes to the largest of its disjoint shares (`causeOf` in hitch-ab.ts): sim-thread route plans (`route` minus the plan's wait for the job), route-job waits (`rtLock + rtOtherLock + rtJob + rtPlanWait`, named by `rtMaxKey`), GC (`gcMark + fin`), script (TaskObjects + ThreadedObjects + one-shot - nested route and waits), locomotion, animation, other (untimed). Sync plans get their reason from the run's `*-ts3native.log` `sync plan N ms, IC X, reason R` line of the closest length (within max(10 ms, 10%)): the log has no time stamp, so the match is by duration [inferred]; runs without `LogFallbackMs` say "reason not logged". Script iterations name the most frequent managed task root of the samples worst-hitches.ts aligned to them; most runs had no managed sampler then.

| # | Cause family | >= 300 ms | >= 1000 ms | worlds | Fix status |
|---|---|---|---|---|---|
| 1 | **Sync `Route.Plan` fallback, `NoStart` / `Foreign`** (logged) + unlogged `Route_Plan` | 21 (NoStart 10, Foreign 3, unlogged 8) | **12** (NoStart 8, Foreign 1, unlogged 3) | BP SV (crowd, route bits, play) | **Built + switch:** `[RouteAsync] FallbackAsync=1` + `M1Retry` (`--ab fallbackasync,m1retry`), `SyncCapMs` (1000 decided, docs/134 §10); NoStart callers ranked (docs/163, queue `nostart-fix-ab.json`). Pairs in progress (PLAN v0.1.5 targets 1-2). |
| 2 | **Script batch, TaskObjects stage** (no managed sample) | 74 | 0 | BP IP SV | Frame unknown. Built + switch candidates: `overwatchspread`, `easpread*`, `errortrapspread`, `unseenbroadcast/env/motives` (docs/144-147). Needs managed samples inside these hitches (sim-budget queue, docs/160). |
| 3 | **Sync replans on the sim thread** (`Route_ReplanFromPosition` 14, `Route_Replan` 10, `Route_PlanFromPoint` 3; reason not logged / no logged fallback) | 27 | 1 | IP (mostly), SV | `SyncCapMs` bounds them (built, every sim-thread sync plan); async ReplanFromPosition P5c parked, `asyncreplan` dropped (keyed +7.4); early-out observe probe built (docs/142); replan callers designed (docs/141). |
| 4 | **Locomotion** | 29 | 1 | BP IP SV | Built + switch: `[UnseenThrottle]` phase 1 (`--ab unseenthrottle`, docs/143), not yet a measured gain. |
| 5 | **Script batch, ThreadedObjects stage** (29 no sample; with a sample: `Sim.Simulate` 8, `FunctionTask.Simulate` 7, `SimUpdate.Simulate` 1, `Lot.Simulate` 1, no script 6) | 52 | 1 | BP IP SV | None specific; docs/74 hot-spot designs (Stereo, tooltips, lot detail) and docs/129 offload candidates. |
| 6 | Route-job waits by key (steering obstacle query 7, UpdateSimFootprint 3, gate 12 LotTable_Contains 3, avoidance A 2, ObjectFootprintFromTool 2, gate 12 F-G 2, `0x85F6CB` 2, `0x75A64C` 2, destroy 1, gate 10 1, gate 0 1, UpdateObjectLot 1, `0x85FE81` 1) | 28 | 0 | IP SV | Built + switch: SteerRO/S2, `[AvoidRO]`, `[SlotEnabledRO]`, DeferFP (design U), DestroyPark (design G); RouteToObjRO designed (docs/140). None leads a >= 1 s freeze. |
| 7 | `M1Pending` sync plans | 4 | 0 | IP | Built + switch: `M1Retry`. |
| 8 | Other (untimed) | 4 | 0 | IP SV | none |
| 9 | **GC** | 0 | 0 | - | Not a hitch leader in release-like runs [verified, data]. |

Crowd/route-bits alone: 5 of 5 >= 1 s iterations are `NoStart` sync `Route.Plan` (SV, 18.9 s in total). Play alone: >= 1 s = NoStart 3, unlogged Route_Plan 3, Foreign 1, ReplanFromPosition 1, locomotion 1, ThreadedObjects 1.

**Weekend order this gives:** (1) the fallback/NoStart family (12 of 15 >= 1 s iterations; fix built, finish the pairs and the NoStart callers); (2) a managed-sampled run per world to name the TaskObjects batches (largest >= 300 ms count, 74 + 52 script); (3) IP replans (27 >= 300 ms, IP's own hitch rate); locomotion and route-job waits after that; GC needs nothing.

## 4. Whole-window context (every run 2026-10-20..22, all variants)

`worst-hitches.ts --min 300 --since 2026-10-20` over 2,252 profiled minutes (all A/B arms, probes, repros): ThreadedObjects no route 868, route search 667, TaskObjects burst 680, first minute after load 601, harness step 234, locomotion 296, mixed 127, other 51. The same order as §3 (script and route lead by count; route leads the long ones), but it mixes fix-on arms and probe builds, so §2-§3 is the baseline.

## 5. Limits

- 292 release-like minutes: the >= 1 s counts are small (15 iterations); SV crowd's 5 come from 3 runs.
- Fallback reasons are matched by duration [inferred]; `release-vs-baseline` and `release-validate` had no `LogFallbackMs`, so their route-led hitches show the entry only.
- Harness steps are known only for iterations >= 300 ms (worst-hitches.ts classes); the >= 100 ms counts may include a few.
- `release-vs-baseline` runs had high other-CPU noise (PLAN 2026-10-21); counts of >= 300 ms iterations are less noise-sensitive than it/s but not immune.
- Ledger: docs/51 §253 (first table and causes), §258 (real vs excluded).

## 6. Real vs excluded: only hitches a player would see (2026-10-22)

Rodney's rule: a result contains only real hitches; every invalid point is excluded and listed. One shared classifier in `tools/harness/worst-hitches.ts`, used by `hitch-ab.ts`, `freeze-report.ts` and `worst-hitches.ts` itself:

- `exclusion(x, ctx)`: one slow iteration `[t - wallMs, t]` or one publish gap `[t0, t1]` (with its longest sim iteration and the boundaries it spans) gives `null` (real) or the first matching reason, in this order:
  1. **first minute**: up to the 60th seconds row (load).
  2. **profiler edge**: the last 2 s before the last seconds row (stop); the start edge is inside the first minute.
  3. **repro-triggered**: the test agent's own commands: every `repro ... step X` (SimRouteTo, `reflect_call @route.Replan`, `go_here`, `crowd_go`, `spawn_sims`, `set_speed`, `camera_pan`; not `wait`) from the end of the previous step (at most 15 s before the logged return, since a sync plan runs inside the call) to 10 s after; saves; `test_env guard` season/moon resets (3 s before to 11 s after). Also an iteration with 0 sim steps and >= 1 s (an unlogged save/load). These are game cost, but not natural play: their table row keeps n, ms, >= 300, >= 1000 and max apart from the play counts.
  4. **sampler**: the managed (minutes 1-4) and native (minutes 5-8) sampler windows. Both suspend the sim thread at 5 Hz, so every iteration >= 200 ms in a window overlaps a suspension: the whole window goes, not single samples. Old runs: from the queue log lines `managed|native sampler N s` (1 s stamps); new runs: `<tag>-samplers.jsonl` (`{name, t0, t1}` epoch, written by profile-world.ts, added for this).
  5. **probe call**: agent HTTP calls run on the sim thread. New runs: `<tag>-calls.jsonl` (`{cmd, t0, t1}` per `ag()` call in profile-world.ts, added for this; repro steps win the overlap). Old runs: the 30 s upkeep round (keep_alive, decline_requests, autopilot, sims_activity, patches) as 2.5 s before to 1 s after each `activity.jsonl` line, with profile-world's `t0` taken from the `upkeep.log` stamps (I-161-9). The clock-guard `time` / `test_env` calls of old runs have no stamp and are not excluded.
  6. **render-paced** (publish gaps only): no sim iteration >= 50 ms inside the gap: the screen waited on the render side (unfocused `Sleep(10)` / `SleepEx(30)`, S3SS publishes at most once per render frame, docs/41). With `[Harness] ForegroundRender=1` (another agent's switch; detected as `ForegroundRender=1` / `: 1` text in the run's `ts3native.log` or queue log) a gap over >= 2 publish-less iteration boundaries is a sim-side cause and stays real.
- Clock: QPC (profiler) = epoch - offset. freeze-report uses the exact offset from `frames.csv` (pad 0.2 s); hitch-ab uses the queue-log start + 3.8 s (pad 1.5 s); worst-hitches uses its boot fit when it has one.
- Rows (seconds) inside an excluded window leave the minutes, iterations and publishes; an excluded iteration or gap in a kept row also leaves the iteration / publish count, so p99 / p99.9 rank over real ones only. Frames (camera stutter) in excluded windows leave the frame stats.
- `runInvalid()`: whole run out of pair means and pools, named in the output: **profiler cut short** (< 80% of its seconds rows), **noisy machine** (`machine-load.ts summarize` not comparable = other CPU >= 10% of the machine or clock below 80% of its start, or mean `og` = other apps >= 10% of the game CPUs), **crash or abort** (no `done; game alive` in the run's queue-log segment). Invalid runs still print, marked `INVALID`.
- Output: hitch-ab per-arm rows gain `excluded n (>= 300)`; every report prints `Excluded` (reason | n | ms | >= 300 | >= 1000 | max ms | min dropped) and the invalid runs; freeze-report prints `RUN INVALID` and the same table for publish gaps; worst-hitches prints it on top and its game-only set = real.
- `--self-check` of all three has fixtures for every reason: worst-hitches (each window, priority, 0-step save, render-paced with and without ForegroundRender, cut / noisy (og, other CPU, clock) / crash / valid, table), hitch-ab (first minute, edge, sampler rows, repro hitch in a kept row, 0-step save, percentiles over real), freeze-report (render-paced, ForegroundRender keep, sampler rows, invalid run).

What it changed (§2): 15 of 43 baseline runs invalid (14 noisy, 1 cut short); real minutes ~30% of the profiled ones (samplers 27.8 min, repro windows 39.2, agent upkeep 25.9, first minute 12.0, of the five sets). In-window >= 300 ms rates per 10 min vs real: samplers 3.8 vs 5.4 (BP play), 19.3 vs 20.1 (IP), 1.9 vs 1.9 (SV): the samplers do not inflate the tail (I-161-7); the agent's upkeep round does in SV play (12.1 vs 1.9, 8 hitches, coarse clock; I-161-8). For new baselines: `--no-samplers`, a quiet machine, and the exact `calls.jsonl` spans.

**freeze-measure-2026-10-22-bp-freeze-pan** (rerun 12:05 UTC, asi.0329, ForegroundRender=1; the 11:40 run's files were overwritten by it): **run invalid**, noisy machine (others 13.7% of the game CPUs) and crash (the game died at 12:15:54 UTC at the profile end, no `done; game alive`). For reading only: 2.0 real minutes of 10; excluded publish gaps: sampler 1,682 (5 >= 300 ms, max 566, 4.9 min), repro-triggered (test_env resets) 456, first minute 261 (3 >= 300, 1 >= 1 s), probe call 93, render-paced 17 (all <= 58 ms), edge 7. In the real 2 min: no gap >= 300 ms, gap p99 129 / p99.9 207 / max 279 ms, 0.68 publishes per iteration. Presented frames: real 29.6 FPS; in the sampler windows 19.5 FPS (repro windows 21.0, first minute 37.6): the samplers slow the main thread too (coordinator: per-minute FPS 9.7-16.4 in sampler minutes; the main thread waits on render jobs the suspended sim thread's pump runs, docs/41, inferred). So sampler windows are excluded as a whole for gaps and for frames, and frames dropped are counted apart by reason (`excluded frames` in the frame line).

**Recommendation:** measurement jobs (hitch counts, freezes, frame times, pairs) run with `profile.noSamplers: true` (`--no-samplers`, both exist already); cause jobs (which frame leads a hitch: sim-budget, managed-sampled per world) keep the samplers and read causes only, never rates.
