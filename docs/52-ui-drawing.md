# 52. UI drawing into D3D9 (M-UI5)

This is a static map of TS3W.exe (Steam 1.67), made on a Ghidra copy with our symbols applied. No game was run for it. The main-thread costs come from the existing main samples of the 2026-10-15 verify runs (`probe-out/profile/verify-2026-10-15-{bp,ip}-verify-mainstack.jsonl`): 2,991 samples, with stacks capped at 24 frames. 1% of main samples is about 10 ms/s of wall time.

Tags: **[verified]** means read in a decompile, the disassembly, the xrefs or a vtable dump. **[measured]** means it comes from the sample files above. **[inferred]** means it is reasoning that still needs to be confirmed.

Builds on docs/47 (window tree, lock `0xFBA5CC`), docs/49 (layouts are built on the sim thread), docs/43 (UI ICs, PendingTextures, the ThumbnailManager crash lead), docs/37 (device wrapper) and docs/41 (frame loop).

## 1. Summary

- **The UI is a retained, recorded command stream.** On the sim thread, the paint message `0xD` makes each dirty window record into a render context (RC, vtable `0xFBA310`). Each RC call (fill rect, frame, image, text) appends 7-dword vertices and *batch records* (texture, mask texture, primitive type, vertex count) to a `Renderable2D` [verified]. Batches with the same textures and type are merged at record time [verified: `Renderable2D_BeginBatch 0x507460`]. Every texture is AddRef'd per batch [verified].
- **A display list (0xF0 bytes, pooled) is one window's frame.** It holds its renderables (each with the "renderer" object that will draw it), the window's effect (AddRef'd), its size and depth, and two transform slots (`+0x30`/`+0x90`, flip byte `+0xE`) with the colour at slot `+0x50` [verified].
- **Main thread:** `WindowManager_Draw 0x50B270` takes `0xFBA5CC` twice. The first hold covers the PendingTextures drain and the adopt; the second covers `UTFWin_DrawDisplayLists 0x50EB20`, which is the whole UI walk *including the 3D scene* (`SceneWindow_Draw`) [verified]. Each renderable is drawn by the shared **UI renderer** (vtable `0xFFCFE8`). That renderer batches up to 64 batches and 16 per-window constant sets ("windata": a 4x4 matrix plus an RGBA colour) per flush, through the `uishader` effect (`texMap`, `texMaskMap`, `texDepthMap`). It writes the vertices into the immediate dynamic VB and draws with `Device_ImmQuadFlush` [verified].
- **Text glyphs are rasterized on the main thread, during the draw.** `UIGlyph_ExpandSpan 0x505EF0` looks each glyph up in the UI glyph cache `[0x11CC9A8]`. On a miss it rasterizes the glyph through the font, then Lock/Add/Unlock on the cache page texture (`EATextGlyphCache` vtable `0xFF22D0`; page create `0x6916F0` = `Texture_Create2D`) [verified]. The cache global is read only by that function at runtime [verified xrefs], and the EAText cache slots take EAText's own locks [verified callees].
- **Race verdicts (§4):** no sim-thread UI path touches D3D or the published or drawn lists without the lock, except one benign unlocked read of an old entry's depth fields. Destroying a window on the sim thread while main draws is safe: everything in the old display list holds its own atomic reference, and SceneWindow's renderable holds a reference on its scene object [verified]. The effect parameter tear (docs/47 R1) and the ThumbnailManager sim-thread texture create (docs/43) remain the open items.
- **Cost on main [measured]:** UI-pass own work (leaf-self in the UI renderer, the UTFWin draw walk and glyph code) is about **1.1% of main samples (~11 ms/s)**. The biggest single leaf is the vertex copy into the dynamic VB, `UIRenderer_FillVB 0x723F50` (0.47%). `WindowManager_Draw` is on 4.7% of main stacks, and 2.7% of those are under `SceneWindow_Draw` (the 3D scene). Lock waits on `0xFBA5CC` from the main side reach at most 0.2%.

## 2. Functions

### Record side (sim thread, paint message `0xD`)

| Address | Name | What it does |
|---|---|---|
| `0x50CF40` | `Window_PaintIntoRC` | Sets the RC renderer (`window+0x208`, or the default `0x505EA0`), sets the clip (style `0x400`, or bit `0x1F9&4`, which also intersects the parent clips), dispatches `0xD`, then `UTFWinRC_CommitRenderable` [verified] |
| `0x50E7D0` / `0x5064F0` | `UTFWinRC_ctor` / `UTFWinRC_BaseInit` | 0xA0 bytes, `UpdateVisualsRC` (sim) or `PlotRC` (main); vtable `0xFBA310`; renderable list `+0x58/+0x5C`, current renderer `+0x40/+0x44`, colour `+0xC`, clip `+0x14..+0x20` with flag `+0x10` [verified] |
| `0x507640` | `UTFWinRC_FillRect` (RC `+0x38`) | Returns early if alpha is 0 (`colour < 0x1000000`); clips; one batch of 4 vertices [verified] |
| `0x506A30` | `UTFWinRC_DrawFrame` (RC `+0x1C`) | Four `FillRect` calls [verified] |
| `0x508570` | `UTFWinRC_DrawImageQuad` | Image quad with UV and optional mask UV; clips the UVs together with the rect [verified] |
| `0x509560` | `UTFWinRC_DrawImage` (RC `+0x58`) | Queries the image for interface `0x1BE6AB3`, then `DrawImageQuad`; `+0x50`, `+0x60` and `+0x64` also end in `0x508570` [verified callees] |
| `0x5091A0`, `0x508E20` | RC `+0x30`, `+0x34` | Text-span or glyph quads (they read `[0x11CC9C4]`; no glyph-cache access) [verified callees; role inferred] |
| `0x5065B0` / `0x506550` | `Renderable2D_Alloc` / `_ctor` | 0x50 bytes, pool lock `0xFBA244`; lists `Verts`, `GlyphInfos`, `TextSpans`, `Parts`; vtable `0xFBA3DC` (slot `+0xC` is an empty stub, so drawing always goes through the renderer) [verified] |
| `0x507460` | `Renderable2D_BeginBatch` | Merges with the previous batch when type, texture and mask match; otherwise AddRefs both textures and appends a 5-dword batch record (texture, mask, type, count, first vertex) [verified] |
| `0x507270` | `Renderable2D_AppendVertex` | A 7-dword vertex `{x, y, colour, u, v, u2, v2}` into a free-list node pool (`0x50D5A0` allocates new nodes) [verified] |
| `0x505EB0` | `Renderable2D_EndBatch` | Adds the vertex count to the open batch; drops the batch (and Releases its textures) when it is empty [verified] |
| `0x50E710` | `UTFWinRC_CommitRenderable` | Appends `{renderable (AddRef), renderer}` to the RC's renderable list [verified] |
| `0x509F10` | `UTFWin_DisplayListAlloc` | 0xF0-byte `UI/UTFWin/DisplayList` from the free list `0x114E488`, lock `0xFBA474` [verified] |
| `0x509A70` / `0x509900` | `Renderable2DPool_Init` / `_ctor` | Sets the UI glyph cache `[0x11CC9A8]` and the pool `[0x11CC9B0]` [verified] |

### Publish (sim thread, `WindowManager_UpdateVisuals 0x50DE70`)

Without the lock, the function repaints every dirty window into a new display list. It copies the window's world matrix (`+0x150..+0x190`), its colour (`+0x1F0`), its size and its effect (AddRef) into the list, links the previous list as entry `[2]` and stores the new one in `window+0x1F4` [verified]. Then, under `0xFBA5CC`, it does four things. It writes the transform-only windows into the *back* transform slot and flips `+0xE`. It splices the lists that main retired (`+0x74`) into a local free list. It publishes the new lists to `+0x6C`, merging them in place when an earlier publish was not yet adopted (incremental flag `+0x69`). Finally it sets `+0x68`. After the lock it calls `WindowManager_FreeRetiredVisuals 0x50B640`, which Releases the renderables and effects and returns the nodes to their pools [verified].

### Draw (main thread)

| Address | Name | What it does |
|---|---|---|
| `0x50B270` | `WindowManager_Draw` | Lock: drain + `WindowManager_AdoptVisuals 0x50B1C0`. Lock again: `UTFWin_DrawDisplayLists(+0xB0)` [verified] |
| `0x50B1C0` | `WindowManager_AdoptVisuals` | Full: swaps `+0x6C` with the draw list `+0xB0` (`UTFWin_ListSwap 0x50AB90`) and moves the old draw list to the retired list `+0x74`. Incremental: `WindowManager_MergeIncrementalVisuals 0x50B100` replaces each window's old entry in place and fixes up the depth (`+0x12`) of the next entry [verified] |
| `0x50EB20` | `UTFWin_DrawDisplayLists` | Builds the screen-to-clip transform from the viewport. Pre-pass: each entry's effect `+0x10` decides whether its subtree is drawn first. Main walk in depth order: per entry, it reads the front transform slot, multiplies the colour by the parent colour (`0x513560`) and skips the entry if alpha is 0. With no effect, it calls each renderable through its renderer: `+0x10` begin when the renderer changes, `+0x14` draw, `+0x18` end. With an effect, it calls effect `+0x14`, then `UTFWin_DrawEffectSubtree 0x50E880`, which collects the subtree by depth and calls effect `+0x18(rc, entries, n, mode)` [verified] |
| `0xFFCFE8` | `UIRenderer_vtable` | `+0x10` `UIRenderer_Begin 0x723DF0` (render states through the `0x60F7xx`-`0x60F9xx` state wrappers), `+0x14` `UIRenderer_DrawRenderable 0x724540`, `+0x18` `UIRenderer_End 0x724810`, `+0x1C` `UIRenderer_LoadShader 0x723CD0` (`uishader`, `windata`, `texMap`, `texDepthMap`, `texMaskMap`) [verified] |
| `0x724540` | `UIRenderer_DrawRenderable` | Walks the batches (`Renderable2D_NextBatch 0x506410`, which expands text spans through `0x505EF0`). It flushes when the texture or mask changes, when the flag bits `0x12` or `1` change, when 64 batches are queued, or when 16 windata sets are queued. Each new entry adds one windata set (16 matrix dwords plus a float RGBA colour). Each vertex carries `windata index * 0x500 + alpha` [verified] |
| `0x7243B0` | `UIRenderer_Flush` | Sets `windata`, `texMap` and `texMaskMap`, the sampler and filter state from flag bit 1, the blend enable (`flags & 0xC`) and the blend mode (`flags & 2` and `& 0x10` pick one of three engine blend pairs). It then runs the effect pass with the callback `0x7243A0` -> `UIRenderer_FillVB` [verified calls; the state meaning is inferred] |
| `0x723F50` | `UIRenderer_FillVB` | Locks the immediate VB (`0x60FE50`) in chunks of at most 2,400 vertices and copies 8-dword vertices out of the *linked vertex nodes*, one node at a time, then `Device_ImmQuadFlush` [verified] |
| `0x505EF0` | `UIGlyph_ExpandSpan` | Glyph-cache lookup (`+0x14`). On a miss: font `+0x54` rasterize, cache page (`+0x2C`), `+0x44` LockRect, `+0x20` add, `+0x48` unlock, font `+0x58` free; `+0x38` at the end [verified] |
| `0xFF22D0` | `EATextGlyphCache_vtable` | `+0x24` `EATextGlyphCache_CreatePageTexture 0x6916F0` (`Texture_Create2D`, power-of-two size, tag `EATextRenderer`), `+0x44` `0x691350` `Texture_LockRect`, `+0x48` `0x6913B0` `Texture_UnlockRect`; `+0x14`, `+0x20`, `+0x34` and `+0x38` call `Lock_Acquire` [verified] |
| `0x724890` / `0x724840` | `UIRenderer_GetOrCreate` / `UIRenderer_ctor` | Creates the `UI/TextGlyphCache` `[0x11D2FE4]` (`0x691310`), the default renderer (`0x505E60(&0x11D2FF0)`) and the pool [verified]. No direct caller was found (it is reached through a pointer) |
| `0x7EBEE0` | `SceneWindow_PaintRenderable` | Paint of the scene window: a 0x14-byte `Renderer/SceneMgrWindow/Renderable` (vtable `0x101D100`) that **AddRefs** the scene object `window+0x270` [verified]. `0x7EB690` is its destructor (Release) |
| `0x691AE0` / `0x691910` | `EAText_DrawGlyphRun` / `EAText_FillGlyphQuads` | The EAText immediate path for world and debug text (callers `0x93F3D0`, `0x946A40`, `0xEC0560`), not the UTFWin UI [verified callers] |

## 3. Data flow sim -> main

1. Sim: a dirty window's `0xD` paint records into `UpdateVisualsRC`. The vertices and batches go into pooled `Renderable2D`s, and the textures are AddRef'd per batch.
2. Sim: `UpdateVisuals` wraps the renderables in a new display list (with the transform, colour, effect and depth) and links the previous list.
3. Sim, under `0xFBA5CC`: transform-only updates go into the back slot and the flip is set; the lists are published to `+0x6C`; main's retired lists are collected.
4. Main, under `0xFBA5CC`: PendingTextures is drained (D3D creates and releases of UI images, docs/43) and the lists are adopted into `+0xB0`. The replaced lists go to `+0x74`.
5. Main, under `0xFBA5CC`: the draw walk runs. Renderables go through the UI renderer to `uishader` and the dynamic VB. The text glyphs are rasterized into the EAText cache pages. The scene window's renderable draws the whole 3D frame.
6. Sim, on its next publish: the retired lists are freed outside the lock (the pools have their own locks, and every refcount is atomic).

Locks [verified]: `0xFBA5CC` (UI publish and draw), `0xFBA4D8` (PendingTextures), `0xFBA474` (display-list pool), `0xFBA244` (Renderable2D pool), plus the RenderableList node pool (`0x509D80`) and EAText's internal locks. The refcounts of `Renderable2D` (`0x505B70`/`0x509C60`) and of the UI textures (`0x731D00`/`0x731D40`) use `lock`-prefixed adds [verified].

## 4. Race verdicts per path

| # | Path | Thread | Touches | Verdict |
|---|---|---|---|---|
| A | Publish, adopt, draw list `+0x6C`/`+0xB0`, transform slots | sim / main | shared lists | **safe**: both sides hold `0xFBA5CC` [verified] |
| B | Repaint reads the old entry (`window+0x1F4`): depth `+0x10/+0x12`, effect `+0x1C` | sim, no lock | an entry that main may have in its draw list; main's incremental merge writes `+0x12` of neighbouring entries under the lock | **benign**: aligned 16-bit reads, so at worst a one-frame wrong depth for one window [verified code; effect inferred] |
| C | Free retired lists `0x50B640` | sim, no lock | lists that main handed back | **safe**: unlinked under the lock, the pools are locked and the refcounts are atomic [verified] |
| D | Window detach or destroy (`WindowManager_DetachWindowRecursive 0x50C7E0`), including layouts rebuilt on every `LoadLayout` (docs/49) | sim | clears `window+0x1F4`, scrubs the queues under the lock, sets full rebuild `+0x30`; never touches `+0x6C`/`+0xB0` | **safe**: the old list stays drawable for one more frame, because its effect, renderables and textures hold their own references and the scene renderable holds the scene object [verified] |
| E | Effect objects (`entry+0x1C`): main calls `+0x10/+0x14/+0x18`, sim runs `UTFWinEffect_Update 0x5172E0` | both, no common lock | effect parameters | **open (docs/47 R1)**: torn alpha or offset values are possible; a crash would need an effect slot that allocates [inferred] |
| F | UI texture lifetime: the last Release can happen on the sim thread (path C) | sim | texture object | **safe**: the D3D release is routed to the owner thread (`UITextureResource_ReleaseD3D`, docs/43) and the refcount is atomic [verified] |
| G | Glyph cache and font rasterize during the draw | main | cache `[0x11CC9A8]`, font objects | **safe for the cache** (main only, EAText locks) [verified]. Font objects are shared with the sim-side text layout; protection by EAText's locks is [inferred] |
| H | D3D from sim-thread UI code | sim | - | **none in the record path**: the RC slots only append to pools [verified for `+0x1C`, `+0x38`, `+0x58`, `0x508570`; the other RC slots are inferred]. The exception stays the ThumbnailManager `CreateTexture`/`LockRect` path (docs/43) |

## 5. Main-thread cost attribution (for the main sampler)

The two verify runs give 2,991 main samples [measured]. 1,064 of them (35.6%) are the 4-frame idle wait in `App_IdleIfNotForeground 0x5887A0`. 1,559 stacks hit the 24-frame cap, so inclusive counts lose their bottom frames, and stale return addresses inflate them (docs/45). Treat the inclusive rows as rough and the leaf rows as the better figure.

| Bucket (innermost wins) | Functions | Main samples |
|---|---|---|
| UI draw, inclusive | `WindowManager_Draw 0x50B270` | 140 (4.68%) |
| of it, the 3D scene | `SceneWindow_Draw 0x7EB6D0` | 80 (2.67%) |
| UI VB fill (leaf) | `UIRenderer_FillVB 0x723F50` | 14 (0.47%) |
| UI walk (leaf) | `UTFWin_DrawDisplayLists 0x50EB20`, `0x5063A0`, `0x50AB90` | 13 (0.43%) |
| UI renderer batching (leaf) | `0x724540`, `0x506410`, `0x7243B0` | 4 (0.13%) |
| Effects (leaf) | `0x523860` (Glide block), `0x549120` | 2 (0.07%) |
| Text, EAText core (inclusive) | `0x44EE50`-`0x467D60`, `UIGlyph_ExpandSpan 0x505EF0` | 7 (0.23%), glyph expand 1 |
| Lock acquire right under `WindowManager_Draw` | `Lock_Acquire` with the caller `0x50B270` | 6 (0.20%); this is an upper bound for main waiting on the sim's publish |
| PendingTextures, UI image create | `0x50A860`, `UITexture_Load 0x7E77E0` | 1 (0.03%) |

For docs/41 §7, add these sub-buckets under "draw (UI + scene)": `ui-vbfill 0x723F50`, `ui-walk 0x50EB20`, `ui-batch 0x724540/0x7243B0/0x506410`, `ui-glyph 0x505EF0 + 0x44EE50-0x467D60`, `ui-lock Lock_Acquire<-0x50B270`, and keep `scene 0x7EB6D0` separate. `KeyList_Find16 0x5492E0` (10 leaf samples, all with non-UI callers) does not belong to the UI.

## 6. Candidates

1. **UI5-C1: UI draw counters (tooling, cheap).** Per frame: `UIRenderer_Flush` calls, `UIRenderer_FillVB` vertices, glyph-cache misses (`0x505EF0` taking the `+0x54` branch), and main hold time and sim wait time on `0xFBA5CC`. This extends UI-L1 from docs/47. It settles whether the 64-batch and 16-windata limits cause many small flushes on heavy HUD or CAS screens.
2. **UI5-C2: contiguous vertex storage or a flat copy in `0x723F50`** (only if C1 shows a large vertex count). The copy walks linked nodes one at a time and is the top UI leaf (0.47% of main, ~5 ms/s) [measured]. A native hook could copy from an array kept beside the node list. The gain is small and the work is medium. It is main-thread work, so it only helps when main is the bound.
3. **UI5-C3 = docs/47 UI-L2, refined: run `0x50EB20` outside `0xFBA5CC`.** Sim writes that main could see during an unlocked walk: the back transform slot plus the flip (a torn transform is possible only if two publishes land inside one walk), and the depth fix-up in path B. Nothing is freed under main, because frees happen only after adopt (path C) [verified]. So it is plausible, with a visual-only risk. The value is low until C1 shows sim waits; docs/47 measured 0 sim samples waiting.
4. **Not candidates:** moving glyph rasterization off main (0.03%), and caching the scene draw out of the UI walk (it is the frame itself; that belongs to the docs/41 frame-loop work).

## 7. Open items

- The RC slots `+0x20`-`+0x2C`, `+0x3C`-`+0x4C`, `+0x54` and `+0x5C` were not decompiled (they are assumed to record only).
- The exact D3D render state behind the `0x60F940`/`0x60F950`/`0x60FA10`/`0x60FA50` wrappers (blend factors, filter).
- Which lock EAText's glyph-cache slots take, and whether the sim-side text layout takes the same one (path G, font objects).
- The caller of `UIRenderer_GetOrCreate 0x724890` (reached through a pointer).

## 8. Names

53 rows appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv`, tagged `<docs/52>`, and applied to `game-copy/ghidra-ui5`.
