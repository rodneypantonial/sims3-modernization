# 164. Sync plan phases: where a multi-second sim-thread plan spends its time, and which cap would cut it

Written 2026-10-22. **Offline build, NOT run in game.** Builds on docs/150 §7 (`[RouteJobPhases]`, worker jobs only), docs/134 (sync fallback plans, `SyncCapMs`, `LogFallbackStack`), docs/142 (failing plans, planner loops), docs/163 (SV big crowd 7,361 / 7,350 ms failed `Route_Plan`s in ThreadedObjects, caller unknown). Ledger rows: docs/51 §256. Tags: **[verified]** = code, bytes or selftest; **[inferred]** = derived, method named; **[measured]** = runs.

## Summary

- **Target:** player-visible freezes. A sync plan of N ms on the sim thread is a frame of at least N ms. docs/150 measured the worker's jobs by phase; the long sim-thread plans had only 57 sampler hits (cell A\* 56%, water 19%, world A\* 14%, docs/150 summary) and docs/134 §11.2 showed that `SyncCapMs` (a world A\* node clock) cannot see ~1.6 s of a 1.9 s failed plan. The multi-second SV crowd plans have no split at all.
- **Built (off, observe only): `[RouteJobPhases] SyncPlans=1`** (`native/ts3native/syncphases.cpp`, counters v97 `syp*` at `+0x4120`, `game-copy/ts3native.asi.0328`): docs/150's phase timers on every sim-thread sync plan, per plan: total, Route_PlanLocked calls and result (succeeded / failed), self time per phase (plan own code, cell A\*, post pass, PathNodes_Insert, world A\*, lot legs, portal A\*, candidate loops, LotGraph_Build, pending applies, water, allocator, lock wait, other), and world A\* calls split by caller: **goal-lot entry loop** (`0x65F809`), **open-ground goals** (`0x65FB99` / `0x65FDA5`), circle pre-pass (`0x6762B4`). One log line per plan >= `[RouteAsync] LogFallbackMs` right after the existing `route async: sync plan N ms` line (and its script stack line), plus aggregate counters for all plans and for the long ones.
- **Covers both sync paths:** the route IC's sync path (`SyncIc`: `[RouteAsync]` fallback / no job, the whole IC incl. goal step) and every other `Route_PlanLocked` the sim thread runs itself (Replan / PlanFromPoint with A10 off, which never reach `SyncIc`: the BP 4,443 ms failed `Route_Replan` of docs/163 §2) [verified, selftest].
- **`SyncCapMs` cuts only world A\* time, only on the IC path, and only at a 16-node clock** (§3, from the code): it would not cut a plan whose time sits in cell A\*, lot legs, portal A\*, water, post pass, allocator, lock wait or the goal step, and it never applies to Replan with A10 off. §4 designs the cap that would (a per-plan wall deadline at the planner's search entries, set at the session start) and where it hooks; not built.
- **Checks:** selftest `TestSyncPhases` (dummy Route_PlanLocked calling a dummy world A\* from two registered loop sites) + full `selftest ok`; `steerro-test` default, `STEERRO_PHASES=1` and new `STEERRO_SYNCPHASES=1` (the game's own planner, 482 sessions: entry loop 260, open ground 168, pre-pass 428 world A\* calls, **0 unclassified**, with the world A\* site timed inside K8's counter hook as it would be inside `[RouteEarlyOut]`) pass; `tools/hook-sites.ts --check` 0 unlisted; `tools/check-docs-code.ts` ok.
- **Next:** day queue `tools/harness/queues/sync-phases.json` (SV big crowd, BP route bits, 5 min each, only `SyncPlans=1` + `LogFallbackStack=16`); report `node tools/harness/route-phases-report.ts --sync <tag>-ts3native.log --min 300`. The split decides which part of §4 to build.

## 1. What a session is [verified, syncphases.cpp]

| Opened at | When | Includes | Ends |
|---|---|---|---|
| `SyncIc` (first line: `SypIcWants()` -> `SypIcRun`) | the route IC plans on the sim thread (`IcCollect` "nojob", "mismatch", "pending", "unstarted"; A10 kinds) | the whole IC: start points, `Route_PlanCommon` goal step, every nested `Route_PlanLocked`, result mapping | when `SyncIc` returns |
| the `Route_PlanLocked 0x82C6E0` stub | a sim-thread (`RgIsSim`) entry with no session open | that call (its lock wait included when taken inside) | when that frame leaves (result = its AL) |

- While a session is open, `g_rjpSyncTid` = the sim's id and every `[RouteJobPhases]` stub times that thread as it times the worker inside a job; frames go to per-plan sums (`SypFrame`), never to the worker's v87/v92 rows or the cell memo probe.
- The `Route_PlanLocked` stub admits every thread to its C entry while `SyncPlans` is on (`g_rjpAny`): one `pushad` + call per plan on other threads; every other stub stays a compare and a jump for threads outside a job or session.
- **Lock wait:** on the sim thread `LockAcquireHook` pumps (`LockAcquireAsync`: try-lock, run the worker's managed calls, retry), so the stub on `g_lockOrig` sees only the tries. With a session open, the hook pushes a manual lock frame around the whole wait: lock = wait + pump.
- One session at a time (`sypBusy` counts another thread's plan); a session whose opening frame was unwound is closed by the next entry at or above its slot (`sypStale`).
- `SyncPlans=1` works without `Enabled=1`: the sites are installed, `rjpReady` stays 0 (worker jobs untimed, cell memo and HostSplit off). With both on, both are timed.
- Not covered: plans of the `[RouteWorker]` Mode 1 offload (off in the release ini) run on the worker outside a job and are not timed; a plan whose `Route_PlanCommon` goal step runs before `Route_PlanLocked` on the non-IC path has that goal step outside its session (it is inside on the IC path).

**World A\* by caller** [verified, E8 scan of the Steam exe: the only four direct callers of `0x676260`]: `0x65F809` (goal-lot entry loop, return `0x65F80E`), `0x65FB99` and `0x65FDA5` (open-ground goals, `0x65FB9E` / `0x65FDAA`), `0x6762B4` (the A\*'s own circle pre-pass, `0x6762B9`). The stub's saved return address is compared first; when an owner hook calls the A\* from C (`[RouteEarlyOut]`'s `EoAsHook`, steerro's K8 counter) the game's return address sits in that hook's frame, so up to 256 stack words above the slot are scanned (shortcut: a stale copy of one of the four values in such a frame would misfile one call; K8: 0 misfiled). Loop time = inclusive time of those world A\* calls (allocator and pre-pass inside); the per-candidate colour check and other loop-body work stay in "candidate loops" (`PathPlanner_PlanHierarchical` own code).

## 2. Output

Log line (one per plan >= `LogFallbackMs`, default 200; 20 per minute, `sypLinesDropped` beyond):

```
route phases: sync plan 7361 ms failed, entry IC Plan/NoStart/nojob, route 257CB664, planlocked 3; loops entry=5/640 ground=0/0 prepass=5/20 othercaller=0/0; ms plan=40 cell=4100 post=100 insert=20 world=700 lotleg=300 portal=12 cand=50 lotgraph=0 pending=0 water=0 alloc=900 lock=0 other=1139
```

(illustrative numbers, the report self-check's input). `entry` = `IC <kind>/<reason>/<collect path>` or `Route_PlanLocked (no IC)`; `loops x=n/ms`; phase values are self ms that sum to the plan. Result `no-plan` = no `Route_PlanLocked` returned inside the session (the IC failed before planning).

Counters v97 (docs/registry.md): `sypPlans`/`sypUs` by result, `sypEntry` by entry, `sypPhaseUs` and `sypLoopN`/`sypLoopUs` for all plans, `sypLong*` for plans >= `LogFallbackMs`, `sypBusy`, `sypStale`, `sypLines`.

Report: `node tools/harness/route-phases-report.ts --sync <ts3native.log> [...] [--min 300]`: per plan >= min the top 4 phases with shares, entry-loop and open-ground counts and ms, and "cap: yes" when world A\* self time is at least half the plan (the only phase `SyncCapMs` can cut); a header line with the phase shares over all those plans. Exit 1 when no split line is found (queue pass rule).

## 3. Would `SyncCapMs` cut it? By phase [verified from the code unless marked]

How the cap works (`ts3native.cpp` `SyncIc`, `CallPlanBudget`, `BudgetStep`, `CostGate`): `SyncIc` sets `t_fbCapUs` only when `asyncMode == 1`; `CallPlanBudget` (the `Route_PlanLocked` detour) starts a fresh `Budget` per `Route_PlanLocked` call with `maxUs` = the cap; the clock starts at the first **generated** world A\* node (heuristic call, cost arg 5 = 0) and is read every 16th generated node; once over, every world A\* **edge** cost returns the wall `1e9` for the rest of that `Route_PlanLocked`, so the open list drains through the A\*'s own "open list empty" exit (an ordinary no-path). Only the five cost-function sites inside `0x676260` are stubbed.

| Phase where the plan's time sits | Cut by `SyncCapMs`? | Why |
|---|---|---|
| world A\* (one long search, many nodes) | **yes**, within ~16 nodes of the cap | the node clock |
| world A\* (few nodes, slow expansions: BP 32 nodes in 1,942 ms, docs/134 §11.2) | **no / late** | the clock is read only at nodes 16, 32, ...; edge-heavy expansions between reads run on |
| goal-lot entry loop / open-ground goals (K candidates, one world A\* each) | **partly**: after the first cap every later candidate's A\* drains after its first pop (`b.capped` stays set for the whole `Route_PlanLocked`) | the colour check, the lot leg in the loop body (docs/142 §2.2, inferred) and anything else per candidate still run |
| circle pre-pass | yes (its nodes count) | same A\* |
| cell A\* `0x61E500` (in-room quadtree) | **no** | own search, no stubbed cost pointer; docs/150: 56% of the long sim plans' samples |
| lot legs `0x6410B0` / `LotPlan_Inner`, portal A\* `0x63E340` | **no** | own searches |
| water `0x671930` | **no** | own search (docs/150: 19% of the long plans) |
| post pass `0x64E380`, `PathNodes_Insert`, allocator | **no** | not searches; mostly on the success path |
| `LotGraph_Build`, pending applies | **no** | builds |
| lock wait (sim waits for the worker's job, pump) | **no** | before the first node; the clock starts after the lock |
| goal step `Route_PlanCommon`, IC own code ("other") | **no** | outside `Route_PlanLocked` |
| PathPlan_WithRetries rerun, RoutingJob relaxed replan (same `Route_PlanLocked`) | world A\* part yes (capped stays set), the rest no | |
| any phase of a **Replan / PlanFromPoint with A10 off** or another non-IC sim plan | **no** | `t_fbCapUs` is set only in `SyncIc` (docs/134 §11.1) |
| any phase when `asyncMode != 1` | **no** | `SyncIc` sets cap 0 |

So `SyncCapMs` bounds a failing plan only when its time is many-node world A\* on the IC path. The two measured hints say that is not where the multi-second plans sit (docs/134 §11.2: >= 1.6 s of 1.9 s outside node generation; docs/150: long sim plans mostly cell A\* and water) [inferred until `sync-phases.json` runs].

## 4. The cap and memo that would cut each phase (design only, not built)

Goal: the same answer kinds the game already produces (a failed plan is an ordinary `FailedBecauseNoPathToDestination`), bounded wall time per sync plan, nothing changed for plans under the bound. All parts read one per-session deadline set by `SypBegin` (or an equivalent entry for the non-IC path) from a new `[RouteAsync] SyncDeadlineMs`.

| # | Cuts | Mechanism | Hook | Open checks before building |
|---|---|---|---|---|
| D1 | world A\* incl. few-node slow searches, both paths | set `t_fbDeadline` (the docs/134 §11.3 `FbJobCapMs` absolute QPC deadline, read every 4th cost call of either kind) for the session instead of only `t_fbCapUs`; `CallPlanBudget` already passes it into `Budget.dl` | session start; for the non-IC path the deadline must be set **before** `CallPlanBudget` reads it: `Route_PlanCommon 0x7606E0` entry (G10-C's site, routegate.cpp) or the A10 collect hooks for Replan / PlanFromPoint, since the `Route_PlanLocked` stub runs after the owner's hook | none new (FbJobCapMs path is built and selftested) |
| D2 | entry loop / open-ground loop per-candidate work after the deadline | at the loop's world A\* call sites `0x65F809`, `0x65FB99`, `0x65FDA5`: past the deadline, skip the call and return "no path" as the drained A\* would (result 0, `*best` = start, closed bits as the early-out acting mode writes them, docs/142 §9) | E8 retarget of the three call sites (same pattern as TailUnlock's call-site retargets) | reuse docs/142 §9's verified "failed search leaves" list (`EoActBest`); loop exit taken when every candidate fails |
| D3 | cell A\* `0x61E500` | past the deadline, the entry returns its no-path result without searching; inside one long search, a check per pop | entry: the `[RouteJobPhases]` cell stub site (stub already there); per pop: the open-list pop call inside `0x61E500` | the function's no-path contract (return value, out list empty, pool state) from docs/98 (in-room A\* spec) and a decompile check; whether callers retry a failed leg (lot leg loop) |
| D4 | lot legs `0x6410B0`, portal A\* `0x63E340`, water `0x671930` | same as D3 at their entries | the existing stub sites | each one's failure contract; the water path's frozen-pond rules (docs/100) |
| D5 | the same failure repeated (SV 7.36 + 7.35 s ran 0.3 s apart, docs/163) | failure memo keyed on (route handle or agent, start cell, goal set, route flags, world version from `[RouteSnap]`'s change counters) within 500 ms: return the recorded failure without planning | the IC sync path (`SyncIc`) and `Route_PlanLocked` for the non-IC path; `[RouteAsync] ReplanDedupe` (docs/150 §13) is this for Replan / ReplanFromPosition; ts3fixes `failrepeat` (docs/163 §4) is the managed one for a Sim's own Route.Plan | whether the SV pair is the same request (the split lines' route handle and the script stack decide) |
| D6 | lock wait inside a sync plan | none in the plan: the wait is the worker's job (docs/134 §11.1); fix = the job caps (`FbJobCapMs`) and job shrinking (docs/150, docs/159) | - | - |

Order: D1 is a small change on a built path (~30-60 min + selftest); D2/D3 follow from the split (build the one whose phase holds the multi-second time; ~2-4 h each incl. contract read and selftest on a dummy, then one verify run); D5 only when a split line pair shows the same route repeated. Every part changes answers (a findable path over the deadline becomes "no path", like `SyncCapMs`): A/B with the docs/134 stranded-Sim checks before any default.

## 5. Tests [verified]

- `selftest.exe` `TestSyncPhases` (`syncphases-test.h`): layout v97; non-sim thread passes untimed; a sim-thread dummy `Route_PlanLocked` (real prologue bytes) that calls a dummy world A\* from two registered loop sites: one session, failed, entry loop 1 / open ground 1, phases sum to the plan, one log line, worker rows untouched; the IC path through `SyncIc` with a dummy IC (session around the nested plan, result succeeded); nested plans keep the IC session open; the lock frame; another thread's session (busy); unwound opener (stale); a worker job on the same thread still goes to the job rows; the line format. Full `selftest ok`.
- `steerro-test.exe` (maps the Steam `TS3W.exe`, runs the game's planner): default ok; `STEERRO_PHASES=1` ok (480 jobs); `STEERRO_SYNCPHASES=1` ok: 482 sessions, world A\* calls 260 entry loop + 168 open ground + 428 pre-pass (each outer call has one pre-pass), 0 other caller, phases 172.9 of 173.7 ms (rounding), 0 frames dropped.
- `node tools/harness/route-phases-report.ts --self-check` ok (parser and per-plan rows).
- `tools/hook-sites.ts --check`: 0 addresses outside docs/68 (row added: no new site, four compared return addresses). `tools/check-docs-code.ts` ok.

## 6. Build and queue

- `game-copy/ts3native.asi.0328` (main checkout), sha256 `4F534E3784D41A5FAA80430DB9CD39E88EB812D8C366CCF50CB83565F2215900`, 708,608 bytes: master `73707448` (asi.0327 content: host-call split + DoorMemo, counters v96) + this change; combined build; version field 97; `TS3NATIVE_VERSION` 0.36.23.
- Queue `tools/harness/queues/sync-phases.json` (not run): `sv-syncphases-big-crowd`, `bp-syncphases-route-bits`, 5 min each, release ini + `[RouteJobPhases] SyncPlans=1` + `[RouteAsync] LogFallbackStack=16`. Day OK (observe only, split read per plan). Pass: no crash, game alive, `OK   route phases SyncPlans` in the log, at least one split line (report exit 0).
- Effort so far: ~3 h (build, tests, doc). Queue ~25 min real, 2 game runs.

## 7. Addendum 2026-10-22: D1-D5 built (`[RouteAsync] PlanDeadlineMs`, `PlanDeadlineOwnMs`, `FailMemo`; ts3native 0.36.27, counters v99, `asi.0332`, NOT run)

Built before `sync-phases.json` ran (Rodney 2026-10-22: target the player-visible multi-second freezes now). All off by default; `native/ts3native/plandeadline.cpp`, ledger §270.

### 7.1 What is cut, and where [verified, code + disassembly]

One wall deadline per sim-thread sync plan (a session), opened at the outermost of `SyncIc` (the route IC's sync path: goal step, nested plans, result mapping) and `CallPlanBudget` on the sim thread with no session open (Replan / PlanFromPoint / ReplanFromPosition with A10 off, any other non-IC sim plan; its goal step runs before `Route_PlanLocked`, outside). Past the deadline:

| # | Phase | How | Same answer as |
|---|---|---|---|
| D1 | world A\* `0x676260`, any caller | the session sets `t_fbDeadline`; `CallPlanBudget` gives every `Route_PlanLocked` a `Budget` with it (the FbJobCapMs path, docs/134 §11): the clock is read on every 4th cost call (the first included), then every edge is the wall | the A\*'s own "open list empty" exit `0x676BF7` |
| D2 | goal-lot entry loop `0x65F809`, open-ground loop `0x65FB99` / `0x65FDA5` | every later candidate's world A\* drains after its first pop (`b.capped` stays set for that `Route_PlanLocked`; a new one is capped at its first cost call). No call-site retarget needed | same |
| D3 | cell A\* `0x61E500` | entry stub: the A\*'s empty-start-set exit (`0x61E53C` -> `0x61E62F`): `[this+0x24]` = 0, AL 0, `ret 0x18`, nothing else written | the game's own exit |
| D4 | lot legs `0x6410B0` / `LotPlan_Inner` | through D3: `LotPlan_Inner` takes its own code-5 path, `LotPlan` its own per-room retry (each retry cut again) | the game's code 5 |
| D4 | water `0x671930` | entry stub: its "not the same water body" exit (`0x671948` -> `0x671B98`): arg 9 out vector end = begin, AL 0, `ret 0x24` | the game's own exit |
| D5 | the same failure repeated | `FailMemo`: ReplanDedupe's `RdRun` (docs/150 §13) on `SyncIc`'s Plan call: a repeat of a FAILED plan within 500 ms (same follower, goal, start within 10 cm, route state as that plan left it, no routing write since, not result 18, world quiet) gets that result without planning | the previous plan's result bytes |

Not cut (no exact no-write exit, or not a search): portal A\* `0x63E340` (writes its context fields first), `LotPlan` itself (`PathPlanner_PlanHierarchical` hands its out indices to `0x650950` whatever the code, so a skip would pass unset indices), one cell A\* already running (bounded by the game's 64,000-node cap), `LotGraph_Build`, pending lot updates, post pass / `PathNodes_Insert` (success path), the IC goal step and script callbacks. Plans that end before their deadline are unchanged (stubs: one thread compare; cost stubs: a clock read every 4th call). With a deadline, `SyncCapMs`'s node clock is not used inside that plan (`CallPlanBudget`: `dl` wins).

### 7.2 Hooks

Entry detours on `0x61E500` (6 bytes) and `0x671930` (7 bytes), installed after `[RouteAsync] ReplanDedupe` and before `[RouteJobPhases]`, which then times both inside our trampoline pointers (`RjpOwner`); docs/68 row added, `hook-sites.ts --check` 0 unlisted. World A\* uses the `[RouteBudget]` cost stubs (patched for this even with `[RouteBudget]` off). FailMemo has no site (`CallIc` with `t_fmSync`, set only by `SyncIc`: a worker job's Plan never goes through it). `[RouteSnap]` change counters turn on with `FailMemo` (its world version).

### 7.3 Caller classes (docs/141 §3) and what changes for them

| Class (docs/141) | Path | Deadline | What a cut means for the caller |
|---|---|---|---|
| a: the routed Sim's own task (Go Here, interactions) | IC, reason NoYield / QueueFull / M1\* / Unstarted / Skipped / Uncollected / KindMismatch / Los | `PlanDeadlineOwnMs`, **0 = never** (default): counted in `pdOwnExempt` | none by default. With it set: the Sim's route fails (route-fail reaction, interaction retries or cancels) |
| a, when ts3fixes `asyncroute` is off | IC, reason NoStart | `PlanDeadlineMs` | own-task plans then look like NoStart and are cut [inferred; the release has `asyncroute` on] |
| b: another yielding task (alarms: `RunStuckCheckHelper` 3 AM, party / role / service placement, oneshot tasks) | IC, NoStart or Foreign (FallbackAsync=1 makes Foreign jobs async; FbJobCapMs caps those) | `PlanDeadlineMs` | the point counts as unreachable: the stuck check's 10 random points were failing (sea); placement picks another spot or gives up as on any failed plan |
| c: reset -> safe location (`IsPointInLotSafelyRoutable` per cell) | IC PlanFromPoint, NoStart | `PlanDeadlineMs` | that cell counts as not routable; the search goes on to the next and ends at the game's own fallback (lot front / teleport) |
| c: route-follow replan callbacks, replan-all after build/buy or edit town | IC Replan / ReplanFromPosition with A10 on (NoStart); `Route_PlanLocked` session with A10 off | `PlanDeadlineMs` | the replan fails: the follower stops and the script plans again on its next action (route-fail path) |
| c: `GetFarthestLot`, career job positions (`GetRoutableIndoorPositions`), load-time replans, UI placement, dispose | IC NoStart (or non-IC) | `PlanDeadlineMs` | a lot / position whose plan was cut is skipped as unreachable; load-time replans fail and are redone on the Sim's next action |

### 7.4 Stranding

- Own-task plans are never cut unless `PlanDeadlineOwnMs` is set; a plan that ends before its deadline is untouched.
- What can change: a plan that would have **succeeded** after more than `PlanDeadlineMs` now fails. `pdHitOk` counts cut plans that still succeeded (found before the cut); every other hit is a plan the cut ended. The loop A/Bs read stranded Sims and route-fail reactions per arm (docs/134 stranded-Sim checks); any stranded Sim in an on arm = raise the deadline (or keep cuts to failing tails only) before any default (ledger I-164-8).
- FailMemo answers only a failure, only within 500 ms of the real plan, only when route state and world are unchanged (ReplanDedupe's verified rules, docs/150 §13.2); `FailMemo=2` observes first (`fmWouldSame` / `fmWouldDiff`, I-164-9).

### 7.5 Switches and counters

- Ini `[RouteAsync]`: `PlanDeadlineMs` (0 = not installed; 50-60000), `PlanDeadlineOwnMs` (0 = never cut own-task plans), `FailMemo` (0 off, 1 serve, 2 observe).
- Runtime (`native_set` in repros, `--ab` names): `plandeadline` (`pdMode`, +0x4244), `failmemo` (`fmMode`, +0x4280); values `plandeadlineus` (+0x4248), `plandeadlineownus` (+0x424C). The ini value must be > 0 for the stubs to exist.
- Counters v99 (docs/registry.md): `pdSessions[2]`, `pdHit`, `pdHitOk`, `pdHitUs`, `pdLateMaxUs`, `pdSkipCell`, `pdSkipWater`, `pdWorldCapped`, `pdOwnExempt`, `pdLines`; `fmCalls`, `fmNoKey`, `fmFails`, `fmRep`, `fmInval[4]`, `fmWould[2]`, `fmWouldSame[2]`, `fmWouldDiff[2]`, `fmServed`, `fmServedUs`. One log line per cut plan (20 per minute): `route deadline: sync plan N ms failed, M ms past its deadline, entry IC Plan/NoStart/nojob, route ...; world A* capped n, cell skipped n, water skipped n`.

### 7.6 Tests [verified]

- `selftest.exe` `TestPlanDeadline` (`plandeadline-test.h`): v99 layout; the stubs on dummy cell A\* / water functions with the game's prologue bytes: pass outside a session, before the deadline, with the switch off and on another thread; past it the game's exits (fields, AL, stack balanced); non-IC session on the sim thread only, nested once; world A\* capped through `CallPlanBudget` and the cost stub (a plan under its deadline unchanged); IC sessions by reason (NoStart / Foreign = `pdUs`, own task exempt); `SyncIc` path end to end; FailMemo observe / serve (no `+0x1F4` bump, ReplanDedupe rows untouched) / never on a non-`SyncIc` call / successes never served. Full `selftest ok`.
- `steerro-test.exe` (the game's planner on the Steam exe): default, `STEERRO_PHASES=1`, `STEERRO_SYNCPHASES=1` ok; new `STEERRO_PLANDEADLINE=1`: K8 plans by query hash in a session already past its deadline (none / all / cell only / water only), live == copy for every plan; 400 cases: all-cut 15 plans 0 found (10 world A\* capped), cell only 16 (1 found), water only 14 (6 found, 0 water legs), 188 cell and 12 water skips; 1,200 cases: 32 / 0 found, 402 cell and 34 water skips.
- `tools/hook-sites.ts --check` 0 unlisted; `tools/check-docs-code.ts` ok; `queue.ts repro-check-route.json --dry-run` ok.

### 7.7 Build and queue

- `game-copy/ts3native.asi.0332` (main checkout), sha256 `2F17629B93032D8E2FC31E0FC05E284176D6742ABBA1E7A380F7CE4DABF6CEF6`, 716,800 bytes: master `f8ef415f` (asi.0331 content, 0.36.26, v98) + this change; version field 99; `TS3NATIVE_VERSION` 0.36.27.
- Repros (`tools/harness/repros.json`): `route-sv-seacheck-freeze`, `route-nopath-crowd-loop`, `route-stuckcheck-reset-loop` flip `plandeadline` + `failmemo` with their existing arms (ABBA, `native_set`); other steps unchanged. They need asi.0332 (an older asi has no v99 fields at those offsets).
- `tools/harness/queues/repro-check-route.json`: the three jobs now run asi.0332 with `PlanDeadlineMs=1000`, `FailMemo=1`, `minCounters` 99, and also require `OK   route deadline` / `OK   fail memo` in the log. Correctness only (5 min each); timing A/B on a clean night.
- Effort: ~3 h (build, tests, docs). Queue ~25 min real, 3 game runs.