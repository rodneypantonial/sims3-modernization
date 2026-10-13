# 20. Native work on the sim thread: what could move to other cores

Written 2026-10-13. Read-only analysis of existing samples; the game was not run for this note. Steam TS3W.exe, image base `0x400000`.

Tags: **[verified]** = seen in decompiled code, Ghidra references or the sample data. **[inferred]** = reasoning still to confirm by a run or a probe. **[measured]** = counted from the sample files named below.

## Short answer

- Pooled over 15 native sim-thread profiles (9,890 samples, BP/SV/IP), the sim thread spends **~45% with the Mono interpreter on the stack** and ~55% without it. By nearest owner: interpreter 28.5%, Mono runtime 14.8%, GC 9.9%, **route search 14.5% (incl. water JPS)**, **EA arena allocator 6.9%**, OS/other 9%, and a long tail of engine work, each under 3.5% [measured].
- The biggest native item that is not route search is **not offloadable**: EA's own arena allocator `0x4E5E40` (6-16% of the sim thread, incl. its lock). It needs a faster allocator, not a worker.
- **Top offload candidates:**
  1. Route search (`Route_Plan` family; docs/18 B+C), including the **water-grid JPS `0x620560`/`0x61FE10`**, which alone is **7.8% of the sim thread in Isla Paradiso**.
  2. **Animation update `0x571B90` -> rig pose `0x940430`**: 3.3%, engine-only, a per-actor loop that could become a parallel-for.
  3. **Read-only spatial queries** (floor/terrain height raycast `0x67E2B0` chain, footprint quadtree `0x681CF0`, `Query_GetObjectsOnLot`): ~4.2%. These are better cached or batched than moved to another thread.
- **Create-a-Sim/Buy:** no native or managed stack samples exist for CAS or Buy; `store-cas-browse` recorded only timings. What to capture is in §5.

## 1. Data and method

**Inputs** (`probe-out/profile/`, non-empty `*simstack*.jsonl`): `bp-all, bp-detour2, bp-gc0, bp-gc1, bp-mod, bp-worker1, simstack-bp` (3,716 samples), `sv-all, sv` (1,723), `ip-all, ip-gh, ip-mod, ip-on, ip-rh, ip` (4,451). Empty files (`bp-nobudget`, `bp-wab`, `bp-nab`, `bp-budget`, `bp-detour`, `ip-ab`) were skipped. `profile-world.ts` takes each sample with `GET /simstack?frames=24` at 5 Hz over 3 min (minutes 5-8 of a 10-min run). The managed C# sampler runs earlier in the same run (minutes 1-4), not at the same moments.

**How a stack is walked** (`tools/probe/agent.js` `walk()`) [verified]:
- The leaf PC is exact.
- Next comes the EBP chain. If that yields fewer than 4 frames, the walker scans the stack for values that point into code, and stale values can show up.
- The probe server strips the `?` marker, so scanned frames cannot be told apart from real ones. The median stack has 24 frames, which is the cap, so deep native stacks lose their root.
- **So self (leaf) time is reliable. Inclusive time and roots are approximate.** For example, `nvgpucomp32.dll` shows up in 28% of stacks, although the sim thread does not render.

**Attribution:** each sample goes to the nearest owner. The walk goes from the leaf outward over the first 12 TS3W frames, and the first frame inside a known family wins. Families are function sets and address ranges taken from docs/11 and docs/18 plus this session's decompiles. If the leaf is in an OS DLL, the sample goes to the first TS3W frame's family and is marked "OS wait/call".

**Names:** `game-copy/analysis/functions.csv` (Ghidra function ranges), `tools/ghidra/anchors.csv`, and `game-copy/analysis/internal-calls-map.csv` (IC address to C# extern). Decompiles and caller trees came from Ghidra headless `DecompileTo.java` and an ad-hoc `Callers.java`. Both ran read-only against the TS3 project, with output in the session scratchpad (not committed).

**C# side:** the managed samples (`*-managed.jsonl`, 15,859 samples, 20 files) give, for each internal call (IC) on top of the C# stack, the C# method that called it. 31.0% of managed samples have an IC on top; 26.9% are empty, meaning the sim thread was outside any script frame [measured].

**Error bars:** at 9,890 pooled samples, a 3% share has a 95% interval of about ±0.35 points. Per-world shares (1,700-4,500 samples) are good to about ±0.5-1 point. 1% of samples is about 10 ms per second of sim-thread wall time; the samples include waits.

## 2. Where the sim thread's time goes

| Family (nearest owner) | All | BP | SV | IP | Notes |
|---|---|---|---|---|---|
| Interpreter `0xE53EB0` (self) | 28.5% | 29.6% | 30.0% | 26.9% | C# itself |
| Mono runtime (`0xE20000-0xEF0000`, `0xD82000-0xDC0000`) | 14.8% | 15.5% | 14.3% | 14.5% | e.g. type compare `0xEA8720` 2.2%, IC trampoline `0xDA5970` + its ntdll lock calls ~1.5% |
| GC (Boehm mark/stop, `0xE4C3E0`, `0xE4B8D0`, …) | 9.9% | 7.5% | 17.0% | 9.1% | docs/13; ts3native heap patch |
| **Route search, other routing lib** (`0x618000-0x684000`, `0x819000-0x837000`, route ICs) | 11.4% | 11.0% | 9.6% | 12.3% | incl. 0.4% OS wait |
| **Water-grid JPS** `0x620560`/`0x61FE10` | 3.5% | 0.0% | 0.0% | **7.8%** | part of route search |
| **EA arena allocator** `0x4E5E40` (+lock wrapper `0x4E69D0`) | 6.9% | 9.9% | 4.8% | 5.2% | incl. 1.0% lock wait; **16% in `bp-detour2`+`bp-worker1`** |
| Animation update `0x571B90` | 3.3% | 3.2% | 2.9% | 3.4% | |
| Object-ID hash lookup `0x939100` (+ `0xD726F0`) | 2.4% | 3.0% | 2.1% | 1.9% | |
| Locomotion `0x83CED0` | 2.4% | 2.6% | 3.3% | 1.8% | |
| Floor/terrain height raycast `0x67E2B0` chain | 1.9% | 1.6% | 1.0% | 2.5% | |
| Footprint quadtree query `0x681CF0` | 1.3% | 1.6% | 0.8% | 1.4% | |
| `Query_GetObjectsOnLot` impl `0x7499B0` | 1.1% | 1.5% | 1.0% | 0.9% | |
| Sim/render triple-buffer sync `0x593590` (wait) | 0.6% | 0.7% | 0.6% | 0.6% | |
| Line of sight `0x6814B0` | 0.4% | 0.5% | 0.1% | 0.3% | |
| ntdll, not attributed (waits, idle, Mono locks) | 5.3% | 5.7% | 5.5% | 4.9% | ntdll-only frames 1.8%; ts3native worker waits in `bp-worker1` |
| MSVCR80 (memcpy/memset, mostly under the interpreter) | 2.2% | 2.2% | 2.1% | 2.2% | |
| Native, no family found in 12 frames | 3.0% | 3.1% | 3.4% | 2.7% | spread thin |
| Frida agent / S3SS asi / kernel32 | 1.1% | | | | probe overhead and S3SS |

Route search inside these samples is less than the C# view shows: route ICs are on top of 16.2% of managed samples (`Route_Plan` 9.4%, `ReplanFromPosition` 3.0%, `PlanFromPoint` 2.1%, `Replan` 0.7%). Truncated native stacks therefore hide some routing under "native, no family" and under the shared raycast and quadtree families [inferred].

## 3. Top 10 native hotspots (not interpreter, Mono or GC)

"Share" = percent of all sim-thread samples, so 1% is about 10 ms/s.

| # | Function (address) | Share | What it does | Caller (C# where known) | Offloadable? How | Risk |
|---|---|---|---|---|---|---|
| 1 | Route search: Planner `0x65E0D0`, world A* `0x676260`, lot portal A* `0x63E340`, quadtree-cell A* `0x61E500` ("Routing/AStarSearch"), cost functions `0x674250`/`0x673850`/`0x674E40`/`0x675800` via `0x627780`, lot-cell helpers `0x663B20`/`0x66A040`/`0x66B090` ("Routing/QuadTreeCellPos") | 11.4% (IP 12.3%) | Path planning under the routing lock (docs/11) [verified] | `Route_Plan` (from `RouteBackoffHook.Plan`, `Route.PlanToPointRadialRange`, `Lot.PlanToLotEx`, `PlanToSlot`, `PlanToPoint`), `ReplanFromPosition` (from `RoutingComponent.DoRoute`), `PlanFromPoint` (from `PlanToPointFromPoint`, `Sim.IsPointInLotSafelyRoutable`) [measured, managed] | **Yes.** Already in progress: the async plan for the Sim's own task (docs/18 B+C); worker mode 1 runs in game (PLAN) | Medium-high (lock overlap, Mono callbacks proxied) |
| 2 | EA arena allocator `0x4E5E40`, lock wrapper `0x4E69D0` (`EnterCriticalSection` around it) | 6.9% (BP 9.9%; `bp-worker1` 11.9% self + 3.4% lock wait) | dlmalloc-style bins: small bins by `size>>3`, a sorted large-bin scan in a loop (`LAB_004e5f41`); its own `VirtualAlloc` arenas, not MSVCR80, so S3SS mimalloc does not cover it (docs/17 §8) [verified] | Everywhere: routing, engine update, IC marshalling; the stack scan cannot name the callers (frameless) | **No.** Make it cheaper instead: a thread-local size-class cache in front of `0x4E5E40`, or route arena allocations to mimalloc. The higher lock wait in `bp-worker1` suggests the route worker and the sim thread now contend for this lock [inferred] | Medium (cross-arena frees, S3PP `AllocatorArenaTuning` patches the same code) |
| 3 | Water-grid JPS: `0x620560` ("PathPlan/closed", "PathPlan/extraData") with recursive jump `0x61FE10`, forced-neighbour test `0x61FA10`/`0x61F890`, cell test `0x61F9A0` | 3.5%, **IP 7.8%**, BP/SV 0% | Jump-point search on a byte grid; the jump recurses once per cell along a line [verified decompile] | Water leg of the planner: `0x65E0D0` -> `0x671930` ("PathPlan/path", "Routing/waterPath") -> `0x620560`, i.e. `Route_Plan` for boats and swimming [verified xrefs] | **Yes, with item 1** (same call). Also pure: it only reads the grid and writes its own closed set, so it can be made cheaper where it runs. Turn the recursion into a loop, or precompute JPS+ jump distances, since the water grid is static per world [inferred] | Low (in-place, pure); the result must match exactly |
| 4 | Animation update `0x571B90` -> per-actor time step `0x942C00` and rig pose `0x940430` ("Animation/RigPose"); leaves `0x582830`, `0x566010`, `0x56BC80` | 3.3% | Walks the list of animated actors; for each one advances the clock and evaluates the rig pose; `IC_StreamHost_Flush` on a streaming path [verified decompile] | `MonoScriptHost_Simulate` directly, with no C# caller [verified xrefs] | **Maybe.** It is a per-actor loop, and the per-actor refcounts already use `LOCK`-prefixed increments. A parallel-for over actors (8 cores) could take most of it off the thread, but only if pose evaluation does not fire script events or touch shared scene state [inferred; check `0x940430`'s callees] | High |
| 5 | Object-ID hash lookup `0x939100` (fixed 1,033 buckets: `key % 0x409`, chained), plus `0xD726F0` (script-host handle map) | 2.4% | Finds an object by its 64-bit id [verified decompile] | About 300 ICs reach it, e.g. `Slots_GetParentIdImpl` <- `GameObject.get_Parent` <- `get_LotCurrent` (0.7% of managed samples), `CanSeeObject`, `Objects_GetPosition` [verified xrefs, measured] | **No** (too small and too frequent). Fix in place: more buckets (the modulo is hard-coded at every user) or a one-entry last-hit cache [inferred: chains are long when worlds hold more than 10k objects] | Medium (every inline user of `% 0x409`) |
| 6 | Locomotion `0x83CED0` -> per agent `0x957210` -> obstacle avoidance `0x847390` ("Locomotion/IgnoreList") / `0x85D980` | 2.4% | Per-iteration obstacle check for walking Sims under the routing lock (docs/18) [verified] | `MonoScriptHost_Simulate` [verified] | **Hard.** Batchable per agent, but it writes agent state and holds the routing lock; this lock is the reason route offload only overlaps until the next tick (docs/18) | High |
| 7 | Floor/terrain height raycast: `0xC2A570` (world cell grid) -> vtable `+0x10` -> `0x67EDE0`/`0x67EA80`/`0x67E740` -> ray-triangle `0x67E2B0` (int16 vertices) | 1.9% (IP 2.5%) | Height of floor or terrain at (x, z) via a mesh ray test [verified decompile; class through vtable data ref `0xFEFA4C`] | `World_GetTerrainHeightImpl`, `Route_PlanCommon`/`PlanFromPoint`, `RoutingBlockerCheck`, `GetObstructionAtPoint`, `Slots_GetPositionOfSlotImpl`, `World_IsSimNearShoreline`, … [verified xrefs, depth 4] | **Cache rather than offload.** Pure read of static geometry. Memoize by (lot/world, quantized x, z, level), cleared on build/buy edits [inferred] | Low-medium (invalidation on edits) |
| 8 | Footprint quadtree query `0x681E00` -> recursive `0x681CF0` (bbox test `0x6809F0`) -> `0x681B40` | 1.3% | Spatial query for footprints or objects in a box/radius [verified decompile] | `Query_GetObjectsInRange` (from `ReactionBroadcaster.GetInRadiusSims`, 1.0% of managed samples), `CountObjectsInRange`, `RouteManager_RoutingBlockerCheck`, `IsRouteToObjectUnobstructed`, `IsOkToIdleAtCurrentLocation`, `Query_GetSimsOverlappingFootprint`, `Route_PlanCommon` [verified xrefs, measured] | **Batch or throttle in C#** (broadcaster throttle F12 already exists); read-only, so a per-tick result cache is possible [inferred] | Low (C#) |
| 9 | `Query_GetObjectsOnLot`/`CountObjectsOnLot` impl `0x7499B0` (+ component lookup `0x935770`, popcount sparse array) | 1.1% (+1.7% of managed samples on the IC) | Walks a lot's objects with a type filter [verified decompile] | `Lot.GetObjects<T>` (1.0%), `Sim.GetMostExpensiveUsableParkedVehicle` (0.2%), `LightGameObject.AutoLightsOn`, `Lot.CountObjects` (0.3%), `WorldLot.GetObjects` via `GetGlobalObjects` (0.4%) [measured, managed] | **Cache in C#** per (lot, type) per tick or until objects change; no native work needed | Low |
| 10 | Sim-to-render triple buffer `0x593590` (`EnterCriticalSection`), and line of sight `0x6814B0` | 0.6% + 0.4% | Publishes render state each tick; `CanSeePoint`/`CanSeeSky` ray [verified decompile and xrefs] | `MonoScriptHost_Simulate` via `0x76C5C0`/`0x76C8A0`; `World_CanSeeObjectImpl` <- `Sim.CanSeeObject` <- `ReactionBroadcaster.GetInRadiusSims` | No (sync); line of sight can be throttled with item 8 | Low gain |

Smaller items, none above 0.5% each: `0xAB7ED0`/`0xA8DF80` (lot tile and room lookups used by many World ICs), `0xC5FA60` (recursive scene-node find), `0x80E060` (matrix copy through a virtual call), and `0x61C020` (callback dispatch over a 10k-entry stack array).

## 4. Ranking for offload work

| Rank | Candidate | Expected gain (sim-thread ms/s) | Effort | Status |
|---|---|---|---|---|
| 1 | Route search async, including the water JPS (docs/18 B+C) | Table share ~110 BP / 96 SV / 201 IP. Realistic after the lock overlap: 15-70 BP, 35-125 IP (docs/18, E×O). The water JPS moves with it | M-L | Worker mode 1 works in game; async mode being built |
| 1b | Water JPS made cheaper in place (iterative jump or JPS+) | IP only: up to ~40-60 of the ~78; also shortens long IP searches that async cannot hide | M | New, pure function, low risk |
| 2 | Animation update as a parallel-for over actors | ~20-28 of ~33 (8 cores, if safe) | L | New; first decompile `0x940430`'s callees for script or event calls |
| 3 | Read-only query caches (raycast height, quadtree, `GetObjectsOnLot`) | ~15-30 of ~43 | S-M each (C# for #8 and #9, native for #7) | New |
| n/a | Allocator: thread-local cache or mimalloc behind the arena (not an offload) | ~30-80 of 50-160 | M | Built (ts3native 0.8.0, `[Alloc]`, docs/15 Phase 8): per-thread cache in front of the locked entry points, not run in game. S3PP `AllocatorArenaTuning` patches other sites (compatible) |

Not offloadable, and handled elsewhere: interpreter (28.5%), Mono runtime (14.8%), GC (9.9%; docs/13, ts3native heap).

## 5. Create-a-Sim and Buy: no profile yet

- `probe-out/` holds no `simstack` or `managed` capture from CAS or Buy. `repro-cas.log` / `store-cas-browse` measured timings only: CAS open 2.2 s, hair 3.2 s / 261 items, makeup 1.0 s / 17 items, then `CASClothingCategory not ready after 20 s` [measured].
- The live-mode managed samples already show CAS ICs on the sim thread: `CASUtils_ApplyPresetToPartImpl` 0.49%, `SimOutfit.GetPartPreset` 0.45%, `PartDataGetPreset` 0.16%, `CreatePartSearchImpl` 0.14% (leaf `0x4AC9C0`), and `UIManager_LoadUIImageImpl` 0.37% (thumbnails) [measured]. Native anchors: `CASCache_GetCachedPart 0x5A44A0`, `CASPartCacheService_*` (`0x5AEDF0`).

**What to capture:**
1. Run `store-cas-browse` (store + S3PP, then without store) with a 20 Hz sampler during each `cas_category` and `buy_category` switch. Use `GET /simstack?frames=24` plus `GET /managedstack` at the same moments, tagged with the current step (category, item count, ms).
2. Add a single-thread stack endpoint for the **main/render thread**, in the same safe style as `/simstack`; never the all-thread `/threads`. Thumbnail decode and the UI grid probably run there, not on the sim thread [inferred].
3. Then rank as in §3, looking for:
   - the part search/filter (`CreatePartSearchImpl` -> `0x4AC9C0`), which is pure over the part list and cacheable per category;
   - thumbnail load/decode (`LoadUIImageImpl`), a classic case for a worker;
   - preset application (`ApplyPresetToPart`), which writes the Sim and must stay on the sim thread.

## 6. Open checks

- Inclusive numbers depend on the stack scan. A run with the `?` scan marker kept in `simstack` output (`ts3probe.py` currently strips it) would separate exact EBP frames from scanned ones.
- The allocator share varies from 1.8% (`ip-all`) to 11.9% (`bp-worker1`). Confirm whether worker contention or S3PP's arena patch explains the recent BP rise: run an A/B `--ab routeworker` with `/simstack` on.
- Item 4: whether `0x940430` calls into Mono or fires events (decompile its callees `0x942C50`, `0x959170`, `0x947590`).
- Item 5: count the objects in the `0x939100` table in BP and IP to confirm long chains.
