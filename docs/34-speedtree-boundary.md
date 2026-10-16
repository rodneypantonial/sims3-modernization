# 34. SpeedTree boundary (M-ST)

PLAN backlog item 7, row M-ST. Where SpeedTreeRT sits in TS3W.exe (Steam 1.67, image base `0x400000`), what calls it, what it calls, what it costs. Boundary only, not its internals. Static only: Ghidra headless on a private copy of the H1 project (`game-copy/ghidra-st`, read-only), plus the existing sampler files in `probe-out/profile/`. The game was not run.

Tags as in AGENTS.md: [verified] = read in the disassembly, decompile or strings; [measured] = from existing sample files; [inferred] = reasoning, still to confirm; [community] = public knowledge.

## 1. Range and version

**docs/32's range `0x70E000-0x7BF000` is too wide.** About 550 KB of it is unrelated EA code: resource system (`ResourceSystem/AsyncLoader`, `PackedFile`), scripting ICs, routing (`Routing/SlotArrayDestination`), UI, footprints and the shader system (`0x79A000-0x7A0E50`) [verified: string references per 16 KB bucket]. SpeedTree is three separate blocks:

| Block | Range | Size | What it is | Conf. |
|---|---|---|---|---|
| **SpeedTreeRT core** | `0x7A0F40-0x7C1D70` | ~135 KB, ~400 functions | IDV's runtime: tree engine, branch/frond/leaf geometry, LOD, billboards, wind matrices, lighting, collision objects, Bezier splines, `.spt` parser (file tag `__IdvSpt_02_` at `0x7BF9D0`) | [verified] bounds: the 56 functions that update SpeedTree's own byte counter `0x11D8628` run from `0x7A1F70` to `0x7C11E0`; the shader system ends at `0x7A0E50`; the helpers `0x7A0F40-0x7A1F70` and the spline code up to `0x7C1D70` are reached only from inside |
| EA islands inside the core | `0x7B6F40-0x7B8320`, `0x7BFBE0-0x7C0600` | ~5 KB + 2.5 KB | EA tree template classes (`SpeedTree/SpeedTreeTemplateData`, own vtables at `0x1016218`, called from world tree code `0x95Cxxx`); EA tree descriptor parser (`pivot`, `radius`, `large/small/shrub`, `fallColor0/1`, `snowParams`, placement flags `inside/outside/slope/terrain`), called from the glue at `0x714C10` | [verified callers], EA authorship [inferred: no byte-counter use, EA vtables and Sims-only keys] |
| **SpeedWind** (IDV wind controller) | `~0x7C2000-0x7C4450` | ~9 KB | Reads `SpeedWind.ini` keys (`NumWindMatricesAndLeafAngles`, `MaxBendAngle`, `GustStrengthFreqDuration`, `BranchOscillationX_LaLsHaHs`, `LeafRocking_*`, `LeafRustling_*`), advances wind, hands out wind matrices and leaf angles | [verified strings and callers]; IDV origin [inferred from the key names, which match IDV's public SpeedWind docs] |
| **EA glue** (renderer side) | `0x70D990-0x718620` | ~44 KB, ~80 functions | `Renderer/SpeedTree/Service`, `SpeedTree/SpeedTreeVariant`, `SpeedTree/SpeedTreeClusterModel` (per-LOD branch/frond/leaf index lists), vertex/index buffers, `SpeedTree.ini`, LOD distances | [verified strings] |
| EA world tree data | `0x95BA60-0x95DC60` | ~8 KB | `SpeedTree/TreeData`, `SpeedTree/TreeTemplateData`, `SpeedTree/ShapeDataVector`: the per-world list of placed trees (records of `0x50` bytes) | [verified] |

**Version: 4.x** [inferred]. No version string and no RTTI (built without it) [verified: string and `.?AV` scan]. The evidence: `.spt` tag `__IdvSpt_02_`; class names in allocator tags (`CSpeedTreeRT`, `CTreeEngine`, `CWindEngine`, `CFrondEngine`, `CLightingEngine`, `CLeafGeometry`, `CSimpleBillboard`, `CProjectedShadow`, `CMapBank`, `SEmbeddedTexCoords`, `SCollisionObjects`); error strings naming the public 4.x API (`SetBranchLightingMethod() has no effect after Compute() has been called`, the same for leaf/frond lighting and branch/leaf/frond wind, `Compute() called more than once`, `DeleteTransientData() called with no intact transient data`, `GetBoundingBox()`, `SetCamera()`/`GetCamera()` requires non-NULL position). That is the `CSpeedTreeRT` API of IDV's SpeedTreeRT 4.x SDK [community: public API docs]; SpeedTree 5 replaced it with a different API and `.srt` files [community].

**EA rebuilt SpeedTreeRT from source** [inferred]: the RT code allocates through EASTL containers and EA's allocator with EA-style name tags (`SpeedTree/Branch`, `SpeedTree/LeafLods`, ...) and keeps the byte counter at `0x11D8628`. A stock 4.x library would use its own allocator interface and STL.

## 2. Calls in

### 2.1 Engine to EA glue (the real entry points)

| Caller | Callee | Proposed name | Subsystem | Thread | Conf. |
|---|---|---|---|---|---|
| `0xEC9680` (app init, registers `World/WorldComponentsService` and more) | `0x712CD0` | `SpeedTreeService_Register` (service id `0x87434D8D`, 16-byte object, vtable `0xFFB6C4`) | renderer service registry `0x59EFE0`/`0x59F500` | main, once | [verified call]; main thread [inferred: init next to the `World/Service` registration, docs/28 §5.2] |
| static init `0xF82DF5` | `0x7140D0` | `SpeedTreeRenderer_ctor` (vtable `0xFFB7F0`, `SpeedTree/mVariants` list, QPC timer) | renderer | main thread, CRT static init before `main` | [verified call], thread [inferred] |
| vtable `0xFFB7F0` slot 0 | `0x714410` | `SpeedTreeRenderer_Init`: white texture, `DefaultTextures`, shader constants (`WindMatrices`, `LeafRockRustleRotations`, `TreeLocations`, `BillboardLodLimits`, `TreeFallColor0/1`, `TreeSnowParams`, ...), reads `SpeedTree.ini` and `SpeedWind.ini` | renderer | main/render [inferred] | [verified strings] |
| vtable `0xFFB7F0` slot +8 | `0x7135B0` | `SpeedTreeRenderer_RenderFrame`: `SetCamera`/`GetCamera`, wind matrices, tree count from world tree data `0x95BBE0` | renderer frame | main/render [inferred: the renderer runs on the main thread, docs/28] | [verified calls] |
| `World/Service` update `0xC7E3C0` (`+0xCD`) | `0x712F00` | `SpeedTree_UpdateWindAndTime(dt)`: adds dt to a global clock (wraps at `[0xFFB6BC]`), `CSpeedTreeRT::SetTime` `0x7BA0C0`, then only when `SpeedTree.ini` wind is on (`[0x115B488]+0x15`): SpeedWind advance `0x7C2FA0`, reads all wind matrices `0x7C2700`/`0x7C2710` and leaf rock/rustle angles `0x7C2CB0`/`0x7C2D00` | world update | **main** [verified chain: `0xECA960` -> service list -> `0xC7E3C0`, docs/28 §5.2] | [verified] |
| `WorldManager::Update` `0xC6D570` -> `0xD6E350` (`+0x14E9`) | `0x713080` | `SpeedTree_SetWind(strength, dir)` -> SpeedWind `0x7C2650` | world/weather update | **main** [verified chain, docs/28 §5.2] | [verified] |
| `0xD6E350`, `0x6DF8E0`, `0x6EAC70`, `0x6EC0C0`, `0x6EC840`, `0x6F0DB0`, `0xC28CD0`, `0xD64C90`, `0xAE6110`, `0xAE6290` | `0x712BD0` | `SpeedTree_GetSettings` (returns the `SpeedTree.ini` block `[0x115B488]`; flags `+0x15` wind, `+0x1D8`/`+0x1D9` winter) | renderer, world, seasons | any; read-only | [verified] |
| SceneObjectManager `0x703CB0` -> `0x7006B0` | `0x712480` | `SpeedTreeClusters_Rebuild` on scene add of object type `0xC4E8FD0F` (sets a 50-frame countdown `+0x10C8`) | `Renderer/SceneObjectManager/SpeedTreeClusterModelSet`, `Renderer/mSpeedTreeClusters` | not determined; if this runs from the scene add of lot streaming it is on the **sim thread** (docs/28 §5.2 step 5) | [verified call], thread [inferred, to verify] |
| resource factories, vtables `0xFFBD44`-`0xFFBD5C`, `0xFFBCA0` | `0x714AF0`, `0x714A00`, `0x714A50`, `0x714BE0`, `0x715120` | `SpeedTreeVariant` factory create/destroy (`SpeedTree/Factory`, `SpeedTree/DataFactory`) | resource system | load path; thread not determined | [verified data refs] |
| `IC_World_GetNumSpeedTreesImpl` `0x78E730` | `0x95BBE0` | tree count = world tree vector length / `0x50` | script IC (`ScriptCore.World`) | **sim** | [verified]; touches only the EA vector, not the RT |

### 2.2 EA glue to SpeedTreeRT (the RT API in use)

All direct RT API calls come from the glue `0x70D990-0x718620` and SpeedWind; no other engine code calls the RT API [verified: every outside caller of the core range, see 2.3]. Names follow the public 4.x API where an error string or the call shape fixes them.

| Glue caller | RT callee | Proposed RT name | Conf. |
|---|---|---|---|
| `0x715B60` (`SpeedTreeVariant` load) | `0x7B8CD0(0xE4, "SpeedTree/CSpeedTreeRT")`, `0x7BB5E0`, `0x7BC840`, `0x7BC0D0`, `0x7B8D00`, `0x7B8770(1)` | operator new (228-byte `CSpeedTreeRT`), constructor, `LoadTree` (from the resource buffer; it holds the lighting-method checks), destructor, delete, a static flag setter | [verified calls]; names [inferred] |
| `0x718290` (`SpeedTreeVariant` build, refcount `+0x320`) | `0x7BB1C0`, `0x7BB220`, `0x7BB190`, `0x7BAE10`, `0x7BAE80`, `0x7BAE50` (all with 0) | `SetBranchWindMethod`, `SetFrondWindMethod`, `SetLeafWindMethod`, `SetBranchLightingMethod`, `SetFrondLightingMethod`, `SetLeafLightingMethod` | [verified strings] |
| `0x718290` | `0x7BAF30`, `0x7BBA50(0,1,1)`, `0x7B8350`, `0x7B9880`, `0x7B9110`, `0x7BB500(.., 0xF)`, `0x7B9240`, `0x7B9330`, `0x7BA0E0`, `0x7BA110`, `0x7BADE0`, `0x7B83B0`, `0x7B84A0`, `0x7B8420` | `SetCamera`, `Compute`, geometry getters (`0x7B9880` allocates the geometry struct, `0x7B9330` frees it), `DeleteBranchGeometry`/`DeleteFrondGeometry` (`0x7BA0E0`/`0x7BA110`), `DeleteTransientData` `0x7BADE0`, small accessors | [verified strings for Compute/Delete*/SetCamera], rest [inferred] |
| `0x710200`, `0x712030`, `0x7129C0` (cluster model per LOD) | `0x7B8880(geom, lod, flag)`, `0x7B9570(lod)`, `0x7B8370`, `0x7B8830`, `0x7B87C0`, `0x7B87F0` | LOD geometry fetch and LOD level set, LOD count getters | [inferred from call shape and the `BranchesAtLod`/`FrondsAtLod`/`LeavesAtLod` tags] |
| `0x7135B0`, `0x712D40` | `0x7BAEC0`, `0x7BAF30`, `0x7B8440` | `GetCamera`, `SetCamera` (static), accessor | [verified strings] |
| `0x712F00` | `0x7BA0C0` -> `0x7B9620` | `SetTime` (static) | [inferred] |
| `0x70D990`, `0x70DD50`, `0x70DEF0`, `0x70DF60`, `0x70E5E0`, `0x70EBB0`, `0x70F100`, `0x714F00`, `0x716E60`, `0x7172B0`, `0x7154E0` | `0x7BB280`, `0x7B8310`, `0x7B84F0`, `0x7B8500`, `0x7B8510`, `0x7B8430`, `0x7B84E0`, `0x7B8820`, `0x7B81C0`, `0x7B8480`, `0x7B8490`, `0x7B6F90` | `GetBoundingBox` (`0x7BB280`, verified string) and 4-30 byte accessors | [verified calls] |
| `0x712F00`, `0x713080`, `0x714240`, `0x7135B0`, `0x7162F0`, `0x716560`, `0x716930`, `0x713260` | SpeedWind `0x7C2FA0`, `0x7C2750`, `0x7C2650`, `0x7C2700`, `0x7C2710`, `0x7C2740`, `0x7C2CB0`, `0x7C2D00`, `0x7C23D0`, `0x7C3EF0` | advance, reset on wrap, set strength/direction, wind matrix count/get, leaf angle count, rock/rustle angles, ini load | [verified calls], names [inferred] |

### 2.3 Calls into the core range that are not SpeedTree

The linker folded identical EASTL template code (identical COMDAT folding), so some functions inside the core range are shared helpers that the whole exe calls: `0x7A3340` (uninitialized copy of 24-byte elements, 22 callers in UI, objects, routing), `0x7AAB20` (EASTL `vector<uint32>::insert(n)`, 26 callers), `0x7AAD40` (routing callers `0x7FC050-0x7FC880`), `0x7AA5C0`, `0x7AA660`, `0x7A3790`, `0x7A37F0`, `0x7B79E0`, `0x7BE4B0`, `0x7C3BF0` [verified decompile of `0x7A3340` and `0x7AAB20`; the others inferred from size, `EASTL/allocator.h` tag or non-SpeedTree callers]. They are not calls into SpeedTree and must be filtered out of any "who calls SpeedTree" list or sampler attribution.

## 3. Calls out

| Target | What | Sites | Conf. |
|---|---|---|---|
| `0x4E3F90` (EASTL allocate -> `0x4E69D0` Malloc), `0x4E69D0` Malloc, `0x4E5920` Free, `0x4E3F70` free-if-non-null, `0x4E3FB0` (aligned allocate) | **EA GeneralAllocator, default MemoryMan heap** (docs/15 Phase 8). Not MSVCR80 `malloc`, so S3SS's mimalloc never sees SpeedTree memory; our `[Alloc]` thread cache does (it hooks `0x4E69D0`/`0x4E5920`) | 289 call sites | [verified] |
| `[0x11D8628]` (`g_SpeedTreeAllocBytes`) | plain `ADD`/`SUB` of the block size at 110 sites in 56 RT functions, no `LOCK` prefix | | [verified listing]; non-atomic, so only safe while SpeedTree allocates from one thread at a time [inferred] |
| CRT: `_CIpow`, `_CIacos`, `_CIasin`, `_ftol2` (`0xF7EC40`), `memcpy`, `memset`, `_onexit` | math and copies | ~40 | [verified] |
| shared EASTL helpers in EA code (`0x405A50`, `0x405FB0`, `0x8621C0`, `0x56DBE0`, ...) and the empty function `0xC0FD60` (folded, Ghidra names it `IC_StreamHost_Flush`) | container code only | | [verified decompile] |
| D3D9, file system, resource system, threads | **none from the RT or SpeedWind**: no import references; the RT gets the `.spt` bytes in a buffer from the glue, and the glue owns textures and vertex/index buffers (EA renderer `0x61A010-0x61A9B0`, not raw `IDirect3DDevice9`) | | [verified: no import or out-of-range flow other than the rows above] |

## 4. Cost (existing samples only)

Pooled over the 16 non-empty `probe-out/profile/*simstack*.jsonl` files (10,251 sim-thread samples: BP 4,077, SV 1,723, IP 4,451), method of `tools/harness/sample-split.ts` (self = leaf-most TS3W frame, inclusive = any of the first 12 TS3W frames) [measured]:

| Range | Self | Inclusive | Note |
|---|---|---|---|
| RT core + SpeedWind `0x7A0F40-0x7C4450` | 1 sample (0.01%) | 27 (0.26%) | 12 of the 27 are the folded EASTL helper `0x7AAD40` under routing; the rest are single hits from the partial stack scan (docs/20). Real SpeedTree work on the sim thread is **~0%** |
| EA glue `0x70D990-0x718620` | 1 (0.01%) | 2 (0.02%) | |
| World tree data `0x95BA00-0x95DD00` | 0 | 0 | |

**Main thread: no data.** All existing sampler files are sim-thread only (`/simstack`); there is no main- or render-thread stack file in `probe-out/` [verified file scan]. SpeedTree's per-frame work (wind `0x712F00`, render `0x7135B0`) runs on the main thread, so its cost is **[to measure]**. In multithreaded mode the main thread renders while the sim thread simulates, so main-thread time costs frame rate, not sim tick rate (docs/28) [inferred].

Not a docs/28 row: no measurable sim-thread cost.

## 5. Memory

| Item | Size | Where | Conf. |
|---|---|---|---|
| `CSpeedTreeRT` object | `0xE4` (228) bytes per loaded tree model | `0x715B60` | [verified] |
| `SpeedTreeVariant` (EA, per species/season variant) | `0x330` bytes, 16-aligned | `0x7154E0` | [verified] |
| tree engine block | `0x13810` (79,888) bytes, freed as `0x13800` + `0x10` | `0x7BDBA0` / `0x7BE670` (`SpeedTree/Engine`) | [verified sizes]; per model [inferred] |
| tree template data | `0x80` bytes | `0x95C030` | [verified] |
| placed tree record | `0x50` (80) bytes each in the world tree vector | `0x95BBE0` | [verified] |
| cluster vertex buffer | sized so one batch stays under **8,000,000 bytes** and 32,767 indices: trees per cluster = min(9, 8 MB / bytes per tree) | `0x718290` | [verified decompile] |

Address-space relevance [inferred]: per-model RT memory is small (~80 KB engine block + geometry per species), and there are few species per world, so the RT heap is likely a few MB. The bigger share is the glue's vertex and index buffers (up to 8 MB per cluster batch); if they are D3D9 managed-pool buffers, Direct3D keeps a system-memory copy inside the 32-bit process. The exact SpeedTree heap size is one counter read away: `[0x11D8628]` holds the live byte count.

## 6. Candidates

| # | Candidate | Why | Effort | Conf. |
|---|---|---|---|---|
| 1 | **Read `[0x11D8628]` in the probe** (memory line next to `/rescache`, per world) | gives SpeedTree's live heap bytes for the address-space budget with no hook | trivial (a read) | [inferred] |
| 2 | **Main-thread timers** around `0x712F00` (wind) and `0x7135B0` (render frame), and a main-thread sampler | the only place SpeedTree can cost anything; nothing measured yet | small (Frida, `stutter-profile.js` style) | [inferred] |
| 3 | Wind throttle: run `0x712F00` every 2nd-3rd frame, or turn wind off through the existing `SpeedTree.ini` flag (`[0x115B488]+0x15`) | SpeedWind advance + matrix copy every frame; config already has an off switch | small; only after #2 shows a cost | [verified flag], gain [inferred] |
| 4 | LOD distances: `RenderTreeLargeLODDistances`, `RenderTreeSmallLODDistances`, `RenderTreeAlphaEdges` in `Config` (read at `0x713430`) | tune per graphics level without code | none (config) | [verified strings] |
| 5 | Cluster batch cap (8 MB, 9 trees) at `0x718290` | smaller batches = less contiguous address space per allocation, more draw calls | patch one constant; only if #1 shows tree buffers matter | [verified constant] |
| 6 | Moving work off the sim thread | **nothing to move**: SpeedTree runs on the main thread. Only open question: whether `SpeedTreeClusters_Rebuild` `0x712480` runs on the sim thread during lot streaming (docs/28 blocker **E**, synchronous scene events); check with a thread-id probe on `0x7006B0` | probe only | [inferred] |

**x64:** SpeedTreeRT 4.x is commercial IDV code; there is no public source or public x64 build of 4.x [community]. EA built it from licensed source, so a 64-bit TS3W would need a SpeedTree re-implementation (the `.spt` 4.x format is not public) or trees replaced by static meshes/billboards (the community Bevy port in docs/24 bakes SpeedTree billboards). The boundary is narrow: the RT has no D3D9, file or thread calls, only the EA allocator and CRT math, so a replacement would sit behind the ~55 RT functions the glue calls (§2.2) [inferred].

## 7. Mapping method notes (for M-D3D, M-UI)

- **Script:** `tools/ghidra/BoundaryCalls.java <out> <lo> <hi>` prints every function in a range (`FN`), every reference into it from outside (`IN`, calls and vtable data refs), every flow out of it (`OUT`), the strings it uses (`STR`) and its computed-call count (`IND`). Run it on a wide range first, bucket `STR` lines per 16 KB, then narrow.
- **Best anchor: a library-private global.** SpeedTree's byte counter `0x11D8628` (found with `ScalarRefs.java`) gave tight bounds; string clusters alone (docs/32 p10-p90) overreached by ~550 KB because EA code is linked in between. Look for the same in other libraries (a debug counter, an error callback pointer, a singleton).
- **EA allocator tags** (`"SpeedTree/..."`, `"Renderer/..."`) are passed by address, so they mark both the library and its EA wrapper; separate them by who uses the private global.
- **Library error strings name the public API** (`SetBranchLightingMethod() has no effect after Compute()`): use them to name entry points, never SDK code.
- **Identical code folding:** EASTL template functions inside a library range get callers from the whole exe. Before calling something an API entry, check its size, its `EASTL/allocator.h` tag and whether its callers are unrelated subsystems; decompile one to confirm. The same applies to sampler attribution (12 of 27 inclusive "SpeedTree" samples were a folded EASTL helper).
- **EA islands inside a library range:** EA vtables in `.rdata` and callers from EA-only code (here `0x95Cxxx`) mark them.
- **Threads:** reuse the verified chains in docs/28 §5.2 (main loop `0xECA960` -> `World/Service` `0xC7E3C0` -> `WorldManager::Update` `0xC6D570` = main thread; IC = sim thread); virtual entry points need a runtime thread-id probe.
- **Sampler data is sim-thread only.** M-D3D and M-UI (main-thread work) need a main-thread sampler before they can report a cost.
