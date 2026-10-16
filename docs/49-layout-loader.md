# 49. `.layo` layout loader (M-UI2b)

Static map of how TS3W.exe 1.67 (Steam) loads a UI layout: the resource type, the factory, the parse, how windows are built, what is cached, threads and locks, and the sim-thread cost from existing samples. Builds on [docs/43](43-ui-boundary.md) (UI IC boundary), [docs/39](39-resource-system.md) (resource manager, `GetResource` path, factories) and [docs/21](21-cas-offload.md) (CAS tile fill). Static only: no game run. Names are in `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/49>`).

## 1. Short answer

- **Format: XML text**, parsed by the UTFWin XML deserializer on every load. Element names `graph` and `object`. Each object carries a numeric class id (parsed with `wcstoul`) and is created through the UTFWin class registry [verified decompile].
- **Raw bytes are cached, parsed layouts are not.** The `.layo` resource (type `0x025C95B6`) holds only a memory stream of the record. It goes into the generic ResourceCache, so repeat loads skip disk and RefPack. Every `LoadLayout` still parses the XML again and builds a new window tree [verified].
- **Thread: the caller's, which is the sim thread for every LoadLayout IC.** `g_ResourceLock` is held only while the resource is fetched. The XML parse and window creation run with no lock [verified call path].
- **Cost in the existing samples is small** (LoadLayout 0.29%, LoadUIImage 0.48% of managed samples). CAS tile fills are the expensive case (docs/21: 1.6-2.4 ms per `GenericCasItem` tile) [measured].

## 2. Functions

| Address | Name | What it does |
|---|---|---|
| `0x7E86F0` | `UI_RegisterResourceFactories` | Called once from `0x7F1EF0` (UI service init). Registers through manager slot `0x28`: LayoutFactory (0x88 bytes), UITextureFactory (0x34), HitMaskFactory (8) and, unless the config flag `UINoAtlas` is set (`0x58B250`), UITextureUVMap [verified] |
| `0x7E85B0` | `LayoutFactory_ctor` | Typed factory base `0x72F190` with type `0x025C95B6` and id `0x025F4565`; vtable `0x101C990`; binder vector `UI/LayoutFactory/Binders` (+0x10); TextureBinder `0x54A320` (+0x2C), HitMaskBinder `0x548AD0` (+0x40); live-instance list (+0x38) [verified] |
| `0x7E9370` | `LayoutFactory_Init` (slot `0x10`) | Sets `g_LayoutFactory` `[0x11D8BF8]`, adds 4 binders to the vector, registers the change callback `0x7E7DE0` for type `0x025C95B6` (`0x733A70`) [verified] |
| `0x7E7ED0` | `LayoutFactory_Shutdown` (slot `0x14`) | Clears `g_LayoutFactory` [verified] |
| `0x72F090` | `TypedFactory_CreateResource` (slot `0x1C`, shared base) | slot `0x38` (`0x72F070`, type check, then slot `0x34`) creates the object; slot `0x24` (`0x72F120`, copies the key, then slot `0x3C`) reads it [verified] |
| `0x7E86A0` | `LayoutResource_New` (slot `0x34`) | 0x30 bytes, tag `UI/LayoutResource`, vtable `0x101C77C` [verified] |
| `0x7E74B0` | `LayoutResource_Read` (slot `0x3C`) | Allocates `UI/LayoutResourceData` the size of the record, copies the whole record and wraps it in `UI/LayoutResourceMemoryStream` (`0x4DF120`) at +0x28. **No parse here** [verified] |
| `0x7E7B30` | `LayoutInstance_ctor` | 0xC0 bytes, tag `UI/LayoutInstance`, base `0x54A460` (UTFWin serializable container) [verified] |
| `0x7E7C30` | `LayoutInstance_Load` | `GetResourceEx(key)` -> interface `0x025F4C07` -> stream rewind (slot `0x28`) -> mode (`0x5499D0`) -> `UTFWin_XmlDeserialize` -> release the resource [verified] |
| `0x7E7B90` | `LayoutInstance_LoadIntoWindow` | Same as `LayoutInstance_Load`, but builds into an existing parent window (`0x54A4E0`). Callers: `LoadLayoutAndAddToWindow`, `...ByID` and the change callback [verified] |
| `0x7E7CF0` | `LayoutInstance_LinkLive` | Links the instance into the factory's live list (+0x38), only for one window class id range [verified] |
| `0x7E7DE0` | `LayoutFactory_OnLayoutChanged` | Resource-change callback. Re-fetches the layout and re-applies it to every live instance with the same key. Used for hot reload [verified code]; triggered by auto-update/DDF changes [inferred] |
| `0x54BEA0` | `UTFWin_XmlDeserialize` | Reader `0x54B7A0` (tag `UI/UTFWin/XmlReaderState`), parse `0x54BC50`, free `0x54B720` [verified] |
| `0x54BC50` / `0x54BB80` / `0x54BA10` | `UTFWin_XmlParseGraph` / `_ParseObjects` / `_CreateObject` | Root `graph` or `object`, a loop over the `object` children, then object creation: class id through `wcstoul`, class registry `0x4D8C80` slot `0x20` (interface `0xEE3F516E`), properties read by `0x54B890`, reference fix-up pass at the end [verified] |
| `0x471260` / `0x471040` / `0x471060` / `0x471070` | `XmlReader_Next` / `_NodeType` / `_Name` / `_GetAttribute` | EA XML pull reader with wide strings [inferred from use] |
| `0x548F10` | `HitMaskFactory_ctor` | vtable `0xFC0AA8` [verified] |
| `0x731E50` | `ResourceCache_IsCacheableType` | The ResourceCache refuses 8 types, including `0x019AEF76`, the type that `LoadUIHitMask` passes. `.layo` `0x025C95B6` passes [verified] |

## 3. Load path, threads and locks

`UIManager.LoadLayout` (C#) -> `IC_UIManager_LoadLayoutImpl 0x782BC0` (sim thread):

| Step | Code | Lock |
|---|---|---|
| 1 | `LayoutInstance_ctor`, build the key from the IC arguments (`0x7E91D0`, which Ghidra cannot decompile) | none |
| 2 | `LayoutInstance_Load` -> `ResourceMan_GetResourceEx` (slot `0x0C`): cache probe; on a miss, package probe, read, RefPack decode, `LayoutResource_Read` copy, then `CacheResource(res, 1)` | `g_ResourceLock` for the whole fetch (docs/39 §2.1) |
| 3 | Rewind the stream, `UTFWin_XmlDeserialize`: XML parse, one UTFWin object per `object` element, properties, reference fix-up | **none** |
| 4 | Release the resource. The cache still holds it. | none |
| 5 | For each new top-level window: interface `0x03F29FC8`, slot `0x18` (post-load init) [inferred purpose] | none |
| 6 | `ScriptHost_GetWorld` slot 200 registers a handle (type `0x30`) for the instance and returns it to C# | none |

Every step runs on the sim thread [verified call path]. The window objects are created and linked into the UTFWin tree while the main thread may be drawing it. This is the existing M-UI5 race (docs/43), not a new one [inferred].

`LoadLayoutAndAddToWindow(ByID)` (`0x77A300`, `0x77A440`) does the same through `LayoutInstance_LoadIntoWindow`, under a parent window looked up by handle [verified].

Images and hit masks:
- `LoadUIImage 0x77F350`: `GetResourceEx` with interface `0x057FA5EA`, then `UIImage_BuildTexture` creates a **new UI texture on every call**; the D3D create is queued to the device owner thread (docs/43). Thumbnail keys (type `0x0CC0EC90`) take the ThumbnailManager file path. The IC has no handle cache [verified].
- `LoadUIHitMask 0x779470`: `FindFactory`, then `GetResourceEx` with that factory. Type `0x019AEF76` is excluded from the ResourceCache, so each call reads and decodes the record again [verified].
- Inside layouts, textures and hit masks are resolved by the factory's binders (TextureBinder, HitMaskBinder) [verified wiring; per-binder caching not traced].

## 4. Caching

| What | Cached? | Evidence |
|---|---|---|
| `.layo` raw XML bytes (LayoutResource) | Yes, in the generic ResourceCache under `CacheBudget` (docs/08 §2.5). Kept while referenced and on the flush list until the cache is over budget | `GetResourceEx` calls `CacheResource(res,1)`; the type passes `0x731E50` [verified]; hit rate [not measured] |
| Parsed layout (object graph, window tree) | **No.** Parsed again on every `LoadLayout`. No template, clone or pool | `LayoutInstance_Load` always runs `UTFWin_XmlDeserialize`; the factory keeps only a live-instance list, used for hot reload [verified] |
| Hit masks | No (type excluded) | `0x731E50` [verified] |
| UI images from `LoadUIImage` | Source resource: depends on its type; built texture: no | [verified for the IC] |

## 5. Sim-thread cost (existing samples, sample-split method)

Top-frame split of the existing files under `probe-out/` (sample-split method, docs/20; counted with a one-off script, no new run) [measured]:

| Source | Samples | LoadLayout family | LoadUIImage |
|---|---|---|---|
| Managed, 23 `*managed*.jsonl` | 12,232 | `LoadLayoutImpl` 36 (0.29%) + `LoadLayoutAndAddToWindowByIDImpl` 7 (0.06%) | 59 (0.48%) |
| Native, 35 `*simstack*.jsonl`, top TS3W frame | 10,251 | XML reader + UTFWin serializer + UI glue: 24 (0.23%); 15 of these are the shared helpers `0x5492E0`/`0x549120` that other callers use too (docs/43 counts them as UTFWin core); parse-only functions 5 (0.05%) | (UI glue, see docs/43) |

Managed callers (the first non-UIManager frame):
- LoadUIImage: **58 of 59 from `MotiveBar.Update`**: a new image every motive bar update.
- LoadLayout: **30 of 36 from `SimDisplay.PopulateBuffGrid`** (the moodlet grid rebuilds its items), 4 from `RelationshipsPanel.AddRelationToGrid`, plus single samples from the tooltip header and the interaction queue.
- LoadLayoutAndAddToWindowByID: dialogs, tooltips, notifications (7).

docs/43 gives 0.23% / 0.36% on its own file set; the order of size is the same. These sessions are live play with little CAS. For CAS, docs/21 measured `LoadLayout(GenericCasItem)` at about 97% of the per-tile fill cost (1.6-2.4 ms per tile, 261 hair tiles in one call) [measured, docs/21].

## 6. Candidates

| # | Candidate | Gain | Difficulty |
|---|---|---|---|
| 1 | `MotiveBar.Update`: keep the UIImage handle per motive state and call `LoadUIImage` only when the state changes (script mod) | removes most of the 0.48% | easy |
| 2 | `SimDisplay.PopulateBuffGrid`: update the existing moodlet items instead of destroying and reloading them (script mod) | most of the 0.29% | easy-medium |
| 3 | Layout pooling for CAS tiles (`GenericCasItem`): reuse windows across fills in C# (docs/21 "Later") | CAS tab fill: the only lever on the 1.6-2.4 ms per tile | medium |
| 4 | Native parsed-layout cache: keep the parsed object graph per key in `LayoutInstance_Load` and clone it instead of parsing XML | every LoadLayout; needs a deep-copy path for UTFWin objects (no clone method found) | hard |
| 5 | Prefetch CAS tiles: warm the ResourceCache with the `GenericCasItem` layout and tile thumbnails before the tab opens | small: the layout bytes are already cached after the first tile; thumbnails go through the ThumbnailManager anyway | easy, low value |
| 6 | Cache hit masks (`0x019AEF76` is refused by the cache) | not seen in the samples | low value |

Candidates 1 and 2 are cheap script-side wins in live play; 3 is the CAS case. Candidate 4 is only worth it if 3 cannot reach the callers (EA code that creates layouts the mod cannot see).

## 7. Open items

- Key build `0x7E91D0` does not decompile (bad memory range). Its arguments match the four key dwords [inferred].
- Per-binder caching of textures inside layouts (TextureBinder `0x54A320`) is not traced.
- The ResourceCache hit rate for `.layo` is not measured; a counter on `GetResourceEx` hits for type `0x025C95B6` would show it.
