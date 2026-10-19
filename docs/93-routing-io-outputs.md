# 93. Routing I/O study C: what the route search outputs, what it changes, who reads it

Written 2026-10-18. Static only; the game was not run (PLAN item 6, "next is the snapshot design": I/O study part C of 4). Goal of the series: replace the game's route **search** with our own search over our own routing snapshot, so that callers do not notice. This part maps the search's outputs, its side effects on game state and every consumer, and ends with the compatibility contract and the shadow metric. Inputs are parts A/B; this doc names an input only where an output depends on it.

Tools: Ghidra headless decompiles (depth 0, 90 functions) on a private copy of the routing-named project (`game-copy/ghidra-io-c`, copied from `ghidra-usf`, read-only runs); capstone 5 over `game-copy/bin/TS3W.exe` (Steam 1.67) for every offset quoted below; a raw `E8`/`E9` caller scan over `.text` mapped to `game-copy/analysis/functions.csv`; `tools/managed-callgraph.ts` index (`game-copy/analysis/callgraph/index.json`) for the managed callers and readers, cross-read in `game-copy/decompiled`. Scratch scripts stay in the session scratchpad.

Tags as in AGENTS.md: **[verified]** = read in the disassembly, decompile, IL index or decompiled C# (address or file given); **[inferred]** = reasoning, to confirm with a probe or the shadow job of §9.

## 1. Summary

- **Boundary.** The search proper is `Route_PlanLocked 0x82C6E0` (takes `+0x918`, applies pending world updates, runs `RoutingJob_Execute 0x81B2A0`, which calls `PathPlan_WithRetries 0x661B50` once, and a second time for the obstacle replan) [verified]. Everything before it (start points, destination candidates, goal step) and after it (result mapping, destination transform, lengths, EditPath) is game code that consumes the search output. This doc maps the output at two levels: **O1** = what `Route_PlanLocked` leaves behind (path list `route+0xE0` of `RoutingPathImp` objects, chosen start and goal index, bool), **O2** = what `PathPlan_WithRetries` leaves behind (a node array of 64-byte search nodes, a return code, the two indices, a blocker list). A replacement that emits O2 can reuse the game's own path builder (§4.3), which is pure apart from reading the routing world pointer and options [verified decompile of `0x81A340`, `0x819E10`, `0x819680`, `0x8204B0`].
- **Result struct (0x48 bytes) = C# `RoutePlanResult`** (`SimIFace/RoutePlanResult.cs`, sequential layout) [verified: every offset matches the native writes, §2]. Status `mType` has 19 values; the native code produces 1, 2, 4, 10, 16, 18 and whatever the destination candidate generators write; `3` (PlannerAborted) only comes from C# `IsDestObjectInvalid` [verified]. Only `mType`, the two slot hashes, `mDestination`, `mDistanceTotal`, the blocker fields and three point counters carry data; `mDestinationTag` is the goal record's `+0x2C`.
- **Partial paths exist** (§3): a destination whose points all land in the "non-candidate" list is planned to those points; the route then holds a real path but the result reports `FailedBecauseNoPathToDestination` (16) with a blocker; a failed plan leaves one path object (no nodes) in the list, so `GetNumPaths` is 1, not 0; a multi-subpath route whose second or later subpath fails to build is reported as **Succeeded** with a truncated path list, because only the last path gets the failure status and `Route_PlanLocked` reads the first [inferred from decompile and the status-write disassembly; never observed].
- **Side effects (§5).** Of the search's 13 effects on shared state, two are **required** for later code: applying pending routing-world updates (`0x82C420`, the only other caller is the lot path `0x8361B0`), and the portal-lock callback into C# (World event 59, `Sim.OnPortalPathPlan`), which fills `SimRoutingComponent.LockedDoorsDuringPlan` read by the route-fail door logic; its answers also decide the search. The obstacle replan's blocker list is an output, not a side effect. Everything else (A* Q and stamp, node pool, closed bitset, balanced plain refcounts, quadtree pin and flush, lazy lot/room builds, lot table inserts, lot index rebuild, profiler and allocator tags) is skippable: scratch state or lazy caches that other readers build on demand [verified writers; "nobody needs it" inferred per row].
- **Consumers (§6).** Managed: 234 caller methods of the four plan methods (`Route.Plan` 178, `PlanFromPoint` 21, `Replan` 24, `ReplanFromPoint` 21; the IC `Route_ReplanFromPosition` is bound to `ScriptCore.Route::ReplanFromPoint(Vector3)`) [verified index]. Across the whole script code, 284 methods call `RoutePlanResult.Succeeded`, 26 read `mDestSlotNameHash`, 22 `mDestination`, 7 `mDistanceTotal`, 5 `mBlockerObjectId`, 4 `mBlockerLocation`, 2 `mBlockerType`; path accessors: `GetNumPaths` 50, `GetDistanceRemaining` 35, `GetDestPoint` 33, `GetPathData` 25, subpath edits 23+19+7+6, geometry getters 4-16 each [verified index]. Native: the plan's own tail (EditPath per path, end orientation, lengths), follow start (`0x752C20` hands the path list to the steering component), locomotion and steering every tick (docs/84, 86), and the 96 `ScriptCore.Route` ICs.
- **Contract (§8).** No consumer needs a bit-identical path for play. What callers can notice, and therefore must match exactly: the **status code**, the **chosen goal and start** (slot hashes, destination, destination transform), and the **subpath signature** (number of subpaths, `PathType`, portal object id, order: subway/hover-train station choice, vehicle/boat/horse-lane/pond/stroller transitions, route actions). Geometry and lengths only need to be **equivalent** (same corridor, length within tolerance) because they feed thresholds (walk vs run, umbrella > 1.5 m, chase timing) and nearest-choice comparisons. "Status only" holds only for plans whose route is thrown away after `Succeeded()`. Bit-identical output is still needed for one thing: seeded A/B and replay comparisons, where any path change shifts the timeline.
- **Shadow metric (§9):** per plan, game vs replacement on the same inputs and the same snapshot epoch: status equal, start/goal index equal (ties counted apart), subpath signature equal, length within max(0.25 m, 1%), discrete Hausdorff ≤ 0.5 m, blocker and locked-door set equal on failures; "stale" when the epoch moved. Pass: zero non-tie mismatches in status, goal and signature over ≥ 10k plans BP + IP, geometry outliers ≤ 1%.

## 2. The result struct, field by field

The IC writes the default (`mType` 2, all else 0 except `+0x08` and `+0x44`, which it leaves alone) into the caller's hidden result pointer, then copies the 18 dwords `Route_PlanCommon` returns [verified `IC_Route_Plan 0x765160`, `IC_Route_Replan 0x765490`; same copy in the point variants]. `Route_PlanCommon 0x7606E0` builds the struct on its stack at `esp+0x78` (offsets below are from there, checked in the disassembly around `0x7610F2..0x761318`).

| Off | C# field | Written by | Value | Tag |
|---|---|---|---|---|
| 0x00 | `mType` (`RoutePlanResultType`) | PlanCommon (early outs, success), `Route_MapStatusToResult 0x752620` (failure with a path), destination candidate generators (`0x751FF0` -> each destination object's vtable `+0x10`) | §2.1 | verified |
| 0x04 | `mStartSlotNameHash` | PlanCommon success only | `+0x28` of the chosen start record (64-byte records), index from `Route_PlanLocked` arg 7, bounds-checked | verified |
| 0x08 | `mDestinationTag` | PlanCommon success only | `+0x2C` of the chosen goal record | verified (offset); meaning of the goal field inferred |
| 0x0C | `mDestSlotNameHash` | PlanCommon success only | `+0x28` of the chosen goal record (index from arg 8) | verified |
| 0x10 | `mDestination` (Vector3) | PlanCommon success only (`0x76C040` copies x, y, z) | goal record `+0x00..+0x08` | verified |
| 0x1C | `mDistanceTotal` | PlanCommon success only | `route+0xA4` after `Route_UpdateLengths 0x750260` (sum of path vtable `+0x12C`) | verified |
| 0x20 | `mBlockerObjectId` (u64) | `0x752620` (status 7/8), candidate generators | blocker id from path vtable `+0x164`; zeroed first | verified |
| 0x28 | `mBlockerType` | `0x752620`, candidate generators (3 = Object) | §2.2 | verified |
| 0x2C | `mBlockerLocation` | `0x752620`, candidate generators | x and z from the blocker record, y from `0x74A8F0` (height at the point) | verified writes; `0x74A8F0` role inferred |
| 0x38 | `mNumCandidatePointsGenerated` | PlanCommon before the search | candidate list size (64-byte records), the non-candidate list when §3 P1 applies | verified |
| 0x3C | `mNumAcceptedPoints` | PlanCommon before the search | accepted goals after the goal step | verified |
| 0x40 | `mNumRejectedPoints` | PlanCommon before the search | rejected goals | verified |
| 0x44 | (padding to 8) | nobody | stale stack | verified |

On every failure that does not reach `0x752620`, only `mType` and the three counters are meaningful; on early outs before the goal step the counters are 0 [verified].

### 2.1 Every status the native code can return

| `mType` | Name | Native producer | Tag |
|---|---|---|---|
| 1 | Succeeded | PlanCommon when `Route_PlanLocked` returns true and §3 P1 does not apply; also §3 P2 | verified (P2 inferred) |
| 2 | Failed | IC default (handle table null or handle not found); PlanCommon default: destination container `route+0x144` null, or `Route_PlanLocked` false with an empty path list (early outs: `world+0x97E` and `+0x976` both set, empty start or goal vector, plan byte 0); path status 3 (`RoutingWorld_Get` null in Execute); any path status above 8 | verified |
| 3 | FailedBecausePlannerAborted | never native (path status 4 is never written); C# `ScriptCore.Route.IsDestObjectInvalid` | verified |
| 4 | FailedBecauseNoStartSpecified | path status 5: search code 2, an empty start vector in Execute, or a subpath that fails to build (`0x819E10` false or nodes out of order) | verified |
| 5-9 | start-object codes | not seen in the mapped code (route starts are positions); a destination generator could write them | open (generators not decoded) |
| 10 | FailedBecauseNoDestinationSpecified | empty destination list, no candidate points, all goals rejected with the first rejected record `+0x30 != 1`, the x/z bounds test over accepted goals (`0x760D21..0x760D68`), path status 6 (search code 3, or an empty goal vector in Execute) | verified |
| 11-15 | destination-object codes | the destination candidate generators (`0x751FF0` ORs their bools; a generator that names an obstructing object sets 16 + blocker type 3 + that object's position) | verified dispatcher; per-generator codes open |
| 16 | FailedBecauseNoPathToDestination | path status 7/8 (search codes 4, 5, 8; >1 subpath without `CreateSubPaths`, §3 P4); §3 P1 | verified |
| 17 | FailedDestinationBlockedBySim | not seen in the mapped code | open |
| 18 | FailedBecauseDestinationPortalLocked | goal step rejected every goal and the first rejected record has `+0x30 == 1` | verified |

Path status chain [verified]: `RoutingJob_Execute` sets the first path's status (`RoutingPathImp+0x94`, atomic) to 1 at start, then on the last created path 2 on success, else 3 and remaps the `PathPlan_WithRetries` return: 2 -> 5, 3 -> 6, 4 -> 7, 5 and 8 -> 8 (others keep 3). `Route_PlanLocked` returns true when the first path's status (vtable `+0x20`) is at most 2. `0x752620` maps 0-2 -> 1, 3 -> 2, 4 -> 3, 5 -> 4, 6 -> 10, 7/8 -> 16, else 2. It reads `route+0xEC` when `route+0xF4` is set, else the first path.

### 2.2 Blockers (status 16)

`0x752620` zeroes the id, then walks the path's blocker list (count vtable `+0x160`, entry `+0x164`; the list is `RoutingPathImp+0xD0..+0xD8`, 40-byte records, filled only by the obstacle replan in Execute, `0x81C67A`, when `ReplanToFindObstacleWhenPathPlanFails` is set and the code was 4, 5 or 8) and takes the **first** record whose id differs from the follower (`route+0x40/+0x44`) and whose native kind maps [verified]:

| Native kind | `RouteBlockerType` |
|---|---|
| 0x14 | 1 Sim |
| 1, 9 | 3 Object |
| 4, 0x15 | 4 Roof |
| 8 | 5 Sea |
| 0x0C, 0x17 | 6 Wall |
| 0x0E, 0x10 | 7 Terrain |
| anything else | skipped; 2 Vehicle is never produced |

So the blocker is an ordered-list property of the obstacle replan's search: a different search order can name a different blocker even with the same status [inferred].

## 3. Partial-path and quirk cases

| # | Case | What the route holds | What the caller sees | Tag |
|---|---|---|---|---|
| P1 | Candidate generation returns false with status still 2 and a non-empty "non-candidate" list (destination points all filtered as non-candidates) | PlanCommon swaps the lists and plans to the non-candidates; on success it sets the first path's status to 8 (vtable `+0x24(8)`) and maps it | a full path in the route (EditPath and end orientation applied; `0x750260` not run, so `route+0xA4/+0xA8` stay 0) but `mType` 16 with a blocker, `mDistanceTotal` 0 | verified code; when generators do this is open |
| P2 | `CreateSubPaths` on, the search succeeds, a later subpath fails to build | earlier subpaths built and linked, failure status on the last path only, first path still 1 | `Route_PlanLocked` true -> **Succeeded** with a truncated route | inferred (decompile + `0x81C5B8` status write on `[esp+0x44]`, the last path); never observed |
| P3 | Any search failure | one `RoutingPathImp` (no nodes, status 5-8) stays in `route+0xE0`; PlanCommon's tail still runs EditPath and end orientation on it | `GetNumPaths` = 1 on a failed plan | verified |
| P4 | `CreateSubPaths` off and the node array would split into more than one subpath (vehicle, boat, portal-type change) | one SimPath-type record | search code forced to 5 -> `mType` 16 (and the obstacle replan runs) | verified decompile of `0x81BFB2..` (corrects docs/11 Q2 "no effect") |
| P5 | Plan without any accepted goal | no `Route_PlanLocked`, the old path list already freed | 10 or 18 | verified |

## 4. What the plan writes into the route object

### 4.1 Route object (`ScriptCore.Route` native peer, ctor `0x75D4B0`)

| Field | Written by | Meaning | Tag |
|---|---|---|---|
| `+0x1F4` | `IC_Route_Replan` and `IC_Route_ReplanFromPosition` (`add [route+0x1F4],1`), zeroed by `0x75D4B0`, `0x760200`, `0x760370` | replan counter; **no reader** found in `0x740000-0x770000`, `0x810000-0x870000`, `0x940000-0x960000` | verified writers; absence verified in those ranges only |
| `+0x3C` | PlanCommon from the options (0x800/0x900/0xA00/0xB00/0x4400) | agent class, passed to the search | verified |
| `+0x48/+0x4C` | PlanCommon: 0 | a second id read by `IC_Route_GetDestinationObstruction` with `+0x40/+0x44` | verified write; meaning open |
| `+0xA4` | PlanCommon: 0, then `0x750260` on success | total route length (sum of path lengths, `0x74FEF0`) | verified |
| `+0xA8` | PlanCommon: 0, then `0x750260` | distance remaining (length minus the follower's projected progress, path vtable `+0x120`); read by `IC_Route_GetDistanceRemaining` | verified |
| `+0xE0/+0xE4` | PlanCommon empties it first (`0x757250` unlinks: path vtable `+0x78/+0x80/+0x88/+0x90` with 0, releases; `0x866E90` frees nodes); Execute appends | EASTL list of `{next, prev, RoutingPathImp*}` | verified |
| `+0xEC` | PlanCommon: released, then the first path (AddRef) | current path | verified |
| `+0xF0` | PlanCommon: -1 | current path index | verified write; meaning inferred |
| `+0x100..+0x13C` | PlanCommon success | destination transform (rotation rows + goal position, w = 1); identity rotation when the goal has no facing | verified |
| `+0x140`, `+0x141` | PlanCommon: 0, then 1 | has facing, has destination transform (the follow start `0x752C20` hands paths over only when `+0x141`) | verified |
| `+0x144` (destination container) | PlanCommon: byte `+0x48` from option bit 4; with option 0x200, the destination object's routing id pushed to `+0x20..+0x28` and its id to `+0x38/+0x3C` | destination bookkeeping | verified writes; meaning inferred |
| `+0x60` | not by the plan (follow start sets it, `0x753230` clears it) | being followed (docs/87 I-87-11) | verified |

### 4.2 `RoutingPathImp` (0x190 bytes, "Routing/RoutingPathImp", ctor `0x8204B0`, vtable `0x10214D8`)

| Field | Content | Tag |
|---|---|---|
| `+0x04` | refcount (atomic) | verified |
| `+0x94` | status (0 at ctor, §2.1) | verified |
| `+0x98..+0xA0` | node vector, 48-byte nodes: position xyzw `+0x00`, unit direction to the next node `+0x10`, segment length `+0x20`, cumulative length `+0x24`, `+0x28` = 0, flags `+0x2C` (search-node flag bit 0 -> 0x10, bit 1 -> 0x20); written by `0x819E10`, which then sets the path length (vtable `+0x130`) | verified |
| node flags after the build | last node `|8`, second to last `|4` (vtable `+0xC4`), on the final path only | verified |
| `+0xB0` | `PathType` (ctor SimPath `0xC6600B67`) | verified |
| `+0xB8/+0xBC` | path object id (portal / vehicle) | verified copy from the subpath record |
| `+0xC0..+0xCC` | `PortalStartPos` (xyzw) | verified copy |
| `+0xD0..+0xD8` | blocker list (40-byte records) | verified |
| vtable `+0x30` | returns `PathData` {type, object id, portal start} for `IC_Route_GetPathData` | verified |
| vtable `+0x78/+0x80` | link to the next / previous subpath (Execute links neighbours) | verified call shape; slot names inferred |
| vtable `+0x144/+0x148/+0x14C` | end orientation, end position, destination object id (PlanCommon tail, last path) | verified call sites |
| vtable `+0x15C` | interest point list (`IC_Route_GetNumInterestPoints`) | verified reader; producer open |

### 4.3 What the search itself hands to the builder (O2)

`PathPlan_WithRetries` returns 0 on success or a failure code (2, 3, 4, 5, 8 seen in Execute's switch; 6 is retried inside, docs/11) and fills an out "Routing/Path" object (0x18 bytes, ctor `0x64E5C0`) whose node array (begin `+0x08`, end `+0x0C`) holds **64-byte search nodes**, plus `*job+0x38` (start index) and `*job+0x3C` (goal index) (corrected 2026-10-18, see docs/99 §10.5 and ledger I-99-15; this line had the two the other way round) [verified call shape at `0x81BF79`; the obstacle call at `0x81C67A` passes no path and a blocker container read with the 40-byte accessor pair `0x64E550`/`0x64E570`]. The builder then runs:

1. `0x81A340` splits the node array into 64-byte subpath records by node type `+0x1C` (values 0, 1, 3, 6, 10, 0x0B used), flags `+0x0E` (u16; bits 0x40, 0x200, 0x800 used) and the portal/kind hash `+0x2C` (0x3B83, 0x347C, 0xA582 used), and the option bits; it is the **only** writer of `PathType` for planned routes: SimPath, VehiclePath, kUFOPath, BoatPath, HorseLanePath, OceanPath, WadePath, FrozenPondPath, kStrollerPath, PortalPath, PortalTraversalPath (every `PathType` value except ElevatedTrainPath appears as a constant in `0x81A340..0x81B2A0`) [verified].
2. Per subpath: a new `RoutingPathImp` (the first one already exists), the record's type, object id and portal start copied, `0x819E10` copies the node range (start at the previous subpath's end), start position and facing set (vtable `+0x148`, `+0x144`) [verified].
3. Neighbour links, final node flags, status 2 [verified].

Node fields still undecoded: `+0x10..+0x1B`, `+0x20..+0x28`, `+0x30..+0x3C` (lot, room, level and portal ids are the likely content [inferred]). A replacement that emits O2 must fill them as the game does, or the splitter will mis-type subpaths (§10 Q1).

### 4.4 Path options copy: the third id list `O+0x58` (added 2026-10-18)

> **Changes 2026-10-18:** new section from docs/107 §9 (writer verified, meaning inferred); F-4b caveat from docs/99 §11.2.

- **What it is** [verified writer, docs/107 §9.1]: each `RoutingPathImp` holds a copy of the route options (path slot `+0x0C` returns `path+0x08`); its id vector at options `+0x58` (getter `0x545170`) is the **EditPath footprint skip list**. The route's and the job's copies of that list are always empty (the options code only constructs, clears, destroys and assigns it); the path's copy is assigned empty from the route at `0x760F7A` and then filled by `RouteFollow_EditPathForRoute 0x85FF30` -> `RouteFollow_EditPath 0x85F5A0` -> `IdVec_PushBack 0x572400` (at `0x85FC42`, after a duplicate test).
- **Writers, all on the sim thread** [verified sites]: `Route_PlanCommon 0x7606E0` tail at `0x760F8B`/`0x760F92` (every plan), `IC_Route_SetAGS 0x755600`, `IC_Route_InsertRouteSubPathsAtIndex 0x758BA0`, `IC_Route_InsertCustomPathAtIndex 0x7623A0`. The gather inside `EditPath` takes `+0x918` (docs/81, 83).
- **Meaning and readers**: the ids of the footprints a node of the planned path stands in (start, goal, or a portal node inside a footprint) that EditPath did not cut the path around [inferred]; steering skips them (`RouteFollow_A 0x846B70` at `0x846DB3`/`0x846FDB`, `Locomotion_ObstacleList 0x85FFE0` at `0x8602A6`, via `0x81C930`) so a Sim does not steer away from the object its own route starts or ends in [verified reads, meaning inferred].
- **Contract**: this is an O1 output built after the search. A replacement that hands O2 nodes to the game's builder and PlanCommon tail (§4.3) gets it for free and must not fill it itself. A replacement that bypasses the tail must call `0x85FF30(path, &options+0x58, ...)` on the sim thread for each path, or steering will push Sims out of their own start and goal footprints. The shadow compare (§9) needs no field for it as long as both sides run the tail; how often it is non-empty is open (I-107-15).
- **F-4b caveat (geometry and lengths)** [verified build side, consumer use open; docs/99 §11.2]: every planner node carries y = -100; only the start and goal nodes get a terrain height, and nothing between the planner and the 48-byte route node resolves y. CopyNodes (`0x819E10`) computes direction and segment length `+0x20` in **3D**, so a world route's first and last segments are at least `100 + h` m long and point steeply down, and the path length (vtable `+0x130`) includes them. Until F-4b is read live (first two route nodes and the path length against the 2D sum), a replacement that emits O2 must keep y = -100 on inner nodes and let the game's CopyNodes compute lengths; it must not emit real heights, and the §8 metric and §9 `L` check compare lengths computed the same way on both sides.

## 5. Side effects of planning on game state

"Plan" here is everything `Route_PlanLocked` runs. The goal step (`0x75C2D0`, docs/87) and start-point builders run before it and stay the game's in this design, so their effects (they write the same lot planner state) are not in this table.

| # | Effect | Where | Later reader | Verdict for a replacement | Tag |
|---|---|---|---|---|---|
| E1 | Pending routing-world updates applied | `RoutingWorld_ApplyPendingUpdates 0x82C420` at `0x82C73A`; only other caller `0x8361B0` (from the lot function `0xAB1E60`) | every routing reader after the plan (footprints, portals) | **required**: call it (under `+0x918`) where the plan would have, or snapshot-apply the same pending set; else updates wait for the next lot event | verified callers; effect of delay inferred |
| E2 | `[world+0x960]+0x88 = world+0x97D` | `Route_PlanLocked` | not traced | keep (one byte) | verified write; reader open |
| E3 | Portal-lock query into C# (World event 59 -> `LotManager.PortalLockedCallback` -> `Sim.OnPortalPathPlan`), answer cached per (portal, rooms) in `0x64F1E0` | `PortalEdgeCost 0x634500` -> `0x64F550`/`0x64F570` -> `0x64F2C0` | the search itself (locked / discouraged edges); **`SimRoutingComponent.LockedDoorsDuringPlan`** (cleared in `AboutToPlanRouteCallback`, filled here, read by `DoRouteFailureBehavior` for status 18), `Ladder.DiscourageSimFromUsing`, mod handlers | **required**: ask the same doors on the sim thread (before the search or proxied, docs/18) and use the answers; the goal step also asks for its own portals | verified chain (docs/18) and C# readers |
| E4 | Obstacle replan blocker list | `0x81C67A` second `PathPlan_WithRetries` with flags 0x1F, list copied to path `+0xD0` | `0x752620` -> `mBlocker*` -> `GetRouteFailPath`, `DoRouteFailureBehavior`, interrupt actions | output, required (§2.2) | verified |
| E5 | A* scratch Q, node clear, generation stamp `0x11580C4` | `0x61E320`, `0x61C020`, `0x61E360` | the next search (reset first) | skippable (and must not be touched) | verified (docs/18, 87) |
| E6 | World A* node pool, closed bitset, open heap (`planner+0x10`) | `0x676260` | next search (`memset` at start) | skippable | verified (docs/11) |
| E7 | Plain cell / portal / footprint refcounts, +1/-1 around their own loops | lot planner (docs/18 table) | balanced within the search | skippable, must be skipped (non-atomic) | verified sites; balance inferred |
| E8 | Quadtree pin count and deferred flush | `0x667480`, `0x666A40` | the next pinner flushes | skippable (flush moves to the next reader) | verified sites; timing inferred |
| E9 | Lazy lot graph / room graph builds, `RoomToRoomAStar` caches | `0x63CB90`, `0x63D2F0`, `0x6339F0` | goal step, obstacle and slot queries (they build on first use) | skippable; the build cost moves to the first sim-thread reader | verified lazy pattern; cost move inferred |
| E10 | Lot table find-or-insert (rehash on growth) | `0x62EAB0` (`P+0x48`) | every lot reader | skippable (find-or-insert everywhere) | verified |
| E11 | Lot index rebuild | `0xC68680` under `[WM+0x228]` | lot lookups | skippable (lazy) | verified site |
| E12 | Our own `[HeightCache]` table writes from height reads | `0xC6A540` path | our cache | skippable | verified (docs/18) |
| E13 | RoutingJob on the stack, its dtor's lock sites, profiler/allocator tags ("Routing/RoutingJob/startPts", ...) | `0x819D50`, `0x819AE0` | none | skippable | verified |

Not a side effect of the search: `route+0x1F4` (the IC bumps it before planning), the route fields of §4.1 (PlanCommon writes them around the search), `+0x60` (follow only).

## 6. Consumers

### 6.1 Native

| Consumer | Reads | Shape sensitivity | Tag |
|---|---|---|---|
| PlanCommon tail (`0x760F41..0x760F92` and after) | each path: vtable `+0x10` (options), `+0x0C`, then `RouteFollow_EditPathForRoute 0x85FF30` -> `EditPath 0x85F5A0` (from the 2nd node; obstacle query at the first flagged node, docs/84 §3.6); last path: end facing / position / object | EditPath cuts the end near goal footprints: depends on the final nodes' positions | verified calls |
| `Route_UpdateLengths 0x750260` | path lengths (`+0x12C`), follower progress (`+0x120`) | metric | verified |
| Follow start (`IC_Route_BeginFollowingRouteAnimationSpec(Mult)` -> `0x7573E0`/`0x757460` -> `0x752C20`) | the path list and its first/last path, `+0x141` | hands the `RoutingPathImp` objects to the follower's steering component (RefPtrs), so they outlive the next replan | verified |
| Locomotion / steering every tick (`Locomotion_Update 0x83CED0` -> `0x957210`, `RouteFollow_A/_B`, `0x85FFE0` obstacle list, EditPath again; docs/84, 86) | 48-byte nodes, node flags 4/8/0x10/0x20, `PathType`, subpath links | any valid path can be followed; node flags and subpath types must follow the game's rules (the builder of §4.3 guarantees that) | verified readers in docs/84/86; per-field use inferred |
| Path and route ICs (96 `ScriptCore.Route` bindings in `internal-calls-map.csv`; §6.2 names the managed users) | lists, nodes, `PathData`, lengths, interest points, `+0xA8`, destination transform | as their managed users | verified |
| Other `Route_PlanLocked` caller: `World_FindPlaceOnRoadOffScreen 0xC7BF70` (two sites) | the bool and path list | status + path | verified (docs/11) |

### 6.2 Managed

Callers of the four plan methods (234 methods; 9 are the `SimIFace.Route.PlanTo*` wrappers, the rest are game code) [verified index; reads counted in the caller's own body only]:

| Group | Methods | What they read right after the plan | Examples |
|---|---|---|---|
| Hand the route on (return it or call `DoRoute`/route actions) | 128 | nothing directly; `DoRoute` then reads `Succeeded`, `IsLoop`, `GetPathStartPoint`, `PopPath`, and per subpath `GetPathData` -> route action (`GetRouteActionForSubPath`), walkstyle rules (path lengths, distance remaining) | `PetDestroyNewspaper.RouteToNewspaper`, `Sphinx.GetRouteToShell` |
| Status only | 48 | `Succeeded()` / `PlanResult` | `Stereo.RouteInFrontOfStereo`, `HitchingPost.RouteSimToHitchingPostSlot` |
| Goal / geometry | 14 (+ 22 with more) | `mDestination`, `mDestSlotNameHash`, `GetDestPoint`, start/end points and directions, segment points | `SocialInteraction.SocialRouteA`, `ShowStage.RouteSimNearShowFloorsLookingAtStage`, `CommonDoor.GetRouteThroughDoor` |
| Structure (subpaths) | 25 | `GetNumPaths`, `GetPathData`, subpath inserts, `PopPath`, `ClearPaths`, `SmoothenSubPathConnections` | houseboat gangway/ladder/boat, `VehicleFollowSubPathRouteAction.ForceReplan`, `CreateRoamingPathInternal`, `HorseJump.DoPlan`, `Sim.PlanRunUsingPathPoints` |
| Metric | 16 | `GetDistanceRemaining`, `GetRouteLength`, `GetPathLength`, `mDistanceTotal`, `GetEstimatedTravelTime` | `HorseJump.Jump.Run`, `Trampoline.SwapSidesIfNextInteractionIsCloserToOtherSide`, `SwimHere.RouteActorToDestination` |
| Failure detail | 3 (+ the fail-path code below) | `mType`, `mBlocker*` | `Sim.IsPointInLotSafelyRoutable`, `ObstacleEncounteredRouteInterruptAction`, `ReplanRouteInterruptAction` |

By IC: `Plan` 173 game callers (120 hand on, 42 status, 7 geometry, 4 structure); `PlanFromPoint` 17 (10 structure); `Replan` 24 (10 status, 6 structure); `ReplanFromPoint` 21 (12 structure) [verified index]. So the Replan family is mostly follow-time edits of a route being walked (route actions, houseboats, vehicles), where the subpath signature matters most.

Readers anywhere in the script code (not only plan callers) [verified index]:
- **Status:** `RoutePlanResult.Succeeded` 284 methods; `mType` 6 (incl. `DoRouteFailureBehavior` for status 18, `IsPointInLotSafelyRoutable`).
- **Goal:** `mDestSlotNameHash` 26 (which slot the Sim ends in: `ChairBarStool.RouteToForSitting`, `HitchingPost.TieUpMounted`, `Bathtub.CreateBathDogJig`), `mDestination` 22, `GetDestPoint` 33.
- **Structure:** `GetNumPaths` 50, `GetPathData` 25 (`Subway`/`HoverTrainStation` `GetDestination...ForSimWithPlannedRoute` pick the arrival station from the route's `PortalPath` subpaths; `RoutingComponent.GetRouteActionForSubPath` picks the route action, umbrella, stroller, pond and wade transitions from `PathType`), `InsertRouteSubPathsAtIndex` 23, `ClearPaths` 19.
- **Metric:** `GetDistanceRemaining` 35, `GetRouteLength` 6 (`SocialTest.GetClosestAvailableBoxStallForWooHoo` compares routes), `GetEstimatedTravelTime` 6 (mail carrier chase, pet chase), `GetPathLength`/`GetPathDistanceRemaining` 5 (walkstyle rules, distance until portal).
- **Geometry:** `GetPathEndPoint` 16, `GetPathStartPoint` 15 (also `ScriptCore.Route.Plan` itself stores the first path's start as `mOriginalStartPoint`), `GetSegmentAtDistanceBeforeEnd` 11 (social jigs, route sounds), segment start/end 8 each, `GetPointAtDistanceOnRoute` 3.
- **Node count:** `GetNumNodes` 1 (`Sim.BeingDismounted.Run`).
- **Failure detail:** `GetRouteFailPath` (blocker id, then location) and `DoRouteFailureBehavior` (status 18 + `LockedDoorsDuringPlan`), `DivingBoard.TestRoutabilitySafety` (blocker type and location).
- **Callbacks around every plan:** `Route.AboutToPlanCallback` / `PostPlanCallback` are set by `SimRoutingComponent`, `CarRoutingComponent`, `TombRoomManager`, `BunkBedContainer` (corrects docs/11 Q5 "nothing sets them"); they run in the C# method, outside the search, so a native replacement keeps them.

## 7. Which consumers are sensitive to exact shape

| Sensitivity | Consumers | Why |
|---|---|---|
| Tie-breaking between equal goals or starts | slot hash readers (26), `mDestination` (22), `GetDestPoint` (33), destination transform, `mStartSlotNameHash` | a different but equally short goal puts the Sim in another chair, slot or jig side: visible, and it changes later interactions |
| Subpath signature | `GetPathData` users, `DoRoute` route actions, station choice, vehicle/boat/horse-lane/pond transitions, houseboat replans, `InsertRouteSubPathsAtIndex` composers | another portal or another vehicle/boat switch point is a different behaviour, not a different polyline |
| Blocker choice | route-fail balloon target, `GetRouteFailPath` replan target, interrupt actions | first matching record of the obstacle replan's ordered list (§2.2) |
| Node count / exact nodes | `GetNumNodes` (1 caller), segment-index ICs (indices are only used within the same route: self-consistent), `SelfIntersectionTest`/`SubdividePathsWithSelfIntersections` (roaming paths), `FindObstaclesForSimAlongRoute`, EditPath's goal cut | sensitive in detail, but each reads its own route consistently; equivalence suffices [inferred] |
| Length thresholds and comparisons | walkstyle (walk vs run vs jog), umbrella (> 1.5 m), stroller, chase timing, nearest-box-stall, trampoline side, horse jump | numeric; a longer path can cross a threshold or flip a nearest choice |
| Status only | the 48 status callers whose route is discarded, reachability tests (`IsPointInLotSafelyRoutable`, `DivingBoard.TestRoutabilitySafety` uses blocker too) | only success/failure (and the code) |

## 8. Compatibility contract for a replacement search

Levels: **exact** = equal to the game's value; **equivalent** = a valid result of the same kind within tolerance; **status only** = same `mType`.

| Group | Must match | Level | Notes |
|---|---|---|---|
| All plans | `mType` (§2.1), including P1/P3/P4 behaviour; P2 only if the shadow sees it | exact | P2 (Succeeded with a truncated route) is a game bug; reproduce it only if it occurs, and log it |
| All successful plans | chosen goal index and start index (so slot hashes, destination, tag, transform, `mOriginalStartPoint`) | exact; on cost ties the game's tie order | tie order of the game's A* (heap order, goal list order) is open (§10 Q4) |
| Follow, structure, Replan family | subpath signature: count, `PathType`, object id, order; PortalStartPos within 0.05 m | exact | emit O2 nodes and let the game's builder type them (§4.3), or match its rules |
| Follow, geometry | polyline in the same corridor: discrete Hausdorff ≤ 0.5 m; first and last node equal (start point, goal) | equivalent | EditPath and locomotion recompute from the nodes |
| Metric | total and per-path length within max(0.25 m, 1%) | equivalent | lengths feed thresholds; one cost model for both searches keeps rankings |
| Failure detail | blocker id and type equal; location within 0.5 m; `LockedDoorsDuringPlan` set equal | equivalent (same object), exact for the door set | requires E3 and E4 |
| Discarded reachability plans | `mType` | status only | everything else may differ |
| Seeded A/B, replay, shadow proof | every field and every node | exact (nice to have) | only for measurement; a replacement that is equivalent but not exact makes seeded runs diverge |
| Side effects | E1 applied at the same point; E3 asked for the same doors with the same answers | exact | E5-E13 must not be done by a search that runs off the game's data |

## 9. Shadow-compare metric

Run the game's search as today and ours on the same inputs (start and goal records, options, agent class, follower) against a snapshot whose epoch equals the live routing epoch at plan time; ours never feeds the caller in shadow mode. Per plan record:

| Field | Check | Class on failure |
|---|---|---|
| epoch | snapshot epoch == live epoch at `Route_PlanLocked` entry | `stale` (not a mismatch; counts coverage) |
| status | path status and the mapped `mType` equal | `S` |
| start / goal index | equal; if not, both goals' path costs within 1e-3 relative | `G` or `G-tie` |
| subpath signature | count, `PathType`, object id, order equal | `T` |
| length | per route and per path within max(0.25 m, 1%) | `L` |
| shape | discrete Hausdorff between node polylines ≤ 0.5 m; node count difference logged | `H` (info: `N`) |
| failure detail | blocker id/type equal, location ≤ 0.5 m, locked-door set equal | `B` |
| cost | our search µs and the game's µs | report only |

Report per IC (Plan, Replan, PlanFromPoint, ReplanFromPosition) and per caller group (P10 tags), with the first 20 mismatches' inputs dumped for offline replay. **Pass for "equivalent":** `S`, `T` and non-tie `G` = 0 over ≥ 10,000 plans in BP and IP each, `L` + `H` ≤ 1%, `B` ≤ 1% of failures, `stale` ≤ 5%. **Pass for "exact"** (seeded runs): every class 0 including `G-tie` and `N`. A shadow job also needs E1 and E3 on the replacement side, or the door set and the pending-update state will differ by construction.

## 10. Open questions

> **Changes 2026-10-18:** Q4 partly answered (docs/108 §7 F-7); Q9 and Q10 added (docs/107 §9, docs/99 §11.2).

1. The 64-byte search node fields not decoded (§4.3): needed before a replacement can emit O2 that the splitter types correctly.
2. Which `PathPlan_WithRetries` codes occur and how often (2/3/4/5/8), and how often P1/P2/P4 fire: a count-only probe at `0x81C5C9` and in PlanCommon's P1 branch.
3. Meaning of search-node flag bits 0/1 (path node flags 0x10/0x20) and of `world+0x97D` (E2).
4. The game's tie order for equal-cost goals and starts (heap order in `0x676260`, goal list order from the goal step). (2026-10-18, docs/108 §7 F-7: goal ties occur, 0 in 21,063 BP plans and 2 in 8,257 IP plans [measured]; the order is docs/99 §2.5. Until a replacement copies that order exactly, the §9 compare classes such cases `G-tie`, not `G`.)
5. Whether skipping E9/E10 in the search moves noticeable build cost onto sim-thread readers (gates 0/12, docs/86/90).
6. Which doors the search asks about (E3) versus the goal step: the replacement must ask the same set, or `LockedDoorsDuringPlan` and ladder discouragement differ.
7. The destination candidate generators' status codes (5-9, 11-15, 17).
8. Producer of the interest point list (path vtable `+0x15C`) and any reader of `route+0x1F4` outside the scanned ranges.
9. How often the path's `O+0x58` list is non-empty and which object kinds land in it (§4.4, I-107-15; live read of `path+0x60..+0x64`).
10. F-4b: does any consumer use the 3D segment length or direction of the first and last route segments (§4.4, docs/99 §11.2)?

## 11. Ledger

Rows `I-93-1` .. `I-93-10` in docs/51 §26.
