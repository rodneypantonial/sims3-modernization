# 42. CAS and the Sim builder in TS3W.exe (M-CORE5)

PLAN item 7, row M-CORE5. Static only: Ghidra headless on a private copy of the M-PHY project (`game-copy/ghidra-core5`, symbols CSV imported first); the game was not run. Cost numbers come from the existing `probe-out/profile/*simstack*.jsonl` files only (§6). Steam TS3W.exe 1.67, image base `0x400000`. Builds on docs/21 (CAS category fill, thumbnails), docs/36 (service slots, job masks), docs/39 (resource system), docs/08 §2.5 (CAS caches).

Tags: **[verified]** = read in the decompile or disassembly; **[measured]** = from the sample files in §6; **[inferred]** = reasoning, still to confirm.

## 1. Summary

- **Outfit building does not run on the sim thread.** The script-side calls `SimBuilder.BuildModel`/`ProcessModel` and `SetOutfitInGameObject(Delayed)` only post a message (id `0x30290F0`) to the message server and put the calling script task to sleep until a completion message comes back [verified decompile of `0x5A80A0`, `0x5A8160`, `0x5A9E90`, `0x5ADD40`]. The build itself runs in `SimService_Update 0x5F0E50` and in the message callbacks `0x5AD8F0`/`0x5ADA30`; SimService, TextureCompositor and ObjectDesignerService update in their **main-thread** service slot (`+0x1C`) [verified slots, docs/36 §3.3 convention]. **Correction (docs/61):** `CAS_PostOutfitSwapMsg 0x5ADD40` posts `0x30290F0` to the **script** message server (`ScriptMessageServer_Get 0x7692C0`, slot `+0x1C`; handler registered at `0xD77730`), which is pumped on the sim thread, so the outfit-swap callback is delivered on the **sim thread** [verified site scan, docs/61]. The earlier inference here (delivery on the main thread, because the build callback re-posts itself while the device is busy) is withdrawn for that post. The `BuildModel`/`ProcessModel` posts use the same id; docs/61 did not re-check their server [inferred: same server].
- **What the sim thread does pay** is the script work around it: preset XML ("Complate") parsing and application in `IC_CASUtils_ApplyPresetToPartImpl`, `SimOutfitGetPartPreset`, part searches, plus the Mono interpreter running `SimBuilderTask`, `OutfitUtils` and `SimOutfit` C#. Native CAS self time on the sim thread is **~0.6% (~6 ms/s)**; named CAS internal calls appear on ~1.3% of stacks [measured]. docs/28 S8's 2.7% is mostly interpreter time in C# outfit code (docs/04 F6: `SimBuilderTask` 2.1% of task samples), not native building.
- **`ObjectDesignerService` lock `0x0106B874`** is taken by 45 call sites in 41 functions, all inside `0xB31000-0xB3B000` [verified]. 31 of them are reached only from `World_ObjectDesigner_*` / `CASUtils_PartData*` internal calls, i.e. the **sim thread**; the service's own update `0xB3A960` (vtable `+0x1C`, main thread; also through its callees `0xB39000`, `0xB319E0`, `0xB32030`), slot `+0x14` `0xB3A340` and three virtual-only functions take it too [verified]. So it is a real cross-thread lock between CAS/design-mode script calls and the main-thread designer update; the samples show only 0.01% self and 0.25% inclusive for the whole service, so it is not a live-mode cost [measured].
- **Jobs:** outfit resources (`SimOutfitResource_Parse 0x5E9AA0`, job at `+0xB8`) and CAS part data (`CASPartDataResource_Parse 0x5B2290`, job at `+0x1CC`) submit JobManager jobs with the default mask -1 (docs/36), so any of `JobThread0`, the main pump or the **sim pump (no deadline)** may run them [verified posts; mask inferred, docs/36]. Two blocking waits exist: `SimOutfit_LoadParts 0x5E84D0` and `CASPartData_WaitJob 0x5AF0D0`, both behind a "block" argument [verified]; `CASPartData_WaitJob` is also reached from `IC_CASUtils_SimBuilderCachePartImpl` (sim thread) [verified caller].
- **The SimService has no time budget by default:** `SimService_Update` loops on a QPC deadline that is infinite unless `[SimService+0x139]` is set, and that byte is written only by `IC_CASUtils_SimServiceEnableTimeSlicing` [verified]. When script turns slicing off, one main-thread frame can run every queued outfit build [inferred effect; which callers turn it off not read].
- **CAS part cache:** `CASCache_GetCachedPart 0x5A44A0` scans every cached part matching the age/gender/body-type flags when no instance key is given (the `GetVisibleCASParts` path) [verified]. No removal path was found in the `CAS/CacheMap` block `0x5A3E00-0x5A4C00`, which fits docs/08's "built at startup, never evicted" [inferred: absence in one block, not a whole-program proof].

## 2. Module map

| Range | Content | Tags (allocator strings) |
|---|---|---|
| `0x5A2C00-0x5A4C00` | blend geometry / blend unit resources, `CAS/CacheMap`, `CASCache_GetCachedPart` | `CAS/BlendGeometryResource*`, `CAS/CacheMap`, `CAS/CASCache` |
| `0x5A4B70-0x5AE000` | 199 `IC_CASUtils_*` internal calls (script boundary), part/key/text search, message callbacks `0x5AD610-0x5ADE60` | `CAS/Host`, `CAS/PartSearch`, `CAS/KeySearch`, `CAS/TextSearch`, `CAS/CASSwapReadyCallback` |
| `0x5AE170-0x5AEDF0` | `CASPartCacheService` (builds `CASPartCache.package`) | `CAS/CASPartCacheService` |
| `0x5AF070-0x5B6500` | part data, part, colour-info, fabric resources and factories | `CAS/CASPartDataResource*`, `CAS/PartResource*`, `CAS/*ColorInfoResource*` |
| `0x5B7D10-0x5C1C00` | "Complate" = preset/compositor-template XML parse and apply | `CAS/Complate` (19 tags) |
| `0x5BB6A0-0x5BE900` | compositor cache (MRU), TC dependency, compound blend, design-mode presets | `CAS/CompositorCache`, `CAS/CompoundBlendResource`, `CAS/DesignModePresetResource` |
| `0x5C4300-0x5CA600` | facial/body/fur blends, geometry, hair tone, skin tone ramp | `CAS/FacialBlendResource`, `CAS/GeometryResource`, `CAS/HairToneResource` |
| `0x5CB3B0-0x5DF000` | **model builder**: skin build, part textures, drawables, blend geometries (morphs), LODs | `CAS/BuildSkinJob`, `CAS/ModelBuilder` (55), `CAS/SetupDrawable`, `CAS/LoadBlendGeometries`, `CAS/BuildLOD` |
| `0x5DF180-0x5DFA00` | resource lookup tables | `CAS/ResourceLookup*` |
| `0x5DFE70-0x5E4150` | **SimBuilder** (parts, presets, build steps `0x5E2050`) | `CAS/SimBuilder` (22) |
| `0x5E4DB0-0x5EB550` | **SimOutfit** resource and factory | `CAS/SimOutfitResource*` (36) |
| `0x5EB9C0-0x5F16F0` | **SimService** (sim texture cache, scene objects, update `0x5F0E50`) | `CAS/SimService`, `CAS/SimTextureCache`, `simCompositorCache.package` |
| `0x5F1760-0x5F4250` | skin controller, skin tone, species definitions | `CAS/SkinTone*`, `CAS/SpeciesDefinition*` |
| `0x5F7BA0-0x608600` | **texture builder / compositor** ops (HSV, skin tone, hair tone, fabric), `TextureCompositorQueue` | `CAS/TextureBuilder`, `CAS/TextureCompositorResource` (37), `CAS/TextureCompositorQueue` |
| `0x608510-0x60A920` | material texture compositor init, user preset lists | `CAS/MaterialTextureCompositor`, `CAS/UserPresetListResource` |
| `0x6CBF90-0x6CE300` | renderer-side compositor, `compositorCache.package` | `CAS/CompositedTextureMediator`, `Renderer/MaterialTextureCompositor` |
| `0x787000-0x788000`, `0x794FC0` | `IC_World_ObjectDesigner_*` (47) | |
| `0xB31000-0xB3B000` | **ObjectDesignerService** (designer state, presets, thumbnails) | `World/ObjectDesignerService` (68) |
| `0xD4B000-0xD65000` | UI ThumbnailManager / ThumbnailRenderer (docs/21 §1.2) | `UI/ThumbnailManager` (89) |

2,055 functions, 442 KB in the three CAS/designer ranges [verified, `RangeMap`]. There is no explicit thread check (`GetSimThreadId`, `GetCurrentThreadId`) anywhere in them [verified]; threads below come from callers and service slots.

## 3. Key functions

| Address | Name | Role | Thread |
|---|---|---|---|
| `0x5A80A0` / `0x5A8160` | `IC_CASUtils_SimBuilderBuildModelImpl` / `ProcessModelImpl` | allocate a callback message (`0x5AD8F0` / `0x5ADA30`), post id `0x30290F0`, script task waits (script host `+0x118`/`+0x120`) [verified] | sim (post), task sleeps |
| `0x5AD8F0` / `0x5ADA30` | `SimBuilder_BuildModelMsg` / `ProcessModelMsg` | up to 100 `SimBuilder_BuildStep` calls; on "needs device" re-post itself; then post completion to the script host [verified] | main [inferred] |
| `0x5A9E90` (`0x5AA880` forwards) | `IC_CASUtils_SetOutfitInGameObjectDelayedImpl` | live-mode outfit swap: optional `CAS/CASSwapReadyCallback`, request object, `CAS_PostOutfitSwapMsg 0x5ADD40` (id `0x30290F0`) [verified] | sim (post only; delivered on the sim thread through the script message server, docs/61) |
| `0x5E2050` | `SimBuilder_BuildStep` | one build step (0 done, 1 progress, 2 needs device); skin through `0x5DE400` -> `BuildSkinJob_RunJob 0x5CB3B0` inline [verified callers] | main [inferred] |
| `0x5F0E50` | `SimService_Update` | vtable `0xFE514C+0x1C`; swaps the request list under the SimService request lock (`Lock_Acquire` with the lock in `ecx`; `0xFE4DC4` is the static `-1` timeout pointer its 3 call sites pass, not the lock: correction from the docs/15 [Verify] follow-up and docs/data/results/2026-10-15-verify-findings.md); QPC-deadline state machine; state `0xB` = `SimService_Process 0x5EBC50`; also calls outfit part loading `0x5EEFB0` and skin build [verified] | main [verified slot] |
| `0x5A50F0` | `IC_CASUtils_SimServiceEnableTimeSlicing` | writes `[SimService+0x139]`; `0` = no deadline (`0xFFFFFFFF`), else `[+0x2C]` [verified] | sim |
| `0x5E9AA0` | `SimOutfitResource_Parse` | parse outfit, create + submit job `+0xB8` [verified] | resource load thread (caller's) |
| `0x5B2290` | `CASPartDataResource_Parse` | parse part data, submit job `+0x1CC` [verified] | same |
| `0x5E84D0` | `SimOutfit_LoadParts` | per part async request (type `0x034C777B`, callback `0xD4B150`), block mode: `JobManager_Wait` [verified] | SimService (main) |
| `0x5AF0D0` | `CASPartData_WaitJob` | `JobManager_Wait` on `+0x1CC` when asked [verified]; callers `SimBuilder_BuildStep` path and `IC_CASUtils_SimBuilderCachePartImpl` | main or sim |
| `0x5AB440` | `IC_CASUtils_ApplyPresetToPartImpl` | parse preset XML (Complate `0x5BF260`/`0x5BF800`/`0x5BE6B0`/`0x5BFAB0`), apply to the builder part `0x5E2FA0` [verified] | **sim, synchronous** |
| `0x5AB300` | `IC_CASUtils_SimOutfitGetPartPresetImpl` | builds the part's preset string `0x5E6370` [verified] | **sim, synchronous** |
| `0x5A44A0` / `0x5AEF40` | `CASCache_GetCachedPart` / `CASCache_FindPart` | part cache lookup; callers `IC_CASUtils_GetCASPartImpl`, `FindReplacementPartForCategoryImpl` [verified] | sim |
| `0x608510` / `0x608630` | `TextureCompositorQueue` init / `TextureCompositorQueue_Update` | creates `CAS/MaterialTextureCompositor` [`0x11CEB8C`] and the queue [`0x11CEB88`], reads `generateThumbnails`, registers `Timeslicing` 10000; update = vtable `0xFE6B08+0x1C` [verified] | main [verified slot] |
| `0xB3A1D0` | `ObjectDesignerService_Create` | 0x338-byte service, `[0x11EBAE0]` [verified] | startup |
| `0xB3A960` | `ObjectDesignerService_Update` | vtable `0x106BE6C+0x1C`, takes `0x106B874` [verified] | main [verified slot] |
| `0xB396E0` | `ObjectDesigner_SetCASPart` | docs/21: lock, drop old part, sync fetch new part, rebuild presets [verified docs/21] | sim |
| `0xEC2640` / `0xEC1D10` | `IC_GameUtils_WaitForSimService` / `WaitForTextureCompositor` | post notifier (ids `0xD4A8E5CD` / `0xD4A8E5CC`) and sleep the script task until SimService / compositor requests drain [verified] | sim task sleeps, thread does not block |

## 4. Outfit build pipeline, thread per step

1. **Script (sim thread, Mono):** `SimBuilderTask.Simulate` (ageing; `Simulator.Sleep(0)` between steps), `OutfitUtils.SetOutfit`, `Sim.SwitchToOutfit` etc. call `CASUtils.*` getters/setters: `SimBuilderAddPart`, `SetPartPreset`, `ApplyPresetToPart` (preset XML parse, synchronous), `SimOutfitGetPartPreset`, `GetCASPart` (part cache) [verified managed + native].
2. **Request (sim thread):** `SimBuilderBuildModel`/`ProcessModel` or `SetOutfitInGameObject(Delayed)` post message `0x30290F0`; the script task sleeps (`ScriptHost +0x120`) [verified]. `CacheOutfit` / `CachePart` call into the builder directly; `CachePart` may wait on the part data job (`0x5AF0D0`) [verified caller].
3. **Resource load (job threads / pumps):** missing outfit and part resources load through the resource manager; their parse functions submit JobManager jobs, mask -1 [verified posts, mask docs/36]. These jobs can be drained by the sim pump without a deadline (docs/36 C2).
4. **Build (main thread):** message callbacks and `SimService_Update` run `SimBuilder_BuildStep`: part textures, morph blends (`CAS/LoadBlendGeometries`), skin (`BuildSkinJob_RunJob`, called inline here), drawables, LODs [verified call chain; thread inferred from slots].
5. **Composite (main thread, timesliced):** texture compositor queue and material compositor, `[Timeslices] TextureCompositor = 10,10,25,50`, disk caches `compositorCache` / `simCompositorCache` (docs/21 §1.3, docs/08) [verified slot and init].
6. **Swap and notify:** completion message wakes the script task; `CASSwapReadyCallback` fires for delayed swaps [verified objects, order inferred].
7. **Thumbnails (separate):** requests from script ICs go to `UI/ThumbnailManager` (lock `0x1088330`, sim slot `0xD4D6D0`, 11 remote-call sites to the sim queue, docs/36), rendering via `ThumbnailRenderer/RTT` on the device thread (docs/21 §1.2) [verified docs/21, docs/36].

## 5. Locks

| Lock | Taken by | Threads |
|---|---|---|
| `0x0106B874` ObjectDesignerService | 41 functions in `0xB31000-0xB3B000`: `SetCASPart`, `SetObject`, `SetPattern`, `SetBuildBuyProduct`, design preset get/add/count/index, compositor/thumbnail setters, `EndDesigning`, `DesignModeSwap` create/release, `PartDataIsUserPreset`, `PartDataAddDesignPreset`, plus `ObjectDesignerService_Update 0xB3A960`, slot `+0x14` `0xB3A340`, `0xB3AD20`, `0xB35680`, `0xB36D10` (virtual, no direct callers) [verified] | sim (ICs) and main (service update) |
| SimService request list (lock in `ecx`; call sites pass timeout pointer `0xFE4DC4`, docs/15 [Verify] follow-up) | `SimService_Update` (swap) [verified]; poster `0x5EDA20` from `IC_CASUtils_SimOutfitFreeImpl` (docs/15) | main + sim [measured: docs/51 I-42-3, 562 sim vs 31,657 main acquires] |
| `0x1088330` ThumbnailManager | docs/36 | sim slot |
| CAS-internal `Lock_Acquire` | 94 functions in the CAS ranges, mostly texture compositor ops `0x5FED00-0x600800` (27) and the designer service (41) [verified count]; lock objects are members, not globals, so not split further | main mostly [inferred] |
| `g_ResourceLock 0xFB2CD0` | every sync part/preset fetch (docs/39) | any |

## 6. Sim-thread cost (existing samples)

Source: all 16 non-empty `probe-out/profile/*simstack*.jsonl` (10,238 samples: BP 4,076, IP 4,439, SV 1,723; base and fix configs mixed). Method of `tools/harness/sample-split.ts`: leaf-most TS3W frame = self, any of the first 24 TS3W frames = inclusive, nearest function from `game-copy/analysis/functions.csv`. Two corrections [measured]: stack values that equal a function entry exactly are dropped from inclusive (they are pointers left on the stack, e.g. the folded stub `0x5BFB90` showed on 2.7% of stacks at depth 1), and `0xD4E550` is excluded from the ThumbnailManager block (a folded hash lookup shared with D3D buffers and resource type names). 1% is about 10 ms/s (docs/28).

| Block | Self all | BP / IP / SV self | Inclusive all |
|---|---|---|---|
| CAS core `0x5A2C00-0x60AA00` | **0.46%** | 0.32 / 0.68 / 0.23% | 6.4% (upper bound, see below) |
| ... CAS internal calls | 0.07% | | 0.82% |
| ... Complate (preset XML) | 0.07% | | 1.27% |
| ... part cache / CacheMap | 0.09% | | 0.16% |
| ... SimOutfit resource | 0.08% | | 3.6% (stale, see below) |
| ... model builder / skin | 0.00% | | 0.12% |
| ... texture builder / compositor | 0.06% | | 0.33% |
| Renderer compositor | 0.00% | | 0.03% |
| ObjectDesignerService | 0.01% | | 0.25% |
| ThumbnailManager | 0.10% | 0.10 / 0.07 / 0.17% | 0.73% |

Named CAS internal calls on the stack (inclusive, approximate): `ApplyPresetToPart` 0.28%, `SimOutfitGetPartPreset` 0.23%, `CreatePartSearch` 0.17%, `PartSearchGetCurrent` 0.10%, `PartDataGetPreset` / `PartDataNumPresets` 0.07% each, `InjectHairColors` 0.06%, `SimBuilderAddPart` / `SetPartPreset` 0.05% each, `SimBuilderCacheOutfit` 0.04%; together ~1.3% [measured]. The SimOutfit-block inclusive is mostly three return addresses (`0x5E5A05`, `0x5E5E4E`, `0x5E5F15`) sitting below a leaf in the interpreter; native SimOutfit code does not call into Mono, so these are stale frames from the stack scan [inferred] and the block's real inclusive share is near its self share.

Reading: natively, live-mode CAS on the sim thread is ~0.5-0.6% self and at most ~1.3% with its internal calls [measured]. The rest of S8's 2.7% is Mono interpreter time in the C# outfit code (docs/04 F6 task samples: `SimBuilderTask` 2.1%; docs/20 managed samples: `ApplyPresetToPart` 0.49%, `SimOutfit.GetPartPreset` 0.45%). The model build, skin and compositing are off the sim thread (0.00-0.06% self) [measured + §4].

## 7. Candidates

| # | Candidate | Covers | Blocker (docs/28) | Verdict |
|---|---|---|---|---|
| CAS1 | **Cache parsed presets** (Complate) per preset string for `ApplyPresetToPart` / `GetPartPreset`, in script (ts3fixes, memo by outfit key + part) or natively | ~0.5-1% sim (native + interpreter) [inferred from §6 and docs/20] | O (results used at once), but pure in the preset string [inferred] | Best live-mode lever; measure first with `managedstack` during an outfit-change repro |
| CAS2 | **Turn on SimService time slicing** (`[SimService+0x139]`) when script leaves it off, or cap the default deadline | main-thread hitches during many outfit builds (household load, travel, ageing) [inferred] | none for correctness; slower outfit pop-in | Small patch; needs the main-thread sampler (PLAN priority 0) to measure |
| CAS3 | Sim pump skips the shared job queue (docs/36 C2) | CAS outfit/part data jobs no longer run on the sim thread | none known | Already a docs/36 candidate; CAS is one of its users |
| CAS4 | Move `ObjectDesignerService` work off the lock | CAS/design-mode stalls only | S (lock shared with the main-thread update) | Not worth it: 0.01% self in live mode; CAS category fill is layout-bound (docs/21 §6.1) |
| CAS5 | Move outfit composition to a worker | — | already off the sim thread; compositor needs the device | Not needed for the sim thread; main-thread gain unknown until the main sampler runs |
| CAS6 | Bounded or evicting CAS part cache | address space with large CC | every consumer keeps part pointers (docs/08 Phase 3) | Research only; measure bytes per part first (docs/08 open item 4) |

Repro need (rule "repro per fix"): an agent action that changes outfits on N Sims at once (`switch_outfit` per household member, or ageing a Sim) with `simstack` + `managedstack` sampling; none exists yet.

## 8. Names

28 new rows in `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/42>`), applied to `game-copy/ghidra-core5` only through the next import. No existing address was renamed.

## 9. Open items

- Delivery thread of id `0x30290F0`: answered for the outfit-swap post (script message server, sim thread; docs/61); still open for the `BuildModel`/`ProcessModel` posts. Also open: which script callers switch SimService time slicing off.
- Role of the ODS slot `+0x14` `0xB3A340`. (Posters of the SimService request list, timeout pointer `0xFE4DC4`: answered by docs/15 and docs/51 I-42-3.)
- Bytes kept per CAS part in the part cache (docs/08 open item 4).
