# 168. Hitch confidence, natural vs forced play, scenario costs and profiler cost

What a hitch or freeze A/B can and cannot show, how often players see freezes in plain play, what each common player action costs on its own, and what the measurement itself costs. Tools and queues are built and dry-run; **no game run yet**.

Tags: [verified] = self-check fixture or existing captures; [inferred] = to confirm in game.

## 1. Confidence in pair results (`hitch-ab.ts`)

`node tools/harness/hitch-ab.ts <tags>` now prints a **Confidence** table under the pair table, and `--pool` prints one under its row:

- Events: iterations >= 300 / 500 / 1000 / 2000 ms (real only, docs/161 §6) and **visible freezes** (real publish gaps from `<tag>-<secs>-freezes.jsonl`, the same rules as `freeze-report.ts`: `exclusion()` with the longest iteration inside, else the row's reason) when every run of the pair has a freezes file.
- Per arm: count, rate per 10 min (pool: per hour and per 10 min) with the **exact Poisson (Garwood) 95% interval** over the arm's real minutes.
- On vs off: **difference** per 10 min with a Wald 95% interval, and **rate ratio on/off** with the exact conditional (Clopper-Pearson) 95% interval. Counts and minutes are pooled over the pair's runs (both orders: the two `--ab` runs; two loads: `-off-N` + `-on-N`, plus all two-load pairs of one base pooled).
- **Verdict rule:** fewer than `MIN_EVENTS = 10` events in both arms together = `not enough events`, not read. Otherwise `fewer with on` when the ratio's upper bound is < 1, `more with on` when its lower bound is > 1, else `no clear difference`. Why 10: below it the exact ratio interval spans 1 for every split except the most lopsided ones (3 vs 7 gives 0.07-1.9), so a "result" there is one or two hitches moving.
- [inferred] Slow iterations cluster (one stall re-plans several Sims; a burst of TaskObjects work spans iterations), so a Poisson interval is somewhat too narrow. The both-orders pair agreeing is the practical check; an overdispersion test on natural-play per-minute counts is ledger I-168-1.
- An `it/s` column is added to every run row (iterations per real second).

Self-check fixtures [verified]: Garwood k=0 -> [0, 3.689], k=10 -> [4.795, 18.39], k=2000 without overflow; Clopper-Pearson 5/10 -> [0.187, 0.813]; 2 vs 20 events in 100 min = `fewer with on`, 3 vs 4 = `not enough events (7 < 10)`, 30 vs 33 = `no clear difference`, 40 vs 10 = `more with on`; freeze split (render-paced gap out, gap with a 350 ms iteration in); scenario labels. On the existing 2026-10-21 `clean-night-0.2.0` pairs every row reads `not enough events` (2-4 events >= 300 ms per pair).

### Runs needed (`hitch-ab.ts --power [--reduction 0.3] [--arm-min 3.5]`)

Two-sided 5%, power 80%, normal approximation on the log rate ratio, from the docs/161 §2 real-only baseline; one 10 min `--ab` run (slice 2) gives ~3.5 real min per arm, a both-orders pair = 2 runs.

| World / set | >= 100 ms (per 10 min -> runs, 30% / 50% cut) | >= 300 ms | >= 500 ms | >= 1000 ms |
|---|---|---|---|---|
| BP play | 182.2 -> 3 / 1 | 5.4 -> 80 / 26 | 0.4 -> 1071 / 351 | 0.4 -> 1071 / 351 |
| BP crowd/route-bits | 99.5 -> 5 / 2 | 2.8 -> 153 / 51 | none | none |
| IP play | 529.9 -> 1 / 1 | 20.1 -> 22 / 7 | 2.8 -> 153 / 51 | 0.5 -> 857 / 281 |
| SV play | 86.6 -> 5 / 2 | 1.9 -> 226 / 74 | 0.9 -> 476 / 156 | none |
| SV crowd/route-bits | 83.5 -> 6 / 2 | 9.0 -> 48 / 16 | 2.6 -> 165 / 54 | 2.6 -> 165 / 54 |

Reading [verified, arithmetic on docs/161 rates]: a single both-orders pair (2 runs) can only show a change in the **>= 100 ms** count. A 30% cut of **>= 300 ms** iterations needs ~22 runs in IP and 80 in BP; **>= 1000 ms** freezes are too rare for any A/B in natural play. Consequences: (a) A/B verdicts on long hitches must come from **forced scenarios** (crowds, route-bits, the §3 jobs) where the event rate is high, or from pooling every clean night's off arms against the on arms; (b) a fix aimed at rare multi-second freezes is judged by its mechanism (counter + repro), not by a hitch-count A/B (rare-case-fixes-bundle rule); (c) natural-play rates (§2) are the reporting number, not the test.

## 2. Natural vs forced: `tools/harness/queues/natural-play.json`

Three jobs, 30 min plain play each (BP, IP, SV `Baseline-*-Spring`), no repro, camera untouched, seed 12345, the measurement settings of `freeze-measure.json` (`asi.0329` all-on set, `[Harness] ForegroundRender=1`, `[HitchAttr]`, `RouteJobPhases` + `SyncPlans`, `LogFallbackMs 20` / stack 16, `profile.noSamplers`, frames.csv session kept loaded, per-task attribution on by default). ~35 min per job, ~1 h 50 min, clean night only.

Read per world: `node tools/harness/hitch-ab.ts --pool bp natural-play-<date>-bp` (real minutes, iterations and visible freezes >= 300/500/1000/2000 ms **per hour with 95% CIs**) and `freeze-report.ts <tag>` (FPS, causes). One 30 min job gives ~28 real min. At the docs/161 >= 300 ms rates that is ~56 events in IP (120 per hour, 95% interval about -25%/+30%), ~15 in BP (32 per hour, -44%/+65%) and ~5 in SV (11 per hour, -68%/+133%); >= 1000 ms freezes will mostly read 0-1 per world. Pool nights for tighter numbers.

## 3. Scenario costs: `tools/harness/queues/scenario-hitches.json`

One 10 min hitch-measured repro job per player scenario (Bridgeport, same measurement settings, seed 12345). Each repro (`tools/harness/repros.json`) starts with a 75 s lead-in (`time`), because the load minute is excluded before anything else; then the scenario, then natural play to the end of the window.

| Job / repro | Steps (agent commands; labels in the report) | Note |
|---|---|---|
| `scen-travel` | `travel_home` after `profile.travel france` | the trip out runs before the window on a save-matrix `Matrix-*` copy (never a Baseline); the trip home with its load is the scenario [inferred: the profiler stays attached across the world switch, I-168-4] |
| `scen-lotswitch` | `sim_visit_lot far` / `home` (vehicle), twice | active lot change |
| `scen-buildbuy` | `build_mode` (enter), `enter_live_mode` (exit), `buy_open`, `buy_close`, twice | build exit uses `enter_live_mode` (same `TransitionToLiveMode` as `build_mode on:false`) so enter and exit get separate labels |
| `scen-cas` | `cas_open`, `cas_close`, three times | |
| `scen-edittown` | `reflect_call_async Sims3.Gameplay.GameStates.TransitionToEditTown` (enter), `enter_live_mode` (exit), `set_speed ultra`, three times | no new agent action needed: the static transition runs as a game task; Edit Town pauses the clock [inferred, I-168-3] |
| `scen-save` | `save_game Scenario-Scratch`, twice | scratch name only |

No ts3agent change was needed: every step uses an existing command.

`node tools/harness/hitch-ab.ts --scenario scenario-hitches-<date>` lists, per run and step label, the iterations >= 100 / 300 / 1000 ms, max, total ms and the freezes >= 300 / 1000 ms inside the harness-triggered windows (step, from the end of the previous step or at most 15 s before its return, to 10 s after; `worst-hitches.ts runContext`), plus 0-step iterations >= 1 s (`save/load`), beside the same run's natural >= 300 ms rate. These are the windows the real-only tables exclude; here they are **the scenario's own cost**, labelled, never mixed into natural play. A window ending inside the load minute is the lead-in, not counted. Tried on `freeze-data-2026-10-20` [verified]: `crowd_go`, `go_here`, `test_env guard`, `set_speed` windows listed per run.

Causes inside the scenario (2026-10-23): `--scenario` (and `freeze-report.ts <tag> --repro-causes`) also prints, per arm (loop firings) or per step window, a top-causes table and the worst 10 of the iterations >= 300 ms inside them, with `worst-hitches.ts cause()` columns (class, lead bucket, route-job wait key, lock holder, slowest script runs) plus a sync plan joined by duration from the ts3native log (`route phases` entry/reason and top phase, else `route async` reason) [inferred join] and the slowest frame's longest service. First minute, profiler edge, sampler, upkeep probe calls, 0-step save/load and invalid runs still leave them out; the repro's own step commands and repro-loop's quiet-wait `ping`s are agent calls too and do not exclude here.

## 4. Profiler cost control: `tools/harness/queues/profiler-cost.json`

New switches:

- `tools/frida/stutter-profile.js` `-P {"stages":false}`: iteration boundary, catch-up steps and publish gaps only; no stage, GC, route or task hooks (tasks forced off). Seconds rows, hitch lines (`wallMs`, `steps`, `n`) and `freezes.jsonl` stay complete, so exclusion, counts and freezes work; causes do not (no stage split). `-P {"tasks":false}` (docs/166) stays for tasks alone. Dummy `stutter-tasks-test.py` still PASS after the restructure (task-only path); the `stages:false` path itself is not dummy-tested (it needs the game's hook table) [inferred, I-168-2].
- `profile-world.ts --prof-opts <JSON>` merges fields into the profiler's `-P` (out and hitchMs stay the harness's); queue `profile.profOpts: {...}`.

Queue: four Bridgeport jobs, same save and seed, camera untouched, ABBA: `bp-profcost-off-1` (lean), `-on-1` (full = `freeze-measure.json bp-freeze-still`), `-on-2`, `-off-2` (lean). Lean = `stages:false` + `[HitchAttr] Enabled=0` + `RouteJobPhases Enabled=0, SyncPlans=0`; frames.csv on in both (the frame log is what the FPS number comes from). Read: `hitch-ab.ts profiler-cost-<date>-bp-profcost` (it/s per run, two-load pairs -1 and -2 = both orders, and the pooled pair with CIs) and `freeze-report.ts <tag>` per run (FPS, frames > 50/100 ms, freezes). ~50 min, clean night. Expected [inferred]: lean it/s higher by the stage hooks' cost (~15 Frida hooks per iteration plus ~0.3 us per task run, docs/166 §3); if the gap is > 2% it/s, measurement runs should switch to lean and use full only for cause runs.

## 5. Next and open

- Clean night: `natural-play.json` (1 h 50 min), `scenario-hitches.json` (~1 h 20 min), `profiler-cost.json` (~50 min). Game runs: 13 jobs.
- Ledger §263: I-168-1 (overdispersion), I-168-2 (`stages:false` output complete), I-168-3 (Edit Town via `reflect_call_async`), I-168-4 (profiler across the world switch), I-168-5 (lean vs full cost).
- `freeze-report.ts` still owns FPS and frame stats; `hitch-ab.ts` adds counts, CIs and scenario cost only.

## 6. Addendum: repro loops that stop at a fixed event count (Rodney 2026-10-22)

Repro runs and repro A/Bs end once enough data exists, not after a fixed 10 min. Built and self-checked; **no game run yet**.

**Rule (fixed count, no optional stopping):** the stop count N is written in the repro before the run and the run stops when the count of events reaches it, never when a difference looks significant. Checking a p-value or interval after every firing and stopping on the first "significant" look inflates false positives; stopping on a pre-set count does not, because the count does not depend on the on/off difference. Pick N from §1: at least `MIN_EVENTS = 10` events per arm, more for a small effect (`hitch-ab.ts --power`).

**Repro format** (`tools/harness/repro-loop.ts`): beside or instead of `steps`, a repros.json entry may carry
`loop: { steps: [...], arms: ["off","on","on","off"], until: {perArm: N} | {events: N}, maxMinutes? }`.

- `profile-world.ts --repro` runs the plain `steps` first, then the loop body once per arm, the `arms` list over and over (one pass = one ABBA cycle).
- In loop step args the string `"$on"` becomes `true` on an on firing and `false` on an off one. Each loop step's note becomes `<note or cmd> <arm> #<firing>` (`arm off #1`, `fire off #1`), the same form as the hand-written `bp-overwatch-ab-loop`.
- Events: a step with `count: {counter: name}` adds the rise of that ts3fixes counter (agent `patches`, read just before the step and after its `waitS`; a missing counter fails the run); `count: {result: field}` adds that numeric field of the step's answer; with no counted step, each firing is 1 event.
- The rule is checked only at the end of a whole arms cycle, so the arms stay balanced. `perArm` needs both arms to reach N; `events` counts both together.
- `maxMinutes` (default and upper bound: `--minutes`) caps the loop. At the cap the window also ends, with `loop cap reached: ... rule not met`. `--minutes` / queue `profile.minutes` is then only the cap.
- Not combinable with `--ab`, `--until-compares` or the side probes (inroom, watchstalk, livereads2, f4b, vres); `queue.ts` refuses such jobs at start and `--dry-run` prints the stop rule and the cap.

**Early end** (shared with `--until-compares`): `stopWindow()` sets `stopped` (upkeep, clock guard, native-log stamping, main-thread sampler and the frames wait all end), writes `<tag>-stop.json` (`secs` = real window length, `capSecs`, the line, loop result) and `<tag>-stop.flag`. The stutter profiler gets that path as `-P stopFile`; it polls it once a second on the agent's JS thread (only when set), then removes every hook, writes the last seconds row and closes its files (the same `closeAll()` as `dispose()` on a normal unload) and prints `[ts3prof] stopped early`; the harness then closes the repl (at most 15 s later). The frames session stays loaded until the game closes, as at a normal end. Before this, `--until-compares` killed the repl without the dispose work.

**Readers:** the profiler files keep the cap in their name (`<tag>-<cap secs>-*`); `worst-hitches.ts runContext` takes `<tag>-stop.json` `secs` as the run length, so an early end is not `profiler cut short`. Repro step log lines now carry the note (`repro <id> step reflect_call [fire off #3]: ...`), and runContext labels such windows `reflect_call off` / `reflect_call on`, so `hitch-ab.ts --scenario <tag>` lists each arm apart (for `bp-overwatch-ab-loop` too). `queue.ts` puts the line in `row.stop` and the verdict: `PASS: profile stopped at 20 events after 15.3 min (off 10, on 10; 20 firings, 5 cycles; rule 10 events per arm), cap 22 min, seed 12345`.

**Example:** repro `bp-overwatch-ab-loop2` (the original `bp-overwatch-ab-loop` is left as written: other agents work on repros.json) with queue `tools/harness/queues/overwatch-loop2.json`: `set_speed normal`, then `patches {overwatchspread: "$on"}` + `reflect_call OverwatchSpreadNow` (45 s), ABBA until 10 firings per arm (5 cycles, ~15.3 min), loop cap 20 min, profile cap 22 min.

**Checks [verified]:** `node tools/harness/repro-loop.ts` (fixtures with a fake agent and clock: 12 firings for perArm 6 with the hand-written notes and `$on` values, rule only at cycle ends, counter and result counts, cap with 0 events, window end mid-cycle, missing counter fails, validation); `worst-hitches.ts --self-check` (`stepLabel`); `queue.ts --self-check` (example parses, `STOP_LINE`); `queue.ts overwatch-loop2.json --dry-run`; `python tools/probe/stutter-tasks-test.py` on the ping.exe dummy (stop file: `stopped early` within 3 s, hitch file stops growing, dummy alive and looping). Not checked: the early end in the game (ledger I-168-6, I-168-7).

## 7. Addendum: three A/B method fixes (2026-10-23)

Found in the 2026-10-22/23 loop runs. Harness and data only; self-checks and dry-runs, **no game run**. Ledger §282.

**1. Setup steps leave the arm.** In `script-hitch-loops` `bp-stuckcheck-loop` the on arm's 1698 ms iteration was the harness's own `strand_many` call (`Ts3Agent.PollTask`, PlanFromPoint 69 plans 1469 ms), not the fix. A step may now say `setup: true|false`; the default (`repro-loop.ts isSetup`) is true for `patches`, `native_set`, `strand_*`, `spawn_sims` and `set_speed` unless the note starts with `fire`. Every loop step in repros.json carries an explicit `setup` (99 steps, 16 loops): build/buy enter and crowd placement or returns are setup; build/buy exit replans, crowd starts that fill the queue, route-mode plans, destroys and the fire step are not; `alloc-churn-loop` is all workload (`false`). `checkLoop` refuses a body with only setup steps, and the repro-loop self-check refuses an unmarked loop step.
- `profile-world.ts` logs a setup step as `repro <id> step <cmd> [note] (setup): ...`, and `runStep` logs the end of its wait (`firing #n done after X s (fixed|signal|cap) [note]`) for every setup step, not only adaptive ones.
- `worst-hitches.ts runContext` gives such a window `setupEnd` = the wait's end line + 1 s (log seconds; no clock pad: the coarse 1.5 s pad ate the next fire call). Logs from before the mark fall back to repros.json (loop steps by position in their firing, plain steps by command + note). Every wait-end line now also starts the next step's window, so a fire step keeps its own wait (its effect) instead of the next firing's arm switch taking it (old behaviour: a firing ended at the next step's window start, which began right after the fire call returned).
- `firingWins` cuts each firing into the arm piece(s) and `<arm> setup` pieces (step start .. wait end). `hitch-ab.ts --scenario` lists `off setup` / `on setup` rows beside `off` / `on` (seconds and iterations split; firings counted once per row) and the confidence rows use `off` / `on` only; `freeze-report.ts --repro-causes` names setup causes under `off setup` / `on setup`. An iteration (or publish gap) belongs to the piece it **starts** in: an agent call's iteration starts inside its step.
- Re-read of 2026-10-22 `bp-stuckcheck-loop` (no marks, fallback, coarse clock): the iterations >= 300 ms led by a `Ts3Agent.PollTask` script run move to the setup rows (scenario table: off setup 9, on setup 5, the 1698 ms one included; the cause table, after probe-call exclusions: 8 and 5, every one `Ts3Agent.PollTask`); real arms: off 1 (1160 ms, `WorldLot` script run), on 0. Too few events for a verdict either way (§1 rule).

**2. Enough firings per arm.** The route loops counted instances per firing (docs/170 §6), so `perArm` held after 2-4 firings per arm, far below the §1 rule's `MIN_EVENTS = 10` slow iterations. `until` takes `minFirings: N` (every arm in `arms` needs N firings too, checked at cycle ends like the rest). A queue job sets its own stop rule with `run.profile.loopRule: {until?, maxMinutes?}` (`profile-world.ts --loop-rule`), so `repro-check-route.json` keeps the repros' short rules. A/B jobs now: `bp-route-stuckcheck-reset-ab` `{perArm 40, minFirings 10}` cap 14 of 15 min; `bp-route-queuefull-replan-ab` `{perArm 10, minFirings 10}` cap 18 of 19 min (was 2 firings per arm); `bp-route-nopath-crowd-ab` `{perArm 50, minFirings 10}` cap 14 of 15 min; `bp-stuckcheck-loop` `{perArm 120, minFirings 10}` cap 12 of 12 min (was capped at 10); `bp-overwatch-ab-loop2` `{perArm 10, minFirings 10}` cap 20 of 22 min; timeouts minutes + 10. Caps from the steps' `waitS` / `maxS` for 20 firings plus the 65 s lead-in. The plain ABBA jobs (`sv-route-seacheck-freeze-ab`, the easpread and errortrap loops) keep their hand-written firing counts.

**3. Measurement settings.** The three A/B queues already carried `freeze-measure.json`'s settings (frames on, `Harness.ForegroundRender=1`, `profile.noSamplers`, `HitchAttr` `Holders=1`, `RouteJobPhases` `SyncPlans=1`) on asi `.0332` (freeze-measure itself names `.0331`); their own switches (`fixesOn` without the armed fix, `cameraPan` only on the route loops) stay. The repro-loop self-check now asserts these settings, a `minFirings >= 10` loop rule on every loop job and `timeoutMin >= minutes + 10` for all 10 jobs of `route-hitch-loops`, `script-hitch-loops` and `overwatch-loop2`.

**Checks [verified]:** `node tools/harness/repro-loop.ts` (minFirings: a counted perArm 20 stops after 1 cycle alone, after 5 with minFirings 10; single-arm minFirings; `withRule`; `isSetup` defaults; repros.json marks; queue settings), `hitch-ab.ts --self-check` (setup pieces 70-74 / 100-104 s, a 1700 ms setup iteration in `off setup`, seconds 26/4/96/4), `freeze-report.ts --self-check` (`off setup` cause row), `worst-hitches.ts --self-check` (`stepSetup` by mark and by repros.json position), `queue.ts --self-check`, `--dry-run` of the three A/B queues and `repro-check-route.json` (correctness rules unchanged). Not checked in the game: the `(setup)` mark and wait-end lines in a real log (I-168-8), the firing lengths behind the new caps (I-168-9).

## 8. Addendum: probe calls exclude only what they explain (2026-10-23, docs/178 T2)

`freeze-report.ts` dropped sv-crowd-0333's 2085 ms publish gap (`rerun-0333-2026-10-23-sv-crowd-cause-0333`, iteration n 5438: `rtLock` 2069 ms at steering `0x85FE10` behind a foreign 2.08 s route job) as `probe call`, because a 110 ms `keep_alive` call ended inside it (docs/178 §3 named it `game_state`; the call on the gap by the frames.csv offset is `keep_alive`). Harness only, **no game run**. Ledger §295.

**Rule (worst-hitches.ts `exclusion()` / `callExplains()`, shared by freeze-report, hitch-ab, story-report, worst-hitches):** an overlapping probe call excludes an iteration, publish gap or seconds row only when it can explain it:
- never when the iteration (the gap's / row's longest) is led by a route wait: `rtLock + rtOtherLock + rtJob + rtPlanWait` >= half its wall time (the sim waited on a route job's lock or result; no agent call causes that);
- else when the call's own round trip (`calls.jsonl` t1 - t0) is >= half the hitch, or the command leaves sim-thread work behind after it returns (`spawn_sims`, `remove_sims`, `crowd_go`, `save_game`);
- rows with no hitch, frames, and older runs without `calls.jsonl` (upkeep-round guesses, no duration) keep the old rule (any overlap excludes). A gap or hitch judged real by its own check is no longer re-excluded by its row's probe call.
- Self-check cases in `worst-hitches.ts --self-check`: the exact sv-crowd-0333 iteration/row/gap (real), the same call without the route wait (real: 110 ms < half of 2 s), a 350 ms call on a 400 ms hitch (excluded), a 26 ms call (real), a 26 ms `spawn_sims` (excluded), a 350 ms call on an `rtJob`-led hitch (real).

**Re-read (real counts, before -> after; freeze-report publish gaps, hitch-ab iterations):**

| Run | real min | gaps >= 300 | gaps >= 1000 | iterations >= 300 | iterations >= 1000 |
|---|---|---|---|---|---|
| rerun-0333 sv-crowd-cause-0333 | 4.6 -> 4.8 | 3 -> 5 | 0 -> **1** (2085 ms) | 4 -> 7 | 0 -> 1 |
| natural-play-2026-10-22 bp-natural-30 | 24.3 -> 25.8 | 5 -> 7 | 0 -> 0 | 5 -> 8 | 0 -> 0 |
| natural-play-2026-10-22 ip-natural-30 | 25.0 -> 26.2 | 32 -> 33 | 0 -> 0 | 34 -> 43 | 0 -> 0 |
| natural-play-2026-10-22 sv-natural-30 (INVALID: noisy) | 24.2 -> 25.2 | 24 -> 26 | 9 -> 10 (a 1595 ms gap over 663 short iterations; the only calls, `test_env` 27 ms + `time` 4 ms, start ~100 ms after it ends and were matched only through the 200 ms pad and the row) | (invalid run) | |

Probe-call exclusions shrink to short calls on short hitches (sv-crowd 42 -> 8 gaps, max 2085 -> 123 ms; BP 481 -> 112, IP 270 -> 54). The SV crowd >= 1000 ms rate is 2.1 per 10 min (was 0.0), as docs/178 T2 predicted (~2.2). Conclusions that change: the lock-taker / foreign-job targets (docs/178 T1, T5) see more real freezes in every agent-polled run; IP natural's ~9 extra iterations >= 300 ms are the sim-side waits the calls had hidden.
