# The Sims 3: existing performance and memory fixes compared

Researched 2026-10-10 for a Steam copy with all expansions, running on Windows 11 on an Alienware m15 R6 (i7-11800H, 8 cores and 16 threads, 32 GB RAM, RTX 30-series laptop GPU).

How sure each claim is: **[verified]** means checked against the project's own page or source on the date above. **[community]** means it's widely reported but I didn't confirm it myself. **[inferred]** is my own reasoning, which we'll check in step 3 (reverse engineering).

---

## 1. The real limits first

| Limit | What actually happens | Can it be fixed? |
|---|---|---|
| **The 4 GB address space** | TS3W.exe is a 32-bit program. With the Large Address Aware (LAA) flag on 64-bit Windows, it can address 4 GB at most, however much RAM you have. Once its virtual address space fills up, you get **Error 12** or a crash on save. [verified, Nexus mod 235 and the S3SS docs] | **It can't be raised** without rebuilding the game as 64-bit. You can only **waste less of it** by trimming stacks, caches and allocator overhead, and by moving texture memory out of the process with DXVK. Your 32 GB is still useful: Windows caches game files in it, so loads are faster. |
| **"Doesn't use multiple cores"** | The simulation (the Mono script VM, the scheduler that runs every Sim's interactions, and pathfinding) mostly runs on one main thread. A handful of helper threads deal with streaming, audio and decompression. [community, matches what S3SS patches] | **Partly.** You can make the main thread cheaper per tick (Smooth Patch, the GC throttle, faster decompression, faster lot streaming) and push side work onto other cores, as DXVK does by compiling shaders and submitting work on its own threads. Making the sim itself truly parallel would be a very deep rewrite. That's the step 3 research question. |
| **No frame or tick cap** | The game ticks and renders as fast as it can. That causes "fast forward" sims, CAS lag, heat and throttling on laptops. [verified, Steam forum] | **Yes.** Smooth Patch or S3SS caps the tick rate, and DXVK or the Nvidia driver caps FPS. |
| **Newer GPUs aren't recognised** | GraphicsRules.sgr only knows GPUs from around 2009 to 2013. An unknown card falls back to low settings and 32 MB of texture memory. [community, EA forums] | **Yes.** DXVK reports the GPU as a GTX 580 to TS3W.exe (this is built into DXVK) [verified, DXVK source `config.cpp`]. You can also edit GraphicsRules.sgr by hand. |

---

## 2. The fixes, one by one

### A. Sims 3 Settings Setter (S3SS), by sims3fiend. **Core recommendation**
- **What it is:** an ASI plugin (`Sims3SettingsSetter.asi`) with an in-game overlay (press Insert). It applies assembly patches to the running TS3W.exe. Version 1.6.3 was current when checked. Source: https://github.com/sims3fiend/Sims3SettingsSetter [verified]
- **What it fixes:**
  - **Memory:** the Oversized Thread Stack Fix (saves about 80–170 MB of address space), the WorldCache 512 MB uncap, a **mimalloc allocator** swap, and a **memory monitor** that warns you as you get close to 4 GB.
  - **CPU and threading:** CPU Thread Optimization (thread placement for hybrid or multi-CCX CPUs, which also acts as the Alder Lake fix), Smooth Patch "Precise" (tick limiter, 480 TPS by default, plus an optional 60 FPS cap), and a 1 ms timer.
  - **Stutter:** the GC Finalizer Throttle, the "Chunky" patch that skips `GC_try_to_collect` in the sim loop, a SIMD (AVX2) RefPack decompressor, and Lot Streaming optimisations.
  - **Other:** expanded crash logs, borderless window, and live editing of GraphicsRules and Variable settings.
- **Risks:** several patches are marked experimental. Chunky raises memory use. A WorldCache value set too high can crash the game. Uncompressed textures aren't recommended without DXVK. The overlay can crash with some d3d9 wrappers; the workaround is `disable_overlay = true` under `[qol.ui]` in `S3SS.toml`.
- **Steam:** the README has no explicit version list but says Steam, EA App and retail builds differ internally. The patches find their targets by pattern, so the Steam build should work. **We'll confirm this in the baseline test.**
- **Most relevant to us:** it's the closest existing project to our goal and the open-source reference for step 3. It already proves that ASI-hooking TS3W.exe works.

### B. Sims 3 Performance Patch, by Sonja92yu (Nexus mod 235)
- **What it is:** an ASI plugin (`Sims3Performance.asi` + `.ini`) that needs Ultimate ASI Loader as `wininet.dll`. Version 1.1 was released in July 2026 and is labelled a **beta**. Link: https://www.nexusmods.com/thesims3/mods/235 [verified]
- **What it fixes:**
  - **Load time:** a faster index for script-type lookups (the largest gain it claims) and background pre-reading of Mods package indexes.
  - **Memory:** smaller string-table layouts (about 290 MB claimed), smaller thread stack reservations (about 70 MB), and tuned arena growth.
  - **Smoothness:** text-lookup and type-check caches.
- **Risks:** it's a beta, and the text cache can produce blank or wrong labels. It supports Steam, EA App and disc versions.
- **Overlap with S3SS:** both shrink thread stacks, and the author says it's partly based on S3SS. **Don't enable both stack fixes at once** [inferred]. Add it after S3SS in the baseline, and test it with S3SS's stack fix turned off.

### C. DXVK (D3D9 → Vulkan), by doitsujin
- **What it is:** a replacement `d3d9.dll` from the release's **x32** folder that you put next to TS3W.exe. Link: https://github.com/doitsujin/dxvk/releases [verified]
- **What it fixes:**
  - **Error 12.** Native D3D9 keeps copies of "managed" textures inside the 32-bit process. DXVK unmaps locked textures from the process address space and caps the mapped amount with `d3d9.textureMemory` (default 100 MB). This is the single biggest address-space saving available. [verified, dxvk.conf]
  - **GPU detection.** DXVK has a built-in Sims 3 profile that reports a GeForce GTX 580, so the game picks proper settings without any GraphicsRules edits. [verified, DXVK source]
  - **Multi-core rendering.** Shader compilation and command submission happen on DXVK's own worker threads. [verified behaviour of DXVK in general]
  - **FPS cap and vsync:** `d3d9.maxFrameRate`, `d3d9.presentInterval`, and a HUD (`dxvk.hud`) for measuring.
- **Risks:** a little stutter the first time each shader compiles, which goes away once the cache warms up. Overlays such as Steam, Discord or GeForce can conflict. It needs Vulkan, which your RTX 30-series card supports.
- **Steam:** works. The DLL simply sits in `Game\Bin`.

### D. Smooth Patch (original), by LazyDuchess
- **What it is:** an ASI or `.package` combination that limits the simulation's ticks per second (TPS) so the sim isn't starved by rendering, or the other way round. Smooth Patch 2.0, a script mod, adds faster CAS clothing loads, CAS audio fixes and MasterController CAS compatibility. Links: LazyDuchess on Patreon (https://www.patreon.com/posts/67835239) and Tumblr. [verified]
- **Overlap:** S3SS ships both an "Original Flavour" port and its own "Precise" version. **Use S3SS's version and don't install LazyDuchess's ASI as well**, but do keep the Smooth Patch `.package` in Mods, as the S3SS docs say. [verified]

### E. LazyDuchess's Alder Lake / 12th-gen exe patcher
- It fixes TS3W.exe failing to start on Intel 12th-gen and newer CPUs. EA fixed the EA App build in January 2025; **the Steam and disc builds still need the patcher.** [verified, EA forums]
- **You don't need it**, because your i7-11800H is an 11th-gen (Tiger Lake) chip with no E-cores. It's listed here for completeness, and as a reminder that exe patching has a precedent.

### F. The LAA / "4GB Patch" (NTCore 4GB Patch, CFF Explorer)
- It sets the `IMAGE_FILE_LARGE_ADDRESS_AWARE` flag so a 32-bit exe can use 4 GB instead of 2 GB.
- **Status on Steam:** the community consensus is that TS3W.exe has shipped with LAA since patch 1.67. [community; **to be checked in the baseline**, see guide step 2.3]. If it's already set, patching again does nothing. If it's ever reset, which can happen after "Verify integrity", it **doubles** the usable address space.
- **Risk:** Steam may restore the original exe after an update. Keep a backup.

### G. NRaas suite (nraas.net)
- **Overwatch:** regularly cleans up orphaned Sims and objects, broken relationships and similar junk, so saves stay small and the sim loop has less to do. **ErrorTrap:** catches script exceptions and repairs corrupt data. **MasterController**, **StoryProgression** and **Register** are optional gameplay management mods. [community]
- **What it helps:** long-term save bloat and lag in large neighbourhoods. It doesn't change the engine.
- **Steam:** fine. Mods go in `Documents\Electronic Arts\The Sims 3\Mods\Packages`, along with a `Resource.cfg`.

### H. Config and housekeeping tweaks
| Tweak | Value | Notes |
|---|---|---|
| Delete the cache files before each test | `CASPartCache`, `compositorCache`, `scriptCache`, `simCompositorCache`, `socialCache.package`, the `WorldCaches\*` and `Thumbnails\*` folders, `DCCache\dcc.ent` and `missingdeps.idx` | Standard practice; caches that keep growing cause bloat. [community] |
| GraphicsRules.sgr / texture memory | Leave it alone if you use DXVK | DXVK's GPU spoof does this job. Hand-editing is only for running without DXVK. [community] |
| A "max memory" setting in an `.ini` file | **Treat as a myth** | An EA forum helper and the address-space math agree that the game can't be made to use more than about 4 GB. |
| Windows 11 "High performance" GPU setting for TS3W.exe, and the Nvidia per-app profile | Required on a laptop | Otherwise the game may run on the Intel iGPU. |
| Disable the Steam overlay | Recommended with DXVK | Fewer hooks fighting over d3d9. |

### I. Other things you'll come across (not recommended for us)
- **"RAM booster" / memory-cleaner tools:** they don't touch the 32-bit address space limit at all, so they're snake oil for this problem.
- **Running TS3 as a 64-bit process:** there's no 64-bit Windows build. EA's old macOS 64-bit build (2020) is a different binary and doesn't apply.
- **Process Lasso / CPU affinity tricks:** at most a small effect. S3SS's thread placement already covers it.

---

## 3. Summary matrix

| Fix | Address space (Error 12) | FPS / stutter | Load times | Uses more cores | Risk | Steam OK |
|---|---|---|---|---|---|---|
| DXVK | ★★★ | ★★ | ★ | ★★ (render) | Low | ✅ |
| S3SS | ★★ (stacks, mimalloc, monitor) | ★★★ (Smooth, GC, streaming) | ★★ (RefPack SIMD) | ★ (placement) | Medium (experimental toggles) | ✅ (to confirm) |
| Sims 3 Performance Patch | ★★ (strings, stacks) | ★ | ★★★ | – | Medium (beta) | ✅ |
| Smooth Patch (via S3SS) | – | ★★★ | – | – | Low | ✅ |
| LAA flag | ★★★ if missing, 0 if already set | – | – | – | Low | ✅ |
| NRaas Overwatch / ErrorTrap | ★ (save bloat) | ★★ (long saves) | ★ | – | Low | ✅ |
| Alder Lake patch | – | – | – | – | – | Not needed (11th gen) |

**Where our own work can add something (steps 3 and 4):** no existing project parallelises the simulation tick, moves Mono's heap or GC work onto other threads, or offloads the script VM. Two areas look like the best places to start. **[inferred]**
- **Stable fixes for the address-space limit:** large reserved arenas, the string tables and the remaining managed-texture copies.
- **Work on the main thread that could go to workers:** asset decompression, routing and pathfinding queries, and lot streaming. S3SS and the Performance Patch only make some of these faster in place.

Both existing ASI projects give us working hook points and patterns to build on.

---

## 4. Tooling for steps 3 and 4 (reverse engineering)

### REA, "Reverse Engineer Anything" (github.com/morluto/rea). **The repo Rodney mentioned**
- **What it is:** an MCP server and CLI, MIT licensed, that gives an AI agent (Claude Code among others) reverse-engineering tools. It's at about 45k stars and is trending as of October 2026. It installs with `npx rea-agents setup` and needs Node 22 or later. [verified, repo README]
- **Native binaries:** it wraps **Ghidra**, IDA or Hopper to give pseudocode, assembly, strings, symbols, call graphs and cross-references. Ghidra handles 32-bit x86 PE files like TS3W.exe without trouble. **This is how we'd drive the analysis of TS3W.exe.** [verified for the backends; using it on TS3W specifically is inferred]
- **.NET assemblies:** it can read metadata and CIL statically. That fits the game's script DLLs (`Sims3GameplaySystems.dll`, `ScriptCore.dll`, `Sims3GameplayObjects.dll`, `UI.dll` and so on), which run on the game's embedded Mono. [inferred]
- **Gaps for us:**
  - Runtime tracing is Linux and macOS only. On Windows we'd still use **x64dbg** (its 32-bit build), **Frida** or our own ASI hooks for live profiling.
  - Its README doesn't mention Mono itself.
- **Plan:**
  - In step 3, run REA with Ghidra **here in the project's cloud workspace** on copies of TS3W.exe and the script DLLs. That way nothing needs installing on your laptop for the static analysis.
  - Do the runtime profiling on the laptop with your baseline save.

### Other tools we'll use alongside REA
| Tool | Used for |
|---|---|
| Ghidra (NSA, open source) | The engine REA drives; it can also be used by hand on TS3W.exe |
| ILSpy / dnSpyEx | Decompiling the Mono script DLLs to C# for reading and patching |
| x64dbg (32-bit) | Live debugging and breakpoints on TS3W.exe on Windows |
| Frida | Scripted runtime hooks and timing of hot functions without rebuilding a DLL |
| Ultimate ASI Loader + MinHook | How we'll load our own patch DLL (the same approach as S3SS and the Performance Patch) |
| Windows Performance Recorder / Superluminal / Tracy | Per-thread CPU profiling, to show what's stuck on the main thread |
| S3SS source (sims3fiend) | Existing signatures and hook points to build on |
