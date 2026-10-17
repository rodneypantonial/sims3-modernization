# 80. The lot-table window closer (`0x62EAB0`) and how to keep the H6 window open

Written 2026-10-17. Static only: Ghidra headless on a private copy of the project (`game-copy/ghidra-lt`, copied from `ghidra-rtb`, symbol CSV imported first), Steam 1.67 `TS3W.exe`. The game was not run. Builds on docs/18 (H6 plan, S0 results), docs/60 §1-§2 and the G11 measurement of 2026-10-16 (PLAN): IP movable 6.3 of 65.9 plan ms/s, BP 14.2 of 55.1; gate site `LotTable_GetOrInsert 0x62EAB0` had 6.2M entries in 10 min, 1.37M unlocked on the sim thread, 1.12M of them outermost (~1,860/s); every other gate site together ~10/s.

Tags as in AGENTS.md: **[verified]** = read in the disassembly or decompile (address given); **[inferred]** = reasoning still to confirm by a probe (§5). Scratch scripts (kept in the session scratchpad, not committed): a reverse walk from `0x62EAB0` over direct calls that skips call edges inside a `+0x918` window (window = first `Lock_Acquire` with `0x918` in the 20 instructions before it, to the last `Lock_Release`, as docs/60), and a 2-hop forward dependency scan that flags `Lock_Acquire` sites (with `ecx`), `RemoteCall_Dispatch 0x7D98C0`, `ScriptHost_InvokeDelegate 0xD75FA0`, runtime_invoke `0xEA1400`, the object id hash (`0x939560`/`0x9395A0`), `Object_GetComponent 0x935770`, physics quadtree reads (`0x83B3A0`, `0x682590`, `0x682610`) and writes to absolute globals. Limits: direct calls only (vtable calls counted, not followed), address-order windows.

## 1. Summary

- **What `0x62EAB0` is.** Find-or-insert in the lot table at `P+0x48` (P = lot planner `[world+0x95C]`). On a **hit it writes nothing shared** [verified, §2]. Insertions happen once per lot, so nearly all 6.2M entries are hits [inferred].
- **Who calls it.** 75 direct call sites in 68 functions; the reverse walk reaches 475 functions, 69 roots (68 `IC_*` plus `MonoScriptHost_Simulate`) and 39 vtable-only entry points [verified]. The hot ones fall in four families (§3): (A) **lot transform helpers** (8 tiny wrappers, `0x62EB40..0x62F070`, pure reads of the lot's 4x4 matrices), called per obstacle by Sim steering every tick; (B) **surface/room at a point** (`0xAB9580 -> 0x834B80`), called per footstep by the footstep audio handler and by placement tests; (C) **blocker check** `0x82E340` from a per-tick component update; (D) portal and footprint-lock-key ICs (rare writes).
- **Catch for family A [verified].** Every Sim-steering path calls `RoutingWorld_ObstacleQuery 0x82EDB0` (takes `+0x918`) or `GoalFootprintGather 0x82F0B0` (takes `+0x918`) **before** the transform call in the same function. During a job those acquires block anyway, so a lock-free `0x62EAB0` alone moves nothing on these paths. G11 counts the transform call as an outermost unlocked entry because the lock was already released when it runs.
- **Recommendation.** Before building anything, measure which event **ends** each window (closer attribution, §5 `vfG11CloseBy`) and the caller histogram at `0x62EAB0`. If families B/C close most windows: build a **lock-free hit path for the transform and lookup-only callers** (seqlock against worker inserts and rehash, §4 option 1), expected to move up to the share of windows those callers close today. If family A (steering) closes most windows, `0x62EAB0` is not the real closer: the fix is the docs/18 try-lock/snapshot for `0x82EDB0`, which is outside this doc. Queuing writes to the routing thread (the co-location idea) only fits family D, which is rare: gain near zero.

## 2. What `0x62EAB0` writes

| Case | Writes | Tag |
|---|---|---|
| Hit | `LotTable_Find 0x627E00` reads count `[tbl+8]`, buckets `[tbl+4]`, walks the chain through `node+0x190`, compares the 8-byte key at `node+0`; writes only the caller's stack iterator. `0x62EAB0` compares the result with the end slot `buckets[count]` (`0x62EAD8`) and returns `node+0x10` (`0x62EB35`). No refcount, no stamp, no LRU, no lazy build | verified (disassembly `0x627E00-0x627E68`, `0x62EAC9-0x62EB3C`) |
| Miss | Builds a default lot graph on the stack (`LotGraph_ctor 0x62AA60`, about 0x180 bytes, allocates its vectors), copies it (`0x62DE40`), then `LotTable_Insert 0x62E680`: allocates a 0x1A0-byte node (`0x62E5F0`, allocator from `[tbl+0x1C]`), key at `+0`, graph at `+0x10`, next `+0x190` = 0; rehash check `0x4E09B0`; optional `LotTable_Rehash 0x625940`; then `node+0x190` = bucket head (`0x62E710`), bucket head = node (`0x62E719`), count `+0xC` += 1 (`0x62E720`). Two `LotGraph_dtor 0x62B930` free the temporaries | verified |
| Rehash | New bucket array (`EA_AllocTagged`), pops every node off the old buckets one by one and pushes it on the new ones, frees the old array (`EA_Free`, when count > 1), then stores `[tbl+4]` and `[tbl+8]` | verified (decompile `0x625940`) |
| Lazy builds | Not reached from `0x62EAB0` itself: its only callees are `0x627E00`, `0x62AA60`, `0x62DE40`, `0x62E680`, `0x62B930` [verified]. `0x63CB90`/`0x63D2F0` run in callers (`LotPlanner_GoalRooms 0x63F490`, room ICs), not in families A-C | verified (callee list) |
| Node lifetime | Nodes are never moved by insert or rehash (only relinked), so `node+0x10` stays valid until the lot is removed (`LotGraph_Reset 0x62E420` from `RoutingWorld_RemoveLot 0x829D50`, under `+0x918`) | verified relink; removal semantics inferred |

`LotTable_Contains 0x628A90` (used first by most non-transform callers, e.g. `0x62FB60`, `0x62FB10`, `0x62F600`, `0x62F760`) is the same read-only find plus a reject of the key returned by `0xC88FD0` [verified]. So the `Contains` + `GetOrInsert` pair never misses.

## 3. Hot callers: paths, frequency, use of the result, class

Class: (a) sync read the sim needs now; (b) write the sim need not wait for; (c) reaches managed code / remote call / host call. No caller below reaches `0x7D98C0`, `0xD75FA0` or `0xEA1400` within 2 direct hops [verified, scan]; vtable calls inside them were not followed, so (c) is "not seen", not "excluded".

| # | Family / caller | Path from a sim-thread root (direct calls; vt = vtable entry) | Frequency class | Result use | Prior `+0x918` in same call? | Class |
|---|---|---|---|---|---|---|
| A1 | Steering, primary | vt `0x1026E10` `ObstacleAvoidance 0x847390` -> `RouteFollow_A 0x846B70` -> `0x62EF30` (call `0x846E09`, inside the per-obstacle loop) -> `0x62EAB0` | per tick x walking Sim x obstacle [inferred from the loop] | world-to-lot XY transform into the caller's stack (`entry+0xA0..+0xD8`) | yes: `0x82EDB0` runs first | a |
| A2 | Steering, second | `Locomotion_Update 0x83CED0` (from `MonoScriptHost_Simulate`) -> `Locomotion_UpdateAgent 0x957210` (agent state 1 or 4) -> `Locomotion_ObstacleList 0x85FFE0` -> `Locomotion_ObstacleCheck 0x85FD10` (per listed obstacle) -> `RouteFollow_EditPath 0x85F5A0` (calls `0x85F768`, `0x85F7A4`) -> `0x62EF30` | per tick x walking Sim x flagged obstacle; 2 calls each | same transform, start and end point | yes: `0x82EDB0` in `0x85FD10`, then `0x82F0B0` at the top of `0x85F5A0` | a |
| A3 | Steering, B variant | vt `0x8538B0` -> `0x852700` -> `ObstacleAvoidance_B 0x85D980` -> `0x85C590` -> `0x85B670` -> `RouteFollow_B 0x85B470` -> `0x62EF30` (`0x85B579`) | per tick x Sim using that avoidance mode [inferred] | same | yes (`0x82EDB0` first) | a |
| A4 | Script queries | `IC_Route_IsPointObstructedBySim 0x751480`, `GetObstructionAtPoint 0x74C8E0` / `GetDestinationObstruction 0x74C820` (-> `RouteFollow_A`), `FindObstaclesForSimAlongRoute 0x750430`, `SetAGS 0x755600` (-> `0x85FF30` -> `0x85F5A0`) | per script call | same | yes (`0x82EDB0`, or `0x82F0B0` via `0x85F5A0`) | a |
| B1 | Footstep surface | vt `0x1029664` `0x86C000` -> `0x8693E0` ("Audio/TempObjectList", "puddle") -> `World_SurfaceTypeAtPoint 0xAB9580` (flags `0x27`) -> `RoutingWorld_SurfaceAtPoint 0x834B80` -> `0x8341D0` -> `LotTable_GetIfPresent 0x62FB60` -> `0x62EAB0`; then `0x631FD0` -> `0x62FB10` -> `0x62EAB0` | per footstep x Sim [inferred from the audio strings]; thread not confirmed | reads lot bounds, routing-quadtree footprints at the point (`0x631FD0` -> `0x667480`), lot graph `+0x164`, cell graph vector `+0x74` | **no** (`0x8341D0` may take it later via `UpdateTerrainFootprints 0x831710`, only when terrain footprints are stale) | a |
| B2 | Placement and slot tests | `IC_World_TestObjectPlacementPQEI 0x796E90`, `FindGoodLocation 0x78AE70`, `FindGoodLocationAtCustomDepth 0x78B0A0`, `FindPlaceOnRoad 0x78B330`, `TestIfObjectsIntersect 0x794730`, `Slots_CanAttachToSlot(WithOffset)Impl 0x76E430/0x76EC40`, `GetFloorSurfaceTypeImpl 0x793A30`, `GetTerrainSurfaceTypeImpl 0x7862A0`, `GetInformationAboutRoomImpl 0x7933A0` -> ... -> `0xAB9580` or `0xC6C0C0` -> `0x834B80` -> `0x62FB60` | per script call x candidate point (placement searches loop over many points) [inferred] | same as B1 | no on the direct paths found | a |
| C1 | Blocker check | vt `0x1045A14` `0x970200` (thiscall with a float time step: a per-tick component update, role not identified) -> `RoutingWorld_BlockerCheck2d 0x82E340` -> `0x62EF30` (twice) and `0x631E20` (portal-check cache) -> `0x62ED40`; also `IC_RouteManager_RoutingBlockerCheck2d 0x74E110` | per tick x instance [inferred] | transform, then footprint/portal gather and physics range query `Realm_RangeQuery 0x682590` | no | a |
| C2 | Idle check | `IC_RouteManager_IsOkToIdleAtCurrentLocation 0x7556B0` -> `0x62FB60` | per script call | lot graph pointer | `0x82EDB0` also called (order not checked) | a |
| D1 | Portal writers | `IC_RouteManager_AddPortal 0x757E10` (-> `LotRouting_PortalsForObject 0x62F760`, `LotRouting_AddPortal 0x6379E0`), `SetPortalCost 0x758110`, `SetPortalAgeGenderFlags 0x758220`, `RemovePortals 0x74D6B0`, `Add{DivingBoard,WaterSlide,Platform,Stair}Portals 0x74B3B0..0x74B440`, `AddPortalLocationVariation 0x74D3F0`, `SetPortalLockStatus 0x74B490` | object placement, build mode, portal state changes [inferred rare] | mutate lot graph portal lists, `LotTable_MarkDirty 0x631AA0`; `0x62F760` copies portal pointers and AddRefs each through vtable slot 0 | no | b (but `AddPortal`, `RemovePortals`, `SetPortalLockStatus` return a value to script [verified signatures]) |
| D2 | Lock keys | `IC_RouteManager_Footprint{Set,Add,Remove,Has,Clear}LockKey(s) 0x74B540..0x74B680` -> `0x82B3E0` -> `0x82B2B0` -> `LotRouting_FindFootprintByKey 0x62F600` | interaction setup [inferred rare] | footprint lock-key map `+0x8BC`; `0x62F600` does plain `+1`/`-1` on footprint refcounts | no | b, except `Has` (a) |
| D3 | Room dirty | `IC_RouteManager_MarkRoomForUpdate 0x74B4D0` -> `0x635600` -> ... -> `0x630F70` (beyond 2 hops) | rare [inferred] | marks a room for rebuild | no | b (void) |

Not hot or not sim-thread: `Route_GoalStep 0x75C2D0` (the plan itself), save/bulldoze/houseboat/import ICs (`LotTable_MarkDirty`), build-tool vtables `0xCDxxxx`/`0xD1xxxx`, `Lot_Load`/`Lot_Save`, `RoutingDebug_DisplayWorldData 0x816D50` (main thread, debug flag), and `RoutingJob_Execute 0x81B2A0` (inside `PlanLocked`) [verified paths; frequency inferred].

**Ranking by likely count** [inferred, to confirm with §5]: A1/A2 (per tick x walking Sim x obstacle) > B1 (per footstep) and C1 (per tick x instance) > B2 (bursty, per placement search) > A4, C2 > D1-D3. Because A1-A4 already took `+0x918` earlier in the same call, the G11 window was closed by that acquire, not by `0x62EAB0`, whenever the acquire came first after the plan returned. The 1,860/s outermost count therefore overstates what a `0x62EAB0`-only fix can recover.

**Locks reached (transitive, 2 hops).** `+0x918` in `0x82EDB0`, `0x82F0B0` (A), `0x831710` (B, lazy only); route handle table `0x1004124` in `RouteHandle_Lookup 0x74B2C0` (A4); none in C1 and D within 2 hops [verified sites]. The `0x1004124` lock is a leaf and is never held while `+0x918` is taken (docs/18 S0), so the order stays `+0x918` first [verified, S0].

## 4. Design options to keep the window open

| # | Option | Size | Safety | Covers |
|---|---|---|---|---|
| 1 | **Lock-free hit path in the `0x62EAB0` gate stub**, sim thread, only for callers whose return address is inside the 8 transform wrappers (`0x62EB40`, `0x62EC00`, `0x62ECD0`, `0x62ED40`, `0x62EE00`, `0x62EF30`, `0x62F000`, `0x62F070`) and `0x62FB60`/`0x62FB10`/`0x628A90` lookups. A seqlock word owned by our detour is made odd around any miss branch (insert and rehash are only reachable from `0x62EAB0`: `0x62E680` and `0x625940` have one caller each [verified]) and around `0x829D50`. Reader: read seq (even), run our own copy of the `0x627E00` walk, re-read seq; any change, odd value or miss means fall back to the waiting gate. Wrap the walk in SEH, since a rehash frees the old bucket array under a reader | ~1 day: one stub branch, seqlock, SEH, verify counters | Hit path is safe against a plain insert: the node is complete before `0x62E710`, the bucket head is published after it at `0x62E719` (x86 store order) [verified order]. **Not** safe against a concurrent rehash without the seqlock (a reader sees nodes vanish, gets a false miss, and would insert a duplicate). Caller-side reads after the hit: transforms read `entry+0xA0..+0x118`, written at insert (`0x62DE40`) [verified] and otherwise not known (houseboat lots move: §5 write watch) [inferred]. B1/B2 then read the per-lot quadtree and cell graphs, which a job writes when it applies pending lot updates (`0x82C420`) or lazy-builds: unsafe unless the pending updates are applied on the sim thread before the job starts (docs/60 §1.4 item 5) | A (pointless, prior lock), B, C1, C2 |
| 2 | **Transform snapshot**: our own table of lot key -> the two matrices (`entry+0xA0..+0x11F`, 0x80 bytes), refreshed on the sim thread when no job runs; the 8 transform wrappers read it during a job | ~1 day: 8 small detours, invalidation on lot insert/remove/move | Safest: never touches EA's table during a job. Invalidation needs the matrix writers (unknown beyond insert) | transform callers only (A, C1, part of B) |
| 3 | **Warm the table** at lot load: call `0x62EAB0` for every lot in the world manager lot list on the sim thread, so a job never misses and never rehashes | small | Removes the rehash hazard for option 1 in practice; keep the seqlock as the guard | precondition for 1 |
| 4 | **Queue writes to the routing thread** (co-location idea) | medium: per-IC command records, ordering, read-your-writes for `FootprintHasLockKey` and portal reads | Only class (b) fits: D1-D3. Object ids must be resolved on the sim thread first (object id hash is sim-only, docs/45). `AddPortal`, `RemovePortals`, `SetPortalLockStatus` return values to script, so they cannot be fire-and-forget unless the script ignores them (not checked) | D only: rare, gain ~0 |
| 5 | **Per-tick cache of the caller's answer** (e.g. surface type per Sim position per tick) | medium per caller | Each answer depends on world state the job does not write except via pending updates; needs per-caller keys | B1 if footsteps dominate |

**Expected gain** [inferred]. Upper bound: the windows closed by family B/C entries that come before any `+0x918` acquire after a plan returns. If they were all of the 1,860/s, movable could rise toward the next closer (the per-tick `0x82EDB0` acquire of any walking Sim), so in a busy lot the window would still end within the same sim tick. Realistic gain is likely a few ms/s, far below the 65.9 ms/s plan time, unless steering stops blocking too. The closer attribution in §5 turns this into a number without a build: re-run the G11 accounting with the option-1 caller set treated as non-closing.

### 4.1 Dependencies per candidate (2 hops)

Hop 1 = what the candidate itself locks, waits on or calls; hop 2 = the same for each hop-1 item that would also have to move. "Movable" = could run on the routing thread holding `+0x918`. Items past hop 2 are listed as open, not followed.

**A. Steering (`RouteFollow_A 0x846B70`, `RouteFollow_EditPath 0x85F5A0`, `RouteFollow_B 0x85B470`)**

| Hop 1 | `+0x918` via `0x82EDB0` / `0x82F0B0`; `Object_GetComponent 0x935770` (object model); `0x5ECB30` writes global `0x1157D28`; lot transforms (`0x62EF30`); `World_LotLookup 0xC68990`; 20/16/6 indirect calls |
| Hop 2 | lot table find/insert; allocator `EA_Free 0x4E3F70` (thread-safe); world manager lot lookup (no lock, docs/60) |
| Verdict | **Stays on sim**: writes the Sim's steering state this tick, answer needed now |
| Open | the indirect calls; the `0x82EDB0` callees (`0x631F10` -> `0x62FB10`, quadtree reads) |

**B. Surface at point (`0x8693E0`, `0xAB9580`, `0x834B80`)**

| Hop 1 | `Object_GetComponent` (`0x8693E0`, `0x86B900`, `0x5A70E0`); resource manager lookup (`ResourceMan_GetManager 0x4AFD20` plus vtable `+0xC`); `World_LotLookup`; `0x8341D0`; `0x631FD0`; `LotTable_GetIfPresent 0x62FB60` |
| Hop 2 | `UpdateTerrainFootprints 0x831710` (`+0x918`, lazy write); object id hash in `0xAC8310`; `0x62FB10` (cell graph), `0x667480` (per-lot quadtree read) |
| Verdict | **Stays on sim**: the answer feeds the footstep sound and script now; object model reads are sim-only. Keep it on sim with option 1 (+ pending updates applied before each job) or option 5 |
| Open | thread of the footstep handler `0x86C000` (sim or audio, docs/35); `0x667480` internals; the resource vtable |

**C1. Blocker check (`0x82E340`)**

| Hop 1 | physics quadtree `Realm_RangeQuery 0x682590` (no lock, sim-written); `Object_GetComponent`; `0x82A780` (object id hash); lot transforms; `0x631E20` (portal-check cache); 19 indirect calls |
| Hop 2 | `0x62FB10`, `0x62ED40` (lot table) |
| Verdict | **Stays on sim** (physics quadtree and object id hash are sim-only; moving it needs the `0x83B3A0`-style proxy, which brings it back to the sim) |
| Open | role of `0x970200`; whether `0x631E20` writes its cache here without the lock |

**D1. Portal writers (`0x757E10`, `0x758110`, `0x758220`, `0x74D6B0`, `0x74B3B0..`)**

| Hop 1 | object id hash (`0x758110`, `0x758220`, `0x74D6B0`); `0x62F760` (portal pointer copy plus AddRef through vtable slot 0, plain refcount); `LotRouting_AddPortal 0x6379E0` / `LotRouting_RemovePortals 0x6399E0` (lot graph writes) |
| Hop 2 | lot transforms; `LotTable_MarkDirty 0x631AA0`; `0x631AF0`; `0x635EE0`; `0x82D2E0` (object id hash) |
| Verdict | **Movable with extra moves**: resolve object ids and AddRef the objects on the sim thread, hand pointers to the routing thread. Blocked for the three ICs that return a value to script until the script side is shown to ignore it |
| Open | the portal vtable calls; managed users of the return values |

**D2. Lock-key setters (`0x74B540..0x74B680`)**

| Hop 1 | `RouteManager_FootprintLockKeyById 0x82B3E0` (object id hash) |
| Hop 2 | `RouteManager_FootprintLockKeyApply 0x82B2B0` (`Object_GetComponent`, `+0x8BC` map without lock, docs/60; `0x62F600` refcounts) |
| Verdict | **Movable with extra moves** for Set/Add/Remove/Clear (void): resolve the object on the sim thread. `Has` stays on sim and must drain queued writes first |
| Open | `0x947FA0` (called by `0x74B540`) |

**D3. `MarkRoomForUpdate 0x74B4D0`**: hop 1 `0x635600`, hop 2 nothing flagged [verified scan]; verdict **movable** (void, write-only); open: the path from `0x635600` down to `0x630F70`.

## 5. Runtime checks (ts3native `[Verify]` style, observe only)

| Counter | What | Confirms |
|---|---|---|
| `vfG11CloseBy` | For each G11 window end: the closing event kind (`+0x918` acquire with its return address, or gate site plus caller return address) as a 32-bin histogram | the real closer; whether A (prior lock) or B/C close windows |
| `vfLtCallerHist` | At the `0x62EAB0` stub, sim thread, outermost unlocked: two return addresses (the wrapper and the wrapper's caller) | the frequency ranking in §3 |
| `vfLtHit` / `vfLtMiss` / `vfLtRehash` | per thread; miss = result equals the end slot; rehash = entries into `0x625940` | "nearly all hits"; rehash only at load |
| `vfLtMissInJob` | worker misses while a job runs | must be 0 after option 3 |
| `vfLtShadow` | option 1 dry run: during a job, do the lock-free find, then the gated one, compare pointers; count mismatches and seq retries | hit path safety (must be 0 mismatches) |
| `vfXformWrite` | hardware write watch (debug registers on the sim and worker threads) on `entry+0xA0..+0x11F` of the active lot | transforms are read-only after insert (houseboats included) |
| `vfLtPriorLock` | `0x62EAB0` entries where the same thread released `+0x918` since the last plan return | share of entries that cannot close a window |
| `vfFootstepTid` | thread id of `0x8693E0` entries | B1 is sim-thread work |
| `vfBlockerTick` | entries/s of `0x970200` -> `0x82E340` | C1 frequency |

Offline first: the closer attribution and caller histogram need no behaviour change and can extend the existing `/routelock` G11 probe (it already hooks all 12 gate sites); recompute "movable" with the option-1 caller set treated as non-closing before building option 1.

## 6. Risks

1. The 1,860/s figure mixes closers with entries after a prior `+0x918` acquire (§3). Building option 1 without `vfG11CloseBy` may move nothing [inferred].
2. Rehash under a lock-free reader frees the old bucket array [verified free in `0x625940`]; the seqlock plus SEH plus warming (option 3) is required, not optional.
3. B/C readers touch more than the lot table (per-lot quadtree, footprints, physics quadtree, object model). Option 1 makes only the table lookup safe; the follow-on reads need the pending-update fix or must keep waiting.
4. Vtable calls were not followed: managed or remote calls inside the hot callers are not excluded.
5. `0x62F760` and `0x62F600` change refcounts without atomics; they must stay behind the gate (never in the option-1 caller set).
