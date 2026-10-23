# 178. Next routing targets after v0.1.5, ranked by player-visible hitch value

Written 2026-10-23. **Static + existing run files only: no build, no game run.** Inputs: `freeze-report.ts --min 300 [--repro-causes]` on the runs below, their `-hitches.jsonl` (per-iteration `rtMaxMs` / `rtMaxKey`), `-native.json` counter deltas (`rjp*` v87 phases, `pg*` v102 P-G, `fbjc*` v81/v84 foreign-job split), the ts3native logs (`route async: long job`, `route phases: long rest`, `route jobs split`), the TS3W.exe prologue bytes (Steam 1.67, `game-copy/bin`, read only) and our source (`ts3native.cpp` worker job loop and `BudgetStep`, `plandeadline.cpp`, `routephases.cpp`). Tags as in AGENTS.md: [verified] = read in a run file, our source or the exe bytes; [inferred] = reasoning, with the probe or run that settles it.

Runs used. All have `OK route worker: runtime_invoke ... -> InvokeHook` in the log (worker hook OK):

| Run | Build | Valid for timing | Use here |
|---|---|---|---|
| `rerun-0333-2026-10-23-sv-crowd-cause-0333` | asi.0333 | yes (day) | §3, §2 SV |
| `rerun-0333-2026-10-23-ip-locktaker-verify` | asi.0333 | yes (day, shadow verify, 1 firing per arm) | §2 IP loop |
| `route-goal-ab-2026-10-23-ip-goal-split-verify` | asi.0334 (0.36.30) | **INVALID** (others 15.3%); correctness and counters only | §1 |
| `route-hitch-loops-2026-10-23-bp-route-{queuefull-replan,nopath-crowd}-ab` | asi.0332 | yes | §2 BP loops |
| `natural-play-2026-10-22-ip-natural-30` | 0.36.27 | yes | §2 IP natural |
| `natural-play-2026-10-22-sv-natural-30` | 0.36.27 | **INVALID** (others 10.9%); lock waits only | §3 (same cause, bigger) |

## Summary

- **The biggest player-visible routing freeze left is a sim lock wait behind a long *foreign* route job, and FbJobCapMs does not cut it [verified].** SV crowd 0333: one job of 2,076,957 us (`foreign START, IC kind 0` = a `Route.Plan` from another Sim's task sent async by FallbackAsync, `capAt -1`, `plan 2076 ms`), and the sim waited for it: `fbjcSimWaitMaxUs` 2,069,045, hitch iteration `rtLock` 2069 ms at steering key `0x85FE10`, wall 2085 ms. SV natural-30 (noisy, but these are lock waits): 13 foreign jobs >= 300 ms up to 2,211 ms, all `capAt -1`; sim waits behind them 1/3/7/1 in the top four buckets; visible freezes >= 1000 ms 3.7 per 10 min, every one an `rtLock` wait (`0x85FE10`, `0x8334CC`, `0x82D296`, `0x62FB74`).
- **Why no cap [verified code]:** the worker sets `t_fbDeadline` for foreign jobs (`j->fb`, `FbJobCapMs=300`), but only the world A\* cost stubs (`BudgetStep`) read it. The cell A\* `0x61E500` and water `0x671930` deadline exits (`PdCellStub` / `PdWaterStub`, docs/164 D3/D4) fire only on `g_pdTid`, the sim thread inside a sync session. SV foreign jobs spend almost nothing in world A\* (122 ms of 46.1 s job time in natural-30) and most of it in cell A\* (18.6 s, 10,696 frames, max frame 129 ms) [verified counters], so the deadline is never read after 300 ms. That the 2 s job is many cell A\* frames is [inferred]: no single phase frame is longer than 181 ms in that run.
- **freeze-report hides this freeze [verified]:** the 2085 ms gap in sv-crowd-0333 is excluded as `probe call` (a 110 ms `keep_alive` agent call overlaps its end; first read as `game_state`, I-168-8a), although the iteration is 99% `rtLock` behind the job. PLAN's "worker-side, not a sim freeze" for this job is wrong (ledger I-178-3).
- **Goal step `0x75C2D0` outliers (item 1): 209 and 331 ms of goal-step *self* time, IP only, cause unknown [verified size, cause inferred].** Self time already excludes LotGraph_Build, portal A\*, cell A\*, lot legs, allocator and `Lock_Acquire` (all their own phase frames; `rjpMaxUs` lotgraph 11 ms, lock 22 us). What is left is untimed code inside the goal step: lot planner reachability `0x63F490` / room graph first use `0x63D2F0` own work, `LotPortalSearch 0x63EC40`, Q reset/clear `0x61E320`/`0x61C020`, the obstacle query and gather `0x82EDB0`/`0x82F0B0` per goal, physics range query `0x83B3A0`, lot index rebuild `0xC68680`. One of the two jobs had 0 goal validations (not a radius route). Probe P-GS (§1.3) splits it in one IP run.
- **Remaining sim freezes >= 300 ms by cause (item 2, §2):** in the valid runs most are covered by the built lock-taker bundle (steering, avoidance, gate 0, UpdateSimFootprint, F-G placement, RouteToObject) plus ForeignKinds (foreign `ReplanFromPosition` sync plans). Uncovered keys: sim goal validation steering `0x75A64C` (up to 376 ms), gate-12 `GetIfPresent` `0x62FB74` (up to 338 ms), `RemovePrivateFootprint` `0x82A0F6` (191-227 ms), destroy `0x2000003`. Under conservation (docs/152 §1) every one of these is "the rest of a running job", so shortening long jobs (T1, T4) cuts all keys at once; covering keys one by one only moves the wait.
- **P5 world copy (item 4, §4)** is the structural fix for "sim waits behind any job", own jobs included, but today it lends only on A10 `ReplanFromPosition` jobs (0 in the release config, docs/158 §1), has not had its first live correctness run, and needs a P6 extension to own and foreign `Route.Plan` jobs. ~15-25 h + 4-6 game runs + 2 clean nights before any hitch gain.

## Ranked targets

| # | Target | Evidence | Est. hitch gain [inferred] | Effort | Risk | Next step |
|---|---|---|---|---|---|---|
| **T1** | **Foreign-job deadline for every search** (FbJobCapMs reaches cell A\* and water on the worker: a second thread slot in `PdCellStub`/`PdWaterStub`, opened by the worker for `j->fb` jobs with the job's `t_fbDeadline`) + a "long job phases" log line (per-phase self ms for jobs >= 300 ms) | SV crowd 2.08 s job, sim wait 2069 ms; SV natural 13 foreign jobs >= 300 ms (max 2.2 s), >= 1000 ms freezes 3.7 / 10 min all `rtLock`; IP natural foreign max 445 ms, IP locktaker 854 ms (host 852 ms: sim serving, not waiting); `capAt -1` everywhere | SV: >= 1000 ms freezes -3..-4 per 10 min, max freeze ~2.2 s -> ~0.3-0.45 s; IP: 0-1 fewer >= 300 ms per 10 min | 1.5-3 h (reuse D3/D4 exits and their selftests; worker slot + selftest case) + 1 day verify + 1 clean-night SV A/B | low-medium: a foreign plan past 300 ms fails like a sync PlanDeadline plan (script retries; docs/164 §7); more failed routes in SV crowds is the behaviour to watch (`fbjcCapped`, stuck log) | build on top of asi.0334, `--ab fbcapall`, SV crowd loop + `sv-crowd-cause` (both orders) |
| **T2** (done 2026-10-23, docs/168 §8, ledger §295) | **freeze-report: keep `rtLock`-led gaps as real** (do not exclude a gap as `probe call` when its longest iteration is >= 80% `rtLock`) | the 2085 ms SV gap was dropped; same risk for every agent-polled run | 0 game gain; fixes the metric (SV >= 1000 ms per 10 min 0.0 -> ~2.2 in sv-crowd-0333, one freeze in ~4.5 real min) | 20-40 min + `--self-check` case | none | change `freeze-report.ts` / `worst-hitches.ts` exclusion, re-read today's SV and IP runs |
| **T3** | **Goal step split, probe P-GS**: 8 more `[RouteJobPhases]` sites inside `0x75C2D0` (goal-room reachability `0x63F490`, room graph `0x63D2F0`, `LotPortalSearch 0x63EC40`, Q reset `0x61E320`, obstacle query `0x82EDB0`, gather `0x82F0B0`, range query `0x83B3A0`, lot index rebuild `0xC68680`) + goal count / accepted / lots / houseboat flag in the `long rest` line | 2 of 4,736 goal steps >= 200 ms (209, 331 ms self) in 8 IP min; 0 in 54 BP/SV runs (docs/177); prologues read (§1.3) | probe: 0. Fix after it: IP 0-2 fewer >= 300 ms per 10 min (only if the sim waits; §1.2) | probe 45-90 min (most sites are already detoured: chain like RjpOwner) + 1 IP run; fix 2-6 h | low (observe only); fix risk depends on the sub-call | build P-GS with T1 in one asi, one IP job |
| **T4** | **Own long jobs: measure the sim wait behind them** (extend the `fbjc` wait split to own START jobs) | own jobs >= 300 ms: IP locktaker 15 (max 1121 ms), IP natural 4 (max 750), SV crowd 2, goal-split 2 (max 493); no counter says how long the sim waited behind them | probe: 0; tells whether own jobs (which may not be cut: the route Sim must get its path) are worth P5/P6 or exact memos | 30-60 min with T1 | none | same build as T1 |
| **T5** | **Lock-taker bundle + ForeignKinds A/B** (already built: `steerro,avoidro,gate0ro,deferfootprint,deferfootprint2,routetoobjro,placementro` + `a10foreign`) | IP natural: 22 of 32 freezes >= 300 ms are keys this set covers (steering 7, F-G 4, UpdateSimFootprint 2, foreign RFP sync 8, `0x83156D` 1); locktaker-verify on arm still shows `0x62FB74`, `0x75A64C`, destroy | conserved by design for the same job; gain = residual events only (docs/152: ~15-35 ms/s B1); visible-freeze gain unknown until the clean-night A/B | 0 build | known (selftested, verify run clean) | read `clean-night-1023` lock-taker A/B with `hitch-ab.ts --scenario` |
| **T6** | **Sim goal validation read-only** (`0x75A64C` = return of the steering query call at `0x75A647` in `GoalPoint_Validate 0x759B20`, sim sync plans): retarget that call to S2's read-only obstacle query under QL shared, misses fall through | 376 ms (locktaker on arm), 199/189 (goal-split), 190 (IP natural), queuefull off arm; unmapped in docs/152 G7 | IP 0-1 fewer >= 300 ms per 10 min; also shrinks as ForeignKinds moves sync plans off the sim | 3-5 h + 1 verify | medium (21 unresolved indirect sites in validation, docs/177; RO only on the query, not the body) | after T5's A/B shows which keys absorb |
| **T7** | `RemovePrivateFootprint` defer (docs/152 G4) | SV crowd 191/227 ms (`0x82A0F6`), IP natural 4 waits >= 150 | 0-1 >= 300 ms per 10 min in crowds | 1-2 h | low | with T6 |
| **T8** | **P5 world copy for `Route.Plan` jobs (P6)** | §4 | removes the sim wait behind every clone-run job, own and foreign (the T1 cut only covers foreign) | ~15-25 h + 4-6 runs + 2 clean nights | high (clone correctness: lock-key writer `0x807110` unrecorded, docs/156; G3/G4 not passed) | first live P5 correctness run on A10 RFP (ready since 0.36.23), then decide |

Not routing, seen in the same tables (for the record): Overwatch nightly tick (`OverwatchSpreadHook.OnTimer`, 322-488 ms; overwatchchain v2, clean-night confirm pending), `Lot.Simulate` script runs 312-336 ms (`WorldLot`), and harness setup (`Ts3Agent.PollTask` 0.9-1.9 s, `StateMachine` setup): not natural play.

## 1. Goal step `0x75C2D0` (item 1)

### 1.1 What the data says

- P-G counters (`route-goal-ab-2026-10-23-ip-goal-split-verify`, delta): goal step `pgN_1` 4,736 calls, `pgUs_1` 4.49 s, max 331.8 ms; goal generation max 1.6 ms, EditPathForRoute max 3.1 ms, validation max 0.55 ms (246,277 validations). `pgLongRest` 2 [verified].
- The two lines: `long rest 209 ms of a 229 ms job (kind 0, ... ) goal step 209 ... validate 0 ms (0 validations)` and `long rest 335 ms of a 381 ms job (kind 0, ...) goal step 331, EditPathForRoute 1, validate 2 ms (102 validations)`; both `own START, IC kind 0` [verified log].
- Goal-step time is **self** time: `RjpLeave` charges each frame's time minus its children's, and `LotGraph_Build 0x63CB90`, `LotPortalAStar 0x63E340`, `QuadTreeCellAStar 0x61E500`, `LotSearch_Entry 0x640E00`, `EA_AllocTagged`/`EA_Free` and `Lock_Acquire 0x4E16F0` are their own frames [verified `routephases.cpp` site table]. Their run maxima are small (lotgraph 11 ms, portal 3 ms, lock 22 us, alloc 2 ms) [verified `rjpMaxUs`], so neither a lazy lot build, a lock wait nor portal A\* explains 200-330 ms.
- It is an IP-only tail: in docs/177's 56 phase-timed runs only IP runs ever had "rest" >= 200 ms [verified docs/177]. Isla Paradiso has houseboats (moving lots) and water lots; the lot index rebuild `0xC68680` and lot planner reachability over boat lots are the first suspects [inferred].

### 1.2 Does the sim wait on it

The goal step runs inside the job, under the job's `+0x918` hold (TailUnlock lends only in post-pass tails, DoorMemo only on host calls) [verified docs/150, docs/159]. So any sim `+0x918` taker during it waits until the job ends. In the same run 7 iterations wait >= 150 ms at steering `0x85FE10` (365 and 453 ms among them), 3 at gate 0 `0x62EF7A` and 2 at sim goal validation `0x75A64C`. The run has no per-job timestamp, so these cannot be joined to the two long jobs [inferred: likely for the 365/453 ms steering waits, which match 381 ms]. P-GS's log line adds the job's QPC end so the next run can join it.

### 1.3 Probe P-GS (proposed, not built)

Phase sites inside the goal step, prologues read from the Steam 1.67 exe (no relative operand in the first instruction(s); branch-into scan still to do per site, as for v87):

| Site | Function | First bytes | Existing detour to chain through |
|---|---|---|---|
| `0x63F490` | `LotPlanner_GoalRooms` | `83 EC 54 8B 44 24 5C` | `[RouteGate]` "goal-step lot planner" |
| `0x63D2F0` | `RoomGraph_FirstUse` | `55 8B EC 83 E4 F8` | `[RouteGate]` gate site 2 |
| `0x63EC40` | `LotPortalSearch` | `55 8B EC 83 E4 F8` | values probe call sites only |
| `0x61E320` | `AStarSearch_Reset` (Q) | `53 8B 5C 24 08` | `[RouteGate]` / CellMemo "Q reset" |
| `0x82EDB0` | `RoutingWorld_ObstacleQuery` | `55 8B EC 83 E4 F0` | steering / S2 |
| `0x82F0B0` | `RoutingWorld_GoalFootprintGather` | `55 8B EC 83 E4 F0` | gather RO |
| `0x83B3A0` | `Routing_PhysicsRangeQuery` | `83 EC 10 53 55` | rtsites `RT_QREAD` |
| `0xC68680` | `WorldManager_RebuildLotIndex` | `83 EC 1C 53 55 56` | rtsites `RT_IDXREBUILD` |

Counters: 8 more `pg` rows (reserve after v102 in `docs/registry.md`). Log line per goal step >= 50 ms: goals in / accepted / rejected, distinct lots, houseboat lot among them, per-site self ms, job QPC start/end. Pass: the sum of the new rows explains >= 80% of a >= 100 ms goal step.

Fix candidates by outcome [inferred, exactness to prove per case]: reachability memo across calls keyed by (lot, start room, goal room, lot graph epoch) if `0x63F490`/`0x63D2F0` lead; skip the lot index rebuild when the lot grid did not change (epoch) if `0xC68680` leads; one obstacle query per distinct goal cell if `0x82EDB0`/`0x82F0B0` lead.

## 2. Remaining sim freezes >= 300 ms (item 2)

Natural play (outside repro firings), cause of the longest iteration per freeze (`freeze-report.ts --min 300`):

| Run | >= 300 / >= 1000 per 10 min | Causes (n, max ms, wait key) |
|---|---|---|
| SV crowd 0333 | 6.6 / 0.0 (~2.2 with the T2 fix) | steering `0x85FE10` 1 (348 ms wait); `0x85FE81` 1 (371, 513 ms gap); `RemovePrivateFootprint 0x82A0F6` 1 (191); excluded as probe call: **foreign job wait 2069 ms** |
| IP locktaker verify | 16.8 / 0.0 | gate-12 `GetIfPresent 0x62FB74` 338 ms (with a NoStart `ReplanFromPosition` sync plan); `Lot.Simulate` script 312; foreign RFP sync plan 257 |
| IP natural-30 (10-22) | 12.8 / 0.0 | ThreadedObjects waits 9 (F-G placement 4, UpdateSimFootprint 2, `ObjectFootprintFromTool` 483); route search 8 (foreign RFP sync); locomotion 7 (steering 6); Overwatch 6 |
| BP queuefull / nopath loops | 0 / 0 | nothing >= 300 outside firings |

Inside repro firings (`--repro-causes`, scenario cost): queuefull off arm: sync `Route_PlanLocked (no IC) cell` 218/117 ms, `0x75A64C`; on arm: `IC Plan/QueueFull/nojob` 489 ms. locktaker-verify off arm: steering 289 ms, `0x83156D` 117 ms, RFP sync; on arm: `0x75A64C` 376 ms, `GetIfPresent` 188/338 ms, destroy `RtWaitJob`. SV crowd: `RemovePrivateFootprint` 227 ms in `crowd_go`.

Iterations with a route wait >= 150 ms by key (four runs: SV crowd 0333, IP locktaker verify, IP natural-30, IP goal-split verify): `0x85FE10` steering 58, `0x833593`/`0x8334CC` UpdateSimFootprint/ObjectFootprintFromTool 22, `0x62FB74` 10, `0x62EF7A` gate 0 10, `0x83156D` 7, `0x82D296` 6, `0x75A64C` 5, `0x82A0F6` 6, `0x846D21` 7, `0x2000003` 8, `0x06000000` discourage drain 5, `0x62F782` 4.

| Key | Covered by | State |
|---|---|---|
| `0x85FE10`, `0x85FE81` steering | S2 `steerro` | built, in T5 |
| `0x846D21`, `0x85B50C` avoidance | AvoidRO | built, in T5 |
| `0x62EF7A` gate 0 | Gate0RO | built, in T5 |
| `0x833593`, `0x8334CC`, `0x82D296` | DeferFootprint, DeferFootprint2 | built, in T5 |
| `0x83156D` | RouteToObjRO | built, in T5 |
| `0x631F40` F-G | PlacementRO | built, in T5 |
| `0x62F782` gate 12 F-B | SlotEnabledRO | built, in T5 |
| foreign RFP sync plans (`route search`) | ForeignKinds=4 (docs/176) | built, in T5 |
| foreign `Route.Plan` job length | **none** (FbJobCapMs world A\* only) | **T1** |
| `0x75A64C` sim goal validation | **none** | **T6** |
| `0x62FB74` GetIfPresent writers | none planned (real writers) | residual |
| `0x82A0F6` RemovePrivateFootprint | **none** (G4 gap) | **T7** |
| `0x2000003` destroy | design G park (`destroypark`) | built (docs/152 B0 list) |
| `0x06000000` discourage drain | Discourage D (off) | residual |

## 3. The 2.08 s worker job (item 3)

- **Kind [verified log line 245]:** `route async: long job 2076 ms, foreign START, IC kind 0, route 250D7EB7, capAt -1 ms tail -1 ms, plan 2076 ms host 65 ms`. A `Route.Plan` started from a task that is not the route Sim's own (FallbackAsync `NoStart` path, docs/134), run whole on the worker. Almost all of it is inside `Route_PlanLocked` (plan 2076, host 65 nested).
- **Cap [verified code]:** `FbJobCapMs` applies to foreign jobs only (`t_fbCapUs = j->fb && fbjcMode == 1 ? fbjcCapUs : 0`); own jobs (`j->fb == 0`) get no cap by design, and A10 foreign kinds (`af`, docs/176) are excluded too. This job *was* foreign and the deadline was set, but `BudgetStep` (the only reader on the worker) runs in world A\* cost calls only; the cell A\* and water deadline exits of plandeadline.cpp test `g_pdTid` (the sim's session) and pass the worker straight through. `capAt -1` = no world A\* cost call came after 300 ms.
- **Does the sim wait [verified]:** yes: `route jobs split ... sim wait max 2069 ms`, `fbjcWaitHist_5` 1, and the hitch iteration at QPC 129703071 ms: `locomotion` 2069.5 ms, `rtLock` 2069.05 ms, key `0x85FE10`, wall 2085 ms. It is a full 2 s visible freeze; freeze-report dropped it as a probe call (T2).
- **Where the 2 s went [inferred]:** SV foreign jobs: cell A\* 3.97 s of the run's foreign time, world A\* ~1 ms; no phase frame longer than 181 ms. So the job is many cell A\* calls (goal-lot entry loop or lot-leg retries per goal room), each under the game's 64,000-node cap. T1's long-job line confirms it.

## 4. P5 world copy (item 4)

What it would remove: during a P5 clone run the job plans on its own copy and lends `+0x918` (P5b: steering and read-only gate leaves; P5c: every plain sim acquire), so the sim stops waiting behind that job's plan, whatever its length and whatever the key [verified docs/155 §1]. That is the one fix that also covers **own** jobs (T4), which T1 may not cut.

What remains [verified docs/158 §1, docs/156, docs/157, PLAN]:
1. First live correctness run of P5/P5b/P5c (ready since 0.36.23, never run; docs/157 fault paths selftested).
2. G3/G4 shadow gates of docs/116 (no pass yet) and the clone writer gap `Footprint_SetLockKeyObject 0x807110` (lock-key ICs, no W1/W2 record, docs/156).
3. Eligibility: today only A10 `ReplanFromPosition` jobs lend; A10 is off in the release, so the gain is 0 [measured docs/158]. Own and foreign `Route.Plan` jobs need the P6 design (docs/94 §22.30.4).
4. Gain by conservation: the sim used at most 4-17% (P5b) / 22-50% (P5c) of the lent time on BP [inferred, docs/158]; the hitch value is in the long jobs, which P6 would cover.

Estimate: ~15-25 h of build and fixes, 4-6 game runs, 2 clean nights before a hitch A/B. It ranks after T1 because T1 removes the measured 1-2 s freezes in a few hours with an exit the game already uses, and after T3/T4 because those decide whether own jobs need P6 at all.

## 5. Ledger

docs/51 §293 (I-178-1 .. I-178-7), §294 (I-178-8 .. I-178-14, the build below).

## 6. Build: T1, T3, T4 (ts3native 0.36.31, counters v103, `asi.0335`; selftest only, no game run)

Built 2026-10-23 on top of asi.0334 (0.36.30) on branch `worktree-agent-a2d2d0ec1ce7b0636` as `out-t1/ts3native.asi.0335`. Every piece is off by default; selftest ok, `tools/model/p5-lock-model.ts --self-check` ok (no lock order changed: T1 only makes a job's +0x918 hold shorter), `tools/hook-sites.ts --check` ok (no new address: all eight P-GS sites were already named). T2 (freeze-report) is not in this build.

### 6.1 T1 `[RouteAsync] FbJobDeadline=1`

- **What:** the two plan-deadline entry stubs (`PdCellStub` at cell A\* `0x61E500`, `PdWaterStub` at water `0x671930`, docs/164 §7 D3/D4) get a second thread slot, `g_pdWkTid`. ServeAsync opens it (the worker's id) for exactly the span of `CallIc` of a job that has a deadline, i.e. a foreign job (`j->fb`) with `fbjcMode` 1 and `FbJobCapMs` > 0: `t_fbDeadline` = job start + FbJobCapMs, the same value the world A\* cost stubs (`BudgetStep`) already read. Past it, each further cell A\* / water search on that thread takes the game's own no-path exit: cell `[this+0x24] = 0`, AL 0, `ret 0x18`; water out vector end = begin, AL 0, `ret 0x24` [verified disassembly, docs/164 §7]. World A\* capping is unchanged. Own and A10 foreign jobs have no deadline and are never cut. Runtime switch `fbjdMode` (+0x440C, `native_set fbjobdeadline`), read per search, so `--ab fbjobdeadline` flips it per firing; installs only with `FbJobCapMs` > 0 and the cost stubs (else `FAIL foreign job deadline`).
- **Ends as an ordinary failed plan [verified source, live unverified]:** the cut search returns what the game's own exits return, so the planner's own failure handling runs (lot legs try their next goal room and are cut again, the world A\* drains its open list), the IC maps the result, and IcCollect copies `j->result` to the caller as for any finished job, exactly as a world-A\*-capped foreign job (FbJobCapMs, built 0.36.16) does today. No extra write: the stubs write only the fields the game's exits write. Same failure as SyncCapMs / PlanDeadlineMs on the sim thread (no path; script callers retry). Differences from an uncut failed plan, both on our side only: P4's shadow compare of that job (its clone run has no deadline) would see a mismatch, and `[CellMemoProbe]` never sees the cut calls (the stub answers before the phase frame). One cell A\* already running when the deadline passes finishes (game cap 64,000 nodes); portal A\*, LotGraph_Build, post pass and the goal step are not cut (docs/164 §7.2), so a cut job ends at deadline + the in-flight search + the uncut rest: `fbjdLateUs` / `fbjdLateMaxUs`.
- **Counters (v103):** `fbjdSkip[2]` searches cut (cell, water); `fbjdCapped[3]` jobs cut in world A\* / cell / water; `fbjdLateUs`, `fbjdLateMaxUs` (job end - deadline of cut jobs); `fbjdLong[2]` foreign jobs >= 300 / >= 1000 ms in either mode (the A/B's per-arm read); `fbjdLines`. A cell/water cut also sets the job's `capAt` and counts in `fbjcCapped` (the "route async: long job ... capped" line).
- **Log:** with `[RouteJobPhases] Enabled=1`, one line per foreign job >= 300 ms (worker, 20 per minute): `route phases: foreign job N ms (route H, deadline on|off): world, cell, water, portal, lot legs, goal step (P-G row, or n/a), candidate, lot graph, plan, post, insert, host, alloc, other ms; cut world / cell / water` (self time per phase from a per-job copy of the v87 rows). Settles I-178-4.

### 6.2 T4 `[RouteAsync] OwnJobWait=1` (observe only)

The sim thread's two job waits (the +0x918 pump in LockAcquireAsync and RtWaitJob) note whether an own job (neither `fb` nor `af`) ran during the wait (`g_jobOwn`, set with `g_jobFb`) and its route handle: `owWaits`, `owWaitUs`, `owWaitMaxUs`, `owWaitHist[6]` (buckets of v84), and a line `route jobs: sim waited N ms behind an own job (route H)` for waits >= 300 ms (20 per minute). One wait is the sim's whole stall behind that job (it is blocked until the job releases), so per wait = per job. The minute counter line `route jobs T1/T4` sums both.

### 6.3 T3 probe P-GS `[RouteJobPhases] GoalStepSplit=1` (observe only, needs GoalSplit=1)

Eight more `[RouteJobPhases]` sites (stubs 19-26): `0x63F490` lot planner, `0x63D2F0` room graph first use, `0x63EC40` LotPortalSearch, `0x61E320` Q reset, `0x82EDB0` obstacle query, `0x82F0B0` gather, `0x83B3A0` range query, `0xC68680` lot index rebuild, prologues of §1.3. A frame is pushed only on the worker inside a job and only when the innermost open frame is the goal step `0x75C2D0` (or another P-GS frame inside it), so its self time leaves the goal step's self time and no v87 row changes. Ownership: [RouteGate]'s gate stub (`0x63F490`, `0x63D2F0`, `0x61E320`) or its Sites stub (the other four; chosen by address), else `[LoadProf]`'s room-graph entry, else our own detour when the bytes still match (`0x63EC40` has no other entry owner), else SKIP (logged, bit missing in `gsReady`); `[CellMemoProbe]` now chains its Q reset through P-GS's detour when [RouteGate] is off. Branch-into scan: the seven owned sites are already live detours with the same stolen length; `0x63EC40` (`push ebp; mov ebp,esp; and esp,-8`) not scanned [inferred safe: frame prologue]. Counters `gsN/gsUs/gsMaxUs[8]`, `gsSteps`, `gsLong`; line per goal step >= 50 ms: `route phases: goal step N ms (self S ms) qpc A-B ms, job kind K route H: <site> ms/calls ...; P-GS P% of the step's own time` (QPC window joins `-hitches.jsonl` `t`, I-178-7). **Houseboat flag: not built** (no boat-lot field read yet); the lot index rebuild calls are the proxy.

Also fixed: `RjpOwner(RJS_PENDING)` returned `&g_rtTramp[RT_PENDING]` = slot 5 (`RT_PENDING` is a kind, the pending site is table row 9), so with `[RouteGate] Sites=1` the v87 "pending" row timed the quadtree ray leaf `0x681820` on the worker. Owners are now found by address. v87 pending rows of runs with Sites=1 are suspect.

### 6.4 Queue `tools/harness/queues/route-t1.json` (dry run only)

Job 1 `sv-crowd-t1-verify`: the sv-crowd-cause settings (rare-big-crowd, 10 min) + FbJobDeadline, OwnJobWait, GoalSplit, GoalStepSplit; `minCounters` 103; correctness (OK lines, `fbjdReady` 7, `gsReady` 255, no watchdog). Job 2 `sv-crowd-t1-ab`: new repro `sv-crowd-foreign-loop` (30 spawned Sims + household to the far lot, then the game's stuck check armed for every spawned Sim, attempt 1: foreign `Route.Plan` jobs under crowd contention), ABBA flipping `fbjobdeadline` only, 10 firings per arm fixed, tool-check measurement settings; read with `hitch-ab.ts --scenario`. Copy `out-t1/ts3native.asi.0335` to `game-copy/ts3native.asi.0335` first.

### 6.5 P-GS accounting fix (ts3native 0.36.32, `asi.0336`, counters unchanged v103; selftest only, no game run)

`route-t1-2026-10-23-sv-crowd-t1-verify` printed `goal step 172 ms (self 0 ms) ... portal search 0.8/1 ...; P-GS 97% of the step's own time`: the eight sites add up to ~1 ms of a 172 ms step. Cause [verified `routephases.cpp` 0.36.31 + the run's `-native.json`]: the line printed only the eight sites' **self** time and divided it by (sites + step self). Every other timed frame nested in the step (v87 phases such as cell A\*, portal A\*, alloc, lock, host, and P-G's validate / EditPathForRoute), at any depth, including those under a P-GS site (portal A\* inside LotPortalSearch), was in neither, so both "self 0 ms" and "97%" were right by their own definition and meaningless together. Not a unit error (us, `/1000` and `/100 % 10` are right), no double counting (each frame's self is counted once), no other-thread time (P-GS frames only on the worker inside a job, and only under an open goal step).

The counters say the time is not in the goal step's own code: `pgUs_1` (goal step self) 308 ms over 5,481 steps, `pgMaxUs_1` **879 us** [verified]. So every 52-172 ms step of that run is in nested timed frames; P-GS site self totals 3.91 s (LotPortalSearch 2.51 s, room graph 0.61 s with one 50 ms call, obstacle query 0.58 s), P-G validate 3.02 s over 295,196 calls (not split per step before; which share of it is inside goal steps is what the new line shows).

Fix: per goal step, every frame closed while the step is open (any depth) adds its self time to a per-phase row (`t_rjp.gsPh/gsPhN[RJP_NALL]`, thread-local, no new counters), so step = its own code + the rows, exactly (selftest). New line: `route phases: goal step N ms qpc A-B ms, job kind K route H: P-GS sites <8 x self ms/frames>; other phases inside <phase self ms/frames, non-zero only> (self ms/frames); unattributed U ms (the step's own code); P-GS P% of the step` with P = sites' self / the whole step. The global `gs*` counters keep their v103 meaning (P-GS site self time). Naming further `0x75C2D0` callees is not needed while `unattributed` stays small (it was <= 0.9 ms per step in this run); a run where it leads makes that the next static step. Selftest `TestGoalStepSplit`: a 100 ms step with an 80 ms LotPortalSearch frame holding a 70 ms cell A\* frame reads cell 70, site 10, unattributed 20, sum 100. Ledger §299.
