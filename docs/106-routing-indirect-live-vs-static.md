# 106. Routing indirect calls: live probe vs static map

Written 2026-10-18. Static and offline only: the game, the probe server and the test queues were not run, and no native code was built. This compares the first live run of the indirect-target sampler (docs/94 §7, `[ProbeSnap]`, Bridgeport and Isla Paradiso, 10 min each) with the static map of docs/102 (`docs/data/routing-indirect-sites.csv`), and then closes the gaps statically. It is part of PLAN "Routing off-thread" item 6: no gaps in our knowledge of the routing code before we replace the route search.

Inputs: `docs/data/snap-probe-bp-ip-2026-10-18.txt` (the `snap-report.ts` report: 454 sites, 721 pairs, only sites with a NEW or OUT target) and the two raw logs it was made from (`probe-out/queue/snap-probe-2026-10-18-bp-snap-probe-0947-ts3native.log`, `...-ip-snap-probe-0932-ts3native.log`, local only: 607 sites, 906 pairs, every pair). Capstone 5 over `game-copy/bin/TS3W.exe` (Steam 1.67) and `game-copy/analysis/functions.csv`; scratch scripts in the session scratchpad (`io-x/`). Ghidra was not needed. Tags as in AGENTS.md: **[verified]** = read in the disassembly; **[inferred]** = reasoning, with a ledger row in docs/51 §42.

## 1. Summary

- **No live target contradicts a fully resolved site** [verified]: all 253 pairs at the 145 statically resolved sites that fired are in the CSV targets column (cost function, sort comparator, switch tables, imports, constants, argument chains, data-flow receivers). Checks I-102-1 (only `RouteCost_Mode1..4`; `RouteCost_Default` never ran) and I-102-2 (only `0x64FE80`) pass.
- **82 pairs at 60 partly resolved sites are truly new** (the CSV listed other targets). None reaches managed code and none writes routing data outside the writer paths docs/83 and docs/94 already list (§4). They come from five static mistakes, not from unknown code: the **footprint classes have a second vtable** (an interface subobject at object `+4`, 11 vtables, docs/102 §2.4 missed it); the routing-path family was assigned to 21 sites whose receiver is a footprint, that interface, the resource manager or the script host; footprint-family sites in three ICs and the Lot code really call the `Lot`, script-host and Mono-host vtables; two A\* "node" sites call a **heuristic object**; the slot-signature guess at `0x636BF0` (12 sites) picked a non-routing class.
- **181 statically unresolved sites fired and are now resolved** (253 pairs, 120 targets; 121 of the sites are in the search group). 22 of them are non-lifetime search sites: 22 of the 31 in docs/102 §4.1 (§5).
- **Lifetime calls (I-102-5): none writes shared routing data** [verified per target], but the slot pattern is wrong at **20 of 148 lifetime-pattern sites that fired**: their `+0`/`+4` slot is a type query, a cost or heuristic function, a getter or the path reset, not AddRef/Release (§6).
- **1,212 CSV sites never fired** (raw logs; 1,371 in the report). 600 were never armed (imports and `[disp32]` forms the sampler skips, sites in the first 16 bytes of a function, code outside the probe's 12-hop closure). Of the armed ones, the ones that matter are a few flag-gated or event-gated paths: the A\* stamp reader (53 sites), the quadtree blocking-type count, the lot-table miss path and lot unload, the lot-manager index rebuild and build/buy edits (§7).
- **Nothing here changes a docs/91-105 conclusion**: the only managed exits seen live are the two known script-host slots in `World_FireEvent` (`0x78F2FC` -> `ScriptHost_InvokeDelegate`, `0x78F363` -> `0xD7D2F0`); the only routing-data writes behind the new targets are the lazy polygon cache (docs/83 W6a) and the footprint member list (`Footprint_GetOrCreateMember`, writer side). Two facts are new for the replacement search: footprints are also reached through their interface pointer (`object + 4`), and the in-room searches take a heuristic object (Euclid or zero).
- New tool `tools/indirect-live-vs-static.ts` does the join for later probe runs (§8). 56 names appended to the symbols CSV.

## 2. Method

1. Parse every `(site, target, count)` pair: the report format (`0x... in FUN...: N targets` blocks) or the raw `vcall SITE -> TARGET n=N` log lines (last cumulative count per pair, logs added).
2. Join on `site` with the CSV and class each pair:

| Class | Meaning |
|---|---|
| match | site resolved statically, target in its targets column |
| family | site partly resolved, target in its targets column |
| new | site resolved or partly resolved, target not predicted (truly new) |
| resolves | site unresolved statically: the live target is its first real answer |
| hint | site unresolved, target was in its closure-built hint list |
| no-row | site in the probe's table but not in the CSV (different closure) |

   A target outside TS3W.exe matches an import row that names that DLL (all 19 such pairs are `MSVCR80.dll` at import or `call reg` import sites).
3. For each target that is new, newly resolved, hint or no-row (157 distinct functions): the function (functions.csv, or a linear read to the first `ret` for the 79 entries functions.csv does not have: vtable-only functions Ghidra never made), every data reference to it, and **the receiver vtable** = reference minus the site's slot offset, kept when it is a vtable start (a store site, or the first of a run of code pointers). Forward direct-call closure (8 hops) against the 18 managed entries of docs/92 (`World_FireEvent`, `ScriptHost_InvokeDelegate`, `RemoteCall_Dispatch`, Mono invoke, ...) and against the 74 routing-writer roots of docs/94 §2.2, docs/102 §2.1 and docs/83 W1-W6.
4. Unfired sites: CSV rows with no live pair, split by whether `native/ts3native/vcallsites.h` (the 1,182-site table the probe armed) has them.

Limits: the probe samples the first 16 hits per site per 30 s window and has no thread or caller split, so a target at a shared site (allocator, job manager, scene graph, `World_FireEvent`) may come from code that is not routing. Receiver vtables are inferred from "which vtable has this target at this slot"; for small shared stubs (`return 1`, `return true`) several vtables fit, and the doc says so.

## 3. Counts

### 3.1 Pairs and sites per class

| Class | Pairs (report, 721) | Pairs (raw, 906) | Sites (raw) |
|---|---|---|---|
| match (resolved) | 206 | 253 | 145 |
| family (partly resolved) | 226 | 310 | 218 |
| new (truly new) | 65 | 82 | 60 |
| resolves (unresolved before) | 212 | 249 | 180 |
| hint (unresolved, in the hint list) | 4 | 4 | 3 |
| no-row (not in the CSV) | 8 | 8 | 6 |

A site can hold pairs of two classes (family and new). The report lists only sites with a target missing from the symbols CSV, so its counts are a subset; the raw logs are complete.

### 3.2 By basis (raw pairs at resolved and partly resolved sites)

| Basis (CSV note) | match | family | new |
|---|---|---|---|
| switch table | 124 | | |
| receiver class (data flow) | 52 | | |
| argument chain / constant | 51 | | |
| import | 19 | | |
| family | 7 | 256 | 39 |
| closure-built heuristic | | 52 | 21 |
| slot signature | | 2 | 22 |

I-102-6 (closure-built heuristic rows hold the live targets): 52 of 73 pairs, 16 of the heuristic sites that fired have a miss. The ≥ 95% expectation fails; §4 row E has the causes.

## 4. Truly new targets (60 sites, 82 pairs)

| # | Cause | Sites | Live receiver and targets [verified slot contents] | Reads / writes | Managed |
|---|---|---|---|---|---|
| A | Routing-path family assigned, receiver is a footprint | `0x620C5D`, `0x6644B4`, `0x807421`, `0x807443`, `0x8295F5`, `0x80E24F`..`0x80E403` (9 sites in `0x80E200`) | footprint `+0x90`/`+0x94` = `RefCounted_AddRefAt8`/`ReleaseAt8`; `+0x50` = `Footprint_GetOrCreateMember 0x807400` | refcount; `0x807400` adds a member to the footprint's list `+0x24` under lock `0x11D8F90` (pool `0x1165B80`); `0x80E200` is called only from `RoutingWorld_UpdateLotWalls` and `RoutingWorld_UpdateObjectFootprintCore` (and `0x75DE90`) paths (writer group) | no |
| B | Routing-path family assigned, receiver is the footprint interface | `Route_GoalStep` `0x75CC83` (+4), `0x75CC9A` (+8), `0x75CCBF`, `0x75CCF9` (+0x1C) | the second vtable of each footprint (`vt_FootprintIface_*`, subobject at `+4`, every slot a `this-4` thunk): `+4` `FootprintIface_GetOwnerId 0x620E60`, `+8` type getters `0x624ED0`/`0x620E50`/`0x626300`, `+0x1C` `FootprintIface_IsKind 0x620D20` or `Stub_ReturnFalse` | read only | no |
| C | Routing-path family assigned, other class | `0x4AEE6A`, `0x4AEEB6` (`0x4AECE0`); `IC_RouteManager_AddPortal` `0x7580DB` | `vt_ResourceMan 0xFB2DA0` (`ResourceMan_GetKeyName`, `ResourceMan_GetTypeIdByName`); `vt_ScriptHostBase 0x10078F0` `ScriptHandles_GetOrCreate` | resource lookups under the resource lock; script-handle table | no |
| D | Footprint family assigned, other class | `RouteHandle_Lookup 0x74B2F9`, `IC_RouteManager_AddPortalLocationVariation 0x74D454` (+0xCC); `IC_Route_SetLockKeys 0x74D1B2` (+0x68); `Lot_RoomLookup 0xA8E064`, `0xAB33B6` (+0x64), `0xB0A358` (+0x68); `0xB7CC9E` (+0x58); `WorldManager_FindLotByIdTyped 0xC62D62` (+0x40); `AStarSearch_ExtractPath 0x61DD67` (+0x1C) | script host `ScriptHandles_HandleToPtr`; `vt_MonoScriptHost 0x108B690` `MonoScriptHost_ArrayData 0xD71E10` (data pointer and count of a managed array: reads managed heap data, calls nothing); `Lot` vtable `0x1065268` getters `Lot_GetVec190`, `Lot_GetFloat1A0`; `0x106EE48` byte getter `0x8AB4F0`; `Stub_Return1` (Lot type check, docs/105); `mov al,1` stub on an A\* node class | read only | no |
| E | Closure-built heuristic missed | resource/package code `0x4B5A9B`, `0x5BB063`, `0x5BB086`, `0x5BB15C`; water A\* `WaterJps_EdgeCost 0x61FFE9` (+0), `0x620008` (+8); footprint interface `0x631471`, `0x631492` (`LotGraph_Build` helper), `Route_GoalStep 0x75D021`; footprint `+0x7C` `0x665C7F`, `0x6675C0`; `FootprintQuadtree_Leaf 0x681BD9` (+0x14); `0x958643` (+0x18); `0xB7C28F`, `0xB7C2AC`, `0xB7C45D` (+0x2C) | stream read `0x4DEFA0`, `ResourceMan_FindDatabase`; **`vt_WaterPathGoal 0xFEF3A8`** (built by `WaterPath_Plan 0x671930`: `+0` `WaterPathGoal_BothNear`, `+4` `WaterRoute_NearStartOrGoal`, `+8` `WaterPathGoal_Heuristic`); interface thunks as B; `Footprint_GetOwnerId`; animation transform controller (ctor `0x56B950`) and capsule shape `0xFEFCF0` in the physics loose quadtree; transform component getter `0x7F1620`; `0x106EE48` indexed getter `0xB71940` | read only (water goal: float math on its own fields) | no |
| F | A\* node family assigned, receiver is a heuristic | `AStarSearch_NodeState 0x61E155`, `0x61E23E` (+0, pattern "lifetime") | `vt_AStarHeuristicEuclid 0xFEBF48` -> `InRoomHeuristic_Euclid 0x61ECD0`; `vt_AStarHeuristicZero 0xFEBCC0` (built in `RoomGraph_FirstUse 0x63D2F0`) -> `AStarHeuristic_Zero 0x624870` | read only | no |
| G | Slot signature matched one non-routing class | 12 sites in `0x636BF0` (hanging footprints, lot build) | the footprint vtables and the member `0xFEBF90`: `+0x7C`/`+0x84` owner id, `+0x64` `Footprint_HasTypeBits`, `+0x58` `Footprint_HasMembers`, `+0x5C`/`+0xC0` constant stubs, `+0x40` `FootprintMember_GetFloat10`, `+0x2C` `Footprint_LevelPolygonQuery 0x80EF80` / `FootprintMember_FwdSlot2C` | `0x80EF80` calls `Footprint_EnsureLevelPolygons 0x80E5B0` (lazy polygon cache: docs/83 W6a, a known writer) | no |

What this changes:

- **Footprint interface vtables** [verified]: `0xFEADE0`, `0xFEB5D4`, `0xFEB6F8`, `0xFEB820`, `0xFEB94C`, `0xFEBA74`, `0xFEBB98`, `0xFEBCE8`, `0xFEBE10`, `0x1020AC4` and, for the member class, `0xFEBF6C`; 9 slots each, all thunks that subtract 4 and jump to a primary slot. `Route_GoalStep`, `RouteFollow_EditPath`, `RoutingWorld_SurfaceAtPoint` (`0x834CB5`) and the lot-graph helper `0x631050` hold footprints by this interface pointer. A replacement that takes footprints from the snapshot must accept both the object and `object + 4` (or translate at the boundary). No new class: same objects, second view.
- **Heuristic objects** (F): in-room A\* passes a heuristic object; the room graph uses a zero heuristic (plain Dijkstra), the portal gather (`0x633750`, from `LotEdgePortals_Gather` and `RoutingWorld_ApplyLotUpdate`) the Euclid one. The replacement must pick the same heuristic per search kind to give the same answers (docs/94 "same answer" check).
- **Water path goal object** (E): `WaterPath_Plan` builds a goal object whose `+0` tests "both points near start or goal" and `+8` gives the cell-to-goal estimate used by `WaterJps_EdgeCost`. Read only; docs/100 (water grid) did not list it.
- Docs/102 §2.4 family rule: "routing-path family" is over-assigned (21 sites in A-C). The CSV is the static record of docs/102 and is not edited; this doc and `tools/indirect-live-vs-static.ts` carry the correction.

## 5. Newly resolved sites (181 sites, 253 pairs incl. hint)

| Group | Sites | Live targets | Matters for the search |
|---|---|---|---|
| Lifetime pattern (`+0`/`+4`, delete) | 118 | AddRef/Release/deleting destructors of the portal (`vt_Portal`), lot quadtree (`0xFED994`), search path (`0xFEC058`), A\* nodes and node tables, footprint map entries, `RoutingPathImp`, plus engine refcounts; 13 of these pairs reach a non-lifetime slot (§6) | see §6 |
| Allocator interface | 14 | `ICoreAllocator_Alloc`/`AllocAligned`/`Free` on `0xFB8B38` (writer and resource code) | no |
| Footprint slots in lot build and obstruction (§4.1 #1, #2, #3, #5, #7) | 5 | `Footprint_GetTypeByte 0x624450`, `Stub_Return10`, member `+8` forward `0x806BF0`, `0x807090` (ignore-list test) | read only; same family |
| A\* setup callback (§4.1 #4, `0x62A665`) | 1 | constant predicates `0x620D60` (false), `0x7EAEA0` (true) | read only |
| Height and obstruction queries (§4.1 #8, #16, #17, `LooseQuadTree_RayNodeShapes`, `0x682B83`, `0xAC9939`) | 6 | `TerrainRay_A 0x67EDE0` (cell shape `0xFEFA3C`), box/capsule/mesh ray casts `0x682BE0`..`0x684E20`, `BoxShape_RayCast`, oriented box tests `0x6867D0`/`0x686A50` | read only; the snapshot replaces these reads (docs/91 rows 9-10) |
| Scene graph (§4.1 #18-20, `0xC5FAE0`) | 4 | type and child getters of `0x10641B0` (`0xAA9190`, `0xAA93C0`), `Stub_Return1` | read only |
| Object component (§4.1 #14, #15) | 2 | `Matrix_CopyVirtual 0x80E060`, `0x80DFB0`, `0x8C4110` | read only |
| Destinations (`0x752042`, `0x74FDD0`) | 2 | `RouteDest_Point_Generate`, `RouteDest_SlotArray_Generate`, `RadialRangeDest_GenerateGoals` (the known event 68 path, docs/92 §4, docs/105); per-type getters | reaches `World_FireEvent` and the lazy lot-table builds (known) |
| Job manager `0x59974F` (§4.1 list) | 1 | render cull/sort jobs, async loader jobs, `RemoteCall_JobFn` | shared site: these are other threads' jobs. The routing path to it (`Route_PlanLocked` -> `RoutingJob_dtor` -> `Job_Release` -> `Job_Cancel` -> `JobManager_ReadyJob` -> `JobManager_ExecuteJob`) only executes jobs with no function: `ReadyJob` calls `ExecuteJob` when `job+0x10` is 0, and `ExecuteJob` skips the call then [verified `0x599FC6`, `0x59972F`] |
| Allocator hooks (`0x4E626C`, `0x4E431F`) | 2 | `0xD71510` (new arena, at least 1 MB), `0x624590`, `0x5C32D0` | no |
| Footprint interface `+8` (`RouteFollow_EditPath` 6 sites, `RoutingWorld_SurfaceAtPoint 0x834CB5`) | 7 | `FootprintIface_GetTypeByte`, `FootprintIface_GetTypeConst`, `FootprintMemberIface_FwdSlotC` | read only; the interface of §4 |
| Resource, package and stream code | 15 | resource-manager and package-index functions (`0x5C2880`, `0x5B7240`, `0x5BFC50`, ...) | not routing: reached from the writer and portal groups through object and lot loading |
| Portal ICs (`0x74D46A`, `0x74D6FE`), `RoutingWorld_AddObject` (`0x82D872`) | 3 | portal type query `0x64E8B0`, Lot interface query `0xAB1CD0`, `Matrix_CopyVirtual` | read only |
| Engine function pointer `0x4F5966` | 1 | `0x4F65B0` (11 KB engine routine, no routing writer and no managed entry in its closure) | no |

22 of the 31 non-lifetime search sites of docs/102 §4.1 fired; the other 9 are in §7 (`0x6697B7`, `0xC66714`, `0xC6676F`, `0xC66786`, `0x59A603`, `0x931292`, `0x93117F`, `0x9311A3`, `0xAC5832`).

## 6. Lifetime calls (I-102-5)

200 pairs at 148 lifetime-pattern sites fired (65 targets). Every target was read:

- **Real lifetime functions (45 targets)**: AddRef (`inc`/`lock xadd`), Release (decrement, then slot `+8` delete with flag 1 at zero), deleting destructors, and empty `ret` Release of static objects (`0xC0FD60`). Their writes: the object's own count and fields, and free-list pushes. Three destructors push onto **global free lists without a lock** [verified]: `AStarNode_DeletingDtor 0x61AD00` (`[0x11CEF50]`), `AStarGoalNode_DeletingDtor 0x61EBD0` (`[0x11CEF90]`) and `LotQuadNodeList_DeletingDtor 0x668920` (`[0x11CF42C]`); `FootprintMapEntry_DeletingDtor 0x61ED10` takes lock `0x11CEFA8` and releases its footprint (`+0x94`). These are allocator pools, not routing data, but they are only safe while one thread runs the game's planner (today: the routing lock around `Route_PlanLocked`) [inferred, I-106-6].
- **Not lifetime (20 targets, 20 sites, 38 pairs)** [verified]: footprint `+0` = type query (`Stub_Return1`, `Stub_Return3`, member forward `0x626210`); footprint `+4` = agent cost scale (`Footprint_AgentCostScale*`, `FootprintMember_AgentCost`) or float getter `Footprint_GetFloat1C`; interface `+4` = owner id thunk; A\* heuristic `+0`; water goal `+0`/`+4`; destination `+0` = type id (`RouteDest*_TypeId`); allocator `+4` = `ICoreAllocator_AllocAligned` (`0x4E053A`); `RoutingPathImp` `+4` = `RoutingPathImp_Reset 0x820070` (clears the path object's own fields, `0x81B395`). All read only except the path reset, which writes the plan's own output object (docs/93), not shared routing input.
- **Verdict**: no lifetime-pattern target writes footprints, lot tables, quadtrees, the world graph or other shared routing input. The slot-pattern count of docs/102 §3.3 overstates lifetime calls: footprint and destination classes do not use `+0`/`+4` for lifetime.

## 7. Sites that did not fire

Raw logs: 607 of the 1,819 CSV sites fired; 1,212 did not. 600 of those were never armed:

| Not armed, why | Sites |
|---|---|
| `call [disp32]` forms (imports, `global-fnptr`), skipped by design | 263 |
| First 16 bytes of a function (detour space) | e.g. `Footprint_OwnerIsStairsHelper 0x64301E`, `0x6430FE`, `0x64318E`, `Footprint_OwnerLotLevel 0x627C4E`, `0x627D1E` (all footprint `+0x84` = owner id alias, now known), `WorldAStar_Search 0x676339` switch, `WaterJps_Step 0x61F75C` switch, `0x6248FB` |
| Outside the probe closure (12 hops, stops at lock and allocator; different writer roots) | mostly writer and portal sites, among them `0x671000` (4 sites, `WaterGrid_BuildCells` cell builder, docs/102 §4.1) |

Armed but never hit, by what they need (all search group unless noted):

| Path | Sites | Why it did not fire | Matters? | Would exercise it |
|---|---|---|---|---|
| `AStarSearch_StampReader 0x61C9B0` | 53 (all) | `QuadTreeCellAStar` calls it only when the search byte `+0x1C` is set (`0x61E7E1`) [verified]; meaning of the byte unknown | yes: inside the in-room A\* loop; its targets are family-resolved statically, but a replacement must know when the flag is set | find the setter of `+0x1C` statically (callers of `QuadTreeCellAStar`: `InRoomPlan`, `InRoomPlanToPoint`); then a repro that plans with it |
| `QuadLeaf_CountBlockingTypes 0x669530` (§4.1 #6) | 12 | `QuadTreeCellPos_C` calls it only when option bit `+0x50 & 0x20` is set and the leaf holds more than one footprint (`0x66B2B2`) [verified] | yes, same reason (cell class of a crowded leaf) | a plan with that option bit (agent option, not yet named) through a cluttered room |
| Rest of `InRoomPlanToPoint` (12), `QuadTreeCellAStar` (6), `LotQuadTree_CollectLeaf` (4) | 22 | branches of fired functions (goal-on-footprint, empty leaf) | low: family-resolved | point goals inside furniture clusters |
| Lot-table miss path: `0x62DE40`, `LotGraph_dtor 0x62B930`, `0x62F470`, `LotRouting_FindFootprintByKey` | 7 | first plan on a lot not yet in the lot table, eviction | medium: writes lot tables (known writer, docs/80, docs/86) | travel to a new lot or plan on many lots (lot-table churn); lot unload |
| `0xC66700` (§4.1 #9-11) | 4 | lot manager cell-index rebuild (dirty flag, docs/18 §5) | low: engine index, not routing data | place or move a lot object, then plan |
| Build/buy edits: `LotQuadTree_Insert`, `LotQuadTree_ApplyInsert 0x665DAD`, `LotRouting_QuadTreeBuild 0x62F9C1`, `0x635440 0x6354D7`, `RouteObstructionAtPoint` (2), `Footprint_EnsureLevelPolygons` `+0xF8`/`+0xFC` (plan and writer) | 9 | quadtree insert of new objects, multi-level footprints | medium: writer paths docs/94 already gates | a build/buy session with stairs and a second floor, then plans |
| `Job_Cancel 0x59A603` (§4.1 #13) | 1 | only for a cancelled routing job | low: job callback with the cancel opcode | cancel a route mid-plan (queue cancel during a plan) |
| Debug memory tracer `0x931292`, `0x93117F`, `0x9311A3` | 3 | off in release (flag `+0x56C`, docs/84) | no (dead in release) | none |
| `0xAC5832` (§4.1 list) | 1 | not reached in either world | low | unknown; leave for the next run |
| `Route_UnlinkPaths` `0x75732D`/`0x75733A`, `Route_PlanCommon 0x760F0A` (plan group) | 3 | path insertion and loop ICs | low: routing-path family | `IC_Route_InsertCustomPathAtIndex`-style script routes |
| Lifetime sites (search, armed) | 268 | temporaries on rare branches | low (§6 found no writer) | — |

## 8. Tool

`tools/indirect-live-vs-static.ts` (node, no dependencies):

- `node tools/indirect-live-vs-static.ts docs/data/snap-probe-bp-ip-2026-10-18.txt` (report) or the raw `ts3native.log` files (complete; several are merged).
- `--pairs` prints one line per pair with its class (filter with `--class new,resolves`), `--unfired` one line per unfired CSV site with "armed"/"not-armed" from `native/ts3native/vcallsites.h` (`--sites`), `--csv` another static map.
- `--self-check` tests both input formats, the cumulative-count rule, quoted CSV notes, module matching for out-of-exe targets and every class.

## 9. Files and names

- Ledger: docs/51 §42 (I-106-1 to I-106-8), with verdicts on I-102-1 to I-102-7.
- 56 names appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/106>`): the footprint slots (`Footprint_GetOwnerId 0x806FE0`, `Footprint_GetOrCreateMember 0x807400`, `Footprint_LevelPolygonQuery 0x80EF80`, `Footprint_AgentCostScale*`, ...), the interface vtables `vt_FootprintIface_*` and thunks, `vt_AStarHeuristicEuclid 0xFEBF48`, `vt_AStarHeuristicZero 0xFEBCC0`, `vt_WaterPathGoal 0xFEF3A8` and its slots, `Lot_GetVec190`, `vt_MonoScriptHost 0x108B690`, `MonoScriptHost_ArrayData 0xD71E10`, the A\* node and quad-node deleting destructors, `RoutingPathImp_DeletingDtor`/`_Reset`, `RouteDest*_TypeId`, `Stub_Return1`, `Stub_Return3`. The interface vtables `0xFEBB98`, `0xFEBCE8`, `0xFEBE10` (of `0xFEBBC0`, `0xFEBD10`, `0xFEBE38`) are verified by layout but not named.
