# Route planner: structure, budgets, lock, and where to make failures cheap

Written 2026-10-12 against the Steam TS3W.exe (base `0x400000`, see 04). Static analysis only: Ghidra headless on the read-only TS3 project, plus the decompiled C#. No measurement was done for this note.

Tags: **[verified]** = read in the decompiled code or disassembly (address given). **[inferred]** = reasoned from code, still to be confirmed by a run.

Scratch outputs (git-ignored): `game-copy/analysis/decomp/route-chain.c` (older), `route-astar.c`, `route-astar2.c`, `route-result.c`, `astar-676260.asm`, `xrefs-route.txt`, `xrefs-lockusers.txt`, `xrefs-job.txt`. Two ad-hoc Ghidra scripts (`Xrefs.java`, `Disasm.java`) were run from the session scratchpad, not added to `tools/ghidra/`.

> **Name fix.** In the Ghidra project, `0x81b2a0` is labeled `Route_SearchAStar` and `0x661b50` is labeled `RoutingJob_Init`. Both labels are wrong. `0x81b2a0` is the RoutingJob **Execute** (vtable slot 1 at `0x102125c`). `0x661b50` is **PlanPath with retries**. The A* searches sit two and three levels below them (table below).

---

## Call tree (corrected)

```
IC_Route_Plan 0x765160 / Replan 0x765490 / PlanFromPoint 0x765240 / ReplanFromPosition 0x765550
 -> Route_StartPointsFromPosition 0x7641d0 | Route_StartPointsBuild 0x7642c0
 -> Route_PlanCommon 0x7606e0          builds goal pts, result struct (18 dwords), default mType = 2 (Failed)
    -> Route_PlanLocked 0x82c6e0       early-outs, Lock_Acquire(0x1021cf0), 0x82c420 (pending routing-world updates)
       -> RoutingJob ctor 0x819d50     a stack object (vtable 0x1021258); options copied by 0x81d440
       -> RoutingJob::Execute 0x81b2a0 option bits -> internal flags; status written to RoutingPathImp+0x94
          -> PlanPath 0x661b50         wrapper that RETRIES on failure (see Q1)
             -> Planner 0x65e0d0       hierarchical: world, lot and water legs
                -> world A* 0x676260   ("Routing/PathContainer"), binary heap, closed bitset, node pool
                -> lot plan 0x6410b0 -> 0x63f6e0 -> portal A* 0x63e340 + in-room 0x632e40 ("Routing/portalPlan")
                -> water 0x671930 / 0x656a70 ("Routing/waterPath")
       -> RoutingJob dtor 0x819ae0 ; result = path status (vtable +0x20) <= 2 ; Lock_Release 0x4e17b0
    -> on failure: 0x752620 maps path status -> RoutePlanResult.mType (+ blocker id/location)
```
Also: `0xc7bf70` (World_FindPlaceOnRoadOffScreen path) calls `0x82c6e0` directly, twice [verified xrefs].

---

## Q1. Planner structure, limits, and where the result is decided

**No node cap, iteration cap, time limit or cost budget exists anywhere in the chain.**
- **World A\* `0x676260`** [verified, disasm `astar-676260.asm`]:
  - The open list is an EASTL vector heap (push `0x6761f0`, pop/sift `0x673b60`).
  - The closed set is a bitset that is `memset` at the start of each search.
  - Nodes come from a free-list pool that grows in blocks of 1,000 nodes (`0x675e90` allocates `0x4e28` bytes, 200×5) with no limit.
  - The edge cost function is picked by travel mode (`param_6` 1..4 → `0x674e40`, `0x673850`, `0x674250`, `0x675800`).
  - An edge counts as blocked when its cost is ≥ `1e8` (`DAT_00fef678`). That is a "wall" constant, not a budget.
  - Loop: head at `0x676495`, back-edge at `0x676be9 MOV EAX,[ESP+0x3c]; 0x676bed CMP [ESP+0x38],EAX; 0x676bf1 JNZ 0x676495`.
  - The loop exits only when the goal is popped (`0x676c14`) or the open list is empty (`0x676bf7`, returns 0).
  - It remembers the node with the best heuristic (`*param_8`), which later gives the "blocker"/partial target.
  - **A failed search therefore expands every reachable node.**
- **Lot portal A\* `0x63e340`**: same pattern, a `do { … } while(true)` that returns 1 at the goal and 0 when the list is empty, with no counter [verified in decompile; not disassembled].
- The cost-parameter swap at `0x676260` (`[container+0x24]`, chooses parameter set `+0x28` or `+0x4c` by the initial heuristic) is a weighting choice, not a cap [verified].

**Failure amplifiers.** These turn one failed request into several full searches, all under the lock:

| Where | Trigger | Extra work | Tag |
|---|---|---|---|
| `0x661b50` | Planner returns 6 | Re-runs the planner with internal flag `0x800000` | verified |
| `0x661b50` | Start cell not routable | Scans up to 9 rings around the start and re-plans from the first routable cell (one extra plan) | verified |
| `0x661b50` | Return 5 with >2 goals | One planner run **per distinct goal lot** | verified |
| `0x661b50` | Internal flag `0x400` (RouteAsGhost) | Straight-line fallback, up to 3 sub-plans | verified |
| `0x6410b0` | Lot plan fails | One retry per goal room | verified |
| `0x81b2a0` `0x81c67a` | Codes 4/5/8 and **ReplanToFindObstacleWhenPathPlanFails** | A second full PlanPath with relaxed flags `0x1f` to collect blockers | verified |
| C# `SimRoutingComponent.DoRouteFailureBehavior` | Failure, `DoRouteFail`, Sim not set to NeverPlayRouteFail | `GetRouteFailPath` (up to 2 plans), called up to twice, then `PlanToLot` (1–2 plans) | verified call sites, count inferred |

`SimRoutingComponent.SetRouteOptions` (SimRoutingComponent.cs:2797) sets `ReplanToFindObstacleWhenPathPlanFails = true` for **every Sim route** [verified]. So every failed Sim route runs at least two native searches, and often 4–6 more plans from C#. That matches F7b: four failed plans took 1 s.

**Where the result is decided** [verified]:
- `0x81b2a0` writes the status to `RoutingPathImp+0x94` with atomic stores:
  - 1 = running, 2 = succeeded, 3 = failed.
  - The inner code is then remapped: 2→5, 3→6, 4→7, 5/8→8.
  - 5 = no start, 6 = no goal (set directly before the search).
- `0x82c6e0` returns `status <= 2`.
- `0x752620` maps the status to the C# `RoutePlanResultType`:
  - 0–2 → Succeeded.
  - 3 → Failed.
  - **4 → FailedBecausePlannerAborted.**
  - 5 → NoStartSpecified.
  - 6 → NoDestinationSpecified.
  - 7/8 → FailedBecauseNoPathToDestination, plus blocker id and location from path vtable `+0x160/+0x164`.
- **Nothing in the synchronous path writes status 4.** The "planner aborted" value is a leftover from a job/abort design. In C#, `FailedBecausePlannerAborted` is only set by `ScriptCore.Route.IsDestObjectInvalid`.

**Existing setting we could lower?** None [verified for `0x676260` and `0x81b2a0`; the RouteOption/RouteOption2 enums have no budget field].

## Q2. What the C# RouteOptions do natively

`0x81c880(lo, hi, which)` tests the RoutingOptions words: RouteOption at `opts+0x28/+0x2c` (lo/hi), RouteOption2 at `+0x30/+0x34` [verified]. Its `this` is RoutingJob+0x40. Bit translation in `0x81b2a0`:

| RouteOption (C#) | Native effect | Search size |
|---|---|---|
| `EnablePlanningAsCar` 0x10 | Travel mode 1 → 3 (pedestrian + car) | **Bigger**: the road graph joins the search [mode verified, size inferred] |
| `DisablePlanningAsPedestrian` 0x20 | Clears the pedestrian bit (car only) | Smaller [inferred] |
| `IgnoreSidewalkAndLotRestrictions` (hi 0x8000) | Mode 4 | Unknown |
| `EnableWaterPlanning` (hi 0x10000000) | Internal `0x1000000` + water cost table (`0x10212d8…`; mermaid/lifeguard variants) | **Bigger**: with mask `0x9008000` set, `0x65e0d0` adds `Routing/waterPath` legs (`0x671930`, `0x656a70`) [flag verified, size inferred] |
| `EnablePondPlanning` (hi 0x20000000), RouteOption2 `EnablePlanningAsBoat` 2 | Internal `0x8000000`, `0x8000` (same water mask) | **Bigger** (water legs) [inferred] |
| `ReplanToFindObstacleWhenPathPlanFails` 0x100 | Second full plan on failure (Q1) | **Doubles the cost of failures** [verified] |
| `RouteAsGhost` 0x80000000 | Internal 0x400 → straight-line fallback (up to 3 sub-plans) on failure | Bigger on failure [verified] |
| `PassThroughObjects` 2 / `PassThroughWalls` 1 / `UseMostDirectPath` 8 | Internal 4 / 8 / 0x10 | Smaller (fewer blocked edges) [inferred] |
| `BeginAsCar`/`EndAsCar`, stroller, `RouteAsLargeAnimal`, `RouteWhileMounted`, boat begin/end | Internal flags 0x80/0x200, 0x800–0x4000, 0x200000, 0x400000, 0x40000000/0x80000000 | Neutral [inferred] |
| Follower type: RouteAsFlier/WalkingDog/LittleDog/LeadingHorse, RouteOption2 RouteAsShark | Agent class `0x800/0x900/0xa00/0xb00/0x4400` at job+0xc0 | Neutral |
| `CreateSubPaths` 0x200000 | Post-processing after a successful search | None [verified] |
| `DoLineOfSightCheckUserOverride` 0x10000 | **Not tested in `0x81b2a0`** | Not a search-size switch; it affects goal or slot checks elsewhere [absence verified in this function only] |

More goals also make searches bigger. On failure, `0x661b50` re-plans once per distinct goal lot [verified].

## Q3. Can a budget be set per route?

**Not from C#.** There is no internal call and no RouteOption for a budget [verified: the 103 `Route_*` internal calls in `internal-calls-map.csv`; the RouteOption enums; `0x819d50` copies no limit]. **A native patch is required.**

**Patch site (world A\*).** The 14 bytes at `0x676be9`–`0x676bf6` (`MOV EAX,[ESP+0x3c]` / `CMP [ESP+0x38],EAX` / `JNZ 0x676495`) are replaced by a `JMP` to a code cave, padded with NOPs. The cave:
1. Increments an expansion counter. Reset it at the entry of `0x676260`; the recursive call at `0x6762b4` runs before the loop, so one shared counter works.
2. Runs the three original instructions.
3. When the counter exceeds N, goes to an abort stub instead of the back-edge.

The abort stub must **drain the open list** before jumping to `0x676bf7` (frees the heap array, returns 0). Draining means decrementing the refcount of each remaining heap entry and returning it to the pool free list `[EBP+0x20]`, like `0x676bc1–0x676be7`. Without the drain, the pool leaks nodes on every abort.

**Risks** [inferred]:
- A budget abort looks like an ordinary failure (code 5), so it **still triggers every retry** in Q1: the per-goal-lot loop, the obstacle replan and the C# fail-path plans. A budget alone is multiplied by those retries.
- To stop that, the abort would also have to set status 4 (PlannerAborted) and skip the `0x81c67a` replan. That is a second patch, in `0x81b2a0`.
- Legitimate long trips (cross-water, Isla Paradiso) would fail if N is too low.
- Some failures are spent in the lot search `0x63e340`/`0x632e40`, which a world-A\* budget does not bound.
- Per-route selection ("only unseen townies") needs a signal from C#. One option is a route option bit that the native code never tests: `DebugPathPlan` (hi 0x1) is not tested in `0x81b2a0` [absence verified there only; other readers not checked]. The C# side would set it with the F8 patch host and the A\* hook would read it from RoutingOptions+0x2c.

## Q4. Lock `0x1021cf0`

- **Implementation** [verified]: `Lock_Acquire` `0x4e16f0` is a recursive lock: a CRITICAL_SECTION, or a mutex when the flag at +0x24 is clear, with an optional timeout (TryEnter + `Sleep(1)` loop). `Lock_Release` is `0x4e17b0`.
- **Users**: 29 functions in `0x829370`–`0x834860` take it [verified xrefs, `xrefs-lockusers.txt`]. This is the RouteManager:
  - Footprint add/remove (`IC_RouteManager_AddPrivateFootprint`, `AddDiscourageFootprint`, `RemoveDiscourageFootprint`, `RemovePrivateFootprint`).
  - Obstruction queries (`IC_Route_IsPointObstructedBySim`, `IC_RouteManager_IsOkToIdleAtCurrentLocation`, `IsRouteToObjectUnobstructed`).
  - Plus non-script callers: object system `0x946160`/`0x947xxx`/`0xb57f20`, world/terrain `0xc6b090`/`0xc6bcc0`/`0xc622d0`, lots `0xaac110`/`0xaacf60`/`0xab1e60`/`0xab4920`, and route-follow/steering `0x846b70`/`0x85b470`/`0x85f5a0`/`0x85fd10`.
- **Threads**: which threads run the non-script callers cannot be told statically. They are candidates for main/render-thread contention [inferred]. To measure: log `GetCurrentThreadId` in `Lock_Acquire` when ECX is `0x1021cf0` (a later probe, not done here).
- **No routing worker exists** [verified]. The RoutingJob vtable `0x1021258` is referenced only by its ctor `0x819d50` and dtor `0x819ae0`. The ctor is called only from `Route_PlanLocked`, so the job is never queued to JobManager.

**Off-thread planning is feasible in principle** [inferred]. The code is already shaped as a job: a RoutingJob object with an Execute method, a refcounted `RoutingPathImp`, and a status field written with atomic stores (1 = running), which another thread could poll. The obstacles:
1. C# `Route.Plan` is synchronous. Every caller reads `PlanResult` right away. Running async needs C#-side yields (patch host), and some callers cannot yield.
2. The planner uses one global PathContainer (closed bitset and node pool at planner+0x10), so only one search runs at a time. The sim thread would still block on the lock for any RouteManager call while the worker plans.
3. The planner reads world, lot and object state (lot lookup `0xc68990`, terrain height `0xc6a540`, Sim obstruction) that may not be guarded by this lock.

Effort L, risk high. This is not the first fix.

## Q5. Where to put the failed-route cache, and the key

**Recommended layer: a native hook on `Route_PlanLocked` `0x82c6e0`.**

Why:
1. It sits above every native retry (Q1), so a cache hit skips all of them.
2. It runs before `Lock_Acquire`.
3. Its inputs are explicit (shown below).
4. Returning 0 there is already a normal path. `Route_PlanCommon` then returns mType = **Failed (2)** with no blocker, its default at `0x7606e0` [verified]. With no blocker id or location, C# `GetRouteFailPath` returns null, so the 4–6 C# fail-path plans are skipped as well [verified logic: SimRoutingComponent.cs:1265–1300].
5. It also covers `PlanFromPoint`, `Replan`, `ReplanFromPosition` and `0xc7bf70`.

Inputs at the hook:
- `ECX` = route world.
- arg1 = output path list (route+0xe0).
- arg2 = start points (64-byte records).
- arg3 = goal points (64-byte records).
- arg4 = follower (route+0x3c).
- arg6 = RoutingOptions (route+0x158; option words at +0x28..+0x34).
- arg9/arg10 = lot id (route+0x40/+0x44).

Offsets are from the `0x7606e0` call site [verified]; the meaning of the option-word offsets inside the route's own options block is [inferred].

**Key** ("the same route"):
- Follower: its object pointer or ObjectGuid. This makes the backoff per Sim.
- Both option words with flags that do not change reachability masked out: `CreateSubPaths`, `PushSimsAtDestination`, `DebugPathPlan`, and others.
- Start: quantized x,z (e.g. 0.5 m), level, start lot.
- Goal set: count plus sorted quantized goal positions and levels (or the destination object id when there is one).
- Routing-world epoch.

**Epoch**: invalidate when the routing world changes. Candidates:
- The pending-update flag `[world+0x95c]+0x10e` that `0x82c420` checks before every plan [check verified, meaning inferred].
- The footprint add/remove functions above.
- Build/buy and door-lock changes.
- Plus a TTL (e.g. 2–5 sim-minutes) as a safety net.

Cache only results with status 7/8 (no path), never 5/6.

**C# alternatives:**
- A `Route.Plan` hook in `ScriptCore` through the F8 patch host can see who is routing (selectable, on-screen, townie). But the key would be built in the interpreter, and the stored `RoutePlanResult` lives in a private field.
- `Sims3.SimIFace.Route.AboutToPlanCallback` / `PostPlanCallback` are public static delegates that nothing in the game sets [verified]. A script can observe every plan through them without a core mod. They cannot cancel a plan.

C# is the right place for **policy** (per-Sim backoff length, the unseen-townie budget flag). Native is the right place for the **cache**.

---

## Recommendation: "cheaper failures"

1. **Native negative cache at `0x82c6e0`**, keyed as in Q5 with an epoch and TTL.
   - On a hit, return 0 before the lock: the result is Failed with no blocker, so no native retries and no C# fail-path plans run.
   - Record status 7/8 failures on the way out.
   - Expected effect: repeat failures cost close to nothing. That covers the 7–12% failure share in Sunset Valley and Isla Paradiso, which F7b found to be the most expensive plans.
2. **Stop the obstacle replan for NPCs.** Either clear `ReplanToFindObstacleWhenPathPlanFails` for unseen townies in C# (patch-host hook on `SimRoutingComponent.SetRouteOptions`), or skip the second PlanPath at `0x81c67a` natively when the follower is not selectable. This halves the cost of every first failure. Cheap and low risk; the only loss is the blocker hint used by the route-fail animation.
3. **Per-Sim backoff in C#** (patch host): after a failure, keep the Sim from re-planning the same destination for N sim-minutes. This complements the cache for townies whose start point moves a little between attempts, which a quantized key misses.
4. **Budget cap last**, and only for unseen townies (signal bit, Q3), at the `0x676be9` back-edge with an open-list drain. Measure first which search dominates failed plans (world `0x676260` or lot `0x63e340`), because a budget on the wrong one does nothing.
5. **Off-thread planning**: not now (Q4).

**Next measurement** before building: count native searches per C# `Route.Plan` on failures. A Frida counter on `0x676260`, `0x63e340` and `0x661b50` per `0x82c6e0` call, on Isla Paradiso, will show how much of the failure cost is retries rather than the first search.

---

## Addendum 2026-10-12: Route_PlanLocked ABI, checked for the native cache

The native cache is built (docs/15, phase 2, off by default). Corrections and confirmations, checked with dumpbin disassembly of `0x82C6E0` and the three call sites `0x760DE1`, `0xC7C6E3` and `0xC7C9D5` **[verified]**:

- **Calling convention:** `__thiscall` with `ecx` = RoutingWorld (`0x8292D0()`), 10 stack dwords, `ret 28h`, result in `AL`.
- **Q5 argument list, corrected:**
  - arg4 `[route+0x3C]` is the **agent class** (0x800/0x900/0xA00/0xB00/0x4400, set by PlanCommon from the options), not a follower object.
  - arg5 is the do-plan byte (1).
  - arg6 is RoutingOptions `route+0x158`. The option words at +0x28..+0x34 are verified for this block too: `0x81C880` is called with `ecx = route+0x158`.
  - arg7/arg8 are zeroed out-dwords.
  - arg9/arg10 `[route+0x40]/[route+0x44]` are the **follower ObjectGuid**: `0x752620` skips a blocker whose id equals them.
- **Lock:** the lock object is `world+0x918`; `0x1021CF0` is a constant pushed as `Lock_Acquire`'s argument (Q4 named it as the lock).
- **Plain failure:** PlanCommon empties the out list before the call. If the call returns 0 with the list still empty, PlanCommon returns its default result (`mType = 2` Failed, no blocker) at `0x76156D`.
