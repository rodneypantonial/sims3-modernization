# 189. P6 and the sim's wait behind route jobs: the whole problem, why P6 measures ~0, and the plan that removes the wait

Written 2026-10-25. **Design and plan only:** no code change, no build, no game run. Inputs: PLAN.md "Resume here", docs/116, docs/94 §21-§24 (§24.11 lock-hold analysis, §24.13 G1 v2, §24.8 clone defect), docs/182, docs/178 §9, docs/159, docs/160, docs/161, docs/86-90, docs/137, docs/140, docs/174, docs/175, docs/81-83, docs/134, docs/152, docs/158, docs/183, docs/164; `native/ts3native/ts3native.cpp` (`ServeAsync`, `LockAcquireAsync`, `WaitAsyncDone`), `routegate.cpp`, `rtsites.cpp`, `p5.cpp`, `p5c.cpp`, `p4clone.cpp`, `snap.cpp`, `replay.cpp`, `hostcalls.cpp`; run data named per row. The two P6 fixes another agent builds in the `p6fix` worktree (`[RouteSnap] P6EarlyCopy`, counters v122; P6Rebase, per the registry reservation **not built** yet; docs/94 §24.14 when it lands) are treated as given inputs. Tags: **[verified source]** = read in our code or the disassembly-backed docs; **[measured]** = run counters or run files; **[inferred]** = derived, method named. Effort = wall clock for Claude with agents; game runs and clean nights listed apart.

## 0. Summary

- **The sim does not wait for the lock. It waits for the job.** Six mechanisms put the sim thread behind a route job (§1). Only one of them is the `+0x918` hold itself; the others key on `A_RUNNING` (a job exists), on the job's result, or on a sync plan the job could not take. P6 shortens the hold of a taken job (IP ~9.7 -> ~2.4 ms, BP ~5.6 -> ~1.9 ms [measured]) but leaves `A_RUNNING` for the whole job, and on BP it makes that span **longer** (9-14 ms vs 5.6 ms) [measured, docs/94 §24.11.2]. The `A_RUNNING` waiters hit at 22-30 per second in the P6 runs (`p5bHeldOut` BP 3,518 / IP 5,042 in ~2.7 min [measured]), 2-3 per job, so nearly every taken job is still waited out in full.
- **Three causes of ~0, ranked (§2):** (1) `A_RUNNING`-keyed waiters, ~2-3 per job, wait the span not the hold: expected net ~0 on BP, small on IP by arithmetic, before any noise; (2) take rate 14% BP / 29% IP, with 67% / 50% stale refusals under a **global** staleness rule (`g_spWrites`: any recorded routing write anywhere since the cut) at ~1,600-1,900 recorded writes per second [measured, docs/116 P1], and every refused job is longer than a plain one; (3) the measurement: `VerifyEvery=2` halves the takes and runs clone plus game plan under the hold, a 2.6-2.8 min forced loop with noise medium against an expected 0.75 / 1.9 s of saved wait, and no P6-off arm in the same load.
- **Architecture (§3):** "lock-free taken job". (A) Waiters key on `A_HOLD` (the job holds `+0x918`, or runs a live plan) instead of `A_RUNNING`, with the existing stale rule as the safety net; (B) the taken job holds nothing but the answer move and the IC tail (P6EarlyCopy for the copy, no retake around door asks, cross-job door memo); (C) take rate toward ~100% with per-lot staleness, rebase instead of refusal (P6Rebase) and all own kinds; (D) cut sharing on. Design E of docs/82 (worker-exclusive writes, read-only sim) is not the path: it makes the sim's readers lock-free, which the RO clones already did and which conservation (docs/152) showed cannot gain while gates and writers wait on the job. Expected on IP natural play: sim wait behind jobs ~74 -> ~15-25 ms/s [inferred, arithmetic in §3.6], about -6..-8 iterations >= 300 ms per 10 min out of 20.1 and p99 ~115 -> ~95-100 ms; the >= 1 s freezes (sync `NoStart` plans, script batches) are untouched by this and stay with docs/134/163/183.
- **Phases (§4):** 0 = the missing baseline with new per-outcome wait counters (~1 h + 2 day jobs + 1 night slot); 1 = `A_HOLD` gates (~3-5 h + 2 runs); 2 = take rate (per-lot staleness ~4-6 h, P6Rebase given, all kinds) (+ 2 runs); 3 = no retakes around door asks + cross-job memo (~4-6 h + 2 runs); 4 = own-task result wait (`JobYield`, built) and foreign jobs. Each phase one switch (off), counters, selftest + lock-model configs, one IP correctness job, one IP in-load ABBA with `[MovedWaits]`.
- **v0.2.0 (~Tue 10-27):** P6 as built (TakeKinds + ShareCuts + P6EarlyCopy, opt-in off) plus phase 0's baseline can make it; phases 1-3 go to v0.2.1 (they need two more clean nights each at the measured pace).

## 1. The whole problem: every way the sim thread ends up behind a route job

A route job is an own `Route.Plan` START (kind 0), an A10 replan (kinds 1-3, off in the release config) or a FallbackAsync foreign plan (`j->fb`). The worker runs one at a time: `ServeAsync` takes `+0x918` (`LockAcquireHook(j->lock)`), publishes `A_RUNNING`, runs the IC (`CallIc`), releases, publishes `A_IDLE` [verified source, `ts3native.cpp:2757-2832`]. Everything below is a sim-thread wait that exists only because of that job.

| # | Mechanism | Where the sim waits [verified source] | Keyed on | Measured share | What P6 changes today |
|---|---|---|---|---|---|
| M1 | **Plain `+0x918` acquire** while the worker holds it: steering `0x85FE10`, avoidance `0x846D21`, `UpdateSimFootprint 0x833593`, route-to-object `0x83156D`, `0x8334CC`, `0x82D296`, ICs (`0x755990`) | `LockAcquireAsync` pump (`ts3native.cpp:3739`): `P5bLock` try, then pump until free; `WaitNote(key)`, `HaNote(HA_LOCK)` | the lock (P5b/P5c lend admits these in the window) | the bulk of the keyed wait: BP 68-92 ms/s, IP 73-99 ms/s all keys (docs/116 §0, §4.1; docs/94 §22.29); IP natural play with the v0.1.5 bundle: `asyncPumpUs` 44.3 s / 600 s = **74 ms/s** (`ip-v015bundle-offfirst-0338`), 71 ms/s in `ip-locktaker-ab` [measured] | a taken job lends the lock during copy (own-only) and clone run; these takers pass (P5c `FreeLock`). Hold left per taken job ~1.9 / ~2.4 ms |
| M2 | **Gate waits keyed on `A_RUNNING`:** sites 1-11 (lot planner, Q, goal step = gate 10 `0x760BFF`), coarse gates 13-14, gate-0 misses, gate-12 writer families F-A/D/E/F and F-G placement `0x631F40` | `RgEnter` (`routegate.cpp:323-347`): `lock = g_async.state == A_RUNNING ? g_async.lock : NULL`, then `LockAcquireHook(lock)`; P5b admits only the read-only leaves (`SlGate`, `G0Gate`) | **job state, not the lock** | gate 10 ~4-6 ms/s BP, gate 0 ~9-14 BP / 5-6 IP, F-G 110 ms each on IP (docs/174), gate 12 ~14-18 BP before SH; `p5bHeldOut` BP 3,518 / IP 5,042 per ~2.7 min = **22-30 waits/s** in the P6 runs [measured, docs/94 §24.11.1] | nothing: they wait for the whole span, which on BP grows (copy + clone 8-11 ms vs 4.5 ms plan) |
| M3 | **Lifetime and drain waits keyed on `A_RUNNING`:** destroy `0x2000003` (`RtWaitJob`), saves, BuildRoutingData, closing, discourage drains, D-X | `rtsites.cpp` `RtWaitJob` -> `WaitAsyncDone`: `g_async.state != A_RUNNING` returns at once, else waits | job state | destroy 8.7-8.8 ms/s BP, 3.1-4.3 IP (docs/88 §1) | with P5c and `DestroyPark` a destroy parks one reference and runs; saves and drains still wait |
| M4 | **Host delegate calls from the job to the sim** (event 59 door asks through `0xD75FA0`): the worker keeps `+0x918` while the sim serves them; a sim taker that reaches the lock meanwhile waits behind the ask | `P5bAsk` retake / reopen (`p5b.cpp:214-216`, `p4.cpp:442/458`); served by `ServeInvoke` from the pump, READY poll, `WaitAsyncDone` | the lock during the ask + the pump's serve points | worker hold share BP 18.8 of 73.5 ms/s, IP 11.1 of 64 (docs/159); 6.7-8.6 asks per taken job, ~0.8 ms held per job [measured, §24.11.2] | retaken for each ask (step 6 of the timeline); `DoorMemo` (docs/159 H2, built, `--ab doormemo`) removes repeats within a job |
| M5 | **Result and collect waits:** the own task needs the answer (`RunPlan` -> `WaitAsyncDone(SW_RUNPLAN)`, `WaitJobDone(SW_COLLECT)`), queue-full / M1Pending fallbacks | `ts3native.cpp:2910`, `:3371`; docs/183 `sw*` by site | the job's completion | `swN` lines >= 300 ms; cand-1721 >= 1 s freezes were long own-START no-path jobs with the sim blocked in "other" (docs/183) | nothing; `JobYield` (docs/183, built, off) lets the task sleep instead |
| M6 | **Sync plans the job system did not take:** `NoStart`, `Foreign`, `M1Pending`, `QueueFull` fallbacks and the kept-sync `Replan` family run `Route_PlanLocked` on the sim under `+0x918` for seconds | `PlanLockedBody` on the sim; docs/134, docs/164 | not a wait: sim time | 12 of 15 >= 1 s iterations in the baseline set (docs/161 §3); IP replans 27 >= 300 ms | out of P6's scope (`FallbackAsync`, `SyncCapMs`, `PlanDeadlineMs`, `stuckchecknear`, `failrepeat`) |

Two facts tie the table together:

- **Conservation [measured four times, docs/152 §1; docs/89 §11; docs/137 §1; docs/178 §8-§9].** In a tick where a job is live the sim waits out the rest of that job as soon as any taker touches the lock or an `A_RUNNING` wait. Removing one taker (SlotEnabledRO, AvoidRO, DeferFootprint, IdleCheckRO, T6) moved its wait to the next taker in the same tick: IdleCheckRO's 118 skipped waits all reappeared at `0x755990` (823 ms moved vs <= 882 avoided), T6 net <= 2 ms. The wait is bounded by the job's span, not by the number of takers.
- **The job's span is what the sim waits for, not its hold.** M2 and M3 wait on `A_RUNNING`; M1 waits on the lock but, once the sim is blocked in one wait, the next taker of the same tick waits for whatever is left. So the quantity to shrink is the time from `A_RUNNING` to `A_IDLE` as seen by the sim's waiters, per job, times the jobs per second (IP natural ~9-10 jobs/s, mean hold ~16 ms incl. host waits: `asyncUs` 92.6 s / 600 s = 154 ms/s in `ip-v015bundle-offfirst-0338` [measured]).

## 2. Why P6 measures ~0, decomposed and ranked

Data: `night-1024-p6-fix` (`bp-p6-verify2`, `ip-p6-verify2`, 0.36.43, `TakeKinds=9 ShareCuts=1 VerifyEvery=2 VerifyLend=1`, ReleaseLock / ReleaseGates / FreeLock on, repro `route-queuefull-replan-loop`, 2.6-2.8 min, noise medium, timing invalid) against `night-1024-p6` (`VerifyEvery=1`, no take) [measured, docs/94 §24.11]. Sim `+0x918` waits `haUs[0]`: BP 24.79 s vs 22.08 s, IP 26.02 vs 27.14 s.

| rank | cause | numbers | effect on the measured gain |
|---|---|---|---|
| 1 | **(c) `A_RUNNING` waiters see the span, not the hold.** Gate waits (sites 1-11, 13-14), gate-0 misses, the gate-12 writer families incl. F-G, saves, drains and result waits ignore the lent lock | `p5bHeldOut` 3,518 BP / 5,042 IP per ~160 s = 22 / 30 per s, against ~14 / ~6 jobs per s = **~1.6-5 held-out waits per job**; taken-job span ~9-14 ms BP, ~9-11 ms IP vs a plain job's 5.6 / 9.7 ms [measured §24.11.2] | on BP a taken job is waited out longer than a plain one; on IP about 0-1.4 ms shorter. The §24.11.3 gross figure (2.4 / 6.1 ms per taken job) assumed the wait follows the held ms; with 2-5 `A_RUNNING` waiters per job it follows the span. **Expected net ~0 BP, <= ~+2-4 ms/s IP** [inferred, arithmetic] |
| 2 | **(a) take rate** 14% BP / 29% IP; stale refusals 67% / 50% (`p5cStale` 2,048 early + 961 at the retake on BP) | staleness is **global**: `P5cStaleEarly` / `P5cStale` compare `g_spWrites` with the cut's count [verified source, `p5c.cpp:54-75`]; `g_spWrites` increments on every recorded writer leaf that is not `SL_SRC` and on lot / epoch world events [verified, `snap.cpp:583-586`]. At ~1,600 BP / ~1,941 IP recorded writer rows per second (docs/116 P1) a job open for 10-20 ms from START to retake meets a write most of the time; the measured 50-67% says the writes cluster (animation passes) [inferred]. A stale-refused job = lent clone run + copy + **the game's plan under the hold**: longer than a plain job (docs/158: +3.8..+9.6 ms per eligible job) | caps the gross at 14-29% of jobs and adds span to the other 71-86%. Not takeable today: stale (global), verify, `nr` (non-routing live reads D1-D6), `noclone` / `cold`, FallbackAsync foreign STARTs (never armed), kinds 1-2 when A10 is on |
| 3 | **(e) measurement** | `VerifyEvery=2`: half the armed jobs ran clone + game plan under the hold (longer than plain); forced loop `route-queuefull-replan-loop` (queue 12-15 ms per job: jobs back to back, so the sim is behind a job most of the time either way); 2.6-2.8 min, noise medium, harness timing invalid; expected saving 0.75 / 1.9 s against run-to-run differences of 2.7 / 1.1 s between the two nights; no `--ab routesnapplan` arm in one load; `[MovedWaits]` not in 0.36.43; `haUs` has no split by job outcome | the run could not have shown the §24.11.3 gross even if cause 1 did not exist |
| 4 | **(b) remaining holds per taken job**: IC body outside the planner ~1.1-1.6 ms, door-ask retakes ~0.8 ms (6.7-8.6 asks), final retake, copy when not own-only (5.8 / 3.8 ms after an R image change) | ~1.9 / ~2.4 ms held per taken job [measured]; the copy is "mostly lent" by sums only [inferred] | second-order next to 1-2; becomes first-order once 1-2 are fixed |
| 5 | **(d) waits moving to other takers** | the lent-window takers (steering, avoidance, footprint writers under P5c) pass; the wait moves to the first `A_RUNNING` waiter of the tick (cause 1) or, with none, is really avoided (`[MovedWaits]` will show it) | already counted in 1 |

So: not one bug, but the hold model. P6 attacked the hold; the waiters watch the job.

## 3. Target architecture: the sim never waits for a taken job

Goal: for a job answered from the clone, no sim-thread wait of classes M1-M4 at all; M5 only for the own task that needs the answer (and that task can step aside); M6 unchanged. The design has four parts; each is a switch; all compose with P6EarlyCopy and P6Rebase.

### 3.1 (A) Waiters key on `A_HOLD`, not `A_RUNNING`

- **Change [design].** A second published state, `A_HOLD` = the worker holds `+0x918`, or runs a live plan (a refused / verify / plain job), or is in the IC tail. Set at every worker `LockAcquireHook` / retake, cleared at every lend (`P5cLend`) and at the release; published before the worker reads `g_rgOffSimOpen` (the same Dekker order `ServeAsync` uses for `A_RUNNING` today). The M2 waiters (`RgEnter` sites 1-11, 13-14, gate-0 misses, gate-12 writer families, F-G) and the M3 drains test `A_HOLD`; saves, closing, BuildRoutingData and the result waits (M5) keep `A_RUNNING`.
- **Why it is safe for a taken job [verified source + inferred].** During the lent window the job reads only the clone (option G: every per-search global redirected, docs/94 §12, U1 / G2 pass), private A\*, W' and Q (docs/94 §16.6 private pools, §21.13). A sim writer admitted through a gate in that window (a sync plan's goal step, a lot rebuild, a footprint writer family) writes live structures the clone does not read; what it does change is the **answer's validity**, and that is exactly what the existing stale rule catches: every such write is a recorded writer leaf or a lot / epoch event (`g_spWrites`), so `P5cStale` at the retake refuses the answer and the game's plan runs under the retaken hold with `A_HOLD` set, where the gates wait as today. Lifetime: `DestroyPark` / `P5cPark` already park one reference for destroys in the window (docs/88 design G). Non-routing live reads D1-D6 are refused by the `nr` bits as today.
- **What must be proven.** Q1 (§6): every write leaf under each admitted gate site is in the recorder's site table (`kSn` in `routesnap.cpp`) or raises an epoch event; a gate whose writer is not recorded stays on `A_RUNNING`. The lock model (`tools/p5-lock-model.ts`) gets `hold-*` configs; `TestP5` gets a window with a gate entry that writes (expect: answer refused, no use after free, no hang). G1 v2 must re-pass with the switch on (>= 10,000 compares, tier 3 = 0), since admitted writers widen the skew window; G5 (keyed waits not above off, it/s not down > 2%).
- **What it removes.** The M2/M3 waits during taken jobs: with `p5bHeldOut` at 22-30 per second, this is the one change that lets the measured hold reduction reach the sim. Risk: medium (a missed writer = stale answer not refused = wrong route, visible as M2/M3 in shadow). Effort ~3-5 h + 2 game runs. Depends on: P6EarlyCopy (else the span still carries the copy), P5c on.

### 3.2 (B) The taken job holds nothing but the answer move

- **Copy before the hold:** P6EarlyCopy (given): own-only acquire + copy in `P5PreLock` time, before `A_RUNNING`. Leaves: clone run (lent) + door asks + final retake + IC tail.
- **Door asks without a retake [design].** The retake exists because a live job's search state must not move while the sim serves the ask (docs/159 §3). A clone job's search state is private, so `P5bAsk` can post the ask with the lock still lent; the sim's handler (event 59, GoHere) may plan a nested route and write live routing state, which the stale rule refuses as in 3.1. Proof: Q2. Removes ~0.8 ms held per job and the retake / reopen pairs that today make the sim's takers wait mid-window.
- **Cross-job door memo [design, docs/159 H4-lite].** `DoorMemo` already answers repeats within a job (docs/159 H2, `hostcalls.cpp`). A cross-job memo keyed by the same door key, invalidated by the lot's portal-lock IC (`IC_RouteManager_SetPortalLockStatus 0x74B490`), the event-59 handler-list cache (docs/115) and the lot epoch, answers the same ask in the next job without the sim (asks repeat across jobs within 1 s: `hc*` counters measure it). Mod risk as docs/104 guard M2-1..M2-3; shadow mode (ask anyway, count answer changes, must stay 0) before acting. Effort ~3-4 h.
- **IC tail.** The IC body after `Route_PlanLocked` (~1.1-1.6 ms) runs under the final retake. Q10 asks what it touches; if it is route-object and result writes only (docs/93), the retake shrinks to the answer move (~0.1 ms).

### 3.3 (C) Take rate toward ~100%

| class of untakeable job today | share [measured] | closer | effort |
|---|---|---|---|
| stale, global rule | 67% BP / 50% IP of take attempts | **per-lot staleness:** the recorder already keys per-lot writes (`g_spLot->note` for W1-W6, FPCORE, PRIVATE, FPREMOVE, LOTREMOVEFP, REMOVELOT [verified, `snap.cpp`]) and the replay records carry the lot key (RP_INS / RP_REM `a`, `b` [verified, `replay.cpp:1976`]); add a small ring of (lot key, write seq) and a world-level counter for epoch / grid / R writes; the clone run records its read set (lots it looked up through `RpGetIfPresent` / `c->lotBk`, plus start and goal lots); stale = a written lot in the read set, or any world-level write. Expected stale rate 10-20% [inferred: a `Route.Plan` touches 1-3 lots; writes spread over the lots with moving Sims]. Q3 measures it offline first | ~4-6 h + offline study ~1 h |
| stale, after the fix above | the rest | **P6Rebase** (given when built): rebase the clone to the newest cut and rerun once with the same-cut copy instead of refusing | given |
| verify | `VerifyEvery` | release setting 0; shadow runs keep it | 0 |
| `nr` (D1-D6 live reads), `noclone`, `cold`, storm skips | `m2FbNr`, `m2Fb[*]` (share at P6 rates not read yet: Q9) | per cause after Q9; `cold` ends once W' and A\* are warm at the cut (docs/94 §24.11.4 item 5) | ~1-2 h |
| kinds 1-2 (A10 `Replan`, `PlanFromPoint`) | 0 in the release config (A10 off) | `TakeKinds` bits when A10 is on; same path | ~0.5 h |
| FallbackAsync foreign STARTs | never armed (keep `FbJobCapMs`) | phase 4: arm them with the deadline kept; these are the long cell-A\* jobs behind SV's 1-2 s freezes (docs/178 §3) | ~2-3 h |

### 3.4 (D) Cut sharing

`ShareCuts=1` (built, docs/94 §24.2) is a precondition at Plan rates: one 9-10 ms sim-side cut per START (docs/94 §22.30.2 item 4) at ~10-20 STARTs per second is unaffordable. Ship condition from §24.2: dT <= 0 and `snPcCutUs` <= ~5 ms/s. Phase 0 reads both.

### 3.5 Alternatives considered

- **(iv) docs/82 design E (worker-exclusive writes, read-only sim under QL shared), 6-9 days.** It makes the sim's quadtree readers lock-free against a live job. The RO clones (S2, AvoidRO, SlotEnabledRO, Gate0RO, RouteToObjRO, PlacementRO) already give that per taker, and the measured result is conservation: the gates and writers still wait on the job. E does not touch M2-M5. Its QL shared infrastructure is reused by 3.1 (the gate leaves P5b admits run under it). Not the path.
- **Our own search (option R)** and the **native door evaluator** (docs/105, 10-16 h): only if 3.2's memo is not enough (G5 rule of docs/94 §19.1).
- **Shorter job windows (H2 `LocoWindow`, F start-after-locomotion):** no-op measured (docs/136 §3.7); they move job time to other stages.

### 3.6 Expected gain, with the arithmetic

Inputs (IP natural play, `ip-v015bundle-offfirst-0338` and `ip-locktaker-ab` [measured]): jobs ~9.7/s, worker hold 154 ms/s (mean ~16 ms incl. host-call waits), sim pump wait 71-74 ms/s, so the sim waits r = 0.48 ms per held ms (the forced P6 repro gives 0.84: there the sim is behind a job almost always). Per-job model: sim wait = r x (span the waiters see) x jobs/s.

| state | take rate t | span of a taken job | span of a non-taken job | wait ms/s = r x 9.7 x [t x s_t + (1 - t) x s_n] |
|---|---|---|---|---|
| today, P6 as measured (A_RUNNING waiters) | 0.29 | ~11 ms (copy + clone + asks + retakes) | ~16 + ~4 (stale: lent run + game plan) = ~20 | 0.48 x 9.7 x (0.29 x 11 + 0.71 x 20) = **81** (vs 74 off: ~0 or worse, as measured) |
| + P6EarlyCopy + P6Rebase | 0.5 | ~7 (clone + asks + retake) | ~16 | 0.48 x 9.7 x (3.5 + 8) = **54** |
| + (A) `A_HOLD` waiters | 0.5 | ~2.4 (held parts only) | ~16 | 0.48 x 9.7 x (1.2 + 8) = **43** |
| + (C) per-lot staleness, all own kinds | 0.9 | ~2.4 | ~16 | 0.48 x 9.7 x (2.2 + 1.6) = **18** |
| + (B) no door retakes, cross-job memo, short IC tail | 0.9 | ~0.8 | ~14 (fewer host waits) | 0.48 x 9.7 x (0.7 + 1.4) = **10** |

So IP sim wait behind jobs ~74 -> ~15-25 ms/s after phases 1-3 [inferred; the first three rows are the pieces the baseline of §5 must confirm: r, the span per outcome, the stale share]. Hitch effect [inferred from docs/161 §3 and docs/174]: route-job waits led 28 of the >= 300 ms iterations in the baseline set (IP and SV) and 9 of 32 IP natural freezes, i.e. ~30-40% of IP's 20.1 >= 300 ms per 10 min; removing ~75% of that wait gives ~-6..-8 per 10 min and p99 ~115 -> ~95-100 ms. Max hitch and the >= 1 s counts do not move: those are M6 (sync `NoStart` / `Foreign` plans) and script batches. On BP (jobs ~14-27/s, hold 110-128 ms/s, wait 68-92) the same model gives ~70 -> ~20 ms/s; BP has few >= 300 ms iterations left (5.4 per 10 min), so the gain there is p99 and catch-up ticks, not counts.

## 4. Phased plan

Every phase: one `[RouteSnap]` switch (default 0, `--ab` name), counters in a new registry version (v122 is taken by `p6fix`; reserve v123+ with `node tools/registry.ts` when building), selftest + `p5-lock-model.ts` configs (+ one mutation each), `hook-sites.ts --check`, one IP correctness job (`VerifyEvery=1`, shadows, G1 v2 report, `ulLockTrip` = 0, no forbidden log lines), then one IP timing job: `route-queuefull-replan-loop` in-load ABBA, `[MovedWaits] Enabled=1`, `[HitchAttr] Enabled=1 Holders=1`, read with `hitch-ab.ts` (p99 / p99.9 / max / counts >= 100 / 300 / 1000) and `wait-ab.ts`. Stop rule per phase: any crash = our bug first (AGENTS.md order); G1 v2 tier 3 > 0 = stop; keyed wait up or it/s down > 2% = stop and read `[MovedWaits]` before anything else.

| phase | what | switch / counters | selftest | correctness job | timing job | go if | effort | release |
|---|---|---|---|---|---|---|---|---|
| **0** | the missing baseline (§5) + per-outcome wait split: `haUs` by job outcome (taken / stale / verify / plain / none) and by waiter class (lock pump vs `RgEnter` gate vs `RtWaitJob` vs result), `p5bHeldOut` per gate site, span per outcome | counters only (`p6w*`, v123) | counter asserts | none (counters) | `ip-p6-baseline` (loop, `--ab routesnapplan`, VerifyEvery 0, ShareCuts 1, DoorMemo fixed in both arms) + `ip-p6-natural` (10 min, both orders) | always; decides phase 1 vs 2 first | ~1 h + 2 day jobs (~35 min) + 1 clean-night slot | v0.2.0 (data) |
| **1** | (A) `A_HOLD` waiters: gates 1-11, 13-14, gate-0 misses, gate-12 writer families, F-G, drains test `A_HOLD` during a taken job's lent window | `HoldGates=1`, `--ab holdgates`; `hgAdmit[site]`, `hgStaleAfter`, `hgKeptRunning[site]` (sites left on `A_RUNNING` by Q1), `hgTrip` | `TestP5` window + writing gate entry; lock model `hold-*` + mutation `mut-hold-no-stale` | `ip-holdgates-verify` (+ BP), G1 v2 >= 10k | `ip-holdgates-ab` | `p5bHeldOut` during taken jobs -> ~0, sim wait down, `hgStaleAfter` adds < 10 points to the stale rate | ~3-5 h + 2 runs | v0.2.1 |
| **2** | (C) take rate: 2a per-lot staleness (`StaleScope=1`), 2b P6Rebase (given), 2c `TakeKinds` all own kinds, 2d `cold` fix (warm W' / A\* at the cut) | `p6StaleLot`, `p6StaleWorld`, `p6ReadLots` hist, `p6TakenScoped`; shadow mode first (`StaleScope=2`: decide per lot, count what the global rule would have refused, take nothing extra) | `TestP5` read set vs write ring cases (hit, miss, world-level, child footprint -> world-level) | `ip-stalescope-verify` (shadow mode, then acting), G1 v2 | `ip-stalescope-ab` | take rate >= 70% IP with tier 3 = 0 | 2a ~4-6 h + Q3 ~1 h; 2c ~0.5 h; 2d ~1-2 h + 2 runs | v0.2.1 |
| **3** | (B) door asks with the lock lent (`AskLent=1`, built: §8), cross-job door memo (`DoorMemoX=1`, shadow mode first), IC tail retake shortened if Q10 allows | `auAsks`, `auStaleAfter`, `dmxHit`, `dmxMismatch` (must stay 0) | threaded test: ask during a window with a nested sim plan | `bp-door-lock` + GoHere mod job (docs/94 §20.2 jobs 14, 16) | `ip-askunheld-ab` | held ms per taken job < 1, `dmxMismatch` 0 | ~4-6 h + 2 runs | v0.2.1 / v0.3 |
| **4** | M5 and foreign: `JobYield` (built, docs/183) in the same arm; FallbackAsync STARTs armed with the deadline kept | existing `jy*`; `p6Taken[fb]` | existing | SV crowd verify | `sv-crowd` + IP | >= 1 s counts on SV down | ~2-3 h + 2 runs | v0.3 |

**Composition with the given fixes.** P6EarlyCopy is a precondition for phase 1's gain (it takes the copy out of the span; without it `A_HOLD` still covers 4-6 ms of copy whenever the acquire is not own-only). P6Rebase is phase 2b; with 2a it only has to handle the 10-20% that stay stale. Neither conflicts: phase 1 touches `routegate.cpp` / `rtsites.cpp` / the state publish in `ServeAsync`; the fixes touch `p5.cpp` / `p4clone.cpp`. Build phases on top of the merged `p6fix` branch.

**Order.** 0 first (it is cheap and it is the data every later verdict needs). Then 1 before 2 if phase 0 shows `A_RUNNING` waits >= 30% of the job-attributed wait during taken jobs (expected from `p5bHeldOut`); else 2 first. 3 after both.

**Release fit.** v0.2.0 (~Tue 10-27) = routing complete per memory `release-v020-plan`: P6 as built (TakeKinds 9, ShareCuts 1) + P6EarlyCopy, **opt-in, off by default**, after the two P6 correctness jobs and the docs/94 §20 release queue; phase 0's counters ride along (observe only). Phases 1-3 need ~12-17 h and 4-6 game runs across 2-3 clean nights: v0.2.1. State that in the v0.2.0 notes: "snapshot answers opt-in; no measured hitch gain yet; the hold-model change is next".

### 4.1 Phase 4 build (2026-10-25, branch `p6/phase4`, counters v128, not run)

Built on master 4c913d71 (0.36.56, P6EarlyCopy merged), `build.cmd out-p6p4` + `selftest ok`, `selftest-all` pass; no game run. Tags as in the header.

**Scope.** M5 needs no new code: `JobYield` (docs/183, native `jobyield` + ts3fixes `jobyield`) is already a runtime switch on both sides and goes into the phase-4 arm as built. The build is the foreign half of M6 / §3.3's last row: **`[RouteSnap] TakeForeign`** (ini, default 0; runtime switch `routesnapforeign`, `--ab` / `native_set`). With it, `P5OnStart` arms a FallbackAsync foreign Route.Plan START (`j->fb`) like an own one (needs `TakeKinds` bit 1, the same cut / ShareCuts / P5c / P6EarlyCopy path), always as a take job (`p4 = 2`, never verify; the own jobs' verify cadence `g_p5N` is not advanced). The switch is read once per job at its START; nothing changes inside a running job.

**Deadline kept [verified source].** A foreign job's `FbJobCapMs` (`t_fbDeadline`, set by `ServeAsync` from the job's start) now holds over every part of an armed job:
- cell A* / water: the plan-deadline entry stubs answer the worker inside the clone run already (`PdSkip` compares the thread with `g_pdWkTid` and reads `t_fbDeadline`; it has no `t_snapRun` test), and their exits write only the search's own fields (docs/164 §7) [verified source];
- world A*: the clone run calls `g_planOrig` directly (`P4Guarded`), outside `CallPlanBudget`, so `P5Plan` now wraps `P4RunClone` in `P6fbCloneBudget` / `P6fbCloneBudgetEnd`: `t_budget = {on, dl = t_fbDeadline}` for that run only, the outer budget restored after; a cap there sets `t_fbCapHit`, so the job counts in `fbjcCapped` and its long-job line as before. `BudgetStep` writes only TLS (`t_budget`, `t_jobCapAt`) and counters; the cost stubs (`CostGate` at `0x676340`..) are code the clone run executes anyway and pass while `t_budget.on` is 0, which is still the case for every own job [verified source];
- a refused answer (stale, nr, cold, ...) runs `PlanLockedBody` -> `CallPlan` -> `CallPlanBudget` with the same deadline, exactly as an unarmed foreign job; a clone run that used up the deadline makes that game plan cut at its first cost call.
A capped answer is the planner's own no-path, as the game's capped plan; the stuck-check / Go Here callers already handle failed plans (docs/134 §10).

**Why arming is safe [verified source].** The only reason `j->fb` was skipped was the deadline (docs/94 §22.32.3, P6-7). A10 foreign kind-3 STARTs (`j->af`) from the same foreign tasks are armed today, and no P5 / P5b / P5c / P6EarlyCopy / door-ask path reads which task started the job (the owner thread is the sim either way; grep of `g_jobOwn`, `g_jobFb`, `->af`, `->fb`). The take checks (capture, epoch, cold, P5c stale before and after, nr, storm) are unchanged. §22.32.3's "no Mode=2 job has a deadline" now reads "only an armed foreign job with `TakeForeign`, and its deadline is the one its unarmed plan had".

**Counters v128** (`docs/registry.md`, 0x50DC): `p6fbOn`, `p6fbArmed`, `p6fbTaken`, `p6fbGame` (the game's plan answered), `p6fbCloneDl` (clone runs under the deadline), `p6fbCloneCapped`, 2 spare. Foreign jobs also count in `p6Armed[0]` / `p6Taken[0]` and the P6 phase split slot 0 (they are kind 0): read own = slot 0 - `p6fb*`. On this branch v123-v127 are a 328-int32 placeholder (`p6PadV123to127`, also in agent.js) so the offsets match the reservations; the integration merge replaces it with the other phases' fields.

**Selftest** `TestP6Foreign` (in TestP6, real clone life): switch off = unarmed; on = armed take-only with VerifyEvery 1 and `g_p5N` unchanged; its plan taken with a future deadline that reaches the clone run (`p6fbCloneDl`, `t_budget` restored, no cap); the budget helpers alone (own job / no deadline = no change; a passed deadline walls the first cost call, `t_fbCapHit`, `p6fbCloneCapped`); a stale foreign job plans with the game (`p6fbGame`), consumer hold released.

**Repro and queue (drafts, not run).** `tools/harness/repros.json` `sv-crowd-foreign-p6-loop` (sv-crowd-foreign-loop with the arm = `routesnapforeign` + `jobyield` + patch `jobyield`); `tools/harness/queues/p6-phase4.json`: job 1 SV correctness (trio on), job 2 SV clean-night ABBA judged with `hitch-ab.ts --scenario` and `wait-ab.ts`. No IP job is drafted: foreign jobs exist only with `FallbackAsync=1`; add the IP arm with phase 1's IP jobs.

**Open risks.** (1) A foreign START now takes a cut on the sim (~9 ms when not shared, §3.4): with `ShareCuts=1` most share one; watch `snPcCutUs` on SV crowds. (2) Gate waits still key on `A_RUNNING` until phase 1, so the gain on a taken foreign job is its held part only; a stale-refused one is longer than unarmed (clone run + game plan), but both run inside the one deadline, so it stays bounded as an unarmed foreign job is (plus the parts docs/164 §7.2 never cuts, as before). (3) The clone's world A* deadline exit is the same `BudgetStep` path as live plans but has never run inside a clone: job 1 must show capped foreign long-job lines on arm and `m2Off` 0. (4) Foreign jobs never verify, so G1 coverage for them comes only from the own Route.Plan verify jobs (same planner, same clone); a foreign-specific input (none found) would go unseen. Ledger §346 (I-189-40..43).

## 5. Measurement plan: the baseline that is missing, and the numbers that decide

No run exists with P6 on vs off in one load on the routesnap stack with `VerifyEvery=0` (docs/94 §24.11 opening). Two jobs, both IP, both on the merged `p6fix` build with phase 0's counters:

1. `ip-p6-baseline-loop`: repro `route-queuefull-replan-loop`, in-load ABBA `--ab routesnapplan` until 10 firings per arm, `TakeKinds=9 ShareCuts=1 VerifyEvery=0 VerifyLend=0`, P5b / P5c on, `DoorMemo` the same in both arms, `[MovedWaits] Enabled=1`, `[HitchAttr] Enabled=1 Holders=1`, shadows off, clean night, machine load line checked.
2. `ip-p6-baseline-natural`: 10 min natural play, both orders, same settings (the loop's r = 0.84 is not natural play's 0.48).

Read per arm:

| number | from | decides |
|---|---|---|
| sim `+0x918` + job wait ms/s (`haUs`, `rtJob`, `asyncPumpUs`) on vs off | `wait-ab.ts`, counters | the headline: P6 as built gains or not |
| wait split by job outcome and waiter class (`p6w*`) | phase 0 counters | **cause 1 vs cause 2**: if >= 30% of the wait during taken jobs is `RgEnter` / `RtWaitJob` (A_RUNNING), phase 1 first |
| `p5bHeldOut` per site | phase 0 | which gates phase 1 must admit first |
| span per outcome (taken / stale / plain) | `p6j*` + phase 0 | the §3.6 model's s_t and s_n |
| take rate, `p5cStale` early vs retake, `m2FbNr`, `m2Fb[*]` | existing | phase 2's size |
| `snPcCutUs`, dT (§24.2) | existing | ShareCuts holds at Plan rates |
| `moved waits` / `next takers` lines | `[MovedWaits]` | where the avoided wait went |
| p99 / p99.9 / max, counts >= 100 / 300 / 1000 per arm | `hitch-ab.ts` | the player-visible verdict (target metric) |

Go for phase 1 if: the taken jobs' wait is dominated by `A_RUNNING` waiters, and the span of a taken job (with P6EarlyCopy) is <= the plain job's hold on both worlds. Go for phase 2 first if: stale refusals > 50% and `A_RUNNING` waits < 30%. No-go for the whole line if: with `VerifyEvery=0` the on arm's sim wait is **higher** than off by more than the slice noise **and** the per-outcome split shows the taken jobs themselves (not the refused ones) carrying it: then the clone run's own span is the problem and the next step is the span, not the waiters.

## 6. Open questions to close before building (each with its closer)

| # | question | why it matters | closer | effort |
|---|---|---|---|---|
| Q1 | Is every write under each gate site (1-11, 13-14, gate-12 writer families, F-G, gate-0 miss insert) a recorded writer leaf in `kSn` or an epoch / lot event? | phase 1's safety net is the stale rule; an unrecorded writer = a stale answer taken | static: docs/87 §3 (gate 10), docs/60 §2.2 (13-14), docs/84 §4 (gate-12 families), docs/86 §2 (gate-0 insert / rehash) against the `kSn` table in `routesnap.cpp`; list per site: recorded / epoch / **not recorded** (stays `A_RUNNING`) | ~1-1.5 h |
| Q2 | Does `P5bAsk`'s path (post, serve, result copy) read anything the sim could free or move while the lock is lent? | phase 3 drops the retake | **closed 2026-10-25: holds, with two code changes (§8.1)**: no lock-protected read on the worker during the ask; the callback must not borrow and its nested plans must plan on the sim; lock model `ask-unheld` clean, its mutation deadlocks | done |
| Q3 | Write locality: how many distinct lots get recorded writes per 20 ms window, and how many lots does a `Route.Plan` clone run read? | phase 2a's expected take rate (10-20% stale is a guess) | offline from an existing replay ring dump or P1b attribution files; else a shadow-mode counter run (`StaleScope=2`) | ~1 h offline, or part of phase 2's verify job |
| Q4 | Was `DoorMemo` on in `night-1024-p6-fix`, and is it in the release ini? | it changes held ms per job between arms | read the two runs' ini / log lines; fix both baseline arms to the same value | ~10 min |
| Q5 | Natural-play ratio r (sim wait per held ms) on IP with P6 on | the §3.6 model uses 0.48 (bundle runs) vs 0.84 (forced loop) | phase 0 natural job | 1 run |
| Q6 | Which `A_RUNNING` waiter dominates per world (gate 10 vs gate-12 writers vs gate-0 misses vs destroys vs collects)? | phase 1's admission order and whether F-G placement needs its own path | phase 0 `p5bHeldOut` per site | counters |
| Q7 | Clone-run span vs the game's plan per kind on IP with asks (3.6 vs 6.9 ms in verify2): does it hold with `VerifyEvery=0`? | if the clone run itself is longer than the plan, no waiter change saves the taken job | phase 0 `p6j*` | counters |
| Q8 | What exactly lands as P6Rebase (reservation says not built) and does it rerun on the same-cut copy? | phase 2b | read docs/94 §24.14 when it lands | ~10 min |
| Q9 | Shares of `nr`, `noclone`, `cold`, storm at Plan rates | phase 2's residual | `m2Fb[*]` in phase 0 | counters |
| Q10 | What does `IC_Route_Plan 0x765160` touch after `Route_PlanLocked` returns (route object, world events, lot-table reads)? | whether the final retake can shrink to the answer move | docs/93 outputs + the IC's disassembly after the planner call; list reads of live routing state | ~45 min |
| Q11 | G1 v2 audit evidence: the per-lot rebase lines of 0.36.55 (docs/94 §24.13.5) must make the audit check pass before phases 1-2 widen the skew window | otherwise every G1 run fails on evidence, not on drift | the queued `night-1025-g1dump3` + report regex | 1 run (queued) |

## 7. Ledger and registry

No ledger section or counter version is reserved here (nothing built). When phase 0 starts: `node tools/registry.ts reserve ledger --for "docs/189 P6 whole-problem plan"` (next after §340) and a native counter version after v122. Claims to carry into the ledger then: I-189-1 the `A_RUNNING` waiters (`p5bHeldOut` 22-30/s) are why P6's hold reduction does not reach the sim [inferred, phase 0 decides]; I-189-2 staleness is global and the write rate makes a high take rate impossible without per-lot scope [verified source + measured rate; phase 2 shadow decides the per-lot rate]; I-189-3 the stale rule is a sufficient safety net for admitting gate writers during a taken job's window [inferred, Q1 decides].

## 8. Phase 0 built and Q1 closed (2026-10-25, ts3native 0.36.57, counters v123, not run)

### 8.1 `[P6Waits]` (phase 0 counters, `p6wait.cpp`, branch `p6/phase0`, built `out-p6p0` -> `game-copy/ts3native.asi.p6fix`)

Observe only, no hook, `[P6Waits] Enabled` default 1 (cost: a 32-slot scan and a few adds per sim job wait, two stores per job end). Every sim-thread job wait is booked by **arm** x **job outcome** x **waiter class**:

- **Arm** = the P6 switch (`p6Plan`, `--ab routesnapplan`) at the job's start (worker, `ServeAsync`), so an in-load A/B splits by job, not by the moment the switch flipped; a wait with no running job takes the switch at the wait's start. Shortcut: the arm key is P6 alone; when phases 1-3 ride in the same build, the integration widens the key (one line in `ServeAsync`).
- **Outcome** (`P6O_*`, set by `P5Plan`, last plan of the job wins): 0 plain live (not armed), 1 taken, 2 taken on a borrowed cut, 3 stale-refused (`p6Stale`), 4 other refusal or verify (nr, cold, capture, epoch, no plan), 5 no running job.
- **Class** (`P6W_*`): 0 `+0x918` pump (`LockAcquireAsync`, M1), 1 `A_RUNNING` gate (`RgEnter`; `t_p5bSite` read before `P5bLock` clears it on a gate-0 miss; M2), 2 `RtWaitJob` (destroy, drains, saves; M3), 3 result (`RunPlan`'s `WaitAsyncDone`, the collect's `WaitJobDone`; M5), 4 `+0x918` pump in which the sim served the job's host calls (M4). Other-lock pumps are not job waits and are not booked.
- A wait that ends before its job (P5b / P5c lend the lock mid-job) waits in a 32-slot pending ring; the worker publishes (seq, outcome, arm) in a 16-slot ring at the job's release and the sim books pending waits at its next wait. Overruns count in `p6wLost`.

Counters (v123, 0x4BBC-0x4E7B): `p6wN` / `p6wUs` `[arm * 30 + out * 5 + cls]`, `p6wJobN` / `p6wSpanUs` `[arm * 5 + out]` (`A_RUNNING` span, IC call to release), `p6wGateUs` `[arm * 16 + site]` (Q6: which gate dominates per arm), `p6wLost`, `p6wServeN` / `p6wServeUs`. Log lines per stats block: `p6 waits arm <a> <outcome>: jobs .. span .. ms; lock n/ms gate .. rtwait .. result .. host ..`, `p6 waits arm <a> gates (site ms)`, `p6 waits: host calls served ..; lost ..`. `p5bHeldOut` per gate site (§4 phase 0 row) did not fit the 176-int32 reservation next to the arm split; `p6wGateUs` per site per arm carries the same ranking. Selftest `TestP6Waits` (layout, pending ring, arm taken at job start, slot reuse, owner-only). The probe agent reads v123 once `tools/probe/agent.js` from `p6/phase0` is merged; until then the job reads the log lines.

Baseline run: `tools/harness/queues/p6fix-verify.json` job `ip-p6fix-ab` is the §5 job 1: in-load ABBA on `routesnapplan` + `routesnapearly` (on arm = P6 + P6EarlyCopy; no third arm), `VerifyEvery=0`, `ShareCuts=1`, `DoorMemo=0` in both arms (Q4: not set in `night-1024-p6-fix` either, default 0), `[MovedWaits]`, `[HitchAttr] Holders=1`, clean machine. §5 job 2 (natural play) is not queued.

### 8.2 Q1: gate-site writers against the recorder (static) [verified source unless marked [doc]]

Recorder: `kSn` in `routesnap.cpp:51-89`; `SpNote` (`snap.cpp:576-586`) bumps `g_spWrites` at a leaf's entry for every row but `SL_SRC`, and for `World_FireEvent` ids 5 / 7 / 9 / 0x0A (epoch) and 0x0D-0x10 (lots / grid). Site numbers = `kRgSites` index (`routegate.cpp:29-49`; `t_p5bSite` = index + 1).

| site | gate | writes it protects | recorded? | evidence |
|---|---|---|---|---|
| 0 hit | `0x62EAB0` lot table find-or-insert | none | read-only | docs/86 §3; `p5b.cpp:78-83` |
| 0 miss | same | `LotTable_Insert 0x62E680` (rehash `0x625940` only from it) | yes, W5 | `routesnap.cpp:55` |
| 1 | `0x63F490` goal-step lot planner | Q / stamp (scratch), site 0 (W5), site 2 (W4), room-record fill | partly: W4 / W5 yes, room-record fill **no** | `routesnap.cpp:55,64`; docs/87 §3 [doc] |
| 2 | `0x63D2F0` lazy room connectivity | `LotGraph_Build` (W4), room records `0x63D36A` + `RoomToRoomAStar 0x6339F0` | W4 yes; room fill **no** (lazy cache fill from unchanged inputs [inferred]) | `routesnap.cpp:64`; docs/112, docs/91 [doc] |
| 3 | `0x63CB90` lazy lot build | itself | yes, W4 | `routesnap.cpp:64` |
| 4 | `0x63D7B0` lot rebuild | W4; caller `0x82B810` -> ApplyLotUpdate | yes, W4 + SL_APPLYLOT | `routesnap.cpp:64,70` |
| 5 | `0x63D860` room graph refresh | unknown | **unknown = not recorded** | no writer list in docs |
| 6-9 | Q resets `0x61E320` / `0x61E420`, stamp `0x61E360`, node clear `0x61C020` | search scratch, not routing inputs (the clone's are redirected) | no coverage needed | `p4.cpp:97,186` |
| 10 | `0x75C2D0` goal step (coarse) | Q, W1 / W2 (flush), W4, W5, W6, lot index `0xC68680`, quadtree pin, footprint refcounts | W1 / W2 / W4 / W5 / W6 yes; lot index via the nr family; pin and refcounts **no** (counters, not search inputs) | `routesnap.cpp:52-64`; `p4.cpp:397,411`; docs/87 §1 [doc] |
| 11 | `0x7929F0` GetConnectedRooms (coarse) | through `0x63D2F0` | as site 2 | docs/94 [doc] |
| 12 | `0x628A90` LotTable_Contains | none | read-only | docs/84 §4.2 [doc] |
| 13 | `0x835240` OnExitBuildBuy (coarse) | lock-key map free `0x82C630`; water grid `0x82A030` only with `+0x983` | map **no**; water grid SL_GBUILD on that branch only | `routesnap.cpp:78-80`; `snap.cpp:618-624` |
| 14 | `0x835110` RebuildLotRouting (coarse) | partition rebuild `0xB54B60`, lot-edge portals `0x832890`, `0x630CF0`, `RestoreLotBlockedBits 0x82A190` | SL_RESTORE yes; partition / portals **no** | `routesnap.cpp:71`; docs/101, docs/105 [doc] |
| 15 | `0x78C440` FindPlacesOnShoreline (coarse) | in-room A* scratch on live `P+0xBC`; lazy W4 via `0x63D2F0` | as site 2 (missing from §3.1's list) | docs/67 §13 [doc] |
| F-A | gate 12 key `0x80E5B0` -> `0x80CB40` | W6a, W6g | yes, W6 (already admitted) | `routesnap.cpp:56,62-63`; `p5b.cpp:93` |
| F-B / F-C | `0x62F782` + `0x76F15D`; `0x631814` | none at the leaf (F-C tail vcalls unresolved) | read-only (already admitted) | `p5b.cpp:74-75`; docs/155 [doc] |
| F-D | portal ICs `0x758110`, `0x758220`, `0x757E10`, `0x82D2E0`, `0x74B490` | portal cost / flags / entry vector | not in `g_spWrites`; refused by the nr portal family | `snap.cpp:618-624`; `p4.cpp:402-406` |
| F-E | `0x7883C0` -> `0x631720` | lot entrance | **no** (IC slot 34: no flag, no write count, no nr) | `routesnap.cpp:130`; `snap.cpp:618-624` |
| F-F | `0x834B80`; `0x7556B0`; `0x764910` -> `0x823800` | lazy terrain footprints (W1); PlaceGroupSocial writes unknown | W1 yes (the frame is SL_SRC); `0x823800` **unknown** | `routesnap.cpp:52,88` |
| F-G | `0x631F40` in `0x631F10` | unlocked tail `0x667480`: pin + flush -> W1 / W2 | flush yes, pin no; the issue is the docs/155 data race, not staleness | `p5b.cpp:72-73`; docs/155 §4 [doc] |

**Order check [verified source].** `P5cTake` retakes `+0x918` before the stale test (`p5.cpp:383` -> `P5bEnd`, `p5c.cpp:50`); `P5cStale` (`p5c.cpp:54-62`) then compares `g_spWrites` with the START cut's count, and `SpNote` counts at leaf entry, so every recorded write in the lent window is seen. Caveats: `P5cStale` returns false when `!t_p5cRan` (verify, P5c off, refused window), so phase 1 must lift a gate only in windows with `t_p5cRan`; a lockless writer that starts after the test is not counted (exists in P5c today); `g_spOn` 0 or a missing stub counts nothing (the clone acquire already refuses with `g_spOn` 0, `p4clone.cpp:413`).

**Verdict: phase 1's safety holds for a subset of gates, not for every gate.**

- Admit: gate-0 misses (W5), sites 3 and 4 (W4), 6-9 (scratch), 12 (read-only), F-A, F-B, F-C (already admitted), F-D (nr refuses).
- Admit only after closing the room-record fill (prove a fill from unchanged inputs cannot change an answer, or add `0x63D36A` / `0x6339F0` as a `kSn` leaf): sites 1, 2, 10, 11, 15. Gate 10 is the largest `A_RUNNING` waiter (§1 M2), so this closer comes first in phase 1 (~1 h static, or one leaf + selftest).
- Stay on `A_RUNNING`: site 5 (writers unknown), 13 and 14 (lock-key map free, partition and portal rebuilds unrecorded; rare, little gain), F-E (lot entrance unrecorded), F-F's `0x823800` path (unknown), F-G (race).
- Outside the gate list: IC slots 15-16 (`SetOceanPortalOwner`, `DestroyPortalHandle`) and 34 are neither counted nor nr-refused; any gate that reaches them stays on `A_RUNNING`.

Ledger: docs/51 §341 (I-189-4..I-189-6 new; I-189-1..3 from §7).
## 9. Phase 1 built: `[RouteGate] HoldKeyed` (ts3native 0.36.58, counters v124, branch `p6/phase1`, NOT run in game)

Built 2026-10-25 on master 0.36.56 with a v123 placeholder (176 int32 at +0x4BBC) until phase 0's branch merges. Ledger docs/51 §342. Sources: `p5b.cpp` (`P5bAllowed`, `P5bLock`, `P5bBegin` / `P5bEnd` / `P5bAbort`, `P5bRetake`, `ApplyP5b`), `routegate.cpp` (`LockReleaseHook`, `RgEnter`), `ts3native.cpp` (`LockAcquireAsync`), `p5c.cpp` (window rules, unchanged), §8.2 (Q1).

**No new job state.** "The worker holds `+0x918`" (A_HOLD) is already published: `g_async.state == A_RUNNING` and no P5c window (`g_p5bWin` 0, published with the Dekker on `g_p5bIn` that `P5bOpen` uses) [verified source]. The window closes for every door-ask retake, the final retake and any tripwire retake, and reopens after an ask, so HOLD toggles as §3.1 asks. A gate wait already goes through `LockAcquireHook` -> pump -> `P5bLock` on every try (`LockAcquireAsync`), so re-keying a site = admitting its wait to the lent lock in `P5bAllowed` (class `P5B_HOLD`). The admitted entry then holds the real `+0x918` for its call (RgEnter's `RG_REL_LOCK` frame): the worker's retake waits for it as for a P5c-admitted writer, and `g_rgOffSimOpen` keeps job starts off as before.

**Switch.** `[RouteGate] HoldKeyed` (default 0), runtime switch `holdkeyed` (`native_set holdkeyed`, `--ab holdkeyed`, counter `hgOn` +0x4E7C). Acts only with `[RouteSnap] FreeLock=1` (`hgReady`). Latched per job: `P5bBegin` copies `hgOn` into `g_hgJob` before the window opens and only for a P5c job (`g_p5cJob`, so only `t_p5cRan` windows: the caveat of §8.2); verify jobs, refused windows, `P5bEnd`, `P5bAbort` and the final retake clear it. A flip between jobs never changes a running job.

**Per site [verified source unless marked].**

| site | gate | verdict | why HOLD suffices / why kept |
|---|---|---|---|
| 0 | lot table find-or-insert `0x62EAB0` | re-keyed (hits and misses) | a miss inserts through `LotTable_Insert` (recorded W5, §8.2) under the real `+0x918`; the clone run reads its own P copy (docs/94 §12) |
| 3 | lazy lot build `0x63CB90` | re-keyed | recorded W4; the clone has its own lot copies and portal references (H20b); a lazy build inside a clone run stops it (D4) |
| 4 | lot rebuild `0x63D7B0` | re-keyed | recorded W4 + `SL_APPLYLOT` (§8.2 [doc] for the caller); old portals the clone holds stay alive through its own references (H20b exists for rebuilds beside a clone) |
| 6-9 | Q resets, stamp, node clear | re-keyed | search scratch; the clone's Q, stamp and node table are redirected (docs/94 §12, `p4.cpp`) |
| 1, 2, 10, 11, 15 | goal-step lot planner, lazy room connectivity, goal step, GetConnectedRooms, FindPlacesOnShoreline | **kept** | Q1 conditional: the lazy room-record fill under `0x63D2F0` is not recorded. Closer: prove the fill cannot change a clone answer (the clone copies room vectors, H20b [inferred]) or add `0x63D36A` / `0x6339F0` as a `kSn` leaf; then add bits 1, 2, 10, 11, 15 to `kHgSites`. **Closed by §10: the fill is harmless to a clone answer (no recorder leaf needed); the sites are re-keyable** |
| 5 | room graph refresh | kept | writers unknown (Q1) |
| 12 writer families | `0x62F782` (not F-B), `0x62FB74` | kept | F-D / F-E / F-F share these keys and are not told apart without a second-frame check; F-E and F-F `0x823800` are unrecorded (Q1). F-A (C-A key `0x80E5B0`) and F-B / F-C are already admitted |
| F-G | `0x631F40` | kept | docs/155 data race of the unlocked tail |
| 13, 14 | coarse rebuilds | kept | unrecorded lock-key map / partition / portal rebuilds; D2 waits for the clone run anyway |

**Safety net.** A recorded write by an admitted entry lands in the window, so `P5cStale` refuses the answer at the retake (it tests after `P5cTake`, §8.2 order check); the game's plan then answers under the retaken hold as a fresh `Route_PlanLocked` (docs/94 §22.11.6 R3). The P5c deadlock brackets (`P5cHandBegin` for job waits under the lent lock, `P5cPlanHere`) cover the admitted holders unchanged. **Tripwire:** an admitted outer acquire sets `t_hgHeld` and `g_hgOpen`; the outermost release (`LockReleaseHook`, count 1) clears them before the lock goes; every worker retake (door ask, final, tripwire) checks `g_hgOpen == 0` with all levels back, else `hgTrip` +1, a FAIL line and `hgReady` 0 for the session (a P5c hand-over turns P5c off first and is not a trip).

**Selftest** (`TestP5cIn` phase 3: jobs 270+ with FreeLock + HoldKeyed in the threaded fake world, real CRITICAL_SECTION for `+0x918`): re-keyed gates (sites 3, 4) and gate-0 misses get the lent lock inside job bodies (32/36 and 9/11 in the build run; 0 in the held, P5b and FreeLock-only phases), kept gates (F-G, site 10) never; 1 in 4 admitted gate entries makes a recorded write, and every such job's answer equals the live world at the IC's tail (the existing skew check: refused, never taken stale); door asks retake and reopen with admitted holders around (tripwire 0); the frame's object is never freed under the frame. Single-thread: the tripwire trips with `g_hgOpen` set and stays quiet after a hand-over. Build run + 2 reruns pass.

**Counters v124** (+0x4E7C, 48 int32): `hgOn`, `hgReady`, `hgAdmit[16]` (per gate site, admitted outer acquires in a window), `hgJobs` (windows with an admitted gate), `hgStaleAfter` (of those, a recorded write inside the window: refused by `P5cStale`), `hgTrip`, `hgSpare[27]`. Kept-site waits held out in windows = phase 0's per-site `p5bHeldOut` in the on arm.

**Measurement note [verified source].** `p5bHeldOut` (and `p5bFgHeldOut`) count pump **tries**, not waits: `LockAcquireAsync` calls `P5bLock(..., kTry)` about once per millisecond while it waits, and each call that sees the window counts once. So the 22-30 per second of §0 / §2 are milliseconds of held-out waiting (at 1 ms timer resolution), not 2-3 distinct waits per job; re-read §2 rank 1 with phase 0's per-wait counters.

**Run (not queued; the integration agent writes the combined P6 queue).** Correctness BP + IP with `VerifyEvery=2`, P6 + EarlyCopy + HoldKeyed on: no crash, `m2VerBad` 0, `p6BorrowVerBad` 0, no P4 FAULT, `hgTrip` 0, no retake watchdog / escalation line. Timing: IP in-load ABBA `--ab holdkeyed`, repro `route-queuefull-hold-loop` (P6 pair on in both arms), `VerifyEvery=0`, `[MovedWaits]`, phase-0 counters: sim job-wait ms/s by waiter class, `p5bHeldOut` per site, hitch counts. Expect less than §3.6 row 3 until gate 10 is re-keyed.

**Risks.** (1) An admitted holder that runs long delays the retake; the retake escalation (4 s) then turns lending off for the session (sites 0/3/4/6-9 reach no sync plan [inferred from docs/87 §3 hop lists]). (2) docs/94 §22.11.6 R1 (an admitted holder waiting for a game lock the clone run holds during a retake) widens to the re-keyed sites [inferred, open]. (3) Q1 rows for sites 0 and 4 lean on [doc] reads (docs/86, the `0x82B810` caller); sites 3 and 6-9 are fresh source reads. (4) Not done: lock-model `hold-*` configs and mutation in `tools/model/p5-lock-model.ts`; G1 v2 with the switch on.

**Effort:** ~2.5 h (reading, code, selftest, docs). Left: two correctness jobs and one ABBA in the combined queue.

## 10. The room-record fill under `0x63D2F0` vs the stale rule (2026-10-25, static, branch `p6/roomrec`, no code)

Question (§8.2 Q1, §9, ledger I-189-4 on `p6/phase0`): gate sites 1, 2, 10, 11 and 15 all reach `RoomGraph_FirstUse 0x63D2F0`, whose room-record fill the `kSn` recorder does not see. Can an admitted gate's fill in a lent window make a taken clone answer wrong without `P5cStale` noticing? Sources: Ghidra headless decompile of `0x63D2F0` and `RoomToRoomAStar 0x6339F0` with their depth-1 callees (game copy, kept out of the repo), `replay.cpp` (`RpLotShell` room copy, `RpRoomListsCopy`), `p4clone.cpp` (header, `PcPrepare`), `p4.cpp` (D4), docs/18 §3 (node table), docs/94 §22.11.2.

### 10.1 What the fill writes [verified source unless marked]

`0x63D2F0` (thiscall on the planner P; arguments: lot graph G, room index) builds the lot first when `G+0x8C` is 0 (`LotGraph_Build`, site 3, recorded W4). Then, only when the room record (`G+0x90 + room * 0x44`) has its built byte `+0` at 0, it fills that record:

| write | target | who else reads it |
|---|---|---|
| portal index list `+0x24` (u16 per portal with no connectivity yet), connectivity `+0x34` (one vector of (portal, cost) pairs per portal, both directions), built byte `+0` = 1 | the live room record | live plans only; the clone copies every room record with private EA blocks for `+0x24` / `+0x34` and private `+4` / `+0x14` portal vectors (`replay.cpp` H20b + 0.32.6), and a lot whose room copy fails is not built (`RpFailW`: no clone run reads a live room vector) |
| EA free of the old `+0x34` inner buffers (`0x61EE20`), resize (`0x622A30`), grows (`0x5D1D60`, `0x6202E0`) | the live room record's own vectors | as above: the clone never holds a pointer into them |
| `P+0xBC` reset `0x61E320` before and after | the live planner A* object | search scratch (sites 6-9 class); the clone run uses a private A* object (`p4clone.cpp` header: `P+0xBC` and every clone Q's `+0x28` redirected) |
| A* nodes made on demand (`0x6320D0` -> `AStarNode_Get 0x664B10`), their int refcount `+0x28` (+1 / -1), the cell object's word refcount `+4` (+1 / -1), its start position `+0x28` / `+0x2C` | the live node table under live `Q+0x28` | search scratch: the clone's Qs point at a private node table (same redirect); docs/18 §3 node table [verified there] |
| the in-room search itself (`0x61E8A0`) | an A* container on the caller's stack (zero heuristic `0xFEBCC0`) | nobody |
| lot table find-or-insert `0x62EAB0` (via `0x6320D0` and `0x6339F0`) | live P's lot table | gate site 0: a miss is `LotTable_Insert`, recorded W5 |

Reads: the room's portal pointers (`+4`, portal `+0x14` / `+0x20` positions, `+0x48` room, `+0x58` flags), `G+0x16C` (outside room), the level Qs and their footprints (in-room A*), the lot table. All are routing inputs whose writers are recorded (W1-W6, `SL_APPLYLOT` for the room reset `0x630F70` via `0x82B810`) or refused by the nr portal family (portal ICs) [verified source for the recorder rows, §8.2; the in-room A*'s full read set is docs/98, not re-read here].

### 10.2 Verdict: (a) harmless, no recorder leaf needed [verified source, one step inferred]

1. **The clone never reads what the fill writes.** Room records, their vectors, the A* object and the node table are all private in a clone run (table above). The only shared write is the lot-table insert, and that is recorded. So an admitted fill in the lent window cannot change the bytes a clone run reads.
2. **The fill does not change a routing input.** It is a cache of (portals, in-room geometry, outside room) [inferred from the read list; the room record has no other writer than this fill, the room reset under `SL_APPLYLOT` and `LotGraph_Build` under W4]. When the live fill and the clone's copy (filled at the cut, or filled privately by the clone run on its own EA blocks) could differ, an input changed, and that input's writer is recorded: `P5cStale` refuses the answer at the retake. A fill that differs with no input change would be a nondeterministic fill, and that would already split live and clone answers today (P5c has run with the same fill on the sim between jobs since 0.32.6), so admitting the gate adds no new case.
3. **No race on the copies.** Our code reads live room records only at the cut (`PcPrepare` first use, `RpLotShell` W4 shell refresh), on the sim under `+0x918`; an admitted gate holds the real `+0x918` (RgEnter's frame), so the two are serialized. `RecsUnlocked` rounds apply INS / REM / NOQ / STALE / SYNC records only and never copy a room (docs/182).
4. **The suspended IC frame** holds no lot-graph or room pointer across `Route_PlanLocked` (docs/94 §22.11.2 [inferred there]); a refused answer falls back to a fresh `Route_PlanLocked` under the retaken hold.

So **sites 1, 2, 10, 11 and 15 are re-keyable** on the Q1 axis: add bits 1, 2, 10, 11, 15 to `kHgSites` (`p5b.cpp`; bit = site). Gate 10 is §1 M2's biggest waiter. Not built here (task scope: proof only). The integration step should put the extra bits behind a runtime-flippable `HoldKeyed=2`, latched per job like `g_hgJob`, and extend `TestP5cIn` phase 3 so site 10 is admitted with `HoldKeyed=2` and kept with 1.

**Left open, not part of this question.** (1) Gate 10 runs the game's own sync `Route_PlanLocked` right after the goal step, so an admitted gate-10 holder can hold `+0x918` for a whole sync plan. That widens §9 risk (1): a long holder delays the retake and can trip the 4 s escalation, which turns lending off for the session. Watch `hgAdmit[10]` against retake waits in the timing run. (2) The lot index rebuild `0xC68680` under gate 10 relies on the nr family (`P4R_LOTIDX`) as §8.2 says; whether the clone's live read of `wm+0x228` is safe while a rebuild runs was not re-checked here. (3) Sites 5, 13, 14, F-E, F-F `0x823800` and F-G stay as §8.2 says.

Ledger: docs/51 §347 (I-189-60, I-189-61; I-189-60 closes I-189-4 of `p6/phase0`).
## 11. Phase 2a as built: per-lot staleness (`[RouteSnap] StaleLot`, 0.36.59, counters v125, branch `p6/phase2`, not run)

Built 2026-10-25 on master 0.36.56 (phase 0 / 1 reservations v123 / v124 kept as zero placeholders). Record in docs/94 §24.16, ledger §343 (I-189-4..7). One switch, flippable per plan in one load (runtime `stlMode` at `+0x4F3C`, `--ab routesnapstalelot`): 0 off, 2 shadow, 1 acting. The ini key `StaleLot` (1 or 2) installs the ring and the detour at load; 0 installs nothing.

### 11.1 Write side: what stays global

A 1024-slot seqlock ring next to `g_spWrites` (`snap.cpp` `SpWrite`): each `SpNote` increment stores its lot key or its world kind. Lot-keyed: W1 / W2 (the Q's lot `+0x18/+0x1C`), W3, W4, LOTREMOVEFP. **Global (any one in the window = stale, as today):** epoch events 5/7/9/0x0A, lot events 0x0D-0x10, R (APPLYLOT / RESTORE), footprint grid and blocked-node map, high-cost tiles, G build / blend, footprint objects (FPCORE / PRIVATE / FPREMOVE), W6 polygon caches, the lot table (W5, REMOVELOT), lot key 0, and every increment outside `SpNote` (lock-key cover, fault injection: no ring entry = unknown). Footprint leaves stay global because the footprint object and its polygon caches are state outside one lot's Q; giving them lots needs the footprint map read off the sim thread (next step, only if the shadow says they dominate).

### 11.2 Read set: definition and proof level

The keys the clone run passes to `LotTable_Find 0x627E00` (worker, inside P5Plan's `P4RunClone`), plus the active lot. `0x627E00` is the one key lookup under `LotTable_Contains` and `LotTable_GetOrInsert` [verified, raw call scan: 4 callers]. **Not proven complete:** lots reached by pointer (portal or R hub lot graphs, Qs through the footprint map `world+0x95C`, inline walks) are not recorded, and the read set cannot be bounded before the search. A static superset (all lots) equals the global rule. So acting is guarded: it is only as good as the shadow evidence, and a per-lot-only verify mismatch turns it off for the session (`stlActOff`).

### 11.3 Counters v125 (`+0x4F3C`, 48 int32)

`stlMode`, `stlReady`, `stlJobs`, `stlMat[arm][4]` (both clean / per-lot-only / both stale / global clean + lot stale, must be 0), `stlWhy[6]` (read lot, world, unknown, ring, set cap, no set), `stlWorld[12]` (first world kind), `stlWrites[2]`, `stlReadHist[8]`, `stlVer[3]` (per-lot-only verify: equal / differs / n.c.), `stlNoRun`, `stlFinds`, `stlFaults`, `stlActTaken`, `stlActOff`. agent.js `>= 125`.

### 11.4 Shadow plan and expected take rate

1. Shadow, `VerifyEvery=1 VerifyLend=1` (every armed job runs the clone in the lent window, then the game's plan): `stlMat[0]` gives the per-lot-only share of written windows, `stlVer` checks each per-lot-only answer against the game's. Pass: no crash, no P4 FAULT, `stlVer_1` = 0, `stlMat_3` = 0.
2. Only then acting with `VerifyEvery=2` (half the jobs still verify and feed the tripwire): `stlActTaken`, take rate, `stlActOff` = 0.

Expected [inferred]: small in this build. W6 (~430/s) and footprint removes / updates (~330/s) are world-level and land in most 10-20 ms windows, so per-lot-only is likely well under half of today's 50-67% stale share; `stlWorld` names the next lever (footprint leaves to lots). Risk: wrong routes if the read set misses a lot (guarded by shadow first and the tripwire; verify jobs are never taken), one more detour on a hot lookup (installed only with `StaleLot` != 0; ~TLS check per call), merge conflicts on the counter struct and the size asserts with phases 0 / 1.
## 12. Phase 2c built: P6 takes every own job kind (0.36.57, counters v127; branch `p6/allkinds`, not run)

Written 2026-10-25 (PHT). Off by default; nothing changes unless `[RouteSnap] TakeKindsAll=1` or the runtime switch `routesnapkinds` (`p6All`, +0x503C) is on. Built in parallel with phases 1 (`A_HOLD`), 2a (per-lot stale), 3 (door asks) and 4 (own result wait / foreign jobs); this part touches only the arm decision and counters, not the stale rule, the ask path or the gates.

### 12.1 Job kinds: taken before, added, left out

| kind | before | now | why it was not taken | what taking it needs | safe? |
|---|---|---|---|---|---|
| 0 own Route.Plan | taken with `TakeKinds` bit 1 (`p6Plan`, `routesnapplan`) | unchanged | - | - | as docs/94 §24 (G1 BP 10,075 compares, M2 0) |
| 3 own ReplanFromPosition | taken with `TakeKinds` bit 8 (P5 default) | unchanged | - | - | as docs/94 §19-§22 |
| 1 own Replan (A10) | never: `P5OnStart` armed kinds 0 and 3 only; `ApplyP5` kept `TakeKinds & 9` [verified source] | **taken with `TakeKindsAll`** | not built: no live shadow compare of this kind ever ran (`[RouteAsync] Kinds` bit 1 never ran with P4, docs/94 §22.29 next steps item 2); A10 is off in the release config | nothing new in the clone. The IC is `IC_Route_Plan`'s layout plus a `route+0x1F4` bump (a route field, not routing state) before `Route_StartPointsFromPosition` (docs/87 §5 as built); start points are built live under the job's hold as for kind 0; the first `Route_PlanLocked` is captured (`t_p4Cap`) and `P5Plan` has no kind branch besides counters; outputs = out list + out1 / out2 + the IC's own 0x48-byte result, as kinds 0 / 3; no managed entry before the planner (I-87-10); door asks proxied as for every kind; a followed route is refused at START (`arActive`, I-87-11) | yes by source (I-189-4); live proof = Mode=1 shadow or `VerifyEvery` jobs, see 8.4 |
| 2 own PlanFromPoint (A10) | never (same two filters) | **taken with `TakeKindsAll`** | same as kind 1 | start points from `Route_StartPointsBuild 0x7642C0` with the job's own copy of the point (`j->pt`), the same builder as kind 3, which is taken today; the IC first runs lot-local pre-checks, read-only and inside a Route.Plan job's closure (I-87-10, I-189-5) | yes by source; I-189-5 inferred |
| 0 foreign (`fb`, FallbackAsync) | never armed | unchanged | its `FbJobCapMs` deadline (docs/94 §22.32.3: no Mode=2 job has a deadline); not an own kind | phase 4 (docs/189 §4): arm with the deadline kept | left to phase 4 (P4 agent) |
| 1-2 foreign (`af`, docs/176 ForeignKinds) | never armed (kind filter) | **still never armed**, counted `pkAfSkip[kind-1]` | not own | phase 4 | left to phase 4. Note: an `af` kind-3 job **is** armed today (P5OnStart skips only `fb`, I-189-6); unchanged here |

Jobs of a taken kind that still fall back (stale, `nr`, `cold`, `noclone`, storm, capture) are phase 2a / 2b / 2d's: the stale rule (`P5cStale*`) was not touched.

### 12.2 As built

- `p5.cpp`: `P5TakeMask()` adds bits 2 | 4 when `p6All` is on. `P5OnStart` accepts kinds 0-3 (not `fb`); an `af` kind 1-2 START returns before arming (`pkAfSkip`); a kind outside `Kinds` returns as before; an own START of a kind in `Kinds` but outside the take mask counts `pkUntaken[kind]` (what the switch would add, read on the off arm). The switch is read once per job at its START: that is the take decision (a job armed on is still taken after the switch goes off; armed off is never taken), as `p6Plan` does.
- The P6 [2] split (`p6Armed`, `p6Plans`, `p6Taken`, `p6Borrowed`, `p6GameUs`, `p6PlanUs`: [0] Route.Plan, [1] ReplanFromPosition) and the §24.7 phase split (`p6T0` now set only for kind 0) are not fed by kinds 1-2, so their dT reads stay Route.Plan's.
- M2-1 site guard: with `p6All` on at install, the A10 collect detours `0x765490` (Replan) and `0x765240` (PlanFromPoint) are snapshotted when A10 hooked them (`arReady` bits 1 / 2), as `0x765160` is for `p6Plan`. Same limit as `p6Plan`: turned on only at runtime, those two sites are not guarded.
- `ApplyP5` logs `TakeKinds` bits 2 / 4 as ignored (use `TakeKindsAll`), and a `TakeKindsAll=1` without `Kinds` bits 2 / 4 or without A10 hooks for kinds 1 / 2.
- Counters v127 (docs/registry.md): `p6All`; by kind 0-3 `pkArmed`, `pkPlans`, `pkTaken`, `pkStale` (P5c write since the cut, early or at the retake), `pkVerify`, `pkFb` (every other refusal incl. `nr`), `pkPlanUs`, `pkGameUs`, `pkUntaken`; `pkAfSkip[2]`. Per arm = the harness's per-slice counter deltas (`--ab routesnapkinds`). agent.js `>= 127` (with a `pkGap123[288]` placeholder for v123-v126 until those phases merge); `native-switches.ts` `routesnapkinds: 0x503C`; repro `replan-buildbuy-p6kinds-loop` (two build/buy exits per firing replan every routing Sim; which IC each script replan reaches is inferred, read `arStarted_<kind>`).

### 12.3 Expected take-rate change [inferred]

- **Release config: 0.** `[RouteAsync] Kinds=0`, so no kind 1-2 job starts (`AsyncStart` refuses an unhooked kind, I-189-7).
- **With A10 on (`[RouteAsync] Kinds=7 KindsMode=1`):** the P10 live sync plan time per 10 min (docs/87 P10, BP / IP) was Replan 3.6 / 4.9 s, PlanFromPoint 6.3 / 16.8 s, ReplanFromPosition 8.2 / 46.0 s. So the newly armable job time is ~1.2x (BP) / ~0.5x (IP) of the ReplanFromPosition time P5 already arms, and small next to Route.Plan (86-100% of the release hold, docs/158). The per-job take rate of the new kinds is the same stale-limited 14-29% as kinds 0 / 3 until phase 2a lands; the gain is more taken jobs, not a higher rate.

### 12.4 Checks, and what must pass before a run is read

- Offline: `build.cmd out-p6pk` (all targets, `/W4 /WX`) and its selftest ok, incl. `TestP6Kinds` per kind 1 and 2 on the fake world (inside `TestPcMode2`'s clone life): off = not armed + `pkUntaken`; on = armed and taken from the clone; decided at START (flip after arm); `af` not armed (`pkAfSkip`); `Kinds` gates; a write after the cut refuses stale (`pkStale`); `VerifyEvery=1` verifies the kind and the §13 record names the IC (`"ic":"Replan"` / `"ic":"PlanFromPoint"`); the P6 split and phase split unchanged. `node tools/selftest-all.ts` ok. The lock model (`p5-lock-model.ts`) is kind-independent: no new config. Not run: any game run, steerro-test.
- Live gate before any gain read (G1 for these kinds; no compare has ever run): one correctness job with `[RouteAsync] Kinds=7 KindsMode=1`, `[RouteSnap] Mode=2 Kinds=15 TakeKinds=9 TakeKindsAll=1 VerifyEvery=1 VerifyLend=1` on `replan-buildbuy-p6kinds-loop`, BP then IP: pass = `pkVerify_1`, `pkVerify_2` > 0, `m2VerBad` 0, G1 v2 tier 3 = 0 for the Replan / PlanFromPoint records, `m2Off` 0, no P4 FAULT. Then `VerifyEvery=2` for takes. Effort: ~10-15 min per job, 2 jobs; no clean night needed for correctness.
- Risk: medium-low. The take path is the one kinds 0 / 3 already use; the new inputs are two start-point builders the clone run never reads (it reads the captured start list). A missed difference would show as verify M2 / M3 per IC before any take.
## 13. Phase 3 built (2026-10-25, ts3native 0.36.60, counters v126, branch `p6/phase3`, NOT run)

Ledger §344. Both switches are off by default and can be flipped at runtime in one load (one asi for every P6 phase; tests flip switches). Built on master `4c913d71` (0.36.56). Phases 0-2 (v123-v125) are a padding block in the counters (`p6PhasePad`) until they merge.

### 13.1 Q2: is a door ask with `+0x918` lent safe? Yes, with two changes

What the worker does between `P5bAsk(true)` and `P5bAsk(false)` (`P4DoorEnter`, `p4.cpp` door stub) [verified source]:

| step | reads / writes | protected by `+0x918` today? | verdict with the lock lent |
|---|---|---|---|
| `PoolsLeaveJob` / `PoolsEnterJob` | the worker's private pool set (`g_plJobTid`, SRW `g_plSet`), per thread | no | safe |
| `P4Disarm` / `P4Arm(ctx, false)` | `t_snapRun`, `g_snapCtx`, `g_snapRunTid`; no live read (`waitNr` false) | no | safe |
| `HcDoorStub` (DoorMemo / DoorMemoX) | worker-only tables | no | safe |
| `World_FirePortalLockedQuery 0x791C00` -> `World_FireEvent 0x78F2B0` | args 1-9 by value (I-94-141), the event 59 delegate slot `0x11D72C4` (docs/104 P1) | no: the slot's writer `IC_World_RegisterCallbackDelegateImpl 0x7861A0` runs in script on the sim, which the retake never blocked [inferred: the retake only stops `+0x918` acquirers] | unchanged by AskLent |
| host call `0xD75FA0` | proxied: `Proxy` -> sim `ServeInvoke`; the answer in eax -> `g_p4DoorAns` (worker only) | no | safe |
| the clone run's parked frames | clone structures (option G redirect) plus live portal objects and `FindById` pointers | lifetime comes from `A_RUNNING` (`RtWaitJob`) and `DestroyPark`, not from the lock | unchanged: the same frames are already lent during the run |

docs/159 §3 gives three reasons to keep the hold during an ask. All three are about a **live** job. For a clone job:

1. Mid-search frames are private, so a lent window cannot move them.
2. The callback's acquires and nested plans. The borrow rule (`LockAcquireFull`, `RgEnter`) assumes the worker really holds the lock. With the lock lent, the callback would write while nobody owns it. Fix: under `g_auAsk` the callback's `+0x918` acquires skip the borrow and take the real lock (`P5bAllowed` -> `P5B_AU`, also under an outer gate wait's key). `RunPlan` plans nested plans on the sim instead of sending them back to the worker, which would then wait for a lock the sim may hold. Gate entries inside the callback still pass as borrowed (`routegate.cpp` unchanged). Gates order the sim against the worker's live plan, and the worker runs none during an ask [verified source].
3. "A lend only moves the wait." For a clone job the lend already exists for the whole run. AskLent only removes the retake / reopen pair, so the sim's takers pass during the ask's queue time as they do during the run.

Nested plans [verified source]: inside the event 59 call `CanYield` is false (native -> managed), so a `Route.Plan` there is a NoYield sync plan (`JyBusy`, START refusals), i.e. `RunPlan`. With AskLent it runs `CallPlan` on the sim under a real admitted acquire. It never touches `g_async` or the outer job, and the worker is blocked in `Proxy`, so no clone read runs at the same time. docs/185's "never touches the outer job" holds for the lent case. Deadlock cases: if the sim already holds the lent lock (admitted) when it serves the ask in a pump, the callback's acquire is recursive; if a third thread holds `+0x918`, it is a real wait that ends without the job. Lock model: `ask-unheld` (14,425 states) and `ask-unheld-share-rp` (with ShareCuts, the replay consumer and RecsUnlocked; 114,908 states) are clean; `mut-ask-unheld-noadmit` (the callback held out by the open window) deadlocks as expected.

Stale rule [verified source]: a routing write in the callback, or by an admitted sim writer during the ask, goes through `SpNote` on the sim (not a clone run, not the consumer) and bumps `g_spWrites`. `P5cStale` at the final retake then refuses the answer (`auStaleAfter` counts these). A nested plan that records no write leaves the answer valid, as today. Residual risk = phase 1's Q1 class (an unrecorded writer), now also during the ask's queue time.

### 13.2 What was built

- **`[RouteSnap] AskLent`** (`auMode` +0x4FFC, `--ab asklent`, read at each ask; needs FreeLock=1). In a P5c window, `P5bAsk` sets `g_auAsk` and posts the ask with no retake; the window and `t_p5b` stay open. `P5bAbort` clears the flag. Outside a P5c window (P5b alone) the ask retakes as before (`auSkip`). `ts3native.cpp`: the borrow rule and `RunPlan` check `g_auAsk`; `p5b.cpp`: `P5B_AU` admission; `p5c.cpp`: `auStaleAfter`.
- **`[RouteSnap] DoorMemoX`** (`dmxMode` +0x5014, `--ab doormemox`; ini `-1` not installed, `0` installed and off, `1` memo, `2` shadow; `DoorMemoXMs` 250). It lives in `HcDoorEnter` (same stub, same pass-through rules and key as DoorMemo): a 256-row ring of other jobs' answers. A row is valid only while the **door generation** and the event 59 delegate slot are unchanged and the row is younger than `DoorMemoXMs`. The generation is the sum of routesnap IC slots 4-14 (portal ICs, incl. `SetPortalLockStatus 0x74B490`) and world events 5, 7, 9, 0x0A, 0x0D-0x10. Only answer 2 ("unlocked") is kept: a "locked" answer adds the door to the plan's `LockedDoorsDuringPlan` (docs/159 §4.1), a managed side effect a new plan would lose. Managed inputs (time, relationships, running interactions) are not tracked; shadow mode (`dmxMismatch` must stay 0) and the age cap cover them [inferred]. A flip clears the table on the worker's next ask.
- **Counters v126** (0x4FFC-0x503B): `auMode`, `auAsks`, `auAcq`, `auPlanHere`, `auStaleAfter`, `auSkip`, `dmxMode`, `dmxHit`, `dmxMiss`, `dmxInval`, `dmxShadow`, `dmxMismatch`, `dmxClear`, 3 spare.
- **Tests.** Selftest `TestAskLent` (`asklent-test.h`): a worker thread posts a lent ask and the test thread serves it; the callback gets the real lock (no borrow) and its recorded write makes `P5cStale` refuse. Also covered: the admission rules, the retake with no P5c window and with the switch off, a tripwire mid-ask, and DoorMemoX hit / miss / "locked" not kept / invalidation by portal IC, world event, delegate and age / shadow mismatch / flips. Lock model as in §8.1. Fault injection was not extended: the P5 fault table has no ask-time fault point, and the tripwire-mid-ask case covers the abort path.
- **Verify (in the integration agent's combined queue).** Correctness BP + IP with P6 + EarlyCopy + HoldKeyed + `AskLent=1` + `DoorMemoX=2` (shadow), VerifyEvery 2: no crash, `m2VerBad` 0, `p6BorrowVerBad` 0, no P4 FAULT, `dmxMismatch` 0; read `auAcq` / `auPlanHere`. Then IP in-load ABBA `--ab asklent` + `--ab doormemox` (DoorMemoX=1), HoldKeyed on in both arms, VerifyEvery 0, `[MovedWaits]`, phase-0 counters. Go if held ms per taken job < 1 and `dmxMismatch` stays 0.

## 14. Phase 2b as built: footprint and polygon-cache writes judged by lot (`[RouteSnap] StaleLotFp`, 0.36.62, counters v129, branch `p6/fplots`, not run)

Built 2026-10-25 on `p6/phase2` (0.36.59), worktree `.claude/worktrees/p6fl`. Ledger §348 (I-189-8..13). §8.4 expected that W6 (~430-570/s) and the footprint-object leaves (~240-320/s FPCORE plus PRIVATE / FPREMOVE) keep the per-lot verdict world-level in most windows. A third world-level writer rides on every footprint update: the grid remove `0x62D9F0` that `LotRouting_RemoveObjectFootprints 0x635700` calls on every remove-all (P1: 337 BP / 407 IP per s, "also when the object had nothing in the grid", docs/94 §11.1), while grid adds run at only 5.8 / 13 per s.

### 14. Mapping rule per writer and proof level

The rule is not "footprint bounds to lots". A footprint object carries no lot field (its `+0x14/+0x18` is the owner object id, docs/98 §15), and a bounds-to-lot-table map would add a lookup per write. Instead the outer writer's ring entry is **covered**: it changes no routing input of its own, and every input it does change is written by a nested leaf that already has its own ring entry with the exact lot. That is the lot of the Q the footprint is inserted into or removed from (W1 / W2, `Q+0x18/+0x1C`), the remove-all's lot argument (LOTREMOVEFP), or a world-level kind (grid add, high-cost marks, lot 0). Per writer:

| Writer (ring kind) | What it writes itself | Where the routing effect is recorded | Proof |
|---|---|---|---|
| W6 polygon caches, 8 sites (`SLW_W6`) | fills of invalid cache levels, derived from the footprint's own data (lazy, by readers); W6d's in-place transform only inside W1/W2 first insert / last remove or grid add; W6f only on new footprints before their first insert; W6e has no caller | the enclosing W1 / W2 / grid add | [verified source] docs/156 row 10 and §1 ("no game writer refreshes an inserted footprint's geometry in place"); the replay recorder records no W6 entry and its audit holds the clone equal to live (docs/94 §21-22) |
| `UpdateObjectFootprintCore 0x8321F0` (`SLW_FP`) | new footprint objects (unreachable until inserted), the private polygon `C+0x38` (not read by the route worker), lock-key map parking, world `+0x994` (not read by the job, I-89-1) | remove-all `0x635700` (LOTREMOVEFP: lot, or lot 0 = world) -> W2 per Q + grid remove; insert `0x6376D0` -> W1 per Q (or the parked list, later `LockedLotUpdate` -> `0x6376D0`) and, for footprints that reach outside their lot or have none, grid add | [verified source] docs/84 §2, docs/89 §15.2-15.3, docs/107 §10.2; replay records none of these leaves (`replay.cpp` default branch) |
| `AddPrivateFootprint 0x831BA0` (`SLW_FP`) | same as above (turn-helper footprint) | same | [verified source] docs/89 §15.2, I-89-15-2 |
| `RemoveFootprintLocked 0x82A0C0` (`SLW_FP`) | `C+0x38` via `0x947880` | `0x635700` | [verified source] docs/89 §15.3, docs/107 §10.2 |
| grid remove `0x62D9F0` (`SLW_GRID`) | erases the records with key `key` or `~key` (and lot id `lotId`) in every cell listed for it; per found record `0x670510` (high-cost clear) and, for plain world records, `0x66F6D0` | **covered only when no live grid record has the key** (a no-op). A real remove stays world-level (the grid is read by world cost modes 1-4 for any lot, docs/107 §3.1) | grid layout and erase rule [verified, docs/107 §10]; the no-op test reads the live grid itself (below); "`0x670510` once per found record" [inferred from rates: 9.8 / 14 per s vs 337 / 407 removes] backs the tripwire only |
| W1 / W2, W3, W4, LOTREMOVEFP | unchanged from §8.1 (lot-keyed) | | §8.1, I-189-6 |
| grid add, high-cost marks, R, G, epoch / lot events, lot table, lot 0 | unchanged: world-level | | §8.1 |

**Grid no-op test.** A sorted array of the canonical keys `min(k, ~k)` of every record in the live grid `P+0xDC` (32-byte cells, buckets `+4`, count `+8`, chain `+0x48`, key `+0/+4`; the walk is P3's `SpGridLive`, which live runs already use). It is re-read at a grid remove's entry when it is old: after a grid add, after a real remove, after epoch / lot events, or for another planner. That is at most the add rate plus the real-remove rate (~10-25/s), not 400/s. Every grid insert is `0x636BF0` (docs/156 row 5), a hooked leaf, and every walk runs at a remove's entry under the remover's `+0x918`, so no grid writer runs during it. Lot ids are ignored (a superset). An unhooked `PathPlanner_Reset` only leaves extra keys, which errs on the stale side. Fallback to world-level: set over 8,192 records (`fplFb_0`), a faulting walk or no planner (`fplFb_1`), the tripwire (`fplFb_2`).

**Cross-lot reads (task question).** With covered entries, which lots a footprint change stales follows from where the game actually put it, not from its bounds. A footprint inserted into lot A's and lot B's Qs fires W1 on both, and both lots go stale. The parts that reach outside the owner's lot live in the world-level grid (grid add, world). The 2 m lot skirts and world obstacles are in R / G / the grid (world-level) [inferred]. A request on lot B reads lot A's footprints only through A's Q (A in its read set when looked up by key, which is P2's open completeness question, I-189-5) or through the grid (world). So the write side is per Q and exact. The read side is §8.2's set, unchanged.

### 14. What stays global

Grid adds (~6-13/s), real grid removes (about the same), high-cost marks (~10-14/s), remove-alls and Q changes on lot 0 (open ground: the "all lots" branch of `0x635700`, design D's standing footprints of Sims in the street), R / G / epoch / lot events, the lot table, every increment outside `SpNote`.

### 14. Switch, per job

The ini `[RouteSnap] StaleLotFp=1` (with `StaleLot` 1 or 2) turns the grid check on and sets `fplMode` 1. The covered flag (`SLW_COV`, bit 8 of the ring kind) is stored whenever the ring is on. Runtime `fplMode` (`+0x50FC`, `--ab routesnapstalelotfp`) is read once per plan with the StaleLot arm (`StlModeNow` -> `t_fpl`). At 1, the per-lot verdict skips covered entries (both StaleLot arms: shadow counts it, acting acts on it). At 0 it is the phase-2 verdict. Both per-lot verdicts are always counted (`fplMat`). Tripwires: an fp-only verify job (phase-2 stale, fp clean, clean run) whose answer differs sets `fplOff` (fp keys off for the session, log line); a grid remove judged a no-op that reaches `0x670510` sets the grid check off (`fplGridTrip`).

### 14. Counters v129 (`+0x50FC`, 32 int32)

`fplMode`, `fplReady`, `fplCov[4]` (W6, FPCORE, PRIVATE, FPREMOVE), `fplGrid[3]` (no-op, real, not judged), `fplMat[4]` (both clean, fp-only, both stale, phase-2 clean + fp stale = must be 0), `fplVer[3]`, `fplFb[3]`, `fplGridReads`, `fplGridRecs`, `fplGridTrip`, `fplOff`, spare 9. v126-v128 are zero placeholders. agent.js `>= 129`. Repro `route-queuefull-stalelotfp-loop`.

### 14. Expected share and evidence plan

Of the ~760/s that §8.4 named (W6 + footprint objects), all become covered. Of the grid removes (337 / 407 per s), about 96-98% find nothing and become covered. So roughly 1,000-1,300 of the 1,600-1,900 writer rows per s leave the world-level class [inferred from the P1 rates; the lot-0 share of remove-alls is not measured, and `stlWorld_10` will show it]. A window then goes world-stale only on grid adds / real removes / high-cost marks (~25-40/s together, i.e. one per 25-40 ms) or on lot-0 footprint churn. Lot-0 churn is the likely next lever. Plan: shadow (`StaleLot=2`, `StaleLotFp=1`, `VerifyEvery=1 VerifyLend=1`), pass = no crash, `fplMat_3` = 0, `fplVer_1` = 0, `fplGridTrip` = 0; read `fplMat_1 / (fplMat_1 + fplMat_2)` and `stlWorld`. Only then acting. Risks: a covered writer that does change an input outside its nested leaves (verify / tripwire); the grid walk on the sim at ~10-25/s, a few hundred records each [inferred, `fplGridRecs` measures it]; merge conflicts on the counter struct, the size asserts and the version asserts with phases 0, 1, 3 and the v126-v128 branches.

## 15. Integration: one build of every phase (2026-10-25, ts3native 0.36.63, counters v129, branch `p6/integration`, NOT run)

Merged in this order, each design reviewed against §3-§4 and its own section before the merge (diffs read by a reviewer, interplay by the integrator): `p6/phase0` (§8) -> `p6/phase1` (§9) -> `p6/roomrec` (§10, HoldKeyed=2 built here) -> `p6/phase2` (§11) -> `p6/allkinds` (§12) -> `p6/phase3` (§13) -> `p6/phase4` (§4.1) -> `p6/fplots` (§14). Build `out-p6int` = `game-copy/ts3native.asi.p6int`; `build.cmd` selftest, `tools/selftest-all.ts` (107 PASS), `tools/model/p5-lock-model.ts` (all configs clean, mutations caught) and `tools/hook-sites.ts --check` pass. Nothing ran in the game.

### 15.1 Review findings and what changed

| phase | blocking | fixed in the integration | notes (open, watched by the queue) |
|---|---|---|---|
| 0 `[P6Waits]` | none | `Enabled` defaults 0 (every new switch off); the serve timer pair runs only when it is on [verified source] | counters only; site 4's W4 coverage rests on `0x63D7B0 -> 0x63CB90` (routesnap.cpp:532-533 [verified disassembly there]) |
| 1 `HoldKeyed` | none | TOCTOU: `P5bLock` re-reads `g_hgJob` after the acquire (a job end + a P5b-only window between `P5bAllowed` and the acquire would admit a gate into a window `P5cStale` does not cover) [verified source] | `hgStaleAfter` counts any recorded write in the window, not only an admitted gate's; `hgTrip` sees bookkeeping leaks, not data races (the retake already blocked on the lock) |
| roomrec `HoldKeyed=2` | none | built: `kHgSites2` adds sites 1, 2, 10, 11, 15; latch `g_hgJob` 1 / 2 per job; retake that waits `RetakeEscalateMs` on an admitted gate frame (`g_hgOpen`) turns HoldKeyed off (`hgLongHold`) and keeps lending (the frame waits for no job: nested gates pass on `rgOwnerTid`, a job wait hands over); any other holder escalates as before; `TestP5cIn` phase 3 alternates 1 / 2, site 10 admitted with 2 only [verified: selftest] | §10 open point 2: the lot-index read is under the game's own `WM+0x228` lock on both sides (docs/94 N2) and the nr family refuses the take after a rebuild [verified source]; gate 10 under the lent lock = the C_PLAN class FreeLock already admits |
| 2 `StaleLot` | none | agent.js v42 names `slReady` / `slMode` restored (phase 2 had renamed them onto v125's) [verified] | a run with no recorded find is a legitimate empty read set, not a detour miss: the pointer paths past `LotTable_Find` are the same gap for every n; acting only after shadow shows `stlVer_1` 0, `stlMat_3` / `stlMat_7` 0, `stlFaults` 0, `stlReadHist_0` small; kinds 2 / 3 reach the start lot through the point map, so gather shadow evidence with TakeKindsAll on |
| 2c `TakeKindsAll` | none | none | verify stream is one cadence over all kinds (no per-kind M2 / M3 counter: read the §13 record's kind); needs `[RouteAsync] Kinds` bits 1 / 2 and ini `TakeKindsAll=1` at install |
| 3 `AskLent` / `DoorMemoX` | none | DoorMemoX installs only with routesnap's `World_FireEvent` hook and IC slots (`snReady & 6`: its generation never moved without them); mode 1 asks every 8th valid row anyway and a mismatch turns the memo off for the session (`dmxMode` 0) [verified source] | Q2 holds (`P4RunClone` -> `P5cTake` -> `P5cStale` order); `P5bAllowed` keeps the callback check (`g_inInvoke`) before `HgKeyed`, so HoldKeyed admits nothing inside a served ask; the memo generation covers native portal state, not managed answer inputs (GoHere filters): shadow needs mod worlds too |
| 4 `TakeForeign` | none | a foreign START verifies on the own cadence when `FbJobCapMs=0` (no deadline: answers comparable); `TestP6Foreign` covers it [verified: selftest] | class-c foreign plans (docs/141) never get an M2 / M3 record otherwise; clone + fallback game plan share one deadline (`fbjcCapped` may rise on the on arm) |
| 2b `StaleLotFp` | **W6g** `0x80CB40` / `0x806770` (`DoorRouting_AddDoorPortals`): creates door portal objects lazily, a routing input with no proven nested lot-keyed leaf -> kept world-level (`SLW_W6`, not covered) [inferred from docs/105; closure not read] | grid no-op flag cleared by the next grid add only (a nested leaf inside `0x62D9F0` before its `0x670510` could blind the tripwire); an unreadable grid cell gives no set (world-level) instead of a subset [verified source] | acting after `fplVer_1` 0 over >= 1,000 fp-only jobs, `fplGridTrip` 0, `fplOff` 0; a deferred quadtree insert (`0x666C50` queued at `Q+0x4C`) is the one covered outer write whose nested W1 may land later [inferred, practically unreachable] |

Nothing had to be disabled: every switch ships default off, is runtime-flippable, and its acting mode is gated by the shadow rule above.

### 15.2 Final switch list (all default off)

| ini | runtime (`--ab` / `native_set`) | values | counters |
|---|---|---|---|
| `[P6Waits] Enabled` | none (ini only) | 0 / 1 | v123 `p6w*` +0x4BBC |
| `[RouteGate] HoldKeyed` | `holdkeyed` +0x4E7C | 0 / 1 / 2 | v124 `hg*`, `hgLongHold` +0x4ED0 |
| `[RouteSnap] StaleLot` | `routesnapstalelot` +0x4F3C | 0 off, 2 shadow, 1 acting (ini != 0 installs) | v125 `stl*` +0x4F3C |
| `[RouteSnap] AskLent` | `asklent` +0x4FFC | 0 / 1 | v126 `au*` |
| `[RouteSnap] DoorMemoX` | `doormemox` +0x5014 | -1 not installed, 0, 1 acting (self-check 1 in 8), 2 shadow | v126 `dmx*` |
| `[RouteSnap] TakeKindsAll` | `routesnapkinds` +0x503C | 0 / 1 (ini 1 installs the A10 collect guards) | v127 `pk*` +0x503C |
| `[RouteSnap] TakeForeign` | `routesnapforeign` +0x50DC | 0 / 1 | v128 `p6fb*` +0x50DC |
| `[RouteSnap] StaleLotFp` | `routesnapstalelotfp` +0x50FC | 0 / 1 (with StaleLot 1 or 2) | v129 `fpl*` +0x50FC |

Counter layout: v122 ends at +0x4BBC; v123 (176) -> v124 (48) -> v125 (48) -> v126 (16) -> v127 (40) -> v128 (8) -> v129 (32) = 5215 int32, version field 129; every test header asserts `5215 * 4` and `version == 129`; `tools/probe/agent.js` reads v123-v129 by name; `docs/registry.md` rows v123-v129, reservations v123-v128 + ledger §341-§348 committed.

### 15.3 Queue `tools/harness/queues/p6-integration.json` (dry-run clean, not started)

(a) `bp-p6int-verify`, `ip-p6int-verify`: everything on (HoldKeyed=2, StaleLot=2 shadow, StaleLotFp=1, AskLent=1, DoorMemoX=2 shadow, TakeKindsAll=1, TakeForeign=1, P6EarlyCopy=1), VerifyEvery=1, VerifyLend=1, 12 min on `route-queuefull-replan-loop`; pass = no crash, no P4 FAULT, no escalation, `m2VerBad` 0, `p6BorrowVerBad` 0, `stlVer_1` 0, `stlMat_3` / `stlMat_7` 0, `fplVer_1` 0, `dmxMismatch` 0, `hgTrip` 0, `hgLongHold` 0 (`nativeRequire` + `logForbid`). (b) G1 v2: `shadow-compare-report.ts <bp shadow.jsonl> --g1`, tier 3 = 0. (c) IP timing ladder, 7 loads, each an in-load ABBA (off, on, on, off, 16 firings per arm) on a generated repro `route-queuefull-p6int-<rung>-loop` whose arm step writes every P6 switch: off = the rungs below, on = + this rung: P6 -> HoldKeyed=1 -> HoldKeyed=2 (`"$on:2:1"`, repro-loop.ts) -> StaleLot acting (gated on (a)) -> StaleLotFp (gated) -> AskLent + DoorMemoX (DoorMemoX acting gated) -> TakeKindsAll + TakeForeign. VerifyEvery=0, `[MovedWaits]` + `[P6Waits]` + `[HitchAttr] Holders`; read `hitch-ab.ts` and `wait-ab.ts` per arm. ~3.8 h of game time. The harness loop knows two arms per load (`repro-loop.ts`), so the ladder is cumulative across loads; a one-load N-arm ladder needs loop + `hitch-ab` arm support (follow-up).
