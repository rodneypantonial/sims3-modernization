# 43. UI boundary: C# `Sims3.UI` to native UI (M-UI1)

Static map of every UI internal call (IC) the script layer can make, what the native side does behind it, which thread runs it, and where it hands work to the device or main thread. Anchors for M-UI2 to M-UI5. PLAN.md item 7, row M-UI1.

Sources: the binding table (`tools/internal-calls-map.ts` output), our Ghidra database with `tools/ghidra/symbols/ts3w-1.67.symbols.csv` applied, the decompiled `ScriptCore` extern declarations (return types), and the existing sampler output in `probe-out/profile/` (no new game runs). Tags: [verified] = disassembly, decompiled code or call graph; [measured] = existing samples; [inferred] = not confirmed.

## 1. Summary

- **156 UI ICs**, all bound in `ScriptCore`: `UIManager` 110, `ThumbnailManager` 39, `LocalizedStringService` 7, `EAText` 4 [verified binding table]. `Sims3.UI` and `Sims3.SimIFace` wrap them; there is no IC bound to a `Sims3.UI` class directly [verified]. The native bodies sit in one block, `0x777500`-`0x783C00` (ThumbnailManager then UIManager), plus `0x741820`-`0x743840` for EAText and strings [verified].
- **Thread:** every IC runs on the sim thread, because Mono runs only there (docs/03 §6, docs/29) [verified for Mono]. No UI IC reaches `GetSimThreadId 0x7D99F0` or reads the main/sim thread id globals `0x11D8A88`/`0x11D8A8C` within 6 direct calls [verified call graph]. The window ICs (about 90 of the 110 UIManager ICs) are short: look up the handle, call the window object through its vtable, no lock [verified size and direct callees; what the virtual calls do is M-UI2].
- **Hand-offs that exist** [verified decompile]:
  - UI images (`LoadUIImage`): `UITextureResource_FromImage 0x7E8200` builds the texture object and queues its D3D creation on the `UI/UTFWin/PendingTextures` list under lock `0xFBA4D8` (`UTFWin_QueuePendingTexture 0x50A780`, flag 1). Release (`0x7E7610`, `0x7DBBE0`) frees the D3D object only when `Device_IsOwnerThread` is true, else queues it (flag 0). Who drains the list is [inferred: the owner/main thread; M-UI5].
  - Thumbnail images (`GetThumbnailImage`, `GetCASThumbnailImage`, `GetSaveGameMetadata`): placeholder texture object (`ThumbnailManager_Placeholder 0xD5C830`) and a request posted with `PostRenderMessage 0x692F70` (docs/21 §1.2).
  - `SetResolution`: allocates a `UI/SetResolutionMessage` [verified string in the IC].
  - Trigger hooks (`Add/Remove/Activate/DeactivateTriggerHook`) and `SendBrowserCommand` take a critical section.
- **Missing hand-off, crash lead:** 12 ThumbnailManager ICs (`Generate*`, `StitchPhotoboothPictures`, `UpdateDirtyLotThumbnails`) call `ThumbnailManager_CreateThumbnailTexture 0xD5D910`, which calls `Texture_Create2D` (`IDirect3DDevice9::CreateTexture`, vtable `+0x5C`, MANAGED pool, name "Thumbnail") and then locks, fills and unlocks every level (`Texture_FillLevels 0x618570`) **on the calling thread, with no owner-thread check**, and only then takes the thumbnail lock `0x1088330` [verified decompile and call paths]. From an IC the calling thread is the sim thread. The device is created without `D3DCREATE_MULTITHREADED` (docs/37 [inferred]), so this is an unsynchronised device call that can overlap main-thread rendering [inferred race]. The same function is reached from non-UI ICs: `LoadSaveManager_SaveWorld` (`0x743470` -> `0xD61280`), `CASUtils_SimBuilderShareOutfit`, `DownloadContent_SaveCustomSim`, `World_ObjectDesigner_ShareDesignPreset` [verified caller tree]. The UI texture path above shows EA guarded the same operation elsewhere; this one is a candidate for Error 12 or crash-on-save reports [inferred, not reproduced].
- **Correction to docs/37 and docs/40:** `0x619770` is not texture creation. It is `Texture_ClampToCaps`: rounds width and height up to a power of two when the caps need it and clamps them to the caps maximum, no device call [verified decompile]. The device-touching helper is `0x618570` (`Texture_FillLevels`: `Texture_LockRect`, fill `0x615B80`, `Texture_UnlockRect` per level) [verified]. `LoadUIImage` reaches only `0x619770`; the thumbnail ICs reach both.
- **Services:** `Services/UTFWin` (`0x724310`) and `UI/IME/Service` (`0x7E5110`) have main-thread slots only. The UI service `0x7F14E0` has only a sim-thread slot, `UIService_SimUpdate 0x7F1530`: samples the UI clock, then calls the window manager `[0x11CD080]` through vtable `+0x24` and `+0x28` **on the sim thread** [verified decompile; what those two slots do is M-UI2]. `UI/ThumbnailManager` has both (§5).
- **Sim-thread cost** [measured, existing files]: UI ICs are 1.97% self of 15,860 managed samples (21 runs); `Sims3.UI` frames are on 4.39% of samples. Native UTFWin window-tree code is 1.19% self of 9,575 sim-stack samples (34 runs), mostly with no IC above it (§7). The largest single UI cost is the motive panel: `SimInfo.OnMotivesChanged` 1.11% inclusive.

## 2. Method

1. IC list: `game-copy/analysis/internal-calls-map.csv`, filtered to `ScriptCore.UIManager`, `ThumbnailManager`, `EAText`, `LocalizedStringService`. `CASUtils` (205 ICs, docs/21), `UserToolUtils`, `CameraController` and the `World` window-attach ICs (`World_WinAttachToObj` etc.) touch UI too but belong to their own maps.
2. `tools/ghidra/UIBoundary.java` (added with this doc): per IC, BFS over direct calls to depth 6; reports the first path to `GetSimThreadId`, the thread-id globals, `PostRenderMessage`, `RenderMessageBuffer_Post`, `Texture_Create2D`/`CreateD3D`/`LockRect`, `Device_IsOwnerThread`, `0x619770`, `0x618570`, critical sections and waits. Virtual calls are not followed, so "no flag" means "none in the direct-call graph", not "none at all".
3. `tools/ghidra/Services.java` (added): every caller of `Service_ctor 0x59ECC0` with the vtables it stores and their `+0x1C` (main) / `+0x20` (sim) slots. `0x861C50` is the empty stub.
4. `StringXrefs` on `UI/`, `UTFWin`, `EAText`, `IME/` tags for the address blocks; `DecompileTo` and `CallerTree` on the hand-off functions.
5. Return types from the decompiled `ScriptCore` extern declarations (read locally, not committed).
6. Cost: innermost managed frame and native top frame of the existing `*-managed.jsonl` and `*simstack.jsonl` files, split by IC name and address block.

## 3. IC table

"Returns" is the C# return type (`-` = void, `out/ref` = void with out or ref parameters). "Bytes" is the native body size. "Self %" is the share of managed samples whose innermost frame is that IC (blank = 0). Rows marked "small ... [inferred]" are bodies under 160 bytes with at most 3 direct callees; their work happens in a virtual call into UTFWin.

#### 1a. Layouts and window tree

| Address | IC | Returns | Bytes | Native side [verified call graph, depth 6] | Self % |
|---|---|---|---|---|---|
| `0x7793C0` | RegisterCustomControlFactory | - | 33 | small: handle lookup, virtual call [inferred] |  |
| `0x7793F0` | ShutdownLayout | - | 52 | small: handle lookup, virtual call [inferred] |  |
| `0x779430` | DestroyDrawable (= DestroyWindow, DestroyUIImage, DestroyUIHitMask) | - | 56 | one folded body for 4 ICs; small: handle lookup, virtual call [inferred] |  |
| `0x779540` | GetMainWindow | uint | 64 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x77A130` | InitUIHierarchy | - | 355 |  |  |
| `0x77A2A0` | RegisterCustomControlDelegates | - | 88 | small: handle lookup, virtual call [inferred] |  |
| `0x77A300` | LoadLayoutAndAddToWindow | uint | 314 |  |  |
| `0x77A440` | LoadLayoutAndAddToWindowByID | uint | 296 |  | 0.04 |
| `0x77A570` | DestroyLayout | - | 79 | small: handle lookup, virtual call [inferred] |  |
| `0x77A5C0` | GetClassID | uint | 72 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x77A610` | GetInstanceID | uint | 72 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x77A660` | GetCustomControlID | uint | 151 | small: handle lookup, virtual call [inferred] | 0.03 |
| `0x77A700` | IsCustomProxyControl | bool | 89 | small: handle lookup, virtual call [inferred] |  |
| `0x77A760` | GetParentWindow | uint | 110 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x77A7D0` | GetChildByID | uint | 123 | small: handle lookup, virtual call [inferred] | 0.04 |
| `0x77A850` | GetChildByIndex | uint | 199 |  |  |
| `0x77A920` | AddToTopWindow | - | 119 | small: handle lookup, virtual call [inferred] |  |
| `0x77A9A0` | RemoveFromTopWindow | - | 119 | small: handle lookup, virtual call [inferred] |  |
| `0x77AA20` | IsWindowAttached | bool | 91 | small: handle lookup, virtual call [inferred] |  |
| `0x77AA80` | AddChild | - | 143 | small: handle lookup, virtual call [inferred] |  |
| `0x77AB10` | RemoveChild | - | 143 | small: handle lookup, virtual call [inferred] |  |
| `0x77ABA0` | DestroyAllChildren | - | 73 | small: handle lookup, virtual call [inferred] |  |
| `0x77ABF0` | MoveDrawOrder | - | 117 | small: handle lookup, virtual call [inferred] |  |
| `0x77AEE0` | Invalidate | - | 73 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x77D370` | WinAttachToCycle | - | 142 | small: handle lookup, virtual call [inferred] |  |
| `0x77D400` | WinDetachFromCycle | - | 59 | small: handle lookup, virtual call [inferred] |  |
| `0x77F2A0` | ToggleDebugLayoutNamesVisible | - | 170 |  |  |
| `0x77F500` | GetWindowByExportID | uint | 152 | small: handle lookup, virtual call [inferred] |  |
| `0x782BC0` | LoadLayout | uint | 317 |  | 0.23 |

#### 1b. Window properties and controls

| Address | IC | Returns | Bytes | Native side [verified call graph, depth 6] | Self % |
|---|---|---|---|---|---|
| `0x77B240` | SetupScrollbars | - | 382 |  |  |
| `0x77B3C0` | GetUInt32Property | uint | 227 |  | 0.11 |
| `0x77B4B0` | SetFloatProperty | - | 245 |  |  |
| `0x77B5B0` | GetFloatProperty | float | 233 |  |  |
| `0x77B6A0` | SetRectFloatProperty | out/ref | 357 |  | 0.01 |
| `0x77B810` | GetRectFloatProperty | out/ref | 225 |  | 0.01 |
| `0x77B900` | SetVector2FloatProperty | out/ref | 255 |  |  |
| `0x77BA00` | GetVector2FloatProperty | out/ref | 213 |  |  |
| `0x77BAE0` | SetStringProperty | - | 351 |  | 0.03 |
| `0x77BC40` | SetImageProperty | - | 324 |  | 0.03 |
| `0x77BD90` | GetImageProperty | uint | 260 |  |  |
| `0x77BEA0` | GetDrawableProperty | uint | 271 |  | 0.05 |
| `0x77BFB0` | GridSetCellTextValue | - | 118 | small: handle lookup, virtual call [inferred] |  |
| `0x77C030` | GridGeneric | bool + out | 2334 |  | 0.01 |
| `0x77CA20` | ComboBoxGeneric | int | 426 |  |  |
| `0x77D070` | FrameDrawableGeneric | uint | 124 | small: handle lookup, virtual call [inferred] |  |
| `0x77D100` | ButtonAutoSize | - | 70 | small: handle lookup, virtual call [inferred] |  |
| `0x77D150` | TextAutoSize | - | 75 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x77D570` | SetUInt32Property | - | 500 |  | 0.07 |
| `0x77E610` | GetColorPickerSwatches | uint[] | 743 |  |  |
| `0x77F820` | GetStringProperty | string | 440 |  | 0.20 |
| `0x77F9E0` | GridGetCellTextValue | string | 333 |  |  |
| `0x77FB30` | ComboBoxGetValue | string | 308 |  |  |

#### 2. Input, focus, cursor, modal, events

| Address | IC | Returns | Bytes | Native side [verified call graph, depth 6] | Self % |
|---|---|---|---|---|---|
| `0x779580` | GetFocus | uint | 65 | small: handle lookup, virtual call [inferred] |  |
| `0x7795D0` | GetCaptureTarget | uint | 65 | small: handle lookup, virtual call [inferred] |  |
| `0x779620` | SetCursorPosition | out/ref | 20 | small: handle lookup, virtual call [inferred] |  |
| `0x779640` | FreezeMouse | - | 16 | small: handle lookup, virtual call [inferred] |  |
| `0x779650` | GetWindowFromPoint | uint + out | 65 | small: handle lookup, virtual call [inferred] | 0.08 |
| `0x7796A0` | SetOverrideCursor | - | 22 | small: handle lookup, virtual call [inferred] |  |
| `0x7796C0` | GetWindowCursor | uint | 67 | small: handle lookup, virtual call [inferred] |  |
| `0x779710` | GetModalWindow | uint | 63 | small: handle lookup, virtual call [inferred] |  |
| `0x779750` | SetUICallbacks | - | 55 | small: handle lookup, virtual call [inferred] |  |
| `0x77AC70` | SetFocus | - | 117 | small: handle lookup, virtual call [inferred] |  |
| `0x77ACF0` | SetCaptureTarget | - | 90 | small: handle lookup, virtual call [inferred] |  |
| `0x77AD50` | ReleaseCapture | - | 87 | small: handle lookup, virtual call [inferred] |  |
| `0x77ADB0` | GetCursorPosition | out/ref | 39 | small: handle lookup, virtual call [inferred] |  |
| `0x77ADE0` | WindowScreenTransform | out/ref | 157 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x77AE80` | GetPhysicalArea | out/ref | 96 | small: handle lookup, virtual call [inferred] |  |
| `0x77AF30` | InvalidateCursor | - | 84 | small: handle lookup, virtual call [inferred] |  |
| `0x77AF90` | RegisterWindowCursor | out/ref | 158 | small: handle lookup, virtual call [inferred] |  |
| `0x77B030` | BeginModal | bool | 89 | small: handle lookup, virtual call [inferred] |  |
| `0x77B090` | EndModal | bool | 89 | small: handle lookup, virtual call [inferred] |  |
| `0x77B0F0` | IsModal | bool | 87 | small: handle lookup, virtual call [inferred] |  |
| `0x77B150` | RegisterEvent | - | 78 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x77B1A0` | DeRegisterEvent | - | 78 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x77B1F0` | DeRegisterAllEvents | - | 73 | small: handle lookup, virtual call [inferred] | 0.01 |

#### 3. Effects and trigger hooks

| Address | IC | Returns | Bytes | Native side [verified call graph, depth 6] | Self % |
|---|---|---|---|---|---|
| `0x779790` | RemoveTriggerHook | bool | 25 | takes a critical section |  |
| `0x7797B0` | ActivateTriggerHook | bool | 25 | takes a critical section |  |
| `0x7797D0` | DeactivateTriggerHook | bool | 25 | takes a critical section |  |
| `0x7797F0` | NewEffect | uint | 330 |  |  |
| `0x779960` | DestroyEffect | - | 90 | small: handle lookup, virtual call [inferred] |  |
| `0x77CC00` | AddTriggerHook | uint | 135 | takes a critical section |  |
| `0x77CC90` | TriggerEffect | - | 121 | small: handle lookup, virtual call [inferred] |  |
| `0x77CD10` | ResetEffect | - | 121 | small: handle lookup, virtual call [inferred] |  |
| `0x77CD90` | EffectListGeneric | int | 704 |  |  |

#### 4. Images and thumbnails

| Address | IC | Returns | Bytes | Native side [verified call graph, depth 6] | Self % |
|---|---|---|---|---|---|
| `0x779470` | LoadUIHitMask | uint | 193 |  |  |
| `0x7799C0` | PickPixel | float[] | 607 |  |  |
| `0x779E70` | CaptureSceneAsImage | uint | 249 |  |  |
| `0x77D480` | AttachCameraFlashWinProc | - | 127 | small: handle lookup, virtual call [inferred] |  |
| `0x77F350` | LoadUIImage | uint | 420 | D3D create queued to owner thread (`UTFWin_QueuePendingTexture`) | 0.36 |
| `0x782D00` | GetThumbnailImage | uint | 497 | placeholder texture object, request posted with `PostRenderMessage` (hand-off) | 0.01 |
| `0x782F00` | GetCASThumbnailImage | uint | 299 | placeholder texture object, request posted with `PostRenderMessage` (hand-off) |  |

#### 5. World picking and map tags

| Address | IC | Returns | Bytes | Native side [verified call graph, depth 6] | Self % |
|---|---|---|---|---|---|
| `0x779C20` | InitMapTagSystem | - | 266 |  | 0.02 |
| `0x779D30` | UpdateMapTagCamera | - | 17 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x779D50` | CullMapTagByPosition | bool | 45 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x779D80` | UpdateMapTags | - | 17 | small: handle lookup, virtual call [inferred] | 0.01 |
| `0x779DA0` | ShutdownMapTagSystem | - | 25 | small: handle lookup, virtual call [inferred] |  |
| `0x77D770` | GetPickInfo | out/ref | 3306 | segment ray picking (docs/33) |  |
| `0x77E460` | GetMapTagInfo | bool + out | 428 |  |  |

#### 6. Metadata, resolution, misc

| Address | IC | Returns | Bytes | Native side [verified call graph, depth 6] | Self % |
|---|---|---|---|---|---|
| `0x77D1A0` | SetResolution | - | 139 | posts `UI/SetResolutionMessage` |  |
| `0x77D230` | UpdateResolution | - | 148 | small: handle lookup, virtual call [inferred] |  |
| `0x77D440` | SendBrowserCommand | bool | 64 | takes a critical section |  |
| `0x77EFB0` | InitUserToolHelper | - | 350 |  |  |
| `0x77FC70` | SetSaveGameMetadata | - | 517 |  |  |
| `0x780080` | GetWorldNameMetadata | string | 351 |  |  |
| `0x783030` | GetSaveGameMetadata | string | 1615 | placeholder texture object, request posted with `PostRenderMessage` (hand-off) |  |
| `0x783AC0` | GetCustomMusicInfo | string | 718 |  |  |

#### 7. ThumbnailManager

| Address | IC | Returns | Bytes | Native side [verified call graph, depth 6] | Self % |
|---|---|---|---|---|---|
| `0x777500` | SetCallbacks | - | 26 |  |  |
| `0x777520` | NotifyPreparationCompleted | - | 16 |  |  |
| `0x777530` | GetPregenerationStatus | ulong | 95 |  |  |
| `0x777590` | AddPose | ulong | 41 |  |  |
| `0x7775C0` | AddNameSpace | ulong | 21 |  |  |
| `0x7775E0` | AddOccultState | ulong | 21 |  |  |
| `0x777600` | AddRotation | ulong | 21 |  |  |
| `0x777620` | AddRig | ulong | 41 |  |  |
| `0x777650` | AddModel | ulong | 53 |  |  |
| `0x777690` | AddSimBuilder | ulong | 45 |  |  |
| `0x7776C0` | AddBoneDeltaFromBuilder | ulong | 76 |  |  |
| `0x777710` | SetSceneObject | ulong | 26 |  |  |
| `0x777730` | InvalidateHouseholdThumbnail | - | 41 |  | 0.01 |
| `0x777760` | InvalidateHouseholdSimThumbnail | - | 61 |  |  |
| `0x7777A0` | SetCheatSaveSimFace | - | 21 |  |  |
| `0x7777C0` | IsCheatSaveSimFaceEnabled | bool | 18 |  |  |
| `0x7777E0` | InvalidateLotThumbnails | - | 41 |  |  |
| `0x777810` | InvalidateLotThumbnailsForGroup | - | 46 |  |  |
| `0x777840` | GenerateTravelSimThumbnail | bool | 96 |  |  |
| `0x7778A0` | CloneObject | ulong | 31 | placeholder texture object, request posted with `PostRenderMessage` (hand-off) |  |
| `0x7778C0` | SetLotView | - | 46 |  |  |
| `0x7778F0` | SetHouseholdCamera | - | 21 |  |  |
| `0x777920` | AddDisplacement | ulong | 79 |  |  |
| `0x777B80` | LookupBBPreviewBound | float[] | 239 |  |  |
| `0x777C70` | GenerateLotThumbnails | - | 68 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x777DE0` | GenerateLotThumbnailsForGroup | - | 73 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x777F50` | GenerateHouseholdThumbnail | - | 68 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x7780C0` | GenerateThumbnailForSimsUsingObject | - | 290 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x7781F0` | GeneratePromPicture | - | 300 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x778320` | StitchPhotoboothPictures | - | 439 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x7784E0` | GeneratePhotoBoothPicture | - | 300 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x778610` | GenerateSelfPhotoPicture | - | 300 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x778740` | GenerateHouseholdSimThumbnail | - | 93 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x7788E0` | GenerateServoBotArenaPicture | bool | 290 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x778A10` | GenerateSculpture | bool | 289 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x778B70` | SaveSimFaceThumbnail | bool | 329 |  |  |
| `0x778CC0` | UpdateDirtyLotThumbnails | - | 643 | **D3D CreateTexture + Lock/Unlock on the calling (sim) thread** via `0xD5D910` |  |
| `0x779080` | KeyExistsInDB | bool | 192 |  |  |
| `0x779140` | InvalidateThumbnail | - | 230 |  |  |

#### 8. Text and strings (EAText, LocalizedStringService)

| Address | IC | Returns | Bytes | Native side [verified call graph, depth 6] | Self % |
|---|---|---|---|---|---|
| `0x741820` | Compare | int | 70 |  |  |
| `0x7419B0` | GetMoneyString | string | 232 |  |  |
| `0x741AA0` | GetNumberString | string | 224 |  | 0.02 |
| `0x741B80` | GetTimeString | string | 253 |  | 0.01 |
| `0x743790` | GetLocalizedStringByUInt64 | string | 23 |  |  |
| `0x7437B0` | GetLocalizedStringByStringKey | string | 33 |  |  |
| `0x7437E0` | GetLocalizedStringByString | string | 33 |  | 0.32 |
| `0x743810` | ChangeLocale | - | 5 |  |  |
| `0x743820` | GetLocale | string | 5 |  |  |
| `0x743830` | GetLocalizedLogoSuffix | string | 5 |  |  |
| `0x743840` | GetLocalizedLogoSuffixEP | string | 5 |  |  |

## 4. Threads and hand-offs

| Path | Calling thread | Device or shared state touched | Hand-off | Tag |
|---|---|---|---|---|
| Window ICs (layout, tree, properties, focus, modal, events) | sim | UTFWin window objects through vtables | none in the direct calls; no lock | [verified direct calls], race with main-thread drawing [inferred, M-UI5] |
| `UIService_SimUpdate 0x7F1530` (service sim slot) | sim | UI clock `0x11CD088`, window manager `[0x11CD080]` vtable `+0x24`, `+0x28` | none | [verified] |
| `UTFWinService_Update 0x723EE0`, `IMEService_Update 0x7E6840` | main | UTFWin, IME | n/a | [verified slots] |
| `LoadUIImage` -> `UIImage_BuildTexture 0x7E8390` -> `UITextureResource_FromImage 0x7E8200` | sim | texture object only; `Texture_ClampToCaps` reads caps | D3D create queued: `UTFWin_QueuePendingTexture(res, 1)`, list `0x11CD034`, lock `0xFBA4D8` | [verified]; drain thread [inferred] |
| UI texture release `0x7E7610` / `0x7DBBE0` | any | D3D texture release | direct only if `Device_IsOwnerThread`, else queued (flag 0) | [verified] |
| `GetThumbnailImage`, `GetCASThumbnailImage`, `GetSaveGameMetadata` | sim | placeholder texture object | `PostRenderMessage` (render message buffer, critical section) | [verified] |
| Thumbnail `Generate*`, `StitchPhotoboothPictures`, `UpdateDirtyLotThumbnails` -> `0xD5D910` | sim | **`IDirect3DDevice9::CreateTexture`, `LockRect`, `UnlockRect`** | none before the device calls; request queued afterwards under lock `0x1088330` | [verified]; race [inferred] |
| `ThumbnailManager_SimUpdate 0xD4D6D0` (sim slot) | sim | thumbnail scene actors: `SACS_DriverUpdate`, `Actor_TimeStep` | checks state under lock `0x1088330` | [verified] |
| `ThumbnailManager_Update 0xD61670` (main slot) | main | request queues, render-to-texture (`ThumbnailRenderer_RTT 0xD62200`) | `PostRenderMessage` | [verified] |
| `SetResolution` | sim | none directly | `UI/SetResolutionMessage` | [verified string]; receiver [inferred] |
| Trigger hook ICs, `SendBrowserCommand` | sim | trigger map, browser | critical section | [verified] |
| `GetPickInfo` (3.3 KB, 265 functions reachable) | sim | world picking (docs/33) | none | [verified call graph] |
| EAText / LocalizedStringService | sim | string tables, number/money/time formatting | none; no lock in the direct calls | [verified] |

## 5. Services and update slots

| Service | ctor | vtable | Main slot `+0x1C` | Sim slot `+0x20` |
|---|---|---|---|---|
| `Services/UTFWin` | `0x724310` | `0xFFCF8C` | `UTFWinService_Update 0x723EE0` | stub |
| `UI/IME/Service` | `0x7E5110` | `0x101C158` | `IMEService_Update 0x7E6840` | stub |
| UI service (cursor / main window owner [inferred from the sub-vtable `0x101E748` holding `0x7F1EF0`, which allocates `UI/CursorMgrImpl` and `UI/MainWindow`]) | `0x7F14E0` | `0x101E758` | stub | `UIService_SimUpdate 0x7F1530` |
| `UI/ThumbnailManager` | `0xD57D30` | `0x1088F2C` | `ThumbnailManager_Update 0xD61670` | `ThumbnailManager_SimUpdate 0xD4D6D0` |
| `UI/TriggerService` | `0x7F1350` | `0x101E5D8` | stub | stub |
| `UI/CollectionsManager` | `0x7DECF0` | `0x101AFE0` | stub | stub |
| `UI/HeadlineManager` | `0x7E4590` | `0x101BD04` | stub | stub |
| `Services/Serialization` | `0x7ECAA0` | `0x101D668` | stub | stub |

All [verified: Service_ctor callers and vtable slots]. This resolves the "`0x7F14E0`, UI range [inferred]" row of docs/36 §3.3. Whether each service's run flag is set is not read.

## 6. Address blocks (anchors for M-UI2 to M-UI5)

| Block | Range | Contents | For |
|---|---|---|---|
| EAGimex | `0x444080`-`0x446180` | UI image codec buffers and factory (`UI/EAGimex/*`) | M-UI5 |
| EAText core | `0x44EE50`-`0x467D60`, `0x4F4930`-`0x4F4F40`, `EATextUtil` `0x5000F0`-`0x50023F` | fonts, glyph cache, typesetter (docs/32) | M-UI3 |
| UTFWin core | `0x505380`-`0x54B800` | window tree, window manager (vtables around `0xFBA6A0`-`0xFBA700`), controls, `UI/Grid` `0x52A2E0`-`0x52A710`; pending textures `0x50A6A0`-`0x50A780`; window manager getter `0x50AB70` | M-UI2, M-UI4 |
| UI/SaveGameMetadata, VarMan, BlendUnit | `0x59D6C0`-`0x5A3738` | metadata objects for the save dialogs | - |
| EAText renderer | `0x6916F0` (`EATextRenderer`, glyph cache texture), `0x691DE0` | text drawing | M-UI3, M-UI5 |
| UTFWin service, text glyph cache | `0x723C70`-`0x724979` (`UI/ImmediateModeEx_TSS`, `UI/TextGlyphCache`, `UI/WindowManager`, service ctor `0x724310`) | UI immediate-mode drawing and service | M-UI5 |
| UI IC block | `0x741820`-`0x743840` (EAText, strings), `0x777500`-`0x783C00` (ThumbnailManager, UIManager), `UI/UIHost` `0x7801E0` | script boundary | this doc |
| Sims3 UI glue | `0x7DBBE0`-`0x7F35D4` | camera flash, collections, color picker, custom proxy controls `0x7E29B0`-`0x7E3BF5`, `UI/TextRenderer` `0x7E4500`, IME `0x7E5110`-`0x7E6DDE`, UI textures `0x7E7610`-`0x7E8740` (`UITexture_Load 0x7E77E0`), layout/hit-mask/texture factories `0x7E85B0`-`0x7E86F0` (the `.layo` resource entry), `UI/StringBinder` `0x7E911D`, map tags `0x7EA840`, serialization `0x7ECAA0`, `UI/SoundManager` `0x7ED360`-`0x7ED470`, input devices `0x7EDD70`-`0x7EE040`, trigger service `0x7EFCF0`-`0x7F1443`, UI service `0x7F14E0`, cursors `0x7F1910`-`0x7F35D4` | M-UI2, M-UI4, M-UI5 |
| ThumbnailManager | `0xD4B390`-`0xD64408` | requests, caches, RTT renderer (docs/21 §1.2) | M-UI5, D3D |
| Startup and main menu | `0xEC7140`-`0xEC8BF6` | `UI/StartupScreen`, `UI/MainMenu` | - |

[verified: tag string xrefs; block edges are the first and last tagged function, so code without tags just outside an edge may belong to the block.]

## 7. Sim-thread cost (existing samples only)

Managed sampler, 21 `*-managed.jsonl` files, 15,860 samples, live mode, mostly Bridgeport [measured]:

| Frame | Self % | Inclusive % |
|---|---|---|
| All UI ICs (innermost frame) | 1.97 | - |
| `UIManager_LoadUIImageImpl` | 0.36 | 0.36 |
| `LocalizedStringService_GetLocalizedStringByString` | 0.32 | - |
| `UIManager_LoadLayoutImpl` | 0.23 | - |
| `UIManager_GetStringPropertyImpl` | 0.20 | - |
| `UIManager_GetUInt32PropertyImpl` | 0.11 | - |
| Any `Sims3.UI` / UI IC frame on the stack | - | 4.39 |
| `Sims3.UI.Hud.SimInfo.OnMotivesChanged` | - | 1.11 |
| `Sims3.UI.UIManager.ProcessEvent` | - | 0.91 |
| `Sims3.UI.Hud.MotivesPanel.UpdateMotiveBar` | - | 0.61 |
| `Sims3.UI.TooltipManager.Simulate` | - | 0.52 |
| `Sims3.UI.SceneMgrWindow.OnTick` | - | 0.38 |
| `Sims3.UI.Hud.SimInfo.OnBuffsChanged` | - | 0.36 |

Native sim-stack sampler, 34 `*simstack.jsonl` files, 9,575 samples, top frame [measured]: UTFWin core 1.19% (top functions `0x50B4C0`, `0x50D2F0`, `0x50CB60`, `0x50AF60`, `0x5492E0`, all reached only through window vtables), ThumbnailManager block 0.20% (`0xD4E550`), UI IC block 0.06%, EAText 0.02%. Of the UTFWin top-frame samples, about 115 of 150 have no IC, service-slot or other named frame in the next 11 stack slots, so they come in through virtual calls: the UI service sim slot or managed callbacks [inferred]. "Anywhere on the stack" counts are higher (UTFWin 7.0%) but the stack walker picks up stale return addresses, so only the top frame is used.

The measured runs are live-mode play with the HUD open; CAS, build/buy catalog and save dialogs are not in these samples (the CAS fill is docs/21).

## 8. Candidates

Ranked by expected value for stutter and crashes; all switches default off.

1. **Thumbnail texture creation off the sim thread** (crash lead, §1). Options: (a) create the device with `D3DCREATE_MULTITHREADED` (docs/37), cheapest and covers every unguarded path, small per-call cost to measure; (b) a ts3native detour on `0xD5D910` that, when `Device_IsOwnerThread` is false, defers the create and fill to the main thread the way `UTFWin_QueuePendingTexture` does. Repro: an agent action that takes in-game photos, regenerates lot and household thumbnails and saves in a loop (tools/harness/repros.json entry needed). Check first whether the crash ledger has crashes with a thumbnail or save context.
2. **Motive and buff panel updates** (1.1% + 0.4% inclusive): `SimInfo.OnMotivesChanged` fires per change and refreshes bars through property ICs. Coalesce to one refresh per UI frame in a script hook (patch host), keep the last value. Low risk, UI only.
3. **`LoadUIImage` 0.36% self**: find which images are loaded again and again from the HUD (likely motive or moodlet icons) and cache the handle on the C# side.
4. **`GetLocalizedStringByString` 0.32%**: string-table lookups from UI code each tick; a small C# cache keyed by string would remove most of them [inferred: needs the callers from the managed stacks].
5. **`TooltipManager.Simulate` and `SceneMgrWindow.OnTick`** (0.5% + 0.4%): per-tick UI polling on the sim thread; rate-limit to a few Hz.
6. **UTFWin window tree off the sim thread**: blocked. The window manager is ticked on the sim thread and the ICs mutate the tree without a lock; moving either needs the main-thread draw path and its locking first (M-UI5). Not an offload candidate until then.

## 9. Open items

- Who drains `UI/UTFWin/PendingTextures` and when (M-UI5).
- Whether UTFWin drawing on the main thread reads the window tree while sim-thread ICs and `UIService_SimUpdate` change it, and under which lock if any (M-UI2, M-UI5).
- Window manager vtable `+0x24` / `+0x28` behaviour (layout pass, timers, animation) (M-UI2).
- The thumbnail race: confirm the device has no `MULTITHREADED` flag in the DXVK and native D3D9 setups, and look for crashes with `0xD5D910` or `Texture_CreateD3D` on the sim-thread stack.

## 10. Names added

30 rows appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv`, tagged `<docs/43>`: `Texture_ClampToCaps`, `Texture_FillLevels`, `UTFWin_PendingTexturesInit`, `UTFWin_PendingTexturesPush`, `UTFWin_QueuePendingTexture`, `UTFWin_GetWindowManager`, `UI_SampleClock`, `UTFWinService_Update`, `UTFWinService_ctor`, `UIResource_ReleaseD3D`, `CollectionsManager_ctor`, `HeadlineManager_ctor`, `IMEService_ctor`, `IMEService_Update`, `UITextureResource_ReleaseD3D`, `UITextureResource_ctor`, `UITextureResource_FromImage`, `UIImage_BuildTexture`, `SerializationService_ctor`, `TriggerService_ctor`, `UIService_ctor`, `UIService_SimUpdate`, `ThumbnailManager_SimUpdate`, `ThumbnailManager_ctor`, `ThumbnailManager_CreateThumbnailTexture`, `ThumbnailManager_Update`, and data `g_UTFWinPendingTextures`, `g_UTFWinWindowManager`, `g_UTFWinPendingTexturesLock`, `g_ThumbnailManagerLock`.
