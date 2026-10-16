# 48. Scene graph and culling (M-D3D3b)

Static map of the renderer's scene side: the Scene object and its quadtree, the SceneObjectManager (object table, message handler), how objects get into the scene and on which thread, the cull pipeline, and the SpeedTree cluster rebuild. Steam 1.67 `TS3W.exe`, Ghidra with `tools/ghidra/symbols/ts3w-1.67.symbols.csv`, tools `CallerTree`, `RangeMap`, `StringXrefs`, `ScalarRefs`, `XrefsTo`, `CallPaths`, `DecompileTo`. No game run. Builds on docs/37 §9 (anchors), docs/34 (SpeedTree), docs/38 (lot lifecycle), docs/41 (frame loop, FrameDatabase), docs/28 §5.2 (lot streaming path).

## 1. Summary

- **The renderer's scene is owned by the main thread and fed by messages.** The sim side never touches scene structures directly in the paths traced here: it posts 0x50-byte render messages (`PostRenderMessage 0x692F70` -> `RenderMessageBuffer_Post 0x593E60`), and the main thread drains them in `FrameDb_ConsumeAndInterpolate 0x693780` and hands each one to the `MessageServer` (`RenderMsg_Dispatch 0x693170`: id `0x489122F` = run a callback in the message, any other id = `MessageServer` vtable `+0x18`, synchronous) [verified]. The `SceneObjectManager` registers ~40 message ids with the `MessageServer` in its constructor and handles them in one big switch, `SceneObjMgr_HandleMessage 0x703CB0` [verified].
- **SpeedTree cluster rebuild answer: main thread.** `SpeedTreeClusterModel_Rebuild 0x712480` has one caller, `SceneObjMgr_OnSpeedTreeClusterMsg 0x7006B0`, which has one caller, the `0x703CB0` switch case for message `0x3359DC3C` [verified]. `0x703CB0` has no direct callers; it is slot 1 of the listener vtable `0xFFA1F4` that the `SceneObjectManager` constructor stores [verified]. The only code that builds message `0x3359DC3C` is in the tree component block (`0x95CFD0`, `0x95DC60`, `0x95EC10`), and all three send it with `PostRenderMessage` [verified: raw byte scan of the exe for the id; the other hits are the register, unregister and switch]. So the rebuild runs on the **main thread, inside the render message drain, while the main thread holds the FrameDatabase frame lock `+0x48`** (S1 in docs/41) [verified code path; that the drain at `0x693935` sits inside the TryEnter region is from docs/41].
- **What that costs the sim:** nothing directly; indirectly, a long rebuild lengthens the S1 hold, and the sim waits for it in `SimRender_TripleBufferSync 0x593590` (0.60% self of sim samples today, all causes) [measured total; the share caused by cluster rebuilds is unmeasured]. The posters run on the sim thread: tree component creation during lot load (`0xC6DE80` <- `0xACE4A0`), `IC_World_LotClusterTreesImpl 0x7868A0`, `Lot_Save 0xACB8A0`, build-mode tool paths (`0xD41310`, `0xD44DC0`) [verified callers; thread inferred from the callers: IC_ and lot load are sim thread, docs/28, docs/38].
- **Lot streaming (`Lot_AddLotObjectsToScene 0xAC1130` / `Lot_UpdateObjectSceneNode 0xABFAC0`) does not reach `PostRenderMessage` or `MessageServer_Get` through direct calls within 7 levels** [verified, `CallPaths`]; its object event `0x14` goes through the listener chain `GlobalHandlerChain 0x936C90` (virtual), so the scene side of a lot add was to be reached through listeners not traced yet. **Answered later (docs/50 §1, docs/61 §1):** no native code registers a handler for event `0x14`, and the lot-add script message scope has no listener, so the chain is empty and a lot add runs no managed code [verified].
- **Culling is main-thread work, optionally split into render jobs.** `SceneManager_Draw 0x6F3C90` either culls inline (`SceneManager_CullAndBuildRenderSets 0x6F1B00`) or, when flag `SceneManager+0x28F` is set, waits for up to five jobs with `JobManager_Wait` [verified]; the flag is very likely the `Multithreaded Cull` debug option (string next to the other scene options in `Renderer_RegisterDebugOptions 0x6EF780`) [inferred]. Those jobs are the `Renderer_BeginFrame` chain (M-D3D3a). One sim sample has `Scene_CullLinear 0x6E5880` directly under `MonoScriptHost_Simulate` [measured, 1 sample]: the sim thread's unbudgeted job pump (docs/36 C1) does pick up cull work sometimes.
- **Scene structures have no locks.** No function in `0x6F4000-0x706000` (SceneObjectManager, scene models, quadtree) calls `EnterCriticalSection`, an `Interlocked*` or `GetCurrentThreadId` [verified: callee scan of `RangeMap` output]. The SceneManager functions that do take a lock are the render-entry ones (`0x6F3C90`, `0x6F3D30`, `0x6F3E90`, `0x6F3F90`, `0x6F40B0`, `0x6F0DB0`, `0x6F1390`, `0x6F1D90`) [verified callees; which lock, not read]. Safety comes from single ownership (main thread) plus the message queue [inferred].
- **Sim-thread cost of the scene block is ~0%:** 0.04% self, and the 1.2% inclusive is almost all one folded struct-copy helper (§6) [measured]. Render message posting costs the sim 1.2% inclusive (0.13% self in the lock/append), shared by every message type [measured].

## 2. Structures

### 2.1 Scene (`Renderer/Scene`, `[0x11D1860]` renderer +, built by `Renderer_InitScene 0x6EB610`)

| Offset / item | What | Evidence |
|---|---|---|
| ctor `Scene_ctor 0x6E4530` | tags `Renderer/Scene/ObjectTable`, `ObjectsInFrustum`, `VisibleObjectsForLighting` | [verified tags] |
| `+0x04..+0x08` | object pointer vector (all scene objects); cull allocates a visited bitset of `(n+31)/32` words (`0xAEF830`) | [verified decompile] |
| `+0x2C` | `SceneQuadtree` (0xA0 bytes, `Renderer/Scene/SceneQuadtree`), null when not built | [verified] |
| `+0x30` | quadtree query state passed to `SceneQuadtree_QueryFrustum 0x705CB0` | [verified call] |
| build | `Scene_BuildQuadtree 0x6E4770`: clears the old tree (`SceneQuadtree_Clear 0x705400`), allocates, `SceneQuadtree_ctor 0x705380`, fills it (`SceneQuadtree_Build 0x7057B0`). Only caller chain: `Renderer_BuildSceneQuadtree 0x6E8360` <- `LoadingScreen_Present_A 0xEC9CD0` (profiler label `SceneQuadTree` there) | [verified]; built once at the end of a world load on the main thread [inferred from the only caller] |
| teardown | `Scene_DestroyQuadtree 0x6E4DE0` (from `0x6E97F0`, scene reset on load / exit) | [verified] |

The quadtree is **static**: it is built from the objects present when the loading screen ends. Objects added later (lot activation, build mode, spawned objects) are found by the cull's linear path or by flags on the object; the exact rule (object `+0x65`, flag `+0x40 bit 0x100`) is in §4 [verified flags, meaning inferred].

### 2.2 SceneObjectManager (`Renderer/SceneObjectManager`, created in `RenderSystem_ReadConfig 0x6E05A0`)

| Offset / item | What | Evidence |
|---|---|---|
| ctor `SceneObjMgr_ctor 0x703510`; dtor `0x702B40`; deleting dtor `0x703C90` | stores listener vtable `0xFFA1F4` (slot 0 = `0x703C90`, slot 1 = `0x703CB0`); registers ~40 message ids with `MessageServer` vtable `+0x24` (incl. `0x3359DC3C`, `0x5B70C75`/`76`, `0x9375C04F`, `0xCB3BB2C9`); `ObjectHiding` config | [verified] |
| `+0x0000..+0x1023` | object table: hash map, 0x409 buckets, key = 64-bit object id; lookup `SceneObjMgr_FindObject 0x6FDFB0`; `+0x1024` = end sentinel, `+0x1028` = "no object" value | [verified decompile] |
| other maps (ctor tags) | `SceneObjectLocationMap`, `SceneNodeMap`, `LotSceneNodeInterfaceMap`, `mSpeedTreeClusters`, `TintOpacityTransitions`, `SceneModelArrayMap`, `mInstanceModelKeyMap`, `mUnfadeableObjects`, `mRegionalFogMap`; `SpeedTreeClusterModelSet` `0x705260` | [verified tags]; offsets not mapped |
| `+0x10C8` | frame countdown, set to 50 by `0x7006B0`, `0x6FDD20`, `0x703430`, counted down in `SceneObjMgr_FrameUpdate 0x701270` (scene update from `RenderSystemService_Update 0x6E3620`, docs/41) | [verified writes]; what fires at 0 not read |
| `SceneObjMgr_HandleMessage 0x703CB0` (5.3 KB) | binary-search switch on message id; each case reads the id pair at `msg+8/+0xC` and calls a small handler in `0x6FE3B0-0x702A90`: model create (`Renderer/SceneModel`, `SceneModelFur`, `SceneModelInstance`, `SceneModelArray` in `0x700E50-0x701000`), the FixupLOD family (`Ghost`, `Sculpture`, `Burninate`, `Scratch`, `TexComp`, `Vampire`, `Werewolf`, ... `0x6FE5F0-0x700470`), the SpeedTree cluster case `0x7006B0` | [verified callees and tags] |

### 2.3 SceneManager, cull filters, LOD scheduler

| Function | Role | Evidence |
|---|---|---|
| `SceneManager_dtor 0x6F1800` (vtable `0xFF91B8`) | unregisters the scene debug options (`Scene Cull`, `Multithreaded Cull`, `Scene Shadow`, ...) when it is the global `[0x11D1868]` | [verified] |
| `SceneManager_Draw 0x6F3C90` | waits for the cull jobs (`+0x28E`, `+0x294`, `+0x29C` and two more) when `+0x28F` is set, runs `Renderer_RunDeferredCallbacks`, else culls inline with `0x6F1B00`; then `0x6E4720`, `0x6E7DB0` (draw the render sets) | [verified] |
| `SceneManager_CullAndBuildRenderSets 0x6F1B00` | camera setup (`0x68D9E0`), frustum build (`0x68DF50`, `0x68DFF0`), cull (`Scene_Cull 0x6E6530` / `Scene_CullAndSort 0x6E6570`), `Renderer/SceneManager/RenderSetEntryBuffer` (`0x6F16B0`) | [verified calls]; callee roles [inferred from call shape] |
| `SceneManager_PumpPendingLoads 0x6F3E90` | per frame from `RenderSystemService_Update`: list `+0x4F8` of pending scene loads, each checked by `0x6F3DC0`, finished ones submit a job and are freed; QPC deadline per entry | [verified] |
| `SceneCullFilter_Render 0x6DF8E0` | render-to-texture pass with its own `SceneCullFilter/ObjectList` cull (`0x6E6530`/`0x6E6570`), from the pre-pass `0x6DFEC0` <- `Renderer_BeginFrame` | [verified]; other cull filter users: `0xA522A0`, `0xA66910`, `0xC48560`, `0xC5EDD0`, thumbnails `0xD62950` |
| `SceneLODScheduler_ctor 0x6E7850` | reads `ForceHighLODObjects`, `RenderSimLODDistances`, `ForceMinspecObjectLOD`, `ObjectSizeCullFactor` (global `0x115B148`) | [verified strings] |

Correction to docs/37 §9: the `0x9DE000-0x9DF000` block is **video capture** (`VideoRecording/VidCapEngine`, `VideoCapture_RenderFrame 0x9DE140`); it only uses the `Renderer/SceneManager/PerFrameMaterial` tag [verified tags]. It is not part of the scene graph.

## 3. Add, remove, move: who calls, which thread

| Path | Chain | Thread | Evidence |
|---|---|---|---|
| Any scene object message (create model, set LOD fixup, transform, SpeedTree cluster) | poster -> `PostRenderMessage 0x692F70` -> `RenderMessageBuffer_Post 0x593E60` (lock `FrameDb+0xD0`, append 0x50 bytes) | poster's thread, usually sim | [verified] |
| Delivery | `RenderSystemService_Update 0x6E3620` -> `FrameDb_ConsumeAndInterpolate 0x693780` (TryEnter `+0x48`) -> `FrameDb_DrainEvents 0x5937B0` -> `RenderMsg_Dispatch 0x693170` -> `MessageServer` `+0x18` -> `SceneObjMgr_HandleMessage 0x703CB0` | **main** | [verified]; the second drain `FrameDb_DrainEventsLocked 0x693620` (EnterCriticalSection `+0x48`, drain everything) runs from `RenderSystemService_Slot14 0x6E3A20` (service vtable `0xFF7D98` = `0xFF7D84 + 0x14`) [verified], also a service slot on the main thread [inferred] |
| SpeedTree cluster rebuild | tree component (`0x95CFD0`, `0x95DC60`, `0x95EC10`) posts `0x3359DC3C` with the tree's id pair -> main drain -> `0x7006B0`: look up the object; if it answers type query `0xC4E8FD0F` (vtable `+0xC`), set the 50-frame countdown and call `0x712480` on that cluster | posters sim, rebuild **main** | [verified] |
| Lot add / remove (streaming) | `Lot::SetActive 0xAC20E0` (main) -> remote call -> sim pump -> `0xAC1130` / `0xAB4C00` -> per object `0xABFAC0` -> object event `0x14` / `0xC` via `GlobalHandlerChain 0x936C90` | sim (docs/28 §5.2) | [verified docs/28]; event `0x14` has no registered handler (docs/50), the scope messages no listener (docs/61) |
| Static quadtree build | `LoadingScreen_Present_A` -> `0x6E8360` -> `0x6E4770` | main, end of load | [verified caller]; thread [inferred] |

`Lot_UpdateObjectSceneNode 0xABFAC0` has many other callers besides lot streaming: `IC_World_CanSeeObjectImpl 0x78F090` (via `0x789DD0`), `WorldService_Update 0xC7E3C0` (via `0x818EA0` -> `0x815C70`, main), `IC_World_SetActiveLotImpl`, `IC_UserToolUtils_UserToolGenericImpl`, `Lot_FinishLoad` [verified `CallerTree`]. So the per-object scene-node update runs on **both** threads depending on the caller [inferred from caller roots: `WorldService_Update` = main, IC_ = sim]; it must therefore not touch main-only scene state directly. The decompile shows it either records a deferred LOD entry in a hash map or fires object event `0x14` (docs/28) [verified], consistent with that.

## 4. Culling pipeline

Per frame on the main thread [verified call chain; per-step meaning inferred from call shape where marked]:

1. `RenderSystemService_Update` (service `+0x1C`): consume + drain messages (scene changes applied), `SceneObjMgr_FrameUpdate 0x701270` (countdowns, transitions), `SceneManager_PumpPendingLoads 0x6F3E90` (calls `0x6F3DC0` once per pending entry at `0x6F3ED7`; `0x6F3DC0` also leads to `SceneManager_Draw` and is the check `IC_CASUtils_IsSceneLoadedImpl` uses).
2. `Renderer_BeginFrame 0x6EBB70`: pre-passes incl. `SceneCullFilter_Render` (`0x6DFEC0` -> `0x6DF8E0`), then up to five frame jobs (M-D3D3a).
3. Draw: `SceneManager_Draw 0x6F3C90` (from `0x6F3DC0`, the UI scene window `0x7EB6D0` via `0x6F3F90`, thumbnails, video capture). Multithreaded mode: wait for the jobs; else cull inline.
4. Cull = `Scene_Cull 0x6E6530` / `Scene_CullAndSort 0x6E6570`: if the scene has a quadtree and the query does not opt out (object `+0x65`), set flag `+0x40 |= 0x100` and use `Scene_CullQuadtree 0x6E4F90` (quadtree query `0x705CB0`, then the object loop), else `Scene_CullLinear 0x6E5880` (walk every object in `+0x04..+0x08`). Per object: bounds tests `0x68D130`, `0x68C780`, `0x68CCF0` (frustum/box) [inferred], model/LOD fetch `0x6FAF40` [inferred], then appended to one of 15 render bins (`0x6E49D0`, `0x6E4D80`, by flag bits `0x1000`, `0x2000`, `0x4000`, `0x10400`) [verified bit tests]. `Scene_CullAndSort` then sorts each of the 15 bins (`0x6E48C0`, 0x30-byte entries) and calls `0x68E440`.
5. LOD choice: `SceneLODScheduler` distances and `ObjectSizeCullFactor` (small-object distance cull) [verified config names; per-object use not traced].
6. Occlusion: no portal/occlusion-query culling found in the scene block. `Cull Backfacing Portals` is a debug option, and the occluder code (`Lighting/LightingOccluder`, `0x69DE20`, `0x6B1190`) belongs to lighting/shadows (M-D3D6) [verified strings; "no occlusion culling for visibility" inferred from the string scan].

## 5. Shared state and locks

| State | Writers | Readers | Lock |
|---|---|---|---|
| Render message back list | any thread (`RenderMessageBuffer_Post`) | main (swap) | `FrameDb+0xD0` [verified] |
| Scene object table, models, maps, quadtree | main (message handlers, quadtree build) | main (cull, draw), render jobs during the frame | **none** [verified: no lock calls in `0x6F4000-0x706000`] |
| Frame ring + whole drain (incl. every scene message handler and cluster rebuild) | main | sim (`SimRender_TripleBufferSync`, `FrameDb_PublishFrame`) | `FrameDb+0x48`: main TryEnter, sim Enter -> **sim waits for the drain** [verified docs/41] |
| `g_SpeedTreeAllocBytes [0x11D8628]` | SpeedTree glue | | non-atomic `ADD`/`SUB` (docs/34); safe because the rebuild is main-only [inferred, now backed by §3] |

Hazard [inferred]: cull jobs read the scene while no lock is held; that is safe only because message handlers run before `Renderer_BeginFrame` submits the jobs, in the same main-thread frame. Any change that applies scene messages from another thread, or during the frame jobs, breaks that ordering.

## 6. Sim-thread cost (existing samples)

35 non-empty `probe-out/profile/*simstack*.jsonl` files, 10,238 sim samples (BP 3,400, SV 1,723, IP 4,439, other 676); self = leaf-most TS3W frame, inclusive = any of the first 12 TS3W frames (docs/34 §3 method) [measured]:

| Block | Self | Inclusive | Note |
|---|---|---|---|
| Scene `0x6DE000-0x706000` | 4 (0.04%) | 121 (1.18%) | 86 of the 121 are `0x6DF2C0` <- `0x5C89E0`: a struct copy that copies three arrays through the array helper `0x4CF560` with element function `0x5C89E0`; a shared copy helper or a stale return address from the partial stack scan, not scene work [inferred]. Real scene work ~0.1% |
| SpeedTree glue `0x70D990-0x718620` | 1 | 2 (0.02%) | cluster rebuild does not run on the sim (§3) |
| Quadtree `0x705000-0x706100` | 0 | 1 | |
| Lot add/update/remove `0xAC1130`, `0xABFAC0`, `0xAB4C00` | 0 | 4 (`0xAC1130`) + 2 (`0xABFAC0`), 0 (`0xAB4C00`) | no camera panning in these profiles (docs/38) |
| Tree component `0x95C000-0x960000` | 0 | 1 | |
| `PostRenderMessage` | 1 | 126 (1.23%) | all render messages (mostly transforms, docs/22) |
| `RenderMessageBuffer_Post` | 13 (0.13%) | 109 (1.07%) | lock `+0xD0` + append |
| `SimRender_TripleBufferSync` | 61 (0.60%) | 64 | wait on S1, i.e. on the main-thread drain |

Main thread: no `*mainstack*` file exists yet [verified file scan], so the cost of the drain, the cluster rebuild and the cull is **[to measure]** with `--mainstack` (docs/20 §7). Attribution list for that run: `0x703CB0` and its case handlers `0x6FE3B0-0x702A90`, `0x712480`, `0x6F1B00`, `0x6E4F90`, `0x6E5880`, `0x705CB0`, `0x6F3E90`, `0x701270`.

## 7. Candidates

| # | Candidate | Expected gain | Cost | Risk | Status |
|---|---|---|---|---|---|
| S1 | **Drain render messages outside the frame lock** (swap under `+0x48`, run handlers after release): the sim stops waiting for scene message handling and cluster rebuilds | part of the 0.60% sim wait, and spikes when many messages land in one frame (lot activation, tree clustering) | medium (detour `FrameDb_ConsumeAndInterpolate`; the interpolation part still needs the lock) | medium: handler order vs. frame consume must stay the same | same as docs/41 P2; measure the main-thread hold time first |
| S2 | **Batch SpeedTree cluster rebuilds**: coalesce `0x3359DC3C` per cluster per frame (each call rebuilds the cluster's per-LOD lists [verified]; that several trees of one cluster post in one burst is [inferred] from the per-tree posters in lot load and `LotClusterTrees`) | main-thread spike during `LotClusterTrees`, lot load and build mode | small (hook `0x7006B0`, mark dirty, rebuild once in `0x701270`) | low-medium: the cluster must be rebuilt before the next draw | needs a count of `0x7006B0` calls per frame (Frida counter) before building |
| S3 | "Defer scene add to main" | none | — | — | **Not needed**: scene adds already run on the main thread through the message queue (§3). The object event listeners (`0x936C90`) turned out empty for event `0x14` (docs/50, docs/61); the native lot add measured small (docs/data/results/2026-10-16-queue-lotstream.md) |
| S4 | Multithreaded cull on by default (`SceneManager+0x28F`) | main-thread cull time moves to job threads | small (flag) | medium: the sim pump also runs cull jobs (1 sample seen), which adds to sim tick time; pair with a budget on the sim pump (docs/36 C1) | check the shipped default and what sets `+0x28F` first |
| S5 | Rebuild the static quadtree after lot activation | cull time when many objects were added after load and go through the linear path | medium | low | only if main-thread samples show `Scene_CullLinear` / quadtree misses |

## 8. Open items

- ~~Object event `0x14` listeners of `Lot_UpdateObjectSceneNode`~~: answered. No native code registers a handler for event `0x14` (docs/50 §1), and the lot-add script message scope has no listener, so a lot add runs no managed code (docs/61 §1). The lot-add cost is the native per-object work, measured small (docs/data/results/2026-10-16-queue-lotstream.md).
- `SceneManager+0x28F` (multithreaded cull): default value and setter; the 5 job bodies (M-D3D3a).
- What `SceneObjMgr+0x10C8` triggers at 0.
- Main-thread cost of everything in §6's attribution list.
