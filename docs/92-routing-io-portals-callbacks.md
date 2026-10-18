# 92. Routing I/O study B: portals, script callbacks, route parameters

Written 2026-10-18. Static only; the game was not run. Part B of the routing I/O study (PLAN item 6, snapshot design: our own search over our own snapshot, so the worker never touches game state). It maps the hard inputs of the route search: portals, every managed (script) call the search can reach, the route parameters the search reads, and dynamic obstacles.

Tools: Ghidra 12.1.4 headless decompiles on a private copy of the docs/89 project (`game-copy/ghidra-io-b`, copied from `ghidra-usf`, read-only runs); capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67): the direct-call caller index of docs/89, forward closures over direct calls (no depth cut: 60 hops) from `Route_PlanLocked 0x82C6E0` and from the four route ICs, a reverse closure (8 hops over direct callers) from every managed entry (`World_FireEvent 0x78F2B0`, `World_FirePortalLockedQuery 0x791C00`, `ScriptHost_InvokeDelegate 0xD75FA0`, runtime invoke `0xEA1400`/`0xEA1090`, the interpreter `0xE59FD0`/`0xE5A600`, the direct invoke wrappers `0xD7EED0`/`0xD7F210`/`0xD8A8E0`, `ScriptHost_RunTask 0xD7F000`, `ScriptHost_RegisterManagedObject 0xD77920`, `RemoteCall_Dispatch 0x7D98C0` and the remote-call post wrappers), and a vtable over-approximation (all 2,865 vtable bases found as immediates stored to memory; for each register-indirect call site in the closure, every vtable whose slot at that offset holds a function in the reverse closure). Script side: `game-copy/decompiled` (grep) and `tools/managed-callgraph.ts query` (docs/78). Scratch scripts stay in the session scratchpad (`io-b/`).

Tags as in AGENTS.md: **[verified]** = read in the disassembly, decompile or decompiled C# (address or file given); **[inferred]** = reasoning, to confirm with the probes in the docs/51 §25 rows.

## 1. Summary

- **All portals are one native class [verified].** Vtable `0xFED048` has four slots (AddRef `0x64E8E0`, Release `0x64E8F0`, deleting dtor `0x64F1A0`, type query `0x64E8B0`) and no virtual cost or allow method; the only two constructors are `0x64EEF0` (default, lot load) and `0x64EF70` (`LotRouting_AddPortal 0x6379E0`). Doors, stairs, ladders, elevators, pool ladders, diving boards, water slides, platforms, horse jumps, poles, subways, hover trains and tube elevators differ only in data: rooms, cost, type word, flags, age/gender/species (AGS) mask and per-AGS endpoint variations (§2). A snapshot can copy portals as plain records.
- **The search's per-portal decision has four inputs [verified, `Portal_LockState 0x64F2C0`]:** cost threshold (cost >= 1,000,000 is locked), the Sim's AGS against the portal mask and its variations, the route's forbidden-portal list, and, on any lot with the script flag `lot+0x8F` set (every loaded non-world lot), **one script call per portal, room pair and direction** (World event 59). Discouraged adds 500 to the edge cost; locked is cost 1,000,000 (§3).
- **The answers are not cached across plans [verified layout, lifetime inferred].** The cache `PortalLockCache_Find 0x64F1E0` lives in a structure on the stack frame of `LotPortalSearch 0x63EC40` (passed as `lea [esp+0x13C]` at `0x63F059`). Every plan asks script again for every portal it costs.
- **The search writes shared data with per-Sim answers [verified decompile `0x632460`].** Before planning inside a room, `InRoomPlan 0x632E40` (and `LotPlan_Inner` through `0x632630`) sets each room portal's tunnel footprint "locked" bit (`8`, footprint slot `+0xCC`) from `Portal_IsLocked` for *this* Sim, removing and reinserting the footprint in the shared lot quadtree when the bit changes. Our search must keep locked-portal blocking per search, not in shared data.
- **Managed bridges: two, not one [verified paths].** (1) World event 59 `OnPortalLocked`, reached from the search (`PortalEdgeCost 0x634500`, the in-room pre-pass `0x632460`) and from the goal step (`Route_GoalStep 0x75C2D0 -> 0x63F490 -> LotPortalSearch`); docs/18's bridge, now also confirmed for the goal step. (2) **New:** World event 68 `OnLotLevelInfoChange`, reached through a *virtual* call in goal building: `Route_PlanCommon -> 0x751FF0` calls each destination's slot `+0x10`; for a radial-range destination (vtable `0x100423C`) that is `0x75BE60 -> 0x75AFA0 -> 0x759B20`, which, when the route has `ForcePhysicsBasedRaycast` (`0x2000000`), calls `World_CanSeeObject 0x789DD0`, which sets the lot display level twice (`Lot_SetDisplayLevel 0xAC0570`, forced) and fires event 68 each time (`0x7920A0`). **Wrong on the last point (corrected 2026-10-18, see docs/105 §1 and ledger I-105-1): both `Lot_SetDisplayLevel` calls pass the quiet flag, which skips the notify block, so goal building never fires event 68; event 59 is the only managed exit of the four route ICs.** This is goal building, before `Route_PlanLocked`, so docs/18's search-only count stays right; it matters for A10 (docs/87 §10) and for any design that moves `Route_PlanCommon` off the sim. No other managed entry is reachable through direct calls (1,308 functions below `Route_PlanLocked`, 1,379 below the ICs), and the vtable over-approximation found no other routing-library candidate that survives a receiver check (§4).
- **Callback verdicts (§5): 12 decisions in 4 handlers.** 7 can be precomputed per (Sim, portal) with change hooks (b), 4 depend on the Sim's running interaction and must be evaluated per plan on the sim thread at submit time (b at submit), 1 (event 68) can be replaced natively (a) or left in sim-thread goal building (c) (corrected 2026-10-18, see docs/105 §1: event 68 is never fired from goal building, so this row needs no handling). None needs to run *during* the search. Mod risk is high: the lot's handler list and `Sims3GameplaySystems` itself are commonly replaced (simler90 core mod, MonoPatcher mods, NRaas GoHere door rules).
- **Route parameters (§6):** the route's AGS word `route+0x3C` (forced to a vehicle/carrier code by some options), the options object `route+0x158` (RouteOption bits at `+0x180`, RouteOption2 at `+0x188`, ignore list `+0x190/+0x194`, forbidden portals `+0x1C0/+0x1C4`, preferred portal `+0x1D0`, lock keys at `+0x160`), the destination set `route+0x144` (destinations, valid rooms), the follower (Sim id and position) or the given point. Option bits are turned into internal flags once, in `RoutingJob_Execute 0x81B2A0` / `0x81A340`; inside the search only one footprint filter tests a bit, and the quadtree collectors read the ignore list through the agent record (`0x8294E0`, `0x81C970`).
- **Dynamic obstacles (§7):** the search reads **standing Sims' footprints** (footprints of Sims without a locomotion track, kept in the lot quadtree by `UpdateSimFootprint 0x833560`), discourage and private footprints, filtered by `BlockedByPeople`/`BlockedByDynamicFootprints` and the ignore lists; and footprints near a point through `RoutingWorld_ObstacleQuery 0x82EDB0` (hop 8). It does not read moving agents' positions (no locomotion agent code in the closure) [verified direct calls]. The snapshot needs the footprint stream, not a per-Sim position feed.
- **Go/no-go for a pure snapshot search: GO, with three conditions.** (1) A per-plan **portal answer table** built on the sim thread at submit time replaces event 59 (answers for the start, goal and intermediate lots' portals, both directions); (2) a handler-set check (EA delegate target and invocation lists) that falls back to today's sync path when a mod adds or replaces handlers; (3) goal building either stays on the sim thread (today's IC frame) or does the radial line-of-sight test natively over snapshot geometry without the display-level change. Blockers left: the cost of the answer table (portals x 2 directions per lot vs 2-30 lazy calls today, §8), the `LockedDoorsDuringPlan` side effect, and the `lot+0x8F` flag clear on `NoListeners`.

## 2. Portals

### 2.1 Native record (class `0xFED048`)

Fields from the default constructor `0x64EEF0` and the readers `Portal_LockState 0x64F2C0`, `PortalEdgeCost 0x634500`, `0x64E910` [verified offsets; meanings as tagged]:

| Offset | Content | Read by | Tag |
|---|---|---|---|
| `+0x00` | vtable `0xFED048` | refcount, delete | verified |
| `+0x08/+0x0C` | owner object id (u64) | lock cache key, forbidden-list test, `ObjectService_FindById`, event 59 arg `portalObjectId` | verified |
| `+0x2C` | cost (float) | edge cost; `>= 1e6` (`[0xFED008]`) = locked by default | verified |
| `+0x34`, `+0x40` | portal type word, default `0xB828` = `PortalTypeRouteThrough` (47144); `AnimateThrough` = `0xB0C2` | not read by the cost function | verified default, role inferred |
| `+0x48/+0x4A` | room from / room to (u16, `0xFFFF` = none) | edge direction, lock cache key, event 59 rooms (through `0x6240C0`) | verified |
| `+0x58` | flag byte: bit 0 one-way (only from `+0x48`), bit 1 skips the second endpoint test | edge cost | verified bits, names inferred (`RoutePortalFlags` 1/2/4) |
| `+0x5C..+0x64` | vector of 0x38-byte location variations, AGS mask at `+0x30/+0x34` of each | AGS allow test | verified |
| `+0x68` | sub-object `0xFED028` | not traced | verified store |
| `+0x70` | reference count (plain; docs/90 SH makes it interlocked) | AddRef/Release | verified (docs/84, docs/90) |
| `+0x78/+0x7C` | AGS availability mask (u64, default all ones) | AGS allow test | verified |

Endpoints for a given Sim: `0x64E910 (portal, agentAGS, side, dir)` picks the variation matching the Sim's AGS (`0x624180` turns the native AGS word into a CAS mask) [verified call shape]. The lot entry holds portals per room (room record `0x44` bytes, portal vector at `+0x14/+0x18`) and per node id (`room << 16 | index`) [verified from `0x632460`, `0x634500`].

### 2.2 Creation paths

| Path | Entry | What it makes | Tag |
|---|---|---|---|
| Script `Route.AddPortal` | `IC_RouteManager_AddPortal 0x757E10 -> LotRouting_AddPortal 0x6379E0 -> 0x64EF70` | per-object portals with script start/end, cost, type, flags, AGS mask | verified |
| Script `Route.AddPortalLocationVariation` | `IC_RouteManager_AddPortalLocationVariation 0x74D3F0` | per-AGS endpoint variation (`+0x5C`) | verified name, store inferred |
| Script helper portals | `IC_RouteManager_AddPlatformPortals 0x74B410` (kind 0), `AddStairPortals 0x74B440` (3), `AddDivingBoardPortals 0x74B3B0` (5), `AddWaterSlidePortals 0x74B3E0` (6) -> `0x82D2E0 (objId, kind)` | portals between the object's helper slots: names `PoolHelper`, `PoolLadder`, `StairsHelper`, `PlatformHelper`, `FountainHelper` | verified ICs, kinds and strings; kind-to-slot mapping inferred |
| Batch re-add | `0x830250` (world `+0x974` set) -> `0x82D2E0` kind 0 for each pending object | deferred helper portals | verified |
| Doors | `0x80CB40` (slot of vtable `0xFEB948`, a door routing component) | from `RoutingSlot_Door%i_Front` / `_Rear` slots | verified strings, owner class inferred |
| Lot rebuild | `RoutingSystem_RebuildLotRouting 0x835110` / `0xAC79E0` -> `0x832890` | re-adds existing portals (`Routing/existingPortals`) after a lot footprint rebuild | verified |
| Lot load | `RoutingWorld_LoadLotData 0x82CA10 -> 0x63C8E0 -> 0x6236D0 -> 0x62C920 -> 0x64EEF0` | deserialized portals (`Routing/Portal`) | verified |
| Ocean owner | `IC_RouteManager_SetOceanPortalOwner 0x74B470 -> 0x829520` | stores the ocean owner id at world `+0x988/+0x98C` (no portal record) | verified |
| Script cost and AGS edits | `Route.SetPortalCost`, `Route.SetPortalAgeGenderFlags` (door component, velvet ropes, giant boulder) | `+0x2C`, `+0x78` | verified script calls, native store inferred |

### 2.3 Portal types the search can traverse

`PortalType` has two values (`PortalTypeRouteThrough` 47144, `PortalTypeAnimateThrough` 45250); `RoutePortalFlags` are `OneWayPortal 1`, `RabbitHoleEntry 2`, `RabbitHoleExit 4` [verified `SimIFace`]. Script portal components (26 classes deriving from `PortalComponent`) and what decides traversal [verified C# files; EP attribution inferred from the object families]:

| Script type | Created by | Type / flags / cost | Allow decision in the search | EP (inferred) |
|---|---|---|---|---|
| `Door` (and `DoorSingleGender`, `Turnstile`, `MysteriousDeviceDoor`) | native door slots (`0x80CB40`); cost and AGS from `DoorPortalComponent` | RouteThrough, cost from the door | **script**: `CommonDoor.IsAllowedThrough` (+ overrides) through event 59 | base, Resort (VIP), Store (turnstile), ITF |
| `NonAnimatedDoor`, `SecretDoor` (derive from `CommonDoor`, not `Door`) | `Route.AddPortal` | RouteThrough, `Owner.PortalCost` + variations | native data only: `Sim.OnPortalPathPlan` tests `is Door`, so these are never asked | various |
| `Stairs` | `Route.AddStairPortals` (kind 3) | helper-slot portals | native data only | base |
| `StaircaseSpiral` | `Route.AddPortal` | AnimateThrough, cost 2, one-way pair + variations | native data only | store / later EP |
| `Ladder` | `Route.AddPortal` x2 | AnimateThrough, one-way, `kPortalCost` 1 | **script**: discouraged for elders and ladder-avoiding traits | WA |
| `FirefighterPole` | `Route.AddPortal` x2 | AnimateThrough, one-way, down/up costs | **script**: locked for toddlers and below, or carrying a child | Ambitions |
| `ElevatorDoors` | `Route.AddPortal` | RouteThrough, cost 3 | **script**: locked for toddlers, horses, wild animals; apartments (`HasVirtualResidentialSlots`) need `CanTravelInElevator` | Late Night |
| `ElevatorInterior` | `Route.AddPortal` (6 one-way pairs per floor pair) | AnimateThrough, `kElevatorToElevatorPortalCost` | native data only | Late Night |
| `TubeElevator` | `Route.AddPortal` x3 | AnimateThrough, one-way | native data only | ITF |
| `Subway`, `HoverTrainStation` | `Route.AddPortal` (with AGS flags) | AnimateThrough, one-way, cost 1; subway needs `EnableSubwayPlanning`, hover train `EnableHoverTrainPlanning` | native data + options | Late Night, ITF |
| `BoxStall` | `Route.AddPortal` + variation (human AGS mask on the main portal) | RouteThrough, cost 0 | **script**: open only while the Sim's running interaction targets this stall | Pets |
| `HorseJump` | `Route.AddPortal` (one two-way, one one-way) | AnimateThrough, cost 0 | **script**: open only while the current or linked interaction is a horse-jump interaction | Pets |
| `TrainingPosts` | `Route.AddPortal` | RouteThrough, cost 0 | native data only | Pets |
| Pools: `PoolLadder`, pool edge (`PoolHelper`) | helper portals (kinds 1/2, inferred) | helper-slot portals | native data only (`Pool.IsAllowedThrough` and `PoolLadder.IsAllowedThrough` exist but are not called by the plan callback) | base |
| `DivingBoard` | `Route.AddDivingBoardPortals` (kind 5) | helper portals | **script**: locked for toddlers and below or visibly pregnant Sims (only if not already locked) | base |
| `WaterSlide` | `Route.AddWaterSlidePortals` (kind 6) | helper portals | native data only | IP / store |
| `Platform` | `Route.AddPlatformPortals` (kind 0) | helper portals; options `AllowGoalsToLeavePlatforms`, `PlatformsNotBlocking`, `DisallowGoalsOnThisPlatform` | native data + options | IP |
| `Ocean` | `Route.SetOceanPortalOwner` | world-level water legs (`WaterPath_Plan2 0x656A70`), boats through RouteOption2 | native data + options | IP |
| `Houseboat`, `HouseboatJig` | no route API in their components | no search portal [inferred]; boarding is script | IP |
| `Trampoline`, `SlidingBookcase`, `VelvetRopes1x1/2x1`, `GiantBoulderPathBlocking` | `Route.AddPortal` | costs `kPortalCost` 10 / 2 / door cost / 100,000 (below the 1e6 lock threshold, so "very expensive", not locked) | native data only; velvet ropes and boulder change cost through `SetPortalCost` | Generations, Late Night, WA |
| any portal on a lot with an active `PrivacySituation` | (situation handler) | — | **script**: locks portals into the privacy target's room for most interactions | base |

No class in the list belongs to University or Showtime [inferred from the object families]; their routing changes would be options or objects without portals.

## 3. What the search reads per portal

### 3.1 Edge cost `PortalEdgeCost 0x634500` [verified decompile]

`thiscall(search)(lotState, nodeFrom, nodeTo, goalLo, goalHi)`: both nodes inside one portal: (1) one-way: allowed only from room `+0x48`; (2) `Portal_IsLocked` for the direction(s) used -> locked returns 1,000,000 (`[0xFEB5B0]`); (3) endpoints for the Sim (`0x64E910`) and a point test `0x6338A0` (`"Routing/PortalCheckCache"`, room graph node and a cell test) on each; (4) `Portal_IsDiscouraged` either direction adds 500 (`[0xFEB5B4]`); (5) cost = `+0x2C` + penalty, or 1 if the portal's owner is the goal object, or `4 x cost` + penalty when a goal id is given. Portal-to-room and room-to-portal edges use the endpoint distance with a level term (`9 x dLevel²`, `[0xFBBFF0]`) or the room centre cost `0x624810`, plus the discourage penalty.

### 3.2 Lock state `Portal_LockState 0x64F2C0` [verified decompile]

`thiscall(portal)(lotState, direction)`, cached per (owner id, rooms) with one byte per direction:

1. Default: locked if cost >= 1e6, else unlocked.
2. AGS: the Sim's native AGS word (`agent+0`) mapped to a CAS mask (`0x624180`); unless the species test `0x55DA60 (ags & 0xCF00)` passes, a mask not covered by `+0x78` and by no variation's mask -> locked.
3. Forbidden list: `0x8294E0 (agent)` returns the agent's options object (`agent+0x28`), `0x81C9B0` tests the portal owner id against the forbidden-portal vector (`options+0x68/+0x6C`) -> locked.
4. Script: `ObjectService_FindById (owner)`, the owner's lot component (`[0x11ECF08]`) gives the lot id, `0x62FB60` the lot record; if `lot+0x8F` is set: `World_FirePortalLockedQuery 0x791C00 (lotId, routerId = agent+8, portalId, roomFrom, roomTo, initialLocked)`. Answers 1 locked, 2 unlocked, 3 discouraged; 0 (`NoListeners`) or > 3 clears `lot+0x8F` and keeps the native answer.

The agent record (`lotState+0x40`) carries the Sim's AGS word (`+0`), its object id (`+8/+0xC`) and its options object (`+0x28`). The cache map is at `lotState+0x20/+0x24/+0x28`; `lotState` is built on `LotPortalSearch`'s frame, so its lifetime is one lot search [verified address, lifetime inferred].

### 3.3 Shared writes during the search [verified decompile `0x632460`]

For each portal of the room (`room+0x14..+0x18`), on a lot found by `LotTable_Contains`/`GetOrInsert`: find the portal's footprint (`0x64E870`, `0x6649C0`); if none and `[0x11ECBC4]` is set, mark the lot dirty and rebuild (`LotTable_MarkDirty`, `0x630F10`, `0x631AF0`); else compare footprint slot `+8` with `Portal_IsLocked ? 8 : 0` and on a change remove (`0x6675B0`), set (`+0xCC`) and reinsert (`LotQuadTree_Insert 0x666C50`). So two Sims with different door permissions planning on the same lot rewrite the shared quadtree in turn. A snapshot search must apply locked portals as a per-search overlay; it must never write this bit.

## 4. Managed reach of the search

| Check | Result | Tag |
|---|---|---|
| Direct closure of `Route_PlanLocked` (1,308 functions) | one managed entry: `World_FirePortalLockedQuery` at `0x64F4D3` in `Portal_LockState` (path `0x64F550 <- 0x634500 <- 0x63EC40 <- 0x640E00 <- 0x65E0D0 <- 0x661B50 <- 0x81B2A0`) | verified |
| Direct closure of the four ICs (1,379 functions) | the same entry, also through the goal step (`0x63F490 <- 0x75C2D0 <- 0x7606E0`) | verified |
| Reverse closure of all managed entries (3,395 functions, 8 hops) intersected with both closures | only the portal-lock chain (`0x64F2C0`, `0x64F550/570`, `0x632460`, `0x634500`, `0x632630`, `0x632E40`, `0x63E340`, `0x63EC40`, `0x63F6E0`, `0x633FA0`, `0x63F490`, `0x640E00`, `0x6410B0`, `0x63DF60`, `0x75C2D0`, `0x65E0D0`, `0x656A70`, `0x7606E0`, `0x661B50`, `0x7641D0`, `0x7642C0`, `0x81B2A0`) | verified |
| Indirect calls in the planner closure | 858 sites: 103 through import slots, 254 slot `0` and 144 slot `+4` (refcount release and deleting destructors), 357 other slots, 50 not resolved to a slot | verified count |
| Vtable over-approximation (any of 2,865 vtables whose slot at the call's offset reaches managed code) | 264 (slot, vtable) pairs for the planner, 267 for the ICs; nearly all UI and game classes (`0xC.....`-`0xD.....` targets). Routing-library candidates: route vtable `0x10047CC` slot `+8` = `Route_OnRouteEvent 0x7521B0` (event 57); radial destination `0x100423C` slot `+0x10` = `0x75BE60` (event 68, §1); locomotion class `0x102727C` (`0x84CF00`/`0x8538B0`: lot membership and `IsOkToTurnFaceToDestination` 60); camera class `0x1045CF8` slot `+0x5C` (`0x975EE0`, telemetry 75); class `0x1029060` slot `+0x14` (`0x8690D0`, remote call) | verified list |
| Receiver check of the routing candidates | `+8` sites in the planner are footprint/quadtree code (`0x626F40`, `0x6271A0`, `0x631050`, `0x632630`, `0x636BF0`, `0x667A10`, `0x667DB0`, `0x6681E0`), and every route-object call in the IC and `Route_PlanCommon` frames uses only `+0x10`/`+0x14` (AddRef/Release) -> `Route_OnRouteEvent` not reached; locomotion, camera and remote-call classes are not routing data; the radial destination is reached, but only from goal building (`Route_PlanCommon` calls `0x751FF0` with `ecx = route+0x144` at `0x76099F`; it iterates the list at `+0x18` and calls each destination's slot `+0x10`) | receivers inferred from the code family; `0x751FF0` verified |

The event 68 chain [verified addresses]: `0x75BE60` (radial goal generation: target `FindById`) -> `0x75AFA0` -> `0x759B20` (goal point validation; the `World_CanSeeObject` call at `0x75AC22` sits behind `RoutingOptions_Test(0x2000000)` at `0x75AB9C` and `0x74CB80`) -> `0x789DD0` (`World_CanSeeObject`, also behind `IC_World_CanSeeObjectImpl`, UNSAFE in `ic-safety.csv`) -> `Lot_SetDisplayLevel 0xAC0570` at `0x789EB5` and `0x78A218` (forced: the "same level" early out is skipped) -> `0x7920A0` -> `World_FireEvent(0x44)`. C# side: `LotManager.OnLotLevelInfoChanged` (`Lot.OnLevelChanged`) and `HudModel.OnLotLevelInfoChange` (`LotChanged` on the active lot, UI). Whether the restore call fires a second time depends on a flag set by the first [inferred]. (corrected 2026-10-18, see docs/105 §1 and ledger I-105-1: the chain ends at `Lot_SetDisplayLevel` without firing; the quiet flag skips `0x7920A0` and `World_FireEvent(0x44)`, so no event 68 fires from goal building.)

## 5. Script callbacks: what each reads, verdict, mod risk

Verdicts: **(a)** rewrite natively over snapshot data; **(b)** precompute per (Sim, portal) and refresh on change; **(b-submit)** evaluate per plan on the sim thread when the plan is submitted, before the worker starts; **(c)** must stay a sim-thread call.

Chain [verified]: event 59 -> `LotManager.PortalLockedCallback` (two `ObjectFromId`, the lot) -> `Lot.OnPortalPathPlan` (new `PortalPlanEventArgs`, `GetInvocationList`, each `PortalPathPlanHandler` in a try/catch; Locked beats Discouraged; no handler = `NoListeners`). Vanilla subscribers: `Sim.OnPortalPathPlan` (added for every lot at load, `Lot.cs:4031`) and `PrivacySituation.OnPortalPathPlan` (while a privacy situation runs on the lot) [verified: the only two `PortalPathPlan +=` sites]. Managed closures (docs/78 tool, depth 12): `Sim.OnPortalPathPlan` 344 methods, worst a delegate (`List.Find` predicate in the VIP lookup), 13 PROXY ICs (slot parent ids for `LotCurrent`), one SAFE IC; `PrivacySituation.OnPortalPathPlan` 44 methods, worst a virtual (`OtherSimCanIgnorePrivacy`).

| # | Decision (handler) | Reads | Writes | Verdict | Mod risk |
|---|---|---|---|---|---|
| 1 | Dispatcher (`LotManager.PortalLockedCallback`, `Lot.OnPortalPathPlan`) | lot by id, handler list | allocations only; `NoListeners` makes native clear `lot+0x8F` | (a) for the vanilla handler set; (c) when the set differs | **high**: any mod can add a `Lot.PortalPathPlan` handler; core mods replace the class |
| 2 | Tomb mummy -> locked (`Sim`) | `SimDescription.IsTombMummy` | — | (b) per Sim | medium (core mods) |
| 3 | Proprietor on own venue -> unlocked (`Sim`) | assigned role, role object's lot, portal object's lot | — | (b) per (Sim, lot) | medium |
| 4 | Door (`CommonDoor.IsAllowedThrough`; `DoorSingleGender`, `Turnstile` overrides; only for `Door` subclasses) | hidden rooms, service type, stray and greeted state, one-way room, horse and gate type, fire on lot (pets, barn doors), `IgnoreDoorLocks` flag, lock type and owner (actor, household, VIP room data and resort state, pets, owner list); gender rule on resort/residential/open venues; turnstile: tickets and carried child/pet | `SimRoutingComponent.LockedDoorsDuringPlan.Add(door)` | (b) per (Sim, door) with invalidation on `World.DoorLockStateChanged`, household, role, greeted, fire, resort state; turnstile (b-submit) | **high**: NRaas GoHere changes door filters, costs and time locks (docs/03); simler90 core replaces `CommonDoor` |
| 5 | Firefighter pole (`FirefighterPole.IsAllowedThrough`) | toddler-or-below, `CarryingChildPosture` | — | (b) per Sim (posture change event) | low |
| 6 | Box stall (`BoxStall.IsAllowedThrough`) | the Sim's running interaction (`IAllowUseOfGateInteraction` targeting this stall) | — | (b-submit) | low |
| 7 | Horse jump (`HorseJump.IsAllowedThrough`) | current or linked interaction is `IHorseJumpInteraction` | — | (b-submit) | low |
| 8 | Elevator doors (`ElevatorDoors.IsAllowedThrough`) | toddler, horse, wild animal; lot apartment slots + `CanTravelInElevator` | — | (b) per (Sim, lot) | medium (apartment mods) |
| 9 | Diving board (`Sim`) | toddler-or-below, visibly pregnant | — | (b) per Sim | low |
| 10 | Ladder discourage (`Ladder.DiscourageSimFromUsing`) | elder, ladder-avoiding traits | — | (b) per Sim (trait change) | low |
| 11 | Privacy (`PrivacySituation.OnPortalPathPlan`) | target room and lot, the Sim's current or transition interaction (autonomous, priority), virtual `OtherSimCanIgnorePrivacy` | — | (b-submit), only for lots with a live privacy situation | medium (subclasses override the virtual) |
| 12 | Event 68 from radial goal line-of-sight (`World_CanSeeObject`) (corrected 2026-10-18, see docs/105 §1: event 68 does not fire here, quiet flag; row needs no handling) | physics rays at the object's level | lot display level twice; C# `Lot.OnLevelChanged`, HUD lot refresh | (a) do the line-of-sight test over snapshot geometry without the level change, or (c) keep goal building on the sim (today's behaviour) | low (UI handlers) |

So **no callback has to run during the search**: every answer is fixed for the duration of one plan (the Sim and the portal objects do not change while the sim thread waits for or hands off the plan), and the vanilla game already re-asks per plan (§3.2). The (b) rows can be cached across plans only with the listed change hooks; until those are built, (b-submit) for all rows is the safe start.

## 6. Route parameters per IC

Route object fields [verified from the setter ICs and `Route_PlanCommon 0x7606E0`]:

| Field | Content | Set by | Read by |
|---|---|---|---|
| `route+0x3C` | native AGS word | `Route_SetAGS 0x755600`; `Route_PlanCommon` overrides it with `0x800`/`0x900`/`0xA00`/`0xB00`/`0x4400` when RouteOption bits `0x20000`-`0x100000` (high word: flier, walking dog, walking little dog, leading horse) or RouteOption2 `0x20` (shark) are set | goal building `0x751FF0`, goal step, `Route_PlanLocked` (arg), agent record `+0` |
| `route+0x40/+0x44` | two words copied with `+0x3C` into the goal step record | — | `Route_GoalStep` |
| `route+0x144` | destination set (class `0x1004448`, made on demand by `0x752130`): destination list `+0x18`, valid rooms vector `+0x20/+0x24`, flag `+0x30`, valid-rooms lot `+0x38/+0x3C` | `Route_Add*Destination`, `Route_SetValidRooms 0x7635F0`, `Route_ClearDestinationList` | goal building (each destination's slot `+0x10`), goal step |
| `route+0x158` | options object O | — | passed to goal building, the goal step and `Route_PlanLocked` |
| `O+0x08` (`route+0x160`) | lock keys | `Route_SetLockKeys 0x74D160 -> 0x81D160` | footprint lock-key filter [inferred] |
| `O+0x28/+0x2C` (`route+0x180`) | RouteOption (u64) | `Route_SetOption 0x7543C0 -> 0x81C810` (also pushed to each follower path object, slot `+0x14`) | `RoutingOptions_Test 0x81C880` |
| `O+0x30/+0x34` (`route+0x188`) | RouteOption2 (u64) | same, `set2` = 1 | same |
| `O+0x38/+0x3C` (`route+0x190/+0x194`) | ignore-for-route object ids (vector of u64) | `Route_Add/Remove/ClearObjectToIgnoreForRoute` (`0x81C8D0`) | `0x81C970`: quadtree collectors (`0x667DB0`, `0x6681E0`, `0x669230`, `0x669530`, `0x669850`, `0x66B090`), `RouteCost_Dispatch 0x627780`, `0x627540`, `0x62FBE0`, `RouteObstructionAtPoint 0x655880`, goal validation `0x759B20` |
| `O+0x68/+0x6C` (`route+0x1C0/+0x1C4`) | forbidden portal owner ids | `Route_AddObjectToForbiddenPortalList 0x762B20` (`0x81C8F0`) | `Portal_LockState` (`0x81C9B0`) |
| `O+0x78/+0x7C` (`route+0x1D0`) | preferred portal id | `Route_SetPreferredPortal 0x74D3A0 -> 0x81C900` | not traced [inferred: portal search] |
| `route+0x1F4` | replan counter | `+1` by `Replan`, `ReplanFromPosition` | not traced |
| `route+0xE0` | path list (`Route_PlanLocked` output) | cleared by `Route_PlanCommon` | — |

The ignore-for-route-end-turn list is not read by the search closure [inferred: its IC writes another vector; no reader found among the `0x81C970` callers]. **Posture and walk style are not route parameters of the search** [inferred: no setter IC for them; walk style travels in `RouteAnimationSpec` to route following]; posture matters only through the AGS override and the callbacks (carrying a child, §5 rows 5 and 4). RouteOption bits are converted to internal planner flags once: of the 49 `RoutingOptions_Test` calls in the planner closure, 34 are in `RoutingJob_Execute 0x81B2A0` and 14 in `0x81A340`, and only one (`LotQuadTree_CollectLeaf 0x669850`) is inside the search [verified]. The planner reads RouteOption `PassThroughWalls`, `PassThroughObjects`, `BlockedByPeople | BlockedByDynamicFootprints`, `UseMostDirectPath`, car options (`0x10`-`0x80`), `ReplanToFindObstacleWhenPathPlanFails`, `CreateSubPaths`, `ForceUseMainEntrance`, `RouteAsGhost`, stroller options, `RouteAsLargeAnimal`, `RouteWhileMounted`, `IgnoreSidewalkAndLotRestrictions`, the carrier options, `EnableUFOPlanning`, `EnableWaterPlanning`, `EnablePondPlanning`, and RouteOption2 boat, shark, mermaid and lifeguard bits; the IC closure adds RouteOption2 `0x200`/`0x400` (follower orientation), `DisallowGoalsOnRoads`, `DisallowGoalsOnBridges`, `AllowGoalsToLeavePools` and `IgnorePortalTunnelFootprintsAtDestination`; radial goal validation (reached only through the destination's slot `+0x10`, §4) adds `ConstrainGoalsToSingleLevel` and `ForcePhysicsBasedRaycast` [verified constants].

Per IC [verified disassembly of the IC bodies; docs/87 for the async split]:

| IC | Arguments | Start points | Extra reads | Then |
|---|---|---|---|---|
| `Route_Plan 0x765160` | handle | `Route_StartPointsFromPosition 0x7641D0 -> 0x75D920`: the follower's position, room and level (`ObjectService_FindById` on the follower) | — | `Route_PlanCommon` (goals, goal step, `Route_PlanLocked`) |
| `Route_Replan 0x765490` | handle | same as Plan | `route+0x1F4` += 1 | same |
| `Route_PlanFromPoint 0x765240` | handle, x, y, z | `Route_StartPointsBuild 0x7642C0` from the point | `World_LotLookup (x, z)` in the IC; `0x74BE50` lot/room of the point; follower orientation when RouteOption2 `0x200`/`0x400` (`0x752480`); `0x654350` | same |
| `Route_ReplanFromPosition 0x765550` | handle, position | `Route_StartPointsBuild` from the position | `route+0x1F4` += 1; same point reads as above | same |

Common to all four: the route handle table (`RouteHandle_Lookup 0x74B2C0`, the route's AddRef/Release slots `+0x10`/`+0x14`), the destination set and options above, the Sim (agent record: AGS, id, options), and the routing world under `+0x918`.

## 7. Dynamic obstacles

| Source | Read by the search? | How | Tag |
|---|---|---|---|
| Standing Sims (footprint component active: no locomotion track this update) | yes | their footprints are in the lot quadtree, inserted and removed under `+0x918` by `UpdateSimFootprint 0x833560` (docs/89); the quadtree collectors and the stamp reader `AStarSearch_StampReader 0x61C9B0` (hop 8) read them, filtered by `BlockedByPeople`, `BlockedByDynamicFootprints`, `IgnorePrivateFootprints` and the ignore list | writers verified (docs/89), reader filter verified, "Sim footprints are in the walked cells" inferred |
| Moving Sims (agents) | no | no locomotion agent code (`Locomotion_UpdateAgent 0x957210`, the agent list) in either closure | verified direct calls |
| Footprints near a point | yes | `RouteObstructionAtPoint 0x655880 -> RoutingWorld_ObstacleQuery 0x82EDB0` (hop 8 below `PathPlanner_PlanHierarchical`, through `0x6435B0`/`0x64B430`/`0x64E380`) | verified path |
| Discourage and private footprints | yes | lot quadtree (docs/84 §2) | verified writers, reader inferred |
| Portal tunnel footprints | yes, and written per Sim (§3.3) | `0x632460` | verified |
| Physics world (walls, objects) | yes | `Routing_PhysicsRangeQuery 0x83B3A0`, `Routing_PhysicsRayVertical 0x6583E0 -> PhysicsWorld_RayCastVertical 0x686CC0`, `Realm_RangeQuery 0x682590` (hops 5-6) | verified paths; Sims are not physics shapes [inferred, docs/33: a static collision world] |
| Objects by id | yes | `ObjectService_FindById` (follower, portal owners, goal targets), `Object_GetComponent` (`0x65BC20`, hop 5) | verified |
| Terrain and lot lookup | yes | `World_LotLookup 0xC68990`, `Terrain_GetHeight 0xC6A540` (hop 2) | verified |

Verdict: the search does read live Sim state, but only as footprints in the shared routing data, never as positions. A snapshot that mirrors the lot quadtree (docs/76, docs/79, docs/89 hooks) carries it; a one-tick stale Sim footprint is what route following already tolerates (docs/18 hazard 4).

## 8. Go/no-go for a pure snapshot search

**GO**, with these parts:

1. **Portal answer table (PAT), built on the sim thread at submit** (the IC frame, where today's sync plan would run). For the Sim and each lot the plan's lot legs can touch (`LotSearch_Entry 0x640E00` is called from three sites in `PathPlanner_PlanHierarchical`: start, goal and transit lot legs [verified sites, roles inferred]; plus the goal step's goal lots), for each portal whose lot has `+0x8F` set: the EA answer for both directions. The worker's search reads the PAT instead of asking; a missing entry is "ask": the job falls back to today's proxied call or to a sync plan. Native rules 1-3 of §3.2 (cost, AGS, forbidden list) are pure data and run on the worker.
2. **Handler-set guard.** At submit, accept the PAT only if the event 59 delegate target is EA's `LotManager.PortalLockedCallback`, every touched lot's invocation list is a subset of {`Sim.OnPortalPathPlan`, `PrivacySituation.OnPortalPathPlan`}, and `Sims3GameplaySystems` has the EA MVID (docs/78 index has it). Otherwise run the plan as today. This keeps GoHere, simler90 and MonoPatcher overrides correct at the cost of no gain for those players.
3. **Side effects reproduced on the sim at collect:** `LockedDoorsDuringPlan` gets the doors the search actually found locked (needs the worker to report which PAT entries it used), and a `NoListeners` answer clears `lot+0x8F` exactly as the native code does.
4. **Per-search portal overlay**: locked portals block their tunnel footprints inside our search only (§3.3); never write the shared bit.
5. **Goal building**: keep `Route_PlanCommon`'s goal building on the sim (it is where event 68 fires (corrected 2026-10-18, see docs/105 §1: it does not fire there; the line-of-sight test still reads live physics, so the sim-thread advice stands, docs/105 §6)), or reimplement radial line-of-sight natively for `ForcePhysicsBasedRaycast` routes. For A10 (whole Replan-family ICs on the worker), event 68 is a new managed exit: a radial destination with that option must stay sync until this is handled.

**Blockers and costs** [inferred, to measure]:

- PAT size: doors, stairs and other portals of two or three lots, x2 directions, at tens of microseconds per managed call (docs/18 estimate) — likely more calls than the 2-30 lazy ones per plan; per-Sim caching with change hooks (rows 2-5, 8-10) is the follow-up that makes it cheaper than today.
- Precision: the lazy path asks only portals the search reaches; the PAT asks all. Same answers (they depend on Sim and object state, not on search progress), but more script work and more allocations on the sim thread (GC pressure, docs/18).
- Unknown handlers on players' installs (row 1) — the guard turns them into "no gain", not "wrong answer".
- Preferred portal (`O+0x78`) reader and the end-turn ignore list are not traced; check before relying on them.

## 9. Verified vs inferred

Verified: the portal class and its four slots, the two constructors and all creation paths in §2.2, the field offsets of §2.1, the cost and lock rules of §3.1-3.2, the per-Sim footprint write of §3.3, both managed bridges and their addresses, the direct-closure and reverse-closure results, the C# handler set and each handler's reads, the route field offsets of §6 and the option constants, the four ICs' start-point sources.

Inferred: the cache lifetime (stack frame implies per lot search), the meaning of flag bit 1 and of the type word, the helper kind-to-slot mapping, the receiver classes that rule out `Route_OnRouteEvent` and the non-routing candidates, whether the second display-level call fires, Sims not being physics shapes, the end-turn list and preferred portal readers, EP attribution, and every cost estimate in §8. Ledger rows: docs/51 §25 (I-92-1..I-92-10).
