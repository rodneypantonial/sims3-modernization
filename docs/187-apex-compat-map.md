# 187. Apex Radiance compatibility map: Apex vs the game vs ts3native (2026-10-24)

Static only: no game or queue run. Goal: when a crash shows both mods, find in minutes which Apex hook, which game structure and which of our features meet. This page is an **index**: Apex already documents most of its hooks at field level, so each row points to their page (file + section) and to our docs/148, docs/68 and docs/186. New material here is only what neither side had: the per-address table, the draw dispatch fields, and the overlap/risk/signal table.

Sources:

- Apex source `loinyx/Sims3-ApexRadiance` at HEAD 2026-10-23 (= v2.15.3, `apex_version.h`), MIT; their `docs/` (architecture, engine, features). Their doc paths below are relative to their `docs/` folder.
- The tested binary `ApexRadiance.asi` v2.15.3 (SHA-256 `ecd6666f...992173`), read only.
- Steam 1.67 `TS3W.exe` analysis copy (capstone), our `native/ts3native` (`node tools/hook-sites.ts`), `native/ts3native/ts3native.ini` defaults, docs/41, 46, 48, 68, 148, 186.

Tags: **[verified]** read in source, binary or disassembly; **[claim]** Apex's doc, not re-checked; **[inferred]** reasoning.

## Summary

- **Apex hook surface:** 386 TS3W addresses are named by Apex feature code (literals, `GameAddr` ids, chain sites). About 116 are write sites: 32 entry detours (`EntryChain` + one Detours entry), 11 vtable slots (`SlotChain`), ~64 call-site or byte patches, 9 immediates/bytes; the rest are call targets, reads and byte checks [verified source, classification by script, see `docs/data/apex-hooks.csv`]. 369 of 386 appear as a 32-bit value in the v2.15.3 binary; the 17 that do not are development-build only (`0x4E3F70/90/B0`), found by signature (ScriptMath `0xE54D1B..0xE58304`) or computed from a base [verified byte search].
- **Apex documents its own hooks well:** 370 of 386 (96%) are cited in their `docs/`. The 16 that are not: the FastCasSort compositor readback chain (8 sites, `0x6082F8`, `0x5FDFEC`, `0x5FDFFF`, `0x5FDC8A` and their targets), 7 `level_light_share.cpp` byte sites `0x6A0E12..0x6A4665`, and `LampLitCall 0x6C7CB6` [verified script].
- **Hooks that change timing or order between threads** (all default on): SceneNodeBudget (defers the pending-node drain while the camera moves), LotLightingMotion (lot lighting 15 -> 3 ms, streaming lot build slice 20 -> 6 ms while moving), RoomLightQueue (re-orders room solves), FastCasSort compositor readback (splits a tile readback over frames), FastMemory (allocator spin, big frees on a worker), ObjectLookupIndex (SRW-locked id index in front of the object service and scene maps, any thread), MemoryGuard (save-time address space release). Night Lights only adds lighting work on main.
- **Our overlap:** 26 addresses named by both; one release-on pair changes Apex behaviour (`[SaveWrite]` at `0xAB12AF`: Apex's early save preparation stays off, docs/148 #1). Two release-on paths run through Apex hooks on the sim thread: our `[RouteGate]` destroy gate and its lookup call `ObjMapFind 0x939100` / `ObjMapErase 0x938D00`, which are Apex ObjectIdMap entry detours [verified: calls at `0x9395B3`, `0x939437`, `0x9394A4`]. No release-on feature of ours touches the draw path, Swarm, lights or the scene drain (docs/186 §3).
- **Gaps:** `ApexStandDown` (`apex.cpp`) does not cover `[Alloc]` vs FastMemory or `[MonoThread]` vs ScriptMath (both ours off by default); who nulls the object draw member `+0x210` and what sits at `ApexRadiance.asi+0x294143` are crash round 3 (docs/186), not here.

## 1. Apex hook surface (index)

How Apex installs and chains hooks, which thread runs what: Apex `architecture.md` §4.5 (patch helpers, `WriteCodeSuspended`), §4.6 (`SlotChain`, `EntryChain`, `CallChain` site tables with prologues and layers), §10.1-10.4 (game threads, Apex threads, which Apex code runs where, sync rules), §11.4 (conflict guard); summary on our side docs/148 §2. Install order vs ours: we write in `DllMain`, Apex after the first Present + 1 s, byte-checked, foreign bytes = feature off with a log line (docs/148 §2, #14) [verified].

Per switch (counts = write sites in `docs/data/apex-hooks.csv`; "timing" = changes when or in which order game work runs):

| Switch (`[patches.X]`) | Default | Write sites | What it changes | Thread | Game data it reads, writes or caches | Timing change | Apex page |
|---|---|---|---|---|---|---|---|
| SceneNodeBudget | on | 8 (call `0x6EBC49`, entries `0x6FD930`, `0x6E6480`, `0x6E4DE0`, checks on `0x6E41DE/E6`) | **defer**: BeginFrame's pending-node drain stops at 512 nodes / 2 ms while the camera moves; the rest stays queued (up to 1.5 s); lifetime guard on node dtor, AddNode, holder teardown | main (drain); dtor guard on any thread | scene pending holder `[scene+8]`, node link `+0x18/+0x1C`; records `link -> holder` (SRW) | yes: node `vfunc+0x48` updates (Swarm list gather, docs/186 §9.2) run frames later | `features/performance/scene-node-budget.md` (Technical reference: game side, budgeted copy, lifetime guard) |
| LotLightingMotion (+ WallShadingWhileMoving) | on | 5 (call `0xADB95D`, imm load `0xAEA6D8`, camera call `0xC6D5C4`, slot `WallAoStep 0xFF05B0`) | **budget**: lot lighting budget 15 -> 3 ms and streaming lot build slice 20 -> 6 ms while moving; wall AO step skipped (one per frame) | main | lot lighting manager, `LotRenderer` (`0xAEA680`), camera eye | yes: lots stay half-lit / half-built over more frames | `features/performance/lot-lighting-motion.md`, `wall-shading-while-moving.md`; `engine/lot-loading-and-streaming.md` |
| RoomLightQueue | on | 3 (+ level light share sites) | **reorder/budget**: room solves by lot priority, step budget, automatic lamps first | main | room light queue, rooms (`0x69EED0` invalidate) | yes: room light results later / in a new order | `features/performance/room-light-queue.md`; `engine/room-light-maps.md` |
| ObjectLookupIndex (ObjectIdMap, RenderObjectMap, lot cache) | on | 7 (entries `0x939100/0x939170/0x938D00`, `0x6FDFB0/0x6FE1F0/0x6FDF40`, `ObjectById 0xC62D40`) | **cache**: id index mirroring the object service map and the scene router map; lot lookup cache instead of the tree walk `0xC60D30`; first 64 answers compared | any (SRW lock) | object pointers by id, scene objects by id | lock only (SRW on every lookup, sim and main) | `features/performance/object-lookup-index.md` (object service ID index, scene object map index) |
| ResourceLookupCache / Misses / FileListCache | on | 4 slots (`0x4AFFC0`, `0x4B2D00`, `0x4B1AE0` families) + `DpfWriteDirect 0x4A7FC0` | **cache**: provider per key, missing keys, key lists; write epochs | resource threads + main | resource manager, databases, keys | no (answers equal, re-checked) | `features/performance/resource-lookup-cache.md`, `remember-missing-files.md`, `file-list-cache.md` |
| FastCacheCompression (+ FastRecordCrc) | on | slot `0xFB901C`, entry `RecordCrc 0x4FA4C0` | **replace**: RefPack compress multi-core, CRC-32C; first 16 decoded with the game's decoder | caller + workers | cache/save stream buffers | no | `features/performance/fast-cache-compression.md` |
| FastTextureCompression | on | entries `0x6152F0`, `0x6154B0` | **replace**: DXT encode multi-core | caller + workers | texture buffers | no | `features/performance/fast-texture-compression.md` |
| FastCasSort (+ compositor readback) | on | entry `CasTriSort 0x5D1960`; calls `0x6082F8`, `0x5FDFEC`, `0x5FDFFF`, `0x5FDC8A` | **replace** (sort) and **defer** (tile readback split over frames, no GPU wait) | caller + workers; compositor on main | CAS mesh, TextureBuilder jobs, staging textures | yes (compositor tiles finish later) | `features/performance/fast-cas-sort.md`; readback only in `features/compositor_readback.h` header (not in their docs) |
| FastMemory | on | calls `0x4E5306`, `0x4E4E81`/`0x4E504C`, trim `0x4E4F90` | **timing**: allocator CS spin 10 -> 2000, big-block `VirtualFree` on a worker, trim every 60 s, top-down flag | any + worker | EA allocator `[0x11CB864]` CS `+0x4E4` | yes: lock hold/wait shape on every thread | `features/performance/fast-memory.md` |
| MemoryGuard (Room to Save) | on | calls `0xAB12AF`, `0xAAC320`, `0xC6D529/537/544` | **reorder**: reserve released at save, early save preparation | sim (save) + watcher | address space, save path | save only | `features/performance/room-to-save.md` |
| ScriptMath | on | 25 handlers by signature, entries `0xEA8A00`, `0xE75340` | **replace**: Mono float compares inline, type object cache (generation per domain) | sim (Mono) | interpreter handlers, `MonoType` objects | no | `features/performance/faster-scripts.md` |
| WindowRepaint | on | `0x4108AE` | **skip**: `InvalidateRect` in the pump | main | window | no | `features/performance/lighter-window-updates.md` |
| Lot streaming group (5 switches) | off | entries `0xC6C290`, `0xAC1130`, `0xABFAC0`, `0xAC20E0`, `0x7D2DB0` scope | budget/reorder lot LOD and object adds | main | WorldManager, lots | yes | `features/performance/lot-streaming.md` |
| Night Lights (and RigTracker, SplitLevelGroundLight) | on | ~64 call/byte sites in `0x69xxxx-0x6Cxxxx`, `0xC2xxxx`, `0xC7xxxx`, `0xADxxxx`; RigTracker Detours entry `0x6F6250` + call `0x6F68C5` | lighting: lamp, room, terrain, level light share; RigTracker is pass-through (docs/186 §2) | main (render); light tree gather on another game thread | lights, rooms, lots, terrain, rig pointer bound by the game | adds main-thread work; requests from other threads set flags handled at the next Present (Apex `architecture.md` §10.4 rule 1) | `features/night-lighting/README.md` and pages; `engine/light-objects-and-rigs.md`, `room-light-maps.md`, `terrain-and-light-bake.md` |
| Picture filters (AO, DepthBlur, EdgeSmoothing, Banding Fix) | on | D3D device hooks only (few game reads) | post-process | main | D3D device, depth | no | `features/picture-filters.md`, `depth-blur.md`, `edge-smoothing.md` |
| Frame Profiler, census, light diag, recorder | off (developer mode) | 10 | timing only (entry/call chains layer 0) | hooked thread | - | no for players | `features/frame-profiler.md` |

Binary samples [verified]: the call `0x6EBC49` in `Scene::BeginFrame 0x6EBB70` is `E8` to `0x6E4130` in the exe; `0xAEA6D8` is the `[0x11ECBC4]` load Apex documents; `RigTracker::ModelDrawHook` = `asi+0x292BB0` byte for byte (docs/186 §2). `asi+0x294143` is crash round 3 (docs/186), not mapped here.

## 2. Game side for the systems Apex touches (index + gaps)

| System | Field-level reference | Thread, ordering EA relies on |
|---|---|---|
| Scene pending-node drain `0x6E4130` (holder `[scene+8]`: list `+0x20/+0x24`, counter `+0x18`, tree `+0x2C`; node link `+0x18/+0x1C`, owner `+0x30`, base vtable `0xFF9D00`, destroyed vtable `0xFA1B78`) | Apex `features/performance/scene-node-budget.md` "The game side (Steam 1.67.2)" [claim, Apex: verified in disassembly]; six callers, `0x6EBC49` every frame | no lock; render thread only [inferred by Apex]; node release atomic, so a node destructor can run on any thread. EA relies on BeginFrame draining **every** pending node (running its `vfunc+0x48`) before the frame jobs cull/sort and `Renderer_DrawMainRenderSets 0x6F0DB0` draws [inferred, docs/41 frame order]. Note: our docs/41 calls `0x6E4130` `Renderer_RunDeferredCallbacks`; Apex's reading (pending-node drain) is the detailed one (ledger I-187-1) |
| Draw dispatch `0x6EE5E0` (the draw-list call `0x6EE872`) | **new here** [verified reads, roles inferred]: each render item (`ebx`) holds `+0` the drawable scene node (vtable; slot `+0x18` = draw, called with `(+4, ctx)`), `+4` the part index (x `0x40` into the part array at `[+8]+0x160`, used for the debug bounds draw), `+8` the model / geometry object (`+4` flags tested `0x200000`, `+0x10` bounds), `+0x20` flags (`0x800`), `+0x24` the transform record compared with the previous item (state change skip); per-item checks `0x6FAC20` and `0x9158F0` on the drawable before the draw; the context reads `[arg+0x210]` once at entry (not the object member of docs/186: different object [inferred]) | main, step 8 (docs/46). Render items come from the sort job `+0x31C` (docs/46 §2) built after BeginFrame's drain |
| Swarm render list (SwarmRenderer `0xFFC820`: entries `+0x3110`, free `+0x3120`, buckets `+0x38EC..`, count `+0x3980`) | ours: docs/186 §9.2 [verified] | add/remove/gather/draw without lock, all on main; lists rebuilt only by the drawable's `vfunc+0x48` in the drain: deferring that node draws stale lists (docs/186 §9.2-9.3) |
| Object draw `0x6FA0B0` and member `+0x210` (resource via `0x6DB810`) | docs/186 §1, §8.1; **who nulls `+0x210` = crash round 3** | main draw |
| Model part draw `0x6F6250`, material draw `0x6F4D40`, shader binding `0x68FD80` | docs/186 §1-2; Apex `engine/light-objects-and-rigs.md` (rig binding `0x6F68C5`) | main draw |
| Lot streaming, LOD, WorldManager `[0x11ECBC4]`, LotRenderer (`0xAEA680`), stage 20 lighting wait | Apex `engine/lot-loading-and-streaming.md` (Data structures, Call flow, Address reference) | main (render = main thread); S3SS LotStreamingOptimizations also there (docs/68 §1.1) |
| Lights, rooms, level light share | Apex `engine/light-objects-and-rigs.md`, `engine/room-light-maps.md`, `features/night-lighting/level-light-share.md` | main; light tree gather on another game thread (Apex `architecture.md` §10.1) |
| Object service map `0x939100/0x939170/0x938D00`, scene router map `0x6FDFB0/0x6FE1F0/0x6FDF40` | Apex `features/performance/object-lookup-index.md` "The game side"; ours docs/45 §4 | sim and main; EA's map has its own rules (docs/45 §5) |
| Main loop, services, sim thread | Apex `engine/main-loop-and-services.md` (Thread model, Address reference); ours docs/41 | main loop `0xECA960`; sim `0xD81840` |

## 3. Overlap with ts3native

Release state from `native/ts3native/ts3native.ini` (2026-10-24). Risk words: crash, timing, feature off, none. "Signal" = what to read first after a crash or odd behaviour.

| # | Ours (state) | Apex (default) | Shared function, data or ordering | Risk | Signal |
|---|---|---|---|---|---|
| 1 | `[SaveWrite]` call `0xAB12AF` (on); `[RouteGate]` count `jmp` `0xAAC110` (on) | MemoryGuard early save preparation (on) | same call / entry | feature off (Apex keeps the older Room to Save path) | Apex log: early preparation refused; ours `compat:` line (docs/148 §7a) |
| 2 | `[RouteGate]` destroy gate `0x939420` + `FindByIdAddRef 0x9395A0` on the sim thread (on) | ObjectIdMap entry detours `0x939100` / `0x938D00` (on) | our path calls the game's find/erase, which land in Apex's index (SRW lock) [verified calls] | timing (lock), none for correctness while Apex's first 64 answers match | Apex log ObjectLookupIndex mismatch/off line; `[CrashForensics] OurEvents=1` ring `destroy-in`/`destroy-out` |
| 3 | `[RouteGate]` destroy later on sim (on) | SceneNodeBudget node dtor guard (on, any thread) | a destroy can release scene nodes off main; Apex's guard then unlinks a recorded node from a list the main thread drains without a lock [inferred; not shown that our destroys free scene nodes] | crash (Apex/game race; ours only shifts when) | `destroy-out` within ms of a `DRAW BAD`; `fxRemOffMain` (docs/186 §9.4) |
| 4 | none on the draw path (release) | SceneNodeBudget + Swarm gather (on) | docs/186 §9: stale Swarm lists when the drawable stays queued | crash (Apex) | `[CrashForensics] EffectLog=1`: `Swarm draw on stale lists`, `fxStaleDraw`, `fxGatherGapMaxMs`, gather `from ApexRadiance.asi+...` |
| 5 | `[CrashForensics] DrawGuards` (off, probe): calls inside `0x6F6250`, `0x6FA0B0`, `0x68FD80`, `jmp` `0x71D562` | RigTracker Detours entry `0x6F6250` (on) | same function, different bytes | none (compose, docs/68) | `OK`/`SKIP crash forensics draw guard` at start-up |
| 6 | `[CrashForensics] EffectLog` slots `0xFFC850/854/290/260` (off, probe) | SceneNodeBudget redirects the drain that runs `0xFFC290`'s function | order only | none | caller of the gather in the effect log |
| 7 | `[Decompress]` call `0x4EC065` in `0x4EC010` (on) | FastCacheCompression slot `0xFB901C` + `RefPackDecompress 0x4EB3B0` read for its check (on) | RefPack code, different sites | none | our `decompress fails`; Apex compression mismatch line |
| 8 | `[SaveCompress]` slot `0xFB901C` (off) | FastCacheCompression (on) | same slot | feature off | `compat: stood down for Apex FastCacheCompression` (covered) |
| 9 | `[IdCache]`, `[Verify]` probes (off) | ObjectLookupIndex (on) | same entries | feature off | stood down (covered) |
| 10 | `[FootprintIndex]` call `0xAAC320` (off) | MemoryGuard (on) | same call | feature off | stood down (covered) |
| 11 | `[LotStream]` (off) | Lot streaming group (off) | same entries | none | stood down if either on (covered) |
| 12 | `[Alloc]` entries `0x4E69D0/6A20/5920/5CB0` (off) | FastMemory (on) | same allocator and CS `+0x4E4` | crash or double effect if ours is turned on | **not in ApexStandDown**; docs/148 #9 |
| 13 | `[MonoThread]` P4 sites / M5 `0xE75260/70` (off) | ScriptMath handlers + type cache (on) | Mono interpreter loop `0xE53EB0` | future crash if P4 writes a handler Apex rewrote | **not in ApexStandDown**; docs/148 #10-11 |
| 14 | `[RouteAsync]` `Lock_Acquire 0x4E16F0` (on); `[Verify]` `0x599720` (off) | Frame Profiler entries (developer only) | same entries | none for players | profiler "skip, bytes differ" |
| 15 | `[Harness] ForegroundRender` `0xEC9FD1` (test only) | Frame Profiler `0xEC9F00` (developer) | same function | none | - |
| 16 | `[GC]` immediates `0xE4E168`, `0xE4A1D8..` (on) | ScriptMath, Frame Profiler `0xE4A050` (developer) | Mono GC | none | - |
| 17 | `[CrashReport]` (off) | crash filter refreshed every 1 s (always) | process filter chain | double report | docs/148 #8 |
| 18 | sim-side waits (`[RouteAsync]`, gates; on) | camera-motion budgets (on) | sim posts (Swarm messages, `MarkDirty`) reach main at other frames than without us | timing [inferred] | our-events `job-start`/`job-end` around a `DRAW BAD` |

`ApexStandDown` vs what Apex covers: stands down `[IdCache]`, `[SaveCompress]`, `[FootprintIndex]`, `[LotStream]`, `[Verify]` probes for ObjectLookupIndex, FastCacheCompression, MemoryGuard, ResourceLookupCache, FileListCache and the five Lot switches [verified `apex.cpp`]. Not covered: `[Alloc]` (#12), `[MonoThread]` (#13), SceneNodeBudget / LotLightingMotion / RoomLightQueue / FastCasSort / ScriptMath / FastMemory (none of ours shares their bytes in release). `node tools/hook-sites.ts --check` now prints every shared address with Apex's kind, switch and default from `docs/data/apex-hooks.csv`.

## 4. Debug recipe for a crash with both mods

1. Fault in `0x6Exxxx-0x72xxxx` on main: docs/186 (draw path), check §3 #3-#4 signals.
2. Fault in a function listed in `apex-hooks.csv`: read the `apex_doc` column, then Apex's log (`ApexRadiance_LOG.txt`: `[Addr]` lines, "entry bytes ... changed", "holds X, expected Y").
3. Fault in our address range (docs/68 §2): `node tools/hook-sites.ts --check` overlap list, then the module's docs.
4. Bisect by switch: Apex `[patches.<X>] enabled = false` in `ApexRadiance.toml`, ours by ini section; one switch per run (docs/186 §5).

## 5. Not done

- The `kind` column is a source-text classification; Apex's page is the authority per site. Vtable slot writes computed at run time (level light share classes) are not rows.
- Who nulls `+0x210`, `asi+0x294143` (round 3); whether our destroys release scene nodes off main (§3 #3).
- Effort: ~2 h static. Ledger rows: docs/51 §328.
