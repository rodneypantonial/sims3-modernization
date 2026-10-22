# 150. Route job `+0x918` hold: breakdown by job kind and by phase

Written 2026-10-22. **Static plus existing data only:** no game run, no queue, no build. Ledger rows: docs/51 §222. Question (docs/143 §8.14-8.15): the sim thread's summed `+0x918` wait is bounded by the route worker's lock hold (`asyncUs` ~115 ms/s on BP); where does that hold go, and which parts can shrink?

Sources: `probe-out/profile/*-ab-native.jsonl` (all 2026-10-20..22 BP jobs, per-slice deltas), `probe-out/profile/*-bp-*-simstack.jsonl` since the 2026-10-16 sampler fix (72 files, 58,211 sim-thread samples, 11,935 s), `probe-out/queue/*-ts3native.log` (`long job` lines), `native/ts3native/ts3native.cpp` (`ServeAsync`, counter definitions), static callee tree `docs/data/route-planlocked-callees.csv` (new tool `tools/route-job-callees.py`, below).

Tags: **[measured]** = counters or samples of real runs; **[inferred]** = derived with the method named; **[verified]** = code or bytes read.

## Summary

- **What `asyncUs` is** [verified, `ServeAsync`]: the time from the worker's `LockAcquireHook(+0x918)` to its `LockReleaseHook`, i.e. the hold itself: the whole route IC of the job's kind (start points, `Route_PlanCommon`, `Route_PlanLocked 0x82C6E0`, result mapping) plus the waits for the sim thread to run the job's managed calls (`hostCalls`, ~270/s on BP). Back-offs before the acquire are outside.
- **By kind** [measured]: in the release config (A10 off) the hold is almost all **own START** jobs (`IC_Route_Plan`, IC kind 0): ~115 ms/s on BP play/route-bits (86-100 in crowd), 22-25 jobs/s, mean ~5 ms. **FallbackAsync** foreign jobs add ~26 ms/s (on slices 148 vs off 122, ~9 jobs/s, ~3 ms mean) while taking ~22 ms/s of sync plans off the sim thread; rare multi-second tails (docs/134). **A10** kinds, when on: ReplanFromPosition 26-45 ms/s (1.2-1.6 jobs/s, **19-31 ms each**, 4-6x an own START), Replan 6.0, PlanFromPoint 3.3. Discourage drains and D applies run on the sim thread, not in the hold (~0 ms/s of hold); pending lot applies inside jobs are ~0.2 ms/s.
- **By phase** [measured on sim-thread sync plans, applied to the job: inferred]: the sim thread runs the same planner for its own sync plans (`g11PlanUs` 47-55 ms/s on BP); its samples inside the planner (2,560 = 4.40% of samples = ~44 ms/s, matching `g11PlanUs` to ~10%) split as: **in-room cell A\* 30%**, **path-node post pass `0x64E380` 19%**, **`PathNodes_Insert` 13%** (mostly allocation), world A\* 10.5%, lot leg loop (`LotPlan_Inner`) 10%, candidate / goal-lot loops 7%, job bookkeeping 5%, portal A\* 1.4%, `LotGraph_Build` 0.8%, pending applies 0.2%. Across phases, **allocator calls are 28%** and terrain height lookups 9%.
- **Long plans are cell A\*** [measured, small n]: in the 7 sim-thread plans that ran >= 1 s (57 samples, all worlds) cell A\* is 56%, water paths 19%, world A\* 14%; `LotGraph_Build` and pending applies 0%. This is the best existing evidence for I-218-5 (the ~1.6 s non-A\* work the cap cannot see): **the in-room quadtree A\*, not lazy builds or pending applies** [inferred: same planner, sim-thread plans, not the foreign job itself].
- **Wait follows hold** [measured correlation, 293 BP slices]: sim `+0x918` wait (`haUs[0]`) = 0.67 x `asyncUs` - 10.5 ms/s (r² 0.93); `asyncPumpUs` = 0.71 x `asyncUs` - 11.9 (r² 0.93). Reading the slope as causal (docs/143 §8.15: the wait is conserved across acquirers, so it tracks the hold) gives ~0.67 ms/s less sim wait per ms/s of hold removed [inferred].
- **Top 5 shrinkable chunks** (§4): in-room cell A\* (~34 ms/s), allocation (~32, overlaps), post pass (~22), A10 ReplanFromPosition / redundant replans (26-45 when on), world A\* + candidate loops (~20 mean, the multi-second tail). Chunks 1-3 together: hold -28..-53 ms/s, **sim wait ~-19..-35 ms/s** [inferred]. Prebuilding LotGraph is not worth it on average (0.9 ms/s).
- **Missing:** host-call wait share inside jobs (all kinds), the job's IC time outside `Route_PlanLocked`, worker-side lock waits, phase mix of the worker's own jobs (vs the sim's sync mix). §6 specs the counters (or a worker-thread sampler) for one short day job.

## 1. Hold by job kind (BP)

All rows [measured] from `-ab-native.jsonl` deltas over whole runs or switch slices; ms/s per wall second.

| Kind | Config / runs | Jobs/s | Mean per job | Hold ms/s | Notes |
|---|---|---|---|---|---|
| Own START (`IC_Route_Plan`, kind 0) | release config, 50 BP runs 2026-10-21/22 (A10 off; `fbMode` 0 or off slices) | 22-25 (play), 15-17 (crowd) | 4.8-5.4 ms | **112-125** play / route-bits, 86-101 crowd, 57-59 cars | `ownJobMaxUs` 130-470 ms; 75 `long job` lines (>= 200 ms) in all logs: median 218, max 531 ms |
| Foreign START (FallbackAsync) | `bp-fb-play` both orders, on vs off slices | ~9.4 | ~2.8 ms (mean of the delta) | **+26** (on 148 vs off 122) | sim `g11PlanUs` falls 57 -> 35 ms/s in on slices (plans moved off the sim thread); tails: BP 1,948 ms, SV 7,755/7,874 ms (docs/134 §11) |
| A10 ReplanFromPosition | `wr-*`, `snap-p5c-ab`, `avoidro` BP (A10 RFP on, 2026-10-20/21) | 1.2-1.6 | 19-31 ms | **26-45** (`arJobUsReplanFromPosition`) | incl. P5 clone time `m2JobUs` 3-11 ms/s; P5b lends 3.6-8.7 ms/s of it back (`p5bWinUs`) |
| A10 Replan | `wr-asyncreplan` BP 2026-10-20 | 0.52 | 11.6 ms | **6.0** | |
| A10 PlanFromPoint | same | 0.52 | 6.4 ms | **3.3** | |
| A10 all three | `clean-night-2 ip-ar-ab-wait` IP 2026-10-18 | 0.29 / 0.56 / 0.62 | 18-22 ms | 5.4 / 10.2 / 13.3 | IP, for scale |
| Pending lot updates applied in a job | all BP runs | `vfRtPendingInJob` 0.1-0.35 | - | ~0.2 (0.2% of planner samples) | plus `LotGraph_Build` from it, 0.8% |
| Discourage drains, D applies | all BP runs | `discApplied` ~3.5 | - | **~0** of hold | applied on the sim thread at drain points; `discDrainWaitUs` < 1 ms per run; the sim's job waits (`haUs[2]`, `RtWaitJob` + drains) are 5.5-9.6 ms/s, 2.2-3.2 with DestroyPark |
| Total `asyncUs` | release BP play | 22-25 | ~5 ms | **~115** (docs/143 §8.14), 137-148 with FallbackAsync on | |

Not split by any counter today: how much of each job's hold is spent waiting for the sim thread to run its managed calls (`t_jobHostUs` exists only for foreign jobs, max only) and how much is IC work outside `Route_PlanLocked` (BP capped foreign job: 6 of 1,948 ms).

## 2. Hold by phase inside the job

**Method** [inferred]: the worker is not sampled. The sim thread's stack sampler (205 ms interval, 24 frames, leaf first) catches the sim's own sync plans, which run the same `Route_PlanLocked` -> `RoutingJob_Execute 0x81B2A0` -> `PathPlan_WithRetries 0x661B50` -> `PathPlanner_PlanHierarchical 0x65E0D0` code. Each sample is assigned to the innermost phase function on its stack (addresses mapped to functions with `game-copy/analysis/functions.csv`). Coverage check [measured]: 2,560 of 58,211 BP samples (4.40%) = ~44 ms/s vs `g11PlanUs` 47-55 ms/s in the same jobs. Applying the shares to the worker's ~115 ms/s assumes the job mix (`Route_Plan` requests) has the sync mix's phase profile (Replan family + refused STARTs); the per-kind means (own 5 ms vs RFP 19-31 ms) show the mixes differ in size, so the ms column is an estimate. Managed callbacks made from inside the planner are mostly invisible here (Mono frames push the planner frames out of the 24-frame window).

| Phase | Function(s) | Share of planner samples (BP, n 2,560) | Est. hold ms/s at 115 | Long plans >= 1 s (n 57, all worlds) |
|---|---|---|---|---|
| In-room cell A\* | `QuadTreeCellAStar 0x61E500` (via `InRoomPlan 0x632E40` / `InRoomPlanToPoint 0x632630` from `LotPlan_Inner 0x63F6E0`); 94% of it in `QuadCell_ExpandNeighbours 0x66BFD0` | **29.6%** | **~34** | **56%** |
| Path-node post pass | `0x64E380` (node heights `0x64C7C0` -> `Terrain_GetHeight 0xC6A540` raycast; `0x64DD40`, `0x645310`, `0x6426B0`) | **19.3%** | **~22** | 0% |
| Path node insertion | `PathNodes_Insert 0x629140` (75% of its samples inside `EA_AllocTagged 0x4E3F90`) | **12.9%** | **~15** | 3.5% |
| World A\* | `WorldAStar_Search 0x676260` (cost `RouteCost_Mode3 0x674250` 8.8% incl.) | 10.5% | ~12 | 14% |
| Lot leg loop | `LotPlan 0x6410B0` + `LotPlan_Inner 0x63F6E0` own code | 10.1% | ~12 | 0% |
| Other in-room | `InRoomPlan` / `InRoomPlanToPoint` own code | 2.2% | ~2.5 | 5.3% |
| Candidate and goal-lot loops | `0x65E0D0` own code, `LotSearch_Entry 0x640E00`, `LotEdgePortals_Choose 0x65BC20` / `_Gather 0x654CC0`, `GoalOrder_Sort 0x6542E0` | 7.3% | ~8.4 | 0% |
| Job bookkeeping | `RoutingJob_Execute` own code (path copy, sub-path split, options), `PathPlan_WithRetries` own code, `RoutingJob_ctor 0x819D50`, `Route_PlanLocked` self | 5.3% | ~6 | 0% |
| Portal A\* | `LotPortalAStar 0x63E340` | 1.4% | ~1.6 | 1.8% |
| Lazy lot builds | `LotGraph_Build 0x63CB90` | 0.8% | ~0.9 | 0% |
| Pending lot updates | `RoutingWorld_ApplyPendingUpdates 0x82C420` own code | 0.2% | ~0.2 | 0% |
| Water paths | `WaterPath_Plan 0x671930` / `0x656A70` | 0.2% | ~0.2 | 19% (IP/SV) |
| **Cross-cutting (overlaps the rows above)** | | | | |
| Allocator | `EA_AllocTagged 0x4E3F90`, `EA_Free 0x4E3F70`, `GeneralAllocator_* 0x4E5E40/0x4E5920` within 3 frames of a phase | **27.7%** | **~32** | - |
| Terrain / floor heights | `Terrain_GetHeight 0xC6A540`, `World_GetHeightAt 0xC6A250` | 9.1% | ~10.5 | - |
| Lock calls inside the plan | `Lock_Acquire` / `Lock_Release` frames | 1.2% | ~1.4 | - |
| Not visible | host-call waits, IC work outside `Route_PlanLocked`, worker-side lock waits | - | **unmeasured** | - |

Allocation by owner (samples with an allocator frame): `PathNodes_Insert` 315, post pass 125, `RoutingJob_Execute` 52, cell A\* 33, portal A\* 32, `LotPlan_Inner` 31, `LotSearch_Entry` 30. Cell A\* nodes are created on demand (`AStarNode_Get 0x664B10` 27% inclusive, `AStarNodeTable_Create 0x61C2C0` 6.1%; nodes live in a hashed cell table, found or created per visit).

Length of sim-thread plans as seen by the sampler (runs of consecutive planner samples): 4,466 single samples, 427 runs of 2-4, 4 of 5-9, 3 of 10+ (max 13 = ~2.7 s). The mean is many short plans; the tail is a few long cell-A\*-bound plans.

## 3. Static callee tree of `Route_PlanLocked` (5 hops)

`python tools/route-job-callees.py --cache <dir>` (BFS, first parent per node, direct calls plus indirect sites resolved by `tools/mono-glue-closure.py`'s resolver with the docs/102/106 facts in `docs/data/routing-indirect-sites.csv`): **466 functions** (hop 1: 7, 2: 48, 3: 76, 4: 124, 5: 210); 293 indirect sites in the 5-hop set, 219 of them unresolved (no edge; mostly managed host delegates and options vtables, docs/107). Full list: `docs/data/route-planlocked-callees.csv`. Two key phases sit deeper than 5 hops [verified, same tool at depth 8]: `LotPortalAStar 0x63E340` hop 6 and `QuadTreeCellAStar 0x61E500` hop 7.

Main spine with sampled inclusive share (BP sync plans, % of the 2,560 planner samples whose 24-frame stack contains the node; truncated stacks under-count the upper levels) [measured]:

- `Route_PlanLocked 0x82C6E0` (takes `+0x918` at `0x82C733`)
  - `Lock_Acquire 0x4E16F0` 2.4% / `Lock_Release 0x4E17B0` 1.4%
  - `RoutingWorld_ApplyPendingUpdates 0x82C420` (0.2% own) -> `RoutingWorld_ApplyLotUpdate 0x82B590` -> `0x63F450` -> `RoomGraph_FirstUse 0x63D2F0` -> `LotGraph_Build 0x63CB90` (hop 5; 0.8%, also reached lazily from the lot planner)
  - `RoutingJob_ctor 0x819D50` (`RoutingJob_CopyOptions 0x81D440` -> `RoutingOptions_Assign 0x81D090` 1.2%)
  - `RoutingJob_Execute 0x81B2A0` (5.8% own; `EA_AllocTagged` 21.3% / `EA_Free` 13.6% incl., `Terrain_GetHeight 0xC6A540` 8.4%, `World_LotLookup 0xC68990` 7.1%, `RoutingPath_CopyNodes 0x819E10`, `RoutingJob_SplitSubPaths 0x81A340`)
    - `PathPlan_WithRetries 0x661B50` (3.5% own)
      - `PathNodes_Insert 0x629140` 13.4%
      - `PathPlanner_PlanHierarchical 0x65E0D0` 31.2% (lower bound; deep stacks lose it)
        - path-node post pass `0x64E380` 23.1% -> `0x64C7C0` 9.5% (heights), `0x64DD40` 7.4%, `0x645310` 6.4%, `0x6426B0` 4.6%
        - `WorldAStar_Search 0x676260` 10.6% -> `RouteCost_Mode3 0x674250` 8.8%, `RouteCost_Mode1 0x674E40` 1.2%, `WorldAStar_OpenPop 0x673B60` 2.2%
        - `LotPlan 0x6410B0` -> `LotPlan_Inner 0x63F6E0` 26.5% -> `InRoomPlan 0x632E40` 23.2% / `InRoomPlanToPoint 0x632630` 15.9% -> `QuadTreeCellAStar 0x61E500` 30.0% -> `QuadCell_ExpandNeighbours 0x66BFD0` 29.6%, `AStarNode_Get 0x664B10` 27.0%, `AStarSearch_NodeState 0x61E030` 14.7%; `LotPortalAStar 0x63E340` 1.5% -> `PortalEdgeCost 0x634500` 1.7%
        - `LotSearch_Entry 0x640E00` 2.3%, `LotEdgePortals_Choose 0x65BC20` 1.7%, `LotEdge_WaypointPosition 0x653020` 3.1%, `ClosestWaypoints_Try 0x6595B0` 2.3%, `LotPath_AddPortalNodes 0x62FBE0` 1.7%, `WaterPath_Plan 0x671930`

## 4. Top 5 shrinkable chunks

Hold numbers use §2 shares x ~115 ms/s [inferred]; sim-wait numbers use the 0.67 slope [inferred]. Overlaps noted.

| # | Chunk | Hold now | Change | Hold cut | Sim wait cut |
|---|---|---|---|---|---|
| 1 | **In-room cell A\*** (`QuadTreeCellAStar` from `InRoomPlan`/`ToPoint` per lot leg; 56% of >= 1 s plans) | ~34 ms/s mean; the long-plan tail | (a) memo in-room legs per (room, entry point, exit point, room epoch) across one job's candidate loop and across jobs (the candidate loop re-plans the same room legs per candidate portal; epoch from docs/76/79 events); (b) reuse the cell node table per worker (node creation `AStarNode_Get` is 27% inclusive); (c) early-out when the goal cell is not connected to the start cell (a per-room connectivity label, like docs/142's flood colours for the world graph) and a node cap under a job deadline (the FbJobCapMs gap of docs/134 §11.3) | -10..-18 | -7..-12, and bounds the multi-second tail |
| 2 | **Allocation** (28% across phases; `PathNodes_Insert` growth is the largest single owner) | ~32 ms/s (overlaps 1, 3) | grow the path-node vector geometrically or pre-reserve from the request size (detour at `0x629140`'s grow path); a job-local arena for search and path nodes released at job end (the worker is the only allocator user inside the hold) | -8..-15 | -5..-10 |
| 3 | **Path-node post pass `0x64E380`** (heights per node through the terrain raycast) | ~22 ms/s | run it after the worker releases `+0x918` (it works on the job's own path; needs a read-set check that it touches no `+0x918`-guarded state, docs/93), or lend the lock during it (P5c `FreeLock` window); else memo heights per (cell, level) | -10..-20 | -7..-13 |
| 4 | **A10 ReplanFromPosition / redundant replans** (when A10 is on) | 26-45 ms/s at 19-31 ms per job | skip a replan whose request (Sim, goal, options) matches a plan of the same world epoch within N s and reuse its route; coalesce queued replans of one Sim; repeat rate unknown (counter in §6) | 30% repeats: -8..-13 | -5..-9 (A10-on configs only) |
| 5 | **World A\* + candidate loops** (failing floods per candidate, docs/142) | ~12 + ~8 ms/s mean; most of the multi-second foreign jobs | `[RouteEarlyOut]` acting Mode 2 (in progress elsewhere); a planner-level abort in the `0x65E0D0` candidate loop under the job deadline | -3..-6 mean | -2..-4 mean; tail bounded |

Chunks 1-3 combined: **hold -28..-53 ms/s, sim wait -19..-35 ms/s** (of ~70) [inferred; overlap between 2 and 1/3 is ~6% of samples, already netted in the range]. Not worth doing for the mean: prebuild `LotGraph` (0.9 ms/s), pending applies (0.2). Lending (P5b/P5c) is orthogonal: today it lends only around P5 clone runs (3.6-8.7 ms/s); a post-pass lend window is option 3.

## 5. What is measured vs inferred

| Item | Status |
|---|---|
| `asyncUs` = worker `+0x918` hold per job | verified (code) |
| Hold by kind (own, foreign, A10 x3, pending, drains) | measured (counters) |
| Phase shares of the sim's sync plans | measured (samples, coverage checked against `g11PlanUs`) |
| Same phase shares for the worker's jobs | inferred (same code, different request mix) |
| Long plans are cell A\*-bound | measured on 7 sim plans (n 57), inferred for foreign jobs |
| Host-call waits, IC outside the plan, worker lock waits | unmeasured |
| Wait = 0.67 x hold - 10.5 | measured correlation (293 slices); causal reading inferred |
| Hold and wait cuts per chunk | inferred (share x hold x slope) |

## 6. Counters for a short day verify job (spec; option B built in §7)

Reserve the next free counter version in `docs/registry.md` first (v84 is the newest on master). Two options; A is cheaper to build and needs no game-function detours.

**A. Worker-thread stack sampler** (probe agent, profile option `rtstack`): sample the route worker every 5 ms only while `g_async.state == A_RUNNING`, 48 frames, tag each sample with the job's kind and fb flag (exported by ts3native next to `g_jobFb`). Gives §2 directly for the worker, with host-call waits visible as the worker parked in the `InvokeHook` wait.

**B. Phase timers in ts3native** (worker thread only, per-thread depth so only the outermost entry counts; QPC or rdtsc via `s2TscPerUs`), arrays `[kind]` with kind = own / foreign / A10:
- `jbJobs[k]`, `jbPlanUs[k]` (summed `Route_PlanLocked`), `jbHostUs[k]` (extend `t_jobHostUs` to every job), `jbOutsideUs[k]` (IC minus plan minus host)
- `jbPhaseUs[k][10]` + `jbPhaseMaxUs[10]` for `0x82C420`, `0x63CB90`, `0x676260`, `0x6410B0`, `0x63E340`, `0x61E500`, `0x64E380`, `0x629140`, `0x640E00`, `0x671930`
- `jbAllocN`, `jbAllocUs` (`EA_AllocTagged` / `EA_Free` on the worker inside a job), `jbCellAStarN`, `jbCellNodes`, `jbLotPlanN` (candidates per job), `jbWorldAStarN`
- `jbLockWaitUs` (contended `Lock_Acquire` on the worker inside a job, other than the recursive `+0x918`)
- `jbReplanRepeat` (A10 replan whose (Sim, goal, options hash, world epoch) matches one planned < 2 s ago)
- the `long job` line prints the phase split for own jobs too.

Hook sites must pass `tools/hook-sites.ts --check` (S3SS/S3PP).

**Job** (`tools/harness/queues/route-hold-split.json`, not written): BP route-bits 5 min + BP play 5 min, day machine OK (ratios and shares, not absolute timing), release config, then once with A10 RFP on. Pass: phases + host + outside sum to 90-110% of `asyncUs`; no crash; report the §2 table per kind from the worker, the long-job phase split, and the replan repeat rate. That closes I-218-5 and decides chunk 1 vs 3 first.

## 8. Chunk 1 follow-up: can the cell A\* answer be cached? (2026-10-22, static, nothing built)

Static only (capstone over the Steam 1.67 exe plus docs/98, docs/23 §18.1, docs/89). Ledger §230. Outcome: **no cache was built.** A safe cross-plan leg cache cannot be proven from what we know today, and its likely hit rate is low. "Node-table reuse" saves nothing because the nodes already come from a pool. What remains is a within-plan leg memo (design M below), and it needs a counter-only job to measure its hit rate before anyone builds it.

### 8.1 What the search reads and leaves behind [verified]

- **Inputs.** All of these are listed in docs/98 §5 and §9: the level quadtree of one lot and level, including every footprint entry in its leaves; the agent's own object id (`options+0x08/+0x0C`), its ignore list (`+0x38`), its option words (`+0x28..+0x34`) and the allow/deny gate `0x946E90`; the context flags `X+0x50`; the exact start point and the exact goal records (target `T`, threshold, any-room flag, no-nudge flag). A cell's class and multiplier depend on the agent, not only on the room.
- **Per-search state.** `AStarSearch_Init 0x61E360` bumps the u16 generation at `0x11580C4`, sets `T`, empties the open list and clears the result vector. `QuadTreeCellAStar` zeroes the inserted-node counter `Q+0x24` at `0x61E539`, so the node cap applies to each search on its own. A skipped search therefore does not change the cap of the searches after it.
- **Output.** The output is a vector of node pointers into Q's cell table. That table lives until the Q reset (`0x61E320` -> `0x61E2B0`) at the start of every hierarchical plan (`0x65E345`). The wrapper turns those pointers into 64-byte path nodes (`0x62A470`). A cache that outlives one plan therefore has to store the emitted nodes, at the wrapper level (`InRoomPlan 0x632E40` / `InRoomPlanToPoint 0x632630`), not the result of `0x61E500`.
- **Allocation.** Graph nodes come from the routing pool `0x61AE80` (size `0x40`, call at `0x61BB9F` in `0x61BB90`; inroom-test replaces it as one of the four routing pools). Cells are found or created through the table (`0x61C2C0` -> `0x61BEC0`). The general allocator `0x4E3F90` is called only in the table constructor `0x61BF60` (once per Q) and for the result vector. Reusing the node table would remove no allocator time. The 28% allocator share in §2 belongs to chunk 2 (`PathNodes_Insert` growth).

### 8.2 Cross-plan leg cache keyed by (room, start cell, goal cell, epoch): rejected for now

1. **Repeats.** The answer depends on the exact start point and the exact goal points, not only on their cells: entry points, path node x/z and g all come from them. docs/23 §18.1 measured that whole-plan inputs almost never repeat, because Sims move between requests [measured]. The first leg starts at the Sim's position and the last leg ends at drawn goals, so a cross-plan hit can only be a middle leg, from portal point to portal point [inferred].
2. **Sharing between Sims.** Sharing a leg across Sims means leaving the own id and the ignore list out of the key. That needs a proof, for every class slot (`0x806F70`, `0x80F7D0`/`0x80F840`, `0x8066C0`, `0x807090`), that the agent's own object has no footprint in that tree. That proof was not done. If the own id stays in the key, only repeats by the same Sim can hit.
3. **Epoch churn.** The right unit for an epoch is one level tree, which is finer than a room. Every footprint insert or remove in that tree has to bump it: the `UpdateSimFootprint 0x833560` / `0x8334A0` family, the object footprint core, the portal lock refresh `0x632460` (which writes the tree inside `InRoomPlan` itself) and quadtree rebuilds after build/buy. Walking Sims rewrite their footprints every few ticks (docs/89), so in a busy room the epoch moves between almost any two plans [inferred]. The existing world versions (`[RouteSnap]` counters and the "static" version used by `[PlanRepeat]`) are global, not per tree. A per-tree epoch needs hooks at the quadtree insert and remove leaves, and those addresses were not verified in this pass.

### 8.3 Design M: memo in-room legs within one plan (not built)

Within one hierarchical plan, the agent, its options and Q are fixed. The plan holds `+0x918`, and other threads' footprint writers wait on it (docs/89 key `0x833593`) [verified in docs/89]. If the candidate loop of `0x65E0D0` / `LotPlan_Inner` plans the same leg again for each candidate portal [inferred, §4 row 1], those repeats can be served from a memo:

- **Key:** wrapper (`0x632E40` or `0x632630`), `X+0x50`, lot key, level, start point bits, then either the target point bits or a hash of the goal vector with the threshold and the any-room and no-nudge flags.
- **Lifetime:** cleared at every Q reset (`0x61E320`, already a `routegate.cpp` site). Inside the plan, an entry is dropped when `0x632460` actually rewrites a portal footprint. That refresh is idempotent once the classes match, so in practice only the first call in a room can rewrite.
- **Value:** the return code, the goal index and a copy of the appended 64-byte nodes. On a hit the memo appends the copies, sets the goal index and restores `X+0x50`. The portal lock refresh still runs before the lookup.
- **Side effects skipped on a hit, and why they are harmless.** The generation bump: stamps are compared only for equality with the current value, and no node carries the skipped value [inferred]. The start and goal cell-node point writes: `NodeState` overwrites the point when it inserts an unfixed node, and the open-state quirk reads the point that the same search set (docs/98 §8.2) [inferred]. Lazy creation of cells and fills of the class cache: these are deterministic for one context [inferred].
- **Unsafe case.** A managed portal callback (docs/92) inside the plan that writes a footprint. Shadow mode (compute both, compare the nodes bit for bit) catches it.

A variant, M2: memo the neighbour list of `0x66B9C0` on the cell object across the searches of one plan. 94% of the A\* time is `QuadCell_ExpandNeighbours`. Neighbours depend only on the tree's structure, not on the agent, so the memo would be invalidated by the same structure writes. Its gain is the share of cells that get expanded again, which is unmeasured.

### 8.4 Next step and cost

1. **Counter-only probe.** Hash the M keys per plan and count would-hit legs and the cell A\* time they cost (`jbPhaseUs` for `0x61E500`, §6). One 10-min day job (counts and shares, so a noisy machine is fine). About 1-2 h to build.
2. Build M behind `[CellAStarCache] Enabled=0 Shadow=1` only if the would-hit cell A\* time is at least ~5 ms/s of hold. Selftest: inroom-test already calls `InRoomPlan` on constructed trees, so add hit = identical nodes, a portal-refresh write invalidates, and a Q reset clears. Then steerro-test seeds 1-3 x 3000, which runs `InRoomPlan` through the live and clone P (docs/98 §15). About 2-4 h, plus 1 day job and 2 clean nights (BP and IP pairs).
## 9. Chunk 3 check: can the post pass `0x64E380` leave the `+0x918` hold? (2026-10-22, static, no build)

Question (§4 chunk 3): does the 19-pass node post pass read only the job's own path plus data the sim cannot change under us, so the worker could release `+0x918` before it (`[RouteAsync] PostOutside=1`), or lend the lock to the sim's read-only takers during it (P5b-style)?

Method: `tools/route-job-callees.py --root 0x64E380 --depth 5` (300 functions; direct calls plus indirect sites resolved from docs/102/106), then the same BFS per pass (depth 4) with the call chain to every routing-state function. Pass list, order and gates from docs/99 §10.3; gate G (byte `0x115824C`) is 1 in the file (docs/98), so passes 1-15 run in play [inferred: no writer of G checked].

### 9.1 Read/write set per pass

| Pass | Reaches `+0x918`-guarded routing state | Chain (all direct calls) | Kind |
|---|---|---|---|
| 1 `0x644FD0` | lot table, lot quadtree, footprint owners | `0x644B00 -> 0x643E60 -> LotTable_GetCellGraph 0x62FB10 -> LotTable_GetOrInsert 0x62EAB0`; `0x643E60 -> LotQuadTree_QuerySegment 0x667400 -> LotQuadTree_FlushDeferred 0x666A40`; `0x643E60 -> 0x643010 -> ObjectService_FindById 0x939560` | **write** (lot-table insert on miss, docs/80; deferred quadtree apply, docs/101 H6) |
| 3 `0x64DD40` | lot table, lot quadtree | `0x644630 -> 0x644080 -> 0x643E60 ->` as pass 1 | write (same leaves) |
| 6 `0x645310` | lot table, lot quadtree, footprints | `LotTable_GetCellGraph -> GetOrInsert -> LotTable_Insert 0x62E680` (+ `Rehash`); `0x6672E0 -> FlushDeferred -> LotQuadTree_ApplyInsert 0x665C50` / `ApplyRemove 0x666750` | **write** |
| 10 `0x64B430` (boat, option `0x8000`) | steering obstacle query, lot table | `0x6435B0 -> RouteObstructionAtPoint 0x655880 -> RoutingWorld_ObstacleQuery 0x82EDB0` (pin and refcount writes, docs/137/140); `0x655880 -> LotXformA_XY 0x62EF30 -> GetOrInsert` | write |
| 2, 4, 7, 13, 19 | none (path nodes, allocator) | - | job-private |
| 5, 8, 9, 11, 12, 14-18 | none; world manager, terrain, water, lot and partition leaves only (docs/94 N1, N2, N4, N5, N9, N13, N14; `Lock_Acquire` there is `WM+0x228` in `WorldManager_RebuildLotIndex 0xC68680`) | `0x64C7C0 -> 0x64C580`, `World_GetHeightAt 0xC6A250 -> WorldCellGrid_Raycast 0xC2A570`, `Lot_WorldToLocal2D 0xAB8540`, `PartitionManager_RoomAtPoint 0xB499D0` | read of non-`+0x918` world data |

[verified: call chains, direct `call` sites in the BFS; the write effect of each leaf is verified in docs/80 (lot table), docs/101 H6 (deferred flush/apply, pin counter) and docs/140 (obstacle query writes)] [inferred: these leaves really execute on common paths; the BFS shows reachability, not frequency]

### 9.2 Verdict

- **`PostOutside=1` (release before the post pass) is not safe and was not built.** Passes 1, 3 and 6 (always on with G) reach lot-table inserts and the deferred quadtree apply, pass 10 reaches the steering obstacle query. With `+0x918` free these would race the sim's own footprint, lot-table and quadtree writers (docs/81, docs/89), the same races the hold exists for.
- **A P5b-style lend during the post pass is not safe either.** P5b lends because its clone run touches no live routing state (docs/94 §22.9). A live post pass writes the same lot table and quadtree the lent sim readers walk (`LotTable_Contains`, steering `0x82EDB0`, gate leaves), so lending turns a wait into a write-read race. Not built.
- **Already outside today:** with `[RouteSnap] Mode=2` + `ReleaseLock`/`FreeLock` (P5/P5b/P5c, 0.36.23), the whole plan, post pass included, runs on the clone with `+0x918` lent or free. Chunk 3's gain is reached for those jobs through P5, not through a post-pass switch.
- **Narrowest candidate (built 2026-10-22 as `[RouteAsync] TailUnlock`, §11, after the three checks below passed): release only around the tail passes 16-19** (pass 16 heights = 9.5 of the post pass's 23.1 points in §3, so ~40% of chunk 3, ~-4..-8 ms/s hold [inferred]). Their reads are job-private plus world-manager data that the hold does not guard anyway. Before building, three open items must be closed:
  1. the `+0x918` hold count at pass 16 entry on the worker (recursive holds: one release may not free it; P5/H2 counters and owner fields read it);
  2. what `PathPlanner_PlanHierarchical 0x65E0D0` and `PathPlan_WithRetries 0x661B50` do after `0x64E380` returns (lot-graph or footprint pointers fetched before the release and used after it would be stale; the retry re-runs the world A\* under the reacquired lock);
  3. lock order on the reacquire (the worker's `WM+0x228` hold is only around `0xC688A0` and `0x82C420`, docs/60 #5, so not across pass 16 [inferred]; `[TerrainGrave]` already covers rays in flight on any thread).
  Effort once these are closed: ~2-4 h build + selftest, then the steerro seeds and a day verify job; counters would take v89 (v87/v88 pending from parallel agents).

Ledger: §231 (I-150-9-1 to I-150-9-4).
## 7. Phase timers built (`[RouteJobPhases]`, 0.36.23, counters v87; offline, NOT run)

Option B of §6, built in `native/ts3native/routephases.cpp` (switch `[RouteJobPhases] Enabled=1`, default 0, observe only; ledger §233).

- **What is timed**: self time and entries per phase and job kind (0 own START, 1 own A10 kind, 2 foreign/fallback START): `plan` (`Route_PlanLocked` own code incl. RoutingJob bookkeeping), `cell A*` `0x61E500`, `post pass` `0x64E380`, `PathNodes_Insert` `0x629140`, `world A*` `0x676260`, `lot legs` `LotPlan 0x6410B0` (with `LotPlan_Inner` / `InRoomPlan` own code), `portal A*` `0x63E340`, `candidate loops` `0x65E0D0` + `LotSearch_Entry 0x640E00`, `LotGraph_Build 0x63CB90`, `pending applies 0x82C420`, `water 0x671930`, `allocator` (`EA_AllocTagged 0x4E3F90` + `EA_Free 0x4E3F70`), `Lock_Acquire 0x4E16F0` (worker-side lock calls, contended waits included), `host-call wait` (a frame in `Proxy`; plans the sim sends back are its children), `rest of job` (IC code outside every phase).
- **How**: QPC at entry and exit, loadprof's return-slot swap with a 48-deep per-thread stack; a phase nested in another is subtracted from the outer one, so all phases sum to the job (`rjpJobUs` = `asyncUs` clock). Stubs compare the TEB thread id with the worker's id (set only around `CallIc` in `ServeAsync`) before anything else: other threads pay one compare. Entries another section owns (`Route_PlanLocked`, `Lock_Acquire`, world A\* with `[RouteEarlyOut]`, `LotGraph_Build` with `[RouteGate]`/`[LoadProf]`, pending with `rtsites`) are timed by putting our stub in that owner's trampoline pointer, after the owner's stub (docs/68 rule 3 order). Prologues from the Steam exe (capstone); branch-into scan done with the 2026-10-22 fix below (I-150-2: no branch into any stolen byte).
- **Not built from §6**: the `rtstack` sampler, `jbReplanRepeat`, the per-phase split in the `long job` line (read the report instead).
- **Tests**: selftest `TestRouteJobPhases` (layout, worker-only stubs, owner interposition, nested self time counted once, unwound frames dropped), `tools/harness/route-phases-report.ts --self-check`, hook-sites `--check` (docs/68 row).
- **Queue** `tools/harness/queues/route-phases.json` (day, 5 min, noisy OK, release defaults + the switch, `game-copy/ts3native.asi.phases`): `bp-route-phases-verify`, `ip-route-phases-verify`. Pass: rjpReady 1, phases within 10% of `asyncUs`, entries > 0 for every kind that ran. Report: `node tools/harness/route-phases-report.ts <native.json> --seconds 300` (ms/s and share per phase and kind).
- **Fix 2026-10-22 (crash, docs/67 §12, ledger §250)**: all 6 jobs with the switch on (`asi.phases`, `asi.0324`) lost TS3W.exe ~10 s in, with no crash report. Cause [verified in the disassembly and offline]: 12 of the 15 sites return with `ret N` (callee-cleanup: `Route_PlanLocked` `ret 0x28`, cell A\* `0x18`, post pass `0x14`, `PathNodes_Insert` `8`, world A\* `0x1C`, `LotPlan` `0x34`, portal A\* `0x2C`, `0x65E0D0` `0x30`, `LotSearch_Entry` `0x18`, `LotGraph_Build` `4`, water `0x24`, `Lock_Acquire` `4`; only pending and the two allocator entries end in a plain `ret`), so the leave thunk sees `at` = slot + N. `RjpPop` walked down from the top while `slot < at`, took its own frame for an unwound one and popped the parent's, returning into the parent's caller with the callee's esp. The first nested ret-N leave in a job did it (a lone frame survived through the `i > 0` stop, which is why the selftest's plain-`ret` pair passed). Fix: search from the outermost frame for the first `slot <= at` (as loadprof's `LpLeave`): an outer frame's slot is always above `at` because the callee's N argument bytes lie below it. Also checked: no branch anywhere in `.text` (rel8/rel32 jmp, jcc, call) lands inside any of the 15 stolen prologues (I-150-2 closed), no site leaves by a tail `jmp` (world A\*'s only indirect `jmp` is its own switch), the stubs share nothing between threads but the counters. Tests: selftest pair now `ret 4` inside `ret 8` (old code: `rjpDropped` 2 instead of 1, assert); steerro-test `STEERRO_PHASES=1` runs every K8 plan as a phases job with the 11 sites K8 does not fake (old code: access violation at `0x625420` in the first plans; fixed: seeds 1-3 x 3000 ok, 2560 jobs per seed, `rjpDropped` 0). Build `game-copy/ts3native.asi.0326` (master 53bdb56d + fix), sha256 `C13367080FED1473EB925AA7F7B85A8FEBD77B62279E6031A73A51BB4566B63C`; `route-phases.json`, `cellmemo-probe.json` and `routealloc.json` now use it.

## 11. Tail passes off the hold: the three checks and `[RouteAsync] TailUnlock` (2026-10-22, static + offline build, NOT run)

§9.2 left one candidate: lend `+0x918` only around passes 16-19 of `0x64E380`. Its three open items were checked in the disassembly (capstone on the Steam 1.67 exe; scratch scripts, direct calls to 8 hops, the docs/68 hook-site list as the alarm set), all passed, and the switch is built. Ledger §234 (I-150-11-1 to I-150-11-7).

### 11.1 Read set of passes 16-19

- Call order in `0x64E380` [verified]: `0x64E4ED` call pass 16 `0x64C7C0` (2 args, `ret 8`, always), `0x64E50B` pass 17 `0x64D5B0` (3 args, option `0x800`), `0x64E523` pass 18 `0x64A690` (1 arg, option `0x1000000`), `0x64E533` pass 19 `0x641DF0` (1 arg, always). Each failing pass jumps to `0x64E3AF` (return false); pass 19's result is the post pass's. Passes 16-19 have no other caller.
- Closure: 166 functions to 8 hops, direct calls. The only hooked or guarded sites reached are `WorldManager_LotGridLookup 0xC688A0` and `WorldManager_RebuildLotIndex 0xC68680` (from `World_GetHeightAt 0xC6A250`; lock `WM+0x228`, not `+0x918`). No routegate gate, no rtsites routing site, no routesnap writer (W1-W6, footprint, lot, water grid, flush), no `0x82EDB0` [verified, direct]. 15 indirect sites stay unresolved, all in leaves: allocator, `Lock_Acquire` internals, terrain kd-tree `0xC66700`, raycast `0xC2A570`, a vector copy `0x810DF0`, and the object service under `World_GetHeightAt` (`0xB74A20`, `0x7460F0`, `0xA8DF80`, with `ObjectService_FindById 0x939560`) [verified sites; targets inferred as non-routing: `World_GetHeightAt` is the sim's own height query too, docs/94 N1-N2].
- Pass 16 writes globals `0x11CF0B0`-`0x11CF110`: a function-static init of six SSE constants with a guard word; a second thread can only write the same constants [verified]. All other writes go to the job's path nodes, locals and EA heap blocks.

### 11.2 The three checks

1. **Hold count at pass 16** [verified]: the worker holds `+0x918` at least twice, ServeAsync's `LockAcquireHook` and `Route_PlanLocked`'s `0x82C733`; the frames between (`Route_SearchAStar 0x81B2A0`, `0x661B50`, `PathPlanner_PlanHierarchical 0x65E0D0`, `0x64E380`) call neither `Lock_Acquire` nor a CRITICAL_SECTION / wait import. The IC frame may add levels, so the switch lends every level as P5b does (1-16, the game's count at `+0x20` equal to the CRITICAL_SECTION's, I-94-340) and takes the same number back.
2. **Pointers across the window** [verified call sites; inferred for frame values not traced]: `0x64E380` is called from six sites, all in `0x65E0D0` (`0x65E61D`, `0x65E750`, `0x6600EF`, `0x660398`, `0x6619B7`, `0x661B22`); `0x661B50` calls `0x65E0D0` nine times. The window always ends inside `0x64E380`, so their code after the call runs under the retaken hold. What they keep across it: locals (`0x643580` list teardown, `[esp+0x50]` float, `[esp+0x58]` arguments), frame-owned objects released after (vtable `+4`, `EA_Free`, `0x625420` -> gate site `0x61E320`, under the hold again), and the lot table is read fresh (`0x62FBE0 -> LotTable_GetOrInsert`). With P5b's admission only the sim's steering runs in the window; its writes (pin `Q+0x48` +1/-1, the deferred flush when the pin is below 1, footprint refcounts +1/-1, docs/81 §1, docs/140) touch nothing passes 16-19 read, and objects the frame holds keep their own reference. Writers, gates, destroys and sync plans see the worker's hold as before (`A_RUNNING` unchanged), so no lot, footprint or object can go away under the frame.
3. **Lock order on the retake** [verified, direct]: inside the window the worker's only lock is `WM+0x228`, scoped in `0xC68680` (acquire `0xC6869C`, release by the tail jump `0xC6889A -> Lock_Release`), so at the retake it holds nothing below `Route_PlanLocked`; above it the frames are the ones P5b lends from (P5Plan runs inside the same `Route_PlanLocked`). Admitted steering waits for no job and takes nothing the worker holds (docs/94 §22.9, docs/83 §1), so the retake (the game's `Lock_Acquire` in a try loop, as `P5bRetake`) waits at most one steering call. A `WM+0x228` wait in the window holds no `+0x918`, so no cycle.

### 11.3 Built: `[RouteAsync] TailUnlock` (`tailunlock.cpp`, counters v89, default 0)

- **Sites**: the four calls above, rel32 retargeted (`PatchCalls`, all or none; the pass entries stay free). `TuS16` opens the window then calls pass 16; `TuS16`, `TuS17` and `TuS18` retake when their pass answers false; `TuS19` retakes after pass 19. Install needs `[RouteAsync]` with `[RouteGate] Enabled/Sites/Enforce=1` (steering frames, `Lock_Release` original), after `[LocoWindow]`.
- **Open** (`TuOpen`): switch `tuMode` on (`--ab tailunlock`, +0x3878, starts at 1), the route worker inside an async job that owns the lock (not a clone run, not with P4 installed, no P5b window open), counts 1-16 equal; publish `g_p5bWin`, wait up to 200 us for no disallowed acquirer inside the game's `Lock_Acquire` (else refused), release every level with the game's `Lock_Release`. `P5bLock` applies P5b's admission (`g_p5bOn || g_tuOn`): the sim's steering frames (and read-only gate leaves only with `[RouteSnap] ReleaseGates`).
- **Retake** (`TuClose`): every level with the game's `Lock_Acquire` (try loop with `SwitchToThread`, 2 s log + `p5bWatchdog`), `rgOwnerTid` restored, window closed.
- **Tripwire** (`TuTrip`, debug, on in every build): on the worker inside a window, a `+0x918` acquire (`LockAcquireHook`), any gate site (`RgEnter`) or an rtsites site other than lot-index / physics reads (`RtEnter`) counts `tuTrip[where]`, logs the caller and retakes before the touch runs. shortcut: lot-table / quadtree writers without a frame of ours are not tripwired; the closure (§11.1) is the proof.
- **Counters v89** (+0x3878-0x38AB, built right after v87 without v88): `tuMode`, `tuReady`, `tuWins`, `tuRefused[4]` (switch, not a holding worker job, waiter, P5b open), `tuReleasedUs`, `tuRetakeUs`, `tuRetakeMaxUs`, `tuTrip[3]`; admitted steering counts in `p5bSimAcq`.
- **Tests**: selftest `TestTailUnlock` (threaded, real CRITICAL_SECTION with the game's count, worker with 2 levels, fake passes on a job-private path): 40 kept + 260 lent jobs, answers equal to the passes called directly, the worker owns both levels after every tail and none inside a window, 20 failing passes retaken at their return, 13 tripwires (pass 17 taking `+0x918`) retaken first, steering granted inside job bodies only when lent (0 kept / ~7,000 lent), retake max < 1 ms, no hang. `steerro-test` checks the four call sites on the image (E8 to each pass) and passed seeds 1-3 x 3000. hook-sites `--check`: docs/68 row added.
- **Build parked**: `game-copy/ts3native.asi.tailunlock`, sha256 `E5737E89E1C71806BCED83CACE3B885EA587066B73908CCAEA00B2D93E729D00` (0.36.23 + v89).
- **Queue** `tools/harness/queues/tailunlock.json`: `bp-tailunlock-verify` first (day, 5 min: `tuTrip` all 0, `tuReleasedUs` > 0, `tuWins` > 0, no crash, retake max < 20 ms), then the night pairs BP and IP off-first / on-first (10 min, 2-min slices, `--ab tailunlock`). Read: hold ms/s through `asyncUs` and the lent share `tuReleasedUs / asyncUs`, the sim's keyed `+0x918` wait total (`wait-ab.ts`, pair mean).
- **Expected** [inferred]: lent share ~5-10% of the hold (pass 16 ~40% of a post pass that is ~19% of a job, §2-§4); sim wait -2..-6 ms/s in BP, only the steering share of the waits that fall in a window (gates, writers and destroys still wait for the job).
- **F-G not admitted** (2026-10-22 integration merge, docs/155 §4/§8, ledger §242 I-155-13): gate-12 F-G (`FootprintsNearPoint 0x631F10`, key `0x631F40`) took the lent lock for its `LotTable_Contains` leaf, then ran the pinned quadtree query `0x667480` at `0x631F9A` without `+0x918` beside the worker's remaining live passes. `P5bGate` (p5b.cpp, shared by P5b and TailUnlock) now admits only F-B and F-C on gate 12; an F-G acquire during an open window waits for the retake as before and counts in `p5bFgHeldOut` (counters v95, +0x3FEC; also in `p5bHeldOut`). Selftest `TestP5b` (gate class table, a lent-window F-G try held out and counted, F-C still granted; the threaded F-G acts count with the writer families). Combined build `game-copy/ts3native.asi.0324`.

## 12. Would design M hit? `[CellMemoProbe]` (2026-10-22, offline build, NOT run)

The §8.4 step 1 probe: measure before building M. Built in `native/ts3native/cellmemo.cpp` (switch `[CellMemoProbe] Enabled=1`, default 0, observe only, needs `[RouteJobPhases] Enabled=1`; counters v88, ledger §235). Nothing is cached: every leg still runs.

- **Plan** = the worker's span from one Q reset `0x61E320` to the next, or to the job's end (M's lifetime, §8.3). Only the route worker inside a job counts (the stubs compare the TEB thread id with `[RouteJobPhases]`'s `g_rjpTid`).
- **Key** at the entry of `InRoomPlan 0x632E40` / `InRoomPlanToPoint 0x632630` (stack args per docs/98 §2; `ret 0x2C` / `ret 0x1C` checked): FNV-1a 64 over the wrapper, `X+0x50`, the 64-bit lot key, the start record `{x, z, level}` bits, then the goal record bits, the threshold bits, every 28-byte goal record (vector `{begin, end}` read at `0x6332EE`), the any-room and no-nudge flag bytes; or the target record bits and the no-nudge byte. A key already seen in the plan is a **repeat** (would-hit).
- **Time**: `[RouteJobPhases]`'s cell A\* frame (`0x61E500`, inclusive, allocator inside it included) hands its time to the probe; the whole time goes to `cmCellUs`, the part inside a repeat leg to `cmHitCellUs` (`0x61E500` has only the two wrappers as direct callers, docs/109, so the last wrapper entry on the thread owns it; the wrapper's retry search counts too).
- **Counters v88** (+0x38AC-0x38FB, appended after v89: v89 already sat at 0x3878 with no gap; the version field stays 89): `cmReady`, `cmSkipped`, `cmPlans`, `cmLegs[2]`, `cmRepeats[2]`, `cmCellUs`, `cmHitCellUs`, `cmHitCellN`, `cmMaxRepeats`, `cmHist[8]` (plans by repeats 0, 1, 2-3, ..., 64+), `cmFull` (legs past a plan's 256-key list).
- **Upper bound** [inferred]: the portal lock refresh `0x632460` rewriting a footprint mid-plan would drop M entries; the probe does not see it (it rewrites only on the first call in a room, §8.3). The wrapper's own work around the search (nudge, node emit) is not counted, so the bound is on cell A\* time only.
- **Tests**: selftest `TestCellMemoProbe` (layout; each key field changes the key, garbage above a flag byte does not; repeats count only within a plan; would-hit time only after a repeat; histogram, max, full list; the stub reads the args at a real `InRoomPlan` prologue, worker only). hook-sites `--check`: docs/68 row added (the 7 addresses it lists as missing are pre-existing: threadstack, monothread-m5, ts3native.cpp:1853-1854).
- **Build parked**: `game-copy/ts3native.asi.cellmemo`, sha256 `54C0C8DAA0C55B9B8E84A5B2C85FE22223D5EB9D42579F5FF73DC662FD82E99D` (0.36.23 + v88).
- **Queue** `tools/harness/queues/cellmemo-probe.json`: `bp-cellmemo-verify`, `ip-cellmemo-verify` (day, 5 min, noisy OK). Pass: `cmReady` 1, `cmPlans` > 0, legs > 0, `cmCellUs` > 0. Read: would-hit ms/s = `cmHitCellUs` / 1000 / 300.
- **Decision rule**: build M (`[CellAStarCache] Enabled=0 Shadow=1`, §8.4 step 2) only if would-hit >= ~5 ms/s of hold in BP or IP; below that drop M and look at M2 (neighbour lists) or the allocator share instead.
## 13. Repeated replans: probe and `[RouteAsync] ReplanDedupe` (2026-10-22, static + offline build, NOT run)

Chunk 4 (§4) and docs/151 D7 asked how often a follower replans the same request again within a short window, and whether a repeat of a **failed** plan can take the failure without planning. Ledger §237 (I-150-13-1 to I-150-13-8). Sources: capstone on the Steam 1.67 exe, the decompiled `ScriptCore.Route` and `SimRoutingComponent`, docs/92 §6-7 and docs/93 §2-5.

### 13.1 Where

Both ICs, whole: `IC_Route_Replan 0x765490` and `IC_Route_ReplanFromPosition 0x765550`. With `[RouteAsync] Kinds` hooking a kind, A10's `CallIc` runs the check (on the worker inside a job, or on the sim for a sync fallback); otherwise the section's own prologue detours do (release config: A10 off, sim thread). The managed wrappers (`Route.Replan`, `Route.ReplanFromPoint`) call `AboutToPlanCallback`, the IC, then `PostPlanCallback`: the callbacks still run either way.

### 13.2 Why a served failure is the plan's own answer

| # | step | tag |
|---|---|---|
| 1 | The IC's effect is: `RouteHandle_Lookup 0x74B2C0`, AddRef, `add [route+0x1F4],1` (`0x7654BA` / `0x765589`), the start builder (`0x7641D0` -> `0x75D920` -> `Route_GetFollowerPosition 0x752480`; or `0x7642C0` from the point), `Route_PlanCommon 0x7606E0` (resets `+0xE0..+0xF0`, `+0xA4/+0xA8`, `+0x140/+0x141`, `+0x48/+0x4C`, AGS `+0x3C`; goals; `Route_PlanLocked`; fills the path list), Release, the 0x48-byte result copy | verified (disassembly, docs/93 §4.1) |
| 2 | Serving writes the stored result bytes and does the `+0x1F4` increment itself; `+0x1F4` has no reader found (docs/93 §4.1) | verified writer; absence of readers verified in the ranges docs/93 names |
| 3 | The route is in the state the failed plan left: hash of the options O (lock keys, RouteOption/2, ignore and forbidden lists, preferred portal) and every route field PlanCommon writes, plus each path's status, nodes and blocker records, taken after the plan and compared before the repeat; any setter, follow start or other plan in between changes it (refused, `rdInval_0`) | verified field list; "no other route state matters" inferred (docs/92 §6 lists the IC's reads) |
| 4 | Same request: same follower (`route+0x40/+0x44`), same IC, same destination set (each destination's first 0x40 bytes; list walk of `0x751FF0`: list at `+0x18`, destination = node - 4) and its `+0x20..+0x40` words, start within 10 cm (ReplanFromPosition: its point; Replan: `0x752480`, the same call the start builder makes) | verified offsets; 10 cm start tolerance inferred (D7's 0.25 m rule, tightened) |
| 5 | Same world: `[RouteSnap]` change counters unchanged since the plan's end (`PrVersions`, as `[PlanRepeat]`); the per-object footprint family may change only when the route has neither `BlockedByPeople` (4) nor `BlockedByDynamicFootprints` (0x1000), the filters standing Sims' footprints are read through (docs/92 §7) | counters verified (docs/94 §10); "other footprints a failed plan reads do not change in 500 ms without a routing write" inferred, checked live by `rdWouldDiff_1` |
| 6 | Same routing world (`g_rgLock`), no pending lot updates and no dirty lot index (`RtQuietWorld`): the plan would apply them first (docs/93 E1) | verified |
| 7 | The planner is deterministic for equal inputs and world (no RNG in the closure; the obstacle replan's blocker order follows the search) | inferred (docs/93 §2.2); checked live by `rdWouldDiff_0` |
| 8 | Door answers: `AboutToPlanRouteCallback` clears `SimRoutingComponent.LockedDoorsDuringPlan`; the plan's door answers (`Sim.OnPortalPathPlan`) refill it; its only readers are in the result-18 branch of `DoRouteFailureBehavior`, so result 18 is never served | verified (decompile); mods reading the list elsewhere inferred absent |
| 9 | What a served call does not do: pending-update applies (refused when pending), lazy lot/room graph builds, A\* scratch, quadtree pin/flush (docs/93 E5-E13: skippable) | verified list (docs/93 §5) |
| 10 | Bounded staleness: only within 500 ms of the real plan's end (a served call does not move the window), only failures; a goal that opened inside the window fails once more, as docs/151 D7 accepts | design |

### 13.3 Built (`replandedupe.cpp`, counters v90, default 0)

- `[RouteAsync] ReplanDedupe` = 0 not installed, 1 installed and serving (`rdMode` +0x38FC starts 1), 2 installed, observe only (`rdMode` 0). `--ab replandedupe`. Turns on the `[RouteSnap]` change counters (its world version), as `[PlanRepeat]` does. Installed after `[ProbeP10]`; owns the prologues before `[UnseenThrottle]` diagnostics (their v83 split then stays 0); docs/68 row.
- **Observe** (both modes): `rdRepN[w][c]` / `rdRepUs[w][c]` = repeats by time since the previous plan's end (<= 250 / 500 / 1000 ms, cumulative) and class (0 failure with the same answer, 1 success with the same answer, 2 another answer), with their planned us; `rdCalls`, `rdUs`, `rdFails` per IC; `rdNoKey`.
- **Check of the argument** (rdMode 0): an eligible failed repeat is planned anyway and compared byte for byte (`+0x44` is stale stack): `rdWould[e]`, `rdWouldSame[e]`, `rdWouldDiff[e]`, e = 0 nothing changed, 1 footprint writes only. `rdWouldDiff_0` must stay 0.
- **Cut** (rdMode 1): `rdServed`, `rdServedUs` (the cached plan's us, the time saved per serve [inferred]); refusals `rdInval[4]` (route state, footprints with BlockedByPeople, routing writes, other: no version, world, not quiet, result 18, P4).
- **Tests**: selftest `TestReplanDedupe` (fake handle table, route, destination set, follower position, IC): bands and classes, the observe check, served bytes and `+0x1F4`, every refusal, no-key cases, eviction. `steerro-test` checks the two `add [esi+0x1F4],1`, the `0x75D93E -> 0x752480` call and the `0x751FF0` list walk on the image; seeds 1-3 x 3000 pass. `hook-sites --check`: no new address missing (7 older ones are listed missing on master too), `check-docs-code` ok.
- **Build parked**: `game-copy/ts3native.asi.dedupe`, sha256 `757CFA1E3174656C15FBA264B6B6C73411886578AA26799DBEDB30E7B8F9A97D` (0.36.23 + v90; built without v88).
- **Queue** `tools/harness/queues/replan-dedupe.json`: `bp-dedupe-verify`, `ip-dedupe-verify` (day, 5 min, `ReplanDedupe=2`, `--ab replandedupe` 1-min slices: repeat counters > 0, `rdWouldDiff_0` = 0 on off slices, `rdServed` > 0 on on slices, stuck Sims not above the off slices, no crash), then night pairs BP / IP on release defaults and BP with A10 `Kinds=5` (off-first / on-first, 10 min, 2-min slices).
- **Expected** [inferred]: unknown until the verify shares (the repeat rate was the open question of chunk 4). With A10 on, a served ReplanFromPosition job ends at once (19-31 ms of hold each, §1); at a 30% failed-repeat share that is chunk 4's -8..-13 ms/s hold. With A10 off, the cut is sim-thread sync plan time directly.
## 14. Chunk 2: where the route job's allocation time goes (2026-10-22, static + offline census + probe build, NOT run)

Question (§4 chunk 2): which callers inside `Route_PlanLocked` call `EA_AllocTagged 0x4E3F90` / `EA_Free 0x4E3F70`, how often per job and with what sizes, and is there a safe change at one or two dominant sites? Rule: measure before build. Ledger: §236 (I-150-14-1 to I-150-14-6).

### 14.1 Static facts [verified]

- **Call sites.** The direct-call closure of `Route_PlanLocked 0x82C6E0` to 10 hops (1,201 functions, `tools/mono-glue-closure.py` index) holds **550 allocator call sites in 261 functions**. Static counts say nothing about frequency, so §14.2 runs the planner.
- **The allocator ignores the tag.** `0x4E3F90` passes only argument 1 (size) and argument 3 to `0x4E69D0` on the one global allocator `[0x11CB864]`; the tag pointer, name and line are dropped. `0x4E3F70` passes the pointer to `0x4E5920` on the same object. Two blocks of the same size, both asked for with argument 3 = 0, are interchangeable.
- **`PathNodes_Insert 0x629140` already grows geometrically.** On a full vector (`0x62914C`) the new capacity is twice the old one, or 1 when empty (`0x6291B4`-`0x6291F2`); the alloc at `0x6291DE` asks for capacity x 64 bytes, the old buffer is freed at `0x629254`. So the §4 idea "grow the path-node vector geometrically" is already what the game does. What is left is the start capacity or a reserve, and that needs the final sizes and each vector's owner (68 callers in the closure).
- **Post pass 16 (`0x64C7C0`) builds a throw-away list.** The list head sits on pass 16's own stack (`[esp+0x24]`, linked to itself at `0x64C7E4`-`0x64C80F`). Its 32-byte nodes come from `0x64CAD4` and from `0x59B120` (alloc `0x59B134`, called at `0x64D1EC` and `0x64D4B3`). Nodes are freed at all three exits: loops at `0x64D4F4` and `0x64D552`, and the clear `0x6263F0` at `0x64D592`. `0x64C580` also erases nodes (free `0x64C7A1`, its only direct allocator call). The node count is capped at `0x800` (`0x64D4C1`). No node outlives the call [verified for direct calls; a callee of `0x64C580` that splices nodes elsewhere was not ruled out].

### 14.2 Offline census: the real planner on K8's worlds [measured offline]

`steerro-test` K8 runs `PathPlan_WithRetries 0x661B50` on built worlds with EA alloc and free redirected to `RfNew`/`RfFreeP` (docs/94 §13.3). With `STEERRO_ALLOCSITES=1`, those two count calls by the game's return address (new, off by default). Counts are calls, not time. The worlds are synthetic, terrain and world manager are fakes, and the 320 plans include the copy runs and the `[OptsInit]` re-runs.

| Site (call) | In | Seeds 1-2 (11 water legs): % of 1,086,824 allocs | Seeds 2-3 (no water leg): % of 321,663 allocs | Mean size | Freed at |
|---|---|---|---|---|---|
| `0x64CAD4` | pass 16 list `0x64C7C0` | 15.2% | **33.0%** | 32 B | `0x64D557` / `0x64D4F9` (42.8% of frees, seeds 2-3) |
| `0x59B134` | list push `0x59B120` (from pass 16) | 8.7% | **9.7%** | 32 B | same |
| `0x6291DE` | `PathNodes_Insert` grow | 8.7% | **18.9%, 80.5% of bytes** | 1,639 B | `0x629254` (16.0%) |
| `0x6DFE2D` | `0x6DFDD0` (water grid search `0x620560` from `WaterPath_Plan 0x671930`) | **45.4%** | 10.6% | 16 B | `0xAEBDD6` |
| `0x90B5FE` | `0x90B5D0` (from `0x620560` and others) | 6.7% | 1.5% | ~1 KB | `0x6209ED` |
| `0x61B630`, `0x862244`, `0x66AFF0` | cell-table and node helpers | 2.0-2.9% each | 3.1-5.6% each | 13-33 B | own |

Without water legs, **the pass 16 list and the `PathNodes_Insert` grow make up ~62% of allocator calls and ~84% of bytes**. With water legs, the water grid search's 16-byte nodes lead (IP/SV plans; 0.2% of BP planner samples, §2). On BP's sim-thread samples, `PathNodes_Insert` (315) and the post pass (125) own 71% of the 618 samples that have an allocator frame (§2). Every source agrees on the same two families. But time per call differs about 50-fold in size (1.6 KB vs 32 B), so their share of the worker's **time** is still unmeasured.

### 14.3 Probe built: per-site table in `[RouteJobPhases]` (counters v92)

- **Where.** `routephases.cpp` already times `0x4E3F90` and `0x4E3F70` on the worker inside a job. Each allocator frame now also writes:
  - a 64-row table keyed by (allocator return address, return address of the innermost open phase frame, i.e. which call into which phase function): calls, self time in 100 ns units, bytes;
  - a size histogram;
  - `PathNodes_Insert` grows by new capacity (with their time);
  - jobs bucketed by how many allocator calls they made.

  Nothing new is hooked, and the switch stays `[RouteJobPhases] Enabled=1`. `raFull` counts calls that found no free row.
- **Rounding fix (applies to v87 too).** Self time is now carried per phase in ticks. Before, each frame was truncated to whole microseconds, so an allocator call under 1 us counted 0 and v87 under-counted the allocator phase. The selftest sum check still holds.
- **Report.** `node tools/harness/route-phases-report.ts <native.json> --seconds 300` prints the phases, then the site table with function names from `game-copy/analysis/functions.csv` (top 20 rows by time, calls per job, mean bytes, top-2 share) and the histograms. `--self-check` covers both parts.
- **Tests.**
  - selftest `TestRouteJobPhases`: v92 layout, rows split by site, bytes, size and per-job buckets, no recording outside a job.
  - `steerro-test` seeds 1-3 x 3000: ok. The K8 census above is an opt-in run of the same test.
  - `hook-sites --check`: no new sites; the docs/68 row notes the compared constant `0x6291E3`. The `OptsInit` FAIL line was already there before this change.
  - `check-docs-code`: ok.
- **Build parked.** `game-copy/ts3native.asi.routealloc`, sha256 `26D7EE3D97B115D12337E16D511103CDC1AB7EFC37A33C9EE58DC64FAD31B200` (0.36.23 + v92; v88/v90/v91 not in this branch).
- **Queue.** `tools/harness/queues/routealloc.json`: `bp-routealloc-probe` and `ip-routealloc-probe`, day jobs of 5 min; a noisy machine is fine. Pass: `rjpReady` 1, `raAllocN`/`raFreeN` > 0, `raFull` 0, phases within 10% of `asyncUs`.

### 14.4 Step 2 (`[RouteAlloc]`): not built yet, and why

1. **Measure before build.** The census counts calls on synthetic worlds, and the sampler counts sim-thread plans. Neither gives the worker's allocator **time** per site. The probe answers that in one day job.
2. **No safe "reserve" without sizes and owners.** `PathNodes_Insert` already doubles. A larger start capacity changes every one of its 68 callers, and some of them keep their vectors (the route path handed to the sim, lot graph data), so slack would stay alive with them. The `raInsCap` histogram gives the final sizes. The owners would still need checking.
3. **A job-local bump arena is unsafe.** Blocks reach `EA_Free 0x4E3F70` from frees we do not own: the list clear `0x6263F0`, the erase in `0x64C580`, vector destructors in the sim's route. A non-EA block there is replay.cpp's crash H. Catching those frees means redirecting `EA_Free`, which touches the allocator itself (docs/148: Apex FastMemory owns it).

**Design ready (build only if `bp-routealloc-probe` shows the two families at >= 50% of allocator time and that time is >= ~8 ms/s of the hold):** a per-thread **recycle cache of genuine EA blocks**, used only at retargeted call sites (rel32 `PatchCalls`, all or none).

- **Sites.**
  - Allocs `0x64CAD4` and `0x59B134` (32 B, argument 3 = 0) and `0x6291DE` (capacity x 64) are served from the cache when a block of exactly that size is held, else by EA.
  - Frees `0x64D4F9` and `0x64D557` (pass 16 list nodes, 32 B by §14.1) put the block in the cache.
  - Free `0x629254` puts the block in the cache with size `[ebx+8]-[ebx]` (the old capacity is still in the vector at that instruction, `0x629266` overwrites it later).
- **Safe by construction.** Every cached block was allocated by EA and is valid for EA's free. A free site we did not retarget just frees to EA, and an alloc site we did not retarget just allocates from EA, so the coverage does not have to be complete.
- **Guards.**
  - Act only on the route worker inside a job.
  - Pass through in clone runs (RpAlloc blocks in vectors, replay.cpp).
  - Exact size classes only.
  - Bounded: ~64 KB per thread.
  - Flush to EA when the switch is turned off (`--ab routealloc`).
- **Tests.**
  - selftest: hits, misses, bound, flush.
  - steerro K8 with the cache on: "no free of a non-EA block" and plans equal.
- **Expected** [inferred]: the two families are ~71% of ~32 ms/s allocator time (§2). At a 50-80% hit rate that is **-8..-15 ms/s of hold, about -5..-10 ms/s of sim wait**.
- **Cost:** about 2-3 h build plus tests, 1 day job, 2 clean nights (BP and IP pairs). The night pairs go into `routealloc.json` when step 2 is built.
