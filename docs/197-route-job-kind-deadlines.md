# Route job deadlines per kind (`[RouteAsync] KindDeadlines`)

2026-10-25. Rodney's decision for v0.2.0: our async route jobs get a deadline per job kind, so the sim thread never waits more than ~300 ms behind one job. Built (ts3native 0.36.77, counters v134 `kd*`, default off), selftest only, **no game run**. Ledger §353 (I-197-1..4). Queue `tools/harness/queues/kind-deadlines.json`, repro `route-kinddeadline-loop`.

Tags: [measured] = log or tool output; [verified] = disassembly, selftest or an earlier verified run; [inferred] = joined from design docs.

## 1. Why a deadline per kind

docs/196 §4 item 5: the waits behind a running route job (`+0x918`, gates, destroy) last as long as the rest of the job (SV 464 ms wait behind a 486 ms job, 313 / 318 [measured]). Covering wait keys one by one only moves the wait (docs/152 conservation, docs/189 §17). The only fix that removes these waits whatever the key is a shorter long job. Until now only two job kinds could be cut:

| Cut today | Kind | Deadline | Exits | Evidence |
|---|---|---|---|---|
| `FbJobCapMs` + `FbJobDeadline` (docs/134 §11, docs/178 T1) | foreign `Route.Plan` (FallbackAsync) | `FbJobCapMs` from the job's start | world A* cost stubs, cell A* and water entry stubs on the worker | T1 verified 2026-10-23; 59 `capped (FbJobCapMs)` long-job lines in the logs below |
| `stuckcheckcap` (docs/163 §10) | own stuck-check probe job, rounds before the last | `PlanDeadlineMs` | same | `stuckcheck-cap-verify` PASS: 41 probe jobs cut at 303-395 ms, 0 stuck Sims; last round plans uncut |
| `PlanDeadlineMs` (docs/164 §7) | sim-thread **sync** plans (NoStart / Foreign, non-IC) | `PlanDeadlineMs` | same, sim thread | `bp-plandeadline-ab` PASS: 32 cut, `pdHitOk` 0, no behaviour change, largest successful sync plan 81 ms |

Own `Route.Plan`, own Replan / PlanFromPoint / ReplanFromPosition (A10) and A10 foreign ReplanFromPosition jobs had no deadline. `PlanDeadlineOwnMs` (sync own-task plans) stays 0 by docs/164's rule "a route the Sim must take is not failed for time"; §4 below is how this build keeps that rule while still cutting.

## 2. Job kinds from existing logs [measured]

All `route async: long job` lines (jobs >= 200 ms, at most 20 lines a minute, so counts are lower bounds) in the 490 `probe-out/queue/*-ts3native.log` files written since 2026-10-21 (0.36.2x-0.36.7x builds, every world and repro). Waits = T4 lines `route jobs: sim waited N ms behind an own job` (>= 300 ms, `OwnJobWait=1`: only the SV stuckcheck / scown runs), joined to the kind of the last long job of the same route.

| Kind (job fields) | long jobs | p50 | p90 | max | >= 300 ms | >= 1 s | sim waits >= 300 ms behind it |
|---|---|---|---|---|---|---|---|
| own `Route.Plan` (kind 0, no stuck-check tag) | 2,722 | 243 | 460 | 12,250 | 708 | 69 | 98 (p50 510 ms, max 9,020, 39 >= 1 s) |
| own `Route.Plan` of a stuck-check round without probe mark (last round) | 31 | 949 | 1,486 | 1,968 | 22 | 12 | 22 (p50 1,030, 12 >= 1 s) |
| stuck-check probe (cut at 300) | 41 | 323 | 350 | 395 | 41 | 0 | 40 (p50 320, max 392) |
| own Replan (A10 kind 1) | 137 | 230 | 524 | 3,769 | 47 | 3 | not logged (no T4 run with Kinds on) |
| own PlanFromPoint (kind 2) | 133 | 243 | 504 | 660 | 35 | 0 | not logged |
| own ReplanFromPosition (kind 3) | 443 | 258 | 412 | 2,990 | 128 | 5 | not logged |
| A10 foreign ReplanFromPosition (`af`) | 863 | 247 | 321 | 1,025 | 199 | 1 | not logged |
| foreign `Route.Plan` (`fb`) | 488 | 250 | 587 | 9,263 | 138 | 32 | not logged (counted in `fbjc` histograms) |

IP natural (the world that still hitches): `natural-play-...-ip-natural-30` (0.36.27, 24 min) own 33 long jobs (4 >= 300 ms, max 750), foreign 17 (5 >= 300, max 445); the v0.1.5 bundle halves (6 min each) own 9-11 (max 290), A10 foreign 23-25 (2-4 >= 300, max 337). Release defaults (`FallbackAsync=0`, `Kinds=0`) leave **only own `Route.Plan` jobs** (plus probe jobs when ts3fixes marks them), so in the shipped config the own kind is the whole problem; the other kinds matter only with FallbackAsync / A10 on.

Not separable in the job (no field): car and boat drivers, NRaas GoHere, crowds. They are own or foreign jobs by the task that STARTed them. bp-plandeadline-ab's car-only plans failed 8 of 8 in both arms (they fail for reasons other than time).

## 3. Per-kind deadline and cut action

The cut action is the one every existing deadline uses: past the deadline the job's world A* (cost stubs), cell A* `0x61E500` and water `0x671930` searches take the game's own no-path exits [verified disassembly, docs/164 §7], the IC runs on through the planner's failure path and the job's result is collected as an ordinary failed plan. No partial path exists to return (docs/142 Mode 2 not built), and a sync fallback after a cut would only move the wait onto the sim thread. Overrun past a deadline = the parts no exit stops (portal A*, LotGraph_Build, goal step, script callbacks, the cell A* already running): probe 3-95 ms at 300 [measured], sync plans late max 30 ms [measured].

| Kind | Deadline (ini key, default) | Cut action | Player-visible effect of a cut | Guard |
|---|---|---|---|---|
| own `Route.Plan` | `KdOwnPlanMs` 250 | failed plan | the Sim's route fails: route-fail reaction, the interaction retries or cancels, autonomy picks another target | per-follower backoff (§4); last stuck-check round never cut |
| own Replan / PlanFromPoint / ReplanFromPosition | `KdOwnReplanMs`, `KdOwnPlanFromPointMs`, `KdOwnReplanFromPositionMs` 250 | failed replan | the walking Sim stops: `ReplanFailure` / `RouteFail` kill or `replandefer`'s route-fail reaction (docs/141 §3), then the interaction's own retry | backoff |
| A10 foreign ReplanFromPosition | `KdA10ForeignMs` 200 | failed replan | the caller's own failure handling (route-follow callbacks, stuck checks): the Sim re-routes later | backoff |
| foreign `Route.Plan` | `KdForeignPlanMs` 200 (replaces `FbJobCapMs` while on) | failed plan (as FbJobDeadline) | stuck-check and alarm callers try their next point; no Sim of its own waits on it | backoff |
| stuck-check probe (rounds before the last) | `KdProbeMs` 180 (replaces `PlanDeadlineMs` for probes while on) | failed probe plan (as stuckcheckcap) | none: a failed probe of an early round only re-arms the check; the last round (the only one that resets) plans uncut | none needed (ts3fixes keeps the last round uncut) |

Why these numbers: the sim waits at most the job's rest, so deadline + overrun is the longest wait. 250 + <= ~95 ms keeps own waits at ~250-345 ms while leaving own jobs that end by 250 ms alone (p50 of the long own jobs is 243 ms: about half the >= 200 ms own jobs are not touched). Foreign kinds have no Sim waiting on their route, so they take 200 (<= ~295 ms with overrun). The probe takes docs/196 §3 #1's ~180 ms so the 300-410 ms probe waits fall under 300.

## 4. Behaviour guards

1. **Per-follower backoff.** The follower id (route `+0x40/+0x44`, as ReplanDedupe's "same follower", read on the sim thread at START) keys a 64-row table. A follower whose job was cut within 10 s gets its next job's deadline doubled (`kdBackoff[0]`); after a second cut its next job runs uncut (`kdBackoff[1]`); a success resets it. A route the Sim must take fails for time at most twice in a row, then plans exactly as without the switch: no route-fail storm and no Sim kept from a reachable target. shortcut: 64 rows, oldest cut evicted (a town with more Sims cut inside 10 s loses their backoff, they stay at the base deadline).
2. **Stuck-check last round never cut.** An own job with a stuck-check tag but no probe mark (`j->sc` set, `j->probe` 0: the last round, the only one that can reset the Sim) runs uncut (`kdExemptSc`). So no stuck reset can come from a cut.
3. **Outcome counters.** `kdAfterOk` / `kdAfterFail`: the follower's next job within 10 s after a cut succeeded / failed. `kdUncutOk`: of those, an uncut backoff job succeeded (a plan the cut would have failed although a route existed: the behaviour cost, should stay small next to `kdCut[0]`). `kdCutOk`: a cut job still succeeded (a later candidate after the cut). `kdAfterStuckCheck`: a stuck-check mark on a follower cut within 30 s (needs ts3fixes `stuckcheckcap` marks; ts3fixes counters 297-301 give the reset parity in any case).

## 5. Build (ts3native 0.36.77, counters v134 `0x521C-0x52BF`, `plandeadline.cpp`)

- `[RouteAsync] KindDeadlines=0/1` (default 0, nothing installed) + 7 per-kind keys in ms (0 = that kind never cut, 50-60000). Runtime switch `kdMode` (+0x521C, `native_set kinddeadlines`, `--ab kinddeadlines`); the ini must be 1 for the stubs to exist.
- Reuses the existing machinery: `ServeAsync` sets the job's `t_fbDeadline` from `KdJobBegin` (kind + backoff) instead of the FbJobCapMs / probe rules; the world A* deadline goes through `CallPlanBudget`'s cost stubs, the cell A* / water stubs answer the worker through `g_pdWkTid` (`PdWorkerSkip` admits `t_kdKind`). Sync plans keep `PlanDeadlineMs`; with KindDeadlines on, the stuck-check PROBE mark is accepted when `KdProbeMs` > 0 even with `PlanDeadlineMs` 0.
- Counters: `kdJobs` / `kdCut` / `kdLong` (>= 300 ms) per kind, `kdLateMaxUs`, `kdBackoff[2]`, `kdAfterOk`, `kdAfterFail`, `kdUncutOk`, `kdAfterStuckCheck`, `kdExemptSc`, `kdCutOk`; minute line `route kind deadlines: ...`; long-job lines say `capped (KindDeadlines)`.
- Not covered: P5/P6 clone runs read only a foreign job's deadline (an own clone job is not cut; every P5/P6 switch ships off in v0.2.0).
- Selftest `TestPlanDeadline` §13: v134 layout, kinds, last-round exemption, backoff doubled / uncut / reset, outcomes, cut ok, late time, stuck-check marks, probe without backoff, row eviction.

Expected [inferred]: IP route-job-wait-led >= 300 ms 6.0 -> ~1-2 per 10 min (waits left = overrun cases), total ~17 -> ~13 per 10 min; SV >= 1 s behind own jobs (39 + 12 in the T4 lines above) -> 0 except backoff-uncut jobs.

## 6. Tests (queue `kind-deadlines.json`, not run)

| Job | Kind | Pass |
|---|---|---|
| smoke | IP 10 min, kd on, the pair's light tools | no crash, `kdReady` 1, smoke rules of queue.ts |
| `sv-kd-loop` | correctness, forced ABBA `route-kinddeadline-loop` (own no-path Go Here crowd), 10 min | no crash, 0 watchdogs, `kdCut_0` >= 1, no own-job wait >= ~350 ms on arms, no Sim stuck at the end |
| `sv-kd-nat-verify` | correctness, SV natural 30 min with every kind live (FallbackAsync, Kinds 7, ForeignKinds 4, stuckcheckown + cap) | no crash, 0 watchdogs, `kdCut_0` >= 1, a cut count read per kind with jobs, stuck-check resets per hour not above `sv-sccap3-nat-on` |
| `ip-kd-nat-off` / `ip-kd-nat-on` | IP natural pair, 31 min each, release defaults, KindDeadlines 0 / 1 | no crash, 0 watchdogs; judged by `hitch-ab.ts --causes` >= 300 ms per 10 min led by route-job waits (off vs on), all >= 300 / >= 1000, p99 / p99.9 / max, no new leader rising by the same amount |

Stop rule: a FAIL in the smoke or the two correctness jobs stops the queue before the pair. Ship-on only with `kdUncutOk` small, stuck-check resets at parity and the IP pair's route-job-led count down.
