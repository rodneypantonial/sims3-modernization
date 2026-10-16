# 54. EAText fonts, glyph cache, text layout and localized strings (M-UI3)

Static map of the EAText library (fonts, glyph cache, typesetter, Uniscribe use), the text renderer, and the localized-string lookup behind `LocalizedStringService_GetLocalizedStringByString`. Static analysis plus a re-split of the existing samples only; no game run. Anchors came from docs/43 §6, docs/32 (USP10 imports) and docs/47 (thread ownership of the UTFWin tree).

## 1. Summary

- **String lookup is a linear walk over every loaded string table** [verified decompile `LocStrSvc_GetString_A 0x59C5B0`]. The service keeps an EASTL list at `+0x48` with one node per STBL resource of the current locale (`LocStrSvc_RebuildTableList 0x59BDE0`: ResourceMan query for type `0x220557DA`, filtered on the instance's top byte). Each lookup probes the tables in list order (`STBL_Find 0x59AF50`: 7,919-bucket chained hash keyed on the 64-bit string hash) and stops at the first non-empty hit. A missing key costs one probe per table. There is no cache in the game [verified: no store on the hit path].
- **`GetLocalizedStringByString` re-hashes the key string on every call**: script string to native (`ScriptHost` vtable `+0x58`), `HashStringFNV64_W 0x7CFE40` (FNV-64 over UTF-16), the walk, then a tagged heap copy of the result (`String/LocalizedStringService`) and a new `MonoString` (`ScriptHost` vtable `+0x50`) [verified]. ja-jp gets one extra replace pass [verified string `ja-jp`].
- **Cost [measured, existing samples]:** 0.32% managed self (51 of 15,860 samples), 0.55% with any localization frame on the stack. 22 of the 51 come from **Sim name getters**: `SimDescriptionCore.get_FirstName` 13, `get_LastName` 7, `get_FullName` 2, mostly via `Sim.get_Name` from `HudModel.UpdateSimInfo`. The getters pass the stored name through the lookup on every read (names of generated Sims are localization keys [inferred]). After those: `MotivesPanel.OnMotivesChanged` 8, moodlet timeout text (`SimClockUtils.GetTextRelative` from `SimDisplay.UpdateMoodletTimeouts`) 9, `VisitCommunityLot.SharedVisitVenueTests` 5, the time tooltip 3.
- **Uniscribe is used only for itemizing and line breaks**: `ScriptItemize` and `ScriptBreak` (the only two USP10 imports) are called from a single function, `EAText_UniscribeItemizeBreak 0x450C70` [verified imports and xrefs]. There is no `ScriptShape`/`ScriptPlace`, and no GDI font API (GDI32 imports are DCs, DIB sections and brushes only) [verified]. EAText rasterizes its own bitmap, outline and polygon fonts.
- **Glyph cache:** a shelf-packed atlas, at most **4 pages of 512x512, 1 byte per pixel** (format code 3, A8 or L8 [inferred]), so about 1 MB of texture memory for the UI cache, under one critical section [verified ctor `0x452950`, `AddPage 0x452D30`, `CreateTexturePage 0x6916F0`]. A full page is evicted whole (`ClearPage 0x452630` drops every glyph on it and bumps a generation counter) [verified]; which page gets evicted and when is not traced [inferred LRU or round robin].
- **Threads:** layout (typesetter, Uniscribe) is reached from UTFWin text controls, which run on the sim thread (docs/47) [verified call graph; thread inferred from docs/47]. Drawing runs on main: world text via `FrameDb_ConsumeAndInterpolate` -> ... -> `EATextRenderer_DrawBatch 0x691AE0` [verified chain], UI text inside the main-thread UI draw (docs/47). The glyph cache sits between them and takes its own lock on every operation [verified].
- **Native text cost is small:** EAText blocks are 0.02% of sim-stack top frames, `IC_EAText_GetNumberString`/`GetTimeString` 0.03% managed [measured]. Text layout is not a stutter source in these samples.

## 2. Method

1. Ghidra copy `game-copy/ghidra-ui3` (from `ghidra-ui1`), `ImportSymbols` with the current symbols CSV first (2,729 functions named).
2. `StringXrefs` on `eatext|font|glyph|locali[sz]|stringtable|typeset|bidi|STBL` (184 references) for allocator tags; `RangeMap` over the docs/43 §6 blocks; an import-reference pass over USP10 and GDI32; `XrefsTo`, `CallerTree` (depth 6 to 8) and `DecompileTo` on the lookup path, glyph cache and renderer. Decompiles stay in the scratch folder (not committed).
3. Cost: innermost managed frame and caller chain in the 21 `*-managed.jsonl` files (15,860 samples), top frame and first 12 frames in the 35 `*simstack.jsonl` files (9,575 samples), split by the address blocks below.

## 3. Function list

### 3.1 Localized strings (`String/LocalizedStringService`)

| Address | Name | Role | Tag |
|---|---|---|---|
| `0x59B0C0` | `LocStrSvc_Create` | allocates the 0x58-byte service if service id `0x6224DD27` is not registered | [verified] |
| `0x59AFC0` | `LocStrSvc_ctor` | `Service_ctor(0x6224DD27)`, STBL type `0x220557DA` at `+0x38`, list head `+0x48` (`mStringTableMap`), sets `g_LocStrSvc 0x11CE2D0` | [verified] |
| `0x59ADA0` | `LocStrSvc_Get` | returns `g_LocStrSvc` | [verified] |
| `0x59BDE0` | `LocStrSvc_RebuildTableList` | ResourceMan vtable `+0x20` query; for each STBL key whose instance top byte equals the locale code (`+0x1C`, alternate `+0x24`) appends a 0x20-byte list node (key at `+8`, table pointer at `+0x18`) | [verified] |
| `0x59C5B0` | `LocStrSvc_GetString_A` | walk the list, `STBL_Find` per table, first non-empty hit; copy and `MonoString` create; empty string when nothing hits | [verified]; S3PP `LocalizedStringCache` detours this (docs/17 §8) |
| `0x59AF50` | `STBL_Find` | bucket = low dword of the key mod 7,919 (`0x1EEF`), chain compare of both dwords; end sentinel at `+0x7BBC` | [verified]; S3PP `StblBucketLayoutTuning` rewrites this code |
| `0x59C850` | `LocStrSvc_GetByString` | `HashStringFNV64_W` then `GetString_A` | [verified] |
| `0x59C800` | `LocStrSvc_GetByStringKey` | parses a key string (`0x4AF200`) then `GetString_A` | [verified] |
| `0x7CFE40` | `HashStringFNV64_W` | `wcslen` + `ResourceKey_HashNameFNV64`; 94 callers (resource names, font names, cache manager) | [verified] |
| `0x7437E0` / `0x7437B0` / `0x743790` | the three `GetLocalizedString*` ICs | thin wrappers: script string to native, then the functions above | [verified] |

Per STBL table the bucket array alone is 7,919 x 4 = 31,676 bytes [verified bucket count; struct size about 0x7BE8 inferred from the sentinel offsets]. With the store set's ~3,187 English STBLs (docs/17) that is ~100 MB of bucket arrays and a ~3,200-node walk per missed key [inferred from the counts]. That agrees with S3PP's claim that its STBL bucket layout saves 90 to 290 MB [community].

### 3.2 EAText core

| Address | Name | Role | Tag |
|---|---|---|---|
| `0x450C70` | `EAText_UniscribeItemizeBreak` | `ScriptItemize` + `ScriptBreak` into a buffer tagged `UI/EAText/` | [verified] |
| `0x451050` | `EAText_AnalyzeRuns` | run analysis around 0x450C70; callers `0x4514D0`, `0x459700`, `0x4635A0` | [verified calls], name [inferred] |
| `0x4635A0` | `EAText_Typesetter_LayoutLine` | line layout (`UI/EAText/Typesetter/LineLayout` near `0x463B69`); callers are UTFWin text controls (`0x518920`, `0x53ED10` WinText vtable `0xFBF938`, `0x548580`, Grid `0x52xxxx`, IME/TextRenderer `0x7E5790`) | [verified callers], name [inferred] |
| `0x4667E0` | `EAText_Typesetter_Init` | `BidiRunInfoArray`, `Schedule` | [verified tags] |
| `0x4592B0`-`0x4594E4` | FontServer | `UI/EAText/FontServer`, `FaceArray`, `FaceMap`, `EffectDataList` | [verified tags] |
| `0x455730` | `EAText_FontTypeFromName` | `.bmpFont` / `.polygonFont` | [verified strings] |
| `0x44EE50`-`0x44F210` | BmpFont | `BmpTextureInfo`, `CharGlyphMap`, `KerningMap`, `BmpGlyphMetricsMap` | [verified tags] |
| `0x45C2A0`, `0x45D450`-`0x45E180` | OutlineFont, PolygonFont, PolygonGlyph | glyph metrics maps, polygon glyphs | [verified tags] |
| `0x453160` | glyph effect script | `SetGlyphColor`, `SetGlyphBrush`, `DrawGlyphSmearOutline` ... (text effects) | [verified strings] |
| `0x452FE0` | `EAText_CompareStrings` | `CompareStringW`, behind `IC_EAText_Compare` | [verified] |

### 3.3 Glyph cache (`UI/EAText/GlyphTextureMap`)

| Address | Name | Role | Tag |
|---|---|---|---|
| `0x452950` | `EAText_GlyphTextureMap_ctor` | base vtable `0xFA2170`; `+0x58` max pages = 4, `+0x5C` page size = 512, `+0x60` format = 3, `+0x64` 8 shelf classes with heights from `+0x68` (4, 8, 12, 12, 16, 16, 20, 20 [values verified, meaning inferred]), glyph hash map, critical section `+0xF8` | [verified] |
| `0x452D30` | `EAText_GlyphCache_AddPage` (slot `+0x34`) | under the lock; no-op past max pages; page size at least 64; slot `+0x24` creates the page surface | [verified] |
| `0x452BD0` | `EAText_GlyphCache_AddGlyph` (slot `+0x20`) | under the lock; slot `+0x40` finds shelf space, `+0x50` writes the glyph, returns UVs scaled by the page's `1/size` | [verified flow], name [inferred] |
| `0x452630` | `EAText_GlyphCache_ClearPage` (slot `+0x38`) | under the lock; removes all glyphs on one page, resets shelves, generation `+0x50`++ | [verified] |
| `0x452840` | `EAText_GlyphMap_Find` | key `(font << 16) + glyph code` mod bucket count | [verified] |
| `0x691310` | `TextGlyphCache_ctor` | renderer subclass, vtable `0xFF22D0`, `+0x120 = 256` fallback size | [verified] |
| `0x6916F0` | `TextGlyphCache_CreateTexturePage` (slot `+0x24`) | `Texture_Create2D(pow2(size), pow2(size), 1 level, fmt 6, 2, "EATextRenderer")`, tag `Renderer/EATextGlyphCache/Texture` | [verified] |
| `0x451770` | `EAText_GlyphCache_CreateCpuPage` | the EAWebkit cache's slot `+0x24` (vtable `0x1054BC8`): CPU buffer size x size x bpp (4 for formats `0x20`/`0x21`, else 1) | [verified] |
| `0x724890` | `UI_GetTextGlyphCache` | lazy `UI/TextGlyphCache` singleton `g_UITextGlyphCache 0x11D2FE4` (0x150 bytes) | [verified] |

Three cache instances exist: the UI one (`0x724890`), one owned by the `Services/EAText` startup path (`0x691630` -> `0x691560`, from `App_StartupFlags`), and the EAWebkit cache (`UI/UTFWin/EAWebkitGlyphCache 0x9EF100`) [verified callers of the ctor].

### 3.4 Renderer and font loading

| Address | Name | Role | Tag |
|---|---|---|---|
| `0x6917A0` | `EATextRenderer_Init` | shader params `glyphCacheTexture`, `screenSpaceAdjust`, vertex format; called by `UITextRenderer_Init 0x7E4500` (0xA394-byte `UI/TextRenderer`) and the startup path `0xEC02E0` | [verified] |
| `0x691AE0` | `EATextRenderer_DrawBatch` | GraphicsDevice calls; reached from `FrameDb_ConsumeAndInterpolate` (world text) and `0xEC0560` | [verified callers], name [inferred] |
| `0x691DE0` | `EATextRenderer_StyleFontCmd` | style command `font`: FNV-64 of the font name, resource type `0x062E9EE0`, the whole file copied into `Renderer/FontData`, wrapped in `UI/EAText/MemoryStream`, added to the FontServer (vtable `+0x30`); `effectID`/`effectName` commands | [verified] |

Fonts load once per style definition and stay resident in `Renderer/FontData` [verified load path; no unload traced].

## 4. Threads

| Work | Thread | Evidence |
|---|---|---|
| Localized string lookup, number/money/time formatting | sim (script ICs) | [verified: IC callers]; no lock on the lookup path [verified]. A string-table rebuild on another thread would race the walk; who calls `0x59BDE0` (2 sites: `0x59C430` and one vtable call) is not traced |
| Itemize, break, line layout, glyph metrics | sim | UTFWin text controls call the typesetter; UTFWin paints on the sim thread (docs/47) [call graph verified, thread inferred] |
| Glyph rasterize and atlas insert | sim during layout or main during draw | not traced through the vtables [open]; the cache lock exists for exactly this sharing [inferred] |
| Glyph page texture create (`Texture_Create2D`) | caller of `AddPage` | if `AddPage` runs on the sim thread, this is a device call off the owner thread, like the ThumbnailManager lead in docs/43 [inferred, open] |
| Text draw | main | `FrameDb_ConsumeAndInterpolate` chain [verified]; UI draw under lock `0xFBA5CC` (docs/47) |

## 5. Caches and sizes

| Cache | Key | Size | Eviction | Tag |
|---|---|---|---|---|
| String tables (per STBL) | 64-bit FNV hash of the key name | 7,919 buckets, ~31.7 KB each plus entries | none; the list is rebuilt on content changes | [verified] |
| String lookup result | none | no cache in the game; S3PP `LocalizedStringCache` adds 16,384 slots keyed on the 64-bit key, cleared at 75% full (docs/17 §8.3) | - | [verified absent in the game; S3PP community] |
| Glyph atlas (UI) | (font, glyph code) | up to 4 pages x 512 x 512 x 1 byte = 1 MB | whole page cleared | [verified] |
| Font files | font name hash | the whole resource in `Renderer/FontData` | none seen | [verified load] |

## 6. Sim-thread cost (existing samples only)

Managed sampler, 21 files, 15,860 samples [measured]:

| Frame (innermost) | Samples | Self % |
|---|---|---|
| `LocalizedStringService_GetLocalizedStringByString` | 51 | 0.32 |
| `StringTable.GetLocalizedString` / `HasLocalizedString` | 4 | 0.03 |
| `EAText_GetNumberString` + `GetTimeString` | 6 | 0.04 |
| Any localization frame on the stack | 88 | 0.55 (inclusive) |

Callers of `GetLocalizedStringByString` (first non-localization frame): `get_FirstName` 13, `MotivesPanel.OnMotivesChanged` 8, `get_LastName` 7, `SimClockUtils.GetTextRelative` 6, `VisitCommunityLot.SharedVisitVenueTests` 5, `HudModel.get_TimeTooltipString` 3, `SimDisplay.GetTimeLeftText` 3, `get_FullName` 2, `BookData.get_Title` 2, `Opportunity.get_TargetInteractionName` 2.

Native sim-stack sampler, 35 files, 9,575 samples [measured]: EAText blocks 0.02% top frame, 0.39% within the first 12 frames; only 2 samples have `IC_LocalizedStringService_GetLocalizedStringByString` itself within 12 frames. The managed sampler charges the native time of an IC to the IC frame, so the 0.32% is the lookup's real cost; the native sampler's stack walk does not resolve those samples well (docs/43 §7).

## 7. Candidates

All switches default off; ranked by value for stutter.

1. **One merged string index instead of the list walk** (ts3native): build a single key-to-entry hash when `LocStrSvc_RebuildTableList` finishes (hook its return) and answer `LocStrSvc_GetString_A` from it, keeping the first-table-wins order. Each lookup becomes one probe instead of up to N table probes, and the index cannot go stale because it is rebuilt with the list, unlike S3PP's cache (docs/17 §8.3). Value grows with STBL count (store content). Check first that S3PP is off or not hooking `0x59C5B0`; the two do not combine. Measure: a counter of tables probed per call (next free ts3native counter version, docs/registry.md) and the managed self % of the IC.
2. **Script-side name cache**: `SimDescriptionCore.get_FirstName`/`get_LastName` localize a key on every read (half of the IC samples). Cache the resolved string per SimDescription in a ts3fixes hook, clearing it when the name key or the locale changes. Smaller win (about 0.14% managed, 22 samples), pure script, low risk. Repro: an agent action that reads `Sim.Name` for every Sim in a loop.
3. **Coalesce `MotivesPanel.OnMotivesChanged`**: already docs/43 candidate 2; it also accounts for 8 of the 51 lookups.
4. **Glyph cache size: no action.** 1 MB, no glyph cache frames in the samples. Measure only if CJK or heavy text UIs stutter: count `ClearPage` calls (generation `+0x50`) per minute.
5. **Glyph page creation thread (crash check)**: confirm which thread calls `EAText_GlyphCache_AddPage` -> `Texture_Create2D`. If it is the sim thread, it belongs with the docs/43 thumbnail lead (one `D3DCREATE_MULTITHREADED` or owner-thread deferral fix covers both). Static next step: `VCallSites` on slot `+0x34` of vtable `0xFF22D0`.

## 8. Open items

- Callers of slot `+0x34` (`AddPage`) and `+0x20` (`AddGlyph`) through the vtables: layout (sim) or draw (main).
- Who calls `LocStrSvc_RebuildTableList` and on which thread (travel, content install, locale change).
- Eviction choice when all 4 pages are full (the caller of `ClearPage`).
- The real STBL count per locale in a base-plus-EPs install (needed to size candidate 1).

## 9. Names added

29 rows appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv`, tagged `<docs/54>`: `EAText_UniscribeItemizeBreak`, `EAText_AnalyzeRuns`, `EAText_Typesetter_LayoutLine`, `EAText_Typesetter_Init`, `EAText_GlyphCache_CreateCpuPage`, `EAText_GlyphPage_ctor`, `EAText_GlyphTextureMap_ctor`, `EAText_GlyphMap_Find`, `EAText_GlyphCache_AddGlyph`, `EAText_GlyphCache_AddPage`, `EAText_GlyphCache_ClearPage`, `EAText_CompareStrings`, `EAText_FontTypeFromName`, `TextGlyphCache_ctor`, `TextGlyphCache_CreateTexturePage`, `EATextRenderer_Init`, `EATextRenderer_DrawBatch`, `EATextRenderer_StyleFontCmd`, `UI_GetTextGlyphCache`, `g_UITextGlyphCache`, `UITextRenderer_Init`, `HashStringFNV64_W`, `LocStrSvc_ctor`, `LocStrSvc_Create`, `LocStrSvc_RebuildTableList`, `LocStrSvc_Get`, `g_LocStrSvc`, `LocStrSvc_GetByString`, `LocStrSvc_GetByStringKey`.
