# 94. Routing snapshot: change events, architecture and proof plan (I/O study part D)

Written 2026-10-18. Static only; the game was not run, no probe, no native code. Part D of the routing I/O study that PLAN "Routing off-thread" item 6 opened after the wait split (`ip-ar-ab-wait`: A10 moved 33 s of planning off the sim thread, and it came back as +49 ms/s of keyed waits on the game's shared routing data). Parts A-C (docs/91-93, written in parallel) map what the route search reads and writes; this doc covers how our own copy of that data stays in sync with the game, which shape the copy takes, and how we prove both.

Sources: docs/50, 59 §3, 60, 63, 66, 68, 76, 79, 80, 83, 84, 87-90 and `native/ts3native/steerro-test.cpp` (read). New static facts come from capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67): a raw `E8` caller index of `.text`, direct-call closures of 5 hops that flag `Lock_Acquire 0x4E16F0` with `0x918` in the 20 instructions before it, `World_FireEvent 0x78F2B0` and `GlobalHandlerChain 0x936C90`, absolute-pointer scans, and a store-displacement scan; plus grep of `game-copy/decompiled`. Scratch scripts stay in the session scratchpad. Ghidra was not needed.

Tags as in AGENTS.md: **[verified]** = read in the disassembly, decompile, source or a run file; **[inferred]** = reasoning, with a ledger row (docs/51 §27, I-94-*). "Direct closure" claims cover direct calls only; vtable and register-indirect calls are not followed.

## 1. Summary

> **Changes 2026-10-18:** §8 added: per world-level input (R base, R type bits, R blocked bits, footprint grid `P+0xDC`, G and its marks, blend layer 0, tuning, door lock state) what the snapshot copies, on which change signal and which hook, and what docs/76 misses (sources: docs/100, 105 §9, 107 §9-§11, 108 §7, 109, 112). Rows E12, E13, E14, E16, E26 and E28 are amended there (§8.3). Phase plan and effort updated in §5: P1 and P3 grew; totals **~13-21 h hands-on (G)** and **~19-31 h (R)**, plus 2-3 clean nights.
>
> **Changes 2026-10-18 (§11):** the P1 runs failed G1 (1,598 / 1,941 writer rows/s BP / IP, 19-38 STARTs/s, start or goal lot dirty at most STARTs). The lot side of §3.2-§3.3 is replaced by **ordered leaf replay into a worker-owned clone** (§11.4): the P1 leaf stubs record each routing-data write, the worker applies the same game functions to its own clone up to the START's sequence number, no lot is copied at START. G1 is rewritten in §11.5; the unattributed callers are mapped in §11.6. Totals **~15-24 h (G)**.

- **Mirror the routing data, not the object world.** The search reads the game's routing structures (lot table, lot graphs, per-lot quadtrees, footprints, portals, lock-key map, world road graph; docs/60 §1.1, docs/83 §2). Those are written only by a small set of routing-world writer functions, and the game itself keeps them in step with objects through its own handlers (footprint component events `0x21`, `0x0E`, `9`, `0x0A`, docs/89 §2). So the sync point for our copy is **the routing-data writer layer**, not the object events of docs/76/79: whatever the game's routing data says, our copy says, bugs included (a carried grandchild whose routing footprint the game never updates is stale in both, which is what "same answer as the game" needs).
- **Every source is now placed (§2).** Footprint writes funnel into `RoutingWorld_UpdateObjectFootprintCore 0x8321F0` (7 call sites, 6 functions) plus the remove leaves `0x82A0C0`, `LotRouting_RemoveObjectFootprints 0x635700` and the lot rebuilds `0x834DC0`/`0x834DF0`/`0x834F00`/`0x8350C0` [verified callers]. The quadtree, lot table, lot graph and polygon writers are the W1-W6 leaves (docs/83 §6) that ts3native already stubs. World/lot lifecycle has `World_FireEvent` ids and the docs/60 §2.5 drain ICs.
- **Coverage gaps (§2.3):** (1) **portal and lock-key ICs** (`AddPortal`, `RemovePortals`, `Add*Portals`, `SetPortalCost`, `SetPortalAgeGenderFlags`, `SetPortalLockStatus`, `AddPortalLocationVariation`, `Footprint*LockKey`) take no `+0x918` and fire no event in their 5-hop direct closure [verified, direct calls]: they need IC binding-slot swaps (no code patch); (2) **door permission is per Sim and managed** (World event 59 callback inside the plan, docs/18): it cannot live in a world snapshot, it stays a per-request call to the sim thread; (3) **frozen ponds** (Seasons: `PathType.FrozenPondPath` exists in SimIFace) have no writer found: until traced, a temperature/season change bumps the epoch; (4) **lazy state**: pending lot updates (`P+0x10E`, applied inside `Route_PlanLocked`), deferred quadtree inserts (flushed at the next query) and first-touch lot graph builds happen inside a plan today; the snapshot must force them on the sim thread at the cut; (5) **houseboat lots** move with no event (docs/76 I-76-8): rebuilt on every cut while one sails; (6) **terrain cell shapes** are replaced on the main thread on LOD change (docs/38 §6), an existing race; heights are static outside build mode and the Meteor IC.
- **Recommended architecture (§3): H, sim-published immutable lot images plus a worker-owned working world.** The sim thread marks lots dirty from the writer-layer barrier, and at a route START (the existing `Route.Plan` handshake) copies only the dirty lots into new immutable images and publishes a version (array of lot-image pointers, refcounted). The worker turns images into its own mutable working copy, so lazy builds and A\* scratch never touch game memory. No job ever reads or locks the game's routing data, which is what removes the A10 failure mode. Memory cap 64 MB for images plus working copy [design], against a 1.34 GB largest free block after a BP session (docs/66).
- **The real fork is which search runs on the copy (§3.4):** G = the game's own search code over a layout-compatible clone (fast to build, but every global the search writes must become per-thread, from the write set of docs/91-93), or R = our own search over our own structures (no shared globals, 2-4x the effort). Decide at gate G2 from the measured global write set.
- **Proof (§4):** a cheap live read-set probe (Frida Stalker block coverage on the route worker only, plus sampled memory-operand callouts for image globals) checks the static maps; an incremental-equals-rebuild audit proves the sync live and offline; a shadow compare of our search against the game's on the same request proves answers; steerro-test-style offline sections run the game's own writers and search on mapped data.
- **Phases (§5):** P1 change counters (observe) ~1.5-2.5 h, P2 read-set probe ~1 h + runs, P3 snapshot plus sync audit ~3-5 h, P4 search shadow ~4-8 h (G), R adds ~6-10 h, P5 A/B ~2-3 h + 1 night, P6 widen and release ~2-3 h + 1-2 nights. Total **~13-21 h hands-on (G)** or **~19-31 h (R)** plus 2-3 clean nights (~2-4 working days calendar) of Claude-with-agents-and-Rodney time, with a go/no-go gate after each phase.

## 2. Change events that alter routing inputs

### 2.1 Principle: two layers

| Layer | What it sees | Use |
|---|---|---|
| **Writer layer (sync source)** | every write to the routing structures, whatever caused it | marks the lot (or the world) dirty; nothing else |
| Event layer (attribution) | object events (docs/76, docs/79), `World_FireEvent` ids, IC slots, drain ICs | epochs (world load, travel, build/buy exit), per-source rates, and the audit's "which change preceded a mismatch" |

Why the writer layer: every object-side path found in docs/76/79/89 ends in the same few routing writers, and some paths have no object event at all (the footprint enabled bit `0x947590`, docs/76 I-76-9; discourage footprints; build tools). Hooking the writers covers them all without a per-source list [inferred, I-94-1].

### 2.2 Source table

> **Changes 2026-10-18:** the table below is the lot-side source list as written. World-level inputs (R bits, footprint grid, G, blend layer 0, tuning) and the amendments to E12, E13, E14, E16, E26 and E28 are in §8; where they differ, §8 wins.

Thread is the sim thread unless stated: every routing writer found runs there (docs/84 §7.3 for `fp+0x20` and `LotGraph_Reset`; docs/60 §1.1; the footprint ICs and object event handlers are sim) [verified in those docs]. Rates are estimates until phase P1 counts them.

| # | Change | Native path to routing data | Hook (prefer existing lists; no new detour where a stub exists) | Rate | Snapshot applies it as |
|---|---|---|---|---|---|
| E1 | Object footprint re-sync (parent changed) | footprint component handler for object event `0x21` (`0x947630` -> `0x947040` -> `RoutingWorld_UpdateSimFootprint 0x833560` -> `0x8321F0`) [verified docs/89 §2] | writer layer: `0x8321F0` entry and the remove leaves (`0x82A0C0`, `0x635700`); attribution: our own node on event `0x21` (docs/76 §3.3 mechanism) | ~1-20/s (prop and carry attach, docs/79) | dirty lot(s) of the object, incremental |
| E2 | Object moved (transform published) | footprint handler for `0x0E` -> `0x9470B0` -> `RoutingWorld_ObjectFootprintFromTool 0x8334A0` -> `0x8321F0` [verified docs/89 §2] | as E1; attribution node on `0x0E` (high rate: count only, docs/79 §1) | non-Sim ~1-30/s; Sims skipped by the game's own test | dirty lot, incremental |
| E3 | Object lot change | footprint handlers for events `9`/`0x0A` -> `0x946160` -> `RoutingWorld_UpdateObjectLot 0x82D280`, `RoutingWorld_UpdateObjectFootprint 0x833680` [verified callers this pass] | writer layer | per lot crossing | both lots dirty |
| E4 | Footprint enabled bit (locomotion start/stop, hidden flags, build tools) | `Footprint_SetEnabled 0x947590` (no object event) -> `0x833560` [verified docs/76, docs/89] | writer layer only (the event layer has nothing) | ~2 per route (I-89-7), ~10-20/s [inferred] | dirty lot |
| E5 | Script footprint enable/reset | `IC_World_ObjectEnableFootprint 0x78D520` (`0x947160`), `IC_World_ObjectResetFootprintState 0x78D5B0`, `IC_World_ObjectSetHiddenFlags 0x78D360`: closure takes `+0x918` at `0x833560` [verified direct closure] | writer layer | script rate, <10/s | dirty lot |
| E6 | Discourage footprints | `IC_RouteManager_Add/RemoveDiscourageFootprint` -> `0x834370`/`0x834860` -> `0x8321F0` [verified callers]; design D already queues them (docs/84 §2.1) | writer layer; D's FIFO is a second attribution point | ~1-10/s [inferred] | dirty lot |
| E7 | Private footprints (social, dynamic Sim footprints) | `IC_RouteManager_AddPrivateFootprint 0x75DCF0` -> `RoutingWorld_AddPrivateFootprint 0x831BA0` (takes `+0x918`); remove `0x754610` -> `0x82A0C0` [verified] | writer layer: `0x831BA0` entry is a new leaf (not in W1-W6) | per social, <5/s | dirty lot |
| E8 | Footprint lock keys | `IC_RouteManager_FootprintAdd/Remove/Set/ClearLockKey(s)` -> `RouteManager_FootprintLockKeyById 0x82B3E0` and the map at `world+0x8B8`; **no `+0x918` in the direct closure** [verified, direct] | IC binding-slot swap (5 slots, docs/59 §3.4 mechanism), record (footprint id, op) | rare | lock-key table row, incremental (world-level table) |
| E9 | Portals added/removed | `IC_RouteManager_AddPortal 0x757E10` -> `LotRouting_AddPortal 0x6379E0`; `IC_RouteManager_RemovePortals 0x74D6B0` -> `LotRouting_RemovePortals 0x6399E0`; `AddStairPortals 0x74B440` -> `0x82F770`, `AddPlatform/PoolPortals 0x74B410` -> `0x82F750`, diving board `0x82F710`, water slide `0x82F730` (all -> `0x82D2E0` -> `0x6379E0`); `AddPortalLocationVariation 0x74D3F0` -> `0x637C30`. **None takes `+0x918`** in its direct closure [verified, direct]; they reach the lot table through the lookups our gate stubs cover [inferred] | IC slot swaps (8 ICs) or one leaf on `0x6379E0`/`0x6399E0`/`0x637C30` (internal callers: lot rebuild `0x832890`, `0x82D2E0`); prefer the slots (no code bytes) and add the leaves only if P1 finds portal writes the slots miss | elevator, velvet rope, trampoline, horse jump, stall, pool and stair changes: rare in live play, bursts in build/buy | dirty lot |
| E10 | Portal cost and age/gender flags | `IC_RouteManager_SetPortalCost 0x758110`, `SetPortalAgeGenderFlags 0x758220`: `LotRouting_PortalsForObject 0x62F760`, then in-place writes on the portal objects; no lock, no leaf function [verified callees] | IC slot swaps (only option: the write is inline) | `Door.SetPortalCost` (privacy situations, door settings), Gialt boulder: rare | dirty lot |
| E11 | Portal lock enable per lot | `IC_RouteManager_SetPortalLockStatus 0x74B490` -> `LotTable_GetIfPresent 0x62FB60`, then the lot entry byte `+0x8F` (store at `0x74B4BD`) [verified]; the planner's `Portal_LockState 0x64F2C0` also clears it (`0x64F4EE`) when the callback says "no listeners" | IC slot swap; the planner-side clear is a search output (docs/91-93) | once per lot load (`Lot.cs`) | lot image field |
| E12 | **Door permission per Sim** | not stored: `Portal_LockState 0x64F2C0` -> World event 59 -> managed `LotManager.PortalLockedCallback` -> `Sim.OnPortalPathPlan` (doors, poles, stalls, elevators, ladders) during the plan, answer cached per plan (docs/18) [verified there] | none possible in a world snapshot (managed state, depends on the Sim) | 2-30 calls per plan with lot legs (docs/18) | **per request**: the worker's existing `Proxy` round trip to the owner thread (`ts3native.cpp` `Proxy`), as A10 does today; option: pre-answer the start/goal lots' portals at START (§3.5) |
| E13 | Build/buy edits (walls, floors, roofs, stairs, pools, placed objects) | tools write through E1-E10 while the clock is paused; on exit `IC_RouteManager_OnExitBuildBuy 0x74B6B0` -> `RoutingSystem_OnExitBuildBuy 0x835240` -> `0x834F00` (walls and floors, `UpdateTerrainFootprints`) and `RoutingWorld_ApplyLotUpdate 0x82B590` [verified callers] | epoch: IC slot `RouteManager_OnExitBuildBuy` (`0x1160DA8`, docs/76 §2.2; `0x835240` itself is `[Verify]`'s, docs/68 rule 3) | ~0-10/h | **lot rebuild** of the edited lot at exit (all lots if the lot is unknown) |
| E14 | Lot load (world load, travel arrival) | `Lot_FinishLoad 0xACF170` -> `0x834DC0` (floor footprints); `RoutingWorld_LockedOp_WorldInit 0x829410` (from `0xAACF60`, `World_LotStreaming 0xC622D0`) [verified callers]; world events `5`, `7`, `0x10`, `0x46` (docs/76) | `World_FireEvent` stub (docs/76 §2.2, not built) plus the world-init leaf | per load | **world epoch**: drop all images, full build after world-load-finished (event `7`) |
| E15 | Lot remove, bulldoze, houseboat create/delete, lot library place | `IC_World_BulldozeLotImpl 0x7868E0` / `DeleteHouseboatLotImpl 0x788800` -> `0xA48CC0` -> `RoutingWorld_RemoveLot 0x829D50` (via `0xAB1EB0`) and `RoutingSystem_RebuildLotRouting 0x835110`; `CreateHouseboatLotImpl 0x792CA0` -> `0x835110`; lot-side create `0xACE4A0` -> `0xAC79E0` -> `0x8350C0`/`0x832890` [verified callers]; world events `0x0D`/`0x0E`/`0x0F`/`0x10` | `World_FireEvent` stub; `0x829D50` is a leaf for "lot gone" | Edit Town, rare | lot added: build; lot removed: drop image (the version keeps the old one alive for running jobs) |
| E16 | Pending lot updates | `P+0x10E` set by `LotGraph_Build 0x63CB90` (`0x63CC1E`) and `0x6236D0` (from `0x63C8E0`), cleared by `RoutingWorld_ApplyPendingUpdates 0x82C420` (callers `Route_PlanLocked`, `RoutingSystem_SaveLot`) [verified store scan and callers]; per-lot `LotTable_LotHasPendingUpdate 0x631A60` | read the flag at the cut | after lot changes | at the cut the sim runs `ApplyPendingUpdates` under `+0x918` (no job holds it any more) before copying, so images never carry pending state [design] |
| E17 | Deferred quadtree inserts | `LotQuadTree_Insert 0x666C50` queues; `LotQuadTree_FlushDeferred 0x666A40` applies at the next query (docs/83 §2) | W1/W2 leaves see the apply, not the queue | per footprint write | at the cut, flush every dirty lot's Q on the sim (the game's own `0x666A40`) before copying [design] |
| E18 | First-touch lot graph builds | `LotGraph_Build 0x63CB90` (W4) and lot-table inserts `0x62E680` (W5) on a miss, inside plans today (docs/80 §2) | W4/W5 leaves | once per lot per change | G: worker builds lazily in its working copy; R: build at image time from our own data |
| E19 | Save | `RoutingSystem_SaveLot 0x8361B0` and `RoutingWorld_Save 0x834FC0` -> `0x834DF0` (lot walls) and `ApplyPendingUpdates` [verified callers]; `IC_World_BuildRoutingDataImpl 0x78AE40` takes `+0x918` in `RoutingWorld_LockedOp_Save 0x8293D0` [verified direct closure] | writer layer (they go through the same leaves) plus `g_rtBusy` (docs/60 §2.4) | per save | dirty lots, or epoch for `BuildRoutingData` |
| E20 | Travel, quit, return home | `IC_GameUtils_TransitionToLoadWorldImpl 0xEC1180`, `...ToNotInWorldImpl 0xEC11B0`, `WorldManager_Shutdown 0xC6B780` (docs/60 §2.5) | the S5 drain ICs (planned there) | rare | close: refuse START, drop the version after the last job; then E14 |
| E21 | Terrain height | `IC_World_LotSetTerrainHeightImpl 0x7897D0` (Meteor only in live play): no `+0x918` in the direct closure, fires world event `0x0F` via `0xAB6D60` [verified]; terrain footprints rebuilt through `RoutingWorld_EnsureTerrainFootprints 0x8341D0` (from `RoutingWorld_SurfaceAtPoint 0x834B80`) and `0x831710` [verified callers] | `World_FireEvent` id `0x0F` | ~never | lot rebuild |
| E22 | Terrain cell collision shapes | main thread, `WorldTerrain_Update` on LOD change (docs/38 §6) | none needed if heights are copied at the world epoch; otherwise the live query stays as today (existing race, not ours) | 64 per 7 min (docs/38) | static per load [inferred, I-94-6] |
| E23 | World road and footpath graph | built at world init and `BuildRoutingData` (docs/60 §1.1) | E14/E19 epochs | per load | static per load [inferred there] |
| E24 | Water | `IC_RouteManager_SetOceanPortalOwner 0x74B470` -> `0x829520` (3-function closure, no lock) [verified]; pond levels read-only ICs | IC slot swap (rare) | rare | world-level row |
| E25 | Seasons: snow, weather | `IC_World_LotSnowUpdate 0x789560` posts to the message server only (8-function closure, no routing writer) [verified]; `IC_World_SetObjectSnowImpl` writes a visual component [verified callees] | none | weather rate | no routing effect [inferred, I-94-7] |
| E26 | Seasons: frozen ponds | `PathType.FrozenPondPath` (SimIFace) and `RoutingComponent` branches exist; **no writer of the frozen state found** | gap: until traced, world epoch on season and temperature change (managed `SeasonsManager` events, ts3fixes listener) | a few per season | world epoch [gap, I-94-8] |
| E27 | Lot streaming (camera, active lots) | `Lot_SetActive 0xAC20E0`: scene add/remove only, no world event (docs/76 I-76-4) | none; P1 confirms the writer layer stays quiet during a camera pan | camera | none [inferred] |
| E28 | Houseboat sailing | lot parented to the boat object; no per-move event (docs/76 I-76-8) | always rebuild a sailing houseboat lot at each cut | while sailing | lot rebuild per cut |
| E29 | Sims in motion | a walking Sim's footprint is inactive (I-89-7); steering avoidance is not a planner input | E4 covers start/stop | per route | dirty lot |
| E30 | Tomb rooms (World Adventures) | `TombRoomManager` room reveal; routing effect not mapped (docs/76 §2.1) | treat tomb lots as always rebuilt, like docs/76 | rare | lot rebuild per cut |

### 2.3 Gaps, ranked

| Gap | Risk if missed | Closure |
|---|---|---|
| Portal and lock-key ICs with no event and no lock (E8-E11, E24) | stale portals: a route through a removed velvet rope or an elevator that is gone, or a missing new one | 15 IC slot swaps (no code bytes, docs/59 §3.4 mechanism); P1 counts leaf writes with no recorded source (must be 0) |
| Door permission per Sim (E12) | wrong door answer if cached across plans | never snapshot it; per-request proxy (today's behaviour) |
| Frozen ponds (E26) | routes over or around ice differ from the game's | epoch on season/temperature change; trace the writer before P4 (ledger I-94-8) |
| Lazy state inside plans (E16-E18) | images with pending updates or unflushed inserts answer differently | force at the cut on the sim thread; the audit (§4.2) compares after the same forcing |
| Writes the leaves miss (cached-pointer writers, register-indirect calls not followed) | silent divergence | P1 "unattributed write" check plus the live audit; the docs/79 store scan method on the routing structures |
| Houseboats and tombs (E28, E30) | stale lot transforms or rooms | always rebuild while present |

## 3. Snapshot architecture

### 3.1 Options

| | A. Double-buffered immutable snapshot, published by the sim at IC points | B. Worker-owned copy updated from a change queue | **H. Per-lot immutable images + worker working copy (recommended)** |
|---|---|---|---|
| Who copies game data | sim, whole world or dirty lots | sim records changes; the worker must not read game memory, so the record has to carry the data (serialised footprint, portal) | sim, dirty lots only, at START |
| Consistency | one cut, consistent by construction | the queue order must replay the game's writer semantics on our copy exactly (a second implementation of every writer) | one cut per START; lots copied whole, so no replay semantics |
| Sim cost | full copy per publish: too much unless incremental | lowest (append records) | dirty lots only: ~20-60 writer calls/s touching 1-5 lots [inferred] |
| Worker writes (lazy builds, A\* scratch, refcounts) | impossible on an immutable shared copy | free | free (the working copy is the worker's) |
| Memory | 2x routing data | 1x + queue | images (shared between versions) + 1 working copy: ~2x, old images freed when no version holds them |
| Failure mode | stale between publishes | replay bug = permanent divergence until an epoch | a missed dirty mark = stale lot until its next change (the audit finds it) |

B's catch is decisive: a record must carry everything the writer read (the object's footprint polygons, its lot, its level, its flags) or the worker would read live objects, and applying it means reimplementing `UpdateObjectFootprintCore` and the portal writers on our copy. H copies the game's own result instead, after the game's writer has run, so there is no second writer to get wrong.

### 3.2 H in detail [design]

> **Changes 2026-10-18:** the cut (step 2) runs under `+0x918` and also refreshes the world-level images of §8.2 (R, footprint grid, G tiles) from their dirty flags; new leaves `0x82B590`, `0x82A190`, `0x636BF0`, `0x62D9F0` and the G-build call sites join step 1 (§8.2).

1. **Dirty marks (sim, at the writer layer).** The leaves of §2.2 record the lot key they touch into a small set (`dirtyLots`, ≤ 64 entries; overflow = all lots dirty). Existing stubs: W1-W6 (`steerro.cpp`, always installed while `[SteerRO]` or its writer set is on) already run on these leaves; they gain a sim-side branch that marks the lot. New leaves: `0x8321F0`, `0x831BA0`, `0x82A0C0`, `0x635700`, `0x829D50`, plus the 15 IC slots of E8-E11/E24. A world epoch counter is bumped by the `World_FireEvent` stub ids of docs/76 §2.2 and the drain ICs.
2. **Cut at START (sim).** In the `Route.Plan`/A10 handshake, before the job is posted: if the epoch changed, full build; else for each dirty lot: `ApplyPendingUpdates` if `P+0x10E` (E16), flush its Q (E17), copy the lot into a new **lot image** (flat arrays: footprint records with polygons per level, quadtree as a footprint index, portals with costs and flags, the lot transform and level data, lock-key rows) and swap it into a new **version** (pointer array over all lots + world-level tables). Unchanged lots share their image with the previous version. Versions are refcounted; the worker holds one per job.
3. **Working copy (worker).** G: the worker materialises each image into game-layout structures in its own heap (lot table node, lot graph, Q, footprint objects with the game's vtables), rebuilt only when the lot's image pointer changed since the last job; the game's search code then runs on it with `world`/`P` pointing at the worker's copy. R: our search reads the images directly.
4. **Per-request data** (route options, lock keys of the route, forbidden portals, ignore lists, valid rooms; docs/91-93 inputs) is copied at START, as A10 does with the start point and kind.
5. **Door permission** (E12): unchanged per-plan proxy to the owner thread; or pre-answers (§3.5).

The game's own planner keeps running untouched on the game's data for every sync plan, fallback and steering query; with H there is no gate, no QL and no `+0x918` wait between them and the worker, because the worker never reads game routing memory.

### 3.3 Memory and rebuild cost

| Item | Estimate | Basis |
|---|---|---|
| Routing data of one world | 10-40 MB [inferred] | "Routing/*" allocation tags (QuadTree, ObjectFootprint, RoutingDataCT, RTData/chunks; docs/60 §1.1); not measured. P1 reads it from the existing `/allocations` tag split |
| H total (images + working copy + one old version during a job) | ~2.2x routing data: 22-88 MB | design |
| Budget cap | 64 MB, refuse to enable above it and log | docs/66: BP largest free 1.53 GB at start, 1.34 GB after 30 min; 64 MB is under 5% of it. Error 12 is not close in these sessions |
| Full build (epoch) | 30-150 ms on the sim [inferred: a few thousand footprints x polygons] | only at world load, travel, build/buy exit, Edit Town: all are loading screens or paused clock |
| Incremental cut | 20-100 µs per dirty lot [inferred]; 1-5 dirty lots per START at ~6 STARTs/s (6,073 async starts in 18 min, PLAN item 6) | ~0.2-3 ms/s sim time; must stay under ~2 ms/s (offload rule, CLAUDE.md) |

A finer cut (per-footprint records instead of per-lot images) is the fallback if the incremental cost misses the 2 ms/s budget.

### 3.4 G or R: the search on the copy

| | G: game's search code on the worker's clone | R: our own search |
|---|---|---|
| Build effort | ~1-2 h build after P3 (materialiser + rebinding the world/planner pointers the search reads) | ~7-12 h (G build plus ~6-10 h writing and matching the A*: lot-level search, portal edges, world A\* over the road graph, path smoothing, result encoding) |
| Answer equality | by construction for the same data, if every global the search touches is private | must be proved case by case; tie-breaks differ (mismatch class M1) |
| Blockers | any global written by the search (A\* scratch `Q`, generation stamp `0x11580C4`, docs/18 S0) that the sim-thread planner also uses: needs per-thread redirection of every such global, i.e. patched code or a TLS swap around the job. **Not possible while the sim also plans** unless the write set is small and redirectable | none on game memory |
| Decision input | docs/91-93 global write set plus the P2 probe | |

**Rule (gate G2):** choose G if the search's written globals are ≤ ~5 addresses that can be redirected by swapping a pointer the code loads (no inline absolute writes), else R. If the worker and the sim both run the game's planner on the same globals, G is a no-go.

### 3.5 Door permission options

1. **Proxy (default):** today's `Proxy` hands each callback to the owner thread; cost is a round trip per (portal, rooms, direction) not yet in the per-plan cache. P2 counts them per job.
2. **Pre-answer at START:** world legs never ask (docs/18), so only the start and goal lots' portals matter [inferred]; the sim asks `Sim.OnPortalPathPlan` for each of those portals before posting the job and ships the answers in the request. Same managed work the vanilla plan does on the sim, but in one batch and without round trips. Build only if P2 shows round trips cost more than ~1 ms/s.

## 4. Proof plan

### 4.1 Live read-set probe (P-RS): confirm the static maps cheaply

Goal: the set of functions and image globals the route search touches in real jobs, compared with docs/91-93. Not a full data trace.

| Level | Mechanism | Cost | Output |
|---|---|---|---|
| L1 block coverage | Frida Stalker `follow` on the route worker thread only, from `RoutingJob_Execute 0x81B2A0` entry to its return, every job; `transform` callback records each new basic block once (compile event), nothing per execution | first execution of each block is slow (compile), then near native; the worker is off the sim thread, so it costs job latency only | set of functions entered (block -> function via `functions.csv`), per job kind; diff against the static closure: functions run but not in the map = map gaps |
| L2 globals | in the same transform, a callout on instructions with an absolute memory operand inside the image (`.data`/`.bss`), sampled 1 job in 20 | callout per such instruction in sampled jobs only | exact global addresses read and written, by function; diff against the docs/91-93 global lists (this is the G/R decision input) |
| L3 heap classes | sampled 1 job in 50: callout on memory operands with a base register, recording (function, operand offset, `VirtualQuery` allocation base bucket); no per-address log | bounded by sampling; ring buffer of 64k entries | per function, which structure offsets are read; cross-check with the read-set tables of docs/83 §2 style maps |
| L4 managed | the existing `InvokeHook`/`HostCallHook` counters per job, plus the MonoMethod pointer | free | managed calls per job (expect only event 59 callbacks) |

Safety (memory: probe safety): test the Stalker script first on a dummy process that runs a mapped `0x81B2A0`-free loop (the steerro-test child), detach cleanly at load end, JS exception handler opt-in. Runs with async on (jobs on the worker), 10 min BP and IP. Sim thread untouched, so the probe does not distort the sim-side numbers it is not measuring.

### 4.2 Sync proof: incremental equals rebuild

- **Invariant S:** at any cut, the image of every lot equals an image built from scratch from the game's live data at that cut.
- **Live audit (`[RouteSnap] Audit=1`):** at every Nth cut (N = 50) the sim rebuilds one lot (round-robin plus every lot dirty in the last cut) from scratch and compares it with the version's image by content hash, then field by field on a hash mismatch. Counters: `snapAudits`, `snapAuditMismatch`, `snapAuditUs`; on mismatch a JSONL record: lot key, differing table (footprints, portals, lock keys, transform), the last 32 dirty marks and their sources (event layer), epoch.
- **Unattributed writes:** every leaf write on the sim with no source recorded since the last cut increments `snapUnattributed` (P1, must be 0 or explained).
- **Mismatch classes:** S1 missed dirty mark (a lot differs and no mark since its last image) = coverage gap, fix the hook; S2 lazy state (pending or deferred insert differs) = cut order bug; S3 off-sim writer (a mark from the main thread or worker) = thread assumption broken; S4 epoch race (world changed during the cut) = drop the version.

### 4.3 Answer proof: shadow compare

At START, the worker runs our search on the version and the game's planner runs as today (sync plan on the sim, or the A10 job with `Kinds` shadow). Compare at collect:

| Class | Meaning | Rule |
|---|---|---|
| M0 | same result code, same node list (positions within 1e-3), same portal ids, same cost | pass |
| M1 | same result and cost, different node list (tie-break) | allowed under R only; logged |
| M2 | same result, cost or end point differs beyond epsilon | 0 under G; under R explain each class |
| M3 | result code differs (success vs fail, or a different fail) | must be 0 |
| M4 | ours refused (unsupported option, kind, lot, epoch mid-flight) | counted, falls back; target < 5% |
| M5 | version stale: a dirty mark for a lot on the path arrived between the cut and the game's plan | not a mismatch; counted to size the staleness window |

Logging: counters `snapShadow`, `snapM[6]`, `snapShadowUs`; a JSONL record per M2/M3 with the request (start, goals, options, Sim id, lots), version id and the dirty marks since the cut, so the case can be replayed offline from a version dump (`[RouteSnap] DumpOnMismatch=1`, capped at 20 per run). (2026-10-18) The per-compare record the G4 report reads is §13.

### 4.4 Offline harness (steerro-test style)

New section K in `native/ts3native/steerro-test.cpp` (mapped TS3W.exe, random lots, lot tables, quadtrees and footprints with the game's vtables, as sections A and E already build):

| Case | What runs | Pass |
|---|---|---|
| K1 sync | random sequences of the game's own writers on the mapped data (`0x8321F0`, `0x833560`, `0x831BA0`, `0x82A0C0`, `LotRouting_AddPortal 0x6379E0`, `RemovePortals 0x6399E0`, `LotGraph_Build`, `LotTable_Insert`, flush `0x666A40`) through the installed leaf stubs; cut after random steps | incremental version == fresh full build, every cut (invariant S); seeds 1-4 x 3000 |
| K2 cut order | a pending update and a deferred insert left open at the cut | images equal to a build after the game's own `ApplyPendingUpdates` and flush |
| K3 search (G) | the game's lot search and world A\* on the game's data vs the same code on the worker clone | M0 for every case (2026-10-19: built for the in-room search on the replay clone, §13.3) |
| K3 search (R) | the game's search vs ours on the same data | M3 = 0, M2 classes listed |
| K4 concurrency | sim-thread writers running while the worker searches an older version | no read of game memory by the worker (guard page on the mapped game heap during the worker's run); answers equal to the version, not the live data (2026-10-19: built without the guard page, writes between plans and uncut writes, §13.3; it found the clone's live parent links, I-94-193) |
| K5 lifetime | versions released while a job holds one; lot removed mid-job | no use after free (debug heap), refcounts back to 0 (2026-10-19: the replay clone's form, a clone path outliving its lot copy, §13.3) |

`tools/emu` specs (docs/63) for the pure pieces: the image builder's footprint polygon copy and the hash.

### 4.5 Live jobs

All on `queues/clean-night*.json` (clean machine, CLAUDE.md), 10 min per profile job, 18 min ABBA for A/B, fail-fast waits.

| Job | Phase | World | Duration | Pass |
|---|---|---|---|---|
| `bp-snap-events`, `ip-snap-events` | P1 | BP, IP | 10 min | per-source rates logged; `snapUnattributed` 0 or every hit explained |
| `bp-snap-events-bb` | P1 | BP + repro `snap-buildbuy-edit` (enter build, add a wall and a door, place a stair, exit) | 10 min | E13 rebuild fires once per exit |
| `bp-snap-events-portal` | P1 | BP + repro `snap-portal-churn` (velvet rope place/remove, elevator ride, lock a door) | 5 min smoke | E8-E11 slot counts > 0 |
| `bp-snap-readset`, `ip-snap-readset` | P2 | BP, IP, async on | 10 min | P-RS outputs; map gaps listed |
| `bp-snap-audit`, `ip-snap-audit`, `sv-snap-audit` | P3 | BP, IP, Sunset Valley | 10 min | `snapAuditMismatch` 0 over ≥ 5,000 audits per world; cut cost ≤ 2 ms/s; memory within cap |
| `snap-travel-audit` | P3 | BP -> France -> BP (travel repro) | one round trip | epoch fires at each load, 0 mismatches after |
| `bp-snap-edittown` | P3 | repro `snap-bulldoze` (Edit Town bulldoze + place a lot) | 5 min | E15 handled, 0 mismatches |
| `bp-snap-shadow`, `ip-snap-shadow` | P4 | BP, IP | 10 min | ≥ 10,000 compares per world; M3 = 0; M2 = 0 (G) |
| `bp-snap-ab`, `ip-snap-ab` | P5 | BP, IP, `--ab routesnap` | 18 min ABBA | §5 gate G5 |
| `bp-snap-soak` + save matrix | P6 | BP 45 min, save BP/SV/IP | 45 min + matrix | 0 crashes, 0 stuck Sims above baseline (logged only), 0 audit mismatches |

Repros named here are new `repros.json` entries with agent actions (memory: repro actions), added in the phase that first runs them.

## 5. Phased build plan and gates

> **Changes 2026-10-18** (§8): P1 gains pass-through counters on the four new leaves and the G-build call sites (~+0.5 h); P3 gains the world-level images (R, footprint grid, G with mark tiles, blend layer 0 and tuning) and their audit (~+1-2 h); G3 checks them. P2, P4, P5 and P6 unchanged. The docs/105 door scheme (including the `CommonDoor.ImportContent` refresh) is costed in docs/105, not here.
>
> **Changes 2026-10-18 (§11):** G1 below is superseded by §11.5 (design B limits); P1b (§11.7, ~1-1.5 h + a count run) comes before P3; P3 becomes the clone, recorder and replay (~4-6 h), P4 loses the materialiser (~3.5-7.5 h for G).

Effort in wall-clock hours for Claude with agents and Rodney, from measured pace (build, game runs, fix rounds); clean nights run unattended and are listed separately.

Figures were converted 2026-10-18 from the older coarse "d" unit.

| Phase | Work | Effort | Gate to the next phase (go / no-go) |
|---|---|---|---|
| P0 | Static maps: docs/91-93 (inputs, outputs) and this doc | done | the read set of docs/91-93 names every structure §2 hooks |
| P1 | **Built 2026-10-18 as ts3native 0.25.0, `[RouteSnap] Count=1`, counters v46 (§10); not run yet.** Observe-only change counters: sim branch in the W1-W6 stubs, new leaf counters (`0x8321F0`, `0x831BA0`, `0x82A0C0`, `0x635700`, `0x829D50`), 15 IC slot counters, the docs/76 `World_FireEvent` stub (counts by id), object-event nodes for attribution; reserve a counter version (docs/registry.md); hook-sites rows (docs/68); (2026-10-18) pass-through counters with thread class on `0x82B590`, `0x82A190`, `0x636BF0`, `0x62D9F0` and the five G-build call sites, shared with `[ProbeValues]` where it owns the site (§8.2) | build ~30-60 min, game runs 2 jobs ~30 min, fix rounds ~30-60 min; total ~1.5-2.5 h | **G1:** `snapUnattributed` = 0 (or each source added); leaf write rate ≤ ~200/s and dirty lots per START ≤ 5 at p95; no off-sim writer except the known main-thread terrain shapes |
| P2 | Read-set probe P-RS (Frida, worker only) | mostly covered by probe pass 1/2 and Stalker U1 (docs/106, 109, 113); remaining ~1 h + runs | **G2:** executed functions ⊆ static closure + listed gaps; global write set known; G or R chosen by the §3.4 rule; managed calls per job = event 59 only |
| P3 | Snapshot H: cut, images, versions, audit; no search yet (`[RouteSnap] Enabled=0 Mode=0` audit-only); steerro-test K1, K2, K5; (2026-10-18) world-level images of §8.2: R, footprint grid table, G with high-cost tiles and `blockedNodeMap`, blend layer 0 and tuning | build ~1.5-2 h, runs 2-4 jobs ~30-60 min, fixes ~1-2 h; total ~3-5 h | **G3:** 0 audit mismatches on BP/IP/SV, travel and Edit Town, **including** the R image against live R (both docs/108 sums plus a bit-7 hash), the grid table and the G tiles; cut cost ≤ 2 ms/s BP; memory ≤ 64 MB; 0 crashes. A miss here stops the plan before any search work |
| P4 | Search on the copy, shadow only (`Mode=1`): G materialiser or R search; steerro-test K3, K4 | G: build ~1-2 h, shadow runs ~30-60 min, fix rounds ~2-4 h; total ~4-8 h. R adds ~6-10 h | **G4:** ≥ 10k shadow compares per world, M3 = 0, M2 = 0 (G) or explained (R), M4 < 5%, worker job time ≤ the game's plan time |
| P5 | Answers taken (`Mode=2`) for `ReplanFromPosition`, the A10 order; in-load A/B | build ~30-60 min, daytime check ~30 min + one clean-night A/B, fixes ~1 h; total ~2-3 h + 1 night | **G5 (no-regression gate, PLAN.md offload goal):** sim keyed waits not above off (the A10 failure mode: `wait-ab.ts` per site), it/s not down more than ~2% vs off on BP and IP, 0 crashes, 0 new sim-thread waits, 0 audit mismatches; else stop and keep the build off |
| P6 | Widen to `PlanFromPoint`, `Replan`, `Plan`; soak, save matrix, travel; release notes | build ~1 h, release checklist queue ~5-6 h unattended, fixes ~1-2 h; total ~2-3 h + 1-2 nights | **G6:** release checklist (`queues/async-release.json` pattern, drafted as `queues/snap-release.json`, §20) passes with the snapshot on |

Totals: **option G ~13-21 h hands-on + 2-3 clean nights (~2-4 working days calendar)**, **option R ~19-31 h + nights**. The first stop points are cheap: G1 (~2 h in) shows whether the writer layer is closed, G3 (~6-10 h in) whether sync holds live, before any search is written.

What this plan does not cover: steering (`0x82EDB0`, `0x85FD10`) and the other sim-thread routing queries stay on the game's data and keep their own designs (S2, ObstructedRO, SH); with H the worker no longer contends with them, so their waits should fall to the vanilla level, which `bp-snap-ab` measures.

## 6. Symbols added (CSV, append only)

`RoutingSystem_LotLoadFloorUpdate 0x834DC0`, `RoutingSystem_SaveLotWalls 0x834DF0`, `RoutingSystem_ExitBuildBuyLotUpdate 0x834F00`, `RoutingSystem_RebuildLotWalls 0x8350C0`, `RoutingWorld_AddObjectPortals 0x82D2E0`, `RoutingWorld_AddLotObjectPortals 0x832890`, `LotRouting_AddPortalVariation 0x637C30`, `World_RemoveLotCommon 0xA48CC0`: callers verified by the raw `E8` index, roles inferred from them.

## 7. Status: P1+P2 probe built (narrowed to the indirect-target sampler, 0.23.0)

> **Changes 2026-10-18:** the root list below names "R blocked bits `0x82B590`/`0x832890`"; per docs/112 §3.2 the bit-7 writers are `0x82B590` and `0x82A190` (`0x832890` only reads bit 7). A later `snap-vcall-sites.py` pass should add `0x82A190` as a root; the shipped table is left as built.

**Scope (Rodney, 2026-10-18):** the live runs only have to find what static analysis cannot see: the runtime targets of indirect calls in the routing closure (vtable slots of EP and mod objects, the per-search callbacks of `QuadTreeCellAStar`, function pointers). The P1 change counters, the docs/93 return-code counts and the G/R checksums are **not built**; they stay in §5 for later.

**What is built [verified offline]:**

- `tools/snap-vcall-sites.py` (capstone 5, Python like `tools/emu`): direct-call closure (E8 and tail `E9`, 12 hops, stops at `Lock_Acquire 0x4E16F0`, `Lock_Release 0x4E17B0`, `EA_AllocTagged 0x4E3F90`, as docs/91 §4) from 51 roots: `Route_PlanLocked 0x82C6E0`, `Route_PlanCommon 0x7606E0`, the four route ICs, every writer leaf of §2.2 and docs/83 W1-W6, `ApplyPendingUpdates 0x82C420`, R blocked bits `0x82B590`/`0x832890`, G marks `0x670510`/`0x670750`, the portal ICs and leaves (E9) and the lock-key ICs and `0x82B3E0` (E8). Result: **1,723 functions, 1,182 sites** (1,122 `call r/m32`, 60 `jmp r/m32` incl. switch tables) written to `native/ts3native/vcallsites.h` (address, length, instruction bytes). Left out: absolute `[disp32]` forms (import thunks), 11 sites in the first 16 bytes of a function (detour prologues get relocated by us, S3SS and Frida), 4 sites within 16 bytes of an address docs/68, `tools/probe/agent.js` or `tools/frida/*.js` names. `0x80E610` (W6b) has no `functions.csv` entry and is walked up to the next entry.
- `native/ts3native/vcall.cpp`, `[ProbeSnap] Enabled=0` (off): int3 on the first byte of each site; a vectored handler (first in the chain, added only when arming) finds the site in the sorted table, computes the target from the CONTEXT (ModRM/SIB decode of the table bytes), records (site, target) with a count (8 targets per site, more counted as overflow) and emulates the instruction (call: push the return address; Eip = target), so it runs on every thread with no trampoline. Each site disarms itself after `PerWindow` hits (16); every `WindowSec` (30) all sites are re-armed, the first time `DelaySec` (90) after start-up so S3SS and the other start-up patchers see the original bytes. Before each arming the bytes are compared with the table; a site another hook rewrote is dropped for good. An unreadable or null target disarms the site for good and lets the original instruction run (the game's own fault, unchanged). Every window the log gets `vcall SITE -> TARGET n=N [jmp] [out=module]` for pairs whose count changed, and a `vcall window` summary line.
- Counters v43 at `0x15C4` (`vcReady`, `vcSites`, `vcSkipped`, `vcHits`, `vcPairs`, `vcOutside`, `vcOverflow`, `vcFaults`, `vcWindows`, `vcArmed`; docs/registry.md), read by `tools/probe/agent.js`.
- `tools/harness/snap-report.ts <ts3native.log> [...]`: merges logs, names site and target functions (symbols CSV, `game-copy/analysis/functions.csv` when present), flags **NEW** (a TS3W.exe target with no row in `tools/ghidra/symbols/ts3w-1.67.symbols.csv`) and **OUT** (outside TS3W.exe, with the module; `anon` = heap code), prints only those sites unless `--all`.
- Tests: selftest `TestProbeSnap` (decode of 6 forms, target from a CONTEXT, unreadable operand, then a live int3 round trip on our own code: `call [eax+4]`, `call ecx`, `jmp [eax*4+table]`, `call [esp+4]`, self-disarm after the budget, native run after it, kernel32 targets counted outside, 9th target overflow, log line count, a rewritten site dropped at re-arm); `steerro-test` seeds 1 and 2 x 3000 pass; `hook-sites --check` 0 missing; `check-docs-code` ok.

**Cost [inferred, I-94-15]:** each sampled hit is one exception dispatch (WoW64, about 5-15 µs). Budget per window = hit sites x 16. With ~600 hot sites that is ~10k dispatches per 30 s, ~0.1-0.2 s per window spread over every thread, about 0.3-0.6% of one core on average; the worst case is a burst at a window start when one sync plan meets many freshly armed sites (tens of ms, twice a minute). Outside the windows' first hits the sites run native. `vcHits` gives the real count. Frida's `enableExceptions()` (agent.js, opt-in) would put a JS callback in front of every hit: do not turn it on in these jobs.

**Limits:** counts are first-N samples per window, not frequencies; a target seen only after a site's budget is spent in every window is missed (more windows or a larger `PerWindow` widen it). Sites in code reached only through indirect calls are not in the table: a NEW target found in the run is a new root for the next `snap-vcall-sites.py` pass. Per-thread and in-plan splits are not recorded.

**Run (Rodney, clean night):** asi parked at `game-copy/ts3native.asi.snapprobe`; `node tools/harness/queue.ts tools/harness/queues/snap-probe.json` (jobs `bp-snap-probe`, `ip-snap-probe`, 10 min each, env-on settings, seed 12345, `ProbeSnap.Enabled=1`). Then `node tools/harness/snap-report.ts probe-out/queue/snap-probe-<date>-bp-snap-probe-<HHMM>-ts3native.log probe-out/queue/snap-probe-<date>-ip-snap-probe-<HHMM>-ts3native.log`. Pass: no crash, `vcReady` 1, `vcFaults` 0, `vcSkipped` = 0 at arm time (else the log names the rewritten sites), frame times within noise of `bp-env-on`/`ip-env-on`. The answer: the NEW and OUT targets per site, which become docs/91 §4 / docs/82 class rows or new closure roots.

## 8. World-level inputs: copy, change signal, hook (2026-10-18)

> **Changes 2026-10-18:** new section. It folds in docs/100 (G), docs/105 §9-§10 (door mirror; §10 and ledger §55 are on master, merged after this branch was cut), docs/107 §9-§11 (`O+0x58`, footprint grid `P+0xDC`, blend layer 0), docs/108 §7 (live values), docs/109 (dead stamp reader, option bits) and docs/112 (blocked-bit writers, type bits, G build thread). Static design only; no code, no game run. New inferred claims: docs/51 §58 (I-94-17 to I-94-23).

### 8.1 What the new findings change

- **R is static except two bit fields** [verified live, docs/108 §7 F-2 and §6.6; writers verified, docs/112 §3]: at load R equals the `.world` resource `0x05CD4BB3` byte for byte (BP and IP checksums equal `rtdata-decode.ts --checksum`). After that, only the flag byte `+1` of a waypoint changes: bit 7 (blocked) through `RoutingWorld_ApplyLotUpdate 0x82B590` and `RoutingWorld_RestoreLotBlockedBits 0x82A190`, and the type bits `0x1F` (`0x10`<->`0x13`, `0x02`<->`0x15`) through the shoreline step `0x66FD00` of every G build. Bit 7 fills in lazily as lots get their first plan (0 -> 284 in a BP 10-min window), not at load.
- **G is load-time state** [verified live, BP and IP]: built once per load on the **sim** thread (688 ms BP, 505 ms IP), no rebuild in 10 min of play; sea height fixed after load. Its footprint-driven parts (10 high-cost maps, `blockedNodeMap`) still change with every footprint insert and remove (docs/100 §5-§6).
- **Two inputs were missing from the docs/91 list**: P's footprint grid `P+0xDC` (world-level records of footprints that reach outside their lot or have none; read by world cost modes 1-4 for edge bytes 2 and 7, and by goal validation) and terrain blend layer 0 (`0xC6BAB0`, a static per-world mask) [verified, docs/107 §10-§11].
- **One "input" is an output**: `O+0x58` (the path's EditPath footprint skip list) is filled by the PlanCommon tail after the search; the snapshot does not hold it (docs/93 §4.4).
- **Tuning is the compiled default** (`0x673C70`; Sims3.ini does not replace it) [verified live]. **Dead code** the search need not mirror: the A\* stamp reader `0x61C9B0` and the context bit `0x20` path `0x669530` (docs/109 §2, docs/112 §4.1). Option bits seen live: `0x4`, `0x1/0x2/0x8/0x10`, and `0x200000` in IP only (docs/108 §7 U6); they are per-request inputs, copied at START with the route options (§3.2 step 4).

### 8.2 Per input: what the snapshot copies, when, and the hook

Thread notes: `0x82B590` runs on the async worker inside `Route_PlanLocked` when the game's async jobs are on (171 of 256 calls in BP), and `LotGraph_Build` (which reaches `0x82A190`, `0x636BF0` and `0x62D9F0`) runs inside jobs too; the stubs therefore set their dirty flags with interlocked operations, and the cut copies under `+0x918` so no writer runs during the copy [inferred, I-94-23].

| Input | Snapshot copies | Change signal | Hook (owner rule: docs/68 rule 3) | docs/76 covers it? |
|---|---|---|---|---|
| **R base** (records, offset table, components) | the whole record array (`world+0x54`: `+0xC`, size `+0x10`; 0.74-1.17 MB) into an immutable R image shared by versions; the R path may parse the `.world` resource offline instead, then apply the two bit fields | world epoch, taken **after the load's G build**, not at world event `7` alone (the build may come after it [inferred, I-94-18]) | the G-build stubs below give the "load G build done" point | no: event `7` gives the epoch, not the G-build order |
| **R type bits** (`0x1F` of flag byte `+1`) | whole-array recopy (as R base) | G-build epoch: any call of `WaterGrid_Build 0x6734A0` (load message `0x38E3C1E`, build/buy exit, message `0x6EF1610`, `BuildRoutingData`; docs/100 §3.1) | after-call stubs at the call sites `0x834A82` (-> `0x6734A0`), `0x82A098`, and `0x8352F1`, `0x835390`, `0x8358B5` (-> `0x82A030`); `[ProbeValues]` owns these rel32 sites today, so one stub serves both sections (or `[RouteSnap]` takes them and `[ProbeValues]` reads its counter) | no: only one trigger (build/buy exit slot) and no load-order guarantee |
| **R blocked bits** (bit 7) | whole-array recopy when the flag is set (1 MB copy, about 50-100 µs [inferred]; at ~0.4 writer calls/s in play that is far under the 2 ms/s budget). Per-lot recopy (the lot's edge waypoint list as `0x82B590` walks it, plus the R references in the lot's portal words as `0x82A190` decodes them) is the fallback if bursts at load cost too much | any call of `0x82B590` or `0x82A190` since the last cut ("R dirty" flag; lot id recorded for the audit) [inferred complete, I-94-17] | entry detours: `0x82B590` (`83 EC 2C 56 57`, owned by `[ProbeValues]` `PV_LAZY3`: share the stub) and `0x82A190` (`8B 44 24 04 85 C0 56`, 7 bytes, no relative operand, free) [verified bytes, docs/112 §3.4] | partly (docs/112 §3.4): build/buy exit, lot add/remove and lot load (event `0x10`) yes; lazy first use, `World.LotSetEntranceByTargetPoint`, lot and world saves, the `WaterPath_Plan2` rebuild no |
| **Lazy first use of a lot** (bits restored inside a plan) | the cut forces `LotGraph_Build` (through the game's own `RoomGraph_FirstUse 0x63D2F0`, on the sim, under `+0x918`) for the request's start and goal lots if unbuilt, then `ApplyPendingUpdates` (E16), then copies [design; need depends on I-94-21] | per START, only for unbuilt start/goal lots | none new (the leaves above see the writes) | no |
| **Footprint grid `P+0xDC`** | a world-level table in the version: records by cell, grouped by lot id (lot 0 = footprints with no lot), rebuilt for dirty lot ids from the live grid at the cut (docs/107 §10.3) | lot ids passed to `PathPlanner_AddHangingFootprint 0x636BF0` (lot argument; key from `lot+0x48/+0x4C`, null = 0) and `0x62D9F0(lotId, key)`; full rebuild on `PathPlanner_Reset 0x635B70` (world load, clear, destroy = world epoch) [inferred complete, I-94-19] | entry detours on `0x636BF0` and `0x62D9F0` (not in any docs/68 row; prologue bytes and relocation to check in P1 with `hook-sites.ts --check`) [design]. Upstream writer leaves (§3.2 step 1) alone are not enough: `LotGraph_Build` and `Lot_FinishLoad` reach the grid without them | partly: object events `0x21`/`0x0E`/`9`/`0x0A` cover object moves; not the enabled bit (I-76-9), lazy lot builds or the remove at `Lot_FinishLoad` |
| **G cells, passability maps, body table, shoreline trees** | whole copy (about 0.5 MB cells + 5 x 0.26 MB maps + small trees, docs/100 §10) | G-build epoch (as R type bits) | the G-build stubs | no |
| **G high-cost maps and `blockedNodeMap`** | copy-on-write tiles of the 10 maps (64 x 64 cells, 4 KB per map tile) + the whole `blockedNodeMap` hash (small) when dirty; never recompute (stale marks after a rebuild are real, docs/100 §12.1) | the same `0x636BF0` / `0x62D9F0` calls (they reach `0x6733C0`/`0x66FB40` and `0x670510`/`0x66F6D0`); dirty tiles from the footprint's bounds widened by one G cell (insert: after the call; remove: before) [inferred exact, I-94-20]; fallback: memcmp of the 10 maps against the last image when the flag is set | the grid detours above; `[ProbeValues]` keeps its own detours on the inner writers | no |
| **Blend layer 0** | the decoded mask (bitmask, 0.5 MB for 2,048 m) per world: from resource `0x05DA8AF6` offline (`rtdata-decode.ts --blend0`) or copied from `[WM+0x54]+0x0C`, vector `+0x2C` layer 0 after load | world epoch; also re-read at build/buy exit and Edit Town exit until a play-time writer is excluded (cheap, clock paused) [inferred: edit-tool brush/undo writers not traced, I-94-22] | none new (world epoch) | yes for the world epoch (events `5`/`7`); exits through the docs/76 build/buy slot |
| **Tuning** | W `+0x8C/+0x90` and the tuning block read by `0x673C70`, once per world load; compare with the compiled defaults and log a difference | world epoch | none new | yes (world epoch) |
| **Door lock state** (not a world snapshot input) | the docs/105 §4.1 **door mirror** (managed, sim side), shipped with the request by the plan prologue; replaces the per-request proxy of E12. **Built lazily** (docs/105 §10): events only mark a lot's door mirror invalid; the plan prologue at START rebuilds it for each named lot whose mirror is invalid | invalidate on: E9 portal add, docs/76 world load and lot added, build/buy exit; refresh on `World.DoorLockStateChanged` (IC slot swap of `0x788310`) and the ts3fixes patches of `CommonDoor.AddToOwnerList`, `RemoveFromOwnerList`, `HouseholdSimChanged`; plus **one ts3fixes postfix on `CommonDoor.ImportContent`** (covers every door subclass, including the closed `ElectronicGate` import). Lock changes with no lock event (docs/105 §9.4, §10): `ImportContent` (lock type only; SelectedActor/SelectedHousehold/Pets/OwnerList come back as Anybody), save load through `PersistReader` `FieldInfo.SetValue` (covered by the world-load invalidation, also travel), lot import (bin/library placement, Edit Town rotate: Lot/object `ImportContent` runs after the native `DownloadContent_ImportLotContentsImpl 0x997540`, so after the door's portals likely exist [inferred, I-105-14/16/17]); handler-list identity by `ReferenceEquals` on a ts3fixes static (docs/105 §9.2) | managed patches and one IC slot; no native detour | invalidation points only (world load, lot added, build/buy exit, destroy event `6`); docs/76 hooks see no managed field writes. Live proof: the I-105-8 shadow diff |
| **Water options per request** (boat class, frozen ponds) | nothing world-side: the route option word picks the passability map, and `EnablePondPlanning` the ice (docs/100 §1 items 4 and 6) | per request | none | n/a |

### 8.3 Amended rows of §2.2

| Row | Amendment |
|---|---|
| E12 door permission | the docs/105 scheme (door mirror, lot mirror, plan prologue, native evaluator) replaces the per-plan proxy as the target design; the mirror is rebuilt lazily per named lot at START after invalidation, plus a `CommonDoor.ImportContent` postfix (docs/105 §9.4, §10). The proxy stays the fallback |
| E13 build/buy exit | also a G-build trigger (when `world+0x983` is set) and an `ApplyLotUpdate` caller: both reach the snapshot through the §8.2 hooks, not only through the exit slot |
| E14 lot load | per lot, `Lot_Load 0xACF520` sends routing message `0xF252DC33`, whose handler runs `0x82B590` then `0x82A190` (docs/112 §3.2); the world image is built after the load's G build (§8.2) |
| E16 pending updates | lazy first use also writes R bit 7 directly through `LotRouting_FinishBuild 0x63CAF0` -> `0x82A190`, before any pending flag is applied; the cut forces first use for the start and goal lots (§8.2) |
| E26 frozen ponds | closed (docs/100 §1 item 6): a per-request option, no world epoch |
| E28 houseboat sailing | not needed (docs/100 §8): houseboat lots sit at fixed off-map slots; only create/delete (E15). The Sim-frame half of L7 is still open (docs/100 §12.2) |

### 8.4 What the docs/76 hooks miss

docs/76 gives a world epoch (`World_FireEvent`), the build/buy slots and the object event lists. For the routing inputs above it misses: (1) every G build except the one after build/buy exit, and the order of the load's G build against event `7`; (2) R bit-7 writes from lazy first use, `World.LotSetEntranceByTargetPoint`, lot and world saves and `WaterPath_Plan2`; (3) footprint grid and G mark writes from lazy lot builds and from the footprint enabled bit `0x947590` (I-76-9); (4) every managed door field write: `CommonDoor.ImportContent` (also the closed `ElectronicGate` import and lot import after `0x997540`) and save load through `PersistReader` `FieldInfo.SetValue` (docs/105 §10). The leaf hooks and managed patches of §8.2 cover all four, which is why §2.1 keeps the writer layer as the sync source and docs/76 as attribution only.

### 8.5 Cut order (replaces §3.2 step 2 for the world-level part)

On the sim thread at START, holding `+0x918`: (1) if the world epoch moved, full build (lots, then world-level images) once the load's G build has run; (2) force first use of unbuilt start/goal lots; (3) `ApplyPendingUpdates` if `P+0x10E`; (4) flush dirty lots' quadtrees; (5) if a G build ran since the last cut: new G image and new R image; else if "R dirty": new R image; (6) grid table rows of dirty lot ids; (7) G mark tiles and `blockedNodeMap` if dirty; (8) lot images of dirty lots; publish the version. Steps 5-7 share unchanged images with the previous version.

### 8.6 Open after this pass

- I-94-18: order of the load's G build against world event `7` (count both with a sequence number in P1).
- I-94-20: high-cost cells of a footprint inside its bounds plus one cell (the polygons come from shape slot `+0xDC`, the bounds from `+0x2C`); the P3 audit compares tiles, so a miss shows as a mismatch.
- I-94-21: whether a plan that first-builds its start or goal lot restores that lot's bits before the world A\* reads them (decides whether step 2 of §8.5 is needed).
- The `0x636BF0`/`0x62D9F0` prologues and their relocation (P1, `hook-sites.ts --check`).
- I-107-19 (grid cell and record counts per world) sizes the grid table; I-107-15 (`O+0x58` non-empty rate) does not affect the snapshot.

## 9. Answers to §8 open questions (2026-10-18)

> **Changes 2026-10-18:** new section. Static only (capstone on the Steam exe, raw `E8`/`E9`/`0F 8x` caller scans, one emulator spec); no game run, no native change. Ledger: docs/51 §59 (I-94-24 to I-94-31). The §8 tables are left as written; §9.7 lists the amendments.

### 9.1 I-94-18: the load's G build runs before world event `7` [verified static; event 7 timing inferred]

Order inside `WorldManager_LoadWorld 0xC6CF80` (sim, from `IC_LoadSaveManager_LoadGame_Impl`, docs/38 §5), with the event and message sites:

| Step | Site | Thread | Routing effect |
|---|---|---|---|
| 1. Old world shut down (if one) | `WorldManager_Shutdown 0xC6B780` at `0xC6CF92` | sim | message `0x38E3C6C` |
| 2. **World event `5`** (load started) | `World_FireEvent` at `0xC6CFD6` | sim | none |
| 3. Message `0x38E3C0E`, sync send through `ScriptMessageServer` slot `+0x18` | `0xC6D154` | sim | routing handler branch `0x8353D0` (docs/100 §3.1: `0x38E3C0E` sets `+0x983`); sent only on one branch |
| 4. World init `World_LotStreaming 0xC622D0`, `RemoteCall_PostSync(0, ...)` | `0xC6D176` | main, sim waits | terrain, R load (`0x66D110`, docs/91), blend maps (`0xC2ECA0` through `0xC2EFE0`/`0xC2F000` from `0xC622D0+0x2B8`/`+0x2BF`) |
| 5. Lots: `WorldManager_CreateLotsFromLayer 0xC65E00`, then `WorldManager_LoadLots 0xC678D0` (or `0xC64630`) | `0xC6D105`, `0xC6D264` | sim | per lot `Lot_Load 0xACF520`: event `0x10` (`0xACF78B`), message `0xF252DC33` (`0xACF984`) -> `0x82B590`, `0x82A190` (docs/112 §3.2) |
| 6. `World_PostLoadInit 0xC6AEB0`, `RemoteCall_PostSync(0, ...)` | `0xC6D311` | main, sim waits | sends `0x38E3C1E` to the main `MessageServer` (`0x44C470`, slot `+0x18`) at `0xC6B07B` |
| 7. **G build**: `0x38E3C1E` sync send through `ScriptMessageServer` slot `+0x18` | `0xC6D33A` | sim | `RoutingWorld_OnMessage 0x835300` -> `0x835390` -> `WaterGrid_BuildFromWorld 0x82A030` while `+0x983` is set, then clears it |
| 8. **World event `0x0A`** (save loaded, `[mgr+0x44]` set) or **`9`** (world file) | `0xC6D3E4` / `0xC6D402` | sim | none |
| 9. `RemoteCall_PostAsync(0xC60230)`, return to the managed caller | `0xC6D424` | sim | none |
| 10. **World event `7`** (load finished), then message `0x6EF1610` posted (slot `+0x1C`) | `ScriptHost_PostWorldReady 0x769950` at `0x769993`, `0x7699CF`; from `ScriptHost_ScriptTick 0xD80BC0` (`0xD81222`, `0xD81296`, `0xD814FC`) | sim | `0x6EF1610` reaches `0x8358B5` -> `0x82A030` only if `+0x983` was set again after step 7 |

The live log (docs/112 §5.4) saw the load build at `0x835390` on the sim thread, which is step 7 (step 6 runs on main) [verified live + static]. Event `7` comes later: `0x769950` fires it only when the script host's pending-task counter `+0xA2C` (raised by `ScriptHost_AddPendingTask 0x769930` from the task starters `0xD7EE21`, `0xD7F81C`, lowered in the tick) reaches 0 and `+0xA28` is 0, then latches the counter at -1 [verified]. The load runs inside a script task (the LoadGame IC), and steps 1-9 are one synchronous call on the sim thread, so no tick can fire event `7` between them [inferred: the load task is one of the counted tasks; not traced through the managed side]. Events `9`/`0x0A` come after the build in the same call [verified].

**Answer:** world event `7` (and `9`/`0x0A`) come after the load's G build. A world image built at event `7` holds the post-build R type bits and a full G. The G-build stubs stay for the later builds: the `0x6EF1610` post right after event `7`, build/buy exit, `RoutingWorld_BuildWorldRoutingData 0x8349C0` (world save, `IC_World_BuildRoutingDataImpl`, world-file save `0xC6CC40`, message `0x7FDA974`, sender not found statically).

### 9.2 I-94-20: high-cost cells vs footprint bounds [line extent verified by emulator; bounds rule refuted as stated]

**Line rasteriser.** `WaterGrid_MarkFootprintPolygon 0x6733C0` draws the closed outline of one polygon (8-byte x, z points; edge i -> i+1, last -> first) into each selected class map with `WaterGrid_MarkB 0x670750(x0, z0, x1, z1, 1, class, record)`. `0x670750` steps in 1 m units from the start point (step count `|floor(x1) - floor(x0)| + |floor(z1) - floor(z0)| + 1`) and writes cell `trunc(v) >> 2` (4 m cells; negative values fail the unsigned bounds check). `tools/emu/specs/watergrid_mark_line_extent.py` runs it on 2,000 random segments (30% with end points on or next to 4 m lines): **every written cell lies inside the box of the two end cells widened by one cell**, only the selected map is written, the start cell always is; 147 cases write a cell outside the strict end-cell box, so the one-cell margin is needed [verified, emulator, 2,000/2,000; ftol ran its x87 path, the live SSE2 path truncates the same way]. So the cells of one `0x6733C0` call lie inside the bounding box of its polygon's vertices plus one cell.

**Polygons vs bounds.** In `PathPlanner_AddHangingFootprint 0x636BF0` the bounds (slot `+0x2C(out, 0, 0.0)`) and the stamped polygons (slot `+0xDC(0, list)`) are widened differently [verified]:

- `w` (bounds widening): 0.75 when slot `+0x5C` is true, the agent mask is not all ones and `fp+0x1C & 0xE1C` is non-zero; 0.25 when slot `+0x5C` is false and the flag argument is 0; else 0. The bounds grow by `w` on each side; these widened bounds go to the grid records and to `WaterGrid_BlockedNodesAdd 0x66FB40`.
- `r` (polygon inflation): `w`, plus `slot +0x40() + 0.25` in the first case. When `r > 0` every polygon is offset outward by `r` with `Polygon_Offset 0x832FB0` (per vertex along the corner bisector with a corner factor: constants 0.95, 1.4 and 0.5 in the code, so a vertex can move more than `r` [exact factor inferred]).

So in the first case the stamped outline can reach `slot +0x40() + 0.25` (times the corner factor) past the widened bounds, and the rasteriser adds one more cell. "Bounds plus one cell" is not a safe dirty rule [verified for the widening; whether the un-inflated `+0xDC` polygons always lie inside the `+0x2C` bounds was not checked per shape class].

**Exact rule instead:** take the dirty tiles from the polygon argument at `0x6733C0` entry (vertex box of each polygon plus one 4 m cell). `0x6733C0` is the only insert-side writer of the high-cost maps after load (docs/100 §5), and `[ProbeValues]` already detours it (docs/68): one shared stub. For removes, `0x670510` clears only recorded cells of the record keyed (owner, footprint); the stub records the tiles per key at the `0x6733C0` call (args 1-4 are that key) and marks them again at `0x670510` entry (also a `[ProbeValues]` detour). After a G rebuild the remove finds no record and writes nothing (docs/100 §12.1), so the stale key entry only over-marks. `blockedNodeMap` (`0x66FB40`/`0x66F6D0`) uses the widened bounds and stays a whole-hash copy when dirty.

### 9.3 I-94-21: first use of the start and goal lots comes before the world A\* in the same plan [verified static]

`PathPlan_WithRetries` (from `RoutingJob_Execute+0xCD9`, inside `Route_PlanLocked`) calls `PathPlanner_PlanHierarchical 0x65E0D0`. Its order:

1. Start lot (`[esp+0x50]` non-null): `LotSearch_Entry 0x640E00` at `0x65EB8E`; failure returns 5 before any world search.
2. Goal lot (`[esp+0x58]`): `LotSearch_Entry` at `0x65ECDE`, then per goal candidate at `0x65EDE5`.
3. `LotEdgePortals_Gather`/`Choose`, `ClosestWaypoints_Find`, then **`WorldAStar_Search 0x676260`** at `0x65F809` (retries at `0x65FB99`, `0x65FDA5`).

`LotSearch_Entry`, once `LotTable_Contains` passes, calls `RoomGraph_FirstUse 0x63D2F0` on the lot's outside room (`entry+0x16C`, at `0x640E66`), which calls `LotGraph_Build 0x63CB90` when the entry's built byte `+0x8C` is 0 (`0x63D310`); `LotGraph_Build` reaches `LotRouting_FinishBuild 0x63CAF0` -> `RoutingWorld_RestoreLotBlockedBits 0x82A190` (docs/112 §3.2). No function under `WorldAStar_Search` reaches `LotGraph_Build` (its callers are `RoomGraph_FirstUse`, `LotGraph_RebuildRooms` and their lot-level callers only).

The goal lot is often built earlier on the sim by `Route_GoalStep 0x75C2D0` (`LotPlanner_GoalRooms` -> `RoomGraph_FirstUse`), which `Route_PlanCommon` runs at `0x760BFA`, before `Route_PlanLocked` at `0x760DE1`; the start lot has no such earlier step.

**Answer:** yes, a plan that first-builds its start or goal lot writes that lot's R bit 7 before the world A\* reads R. A version cut before such a plan would hold the old bits, so §8.5 step 2 (force first use of unbuilt start and goal lots under `+0x918` before the copy) is needed. Intermediate lots the world path crosses are never first-built inside the plan; their bits stay as the last builder left them, which the R image copies as is.

### 9.4 I-94-22: blend layer 0 has no play-time value writer; it is reloaded [verified for the blend-map code; values equal inferred]

Code that touches the world-builder layer vector `+0x2C` after load (every function in `0xC21000-0xC24400` that indexes it, plus the 42 callers of the `+0x0C` getter `0x89F6C0`, none of which indexes `+0x2C`/`+0x30` of the result):

| Function | Caller | What it does to `+0x2C` |
|---|---|---|
| `TerrainBlendMap_RemoveWorldBuilderLayer 0xC22D70(index)` | `TerrainRender_UpdateBlend 0xC825F0` at `0xC8265C` with index **1**, when terrain flags `+0x140 & 6` are 0 | frees layer 1, never layer 0 |
| `TerrainBlendMap_CompactLayers 0xC23580` via `0xC23820` | `0xC825F0` at `0xC82B73` | frees a raw 16 x 16 block only when every byte is 0 inside the rectangle and the block lies fully in it, storing the uniform value 0: weights unchanged; also lists the layers with data in the rectangle |
| `TerrainLayerMaps_CopyRegion 0xC220B0` via `0xC22390` | `0xC24720` <- `0xC24800` <- `0xC825F0` | reads layer blocks into a texture buffer |
| `0xC223C0`, `0xC22C30` | `TerrainData_SaveBlendMaps 0xC2C740` (world-file save `0xC6CC40`) | read: presence test, serialise to resource `0x05DA8AF6` |
| `0xC23370` (via the vtable thunk `0xC23430`) | blend map destruction | frees all layers |

`0xC825F0` runs from `WorldTerrain_Update` (render side). The terrain paint helpers (`0xC223E0`, `0xC22360`, `0xC22A70`, `0xC23270`) work on the paint layers `+0x1C`. So no in-place writer changes layer-0 weights.

**But the layer object is rebuilt during play:** `TerrainData_ReloadJob 0xC62AC0` (a rendering job posted by `WorldManager_PostTerrainReload 0xC63840` on the main record queue) reloads the height map, `TerrainData_LoadBlendMap 0xC29DB0` and `TerrainData_LoadWorldBuilderLayer0 0xC29CE0`. `0xC63840` is called by `IC_World_LoadHeightMapRelatedDataImpl`, `0xB04AF0` (`IC_World_BulldozeLotImpl`, `IC_World_DeleteHouseboatLotImpl`, Edit Town `0xD42640`) and `WorldManager_LotRebuildCaller 0xC6BCC0` (world save, `IC_World_BuildRoutingDataImpl`) [verified callers]. The reload reads resource `0x05DA8AF6` again; the only writer of that resource is the save serialiser above, which writes the in-memory layer, so the content round-trips unchanged [inferred]. The map object and its block pointers change, on main, outside `+0x918`.

**Answer for the design:** copy layer 0 as decoded values (offline decode per world, or a copy at the world epoch), never a pointer into the live map. Re-check at the reload points (bulldoze, houseboat delete, Edit Town lot change, world save, `BuildRoutingData`) only if I-94-28 fails; build/buy exit needs no check (no reload there).

### 9.5 I-94-23: writer threads [verified for the routing writers; two non-routing exceptions]

| Input | Writers | Thread and lock |
|---|---|---|
| R bit 7 | `0x82B590` from `ApplyPendingUpdates` (inside `Route_PlanLocked`), `RoutingWorld_RebuildLot`, `RoutingSystem_RebuildLotRouting`, `RoutingSystem_OnExitBuildBuy`, message `0xF252DC33` (`Lot_Load`); `0x82A190` from `LotRouting_FinishBuild` (lazy first use, `LotRouting_SetEntranceTransform`) and the same message | worker only inside `Route_PlanLocked` / `RoutingJob_Execute`, under the job's `+0x918` hold (docs/112 §5.2 measured); every other caller is an IC, a message from `Lot_Load` (sim at load and lot add) or a save (sim) |
| R type bits, G | `WaterGrid_Build 0x6734A0` from `0x82A030` (`0x835390`, `0x8358B5`, `0x8352F1` build/buy exit) and `0x8349C0` (`LotRebuildCaller`, `0xC6CC40`, message `0x7FDA974`) | sim (load: §9.1 step 7, live); none of the 24 `+0x918` acquire sites is in `0x82A030`, `0x8349C0`, `0x835300` or `0x835240`, but the cut runs on the sim, so a sim writer cannot overlap it |
| Footprint grid `P+0xDC`, high-cost maps, `blockedNodeMap` | `0x636BF0`, `0x62D9F0` and their inner writers | sim writer leaves, or inside `LotGraph_Build` on the worker under `+0x918` |
| Blend layer 0 | reload `0xC62AC0` (§9.4) | **main render job, no `+0x918`** (exception 1; values unchanged, so a value copy stays right) |
| World load | `World_LotStreaming 0xC622D0` (R base load, sets up the planners) and `World_PostLoadInit 0xC6AEB0` | **main with the sim waiting** in `RemoteCall_PostSync` (exception 2; before the world epoch, no plan runs) |

**Answer:** every routing-input writer of §8.2 runs on the sim thread or on the worker inside the job's `+0x918` hold, so a cut on the sim holding `+0x918` sees no concurrent routing write. The two exceptions are outside the cut's reach: the blend reload is copied by value (§9.4), and the world-load steps finish before the epoch. Sim-side writers (lazy first use from the goal step, G builds) take no `+0x918` in the vanilla game outside our `[RouteGate]` stubs; they can overlap a running job today, which the snapshot removes for the worker's reads.

### 9.6 Detour prologues of `0x636BF0`, `0x62D9F0`, `0x82A190` [verified]

| Function | First bytes | Stolen bytes | Relative operand | Branch into bytes 1-4 |
|---|---|---|---|---|
| `0x636BF0` | `55 8B EC 83 E4 F0` (push ebp; mov ebp, esp; and esp, -16) | 6 | none | none |
| `0x62D9F0` | `83 EC 4C 53 55` (sub esp, 0x4C; push ebx; push ebp) | 5 | none | none |
| `0x82A190` | `8B 44 24 04 85 C0` (mov eax, [esp+4]; test eax, eax) | 6 (docs/112's 7 with `56` also works) | none | none |

Checked: no `E8`/`E9`/`0F 8x` rel32 in `.text` and no jump inside each function targets entry+1..+4 (callers: 8, 3 and 2). `node tools/hook-sites.ts --check` passes today (347 addresses, 0 missing) because it scans only `native/ts3native` sources; it will need docs/68 §2 rows for `0x636BF0`, `0x62D9F0` and `0x82A190` in the commit that adds the `[RouteSnap]` stubs, and the `[ProbeValues]` row gains "shared with `[RouteSnap]`" for `0x82B590`, `0x6733C0` and `0x670510`. No third-party window in docs/68 §1 is near these addresses.

### 9.7 Changes to the snapshot design

- §8.2 R base / G rows: the world image may be built at world event `7` (the load's G build precedes it); keep the G-build stubs for the later builds (§9.1).
- §8.2 high-cost row: dirty tiles come from the polygon argument at `0x6733C0` (vertex box plus one 4 m cell) and from a per-key tile list replayed at `0x670510`, both on the existing `[ProbeValues]` detours; the footprint bounds are not used for these maps (§9.2). The `0x636BF0`/`0x62D9F0` detours stay for the grid table.
- §8.5 step 2 (force first use of unbuilt start and goal lots) is required (§9.3).
- §8.2 blend row: value copy only; no blend epoch needed while I-94-28 holds (§9.4).
- §8.2 thread note: confirmed for routing writers; the two exceptions in §9.5 need no lock.

## 10. Status: P1 change counters built (0.25.0, counters v46)

> **Changes 2026-10-18:** new section. Build only; the game was not run. Ledger: docs/51 §63 (I-94-32 to I-94-36).

**What is built [verified offline]:** `native/ts3native/routesnap.cpp`, `[RouteSnap] Count=0` (off by default; `Audit`, `Enabled` and `Mode` of §4.2/§5 stay for P3+). Every hook counts and runs the game's code unchanged.

| Layer | Sites | Counts |
|---|---|---|
| Writer leaves (19 rows) | W1-W6 (12 sites, §2.2 / docs/83 §6), W4 `0x63CB90`, `0x8321F0`, `0x831BA0`, `0x82A0C0`, `0x635700`, `0x829D50`, R bit 7 `0x82B590` and `0x82A190`, footprint grid `0x636BF0` and `0x62D9F0`, high-cost maps `0x6733C0` and `0x670510` (§9.2), the five G-build call sites (§8.2), blend layer 0 reload `0xC62AC0` (reload pass only, §9.4) | per row and thread class (sim / route worker / other); on the sim also per source (none, IC, world event, object event, `Footprint_SetEnabled`); G builds per call site with a sequence number against world event 7 (I-94-18 live check) |
| IC slots (39, no code bytes) | the 16 writers of E8-E11 and E24, plus `DestroyPortalHandle`, `MarkRoomForUpdate`, discourage and private footprint add/remove, `OnExitBuildBuy`, `LotClusterTrees`, `DoorLockStateChanged`, the four E5 footprint/hidden ICs, `BuildRoutingData`, bulldoze, houseboat create/delete, `LotSetTerrainHeight`, `LotSetEntranceByTargetPoint`, and the four route ICs | calls, and sim leaf hits inside each IC's frame; the route ICs are the START points |
| World events | `World_FireEvent 0x78F2B0` entry (id = argument 2, read at `0x78F2B3` [verified]) | calls by id (0-94, 95+), off-sim calls; id 7 registers the object nodes |
| Object events | head node (priority `INT_MAX`) and tail node (`INT_MIN`) for 4, 6, 9, 0x0A, 0x0B, 0x0E, 0x18, 0x21 through `ObjectEvents_Register` | calls by id and thread class; writes between head and tail count as object-event caused |
| E4 | `Footprint_SetEnabled 0x947590` entry | calls by class; a source frame |
| START windows | sim entries of the four route ICs | distinct known dirty lots since the previous START (0..5, 6-10, >10), writes per START, STARTs with writes whose lot is unknown, set overflow |

**One owner per address (docs/68 rule 3):** W1-W6 are counted inside the `[SteerRO]` W stubs when that section is on, `0x63CB90` inside `[RouteGate]` site 3, `0x82B590`, `0x82A190`, `0x6733C0`, `0x670510` and the G-build call sites inside the `[ProbeValues]` stubs when that section is on (and `0x62E680`/`0x63CB90`/`0x80E5B0` when it owns them); otherwise `[RouteSnap]` installs its own pass-through stub with the same bytes. The owner's stub calls `SnEnter` first, before any probe swaps the return address.

**Attribution rule:** a sim-thread leaf hit takes the innermost open source frame (IC slot shim, `World_FireEvent`, `Footprint_SetEnabled`; return-address frames as in `values.cpp`), else "object event" while a head node ran and its tail node did not, else it is **unattributed** and its caller (the leaf's return address) goes into a 16-key table. Worker and other-thread hits are counted by class only.

**Lot per leaf:** only leaves that carry their lot (`0x82B590`, `0x82A190` lot pointer argument 1; `0x636BF0` argument 2; `0x635700`, `0x829D50`, `0x62D9F0` lot id arguments 1-2) mark a lot [inferred for the last four, I-94-33]; the others count as "unknown lot" writes in the START window. Resolving a footprint to its lot is P3 work.

**G1 as the report applies it** (`tools/harness/snap-counters-report.ts`, numbers are after - before over the 10-minute window): (1) `snUnattributed` = 0, else OPEN with the caller list to explain or add as a source; (2) writer-row hits (rows 0-16, all threads) ≤ 200/s; (3) known dirty lots per START ≤ 5 at p95 (printed with the share of STARTs that also had unknown-lot writes); (4) no writer-row hit on an "other" thread (the route worker inside its job's `+0x918` hold and the blend reload on main are the known exceptions, §9.5); (5) probe complete (`snReady` bits 0-2, 0 skipped sites, 39 slots, no frame overflow). PASS only when all five pass; any FAIL stops before P2/P3.

**Checks:** selftest `TestRouteSnap` (layout, attribution per source, unwound frames, object nodes on a fake list with the game's priority rule including a stopped chain, START windows and overflow, blend phase filter, G sequence, live entry detour, call site and IC slot on dummies, sharing through the `[SteerRO]`, `[RouteGate]` and `[ProbeValues]` stubs, shared rows carry the owner's bytes); `steerro-test` seeds 1 and 2 x 3000 pass and check all 32 sites and 39 slots on the image; `hook-sites --check` 0 missing; `snap-counters-report.ts --self-check`; `check-docs-code` ok.

**Run (counts only, daytime OK):** asi parked at `game-copy/ts3native.asi.snapp1` (sha256 `cd21cb12330041eef8f9f5b2d1c4aae8d641284e87906db999cce30aebc08dfc`); `node tools/harness/queue.ts tools/harness/queues/snap-p1.json` (jobs `bp-snap-p1`, `ip-snap-p1`: env-on settings, Spring saves, seed 12345, 10 min, `RouteSnap.Count=1`), then `node tools/harness/snap-counters-report.ts probe-out/queue/snap-p1-<date>-bp-snap-p1-native.json` and the same for IP.

**Cost [inferred]:** one pushad stub per hit (~50-100 ns) at the docs/94 §3.3 estimate of 20-60 writer calls/s plus W6 cache rebuilds, the event rates of docs/76 (0x0E is the high one: two extra cdecl calls per transform publish), and ~6 STARTs/s; well under 1 ms/s of sim time. The build/buy and portal repros of §4.5 (`bp-snap-events-bb`, `bp-snap-events-portal`) are not in this queue: they need new agent actions (memory: repro actions) and add only the E8-E13 slot counts.

## 12. P4 design: option G redirect (2026-10-18)

> **Changes 2026-10-18:** new section. Static design only: no code, no game run. New static facts come from capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67): the bytes of the five patch sites, a raw 4-byte reference scan of the whole image for the three redirected addresses, and the call shape at `0x64F4D3`. Sources: docs/101 §2-§3 (globals census, the five-site thunk form), docs/109 §2 (stamp reader dead), docs/113 (U1/U3 probe, not run), docs/98 §3.3-§3.4, docs/99 §12.5, docs/93 §4, §8-§9, docs/108 §7 F-7, docs/92 §3.2, docs/87 §10 (A10), `ts3native.cpp` `ServeAsync`, `routegate.cpp` (`kRgSites`, `t_rg`), `steerro.cpp` (S2 clone). The footprint and quadtree image format is decided in §11 (parallel branch); §12.8 names where it plugs in. Ledger: docs/51 §68 (I-94-60 to I-94-69; ids start at 60 to stay clear of rows §11 may add).

### 12.1 Summary

- **Five instruction sites, four thunks** (§12.2). docs/101 §3 names the sites; docs/94 §3.4 only gives the rule. Two sites hold the A\* generation stamp (`AStarSearch_Init 0x61E360` and `AStarSearch_NodeState 0x61E030`). One stamp site is dead code (`0x61CC73` in `AStarSearch_StampReader`, docs/109 §2): we patch it anyway because it costs nothing. One site is the clip scratch (`0x66AC37`) and one is the routing world root (`RoutingWorld_GetImpl 0x836200`). The raw scan finds no other reference to these three addresses that the search could run (§12.2.2) [verified].
- **Corrections to docs/101:** the stamp `add` is **8 bytes** (`66 83 05 C4 80 15 01 01`), not 9 [verified]. **`[RouteGate]` already owns `0x61E360`** (gate site 8, a relocating detour), so the stamp patch must be shared with it (docs/68 rule 3).
- **Thread rule:** each thunk compares the TEB thread id (`fs:[0x24]`) with `g_snapRunTid`. That value is non-zero only while the route worker runs the game's search on the working copy. A match takes the per-job context `g_snapCtx`; any other thread gets the original global. No TLS lookup sits on the hot path, which matters because `AStarSearch_NodeState` runs once per node.
- **The stamp lives with the working copy, not the job** (§12.3). Nodes keep their stamp field from one search to the next, so restarting the stamp at each job would make old nodes look current. The scratch object belongs to the worker. The root belongs to the job.
- **How the copy runs** (§12.4, §12.5): `Mode=1` adds a shadow step to `ServeAsync`. The live A10/`PlanAsync` job runs as today, and its `Route_PlanLocked` inputs and outputs are captured. After the live lock is released and `J_DONE` is published, the worker calls the game's `Route_PlanLocked` trampoline with `ecx` = the working copy's world and a private path list. The door answers are replayed from the live run at the `0x64F4D3` call site, so the clone run makes no managed calls. The worker then compares the two results and keeps the game's answer. The handshake is the same one A10 uses: START, then READY, then collect. Collect does not change in `Mode=1`.
- **Every ts3native stub passes through during a clone run** (§12.4.3). This covers the gate, W1-W6/QL, `[ProbeValues]`, `[RouteSnap]` leaves and `[Verify]`. The `[RouteSnap]` leaves instead mark the touched lot "worker-dirty", and that lot is rematerialised from its image before the next job.
- **Effort:** P4 (G) is **~8-13 h** hands-on plus two 10-min shadow jobs. This replaces §5's 4-8 h. Most of the difference is the stub pass-through rule, the door replay and the output capture, which §5 did not cost (§12.9).

### 12.2 The patch list

#### 12.2.1 Sites [bytes verified, Steam 1.67]

| # | Address (function) | Bytes now | Instruction | Redirects (live -> copy) | Patch kind | Thunk (behaviour) | Owner today |
|---|---|---|---|---|---|---|---|
| T1 | `0x61E360` (`AStarSearch_Init`, first instruction) | `66 83 05 C4 80 15 01 01` | `add word [0x11580C4], 1` | stamp word `0x11580C4` -> `ctx->work->stamp` | call-site redirect: `E8 rel32` to `SnStampAdd` + `90 90 90` (8 bytes) | sim/other threads: `add word [0x11580C4],1`. Clone run: `add word [ctx.stamp],1`, then if it wrapped to 0, set it to 1 and set `ctx.work->wrap`. Saves no flags: the next flag writer is `or ecx,-1` at `0x61E375`, and nothing reads flags before it [verified, I-94-61] | **`[RouteGate]` site 8** (relocating detour, 8 stolen bytes). Shared: when site 8 is installed, its trampoline runs `call SnStampAdd` in place of the relocated `add`, and `routegate.cpp` takes that instruction from `routesnap.cpp`. Otherwise `[RouteSnap]` writes the 8 bytes itself |
| T2 | `0x61E030` (`AStarSearch_NodeState`, first instruction) | `A1 C4 80 15 01` | `mov eax, [0x11580C4]` | stamp read -> `ctx->work->stamp` | call-site redirect: `E8 rel32` to `SnStampLoad` (5 bytes) | eax only: the live dword on other threads, the context dword in a clone run. Only `ax` is used after it (`cmp ax,[esi+0x30]`, `movzx edi,ax`) [verified]. Flags are free: `sub esp,0x10` follows | none (not in docs/68, `native/`, `tools/probe`, `tools/frida`) |
| T2' | `0x61CC73` (`AStarSearch_StampReader`, mid-function, target of `je` at `0x61CC68`) | `A1 C4 80 15 01` | `mov eax, [0x11580C4]` | as T2 | call-site redirect to the same `SnStampLoad` (the branch lands on the instruction start, which is fine) | as T2. Dead in 1.67 (docs/109 §2, I-109-3). Patched so a future caller cannot read the live stamp from a clone run; a counter in the thunk that should stay 0 (`snDeadStamp`) | none |
| T3 | `0x66AC37` (`QuadTreeCell_ClipScratch 0x66AB70`, mid-function) | `B9 80 85 15 01` | `mov ecx, 0x1158580` | clip scratch object (two vectors, 0x20 bytes) -> `ctx->worker->clip` | call-site redirect: `E8 rel32` to `SnClipEcx` (5 bytes) | ecx only. `0x1158580` on other threads, the worker's private object in a clone run. eax and edx are already pushed (`0x66AC30..36`) and must be kept. `mov` leaves flags alone, and so does the thunk | none (`watch-stalk.js` reads it under Frida only: never run both) |
| T4 | `0x836200` (`RoutingWorld_GetImpl`, the whole body; `RoutingWorld_Get 0x8292D0` is `jmp 0x836200`) | `A1 28 91 1D 01 C3` | `mov eax, [0x11D9128]; ret` | routing world root -> `ctx->world` (the working copy's world object) | body replacement: `E9 rel32` to `SnWorldGet` + `CC` | eax only: `[0x11D9128]` on other threads, `ctx.world` in a clone run. 32 call sites in the search (docs/101 §2.2) | none. `steerro.cpp` calls `0x8292D0` from the sim (unaffected). `values.cpp` and the tests read or write `0x11D9128` directly, which the thunk still serves |

`this` of `Route_PlanLocked` (`ecx`) is the second root (docs/101 §3). It needs no patch, because the worker passes the working copy's world when it calls the trampoline.

#### 12.2.2 Completeness of the redirected addresses [verified, raw 4-byte scan of the whole image, I-94-60]

| Address | References | Meaning |
|---|---|---|
| `0x11580C4` | `0x61E363`, `0x61E031`, `0x61CC74` | T1, T2, T2': all three |
| `0x1158580` | `0x66AC38`, `0xF91F63` | T3; `0xF91F63` is the static destructor at exit |
| `0x11D9128` | `0x836201`, `0x836224`, `0x83626A`, `0x8362F2` | T4; set in the routing system init `0x836210`, cleared at teardown `0x836260`, null-tested by the setter `0x8362F0` (which sets byte `0x11D912C`). None of the three is in the search closure |

The patch set closes the docs/101 §2.2 must-redirect items only if docs/101 §2.2 is the whole write set. The docs/113 U1 probe checks that; it has not been run yet. Gate (§12.7): U1 verified, or the K3 offline section plus the shadow compare showing no M2/M3, before `Mode=1` counts toward G4.

#### 12.2.3 Thunk mechanics [design]

- Each thunk is four to six instructions. For example: `mov eax, fs:[0x24]`, `cmp eax, [g_snapRunTid]`, `jne live`, then the context load. (T3 and T4 use the register they return in as scratch. T1 needs one push/pop because it returns nothing.) Cost on other threads: one TEB read, one compare and one predicted branch, about 1-2 ns. T2 runs per A\* node, so this is the hot one [inferred].
- `g_snapRunTid` and `g_snapCtx` are written only by the worker. It sets them right before the trampoline call and clears them right after, and also in the exception handler. Another thread can see a stale non-zero value, but its own thread id never equals the worker's, so it always takes the live path. That makes the selector safe without fences [design, I-94-62].
- Install: `[RouteSnap] Mode>=1` with `Enabled=1`. Before writing, each site's bytes are compared with the table above; a mismatch refuses P4 as a whole (`snP4Ready` bit cleared, logged with the site), and plans run as today. T1 is installed through `[RouteGate]` when that section owns the site, so the gate stub's relocated copy runs the thunk call; a selftest checks that the trampoline holds the call. docs/68 gains rows for T2, T2', T3 and T4, and the site 8 row gets "shared with `[RouteSnap]` P4". `hook-sites.ts --check` must pass.
- Uninstall at world close (docs/60 §2.5 drain point): restore the bytes only when no clone run is active (`g_snapRunTid == 0`, checked under the job lock).

### 12.3 Per-job and per-copy state

#### 12.3.1 The context

`SnapCtx` (one per job, in the job slot) points at `SnapWork` (one per working copy) and `SnapWorker` (one per worker thread). The asm thunks read them through `g_snapCtx`. C++ code on the worker also has `t_snap` (`__declspec(thread)`, the pattern of `t_rg` in `routegate.cpp`), so stubs can test "am I in a clone run" without the thread id compare.

| Field | Lifetime | Source | Read by |
|---|---|---|---|
| `world` | job | the working copy's routing world object for `ver` (§12.8) | T4, `ecx` of the trampoline call |
| `ver` | job (refcount held from START to the end of the shadow) | the cut at START (§8.5) | materialiser, epoch check |
| `work->stamp` (u16 in a dword) | working copy, monotonic across jobs | 0 after a full materialise (every node stamp field reset to 0 by the materialiser), then T1 increments | T1, T2, T2' |
| `work->wrap` | working copy | T1 when the stamp wraps | after the job: all lots of the working copy rematerialised and stamps reset (the game itself never resets at wrap; we do because our copy restarts from 0 on every rematerialise and could otherwise meet an old stamp sooner) |
| `worker->clip` (0x20 bytes) | worker thread | at worker start: zeroed vectors, with the allocator words copied from the live object at `0x1158580`, so the game's own clear and refill (`0x67C070`) allocate and free through the same allocator. The exact layout is read in the build [inferred, I-94-63]; freed at worker exit through the game's free (`0x4E3F70`, the static destructor's call) | T3 |
| `args` | job | captured at the live `Route_PlanLocked` entry on the worker: `starts` and `goals` vectors deep-copied, `agent`, `doPlan`, the options object copied (length taken from the copy in `0x81D440`), `guidLo/Hi` | trampoline call |
| `outList` | job | a private EASTL list head laid out like `route+0xE0` (docs/93 §4.1), empty (I-94-67); `out1`, `out2` private ints | trampoline call; compare; release afterwards (path vtable unlink slots and refcount release as `0x757250` does, list nodes through `0x866E90`) |
| `live` | job | captured at the live `Route_PlanLocked` leave: the bool, `out1`/`out2`, then per path in `route+0xE0`: status `+0x94`, `PathType +0xB0`, object id `+0xB8`, `PortalStartPos`, node count, per node xyz, `+0x20`, `+0x24`, flags `+0x2C`, the path length `+0x8C`, blocker records `+0xD0..+0xD8` | compare |
| `doors` | job | (9 argument dwords -> eax) of every `World_FirePortalLockedQuery 0x791C00` call the live run made on the worker (call site `0x64F4D3`, cdecl, `add esp,0x24` [verified]) | replay at `0x64F4D3` in the clone run |
| `abort` | job | first reason that voids the compare (§12.6) | compare -> M4 |
| `workerDirty` (lot keys, max 64, overflow = all) | working copy | `[RouteSnap]` leaf stubs hit inside the clone run | rematerialise before the next job |

#### 12.3.2 Every global the search writes (docs/101 §2.2) under G

| docs/101 item | Class | Under G |
|---|---|---|
| #1 stamp `0x11580C4` | S | T1/T2/T2', per working copy |
| #2 clip scratch `0x1158580` | S | T3, per worker. U3 (docs/113) says how often it runs; it is patched either way |
| #3 quadtree count `0x11CF168` (`0x664650` +1, `0x666350` -1) | C | not redirected. The materialiser creates and destroys quadtrees on the worker, so the non-atomic live count drifts more than today. Nothing in the closure reads it except as the new tree's serial `+0x50` (I-101-3). A drift is harmless. If P4 shows a serial reader, give the worker its own count through two more call-site thunks |
| #4-#6 pools `0x11581DC`, `0x1165B80/8C`, `0x1165D48` | P | none. Each pool takes its own lock; the clone run and the sim share them, as the async jobs do today |
| #7-#10 cached flags | L | none (idempotent) |
| #11-#27 statics | L/I | **warm-up check before the first clone run:** the sim must have run at least one plan of each kind in this load, and the guard bytes of #12 (`0x11CF010`) and #14 (`0x11CF02C/30`) and guard bit 0 of #27 (`0x11D9060`, added 2026-10-19, docs/109 §8.5) must be set; otherwise the clone run is refused (M4 `cold`; `P4Cold` in `p4.cpp`, selftest covers #12 and #27). #14 takes its value from field `+0xB8` of the object being built, so it must be set by the live data first. A clone-set value would equal it only if the materialiser copies `+0xB8` exactly [inferred, I-94-64] |

Heap state (docs/101 §2.3) under G: H1-H8 and H12-H14 are private because they hang off the cloned root. H9 (the portal-lock flag rewrite `0x632460`) and H10 (lot `+0x8F` clear) write the working copy, and their leaves make the lot worker-dirty, so the next job starts from the image again. H11 is per job. H15 (lot index rebuild under `WM+0x228`) stays live and locked. H16-H17 are per request. H18 is atomic. H19 (`LockedDoorsDuringPlan`) is not touched by the clone run (door replay, §12.5).

### 12.4 How the worker job gets the copy

#### 12.4.1 START (sim, inside the existing `PlanAsync`/A10 START, before the job is posted)

1. If `[RouteSnap] Enabled=1` and P4 is ready: run the §8.5 cut under `+0x918`, then take a reference on the new version: `j->snap.ver`. A cut that fails (memory cap, set overflow while the epoch moves) leaves `j->snap.ver` null, and the job runs as today (M4 `nover`).
2. Shadow sampling: arm `j->snap` for 1 in `ShadowEvery` jobs (default 1). Skip it if the slot table holds another queued job (`snShadowBusy`), so the shadow never delays a live job's start. A skipped job releases its version at once.
3. The request inputs are not copied here. `Route_PlanLocked`'s arguments are built by `Route_PlanCommon` inside the job (the goal step runs there, docs/87 §10), so they are captured at the worker's live `Route_PlanLocked` entry (§12.3.1 `args`). This keeps the compare boundary at `Route_PlanLocked`, which is docs/93's boundary.

#### 12.4.2 Worker (`ServeAsync`, `Mode=1`)

1. Live part as today: take `+0x918`, `CallIc`, release. Inside it, the `Route_PlanLocked` detour sees `t_snap.armed`, so it captures `args` at entry and `live` at leave, and the `0x64F4D3` stub (shared with `[ProbeValues]`, which owns that call site) records `doors`.
2. Publish `J_DONE` and signal as today. The sim's collect runs from here on, unchanged. The job's `SnapCtx` moves out of the slot into the worker's shadow queue, so collect and slot reuse never wait for the shadow.
3. Shadow part, with no live lock held:
   - **Epoch check.** `ver->epoch` must equal the live world epoch now, or the run is refused (M4 `epoch`).
   - **Materialise.** Bring the working copy to `ver`: rebuild every lot whose image pointer changed since the last job, or that is in `workerDirty`, or every lot after `work->wrap` (§12.8).
   - **Arm.** Set `t_snap`, `g_snapCtx = ctx`, `g_snapRunTid = GetCurrentThreadId()`. Save the FPU state and load the job's (`x87`, `mxcsr`, as `ServeAsync` does).
   - **Run.** Call `g_planOrig(ctx->world, 0, &ctx->outList, args...)` inside `__try`. `Route_PlanLocked` takes the working copy's own `+0x918`. `LockAcquireHook` sees a lock that is not `g_rgLock`, so it passes the call straight through.
   - **Disarm.** Clear the selector and the FPU state, then compare (§12.6), release the private paths and release `ver`.
4. Any exception in step 3 is caught: log the fault address, disarm, throw the working copy away, set `snP4Ready = 0` for the rest of the session (crash-cause rule: our code is at fault until proven otherwise), and count `snShadowFault`.

#### 12.4.3 Pass-through rule for our own stubs during a clone run

Every ts3native stub whose code can run inside the search must test `t_snap.running` first. When it is set, the stub calls the original and does nothing else: no gate counting, no `+0x918` take or borrow, no QL hold, no tripwire, no probe value, no dirty mark on the live sets. One exception: the `[RouteSnap]` writer leaves add the lot to `workerDirty`.

| Module | Stubs in the closure | During a clone run |
|---|---|---|
| `routegate.cpp` | 15 gate sites (site 8 = T1 host), `LockAcquireHook`/`LockReleaseHook` | pass through; T1 still runs its thunk |
| `steerro.cpp` | W1-W6, QL, tripwires T4/T-S7 | pass through (the clone writes are not game data, and the sim's S2 clone must not see a QL hold) |
| `values.cpp` (`[ProbeValues]`) | 43 sites incl. `0x82C7AD`, `0x63257E`, `0x632595`, `0x64F4D3`, `0x6733C0`, `0x670510` | pass through, except `0x64F4D3`, which replays (§12.5) |
| `routesnap.cpp` | writer leaves, G-build sites, `World_FireEvent` | leaves -> `workerDirty`; `World_FireEvent` from a clone run is a tripwire (`snCloneEvent`, M4 `event`): docs/92 finds no event other than 59, and 59 goes through `0x791C00` |
| `verify.cpp`, `rtsites.cpp`, `lotfd.cpp`, `slotro.cpp`, `discourage.cpp` | any of their sites hit from the worker | pass through (corrected 2026-10-18, §17.4 D1: except the `rtsites.cpp` #1 and #5 worker guards and `[HeightCache]`'s `0xC2A570` in-flight count, which keep acting) |
| `InvokeHook` (managed proxy) | — | a call from a clone run is a tripwire (`snCloneManaged`, M4 `managed`); it is still served so the game does not hang |

### 12.5 Answers and the handshake

- **`Mode=1` (P4, shadow).** No change on the sim side. START, READY, CANCEL and collect (`IcCollect`) stay exactly as A10 built them, the game's answer is the only one handed back, and the shadow result is only compared and counted. A CANCEL or route destroy after `J_DONE` does not affect the shadow, because the shadow holds only its own copies (`args`, `live`, `doors`, `outList`).
- **Door replay.** At `0x64F4D3` in a clone run, the stub looks up the 9 argument dwords in `doors`. On a hit it returns the recorded eax without calling `0x791C00` (I-94-68). On a miss (the clone asks about a portal the live run did not ask about, which means the paths already differ) it returns 0 (`NoListeners`, the native answer is kept) and sets `abort = door-miss`. A miss is counted with the portal id and logged on the first 20. The clone therefore makes no managed call, does not touch `LockedDoorsDuringPlan` (H19), and needs no owner-thread pump after `J_DONE`.
- **`Mode=2` (P5 preview, not built in P4).** The job runs only the clone part, before `J_DONE`, with no live `+0x918` held. It writes into the real `route+0xE0` (the route is owned by the job during A10 as today). Door answers then come from the docs/105 evaluator or the per-request proxy, because there is no live run to replay. This needs I-94-66: paths built on the copy hold no pointer into the working copy.

### 12.6 Shadow compare and fallbacks

**Compare at the end of the shadow (worker).** Under G the same code runs on the same data, so the default rule is **exact**. Tolerances are logged but count only for the classes shown below.

| Field | Rule | Class |
|---|---|---|
| `Route_PlanLocked` bool, path count, per path status `+0x94` | equal | M3 |
| `out1` (start index), `out2` (goal index) | equal; if the goal differs and both goals are in the tie set, the class is M1-tie. The tie set comes from the docs/108 F-7 hook (goal pick, owned by `[ProbeValues]`), which records the candidate scores in both runs. Ties are real: 2 in 8,257 IP plans | M2 / M1-tie |
| per path `PathType`, object id, `PortalStartPos` | equal | M2 |
| node count; node xz | equal; xz bit-equal, with the ULP distance logged on a difference (threshold 1e-3 for the docs/94 §4.3 M0 class) | M2 |
| node y | **bit-equal** (docs/99 §12.5: y feeds every follow reader; inner nodes are -100, start and goal as built) | M2 |
| `+0x20` segment length, `+0x24` running length, `+0x8C` path length | bit-equal (docs/99 §12) | M2 |
| node flags `+0x2C` | equal | M2 |
| blocker records (status 16) | id, type equal; location bit-equal | M2 |
| door questions | the clone asked exactly the live set (no `door-miss`, and every recorded entry was used) | M2 if a recorded entry was unused |
| time | `snShadowUs` against the live `Route_PlanLocked` time | report (G4: worker time ≤ game plan time) |

Classes follow §4.3. Under G, M1 (a different node list at the same cost) and M1-tie are logged as **order defects of the working copy** (§12.8). They are not allowed to pass silently: G4 counts them as M2 unless they are tie-set members and their rate is not above the live F-7 tie rate. M5 (staleness) is recorded when a dirty mark for a lot on either path arrived between the cut and the live plan's entry (`[RouteSnap]` keeps the per-lot mark sequence). An M5 job's mismatches count as M5, not M2/M3.

Logging: counters `snShadow`, `snM[6]` + `snM1Tie`, `snAbort[reason]`, `snShadowUs`, `snMatUs`, `snShadowBusy`, `snShadowFault`, `snDeadStamp`, `snCloneManaged`, `snCloneEvent`, `snP4Ready` (reserve a counter version, docs/registry.md). One JSONL record per M2/M3/M1-tie with the args, the version id, both results and the dirty marks since the cut, capped at 20 per run with `DumpOnMismatch=1` (§4.3).

**Fallbacks.** In every case the game's answer from the live run stands. In `Mode=2` the job is marked `J_SKIPPED`, so the collect runs the plan sync, as `ServeAsync` already does for a changed world.

| Condition | Where detected | Result |
|---|---|---|
| P4 not ready (site bytes differ, a site is owned by an unknown hook, P3 audit off, an earlier fault) | install / START | no shadow; `Mode=2` stays the plain live job |
| no version (cut failed, memory cap 64 MB, `[RouteSnap] Enabled=0`) | START | M4 `nover` |
| version stale: `ver->epoch` ≠ live epoch, or `rtClosing`/`rtBusy` set | worker, before materialise | M4 `epoch`; `Mode=2` sync |
| cold statics (§12.3.2) | worker | M4 `cold` |
| materialise failed (allocation, unknown footprint vtable, §11's own checks) | worker | M4 `mat`; working copy dropped |
| request outside the supported set (kinds not in `Kinds`, a world with a sailing houseboat lot, options with bits not seen live, e.g. `0x200000` until checked) | START / worker | M4 `unsupported` |
| door miss, managed call, world event, live lock acquire from the clone run (`LockAcquireHook` sees `g_rgLock` with `t_snap.running`) | stubs | M4 with the reason; the live-lock case also logs the caller (a design breach: the clone reached live routing data) |
| exception in the clone run | `__try` | M4 `fault`, P4 off for the session |

### 12.7 Gate G4 as applied to G

> **2026-10-19:** the rule as the report applies it, with the tie classes, is §13.2; K3, K4 and K5 as built are §13.3.

G4 (§5) needs all of the following:

- ≥ 10k compares per world (BP, IP); M3 = 0; M2 = 0; M1-tie no more often than the live F-7 tie rate; M4 < 5%;
- `snShadowFault` = 0, `snCloneManaged` = 0, `snCloneEvent` = 0, live-lock aborts = 0, `snDeadStamp` = 0;
- worker shadow time ≤ the game's plan time at p50 and p95;
- U1 (docs/113) verified, or K3 offline plus the 10k-compare shadow, as the stand-in;
- steerro-test K3 (G) and K4 pass. K4 puts a guard page on the mapped live routing heap and checks the clone run never touches it.

### 12.8 Where §11 (footprint and quadtree images) plugs in

§11 picks the image strategy. The G redirect needs these properties from it, whichever strategy it picks:

1. **Game layout with private pointers.** The working copy is a routing world object whose fields the search reads at the game's offsets: lot planner P `+0x95C` with lot table `P+0x48`, Q `P+0xBC`, water planner `+0x960`, flags `+0x974..+0x983`, the footprint grid `P+0xDC`, R `+0x54`, and G. Every pointer reachable from it is private, apart from the non-routing roots docs/101 lists (world manager, object service, physics, terrain, allocators). The footprint lock-key pointer `fp+0x20` must point at a copied key set (docs/101 §1), not the live object's component.
2. **Insertion order kept.** Cell entry lists, neighbour vectors (docs/98 §3.3, insertion order) and lot table bucket order must equal the live order. Otherwise ties break differently, which is the M1/M1-tie class.
3. **Polygon caches prebuilt** (H7). A lazy rebuild (`0x80E5B0`) in a clone run reaches live owner objects and their `X+0xA0` lock (docs/82). The `[RouteSnap]` stub on it counts `snClonePolyRebuild` and aborts (M4 `poly`).
4. **Node stamps reset to 0** on every materialise (§12.3.1).
5. **A registry of working-copy objects** (or a private heap) so that K4 and the live-lock tripwire can tell copy memory from live memory.
6. **Rematerialise by lot.** The working copy rebuilds only the lots whose image changed, plus `workerDirty`. The materialise time goes into `snMatUs` and is charged to the worker, not the sim.

If §11 picks per-footprint records instead of game-layout clones, the materialiser has to build game objects with the game's own constructors and insert functions on the worker (`LotQuadTree_ctor 0x664650`, `LotQuadTree_Insert 0x666C50`, footprint constructors). Those calls run inside a clone run (selector armed), so their writes land in the copy, and properties 2 and 3 are then the materialiser's job.

### 12.9 Effort (hours, Claude with Rodney, CLAUDE.md)

| Work | Hours |
|---|---|
| T1-T4 thunks, installer with byte checks, the `[RouteGate]` site 8 share, selftest on dummies (thread selector, wrap, flags, register preservation) | 0.75-1 |
| `SnapCtx`/`SnapWork`/`SnapWorker`, capture at the live `Route_PlanLocked`, private out list and release, the shadow step in `ServeAsync`, `__try`, fallbacks | 1-1.5 |
| Pass-through rule in every stub module (§12.4.3), `workerDirty` | 0.5-0.75 |
| Door record and replay at `0x64F4D3` (shared with `[ProbeValues]`) | 0.5 |
| Compare, counters (new version), JSONL, report script with the G4 rule | 1 |
| Materialiser on top of the §11 images (game layout, order, polygon caches, stamps) | 1-2 (depends on §11) |
| steerro-test K3 (G) and K4 | 1-1.5 |
| Shadow runs `bp-snap-shadow`, `ip-snap-shadow` (10 min each) + report | 0.5-1 |
| Fix rounds | 2-4 |
| **Total P4 (G)** | **~8-13 h** (§5 had 4-8 h) |

### 12.10 Open risks

1. **Write set not proven live** (U1, docs/113, not run). A global written through a computed address would not be redirected, so the sim and the worker would race on it. The shadow would show this as M2/M3 noise, not as a crash, and the cause would be hard to find. Run U1 before P4 runs. (BP answered 2026-10-18, docs/113 §7.2: 24 sim-thread plans write one address outside docs/101 §2.2, `0x114C900`, the shared empty hash-table bucket array; the store is 0 over 0, so no redirect, no atomic and no new thunk. U3: `0x66AB70` never ran. Still open: IP and the worker thread.)
2. **Live non-routing reads without `+0x918`.** Today's async job holds `+0x918` for the whole job. The clone run holds no live lock while it reads the world manager lot list, Lot objects (floor heights `Lot+0x290`, partitions `Lot+0x298`), the object service, physics and terrain. Those reads were never under `+0x918`, but the job's hold kept sim routing writers out at the same time. Writers that update a Lot object and its routing data together could now be seen half-done (the K4 guard page does not cover non-routing memory). Edit Town and travel are covered by the epoch check (I-94-69). (Answered 2026-10-18 in §17: guards D1-D3 for the seven changing reads, M5 classing D5.)
3. **Order preservation** (§12.8 item 2). Any reordering in the materialiser shows up as M1/M1-tie at a low rate, which is easy to misread as real ties.
4. **Stamp persistence** (I-94-65). If nodes outlive a search inside cells, a stamp scheme other than the per-working-copy one would give wrong state. The design assumes they do.
5. **Door replay misses.** These happen only when the paths already differ, so a high `door-miss` rate means the data differs. It is a symptom, not a cause; the logged portal ids point to the lot.
6. **Shadow sampling.** Skipping the shadow while other jobs are queued lowers the compare count. Reaching 10k per world may take longer runs, or `ShadowEvery=1` with the worker's spare time.
7. **`Mode=2` path validity** (I-94-66): blocker records and the undecoded node fields have not been checked for pointers into the routing world. (Answered 2026-10-18 in §17.5: no pointer in any field the plan writes [verified]; K5 still closes it.)
8. **Hook sharing.** T1 inside `[RouteGate]`'s trampoline and the door replay inside `[ProbeValues]`'s `0x64F4D3` stub each depend on another module's stub layout. Both need selftests and docs/68 rows in the commit that builds them.
9. **Quadtree count drift (#3).** Harmless only while I-101-3 holds.

## 13. Shadow compare record (P4, 2026-10-18)

> **Changes 2026-10-18:** new section. Format and report only; no native side yet, the game was not run.

**Where:** the P4 build (`[RouteSnap] Mode=1`) writes one JSON line per shadow compare at collect, all classes (not only M2/M3 as §4.3 first planned: G4 needs the M0 count and the times), to `probe-out/.../<tag>-shadow.jsonl`, next to the §4.3 counters. A line is written for every START the shadow path saw, refused or stale ones included. Version field `v` = 1, **2 since the P4 part A build** (adds `nr`, §18; the report reads both); a field change bumps it and the report together. No counter slots: this is a log, not a `Ts3nCounters` block (the §4.3 counters get their own docs/registry.md row when P4 is built).

| Field | Content | Source |
|---|---|---|
| `v` | record version, 1 | |
| `plan` | plan id (per-run sequence of STARTs) | our START hook |
| `ic` | `Plan`, `Replan`, `PlanFromPoint`, `ReplanFromPosition` | the route IC slot (§10 START windows) |
| `world` | world tag (`BP`, `IP`, `SV`, ...) | harness job |
| `search` | `G` (game's code on the clone) or `R` (our search), §3.4 | build |
| `epoch`, `ver` | live routing epoch at `Route_PlanLocked` entry, version id the copy ran on | §3.2 |
| `start`, `goal` | first start record xyz, the game's chosen goal record xyz | job `+0x28` records |
| `tie` | goal indices of the job's tie set by the docs/99 §4.1 rule (equal priority, every distance within 0.05 m² of the best); `[]` or absent = no tie. Filled since 0.30.3: all n indices when the job draws (§13.1), `[]` before | `p4.cpp` `P4TieN` on the captured records |
| `refused` | null, or the refusal reason (`option`, `kind`, `lot`, `epoch`, ...) = M4; `copy` is then null | our START hook |
| `stale` | true when a dirty mark for a lot on the path arrived between the cut and the game's plan = M5 | §4.3 |
| `nr` | **v2** (§17.4 D5, added 2026-10-18 with the P4 part A build, §18): bits of the non-routing writer families that changed between the live `Route_PlanLocked` entry and the end of the shadow and that the plan read (1 physics, 2 terrain swap, 4 destroy, 8 lot index rebuild, 16 world events `0x0D`-`0x10`, 32 blend reload, 64 lock-key ICs); a mismatch (M1-M3) with `nr` ≠ 0 is M5 | p4.cpp `P4NrBits` |
| `gameUs`, `jobUs` | the game's plan time (sim thread, sync or A10 job) and our worker job time, µs | |
| `game`, `copy` | an answer each (below) | |

Answer: `mType` (result `+0x00`, docs/93 §2.1), `status` (first path `+0x94`), `startIdx` and `goalIdx` (`*job+0x38`, `*job+0x3C`, docs/93 §4.3), `paths` in route order, each with `type` (`PathType` hash, path `+0xB0`), `obj` (path object id `+0xB8/+0xBC`, hex string), `len` (path `+0x8C`, which CopyNodes computes in 3D, docs/99 §12), `o58` (path options `+0x58` id list, docs/93 §4.4, hex strings) and `nodes`: per 48-byte route node `[x, y, z, +0x20 segment length, +0x2C flags, type]`, where type is the O2 search node `+0x1C` the node came from (-1 when not known). Route cost = the sum of `len` (route `+0xA4`).

<!-- docs-code: ok -->
```json
{"v":1,"plan":1234,"ic":"Replan","world":"BP","search":"G","epoch":77,"ver":76,"start":[310.2,-100,1490.5],"goal":[312.4,4.1,1496.7],"tie":[],"refused":null,"stale":false,"gameUs":812,"jobUs":640,
 "game":{"mType":1,"status":2,"startIdx":0,"goalIdx":0,"paths":[{"type":"0xC6600B67","obj":"0x0","len":7.1,"o58":[],"nodes":[[310.2,-100,1490.5,0,0,0],[312.4,-100,1496.7,6.6,8,0]]}]},
 "copy":{ "...": "same shape" }}
```

**Classes as the report applies §4.3** (`tools/harness/shadow-compare-report.ts`, first match wins; epsilon = 1e-3, relative above 1):

| Order | Class | Rule |
|---|---|---|
| 1 | M4 | `refused` set or no copy answer |
| 2 | M5 | `stale` |
| 3 | M3 | `mType` or path status differs |
| 4 | M0 | both failed with the same code (failures compare status only, docs/93 §8) |
| 5 | M1 goal-tie | goal index differs and both indices are in `tie` (docs/108 §7: ties occur, any member is valid); nothing else is compared |
| 6 | M2 | start index differs; goal index differs outside the tie set; route cost differs; subpath signature (count, type, object id, order) differs; a path's `o58` set differs; last node xz differs |
| 7 | M1 | same result, cost, ends and subpaths, but a path's `len`, node count, or a node's xz, y, `+0x20`, flags or type differs (y and `+0x20` per docs/99 §12.5) |
| 8 | M0 | everything equal within epsilon |

The docs/93 §9 letters map onto these: `S` -> M3, `G`/`T` -> M2, `G-tie` -> M1 goal-tie, `H`/`N` -> M1, `L` -> M2 (route) or M1 (one path only), `stale` -> M5. Under R the docs/93 §9 "equivalent" tolerances (length max(0.25 m, 1%), Hausdorff 0.5 m) are looser than this exact compare; the report does not apply them yet (G is the planned path).

**G4 verdict per world:** §13.2 (2026-10-19; replaces the first rule written here, which had no path ties, no percentiles and left a real M1 under G OPEN). The report also prints a per-IC class table and the first mismatches (M3, then M2, then M1) with their diff lines; exit code 1 when a world FAILs.

**Run:** `node tools/harness/shadow-compare-report.ts <bp-shadow.jsonl> <ip-shadow.jsonl> [--first 10]`; `--self-check` covers every class, the tie set, both tie kinds, the epsilons, each gate rule and the report layout on synthetic records.

### 13.1 Tie rules of the searches P4 runs (2026-10-19)

> **Changes 2026-10-19:** new subsection (G4 prep). Static reads with capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67), raw `E8`/`E9` caller scans of `.text`, and the offline K3 of §13.3. Ledger: docs/51 §100 (I-94-190 to I-94-195; ids start at 190 to stay clear of rows the parallel 0.30.2 work may add).

A tie is two answers of equal cost where the search picks one by its own order. Under G the clone runs the game's code, so it picks the same one whenever its data is in the same order and no draw is random. Per search:

| Search | How it breaks ties | Same in a clone run? | Tag |
|---|---|---|---|
| Goal pick, `RoutingJob_Execute 0x81B2A0` (docs/99 §4.1) | Loop `0x81B8CC..0x81BAB9`: tie flag starts as n > 1; a record whose priority `+0x20` is not equal (`ucomiss`) to the running best's clears it; so does a record i ≥ 1 whose float d² (`0x81B9E7..0x81BA01`, SSE: dx², dz², sum) to the first start (x `+0`, z `+8`, pushed by `0x6563E0` at `0x81B767`) is more than 0.05f (`0x106C770`) from the running best d² before it (x87 `fsub; fabs; fcompi` at `0x81BA11`). With the flag still set the index is `rng(n)` over all n records (`0x81BC5A..0x81BC65`) | **No.** The job is a stack local of `Route_PlanLocked` (ctor `0x819D50` at `0x82C7A4`, `Execute` at `0x82C7AD`); the ctor seeds `job+0xD0` from RDTSC (`push -1` at `0x819DBE` -> `0x4FA800`, `rdtsc` when the argument is -1); 1337 only with the debug byte (`0x588780` true, `0x81BC41`). The clone run builds its own job and draws again: on a tie any index of the set is the game's answer | verified |
| World A\* `WorldAStar_Search 0x676260` (docs/99 §2.5) | Binary heap on f: push sifts up while `parent.f > f` (`0x5EB920`: `comiss; jbe` stops on equal), pop prefers the right child on equal f; push order = R record order, then G-map entries in key order | Yes, when R and G keep their order: R is the P3 image copied in record order (audited), G is live | verified (heap); order of R inferred from the P3 audit |
| In-room cell A\* `QuadTreeCellAStar 0x61E500` (docs/98 §8.3) | Bucket insert walks while `node.f > cur.f` and inserts before the first `cur.f >= node.f` (`0x61B880`: `comiss; jbe`), so a new node goes before equals; best goal by strictly greater score; neighbours in side order -z, +x, +z, -x, high end first | Yes, when entry and child order match (replay K1 audits it) **and** the clone's tree links are its own: they were not (I-94-193, §13.3) | verified; K3 |
| In-room start set (red-black set, `0x61C1E0`: `cmp edi,[node+0x10]; setb`, unsigned pointer key) | Ascending pointer order, the only pointer-keyed order in the search | Moot: one start node per search. One insert site per wrapper (`0x632987` in `InRoomPlanToPoint`, `0x63328B` via `0x61C330` in `InRoomPlan`), outside every loop but the retry loop, which releases the node first | verified (call sites, loop edges) |
| Goal lookup order `0x65E0D0`, lot exits `0x654CC0`, closest waypoints `0x6543B0` (docs/99 §4.2-4.5) | EASTL sort: insertion sort with strict `>` for 28 or fewer (stable), introsort above (deterministic for the same input order) | Yes (input order copied) | verified (docs/99) |
| Other random draws | No caller of the two LCG draws `0x4FA840` and `0x4FA820` in `0x600000-0x6FFFFF` or `0x810000-0x83FFFF` except `0x81BC65`. The A\* object (`0x61BFA5`) and the world planner (`0x657C9C`) seed RNGs at construction that nothing there draws | — | verified for those ranges; the rest of the closure inferred (I-94-191) |

**Result.** Under G a correct clone gives bit-equal answers except at goal ties. A path tie (same result, start, goal, subpaths and end, equal cost, other nodes) means the clone's order or links differ from the game's: an order defect to find, not an accepted answer. Goal ties are real answers of the game.

**Capture (ts3native 0.30.3, `p4.cpp`).** `P4TieN` applies the goal rule above to the captured start and goal records (float steps as the game's, the 0.05 test in double on float operands) and writes `"tie": [0, ..., n-1]` into the §13 record (empty when no draw); `P4Classify` counts a differing goal index inside the set as M1 and `snM1Tie` (v50 slot, already reserved). Record version stays 2: the field existed and was always `[]`. Selftest: d² ties at 0.04 / 0.06 / 0.08 from the running best, a moved running best, unequal priority, one record, no start; end to end `"tie":[0,1]` and M1 goal-tie.

### 13.2 G4 pass rule (2026-10-19)

`tools/harness/shadow-compare-report.ts` classes, first match wins (epsilon 1e-3 relative above 1; tie epsilon 1e-5 relative):

| Order | Class | Rule |
|---|---|---|
| 1 | M4 | refused or no copy answer |
| 2 | M5 | `stale`, or `nr` ≠ 0 with M1-M3 (§17.4 D5) |
| 3 | M3 | `mType` or status differs |
| 4 | M0 | both failed with the same code |
| 5 | **M1 goal-tie** | goal index differs, both in `tie` (§13.1) |
| 6 | M2 | start index differs; goal index differs outside the set; route cost differs beyond 1e-3; subpath type or object differs (portal choice follows the copied portal list, never a tie); a path's `o58` differs; end point differs |
| 7 | **M1 path-tie** | none of 6, route cost equal within 1e-5 (float rounding of a sum in another order), and a path's node count or a node's xz differs |
| 8 | M2 | other geometry at a cost that differs beyond 1e-5 |
| 9 | M1 (real) | same geometry, another y, `+0x20`, flags, type or `len`: the data differs |
| 10 | M0 | equal |

**G4 per world (BP and IP each)**, compares = M0 + M1 + M2 + M3:

| # | Rule | Fails as |
|---|---|---|
| 1 | compares ≥ 10,000 | FAIL |
| 2 | M3 = 0; M2 = 0; real M1 = 0 (under R: M2 and real M1 OPEN, explain each) | FAIL |
| 3 | goal ties: any number, counted apart; records with a tie set ≤ 0.5% of compares (live F-7: 0 in 21,063 BP plans, 2 in 8,257 IP, docs/108 §7) | above 0.5%: OPEN (the tie rule or `P4TieN` is suspect) |
| 4 | path ties = 0 under G (under R they pass) | OPEN: read each; it passes only once its order source is named and fixed |
| 5 | M4 < 5% of all records | FAIL |
| 6 | worker job time ≤ game plan time: sum, p50 and p95 over the compares | FAIL |
| 7 | from the run's counter dump (not in the JSONL): `snShadowFault`, `snCloneManaged`, `snCloneEvent`, `snCloneLiveLock`, `snDeadStamp`, `snDlOnClone` all 0 (§12.7, §21.4) | FAIL |
| 8 | offline: `steerro-test` K3, K4, K5 (§13.3) pass **without** the relink stand-in (no `K3 WARNING` line), K6 passes **with no `K6 WARNING` line** (I-94-220 and I-94-221 fixed in P4's working copy, §13.3 K6), and §12.7's U1 or its K3 stand-in | not ready to judge (K6 part met offline on 0.32.0, §21.13) |

PASS = every rule holds. The report applies 1-6 and prints `M1 = goal-tie a + path-tie b + real c`, the tie-set count and the p50/p95 times; 7 and 8 are read by hand from the counter dump and the steerro-test output.

### 13.3 K3, K4, K5 on the mapped game image (steerro-test, 2026-10-19)

> **Changes 2026-10-19:** new subsection. Built in `native/ts3native/steerro-test.cpp` (test only) next to REPLAY K1; ts3native 0.30.3 (the `p4.cpp` tie capture of §13.1). The game was not run.
>
> **Changes 2026-10-19 (K6):** K6 added: the game's world A\* on a fake routing world, live vs the P3 working copy (test only, no DLL change, ts3native stays 0.31.4). It found that P4's working copy never reaches the world search: the copy keeps the live planner W (I-94-220), and a byte copy of G keeps live tree anchors (I-94-221). Ledger docs/51 §106. On 0.31.4 K3 prints no `K3 WARNING` line (the `RpTree` fix is in).
>
> **Changes 2026-10-19 (0.32.0):** I-94-220 and I-94-221 fixed in `PcAcquire` (§21.13). K6 now runs on the DLL's own cut snapshot (`PcSnap`) and `PcAcquire`, with live W, R, G trees, group hash and blockedNodeMap on guarded pages: a live access or the live W from `0x829C00` fails K6 (`K6 FAIL`), so the two `K6 WARNING` lines are gone. Ledger docs/51 §108.

**What runs.** The REPLAY world (replay-fake.h built from game objects, the game's own ApplyInsert/ApplyRemove, random allocation pads) and its replay clone. The search is the game's `InRoomPlanToPoint 0x632630` (cell A\* `0x61E500`, cell classes, neighbours, node emit `0x62A470`), called once with `ecx` = live P and once with the clone's P, each with its own `cAStarSearch` (ctor `0x61BF60`) in P `+0xBC` and its trees' `+0x28`, as `PcAcquire` sets them, and a Q reset (`0x61E320`) before each plan as the hierarchical plan does. Compared: the return code and every 64-byte node except `+0x3C` (stale stack). Fakes: `FindLotById 0xC62D40` (a dummy lot object the search only hands on), `Lot_RoomIdAtPoint 0x624780` (room 4, the outside room of level 0) and the portal lock refresh `0x632460` (no portal footprints here). The routing pools get the game's pool objects from their own init functions (`0x61AD30`, `0x61AEE0`, `0x61EC00`, `0x66BBD0`), and every tree gets the default class 0 / multiplier 1 through `0x662CD0` as `QuadTreeBuild` does at `0x62FABD` for a lot with a default in P `+0x68` (SetBounds leaves class 12 = blocked) [verified].

| Case | What | Result, seeds 1-2 and 2-3 x 3000 |
|---|---|---|
| K3 | a sweep of random start/target pairs on every lot and level, on the freshly built clone and after the tie lot's cut; a **tie lot**: an empty lot with one blocking square (class `0xFEB5F8`, byte `+0x34` = 3) at 28..36, queried across it on both axes in both directions, so the two ways round cost exactly the same (mirror images in exact binary coordinates) | 1,579 / 1,576 plans equal (1,438 / 1,433 found); tie lot 8 / 8 equal, each path on one side of the box (the tie broken by insertion order, the same way on the clone) |
| K4 | 12 rounds per seed: 25 recorded writer steps, a cut (the consumer applies them), a sweep (clone == live), then 25 more writes **without** a cut and the kept queries again on the clone | 24 cuts, clone == live after each; 591 / 588 clone plans after the uncut writes equal the version's answers while 182 / 171 live answers had moved |
| K5 | a path built on the clone, then its lot removed live and the cut frees the clone's copy | the path bytes unchanged (values only, I-94-66), the clone's copy gone, clone memory back to 0 at the teardown |
| K6 | the game's world A\* `WorldAStar_Search 0x676260` on a fake R and G (below), once through the live planner W and once through a private W' on the working copy that p4clone.cpp's own `PcR` / `PcHighCost` / `PcBnm` build from the P3 version of a reader cut; random start/goal pairs over every waypoint, cost modes 1-4, option bits `0x40`, `0x80`, `0x100`, `0x200`, `0x10` with a start hub; 12 rounds per seed of 25 writes (R blocked bits, blockedNodeMap slots, high-cost polygons through the game's `0x670750`) and a cut, then 25 uncut writes; a **tie lattice** (4 x 4 waypoints 4 m apart, edge byte 7) on every corner pair in modes 1 and 4 | seeds 1-2 / 2-3 x 3000: 1,664 / 1,664 plans equal (path refs, best ref, closed bits; 1,018 / 917 found, 109 / 68 through water links); 26 reader cuts each, audit 0 mismatches, the copy's R, high-cost maps and blockedNodeMap == live at each; 576 / 576 lattice cases equal, all shortest, and an R with the lattice records' neighbour order reversed picks another path of the same length in 448 (the tie follows record order, which the image keeps); 741 / 634 copy plans after the uncut writes equal the version's answers while 564 / 483 live answers had moved. Two `K6 WARNING` lines (I-94-220, I-94-221) on 0.31.4; on 0.32.0 (`PcAcquire`'s W', guarded live pages) the same counts, 2,981 / 2,874 guarded copy runs with no live access, no warning (§21.13) |

**Defect found (I-94-193) [verified offline + static].** The first K3 plan differed (live 28 nodes, clone 10; each side deterministic; every cell's class, multiplier and position record equal on both sides). Cause: `replay.cpp` `RpTree` copies an inner node's 0x1C bytes and keeps `+8`, the parent link, pointing at the **live** parent (309 of 310 inner nodes in the first tree). The game's live trees hold `+8` = parent (0 bad links in 310), and the cell neighbour step `0x66A220` climbs them (`pos+0xC = node+8`, `pos+8 = parent+8`, `0x66A31A..0x66A32A`; called from the neighbour functions `0x66B8D0`/`0x66B9C0` and the small-cell clearance `0x66A380`). So a clone run walks into the live tree: unlocked reads of live memory (the K4 guard page would trip) and answers mixed from both trees (M1/M2 noise in a shadow run). `ApplyInsert`/`ApplyRemove` reach `0x66A040` (descend) only, not `0x66A220` [verified call graph; inline `+8` reads there not checked, I-94-194]. **Fix (replay.cpp, not made here: 0.30.2 is being built there by another agent):** give `RpTree` the clone parent and set it after the copy:

<!-- docs-code: ok -->
```cpp
static u8* RpTree(RpCtx& x, const u8* n, int depth, u8* parent = NULL) {
    ...
        memcpy(m, n, 0x1C);
        *(u8**)(m + 8) = parent;  // the clone's parent, not the live one (inner +8; the root's is 0), I-94-193
        m[7] = 1;
        for (int i = 0; i < 4; i++) *(u8**)(m + 0xC + 4 * i) = RpTree(x, RpP(n + 0xC + 4 * i), depth + 1, m);
```

Until then the test relinks every clone tree before each search (`K3Relink`, a stand-in for the fix) and prints `K3 WARNING (I-94-193)` with the count; `K3_NORELINK=1` shows the failure. G4 rule 8 needs the fix and no warning line. No P4 shadow run is worth judging before it: its in-room plans would carry this noise.

**K6: the world A\* (2026-10-19).** The fake world is snap-fake.h's (G, P, lots, W, world manager; the snapshot on) with R, G's water links, the blockedNodeMap and W replaced by game layouts [layouts verified in the disassembly unless marked]:

- R (docs/99 §1.2-§1.4): 4 x 4 chunks of 32 m, 5-12 waypoints per chunk on a 2 m grid (exact equal costs are common), types `0x10`, `0x11`, `0x07`, `0x0D`, `0x0E`, `0x02`, `0x01`, `0x13`, `0x15`; each waypoint linked both ways to its 2-4 nearest compatible waypoints in its own or a neighbouring chunk, one link per chunk pair, street to street; edge bytes by type pair (3, 5, 6, 1, 2, else 7, sometimes 4); a lot-edge waypoint lists its hub first; 1 in 30 records without edge bytes (flag `0x40` clear, so edge byte 0). Chunk 15 is the tie lattice.
- G: the two water-link trees at `G+0x30` (key `chunk << 16 | wp`: body byte `+0x14`, foot slot `+0x18`, boat slot `+0x1C`, -1 = none) and `G+0x4C` (key = body byte: ref `+0x14`, the slots), EASTL node layout (right `+0`, left `+4`, parent `+8`; anchor right = last, left = first, parent = root), lower and upper bound as `0x676260` walks them, the step `0x4E0B20`; body values `G+0x3A0 + 8 * body` (`0x66D8D0`, `this` = G); blockedNodeMap buckets with a sentinel (`0x66F890`: present = blocked slot).
- W (docs/99 §2.1): `+8` P, `+0x10` R, `+0x14` G as `0x654060` sets them (`0x829FC4..0x829FE3`), the lot-edge group hash `+0xA0` (buckets `+0xA4`, count `+0xA8`, node key / group / next, key `chunk << 16 | wp`, `0x653200`), the container at `+0x18`: `+0` vtable `0xFEF990`, `+4` R (docs/99 §2.1 says `+0`: `0x6735C0` is `add ecx,4; jmp 0x673500`), pool list `+0x14/+0x18`, free list `+0x20`, the compiled tuning defaults (threshold 50, SHORT 1.3/2/2/2/2/2, LONG 1.3/2/4/10/10/10), the closed list from the game's `0x6735C0`.
- The search is called as the planner calls it (`0x65F7F4..0x65F809`): `ecx` = W+0x18, R = `[W+0x10]`, G = `[W+0x14]`; opts is a 16-byte aligned block (`0x673812` stores the circle centre with `movaps`), `opts+0x88` = that W, `opts+0x8C` -> a job block whose first word is the boat AGS code (0 or `0xC00`). Mode 3 never gets `0x80` (its water entry needs RoutingWorld_Get, the lot lookup `0xC68990` and sea level `0xC61670`).
- The copy: a reader cut (`SpCut(world, true)`, every tile built, audit), then `PcR`, `PcHighCost`, `PcBnm` exactly as `PcAcquire` calls them on a byte copy of the cut's world, then K6's own W': the live W's bytes with `+0x10` / `+0x14` -> the copy, G's two trees re-made under the copy's anchors from the live trees, and a fresh container. (0.32.0: replaced by `PcSnap` + `PcAcquire` and the W' that `0x829C00` returns on the copy, §21.13.) Compared per plan: found, every path ref, the best ref (`param 7`) and the closed bits the search left.

**Findings by K6 [verified offline + static].**

1. **I-94-220: P4's working copy runs the world planner on live data.** `PcAcquire` copies the world's 0xA00 bytes and replaces `+0x95C`, `+0x918`, R `+0x54`, the high-cost vector and the blockedNodeMap, but `+0x960` stays the live W. `RoutingJob_Execute` takes the world from `RoutingWorld_Get` (T4: the copy in a clone run) and W from it through `0x829C00` (`0x81B3E7`); K6 calls `0x829C00` on the working copy and gets the live W at every cut. The hierarchical planner `0x65E0D0` then takes R `[W+0x10]` and G `[W+0x14]` (the live world's `+0x54` and `+0x8C`) for the world A\*, writes the live container `W+0x18` (closed bits, node pool, heap: docs/101 H2) and the W caches (H3), and loads P from `[W+8]` 22 times, among them `LotSearch_Entry 0x640E00` (3 calls), `LotPath_AddPortalNodes 0x62FBE0`, the transform `0x62EF30`, so those lot-level calls run on the live P, not the replay clone. Consequences: (a) the P3 R image, the high-cost tiles and the blockedNodeMap copy never reach the world search; (b) a clone run reads live R, G and P without `+0x918` (the copy's lock is private) and writes the live container while the sim may plan: a race [inferred: the overlap was not measured]; (c) a shadow compare of those parts compares live with live, so its M0 says nothing about the copy. **Fix (built in 0.32.0, §21.13; designed as):** give the copy a private W': W's 0x198 bytes, `+8` = the clone's P (W holds a counted reference, `0x654060`: ours must not release the live one), `+0x10` / `+0x14` = the copy's `+0x54` / `+0x8C`, a fresh container (closed list by `0x6735C0` on the copy's R, rebuilt when R's shape changes; own pool), empty private caches at `+0xC4`, `+0xE0`, `+0x100`, `+0x128`, `+0x160` (written per plan), the group hash `+0xA0` copied at the epoch (read-only in a plan [inferred]), and `[copy+0x960]` = W'. ~2-3 h with a selftest; K6 then runs on PcAcquire's W' instead of its own and must print no `K6 WARNING`. **Until then, `snap-p4` results do not count for G4 and the run itself is a race risk.** (0.32.0 builds it; crash F's stack is this path, §21.13.)
2. **I-94-221: a byte copy of G cannot be read by the world search.** G holds its two water-link trees inline (anchors `G+0x30`, `G+0x4C`); in a byte copy the root's parent still points at the live anchor. K6 calls the game's tree step `0x4E0B20` from the copy's last `+0x30` node and gets the live anchor, never the copy's end, at every cut; an equal range that ends at `end()` (the largest key) then walks into the live G header. So W' must not point at a byte copy of G: the trees are copied node by node (K6 does it; they change only at a G build, so once per epoch), or W' keeps the live G and P4 accepts live G (then the blockedNodeMap and high-cost copies are moot for this search).
3. **The world A\* reads from G only** the two trees, the blockedNodeMap (`0x66F890`) and the body table (`0x66D8D0`); no `+0x7C0` (high-cost) read in `0x673500..0x676700` [verified, raw scan]. The high-cost maps matter to other readers (the segment test `0x627780` was not followed with a world manager, I-94-222).

**Not driven by K6:** the segment test `0x627780` (edge bytes 2 and 7 against the job's polygon set) and the lot-edge point test `0xC6BAB0` both need the world manager (`[0x11ECBC4]` = 0 here, so they answer "clear"); mode 3's lot-bound water entry; the planner above the search (goal lookup order `0x6542E0`, lot exits `0x654CC0`, closest waypoints `0x65BB90`, the start/goal lot legs) and the job's goal draw (`RoutingJob_Execute`, RDTSC seed: the selftest of §13.1 covers the rule); the group hash and W caches as P4 would copy them (K6's W' shares the live group hash, read only).

**Not covered offline:** the planner above the world A\* (see K6); the goal draw (`RoutingJob_Execute` is above `InRoomPlanToPoint`; covered by the selftest of §13.1 and the live `tie` field); door answers and portal costs (the lot-level planner; door record/replay is in the selftest, H20 references in REPLAY H20b); the P4 provider's acquire itself (the test sets P `+0xBC` and the trees' `+0x28` the same way; selftest P4 part B covers acquire); agent options (X `+0x40` = NULL); a guard page (the live heap here is the CRT heap; the parent-link check is the live-read finding it would have made).

**Effort (hours, Claude with Rodney):** tie rules and scans ~1; `P4TieN`, classifier, report rule and self-checks ~1; K3/K4/K5 harness and the I-94-193 hunt ~2; docs ~0.5: **~4.5 h**. Left for G4: the `RpTree` fix (~10 min + K3 rerun), the two shadow runs (~30-60 min) and the report. K6: static reads of the world A\* and its callees ~1, fake world, copy and checks ~1.5, the W and G findings ~0.5, docs ~0.5: **~3.5 h**. Left for G4 from K6: the private W' and G trees in `PcAcquire` (~2-3 h, DLL change), then K6 on it and the two shadow runs.

## 14. P3 build status: snapshot H world images and audit (0.26.0, counters v47)

Built 2026-10-18, offline only; not run in the game. Code: `native/ts3native/snap.cpp`, switch `[RouteSnap] Enabled=1 Mode=0` (default off; the section is shared with P1's `Count=`, and `Enabled=1` installs P1's sites too). No search reads the images (that is P4). No detour of its own: the change signals come from P1's stubs (`SnEnter` calls `SpNote`; `SnIcEnter` calls `SpIcNote` and, at a route START, `SpStart`). Counters v47 at `+0x20C0..0x2197` (54 fields, docs/registry.md), after v46's real end.

**Scope change after the P1 live run (BP, §10 report, G1 FAIL):** footprint state is hot (writer rows ~1,600/s: W6 polygon caches 26k/min, footprint removes and grid removes 20k/min, `UpdateObjectFootprintCore` 14k/min driven by object event `0x0E`, W1-W3 4-6k/min each; dirty lots per START p95 6-10), while R bit 7 changes 20-30 times a minute and G and blend not at all in play. So the world-level images below are built as designed, and **the per-lot footprint / quadtree image is a pluggable strategy with no live implementation**: only its interface and the audit/compare scaffolding are built.

**World images** (immutable, refcounted, shared between versions):

| Input | Image | Dirty when | Not imaged (why) |
|---|---|---|---|
| R | `world+0x54`: w, h, size, chunk offset table, record area (type and blocked bits included) | `0x82B590`, `0x82A190`, any G build | component vectors (built once by the flood at load) |
| Gcore | G header `+0..+0x1C`, cells, body table `+0x39C..+0x79C`, sizes `+0x7A0..+0x7AF`, the passability maps | G build | shoreline trees (G-build only; P4 adds them if the G search reads them) |
| Gtiles | the 10 high-cost maps in 64 x 64-cell tiles, one image per tile with every map | `0x6733C0`: vertex box of the polygon argument (arg 5) plus one 4 m cell, recorded per (owner, footprint) key (args 1-4) and replayed at `0x670510`; an unknown key or a full key map: every tile; G build: every tile (§9.2) | |
| bnm | `blockedNodeMap`, sorted (cell, count) pairs (node key `+0`, byte `+4`, next `+8`, as `0xD38B00` reads them) | `0x636BF0`, `0x62D9F0` | |
| grid | `P+0xDC` records (bounds, key, owner, flag, mask, lot id) sorted by lot id; only dirty lot ids are re-read | `0x636BF0` (lot pointer arg 2), `0x62D9F0` (lot id args 1-2); world events `0x0D`-`0x10`: whole grid | polygon set pointer `+0x08` |
| blend | world-builder layer 0 as decoded bytes (layer = `[[WM+0x54]+0x0C]+0x2C`[0]: block array `+0`, count `+4`; map width `+8`, height `+0xC`, blocks per row `+0x10`; a block value up to `0xFF` is uniform, else it points at 256 bytes, as `0xC21AE0` reads it) | blend reload job pass 4 (`0xC62AC0`, main thread: value copy, §9.4) | |
| tune | `W+0x8C/+0x90`, `W+0x18+0x24..+0x63` | world epoch only | |

**Per-lot footprints and quadtrees: where the strategy plugs in.** `struct SpLotStrategy` in `snap.cpp` (the design agent's delta log or per-plan copy implements it; the result is documented in this section):

| Hook | When | Contract |
|---|---|---|
| `note(site, slot)` | every hit of a lot leaf (W1-W6, `0x8321F0`, `0x831BA0`, `0x82A0C0`, `0x635700`, `0x829D50`), any thread, under the snapshot's dirty lock, from `SpNote` | `site` = P1's `kSn` index, `slot` = the stub's return-address slot (`slot[-2]` = ecx, `slot[1..]` = arguments) |
| `begin(v, old, world, d, lots, n)` | at the cut (sim thread, `+0x918` held), from `SpBuildVersion` | give version `v` the images it carries for the lot set (`lots == NULL`: every lot; P4 passes the request's lots); `d` holds the cut's dirty flags (portal ICs and lot events set "every lot") |
| `image(v, key)` | from the audit | the carried image of one lot in the canonical `SpBuildLot` format, or NULL when not carried |

`v->lots` / `v->nLots` (sorted by key) hold what `begin` built; `SpVerRel` releases them. The audit compares every carried image with a fresh `SpBuildLot` of the live lot, and a carried lot that left the table counts as a mismatch. The canonical lot image: key, lot graph pointer, level base `+0x40`, built byte `+0x8C`, both transforms `+0xA0..+0x11F`, portal pointers `+0x44`, add/remove list sizes, and per level quadtree its pointer, `+0x44` and every leaf entry (leaf node, FootprintMapEntry, mask, footprint, footprint id) from the root `Q+0x24`. It leaves out lazy state and search outputs: room records, polygon caches, the Q pin counter `+0x48` and deferred flag `+0x4C`, and lock byte `+0x8F` (the planner clears it, §2.2 E11). Live default: `none` (no lot images, no lot audits). A reference strategy "copy the whole lot when marked" (`kSpLotCopyRef`) exists for the offline tests only. It exercises the interface, the marks (Q from W1/W2 ecx, lot graph from W4 arg 1, lot id from W3 and the remove leaves) and the audit. It is not a design candidate.

**Cut and versions.** At a route START (P1's IC entry, sim thread), at most every `CutMs` (250), never while an async job runs or is queued (`AsyncBusy`: P3 has no consumer, so it never adds a wait), holding `+0x918` through the game's `Lock_Acquire 0x4E16F0` / `Lock_Release 0x4E17B0` (so `[RouteGate]` and the async bookkeeping see the hold). World events `5`, `7`, `9`, `0x0A` or a new routing world move the epoch: full build (the load's G build precedes event 7, §9.1). Otherwise only the dirty images are rebuilt and the rest are shared. A fault while copying keeps the old image (`spFaults`). The new version replaces the current one; a job (P4) holds one with `SpVerRef` / `SpVerRel`. Above `CapMB` (64) the build switches itself off. P3 does not force anything at the cut: first use of the start/goal lots (§9.3), `ApplyPendingUpdates` (counted, `spPendingAtCut`) and deferred quadtree flushes (counted, `spDeferredAtCut`) are left to P4.

**Audit (Mode=0).** Every `AuditEvery` cuts (20; the queue uses 4), under the same hold, every image is rebuilt from live memory and compared with the version (invariant S): `spAudits[input]`, `spMis[input]`. The first `LogMismatch` (32) differences are logged as `snap audit MISMATCH <input> key ...: first difference at +0x.. (image XX, live YY), object <live address>`. A mismatch on an image that was not dirty at its cut is a missed change signal (S1). Report: `node tools/harness/snap-audit-report.ts <native.json> --log <ts3native.log>`. It gives G3 for one world: build on, every world input audited, at least 5,000 image audits, 0 mismatches, cut cost at most 2 ms/s, memory at most 64 MB. Lot audits count only once a strategy is plugged in. `--self-check` tests the report itself.

**Door mirror contract (native side; managed side docs/105 §10.5).** Channel: `Route.GetOption2` with RouteOption bits 63 and 58 set and a code in bits 48-55 (the native hook sees `optHi = 0x84000000 | code << 16`), a 32-bit value in the low half (`optLo`); the route handle is ignored (0 is fine); answer 1 = accepted, 0 = protocol error. Sim thread only. Codes:

| Code | Value |
|---|---|
| `1` / `2` | lot id low / high (high opens the lot and clears its records) |
| `3` / `4` | door object id (portal owner id) low / high (high starts a record) |
| `5` | `DoorRecord.Kind` bits (Door 1, IBarnDoor 2, GateDouble 4, IElectronicGate 8, CanLargeAnimalUse 16, DoorSingleGender 32) |
| `6` | lock type (`tLock` 0-6) |
| `7` / `8` | lock owner SimDescription id low / high |
| `9` / `10` | lock owner household id low / high |
| `11` | one-way room |
| `12` / `13` | room A / room B |
| `14` / `15` | one owner-list id low / high (appended to the record) |
| `16` | managed epoch (`GetLotDoors`' `out epoch`) |
| `17` | end; value = record count (a different count leaves the lot invalid) |
| `18` / `19` | invalidate one lot (low / high) |
| `20` | invalidate all |
| `21` / `22` | query (low / high): answer 1 when the native table of that lot is valid in the current native epoch |

Adapter for ts3fixes (built 2026-10-18, offline only; mapping and tests in docs/105 §10.5 "Native binding"; differences: it sends `16` once per lot right after `2`, and on a 0 answer it drops its own cached lot before the read): at a route START, for each lot it mirrors, query `21`/`22`. On 0 it calls `DoorMirror.GetLotDoors(lot, out epoch)`; when the answer is not null it sends `1`, `2`, then per record `3`-`16`, then `17`. A null answer sends nothing, so the lot stays invalid. The native side invalidates at the world epoch and at the build/buy exit IC; ts3fixes keeps its own `Invalidate` for `ImportContent`. P4's native reader `SpDoorFind(lot, owner id)` answers only for a valid lot of the current epoch. An owner id with no record is unknown and takes the guard path, never Anybody (I-105-32).

**Checks [offline]:**

- `build.cmd` selftest `TestSnapImages`: K1 on 3 seeds x 300 writes with a C rasteriser (210 cuts; audit 0 mismatches and incremental == full rebuild at every cut; unsignalled writes are found), K2, K5, strategy `none` (no lot images, world audit only), the tile rule, world events, START gating and the door channel through the GetOption hook.
- `steerro-test` section SNAP, seeds 1 and 2 x 3000 writes, high-cost stamps and removes through the game's own `WaterGrid_MarkB 0x670750`: 714 and 718 cuts, 0 mismatches, incremental == full, unsignalled writes found, 9,281 and 9,564 tiles rebuilt. The other steerro-test sections pass.
- `node tools/hook-sites.ts --check`: 0 missing, no new site.
- `snap-audit-report.ts --self-check`, `queue.ts tools/harness/queues/snap-p3.json --dry-run`, `node tools/check-docs-code.ts`.

The fake world's writers are stand-ins with the game's layouts (only the rasteriser is game code); the live audit checks the game's footprint writers.

**Run (audit only, no timing claim, so daytime is fine):** asi parked at `game-copy/ts3native.asi.snapp3` (sha256 `6c8a8971f20174cb90f347a4ed663585d08d006ec82df0ec628b0de2355bc365`). Command: `node tools/harness/queue.ts tools/harness/queues/snap-p3.json` (jobs `bp-snap-p3`, `ip-snap-p3`: env-on settings, Spring saves, seed 12345, 10 min, `RouteSnap.Enabled=1 Mode=0 AuditEvery=4`). Then `node tools/harness/snap-audit-report.ts probe-out/queue/snap-p3-<date>-<job>-native.json --log <that job's ts3native.log>`.

**Cost [inferred, I-94-37]:**

- A cut that rebuilds nothing walks the lot table once (deferred-flush count) and copies pointers: tens of µs.
- Grid and bnm are rebuilt at most once per cut while footprints churn: grid removes run at ~330/s live, but one rebuild per cut, ~4 cuts/s.
- A full build (epoch) copies about 8-10 MB: R ~1 MB, G cells and maps ~4 MB, blend 4 MB at 2,048 m.
- An audit rebuilds every world image, several ms. At `AuditEvery=4` that is about one audit per second, so the audit run is not a performance run. The audit's fresh copies count in `spKBMax` while they live.

**Left for P4:**

- The per-lot strategy (design agent) plugged into `SpLotStrategy`.
- The cut moves into the async START handshake with the request's lot set, and the START forces first use, `ApplyPendingUpdates` and the deferred flushes.
- The shoreline trees, if the G search reads them.
- The ts3fixes door adapter above is built (docs/105 §10.5); P4 adds the reader and the request's lot set.
- The search on the copy, and K3/K4.

Effort: this build took about 3 h of session time. Left in P3: the two audit jobs (2 x 10 min) and their report.

## 11. Footprint image design from P1 data (2026-10-18)

> **Changes 2026-10-18:** new section. Static design from the first P1 runs (`bp-snap-p1`, `ip-snap-p1`, ts3native 0.25.0, 10 min each, env-on settings) plus a raw `E8`/`E9` caller scan of the Steam exe for the unattributed callers; no game run, no code. It replaces the lot side of §3.2-§3.3 (per-lot immutable images copied at START) and gate G1 of §5/§10. World-level inputs (§8) are unchanged. Ledger: docs/51 §67 (I-94-50 to I-94-58).

### 11.1 What P1 measured

Per second over the 10-minute window, from `node tools/harness/snap-counters-report.ts probe-out/queue/snap-p1-2026-10-18-{bp,ip}-snap-p1-native.json` [verified run files]:

| Row | BP | IP | What it is |
|---|---|---|---|
| Writer rows, all threads | 1,598 | 1,941 | every hit of rows 0-16, so one logical change counts at several leaves |
| W1 apply insert / W2 apply remove | 96 / 60 | 81 / 48 | quadtree entry changes, the only per-entry tree writes |
| `0x8321F0` UpdateObjectFootprintCore | 240 | 322 | "rebuild this object's routing footprints": remove-all, new 0x44-byte footprint per level, insert. Object event `0x0E` causes 73% (BP) / 84% (IP) |
| `0x635700` remove-all = `0x62D9F0` grid remove | 337 | 407 | one grid remove per remove-all, also when the object had nothing in the grid |
| Grid add `0x636BF0`; high-cost marks `0x6733C0` + `0x670510` | 5.8; 9.8 | 13; 14 | world-level footprint grid and G marks (§8.2) |
| W6 polygon caches (8 sites) | 433 | 571 | 94-95% on the sim; mostly lazy cache fills by readers (§11.6) |
| Discourage ICs: calls / writer rows inside | 59 / 501 | 39 / 320 | design D's Add/Remove; a third (BP) / a sixth (IP) of all rows |
| Route STARTs (4 route ICs) | **38.3** | **19.1** | sync and async; §3.3 assumed ~6/s |
| Known dirty lots per START: p50 / p95 | 2 / 6-10 | 2-3 / 6-10 | only leaves that carry a lot id; 85% (BP) / 81% (IP) of STARTs also had writes of an unknown lot |
| Writer rows per START, median bucket | 11-20 | 21-50 | |
| R bit 7, lot graph builds on the sim | 0.3, 0.12 | 0.3, 0.18 | rare, as §8 expected |

Reading: the rows are dominated by Sims and objects moving on the lots where plans start and end (`0x0E` handlers, discourage footprints). The G1 limits of §5 ("≤ 200 rows/s", "≤ 5 dirty lots per START at p95") assumed that per-lot images would be copied rarely; with 19-38 STARTs/s and the start or goal lot dirty at most of them, any design that copies a lot at START copies on almost every plan.

### 11.2 Which lots a plan reads, and when they are known

| Lot | Reader | Known at START? |
|---|---|---|
| Start lot | `LotSearch_Entry` for the first start point, then the start lot leg (§9.3, docs/99 §2.8 step 6) | yes: `World_LotLookup` on the first start point |
| Goal lot(s) | goal step on the sim (`Route_GoalStep 0x75C2D0`, before the job), then `LotSearch_Entry` for the goal and, on failure, for each goal record in lookup order (docs/99 §2.8 step 7) | the primary goal's lot yes; with several goal records on different lots, only as a set of candidates |
| Transit lots | the world path through a third lot's hub gets its own lot leg between the neighbouring waypoints (docs/99 §2.8 step 9) | **no**: known only after the world A\* returns [I-94-50] |
| PlanCommon tail (`EditPath`, in the job in async mode) | `0x82F0B0` on the goal lot, else the start lot (docs/83 §2) | yes |
| World A\* | R, G, the grid `P+0xDC`, high-cost maps, `W` hashes; no per-lot quadtree [inferred, I-94-51] | world-level, §8 |

So a per-plan copy can cover start and goal at START, but transit lots are discovered during the search. Their lot legs would need either a stale image (wrong answers possible), a lazy copy requested from the sim (a round trip and a wait: the A10 failure mode in small), or a refusal and a sync fallback.

### 11.3 Options

**Lot copy cost.** BP has 82 lots and 330-386 level trees (4-4.7 per lot; 330 levels in the docs/25 `[FootprintIndex]` verify, 386 trees in the docs/98 count). Footprint entries are not measured; the docs/25 save scan (0.84 s for 330 full-map scans, cache-missing entries) suggests about 100k-250k entries world-wide, so 1.2k-3k per average lot and several times that on busy lots [inferred, I-94-56]. A deep copy into game layout (tree nodes, 12-byte leaf slots, 0x14-byte `FootprintMapEntry`, 0x44-byte footprint, polygon cache `0x24` per level plus vertex vectors, pointer fix-up through a map) costs about 0.2-0.5 µs per entry, so **0.25-1.5 ms per average lot** [inferred, I-94-56].

| | A. Per-plan copy at START | B. Ordered leaf replay into a worker clone (**recommended**) | C1. Shared immutable parts + versioned overlay | C2. Live reads under a reader lock (docs/83 QL) |
|---|---|---|---|---|
| What runs | sim copies the start and goal lots (1-2) under `+0x918`; transit lots lazily or refused | the P1 leaf stubs record each routing-data write in order; the worker applies the same game function to its own full clone, then searches | lot images with copy-on-write nodes; static footprints (walls, floors, terrain) shared, dynamic ones in an overlay | the worker reads game data while holding QL shared; sim writers take QL exclusive |
| Sim cost | 0.25-1.5 ms x ~1.3 lots x misses (85%) per START: **12-73 ms/s** BP for all STARTs, **1.8-11 ms/s** for async STARTs only (5.6/s, PLAN item 6) | record header ~0.1 µs per leaf record, footprint clone with forced polygon build ~2-5 µs per insert: **~0.15-0.6 ms/s** BP and IP [inferred, I-94-57] | as B for the overlay plus compaction copies | none, until a writer meets a held lock |
| Worker cost | none extra | 1-3 µs per applied record: ~0.5-1.7 ms/s, off the sim | composition per read | none |
| Budget (≤ 2 ms/s sim, PLAN Goal) | **fails** except the best case with async only | **passes** on both worlds with a 3x margin | passes, but see risks | **fails by design**: worker duty ~3.4% (5.6 jobs/s x ~6 ms) x 250-400 writer calls/s ≈ 9-14 collisions/s x 3-5 ms ≈ **30-70 ms/s** of sim waits, the A10 +49 ms/s failure again |
| Exact cut | yes for copied lots; transit lots stale or refused | yes: the worker applies records up to the START's sequence number before searching | yes | yes |
| Main risk | cost, transit lots | a write that bypasses the recorded leaves; replay determinism | leaf entries of static and dynamic footprints share leaves, final bits and entry order (§11.4), so "immutable parts" are not separable below the leaf | waits; deferring writes instead (design D style) changes what the game's own sync plans and steering see |

**Why not "rebuild from current footprints".** Tree state depends on history, not only on the current set: a leaf's final bits are cleared when an added entry covers a shape partly (docs/98 §15.4) and removes do not set them back [inferred, I-94-52]; the class rule keeps the last type `0x16`/`0x0A` entry of a leaf list (docs/98 §5.2), so entry order matters; node splits and merges follow the insert order. A from-scratch image (and C1's shared/overlay split) can differ from the live tree. Option A copies the live result and is exact; option B is exact if it replays the game's own leaf functions in the game's order on an identical prior state [inferred, I-94-53].

### 11.4 Recommendation: B, ordered leaf replay into a worker-owned clone

1. **Epoch (sim, loading screen):** at world event 7 (after the load's G build, §9.1), build the full clone in the worker's heap in game layout: lot table, lot graphs, level trees, entries, footprints with valid polygon caches, portals, lock-key set contents (`fp+0x20`, docs/101 §6), plus the §8 world images. Cost 30-150 ms (§3.3), once per load, travel, build/buy exit and Edit Town.
2. **Recorder (sim, the existing P1 stubs):** each hit of W1 apply insert, W2 apply remove, W3 tree create, W5 lot table insert, grid add `0x636BF0` and remove `0x62D9F0`, high-cost `0x6733C0`/`0x670510` and R bit 7 appends one record (sequence number, leaf, arguments with live pointers mapped to clone ids) to a single-producer ring. An insert record carries a clone of the footprint made on the sim right there (EnsureAllPolygons `0x80E610` first, so the worker never runs a cache rebuild that reads live lots, heights or the `X+0xA0` lock, docs/82). W6 cache fills are not recorded (§11.6). Inline writers (portal cost, age/gender flags, portal lock status, ocean owner, the 4 lock-key ICs) record the new value from their IC slot shim (the P1 slots).
3. **Rebase records:** a sim-side `LotGraph_Build` / `LotRouting_FinishBuild`, a portal IC that reaches `LotRouting_AddPortal`/`RemovePortals`, lot load, lot remove and build/buy exit record "copy lot K" after the call (option A's copier, used rarely: 0.12-0.18 lot builds/s on the sim) and suppress the leaf records nested inside.
4. **Cut at START (sim, `+0x918`):** §8.5 steps 2-4 (force first use of unbuilt start and goal lots, `ApplyPendingUpdates`, flush the deferred queue of every tree with queued inserts, which the W stubs track), then publish the ring's sequence number with the request. No lot is copied at START.
5. **Worker:** before each job, apply records up to that sequence number with the game's own functions on the clone (single consumer, so no versions, refcounts or copy-on-write for lot data), then run the search (G, docs/101 thunks). Records that arrive during a search wait in the ring.
6. **Footprint identity:** clone ids keyed by the live footprint pointer, dropped at its last W2 remove and at footprint delete (`0x80F2F0`, one more pass-through stub) so a reused address cannot alias. Object pointers stay opaque ids on the worker (owner tests compare them, never dereference).
7. **Overflow:** ring full (4 MB, ~50k records) = world epoch rebase on the sim at the next START; async refused until it is done (counted).

**Costs** [inferred, I-94-57]: sim ~0.15-0.6 ms/s (BP 510, IP 564 records/s by the rows above), plus the cut's deferred flushes (work the game's own first query would do anyway) and rebases ≤ ~0.5 ms/s; worker ~0.5-1.7 ms/s; memory one clone (10-40 MB, §3.3) plus the 4 MB ring, under the 64 MB cap and below H's ~2.2x.

**Correctness risks and their check:**

| Risk | Check |
|---|---|
| A write that bypasses the recorded leaves (in-place footprint scalar writes, portal fields from a path without an IC slot) | P3 audit (§4.2): at 1 START in 50 (async STARTs only, audit runs only) the sim hashes the live lot and the worker the clone lot at the same sequence number; mismatch class S1 names the lot and the last records |
| Replay non-determinism: anything keyed or ordered by pointer value (`FootprintSet` sorted by sub-object pointer in `0x82F0B0`, docs/83 §2) | steerro-test K1 with randomised clone allocation addresses; tree hashes must match, answers through K3 |
| A clone footprint slot that reads live state (polygon rebuild `+0xF8`, per-footprint agent lists) | caches forced valid at record time; a worker-side guard on those slot targets refuses the plan (M4) instead of reading |
| Order of deferred inserts | records are taken at the apply leaves, so the clone sees the game's apply order, including flushes started by readers |
| Lot graphs need live non-routing reads (walls, rooms, `Lot+0x298`) | built only on the sim (step 3 rebases); the worker never first-builds a lot |

**What changes in P3/P4.** P3 drops lot images, versions and the per-lot working-copy materialiser; it gains the epoch clone builder, the recorder (the P1 counters become producers), the ring, the worker apply loop, the lot rebase and the clone-vs-live audit. steerro-test K1 becomes "random writer sequences through the stubs, replay, compare clone and live trees by hash"; K2 (cut order) and K5 (lifetime: footprint delete during queued records) stay; K4 checks that the worker reads only the clone. P4 is unchanged except that the materialiser step disappears (the clone is already in game layout). World-level images (§8) go into the same clone through the same ring.

**Effort** (Claude with Rodney, hands-on hours): P1b (§11.7) ~1-1.5 h + one daytime count run per world; P3 ~4-6 h (was 3-5); P4 (G) ~3.5-7.5 h (materialiser gone); P2, P5, P6 as §5. Totals **~15-24 h (G)** plus 2-3 clean nights; R would replay into its own structures instead and keeps its +6-10 h.

### 11.5 G1 rewritten for design B

Applied by `snap-counters-report.ts` after the P1b additions; PASS only when all pass.

| # | Check | Limit | BP / IP today |
|---|---|---|---|
| 1 | Unattributed sim writer-row hits, after the §11.6 frames | ≤ 1% of sim writer rows, and no single caller ≥ 0.5%; the caller table has no "other" bucket above 1% (64 keys, keyed by leaf + return + the next frame) | OPEN: 16,336 / 7,841 (1.7% / 0.7% of rows), 61% / 47% in "other" |
| 2 | Replay records (W1, W2, W3, W5, grid add/remove, high-cost, R bit 7, inline IC records) | ≤ 1,000/s mean and ≤ 5,000/s in the worst 1-s bucket (ring sizing) | ~510 / ~564 per s (mean) PASS; buckets need P1b |
| 3 | Estimated sim record cost = records x measured per-record time (selftest timing of the clone path) | ≤ 1 ms/s | needs P1b |
| 4 | Rebase rate (sim-side lot graph builds, portal list changes, lot load/remove, build/buy exit) | ≤ 1/s in live play; measured copy time per rebase p95 ≤ 2 ms | 0.12 / 0.18 per s PASS; copy time needs P1b |
| 5 | Off-sim writer rows | only the game's own route job (absent with the snapshot on) and the blend reload | PASS |
| 6 | Probe complete | as §10 | PASS |

Dropped: "writer rows ≤ 200/s" and "known dirty lots per START ≤ 5 at p95" (they sized per-START copies, which design B does not do). Kept as information: STARTs per second and dirty lots per START.

### 11.6 Attribution of the unattributed callers

The P1 caller table keys a hit by the leaf's return address. Static closure: callers of each address through raw `E8`/`E9` scans, stopping at the P1 source frames (39 IC slots, `World_FireEvent`, `Footprint_SetEnabled`, the object event handlers) [verified call edges; which family dominates each count is inferred, I-94-54].

| Return address (hits BP / IP) | Leaf called there | Unattributed sources above it |
|---|---|---|
| `0x63590B` in `0x635700` (2,330 / 815) | grid remove `0x62D9F0` (one per remove-all) | every unattributed remove-all: the `0x82B51F` row, lazy terrain footprints, lazy lot builds, the platform manager and the design D drain (below) |
| `0x82B51F` in `RoutingWorld_LockedLotUpdate 0x82B450` (1,459 / 516) | remove-all `0x635700` | the remove-all of `0x8321F0`/`0x833560` when no frame is open: **Lot PlatformManager footprint refresh `0xB57F20`** (strings "Lot/PlatformManager/existingPlatformObjects"; from `Lot_FinishLoad`, `IC_World_SetActiveLotImpl`, `IC_UserToolUtils_UserToolGenericImpl`, vtables at `0x1085260` and `0x107F8E8`) -> `0x8334A0`; **UserToolWorldObject TryDrop `0xD442B0`** (vtable `0x1087FFC`), which calls the `0x0E` handler `0x9470B0` directly, outside the event list; **`TreeComponent_Create 0x95EC10` -> `FootprintComp_TreeInit 0x948F30`** -> `0x833560`. BP's `0x8321F0` "none" rate (146/min) equals this row |
| `0x833534` in `RoutingWorld_ObjectFootprintFromTool 0x8334A0` (72 / -) | `0x8321F0` | the same `0xB57F20` and `0xD442B0` paths (the only callers of `0x8334A0` besides the `0x0E` handler) |
| `0x666C7A` in `LotQuadTree_Insert 0x666C50` (879 / 388) | W1 apply insert (immediate, tree not pinned) | `PathPlanner_AddFootprint 0x6376D0` under: **lazy terrain footprints** (`World_SurfaceTypeAtPoint 0xAB9580` -> `RoutingWorld_SurfaceAtPoint 0x834B80` -> `EnsureTerrainFootprints 0x8341D0` -> `UpdateTerrainFootprints 0x831710`), reached from `FootstepAudio_Surface 0x8693E0` (audio event handler `0x86C000`) and non-slot ICs (`TestObjectPlacement*`, `FindGoodLocation*`, `FindPlaceOnRoad`, `GetTerrainSurfaceType`, `GetFloorSurfaceType`, `GetInformationAboutRoom`); the `0x82B51F` families; `InRoom_ApplyPortalLockFootprints 0x632460` in plans without a slot frame (`World_FindPlaceOnRoadOffScreen` `0xC7BF70` -> `Route_PlanLocked`) |
| `0x666E54` in `LotQuadTree_Remove 0x666E30` (829 / 283) | W2 apply remove | remove side of the same families (`0x6675B0` from `0x637900` terrain footprints, `0x635700`, `0x632460` portal-lock flips) |
| `0x6669B2` in `LotQuadTree_ApplyRemove 0x666750` (736 / -) | `call edx`: footprint slot inside the remove (W6) | nested in the W2 row |
| `0x63D315` in `RoomGraph_FirstUse 0x63D2F0` (5 / -) | W4 `LotGraph_Build` | lazy first use outside the route ICs: `IC_World_GetConnectedRoomsImpl 0x7929F0`, `IC_World_FindPlacesOnShoreline 0x78C440` (via `LotSearch_Entry`), `0x655EE0` (via `0x63D880`), the `0xC7BF70` plan |
| `0x636CA3`, `0x637365`, `0x637480` in `PathPlanner_AddHangingFootprint 0x636BF0` (1 each / -) | footprint slots, `0x6733C0` | AddFootprint under the same roots |
| `0x85FA55` in `RouteFollow_EditPath 0x85F5A0` (in "other" / 1,927) | W6 `Footprint_EnsureLevelPolygons 0x80E5B0` | **cache fill by steering**: `Locomotion_ObstacleCheck 0x85FD10` in the locomotion tick (docs/82 §2); not an input change |
| `0x834964` in `RoutingWorld_RemoveDiscourageFootprint 0x834860` (in "other" / 203) | remove-all `0x635700` | **design D's own drain** (ts3native replays queued discourage removes at `+0x918` acquires and `RgEnter`, outside the IC frame, docs/84 §2.1) |
| "other callers" (10,023 / 3,709) | | table full after the first keys. By the static map: W6 fills from readers (`0x80E5B0` callers `0x80E690`, `0x80E8A0`, `0x80E910`, `0x85FA50`), deferred flushes started by readers (`LotQuadTree_FlushDeferred 0x666A40` from `QuerySegment 0x667400` (steering `0x82F0B0`), `QueryPoint 0x667480` (`FootprintsNearPoint 0x631F10`, `FootprintsAtPointNoLock 0x631FD0`), `Pin 0x666F70`), and the families above |

**P1b frames** (pass-through stubs or return-address frames as in `values.cpp`; prologues to check with `hook-sites.ts --check` and docs/68): "lazy terrain" `0x834B80`; "platform" `0xB57F20`; "user tool" `0xD442B0`; "tree init" `0x948F30`; "lazy lot build" `0x63D2F0` when no route IC frame is open; "off-IC plan" `Route_PlanLocked 0x82C6E0` when no route IC frame is open; "reader flush" `0x666A40` (its W1/W2 hits belong to the earlier queued insert, order kept); "design D drain" (ts3native sets the frame itself in its drain loop); W6 hits outside any writer frame count as "cache fill" and leave the unattributed total. The caller table grows to 64 keys keyed by (leaf, return, next frame).

### 11.7 P1b: what to add before P3

| Item | Why | Effort |
|---|---|---|
| The §11.6 frames, the 64-key caller table, a "cache fill" class for W6 | G1 check 1 | ~30-45 min |
| Records per 1-s bucket (max, p99) and the record count by kind | G1 check 2, ring size | ~15 min |
| Lot size walk at world event 7 (per lot: trees, leaves, entries, footprint bytes, polygon vertices) and one timed deep copy of the largest and the median lot on the sim | I-94-56, rebase cost (G1 check 4); confirms A's cost estimate | ~30 min |
| Lots per job: distinct lots passed to `LotSearch_Entry` per `RoutingJob_Execute`, split start / goal / transit | I-94-50, how often option A would have needed a lazy lot | ~15 min |

Run: the `snap-p1` queue again (counts only, daytime OK), BP and IP, 10 min each.

## 15. P1b status: attribution frames and design B record rates (0.27.0, counters v48)

> **Changes 2026-10-18:** new section. Build only; the game was not run. Builds the G1 inputs of §11.5 on top of P1 (§10) in `routesnap.cpp`; `snap.cpp` gets one helper (`SpProbeLotCopy`) and a layout assert. Ledger: docs/51 §69 (I-94-70 to I-94-76).

**What is built [verified offline]:** still `[RouteSnap] Count=1` (no new switch, observe only, every hook runs the game's code unchanged).

| Part | What it does | Counters (v48) |
|---|---|---|
| Six source frames (§11.6) | entry detours on Lot PlatformManager refresh `0xB57F20`, UserToolWorldObject TryDrop `0xD442B0` (its direct `0x0E` handler call is inside), `RoutingWorld_SurfaceAtPoint 0x834B80` (lazy terrain footprints), `World_FindPlaceOnRoadOffScreen 0xC7BF70` (the road plan without a route IC), `LotQuadTree_FlushDeferred 0x666A40`; the call `0x95EFF2` in `TreeComponent_Create` to `FootprintComp_TreeInit 0x948F30` (its only caller; the entry itself has a `call rel32` in its first 5 bytes) [bytes, call edge and branch scan verified on the Steam 1.67 exe]. Each pushes a return-address frame on the sim thread, like the P1 IC frames | `snSrc2[row][source]`, `snFrameCalls[6]` |
| Design D drain | `g_discDraining` (discourage.cpp, sim thread) is a source of its own and wins over an open frame: the drain replays the game's Add/Remove outside any IC frame | `snSrc2[row][6]` |
| Cache fills | a W6 hit with no source open counts as a cache fill and leaves the unattributed total (§11.6: steering `0x85FA55`, readers of `0x80E5B0`) | `snCacheFill` |
| Caller table | 64 keys: leaf << 24 \| return address, plus the next frame (the first word above the return slot that is a code address right after a `call`; heuristic, frame sizes are not known per caller) | `snUa2Key/Next/N[64]`, `snUa2Other`; v46 `snUaKey` stays 0 |
| Design B records (§11.4 step 2) | one record per hit of W1, W2, W3, W5, grid add `0x636BF0`, grid remove `0x62D9F0`, high-cost `0x6733C0`/`0x670510`, R bit 7 (`0x82B590`, `0x82A190`), and per call of the four lock-key ICs and the four portal-field ICs (cost, age/gender, lock status, ocean owner) from their slot shims; the route worker's own writes are not records (they go to its clone in B); records nested in a rebase IC frame (portal ICs 4-11, `OnExitBuildBuy`, `BuildRoutingData`, bulldoze, houseboat create/delete, `LotSetTerrainHeight`) count as suppressed | `snRec[12]` |
| 1-s buckets | records per wall-clock second into 8 bins (seconds with none included), the worst second since start | `snRecSec[8]`, `snRecMaxSec` |
| Record cost | QPC time of the record bookkeeping (lock, bucket), and on each sim W1 hit a clone-equivalent copy into a 64 KB scratch ring: footprint 0x44 bytes (W1 arg 1 = the footprint [verified `0x665C58`]), the level 0 cache entry `[fp+0x38]` (0x24 bytes [verified `0x80E5B9`]) and its polygon vector (16-byte elements whose `+0` is a heap block [verified `0x61EE20`]; element `+4` = block end [inferred, I-94-72]) | `snRecUs`, `snCloneUs/N/BytesK/MaxBytes/Fault` |
| Rebase copy | the lot of each sim `LotGraph_Build` (W4 arg 1 = lot graph = lot table node + 0x10, as P3's `SpRefNote`) is copied once with P3's canonical `SpBuildLot` at the next route START and timed; P3's image counters are left as they were | `snLotCopyN`, `snLotCopyUs[8]`, `snLotCopyMaxUs/MaxKB`, `snLotCopyFault` |

**Rules kept from P1:** one owner per address (docs/68 rule 3): `0x666A40` is a `[ProbeValues]` site; when that section owns it, `[RouteSnap]` counts from inside its stub and pushes **no** frame there, because a swapped return address would change the owner's caller keys (`VfRealRet` only knows the `[Verify]` thunk). The same holds for W4 (`[RouteGate]` site 3): no rebase frame there.

**G1 as the report now applies it** (`snap-counters-report.ts`, §11.5, PASS only when all six pass; v46 run files give OPEN on 1-4):

| # | Check | Counters |
|---|---|---|
| 1 | unattributed ≤ 1% of sim writer rows, top caller < 0.5%, "other" bucket ≤ 1% | `snUnattributed`, `snUa2*`, `snLeaf[0..16][0]` |
| 2 | records ≤ 1,000/s mean and the worst 1-s bucket ≤ 5,000 (top non-empty bin; p99 printed) | `snRec`, `snRecSec` |
| 3 | (bookkeeping + clone-equivalent copy time) / window ≤ 1 ms/s | `snRecUs`, `snCloneUs` |
| 4 | rebases ≤ 1/s (sim W4, sim lot removes, rebase ICs) and lot copy p95 ≤ 2 ms (OPEN while no copy was timed) | `snLeaf`, `snIc`, `snLotCopyUs` |
| 5 | no writer row on an "other" thread (blend reload and route worker are known) | `snLeaf[..][2]` |
| 6 | probe complete (`snReady` bits 0-2, 0 skipped, 39 slots, no frame overflow) | as §10 |

Information lines (no longer gates): STARTs/s, known dirty lots per START p95, writer rows/s. On the P1 BP file the new report reads: unattributed 1.87% of 871,618 sim rows (top caller 0.27%, other bucket 1.15%), ~505 records/s from the leaf rows, rebases 0.12/s; checks 1-4 OPEN (v46).

**Known limits [inferred, I-94-73, I-94-74]:** W4's own nested leaf hits are not suppressed (no frame at the `[RouteGate]` site), so the record rate and the worst bucket are upper bounds by the leaves of 0.12-0.18 lot builds/s; the clone-equivalent copy covers level 0 only and does not force the polygon build (a lower bound for the copy, an upper bound for nothing); the deferred flush frame also wraps flushes that a writer starts inside an IC, so `snIcWrites` can drop against P1 for the same play. Not built from §11.7: the lot size walk at world event 7 and lots per job (I-94-50, I-94-56 stay open; the timed lot copies give the rebase side).

**Checks:** `build.cmd` selftest: `TestRouteSnap` (v46 parts, now with the 64-key table) and `TestRouteSnapP1b` (each P1b frame with a leaf inside, a shared site without a frame, the drain over an IC frame, cache fill, caller keys with next frame and overflow, record kinds, 1-s buckets with empty seconds, other-thread records, rebase suppression, IC records, the clone copy on a fake footprint and its fault path, the rebase lot copy on the snap fake world with P3 counters unchanged, a live frame detour on a dummy); `steerro-test` seeds 1 and 2 x 3000 ok, "38 sites and 39 IC slots match" on the image; `hook-sites --check` 0 missing (docs/68 row added); `snap-counters-report.ts --self-check`; queue dry-run; `check-docs-code` ok.

**Run (counts only, daytime OK):** asi parked at `game-copy/ts3native.asi.snapp1b` (sha256 `0b219edeb31aaf7256f43cf74e89989c2cc26eb686797d07c186502f4bff603b`); `node tools/harness/queue.ts tools/harness/queues/snap-p1b.json` (jobs `bp-snap-p1b`, `ip-snap-p1b`: the `snap-p1` settings, 10 min each, `minCounters` 48), then `node tools/harness/snap-counters-report.ts probe-out/queue/snap-p1b-<date>-bp-snap-p1b-native.json` and the same for IP.

**Cost [inferred]:** P1's stubs plus six frame stubs (the frames are entries of rare functions except the terrain lookup and the flush), two QPC reads per record (~0.5-2k records/s), one 100-500 byte copy per sim W1 (~100/s) and one lot walk per sim lot build (0.1-0.2/s): well under 1 ms/s of sim time, the same order as the quantity being measured; the report's check 3 includes our own bookkeeping, so it overstates design B slightly.

**Effort (hours, Claude with Rodney):** this build ~1.5 h; the two runs ~25 min of game time plus reading; if check 1 still fails, one more frame per remaining family ~10-20 min each.

### 15.1 Polygon cache layout (I-94-72)

> **Changes 2026-10-18:** new subsection. Static only (disassembly of the Steam 1.67 exe plus one emulator run); no game run, no code change. Answers I-94-72 and explains the live P1b result: the clone-equivalent copy faulted 647 times out of 39,132 (1.7%, `snCloneFault`). Ledger: docs/51 §79 (I-94-120 to I-94-127).

**Who has a cache [verified].** Only footprints built by the middle constructor `0x80EED0` (outer vtable `0x1020AE8`) have one; that is every concrete class in docs/82 §3.1 except the box footprint `0xFEAE08`. Their vtable slot `+0x70` is `Footprint_EnsureAllPolygons 0x80E610`. The box class (constructors `0x80B3C0`, `0x620C80`, on the base constructor `0x807A20` only, 0x4C-byte object) has slot `+0x70` = an empty `ret` (`0xC0FD60`) and stores its box as four floats at `+0x38`, `+0x3C`, `+0x40`, `+0x44` (`0x80B451..0x80B46B`), flags at `+0x48`/`+0x49`. The lot table entry holds two such boxes at `+0x84`/`+0x88` (`LotTable_InitLotEntry 0x630CF0`, allocated with tag `0xFEAF90`, size 0x4C).

**Layout [verified at the addresses given].**

| Where | Field | Evidence |
|---|---|---|
| footprint `+0x38` | pointer to the level entry array, 0 if the allocation failed | `0x80EEF0` zeroes it, `0x80EDC0` fills it |
| footprint `+0x3C` | level count n (1 or 3; equals vtable slot 0: `0x619EF0` returns 1, `0x83F5F0` returns 3) | `0x80EE48`, `0x80EEB8` |
| array | n = 1: one 0x24-byte block from a pool free list (`0x1165D48`); n > 1: `GeneralAllocator_Memalign` of n * 0x24, align 4 | `0x80EDC0` |
| entry `+0x00/+0x04/+0x08` | polygon vector begin, end, capacity (16-byte elements) | `0x80E716..0x80E749`, `0x806700` |
| entry `+0x0C` | allocator name pointer (`0x1020AA0` after construction) | `0x80EF1E` |
| entry `+0x10..+0x1C` | bounds, four floats: zeroed by the build, then `+0x18 = +0x10`, `+0x1C = +0x14` and grown over the polygons by `0x676EB0` (bounds slot `+0xFC 0x80E570`); min x, min z, max x, max z order [inferred] | `0x80E5E2..0x80E5F1`, `0x80E57D..0x80E59A` |
| entry `+0x20` | valid byte: 0 = not built or invalidated, 1 = built **or build in progress** | `0x80E5BF`, `0x80E5CB`, `0x80E0C8`, `0x80F6F9` |
| polygon element `+0x00/+0x04` | vertex begin, end; vertex stride 8 bytes (x, z floats; count = (end - begin) >> 3) | `0x80E720..0x80E72B` (count passed to `0x7CE550`) |
| polygon element `+0x08` | vertex capacity | `0x806700` (16-byte element) |
| polygon element `+0x0C` | allocator name pointer (`0xFEBF4C` "Routing/PathPlan/FootprintPoly" from resize, `0x1020498` "Routing/Footprint" in the box path of `0x80CB40`) | `0x80673A`, `0x80CCB3` |

**When it is built and cleared [verified].**

- **Build** `Footprint_EnsureLevelPolygons 0x80E5B0(level)`: returns at once if the valid byte is set. Otherwise it sets the valid byte to 1 **first**, clears the polygon vector with `0x61EE20`, zeroes the bounds, then calls vtable `+0xF8` (polygon build; for `0xFEB848`/`0xFEB970` it forwards to `+0x100`, e.g. `0x80CB40`, which resizes the vector with `0x806700` and fills vertices) and `+0xFC` (bounds). `0x80E610` (slot `+0x70`) does the same for every level. Emulator: 300 cases, 100% block coverage of `0x80E5B0` and `0x61EE20` (spec run from the scratch directory, not committed): the build stub sees valid = 1 and an empty vector, every non-null vertex block is freed, the element `+0` words stay as they were, bounds end at 0, a valid entry makes no call.
- **Clear** `0x61EE20` (vector of polygons): frees each element's vertex block with `EA_Free 0x4E3F70` and sets end = begin. It does not free the polygon array and does not null the element `+0` words (dangling, but outside [begin, end)).
- **Invalidate** vtable `+0x18` `0x80E0B0`: sets the valid byte to 0 for all n levels and nothing else; the old polygons and vertex blocks stay allocated (stale) until the next build frees them.
- **Install from outside** `0x80F6A0` (copy-assign through `0x80BA90`) and `0x80F710` (swap begin/end/capacity with the caller's vector): bounds, then valid = 1.
- **Who builds on the sim path:** W1 itself. `LotQuadTree_ApplyInsert 0x665C50` calls slot `+0x70` at `0x665D36`, after its early exits and **after** the P1b detour point (the entry). Lazy readers (`0x80E690`, `0x80EF80`, `0x80F790`, steering `0x85FA50`, ten call sites in all) build one level on first use.

**Copy recipe (design B clone, sim thread).**

1. Read the outer vtable `[fp]`. If slot `+0x70` is not `0x80E610`, the object has no polygon cache: copy it flat (a box footprint is 0x4C bytes, so 0x44 is short by its flags) and stop. Never read `+0x38` as a pointer for such an object.
2. Read n = `[fp+0x3C]` and the array `[fp+0x38]`. If the array is 0, copy the footprint with array 0 (`0x80E610` and `0x80E0B0` check for 0, `0x80E5B0` and the lazy readers do not, so such a clone must not reach them).
3. Take the copy **after** the game's own build: hook W1 so the copy runs after `0x665D36` (or call slot `+0x70` yourself on the sim thread before copying; it is what W1 does next anyway). Then every level has valid = 1 and current polygons.
4. For each level L < n, entry = array + L * 0x24. If the valid byte is 0 (only possible when step 3 was skipped), do not copy the polygons: they are stale. Either build first (step 3) or give the clone an empty vector with valid = 0, and then the clone must never be built on the worker, because the build reads the object model (`0x938B50`, `0x939560`, `0x935770` in `0x80CB40`).
5. Copy the 0x24-byte entry, then give the clone its own polygon array: count = (end - begin) >> 4 elements of 16 bytes; for each element allocate its own vertex block of (end - begin) bytes, copy, and set begin, end, capacity = end in the clone. Keep each `+0x0C` allocator name word as it is.
6. Allocate the clone's vertex blocks and polygon array with the game allocator (`EA_AllocTagged 0x4E3F90` with the same tag), not our own heap: any game code that clears the clone's cache frees vertex blocks with `EA_Free 0x4E3F70`. The clone's level array (n * 0x24 bytes) is ours and is freed by our code; do not take it from the n = 1 pool free list `0x1165D48`, which the game's footprint code owns (not shown to be thread-safe [inferred]).
7. Point the clone's `+0x38` at its own array and keep `+0x3C`. Valid = 1 and an empty vector is a real built state (a build that produced no polygon, e.g. `0x80CB40` with no object), not an unbuilt one.

**How to tell an unbuilt cache:** valid byte `+0x20` = 0. Begin = end alone does not mean unbuilt, and begin ≠ end with valid = 0 means stale (invalidated, polygons from the previous transform). On a thread other than the builder's, valid = 1 can also mean a build in progress (the byte is set before the vector is filled), so off the sim thread neither field proves a finished cache.

**Likely cause of the 1.7% faults [inferred, I-94-125].** `SnCloneProbe` treats `[fp+0x38]` as the level array for every W1 argument. For a box footprint `0xFEAE08` that word is the box's min x as a float, so the probe reads 0x24 bytes and then two more pointers from an address made of float bits: a fault when unmapped, garbage copied (and counted as a success) when it hits mapped memory. Faults are therefore a lower bound on box hits. Less likely causes: an invalidated level is stale but still allocated, so it does not fault (I-94-123); a build racing on another thread frees vertex blocks the probe may read, but the freed heap memory normally stays mapped (I-94-126). Test: the next probe build checks slot `+0x70` before reading `+0x38` and counts W1 hits per outer vtable; if `snCloneFault` drops to 0 and the box count is at least 647, the cause is confirmed. Copy time from P1b is still a lower bound for design B: it did not build the cache and did not allocate (I-94-127).

## 17. P4: non-routing live reads (I-94-69, I-94-66)

> **Changes 2026-10-18:** new section. Static design only: no code, no game run. Sources: docs/101 §2.2 (pointer roots) and §7 (rooms, floor heights); docs/91 rows 3, 8-10, 14; docs/102 §4.1 and docs/106 §3 (engine sites that fired live); docs/60 §2.2 (closures #1-#10 of the async job); docs/88 §1-§2 (destroys); docs/38 C1 and `heightcache.cpp` (terrain graveyard); docs/108 §7 and §11.1 (rates). New static facts from capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67): the blocker record builder and both its call sites, the path's blocker getter, the node copy `0x819E10` and the subpath record setter `0x81A0E0` (§17.5). Ledger: docs/51 §71 (I-94-90 to I-94-98).

### 17.1 Summary

- **Twelve non-routing reads** sit in `Route_PlanLocked`'s closure (§17.2). Under option G the clone run reads them live, without the live `+0x918`. One needs a copy (lock-key sets, already §12.8 item 1). One is replayed (door answers, §12.5). Three are static after load or while the sim runs and are read live with no guard (world bounds and sea level, blend layer 0, floor heights). Seven change in play and **need a guard during the clone window**: lot index, world lot tree, Lot objects, room partitions, objects by id, physics quadtree, terrain cell shapes. None needs the sync fallback in normal play; the fallback stays for lazy lot builds (D4) and the cases already in §12.6.
- **The guards exist already, for the async job** (docs/60 §2.2 closures #1, #2, #4, #5, #6, #7, #10). The §12 design switches most of them off for the clone run, in two ways: (1) §12.4.3 passes every `rtsites.cpp` stub through on the worker, which drops the worker-side `g_qtLock` hold (#1) and the `WM+0x228` hold (#5); (2) the sim-side closures key on job-live (`RgJobLive()` in `routegate.cpp`: `g_callerTid` set or `g_async.state == A_RUNNING` [verified source]), which is false after `J_DONE`, when the shadow runs (§12.4.2 step 2). So the shadow as designed would read the physics quadtree, the object hash, Lot objects and partitions with no guard, a crash risk the async job does not have today. **Design change (§17.4): the clone window counts as "live" for the non-routing closures only**, through `SnLive()` with an in-flight handshake, and the worker-side guards stay on in a clone run.
- **Answers can differ between the live run and the shadow** for reasons that are not defects of the copy: Sims move (physics), objects are destroyed, the camera swaps terrain LOD shapes. §17.4 D5 counts each writer family between the live `Route_PlanLocked` entry and the end of the shadow; a mismatch with such a write in the window is M5 (stale), not M2.
- **I-94-66 [verified for every field the plan writes]:** blocker records are 40-byte plain values (kind, two 64-bit ids, x, z, a level number), built by one function from two call sites and copied with `rep movsd`; route nodes and subpath records hold only scalars. A path built in a clone run holds no pointer into the routing world, only links to sibling paths of its own list (§17.5). K5 stays as the closing test.
- **Effort:** +2-3 h on P4 (P4 (G) now ~10-16 h). docs/88 design G becomes a prerequisite of `Mode=2` (§17.4 D6).

### 17.2 The reads

"Window" = from the live `Route_PlanLocked` entry to the end of the shadow: the live job (mean 5.1-6.1 ms in BP, 10.6 ms in IP, docs/88 §4) plus the shadow (§12.4.2), at 38.3 / 19.1 route STARTs per second (§11.1).

| # | Read in the search | Data, writer, thread | Rate in play | If read while it changes | Handling |
|---|---|---|---|---|---|
| N1 | World manager root `0x11ECBC4`: origin and bounds `WM+0xB4..+0xC4` (G cell index `0x66D850`, range test of `0xC6BAB0`); sea level `0xC61670` | world load | 0 [verified live, docs/108 §7 F-12, L10] | nothing | **live, static after load**; a new world moves the epoch (§12.6 `epoch`) |
| N2 | Lot index: `World_LotLookup 0xC68990 -> WorldManager_LotGridLookup 0xC688A0` (42 sites in P, also cost mode 3), lot list `0xC68960` | `WorldManager_RebuildLotIndex 0xC68680` under `WM+0x228`, main or sim, lazily when `WM+0x1CC` is set (by `Lot_Load`, houseboat create, `World_RemoveLotCommon 0xA48CC0`, docs/109 §D) | rare: lot add, remove, move [not measured] | the rebuild swaps the vectors: freed memory (crash) or a lot not found (other answer) | **live under the docs/60 #5 worker-side `WM+0x228` hold**: a worker-only stub, so it covers a clone run once it is not passed through (D1). A lazy rebuild on the worker nests, as today |
| N3 | World lot tree `WM+0x9C`: `WorldManager_FindLotById 0xC60D30` / `FindLotById 0xC62D40` -> `SceneNode_FindRecursive` (docs/102 §4.1 #18-20, fired live, docs/106 §3), from lot builds and room ids `0x635440` | lot load, houseboat create and delete, lot remove (sim) | rare | a relinked tree under the walk: crash | **live, writers drained** (D2: gate 15, saves, travel); world events `0x0D`/`0x0E` in the window give M5 |
| N4 | Lot objects: key `+0x48/+0x4C`, flag `+0x4BC`, bounds `+0x148..`, houseboat corners `+0x134`, transform for `Lot_WorldToLocal2D 0xAB8540`; job start test `0xB01390`, floor helper `0xAB1F00` | lot load and create (`Lot_FinishLoad`, `0xACEB20`), houseboat sailing (`Lot_SetHouseboatParent 0xAB9C40`), lot remove and `Lot_dtor 0xAC1EB0` (sim). Streaming bytes `+0xC1/+0xC4` (main) are not search inputs [inferred] | 0 outside lot events and sailing | torn transform: another lot-local point (M2); freed Lot: crash | **live, static between lot events**; a sailing houseboat lot is already refused (§12.6 `unsupported`); lot create and remove drained (D2) [I-94-95] |
| N5 | Floor heights `Lot+0x290` (`0xA8DF80` through `Lot_LevelAtPoint 0xAB78B0`, from `LotPlan_Inner`) | lot load; floor and terrain edits in build mode (`LotSetTerrainHeight` IC, floor tools) | not measured; none expected outside build mode | another level or height (M2); a resized level grid: crash | **live, static while the sim runs**: build/buy pauses the sim, so no route START happens during edits, and a window open at build entry closes in ms [inferred, I-94-94] |
| N6 | Room partitions `Lot+0x298` (`PartitionManager_RoomAtPoint 0xB499D0` from `0x624780`; `0x635440` in lazy builds) | lot load and create; rebuild `0xB54B60` from `RoutingSystem_RebuildLotRouting 0x835110` (bulldoze, houseboat, lot save, `BuildRoutingData`); wall-graph edits (build mode) (sim) | **0 writes in 10 min, BP and IP** [verified live, docs/108 §7 U8] | a rebuild frees grid blocks: crash; a stale room id | **live, writers drained** (D2: gate 15 and saves wait for the window) |
| N7 | Objects by id: `ObjectService_FindById 0x939560` (no lock, no AddRef; 15 sites in the job closure, docs/88 §1), component lookup `0x958560`: footprint owners, stairs, portal owners | create (lock-free insert, publish order verified, docs/60 #4); `ObjectService_DestroyById 0x939420`: hash erase (safe for a concurrent reader [verified, docs/88 §1]), then the last `Release` frees the object (not safe) (sim) | ~35 destroys/s in all; 1.0-1.2/s inside a job in BP, 0.15-0.3/s in IP [verified, docs/88 §2] | object freed under a raw pointer: crash; destroyed in the window but found by the live run: other answer (M5) | **hold a reference.** P4: a destroy in the window waits for it (docs/60 #4, extended by D2). Before `Mode=2`: docs/88 design G (park one reference, destroy now, release at a point with no job and no clone run) covers the window without a wait |
| N8 | Physics loose quadtree: `Routing_PhysicsRangeQuery 0x83B3A0` (from `0x654CC0`, hierarchical planner) and `Routing_PhysicsRayVertical 0x6583E0` (from `0x6593F0`, the obstacle path that builds blocker records) | shape add `0x6823E0`, remove `0x682080`, move `0x6819B0` (through `PhysShape_SetTransform 0x6871C0`); sim thread only, other threads post messages (docs/33); no lock | every moving object, every tick [not counted per second; object event `0x0E` drives 73-84% of the 240-322 footprint rebuilds/s, §11.1] | reinsert under a query: crash or a missed shape; positions also move between the live run and the shadow (other answer) | **live under `g_qtLock`** (docs/60 #1). No copy (thousands of shapes, changed every tick) and no fallback: the async job reads it live today. D3: the sim's mutators take the lock always while P4 is armed. Mutations in the window give M5 when the plan called a physics reader |
| N9 | Terrain height: `Terrain_GetHeight 0xC6A540`, `World_GetHeightAt 0xC6A250 -> WorldCellGrid_Raycast 0xC2A570` -> cell shape `cell+0x38` (~70 call sites; cost mode 3) | `WorldCellGrid_SetCellShapes 0xC2A840` on a terrain LOD change (main, no lock); terrain edits (build mode) | 64 swaps per 7 min in BP (~0.15/s) [measured, docs/60 #2] | use-after-free of the old shape (docs/38 C1); a height from another LOD shape [inferred equal, docs/91 §3.8] | **live**: `[TerrainGrave]` parks the swap's release while any ray is in flight on any thread (`g_tgInFlight`, counted in `[HeightCache]`'s `0xC2A570` stub, which the worker's rays also pass). That stub must stay active in a clone run (D1). A swap in the window gives M5 [I-94-96] |
| N10 | Terrain blend layer 0 (`0xC6BAB0` from the cost modes; `[[WM+0x54]+0x0C]+0x2C`) | world load; reload pass `0xC62AC0` (main, a value copy, §9.4) | 0 in play [verified, §14 from the P1 data] | another weight (other answer), no free | **live, static after load**; a reload in the window gives M5 |
| N11 | Lock-key sets through `fp+0x20` (on the object's footprint component, docs/101 §6) | the 4 lock-key ICs (sim, unlocked) | rare [not measured] | set reallocated under the read: crash; another door answer | **copy** (already §12.8 item 1: the working copy's footprints point at copied sets; design B records the IC values) |
| N12 | Door permission: World event 59 through the script host `0x11D4BBC` (`0x791C00` from `0x64F4D3`) | managed, per Sim | 4.30 / 8.78 calls per plan [verified, docs/108 §7] | — | **replay** in `Mode=1` (§12.5); docs/105 mirror in `Mode=2`. Not new |

Not reads of changing data: allocators and the three locked pools (docs/101 §2.2 #4-#6), component bit constants. `0xAC5832` (world object queries) was not reached in either world (docs/106 §3).

### 17.3 Pairs a writer updates together (I-94-69)

I-94-69 asks whether a writer that changes a Lot object and its routing data together can be seen half-done. Under G the routing half is the copy (the cut plus design B records up to the START sequence) and the non-routing half is live, so the question becomes: which pairs can the clone see from two different times?

| Pair | Writer | Seen split? | Effect | Handled by |
|---|---|---|---|---|
| Lot index, lot tree + lot table, lot graph | lot add and remove, houseboat create and delete | yes, if the lot event lands after the cut | a lot found live but missing in the copy: the clone run starts a lazy `LotGraph_Build` on the copy | D2 drains the writers during the window; D4 trips on a lazy build in a clone run (M4 `lazy`, sync in `Mode=2`) |
| Partitions + footprint room ids (`0x635440`) | `RebuildLotRouting 0x835110` | yes | stale rooms, or a freed grid | D2 (gate 15 waits for the window) |
| Floor heights + floor footprints | build-mode edits, then `OnExitBuildBuy` | no in live mode (no START while building) [inferred, I-94-94] | — | build/buy exit is already a rebase and gate 14 |
| Physics shape + object footprint | object move: `0x6871C0` (physics, live) and `UpdateObjectFootprintCore 0x8321F0` (copy, recorded) | yes: the footprint as of the START sequence, the shape as of the clone run | another obstacle answer; no crash | M5 through D5. The async job has the same skew today in a smaller window (footprint writers wait on its `+0x918`, physics writers do not) |
| Object + footprint owner id | destroy | yes | owner not found in the shadow | D2 wait (P4), design G (P5); M5 |

So I-94-69 resolves as: **no crash path once D1-D3 are in; time skew only, which D5 classifies.** The live verdict comes from the shadow runs (I-94-92).

### 17.4 Design changes to §12

- **D1. Pass-through exceptions (§12.4.3).** In a clone run these keep acting: the `rtsites.cpp` #1 reader stubs `0x6583E0`/`0x83B3A0` (take `g_qtLock`), the #5 worker stubs `0xC688A0` and `0x82C420` (take `WM+0x228`), and `[HeightCache]`'s `0xC2A570` in-flight count used by `[TerrainGrave]`. The §12.4.3 row "`rtsites.cpp` ... pass through" becomes "pass through except #1 and #5". The other `rtsites.cpp` sites (destroy, saves, debug frame, lifecycle) are sim-side and are not hit from the worker.
- **D2. The clone window is "live" for the non-routing closures.** `SnLive()` = `g_snapRunTid != 0`. Rare sim-side writers use an in-flight handshake (the `[TerrainGrave]` pattern): the stub does an interlocked increment of `g_snNrIn`, then reads `SnLive()`; if live, it decrements and waits for the window to close (pumping, as `WaitAsyncDone` does); if not, it runs the original and decrements. The worker sets `g_snapRunTid` with an interlocked exchange and spins until `g_snNrIn` is 0 before its first read. Interlocked operations are full fences on x86, so a writer has either finished before the first clone read or waits [design, I-94-91]. Applied to 8 sites: `ObjectService_DestroyById 0x939420` (#4), gate sites 14 `0x835240` and 15 `0x835110` (#7), `Save_WritePackage 0xAAC110`, `IC_World_BuildRoutingDataImpl 0x78AE40` and `RoutingSystem_SaveLot 0x8361B0` (#6), and the travel/quit transition ICs `0xEC1180`/`0xEC11B0` (docs/60 §2.5 drain), which also empty the shadow queue. Job-live behaviour is unchanged; `SnLive()` is an extra condition.
- **D3. Physics mutators lock always while P4 is armed.** With `[RouteSnap] Mode>=1` the #1 writer stubs (`0x6823E0`, `0x682080`, `0x6819B0`) take `g_qtLock` on every call, not only while a job runs, so the hottest writer needs no handshake. Cost: one uncontended critical section per shape update, tens of ns; contended only while the worker is inside one of the two physics readers [inferred, I-94-93; the existing `vfRtQtWaitUs` measures it].
- **D4. Lazy-build tripwire.** `LotGraph_Build 0x63CB90` (owned by `[RouteGate]` site 3) and the insert path of `LotTable_GetOrInsert 0x62EAB0` count `snCloneLazy` when hit in a clone run and set `abort = lazy` (M4). The run is not cut short: the build writes only the copy (through T4) and reads Lot objects under D2. In `Mode=2` the job becomes `J_SKIPPED` and the plan runs sync. Expected 0 with design B's full clone [inferred, I-94-97]. **Changed in 0.30.0 (P4-H20c, §21.8):** the run is now stopped before the build or insert runs (our SEH exception `0xE0503401`, caught by the clone run's guard: record `lazy`, clone discarded, P4 stays on), because their pushes would grow the clone's `RpAlloc` vectors with the game's allocator (an EA free of our block) and write portals into the copy.
- **D5. Window counters and M5.** At the live `Route_PlanLocked` entry the job saves seven counters and reads them again at the end of the shadow: physics mutations (the #1 writer stubs), terrain swaps (`vfSwaps`), destroys (`0x939420`), lot index rebuilds (`0xC68680`, count-only today), world events `0x0D`-`0x10`, blend reloads (`0xC62AC0`) and lock-key IC calls. A family counts only if the plan called one of its readers (a per-job bitmask set by the N2, N7, N8 and N9 reader stubs on both runs). Each family that changed sets a bit in a new `nr` field of the §13 record (record `v` 2); `shadow-compare-report.ts` classes a mismatch with `nr != 0` as M5 and prints the bits.
- **D6. `Mode=2` prerequisite.** Before P5 takes clone answers, docs/88 design G is built and tests `SnLive()` as well as job-live, so a destroy in the window parks a reference instead of waiting.

**Fallbacks added to §12.6:** `lazy` (D4); a travel/quit drain during a shadow discards it (`epoch`). No other read needs the sync plan: N2-N10 are safe under D1-D3, and N11/N12 read no live data.

**What D2 costs in a shadow run.** Destroys in the window wait for it. At 1.0-1.2 destroys/s inside jobs in BP and a window of one job plus one shadow, that is about the docs/88 job-wait cost again (7.8-8.8 ms per wait, ~8-9 ms/s of sim waits in BP) [inferred from docs/88 §4]. Shadow runs are correctness runs, not timing runs; the report prints `snNrWaitUs` so this cost is not read as a P4 result. Design G (D6) removes it.

### 17.5 I-94-66: do paths built in a clone run point into the routing world?

[verified, capstone on the Steam 1.67 exe, unless marked]

- **Blocker records.** `0x64E7E0` builds one 40-byte record from eight by-value stack arguments and appends it with `rep movsd` (10 dwords). Its only callers are `0x62737F` (in `0x6271A0`, quadtree leaf entries) and `0x659597` (in `0x6593F0`, the physics vertical-ray obstacle path). Layout: `+0x00` kind (footprint slot `+0xC`: type byte `+0x35`, or 10 for class `0xFEAE08`); `+0x08` 64-bit footprint id (slot `+0x7C`: `fp+0x14/+0x18`); `+0x10` a 64-bit value (slot `+0x80`: `fp+0xC/+0x10`, stored by `UpdateObjectFootprintCore 0x8321F0` at `0x832397` and `0x8326EE` from its 64-bit third and fourth arguments; an id, not a pointer, since pointers are 32 bits here [meaning inferred]); `+0x18`/`+0x1C` the query point x and z; `+0x20` a level number (`(byte +0x32 & 0xF) - 4` of the query). The physics site passes its kind, zeros for both ids and `+0x20`, and two floats. `+0x04` and `+0x24` are never written or read (the getter `0x81D990`, path slot `+0x164`, reads `+0x00` and `+0x08..+0x20` only). `RoutingJob_Execute` copies the records into `path+0xD0` with `rep movsd` at `0x81C6D7`. **No pointer.**
- **Route nodes.** `0x819E10` writes position, direction, `+0x20`, `+0x24`, `+0x28` (always 0: the local is zeroed at `0x819E9C` and never stored again) and flags `+0x2C` (from search-node word `+0x0E`, bits 0 and 1). It does read the routing world (`RoutingWorld_Get` at `0x819E23`, passed on to `0x819680`), which T4 serves as the copy in a clone run, but stores no pointer. The undecoded 64-byte search-node fields (docs/93 §4.3) are read by the builder and the splitter, not stored in the path.
- **Subpath records.** `0x81A0E0` sets up a 64-byte record with `PathType` `+0x10`, object id `+0x18/+0x1C` and `PortalStartPos` `+0x20`, the three values `RoutingJob_Execute` copies into each `RoutingPathImp` (docs/93 §4.2). The splitter also reads world `+0x988/+0x98C` (ocean portal owner, an id).
- **Pointers the path does hold:** its own node and blocker vectors, the sibling links (path slots `+0x78/+0x80`, other paths of the same private list) and its refcount. None points into the working copy.

Verdict: a path built in a clone run stays valid after the working copy is rematerialised or freed, for every field `Route_PlanLocked` writes. The rest of the 0x190-byte object (constructor `0x8204B0` state, the options copy filled by the PlanCommon tail, which runs live) is not written in the clone run [inferred]. K5 (free the working copy, then walk the paths under the debug heap) stays as the closing test.

### 17.6 Checks this adds

| Check | Where | Pass |
|---|---|---|
| K4-NR: a fake sim thread runs the #1 mutators, `DestroyById`, gate 15 and a save entry against dummies while a clone run is armed | `TestRouteSnap` selftest | each writer either finished before the worker's first read or waited for the window; `g_snNrIn` 0 at disarm; no read of a freed dummy (debug heap) |
| K5 (I-94-66) | steerro-test, offline | as I-94-66 |
| Shadow: `snCloneLazy`, `snNrWaitUs`, the `nr` bits, M2/M3 with `nr = 0` | `bp-snap-shadow`, `ip-snap-shadow` | `snCloneLazy` 0; every M2/M3 either has `nr != 0` (then M5) or is explained |
| `Lot_dtor 0xAC1EB0` hits with `SnLive()` set | count-only stub | 0 |

### 17.7 Effort (hours, Claude with Rodney)

| Work | Hours |
|---|---|
| D1 pass-through exceptions, D3 always-lock under P4 | 0.25-0.5 |
| D2 `SnLive()` and the `g_snNrIn` handshake at 8 sim-side sites; the drain empties the shadow queue | 0.75-1 |
| D4 tripwire, D5 window counters, record `v` 2 and the report's M5 rule | 0.5-0.75 |
| K4-NR selftest, `Lot_dtor` count stub, docs/68 rows | 0.5-0.75 |
| **Total added to P4** | **2-3 h** (P4 (G) now ~10-16 h) |

docs/88 design G (D6) is costed in docs/88 and is needed only before `Mode=2`.

### 17.8 Note 2026-10-18: pass 16 node heights (docs/99 §12.8)

Post pass 16 `0x64C7C0` and its helper `0x64C580` give every planner node its real y inside `Route_PlanLocked` (docs/99 §12.8, I-99-50), so the clone run computes node heights on the worker from live data. The reads are the existing N1, N2, N4, N5 and N9, plus two that §17.2 did not list, both inside `World_GetHeightAt 0xC6A250` (and `0xC6A540` for water):

| # | Read in the search | Data, writer, thread | Rate in play | If read while it changes | Handling |
|---|---|---|---|---|---|
| N13 | Foundation height `0xB06A10 -> 0xB066E0` (lot-local through `Lot_WorldToLocal2D 0xAB8540`), from pass 16 for every node | foundation edits in build mode [inferred] | none expected outside build mode [not measured] | another node y (M2) | **live, static while the sim runs**, as N5 [inferred, I-99-54] |
| N14 | Water height `0xB793F0 -> 0xB74D10`, `0xB745A0` (world manager `0x11ECBC4`, `0x7CE4A0`, `0xC6A540`), from pass 16 | pond edits in build mode; ocean static [inferred] | none expected outside build mode [not measured] | another node y (M2) | **live, static while the sim runs** [inferred, I-99-54] |

No new guard: both writers run only in build mode, like N5. A node y that differs between the live plan and the clone run with a terrain swap in the window is M5 through D5 (N9). The §13 example record shows y = -100; real records carry the pass 16 heights, and the compare keeps y (docs/99 §12.5).

### 17.9 Line-of-sight display level writes (I-105-3)

> **Changes 2026-10-18:** new section. Static only (capstone 5 on `game-copy/bin/TS3W.exe`, Steam 1.67; `functions.csv` bounds); no game run, no code change. Live input: the BP value probe of 2026-10-18 (`values-report.ts`, I-105-3 line): `Lot_SetDisplayLevel` calls inside a `World_CanSeeObject` frame from `0x75AC22`, by thread sim / worker / other **146 / 400 / 0** (1,586 from all CanSee callers). Raise and restore are counted separately, so that is about 73 sim and 200 worker raise/restore pairs in 10 min. Ledger: docs/51 §85 (I-105-50 to I-105-59).

**What is written** [verified, docs/105 §1, §9.1]: `lot+0x39C` (plain store at `0xAC05F6`) and bit `0x10000` of the word `+0x2C` of each of the lot's level groups (`PhysShape_SetLevelEnableBit 0x681F20`: a non-atomic `or`/`and` on memory). On the restore (level goes down) the same call first runs the active camera controller's slot `+0x54` (`0xAC05D1..0xAC05F3`, before the quiet test), which zeroes the main camera's anchor `+0x7B0` (docs/107 §5.1). Quiet skips the scene-node refresh, the message and event 68. No free, no allocation on the quiet path.

**Who reads `lot+0x39C`** [verified sites: scan of every function for displacement `0x39C`, 49 functions, of which these act on Lot objects; meaning and thread inferred unless marked]:

| Reader | Use | Thread |
|---|---|---|
| `World_CanSeeObject 0x789DD0` (`0x789E7F`) | decides whether to raise; saves the level it restores | sim (script IC `0x78F0F7`, sync plans), worker (async goal building) [verified live] |
| `IC_World_LotGetDisplayLevelInfoImpl 0x7869F0`, `0xAC23A0` (`LotChangeDisplayLevel` = current ± 1) | script reads; the floor buttons compute the new level from the current one | sim [verified: ICs] |
| `IC_World_LotSetForceDoorVisibilityCheckImpl` -> `0xAB4AA0` | door visibility refresh when display level <= top | sim [verified: IC] |
| `Lot_UpdateObjectSceneNode 0xABFAC0` (`0xABFBC3`) | clamps the object's shown level to the display level (cutaway) | sim or main, by caller (docs/48 §3) |
| Main camera: `0x972B70` (from the controller update `0x976FC0`, `vt_CameraControllerMain` `+0x1C`, and `0x9743C0`), `0x97C410` | display level of the active lot (`0xC5FB00` compare) for the camera's floor | main [inferred] |
| `0xAB6800` (from `0xC636D0`), `0x981CD0`, `0x86CDF0` / `0x86F120` (from `0x86D250`, vtable `0x1029DDC`), `0xAEA440` (copies it into `+0x29C` of a lot render object), `0xABD060` (build tools `0xCD0ED0`, `0xCDA880`, `0xCF4C80`, `0xD3AD50`, `0xD3C520`) | level of a point or object against the display level (picking, placement, lot render state) | not traced |
| Writers' own reads: `Lot_SetDisplayLevel 0xAC0570`, `Lot_RoofUp 0xAC2190`, `0xAC5C90`, `0xACEEE0`, `Lot_Load 0xACF520`; lot save/load streams `0xAC68D0`, `0xACB2C0` | | sim |
| Routing debug `0x815C70` (`RoutingDebug_FrameUpdate`) | sets each lot to its top level | main, debug flags only |

Build-tool functions in `0xCCEDA0..0xD18690` (11) also read a `+0x39C`; not classified (build mode only, where async START is refused). **No routing function reads it**: the direct-call closures of `Route_PlanLocked 0x82C6E0` (1,320 functions) and `IC_Route_Plan 0x765160` (1,385) reach none of the readers above [verified, direct calls only].

**Who reads the level bit** [verified static]: the group word `+0x2C` is written only by `Realm_CreateGroup 0x682120` (`= 0x9FFFF`, bit set) and `0x681F20`, and read only by the ray leaf visitor `0x680F80` (one caller, `LineOfSight 0x6814B0`) within `0x67D000..0x690000`. The bit counts only for a mask with `0x10000` in it: `PhysicsWorld_RayCastSegment 0x686FD0` with its 5th argument set (CanSee, `IC_World_CanSeeSkyFromPosImpl`, `IC_World_PickWallAtWindowPoint`, `IC_World_PickFloorAtWindowPoint` (2), `RoutingWorld_RouteToObjectCheck`'s `0x82AEC0`, camera `0x964D70`, `0x96BB40`, `0x96C480`, `0x974770`, `0x9E57E0`, `0x9E5940`, build tools `0xCCE990..0xCCF390`; the camera functions `0x97F440`, `0x97F6C0`, `0x97F910` pass 0) and **`PhysicsWorld_RayCastVertical 0x686CC0`, which always uses mask `0x10010`**. The vertical ray is the planner's `Routing_PhysicsRayVertical 0x6583E0` (obstacle path `0x6593F0`, goal step `0x658830`). So routing already depends on the display level in vanilla: a shape under a level group above the lot's display level is invisible to the planner's vertical rays [path verified; that object shapes hang under level groups is inferred, I-105-9].

**Can a raise seen by a reader change anything?** Upper floors count as solid (bit set) for one ray set; nothing is freed. Effects by reader [inferred]:
- A sim-thread `World_CanSeeObject` (script IC) that starts during a worker raise sees the raised level, skips its own raise, and can have its rays cut by the worker's restore: a wrong line-of-sight answer. If it starts with a lower level it raises and later restores the value it saw, so whichever restore runs last wins.
- **Lost update** (the persistent case): a sim-thread level change inside the worker's window (floor buttons, `PlayFlowModel` on a lot change, `Sim.cs` roof/attic, a script CanSee) is overwritten by the worker's restore, which writes the old level and bits back without a scene refresh (quiet). The scene shows the player's floor while `+0x39C` and the bits hold the old one: wrong floor for the camera (`0x972B70`), wrong picks (`PickFloorAtWindowPoint` uses the bit), and other planner and RouteToObject answers, until the next level change. The floor buttons (`0xAC23A0`) read `+0x39C` during a raise and go one floor from the raised level. The `or`/`and` on the group word can lose the other writer's bit the same way.
- Sim-thread readers outside `+0x918` (`LotGetDisplayLevelInfo`, `CanSeeSky`, the pick ICs, object scene updates from sim ICs) get one wrong answer; a scene update keeps the wrong cutaway for that object until its next update (flicker of one object, not of the floor).
- Main-thread readers (camera, scene updates from `WorldService_Update`) and the camera call on the restore are **not new**: vanilla runs the raise and restore on the sim thread, which is not the main thread either.

**(2) Real in today's async build? Yes, a race, not a crash.** With `[RouteAsync] Enabled=1` (off by default, so a default install is not affected) the worker runs the whole `Route.Plan` IC, goal building included, and the probe saw 400 raise/restore calls there. The worker holds `+0x918`, which excludes only sim-thread readers that take it (`RoutingWorld_RouteToObjectCheck` and the gated planner sites). Display-level writers and the readers in the bullets above take no lock. Saves are covered (docs/60 #6: a save waits for the job). A sim-thread write inside a worker window was not observed; its rate is unmeasured (I-105-55).

**Fix for today's async build: keep these plans on the sim thread (chosen).** START refuses a route whose options have `ForcePhysicsBasedRaycast` (`RoutingOptions_Test 0x81C880` with `0x2000000`, the test `GoalPoint_Validate` makes before `0x75AC22`); new refusal reason `Los`, counted. The 10 script sites that set the option all build radial destinations (docs/105 §6.4), so this is exactly the set that can reach the raise, and those plans run as in vanilla. Check: `pvRaiseIn` worker = 0 with async on, and the refused share against all STARTs. Effort 0.5-1 h.
Rejected: a lock that every reader takes (main-thread camera, render and pick paths, many sites; it would also make the main thread wait on the worker). Kept for later, if the refused share turns out large or P5 needs these plans off the sim thread: **line of sight without the write.** On the worker, skip both `Lot_SetDisplayLevel` calls in CanSee and give the ray a per-thread level override (lot, level) that the group test uses instead of bit `0x10000` of that lot's level groups. That needs a replacement for the visitor's group test (the only reader, one call site `0x6814D7`), not for the shapes. Exact by construction, but it touches the hottest physics loop. 1-1.5 days with an emulator check against `los_level_mask.py`. Proxying the raising CanSee calls to the sim thread also works, but in async mode a proxy waits for the next READY poll, one tick per call (docs/60 §2.3).

**(3) P4 copy runs: N15**

| # | Read in the search | Data, writer, thread | Rate in play | If read while it changes | Handling |
|---|---|---|---|---|---|
| N15 | Level-enable bit `0x10000` of the level groups (`+0x2C`), read by the clone run's vertical rays `0x6583E0 -> 0x686CC0` (mask `0x10010`) -> `0x680F80`. The clone run never writes it and never reads `lot+0x39C` (closures above) | `Lot_SetDisplayLevel 0xAC0570`, `Lot_RoofUp 0xAC2190`, `0xAC5C90`: player floor changes, script ICs, script CanSee (sim); the live async job's goal building (worker) until the fix above | 1,586 calls in 10 min BP from CanSee callers alone, mostly paired [measured, value probe]; floor changes not counted | another obstacle answer (M2); no free, no crash | **live, counted.** D5 gains the family "display level": calls of the three writers between the live `Route_PlanLocked` entry and the end of the shadow, counted only if the plan called `0x6583E0`; a write in the window = M5. No D2 guard (no crash path). The live job's own raise ends before its clone starts (same worker, goal building first), and with the fix above radial-LOS plans never become jobs. Tripwire `snDlOnClone`: writer calls on the clone thread while `SnLive()` is set, expected 0 |

For P5 (`Mode=2`): the clone answer depends on the display level at clone time, as the live answer depends on it at plan time; a level change between the two is M5, so P5 needs nothing more. Effort on P4: +0.25 h (one D5 family, one tripwire).


## 18. P4 part A status: redirect, capture and shadow plumbing (0.28.0, counters v50)

> **Changes 2026-10-18:** new section. Build only; the game was not run and no asi is parked (nothing can run a clone before P3-B). Code: `native/ts3native/p4.cpp` (new), hooks in `ts3native.cpp`, `routegate.cpp`, `rtsites.cpp`, `values.cpp`, `routesnap.cpp`, `steerro.cpp`, `verify.cpp`, `discourage.cpp`, `heightcache.cpp`, `snap.cpp` (`Mode` now 0-1). Builds §12 except what needs the clone's internals, plus §17 D1-D5. Ledger: docs/51 §73 (I-94-100 to I-94-106).

### 18.1 What is built [verified offline]

Switch `[RouteSnap] Enabled=1 Mode=1` (default `Mode=0`). It installs only with the P3 images, `[RouteAsync]`, `[RouteGate] Enabled=1 Sites=1 Enforce=1` (every docs/60 site) and the `Route_PlanLocked` detour; otherwise `SKIP` and nothing is patched.

| Part | What it does | Counters (v50) |
|---|---|---|
| T1-T4, T2' (§12.2) | the five sites as §12.2.1, every byte set checked first (all or nothing). T1 goes into `[RouteGate]` site 8's trampoline (its relocated `add` becomes `call SnStampAdd` + 3 `nop`), or onto the game's bytes when the gate does not own the site. Thunks compare `fs:[0x24]` with `g_snapRunTid`; the worker's clone run reads `g_snapCtx` (`+0` world, `+4` per-copy stamp, `+8` per-worker clip). T1 wraps to 1 and sets the copy's wrap flag; T2' counts; T3 keeps eax, edx and the flags | `snP4Ready` bits 0-1, `snStampWrap`, `snDeadStamp` |
| Door answers (§12.5) | `P4DoorStub` at the call `0x64F4D3`: chained behind `[ProbeValues]`' stub (its jump slot re-pointed, one owner of the code bytes) or on its own call site. Live capture: calls `0x791C00` itself and records the 9 argument dwords and eax (128 per job). Clone run: first unused entry with equal arguments, else 0 (`NoListeners`) and abort `door-miss`; never a managed call | `snP4Ready` bit 2 / 3, `snDoorRec`, `snDoorReplay`, `snDoorMiss`, `snDoorUnused`, `snDoorOver` |
| Per-job context (§12.3.1) | armed at the async START (1 in `ShadowEvery`, the provider's cut as version); `ServeAsync` sets a capture flag around the job's IC call; `PlanLockedHook` then captures at the live `Route_PlanLocked`: start and goal vectors deep-copied, the options object as 0x9C bytes with its four id vectors (I-94-100), agent, plan byte, guid, the out-list head's tail words; at the leave the answer (per path status `+0x94`, type `+0xB0`, object id `+0xB8`, length `+0x8C`, per 48-byte node xyz, `+0x20`, `+0x2C`), the plan time, the D5 counters at entry, the reader bits | `snShadowArmed` |
| Shadow (§12.4.2) | after `J_DONE` and the signal, on the worker, from private copies only: busy check (another job queued: skipped, no record), refusals (below), `P4CloneAcquire`, arm (`t_snapRun`, `g_snapCtx`, interlocked `g_snapRunTid`, then wait for `g_snNrIn == 0`, D2), the job's FPU state, `g_planOrig(copy world, private args, private list head)` under `__try`, disarm, answer capture, private paths released (`0x757250`'s per-path loop, then `0x866E90` on the head; verified shapes, I-94-106), compare, one JSON line, `P4CloneRelease` with the wrap/discard flags and the worker-dirty lots | `snShadow`, `snM[6]`, `snShadowUs`, `snShadowMaxUs`, `snLiveUs`, `snMatUs`, `snPathsFreed` |
| Fallbacks (§12.6, §17.4) | refusal reasons in `snAbort` (record `refused`): `clone` (no provider), `nover`, `epoch` (`rtClosing`, `rtBusy`, or the P3 epoch moved since the live entry), `cold` (I-94-105), `unsupported` (kind not in `Kinds`), `capture` (a copy failed, more than 16 paths or 4,096 nodes; the IC returning before `Route_PlanLocked` is counted with no record), `nrbusy` (a writer stayed in flight > 250 ms at the arm), `mat` (provider), `door-miss`, `door-over`, `managed`, `event`, `livelock`, `lazy`, `fault`, `other`. A fault: logged with code and address, the worker's leftover gate frames undone, the copy discarded, `snP4Ready = 0` and `g_p4On = 0` for the session (the thunks stay and serve live values) | `snAbort[16]`, `snShadowFault` |
| Pass-through (§12.4.3 with D1) | `t_snapRun` checks: `RgEnter` (site 8's trampoline still runs T1; site 3 = D4), `LockAcquireHook` (live `+0x918` = `livelock`), `LockReleaseHook`, `PvEnter` (`LotTable_Insert` = D4; the door replays behind it), `SnEnter` (writer leaves -> worker-dirty lot keys, 64, overflow = every lot; `World_FireEvent` = `event`), `SnIcEnter`, `QlEnterBody`, `TwEnterBody`, `VfEnter`, the discourage Add/Remove hooks, `RtEnter` except #1 (`g_qtCs` readers) and #5 (lot grid, pending updates), which keep acting (D1); `InvokeHook` / `HostCallHook` = `managed` (still served). `[HeightCache]`'s ray stub stays on (D1). `lotfd`, `slotro`, `obstructro` already pass every thread but the job owner | `snCloneManaged`, `snCloneEvent`, `snCloneLazy`, `snCloneLiveLock`, `snWorkerDirty`, `snDirtyOver` |
| D2 handshake | `P4NrEnter`: publish in `g_snNrIn`, step back and wait while `SnLive()`; a reader-marked gate frame (`RG_REL_NR`) ends it at the call's return. At the 8 sim-side sites while P4 is installed: destroy `0x939420`, gates 14/15, `Save_WritePackage`, `BuildRoutingData`, lot save, both travel/quit ICs (the shadow runs inline, so there is no queue to empty; `rtClosing` refuses the next one as `epoch`) | `snNrWaits`, `snNrWaitUs`, `snNrArmWaitUs` |
| D3 | the #1 physics mutators take `g_qtCs` on every call while P4 is installed | (existing `vfRtQtWaitUs`) |
| D5 | seven families: physics mutations, destroys and lot index rebuilds (our stubs, `g_nrN`), `vfSwaps`, `snWev[0x0D..0x10]`, the blend leaf, the four lock-key IC slots. A family counts when it changed between the live entry and the end of the shadow and the plan read it (reader bits from the #1, #5 and `[HeightCache]` stubs in either run; destroys, events, blend and lock keys always). A mismatch with `nr` ≠ 0 is M5, natively and in the report | `snNrBits[7]` |
| Record (§13) | `ts3native-shadow.jsonl` next to the asi, one line per shadow (refused ones included), **v2** (adds `nr`); `ShadowWorld` gives `world`. `shadow-compare-report.ts` reads v1 and v2 and applies the `nr` rule | `snJsonl`, `snJsonlFail` |

### 18.2 Interfaces left for the clone (P3-B)

`struct P4CloneProvider { cut, acquire, release }`, registered with `P4SetCloneProvider(&provider)` (external linkage, callable from snap.cpp or a new file):

- `DWORD cut()`: sim thread, inside the async START before the job is queued (§12.4.1 step 1; design B: publish the ring sequence, §11.4 step 4). 0 = no version (`nover`).
- `int acquire(DWORD ver, P4Clone* out)`: worker, before each clone run: bring the clone to `ver` (apply records, rematerialise worker-dirty lots and, after a wrap, every lot), fill `world` (a routing world object in game layout, §12.8), `ver`, `epoch`, and `fresh = 1` when every node stamp was reset to 0 (P4 then restarts the per-copy stamp). Return -1, or a `P4_AB_*` reason (`epoch`, `mat`, `unsupported`).
- `void release(const P4Clone*, LONG flags, const u64* dirty, int nDirty)`: worker, after the run: `P4REL_WRAP` (the stamp wrapped: rematerialise every lot), `P4REL_DISCARD` (fault: throw the copy away), the lot keys the clone run wrote (`nDirty = -1`: every lot).

Not P4's: the epoch build of the clone, the record ring, the materialiser with its order and polygon-cache guarantees (§12.8 items 1-3, 5-6), and the steerro-test K3/K4/K5 sections that need a real copy.

### 18.3 Deviations from §12/§13 and what is not built

- **Boundary values.** The record's `mType` is derived at the `Route_PlanLocked` boundary for both answers (first path status through `0x752620`'s table, no path = 2, I-94-104); `o58` is `[]` on both sides (the PlanCommon tail fills it later); node `type` is -1; `tie` is `[]` (the docs/108 F-7 tie set is not captured, so a goal tie shows as M2 until part B adds it); `stale` is false (the per-lot mark sequence is P3-B's). `PortalStartPos` and the blocker records of §12.6 are not compared (not in the §13 format).
- **Written on the worker after the shadow**, not at the sim's collect (§13 said collect): no sim time. `gameUs` is the live job's `Route_PlanLocked` time on the worker.
- **Unused door answers** are counted (`snDoorUnused`), not classed M2 (no record field).
- **Private arguments** rest on I-94-100 to I-94-102 (options size, plain start/goal records, out-list tail words).
- **Not built:** the `Lot_dtor 0xAC1EB0` count stub of §17.6; the §12.2.3 uninstall at world close (the thunks are inert without a clone run); freeing the per-worker clip scratch at worker exit (the worker never exits); the "one plan of each kind" part of the cold check (the live job of the same kind always runs first, so only the guards are tested, I-94-105); options bits "not seen live" and the sailing-houseboat case as `unsupported`.
- **Counters v50 sit after a 128-int32 placeholder for P3-B's v49** (`p3bReserved`, `+0x2768..+0x2967`). When P3-B merges, its v49 replaces the placeholder; if it is not exactly 128 int32, v50 moves to its end (update the `static_assert` in `TestP4`, `agent.js` and docs/registry.md).

### 18.4 Checks

- `build.cmd` selftest `TestP4`: the v50 layout; T1-T4 on dummy code (refused on one differing byte; live values by default; the copy's values for the armed thread and the live ones for another thread at the same time; the word-only stamp add; the wrap to 1; T2' counted; T3 keeps eax, edx and CF; T1 inside a gate trampoline); the door stub chained behind `[ProbeValues]`' slot and on its own call site, pass-through, record, replay, a used-up entry as `door-miss`; D2 (a writer thread waits for the armed window and enters after the disarm; the arm gives up on a writer in flight; the `RG_REL_NR` undo); D5 bits; the whole shadow with a fake planner and a fake provider: refused `clone` with no provider, M0 with the door replayed and the private path released through a fake `0x866E90`, M1 (node y), M2 (goal index), M3 (status), M5 (a destroy in the window, `nr` 4), `door-miss`, busy (no record), `epoch`, `cold`, `unsupported`, tripwires, worker-dirty keys and overflow, and a fault in the clone run (P4 off, copy discarded, nothing armed afterwards). The 11 records it writes, read by `node tools/harness/shadow-compare-report.ts`, give the same classes (M0 1, M1 1, M2 1, M3 1, M4 6, M5 1).
- `shadow-compare-report.ts --self-check`: v2 `nr` cases and one line exactly as `P4Record` writes it.
- `steerro-test` seeds 1 and 2 x 3000: ok; new line "P4: 5 thunk sites, the door call and the list release prologue match" on the Steam 1.67 image.
- `node tools/hook-sites.ts --check`: 0 missing (docs/68 row added, site 8 row amended); `node tools/check-docs-code.ts`.

### 18.5 Risks

1. **Door replay key** (I-94-103): if any of the 9 arguments is a pointer into routing data, every clone-run query misses (`door-miss` on every plan that asks about a door). Read this first in part B, before the first shadow run.
2. **Private arguments** (I-94-100 to I-94-102): the options size and the out-list tail words are inferred; a wrong guess shows as M2/M3 noise or a fault (caught, P4 off).
3. **D2 waits**: destroys, saves and gates 14/15 wait for a running clone run (§17.4 cost note). The worker's arm waits at most 250 ms for writers already in flight; after the arm a clone run must never need a lock a waiting D2 writer holds. The only shared lock is `g_qtCs` (D3), which the sim takes in physics mutators; a D2 writer that finds itself inside one (it owns `g_qtCs`) does not wait and logs "did not wait" (first 20), so no deadlock, but that write is unguarded. None is expected: no destroy, save or gate 14/15 entry is known to run inside a mutator [inferred].
4. **D3 lock always**: one uncontended critical section per physics shape update while P4 is installed, also when no shadow runs.
5. **A fault leaves the game consistent only if the clone run touched nothing live**: the K4 guard page (with P3-B's copy) is the check.

### 18.6 Effort (hours, Claude with Rodney)

| Work | Hours |
|---|---|
| This build (p4.cpp, hooks in 10 modules, D1-D5, counters v50, report v2, selftest, steerro-test site checks, docs) | ~3 |
| Left of §12.9 and §17.7 for part B: plug in P3-B's provider, the I-94-103 and I-94-100..102 reads, the F-7 tie set, the `Lot_dtor` stub, K3 (G), K4, K5, the two shadow runs and their report, fix rounds | ~6-11 |

## 19. P5 prerequisites: door answers and mod guard (2026-10-18)

Static design only: no code, no game run. Question: what `Mode=2` (P5, our clone answer taken, §12.5) needs for portal/door answers, which no longer have a live run to replay, and which mod checks of docs/104 must pass before `Mode=2` is allowed. Inputs: §12.4-§12.6, §14 (door channel), docs/105 (§2-§5 rules, §9.3 shadow, §10.5 door mirror), docs/104 and docs/107 §2 (mod guard, `[TypePatch]`), docs/103 §7, docs/108 §7 (event 59: 4.30 / 8.78 per plan BP / IP), docs/115 (`invcache`), docs/60 §2 (async proxy latency), docs/116 (go/no-go, gap group J). Ledger rows in docs/51 §74 (I-94-110 to I-94-118). Hours are Claude with Rodney (CLAUDE.md).

### 19.1 Door answers in `Mode=2`

In `Mode=1` the stub at `0x64F4D3` replays the live job's recorded answers (§12.5). In `Mode=2` the clone run is the only run, before `J_DONE`, with no live `+0x918` held, so each `World_FirePortalLockedQuery 0x791C00` call needs an answer from somewhere. Three options:

| | (a) Native evaluator from the door mirror | (b) Proxy the managed callback to the sim thread | (c) Sync fallback for plans that ask |
|---|---|---|---|
| What | docs/105 §4.5: evaluate §2's rules natively from the door mirror (§14, docs/105 §10.5), the lot mirror and a per-plan Sim profile written by a `ScriptCore.Route` prologue; S-tier lots (privacy, turnstile, VIP door, apartment elevator) answered in the prologue or sent to "ask" | the stub calls `0x791C00` as the game does; the host delegate call is already proxied to the owner thread by the `0xD75FA0` detour (0.7.1) whenever an async job runs, and served at READY polls and blocking acquires (docs/60 §2) | the clone run aborts on its first event 59 ask (`J_SKIPPED`) and the collect runs the plan sync; only plans that ask nothing keep the clone answer |
| Build (hours) | evaluator 2-3, prologue + Sim profile + channel codes 1-1.5, S-tier answers 1 (or 0 by sending those lots to "ask"), I-105-12 shadow patch 1 + 1-2 live jobs; **plus the full docs/104 guard** (§19.2) 5-8 and mod probes ~2: **~11-16 h** + 2-4 live jobs | stub branch, tripwire exemption, counters, selftest: **~1-1.5 h**; no extra live job (the P5 daytime check and the G5 night measure it) | ~0.5 h |
| Coverage | all plans whose lots pass the guard; the rest fall to (b) per lot | **all `Mode=2` plans** | plans with 0 asks inside `Route_PlanLocked`: **not measured**. docs/108 gives only the mean (4.30 / 8.78). A Poisson spread with those means would leave 1.4% / 0.02% of plans with no ask; the real spread is lumpier (world legs and in-room plans ask 0, door-heavy house plans many), so the share is higher but unknown [inferred]. A plan cannot be classified before the run (asks are lazy), so every asking plan pays the clone run **and** the sync plan |
| Correctness risk | **medium**: a reimplementation of 12 decisions (venue hours, greeted lists, 8 `OtherSimCanIgnorePrivacy` overrides, turnstiles, VIP rooms). Inputs verified static (I-105-7), equivalence open (I-105-12); a miss is a visible door bug (Sims walk through locked doors or stand at open ones). Correct only together with the mod guard | **low**: the game's own chain (vanilla or modded) answers the same lazy asks the live search would make, on the sim thread, as A10 does today. Answers read managed state at serve time, not cut time: the same skew A10 already has [inferred] | none for answers (the game plans); high waste |
| Sim cost | removes the 121-151 dispatches/s (docs/115 §5) from the sim: a sim-thread gain of its own [inferred, ~0.5-2 ms/s] | keeps today's managed dispatches on the sim (same as A10 and sync) | worse than A10 for asking plans: worker work thrown away, then a sync plan on the sim |
| Latency | none added | each ask waits for the next serve point, up to about one sim tick [inferred, docs/60 §2]; with 4.3 / 8.8 asks per plan (fewer after `Portal_LockState`'s per-search cache) a plan can take several ticks longer. **Difference from A10:** A10's live job waits while holding live `+0x918`, which is what grows sim keyed waits (I-94-11); the `Mode=2` clone holds only the working copy's lock, so the wait delays that Sim's route only (I-94-111) | sync plan latency for asking plans |

Not chosen either: §3.5 option 2 (pre-answer every portal at START) is the eager table that docs/105 §4.7 rejected (20-100 dispatches per plan, more than today).

**Decision: (b) for P5.** It is the only option that covers every plan, keeps the game's own answer (mods included, §19.2), and costs about an hour. (c) is rejected as a mode: it cannot pick plans in advance and doubles the cost of every plan that asks; it survives only as the existing `J_SKIPPED` error path. **(a) is a later, separate step**, built only if G5 shows the door waits are what limits `Mode=2` (rule below), and always together with the docs/104 guard, which (a) needs and (b) does not.

**Rule for building (a)** (from the P5 counters, I-94-112): build it when, in the G5 in-load A/B, either the door wait is more than half of `Mode=2` job wall time (`snM2DoorWaitUs / snM2JobUs` > 0.5), or ticks slept per plan in `PlanAsync` at p95 exceed A10's by more than 1 tick. Otherwise (b) stays.

**What P5 builds for (b)** (ts3native, `[RouteSnap] Mode=2` only; `Mode=1` unchanged):

1. **Stub `0x64F4D3`, `Mode=2` branch.** When `t_snap.running` and the job is `Mode=2`: set a thread-local `t_snapDoorAsk`, call the original `0x791C00` (9 dwords, cdecl, answer in eax, I-94-68), clear the flag, return the answer. No record lookup, no `door-miss` abort. `Mode=1` keeps the replay.
2. **Tripwire exemption.** `InvokeHook` (`snCloneManaged`) and `World_FireEvent` in `routesnap.cpp` (`snCloneEvent`) accept a call from a clone run only while `t_snapDoorAsk` is set and the event is `0x3B`; any other managed call or event from a clone run stays a tripwire (M4 `managed`/`event`, the job `J_SKIPPED`).
3. **No new pump.** The `Mode=2` clone run is inside the async job before `J_DONE`, so A10's serve points already serve it (I-94-110). CANCEL or route destroy during a pending proxied ask takes A10's existing path for a live job.
4. **`LockedDoorsDuringPlan`** is written by the handler as it is served, as in A10; nothing at collect. A `Mode=2` job that asked and is then `J_SKIPPED` replans sync and may add the same doors again: the same as A10's async-to-sync fallback today (I-94-116).
5. **Counters** (reserve a counter version in docs/registry.md): `snM2DoorAsk` (sum), `snM2DoorHist[5]` per job (0, 1-2, 3-5, 6-10, 11+ asks), `snM2DoorWaitUs` (QPC around each proxied ask), `snM2JobUs`. The histogram also measures (c)'s coverage (I-94-113) for the record.
6. **Selftest:** in `TestSnapImages`, a fake clone run through the stub in both modes: `Mode=1` replays, `Mode=2` calls the original once per ask, an exempt ask raises no tripwire, a second managed call does.

### 19.2 Mod guard for `Mode=2`

docs/104 guards the **S tier**: managed code that runs inside event 59. It exists because the PAT variants and the native evaluator replace or batch that chain (docs/104 §2). Under (b) nothing replaces it: every ask the clone run makes goes to the game's own chain, lazily, in the order the search makes it. When the clone equals live, that ask set is the live set (G4's "door questions" row, §12.6), so a modded chain (GoHere's lot handler, a MonoPatcher `[TypePatch]` on `Lot.OnPortalPathPlan` or `Sim.OnPortalPathPlan`, a mod door class overriding `IsAllowedThrough`) runs exactly as it does under A10 today (I-94-114). NRaas overrides in the C and R tiers (GoHere `SimRoutingComponentEx.CreateRoute`, `RoutingComponentEx.DoRoute`) run on the sim before submit and after collect, also unchanged from A10 (docs/104 §3.4).

**Checks P5 must pass before `Mode=2` runs (option (b)):**

| # | Check | When | Trip action |
|---|---|---|---|
| M2-1 | Foreign native hooks inside the clone closure: every hooked site the clone run executes (`Route_PlanLocked` closure, the §12.2 patch sites, the §12.4.3 stub table) has our expected prologue or a site owner listed in the compiled docs/68 table (`hook-sites.ts`) | install (P4's existing "P4 not ready" check, now gating `Mode=2` too) | `Mode=2` off for the session, P4 shadow off, log line `RouteSnap: Mode=2 off: site <addr> owned by unknown hook`; plans run as A10 |
| M2-2 | Event 59 asks come only through the stub (tripwire, §19.1 item 2) | per clone run | that job `J_SKIPPED` (M4 `managed`/`event`), the plan runs sync; after 3 in a session, `Mode=2` off for the session (crash-cause rule: our design is wrong until shown otherwise) |
| M2-3 (log only) | docs/104 L1 (10 EA image GUIDs) and L2 (MonoPatcher ASI signatures: the `0xD7E560` detour and the `0xE59218` bytes, docs/107 §2.2) | load, world load | **no fallback**; one `ModGuard:` info line per finding so G5/G6 run files and player reports show the mod setup (~0.5 h) |

Nothing else from docs/104 is a P5 prerequisite: not L3 (method integrity), L4, L5, the census, P1 (event 59 delegate), P2 (lot handler lists) or O1-O5. They become prerequisites **of (a)**, not of `Mode=2`.

**Checks (a) needs, when it is built** (docs/104 §3, with three amendments):

1. L1, L2 (with the docs/107 §2.2 signatures added to `0xE5CA90`, `0xE5637D`, `0xE648DC`), L3 over the 550 S methods, L5, the census on `0xEA1E30`, P1 (event 59 delegate = one element, `LotManager::PortalLockedCallback`), P2 for each lot the evaluator answers for (chain = `Sim::OnPortalPathPlan`, then `PrivacySituation::OnPortalPathPlan` x n), O1 for census door classes, O2-O5 Sim side in the prologue.
2. **L3 checks the declaring class of each guarded method**, not the patched type, and resolves slots before mods load or by scanning `MonoClass+0xBC`, because a `[TypePatch]` on a subclass can swap the base's `MonoMethod` and overwrites `+0x1C` (docs/107 §2.1, I-107-5).
3. **L4's whitelist adds our own `Lot.OnPortalPathPlan` body patch** (`invcache`, docs/115, and the I-105-12 shadow hook in the same patch, docs/115 §4). `Lot.OnPortalPathPlan` is the dispatcher at the root of the S closure; docs/104 L4 lists only `hoursmemo`, so without this every vanilla install with `invcache` on would trip L3 (I-94-117).

**Trip action under (a): downgrade the answer source, never turn `Mode=2` off** (I-94-118). Session-level trips (L1, L2, L3, P1): evaluator off for the session, every ask proxied as in (b). Lot-level trips (P2, O1/O2 census hit on that lot): the evaluator returns "ask" for that lot's portals, which takes the (b) proxy. Sim-level trips (O3-O5): every ask of that plan takes the proxy. Only M2-1 and M2-2 turn `Mode=2` off or send a plan sync.

### 19.3 Effort and what changes in the plan

- **P5 door path (b) + guard M2-1 to M2-3: ~1.5-2 h** (stub branch, exemption, counters, selftest ~1-1.5 h; M2-3 log lines ~0.5 h; M2-1 is P4's check reused). This replaces docs/116 §4's "door path: proxy ~0 h / evaluator ~3-6 h" for P5; P5 becomes ~3.5-5 h + 1 clean night.
- **(a) evaluator + guard: ~11-16 h + 2-4 live jobs**, deferred behind the I-94-112 rule. In docs/116 gap group J, I-105-7/I-105-12 and I-104-2 to I-104-10 move from "P5 yes" to "(a) only"; the door-mirror rows (I-105-14 to I-105-32, I-105-40 to I-105-43) were already "evaluator path only".
- **Mod runs belong to G6 (release)**, not P5: one `Mode=2` job on a GoHere save with door settings and one with a MonoPatcher test mod (I-94-114), plus one with S3PP installed for M2-1 (I-94-115).
- This section is design only; nothing here is built or measured.

## 20. P6 release checklist (2026-10-18)

Draft queue `tools/harness/queues/snap-release.json` for gate G6 (§5), modelled on `queues/async-release.json`. **DRAFT, not run**: every job note says so. Nothing in it can run before P5 (`Mode=2`, §19) and the P6 widening are built. `node tools/harness/queue.ts tools/harness/queues/snap-release.json --dry-run` parses all 20 jobs; it flags `game-copy/ts3native.asi.snapp5` as missing on every job (the P5/P6 build, not built yet; copy the build there under that name).

### 20.1 Settings shared by every job

- ini: the A10 stack as `async-release.json` (`RouteAsync` Enabled=1 Mode=1, `RouteGate` Enabled/Sites/Enforce=1, `Discourage` Enabled=1) plus `RouteAsync.Kinds=7` (all three Replan-family ICs, P6's widening), `RouteSnap.Enabled=1`, `RouteSnap.Mode=2` and `RouteSnap.Kinds=7`.
- **Placeholders**: `RouteSnap.Mode=2` is the §19 name but P5 has not built it; `RouteSnap.Kinds` is an invented name for "which plan kinds take the clone answer" (P5 = `ReplanFromPosition` only, P6 = all; bits mirror `RouteAsync.Kinds`). Rename both to what P5/P6 define.
- `minCounters: 49` is a floor (v49 is the next free version, docs/registry.md); raise it to P5's reserved counter version so an older asi refuses.
- `-disablestoryprogression`, Spring baseline saves (as `async-release.json`).

### 20.2 Jobs and pass rules

**Pass rule for every job:** 0 crashes, 0 watchdog lines, 0 stuck Sims (logged only, per the stuck-Sims rule), 0 snapshot audit mismatches (`snap-audit-report.ts`), `Mode=2` still on at the end (no `RouteSnap: Mode=2 off` line), fewer than 3 M2-2 `J_SKIPPED` trips (§19.2). One failure of any job fails G6; a crash is our bug until shown otherwise (two clean runs to call it fixed).

| # | Job | Run | Extra pass rule | Expected min | Timeout |
|---|---|---|---|---|---|
| 1-3 | `bp-`, `sv-`, `ip-snap-matrix` | full save matrix (`matrix: []`, all cases + reload) | every case passes (save-change test matrix rule) | ~20 each | 60 each |
| 4-9 | `bp-snap-travel-france`, `-china`, `-egypt`, `-university`, `-future`, `-future-direct` | travel save case, as `travel-all.json` | travel case passes; audit 0 after each world load | ~15 each | 45 each |
| 10 | `bp-snap-soak` | soak 45 min, ultra | largest free block not falling; image memory under `CapMB` | ~55 | 70 |
| 11-12 | `bp-`, `ip-snap-rt-s2b-sweep` | profile 10 min, repro `rt-s2b-sweep` | (shared rules) | ~14 each | 30 each |
| 13 | `bp-snap-steer-crowd` | profile 5 min, repro `steer-crowd` | steering waits not above the A10 level | ~9 | 20 |
| 14 | `bp-snap-door-lock` | profile 5 min, repro `bp-door-lock` | locked-out Sims stop at the door, household passes, `snM2DoorAsk` > 0 | ~9 | 20 |
| 15 | `ip-snap-houseboat` | profile 5 min, repro `ip-houseboat-create` | (shared rules) | ~9 | 20 |
| 16 | `bp-snap-mod-gohere` | profile 10 min, repro `bp-door-lock`, save `Snap-GoHere-BP`, `fixes.install: nraas-gohere` | door behaviour as with `RouteSnap` off on the same save; no M2-2 trip from GoHere's lot handler (I-94-114) | ~14 | 30 |
| 17 | `bp-snap-mod-monopatcher` | profile 10 min, repro `bp-door-lock-typepatch`, `fixes.install: portalplan-typepatch-test` (pulls in `monopatcher`) | the `[TypePatch]` handler runs for clone-run asks (`PortalPlanCounts` calls and locked up between the first and last repro step); `ModGuard:` L2 line present; no M2-2 trip (I-94-114) | ~15 | 30 |
| 18 | `bp-snap-mod-s3pp` | profile 10 min, `fixes.install: s3pp` | M2-1: `Mode=2` stays on, or turns off naming a site owner listed in the docs/68 table, never "owned by unknown hook" (replaces the shared `Mode=2`-on rule) (I-94-115) | ~15 | 30 |
| 19-20 | `bp-`, `ip-snap-ab` | profile 10 min, seed 12345, `ab: routesnap`, slice 2 (warm-up, then ABBA) | **G5 no-regression**: it/s on ≥ off − 2%, sim keyed waits on ≤ off (`wait-ab.ts` per site), 0 new sim-thread waits; valid only with the machine load line ok | ~14 each | 30 each |

Jobs 19-20 are timing runs: **clean night only**. Jobs 1-18 are correctness runs and can go in the same night queue; run them in a daytime window only with `--only` and never mix their timings into G5.

### 20.3 Total queue time

- Expected: ~60 (matrix) + ~90 (travel) + ~55 (soak) + ~28 (sweeps) + ~27 (repros) + ~44 (mods) + ~28 (A/B) = **~330 min, ~5.5 h** unattended, within P6's ~5-6 h (§5). Per-job figures come from past runs: profile 5 min ≈ 8.5 min and 10 min ≈ 13.6 min a job (snap-probe2, 2026-10-18), save matrix 7-9 min of cases per world plus load and reload (2026-10-15 save-matrix runs); travel and soak are [inferred] from their timeouts and `async-release.json`.
- Worst case (every job hits its timeout): 790 min, ~13 h; the queue stops earlier on two crashes in a row.

### 20.4 What is still missing

| Missing | Blocks | Effort (hours, Claude with Rodney) |
|---|---|---|
| P5/P6 build: `Mode=2`, P6 widening, the real ini key names and counter version; copy to `game-copy/ts3native.asi.snapp5` | every job | P5 ~3.5-5 h (§19.3), P6 build ~1 h (§5) |
| `routesnap` runtime switch in `profile-world.ts` (`nativeNames` or `counterSwitches`); without it the toggler sends `routesnap` as a C# patch name | 19-20 | ~0.25 h, with P5 |
| GoHere (2026-10-18: `fixes.json` `nraas-gohere`, manual download; queue `queues/snap-gohere-save.json` job `make-snap-gohere-bp` loads `Baseline-BP-Spring` with GoHere installed and saves `Snap-GoHere-BP`, repro of the same name, UNVERIFIED). Left: put the `NRaasGoHere*.package` download in the installer's downloads folder, run that queue once, then set GoHere door filters on the home front door by hand in game and save over `Snap-GoHere-BP` (no agent command sets them; the dry-run does not check saves) | 16 | ~0.5 h (one 20 min job, then the door settings by hand) |
| MonoPatcher test mod (2026-10-18: `mods/portalplan-test`, `[TypePatch]` on `Lot.OnPortalPathPlan` with the game's behaviour plus counts `Ts3PortalPlanTest.PortalPlanCounts.Read`, `[Plugin]` start; built by `build-fixes.ts` into `build/ts3-portalplan-test.package` against `MonoPatcher.dll` from the `monopatcher` download, `fixes.json` `portalplan-typepatch-test`, repro `bp-door-lock-typepatch`). Builds; never run in game: the first job 17 run is also its first test (patch applied, counts rise, door lock still works) | 17 | ~0 h (runs with the queue) |
| Repros `steer-crowd` and `ip-houseboat-create` never run (UNVERIFIED in repros.json); verify each in a short run first (repro-actions rule) | 13, 15 | ~0.5 h + 2 short runs |
| Travel: none of the 2026-10-16 `travel-all` jobs (build be4267a) failed with "trip aborted" (that message came from the earlier `followup` job `bp-future-travel-2`, where autonomy replaced the queued portal Travel). China, Egypt and France made both trips with every save check passing; they failed only on the `fixes switched on` check because `cachedreflection` was not available in that game, which bbb56ca made an info line for the travel case. University arrived and waited 20 min with no active household in the housing pick (docs/77 §2.1; `uni_move_in` and the 90 s no-progress rule since 2710d87/bbb56ca). Future never left Bridgeport (in_travel set, Sims went on with autonomy) and its child was stopped from outside, so the queue showed no verdict. Since 2026-10-18 a trip failure ends with `at failure: world/dest/in_travel/household/state; queue ...` and a child with no verdict gets `FAIL: exit N with no verdict line`. 2026-10-18 rerun (`chain21`, 23:52 PHT): University arrived (`uni_move_in` into Henry Hall, away save clean), then `travel_home` hung 8 min with no active household and no switch, and the rule did not fire (it needed `in_travel`); cause: the agent sent the NPC dorm roommates along and never dropped the student out, so the van took the early-single-departure path (docs/77 §2.3). Fixed 2026-10-19: `travel_home` in University = the phone's "Leave University" (drop out, roommates stay), plus two stall rules (no household without a switch, 90 s; trip status unchanged 120 s). A rerun of `travel-all` on the current build is needed; G6 still needs the travel cases passing with the snapshot **off** first, or a failure cannot be blamed on it | 4-9 | ~1.5 h unattended (6 jobs, 45 min caps; vacation jobs ~5 min each) |
| `S3PP` is a manual download (`fixes.json` s3pp `source.kind: manual`); the archive must be in place for `install-fixes.ts` | 18 | ~0 h if already downloaded |

Writing this checklist: ~1 h (this section and the queue file). Running it: one clean night plus ~1-2 h of fix rounds (§5).

## 16. P3-B status: design B replay clone and lot audit (0.28.0, counters v49)

> **Changes 2026-10-18:** new section. Build only; the game was not run. Implements §11.4 (design B) as the `SpLotStrategy` "replay" in a new `replay.cpp`, plus three P3 fixes in `snap.cpp` from the first P3 live runs, and the load-time lot prebuild Rodney decided the same day. Ledger: docs/51 §70 (I-94-80 to I-94-89 and I-94-99; I-94-90 to I-94-98 are taken by §17-§19). Layout facts below come from capstone on the Steam 1.67 exe [verified static] unless marked.

**Switches:** `[RouteSnap] Enabled=1 Lots=replay` (default `Lots=none`), `ReplayAuditEvery=4`, `ReplayCapMB=128`, `Prebuild=0|1`. Audit only: no search reads the clone (P4).

### 16.1 What is built

| Part | What it does | Where |
|---|---|---|
| Clone (§11.4 step 1, §12.8) | private copy of the lot planner P (0x110 bytes) with its own footprint map M = `[P+0x24]` (buckets `+4`, bucket count `+8`, size `+0xC`, rehash policy `+0x10`, node pool `+0x1C`; 0x18-byte nodes key `+0`, FME `+8`, next `+0x10`), its own lot table (`P+0x4C` buckets, `P+0x50` count, 0x194-byte nodes chained at `+0x190`, lot graph at `+0x10`), every lot's Q (0x58 bytes: vtable `0xFED994`, owner P `+0x20`, root `+0x24`, refcount `+0x34`), tree nodes (inner 0x1C from pool `0x667990`, leaf 0x14 from pool `0x667920`; flags `+4`, refcount byte `+7`, leaf list `+8`: one inline 12-byte entry or a heap array \| 1 with count `+0xC`), FootprintMapEntries (0x14: vtable `0xFEAC7C`, ref `+4`, footprint `+8`, Q `+0xC`, level and flags `+0x10`; the map node holds the only reference) and footprints. Chain order, bucket count and leaf order are copied as they are; polygon caches are copied valid. Objects the game may free come from the game's pools and allocator (nodes, leaf arrays, FMEs, map nodes and buckets, polygon and vertex arrays); P, M, lot nodes, Q and footprint blocks are ours. Clone Qs and footprints carry a refcount bias so the game's release never deletes them. | `RpBuild` (world event 7, sim, `+0x918`) |
| Recorder (step 2) | in `SpNote` before P3's dirty lock (new strategy hook `pre`): W1 `ApplyInsert(Q, footprint, flag)` → INS record with a footprint copy made on the writing thread; W2 `ApplyRemove(Q, keyLo, keyHi)` → REM; W3 `QuadTreeBuild` / W4 `LotGraph_Build` → STALE (rebase that lot at the next cut). W1's own child recursion (return `0x665F87`, its only self call) is not recorded: the replayed parent inserts the children. W6 is not recorded (§11.6). | `RpPre` |
| Footprint copy | class table from the factory `0x62CD40` (sizes 0x40-0x4C); the box class `0xFEAE08` (0x4C, box floats at `+0x38..+0x44`) is copied flat; the others get `EnsureAllPolygons` (slot `+0x70` = `0x80E610`) on the live footprint first, every level must then be valid, level array copied, polygon (16-byte) and vertex (8-byte) arrays copied into `EA_AllocTagged 0x4E3F90` blocks with their `+0xC` words. Children (list `+0x24`, nodes {next, prev, footprint}) are copied into the parent's block. A refused copy (unknown class, a level still invalid, shape caps, fault) is counted (`rpCopyFail`); its INS forces a rebase of that lot instead. | `RpFpCopy` |
| Ring (step 2) | 65,536 records of 24 bytes, appended under a spin lock by any writing thread (the sim, the game's route job and our async worker all write under `+0x918`), one consumer. Full = records dropped, rebuild at the next cut. | `RpPush` |
| Cut (step 4) | in P3's `SpBuildVersion` (sim, `+0x918`): each lot's signature (node, built byte, level base, Q pointers, both transforms, portal list, add/remove sizes) against the sim's mirror of what the clone holds; changed, new and removed lots plus the forced (STALE / refused-copy) lots go out as one LOTS record: whole-lot copies, their map nodes, fresh copies of every footprint whose key was touched by a record on a Q the clone does not know, and the whole live map order. Then SYNC (map size, bucket count, lot count) and, every `ReplayAuditEvery` cuts, AUDIT (a hash of every live lot). Then the ring position is published. | `RpCut`, `RpRebase` |
| Consumer (step 5) | thread "snap replay": applies records up to the published position with the game's `ApplyInsert 0x665C50` / `ApplyRemove 0x666750` on the clone, holding `+0x918` through the game's `Lock_Acquire` (at most ~1 ms per hold). LOTS: drop the old lot's map nodes and trees, link the new lot, swap every clone FME from the old footprint copy to the fresh one, then re-chain the whole map in the live order with the live bucket count. SYNC: a difference sets a rebuild. AUDIT: the clone lots are hashed (outside the lock) and compared. Footprint blocks no FME holds are freed after each drain. | `RpDrain`, `RpApply` |
| Audit | pointer-free lot hash: key, built byte, level base, transforms, portal pointers, add/remove sizes, per Q its lot id and bounds fields and a pre-order walk of the tree (node flags without the refcount byte; per leaf entry its masks, FME level/flags, footprint vtable, id and `+0x34`). Same code on live and clone memory. | `RpLotHash` |
| Prebuild (`Prebuild=1`) | at world event 7, before the clone: for each lot in the planner's table that `LotTable_Contains 0x628A90` accepts and whose built byte is 0, `RoomGraph_FirstUse 0x63D2F0` (thiscall P, entry, outside room word `+0x16C`), the path `LotSearch_Entry` takes at `0x640E66` (→ `LotGraph_Build` → `LotRouting_FinishBuild 0x63CAF0` → `RestoreLotBlockedBits 0x82A190`). Counters `rpPrebuilt`, `rpPrebuildFail`, `rpPrebuildUs`, `rpPrebuildKB` (process private bytes added: the game's lot graphs). | `RpPrebuild` |

**Deviations from §11.4 and why:**

- **Rebase detection by signature compare** at every cut rather than by IC- or leaf-specific records only: the portal ICs, lot insert/remove, transforms and first-use builds all show in the signature; W3/W4 records only add a forced rebase (a replaced Q may reuse the old Q's address).
- **The consumer holds `+0x918`.** The tree node pools (`[0x11CF42C]`, `[0x11CF430]`, pop at `0x667949`, push at `0x668962` / `0x6679D0`) and the map node pool (`[M+0x1C]+0x10`, `0x624A10`, `0x6281A0`) are unlocked globals; only the routing lock serialises them, as for the game's own route job (docs/106 I-106-6). Applying under the lock is the game's own discipline; a private pool redirect (4-6 detours) is the alternative if the hold time shows up in gate waits. **Refuted 2026-10-18 (§16.6):** many sim writers, and one main-thread path, use these pools without `+0x918`, and `PathPlanner_Reset` frees the pools themselves. The private pools are required, not optional.
- **Footprint state is shared across lots.** `+0x68` (`0x80E0E0`) / `+0x6C` (`0x80E130`) count lot inserts in bits 26-30 of `fp+0x34` and transform the polygons by the lot matrix (`+0xF0` = `0x80EC30`, `entry+0xA0` / `+0xE0`) when the count goes 0 → 1 and back. A record the consumer skips (its Q is not in the clone yet) changes a footprint other lots share, so the cut sends fresh copies of the footprints with those keys, and a rebase swaps every clone FME to the fresh copy.
- **The map is re-chained at each rebase.** `ApplyRemove` removes `~key` entries, then `key` entries, in chain order, and the map insert `0x628220` puts a new node after the first node of its key; the rehash `0x6644E0` re-links nodes head first. Chain order and rehash timing therefore decide the order of removals within a Q, which decides leaf merges and final bits (a K1 seed failed on a final bit before the re-chain).
- **§8.5 steps 2-4 are not forced at the cut.** Records are taken at the apply leaves, so the live state and the clone are equal at the published position without them; P4 forces them for the search.
- **No P4 provider yet.** P4 part A's `P4CloneProvider` wants a routing world in game layout; this clone is the planner P only. The wrapper (a world copy with `+0x95C` = clone P and the P3 world images) and the version handshake are P4's (§16.5).

### 16.2 P3 fixes in this build (from the P3 live runs)

| Finding | Cause | Fix |
|---|---|---|
| `tune` 580/580 (BP) and 483/483 (IP) audits mismatch at `+0x2` | `W+0x8C` is `MinimumDistanceForCarTravel`: `IC_RouteManager_SetMinimumDistanceForCarTravel 0x74B500` → `0x82A590` writes `world+0x968` and `W+0x8C` in play; the G-build path copies `world+0x968/+0x970` to `W+0x8C/+0x90` at `0x829FE8` [verified static] | tune re-read at every cut (72 bytes), shared when equal |
| IP cut cost 4.28 ms/s, Gtiles rebuilt 24,517 times; "key map full 512" | the high-cost key map (`0x6733C0` keys for `0x670510`) never erased a key, so it filled and every remove of an unrecorded key marked every tile [inferred] | erase on `0x670510` (tombstones), 16,384 slots, probe 64; tiles 32 cells (a footprint box copies 10 KB, not 40) |
| steerro-test SNAP K1 seed 4 step 725: one `Gtiles` cell cleared outside its key's box | pre-existing (fails with both changes above reverted); seeds 1-2 pass | open, I-94-89 |

### 16.3 Checks [offline]

- `build.cmd` selftest `TestSnapReplay`: fake world of `replay-fake.h` (stand-ins for `ApplyInsert` / `ApplyRemove` with leaf splits and collapse, final bits cleared and never set back, entry order, map insert after the first equal key, rehash), random allocation pads; 4 seeds x 600 writes, 487 cuts, 4,123 lot audits, 530 rebased lots: clone == live at every cut, no rebuild needed; negative control (an insert with no record) found; consumer thread drain; world event 7 builds and publishes the clone; prebuild of the unbuilt lots the table accepts; W1's child recursion not recorded; memory back to 0. P3's tile rule test updated to 32-cell tiles.
- `steerro-test` section REPLAY on the mapped Steam exe: the game's own `ApplyInsert` / `ApplyRemove`, Q constructor `0x664650` + `SetBounds 0x6646F0`, footprints of six game classes, identity lot transforms; the node, FME and map-node pools and `EA_AllocTagged` / `EA_Free` pointed at random-pad allocators for the section. Seeds 1-2, 2-3 and 3-4 x 3000 writes: 1,479, 1,537 and 1,526 cuts, 13,430, 15,267 and 14,457 lot audits, about 1,190 rebased lots each, 0 mismatches, no rebuild; with a cut after every write and no pads (`RF_CUTALL`, `RF_NOPAD`) seeds 1-4 pass too (62,189 and 50,219 lot audits). The other sections pass (SNAP seed 4: §16.2).
- `node tools/hook-sites.ts --check`: 0 missing (docs/68 row added; no new hook site). `snap-audit-report.ts --self-check` (replay rows and the P3-B verdict), `queue.ts tools/harness/queues/snap-p3b.json --dry-run`, `node --check tools/probe/agent.js`.

### 16.4 Run

asi parked at `game-copy/ts3native.asi.snapp3b` (sha256 `3e43929053ef5da326f3d27610bcad8c1d4fa37ed3e9ee87cb0229917bd34dc8`). Audit only, no timing claim, daytime OK: `node tools/harness/queue.ts tools/harness/queues/snap-p3b.json` (jobs `bp-snap-p3b`, `ip-snap-p3b`: the `snap-p3` settings plus `Lots=replay`, `ReplayAuditEvery=4`, `Prebuild=1`; 10 min each, `minCounters` 49). Then `node tools/harness/snap-audit-report.ts probe-out/queue/snap-p3b-<date>-<job>-native.json --log <that job's ts3native.log>`: P3-B PASS = clone held, 0 lot mismatches in at least 200 lot audits, no divergence / fault / dropped record, footprint copies + rebases at most 1 ms/s of sim time, clone at most 128 MB; and G3 as in §14 (now with `tune` fixed and IP's tile cost).

**Cost [inferred, I-94-86]:** sim: one footprint copy per insert record (P1b measured 418 records/s mean on BP; a copy is ~1-5 µs with the forced build mostly a no-op) ~0.2-0.8 ms/s; rebases near 0 with `Prebuild=1`, each a live map walk plus the lot copy (1-3 ms) when they happen; live lot hashing at audits (all lots, ~1/s with `ReplayAuditEvery=4`) a few ms/s in the audit run only. Consumer: ~1-3 µs per record under `+0x918`, in holds of at most ~1 ms. Memory: footprint blocks ~150-400 bytes each over 100k-250k footprints plus tree and map nodes, ~20-60 MB [inferred], plus the prebuilt lot graphs (`rpPrebuildKB`); against docs/66 (largest free block 1.3-1.5 GB on BP with DXVK) this is small, the run logs both.

### 16.5 What P4 still needs

- A `P4CloneProvider` (P4 part A, branch `worktree-agent-a131459ff8a9ef04e`): `cut()` = publish and return the ring position; `acquire(ver)` = wait until the consumer has applied up to `ver`, then hand out a routing world in game layout: a copy of the world object with `+0x95C` = the clone's P, `+0x54` / `+0x8C` / grid / blend from the P3 version of the same cut, `+0x960` W; `release(dirty)` = rebase the lots the clone run wrote (or the whole clone on `P4REL_DISCARD`). Estimated 1.5-2.5 h.
- Private copies of what a search reads beyond this audit: footprint lock-key sets `fp+0x20` (docs/101 §1), the lot graph's room vector `+0x90` and lot-edge data, P's grid `+0xDC` and pending maps, so no clone pointer reaches live memory (K4).
- §8.5 steps 2-4 at the async START (first use of start/goal lots is already covered by `Prebuild=1` for lots in the table), `ApplyPendingUpdates`, deferred flushes.
- The world manager's active lot is read live by `LotTable_Contains` (`0xC88FD0`, I-94-87); a clone run must see the value of its cut.

**Effort (hours, Claude with Rodney):** this build ~6 h of session time (the game-layout reading, three replay bugs found by the offline checks: child footprint keys, shared footprint counters, map chain order). Left in P3-B: the two audit jobs (2 x 10 min) and their report; P4 as above.

### 16.6 Pool locking (I-106-6)

> **Changes 2026-10-18:** new subsection. Static only, on the Steam 1.67 exe (capstone over every `functions.csv` function, direct `E8` call graph, names from `tools/ghidra/symbols/ts3w-1.67.symbols.csv`). No game run, no code change. Ledger: docs/51 §84 (I-106-20 to I-106-29).

**Question.** §16.1 makes the "snap replay" consumer call `ApplyInsert 0x665C50` / `ApplyRemove 0x666750` on the clone while it holds `+0x918`. It assumes that every other user of the same pools also holds `+0x918` (I-106-6). **Verdict: refuted.** The pools have no lock. The routing lock covers only the `RoutingWorld_*` writers and the planner. Lazy lot builds, lot rebuilds and loads, the portal ICs and reader flushes all pop and push the same free lists without it, mostly on the sim thread and in one case on the main thread. `PathPlanner_Reset` also frees the pools outright, so even a perfect lock would leave the clone's nodes in freed memory.

**Pools the replay touches** (forward closure of `0x665C50` + `0x666750`, every direct call followed) [verified static]:

| Pool | Pop / push | Lock | Shared with the clone how |
|---|---|---|---|
| Leaf node pool `[0x11CF42C]` (0x14 bytes) | pop `0x667920` (only caller `0x627470`, the leaf constructor wrapper); push inline at `0x668962` in the leaf deleting destructor `0x668920` (vtable `0xFEE0B4`, reached only through the node release `jmp [vt]` when the refcount byte `+7` reaches 0) | none | the global itself |
| Inner node pool `[0x11CF430]` (0x1C bytes) | pop `0x667990` (only caller `0x628B70`); push `0x6679D0` (only caller the inner deleting destructor `0x628A40`, vtable `0xFEC0C0`) | none | the global itself |
| Footprint map node pool `[P+0x28]` (0x18 bytes) | map allocator `0x624A10` / `0x624AB0` with `this = M+0x1C` (`[this+0]` = the pool; other sizes go to `EA_AllocTagged`); node free `0x6281A0` uses `[M+0x1C]` | none | `RpBuild` copies M with `memcpy`, so clone `M+0x1C` holds the **live** planner's pool. `PathPlanner_Reset` → `0x62D710` creates this pool and its allocator `P+0x2C`, which M copies at `0x62D7AE` |
| FootprintMapEntry pool `[0x11581DC]` | `0x61ED80` (refill `0xC37710`), freed by `0x61ED10` | lock `0x11CEFA8` | safe |
| EA heap `[0x11CB864]` (leaf arrays, buckets, polygons) | `EA_AllocTagged 0x4E3F90` / `EA_Free 0x4E3F70` → `0x4E5920` | the heap's critical section (`[0xF9512C]` / `[0xF95130]` around `0x4E51B0`) | safe |
| Mask table `0x11CF180` (flag byte `0x11CF428`) | `0x667740`, read by `0x667BA0` / `0x668F20` | none | written once (lazy init), constant afterwards: benign [inferred] |

The pool refill `0x4E42F0` and carve `0x4E4290` take no lock either. `ApplyInsert` and `ApplyRemove` do not reach the pending maps `P+0x30` / `P+0x3C` (pools `[P+0x34]` / `[P+0x40]`) or the A\* and cell pools (`[0x11CEF50]`, `[0x11CEF68]`, `[0x11CEF90]`, `[0x11CF448]`).

**Lock coverage.** There are 30 `Lock_Acquire 0x4E16F0` sites whose `ecx` is built from `+0x918` within the 8 preceding instructions. A reverse walk from every pool pop, push and node-release function stops at those holders. 22 holders cover part of the closure: `Route_PlanLocked`, `UpdateObjectFootprintCore 0x8321F0`, `AddPrivateFootprint 0x831BA0`, `UpdateTerrainFootprints 0x831710` (lazy terrain footprints), `RemoveFootprintLocked 0x82A0C0`, `UpdateLotWalls 0x830940`, `UpdateLotFloorFootprints 0x8336F0`, `RemoveDiscourageFootprint 0x834860`, `LockedLotUpdate 0x82B450`, `RemoveLot 0x829D50`, `UpdateLotLocked 0x82C120`, `ObstacleQuery 0x82EDB0`, `GoalFootprintGather 0x82F0B0`, `PortalCheckLocked 0x82F470`, `RouteToObjectCheck 0x831530`, load/save lot data, `DestroyPlanners 0x8299E0`, `Clear 0x82D140`, `LockedOp_WorldInit 0x829410` and the debug frame update. The analysis works per function: a holder is assumed to make its calls inside the hold. The tree closure (327 functions) and the map closure (298) still reach **72 and 67 roots without a holder** on the path. These are the writer families below, all of which touch both node pools and the map pool.

| # | Unlocked writer family (entry roots) | Pool work | Thread | Rate |
|---|---|---|---|---|
| U1 | **Lazy lot build:** `RoomGraph_FirstUse 0x63D2F0` → `LotGraph_Build 0x63CB90` → `QuadTreeBuild 0x62F8A0` and `RemoveObjectFootprints 0x635700`. Reached from the four route ICs through `Route_GoalStep 0x75C2D0` → `0x63F490`, from `IC_World_GetConnectedRoomsImpl 0x7929F0`, `IC_World_FindPlacesOnShoreline 0x78C440` (`LotSearch_Entry 0x640E00`) and `0x655EE0` (`0x63D880`) | pops (build), pops and pushes (remove-all), map inserts and frees | sim | 0.12 (BP) / 0.18 (IP) lot builds/s (P1, §11.1). `Prebuild=1` removes most of them for lots in the table, but not for lots added later or not in the table (I-94-99) |
| U2 | **Lot rebuild and load:** `IC_RouteManager_OnExitBuildBuy` → `0x835240`; `RebuildLotRouting 0x835110` (bulldoze `0x7868E0`, houseboat create/delete `0x792CA0`/`0x788800`, `Lot_Save 0xACB8A0` via `0x8361B0`, `ImportLotContents 0x997540`) → `AddLotObjectPortals 0x832890` → `0x63A1C0`; `IC_World_LotSetEntranceByTargetPointImpl` → `0x63D720` → `FinishBuild 0x63CAF0`; `Lot_Load 0xACF520` → `Lot_FinishLoad` → `PathPlanner_AddFootprint 0x6376D0` | as U1 | sim | rare: build/buy exit, lot load or add, bulldoze, save |
| U3 | **Portal ICs:** `IC_RouteManager_Add{DivingBoard,WaterSlide,Platform,Stair}Portals 0x74B3B0`-`0x74B440` → `AddObjectPortals 0x82D2E0`; `AddPortal 0x757E10`, `AddPortalLocationVariation 0x74D3F0`, `RemovePortals 0x74D6B0`; native `DoorRouting_AddDoorPortals 0x80CB40` and `0x830250`. All of these go through `RemoveObjectFootprints 0x635700` → `LotQuadTree_Remove 0x666E30` → `ApplyRemove` | pushes (node merges), map node frees | sim | per portal object placed or loaded; not separated in P1 [open] |
| U4 | **Reader flushes:** `LotQuadTree_FlushDeferred 0x666A40` from `QueryPoint 0x667480` (`FootprintsNearPoint 0x631F10`, `FootprintsAtPointNoLock 0x631FD0`: `IsPointRoutable`, `FindGoodLocation*`, `TestObjectPlacement*`, `Get{Terrain,Floor}SurfaceType`, `GetInformationAboutRoom`, `CanAttachToSlot*`, `FindPlace(s)OnRoad*`, user tools, footsteps `0x86C000` → `0x8693E0`), from `QuerySegment 0x667400` (`BlockerCheck2d 0x82E340` from `IC_RouteManager_RoutingBlockerCheck2d` and **camera `0x970200`**, `PlaceGroupSocial 0x764910`), from `0x6672E0` (`SmoothenSubPathConnections 0x761950`) and from `Pin 0x666F70` | applies queued inserts and removes: pops, pushes, map inserts and frees. It works only when `Q+0x4C` is set, which happens when `LotQuadTree_Insert 0x666C50` or `Remove 0x666E30` ran while the pin count `Q+0x48` was above 0 | sim; **main** for the camera (I-86-5); footsteps sim [inferred: `vfRtGateOffSim` 0 in `bp-env-on`, and that path passes gate site 0] | unknown: P1b's "reader flush" frame (§15) counts it |
| U5 | **Pool teardown:** `PathPlanner_Reset 0x635B70` frees and recreates the leaf and inner pools (`0x667B70`, `0x6687F0`, then `0x667A90`, `0x667A30`), the A\*, goal and cell pools, and P's map pools (`0x62E290`, then `0x62D710`). Unlocked callers: `0x829C40` (when `world+0x97A` is set) from `Save_WritePackage 0xAAC110`, from the world manager (`0xC6B090` via `0xC6CC40`, and `0xC6BCC0` from `IC_World_BuildRoutingDataImpl`) and from `0x829CC9`; also `PathPlanner_dtor 0x637520` and `Setup 0x637650`. `DestroyPlanners 0x8299E0` and `WorldInit 0x829410` hold `+0x918` but free the pools all the same | `0x4E4250` hands every pool block back through the pool's free callback | sim (save, world manager) and world load/unload | rare, but **fatal**: the clone's nodes lie in the freed blocks, and `RpFreeClone` at the next epoch would push them onto the new pools |
| U6 | Roots with no direct caller (vtable or jump entries, or a mid-function `functions.csv` entry), lock not proven: `0x627100` (→ `Pin`), `0x639B40` / `0x63AFD0` (terrain footprint helpers → `0x637900` / `AddFootprint`), `0x63DBDC` (→ `RoomGraph_FirstUse`), the Q deleting destructor `0x666A20` → `LotQuadTree_dtor 0x666350` (a Q's last release: lot graph reset `0x62E420`, lot-table eviction `LotGraph_dtor 0x62B930` [inferred]) | pops and pushes | sim [inferred] | open |

The route worker (vanilla job, our async job) and the sim's footprint ICs and object events (`0x8321F0` family) are covered, which is why the static map of §9.5 looked safe for **routing data**. For **allocator state**, the unlocked readers count too, because a flush is a write.

**Why `[RouteGate]` does not close it.** `RgEnter` takes `+0x918` at a gate site only while `g_async.state == A_RUNNING` (our async route job). The consumer's hold is not a job. Gate sites also cover only U1 and part of U2 (`0x63D2F0`, `0x63CB90`, `0x63D7B0`, `0x63D860`, `0x835240`, `0x835110`, `0x62EAB0`), not U3, U4 or U5. Gating every root (about 70 entries, one on the main thread) to pause the consumer would cost more than taking the pools away from the game.

**Required change (before the P3-B audit jobs are trusted, and before P4):** give the clone private pools. **Built 2026-10-19: §16.7.** With those, the consumer no longer shares allocator state with any game thread.

1. **Map pool, no detour:** after the `memcpy` of M, set clone `M+0x1C` to our own pool object (built like `0x62D710` does: `0x4E43D0` with node size 0x18 and the `0x624590` / `0x6245E0` callbacks), and keep `+4..+0xC` as copied. `0x624A10`, `0x624AB0` and `0x6281A0` then use it on any thread.
2. **Tree pools, 4 sites, selected by a global owner tid** like the T-thunks of §12.2. Every clone operation runs under `+0x918` (`RpBuild`, rebase and batch on the sim; drain on the consumer), so one `g_rpOpTid`, set inside the hold, is enough. The sites: pop `0x667920` and `0x667990` (whole body), push `0x6679D0` (whole body) and the inline push at `0x668962` (call-site redirect). Our own direct pops (`g_rpLeafPool` / `g_rpInnerPool` in `RpBuild` and the rebase) call the private pools directly. docs/68 rows and `hook-sites.ts --check` are needed for the 4 sites.
3. **Teardown (U5):** with private pools the clone's memory is no longer in the game's blocks, so `PathPlanner_Reset` cannot free it. Still treat any change of the live `[P+0x28]`, `[0x11CF42C]` or `[0x11CF430]` against the values seen at the clone's build as an epoch, checked at each cut and drain. The clone's P copy still points at the old live P's pending maps and lot graphs.
4. **Keep the `+0x918` hold** for now. It still serialises the consumer with the sim's cut and builder, which also write the clone, but its comment must stop saying "pools".
5. **Count it:** a pop or push on the live pools from the consumer thread while `g_rpOpTid` is set is a bug counter (`rpLivePool`), and a P3-B PASS needs it at 0.

Without the change, a U1-U4 write on the sim during a drain can corrupt a free list: the same node goes to two owners, or a node is lost. That shows up later as a crash in an unrelated quadtree or footprint map, not as an audit mismatch. U5 is a use-after-free at the first save or world change after the clone exists. I-94-81's live check (`rpFaults` 0, no crash) would read a race-free run as a pass. It cannot prove the pools safe.

**P4 note (the original I-106-6).** The A\* node, goal node and cell pools (`0x61AC90` / `0x61AD00`, `0x61AE80` / `0x61B520`, `0x61EB90` / `0x61EBD0`, `0x66BB50` / `0x66BF60`) have the same unlocked writers: U1, U2 and U5 run room-to-room A\* inside `LotGraph_Build` (`InRoomPlan`, `InRoomPlanToPoint`, `RoomToRoomAStar`; 20 roots, of which only `Route_PlanLocked` and `SaveLotData` hold `+0x918`). A P4 clone search on the worker must take these from per-job pools too, through the same tid selector.

**Effort (hours, Claude with Rodney):** private map pool ~0.5 h. Four tree pool redirects with selftest, steerro-test REPLAY on the redirected pools, docs/68 and the hook check ~2-3 h. The epoch check and counter ~0.5 h. P4's A\* and cell pool redirects (6 sites) ~2 h when P4 starts.

### 16.7 Private pools built (counters v53)

> **Decision 2026-10-19 (Rodney):** one worker keeps one 96 MB region (a reservation, committed only as used; the most used live so far is 19.9 MB, 0.30.1 BP). When P4/P5 gets 2-3 route workers, each worker gets at least 64 MB: 128-288 MB reserved against a live largest free block of ~1.25-1.5 GB (docs/66).

> **Changes 2026-10-19:** new subsection. Build only; the game was not run. Implements §16.6 steps 1-5 and the P4 note in a new `native/ts3native/pools.cpp`, with small changes in `replay.cpp`. Ledger: docs/51 §87 (I-106-40 to I-106-47). Reuses the design of the earlier P4-B WIP commit `8f4d437` (never merged), with its own owner switch for P4 jobs and the counters as their own block.

**What is built** [verified static for the site bytes, offline tests for the rest]:

| Part | What it does | Where |
|---|---|---|
| Sites (step 2, P4 note) | every pool's pop and push reads the pool pointer with one `mov eax, [global]` (`A1 imm32`, 5 bytes), so each becomes a `call` to a stub that returns the pool in eax: leaf `0x11CF42C` pop `0x667920`, push `0x668962` (inline in `0x668920`); inner `0x11CF430` pop `0x667990`, push `0x6679D8` (in `0x6679D0`); A\* `0x11CEF50` `0x61AC90` / `0x61AD13`; goal `0x11CEF68` `0x61AE80` / `0x61B533`; cell `0x11CEF90` `0x61EB90` / `0x61EBE5`; quadtree cell `0x11CF448` `0x66BB50` / `0x66BFAE`. Push sites hold the node in `esi`, except `0x6679D8` (`ecx`). The next instruction at every site writes the flags; the stubs change only eax. The bytes of a group are checked first, all or none; a mismatch logs the bytes and patches nothing. | `PlPatch` |
| Selection | pop: private when the thread id (TEB `fs:[24h]`) is the replay op thread `g_rpOpTid` (tree pools only) or the P4 job thread `g_plJobTid` (every pool), else the game's (one TEB read, two compares). push: by node address: a node in our region goes to the private pool on any thread, else to the game's. | `PlPop<i>`, `PlPush<i>` |
| Private pools | game-layout pool objects (size, alignment and nodes per block copied from the live pool object, or the known size 0x14 / 0x1C / 0x18 when it is not there yet), blocks from one reserved region of 96 MB (smaller when the address space has no such block), committed in 64 KB steps, never handed back (the free lists recycle it). The game's own refill `0x4E42F0` works on them through the block callback. | `PlPrivate`, `PlBlockAlloc` |
| Map pool (step 1) | after the `memcpy` of M, clone `M+0x1C` (pool pointer at `+0` of the inline allocator object) = the private map pool; `+4` (bucket sub-allocator, the locked EA heap) stays as copied. `0x624A10`, `0x624AB0` and `0x6281A0` then use it on any thread, no detour. | `RpBuild` |
| Direct pops (step 3 of the brief) | `RpTree` (clone build and lot rebase) takes inner and leaf nodes from the private pools with a C copy of the game's pop and refill (`PlAlloc`), not through the game's pop. | `RpTree` |
| Owner switch | `g_rpOpTid` is set inside the `+0x918` hold: the whole clone build and its push (`RpEpoch`), the rebase and its push, each consumer hold, the consumer's switch-off drop. | `RpOpEnter` / `RpOpLeave` |
| Epoch (§16.6 step 3) | each clone keeps the live `[0x11CF42C]`, `[0x11CF430]` and `[P+0x28]` of its build. The cut compares them with the live values: a change counts `plEpochs` and rebuilds the clone. The drain compares them with its clone at every hold: a change drops that clone at once (private memory only, so freeing it after `PathPlanner_Reset` is safe) and skips records until the cut's new EPOCH arrives. | `RpCut`, `RpDrainBody` |
| P4 jobs (P4 note) | `PoolsEnterJob()` / `PoolsLeaveJob()`: the worker's pops of all six pools are private between them. `PlInstall` sets p4.cpp's `g_p4PoolsEnterJob` / `g_p4PoolsLeaveJob` (called around every clone run) when the A\* family sites are in place; else they stay NULL and the run keeps the game's A\* pools as before. The replay op and a P4 job share one private pool set; p4clone's `g_rpUse` lock already keeps the consumer's drain out of a clone run. | `pools.cpp` |
| Counters (§16.6 step 5) | block v53 (8 int32 at `0x2AB4` in this build; after design G's v52 when that merges first): `plReady`, `rpLivePool` (an owner thread popped or pushed a game pool), `plForeign` (a thread that owns nothing pushed a private node), `plOverlap`, `plEpochs`, `plKB`, `plFull`, `plJobs`. `snap-audit-report.ts` has a "private pools" gate: PASS needs the sites in place and `rpLivePool`, `plForeign`, `plOverlap` at 0; a pre-v53 asi is OPEN. | `ts3native.cpp`, agent.js |

Install: `[RouteSnap] Lots=replay` runs `PlInstall` before the consumer thread starts; without the tree sites the replay is skipped (`SKIP route snapshot replay: no private pools`). The `+0x918` hold stays (§16.6 step 4): it still serialises the consumer with the sim's cut and builder.

**Found on the way (fixed):** a rebased lot was linked at its bucket head in the clone's lot table, while the live table may hold it further down the same chain. The audit hashes lots one by one and missed it; P4's end-to-end selftest (a fake planner that walks the table in order) caught it once the pools changed the random stream of the fake world. The rebase batch now carries the live lot order of the cut and the consumer re-links its table to it (`RpBatch::lotKeys`). Shortcut: a live lot table rehash (bucket count change) is still not followed. The same stream change made the P4 M1 selftest insert a no-op (that footprint was already in the Q); it now inserts with flag 1.

**Checks [offline]:**

- selftest `TestPools`: the 12 stubs on dummy code shaped like the sites (one differing byte: nothing patched); pops by thread (no owner: the game's; replay op thread: tree pools private, A\* family the game's; P4 job: all private; another thread while this one holds a switch: the game's); pushes by address; `rpLivePool`, `plForeign`, `plOverlap` counted; `PoolsEnterJob` refused without the sites; map pool; a pool-object change detected for each of the three values. Race: two threads, 200,000 pops and pushes each, one on a game-layout "game" pool, one as the op thread on the private pool, each node marked by its thread: 0 foreign nodes, free lists without loops, every node back in its own pool. Epoch: the replay on its fake world, the game's leaf pool object replaced between cuts: `plEpochs` +1, one rebuild, the drain drops the old clone, 150 more steps with audits at 0 mismatches, every clone tree and map node private and every live one the game's.
- selftest `TestSnapReplay` and the P4-B end to end now run on private pools (the stand-ins call the stubs as the patched sites would). `selftest ok`.
- steerro-test REPLAY K1 on the mapped Steam exe, seeds 1-2 x 3000: the FootprintMapEntry pool and general new / free still on random-pad allocators; the tree and map node pools now run the game's own pool code on real pool objects, with all 12 sites patched in the image. 1,471 cuts, 11,980 lot audits, 1,224 rebased lots, clone == live at every cut; 19,460 tree and map nodes checked (clone private, live the game's); `rpLivePool` 0, `plForeign` 0, region 320 KB. The other sections pass.
- `node tools/hook-sites.ts --check`: 0 missing (docs/68 row). `snap-audit-report.ts --self-check`, `node --check tools/probe/agent.js`.

**Not checked:** a live game run (the P3-B audit jobs and the P4 shadow runs must be redone on this asi); whether a P4 clone run frees A\* nodes on another thread after `PoolsLeaveJob` (`plForeign` shows it); how much of the 96 MB the clone uses on BP / IP (`plKB`).

**Effort (hours, Claude with Rodney):** ~5 h of session time (port of the WIP onto the merged P4-B code, fakes moved onto pool objects, the lot table order bug, tests, docs). Left: the P3-B audit jobs and the P4 shadow jobs on this asi (10 min each).

### 16.8 P3-B on the pools build: middle class 0x1020AE8 unknown (0.29.1)

> **Changes 2026-10-19:** new subsection. Live result of `bp-snap-p3b` on ts3native 0.29.0 (counters v53), cause, fix built (0.29.1, counters unchanged). Ledger: docs/51 §90 (I-94-148 to I-94-151). The fix was not run in game.

**Live result (BP, 10 min, 2026-10-18 20:44 run; the 14:50 run on the pre-pools build failed the same way)** [verified, `snap-audit-report.ts` on `snap-p3b-2026-10-18-bp-snap-p3b-native.json`]: the replay clone never built (60 builds failed, max 163.1 ms, 0 lot audits); the log names `footprint class 0x01020AE8 unknown` and 8 `clone build failed` lines; sim side 3,774,454 refused footprint copies (1.34 ms/s). P3-B: FAIL (clone none, sim cost 1.34 ms/s). G3 on the same run: 0 mismatches in 40,014 image audits, but cut cost 10.77 ms/s (gate 2).

**Cause [verified, disassembly of the Steam 1.67 exe].** `0x1020AE8` is the vtable the middle constructor `0x80EED0` stores; two callers keep it, because no derived constructor runs after it: `0x63A1C0` (allocator tag "Routing/LotEdgeFootprint", every lot's edge) and the footprint factory `0x62CD40` (tag "Routing/PolygonFootprint"). Both allocate 0x40 bytes through `0x4E3F90` and pass 1 level. Layout: the base fields up to `+0x37`, the level array at `+0x38`, the level count at `+0x3C`; slot 0 is `0x619EF0` (returns 1), slot `+0x70` is `Footprint_EnsureAllPolygons 0x80E610`. So it is the same shape as `0xFEB5F8` / `0xFEB720` (0x40 bytes, one cached level). The copier's class table in `replay.cpp` listed the 8 derived classes, the box and the child class, but not the middle class itself. Every lot holds a lot-edge footprint, so more than 64 lots were left out (or a refusal met only in a tree leaf) and each build failed as a whole.

**Why the cut cost was 10.8 ms/s [verified from the counters].** The cut runs the replay's begin (`RpCut`) inside `spCutUs`. Of 6.46 s in cuts, 4.81 s were the 60 failed clone builds (`rpEpochUs`, ~80 ms each). The 10 s back-off of 0.29.0 was active (60 builds in 600 s; 0.28.0 had 1,978). Each build refused the same footprints again for every FME and leaf entry that held them: 3.77 M refusals / 60 builds = ~63,000 per build. Without the builds the cut is 0.80 ms (2.75 ms/s at 3.42 cuts/s), against 0.50 ms in `bp-snap-p3` (v47). The difference follows the tile rebuilds: 19,413 Gtiles rebuilt (9.5 per cut) against 7,756 (3.3 per cut), both with 64-cell tiles; bnm and grid are rebuilt at every cut in both runs [inferred: more high-cost polygon writes in this session, not a code change; `spKeyMapFull` was 0].

**Fix (0.29.1)** [offline]:

| Part | Change |
|---|---|
| Class table | `{0x1020AE8, 0x40, cache, not child}` added to the copier's table; the copy forces the cache with slot `+0x70` and copies its level array as for the other cached classes |
| Refused once | a build or rebase batch remembers the live footprints whose copy it refused (`RpCtx::bad`): one refusal per object per build, not one per FME or leaf entry |
| Back-off | the wait after a failed build doubles on the same world: 10, 20, 40, 80, 160 s, reset by a good build or another world |

**Checks [offline]:** selftest: the unknown-class test now puts the odd footprint in three Qs of one lot and asserts one refusal for the build; the game table must hold `0x1020AE8` as 0x40 bytes with a cache. steerro-test REPLAY K1 (game code on the mapped exe, including `0x80E610` on the copies) now builds middle-class footprints too: seeds 1-2 and 2-3 x 3000, 1,536 / 1,572 cuts, 12,246 / 11,953 lot audits, clone == live at every cut, no copy refused, `rpExclLots` 0, 10 / 7 middle-class blocks in the final clones. `node tools/hook-sites.ts --check` (0 not in docs/68), `snap-audit-report.ts --self-check`.

**Expected on the next run [inferred]:** the clone builds once per epoch (one build of ~80-160 ms at load instead of one every 10 s); BP cut cost about 2.75 ms/s plus the replay's own cut work (rebases, SYNC, audits), so G3's 2 ms/s cut gate likely still fails on tile rebuilds alone. Options if it does: rebuild dirty tiles lazily (only at armed P4 cuts, §21.5), or a longer `CutMs`. P3-B's sim cost gate (copies + rebases <= 1 ms/s) is then measured for the first time.

**Effort (hours, Claude with Rodney):** ~1.5 h (static check, fix, tests, docs). Left: rerun `bp-snap-p3b` (10 min) on 0.29.1, then IP.


**IP on 0.29.0 (`ip-snap-p3b`, 2026-10-19 04:56 PHT, queue PASS 11.9 min):** same shape as BP. Clone builds 0 (59 failed, max 150 ms), 0 lot audits, sim cost 1.56 ms/s, G3 image audits 0 mismatches in 34,008 but cut cost 11.98 ms/s (1,742 cuts, 74 skipped for a busy async job). Private pools gate PASS in both worlds (sites in place, `rpLivePool`, `plForeign`, `plOverlap` 0, region 1.3 / 2.7 MB). Reruns of BP and IP on 0.29.1 are queued (chain30, `game-copy/ts3native.asi.snapp3b291`), then snap-p4 on the same build; the night snap-p4 run on 0.29.0 has no clone to plan on.

### 16.9 P3-B rerun on 0.29.1: builds still fail, cause now logged (0.29.2)

> **Changes 2026-10-19:** new subsection. Live rerun of `bp-snap-p3b` on 0.29.1 (log of 2026-10-19 05:45), what the counters rule out, the 0.29.2 build (exact failure reasons, leaf-only and footprint-less FMEs), offline tests. Ledger: docs/51 §92 (I-94-152 to I-94-155). 0.29.2 was not run in game; counters unchanged (v53).

**Live result (BP, 10 min)** [verified, report and native dump]: the class fix of §16.8 works: 0 refused copies, `rpUnknownVt` 0, footprint copies 0.225 ms/s (P3-B sim cost PASS). The clone still never built: 6 failed builds in the window (8 in the session), max 162.6 ms, `rpFaults` 0, and the log line of 0.29.1 named no cause ("class 0x00000000"). G3: 0 mismatches in 39,780 image audits, cut cost 3.93 ms/s over 2,037 cuts. Prebuild 36 lot graphs (82 lots in the table), pools gate PASS.

**Cut cost split** [verified, counter deltas]: 2.36 s in cuts, of which 0.66 s were the 6 failed builds inside the cut (1.10 ms/s); the rest is 1.70 s = 0.83 ms per cut (2.83 ms/s), with 20,173 Gtiles rebuilt (9.9 per cut). So even with no failed build, G3's 2 ms/s gate fails on the tiles alone in this session (§16.8, §21.5 for the options).

**What the counters rule out** [verified]: not a fault in the build (`rpFaults` 0; the build's own exception handler counts it), not a refused footprint copy (`rpCopyFail` 0), not the 1,024-lot or 128 MB caps (82 lots, 22 MB), not the private pools (`rpLivePool` 0, `plFull` 0). The builds ran for about 110 ms each, so they got past the lot shells and the map walk. What is left: an allocation, a shape check (lot with more than 16 Qs, a one-bucket map, tree depth, leaf list length), more than 64 lots left out, or a lot left out from a tree leaf only, which 0.29.1 turned into a failed build.

**Most likely cause [inferred, I-94-152].** An FME whose footprint pointer `+8` is 0. The copier looked the footprint up in its live-to-copy tables with key 0, and those tables use 0 for an empty slot, so the lookup "found" the empty slot and returned no copy, without counting a refused copy or a fault. In the map walk that left the lot out; in a tree leaf only (an FME that no map node holds), it failed the whole build. A leaf-only FME whose footprint copy is refused fails the build the same way (that one would show in `rpCopyFail`, which was 0 here). The fake world and steerro-test K1 never make such FMEs. Whether the live game has them is not known: the 0.29.2 log answers it.

**0.29.2** [offline]:

| Part | Change |
|---|---|
| Failure reason | the build records its first cause (21 codes: fault with the exception code, allocation site, no private map pool, lot table walk, more than 16 Qs, room vectors, one-bucket map, more than 64 lots left out, leaf-only refusal, tree depth, leaf list length, FME on another lot's Q, and for a refused copy: unknown class, level count or cache pointer, level not valid after the forced build, polygon or vertex vector shape, child list, child alone, child not in its parent's list) with the lot id (from the Q), footprint address, class and two values. Log: `snap replay: clone build failed: <cause> (lot ..., footprint ..., class ..., a .., b ..)` for the first 8 failures and whenever the cause changes, plus `first lot left out: <cause> ...` for left-out lots. No new counters. |
| Footprint-less FME | copied as it is (`+8` = 0), no lot left out; the table lookup is never made with key 0 |
| Leaf-only FMEs | before the lots are filled, every FME in the live trees gets its footprint copy (`RpPreTree`), so a refusal met only in a leaf leaves its lot out like one met in the map, instead of failing the build |
| Refused copies | the per-build refused table keeps the reason per footprint, so a repeat reports the same cause |

**Checks [offline]:** selftest: (a) a leaf-only FME on lot 1 whose footprint has an unknown class: the build succeeds with that lot left out, the cause is "unknown footprint class" with lot 1's id and the class; 100 random steps after it, audits equal (on 0.29.1 this build fails); (b) a leaf-only FME without a footprint on lot 2: the clone builds with every lot, the next cut audits every lot equal. steerro-test K1 seeds 1-2 and 2-3 x 3000: as in §16.8 (1,536 / 1,572 cuts, 12,246 / 11,953 audits, no copy refused). `hook-sites.ts --check` (0 not in docs/68), `snap-audit-report.ts --self-check`, `selftest ok`.

**Next:** rerun `bp-snap-p3b` on 0.29.2 (10 min). If the clone builds, P3-B gets its first lot audits; if not, the log names the cause, lot and footprint. Cut cost stays about 2.8 ms/s from tiles until the lazy tile rebuild of §21.5 is built.

**Effort (hours, Claude with Rodney):** ~1.5 h (counter reading, static candidates, diagnostics, two fixes, tests, docs). Left: one 10-min run.

### 16.10 P3-B on 0.29.2: children outside their parent's list, FMEs without a Q (0.29.3)

> **Changes 2026-10-19:** new subsection. The 0.29.2 rerun (log of 2026-10-19 06:09) named both causes. This section verifies them in the exe and describes the 0.29.3 model fix and its offline tests. Ledger: docs/51 §94 (I-94-162 to I-94-166). Built into ts3native 0.30.1 together with H20 (counters v54, §21.8); not run in game.

**Live result (BP, 10 min)** [verified, log]: still no clone (6 failed builds, max 188.5 ms, 0 lot audits); sim cost 0.24 ms/s (PASS), cut cost 3.81 ms/s, pools PASS. Causes logged: 8x `first lot left out: child not in its parent's list` (class `0xFEBF90`), 6x `clone build failed: FME of a rebase leaf on another lot's Q (lot 0, ...)`, 2x `more than 64 lots left out (a 209)`.

**Cause 1, FMEs without a Q [verified disassembly + code].** `0x6376D0` (the hanging-footprint path for a footprint with no lot, lot id 0) creates an FME at `0x637809`, sets its Q `+0xC` to 0 at `0x637830` and puts it in the planner's map. So map M holds FMEs whose Q is 0. The copier looked the Q up in its live-to-clone Q table with key 0. That table uses 0 for an empty slot, so the lookup "found" an empty slot with value 0, and the copy got Q 0. The check meant for "a rebase leaf on another lot's Q" fired and failed the build. This is the same key-0 slip as the footprint-less FME of §16.9; that FME shape is still unproven live.

**Cause 2, child footprints outside their parent's list [verified disassembly; how many objects take each path is inferred].** The 0x28-byte class `0xFEBF90` ("Routing/ExtensionFootprint") is made in two ways:
- by `0x807400` on a parent, which sets `+0xC` = the parent and links the child into the parent's `+0x24` list (`0x8075AB..0x8075B8`);
- alone, by `0x6261F0` from the type factory at `0x62CD1B` (the load path), with `+0xC` = 0.

A loaded child gets its parent later, in its own post-load fix-up `0x806ED0` (slot `+0xA4`, step 2), which sets `+0xC` from an id lookup (`0x628C50`). The parent's list is filled in a separate step: `0x807990` (base slot `+0xA4`, step 1) looks up child ids and links whatever it finds via `0x8073A0`. Nothing makes the two agree: a child's `+0xC` parent need not list that child. All 22 of the child's slots that use `+0xC` forward to the parent without a null check (slot 0 `0x626210`). So the game treats `+0xC` as the owner, and the list is only the set of children that the parent's insert recurses into. The 209 left-out lots of one build show that this is systematic, not rare. A likely source is lots loaded from the save, but that is inferred.

**Fix (0.29.3)** [offline]:

| Part | Change |
|---|---|
| Table keys | the copier's open-address tables never store or look up keys 0 and 1 (empty and erased slots) |
| FME without a Q | copied with Q 0, as live (counted in the `clone built` log line) |
| Child outside its parent's list | copied in a block of its own: flat 0x28 bytes, `+0xC` = the parent's clone copy, which it pins (`+8` + 1, so the GC and the record path never free a parent that a child still forwards to); `+0xC` = 0 stays 0. Freeing the child block unpins, except in whole-clone and whole-batch frees (a per-thread flag, so no write goes into a parent freed first). When a rebase batch replaces a parent, the clone's own-block children are re-pointed to the fresh parent and the pin moves with them, so the old parent block is freed, not leaked. |
| Diagnostics | `RW_NOPARENT` is now only "the `+0xC` parent is itself a child" (a = 1, b = its class); the build log adds the counts of own-block children and Q-less FMEs |

**Checks [offline]:** selftest (c) and (d), in one world:
- a child-class footprint whose `+0xC` parent does not list it, inserted on its own into lot 1;
- a map FME without a Q.

The clone builds with every lot, one own-block child and one Q-less FME, and the map counts are equal. The child copy's `+0xC` is the parent's copy, with the parent pinned. After that, 200 random steps (rebases, GC, re-inserts) and cuts run with audits equal, and memory and `rpFps` are back to 0 at teardown. A first try without the re-pointing leaked a parent block, which this test caught.

Earlier tests still pass: (a)/(b) of §16.9, middle class, unknown class. steerro-test K1 seeds 1-2 and 2-3 x 3000 give the same numbers as §16.8. Also `hook-sites.ts --check` (0 not in docs/68), `snap-audit-report.ts --self-check` and `selftest ok`.

**Not covered:** whether a live parent can be freed while a child that is not in its list still forwards to it. The clone pins its copy, so the clone stays safe either way. Whether the next run finds a further shape: the reason log of §16.9 stays on.

**Cut cost:** unchanged from §16.9 (3.81 ms/s with 6 failed builds; about 2.8 ms/s of it is tile rebuilds).

**Effort (hours, Claude with Rodney):** ~1.5 h (static reads, model fix, pin handling, tests, docs). Left: rerun `bp-snap-p3b` on 0.29.3 (10 min).

### 16.11 P3-B on 0.30.1: the clone builds, but rebuilds every 1.2 s (0.30.2)

> **Changes 2026-10-19:** new subsection. Live run of `bp-snap-p3b` on 0.30.1 (log of 2026-10-19 06:35), the causes of the rebuilds and of the cost, the 0.30.2 fixes and their offline tests. Ledger: docs/51 §95 (I-94-167 to I-94-172). 0.30.2 was not run in game; counters unchanged (v54); new ini key `ReplayAuditLots`.

**Live result (BP, 10 min)** [verified, report + log]:
- **Clone:** built, 82 lots, 74,406 footprint blocks, max 42.3 MB. Each build counts about 5,783 children outside their parent's list and 0 to 38 FMEs without a Q.
- **Audits:** 19,188 lot audits, 0 mismatches (PASS).
- **Rebuilds:** 486 builds in 600 s, max 258 ms; 872 divergences in 949 syncs.
- **Records:** STALE 11,712, LOTS (rebase batches) 919; 21,103 records skipped "Q not in the clone", 16,826 applied to a stale lot.
- **Cost:** cut cost 224 ms/s; P3-B sim cost 55.6 ms/s.

**Where the 134.4 s of cut time went** [verified, counter deltas]:

| Part | Time | ms/s |
|---|---|---|
| 486 clone builds | 92.1 s | 153 |
| lot audits: all 82 lots hashed on the sim every 4th cut, ~97 ms each | 22.8 s | 38 |
| 919 rebase batches, ~20 ms each, each walking the whole live map for the batch's map order | 18.4 s | 31 |
| the rest | ~1.1 s | 2 |

Footprint copies (15.0 s) are mostly inside the builds.

**Cause 1, divergence and rebuild: Q-less FMEs had no record** [verified disassembly, matches the log]:
- **Adds:** a footprint with no lot reaches map M straight from `0x6376D0`. It calls `PathPlanner_AddHangingFootprint 0x636BF0` with lot pointer 0, makes the FME at `0x637809` and inserts it into the map at `0x63787E`.
- **Removes:** they leave through `LotRouting_RemoveObjectFootprints 0x635700` with lot id 0, called from `0x637931`.
- **No record:** neither path runs W1 or W2, so the clone never saw these changes. The map count drifted by 1 to 3 (log: entries 126,755 live 126,756, and so on), the next SYNC called it a divergence and the clone was rebuilt. The Q-less count in the build log grows from 0 to 38 over the run, which fits Sims and objects moving off lots.

**Cause 2, STALE storm: W3 is get-or-create** [verified `0x62F8B5..0x62F901`]:
- **What the function does:** `LotRouting_QuadTreeBuild 0x62F8A0` (lot lo, lot hi, level, create) returns the existing Q of that level. It builds one only when the Q is missing and create is not 0.
- **Why it fired so often:** `0x6376D0` calls it at `0x637713` with create 1 for every footprint add. 0.30.1 treated each call as a rebuilt tree: STALE, lot marked stale, rebase at the next cut.
- **Effect:** 11,712 STALE records, 919 batches and the 16,826 "applied to a stale lot" records come from this.

**Cause 3, "Q not in the clone"** [not explained; the next run will log it]. Inserts and removes on a Q that no clone lot holds. The writer also turns each such record into a footprint refresh at the next cut, which forces a batch, and each batch walks the whole map. One explained part: a Q that the create path of `0x62F8A0` really made is unknown until the rebase. Whether the W3 fix removes most of these is not known. 0.30.2 logs the first 8 such records with the Q, its lot id and whether the clone has that lot.

**Fix (0.30.2)** [offline]:

| Part | Change |
|---|---|
| W3 | the pre-hook reads the live lot table (bucket `lot % count`, chain `+0x190`) and the lot's Q vector (`+0x74`, level base `+0x40`); only a missing Q with create != 0 on a lot in the table is marked stale |
| Q-less records | `0x636BF0` with lot pointer 0 (key from the footprint's id slot: `0x806FE0` = `+0x14/+0x18`, `0x6262B0` = ~ the parent's) and `0x635700` with lot id 0 note the key. At the cut, before any batch (a batch re-chains the map to live order), each noted key's live Q-less nodes go to the consumer in chain order with fresh footprint copies (record type 0, `RP_NOQ`, counted in `rpRec[0]` / `rpApplied[0]`). The consumer swaps its Q-less nodes of that key for them. A footprint whose id slot is neither target resyncs every Q-less node once. Shortcut: equal-key order within a bucket follows `RpMapAdd`, not the game's insert. The SYNC count is exact. |
| Audit cost | `[RouteSnap] ReplayAuditLots` (default 2): lots hashed per audit cut, rotating through the table (0 = all, the old way); the consumer checks the live lot count separately. At 2 lots and every 4th cut, a 10-min BP run still gets ~700 lot audits (gate >= 200). |
| Diagnostics | first 8 "Q not in the clone" records logged (Q, lot id, whether the clone has the lot) |

**Checks [offline]:**
- **selftest (e):** a W3 get of an existing Q, a missing Q without create, a lot not in the table and an out-of-range level each give no STALE; a missing Q with create gives one.
- **selftest (f):** 400 steps mixed with the random writer steps; 206 Q-less adds and 171 removes through the two sites. 368 `RP_NOQ` records, 0 divergences, 0 rebuilds, map counts equal.
- **selftest (g):** 2 lots per audit cut, rotating, 80 audits in 40 cuts, equal.
- **Fake world W3 step:** now models get-or-create (a get, or Q removed then created). The main replay test still sees STALE records, rebases and skips.
- **Other checks:** steerro-test K1 seeds 1-2 and 2-3 x 3000 equal (1,507 / 1,478 cuts); `hook-sites.ts --check` (0 not in docs/68, the two id-slot constants added to the P3-B row); `snap-audit-report.ts --self-check`; `selftest ok`.

**Expected on the next run [inferred]:**
- **Builds:** one at load, plus one per world change.
- **Batches:** STALE and batches only where a level Q is really created or a lot graph is built.
- **Cut cost:** about 0.8 ms per cut plus ~2.4 ms per audit cut, so roughly 3 ms/s at BP's 2.4 cuts/s, still over G3's 2 ms/s. The tiles are ~2.8 ms/s of the old figure; with `ReplayAuditLots=1` and the lazy tile rebuild (§21.5) it would fit.
- **Sim cost:** copies at INS (~0.24 ms/s) plus rebases.
- **Open:** if "Q not in the clone" stays high, the batches stay too (each one a whole-map walk); the new log lines say which Qs.

**Effort (hours, Claude with Rodney):** ~2 h (counter split, static reads of `0x62F8A0` / `0x6376D0` / `0x637900` / `0x635700`, three fixes, tests, docs). Left: one 10-min run.

### 16.12 P3-B on 0.31.6: rebase batches fail the map resync (0.32.1)

> **Changes 2026-10-19:** new subsection. Live run of `bp-snap-p3b` on 0.31.6 (log of 2026-10-19 09:43), the two causes, the 0.32.1 fixes and offline tests. Ledger: docs/51 §109 (I-94-232 to I-94-236). 0.32.1 was not run in game; counters unchanged (v54).

**Live result (BP, 10 min)** [verified, report + log]:
- **Clone:** correct, 0 mismatches in 798 lot audits.
- **Rebuilds:** 114 builds (max 272 ms) and 116 divergences. Of the rebuilds, 112 were requested from `RpMapResync` failing at the end of a rebase batch.
- **Records:** INS 34,145 and REM 34,077; of these, 34,086 were skipped "Q not in the clone". LOTS 1,543.
- **Cost:** sim cost 39.8 ms/s (rebases 34.8), cut cost 71 ms/s.
- **The 0.30.2 diagnostic lines** name one Q, `0x7B404EC0`, with lot id 0 that no clone lot holds; it is both inserted into and removed from.

**Cause 1, the consumer recorded its own replay [verified: code path; the counts match].**
- **The loop:** the consumer replays INS and REM with the game's own `0x665C50` / `0x666750`. Those entries carry P1's W1/W2 stubs, so every applied record came back as a new record on a clone Q.
- **The counts:** 17,072 + 17,037 applied ≈ 34,086 skipped.
- **The cost:** each such record also queued a footprint refresh for its key, so nearly every cut sent a rebase batch (1,543), and each batch walks the whole 126k-node live map.
- **Thread-safety:** the recorder also ran on the consumer thread against sim-owned tables (`g_rpKnownQ`), which was not safe.

**Cause 2, the rebase dropped live map nodes [verified offline; whether BP hit it is inferred].**
- **What the apply did:** `RpApplyBatch` dropped every clone map node on the old lot's Qs and re-added only the batch's nodes, which are those on the lot's current Qs.
- **What it lost:** a node whose FME is still in the live map on a Q the lot no longer holds. That happens when a level Q was replaced, or when a removed lot's Q still has footprints nobody removed.
- **Result:** the clone count fell one short and the resync failed.
- **Offline reproduction:** the selftest world of §16.10 (a child outside its parent's list, inserted on its own) with 200 random steps: `map resync failed (clone 67 entries): live node 67 of 68 has no clone partner (key 0x3997, Q 0, footprint = the child)`, 2 divergences, 1 rebuild.

**Fix (0.32.1)** [offline]:

| Part | Change |
|---|---|
| No self-records | `RpDrain` sets a thread flag around the apply; P1's `SnEnter` returns at once on that thread, so the consumer's own game calls are neither counted nor recorded |
| Rebase apply | an old Q of a rebased lot that the new lot does not hold, and every Q of a removed lot, keeps its map nodes; their FMEs move to the clone's dummy Q (as the clone build treats FMEs on a Q no lot holds). Only Qs the lot still holds have their nodes dropped and re-added from the batch. |
| Diagnostics | the first 8 resync failures are logged with the first node that has no partner (key, live Q, `+0x10`, footprint) or with the counts |

Shortcut: later records on such an orphaned live Q are still skipped as "Q not in the clone". If the game removes footprints from a Q no lot holds, the counts drift again, and the next run's resync line names it.

**Checks [offline]:**
- **selftest (h):** the stand-ins run behind a P1 stub, as the patched entries do in the game, so the consumer's applies reach the recorder. The same seed and steps give identical replay counters with and without the stub: 308 INS, 4,603 REM, 59 batches, 0 divergences. 0.32.0 would add a record per applied one.
- **selftest (c):** now asserts 0 divergences and 0 rebuilds over its 200 steps with rebases (it failed before the rebase fix).
- **steerro-test K1 seeds 1-2 and 2-3 x 3000:** 16,172 / 15,924 lot audits, clone equal to live at every cut, 1,235 / 1,239 rebased lots; no K3/K6 WARNING.
- **Other checks:** `hook-sites.ts --check` (430 addresses, 0 not in docs/68), `snap-audit-report.ts --self-check`, `selftest ok`.

**Expected on the next run [inferred]:**
- **Records:** about half as many INS/REM, and "Q not in the clone" near 0.
- **Batches:** near the STALE and W4 count (119), not 1,543.
- **Rebuilds:** near 1 per load.
- **Sim cost:** footprint copies (~5 ms/s on 0.31.6, two thirds of them for the self-records) plus a few rebases.
- **Cut cost:** tiles and audit hashing.

**Effort (hours, Claude with Rodney):** ~1.5 h (log reading, the stub loop, offline reproduction of the resync failure, fixes, tests, docs). Left: one 10-min run.

### 16.13 P3-B on 0.32.1: lot mismatches, child FMEs without a partner (0.32.4)

> **Changes 2026-10-19:** new subsection. Live run of `bp-snap-p3b` on 0.32.1 (log of 2026-10-19 10:27), the causes, the 0.32.4 fixes and offline tests. Ledger: docs/51 §112 (I-94-240 to I-94-244). 0.32.4 was not run in game; counters unchanged (v54).

**Live result (BP, 10 min)** [verified, report + log]:
- **Fixed by 0.32.1:** "Q not in the clone" 0 (was 34k), 244 rebase batches (was 1,543), sim cost 14.9 ms/s (was 39.8).
- **New:** 5 lot mismatches in 906 lot audits; the clone has fewer tree entries than live (for example 15,956 against 16,032).
- **Still:** 198 builds and 200 divergences.
  - 189 rebuilds come from the map resync after a batch, with equal counts but a live node with no clone partner. Every logged one is a child FME: `+0x10` = `0x600` (the child-insert flag), key = ~ the parent's key, footprint = the child.
  - The SYNC divergences show the clone holding 6 entries more than live.
- **Records:** 23,331 records applied to a stale lot.

**Cause 1, one live footprint, two clone copies [verified offline; whether BP hit it is inferred].**
- **When it happens:** an insert record brings a fresh copy of the footprint. When FMEs in other lots still held the old copy, `RpFpUse` kept the old one and dropped the record's copy, even when the parent's child list had changed since.
- **Effect on the audit:** the replayed insert then put the old children into the Q, not the live ones. That is the audit difference.
- **Effect on the resync:** the live child FMEs had no clone partner. That is the resync failure.
- **Batches had the same flaw:** they paired old and fresh children by list position and asked for a rebuild when the lists differed.
- **Offline reproduction, selftest (i):** a parent is in two lots, is removed from one, gets a new child object with another key, and is inserted again. Once the swap was in, a child copied both inside an old block and again on its own still left two copies whose insert counters drifted apart.

**Cause 2, the 0.32.1 orphaned-Q keep was wrong [verified from the log].**
- **What 0.32.1 did:** it kept map nodes on Qs a rebased lot no longer holds (§16.12).
- **What BP shows:** the SYNC lines (clone 126,828 against live 126,822, the same +6 every time) show the game had removed those nodes.
- **Why selftest (c) failed instead:** the fake world's lot removal and Q replacement removed only its own footprint list, not every FME on the dropped Q.

**Fix (0.32.4)** [offline]:

| Part | Change |
|---|---|
| Rebase apply | back to dropping every clone node on the rebased lot's old Qs |
| One copy per live footprint | when a fresh copy replaces an old one, in an insert whose old copy has a different child list, or in a batch, the old block is retired (`c->old`, with the live footprints it copies in `c->oldLive`). Every clone FME on any copy of a live footprint that the fresh block holds, top level or child (matched by the live child, list node `+12`), moves to the fresh copy (`RpUnify`, one map walk per batch or per such insert). The same happens when a fresh copy names a footprint that a retired block still copies. A retired block is freed when no FME holds it, or with the clone. The resync's clone-to-live table includes retired blocks. |
| Insert with the same children | unchanged: the old copy is reused (no map walk) |
| Fake world | dropping a Q (replaced level Q, removed lot) removes every FME on it through W2 (`RfRemAll`) |

**"Applied to a stale lot" (23,331)** [code reading]:
- **What it is:** these records were applied as usual. The counter only marks records that reached a lot between its STALE record and its rebase batch.
- **Why they are correct:** the batch then replaces the lot with its state at the cut, which already contains them.
- **Why the number is high:** it scales with STALE events times the records per cut interval. I made no change; with fewer rebuilds it should fall.

**Checks [offline]:**
- **selftest (i):** changed child list, then 300 random steps: 0 mismatches, 0 divergences, 0 rebuilds, memory back to 0.
- **selftest (c):** 0 divergences and 0 rebuilds with the fake world's Q drop fixed.
- **selftest (h):** hooked-stub counters equal; 0 divergences.
- **steerro-test K1 seeds 1-2 and 2-3 x 3000:** 17,399 / 16,605 lot audits, clone equal to live at every cut, no K3/K6 WARNING.
- **Other checks:** `hook-sites.ts --check` (446, 0 not in docs/68), `snap-audit-report.ts --self-check`, `selftest ok`.

**Expected on the next run [inferred]:**
- 0 lot mismatches.
- No resync failures on child FMEs.
- The SYNC count matches.
- About one build per load.
- Batches cost one extra map walk each (already the case for the resync).

**Effort (hours, Claude with Rodney):** ~2 h. Left: one 10-min run.

### 16.14 P3-B on 0.32.4: stale shared footprint copies, clone memory (0.32.8)

> **Changes 2026-10-19:** new subsection. Live run of `bp-snap-p3b` on 0.32.4 (log of 2026-10-19 10:57), the likely cause of the remaining mismatches, the memory growth, the 0.32.8 changes and their offline tests. Ledger: docs/51 §115 (I-94-260 to I-94-264). 0.32.8 was not run in game; counters unchanged (v54).

**Live result (BP, 10 min)** [verified, report + log]:
- **Better:** sim cost 6.95 ms/s, builds 99, LOTS 121, stale-lot applies 7,121.
- **Mismatches:** 6 lot mismatches in 620 audits. Every one has the clone tree holding fewer entries than live (for example 11,196 against 11,280), with equal map counts.
- **Resync failures:** 97 rebuilds from the batch resync. The logged nodes are child FMEs (`+0x10` `0x600`) whose live footprint no clone FME names.
- **Memory:** clone 126.1 MB (cap 128), private pool region 59.7 MB.

**Cause of the mismatches [inferred; selftest (j) reproduces the shape].**
- **Fewer tree entries with equal map counts** means the clone inserted the same FMEs into fewer cells: its copy of a footprint had smaller or stale bounds.
- **Where the stale copy comes from:** `RpFpUse` reuses the clone's existing copy when other lots' FMEs still hold it and the child list is the same. Before 0.32.1, the consumer's own records triggered a refresh of such copies at nearly every cut (§16.12). Since then nothing refreshed them, so a footprint that changed between its remove and its re-insert (moved, re-shaped, its type bytes or counters set) was re-inserted with its old state.
- **The resync failures on children** fit the same picture: an old parent copy keeps old children while the live parent's children have moved on. This one is not proven.

**Cause of the memory [inferred].** Each BP clone is about 40 MB. 0.30.x had at most two alive (41 MB peak). With rebuilds asked while the previous EPOCH still sat unapplied in the ring, up to three were alive (126 MB). Retired blocks (§16.13) are freed once no FME holds them. The 20,000-step test below shows no growth from them.

**0.32.8** [offline]:

| Part | Change |
|---|---|
| Refresh in place | when the existing copy is reused for an insert record, it takes the record's state (`RpFpRefreshIn`): every field except its reference count, child list head, level array pointer and (child class) parent pointer. The polygon vectors of each level are swapped with the record copy's, so freeing the record copy frees the old arrays. Same for each child (matched by live object). Used when class, live children and level counts match; otherwise the retire + unify path of §16.13. |
| One pending epoch | a rebuild waits while the last EPOCH record is still unapplied in the ring |
| Mismatch detail | an audit mismatch logs the first Q (level index) whose tree entry count differs, live against clone |
| Resync detail | a resync failure logs the live footprint's class, whether it is a child, and which clone copy names it (top level, own block, in a parent's block, retired, none) |

**Checks [offline]:**
- **selftest (j):** a footprint held by two lots changes `+0x34` between its remove from one lot and its re-insert. 0 mismatches; with the refresh disabled the same test gives 8 mismatches.
- **selftest (k):** 20,000 random steps with a child list change every 400. The clone memory goes from 67 KB after the warm-up to a 100 KB peak (flat), with audits equal.
- **Earlier tests:** (c), (h), (i) still pass.
- **steerro-test K1 seeds 1-2 and 2-3 x 3000 on the merged build (0.32.7 crash H + this):** 17,419 / 16,625 lot audits, clone equal to live at every cut, no K3/K6 WARNING.
- **Other checks:** `hook-sites.ts --check` (446, 0 not in docs/68), `snap-audit-report.ts --self-check`, `selftest ok`.

**Not covered:** the threaded consumer lag behind the one-pending-epoch rule (tests drain synchronously). If mismatches remain, the new lines name the Q and the footprint.

**Effort (hours, Claude with Rodney):** ~1.5 h. Left: one 10-min run.

### 18.7 Door replay key and capture checks (2026-10-18)

> **Changes 2026-10-18:** new subsection of §18 (P4 part A; §18.1 to §18.6 come with the P4 part A commit `4879cba`). Static only: capstone over `game-copy/bin/TS3W.exe` (Steam 1.67) and a read of `p4.cpp` at `4879cba`. No game run, no code change. Answers §18.5 risk 1 (I-94-103) and checks I-94-100 to I-94-102. Ledger: docs/51 §78 (I-94-140 to I-94-147).

**The 9 values at `0x64F4D3`** [verified static: `Portal_LockState 0x64F2C0` builds them, `World_FirePortalLockedQuery 0x791C00` reads them]. `0x64F2C0` is `thiscall(portal)(lotState, direction)` (docs/92 §3.2). It asks only on a miss of its per-search answer cache. Arguments are numbered in cdecl order (arg 1 = last push).

| Arg | Pushed from | What it is | How `0x791C00` reads it | Same on a second run of the same route? | Match on |
|---|---|---|---|---|---|
| 1, 2 | `edi`, `ebx` = component `+0x08/+0x0C`; the component comes from `Object_GetComponent 0x935770` with mask `[0x11ECF08]` on the portal owner, found by `ObjectService_FindById 0x939560` | **lot id** (u64) of the lot the portal object is on; the same value is the key of `LotTable_GetIfPresent 0x62FB60` | 8 bytes from the argument's address (`0x4F5E60`, type `0xC`) | yes: an object id is the hash key for the object's lifetime (docs/45). The clone run reads it through the same live object service | full dwords |
| 3, 4 | `[lotState+0x40]` `+0x08/+0x0C` | **Sim (router) id** (u64): the agent record's object id | 8 bytes (`0x4F5E60`) | yes: the same Sim | full dwords |
| 5, 6 | `portal+0x08/+0x0C` | **portal owner object id** (u64), e.g. the door | 8 bytes (`0x4F5E60`) | yes, while the door exists | full dwords |
| 7 | `ebp` = `0x6240C0(word)`: the word minus `0x10`, or 0 when the word is `0x10` or less | **room index**: `portal+0x4A` when `direction` ≠ 0, else `portal+0x48` | low 16 bits (`0x4F5C60`, type 7) | yes, unless the lot's rooms were rebuilt in between (then the paths differ anyway) | low 16 bits (the high 16 are always 0, `movzx`) |
| 8 | `eax` = `0x6240C0(word)` of the other word | **room index** of the other end. Args 7 and 8 swap with `direction`, so their order is the direction | low 16 bits (`0x4F5C60`) | as arg 7 | low 16 bits |
| 9 | `ecx` after `sete cl` (the native lock state == 1) | **initialLocked** (bool): the native answer before the script is asked | **low byte only** (`mov cl, [esp+0x13C]` at `0x791E10`) | low byte: yes, when cost, AGS, forbidden list and route options are the same. **Bits 8-31: no** (below) | **low byte only** |

None of the 9 is a pointer, so the I-94-103 worry (a pointer into routing data that differs in the copy) does not apply and the key needs no clone id table [verified static].

**The trap in arg 9** [leftover verified static; the effect on the key inferred]. Only `cl` is set before the push; the upper 24 bits of `ecx` are whatever the register held:

- `direction` ≠ 0: `ecx` is the zero-extended word `portal+0x48`, so bits 8-15 are that word's high byte. Stable.
- `direction` = 0: nothing writes `ecx` between `LotTable_GetIfPresent 0x62FB60` (`0x64F459`) and the push (`0x6240C0` uses `ax` only). So `ecx` is what `0x62FB60` → `0x62EAB0` (lot table window) left: on a window hit the table's current slot index (`[table+0x50]`), on a miss whatever the destructor and heap free (`0x62B930`, `0x4E3F70` → `0x4E5920`) left. The clone run uses the clone's lot table (`[lotState+0x44]`), whose window state is not the live one, so the same question can carry different upper bits in the live and the clone runs.

`P4DoorEnter` (p4.cpp at `4879cba`) matches with `memcmp` over all 36 bytes. A difference in those dead bits turns a correct replay into `door-miss`, which aborts the shadow and reads like a path difference. The window index is usually below 256, which hides the bug: one run can pass and a later one fail.

**Recommended replay key:** args 1-6 as full dwords (lot id, Sim id, portal id), args 7 and 8 masked to `0xFFFF` and kept in order (the order is the direction), arg 9 masked to `0xFF`. Mask at record time and at lookup. Keep "first unused entry with an equal key" (§18.1), so repeated asks with one key get the live answers in the live order. Print the masked key in the `door-miss` log line. Nothing else belongs in the key: the answer comes from the managed handlers, and the replay supplies their live answers. Selftest: record an ask with garbage in bits 8-31 of arg 9 and replay it with other garbage; it must hit.

**Capture checks (I-94-100 to I-94-102).**

| Row | What was read | Result |
|---|---|---|
| I-94-100 options copy | ctors `0x81CB60` / `0x81CC40`, dtor `0x81CBE0`, assignment `0x81D090`; scan of `0x81C800..0x81D460` for non-stack displacements `+0x80..+0x9B` (none) | **Refuted.** The object is **0x80 bytes** (`+0x00..+0x7C`) and owns **five** heap vectors, not four: `+0x08` (a sorted u64 set: begin/end/capacity `+0x08..+0x10`, allocator `+0x14`, compare byte `+0x18`; filled by `0x81D010` in the assignment, freed by the dtor; the id set of I-98-17) plus `+0x38`, `+0x48`, `+0x58`, `+0x68`. P4 copies 0x9C bytes and deep-copies four, so the private copy's `+0x08` points at the live route's buffer, and the clone job's `RoutingJob_ctor 0x819D50` → `0x81D440` → `0x81D090` reads it on the worker. If the sim thread changes that set meanwhile, the read can hit freed memory. Fix: add `0x08` to `kVec` (same 8-byte elements and layout). The 0x9C size is a harmless over-read of `route+0x1D8..+0x1F3`, which no options code reads |
| I-94-101 start/goal records | vector relocate `0x651350` (from `0x7564D0`), both goal pushes in `0x75C2D0` (`0x75CD9B`, `0x75D0F7`), field reads in that function | **Supported.** Records copy member by member with no AddRef and no deep copy: 16-byte vectors `+0x00`/`+0x10`, float `+0x20`, dwords `+0x24..+0x30`; `+0x34..+0x3F` is never copied (padding). `+0x24` is read as a 16-bit value (`0x75CAD5`) and compared signed (`0x75C6D5`), `+0x28` is the slot hash, `+0x2C` the tag, `+0x30` a small integer. Copy semantics verified; "no raw pointer in `+0x24..+0x30`" inferred from how the fields are read |
| I-94-102 out-list head words | `Route_PlanLocked 0x82C6E0`, `RoutingJob_Execute 0x81B2A0`, route list resets `0x759510`, `0x756840` | **Wording refuted, conclusion holds.** `Route_PlanLocked` reads only `head+0` (`0x82C7BB`), but `RoutingJob_Execute` reads `head+0x08` at `0x81B318` and `0x81C139` and passes it as the name argument of the allocator `0x4E3F90` for every 12-byte node it appends (the same argument slot gets the string `Routing/RoutingPathImp` at `0x81B2B7`). So `route+0xE8` is the list's allocator member and the private head must carry the live value, which P4 does (`head[2] = outTail[0]`). `route+0xEC` is the route's refcounted current path (released at `0x756855`, `0x759534`), not a list field; neither function reads `head+0x0C`, so P4's copy without an AddRef is fine. Deeper closure not scanned |

**Effort (hours, Claude with Rodney):** masked door key, its selftest case and the log line ~0.5; `+0x08` in `kVec` with a `TestP4` check that all five private vectors are private ~0.5.

## 21. P4 part B status: the replay clone as P4's clone provider (0.29.0, counters v51)

> **Changes 2026-10-18:** new section. Build only; the game was not run. Code: `native/ts3native/p4clone.cpp` (new), `replay.cpp`, `p4.cpp`, `snap.cpp`, `rtsites.cpp`, `ts3native.cpp`, `selftest.cpp`, `steerro-test.cpp`, `tools/harness/queue.ts`, `tools/harness/queues/snap-p4.json`. P4 part A (§18) is merged on P3-B (§16): ts3native **0.29.0**, counters v49 (P3-B's real fields replace part A's placeholder), v50 unchanged at `0x2968`, v51 new. Ledger: docs/51 §83 (I-94-130 to I-94-139). **Private pools (§16.6) are not in this build**: a separate agent builds them (`pools.cpp`, counters v53); this build must not run in game before that merge (§21.6 risk 1).

### 21.1 What is built [verified offline]

| Part | What it does | Counters |
|---|---|---|
| P3-B fixes from `bp-snap-p3b` | every clone build failed on footprint vtable `0xFEBF90` (1,978 builds, up to 10.5 ms each): it is a 0x28-byte **child** sub-footprint (`0x807400`, pool `0x1165B80`, `+0xC` the parent, no child list; I-94-130); now copied flat inside the parent's block with `+0xC` re-pointed, never alone; the lot hash reads `+0x34` only for classes that long. An unknown class leaves its lots out of the clone (SYNC, audit and map order skip them; a refused insert copy on a cloned lot rebuilds before that cut's SYNC/audit) instead of failing the build. A failed build backs off 10 s on the same world (0.28.0 compared against a world it never stored, so it rebuilt at every cut). High-cost tiles back to 64 cells (§21.5) | v49 `rpExclLots`, `rpUnkClsVt[4]`, `rpUnkClsN[4]` |
| Door key, options (§18.7) | replay key = args 1-6 whole, 7-8 `& 0xFFFF`, 9 `& 0xFF`, masked at record and lookup, masked key in the miss log; the options copy is 0x80 bytes with the fifth vector `+0x08` deep-copied | (v50) |
| Provider `cut` (sim, armed async START) | refused while an async job runs (the lock must be free); takes `+0x918`; §8.5 steps 2-4 with the game's own writers so P1's leaves record them: `ApplyPendingUpdates 0x82C420` when `P+0x10E`, `FlushDeferred 0x666A40` on each lot Q with `+0x4C`, first use (`RoomGraph_FirstUse 0x63D2F0`) of unbuilt lots `LotTable_Contains` accepts; a forced P3 cut (images + the replay cut, which publishes the ring position); then a snapshot: world bytes (0xA00, I-94-135), P bytes (0x110), the A\* stamp word, the active lot id, the live high-cost map headers and the blockedNodeMap end value, a deep copy of P's footprint grid `+0xDC` when P3's grid image changed (I-94-131), copies of every lock-key set reachable from P's map when a lock-key IC ran or the epoch moved. The consumer is held at the published position (`g_rpHoldAt`, 3 s) | `snPcCuts`, `snPcCutUs`, `snPcCutMaxUs`, `snPcPending`, `snPcFlushed`, `snPcFirstUse`, `snPcRefused_0/1` |
| Provider `acquire` (worker) | waits up to 50 ms until the consumer reached the position, then takes the clone exclusively (`g_rpUse`; the consumer drains under it); refuses `epoch` for an older cut, a passed position, a moved epoch or **a moved active lot (I-94-87: refused, not copied)**, `mat` on a timeout or a failed copy. Working copy: the world bytes with a private unlocked `+0x918` (I-94-136), `+0x95C` = the clone's P with the cut's P bytes over everything the clone does not own (map `+0x24`, lot table `+0x48..+0x67`), P's grid → the private copy, `P+0xBC` and every clone Q's `+0x28` → a private cAStarSearch object (`0x61BF60` on a 0x9C90 block; I-94-134), R (`+0x54`) from the P3 R image, the high-cost maps (`G+0x7C0`) from the P3 tiles (only changed tiles copied), blockedNodeMap rebuilt from the P3 image (`key % count`, I-94-138), every clone footprint's `fp+0x20` → the private lock-key set of its live set (our copies carry their origin). A new A\* object starts the per-copy stamp at the cut's live stamp (`fresh`) | `snPcAcquired`, `snPcMatUs`, `snPcOddQ`, `snPcRefused_2..6` |
| Provider `release` (worker) | the lots the clone run wrote are forced into the next cut's rebase (every lot on `-1` or a fault); `DISCARD` also rebuilds the clone and drops the A\* object, `WRAP` drops it (fresh stamps next time); hold and clone lock released | `snPcDirtyLots`, `snPcDiscard` |
| Rooms | each cloned lot's room records `+0x90` (0x44 bytes) with their `+0x24` and `+0x34` vectors are copied at the clone build and every rebase (flat; I-94-133) | |
| `Los` refusal (§17.9) | async START refuses a route whose option word has `ForcePhysicsBasedRaycast` (`0x2000000`, route `+0x158+0x28`, as `RoutingOptions_Test 0x81C880` at `0x75AB9C`); those plans stay on the sim thread | `arLosRefused`, refusal name `Los` |
| N15 (§17.9) | D5 family 8 "display level": count-only detours on `Lot_SetDisplayLevel 0xAC0570`, `Lot_RoofUp 0xAC2190`, `0xAC5C90`; counted only when the plan ran `0x6583E0` (reader bit set in the `[RouteGate]` #1 stub); a write in the window = M5 (`nr` bit 128); a writer call from a clone run is the tripwire | `snNrBitDl`, `snDlOnClone` |
| Pools hook | `p4.cpp` calls `g_p4PoolsEnterJob()` / `g_p4PoolsLeaveJob()` around each clone run when set (NULL here) | |

### 21.2 Private and live in a clone run

| Data | In the working copy | Evidence |
|---|---|---|
| Lot planner P: map, lot table, lot graphs, Qs, trees, FMEs, footprints with polygon caches | the replay clone (§16) | audited (P3-B) |
| Room records, grid, A\* object, lock-key sets, R, high-cost maps, blockedNodeMap, `+0x918` | private copies (§21.1) | offline only |
| G cells, passability maps, body table, lot-edge data, `+0x964` | **live**: load-time state, rebuilt only by a G build (I-94-137) | inferred (corrected 2026-10-19, §13.3 K6: `+0x960` is the world planner W, not a water planner, and it is not load-time state: it points at the live P, R and G and holds the world A\* container and per-plan caches, so a clone run through it reads live R/G/P and writes live W, I-94-220 [verified]; the G trees cannot be byte-copied, I-94-221) |
| World planner `+0x960` (W') and G's water-link trees `+0x30` / `+0x4C` | private since 0.32.0 (§21.13): W' on the clone's P and the copy's R / G, own container and caches, group hash copied at the cut; the trees re-made under the copy's anchors | offline (selftest, steerro K6 with guarded live pages) |
| Tree, map node, A\*, goal and cell pools | **live** (the game's unlocked pools): the separate pools build makes them private | §16.6 |
| Non-routing roots (world manager, object service, physics, terrain) | live, guarded by D1-D5 (§17) | §17 |

### 21.3 Checks [offline]

- `build.cmd` selftest: `TestSnapReplay` child class (copied in its parent's block, `+0xC` re-pointed, 300 steps 0 mismatches) and left-out lot (unknown class: lot out, per-vtable count, 300 steps 0 mismatches and no divergence); **P4 part B end to end** in `TestP4`: replay-fake.h's world, its replay clone registered as the provider, a fake planner that reads only the routing world it is handed (P's lot table and every lot's trees via the audit hash, the lock-key sets through P's map, one world byte as the status): **M0** on an unchanged world and again after 40 recorded writes, **M1** for an unrecorded tree insert after the cut (only the inner node's y differs), **M3** for a world byte written after the cut (the copy has the cut's), lock-key sets private, the copy's `+0x918` unlocked, an older cut refused `epoch`; door key with register junk in args 7-9; options `+0x08` private; N15: M5 with `nr` 128 when the plan ran the vertical ray, M1 without; the three writer stubs on dummy prologues (counted, tripwire from a clone run); `Los` refusal. `shadow-compare-report.ts` on the selftest records gives the same classes.
- `steerro-test` seeds 1 and 2 x 3000: ok (REPLAY 1,479 / 1,537 cuts, 0 mismatches); new line "P4 part B: pending / flush callees, 3 display level writer prologues and the Los option test match".
- `node tools/hook-sites.ts --check`: 0 missing (docs/68 row for P4 part B); `node tools/check-docs-code.ts`; `node --check tools/probe/agent.js`; `shadow-compare-report.ts --self-check`.

### 21.4 Run

asi parked at `game-copy/ts3native.asi.snapp4` (sha256 `dd2477dce5f69d20148c28ffae79921b6fa7339e5eab8148f0872c412bd6eb60`, built at `c609218`). **Not before the private pools build is merged and the asi rebuilt with it.** Then: `node tools/harness/queue.ts tools/harness/queues/snap-p4.json` (jobs `bp-snap-p4`, `ip-snap-p4`: env-on async + `[RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 ShadowEvery=1 Kinds=15`, 10 min each, `minCounters` 51, daytime OK: shadow only). `queue.ts` now copies `Bin/ts3native-shadow.jsonl` per job (`row.shadowLog`). After: `node tools/harness/shadow-compare-report.ts <row.shadowLog>` and `node tools/harness/snap-audit-report.ts <row.nativeDump> --log <row.nativeLog>`. First-run pass: no crash, `snShadowFault` 0, tripwires 0 (`snCloneManaged`, `snCloneEvent`, live-lock aborts, `snDeadStamp`, `snDlOnClone`), `rpMis` 0, `snPcAcquired` > 0; M2/M3 are findings to triage (G4 needs 0 over 10k compares per world).

### 21.5 Cut cost

The P3-B BP run spent 3.37 s in cuts over 10 min (5.5 ms/s): 1.67 s were the failing clone builds, the rest 0.86 ms per cut against 0.50 in `bp-snap-p3`, with 57,892 32-cell tiles rebuilt against 7,756 64-cell ones (I-94-139). Expected with this build [inferred]: BP world cuts back to ~1.9 ms/s; each armed P4 START adds a forced cut plus the snapshot (world 2.5 KB, P, headers: µs; grid and lock-key copies only when changed), so at `ShadowEvery=1` the cut count follows the async START rate instead of `CutMs`. If G3's 2 ms/s is exceeded, raise `ShadowEvery` or rebuild dirty tiles lazily at armed cuts only.

### 21.6 Risks

1. **No private pools in this build** (§16.6): the replay consumer and a clone run pop and push the game's unlocked tree, map and A\* pools. Do not run `snap-p4` (or `snap-p3b`) with it; merge the pools build first and fill the `g_p4PoolsEnterJob/LeaveJob` hook.
2. **Live G and water planner** (I-94-137): a G build inside a clone window would free what the copy points at (caught as a fault: P4 off).
3. **Shared footprint state in the copy**: a clone run that writes footprints (lazy polygon rebuild, D4) writes the clone's copies; release rebases those lots only when the writer leaves named them (`snPcDirtyLots`).
4. **Inferred layouts**: grid record size (I-94-131), room `+0x34` (I-94-133), world size (I-94-135), Q `+0x28` (I-94-134, counter `snPcOddQ`). A wrong one shows as M2 noise or a fault, not silent success.
5. **Consumer hold**: the consumer stops at the armed cut for up to 3 s; a long job lets the ring fill (65,536 records), which rebuilds the clone (`rpDrop`).

### 21.7 Effort (hours, Claude with Rodney)

| Work | Hours |
|---|---|
| Merge P4 part A on P3-B (0.29.0, v49/v50) | ~0.5 |
| P3-B live fixes (class `0xFEBF90`, left-out lots, back-off, tiles) | ~1.5 |
| Private pools (built here, then handed to the separate build: removed) | ~1.5 |
| Provider (cut, acquire, release, working copy parts, rooms), selftest end to end | ~3 |
| §18.7 door key and options, §17.9 `Los` and N15, docs, queue | ~1.5 |
| Left for G4: pools merge and rebuild, F-7 tie set, K3/K4/K5 on the game image, the two shadow runs and fix rounds | ~6-10 |

### 21.8 P4 portal references H20 (0.30.0, counters v54)

> **Changes 2026-10-19:** new subsection. Build and offline checks only; the game was not run. Code: `p4.cpp`, `replay.cpp`, `p4clone.cpp`, `ts3native.cpp`, `selftest.cpp`, `steerro-test.cpp`, `replay-fake.h`. Source of the items: docs/109 §8.9 (probe pass 3, H20 in docs/101 §2.3). Ledger: docs/51 §93 (I-94-156 to I-94-161).

**Layout checks on the Steam 1.67 exe** (capstone 5, and `steerro-test` asserts on the mapped image; all **[verified]** unless marked):

- Portal vtable `0xFED048`: slot 0 `0x64E8E0` (plain `add [ecx+0x70],1`, returns the new count), slot `+4` `0x64E8F0` (count <= 1: calls slot `+8` with 1 and returns 0, else a plain decrement), slot `+8` `0x64F1A0` (deleting destructor). No `E8`/`E9` in `.text` targets either function and the two vtable slots are their only data references, so the slot swap reaches every AddRef and Release made through a call. The vtable address is stored only by the two constructors (`0x64EEF9`, `0x64EF8F`) and the destructor (`0x64F1A5`). Inline writes of `+0x70` elsewhere were not searched **[inferred: none, I-94-157]**.
- Room record (0x44 bytes; copy `0x6223C0`, destroy `0x621D20`): `+0` a byte, then four 16-byte vectors: `+4` and `+0x14` of 4-byte counted pointers (the copy AddRefs each element through `0x9EC420`, which calls slot 0; the destroy releases each through `0x5BF1E0`, which calls slot `+4`, then EA-frees the buffer), `+0x24` of 2-byte and `+0x34` of 16-byte elements (copied by `0x89D600`).
- Lot graph (lot node `+0x10`): the portal list `+0x44` and the pending adds `+0x54` hold 4-byte counted portal pointers; the **pending removes `+0x64` hold 8-byte portal ids**, not pointers (`0x639A80` walks them with `sar 3` and passes each id to `0x635EE0`). This closes the open `+0x64` side of I-109-37: there is no reference to own there, only the emptiness check (I-94-158).
- `LotRouting_AddPortal 0x6379E0` pushes into `+0x54` only when `P+0xC8` is non-zero (`cmp dword [edi+0xC8],0` at `0x637BAF`), else into `+0x44`.
- `LotRouting_AddPortalVariation 0x637C30` pushes into a vector at `+0x5C` of the object it found (`0x637E85`, call `0x62A830`) **[inferred: that object is the portal, whose destructor frees `+0x5C`; I-94-161]**.

**What is built:**

| Item | What | Counters (v54) |
|---|---|---|
| P4-H20a | `ApplyP4` (`[RouteSnap] Mode=1`) calls `P4PortalRefs`: design SH's `InstallPortalRefX` puts the interlocked `PortalAddRefX`/`PortalReleaseX` in vtable `0xFED048` (or behind P1b's counting thunks), sets `slRefSwap` and turns the clone's references on. Slots holding anything else skip P4 (the log line names H20a). For the whole session, as SH | `slRefSwap` (existing) |
| P4-H20b | `RpLotShell` also copies each room record's `+4` and `+0x14` vectors (the clone reads no live room buffer), and with the pair installed takes one reference per non-null element of `+0x44`, `+0x54` and every room's `+4`/`+0x14` through the element's own vtable (as `0x9EC420` does). The pointers are kept per lot copy (`RpLot.prefs`) and released through slot `+4` in `RpLotFree` (rebase, lot removal, left-out lot, clone free, dropped batch) on whichever thread frees the copy. A sim-side Release (`RemovePortals`, a lot rebuild) then cannot delete a portal the clone holds: docs/88 design G's "park a reference" rule applied to portals. A copy whose portal vectors are not all private takes none (a live vector is never ours to count) | `rpPortalRefs` (gauge) |
| P4-H20c | At the armed cut (`PcPrepare`, after the pending-update apply, the flushes and first use) every live lot is checked: `+0x54` or `+0x64` not empty refuses the cut (no version: the shadow is `nover`). At acquire every clone lot is checked again (what the run reads): not empty refuses with `mat`. Refused, not rebased: a rebase would copy the same non-empty live vectors. D4 (§17.4) changes: a lazy `LotGraph_Build` (gate site 3) or `LotTable_Insert` (`PV_LAZY0`) in a clone run now raises our exception `0xE0503401` **before** the call; `P4Guarded` catches it, the record says `lazy`, the clone is discarded (`P4REL_DISCARD`: rebuilt at the next cut, A\* object dropped) and P4 stays on (not a fault) | `snPcPortalPend[0]` (cut), `[1]` (acquire); `snCloneLazy`, `snAbort` `lazy` |
| P4-H20d | D5 family 9 "portal ICs": the sum of `[RouteSnap]` IC slot counts 4-14 (`AddPortal`, `AddPortalLocationVariation`, the five `Add*Portals`, `RemovePortals`, `SetPortalCost`, `SetPortalAgeGenderFlags`, `SetPortalLockStatus`), always counted (no reader stub: every lot search reads portals). A change in the window sets `nr` bit 256 (M5). No new hook: the IC shims exist since P1 | `snNrBitPortal` |

**Checks [offline]:**

- `selftest`: H20b on the fake world with fake portals (interlocked pair, a deleting slot that only counts): each lot copy holds exactly 9 references; after 600 writes with cuts (rebases, lot adds and removals) raced by a second thread doing AddRef/Release pairs (1.2-1.5 M pairs over seeds 21 and 22), every portal's count equals the live holds plus the clone's; 0 deletes; all released at the teardown (`rpPortalRefs` 0). P4 part B: a live lot with a pending add refuses the cut (`nover`, `snPcPortalPend[0]`), a clone lot with a pending remove refuses the acquire (`mat`, `[1]`). P4 part A: the lazy stop from gate site 3 and from `PV_LAZY0` (record `lazy`, the code after the call not reached, clone discarded, `snShadowFault` unchanged, P4 still on); `SetPortalCost` in the window gives M5 with `nr` 256.
- `steerro-test` seeds 1 and 2 x 3000: ok. New lines: the layout checks above on the mapped image and `P4PortalRefs` installing the pair; REPLAY H20b with portal objects of the game's vtable `0xFED048` (interlocked pair) in every lot and room, ~12.3 M raced pairs per seed, counts exact, all released.
- `node tools/hook-sites.ts --check` (421 addresses, 0 missing; no new hook site), `snap-audit-report.ts --self-check`, `shadow-compare-report.ts --self-check`, `node tools/check-docs-code.ts`.

**Risks:**

1. The portal's variation vector `+0x5C`, cost `+0x2C` and flags `+0x78/+0x7C` stay live. H20d makes a write in the window M5, but `AddPortalLocationVariation` can grow `+0x5C` (EA free of the old buffer) while a clone run reads it: a use after free on the worker, caught as a fault (P4 off for the session). If shadow runs show `snNrBitPortal` together with faults, put the D2 handshake on that IC slot, or copy portals as plain records (docs/92 §1).
2. The lazy stop unwinds the game's frames with SEH. Locals that hold references (a room copy, a local portal vector) may leak a portal count (never freed, no crash), and a `catch (...)` built with `/EHa` in those frames would swallow the stop [inferred: not looked for]. Expected 0 stops (I-94-97, I-94-159).
3. Interlocked counts cover calls through the vtable only; an inline `+0x70` write outside the constructors would race (I-94-157).
4. While P4 is armed each lot copy pays 9-30 interlocked AddRefs at the copy and the same Releases at the free (a few lots per cut); not measured (I-94-160).

**Effort (hours, Claude with Rodney):** layouts ~0.5, H20a-d code ~1.5, tests (fake portals, race, cut and acquire refusals, lazy stop, D5 bit, steerro on the image) ~1, docs ~0.5: ~3.5 h. No game run of its own: it rides on the `snap-p4` shadow jobs (§21.4) with this asi (`ts3native.asi` sha256 `e275dbba916902ceadba9f5a37ce8be1c67988394ec09d021b45b437e4ee94cb`; `snap-p4.json` now needs `minCounters` 54). Pass for H20 in those runs: `snPcPortalPend` 0/0, `snShadowFault` 0, `rpPortalRefs` back near 0 after the clone is dropped, `lazy` records without faults.

#### 21.8.1 Follow-up: the three open H20 risks settled (2026-10-19, static)

> Static reads only (capstone 5 linear sweep of the whole `.text` of the Steam 1.67 `TS3W.exe`, 4.14 M instructions, independent of `functions.csv`; direct-call graph from that sweep; `tools/managed-callgraph.ts callers`; our source). No game run, no code change. Ledger: docs/51 §99 (I-94-177 to I-94-182). Closes I-94-157 and I-94-159, narrows I-94-161.

**Risk 3 of §21.8 (inline `+0x70` writes): none on portals. No fix [verified].**

- The portal vtable `0xFED048` is 4 slots: AddRef `0x64E8E0`, Release `0x64E8F0`, deleting destructor `0x64F1A0`, and a type test `0x64E8B0` (compares two 32-bit type ids). The vtable address appears in code only at the two constructors (`0x64EEF7`, `0x64EF8D`) and the destructor (`0x64F1A3`), and the constructors are called once each: `0x62CA3F` (in `0x62C920`, from `0x6236D0`) and `0x637B46` (in `LotRouting_AddPortal 0x6379E0`). No inlined constructor and no by-value portal copy that stores the vtable exist; every holder found keeps pointers [verified for the vtable stores; "no memcpy copy of a whole portal" inferred from the holders].
- Both construction sites take the first reference **through the vtable** (`mov edx,[esi]; mov eax,[edx]; call eax` at `0x62CA4A`, and the same at `0x637B59`), right after `new`, where the type is known. The compiler did not devirtualise even there, so it never inlines portal AddRef/Release anywhere.
- All 809 stores with a `[reg+0x70]` destination in the sweep (not `esp`/`ebp`): 35 are integer read-modify-writes (`add`, or `mov` after a load of the same `[reg+0x70]`). Apart from `0x64E8E0` and the Release store `0x64E907`, they are other classes: 64-bit accumulators (`add`/`adc` pairs at `0x4CD98F`, `0x4CDDC1`...), byte-stream and buffer cursors (`0x9DAFF8`-`0x9DB551`, `0x7308C4`, `0xE22B8B`, `0xE3B704`), counters in `0x593600`, `0x5FDEF0`, `0x6DD220`/`0x6DD6D0`, `0xF3A360`, none of them in the routing ranges and none called from portal code. In the routing ranges (`0x620000`-`0x660000`, `0x740000`-`0x770000`, `0x820000`-`0x840000`) the `+0x70` stores are the two constructors (init 0), AddRef, Release and objects of other layouts (`0x62AAC7` a vector name pointer, `0x62E8D6` and `0x635DF8` lot table / hash fields, byte flags at `0x642xxx`-`0x646xxx`, floats).
- So the only writers of a live portal's count are the constructors (before the portal is published) and slots 0/`+4`. H20a's interlocked pair covers every write after construction. Nothing for P4.

**Risk 1 of §21.8 (variation vector `+0x5C`, cost, flags, lock word): no use-after-free path for a clone-held portal; accept with the existing fault counter plus one tripwire [verified writers; frequencies inferred].**

Portal layout used below (from the two constructors): `+0x2C` cost (float), `+0x58` flag word, `+0x5C..+0x6C` the variation vector (begin, end, capacity, allocator name `0xFED028` = "RoutingPortalLocationVariants", flags), `+0x70` count, `+0x78/+0x7C` age/gender flags (64-bit). The destructor frees `+0x5C` only at the last Release, which H20b's clone reference prevents while a clone holds the portal.

| Field | Writers (static, whole exe) | Portal written | In a clone window? | Verdict |
|---|---|---|---|---|
| `+0x5C` vector grow (`0x637C30` -> `0x62A830`; the portal is the caller's 4th argument, `ebp` at `0x637E82`) | (a) `IC_RouteManager_AddPortalLocationVariation 0x74D3F0`: script callers are only `BoxStallPortalComponent.AddPortals`, `NonAnimatedDoorPortalComponent.AddPortals`, `StaircaseSpiral.AddPortals`, each passing the handle that `Route.AddPortal` returned a line earlier; (b) builders `0x637EF0`, `0x6383C0`, `0x6387B0` from `RoutingWorld_AddObjectPortals 0x82D2E0` (the four `Add*Portals` ICs), called after its `LotRouting_AddPortal` at `0x82DAD2`; (c) `0x638C20`, `0x639010` from `DoorRouting_AddDoorPortals 0x80CB40` (vtable `0xFEB948`); (d) the same builders from `0x63AB60` <- `0x63AFD0` <- gate site 14 `0x835240` <- `IC_RouteManager_OnExitBuildBuy 0x74B6B0`, on existing portals (`edi`, whose `+0x58`/`+0x6C` it reads) | (a)-(c) a portal created in the same sim-thread call; (d) existing portals | (a)-(c) yes, but the new portal is not in any clone copy: it was made after the cut, and a pending add (`+0x54`) refuses the cut and the acquire (H20c); (d) no: gate 14 is a D2 site, it waits for the window | no UAF on a clone-held portal [verified call order and C# callers; "no cut between `Route.AddPortal` and the variation call" inferred: both run in one script method on the sim thread, cuts happen at route STARTs] |
| `+0x2C` cost | `IC_RouteManager_SetPortalCost 0x758110` (one aligned `movss` per portal of the object, `0x7581C8`); script: `Door.SetPortalCost` from `Door.OnLoad`, `EnablePrivacy`/`DisablePrivacy` (`Lot.UpdateDoorPrivacy`), `OnBuildBuyExit`; `AddPortals` of velvet ropes and the giant boulder | existing portals | yes (privacy toggles in play) | atomic 4-byte store: the worker reads the old or the new cost, never a fault; M5 through H20d (`nr` 256) |
| `+0x78/+0x7C` flags | `IC_RouteManager_SetPortalAgeGenderFlags 0x758220` (two plain stores, `0x7582CB`/`0x7582CE`; script: `Door.SetPortalAgeGenderFlags`, door lock settings); `0x82DB4C` in `0x82D2E0` (new portal); `0x63AE3D`-`0x63AF09` in `0x63AB60` (D2, gate 14) | existing (IC), new, existing under D2 | IC only: user action, rare | a torn pair is possible (new `+0x78`, old `+0x7C`): a wrong answer for one plan, M5 through H20d; no fault |
| `+0x58` flags | `0x637B83`/`0x637B8A` in `0x6379E0` and `0x64F590` (from `0x62C920`) | new portals before publication | no | none |
| lock word (lot entry `+0x8F`, not the portal) | `IC_RouteManager_SetPortalLockStatus 0x74B490` (`0x74B4BD`); the planner's clear in `Portal_LockState 0x64F2C0` (`0x64F4EE`) | live lot table; in a clone run the clear writes the clone's table | the IC: lot load and privacy handler add/remove | the clone reads its own lot table copy (§21.2): stale by design, M5 through H20d [inferred from §21.2] |

Other native callers of `LotRouting_AddPortal 0x6379E0` (`RoutingWorld_AddLotObjectPortals 0x832890`, also from `0xAC79E0` outside D2) make new portals and Release old ones; they write no field of an existing portal [verified store scan].

**Recommendation for risk 1:** neither a D2 handshake on the portal ICs nor plain-record copies of portals: the one writer that reallocates `+0x5C` of an existing portal (`OnExitBuildBuy`) is already behind D2, and the cost/flag writes cannot fault. Keep `snShadowFault` + `snNrBitPortal` and add a count-only tripwire in the existing `[RouteSnap]` slot shim of `AddPortalLocationVariation`: when `SnLive()` and the portal argument is in the acquired clone's `prefs`, count it (new counter, docs/registry.md) and log the first 20. Expected 0; a non-zero count is a mod or a script path we did not see, and then D2 on that slot (~0.5 h more). **Effort: ~0.5 h** (shim branch, selftest line, counter slot, docs). Copying portals as plain records (docs/92 §1) would cost ~4-6 h and is not needed.

**Risk 2 of §21.8 (lazy-stop SEH unwind): nothing in the game swallows the stop; it leaks memory and some portal counts per stop; one log fix [verified frames, leaks inferred].**

- The whole exe registers an `fs:[0]` frame in only ~25 functions (64 stores): CRT start-up and thread code (`0x4089B0`, `0x40A420`, `0x40A4A0`, `0x40E700`, `__SEH_prolog4` `0xF7F3B0` with 5 callers, `0xF7DE60`, `0xF7E0F0`), `__try` blocks with `_except_handler4` (`0xF7EFAA`) in `0x4D6320`, `0x4D6DF0`, `0x4D7E90`, `0x4E2B70`, `0x4E2E30`, `0x4E30A0`, `0x6157A0`, `0x615830`, `0x619540`, `0x6CF5A0`, `0x723F50`, `0xE4D460`, `0xF23BB0`, `0xF34070`, and three tiny C++ `Catch@` blocks at `0x40A4xx`/`0x40E8xx`. Several `__try` filters return `EXCEPTION_EXECUTE_HANDLER` unconditionally (a catch-all), but **none of these functions is in the 1,322 functions reachable by direct calls from `Route_PlanLocked 0x82C6E0`, and none reaches `0x63CB90`, `0x62E680` or `0x62F8A0` through its own direct closure**. The game code is built without C++ exception handling: no `__CxxFrameHandler` frame, so no `catch (...)` (with or without `/EHa`) and no destructor runs during the unwind.
- Direct chains to the stops: `Route_PlanLocked` -> `RoutingWorld_ApplyPendingUpdates 0x82C420` -> `0x82B590` -> `0x63F450` -> `RoomGraph_FirstUse 0x63D2F0` -> `LotGraph_Build 0x63CB90`, and `0x82C420` -> `0x631A60` -> `LotTable_GetOrInsert 0x62EAB0` -> `LotTable_Insert 0x62E680`; 107 functions lie on some direct path. `0x62F8A0` (W3) is reached from the entry only through `0x63CB90`, which stops first. Paths through indirect calls were not enumerated [inferred: no frame of the 25 above can be on them, since none reaches the stops].
- So the first frame handler the dispatcher reaches is `P4Guarded`'s `__except` (our code), and the stop lands there every time. Our own frames are cleaned by `RgDrop` in `P4RunClone` (gate and reader holds), the path list by `P4FreeList`, x87/MXCSR and the pools hook run after `P4Guarded`.
- **What the unwind skips (no destructors):** (1) the `+0x918` lock taken at `0x82C733` and the steering acquire in `0x82EDB0` (on a path via `0x631F10`): in a clone run both are the working copy's lock, which `PcAcquire` overwrites with a fresh unlocked `CRITICAL_SECTION` image and zero count at the next acquire (`p4clone.cpp`, the `+0x918` block) [verified source]; (2) counted portal references in game locals: `0x633FA0` holds a room record copy (`0x6223C0` at `0x633FF6`, released through `0x5BF1E0` at `0x63443F`/`0x63444B` after its call to `0x632E40` at `0x6343AB`, which can reach `0x62EAB0`), `LotPath_AddPortalNodes 0x62FBE0` holds a local portal vector across its `0x62ED40` calls (released at `0x63040F`) [inferred which ones are filled at the stop]; each leaked count pins one portal (0x80 bytes plus its `+0x5C` buffer) for the session, no crash; (3) EA heap buffers of locals (`LotPortalSearch 0x63EC40`'s portal-answer hash map, `LotPlan_Inner 0x63F6E0`, `Route_SearchAStar 0x81B2A0`, `RoutingJob_Init 0x661B50`): a few KB per stop. Bounded by the stop count, which is expected to be 0 (I-94-97).
- **Found on the way:** our crash logger `XcptLog` (ts3native.cpp, a vectored handler that logs the first 16 first-chance exceptions) does not skip `0xE0503401`, so each lazy stop uses one of the 16 crash-log slots and prints an exception line that looks like a crash. Fix: skip `0xE0503401` there (one condition, ~0.1 h). Third-party vectored handlers (S3SS, S3PP, DXVK) were not checked; the usual ones return `EXCEPTION_CONTINUE_SEARCH`.

**Effort for all three (hours, Claude with Rodney):** this study ~1.5; fixes ~0.6 (risk 1 tripwire ~0.5, `XcptLog` skip ~0.1); risks 2 and 3 need no other fix. Pass rule added to the `snap-p4` runs: the new tripwire 0, `lazy` stops (if any) without crash-log lines once the `XcptLog` fix is in.

**Built in 0.31.3 (counters v56, 2026-10-19, offline only; ledger docs/51 §102):**

- **H20 tripwire** (`p4VarTrip`, +0x2AF8): the `[RouteSnap]` slot shim of `AddPortalLocationVariation` (IC slot 5, `routesnap.cpp` `SnIcEnter`) calls `P4VarTrip` (p4.cpp) when `SnLive()`. Its argument 3 is the portal handle, not a pointer [verified, `0x74D432`-`0x74D46E`]: the IC checks the handle type (top byte `0x28`), resolves it through the script host `0x11D4BBC` slot `+0xCC` (`ScriptHandles_HandleToPtr 0x76A520`, under the handle lock) and the object's type test `+0xC` with id `0xF0464184` (`0x64E8B0` returns the portal or NULL), and passes that portal as argument 4 of `LotRouting_AddPortalVariation 0x637C30`. `P4VarTrip` does the same lookup (only while a clone run is live and a replay clone exists), then looks for the portal in every clone lot's `prefs` (fault-guarded: the run holds `g_rpUse`, but the consumer may free the clone after the run ends). A hit is counted and the first 20 are logged (`P4 H20 tripwire: ...`); the IC runs as before. The lookup is the IC's own first step, run once more on the same thread. `snap-audit-report.ts` prints `H20 tripwire: ... N (expected 0; non-zero = put D2 on that IC slot)`; `agent.js` reads it from v56.
- **`XcptLog` skips `0xE0503401`**: a lazy stop no longer uses one of the 16 first-chance slots or writes an exception line.
- Checks: selftest (the replay H20b run: no count outside a run, +1 through `SnIcEnter(5)` on a portal the clone holds during a run, no count for another portal or an unknown handle; the D4 lazy stops with `XcptLog` installed as a vectored handler leave its slot count unchanged, and fail that assert without the skip), `steerro-test` seeds 1 and 2 x 3000 ok (the same tripwire check on game portal objects), `hook-sites.ts --check` (422 addresses, 0 not in docs/68), `snap-audit-report.ts --self-check`.
- Effort: ~0.75 h (tripwire, logger skip, tests, docs).

### 21.9 Lazy Gtiles (0.31.0, counters v55)

Built 2026-10-19, offline only; not run in the game. Code: `native/ts3native/snap.cpp` (`SpBuildVersion`, `SpTilesFill`, `SpTilesAll`, `SpCut`, `SpAudit`), one call in `p4clone.cpp` (`PcCutFn`). No switch: it replaces the eager tile rebuild.

**Problem [verified, counters, §16.9-§16.10]:** the G3 cut cost on BP is about 2.8 ms/s without failed clone builds (gate 2 ms/s): ~0.8 ms per cut at ~3.3 cuts/s, with ~9.9 dirty Gtiles rebuilt per cut. Most cuts have no reader: in a P3 / P3-B run (`Mode=0`) only the audit reads the version, and in a P4 run (`Mode=1`) only the armed START's cut (`PcCutFn`) hands a version to the clone; `SpStart`'s periodic cuts publish versions nobody takes.

**Design:**

- At a cut, a tile that is dirty (its bit, `SPD_TILES`, `SPD_G`, epoch or a new shape) is not rebuilt. The new version does not carry it (null slot). A tile the old version carried and no mark has hit since is shared, as before. So every tile a version carries equals live at its cut (invariant S on what can be read), and an uncarried tile is never a stale image.
- The marks need no state of their own between cuts: "uncarried in the current version" is the pending set. A tile stays uncarried over unread cuts until a reader builds it once, however many marks it collected.
- Readers: (1) `PcCutFn` calls `SpCut(world, true)`: every uncarried tile is built inside the cut (the cut's own time, `spTileFill[0]`), and the cut is refused (`snPcRefused[1]`) if a tile is still missing after a fault (`SpTilesAll`), because `PcHighCost` copies every tile and skips null ones. (2) `SpAudit`, always called right after its cut under the same `+0x918` hold, first builds the uncarried tiles (`spTileFill[1]`, audit time) and then checks every tile, as before.
- A build fault leaves the tile uncarried (before: the old, possibly stale image was kept).
- Not done: building pending tiles in batches off the sim thread. A tile has to be read while live equals the cut, which only holds under the cut's `+0x918` hold on the sim thread; a worker copy outside it would need its own change signal per cell (inferred, not designed further).

**Counters v55** (`+0x2AE4..0x2AF7`, docs/registry.md): `spTileFill[2]` (tiles built after a cut left them out: 0 for a reader, 1 for the audit), `spTileFillUs[2]` (their time; `[0]` is inside `spCutUs`, `[1]` in neither `spCutUs` nor `spAuditUs`), `spTileOut` (tiles the current version does not carry, gauge). `spCopies[SP_GHC]` still counts every tile built.

**G3 cut cost, meaning from v55:** the formula is unchanged (`spCutUs` per second, gate 2 ms/s), but from v55 it holds the cut work plus the tiles a reader needs, and no longer the tiles only the audit needs. `snap-audit-report.ts` adds those to the audit cost and prints a `Lazy tiles:` line (reader and audit builds, ms/s, µs per tile, tiles not carried now). In a `Mode=0` run the gate therefore measures a cut with no reader; a P4 run (`snap-p4`) measures the real armed share. Pre-v55 dumps report as before.

**Checks [offline]:**

- `build.cmd` selftest: K1 (3 seeds x 300), K2, K5 as before, plus LAZY (seed 5, 400 writes, a cut after every write): a reader every 8 cuts builds fewer tiles than eager cuts; with a reader at every cut the tiles built equal the dirty tiles exactly; every cut: each carried tile equals a fresh build from live, a cut without a reader builds no tile; reader cuts: every tile carried, audit 0 mismatches, version == full rebuild. `selftest ok`.
- `steerro-test` new section SNAP LAZY, game rasteriser `WaterGrid_MarkB 0x670750` for the high-cost writes, 3000 cuts, a reader every 16 cuts: seed 1: 1,404 tiles built (0.47 per cut, 7.5 per reader) against 2,864 eager (0.95 per cut); seed 2: 1,453 against 3,006. Carried tiles == live at every cut, audits 0 mismatches. With a reader at every cut: built == dirty (2,864 / 3,006). The fake G has only 9 tiles, so a reader rebuilds at most 9; BP has 72. Other sections pass (`steerro-test ok`, seeds 1-2 x 3000).
- **Fake fix found by this test [verified, docs/100 §12.1]:** the fake's G build (case 8 in `snap-fake.h`) kept its polygon list, so a later remove of a polygon stamped before the build still cleared its cells, which the game never does (the build frees the records; a missing record writes nothing). With a key reused after the build, the snapshot's record held only the new polygon's box and missed those cells (seed 1 step 2122, seed 2 step 2201, also with a reader at every cut, so not a lazy-tile fault). The fake now forgets its polygons at a G build. Not a live gap: the game's remove of such a key clears only the new polygon's record (I-94-175).
- `node tools/hook-sites.ts --check` (0 not in docs/68, no new site), `snap-audit-report.ts --self-check` (v55 case: audit-only builds go to the audit cost), `node tools/check-docs-code.ts`.

**Expected cut cost [inferred, I-94-173, I-94-174]:** from §16.8 the cut went from 0.50 ms (3.3 tiles per cut) to 0.80 ms (9.5 tiles), about 50 µs per 64-cell tile (copy of 10 strided 64-byte rows per map plus the byte-wise FNV hash of 40 KB). With 9.9 tiles that is ~0.5 ms of the 0.83 ms cut, leaving ~0.35 ms, so a `Mode=0` BP run should show about 1.1-1.2 ms/s cut cost (G3 PASS) plus the replay's own cut work if `Lots=replay` (not split out here). In `Mode=1` the armed cuts pay the union of the tiles dirtied since the last reader, at most 72 x ~50 µs; with ShadowEvery=1 on BP's async START rate that is likely the larger share, so G3 there depends on the armed rate (`spTileFillUs[0]` gives it directly). The audit cuts pay more (union of `AuditEvery` cuts' tiles), counted as audit cost.

**Run:** none of its own. The next `bp-snap-p3b` / `snap-p4` jobs on 0.31.0 read the `Lazy tiles:` line and G3 cut cost. asi: `native/ts3native/out/ts3native.asi` of the worktree build, sha256 `fef3bd0312157f55fe40ac01ff95be3f5898d79136d8821c0ca95f43e9201706`.

**Effort (hours, Claude with Rodney):** design and code ~0.5, tests and the fake fix ~1, report and docs ~0.5: ~2 h. Game runs: rides on the next P3-B / P4 jobs (10 min each).

### 21.10 P4 clone-run fault at `0x651374` (live 0.30.1, fixed in 0.31.2)

> **Changes 2026-10-19:** new subsection. Logs and static reads only (capstone 5 on `game-copy/bin/TS3W.exe`, Steam 1.67, raw `E8` caller scans); the game was not run. Ledger: docs/51 §101 (I-94-196, I-94-197). Later the same day (0.31.3): I-94-197 checked statically and verified (no growth possible), docs/51 §102.

**Live result [verified, logs].** Both `snap-p4` jobs on 0.30.1 (BP `2259`, IP `2311`, 2026-10-18 queue) logged `P4 FAULT in a clone run: exception C0000005 at 0x00651374 (plan 2, kind 0); P4 off for this session` on the first plan that reached a clone run (IP plan 1 was refused `mat`, before any clone run). The BP record has start == goal; the IP record does not (start (862.3, 51.7, 1075.3), goal (723.9, 28.5, 886.0)), so start == goal is not the cause.

**Cause [verified static; the alignment of the faulting block inferred].** `0x651350` is the 64-byte record copy loop; its first source read is `movaps xmm0, [ecx]` at `0x651374`, which needs a 16-aligned source. In a clone run its caller chain is `Route_PlanLocked` → `RoutingJob_ctor 0x819D50` → vector assignment `0x758760` (called at `0x819DDE` for job `+0x18` from the starts argument and at `0x819DEA` for job `+0x28` from the goals argument) → allocate-and-copy `0x7564D0` (allocates with alignment `0x10`) → `0x651350`. The source is P4's private copy of the live starts/goals vectors (`P4CopyVec` → `P4Alloc`), which came from `HeapAlloc` on P4's private heap: 8-byte alignment on x86. A block at 8 mod 16 makes the `movaps` raise a general-protection fault, which Windows reports as `C0000005`. The live run never faults because the game's own vectors come from its allocator with alignment 16. The source, not the destination, is ours, so this is P4's capture code, not the clone.

**Not the I-94-193 clone tree links [verified static].** The fault is in the job constructor, before the search reads any tree, and the vectors are P4's capture buffers, which the replay clone never touches.

**Fix (0.31.2, counters unchanged at v55).** `P4Alloc` allocates 15 bytes more, keeps the raw block in `bufs[]` for `P4JobFree` and returns it rounded up to 16. All P4 capture buffers (starts, goals, the five option vectors) are now 16-aligned.

**Checks [offline].**

- selftest `TestP4`: (1) `P4Alloc` for 25 sizes 0..192 returns 16-aligned blocks; (2) the fake planner's clone branch reads every start and goal record with `_mm_load_ps` (the `movaps` of `0x651350`) and asserts alignment. Before the fix the selftest stops at that assert on the first clone run (reproduced); after it `selftest ok`.
- `steerro-test` seeds 1 and 2 x 3000: `steerro-test ok`, no `K3 WARNING`.
- `node tools/hook-sites.ts --check` (422 addresses, 0 not in docs/68), `node tools/harness/shadow-compare-report.ts --self-check` ok, `node tools/check-docs-code.ts`.

**Can game code grow P4's copies? No [verified static, 2026-10-19, I-94-197; docs/51 §102].** P4's copies have capacity == end, so a growth would EA-free our heap block; nothing in a clone run can reach them:

- `Route_PlanLocked 0x82C6E0` reads its start and goal arguments only for the two emptiness checks (`0x82C708`-`0x82C71F`) and passes them to `RoutingJob_ctor 0x819D50` (pushed at `0x82C79B`/`0x82C79C`); it keeps no other copy of the pointers. After the constructor it calls only the search `0x81B2A0` and the job destructor `0x819AE0` on the job.
- The constructor uses them only as the sources of its two vector assignments `0x758760` (`0x819DDE`, `0x819DEA`), which read the source's begin and end and write only the destination (job `+0x18`/`+0x28`). The job stores no pointer to them: its stored arguments are the out list (`+0xC`), argument 2 (`+0x14`), the two out pointers (`+0x38`/`+0x3C`), the agent (`+0xC0`) and the guid (`+0xC8`/`+0xCC`). The options object (argument 6) only feeds `0x81D440` → `0x81D090`, which reads the five source vectors and inserts into the job's own. So P4's five option vector copies are read-only too.
- The goal step `0x75C2D0` (only direct caller `Route_PlanCommon 0x7606E0`, at `0x760BFA`) reserves (`0x758700`) its arguments 3 and 4, vectors of `Route_PlanCommon`, before that function's own `Route_PlanLocked` call at `0x760DE1`. These are the live, game-allocated vectors, on the sim thread, before P4's hook copies them at the `Route_PlanLocked` entry. A clone run enters at `Route_PlanLocked` (`g_planOrig`) and never runs the goal step. The other two callers of `Route_PlanLocked` (`0xC7C6E3`, `0xC7C9D5`) also hand over their vectors at the hooked entry.
- No fix: `P4Alloc` keeps capacity == end. Paths through indirect calls were not walked; none can reach the copies, since no pointer to them is ever stored.

**Run:** the next `snap-p4` BP and IP jobs on 0.31.2 (10 min each). Expected: no `P4 FAULT` line and shadow records past plan 2.

**Effort (hours, Claude with Rodney):** logs and disassembly ~0.5, fix and selftest ~0.25, tests and docs ~0.5: ~1.25 h.

### 21.11 `bp-snap-p4` on 0.31.2 crashed at `0x63576A` (fixed in 0.31.4, counters v57)

> **Changes 2026-10-19:** new subsection. Logs, EA's crash report and static reads only (capstone 5 on `game-copy/bin/TS3W.exe`, Steam 1.67; a map file of an identical 0.31.2 rebuild, `.text` byte-equal to `game-copy/ts3native.asi.snap0312`, sha256 `a3260bd7…`); the game was not run. Ledger: docs/51 §104 (I-94-200 to I-94-206). Crash row: docs/67 §6.

**Live result [verified, logs].** Job `bp-snap-p4`, ts3native 0.31.2, 2026-10-19 08:16-08:21 PHT (log `snap-p4-2026-10-19-bp-snap-p4-0016-ts3native.log`, report `xcpt <pc> 26-10-19 08.21.02.txt`). The game died after 5.1 min with `ACCESS_VIOLATION reading 0x00000007 at 0x0063576A`. Before that:

- **P4:** 83 plans got a clone run. Plan 84 faulted at `0x7CEF3688` (eip in heap, not in any module) and turned P4 off. There was no `0x651374` fault: that line comes from the 0.31.1 log of the queue before (`…-2344-ts3native.log`, first line `0.31.1 07:44`). So the §21.10 alignment fix works.
- **Clone:** 36 builds in 5 min, 1 divergence (live map 6 entries ahead).
- **First-chance AVs:** 11 inside ts3native at load (below). They used up the 16 first-chance log lines, so neither the P4 fault nor the crash got a line of its own.

**Cause 1, the crash: the game's lot-0 footprint remove walks past its range [mechanism verified; what set it up inferred].**

- **Path [verified, report stack and our map file]:** the managed `RouteManager.RemoveDiscourageFootprint` IC (our count stub `SnIc19`) → `DiscRemoveHook` (after its `DiscDrain`) → `RoutingWorld_RemoveDiscourageFootprint 0x834860` → `LotRouting_RemoveObjectFootprints 0x635700` with lot 0 (call at `0x83495F`, return `0x834964` on the stack) → fault.
- **Our detour at `0x635700` is not the cause [verified]:** the 7 stolen bytes are whole instructions with no relative operand. The stub only counts and records (`RpNoQNote`); the return slot still holds `0x834964`, and the loop ran several steps with the right arguments.
- **What the loop does:** with lot 0, the function walks map M's equal range for the object id. For each FME it first steps the iterator once for every pending remove of the same id and Q in `P+0x3C` (`0x635794..0x6357A8`). Then it asks `Q_FindFootprint 0x6649C0`, which answers 0 for an id whose remove is pending. On that 0 it steps again (`0x63580B`) without testing the range end.
- **How it runs off:** when the FME with the pending remove is the last one of the range, the first step lands on the end node and the second steps past it. The walk then goes through other ids' nodes to the bucket sentinel. `esi` becomes `0xFFFFFFFF`, which is not the end node, and `[esi+8]` reads `0x7`.
- **The report matches exactly:** `esi = 0xFFFFFFFF`; `edi = 0x9AFA2E9C` is the bucket array end (the range began at bucket `0x9AF9F600`, its end node `0x79B304D8` sat at bucket `0x9AF9F618`); `ebx` = P. The last FME handled (`0x7A490690`) had a Q.
- **Reproduced on the game's code:** `steerro-test` G2 builds one id with one FME, the next id's node after it, and that id's pending remove. The game's `0x635700` then faults reading `0x00000007` at `0x0063576A`.
- **What left the remove pending [inferred]:** a remove deferred by `LotQuadTree_Remove 0x666E30` while its Q was being iterated (`+0x48 > 0`) and not yet flushed when the lot-0 remove ran. Candidates: (a) the game's own window between a remove inside a query callback and the next touch of that Q; (b) the iteration counter raised by another thread, since `+0x48` is a plain unlocked counter (async job vs. sim); (c) Cause 3. The next run counts which (below).

**Cause 2, first-chance AVs inside ts3native [verified].** All 11 per load are `SnCloneProbe` (P1b's clone-cost probe on sim W1 hits; `SnScratch` → CRT memcpy) reading `0xBF000000`. The probe followed `[fp+0x38]` as a polygon cache for every footprint class. The box class `0xFEAE08` keeps box floats there (`-0.5f` = `0xBF000000`), and the 0x28-byte child class ends before `+0x38` (replay.cpp class table). `__try` caught them, so they were harmless, but they have happened since 0.29.0 and hid later exceptions from the log. Fix: follow `+0x38` only for classes with a cache, and only when level 0 is valid (`+0x20`).

**Cause 3, P4 fault at `0x7CEF3688` [not found; one hazard fixed].** No context was logged. Found by reading code:

- **The hazard [verified static]:** the replay clone's P is a byte copy of the live P. So its pending-insert and pending-remove maps (`P+0x30`, `P+0x3C`) and their node allocators are the live ones; P4's acquire copies the cut's P bytes again, apart from the map at `+0x24` and the lot table. `LotQuadTree_Insert 0x666C50` and `LotQuadTree_Remove 0x666E30` defer into `[Q+0x20]+0x30` / `+0x3C` when `Q+0x48 > 0`, and a later `LotQuadTree_FlushDeferred 0x666A40` on that Q erases nodes from those maps. On a clone Q in a clone run, all of this would happen on the worker, against the live maps, unlocked against the sim.
- **Not shown [inferred]:** that a clone run reaches an iterated clone Q. If one does, a freed and reused node handed to a footprint call fits an eip in heap. If it does not, the cause is still open.

**Fixes (0.31.4, counters v57).**

| Part | Change | Counter |
|---|---|---|
| Lot-0 remove guard (`routesnap.cpp` `SnLotRemovePending`) | in the `[RouteSnap]` stub at `0x635700`, before a lot-0 call: finds the id's pending removes in `P+0x3C` and flushes each of their Qs with the game's `0x666A40`. The game's own remove does the same whenever the Q is not iterated, and its leaves (W2) are recorded as usual. An iterated Q is left alone and counted (logged the first 8 times) | `lrPendFlush`, `lrPendBusy` |
| P4 deferral stop (`p4clone.cpp`) | entry detours on `0x666C50` and `0x666E30` (5-byte prologues, no branch into them). In a clone run on a Q with `+0x48 > 0`, the run stops as a D4 lazy stop (clone discarded, no fault) before the original writes the live maps. Every other call passes through | `pcDeferStop` |
| Probe | `SnCloneProbe` class and valid-level checks | (`snCloneFault`) |
| Fault context | `P4 FAULT context:` line with fault info, thread, registers and 12 stack code refs; `XcptRefs` is shared with `XcptLog` | |
| Rebuild cause | each `clone built` line names the request: `asked by replay.cpp:<line>` (`p4clone.cpp` for a P4 discard) | |

**`0x651374` audit [verified code read].** Every buffer a clone run hands to the game comes from `P4Alloc` (16-aligned since 0.31.2), `RpAlloc` (16-aligned header), `PcAlloc` (on `RpAlloc`) or the private pools' `PlBlockAlloc` (16-aligned bumps). P3 images are copied into those buffers, never handed over in place. No other source was found. The 0.31.2 run had no `0x651374` fault in 83 clone runs.

**Clone rebuilds (item 4) [open].** 36 builds against 1 divergence: the others come from other requests (world events 5, 9, `0x0A`, P4 discards, failed cut steps). 0.31.4 logs the requesting line; read it after the next run.

**Checks [offline].**

- `build.cmd` selftest `selftest ok`, including:
  - the SnCloneProbe cases: box class, child class and an invalid level give no fault and no copy past `0x44`;
  - `TestP4DeferStop`: an iterated clone Q stops the run with the lazy code, counted; live calls and non-iterated calls pass;
  - the v57 `static_assert`.
- `steerro-test` seeds 1 and 2 x 3000: `steerro-test ok`, no `K3 WARNING`. New section G2: the crash reproduced on the game's `0x635700` (reads `0x00000007` at `0x0063576A`); after `SnLotRemovePending` (flush swapped for a fake that does what the flush does to those maps) the call returns; an iterated Q is counted and the walk still faults.
- `node tools/hook-sites.ts --check` (424 addresses, 0 not in docs/68; new row), `snap-audit-report.ts --self-check`, `node --check tools/probe/agent.js`, `node tools/check-docs-code.ts`.

**Run:** `snap-p4` BP and IP on 0.31.4 (`minCounters` 57). Pass:

- no crash;
- `lrPendBusy` 0 and `pcDeferStop` 0, or a logged stop instead of a fault;
- a `P4 FAULT context` line if P4 faults again;
- the `asked by` causes of the clone builds.

`lrPendFlush` > 0 shows how often the crash precondition happens in a run.

**Not done.** The guard needs `[RouteSnap]` on: the game's own window (a) is not covered in other bundles. Not checked whether (b) happens; `lrPendBusy` will show it.

**Effort (hours, Claude with Rodney):** logs, map rebuild and disassembly ~1.5, fixes and tests ~1, docs ~0.5: ~3 h. One 10-min game run per world.

### 21.12 Who else can hit the `0x63576A` walk: the guard on every build (0.31.5)

> **Decision 2026-10-19 (Rodney):** keep `[LotRemoveGuard] Enabled=1` on by default as insurance against the game's own `0x63576A` walk, in the release build and in every test build from 0.31.5 on.

> **Changes 2026-10-19:** new subsection. Static only (capstone 5 on `game-copy/bin/TS3W.exe`, Steam 1.67; direct-call closures, no vtable or register-indirect calls), our source, offline tests; the game was not run. Ledger: docs/51 §105 (I-94-207 to I-94-211). Crash row: docs/67 §6.

The 0.31.4 guard (§21.11) lives in the `[RouteSnap]` stub, so builds without `[RouteSnap]` had none. This subsection asks whether those builds can reach the crash.

**When a remove stays pending [verified disassembly].**

- `LotQuadTree_Remove 0x666E30` and `LotQuadTree_Insert 0x666C50` queue into `P+0x3C` / `P+0x30` only while `Q+0x48 > 0`. Otherwise they flush (`0x666A40`) and apply at once.
- `Q+0x48` is raised by the six query wrappers (`0x6672E0` … `0x667560`; `+1` before the walk, `-1` after, plain `add`) and by the lot pin. `LotRouting_PinLotQuadTrees 0x631BF0` → `0x62FB90` → `0x666F70` pins every level of the lot. `LotRouting_UnpinLotQuadTrees 0x63C5B0` → `0x662CF0` unpins. `LotPlan_Inner 0x63F6E0` holds the pin for the whole lot plan. `0x627100` pins and unpins every Q in a row, which flushes all of them.
- **Ending an iteration flushes nothing.** A pending remove stays until the next pin, query, insert or remove on that Q while it is not iterated. The lot-0 walk in `0x635700` touches the Q only through `Q_FindFootprint 0x6649C0`, and that call does not flush. So a remove left pending from an earlier iteration is still there when a later lot-0 remove of the same id runs.
- **An insert cancels the remove.** A deferred insert erases the id's pending remove (`0x666D2C..0x666D55`, found by id). A deferred remove cancels the id's pending inserts on that Q. A remove followed by a re-insert of the same id therefore leaves no pending remove.

**Stock game [inferred: not shown reachable].**

- Inside the plan pin, the direct-call closure of `LotPlan_Inner` (8 hops) reaches only two removers:
  - the portal refresh `0x632460`: remove, re-flag, re-insert (docs/81);
  - `LotGraph_Build 0x63CB90`: a lot-specific `0x635700` at `0x63D02D`, then a re-insert of the new footprint at `0x63D133`.
- Both re-insert the id, which cancels the remove. Excluding those two and the unpin, the closure reaches no `0x637900`, `UpdateLotWalls 0x830940` or `0x635700` [verified direct calls].
- The unpin path `0x63C5B0` → `0x63AF30` → `0x639A80` → `0x637900` runs only after the lot counter `P+0xC8` is back to 0.
- **Not covered:** query visitors and other indirect calls; and the case where `LotGraph_Build` skips its re-insert (lot id 0, or `QuadTreeBuild 0x62F8A0` returning NULL at `0x63D102` / `0x63D12E`).
- So the double-step is a latent game bug (verified mechanism). No stock sequence was found that sets it up.

**Shipped build [verified ini; exposure inferred as stock].** `ts3native.ini` ships with `[RouteAsync]`, `[RouteGate]`, `[Discourage]` and `[RouteSnap]` all off, and `install-fixes.ts` does not override them. Plans therefore run on the sim thread as in the stock game, so the exposure is the stock one.

**Design D [inferred: does not create the state].**

- Before applying, `DiscDrain` and `DiscOnDestroy` wait for the running job (`WaitAsyncDone`). They block new job starts (`g_rgOffSimOpen`), hold `+0x918` and run at managed IC points, never inside a native quadtree query [verified source].
- With no job, nothing pins a lot. The drained adds and removes therefore apply directly and queue nothing (`Q+0x48 == 0` unless the counter was corrupted, see below).
- D reorders calls but never leaves a deferral in `P+0x3C`.
- D's own lot-0 calls would still fault on such a state if something else made it: the dead-id sweep (`DiscDeadSweep` → `0x635700`) and `0x834860` → `0x635700`. Those are the frames of the 0.31.2 crash.

**RouteAsync + RouteGate [inferred: possible only through a corrupted counter].**

- The worker pins the job's lot for the whole job. Its own removes are re-inserted as in the stock game. With `Enforce=1`, sim-side footprint writers wait for the job on `+0x918`.
- The remaining risk is a lost update on the plain `Q+0x48` (docs/83 §4). Any sim-side query of a Q that runs without `+0x918` while a job runs can leave the counter stuck above 0. After that, every remove on that Q is queued and never flushed.
- No such ungated query is known. The counter `lrPendBusy` shows it if it happens, and the guard cannot fix that case: it never flushes an iterated Q.

**What set up the 0.31.2 crash [open].** None of the paths above was shown to leave a remove pending. The P4 clone-run deferral (I-94-204) is still the only concrete candidate, and 0.31.4 stops it (`pcDeferStop`).

**Decision: (b), guard every build.**

- The precondition is the game's own (a deferral not yet flushed when a later lot-0 walk runs), so the fix goes into the always-on path.
- The guard is cheap. It reads the pending-remove map's size and returns when it is 0. On a hit it calls the game's own flush, which the game's `0x666E30` would run on that same Q a few instructions later if it got that far.

| Part | Change | Counter |
|---|---|---|
| `[LotRemoveGuard] Enabled=1` (default 1; `routesnap.cpp` `InstallLotRemoveGuard`, `LrGuardHook`) | entry detour on `0x635700` (7 bytes `83 EC 34 8B 44 24 38`); before a lot-0 call it runs `SnLotRemovePending`, then the original. Installed after `[RouteSnap]` and skipped when that stub is in (it already runs the guard). Covers the D frames (`DiscDeadSweep`, `0x834860`), the async worker and the stock callers | `lrPendFlush`, `lrPendBusy` (v57, unchanged) |

**Checks [offline].**

- `build.cmd`: `selftest ok`.
- `steerro-test` seeds 1 and 2 x 3000: `steerro-test ok`, no `K3 WARNING`.
- New case G2/D on the G2 state (X's last FME with its remove pending on Q1):
  1. a D Remove of the dead object X is deferred during a job;
  2. at `DiscPoll` the dead-id sweep calls `0x635700` (lot 0) through `LrGuardHook`;
  3. the hook flushes Q1, and the game's walk returns, where without the guard G2 reads `0x7` at `0x63576A`;
  4. the lock is balanced.
- `node tools/hook-sites.ts --check` (new docs/68 row); `node tools/check-docs-code.ts`.

**Not done.**

- No test of the full `0x834860` path with a footprint component; G2/D uses the sweep's own lot-0 call.
- A stuck `Q+0x48` (lost update) is still only counted.
- Indirect callers inside query visitors were not followed.

**Run:** any bundle on 0.31.5. The log line `OK   detour LotRouting_RemoveObjectFootprints (lot-0 guard)` should appear, or `inside the [RouteSnap] stub` when `[RouteSnap]` is on. `lrPendFlush` > 0 outside `[RouteSnap]` would show that the state also arises without P4. `lrPendBusy` should stay 0.

**Effort (hours, Claude):** static reads ~1, fix and test ~0.5, docs ~0.5: ~2 h. No game run needed for the fix itself.

### 21.13 P4's private world planner W' and the copy's G trees (0.32.0, counters v57, I-94-220 and I-94-221 fixed)

> **Changes 2026-10-19:** new subsection. Built in `native/ts3native/p4clone.cpp` (`PcSnap`, `PcWxGather`, `PcWp`, `PcWxApply`, `PcWpDrop`), selftest and `steerro-test` K6; ts3native **0.32.0**, counters v57 (no new counter). Static facts: capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67). The game was not run. Ledger: docs/51 §108 (I-94-225 to I-94-231, I-67-11).

**What changed.** The acquire now gives the working copy its own world planner: `[copy+0x960]` = W', so `RoutingJob_Execute` (W through `0x829C00` at `0x81B3E7`) and the hierarchical planner `0x65E0D0` run on the clone's P, the copy's R and G and a private container. G's two water-link trees are re-made node by node under the copy's anchors. Before 0.32.0 the copy kept the live W (§13.3 K6 finding 1).

**What W' holds** (W = `0x198` bytes: allocated with that size at `0x82998B`, ctor `0x657C50`, init `0x654060`, dtor `0x657A80` [verified]):

| W' field | Source | Evidence |
|---|---|---|
| `+0` vtable, `+0xC` `[world+0x964]` (live object, docs/99 §5) | the cut's W bytes, every acquire | verified (init `0x654060`) |
| `+4` reference count | `RP_BIAS` (no release deletes W'); nothing in the job path takes a reference on W (`0x829C00` returns it raw) | verified (`0x829C00..0x829C3D`) |
| `+8` P | the clone's P (raw, no reference) | verified (init stores arg 1; the planner loads it at `0x65F0EA` for `LotPlan_Inner`) |
| `+0x10` R, `+0x14` G | `copy+0x54`, `copy+0x8C` | verified (init `0x6540AD..0x6540B3`) |
| `+0x18` container | the live container's vtable, name and tuning (`+0x24..+0x6F`) from the cut; closed list built by the game's `0x6735C0` on the copy's R (again whenever P3's R image changes); its own node pool and free list (empty at first). Not the container ctor/dtor (`0x673C70`, `0x674010`): they register and unregister tuning variables by name through `0x5A00A0` | verified (`0x673C70`, `0x674010`; `0x6735C0` -> `0x673500` frees and reallocates, R at `+4`, `ret 8`) |
| `+0x88..+0x9F` | the cut's: R-usable byte, tuning floats, **the RNG word `+0x9C`** (seeded by `0x4FA800` from RDTSC) | verified (ctor `0x657C88`, `0x4FA800`); who reads `+0x9C` not followed (inferred: a plan-time random choice) |
| `+0xA0` lot-edge group hash | a private copy taken at the cut (buckets, chain order, end bucket value); its only writers are `ApplyLotUpdate 0x82B590` (`0x656120` at `0x82B703`) and `LoadLotData 0x82CA10` (`0x656150` at `0x82CBAA`) on the sim thread; the planner only finds in it (`0xD38B00` from `0x653214`, `0x65694F`, `0x6574EA`) | verified (xrefs; node key / value / next `+8`) |
| `+0xC0` "ObjectFootprints" hash, `+0xFC` "GoalPtSet" hash | empty as the ctor leaves them (buckets `0x114C900`, count 1); `+0xC0` is filled lazily per chunk from R (`0x6580F0`), `+0xFC` is cleared per plan (`0x65FBD6`) | verified layout; "`+0xC0` depends only on R's static fields" inferred |
| `+0xE0` FindClosest object | an empty list, then the game's own init `0x652C50` with W', the clone's P, `[world+0x964]` and the copy's R; `+0xEC` / `+0xF0` are set to that P and W' first, so the init takes no reference (`0x652C66`, `0x652C8E`) and appends its 10 blocks | verified |
| `+0x11C..+0x127` | the cut's (lot-exit masks, rebuilt per plan) | verified (ctor) |
| `+0x128..+0x197` (two BestLotEdgePortals sets) | vectors and per-plan words zero as the ctor leaves them, floats from the cut | verified (ctor, reset `0x657EA0`) |

The copy's G: `G+0x30` and `G+0x4C` hold 0x20-byte nodes (value at `+0x10`) in a balanced tree whose root's parent is the copy's anchor; anchor right / left / parent / size set as EASTL keeps them. Values are read in key order at the cut (the walk must end at the live anchor after exactly `size` nodes, or the cut is refused) and shared between cuts while unchanged. The copy is never inserted into, so colours are not kept [inferred: only a G build inserts].

**When W' is made and freed.** Made at the first acquire from the cut's W bytes, kept across acquires (the per-acquire fields above are set each time), made again when the live W differs (a world load) and dropped on a `DISCARD` release. Dropping uses the game's own clear functions for what the game allocated in it: FindClosest reset `0x654110`, the cache clears `0x657650` / `0x7F3790` (as the dtor calls them), `EA_Free` of the buckets and vectors, container clear `0x6741D0`; then our block. An exception while freeing leaks instead of freeing twice (`snPcFaults`). No live W, no W' (`[copy+0x960]` stays the cut's NULL).

**Checks [offline].**
- selftest (P4 part B on replay-fake.h's world with a fake W, group hash and two 3-node trees): after an acquire `[copy+0x960]` = W' != live W, its P / R / G / FindClosest pointers are the copy's, the RNG word is the cut's, every group-hash key is found in private nodes with its value, both copy trees walk in key order and end at the copy's anchor; W' is kept across acquires and made again for another live W. `selftest ok`.
- `steerro-test` K6, seeds 1-2 and 2-3 x 3000: K6 now uses the DLL's path, not its own copy: `PcSnap` (what `PcCutFn` takes at the START) and `PcAcquire` on a lot-less replay clone, then W' from `0x829C00` on the copy. Live W, R's table and records, G's tree nodes, the group hash and the blockedNodeMap sit on pages set to `PAGE_NOACCESS` during the acquire and every copy run; any read or write of them fails K6 (`K6 FAIL`). Result: 1,664 / 1,664 plans equal, 576 / 576 lattice ties equal, K4 741 / 634 copy plans unchanged by uncut writes, 26 acquires each returning W', 52 copy trees ending at the copy's anchor (`0x4E0B20`), 2,981 / 2,874 guarded copy runs with no access. No `K3 WARNING` and no `K6 WARNING` line (the warnings are now failures).
- `node tools/hook-sites.ts --check`: 0 missing (docs/68 row for W', calls only); `node tools/check-docs-code.ts`.

**Crash F and I-94-220.** I-94-220 gives a direct path to crash F's stack (docs/67 §8). In a clone run the planner calls `0x6410B0` with `ecx` = `[W+8]` (`0x65F0EA` -> `0x65F11C`), which runs `LotPlan_Inner 0x63F6E0` (`0x6410FF`) -> `InRoomPlan 0x632E40` (`0x640754`, return `0x640759`) -> the in-room A\* `0x61E500` (`0x633594`, return `0x633599`) [verified call chain]. Crash F's frames are exactly `0x61E6E0`, `0x633599`, `0x640759`. Before 0.32.0 `[W+8]` in a clone run was the **live** P, so the worker ran the in-room A\* on the live P's `cAStarSearch` (`P+0xBC`: its open list and node map) without `+0x918`, while the sim thread could be planning on the same object. A node that one thread frees while the other still holds it in the open list is crash F's "node on a free list". So I-94-220 is a stronger candidate than the two pool pushes 0.31.5 sinks: the sink stops a foreign push, not two threads sharing one open list [inferred: what the worker ran at the crash was not recorded]. 0.32.0 removes that path: `[W+8]` is the clone's P, whose `+0xBC` is the private A\* object.

**Run.** Rodney parks the build: copy `native/ts3native/out/ts3native.asi` of this build to `game-copy/ts3native.asi.snap0320`, point `snap-p4.json` there and run BP then IP, 2x each. Pass: no crash, `snShadowFault` 0, tripwires 0, `snPcAcquired` > 0. If crash F does not come back, I-67-11 holds.

**Risks.**
1. `+0xC0` filled from the copy's R at another time than live's: if it depends on blocked bits it can differ from live (a shadow mismatch, not a crash) [inferred].
2. The RNG word `+0x9C` is copied at the cut: if live plans move it before the live plan for the same job draws, live and copy draw different numbers (shadow mismatch, not a crash) [inferred].
3. Other self-linked containers in the copied world bytes (empty trees or lists whose anchors point at the live world) are still byte copies; the world A\* reads only the two trees (I-94-222), the rest of the planner was not swept for them (I-94-231). **Swept in §21.13.1 (0.32.3):** four (c) containers found (G's highCostFootprints tree, P's hanging-footprint index, and our own grid and blockedNodeMap memory under the game's free), all reached only through the H20c-refused unpin path; fixed by a lazy stop at the world-level writer leaves and an empty copy tree.
4. Game allocator calls from the worker (FindClosest blocks, closed list, node pool growth), as the clone run already makes for its A\* object [inferred thread-safe].

**Effort (hours, Claude):** static reads of W, its ctor/dtor/init, the group-hash writers and the crash F chain ~1.25; code ~0.75; selftest and K6 on the DLL's acquire with guard pages ~0.75; docs ~0.5: **~3.25 h**. Left: the `snap-p4` runs on 0.32.0 (BP, IP; ~13 min each).

**Crash G on 0.32.0 (2026-10-19 10:10 PHT, fixed in 0.32.2; docs/67 §9, ledger docs/51 §110).** `bp-snap-p4` crashed 2 min after load. Plan 3, the first clone run with a real search, faulted on the worker inside EA's heap free, and in the same second the main thread faulted in an empty hash's lookup on the shared empty bucket array `0x114C900`.
- **Cause [mechanism verified offline on the game's code]:** the table above says W' caches start "empty as the ctor leaves them", but `PcWp` kept the live next-resize words `+0xD8` (`+0xC0` hash) and `+0x114` (`+0xFC` hash) from the cut's W bytes. The insert `0x657800` rehashes only when the new size passes that word (`0x4E09B0`). With the live value the first W' insert skipped the rehash and linked its node into bucket 0 of `0x114C900`, which every empty EASTL hash in the game shares.
- **Second finding [verified code path]:** the DISCARD / WRAP teardown `PcAstarDrop` ran the private A\* dtor after the run left the pool job, so its private nodes went to the sink (the 8 `leaked to the sink` lines of thread 40660).
- **0.32.2:** W' starts with both words 0; `PcAstarDrop` frees as P4 pool job work; `RpQKey` checks readability first (the replay consumer's 8 caught AVs on a freed Q).
- **Tests:** K6 inserts through the game's `0x657BC0` into W'+0xC0 after every acquire (a live W with grown caches) and drops W' on another thread; on 0.32.0 code K6 fails at the first insert. selftest `PcCheckWp` and `TestPools` cover the two fixes.
- **What K6 did not cover before:** it runs the world A\* `0x676260` only, not the planner `0x65E0D0` that fills the caches, and its fake live W had both words 0. I-94-231 (other copied containers in W' and the planner above the world A\*) stays open.

#### 21.13.1 Sweep of the byte-copied containers (I-94-231, 0.32.3)

> **Changes 2026-10-19:** new subsection. Static sweep on `game-copy/bin/TS3W.exe` (Steam 1.67, capstone 5), fixes in `routesnap.cpp` (`SnEnter`) and `p4clone.cpp` (`PcWxApply`), K6 extended in `steerro-test.cpp`; ts3native **0.32.3**, counters v57 (no new counter). The game was not run. Ledger: docs/51 §113.

**Method.** A clone run enters at `Route_PlanLocked 0x82C6E0` on the working copy (P4's `P4Guarded`) and reaches `RoutingJob_Execute 0x81B2A0`, `PathPlan_WithRetries 0x661B50` and the planner `0x65E0D0`. Closure: direct calls and tail jumps from `0x82C6E0`, stopped at `Lock_Acquire/Release`, EA alloc/free, `Job_Release 0x59A990`, `LotGraph_Build 0x63CB90` (D4 stops a clone run before it) and `ApplyPendingUpdates 0x82C420` (gated on `P+0x10E`, which the cut clears in `PcPrepare` before the P bytes are taken) = **1,107 functions**; with every resolved target of docs/data/routing-indirect-sites.csv added (an upper bound, like docs/101 D+V) = **2,065**. The container fields come from the constructors: world `0x82F910` (0xA00), G `0x671E30`, W `0x657C50` / dtor `0x657A80`, P `0x62E860`; each container is named by its allocator tag. Each byte-copied container was then checked for (1) any instruction in the closure with its displacement, (2) any call in the closure with its address as `this` (`lea` scan), (3) its writer functions and whether they are in the closure. Classes: **(a)** private to the copy, **(b)** shared and only read (or not reached) in a clone run, **(c)** a live or shared container the clone run can write, grow or free.

| Object, field | What it is | Class | Evidence |
|---|---|---|---|
| world `+0x00..+0x53`, `+0x968..+0x983` | tuning floats, flag bytes | a (bytes) | ctor; closure reads `+0x96C`, `+0x974..+0x97E`, `+0x980`, `+0x988/+0x98C` only [verified scan] |
| world `+0x54` R | table and records from the P3 image; the rest of R (component data) byte copy | a / b | P3 image (§14); no write to R's other fields in the closure [inferred] |
| world `+0x878`, `+0x898`, `+0x8B8`, `+0x8E8` hashes; `+0x8D8`, `+0x908` vectors; `+0x948..+0x958`; object `+0x990` | PersistentLotRoutingData, mLotDirtyStateMap, FootprintsBeingEdited (the lock-key IC's map, docs/101 §6), TerrainPaintBrush..., TerrainTypeTraversalCost, mTerrainTypeNameMap | b (not reached) | no instruction with these displacements in either closure [verified scan]; writers `0x829D50`, `0x82B590`, `0x82CA10` not in the closure [verified] |
| world `+0x918` lock, `+0x95C` P, `+0x960` W' | | a | §21.1, §21.13 |
| world `+0x964` (= W `+0xC`, FindClosest `+0xF4`) | live world-manager-like object (world origin `+0xB4/+0xB8`) | b | read by `0x66D850`, `0x656550`, `0x6580F0`; `Route_PlanLocked` hands it to the job ctor `0x819D50`, which stores it raw (`0x819D61`, no AddRef) [verified]; no write found [inferred] |
| G `+0x1C` cells, `+0x39C` body table, `+0x7B0` passability maps | load-time state | b | written only by a G build (`0x6734A0`, `0x82A030`: not in the closure [verified]) |
| G `+0x2C`, `+0x48` shoreline trees | | a | re-made per acquire (0.32.0) |
| G `+0x64` lotInternalShorelineMaps tree | byte copy: anchor points at live nodes | b (not reached) | no reader in the closure [verified scan of the water-grid functions]; the copy must re-make it if a reader joins |
| G `+0x80`, `+0x174`, `+0x268` fixed vectors | byte copies pointing into the **live** world's inline buffers | b (not reached) | read by the G builder `0x672480` only (docs/100 §2), not in the closure [verified] |
| G `+0x35C` blockedNodeMap | private buckets and nodes (RpAlloc), next-resize word `+0x374` from live | a, **c for our memory** | erase `0x66F6D0` (from `0x62D9F0`) EA-frees a node out of our RpAlloc block; insert `0x636BF0` may rehash and EA-free our buckets [verified disassembly] |
| G `+0x79C` | the **live** P | b (not reached) | no `+0x79C` access in the closure [verified scan] |
| G `+0x7C0` high-cost maps | | a | §21.1 |
| G `+0x7D0` highCostFootprints tree | byte copy: the anchor's root is a **live** node | **c** | its only closure reader `0x670510` finds the (owner, footprint) node through `0x66F390`, clears its cells, unlinks it with the game's `0x4E0E20` and EA-frees the node and its cell vector [verified]: on the copy that is a live node freed by the worker |
| W' `+0` vtable, allocator tags, container name | static | b | ctor |
| W' `+4`, `+8`, `+0x10`, `+0x14`, `+0x18`, `+0x88..+0x9F`, `+0xC0`, `+0xE0`, `+0xFC`, `+0x11C..+0x197` | | a | §21.13 (0.32.0, 0.32.2) |
| W' `+0xA0` group hash | private (RpAlloc) | a | writers `0x656120` / `0x656150` only from `ApplyLotUpdate 0x82B590` / `LoadLotData 0x82CA10`, not in the closure; its clear `0x7D3AF0` is in the closure only on other objects (`0x62B930`, locals of `0x63E340`, `0x63EC40`) [verified call sites]; readers `0x653200` |
| P `+0x24` map, `+0x48` mLotRoutingDataMap, `+0xBC` mpSearch | | a | §21.1 |
| P `+0x30`, `+0x3C` pending insert / remove maps | live | c, fixed in 0.31.4 | deferral stop (§21.11) |
| P `+0x68` mLevelDefaultsMap | byte copy: live nodes | b (not reached) | read by `QuadTreeBuild` inside `LotGraph_Build` (D4 stop); no `+0x68` container call in the closure [verified `lea` scan] |
| P `+0xCC` string tagged "Routing/PathPlanner"; `+0x88..+0xA4`, `+0xC0..+0xC8` | byte copies | b [inferred] | no container call on them in the closure [verified `lea` scan]; what the words hold not followed |
| P `+0xDC` mHangingFootprintMapGrid | private deep copy (RpAlloc records) | a, **c for our memory** | `0x62D9F0` erases records (`0x62C1C0`, EA free), `0x636BF0` inserts [verified] |
| P `+0xEC` mHangingFootprintIndexLookup | byte copy: **live** nodes | **c** | `0x62D9F0` finds its key (`0x6643E0`) and erases the entries (`0xA7E3B0`, EA free); `0x636BF0` inserts [verified calls; `ebx` = `P+0xEC` from `0x62DA2D`] |

**How a clone run could reach the (c) rows.** `0x62D9F0` and `0x670510` are in the closure through one chain only: `LotPlan_Inner` → unpin `0x63AF30` → `0x639A80` → `0x635EE0` (once per 8-byte id in the lot graph's pending removes `+0x64`) → `LotRouting_RemoveObjectFootprints 0x635700` → `0x62D9F0` → `0x670510` [verified chain, both closures]. The acquire refuses a clone lot whose `+0x64` is not empty (H20c), and no closure function appends to `+0x64` (its filler `LotRouting_RemovePortals 0x6399E0` is not in either closure) [inferred]. So the four (c) rows were latent, not the cause of a seen crash. The writers `0x636BF0`, `0x6733C0`, `0x82B590`, `0x829D50` and the G builds are not in either closure.

**Fixes (0.32.3).**
1. **Lazy stop on the world-level writer leaves.** P1's stubs already sit at the entry of every one of them (`kSn`: `RemoveLot 0x829D50`, `ApplyLotUpdate 0x82B590`, `AddHangingFootprint 0x636BF0`, grid remove `0x62D9F0`, `MarkFootprintPolygon 0x6733C0`, water grid mark `0x670510`, the five G-build call sites), and P4 needs `[RouteSnap]`, which installs them. In a clone run `SnEnter` now calls `P4Lazy()` before the original runs (the D4 stop: the run ends as "lazy", the clone is discarded, P4 stays on; first 8 logged with the site name). This covers every (c) row above, ours and live, and any future path into them. No new hook site.
2. **The copy's highCostFootprints tree is empty.** `PcWxApply` self-links the copy's anchor `G+0x7D4` and sets the size `+0x7E4` to 0 at every acquire, so no byte-copied pointer into the live tree is left in the copy even if a stub is missing. Cost: a removal the clone run would apply is not applied to the copy's high-cost cells (a shadow difference, not a crash); with fix 1 such a run stops first anyway.

**Checks [offline].**
- `steerro-test` K6, seeds 1-2 and 2-3 x 3000, new step after every acquire, with the live pages `PAGE_NOACCESS`: the lot-exit masks `0x64FEA0` on W' with `RoutingWorld_Get` = the copy (masks and `+0x124` as the world bytes give them), the ObjectFootprints fill `0x6580F0` on W' for every chunk whose type-0x0E first-neighbour chains close (325 / 364 fills, 52 / 130 points, each list starting at a type-0x0E waypoint of the chunk with its chain length), the highCostFootprints erase `0x670510` on the copy's G with the key of a node the live tree holds (3 live nodes on guarded pages: the live tree keeps 3, the copy's stays empty), and `SnEnter` in a clone run on all 11 world-level leaf sites (each raises the lazy stop): no access, 286 stops per seed pair. **Negative check:** without fix 2 the erase reads the live node and K6 fails (`K6 FAIL (I-94-231): highCostFootprints erase 0x670510 on the working copy touched live memory`). The earlier K6 lines are unchanged (1,664 / 1,664 plans, 576 / 576 ties, 2,981 / 2,874 guarded runs); no `K3 WARNING` or `K6 WARNING` line.
- `build.cmd` selftest ok; `node tools/hook-sites.ts --check` 0 missing; `node tools/check-docs-code.ts`.

**Still not run offline.** The planner `0x65E0D0` itself and the parts of it that need the world manager, terrain or lots: lot lookups `0xC68990` and terrain heights `0xC6A540` / `0xC6A250` at its start, the closest waypoints `0x65BB90` (through `0x6595B0`: 306 functions with physics and lot queries), the lot legs (`LotPlan_Inner`, `LotSearch_Entry 0x640E00`), the BestLotEdgePortals sets (`0x65BC20`, `0x6552D0`), lot exits `0x654CC0` and the water path `0x671930`. The steerro fake world has no world manager (`[0x11ECBC4]` = 0) and no terrain; those are covered by the static sweep above and the live `snap-p4` runs.

**Run.** As for 0.32.2: park this build as `game-copy/ts3native.asi.snap0323`, point `snap-p4.json` at it, BP then IP 2x each. New pass item: `P4: ... in a clone run (byte-copied world containers): run stopped` lines should be 0 (each one is a path the sweep says is unreachable; the run is safe either way).

**Effort (hours, Claude):** closure and scans ~1.5, container layouts and writer reads ~0.75, fixes and K6 step ~1, docs ~0.5: **~3.75 h**. Left: the `snap-p4` runs (BP, IP; ~13 min each).

#### 21.13.2 Room lists in a clone run: crash H (0.32.7)

> **Changes 2026-10-19:** new subsection. Crash H (docs/67 §10, ledger docs/51 §114): `bp-snap-p4` on 0.32.4 faulted in EA's free at plan 5 and then hung. Fixes in `replay.cpp` (`RpRoomListsCopy`) and `p4.cpp` (`P4EaHeapUnlock`), steerro-test K7; ts3native **0.32.7**, counters v57. Not run.

The §21.13.1 sweep counted the clone's lot records as private (class a). Two lists of a room record are not, because the clone run writes them: `RoomGraph_FirstUse 0x63D2F0` builds any room whose built byte is 0 (never built, or reset by `0x630F70`), and the cut (`PcPrepare`) builds only each lot's outside room. The build empties `+0x24` (u16 portal indices) and pushes into it (`0x5D1D60`), frees each `+0x34` element's buffer (`0x61EE20`; `+0x34` is a vector of 16-byte vectors of 8-byte entries, I-94-133 refuted), resizes `+0x34` (`0x622A30`) and pushes into its elements (`0x6202E0`) [verified disassembly]. With the 0.29.0 flat copies the grow EA-freed our `RpAlloc` block (crash H) and the clear EA-freed the live room's inner buffers. Since 0.32.7 both levels are the clone's own EA blocks (class a again), freed with EA's free in `RpLotFree`. `LotGraph_Build` on a clone lot stays a D4 stop (gate site 3); the room portal vectors `+4` / `+0x14` stay `RpAlloc` copies, written only there (I-94-251). Also from crash H: a fault inside EA's alloc or free leaves its heap lock held after `P4Guarded`'s unwind; `P4EaHeapUnlock` releases it.

## 22. P5 status: answers taken, `Mode=2` (0.33.0, counters v61)

> **Changes 2026-10-19:** new section. Build only; the game was not run and no asi is parked in `game-copy/`. Code: `native/ts3native/p5.cpp` (new), `p4.cpp`, `ts3native.cpp`, `snap.cpp`, `selftest.cpp` (`TestP5`), `steerro-test.cpp`, `ts3native.ini`, `tools/probe/agent.js`, `tools/harness/snap-audit-report.ts`, `shadow-compare-report.ts` (record v3), `profile-world.ts` (switch `routesnap`), `queues/snap-p5.json` (new), `queues/snap-release.json` (`Kinds` 15, `minCounters` 61). Design inputs: §12.5, §12.6, §13, §17.4, §19, §20, §21. Ledger: docs/51 §117 (I-94-280 to I-94-288; ids start at 280 to stay clear of rows a parallel §116 may add).

### 22.1 What is built [verified offline]

Switch `[RouteSnap] Enabled=1 Mode=2` (default `Mode=0`; everything `Mode=1` needs, §18.1, §21). For an async `ReplanFromPosition` job (A10 kind 3; `[RouteSnap] Kinds` bit 8, `[RouteAsync] Kinds` bit 4) the game takes the clone run's answer instead of running its own plan. Every other plan runs as A10 does today; `Mode=2` runs no shadow.

| Part | What it does | Counters (v61) |
|---|---|---|
| Arm (sim, START) | `P5OnStart` from `P4OnStart`: only kind 3, only while `m2Take` (runtime switch, `--ab routesnap`) and `Mode=2` is on; M2-1 site check first; the L2 read once per world; every `VerifyEvery`-th armed job is a verify job; the provider cuts the version as in P4 | `m2Armed`, `m2SiteAt` |
| Take (worker, the job's `Route_PlanLocked`) | `P4LivePlan` captures the private arguments as in P4, then `P5Plan`: refusal checks, acquire, the clone run into a private list (`P4RunClone` with `keep`), the checks after the run, then the clone run's list **nodes are moved** into the route's out list `route+0xE0` (empty at entry, allocator words untouched) and `out1`/`out2`/the return value are the clone run's. These are the nodes `P4CaptureAnswerRaw` read for the §13 `copy` answer, so the taken answer is byte for byte what the shadow compare compares (selftest compares the §13 answer JSON of both) | `m2Plans`, `m2Taken`, `m2JobUs` |
| Door asks (§19.1 (b)) | in a `Mode=2` clone run the stub at `0x64F4D3` calls the game's `0x791C00` (9 dwords, answer in eax). The ask runs **disarmed** (§22.2): pools handed back, thread selection off, then re-armed. Its managed call goes to the sim thread through `HostCallHook`/`ForCaller` exactly as in any A10 job | `m2DoorAsk`, `m2DoorHist[5]`, `m2DoorWaitUs` |
| Verify | 1 job in `VerifyEvery` (default 10) runs the clone, then the game's own plan; the **game's answer is kept**; `P4Classify` classes the pair and one §13 record (v3, `"m2":1`) is written to `ts3native-shadow.jsonl`, so `shadow-compare-report.ts` applies the G4 classes to exactly what `Mode=2` would have taken. A verify M2/M3 is logged (`P5 verify: plan n Mk`, first 20) and counted | `m2Verify`, `m2VerM[6]`, `m2VerBad` |
| Fallbacks | §22.3: the game's own plan (`PlanLockedBody`) for that request, so no request is left without a route | `m2Fb[16]`, `m2FbList`, `m2FbNr` |
| Mod guard (§19.2) | M2-1: the first 5 code bytes of every site the clone run passes that we own (P4's five sites, the door call, `Route_PlanLocked`, the RFP collect detour `0x765550`, the deferral stops `0x666C50`/`0x666E30`, every installed `[RouteGate]`, `rtsites` and `[RouteSnap]` P1 site, the D5 writer stubs; up to 160, the count is `m2Sites`) are snapshotted after the last install in DllMain and compared at every armed START: a change means a hook installed after us (S3SS and S3PP install after DllMain, docs/68 §5) and turns `Mode=2` off for the session with `RouteSnap: Mode=2 off: site 0x... owned by unknown hook`. M2-2: 3 clone-run tripwires (managed call or world event outside an ask) turn it off. M2-3: MonoPatcher's L2 bytes (`0xD7E560`, vanilla `51`; `0xE5CA90`, `0xE59218`, `0xE5637D`, `0xE648DC`, docs/107 §2.2) compared once per world, one `ModGuard:` line per finding, no fallback | `m2Off`, `m2Sites`, `m2Trips`, `m2Guard` |
| Staleness measure | taken and verified runs whose cut had replay records after it (the world changed between START and the job) | `m2Stale`, `m2StaleRecs` |

### 22.2 Door asks: disarmed, not exempt (deviation from §19.1 item 2)

§19.1 planned a thread-local "door ask" flag that exempts the event `0x3B` and the managed call from the tripwires while the clone run stays armed. Built instead: the ask leaves the clone run for its duration (`P4Disarm`, `g_p4PoolsLeaveJob`), then re-arms (`P4Arm` without the D2 wait, `g_p4PoolsEnterJob`). Reasons [verified in our source]:

- A plan nested in the managed handler (`RunPlan` on the owner with `g_inInvoke`) is sent back to the worker, which serves it inside `Proxy`. Armed, that nested plan would read the working copy through T4 and trip the live-lock check on its own `+0x918` acquire; disarmed, it plans on the live world under the job's lock, as in A10.
- The private pools are keyed by the job thread (`g_plJobTid`): a nested live plan would pop private nodes into live structures (the E8 deferred flush) while armed.
- The tripwires stay armed for everything outside an ask, so M2-2 keeps its meaning without an exemption list.

The D2 handshake needs no pump: while an ask is open nothing is live (`SnLive()` false), so a D2 writer on the sim goes on to its job wait (`RtWaitJob` serves invokes) instead of spinning. The re-arm does not wait for writers in flight (I-94-284).

### 22.3 Fallbacks

Every row answers the request with the game's own plan, on the worker, under the job's `+0x918`, as A10 does today.

| Condition | Where | Reason (`m2Fb`) |
|---|---|---|
| no clone provider, no version, provider refusal (older cut, moved epoch or active lot, timeout, copy failed) | acquire | `clone`, `nover`, `epoch`, `mat` |
| argument capture failed, or the route's out list not empty at entry (the nodes go into an empty list only) | entry | `capture` (+ `m2FbList`) |
| `rtClosing` / `rtBusy` at entry, or the P3 epoch / `rtClosing` / `rtBusy` changed during the run (**stale cut**) | entry, after the run | `epoch` |
| cold statics | entry | `cold` |
| lazy stop (D4, world-level writer leaf) | clone run | `lazy` (copy discarded) |
| tripwire: managed call, world event, live `+0x918` acquire | clone run | `managed`, `event`, `livelock` (3 managed/event = M2-2: `Mode=2` off) |
| fault in the clone run | `__try` | `fault`: P4 and `Mode=2` off for the session (crash-cause rule) |
| a non-routing family the plan read changed in the window (`nr` ≠ 0, the M5 skew of §17.4 D5) | after the run | `m2FbNr` |
| M2-1 site changed | START | not armed: `Mode=2` off, the job runs as A10 |

Not a per-request fallback: a verify M2/M3 (the game's answer is already the one kept; logged and counted, judged by G5).

### 22.4 Not built, and why

1. **The live `+0x918` stays held through the clone run.** A10's `ServeAsync` holds it for the whole job; `P5Plan` runs inside it. So P5 as built removes the game's search from the job, not the job's lock. Dropping the lock around the clone run (§12.5's "no live `+0x918` held") is a separate step, **P5b**, blocked by A10's job-live invariant (I-94-287): `g_async.state == A_RUNNING` means "the worker holds the lock" to every closure. Keeping `A_RUNNING` while the lock is released lets the sim own `+0x918` inside a job, which `RtWaitJob` reads as "the worker is outside its job body" and lets destroys through while the worker's IC frame is suspended; setting `A_IDLE` instead lets every destroy, save and gate run with no wait at all under that frame. P5b needs: what the IC frame holds across `Route_PlanLocked` (start points, goal records, the Sim), docs/88 design G (§17.4 D6: park a reference instead of waiting), and a reacquire that cannot deadlock against a sim holder waiting for the job. Estimate in §22.8.
2. **L1** (the 10 EA image GUIDs) is not built: log only under (b) (§19.2) and it needs a managed class lookup; it comes with docs/104's guard for option (a).
3. **Staleness by lot.** The clone answers the world as of the START cut; routing writes between START and the job are not refused, only counted (`m2Stale`, I-94-285). A10 has the same kind of skew (START to collect); a sync plan at the IC call would have seen the START state.
4. **E1 and E2 of docs/93 §5** run only in the game's own plan: pending routing updates are applied at the cut (§21.1), later ones by the next sim plan; the one-byte E2 write lands on W' (§21.13) [inferred harmless, reader not traced].
5. **Kinds:** only bit 8 is taken; bits 1, 2, 4 are logged and ignored (P6 widens).
6. `profile-world.ts` gets the `routesnap` counter switch (`m2Take`, +0x2F74) the release queue's A/B jobs need (§20.4).

### 22.5 What only a live run can prove

- The moved list is owned and freed by the route like its own (route reset, replan, destroy): no fault, no leak (I-94-280).
- Paths built in a clone run hold no pointer into the working copy or our private argument buffers after release (I-94-281; §17.5 and K5 cover the routing fields).
- Door asks: the game's chain answers from the clone run as it does from A10's live search (Sims stop at locked doors, households pass, `LockedDoorsDuringPlan` filled), and the ask wait (`m2DoorWaitUs / m2JobUs`) against §19.1's rule for building (a) (I-94-288).
- Verify records: M2 = M3 = 0 and real M1 = 0 over enough jobs per world; goal ties as live F-7.
- No `RouteSnap: Mode=2 off` line on a vanilla install (M2-1 snapshot stable, no false site change).
- Coverage: `m2Taken / m2Armed`. The cut is refused while another async job runs (§21.1), so with jobs queued back to back many armed jobs fall back `nover` [inferred]; the run says how many.

### 22.6 Checks [offline]

- `build.cmd` selftest `TestP5`: v61 layout; a taken answer (no game plan, one door ask through the game's chain while disarmed, pools left and re-entered twice, the route's list holds the clone run's nodes and the §13 answer JSON of the list equals `g_p4Copy`'s, head allocator words untouched); verify jobs (game's answer kept, record v3 `"m2":1`, an M2 counted and Mode=2 kept on); fallbacks no provider, list not empty, `rtBusy`, stale cut (epoch moved during the run, private list released), `nr` 4, lazy stop, cold statics, three managed tripwires (then off, jobs plain A10), the `m2Take` switch, an M2-1 site change (off, address recorded), M2-3 bytes (log only, bits 0 and 2), a fault (P4 and Mode=2 off, the EA heap lock released, the request answered by the game). `TestP4` unchanged except the record version.
- `steerro-test` seeds 1 and 2 x 3000: ok; new line "P5: the 5 MonoPatcher L2 sites hold the vanilla bytes" on the Steam 1.67 image.
- `snap-audit-report.ts --self-check` (P5 lines and the P5 first-run verdict), `shadow-compare-report.ts --self-check` (v3 line, P5 verify line), `node --check tools/probe/agent.js`, `node tools/hook-sites.ts --check` (0 missing; docs/68 row for P5), `node tools/check-docs-code.ts`, `node tools/harness/queue.ts tools/harness/queues/snap-p5.json --dry-run` (2 jobs parse; the asi is not parked yet).

### 22.7 Run and the G5 plan

1. Park the build as `game-copy/ts3native.asi.snap0330`, then **one clean night**: `node tools/harness/queue.ts tools/harness/queues/snap-p5.json` (`bp-snap-p5`, `ip-snap-p5`, 10 min each, `Mode=2 Kinds=8 VerifyEvery=10`, `RouteAsync.Kinds=4`). It is not part of any running chain. After each job: `snap-audit-report.ts <row.nativeDump> --log <row.nativeLog>` (P5 first run PASS: Mode=2 on at the end, answers taken, verify M2/M3 0, no fault) and `shadow-compare-report.ts <row.shadowLog>` (P5 verify line). First-run pass also: no crash, no watchdog line, `rpMis` 0, stuck Sims logged only.
2. **G5** (§5, docs/116 §4) after that: in-load A/B with `--ab routesnap` (switch `m2Take`, ABBA slices, seed 12345, BP and IP, clean night, machine load line ok): it/s with take on ≥ off − 2%, sim keyed waits on ≤ off per site (`wait-ab.ts`), 0 new sim-thread waits, 0 audit mismatches, verify M2/M3 0. Expected [inferred]: about neutral, because the job still holds `+0x918` (§22.4 item 1); the timing gain is P5b's. The door rule of §19.1 (I-94-112) is read from the same runs.
3. Then P6 (§20) or P5b, whichever G5 points to.

### 22.8 Effort (hours, Claude with Rodney)

| Work | Hours |
|---|---|
| This build (p5.cpp, P4 hooks for take / keep / disarmed asks, counters v61, reports, switch, queue, selftest, steerro line, docs) | ~3 |
| `snap-p5` clean night + reading the two reports | ~0.5 + 1 clean night |
| Fix rounds after the first run | ~1-3 |
| G5 in-load A/B (BP, IP) + reading | ~0.5 + 1 clean night |
| P5b: release `+0x918` around the clone run (IC frame audit, design G of docs/88, a reacquire that cannot deadlock, selftests) | ~4-7 [inferred] + 1 clean night |
