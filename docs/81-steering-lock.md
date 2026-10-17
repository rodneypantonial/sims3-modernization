# 81. The steering lock (`0x82EDB0`, `0x82F0B0`) and the H6 window

Written 2026-10-17. Static only: Ghidra headless on the docs/80 project copy (`game-copy/ghidra-lt`, read-only runs), plus a linear capstone sweep of the Steam 1.67 `TS3W.exe` for call sites and field writes. The game was not run. Builds on docs/18 ("H6 plan", "(2)", "Top risks"), docs/60 §1-§2 and docs/80. Input numbers (PLAN, 2026-10-17): live native G11 in BP says the event that ends the free window after a sim-thread plan is an outermost `+0x918` acquire in 83% of cases (gate sites 9%, quadtree writes 8%); movable = 1.8% of plan time; plan time ~55 (BP) to ~66 (IP) ms/s. The async A/B moved 42 ms/s of route time off the sim thread, but the sim then waited ~55 ms/s at locks (iterations/s -8.5%).

Tags as in AGENTS.md: **[verified]** = read in the disassembly or decompile (address given); **[inferred]** = reasoning still to confirm by a probe (§7). Scratch scripts (session scratchpad, not committed): the docs/80 `Dis`, `DecompileTo`, `CallerTree` and `LtDeps` (2-hop forward scan) scripts, and a capstone index of every call site and of plain writes to `+0x48`/`+0x4C` in `0x660000-0x66C000`. Limits: direct calls only; vtable targets named only where a data reference was found.

## 1. Summary

- **Per-tick acquirers.** Only the steering family takes `+0x918` every tick per walking Sim: `RoutingWorld_ObstacleQuery 0x82EDB0` (three steering callers) and `GoalFootprintGather 0x82F0B0` (one steering caller). `UpdateSimFootprint 0x833560` looks per-tick but is not: `Locomotion_Notify 0x947590` returns early unless the Sim's moving bit changes [verified]. Every other `+0x918` site is per script call, per event, per plan or per build/load (§2).
- **The query writes shared state.** Besides reads, `0x82EDB0`/`0x82F0B0` change (1) the per-level lot quadtree's active-query counter `Q+0x48` with plain `add`, (2) on an outermost query, apply the quadtree's deferred inserts and removes (`0x666A40`: unlinks and frees nodes, clears `Q+0x4C`), (3) footprint refcounts at `[fp+4]` with plain `+1`/`-1` (and delete through the vtable at zero). The async job makes the same three kinds of writes: the planner pins and unpins the lot's quadtrees (`0x631BF0` / `0x63C5B0` around `LotPlan_Inner 0x63F6E0`), inserts and removes quadtree entries (`0x632460` portal-lock refresh, pending updates `0x82C420`, lazy lot builds) and calls `0x82EDB0` itself (`PathPlanner_PlanHierarchical 0x65E0D0 -> 0x64E380 -> 0x64B430 -> 0x6435B0 -> 0x655880`) [verified call sites]. So running the query concurrently with a job, even "read-only", races on all three [verified writes; race inferred].
- **Skipped answer.** The callers rebuild their answer every tick and keep no obstacle cache of their own (all footprint sets are stack vectors, "Locomotion/FootprintSet", freed before return) [verified]. A skipped query reads as "nothing in the way" for that tick: no "goal obstructed" notice, no new blocking ids for the agent, "not inside a type 0xC/0xD/0x17 footprint". The same check runs again next tick, so a skip delays detection by one tick per skip [inferred from the per-tick call chain]. No caller replans or stops on a missing answer; the Sim keeps walking its current path.
- **Recommendation.** Design (a) at the **caller** level (not inside `0x82EDB0`): while the job owns `+0x918`, skip the three steering checks and return "nothing new" (or the previous tick's object ids for the same inputs), with a cap of 2 consecutive skips per agent, then wait as today. Verdict **feasible**, ~1 day plus probes. Expected gain is small: **~2-7 ms/s** moved under a Poisson model (§6), below the docs/18 go thresholds (15 BP / 30 IP). Run the §7 counters first; build (a) only if steering is most of the 83%.
- (b) snapshot: feasible with extra moves, large (own quadtree and polygon copy). (c) queue the query to the worker: feasible with extra moves, same latency as (a) for more work. (d) reader lock: **not feasible** (plain refcounts and the plain query counter, written by both sides).

## 2. `+0x918` acquirers by frequency (Q1)

docs/60 method (`Lock_Acquire 0x4E16F0` with `0x918` in the preceding instructions): 30 functions [verified, docs/60 §1.2]. Caller frequency classes from this pass:

| Rank | Acquirer | Sim-thread entry and frequency | Tag |
|---|---|---|---|
| 1 | `RoutingWorld_ObstacleQuery 0x82EDB0` (`lea ecx,[esi+0x918]` at `0x82EDC1`, call `0x82EDD0`) | per tick x walking Sim: (i) `Locomotion_Update 0x83CED0 -> Locomotion_UpdateAgent 0x957210` (agent state 1 or 4) `-> Locomotion_ObstacleList 0x85FFE0 -> Locomotion_ObstacleCheck 0x85FD10` (sites `0x85FE0B`, `0x85FE7C`, only when a path node is flagged); (ii) vtable `0x1026E10` `ObstacleAvoidance 0x847390 -> RouteFollow_A 0x846B70` (site `0x846D1C`, only when the remaining distance is under a threshold); (iii) `ObstacleAvoidance_B 0x85D980 -> 0x85C590 -> 0x85B670 -> RouteFollow_B 0x85B470` (site `0x85B507`, per candidate point, several per tick). Also per script call (`IsPointObstructedBySim`, `IsOkToIdleAtCurrentLocation`, `GetObstructionAtWorldOrLotPoint`, `PlaceSocialFootprint`, `PlaceGroupSocial` via `0x8243E0`), per goal step (`0x75CC36`, `0x75A647`), and **inside the job** (`0x655AF6`) | verified sites; per-tick classes inferred from the call chain |
| 2 | `RoutingWorld_GoalFootprintGather 0x82F0B0` (call `0x82F0D0`) | per tick x walking Sim x flagged path node: `0x85FD10 -> RouteFollow_EditPath 0x85F5A0` (site `0x85F6C6`); per goal step (`0x75CFE0`, `0x75AA11`) | verified sites; frequency inferred |
| 3 | `RoutingWorld_UpdateSimFootprint 0x833560` | per moving-state change of a Sim (`Locomotion_Notify 0x947590` compares the new bit with `[this+0x40] & 1` and returns if equal), footprint enable/reset ICs, tree components | verified |
| 4 | `RoutingWorld_UpdateObjectFootprintCore 0x8321F0` and the other footprint writers | per object move or footprint change (object system vtable `0x946160`), discourage/private footprint ICs | verified callers (docs/60) |
| 5 | `RoutingWorld_RouteToObjectCheck 0x831530` | per script call (`IsRouteToObjectUnobstructed`, `IsSlotUnobstructed`) and per routing-system message (`0x83584C` in the message switch of `0x8357xx` calls `0x818890`) | verified sites |
| - | the rest (`PlanLocked` = the job, `PortalCheckLocked` build tool, lot/save/load/init/teardown, `RoutingDebug_FrameUpdate` with debug flags) | per plan, rare, or main thread | docs/60 |

No other per-tick `+0x918` acquirer was found. Locomotion runs once per sim iteration: `MonoScriptHost_Simulate` calls `Locomotion_Update` at `0xD81C5C`, after the script-task call sites (`0xD81A41..0xD81BDF`) and before the later stages [verified call order; stage identity from docs/04 F17]. A job started by `Route.Plan` in the task stage therefore meets the steering acquires within the same iteration [inferred]. `RoutingWorld_BlockerCheck2d 0x82E340` (docs/80 C1) queries the same quadtree through `0x631E20` **without** `+0x918`; it is covered today only because its earlier `0x62EF30 -> 0x62EAB0` call is a gate site [verified order in docs/80].

## 3. What the steering queries read and write (Q2)

Both functions have the same shape [verified, `0x82EDB0`/`0x82F0B0` decompile]: acquire `+0x918`; release every entry of the caller's output vector and empty it (`0xAAC090`); world-to-lot lookup (`World_LotLookup 0xC68990`: world manager lot grid, no lock); build a stack query context (stack hash set named "Routing/PortalCheckCache" plus a footprint set "Routing/GetFootprintsWithinNode/FootprintSet", both freed before return, so this "cache" is per call); gather; copy the gathered footprints into the caller's vector sorted by pointer (`0x6D8BD0` binary search, `0x82D030`/`0xAAE690` insert), each with a plain `+1` on `[fp+4]`; release `+0x918`. `0x82EDB0` gathers around one point (`0x631F10`; a caller flag picks one of two hit visitors, `0x662DD0` or `0x662DB0`); `0x82F0B0` gathers along a segment (`0x631E20`), swapping start and goal if only the start is on a lot.

The gather (`0x631F10` / `0x631E20`, `ecx` = lot planner P `[world+0x95C]`) [verified]: `LotTable_Contains 0x628A90` (lot key `lot+0x48/+0x4C`), `0x62FB10` (find-or-insert gate `0x62EAB0`, then the per-level object at `entry+0x74[level - entry+0x40]`; docs/80 called it the cell graph, it is the per-level routing quadtree, see below), `0x62ED40` (world-to-lot transform, `entry+0xA0..`), then the quadtree query `0x667480` (point) or `0x667400` (segment) with `ecx` = that quadtree.

| State | Read | Written under the lock by the query | Also written by the async job | Race if the query ran during a job | Tag |
|---|---|---|---|---|---|
| Quadtree active-query counter `Q+0x48` | yes | `add [Q+0x48],1` before the walk, `-1` after (`0x66748F`, `0x6674D0`; `0x667400` alike) | planner pins all levels of the lot for the whole lot plan: `LotPlan_Inner 0x63F6E0` calls `0x631BF0 -> 0x62FB90 -> 0x666F70` (`+1`, `0x666F7E`) at `0x63FB79`, and `0x63C5B0 -> 0x662CF0` (`-1`) at 7 sites; its own queries `0x6672E0` etc. | lost update on a plain `add`: the counter sticks above 0 (deferred changes never applied, stale footprints forever) or reaches 0 early (flush during the planner's walk: use after free) | verified writes; race inferred |
| Deferred changes `Q+0x4C` flag, pending maps at `[Q+0x20]+0x30/+0x3C` | yes | outermost query (counter < 1) calls `0x666A40`: applies every deferred insert (`0x665C50`) and remove (`0x666750`), unlinks map nodes, clears `Q+0x4C` (`0x666B50`) | `LotQuadTree_Insert 0x666C50` / `Remove 0x666E30` set `Q+0x4C` (`0x666C89`, `0x666E64`) and queue while the counter is > 0, else flush and apply directly. Reached in the job from `0x632460` (portal lock refresh: remove, re-flag, insert; from `InRoomPlan 0x632E40` and `LotPlan_Inner` via `0x632630`), pending updates `0x82C420`, lazy builds (`LotGraph_Build 0x63CB90`) | node vectors and map buckets freed or relinked under the other thread's walk | verified |
| Quadtree nodes, footprint lists | yes (walk `0x663E20`/`0x663B20`, visitor `0x662DD0`/`0x662DB0`) | no (beyond the flush) | yes (above) | torn reads, freed nodes | verified callers; visitor body not read |
| Footprint refcount `[fp+4]` | yes | plain `+1` per copied footprint; plain `-1` on the previous contents (`0xAAC090`) and on the temporaries; delete through vtable slot 0 at zero | every job gather (`0x655880 -> 0x82EDB0`, `0x643E60 -> 0x631E20`) and the quadtree insert/remove paths do the same plain `+1`/`-1` | lost update: footprint freed while still listed, or leaked | verified sites |
| Lot table `P+0x48` | yes (hit path, `0x62FB10`, `0x62ED40`) | no on a hit (docs/80 §2) | insert and rehash on a miss | docs/80 §6 risk 2 | verified (docs/80) |
| Lot transform `entry+0xA0..+0x11F`, level vector `entry+0x74/+0x78` | yes | no | lot build (`0x63CB90`) | low (written once per lot) | inferred |
| World manager lot grid (`0xC688A0`) | yes | no | no | none new (already unlocked, docs/60 1.3) | verified |
| Caller's output vector | written | cleared and refilled | no | none (caller-owned) | verified |

No managed call, remote call or `World_FireEvent` is reached within 2 hops of either function [verified, `LtDeps`; vtable calls in the footprint objects not followed]. Allocator use: `EA_AllocTagged`/`EA_Free` (thread-safe, docs/18).

## 4. What the callers do with the answer (Q3)

| Caller | Question asked | Use of the answer | Skipped this tick | Stale (last tick's) | Tag |
|---|---|---|---|---|---|
| `RouteFollow_A 0x846B70` (from `ObstacleAvoidance 0x847390`) | "which object's footprint contains the route's goal point?" Filters out footprint type `0x14`, ignored ids (`0x85AAA0`, the "Locomotion/IgnoreList" built by the caller) and ids rejected by `0x81C970`/`0x81C930` (role inferred), then point-in-polygon through footprint vtable `+0xB8` in lot coordinates (`0x62EF30`); returns the 64-bit object id; if none, scans the locomotion agent list for another walking Sim (state 1 or 4) covering the goal (`Object_GetComponent`, `SimService_Get`) | non-zero id: `ObstacleAvoidance` calls the agent's vtable `+0x64` (role not read: a "goal obstructed" notice) | the footprint part answers "no object", the Sim scan still runs; the notice is not sent this tick and is re-checked next tick while the Sim stays within the distance threshold | the id is valid until the object moves; repeating a non-zero id would repeat the notice | verified flow; role of `+0x64` inferred |
| `Locomotion_ObstacleCheck 0x85FD10` + `RouteFollow_EditPath 0x85F5A0` | footprints near the first flagged path node (`node+0x30`, `0x82EDB0`), then per flagged node and along the segment (`0x82F0B0`); skips types `0x14, 9, 0x11, 0xC, 0xD, 0x17`, ignored ids and ids already listed; polygon tests through vtable `+0xC0`, `+0xE8`, `+0xDC`, `+0xB8` | appends blocking object ids (`0x572400`) to the per-tick list that `Locomotion_UpdateAgent` hands to the agent's vtable `+0x60` when non-empty | no blocking ids this tick; the list is rebuilt next tick (the vector is emptied at `0x85FFE0` each call) | ids for the same node stay correct unless an object moved; the sim thread cannot move footprints during a job (every footprint writer takes `+0x918`, §2 rank 3-4), so only the job's own writes (portal re-flag, pending updates) differ | verified flow; who clears `node+0x30` not found |
| `RouteFollow_B 0x85B470` (from `ObstacleAvoidance_B` candidate loop) | "is this point inside a type `0xC`, `0xD` or `0x17` footprint?" | boolean that rejects a steering candidate | answers "no": a candidate inside such a footprint could be accepted for one tick | correct only for the same point; candidate points change every tick | verified; type meanings unknown |

No caller keeps a cached obstacle set, retry flag or "no result" state: every result vector is on the stack and released before return [verified, all three]. `0x82EDB0` returns 0 only when the point is on no lot; callers treat that as "nothing found" [verified]. So "skip" already has a native meaning (no lot = no obstacles). The worst case of a skip is one tick of walking with a late obstruction notice or a late blocking id; the path itself was planned around the footprints that existed at plan time, so only footprints added after the plan matter (objects placed, Sims stopping, social footprints) [inferred]. A Sim at walking speed covers a few cm per tick [inferred]. Long skip runs are the real risk: with plans back to back, a job may hold `+0x918` across many consecutive iterations, so skips must be capped (§5a).

## 5. Designs and 2-hop dependencies (Q4)

Hop 1 = what the design itself reads, writes, locks or calls; hop 2 = the deps of anything hop 1 forces to move or to run concurrently. Stop at 2.

**(a) Try-lock with skip or reuse (per-agent cache), at the caller level**

| Hop 1 | Detours on `0x85FD10` (whole obstacle check), `0x846B70` and `0x85B470`, sim thread only. Test: "a job owns `+0x918` and this thread is not borrowing it" (routegate owner tracking, no acquire). If true: return "nothing new" (`0x85FD10`: return without appending; `0x846B70`: return 0; `0x85B470`: return 0), or the cached ids from the previous tick when the inputs match (agent pointer plus the query points, bit-exact). Else call the original and store its ids. Cap: after 2 consecutive skips for one agent, call the original (it waits as today). No EA state touched while skipping; the cache holds object ids only, never footprint pointers (refcounts) |
|---|---|
| Hop 2 | Original callees when not skipping: unchanged (`+0x918`, gate `0x62EAB0`, quadtree, refcounts, `Object_GetComponent`, `SimService_Get`, `0x5ECB30` writes global `0x1157D28` (docs/80)). Cache lifetime: agent destroy (key by agent pointer plus a generation, or clear on world teardown) |
| Verdict | **Feasible.** Hooking `0x82EDB0` itself is not: skipping there leaves the caller with an empty vector it cannot tell from "no obstacles", and the goal step and job also call it |
| Open beyond hop 2 | role of agent vtable `+0x60`/`+0x64`; who sets/clears `node+0x30`; whether a 2-tick delay changes stuck-Sim rates (log-only rule) |

**(b) Per-tick read-only snapshot of footprints/portals**

| Hop 1 | Before START (sim thread, lock free): copy every footprint in the walking Sims' lots (polygons via vtable `+0xDC`, type `+8`, object id `+4`, flags `+0xC0`) and the lot transforms into our own structure; re-implement point and segment queries and the polygon tests on it during the job |
|---|---|
| Hop 2 | The copy walks the quadtree (`0x663E20`, counter `Q+0x48`, flush) and the footprint vtables; invalidation is simple because footprint writers wait for the job, but the job's own writes (`0x632460` portal re-flag, `0x82C420`) are not in the copy |
| Verdict | **Feasible with extra moves** (copy cost per job start, re-implemented queries, polygon formats per footprint type unknown). Several days |
| Open | footprint polygon layout per type; copy cost in busy lots |

**(c) Queue the steering query to the routing worker (co-location)**

| Hop 1 | The worker, between or after jobs, runs `0x82EDB0`/`0x82F0B0` plus the caller's filter and polygon tests, holding `+0x918` (recursive), and posts object ids; the sim uses them next tick |
|---|---|
| Hop 2 | Filters use `0x81C970`/`0x81C930` (object id tests: object id hash, sim-only, docs/45), `Object_GetComponent` and `SimService_Get` in the `0x846B70` Sim scan (sim-only), physics/agent list reads: those must stay on the sim thread (split the caller in two). Footprint refs must not cross threads (plain refcounts), so the worker returns ids only. The worker is single and busy with the plan: answers arrive after the job ends, the same latency as (a) |
| Verdict | **Feasible with extra moves**, no gain over (a): the query itself is short; the cost was the wait, which (a) already removes |
| Open | queue depth and ordering with plans; vtable footprint tests on the worker (thread-safe reads inferred) |

**(d) Split the lock / reader lock during the job's read phases**

| Hop 1 | Job holds `+0x918` only around `Route_PlanLocked`; goal step and start points under a reader lock; `0x82EDB0` takes the reader side |
|---|---|
| Hop 2 | Not possible: the reader writes `Q+0x48` (plain add), may flush `Q+0x4C` changes, and does plain refcounts; the planner pins quadtrees and calls `0x82EDB0` itself inside `PlanLocked` (`0x655880`), so the "read phase" is not separable. Making it safe needs atomic `Q+0x48` at ~12 sites, no flush while another thread is inside, and atomic footprint refcounts at an unbounded number of plain sites (the option B blocker in docs/18) |
| Verdict | **Not feasible** |

Other idea (not asked, [inferred]): align job start with the iteration so that the job runs while the locomotion pass is not due, e.g. hold START until `Locomotion_Update` returns. It moves the job's overlap to the later stages and the main thread; worth a G11 split by stage before any build.

## 6. Expected gain (Q5)

Model [inferred]: window closers arrive as a Poisson stream at rate r during a plan of length L; movable fraction f = (1 - e^(-x)) / x with x = r L. Today f = 1.8% gives x ~ 55.6. If a share s of closers stops closing, x' = x (1 - s).

| Case | s | x' | f' | Movable, BP 55 ms/s | IP 66 ms/s |
|---|---|---|---|---|---|
| Today | 0 | 55.6 | 1.8% | 1.0 | 1.2 |
| Steering = all outermost `+0x918` closers (upper bound) | 0.83 | 9.5 | 10.6% | 5.8 | 7.0 |
| Steering = half of them | 0.415 | 32.5 | 3.1% | 1.7 | 2.0 |

The lock-wait cost in the A/B (~55 ms/s) falls by roughly the same share of the job time. Even the upper bound stays below the docs/18 go rule (>= 15 ms/s BP, >= 30 ms/s IP). The model ignores burstiness: locomotion runs as one block per iteration right after the task stage (§2), so the real gain could be larger if no other closer follows in that iteration, or smaller if the gate sites (9%) and quadtree writes (8%) cluster after it. Only the §7 histogram can tell.

## 7. Runtime probes (ts3native `[Verify]` style, observe only)

| Counter | What | Confirms |
|---|---|---|
| `vfG11CloseBy` (docs/80) | closing event per window: `+0x918` acquire with two return addresses | share of the 83% that is steering (`0x85FE10`, `0x85FE81`, `0x846D21`, `0x85B50C`, `0x85F6CB` return sites) vs footprint writers, script ICs |
| `vfLkSite[n]` | per `+0x918` acquire site on the sim thread: outermost count/s, hold time, wait time during a job | §2 ranking; total steering wait in ms/s |
| `vfStSkipDry` | dry run of (a): at the three detours, count "would skip" during a job, then call the original and compare its ids with the cached ids for the same inputs; count mismatches and consecutive-skip runs per agent | stale-answer divergence (must be rare) and the skip cap |
| `vfQtCount` | at job end, check `Q+0x48` of the lots the job touched equals its value at job start (0 in steady state) | no lost updates on the query counter |
| `vfStNodeFlag` | per agent: ticks a flagged node stays flagged | whether a skip is retried next tick |

## 8. Risks and open items

1. A skip hides a new obstruction for up to the cap (2 ticks) [inferred low]; a stale id after the object moved repeats a wrong notice: prefer "skip" over "reuse" for `0x846B70`, reuse only for `0x85FD10` ids with bit-exact inputs.
2. Long jobs: with the cap, a job longer than 2 iterations still makes steering wait; the gain then comes only from the first two.
3. Any design that lets the sim thread run `0x82EDB0`/`0x82F0B0` (or `0x631E20`) during a job without the lock corrupts `Q+0x48`, the deferred-change maps or footprint refcounts (§3). `BlockerCheck2d 0x82E340` must keep its gate-first order.
4. Vtable calls (footprint polygon tests, agent `+0x60`/`+0x64`) were not followed: managed calls inside them are not excluded.
5. The gain estimate rests on a Poisson model and on the BP measurement only.
