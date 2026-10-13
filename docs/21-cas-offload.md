# 21. Create-a-Sim: where the time goes, and what can move off the sim thread

Offload candidate #3 (after route search, docs/18, and resources, docs/08). Research only, 2026-10-13; the game was not launched. Inputs: docs/17 §5 (store set + S3PP: CAS open 2.2 s, hair 3.2 s / 261 items, makeup 1.0 s / 17 items), the decompiled CAS UI in `game-copy/decompiled/UI/Sims3.UI.CAS/`, and Ghidra headless decompiles of the native internal calls the CAS panels make (`DecompileTo.java` depth 2 on `0x782f00 0x7875f0 0x794fc0 0x782bc0 0x5a79f0`; `StringXrefs.java` on thumbnail/compositor strings).

## 0. Short answer

- **The 3.2 s hair number is a synchronous Mono loop on the sim thread**, not thumbnail rendering and not texture compositing. `CASHair.PopulateTypesGrid` builds all 261 tiles in one call; per tile it instantiates a UI layout, does two or three synchronous CAS resource lookups and builds a preset XML string. Thumbnails are only *requested* there and fill in later through the native `UI/ThumbnailManager` queue [verified code path].
- **About 1 s of every category timing is the tab switch itself** (fade out, panel load, fade in): makeup with 17 tiles takes 1.0 s. That leaves roughly **2.2 s for the 261 hair tiles, ~8.5 ms per tile** [inferred from the two numbers; to measure].
- **No piece is pure CPU on owned buffers.** Every per-tile call goes into a shared engine service (UI window system, the global `ObjectDesignerService` lock, the resource manager). A native worker cannot safely run any of them without the same kind of proxying the route worker needed (docs/18). **Nothing was built**; ts3native gets no `[Cas]` section.
- **Best fix is in script (ts3fixes), not native:** (1) fill the hair and makeup grids a few tiles per tick, as EA already does for clothing (`ItemGrid.BeginPopulating(..., 3, ...)`), so the screen answers after the first visible row; (2) memoise the per-part preset data for the CAS session so revisiting a category skips the resource lookups. Expected: first tiles visible in ~1.1-1.2 s instead of 3.2 s; revisits maybe 30-60% cheaper [inferred]. Total fill time only drops with (2).
- **Built 2026-10-13, not yet run in game:** both script fixes and the measurement, see §6.
- **Next measurement** (cheap, decides between the two): a `cas_bench` agent action that times each native call over the hair part list, cold then warm, plus `managedstack` sampling during `cas_category hair` (§4).

## 1. What happens when a CAS category opens

### 1.1 Script side (C#, sim thread) [verified, decompiled `UI`]

Hair (`CASHair.LoadParts` + `PopulateTypesGrid`, `CASHair.cs:871`, `:942`):

1. `CASModel.GetVisibleCASParts(BodyTypes.Hair)`: native, returns every hair `CASPart` for the Sim's age/gender/species (from the `CASPartCache` built at startup, docs/08 §2.5). Filtered in C# by hair type. Called again by `HideUnusedIcons`.
2. `StoreUI.GetCASFeaturedStoreItems(...)`: the store featured-items call, empty with the store off (docs/17 §5.1).
3. For **each part** (261 here), all synchronous:
   - `CASUtils.PartDataNumPresets(key)` -> `IC_CASUtils_PartDataNumPresetsImpl 0x5a79f0`: resource manager lookup of the part (`vtable+0xc`, then the `0x034C777B` interface) and a preset count [verified decompile].
   - `UIUtils.GetCustomContentType(key)`: content-type lookup for the CC/store badge and the filter.
   - `ObjectDesigner.SetCASPart(key)` -> `0x7875f0` -> `FUN_00b396e0`: takes the global **`ObjectDesignerService` lock `0x0106b874`**, drops the previous part, fetches the new one through the resource manager (`DAT_011d3c84 vtable+0x34`, last argument 1 = synchronous [inferred]), then `FUN_00b389a0` rebuilds the design preset list [verified decompile].
   - `ObjectDesigner.DefaultPresetId`, `GetDesignPresetIndexFromId`, `GetDesignPreset(index)` -> `0x794fc0`: builds the preset as a managed XML string. If empty, `Simulator.LoadXMLString` loads the part's preset XML resource (type `0x0333406C`, 53690476) [verified].
   - `new CASPartPreset(part, xml)`: just stores the string (no parsing) [verified].
   - `UIManager.LoadLayout("GenericCasItem")` -> `IC_UIManager_LoadLayoutImpl 0x782bc0`: allocates a `UI/LayoutInstance` and instantiates its window tree, one new layout per tile [verified decompile].
   - `UIManager.GetCASThumbnailImage(key)` -> `0x782f00`: see 1.2. Returns at once.
   - `ItemGrid.AddItem`.
4. Hats (not hair) also loop over every preset of every part (`PartDataGetPresetId`, `PartDataGetPreset`, `DesignPresetCompare` string compares).

Clothing differs: `CASClothingCategory` uses `ItemGrid.BeginPopulating(AddGridItem, mPartsList, 3, layoutKey, null)` (`CASClothingCategory.cs:1296`), i.e. EA already fills clothing **3 rows per tick**, and each `CASClothingRow` does the same per-part calls for every preset (`CASClothingRow.cs:1041-1127`). Smooth Patch changes clothing further to load as you scroll and switches that off in MC CAS (docs/17 §5.2). Hair and makeup tiles are filled in one go [verified].

Where this runs: all CAS UI handlers are Mono code, and Mono runs only on the sim thread (docs/03 §6) [verified for Mono, inferred for the UI event dispatch]. While `PopulateTypesGrid` runs, the sim thread does nothing else, so CAS input and UI animation driven by script stall for the whole fill [inferred]. Rendering continues on the main thread.

### 1.2 Thumbnails: already asynchronous [verified decompile and strings]

`IC_UIManager_GetCASThumbnailImageImpl 0x782f00` builds a thumbnail key (`FUN_005e7480`), then `FUN_00d5c830` allocates a placeholder `UI/ThumbnailManager/ThumbnailUITexture` and a `UI/ThumbnailManager/Image` sized to the next power of two, creates a `Services/FrameDatabase/EventList` (`FUN_00d4e950`) and posts a message (`FUN_00692f70` -> `FUN_00593e60`, id `0x3f204950`). It returns the image handle without loading or rendering anything.

The native `UI/ThumbnailManager` then works the request over later frames: `PendingRequests` / `PendingRequestLookup`, `InProgressRequests`, `LoadFromCacheRequests` (the `Thumbnails\CASThumbnails.package` cache), `LoadFromExportBinCacheRequests`, `LoadFromFeaturedItemsFolderCacheRequests`, and on a miss `ThumbnailRenderer/RTT` + `UI/ThumbnailManager/RenderTargetTexture` (render to texture, D3D9, so main/render thread) [verified strings and their functions, `0xd57d30`, `0xd4e9f0`, `0xd4f810`, `0xd62200`; thread inferred]. `ThumbnailManager_GetPregenerationStatus` / `StartPregeneration` exist; the script pregeneration hook (`ThumbnailHelper.ThumbnailManager_OnStartPregeneration`) only covers food, books and pet breed outfits, **not CAS parts** [verified].

So thumbnails cost render-thread frames and disk reads after the grid is built (icons pop in), and they cost a lot only cold (after `cache-clear` or new content). They are **not** in the 3.2 s, which counts grid items, not loaded icons (`CasHandlers.cs` `Measure`: stable `ItemGrid.Count` for 1.5 s) [verified harness code].

### 1.3 Compositing [verified strings, roles inferred]

`CAS/TextureCompositorQueue/mTextureBuilderQueue` (with a `generateThumbnails` flag, `0x608510`), `CAS/BuildSkinJob/RunJob` (`0x5cb3b0`), `CAS/MaterialTextureCompositor/RequestsCompletedNotifiers`, `Renderer/MaterialTextureCompositor/*` and the `[Timeslices] TextureCompositor = 10,10,25,50` budget show the compositor is already queued, timesliced and partly on job threads. It builds the Sim preview's skin and outfit textures when an item is **applied**, not when a category is listed. Its disk caches are `compositorCache` / `simCompositorCache` (docs/08 §2.5). Not part of the category-open cost.

### 1.4 What the timing numbers contain [verified harness code, split inferred]

`cas_category` measures from the button press: wait for CAS idle, switch tab through the character sheet (old panel fades out, new panel loads and fades in), set the sub-state, then the grid fill. For hair `sync_ms` = `ms` = 3204: everything happened inside the synchronous part. For makeup (17 tiles) 984 ms. With one tab switch in each, the tab switch is ~0.9-1.0 s and hair's 261 tiles ~2.2 s (~8.5 ms per tile) [inferred; the split is what §4 measures]. `cas_open` 2.2 s is CAS entry (Sim build, room load), a separate path.

## 2. Offload options checked

| Option | Verdict | Why |
|---|---|---|
| Run the per-tile native calls on a worker (native ts3native) | **No** | Each touches shared state: UI window system (`LoadLayout`), the global `ObjectDesignerService` lock and its "current part" field (`SetCASPart` is stateful, then `GetDesignPreset` reads it), the resource manager. Results go into managed strings/objects. Same proxying problem as docs/18 with less gain. |
| Prefetch the part and preset XML resources on a worker at CAS open, so the sync lookups hit a warm cache | **Maybe, later** | Only helps if §4 shows the lookups are cold disk/decompress work. Needs the resource manager to be thread-safe for loads from a non-engine thread; `ResourceSystem/AsyncLoader` suggests an async path exists but it is not mapped. Not pure CPU on owned buffers, so not built. |
| Pre-generate CAS thumbnails at load | **No gain on the 3.2 s** | Thumbnails are async and cached on disk across sessions; only cold after `cache-clear`, which the installer runs only after content changes (docs/17 §6b: warm saves ~9 s at load). |
| Parallel compositing | **No** | Already queued, timesliced and partly on jobs; not on the category-open path. |
| Incremental hair/makeup fill (script, ts3fixes) | **Yes, first** | EA's own pattern for clothing (`BeginPopulating`, 3 per tick). Removes the 2.2 s freeze; first visible row after ~15 tiles x 8.5 ms ≈ 0.15 s [inferred]. Total fill time unchanged. |
| Per-session memo of per-part data (script, ts3fixes) | **Yes, second** | Key = part key + age/gender: `NumPresets`, content type, default preset XML. Skips `SetCASPart` + `GetDesignPreset` + `LoadXMLString` on revisits and on hair -> hats -> hair. Must clear on `NewCASContentInstalled` and CAS exit. Gain depends on §4's split. |
| Layout pooling (reuse `GenericCasItem` windows) | Later | Could cut the `LoadLayout` share, but window reuse inside `ItemGrid` is fragile (handlers, tags, `Dispose`). Only if §4 shows layouts dominate. |

Community overlap: Smooth Patch already does the incremental idea for clothing and Create-a-Style (docs/17 §5.2); MC Compact View cuts tiles per part (presets). Neither touches hair/makeup fill or caches part data [community, page text]. A ts3fixes hook must leave clothing alone so it does not fight Smooth Patch.

## 3. Expected gain against the measured 3.2 s

| Fix | Wall time of `cas_category hair` | What the player feels |
|---|---|---|
| none | 3.2 s | ~2.2 s frozen after the tab appears |
| incremental fill | ~3.2 s until the last tile (slightly more: one tick per batch) | tab + first row after ~1.1-1.2 s, rest streams in |
| memo (revisit only) | ~2.0-2.6 s [inferred, if lookups are 30-60% of the per-tile cost] | shorter freeze on revisits |
| both | first row ~1.1 s; full list faster on revisits | |

All [inferred]. A native offload could at best shave the resource-lookup share and would still leave the layout share on the sim thread.

## 4. Test plan (measure first, then build in ts3fixes)

1. **`cas_bench` agent action** (new, in `CasHandlers.cs`): with CAS open on hair, take `GetVisibleCASParts(BodyTypes.Hair)` and time each call in its own loop over all parts with `Stopwatch`: `PartDataNumPresets`, `GetCustomContentType`, `SetCASPart`, `GetDesignPreset` (+ `LoadXMLString` fallbacks counted), `LoadLayout(GenericCasItem)` (dispose the windows after), `GetCASThumbnailImage`. Run it twice: first pass = cold, second = warm. Output ms per call type, count, and the cold/warm ratio. This alone says whether memo, prefetch or layout pooling is worth it.
2. **Split the category timing:** add `fill_ms` to `cas_category` by timing `CASHair.PopulateTypesGrid` / `CASMakeup` fill directly (hook or reflection call), so tab-switch time and fill time are reported apart.
3. **Sampler cross-check:** during `cas_category hair` sample the probe's `managedstack` and `simstack?frames=24` every 50 ms (as `profile-world.ts` does for simstack); expect frames under `PopulateTypesGrid` and the five internal calls above. Not together with `/taskstall` (the two-sampler deadlock, PLAN Open 1).
4. Repro: extend `store-cas-browse` with `hair, makeup, hair` (cold then warm revisit) and `cas_bench`; one run with the store set, one without (`nocc`) to see the per-tile cost growth.
5. Then build the ts3fixes fix with a runtime switch (`patches {casfill:false}`, `{casmemo:false}`) and A/B in one load (`--ab`). Pass: first-row time down, total not worse, no wrong selection highlight (the worn part must still be selected, `mHairTypesGrid.SelectedItem`), no blank tiles, `NewCASContentInstalled` clears the memo.

## 5. Sources

- Decompiled: `UI/Sims3.UI.CAS/CASHair.cs` (`LoadParts` 871, `PopulateTypesGrid` 942, `AddHairTypeGridItem` 1164), `CASClothingCategory.cs` 1296, `CASClothingRow.cs` 1041-1127, `CASMakeup.cs` 453-467, `CASPartPreset.cs`; `ScriptCore/ScriptCore/UIManager.cs` 67/466, `ScriptCore/ScriptCore/ThumbnailManager.cs`; `Sims3GameplaySystems/Sims3.Gameplay.Core/ThumbnailHelper.cs` 248.
- Native (Ghidra, `game-copy/analysis/internal-calls-map.csv` for the IC addresses): `0x782f00`, `0xd5c830`, `0xd4e950`, `0x692f70`, `0x7875f0`, `0xb396e0`, `0xb389a0`, `0x794fc0`, `0x782bc0`, `0x5a79f0`; strings `UI/ThumbnailManager/*` (`0xd57d30`, `0xd4e9f0`, `0xd4f810`), `ThumbnailRenderer/RTT` (`0xd62200`), `CAS/TextureCompositorQueue` (`0x608510`), `CAS/BuildSkinJob/RunJob` (`0x5cb3b0`), `World/ObjectDesignerService/ThumbnailRequests` (`0xb38a90`).
- Timings: `probe-out/repro-cas.log` (2026-10-13, store set + S3PP); harness `mods/ts3agent/src/plugin/Handlers/CasHandlers.cs`.
- docs/03 §5.5-6 (threads, timeslices), docs/08 §2.5 (caches), docs/17 §5 (CAS community fixes), docs/18 (why native offload of engine calls needs proxying).

## 6. Built (2026-10-13; compiled, not run in game)

**ts3fixes `CasGrid.cs`**, tuning `kCasGrid` (**default off**), runtime switches `casgrid` (incremental fill) and `casmemo` (cache, only used by the incremental hair path), counters [59] incremental fills, [60] cache hits, [61] preset lookups run.

- **Hair** (`CASHair.PopulateTypesGrid` replaced): with `mHairType == Hair`, the grid is cleared and filled through `ItemGrid.BeginPopulating(..., 3, ...)` (4 tiles per UI tick, EA's clothing stride). Per tile it does what the original loop does (wardrobe check, content type, disabled-type skip, `SetCASPart` + default preset XML or `LoadXMLString`, `AddHairTypeGridItem` by reflection, worn part selected as it is added); the end call does the original's after-loop work (grid/sort button filter tags, save button, `UpdateFilterButtonState`). Unchanged (original runs): hats (preset loops, share/delete state, temp tile), and any hair list with featured store tiles (the store-on case). `PartDataNumPresets` is not called for hair: its result is only used by the hat loop.
- **Makeup** (`CASMakeup.PopulatePartsGrid` replaced): lipstick/eyeshadow/eyeliner/blush part grid filled the same way; color/delete buttons off during the fill, set from the selection at the end as the original does. Costume makeup (`CASClothingRow` rows) keeps the original.
- **Cancel** [verified in `ItemGrid`]: `Clear`, `Dispose` and a new `BeginPopulating` drop the pending fill, so hair <-> hats, filter changes, undo/redo, new content and leaving the panel (the layout is unloaded) all cancel it. The part list is copied, so `LoadParts` refilling `mPartsList` cannot break the enumerator.
- **Cache** (`casmemo`): content type + default preset XML per part key, cleared on `ICASModel.ClosingDown` (CAS exit), `OnCASContentAdded`, and an age/gender/species change. Pure in the part key for hair [inferred: `SetCASPart(key)` then the default preset of that part; user design presets are extra entries, and hair shows only the default one]. A hit skips `SetCASPart`, so `ObjectDesigner` is left on a different part than the original would leave; every CAS caller that needs it sets it first [verified for `CASHair`; inferred elsewhere]. Thumbnails do not depend on it: the original leaves it on the last part while all thumbnails render later [inferred].
- **Differences from the original** [verified by reading, effect inferred]: the worn part is selected only when its tile was actually added (the original selected the previous tile when the content filter hid it); `mUndoOnDelete` is reset at the start instead of the end; a tile whose calls throw is skipped instead of aborting the fill. During a fill a click on a tile can be followed by `SetCASPart` of later parts (EA's clothing fill has the same interleaving).
- **Why default off:** not run in game yet; the cache's purity and the `ObjectDesigner` interleaving are inferred. Turn on after repro `cas-grid` passes (highlight, no blank tiles, same item count) in both worlds.

**Agent:** `cas_bench` (CAS open; game task, result in `cas_state` timings): over every hair part, cold then warm, total and per-part time of `num_presets`, `content_type`, `set_part`, `design_preset` (after an untimed `SetCASPart`), `layout` (`LoadLayout(GenericCasItem)`, destroyed after the pass), `thumbnail`, plus `load_xml` fallbacks; times from `StopWatch` microseconds (unit of `GetElapsedTimeFloat` inferred). `cas_category` results add `fill_start_ms` (press to fill start = tab switch), `fill_first_ms` (press to first tiles), `fill_ms` (fill start to last tile) and `fill_incremental`, from `ts3fixes.casfill`, recorded with the switch on or off. `patches` reports `casgrid`, `casmemo`, `casgrid_fills`, `casmemo_hits`, `casmemo_lookups`.

**Test:** `node tools/harness/repro.ts run cas-grid --set casgrid=true,casmemo=true`, then the same with `=false,casmemo=false` (cas_open, hair, cas_bench, makeup, hair again, makeup, state). Pass: `fill_first_ms` well under the off run's, `fill_ms` not much worse, the second hair visit faster with `casmemo_hits > 0`, the same item counts, worn hair highlighted, no blank tiles.
