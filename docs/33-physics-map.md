# 33. Physics and collision module map (M-PHY, static)

PLAN backlog item 7, task M-PHY. Written 2026-10-15 from a private Ghidra copy (`game-copy/ghidra-phy`, copied from the G13 project, with `tools/ghidra/symbols/ts3w-1.67.symbols.csv` applied). The game was not run. Steam TS3W.exe 1.67, image base `0x400000`, all addresses absolute.

Tags: **[verified]** = read in a decompile, the disassembly, call graph or the exe's strings; **[measured]** = from existing sample files named below; **[inferred]** = reasoning, still to confirm.

## 1. Summary

- **There is no physics engine in the usual sense.** No Havok, PhysX, Bullet or ODE strings, no rigid bodies, no ragdolls, no integrator. The only "gravity" strings belong to the particle/VFX code (`0x8D82E0`, `0x8EF9E0`, `0x901850`) [verified: string scan]. What EA calls `Physics` is a **static collision world for queries**: shapes (box, sheared box, capsule, height field, kd-tree mesh, group) held in one loose quadtree, asked for sphere range queries and ray casts [verified].
- **Module size:** about **175 functions, ~62 KB of code, `0x676F70`-`0x687440`** [verified function count; the start boundary is inferred: the first function of the block whose only callers are module functions]. 143 functions outside the block call into it, a few of them only through EASTL vector helpers folded into the block (CAS, Mono, ResourceSystem callers) [verified, direct calls].
- **Owner thread: the sim thread.** Every writer (add, remove, move a shape) compares `GetCurrentThreadId()` with the sim thread id `[0x11D8A8C]` and, when called from another thread, posts a message to the simulator instead of touching the tree. Readers have no check and no lock [verified].
- **Cost: 3.2% of sim-thread samples self time** (10,238 samples, 16 files), of which 1.8% is the kd-tree ray (terrain/floor height, docs/28 N10) and 1.0% the loose quadtree (N11, N14) [measured].
- **New finding for the route worker:** the planner reaches the physics **ray** path (`0x6583E0` -> `0x686CC0` -> `0x681E50` -> `0x681820`), which the S0 proxy of `0x83B3A0` (range path only) does not cover [verified, direct calls]. See §5.

## 2. Structure

| Block | Range | What | Key functions |
|---|---|---|---|
| A. Shape narrow phase | `0x6771C0`-`0x67A9F0` | Per-shape sphere and ray tests, called from the shape vtables | `0x6795D0` `PhysShape_RayLocal` (5 KB, from the box and kd-tree ray slots) [inferred role], `0x677A10` (called by the box, sheared box and height-field sphere slots) [verified callers, role inferred] |
| B. 2D polygon helpers | `0x676F70`, `0x67A9F0`-`0x67CC90` | Point in polygon, point within radius of a polygon set, segment vs polygon set, closest point. Polygon sets are vectors of 16-byte entries (begin/end of 2D points) | `Poly2D_ContainsPoint 0x676F70`, `PolySet_ContainsPoint 0x6772E0`, `Poly2D_PointWithinRadius 0x67AE90`, `PolySet_PointWithinRadius 0x67B130`, `PolySet_SegmentIntersects 0x67B190`, `PolySet_ClosestPoint 0x67B990` |
| C. Collision helpers | `0x67CC90`-`0x67E2B0` | Hull and corner helpers ("Physics/PhysicsCollisionHelpers/ptAngs", "corners2d") | `ConvexHull2D 0x67CC90` [inferred], `OrientedBoxCorners2D 0x67D290` [inferred] |
| D. KD-tree mesh shape | `0x67E2B0`-`0x6809F0` | Triangle-mesh shape for terrain and floors; build and ray cast | `KDTree_AllocArrays 0x67E650`, `KDTreeFactory_Create 0x67EE90`, `KDTree_BuildRecurse 0x67F4A0`, `KDTreeShape_Build 0x680510`; ray: vtable slot `TerrainRay_A 0x67EDE0` -> `TerrainRay_B 0x67EA80` -> `TerrainRay_C 0x67E740` -> `RayTriangle_Int16 0x67E2B0` |
| E. Loose quadtree | `0x6809F0`-`0x682120` | The broad phase for every shape. Range: `0x681E00` -> recursive `0x681CF0` (4 children, node box test `0x6809F0`) -> node `0x681B40` (mask test `shape+0x34`, radius via vtable `+0x14`). Ray: `LooseQuadTree_RayCast 0x681820` -> recursive `0x6814B0` (children sorted front to back, early out at the best hit) -> node `0x680F80` | `LooseQuadTree_ctor 0x681770`, `_Insert 0x6818F0`, `_Unlink 0x6813F0`, `_Reinsert 0x6819B0`, `Physics_RayCastHit 0x681E50` |
| F. Realm, shapes, world API | `0x682120`-`0x687440` | Realm (root group, global shape list, quadtree), groups, shape classes and vtables (`0xFEFA3C`-`0xFEFD90`), world facade, service, cross-thread messages | below |

**Objects** [verified, ctor and accessor decompiles]:
- `PhysicsWorld` (global `[0x11CF490]`, getter `PhysicsWorld_Get 0x686A90`, 76 callers; vtable `0xFEFE5C`): `+0x8` Realm, `+0xC` the simulator message sink used for cross-thread writes (set by `PhysicsWorld_InitRealm 0x686AA0`). Written at `0x686D94` and `0x687397` (startup/shutdown, outside defined functions).
- `Realm` (0x18 bytes, `Realm_ctor 0x6822B0`): `+0` root group, `+4` LooseQuadTree (0x60 bytes), `+8..+0x10` global shape vector ("Physics/ShapesGlobalList").
- `Group` (0x40 bytes, `Realm_CreateGroup 0x682120`, vtable `0xFEFC24`): parent link, shape vector "Physics/GroupShapes", default mask `0x9FFFF`. Lots keep one group per level ("Lot/PhysicsGroupsForLevels", `0xAC19F0`, `Lot::Load 0xACF520`).
- `Shape` base (`PhysShape_ctor 0x682890`, vtable `0xFEFCB0`): `+0x10` position, `+0x20` rotation, `+0x30` bound radius squared, `+0x34` collision mask, `+0x38` index in the global list, `+0x3C` index in its group, `+0x40/+0x44` user data (owner object id, `PhysShape_SetUserData 0x682780`), `+0x48` quadtree link, `+0x4C` **non-atomic** refcount (`PhysShape_AddRef 0x682750`, `PhysShape_Release 0x682760`), `+0x50` group.
- Shape classes: box (vtable `0xFEFBDC`, ray slot `BoxShape_RayCast 0x682C40`), sheared box (`0xFEFCD0`, walls, fences and modular stairs, `ShearedBoxShape_Set 0x683270`), capsule (`0xFEFCF0`), height field (`0xFEFD10`, `HeightFieldShape_ctor 0x683120`, grid `HeightFieldShape_SetGrid 0x6827E0`; terrain "Terrain/TerrainData/HeightFieldShape", floors, roofs), kd-tree mesh (`0xFEFA3C`), compound (`0xFEFD78`, forwards user data to children `0x682D90`) [verified vtables and tags; class names partly inferred from the tags of their creators].

**World API** (block F, the entry points other systems call) [verified]:

| Address | Name | Role |
|---|---|---|
| `0x686ED0` | `PhysicsWorld_AddShape` | Sim thread: `Realm_AddShape 0x6823E0`. Other thread: posts message `0x5B57E39` ("Physics/PostMessageToSimulator/AddShape") |
| `0x686F50` | `PhysicsWorld_RemoveShape` | Sim thread: `Realm_RemoveShape 0x682080`. Other thread: message `0x5B57E61` |
| `0x687130` | `PhysicsWorld_UpdateShape` | Sim thread: quadtree reinsert. Other thread: message `0x5B575EF` |
| `0x6871C0` | `PhysShape_SetTransform` | New position/rotation, skips changes below an epsilon, then `UpdateShape` |
| `0x686C20` | `PhysicsWorld_RangeQuery` | Sphere query by type mask (`Physics_MaskForType 0x686B40`) -> `Realm_RangeQuery 0x682590` |
| `0x686C70` | `PhysicsWorld_RangeQueryFiltered` | -> `Realm_RangeQueryFiltered 0x682610` |
| `0x686FD0` | `PhysicsWorld_RayCastSegment` | Segment ray (from, to, type, out hit) -> `0x681E50` |
| `0x686CC0` | `PhysicsWorld_RayCastVertical` | Ray from a point along a fixed direction [direction inferred]; only caller routing `0x6583E0` |
| `0x686DE0` | `PhysicsService_Register` | "Physics/Service", service id `0x175EC5F`, vtable `0xFEFE0C`; called from app startup `0x589080` |

The service has no per-frame simulation step: nothing moves shapes except their owners [inferred from the vtable slots and the absence of an integrator].

## 3. Callers by subsystem

From the 143 external callers (direct calls, `ExtCallers`, nearest named ancestor up to 6 levels) [verified call edges; the subsystem of an unnamed caller comes from its allocator tags or named ancestors]:

| Subsystem | What it uses | Callers (examples) |
|---|---|---|
| Routing (planner, goal step) | Range query, vertical ray, polygon sets | `0x83B3A0` (goal step, planner `0x654CC0`, `IC_RouteManager_IsOkToIdleAtCurrentLocation`), `0x6583E0` (planner `0x6593F0`, `Route_GoalStep` via `0xC73820`/`0x658830`, `IC_Route_IsPointRoutable`), `RouteCost_Dispatch 0x627780` (segment vs polygons), `0x62D0D0`, lot graph `0x66AB70`, footprint builders `0x832890`/`0x832FB0` ("Physics/CollisionHelpers", "Physics/PolygonProcessor"), road clip `0xB74A20` (from `Terrain_GetHeight` inside `RoutingJob_Execute`) |
| Route ICs (sim thread) | Range query, segment ray | `0x824FA0` (`IC_RouteManager_PlaceGroupSocialImpl`), `0x82A8F0`/`0x82AEC0`/`0x82E340` (`IC_RouteManager_IsRouteToObjectUnobstructed`, `IsSlotUnobstructed`, `RoutingBlockerCheck2d`) |
| Locomotion | Shape sphere test | `0x83B230` (from `Locomotion_Update 0x83CED0`), `0x86E570`, `0x86CF40` |
| Height queries | KD-tree ray | `WorldCellGrid_Raycast 0xC2A570` (docs/28 N10), `0xC6C810` |
| Script queries (ICs) | Range, ray | `IC_Query_GetObjectsInRange`, `IC_Query_CountObjectsInRange`, `IC_Query_GetSimsOverlappingFootprint`, `IC_World_FindPlaceOutsideLot`, `IC_World_CanSeeSkyFromPosImpl`, `IC_World_CanSeeObjectImpl` (`0x789DD0`), `IC_World_CanSeePointImpl` (`0x78A230`), `IC_World_PickWallAtWindowPoint`, `IC_World_PickFloorAtWindowPoint` |
| Build/buy placement | Range query, corners | object placement tests `0xAC5610`/`0xAC59B0` ("Lot/ObjectPlacementTests/SimPoly"), `0xAC9460`, `0xB10AB0`/`0xB149A0` (`IC_World_FindGoodLocation`), deck `0xCAC110`, modular stairs `0xCB8C30`, world tools `0xD40EA0`/`0xD41000`/`0xD41310`, `0xD68CA0` |
| Lot geometry (shape owners) | Add/remove/update | floors `0xA93B00`, walls `0xB51290`, fences `0xB4FBC0`, modular stairs `0xB1B1F0`/`0xB1BBE0`, roofs `0xB98430`, partitions `0xB54B60`; reached from lot load, bulldoze, houseboat and build tool ICs |
| Terrain (shape owner) | Add/remove, kd-tree build | `0xC2A840` (cell shape rebuild), `0xC83200`/`0xC84090`/`0xC85270` (from `WorldManager_Update` and lot streaming) |
| Objects | PhysicsComponent | `PhysicsComponent_Load 0x94E120`, `_BuildShapes 0x94E040`, `_AddBox 0x94DF50`, `_AddCapsule 0x94DEA0`, `_SetTransform 0x94DBA0`, `_RemoveShapes 0x94DAE0`; trees `0x95CF50`/`0x95EC10` (SpeedTree capsules) |
| Camera and photos | Segment ray | `0x964D70`, `0x96BB40` (`IC_Camera_*` ancestors), `0x96C480`, `0x974770`, `0x97EA00` (range), `0x97F440`/`0x97F6C0`/`0x97F910` (`IC_World_CapturePhoto`) |
| UI picking | Segment ray | `0xCCEB60` (`IC_UIManager_GetPickInfoImpl`), `0xCCE990`, `0xCCEDA0`, `0xCCF390` |
| Animation | none found | No SACS or rig code calls the module directly [verified for direct calls] |

## 4. Threads, shared state, locks

- **Writers marshal to the sim thread** [verified]: `0x686ED0`, `0x686F50` and `0x687130` read the sim thread id through `0x7D99F0` (`[0x11D8A8C]`). Off the sim thread they allocate a message (`PhysicsSimMsg_ctor 0x686E50`, atomic refcount, AddRef on the shape) and call the sink's vtable `+0x1C`. With `Multithreaded=0` the ids match and every write runs inline.
- **Main-thread writers exist:** `WorldManager_Update` (main thread, docs/28 §5) -> `0xC845C0` -> `0xC83200`/`0xC84090` -> `AddShape`/`RemoveShape` [verified path]. Terrain chunk shapes therefore change on the sim thread one message later.
- **Readers have no thread check and no lock** (`0x686C20`, `0x686C70`, `0x686FD0`, `0x682590`, `0x681E50`) [verified decompile]. No function in the module calls `Lock_Acquire 0x4E16F0`, `Lock_Release 0x4E17B0` or a critical section [verified, direct callees].
- **Unlocked shared state** [verified]: the quadtree nodes and links, two global free lists (`[0x11587F8]` links, `[0x1158808]` nodes) used by `LooseQuadTree_Unlink`, the global shape vector (swap-remove), group vectors, non-atomic shape refcounts at `+0x4C`.
- **Relation to the routing lock `world+0x918`:** none. Physics writes never take it, and the planner reads physics under it only because the planner holds it for its own reasons (docs/18 S0) [verified].
- **Who reads off the sim thread:** the route worker (docs/18): the range path through `0x83B3A0` is proxied to the sim thread; the ray path `0x6583E0` -> `0x686CC0` is not (§5). Camera and UI picking callers are reached only through vtables, so their thread is not known statically [inferred: main thread for the camera, as for other services; to confirm with a thread-id counter in `0x686FD0`]. **Live check (verify rerun 2026-10-16, docs/data/results/2026-10-15-verify-findings.md):** the ray entry `0x6583E0` ran on the sim thread only (4.6M calls, no main callers), so it is a sim/routing path; camera and UI picking do not go through `0x6583E0` [measured].

## 5. Route worker: uncovered physics read (action item)

`RoutingJob_Execute 0x81B2A0` -> `PathPlan_WithRetries` -> `PathPlanner_PlanHierarchical 0x65E0D0` -> `0x6593F0` -> `0x6583E0` -> `PhysicsWorld_RayCastVertical 0x686CC0` -> `Physics_RayCastHit 0x681E50` -> `LooseQuadTree_RayCast 0x681820` -> `0x6814B0` [verified, direct calls, depth 7]. The same function is reached from `Route_GoalStep` (via `0xC73820` -> `0x658830`) and `IC_Route_IsPointRoutable`.

docs/18 S0 proxies `0x83B3A0` (range) and asks Verify to show no other worker read of `0x681E00`; this path never touches `0x681E00`. In worker mode 1 (offload-sync) the sim thread waits, so no writer runs at the same time and the read is safe [inferred from the mode]. In an async worker (H6), a shape add/remove/move on the sim thread during this ray can walk a node that `LooseQuadTree_Unlink` is freeing [inferred race]. Fix options: proxy `0x686CC0` to the sim thread like `0x83B3A0`, or count worker calls of `0x681820` and `0x681E00` in `[RouteGate] Verify` first.

## 6. Cost (existing samples only)

Source: all 16 non-empty `probe-out/profile/*simstack*.jsonl` files (BP 4,076, IP 4,439, SV 1,723 samples; 10,238 total, base and fix configs mixed). Method of `tools/harness/sample-split.ts`: leaf-most TS3W frame for self time, nearest function from `game-copy/analysis/functions.csv`; shared EASTL helpers folded into the range (`0x6774A0`, `0x67EF00`, `0x67F0C0`, `0x67F140`) and the getter `0x686A90` excluded. The script stayed in the session scratch folder [measured].

| Block | BP | IP | SV | All |
|---|---|---|---|---|
| D. KD-tree ray (height) | 1.47% | 2.37% | 0.93% | **1.77%** |
| E. Loose quadtree (range + ray) | 1.23% | 0.88% | 0.70% | **0.99%** |
| B. 2D polygons | 0.17% | 0.27% | 0.23% | 0.22% |
| F. Realm, shapes, API | 0.20% | 0.18% | 0 | 0.16% |
| A. Narrow phase | 0.07% | 0.11% | 0.06% | 0.09% |
| C. Hull helpers | 0 | 0.02% | 0 | 0.01% |
| **Module self** | 3.14% | 3.83% | 1.92% | **3.23%** |

At 5 Hz wall-clock sampling, 1% is about 10 ms per second of sim-thread time (docs/28), so physics is ~32 ms/s pooled, ~38 ms/s on IP [inferred from the method]. Top self functions: `RayTriangle_Int16 0x67E2B0` 1.41%, `0x681B40` 0.39%, `TerrainRay_C 0x67E740` 0.22%, `0x681CF0` 0.21%, `0x680F80` 0.16%. Callers of self samples: `WorldCellGrid_Raycast 0xC2A570` 0.86%, `0xC6C810` 0.18%, CanSeeObject `0x789DD0` 0.12% [measured]. Inclusive time (any of the first 12 frames) reads 14.5% but is inflated by stale frames from the stack scan, so it is not used.

## 7. Candidates

| # | Candidate | Cost covered | Blocker (docs/28) | Verdict |
|---|---|---|---|---|
| P1 | Terrain height cache at `0xC2A570`, invalidated at `0xC2A840` | block D, 1.8% | none (pure read of kd-trees) | Already built as `[HeightCache]` (PLAN H5, parked); measure in game |
| P2 | Proxy or gate the ray path `0x686CC0` for the route worker | correctness, not speed | S | Needed before any async route worker (§5) |
| P3 | Reuse the temporary "Physics/RangeShapeResult" vector in `0x682590`/`0x682610` (allocated and freed per query) | part of N6 allocator share | none for a per-thread scratch buffer | Small [inferred]; only if the allocator stays hot |
| P4 | Snapshot or double-buffer the quadtree so reads can leave the sim thread | E, 1.0% | S (unlocked tree, global free lists, non-atomic refcounts), O (callers use results at once) | Not worth it alone; physics reads move with their callers (routing N3), proxied |
| P5 | Per-tick cache of `CanSeeObject`/`CanSeePoint` rays | ~0.1-0.3% | O | Not worth it (docs/20 §4b) |

Offload verdict: the module has no work of its own to move (no simulation step). It is a shared read structure; its blockers are **S** and **O**, and it moves only as part of the routing job (N3) with the sim-thread proxies.

## 8. Mapping method (how to reuse it for renderer, UI, audio)

What made this map fast:
1. **Allocator tags.** Every EA allocation passes a "Subsystem/Class/member" string (`0x4E3F90(size, "Physics/Realm/Group", ...)`). A string scan for the subsystem prefix, then xrefs, gives constructors, owners and member names in one pass (`tools/ghidra/StringXrefs.java`). This alone named about half of the functions. For the renderer use `Renderer/`, `LotRenderer/`, `SceneObjectManager`; for UI `UI/`, `EAText/`; audio `rw::audio`, `Audio/`.
2. **Code locality.** MSVC links one object file contiguously, so a subsystem is a contiguous address block. Find the extremes of the tagged functions, list everything in between (`RangeMap`), and look where the callers stop being internal: that gives the module boundary and its public API (the functions with external callers).
3. **Vtables next to the code.** The shape vtables sit together in `.rdata` (`0xFEFA3C`-`0xFEFE70`) in the same order as the code. A pointer dump of that range (`DumpPtrs`) groups slots per class, and the constructor that stores each vtable names the class through its tag. There is no RTTI in this build, and identical tiny functions are folded (a 3-byte stub carries the name `IC_RouteManager_GetSteeringObjectRadius` in many vtables), so treat shared stub names as meaningless.
4. **Service registration.** Engine subsystems register through `0x59ECC0` with a hash id and a vtable (`Physics/Service` `0x175EC5F`). Listing the services gives the top-level subsystems and their update slots.
5. **Thread checks in the code.** `GetCurrentThreadId()` compared with `[0x11D8A8C]` (sim) or `[0x11D8A88]` (main) marks the functions that care about threads; grep for calls of `0x7D99F0` in a block before tracing paths.
6. **Internal calls as labels.** The `IC_*` names (1,706 bindings) give named ancestors for unnamed callers; a BFS up the callers to the first `IC_` or known name classifies each external caller by subsystem (`ExtCallers`).
7. **Cost from existing samples.** Map address ranges to blocks and run the sample split per block before any new measurement.

Tools used (in `game-copy/phy-work/`, not committed; worth adding to `tools/ghidra/` if the other mapping tasks need them): `RangeMap.java` (per function in a range: size, callers, data refs, strings, callees), `ExtCallers.java` (external callers with tags and nearest named ancestors), `DumpPtrs.java` (vtable dump), plus the existing `StringXrefs`, `CallPaths`, `DecompileTo` and `ImportSymbols`.

## 9. Names

60 new rows in `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/33>`), applied to `game-copy/ghidra-phy`. Old names kept to avoid churn, with corrected comments: `FootprintQuadtree_*` (`0x681B40`, `0x681CF0`, `0x681E00`) are the **physics** quadtree, not routing footprints; `LineOfSight 0x6814B0` is the quadtree ray recursion used by every ray cast (routing, picking, camera, CanSee*), not a separate line-of-sight routine; `TerrainRay_A 0x67EDE0` is the kd-tree shape's ray slot.

## 10. Open items

- Thread of the camera and UI picking ray casts (`0x686FD0` callers through vtables): a probe counter per thread id would settle it. They do not use `0x6583E0`, which is sim-only in live runs (verify rerun 2026-10-16).
- Role of the 5 KB `0x6795D0` and of the hull helpers `0x67D480`/`0x67D6A0`/`0x67D920` (no direct callers) [inferred].
- Message handler on the simulator side for ids `0x5B57E39`, `0x5B57E61`, `0x5B575EF` (not traced).
