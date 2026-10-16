# 32. Middleware inventory (M-MW)

Every third-party and EA-internal library in the Steam build (TS3W.exe 1.67.2, PE timestamp 2014-01-21, base `0x400000`, no relocations, Large Address Aware) and the DLLs next to it in `Game\Bin`. Static only: PE headers, import/export tables, version and copyright strings, and code references to library-specific strings. No game run.

Method [verified, reproducible]: a small Node PE parser (sections, imports, delay imports, exports) plus a string scan (ASCII and UTF-16) over each binary. **Code ranges** come from scanning `.text` for 32-bit immediates equal to the address of a library-specific string (the exe has a fixed base, so string pushes are absolute). They show where a library's code clusters, not its exact bounds [inferred]. Where docs/13, docs/20 or docs/28 already have Ghidra-verified ranges, those are used.

Confidence tags as in AGENTS.md: [verified] = seen in the binary (string, import, code reference); [inferred] = conclusion from that evidence; [community] = public knowledge, not checked here.

## 1. Libraries linked into TS3W.exe

| Library | Version | Linked | What it does in the game | Approx. code (TS3W VA) | License class | Public 64-bit version | Stutter relevance | Conf. |
|---|---|---|---|---|---|---|---|---|
| **Mono** runtime (interpreter, metadata, marshal, icalls) | `Mono 1.2.3.1` string at `0x10B4BD8`, ref `0xE82161` | static (no mono.dll) | Runs all C# script DLLs (Sims3GameplaySystems, UI, store objects); mint interpreter, no JIT (docs/29) | `0xE20000-0xEF0000`, `0xD82000-0xDC0000` (docs/20, Ghidra) | open source (LGPL runtime, MIT class libs) | Modern Mono and .NET yes; **1.2.3 has no Win64 port** [community] | **Sim thread, top cost**: interpreter 28.5% + runtime 14.8% (docs/28 N1, N2) | [verified] |
| **Boehm-Demers-Weiser GC** | 7.1-ish with EA edits (docs/13), `GC_gww_read_dirty` present | static, inside Mono | Managed heap, stop-the-world mark, finalizers | `0xE4A000-0xE79000` (refs `0xE4D436-0xE78E80`); `GC_try_to_collect` `0xE4A050` | open source (MIT-style) | yes (bdwgc 8.x) | **Sim thread**: GC 9.9% (SV 17%), finalizers ~1.8 ms/s (docs/28 N4, N5) | [verified] |
| **EA GeneralAllocator** (PPMalloc family) | unknown | static | EA native heap ("arena allocator") under almost every native subsystem | core `0x4E5E40`, lock wrapper `0x4E69D0`, debug checks `0x4E78AF`/`0x4E7930` (`GeneralAllocatorDebug::Verify*` strings) | EA internal | no | **Sim thread**: 6.9% (BP 9.9%) (docs/28 N6); our `[Alloc]` thread cache sits here | [verified strings, inferred name for `0x4E5E40`] |
| **RefPack** (EA QFS compression, `Services/UTFFoundation/CompressionRefPack`) | n/a | static | Decompresses .package resources | `0x4A70B5`, `0x4EC115`; decode core `0x4EB3B0` (docs/28 N15) | EA internal (format public via community tools) | community decoders (s3pe, s4pe) are portable | Sim thread in live mode only 0.24%; bigger at load | [verified] |
| **EASTL** | pre-open-source (2008-era) | static (templates) | Containers everywhere (`EASTL fixed_vector`, `fixed_hash_map`, ...) | spread over the whole exe | EA internal at the time; now BSD-3 on GitHub | yes (open-source EASTL) | none of its own (inlined into callers) | [verified] |
| **EAStdC, EAIO, EACallStack, ArgScript, UTFFoundation, UTFXML, UTFSockets** (EA core "services") | unknown; "Copyright (c) 2005/2006, Electronic Arts" | static | String/time utils, file streams and directory notification (`Services/EAIO/*`), crash call stacks with PDB/MAP readers and `DasmX86Dll.dll`, ArgScript config parser, XML DOM reader, socket wrapper | EACallStack `0x4CC900-0x4D4A00`; UTFXML `0x4B4500-0x4B8700`; EAIO mostly `0x4A1B90-0x4DD000` | EA internal (EAStdC, EAThread, EAAssert later open-sourced; EAIO, EACallStack, ArgScript not) [community] | only the open-sourced ones | low; EAIO stream reads happen on the thread that loads | [verified strings, ranges inferred] |
| **EAText** | unknown | static | Font server, glyph cache, bidi typesetter, bitmap/outline/polygon fonts; script ICs `EAText_GetMoneyString` etc. | refs `0x44EECB-0x46218D` core, renderer near `0x691F20` | EA internal | no | UI/render thread [inferred]; C# string formatting ICs run on the sim thread but are cheap (not a docs/28 row) | [verified] |
| **UTFWin** (EA UI window/controls framework, `EA::UTFWin`, `EA::UTFWinControls::WinButton`, ...) | unknown; "Copyright (c) 2005, Electronic Arts" | static | Native widget layer under the C# `Sims3.UI` layouts (`.layo`) | refs `0x50B7C8-0x77EF76` (wide spread) | EA internal | no | UI work, partly on the sim thread through UI ICs [inferred]; M-UI maps it | [verified] |
| **SpeedTreeRT** (IDV) | 4.x [inferred from `CSpeedTreeRT`, `__IdvSpt_02_` file tag] | static | Trees: geometry, LOD, wind, billboards (`SpeedTree/CWindEngine`, `CFrondEngine`, ...) | RT core `0x7A0F40-0x7C1D70`, SpeedWind `~0x7C2000-0x7C4450`, EA glue `0x70D990-0x718620` (docs/34; the earlier string-cluster range `0x70E000-0x7BF000` held ~550 KB of other EA code) | commercial | SpeedTree licensees get x64 SDKs; **no public SDK for 4.x** | render side (`SpeedTreeClusterModelSet` under SceneObjectManager) [inferred]; not in docs/20 sim families | [verified strings, version inferred] |
| **RenderWare Audio Core** (`rw::audio::core`, "RWAC") | unknown | static | Mixer, DSP plug-ins (reverb, Butterworth, TimeStretch, Pan3D, Dac), DirectSound output; EA `Audio/*` layer on top (ambience, emitters, playlists) | RWAC `0xED7000-0xEFE000`; EA audio glue `0x89F000-0x8BC000` | EA internal (Criterion lineage) | no | own thread(s) [inferred]; see docs/31 (audio map, separate doc) | [verified] |
| **RWMovie + On2 VP6** (playback), **VP6 encoder** (in-game video capture), EA-XA / EALayer3 audio codecs | VP6 (`VP60` FourCC), `VideoEncoder_Vp6` | static | Intro/EP movies (`.vp6`, `MVhd` container), "record video" feature (VP6 + audio encoder), `RWMovie audiorenderer_rwac` | video refs `0x9D8D51-0x9E3837`, also `0x86B571`, `0x89B856` | commercial (On2 VP6) + EA internal | FFmpeg has a VP6 decoder (LGPL, x64); no public VP6 encoder | only while a movie plays or recording runs | [verified] |
| **zlib** | **1.1.3** (`deflate 1.1.3 ... Gailly`, `inflate 1.1.3 ... Adler`; `1.1.3` referenced from libpng code at `0x434415`, `0x4403FE`) | static | Deflate/inflate for PNG (only callers seen sit next to libpng) | `0x432000-0x434000` | open source (zlib) | yes (zlib 1.3.x, zlib-ng, libdeflate) | off the sim tick; thumbnail/PNG decode [inferred] | [verified version] |
| **libpng** | **1.0.5** [inferred: `1.0.5` passed from three game call sites `0x41764B`, `0x417A2D`, `0x417F35`, the `png_create_*_struct` version argument pattern] | static | PNG thumbnails, UI images, screenshots | `0x4343DD-0x4411D2` | open source (libpng) | yes (1.6.x) | off the sim tick [inferred]; **crash risk** with malformed PNGs (1.0.5 is from 1999, many later CVEs) | [verified lib, version inferred] |
| **IJG libjpeg** | **6a** (`6a  7-Feb-96`, "Copyright (C) 1996, Thomas G. Lane") | static | JPEG decode/encode (photos, screenshots, JFIF thumbnails) | ~`0x42D000` (message table referenced once at `0x42D573`) | open source (IJG) | yes (libjpeg-turbo, x64, SIMD) | off the sim tick [inferred] | [verified] |
| **Expat** | unknown (UTF-16 build: wide `XML_DTD` strings) | static | XML parsing, probably under EA's UTFXML / XmlStreamAdapter [inferred] | not located (error table is a pointer array) | open source (MIT) | yes | load/config parsing; not in sim families | [verified presence] |
| **gSOAP** | **2.7** (`gSOAP/2.7`) built without OpenSSL and gzip | static | SOAP clients for EA web services (`Sims3UploadDownloadService`, auth tokens, manifests) | `0x9F52F6-0xA1A057` | open source (GPL / commercial dual) | yes | worker threads only; services are offline | [verified] |
| **DirtySDK** (EA networking: ProtoSSL/ProtoHttp; `SSL processing error ... DirtySDK error code`) | unknown | static | TLS and HTTP transport for EAWebKit (`EAWebKit/TransportHandlerDirtySDK`) and online jobs | refs `0x50242A`, `0x9F3B8E-0x9F495D` | EA internal | no | none (online only) | [verified] |
| **Blaze WAL / Crossroads** (EA social backend client) | n/a | static | Login, friends, wall posts, passports, badges; ~70 `BlazeWal*Thread` / `Crossroads*Thread` jobs | `0xA1C7BD-0xA3F58A` (p10-p90) | EA internal | no | own short-lived threads; servers dead, mostly idle (docs/03, docs/16) | [verified] |
| **IGA Radial SDK** (in-game advertising) + **Lua 5.1** + **LuaSocket 2.0.2** + MD5/RSA helpers | Lua `Lua 5.1` (ref `0xF5BC4E`), `LuaSocket 2.0.2` (ref `0xF5F887`), SDK `6.1.0.5` / `6.0.12.0` [inferred as SDK versions], `radial.tdb`/`radial.sdb` | static | Ad SDK scripted in Lua: `iga_lua_threadfunc`, registry helpers, ad-server HTTP via LuaSocket; `InGameAdService`, `IGACacheRefresherThread` | IGA `0xF48000-0xF49000` (+ `0x9E5272` service glue); Lua `~0xF4C000-0xF5F000`; LuaSocket `0xF5F887-0xF660A1`; MD5/RSA `0xF5EEEF-0xF5F2B2` | Lua and LuaSocket open source (MIT); Radial SDK commercial (vendor defunct) [community] | Lua/LuaSocket yes; SDK no | **own thread** (`iga_lua_threadfunc`) [verified string, thread inferred]; Lua is **not** used by game logic | [verified] |
| **EAWebKit glue** | loads `EAWebkit.dll` (UTF-16 name, refs `0x9E6D53-0x9E6D70`), UA `EAWebKit/1.25.00` | dynamic (LoadLibrary + `CreateEAWebkitInstance`) [inferred from strings] | In-game store / web pages | `0x9E6E30` area | see §2 | | only when a web view opens | [verified strings] |
| **TransGaming Cider** hooks (`IsTransgaming`, `TGGetVersion`, `TGMACOSGetVersion`, `TGSwitchFocusToExecutable`) | n/a | static stubs | Mac wrapper detection; inert on Windows [inferred] | small | commercial (TransGaming) | n/a | none | [verified strings] |
| **Telemetry** (`Misc/Network/Telemetry/*`) | n/a | static | Event queue written to file and uploaded | `0x892905-0x8935EE` | EA internal | n/a | low (file append) [inferred] | [verified] |
| **XInput** | `xinput1_3`, `xinput1_1`, `xinput9_1_0` tried in order | dynamic | Gamepad (`gc_button_*` bindings) | small | Microsoft | yes | none | [verified strings] |
| **MSVC 2005 CRT** (MSVCR80, MSVCP80) | VC++ 8.0 (`compiler, version 1400`; manifest `Microsoft.VC80.CRT`) | DLL | C runtime: malloc (replaced by S3SS mimalloc in the baseline), memcpy, streams | system DLL | Microsoft redistributable | yes | memcpy/memset 2.2% of sim samples (docs/20) | [verified] |
| **D3DX 9 (d3dx9_31)** | April 2006 SDK | DLL | Only `D3DXGetShaderSize`, `D3DXFillVolumeTexture` | system DLL | Microsoft redistributable | yes | none | [verified] |

**Not present** (string scan of all binaries) [verified absence of their usual strings, so inferred absent]: libcurl and OpenSSL (the earlier "curl" hit is the CAS hair `CurlDeltas`; gSOAP says `OpenSSL not installed`; TLS goes through DirtySDK ProtoSSL), FreeType (EAText has its own outline fonts), ICU (only Mono's unused `ICU_collator` name), PCRE, TinyXML, SQLite (only Mono's `Mono.Data.SqliteClient` name), Havok, PhysX or any other physics SDK (physics is EA code, e.g. `Physics/RangeShapeResult`), Scaleform, Bink/Miles/Granny (RAD) in TS3W, FMOD/Wwise, Steamworks (`steam_api.dll` not imported). SecuROM: only message strings (`gSecuRom/ErrorMessage:FailToServer`) remain; no SecuROM or SteamStub (`.bind`) section in the Steam exe [verified sections].

## 2. DLLs in `Game\Bin`

| File | What it is | Linked into game? | Contents | Conf. |
|---|---|---|---|---|
| `EAWebkit.dll` (3.7 MB, 2011-05-11, base `0x10000000`, not LAA) | **EAWebKit 1.25.00** (`@EAWebKit: 1.25.00 VS: 2005`), WebKit from the OWB (Origyn Web Browser) port, AppleWebKit/525 (2008) with its JavaScript engine (XPath evaluator etc.) | dynamic, loaded by TS3W on demand [inferred]; one export `CreateEAWebkitInstance` | own **libpng 1.2.29**, **IJG libjpeg 6b** (`6b  27-Mar-1998`), zlib version tag `1.1.3`; imports only KERNEL32, USER32, MSVCR80/MSVCP80 (network and allocator are passed in by the host) | [verified] |
| `TSLHost.dll` (5.2 MB, 2014-01-21) | **The Sims Launcher host**: 99 exports `AppManager_*`, `S3L_*` (disc check, Origin requirement, install validation, spawn exe) | **no**: loaded by the .NET launcher `Sims3LauncherW.exe` (string `TSLHost.dll` there; TS3W has none) [verified strings] | statically re-links a big slice of the game engine: EAText, RW Audio Core plug-in docs, gSOAP 2.7, Blaze/Crossroads, DirtySDK, IGA + Lua 5.1, zlib 1.1.3, libpng 1.0.5, libjpeg 6a, telemetry | [verified] |
| `Sims3GDF.dll` (4.4 MB, 2014-01-21) | Windows Game Explorer **Game Definition File**: resource-only (4.3 MB `.rsrc`), GDF XML says version `1.67.2.024037` | no (read by Windows) | icons, ratings XML | [verified] |
| `Sims3StoreObjects.dll` (1.2 MB) | **Managed .NET assembly** (imports only `mscoree`, metadata `v2.0.50727`): C# store objects (saunas, gnomes, ...) | loaded by the embedded Mono as a script assembly | IL only, no native code | [verified] |
| `ddraw.dll` (2.1 MB, 2021-02-04) | **Ultimate ASI Loader** (ThirteenAG, MIT) renamed to `ddraw.dll`; TS3W imports `DDRAW.dll!DirectDrawCreateEx`, so it loads first and then loads `*.asi` | yes (community mod, not EA) | proxy exports for many system DLLs; embeds RAD Bink/Miles stubs and a Borland/madTools component in its resources [verified strings] | [verified] |
| `TS3Patch.asi`, `D3DShaderReplacer.asi` | community ASI mods present in this copy (VC++ 2015+ CRT; the shader replacer uses `d3dx9_43` + `D3DCompiler_43`) | yes, via the ASI loader | not EA; out of scope | [verified imports] |
| `Sims3LauncherW.exe` (.NET), `S3Launcher.exe`, `Sims3Launcher.exe` (LauncherRunner), `TS3.exe`, `TSLHelper.exe` | launcher chain (VC80 natives + one .NET exe) | no (separate processes) | | [verified imports] |

## 3. TS3W.exe PE imports by DLL

Sections: `.text` `0x401000` (11.6 MiB), `.rdata` `0xF95000` (1.7 MiB), `.data` `0x1146000`, `.tls`, `.rsrc`; no `.reloc`, no delay imports [verified].

| DLL | Count | Used for |
|---|---|---|
| `d3d9.dll` | 1 | `Direct3DCreate9` (the single render device; DXVK replaces this DLL) |
| `d3dx9_31.dll` | 2 | `D3DXGetShaderSize`, `D3DXFillVolumeTexture` |
| `DDRAW.dll` | 1 | `DirectDrawCreateEx` (VRAM query); the hook point the ASI loader uses |
| `DSOUND.dll` | 2 | ordinals #1, #9 (`DirectSoundCreate` and one more) for RWAC output |
| `WINMM.dll` | 3 | `timeBeginPeriod`, `timeEndPeriod`, `timeGetTime` |
| `WININET.dll` | 19 | HTTP for online features, store, content patch checks |
| `WS2_32.dll` | 42 | Winsock (mostly by ordinal) for DirtySDK, LuaSocket, gSOAP |
| `iphlpapi.dll`, `NETAPI32.dll` | 2, 1 | `GetAdaptersInfo/Addresses`, `Netbios` (machine id / MAC) |
| `dbghelp.dll`, `PSAPI.DLL` | 7, 1 | EACallStack crash call stacks (`SymInitialize`, `SymGetLineFromAddr64`, ...), `GetModuleInformation` |
| `MSVCR80.dll`, `MSVCP80.dll` | 216, 16 | VC++ 2005 CRT and iostreams |
| `IMM32.dll`, `USP10.dll` | 8, 2 | IME input; Uniscribe `ScriptItemize`/`ScriptBreak` for EAText line breaking |
| `SETUPAPI.dll` | 4 | device enumeration (hardware detection) |
| `VERSION.dll` | 3 | file version queries |
| `ole32.dll`, `OLEAUT32.dll`, `SHELL32.dll` | 9, 5, 4 | COM init, GUIDs, BSTR helpers, `SHGetFolderPathW`, `ShellExecuteEx` |
| `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ADVAPI32.dll` | 178, 89, 8, 41 | system (threads, files, window, registry) |

Loaded at run time by name [verified strings]: `EAWebkit.dll`, `xinput1_3/1_1/9_1_0.dll`, `rasapi32.dll`, `NVCPL.dll`, `glu32.dll`, `DasmX86Dll.dll` (EACallStack disassembler), `ntdll.dll`; Mono's `mscorlib.dll` / `System.dll` are the managed assemblies in the packages.

## 4. Implications

### x64 question (for docs/31 x64 item 1)

- **The blocker is EA's own code, not the middleware.** Most of the 11.6 MiB `.text` is EA engine code (sim, routing, renderer, UTFWin, EAText, RWAC, allocator) shipped without source; every third-party library listed here is a small share of it. A 64-bit TS3W needs that EA code recompiled or rewritten function by function (PLAN.md §5: 40,263 functions) [inferred].
- **Blocking even with a rebuild:** Mono 1.2.3 (no Win64 port, EA-edited Boehm, icall ABI baked into the exe; the script DLLs are pure IL and would run on a modern 64-bit Mono only after every icall is rebound) [inferred]; SpeedTreeRT 4.x (commercial, no public x64; trees would need a re-implementation or a stub); RenderWare Audio Core and RWMovie (EA internal, no public source; replaceable only by a new audio/movie backend).
- **Replaceable with public x64 versions:** zlib, libpng, libjpeg, Expat, gSOAP, Lua 5.1 and LuaSocket, EASTL (open source now), MSVC CRT, D3DX9, XInput, Ultimate ASI Loader, EAWebKit (open-source LGPL, newer x64 builds exist [community]).
- **Removable (dead services, no gameplay value):** IGA Radial SDK with its Lua and LuaSocket, Blaze/Crossroads, gSOAP web services, DirtySDK, telemetry, TransGaming stubs. A port could stub them.

### Hook or upgrade candidates

| Candidate | Why | Expected effect | Priority |
|---|---|---|---|
| **libpng 1.0.5 + zlib 1.1.3** in TS3W | 1999/1998 code with many later CVEs (zlib 1.1.3 has the 2002 double-free); CC thumbnails and images are untrusted input | **crash hardening** for bad CC PNGs: route the `png_create_read_struct` callers (`0x41764B`, `0x417A2D`, `0x417F35`) to a modern decoder; speed gain small (not on the sim tick) [inferred] | medium (crash), low (stutter) |
| **libjpeg 6a** | libjpeg-turbo is a drop-in API with SIMD, 2-6x faster decode [community] | faster photo/screenshot/thumbnail decode; no sim-thread gain [inferred] | low |
| **RefPack decode** (`0x4EB3B0`) | hot during loads and lot streaming; already in docs/08 and docs/28 N15 | faster load and streaming hitches with a tuned decoder or prefetch on a worker | medium (load time) |
| **EA GeneralAllocator** (`0x4E5E40`) | 6.9% of sim samples, lock contention with workers | `[Alloc]` thread cache exists (crash suspect under test); this is the right layer | high (existing work) |
| **Boehm GC / Mono** | 55% of the sim thread together | existing work (docs/13, docs/29, docs/30); no drop-in upgrade possible for an EA-edited 1.2.3 | high (existing work) |
| **MSVCR80 memcpy/memset** | 2.2% of sim samples under the interpreter | a modern memcpy via IAT hook might save a fraction of that; measure first [inferred] | low |
| **IGA / Lua thread, Blaze jobs** | dead services that still start threads and timers | disable rather than upgrade (S3SS Online Nuke covers part, docs/16); Lua upgrade is pointless because no game logic uses Lua | low |
| **EAWebKit 1.25** | 2008 WebKit, own old libpng 1.2.29 / libjpeg 6b; only loads for the store/web views | stub the store view if it crashes; no stutter role | low |
| SpeedTreeRT, RWAC | render/audio threads, not sim-thread families in docs/20 | no change from this inventory; M-D3D and M-AUD (docs/31) map them | n/a |
