# 94. Routing snapshot: change events, architecture and proof plan (I/O study part D)

Written 2026-10-18. Static only; the game was not run, no probe, no native code. Part D of the routing I/O study that PLAN "Routing off-thread" item 6 opened after the wait split (`ip-ar-ab-wait`: A10 moved 33 s of planning off the sim thread, and it came back as +49 ms/s of keyed waits on the game's shared routing data). Parts A-C (docs/91-93, written in parallel) map what the route search reads and writes; this doc covers how our own copy of that data stays in sync with the game, which shape the copy takes, and how we prove both.

Sources: docs/50, 59 §3, 60, 63, 66, 68, 76, 79, 80, 83, 84, 87-90 and `native/ts3native/steerro-test.cpp` (read). New static facts come from capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67): a raw `E8` caller index of `.text`, direct-call closures of 5 hops that flag `Lock_Acquire 0x4E16F0` with `0x918` in the 20 instructions before it, `World_FireEvent 0x78F2B0` and `GlobalHandlerChain 0x936C90`, absolute-pointer scans, and a store-displacement scan; plus grep of `game-copy/decompiled`. Scratch scripts stay in the session scratchpad. Ghidra was not needed.

Tags as in AGENTS.md: **[verified]** = read in the disassembly, decompile, source or a run file; **[inferred]** = reasoning, with a ledger row (docs/51 §27, I-94-*). "Direct closure" claims cover direct calls only; vtable and register-indirect calls are not followed.

## 1. Summary

- **Mirror the routing data, not the object world.** The search reads the game's routing structures (lot table, lot graphs, per-lot quadtrees, footprints, portals, lock-key map, world road graph; docs/60 §1.1, docs/83 §2). Those are written only by a small set of routing-world writer functions, and the game itself keeps them in step with objects through its own handlers (footprint component events `0x21`, `0x0E`, `9`, `0x0A`, docs/89 §2). So the sync point for our copy is **the routing-data writer layer**, not the object events of docs/76/79: whatever the game's routing data says, our copy says, bugs included (a carried grandchild whose routing footprint the game never updates is stale in both, which is what "same answer as the game" needs).
- **Every source is now placed (§2).** Footprint writes funnel into `RoutingWorld_UpdateObjectFootprintCore 0x8321F0` (7 call sites, 6 functions) plus the remove leaves `0x82A0C0`, `LotRouting_RemoveObjectFootprints 0x635700` and the lot rebuilds `0x834DC0`/`0x834DF0`/`0x834F00`/`0x8350C0` [verified callers]. The quadtree, lot table, lot graph and polygon writers are the W1-W6 leaves (docs/83 §6) that ts3native already stubs. World/lot lifecycle has `World_FireEvent` ids and the docs/60 §2.5 drain ICs.
- **Coverage gaps (§2.3):** (1) **portal and lock-key ICs** (`AddPortal`, `RemovePortals`, `Add*Portals`, `SetPortalCost`, `SetPortalAgeGenderFlags`, `SetPortalLockStatus`, `AddPortalLocationVariation`, `Footprint*LockKey`) take no `+0x918` and fire no event in their 5-hop direct closure [verified, direct calls]: they need IC binding-slot swaps (no code patch); (2) **door permission is per Sim and managed** (World event 59 callback inside the plan, docs/18): it cannot live in a world snapshot, it stays a per-request call to the sim thread; (3) **frozen ponds** (Seasons: `PathType.FrozenPondPath` exists in SimIFace) have no writer found: until traced, a temperature/season change bumps the epoch; (4) **lazy state**: pending lot updates (`P+0x10E`, applied inside `Route_PlanLocked`), deferred quadtree inserts (flushed at the next query) and first-touch lot graph builds happen inside a plan today; the snapshot must force them on the sim thread at the cut; (5) **houseboat lots** move with no event (docs/76 I-76-8): rebuilt on every cut while one sails; (6) **terrain cell shapes** are replaced on the main thread on LOD change (docs/38 §6), an existing race; heights are static outside build mode and the Meteor IC.
- **Recommended architecture (§3): H, sim-published immutable lot images plus a worker-owned working world.** The sim thread marks lots dirty from the writer-layer barrier, and at a route START (the existing `Route.Plan` handshake) copies only the dirty lots into new immutable images and publishes a version (array of lot-image pointers, refcounted). The worker turns images into its own mutable working copy, so lazy builds and A\* scratch never touch game memory. No job ever reads or locks the game's routing data, which is what removes the A10 failure mode. Memory cap 64 MB for images plus working copy [design], against a 1.34 GB largest free block after a BP session (docs/66).
- **The real fork is which search runs on the copy (§3.4):** G = the game's own search code over a layout-compatible clone (fast to build, but every global the search writes must become per-thread, from the write set of docs/91-93), or R = our own search over our own structures (no shared globals, 2-4x the effort). Decide at gate G2 from the measured global write set.
- **Proof (§4):** a cheap live read-set probe (Frida Stalker block coverage on the route worker only, plus sampled memory-operand callouts for image globals) checks the static maps; an incremental-equals-rebuild audit proves the sync live and offline; a shadow compare of our search against the game's on the same request proves answers; steerro-test-style offline sections run the game's own writers and search on mapped data.
- **Phases (§5):** P1 change counters (observe) 1 d, P2 read-set probe 1.5 d, P3 snapshot plus sync audit 4-5 d, P4 search shadow 5-7 d (G) or 12-20 d (R), P5 A/B 2 d, P6 widen and release 2-3 d. Total **~16-20 days (G)** or **~23-34 days (R)** of Claude-with-Rodney time, with a go/no-go gate after each phase.

## 2. Change events that alter routing inputs

### 2.1 Principle: two layers

| Layer | What it sees | Use |
|---|---|---|
| **Writer layer (sync source)** | every write to the routing structures, whatever caused it | marks the lot (or the world) dirty; nothing else |
| Event layer (attribution) | object events (docs/76, docs/79), `World_FireEvent` ids, IC slots, drain ICs | epochs (world load, travel, build/buy exit), per-source rates, and the audit's "which change preceded a mismatch" |

Why the writer layer: every object-side path found in docs/76/79/89 ends in the same few routing writers, and some paths have no object event at all (the footprint enabled bit `0x947590`, docs/76 I-76-9; discourage footprints; build tools). Hooking the writers covers them all without a per-source list [inferred, I-94-1].

### 2.2 Source table

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
| Build effort | 5-7 d after P3 (materialiser + rebinding the world/planner pointers the search reads) | 12-20 d (lot-level search, portal edges, world A\* over the road graph, path smoothing, result encoding) |
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

Logging: counters `snapShadow`, `snapM[6]`, `snapShadowUs`; a JSONL record per M2/M3 with the request (start, goals, options, Sim id, lots), version id and the dirty marks since the cut, so the case can be replayed offline from a version dump (`[RouteSnap] DumpOnMismatch=1`, capped at 20 per run).

### 4.4 Offline harness (steerro-test style)

New section K in `native/ts3native/steerro-test.cpp` (mapped TS3W.exe, random lots, lot tables, quadtrees and footprints with the game's vtables, as sections A and E already build):

| Case | What runs | Pass |
|---|---|---|
| K1 sync | random sequences of the game's own writers on the mapped data (`0x8321F0`, `0x833560`, `0x831BA0`, `0x82A0C0`, `LotRouting_AddPortal 0x6379E0`, `RemovePortals 0x6399E0`, `LotGraph_Build`, `LotTable_Insert`, flush `0x666A40`) through the installed leaf stubs; cut after random steps | incremental version == fresh full build, every cut (invariant S); seeds 1-4 x 3000 |
| K2 cut order | a pending update and a deferred insert left open at the cut | images equal to a build after the game's own `ApplyPendingUpdates` and flush |
| K3 search (G) | the game's lot search and world A\* on the game's data vs the same code on the worker clone | M0 for every case |
| K3 search (R) | the game's search vs ours on the same data | M3 = 0, M2 classes listed |
| K4 concurrency | sim-thread writers running while the worker searches an older version | no read of game memory by the worker (guard page on the mapped game heap during the worker's run); answers equal to the version, not the live data |
| K5 lifetime | versions released while a job holds one; lot removed mid-job | no use after free (debug heap), refcounts back to 0 |

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

Effort in Claude-with-Rodney days (static work, build, offline proof; nights run unattended).

| Phase | Work | Effort | Gate to the next phase (go / no-go) |
|---|---|---|---|
| P0 | Static maps: docs/91-93 (inputs, outputs) and this doc | done | the read set of docs/91-93 names every structure §2 hooks |
| P1 | Observe-only change counters: sim branch in the W1-W6 stubs, new leaf counters (`0x8321F0`, `0x831BA0`, `0x82A0C0`, `0x635700`, `0x829D50`), 15 IC slot counters, the docs/76 `World_FireEvent` stub (counts by id), object-event nodes for attribution; reserve a counter version (docs/registry.md); hook-sites rows (docs/68) | 1 d + 1 night | **G1:** `snapUnattributed` = 0 (or each source added); leaf write rate ≤ ~200/s and dirty lots per START ≤ 5 at p95; no off-sim writer except the known main-thread terrain shapes |
| P2 | Read-set probe P-RS (Frida, worker only) | 1.5 d + 1 night | **G2:** executed functions ⊆ static closure + listed gaps; global write set known; G or R chosen by the §3.4 rule; managed calls per job = event 59 only |
| P3 | Snapshot H: cut, images, versions, audit; no search yet (`[RouteSnap] Enabled=0 Mode=0` audit-only); steerro-test K1, K2, K5 | 4-5 d + 2 nights | **G3:** 0 audit mismatches on BP/IP/SV, travel and Edit Town; cut cost ≤ 2 ms/s BP; memory ≤ 64 MB; 0 crashes. A miss here stops the plan before any search work |
| P4 | Search on the copy, shadow only (`Mode=1`): G materialiser or R search; steerro-test K3, K4 | G 5-7 d, R 12-20 d; + 2 nights | **G4:** ≥ 10k shadow compares per world, M3 = 0, M2 = 0 (G) or explained (R), M4 < 5%, worker job time ≤ the game's plan time |
| P5 | Answers taken (`Mode=2`) for `ReplanFromPosition`, the A10 order; in-load A/B | 2 d + 1 night | **G5:** sim keyed waits not above off (the A10 failure mode: `wait-ab.ts` per site), it/s up and hitches ≥ 50 ms/min down vs off on BP and IP, 0 crashes, 0 audit mismatches; else stop and keep the build off |
| P6 | Widen to `PlanFromPoint`, `Replan`, `Plan`; soak, save matrix, travel; release notes | 2-3 d + 2 nights | **G6:** release checklist (`queues/async-release.json` pattern) passes with the snapshot on |

Totals: **G path 16-20 days**, **R path 23-34 days**, plus 8-9 nights. The first stop points are cheap: G1 (1 day) shows whether the writer layer is closed, G3 (~7 days in) whether sync holds live, before any search is written.

What this plan does not cover: steering (`0x82EDB0`, `0x85FD10`) and the other sim-thread routing queries stay on the game's data and keep their own designs (S2, ObstructedRO, SH); with H the worker no longer contends with them, so their waits should fall to the vanilla level, which `bp-snap-ab` measures.

## 6. Symbols added (CSV, append only)

`RoutingSystem_LotLoadFloorUpdate 0x834DC0`, `RoutingSystem_SaveLotWalls 0x834DF0`, `RoutingSystem_ExitBuildBuyLotUpdate 0x834F00`, `RoutingSystem_RebuildLotWalls 0x8350C0`, `RoutingWorld_AddObjectPortals 0x82D2E0`, `RoutingWorld_AddLotObjectPortals 0x832890`, `LotRouting_AddPortalVariation 0x637C30`, `World_RemoveLotCommon 0xA48CC0`: callers verified by the raw `E8` index, roles inferred from them.

## 7. Status: P1+P2 probe built (narrowed to the indirect-target sampler, 0.23.0)

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
