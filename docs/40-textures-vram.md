# 40. Textures, GPU uploads and the VRAM budget (M-D3D5)

Static map of how TS3W.exe (Steam 1.67) loads textures, which D3D9 pools it uses, how it learns the VRAM size and what it does with it. Done on a Ghidra copy (`game-copy/ghidra-d3d5`) with our symbols applied; no game runs. Cost numbers come only from existing `probe-out` files. Names are in `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/40>`, 49 rows).

## 1. Short answer

- **Every texture loaded from a package is `D3DPOOL_MANAGED`** [verified]. The DDS loader takes a "default pool" switch, but all three callers (texture resources, animated textures, UI textures) pass "managed". So under native D3D9 each loaded texture keeps a full system-memory copy inside the 32-bit process. This is the Error 12 mechanism that DXVK removes (docs/01, docs/05: about 1.75 GB freed).
- **Static vertex and index buffers are `MANAGED` too; dynamic ones are `DEFAULT` + `DYNAMIC|WRITEONLY`** [verified].
- **There is no VRAM-driven texture budget, no LOD streaming and no eviction by VRAM** [verified for the load path; no other consumer found]. VRAM is read once at startup, only to pick the default quality presets in `GraphicsRules.sgr`. Textures live as long as something references them, and unreferenced ones stay in the generic `ResourceCache` (`CacheBudget`, 200 MB) until it is over budget (docs/08). Each texture tells that cache it costs 52 bytes plus its pixels, **but only when it has a system-memory copy** (MANAGED or SYSTEMMEM). So `CacheBudget` works as the engine's texture budget, and it counts address space, not VRAM.
- **The VRAM size comes from DxDiag, not from `GetAvailableTextureMem`** [verified]. On Windows the engine reads DxDiag's `szDisplayMemoryEnglish` and keeps `(MB + 4) & 0xFF8`, so the value wraps at 4 GB: 8 GB or 16 GB cards read as 0 MB. EA's `GraphicsRules.sgr` then falls back to 32 MB and logs `<<OVERRIDE>>`. That is the "texture memory still reports `32MB <<OVERRIDE>>`" line in docs/05 [verified mechanism; matches the measured log line]. The DirectDraw `GetAvailableVidMem` path exists but only runs under Transgaming Cider (Mac). No call to `IDirect3DDevice9::GetAvailableTextureMem` or `EvictManagedResources` was found [inferred: pattern scan, see §8].
- **Texture work never showed up on the sim thread**: 0 of 10,251 existing sim-thread samples are in the texture factory or DDS loader [measured]. Its real thread and cost need the main-thread sampler (PLAN item 7 priority 0).

## 2. Function list

| Address | Name | Role | Tag |
|---|---|---|---|
| `0xECB000` | `App_ParseGraphicsCmdLine` | Command line: `ccinstall`, `shaders`, `emulategpu`, resolution, `systemmemory`, **`texturememory`**, `resetOptions`; then `DeviceConfig_Init` | [verified] |
| `0x58E330` / `0x58E340` | `DeviceConfig_SetSystemMemoryOverride` / `…TextureMemoryOverride` | Store `-systemmemory=` / `-texturememory=` (config `+0x16C` / `+0x170`) | [verified] |
| `0x5928F0` | `DeviceConfig_Init` | `DeviceConfig.log`, `Options.ini`, hardware probe, GraphicsRules | [verified] |
| `0x613CB0` | `HwDetect_ProbeAdapter` | `Direct3DCreate9`, adapter count/identifier, caps, display modes, VRAM into hw info `+0xC4` | [verified] |
| `0x612640` | `VRAM_QueryDxDiag` | DxDiag COM `DxDiag_DisplayDevices`, matches `szDeviceIdentifier`, parses `szDisplayMemoryEnglish` | [verified] |
| `0x612130` | `VRAM_QueryDirectDraw` | `DirectDrawCreateEx` + `IDirectDraw7::GetAvailableVidMem` (caps `0x10004040`), total MB | [verified] |
| `0x58E110` | `HwDetect_IsTransgamingMac` | `ntdll!IsTransgaming` / `TGGetOS("MacOSX")`; selects the DirectDraw path | [verified] |
| `0x612050` | `HwDetect_NvCplQuery` | `NVCPL.dll!NvCplGetDataInt` for NVIDIA-family vendor ids | [verified] |
| `0x613B10` | `HwDetect_SystemInfo` | CPU, WOW64, `GlobalMemoryStatusEx` | [verified] |
| `0x5902B0` | `DeviceConfig_RunGraphicsRules` | Applies the overrides, exports `cpuSpeed`…`textureMemory` to the script, runs it, reads `cardLevel`/`cpuLevel` back | [verified] |
| `0x60E700` | `D3D9_EnsureDirect3D` | `Direct3DCreate9(D3D_SDK_VERSION 32)` into `g_pDirect3D9` `[0x11CECD0]` | [verified] |
| `0x611080` | `GraphicsDevice_CreateDevice` | `CreateDevice`, BackBuffer/DepthBuffer, ImmQuadIB, VertexFormatCache; device in `g_pD3DDevice9` `[0x11CECCC]` | [verified] |
| `0x60F740` | `GraphicsDevice_GetD3DDevice` | returns `[0x11CECCC]`; wrapper singleton `g_GraphicsDevice` `[0x11CECC8]` (`GraphicsDevice_Get 0x60E2C0`) | [verified] |
| `0x722B50` | `TextureFactory_Ctor` | Reads `ForceHighLODObjects`, `DropTexMipsOnLoad`, `TextureSizeThreshold` | [verified] |
| `0x722E00` | `TextureFactory_CreateResource` | Factory vtable `0xFFCBE0` slot; picks a mip-drop config, loads with "managed" | [verified] |
| `0x722C80` | `TextureFactory_OpenStream` | Copies the resource into a `Renderer/TextureFactory/MemoryStream` | [verified] |
| `0x723270` | `TextureResource_Load` | `Renderer/TextureResource`: release old, `DDS_LoadTexture`, report size to the cache | [verified] |
| `0x60D9B0` | `DDS_LoadTexture` | DDS header parse, format map, mip drop, create, upload | [verified] |
| `0x60CF40` | `DDS_DropTopMips` | Skips the top mips in the stream | [verified] |
| `0x60D140` | `DDS_CheckLastMipZero` | Sums the smallest mip's bytes; a zero mip forces a drop path | [verified code, purpose inferred] |
| `0x60D290` | `DDS_UploadLevels` | Per mip and face: direct `LockRect` + copy, or a SYSTEMMEM staging texture + `UpdateTexture` | [verified] |
| `0x60CEA0` | `Texture_CreateByShape` | 2D, cube or volume | [verified] |
| `0x619C80` / `0x619910` / `0x619980` | `Texture_Create2D` / `…Cube` / `…Volume` | Fill the 0x34-byte texture object, then `Texture_CreateD3D` | [verified] |
| `0x618D50` | `Texture_FlagsToUsagePool` | Engine flags to `D3DUSAGE`/`D3DPOOL` (§4) | [verified] |
| `0x619540` | `Texture_CreateD3D` | **The only** `CreateTexture` / `CreateCubeTexture` / `CreateVolumeTexture` site (device slots `0x5C`/`0x64`/`0x60`) | [verified] |
| `0x619770` | `Texture_ClampToCaps` | Rounds width and height up to a power of two when the caps need it and clamps them to the caps maximum; no device call. **Not texture creation** (docs/43 corrects docs/37 and this doc). The device-touching IC helper is `0x618570` `Texture_FillLevels` (lock, fill, unlock per level); `LoadUIImage` reaches only `0x619770`, the thumbnail ICs reach both | [verified, docs/43] |
| `0x619AB0` / `0x619DB0` | `Texture_EstimateBytes` / `Texture_CacheSizeBytes` | Pixel bytes for all mips and faces / size reported to the ResourceCache | [verified] |
| `0x6196B0` / `0x60EDA0` | `Texture_ReleaseD3D` / `GraphicsDevice_UnbindTexture` | Unbind from the 16 cached sampler stages, `Release` | [verified] |
| `0x60EC10` | `GraphicsDevice_UpdateTexture` | `IDirect3DDevice9::UpdateTexture` (slot `0x7C`) | [verified] |
| `0x61A1C0` | `VertexBuffer_Create` | `CreateVertexBuffer` (slot `0x68`); ~45 callers (CAS, terrain, SpeedTree, lots, weather, UI) | [verified] |
| `0x616DF0` / `0x616C40` | `IndexBuffer_Create` / `IndexBuffer_CreateD3D` | `CreateIndexBuffer` (slot `0x6C`); ~40 callers | [verified] |
| `0x731CC0` / `0x731F60` | `Resource_SetCacheSize` / `ResourceCache_AdjustBytes` | Size delta into cache `+0x94` (unreferenced) or `+0x98` (in use) under lock `0xFFDD34` | [verified] |
| `0x7E77E0` | `UITexture_Load` | `UI/Texture`: `DDS_LoadTexture` ("managed") or a MANAGED 2D layout texture | [verified] |
| `0x6880F0` | `AnimatedTexture_Load` | `Renderer/AnimatedTextureResource` frames, "managed" | [verified] |
| `0x5F9A10` | `CAS_HSVToRGB_FillVolume` | The only `D3DXFillVolumeTexture` user (CAS colour lookup volume) | [verified] |

Other texture producers that call `Texture_Create2D` directly (render targets and scratch textures, not package loads): RT pool `Services/RTPool/Textures` `0x6173E0`, shadow maps `0x70BEB0`, default 1x1 textures `0x6E2760`, glyph caches `0x6916F0`, AO roof solver `0x68A7F0`, wall texel pool `0x689400`, lot imposters `0xADDEA0`/`0xADE030`, thumbnails `0xD62200`, CAS texture builder `0x5FDA00`, video capture `0x9DEA80` (also the only `CreateOffscreenPlainSurface`) [verified call sites].

## 3. Texture lifecycle

| Step | Code | Thread | Memory |
|---|---|---|---|
| 1. Request | `GetResource` on a `0x00B2D882` key reaches the texture factory (`0x722E00`) | caller: sim thread for synchronous script loads, the resource JobThread (docs/28 item 7) or the main thread [inferred]; 0% of sim-thread samples (§6) | — |
| 2. Read + decompress | resource system, under the global resource lock `0xFB2CD0` (docs/08) | same | compressed + decompressed buffers on the EA/CRT heap (docs/08 §2.5) |
| 3. Stream copy | `TextureFactory_OpenStream`: `Renderer/TextureFactory` buffer + MemoryStream | same | one more copy of the DDS bytes, transient |
| 4. Decode header | `DDS_LoadTexture`: magic `DDS `, header size 0x7C, pixel format to an engine format id; FourCC `DXT1`..`DXT5` and D3DFMT numbers 0x14-0x74, masks for A8R8G8B8, X8R8G8B8, R5G6B5, A1R5G5B5, A4R4G4B4, L8, A8, A8L8, V8U8, A2B10G10R10 | same | none (no CPU decode: DXT stays compressed, pixel data is copied as is) [verified] |
| 5. Mip drop | `DDS_DropTopMips` drops up to N top mips (keeps at least 8 px) when `min(w,h) >= threshold`, by skipping stream bytes | same | saves 75% per dropped level |
| 6. Create | `Texture_CreateD3D` -> `IDirect3DDevice9::CreateTexture`, flags `0x2` -> `D3DPOOL_MANAGED`, usage 0 | same; **no lock and no thread check** in this path, device created without `D3DCREATE_MULTITHREADED` [verified] | native D3D9: system copy of all mips in process; DXVK: copy in mapped file chunks (§5) |
| 7. Upload | `DDS_UploadLevels`: `LockRect` each level and face, copy rows, `UnlockRect` | same | writes into the managed copy; the GPU copy is made later by the runtime when the texture is first used [community D3D9 behaviour] |
| 8. Account | `TextureResource_Load` -> `Resource_SetCacheSize(0x34 + pixel bytes)` | same | counted in the ResourceCache totals |
| 9. Use | sampler binds through the device wrapper (16 cached stages at wrapper `+0x120`) | render/main [inferred] | runtime/DXVK uploads managed data to VRAM |
| 10. Evict | refcount 0 -> ResourceCache flush list; dropped when the cache is over `CacheBudget` (docs/08). Release unbinds from all stages, then `Release` | whoever drops the last ref | system copy freed |

Mip-drop configuration [verified, `0x722B50`, `0x722E00`, `0x722DA0`]:
- `g_TexMipDropCfg 0x11D2FB0 = {DropTexMipsOnLoad, TextureSizeThreshold}`; `g_TexMipDropCfgAlt 0x11D2FBC = {DropTexMipsOnLoad + 1, 256}`. With `ForceHighLODObjects` set: `{0, …}` and `{0, 2048}`, so nothing is dropped.
- `TextureFactory_CreateResource` uses the main config when a lookup on the resource returns the object `0x115B874`, else the alternative one. So with **High** texture quality (drop 0) the other kind still loses one mip at >= 256 px. Which resources are "the other kind" is open (§8).
- `GraphicsRules.sgr` `TextureQuality`: Low = drop 1 at any size, Medium = drop 1 from 1024 px, High = drop 0 (threshold 2048, unused at drop 0) [verified, EA file].

The DEFAULT-pool branch exists but is unused for package textures: with flags 0 the uploader creates a temporary `Renderer/Texture` in SYSTEMMEM (flags `0x100`), fills it, calls `UpdateTexture` into the DEFAULT texture and releases the temporary one [verified, `0x60D290`, `0x60EC10`].

## 4. Pools and their address-space cost

`Texture_FlagsToUsagePool` [verified]: `0x100` -> SYSTEMMEM; otherwise bit `0x2` -> MANAGED, else DEFAULT. Usage: `0x1` RENDERTARGET, `0x10` DEPTHSTENCIL, `0x4` DYNAMIC, `0x200` AUTOGENMIPMAP (dropped if the format cannot autogen). Buffers [verified, `0x61A1C0`, `0x616C40`]: buffer flag bit 0 -> `DYNAMIC|WRITEONLY` in DEFAULT, else usage 0 in MANAGED.

| Resource | Pool | In-process copy, native D3D9 | With DXVK 3.1.1 | Tag |
|---|---|---|---|---|
| Package textures (objects, CAS parts, terrain, UI, animated) | MANAGED | full system copy of all loaded mips: a 1024² DXT5 is ~1.3 MB, 2048² DXT5 ~5.3 MB, 2048² A8R8G8B8 ~21 MB | copy in 16 MiB `MemoryFilePool` file mappings; only `d3d9.textureMemory` (100 MB) is mapped at a time (docs/08 table row 5) | pool [verified], DXVK [verified source, docs/08] |
| UI layout textures (`"Layout"`) | MANAGED | same | same | [verified] |
| Render targets, depth, shadow maps, RT pool | DEFAULT | none | none | [verified flags] |
| Upload staging (only for a DEFAULT target) | SYSTEMMEM, temporary | one level set, freed after `UpdateTexture` | mapped while alive | [verified] |
| Static vertex/index buffers (most meshes, ImmQuadIB) | MANAGED | full copy | not traced in DXVK source [inferred: DXVK keeps a CPU copy for MANAGED buffers] | pool [verified] |
| Dynamic vertex/index buffers | DEFAULT, DYNAMIC\|WRITEONLY | none | DXVK maps them while locked | [verified] |

## 5. VRAM size and budget logic

Inputs and flow [verified unless marked]:
1. `HwDetect_ProbeAdapter` (from `DeviceConfig_Init`) creates a temporary `IDirect3D9`, checks the adapter, reads `GetAdapterIdentifier` (vendor id; NVIDIA-family ids also query `NVCPL.dll`), caps (`D3DDEVCAPS_HWTRANSFORMANDLIGHT`), display modes 1024x768/1024x600 in X8R8G8B8 or R5G6B5.
2. VRAM in MB into hw info `+0xC4`:
   - Windows (and Wine/Proton): `VRAM_QueryDxDiag` reads `szDisplayMemoryEnglish` of the display device whose `szDeviceIdentifier` matches the adapter GUID, parses the float, rounds and keeps `(MB + 4) & 0xFF8` (`shl 0x14`, `and 0xFF800000`, `shr 0x14` at `0x6128CB`-`0x6128D2`). **Any value from about 4092 MB wraps modulo 4096**: 4 GB -> 0, 6 GB -> 2048, 8 GB -> 0, 12 GB -> 0, 16 GB -> 0; on modern Windows DxDiag's display memory also includes shared system memory, so the result is close to random [verified arithmetic; DxDiag's meaning is community knowledge].
   - Cider (Mac): `VRAM_QueryDirectDraw`, total local video memory from `GetAvailableVidMem`, a 32-bit DWORD.
3. `DeviceConfig_RunGraphicsRules`: if `-texturememory=N` (or `-systemmemory=N`) was given, it replaces the detected value [verified], then the value goes to the `textureMemory` script variable.
4. `GraphicsRules.sgr` (EA original): `textureMemory == 0` -> 32 MB and `textureMemorySizeOK false` (log `<<OVERRIDE>>`); `<= 112` low, `<= 240` medium, `<= 368` high, else uber `gpumemLevel`. `gpumemLevel` only feeds the default choice of TextureQuality, shadows, reflections, visual effects and similar presets, and the log line [verified file]. Our installer already patches the 0 fallback to 2048 (`tools/setup/install-fixes.ts`), which is why the copy in `game-copy/bin` reads 2048 [verified file].
5. Nothing else consumes the VRAM value: no budget, no LOD scale, no eviction threshold [verified for the texture path; inferred for the rest of the engine, see §8].

DXVK interplay [inferred unless marked]: DXVK's TS3W profile spoofs the adapter id as a GTX 580 (docs/07), which fixes the `GraphicsCards.sgr` match (`cardLevel`), but not `textureMemory`: DxDiag is a separate COM component that asks the system, so the wrap still happens (docs/05 shows `32MB <<OVERRIDE>>` under DXVK [measured]). DXVK's own `GetAvailableTextureMem` answer is never asked for.

**Budget that actually limits textures:** `ResourceCache` `CacheBudget` (Default.ini 200 MB, GraphicsRules 160 MB on low RAM, docs/08). `Texture_CacheSizeBytes` reports `0x34 + Texture_EstimateBytes` only when the texture flags have `0x2` or `0x100`, and just `0x34` otherwise [verified, `0x619DB0`]. Under native D3D9 that is honest: the cache bounds the system copies kept for unreferenced textures. Under DXVK the same bytes are mostly unmapped file-backed memory, so textures fill the 200 MB with things that cost little address space and push out meshes and other resources that do [inferred].

## 6. Cost from existing data

- **Sim thread:** in all 35 `probe-out/profile/*simstack*.jsonl` files (10,251 samples, BP/IP/SV, mixed configs), no sample has a frame in `TextureFactory` `0x722B50`-`0x723400` or the DDS loader `0x60CDE0`-`0x60DA00`; the texture-object range `0x618D50`-`0x61A400` is 0.04% (leaf) and the device-wrapper range 0.08% (any frame) [measured; stack-scan frames, so "any frame" is an upper bound]. Texture loads do not run on the sim thread in these scenarios.
- **Address space:** `memory.csv` in 139 run folders (DXVK installed): `mappedMB` 31-256 MB, median of per-run maxima 237 MB, private 1.6-2.1 GB, commit up to 3.5 GB [measured]. Mapped memory above DXVK's 100 MB texture cap comes from other DXVK mappings and the exe's own file mapping at `0xEA4E9F` (docs/08) [inferred split; no per-owner data].
- **DXVK vs vanilla:** about 1.75 GB less committed on the same save (docs/05) [measured]. That is the MANAGED texture copies plus driver overhead leaving the process.
- **Main thread / render thread:** no data; the sampler is sim-thread only (docs/34 §7).

## 7. Candidates

Error 12 (address space):

| # | Change | Expected effect | Risk / cost | Tag |
|---|---|---|---|---|
| E1 | **Keep DXVK** (baseline). Without it, every loaded texture's mips sit in the process | already ~1.75 GB | — | [measured, docs/05] |
| E2 | **Mip-drop tuning without code**: a TextureQuality "High" variant with `DropTexMipsOnLoad 1` and `TextureSizeThreshold 2048` drops the top mip only for 2048+ px textures (most heavy CC). Each such texture shrinks to 25% in VRAM, in DXVK's copies and in the ResourceCache count; the read and decompress buffers stay full size (the drop happens after decompression) | big for 2K CC, also fewer bytes uploaded (stutter) | visible blur on 2K textures up close; needs an in-game A/B of `mappedMB`, `privateMB` and load time on a CC-heavy save | [verified mechanism, gain inferred] |
| E3 | **`CacheBudget` tuning with DXVK in mind** (docs/08 H4): measure the cache's texture share first (`/rescache` + a texture count); if textures dominate under DXVK, either raise the budget or count DXVK-backed textures smaller | depends on the share | patch of `Texture_CacheSizeBytes` changes eviction for everyone; measure before | [inferred] |
| E4 | **DEFAULT pool for package textures**: `0x722E52` `PUSH 0x0` -> `PUSH 0x1` (arg 4 of `TextureResource_Load`), same for `UITexture_Load` | native D3D9: no system copy at all (the full DXVK-sized win without DXVK); with DXVK: small (copies are mostly unmapped already) | each load creates and frees a SYSTEMMEM staging texture + `UpdateTexture`; the cache counts such textures as 52 bytes, so it keeps many more in VRAM; lost-device handling for DEFAULT textures is not traced (the wrapper keeps a resource list at `+0xD8`), so native D3D9 alt-tab in fullscreen may lose them. Low priority while DXVK is the baseline | [verified site, effect inferred] |
| E5 | `d3d9.textureMemory` below 100 MB | less mapped address space | more map/unmap churn, possible hitches | [inferred] |
| E6 | `-texturememory=4096` (or any value > 368) on the launch line | deterministic "uber" presets instead of the wrapped DxDiag value; same as our sgr fallback patch, but also covers wrapped nonzero values (6 GB -> 2048 is fine; 12.5 GB -> 512) | none; not an address-space fix | [verified mechanism] |

Stutter and crashes:

| # | Change | Why | Tag |
|---|---|---|---|
| S1 | **Thread of texture creation** (probe first): count `GetCurrentThreadId()` at `Texture_CreateD3D 0x619540` and `DDS_UploadLevels 0x60D290` against the device thread (wrapper `+0x10`). If the resource JobThread creates textures while the main thread draws, that is a data race in native D3D9 and in DXVK (no `D3DCREATE_MULTITHREADED`) and a crash candidate | device flags are exactly `0x40`/`0x20` at `0x6112A4`/`0x6112B9` [verified] |
| S2 | If S1 shows worker creates: set `D3DCREATE_MULTITHREADED` (imm `0x40` -> `0x44`, `0x20` -> `0x24`) so DXVK takes its device lock | small lock cost on every call; measure frame time | [inferred] |
| S3 | **Upload hitches**: managed uploads happen at first use, on the render side; with many new textures (lot load, CAS) that is a burst. A per-frame upload budget or `PreLoad` on the load thread would spread it; needs the main-thread sampler and DXVK's HUD (`dxvk.hud=frametimes`) first | [inferred] |
| S4 | E2's mip drop also cuts upload bytes and decode copies by 75% per 2K texture | [inferred] |

Side note: the device is created without `D3DCREATE_FPU_PRESERVE`, so native D3D9 switches the creating thread's x87 unit to single precision [community D3D9 behaviour]; whether DXVK does the same and whether the sim thread is that thread is not checked.

## 8. Open items

- Which object is `0x115B874` in `TextureFactory_CreateResource` (main vs alternative mip-drop config): decompile `0x57A7D0`.
- Real thread of texture loads (S1): runtime probe.
- `GetAvailableTextureMem` / `EvictManagedResources`: a pattern scan of every indirect call through `[reg+0x10]` and `[reg+0x14]` in the 169 functions that fetch the device found none; M-D3D1 (docs/37) should confirm with its full device-call inventory.
- Lost-device handling of the tracked-resource list at wrapper `+0xD8` (needed before E4).
- DXVK's handling of MANAGED buffers on 32-bit (address-space share of static VBs/IBs).
- In-game check of the wrap: `DeviceConfig.log` "Texture memory" line on 6/8/16 GB cards, with and without `-texturememory=`.

## 9. Method notes

The device wrapper is small and self-contained (`0x60E000`-`0x61B000`: `Services/Device/*`, `Graphics/IndexBuffer`), so every D3D object type has a single creation site. Found it from the `Direct3DCreate9` import thunk (`0xECC1E0`, 3 callers), then indirect calls by slot offset: MSVC emits `mov reg,[vtbl+slot]; call reg`, so the scan must follow the register back to its load, not only match `call [reg+disp]` (the plain match found no device call at all). `ESP`-relative calls are noise. Push counts (a `CreateTexture` call pushes 9 values) and whether the function fetches the device filter the rest: 145 candidates at slot `0x5C` came down to one. Allocator tags (`Renderer/Texture`, `UI/Texture`, `Renderer/TextureFactory`) gave the owners; `GraphicsRules.sgr` gave the config names. Scripts used (scratch, not committed): a vtable-slot call-site scanner, a reference lister, an instruction lister, plus `DecompileTo`, `DumpPtrs`, `StringXrefs`, `ImportSymbols`.
