# Prior art for in-game stutter: reverse-engineering knowledge and community fixes

Researched 2026-10-11 for the Steam build of TS3W.exe (`GameVersion = 1.67.2.024037`, `Code:0.Steam-0.0.32`, PE timestamp 2014-01-21) on Windows 11. Scope is in-game stutter and "simulation lag" (stuck Sims, threading, bad world routing). Load times are out of scope. This document does not repeat `docs/01-existing-fixes-comparison.md` except where it corrects or extends it.

How sure each claim is:
- **[verified]**: I read the primary source code, or checked it directly in the local Steam `TS3W.exe` (static string and byte scans only; nothing was run).
- **[community]**: reported by players or modders, not confirmed by me.
- **[inferred]**: my own reasoning, to be confirmed in Ghidra or by measurement.

Local install inspected (read-only): `D:\SteamLibrary\steamapps\common\The Sims 3\Game\Bin`.

---

## 0. Findings about the local install that change the baseline plan

These come from reading file metadata and text files in `Game\Bin`. They matter before any measurement is taken.

| File | What it is | Status |
|---|---|---|
| `ddraw.dll` (2,160,128 bytes, 2021-07-23) | **Ultimate ASI Loader by ThirteenAG**, not the Windows DirectDraw DLL and not an EA file. Its version resource says `FileDescription = Ultimate ASI Loader`, `CompanyName = ThirteenAG`. TS3W.exe loads `ddraw.dll` from its own folder, and the loader then loads every `*.asi` next to it. | [verified] |
| `TS3Patch.asi` + `TS3Patch.txt` | **LazyDuchess's Smooth Patch** ASI. The embedded PDB path is `E:\source\repos\TS3FrameratePatch\Release\TS3Patch.pdb`, which matches the GitHub repo `LazyDuchess/TS3FrameratePatch`. Config: `TPS = 1000`, `FPSLimit = 120`, `HighPrecision = 0`. | [verified] |
| `D3DShaderReplacer.asi` + `.cfg` | LazyDuchess's D3D9 shader-replacement hook (`LazyDuchess/D3DShaderReplacer`, PDB path `E:\source\repos\D3DShaderReplacer\...`). Config `Mode = Replace`, `ExtraData = True`. Some lighting/shader mods ship it. It hooks D3D9, so it can interact with DXVK and with the S3SS overlay. | [verified] |
| `TS3W.exe` PE header | `IMAGE_FILE_LARGE_ADDRESS_AWARE` is **set** (Characteristics `0x123`). Default `SizeOfStackReserve` is **1 MB**, which is what S3SS's thread-stack fix trims. | [verified] |

Consequences:
- **Correction to docs/01, section F:** LAA is not just "community consensus". It is set in this Steam exe. [verified]
- **Conflict to resolve before baselining:** `TS3Patch.asi` patches the same `SleepEx` wrapper (`0x004E1320` in this exe, see section 1) that S3SS's "Smooth Patch (Original Flavour)" patches. S3SS's README says not to run LazyDuchess's ASI alongside its own Smooth Patch. Remove or rename `TS3Patch.asi` when testing S3SS. [verified that both patch the same bytes]
- **ASI loaders:** docs/01 says the Sims 3 Performance Patch needs Ultimate ASI Loader installed as `wininet.dll`. This install already has the loader as `ddraw.dll`. Running two copies of the loader may load each `.asi` twice. Keep one loader and confirm in the S3SS log that each plugin initialises once. [inferred]
- `TS3Patch.txt` contains a `HighPrecision` option that is not in the public GitHub source (last pushed 2022-03-03), so the installed ASI (2022-10-23) is a newer, unpublished build. [inferred from dates and options]

---

## 1. Sims 3 Settings Setter (S3SS): every patch, with its anchors

Source: https://github.com/sims3fiend/Sims3SettingsSetter, commit `5eb2c65` (2026-07-22). I read every file in `patches/` plus `cpu_optimization.cpp`, `allocator_hook.cpp`, `dllmain.cpp` and `vtable_manager.cpp`. [verified]

How S3SS finds code: each patch has an `AddressInfo` with optional hard-coded addresses per build (`Retail`, `Steam`, `EA`) and a byte pattern with `??` wildcards plus a `patternOffset`. I re-ran the stutter-relevant patterns against the local Steam exe: **every one matched exactly once at the Steam address S3SS lists** (the PostRemoteMethodCall pattern matches 3 times, which S3SS handles). [verified]

The game's C runtime is **MSVCR80** (Visual C++ 2005), which S3SS's mimalloc hook targets. [verified]

### 1.1 Patches that target stutter

| Patch (display name) | What it hooks | Pattern (offset) and Steam address | Names and comments from the author |
|---|---|---|---|
| **Chunky Patch - Disable GC_try_to_collect()** (experimental) | NOPs the 5-byte `CALL GC_try_to_collect` inside `Sims3::MonoEngine::MonoScriptHost::Simulate` | `68 ?? ?? ?? ?? A3 ?? ?? ?? ?? E8 ?? ?? ?? ?? A1 ?? ?? ?? ?? 83 C4 04` (+10) → **0x00D819AA** | "GC_try_to_collect dominates simulation thread time so we just don't". Collection still happens from `GC_malloc` under memory pressure. Raises memory use. |
| **GC Finalizer Throttle** (experimental) | Two edits in `MonoScriptHost::Simulate`: (1) `CMP EAX, 0xC8` (200 frames) → `0x7FFF`; (2) NOPs the `JNZ` that loops back to `CALL mono_gc_invoke_finalizers` | `03 C5 3D C8 00 00 00 A3 ?? ?? ?? ?? 75` (+2) → **0x00D81A1D**; `E8 ?? ?? ?? ?? 85 C0 75 F7 89 1D` (+7) → **0x00D81A37** | The global is named `sFramesSinceFinalizeFinished`. After 200 consecutive frames where finalizers overrun their budget, the game runs a blocking loop "until ALL finalizers are done" on the sim thread ("Forcing all finalizers to finish"). The patch runs one batch per frame instead. |
| **GC_stop_world() Optimization** (experimental) | Replaces `CMP EAX, 0x100; JL` in `GC_stop_world` with `TEST EAX,EAX; JZ` to skip the thread loop when the count is 0 | `3D 00 01 00 00 7C ?? B8 FF 00 00 00 3B F8 7F` → **0x00E511F5** (Retail 0x00E514E5, EA 0x00E51245) | Author: "most useless patch". Called about once per frame. Useful as an anchor into the Boehm GC code. |
| **Smooth Patch (Original Flavour)** | Overwrites the start of a tiny wrapper `mov eax,[esp+4]; mov ecx,[eax]; push 1; push ecx; call [SleepEx]` with `mov ecx, <ms>; nop` (or `RET` for uncapped) | `8B 44 24 04 8B 08 6A 01 51 FF` → **0x004E1320** (1 match; the call goes to the IAT slot `0x00F951F0` = `KERNEL32!SleepEx`) | "Original game runs at 50 'TPS' (20ms sleep)". MSPT = 1000 / TPS. Because this is a shared sleep wrapper, it also changes sleeps unrelated to the simulator. Credits LazyDuchess, Shapes, Foul Play. |
| **Smooth Patch (Precise Flavour)** | (1) Redirects the `CALL Sims3::Scripting::ScriptHostBase::IdleSimulationCycle` made from `MonoScriptHost::Simulate` to its own hybrid `NtDelayExecution` + QPC busy-wait limiter. (2) Hijacks the "limit to ~30 FPS when the window is inactive" comparison after frame presentation to add an FPS cap and a "tick at most once per frame" signal. | `8B 86 C0 0A 00 00 3B C3 74 0F 8B 4C 24 5C 8B 11 6A 01 53 50 8B 42 24 FF D0 8B CE` (+27) → **0x00D81FDE**; `80 BE 8D 00 00 00 00 5E 74 15 8D 4C 24 24 51 C7 44 24 28 1E 00 00 00` → **0x00EC9FBA** | Script host field offsets: `+0xC08` "simulation running flag" (1 while Simulate runs), `+0xA60` `mbIdlingEnabled`, `+0xAC0` read before the idle call. Graphics device struct `+0x8D` = "window not foreground (or device lost)". Default 480 TPS, 60 FPS. Authored by "Just Harry" with LazyDuchess. |
| **Timer Optimization** | `NtSetTimerResolution(10000)` + `timeBeginPeriod(1)` | none (API calls) | Sleep granularity 15.6 ms → 1 ms. |
| **Adaptive Thread Waiting** (experimental) | IAT-hooks `WaitForSingleObject` / `WaitForSingleObjectEx` in TS3W.exe; spins 50–500 µs (adaptive) before falling back to a kernel wait, only for `INFINITE`, non-alertable waits | IAT hook | Comments name two call sites: `0x00E661AE` (Mono domain-unload path) and `0x00EBB2B0+0x14` (an APC pump that waits on `GetCurrentThread()` with 0 ms). Trades CPU for lower wake-up latency on short locks. |
| **Lot Streaming Optimizations** (four sub-patches) | (a) Object throttle: replaces `Sims3::World::Lot::AddLotObjectsToScene`, builds N objects per window and re-posts the rest cross-thread with `Sims3::Utility::PostRemoteMethodCall`; (b) map-view blocker: detours `Sims3::World::WorldManager::Update` and sets its `+0x258` "skip lot streaming" flag while `Camera_IsMapViewModeEnabled`; (c) visibility override: JZ→JMP on the camera-view bias in the lot visibility metric; (d) live settings `Throttle Lot LoD Transitions` = true, its `Max Active Lot Threshold` = 12, `Camera speed threshold` | AddLotObjectsToScene `83 EC 08 57 8B F9 80 BF C9 00 00 00 00 74 0E C6 87 C1 00 00 00 00 5F 83 C4 08 C2 08` → **0x00AC1130**; WorldManager::Update **0x00C6D570**; cameraBias JZ **0x00C63015**; Camera_IsMapViewModeEnabled **0x0073E060**; PostRemoteMethodCall 3 hits (0x00ABE9C0, 0x00ABEA50, 0x00B35DB0) | Also named: `Lot::UpdateObjectSceneNode` (per object, `__thiscall(lot,obj,char,char)`), `ScriptMessageScope` ctor/dtor (message IDs `0x4C55E8C`/`0x4C55EFA`), `World::IsObjectLargeOrFlora` (reads `Object+0x18` = `mResourceKey`), `Lot::SetActiveImpl`, `Lot::LoadLotObjects`. Author's hint: `PostRemoteMethodCall` "can probably be used for other fun things", i.e. it is the engine's own cross-thread call mechanism. |
| **CPU Thread Optimization** | Hooks `SetThreadIdealProcessor` (the Steam and disc builds call it with hard-coded processor numbers) and, on hybrid CPUs, restricts the process to P-cores for 5 s at start | API hook | Source comment: "The game uses a weird hardcoded SetThreadIdealProcessor call in the steam and disk versions". On an 11800H it only redistributes ideal processors. |
| **Animation Blend Tuning** | Clamps blend-in/out durations in `Sims3::SACS::PlayAnimation` (`AnimRequest +0x50` flags, `+0x68` blendIn, `+0x6C` blendOut) | three patterns around Steam 0x007F9876 / 0x007F9897 / 0x007F98B3 | Visual smoothness of animation transitions, not CPU stutter. |

### 1.2 Patches that help stutter indirectly (memory, I/O)

| Patch | Hook | Anchor |
|---|---|---|
| Oversized Thread Stack Fix (by "Just Harry") | Swaps the `_beginthreadex` pointer at a call site so threads created with stack size 0 reserve 64 KB instead of the 1 MB default | `6A 00 6A 00 C6 86 40 02 00 00 01 FF ?? ?? ?? ?? 00 83 C4 18 85 C0 89 86 24 02 00 00 74 0D` (+11) → **0x004DB355**. The description says the game creates "several dozen" of these threads "for watching file changes". |
| Mimalloc Allocator | Detours `MSVCR80!malloc`, `free`, `realloc`, `_aligned_malloc`, `_aligned_offset_malloc`, … at DLL attach | `MSVCR80.dll` exports |
| RefPack decompressor (SIMD) | Replaces the RefPack/QFS decompressor | `83 EC 10 8B 4C 24 1C 85 C9 53 55 56 8B 74 24 20 57 C7 44 24 1C 00 00 00 00 0F 84` → **0x004EB3B0**. The README calls it "probably the most impactful patch" for streaming stutter. |
| WorldCache Size Uncap | JC→JMP on the 512 MB WorldCache size check | `0F 82 ?? ?? ?? ?? 8B 0F E8` (Steam address commented out in source) |
| CreateFileW Random Access | Adds `FILE_FLAG_RANDOM_ACCESS` to the main `CreateFileW` callers (DBPF access is random) | Steam 0x004DB56C region |

### 1.3 Non-stutter patches (listed for completeness)
Brady Bunch Begone (`Sims3::Renderer::GlobalLightSettings` `mBradyBunchBlue` at `+0x270`/`+0x290`, `LightRig::AddBradyBunchBlueFloor`), Lighting Quality, Split-Level Lighting Fix (`Sims3::Renderer::BaseLight::GetLotID`), Mirror Settings, Uncompressed Sim/Lot Textures (`Sims3::CAS::ModelBuilder::SetBuildOptions`), Resolution Spoofer, Startup Warning Dialog Fix, Expanded Crash Logs, Online Nuke (disables `DownloadStoreFeaturedItems`). [verified]

### 1.4 Engine settings registry (useful for us)
S3SS detours the engine's **VariableRegistry** registration function (vtable slot `0x3C`, validated by a reference to the literal `Debug/VariableRegistry/Variable` or `Debug/VarMan`) and a **config retrieval** function `(category, key, out value)`. That is how it lists and edits live "Variable settings" such as `Throttle Lot LoD Transitions`. [verified] In the Steam exe, the literal `Debug/VariableRegistry/Variable` is at `0x00FDE22C` and is referenced from code at `0x005A175C`. [verified] Routing tunables may be exposed the same way; see section 5.4. [inferred]

### 1.5 What S3SS does not touch
Nothing in S3SS hooks routing, the A* search, the autonomy system, the script task scheduler or Mono's interpreter/JIT. Its stutter work is GC, sleep/tick pacing, waits and lot streaming. [verified by reading every patch]

---

## 2. Sims 3 Performance Patch (Nexus mod 235)

- The Nexus page returned HTTP 403 to my fetch, so I could not re-read it. Search results summarise it as a beta ASI bundle for load times, memory and stutter; the stutter-related parts are the text-lookup and type-check caches, i.e. fewer repeated script lookups, not GC or routing changes. [community]
- I found **no public source repository**: no GitHub user `Sonja92yu` exists and no repo named `Sims3Performance` was found. A search summary claims "source code also included" in the download; I could not confirm that. [community, unconfirmed]
- Because it targets script-type lookup (load time) and memory, it is mostly outside this document's scope. Its type-check cache could cut per-tick script overhead slightly. [inferred]

---

## 3. LazyDuchess's work

GitHub user `LazyDuchess` publishes most of her TS3 work as source. Relevant repos (checked 2026-10-11): [verified]

| Repo | What it is | Why it matters |
|---|---|---|
| `TS3FrameratePatch` | The Smooth Patch ASI (`TS3Patch.asi`). | Scans for `8B 44 24 04 8B 08 6A 01 51 FF` (the `SleepEx` wrapper at `0x004E1320`) and writes `B9 <ms> 90` (`mov ecx, ms; nop`) so the wrapper always sleeps `1000/TPS` ms, or `C3` for uncapped, or a 0 ms sleep for "system". It patches **only the first match**. The FPS limiter hooks `IDirect3DDevice9::Present` (vtable index 17) and busy-waits after presenting. TPS > 1000 enables `HighPrecision`. |
| `TS3ThreadMonitor` | ASI that inline-hooks `CreateThread` and `SetThreadPriority` and prints every thread ID and priority to a console. | A ready-made way to inventory TS3's threads (section 6). |
| `MonoPatcher` (0.3.0) | Runtime C# method replacement for TS3 (replaces IL before Mono compiles it). Native part hooks Mono's code generator. | Addresses are for **1.69 (Origin/EA App)**, not Steam 1.67, but its signatures name Mono internals: `ScriptHost::GetInstance` singleton, `ScriptHost::CreateMonoClass`, `InitializeScriptHost`, `mono_add_internal_call`, Mono's `GenerateCode`, two JIT-cache checks (forcing recompilation), and the `"ret: more values on stack"` print. Comment: "Mono keeps spamming 'ret: more values on stack 1' in vanilla TS3. This clogs up the console and slows the game down a bit as well." It NOPs that path. |
| `TS3Patcher` | 2021 C# exe patcher for 1.67. | Superseded by the ASI. |
| `IntelFix`, `AlderLakePatcher` | Alder Lake boot fix. | Not needed on 11th gen (docs/01). |
| `TS3-CrossEyeFix`, `TS3ShadowExtender`, `TS3-Banding-Fix`, `D3DShaderReplacer` | Visual fixes / shader replacement. | Not stutter. |

Smooth Patch internals, summarised: the game paces its simulation thread with `SleepEx(20, TRUE)` through one shared wrapper, which caps the sim at roughly 50 ticks per second and makes it wake late (15.6 ms timer granularity). Smooth Patch shortens that sleep. S3SS's Precise version instead replaces the call to `ScriptHostBase::IdleSimulationCycle` with an accurate limiter, so other sleeps keep their original durations. [verified from both sources] The Steam post quoting her Tumblr says it makes "the game's logic threads run faster". [community] Smooth Patch 2.0 (Patreon, June 2022) is a separate script mod with a `LazyDuchess.SmoothPatch.Tuning` XML resource. [community; the Patreon page did not load for me]

---

## 4. Embedded Mono and its garbage collector

| Fact | Source |
|---|---|
| The embedded runtime identifies itself as **"Mono 1.2.3.1"** (a 2007-era Mono). The string is at `0x010B4BD8`, referenced from `0x00E82161`. | [verified, TS3W.exe strings] |
| Mono is **statically linked** into TS3W.exe (paths `..\..\mono\mono\metadata\boehm-gc.c`, `gc.c`, `threads.c`) with the **Boehm–Demers–Weiser conservative GC** (`Copyright 1988,1989 Hans-J. Boehm and Alan J. Demers`). There is no `mono.dll`. | [verified] |
| The GC code contains `GC_gww_read_dirty`, the Windows `GetWriteWatch` dirty-bit support, which suggests a Boehm 7.x lineage rather than Mono 1.2's stock 6.x copy. | [verified string; version inferred] |
| Boehm's **environment variables are compiled in and read at start**: `GC_INITIAL_HEAP_SIZE`, `GC_MAXIMUM_HEAP_SIZE`, `GC_ENABLE_INCREMENTAL`, `GC_PAUSE_TIME_TARGET`, `GC_PRINT_STATS`, `GC_DUMP_REGULARLY`, `GC_DONT_GC`, `GC_FIND_LEAK`, `GC_ALL_INTERIOR_POINTERS`, `GC_LARGE_ALLOC_WARN_INTERVAL`, `GC_NO_BLACKLIST_WARNING`, `GC_PRINT_BACK_HEIGHT`. The code at `0x00E4E3DF` is `push "GC_ENABLE_INCREMENTAL"; call ebx; add esp,4; test eax,eax`, the cdecl `getenv` pattern (MSVCR80 `getenv` is imported). `GC_FREE_SPACE_DIVISOR` is **not** present. | [verified strings and call pattern; that `ebx` holds `getenv` is inferred] |
| Mono's own environment variables are present too: `MONO_LOG_LEVEL`, `MONO_LOG_MASK`, `MONO_DEBUG`, `MONO_PATH`, `MONO_CFG_DIR`, `MONO_NO_UNLOAD`. | [verified strings] |
| Garbage collection runs **on the simulation thread**: `MonoScriptHost::Simulate` calls `GC_try_to_collect` every cycle, and drains finalizers with `mono_gc_invoke_finalizers`, including the blocking "finish all finalizers" loop after 200 overrun frames. Boehm collections are stop-the-world (`GC_stop_world` suspends all registered threads). | [verified from S3SS source] |
| GC profiling by the S3SS author shows `GC_try_to_collect` "dominates simulation thread time". | [community: author's statement, no published numbers] |
| `Default.ini` → `[Script] Multithreaded=1`, `HeapSize = 30720`. An EA forum moderator says `HeapSize` is the script heap's theoretical upper limit, not RAM use, and that raising it is an "urban legend" fix. One CrossOver forum comment claims turning off script multithreading reduces hiccups on modern systems. | [verified values; meanings community] |
| The `"ret: more values on stack:  %d"` debug print exists (string `0x0108E820`, referenced at `0x00E59225`), and LazyDuchess says vanilla TS3 hits it constantly. | [verified string; frequency community] |
| EA modified Mono to support **yielding script tasks**: `"Attempting to yield in a non-yielding context!"` (referenced at `0x00E593BB`, inside the Mono code range), `CheckYieldingContext`, and internal calls `TaskControl_SaveNativeYieldObject`, `Simulator_SleepImpl`, `Simulator_WakeImpl`, `Simulator_SetYieldingDisabledImpl`, `Simulator_EnableYieldAnalysis`. Every Sim's interaction runs as a script task that yields back to the scheduler (`Simulator.Sleep`, `Simulator.Yield`). | [verified strings; mechanism inferred] |

Known script-side hitch sources (all [community], see section 7): long-running story progression actions, memories/scrapbook bloat (patch 1.22+), thousands of derelict cars, stereos left on in inactive homes, and broken relationship links.

---

## 5. Stuck Sims and routing

### 5.1 How routing is split between native code and script

**Native (TS3W.exe)** does all path planning. Allocation and debug tags in the exe show the structure: [verified strings; roles inferred from names]
- `Routing::PathPlanner` (per-world state: `mQuadTrees`, `mPortalList`, `mLotRoutingDataMap`, `mHangingFootprintMapGrid`, `mpSearch`), created by `Routing/Init-PathPlanner` and `Routing/Init-PathPlannerWorld`.
- A **quadtree** of footprints (`QuadTree`, `QuadTreeBranch`, `QuadTreeLeaf`, `QTCellRef`, each with a pool allocator).
- **A\*** search (`cAStarSearch`, `cAStarGraphNode`, `cAStarStartNode`, `cAStarGoalNode`, `OpenList/mBuckets` bucketed open list, `ClosedList`, `came_from`).
- **World-level routing** over streets and sidewalks (`WorldPlanPath/StreetRun`, `RoutingDataCT/streets`, `footpaths`, `waypoints`, `streetIntersections`), with costs from `Sims3.ini` `[Routing: World Routing Tuning]` (for example `WR LONG Street cost = 6.0`, `WR Long Distance Threshold = 80.0`).
- **Portals** (doors, stairs, elevators, pools, ladders): `PathPlan/Portals`, `PortalCheckCache`, `SetPortalCost`, `mForbiddenPortalList`.
- **Lot transitions** tuned in `Sims3.ini` `[Routing : Lot Transition Tuning]` (`Near Lot Edge`, `ShortPath …`, `Use Street Crossing Code = False`).
- `RoutingJob` (`mStartPoints`, `mGoalPoints`) and `RoutingSystemImpl` (`mLotDirtyStateMap`, `PersistentLotRoutingDataMap`, `UpdateLotWallsInPlanner`).
- Steering and dynamic avoidance (`Locomotion/AvoidanceField`, `DynamicAvoidance_*` in `Sims3.ini`).

**Script (Mono DLLs)** decides when to route and what to do on failure. The engine exposes 96 `ScriptCore.Route::Route_*` and 62 `ScriptCore.RouteManager::RouteManager_*` internal calls, for example `Route_Plan`, `Route_PlanFromPoint`, `Route_Replan`, `Route_ReplanFromPosition`, `Route_IsPointRoutable`, `Route_GetEstimatedTravelTime`, `RouteManager_IsRouteToObjectUnobstructed`, `RouteManager_AddPortal`, `RouteManager_SetPortalCost`, `World_BuildRoutingDataImpl`, `World_IsSimRoutableImpl`. [verified strings] The script wrappers are `Sims3.SimIFace.Route` and `RouteManager`; gameplay code (`RoutingComponent`, `SimRoutingComponent`, route actions such as `FollowRouteAction`) drives planning, follows the path and replans. [verified via NRaas source, which references these types]

Planning looks **synchronous from script's point of view**: NRaas registers `Route.PostPlanCallback(Route r, string routeType, string result)` and reads `r.PlanResult` immediately. So each plan's A* cost lands on whichever thread runs the script, which is the simulation thread. [inferred from the API shape; confirm in Ghidra whether `Route_Plan` hands work to `JobThread%i` workers and waits]

### 5.2 Why stuck Sims cause lag
- A Sim whose route fails is retried by the route action loop (`ContinueAndPopPathAndReplan`, `ReplanFromPoint`, see NRaas GoHere's copy of `RoutingComponent.DoRoute`). If the goal is unreachable, each retry runs a fresh plan. Off-screen Sims do this too. [verified code path in NRaas; cost inferred]
- The MTS "World Routing Lags" wiki: "The lags is due to the game taking the time to recalculate the routing information when Sims/Pets get stuck in the world due to broken routing", and players had to `resetsim *` every few hours even with Overwatch. https://modthesims.info/wiki.php?title=Game_Help:World_Routing_Lags [community]
- NRaas staff on Steam about Bridgeport: elevators are the main problem; "Sims we cannot see often get stuck trying to use them and that is horrible for game performance as they keep trying to reroute but can't". Also, NPC-only apartment units on ground floors trap residents coming out of hibernation; moving those units upstairs "helped performance a lot". https://steamcommunity.com/app/47890/discussions/0/3133918821790545121 [community]
- CAW tutorial: "One dot that has no lines connected to it can already make a world lag massively", because NPCs spawn anywhere not painted non-routable and then cannot leave. A later moderator note says visibly cut lines alone can be harmless. https://modthesims.info/t/439329 [community]

### 5.3 NRaas logic (source available)
NRaas source is public at **https://github.com/Chain-Reaction/NRaas** (community-maintained, last push 2026-07-29, 5,112 `.cs` files). [verified]

- **Overwatch `StuckCheck`** (`NRaasOverwatch/OverwatchSpace/Alarms/StuckCheck.cs`) has two parts: [verified]
  1. A **route-failure watchdog**: it hooks `Route.PostPlanCallback`. If a Sim's position has not changed and `PlanResult` failed more than `kMinimumRouteFail` (default **10**) times in a row, over more than `kRouteFailTestMinutes` (default **30** sim-minutes), it schedules a reset ("Unroutable").
  2. A **nightly alarm** (`kAlarmHour = 3`) over `LotManager.Actors`: Sims in a hidden room, or standing still in the same interaction at an unroutable point, or not moving for a whole sim-day in the same interaction, get reset ("Stationary"). It skips its own work when called from EA's vanilla `Sims3.Gameplay.Core.LotManager.RunStuckCheckHelper` / `RunStuckCheck`, so **the base game has its own stuck check**.
  3. The reset first tries `Instantiation.AttemptToPutInSafeLocation` and falls back to `ResetSimTask` when `kStuckCheckReset` is true.
- **`ResetSimTask`** (`NRaas/Tasks/ResetSimTask.cs`) resets posture chains, situations, inventory, role, and routing: `ResetRouting` frees all routing lanes on every door in `SimRoutingComponent.LockedDoorsDuringPlan` and resets those doors. A comment notes `FadeOut()` "hangs the game", so it uses `Destroy()`. [verified]
- **`CleanupElevators`** resets every `ElevatorDoors` object and every Sim assigned to its portal component each night. [verified]
- **ErrorTrap** contains about 20 "dereference" performers for routing objects (`DerefRoutingComponent`, `DerefSimRoutingComponent`, `DerefFollowRouteAction`, `DerefCarRoutingComponent`, `DerefPortalComponentAcquireLaneRouteAction`, …) that clear dangling references after objects are deleted. [verified file list]
- **GoHere** replaces `RoutingComponent.DoRoute`, `SimRoutingComponent` and `DoorPortalComponent` behaviour (door filters, costs, time locks) and adds routing options. [verified file list]
- Players can turn off Overwatch's auto-reset (Settings → Stuck Check Auto-reset) while keeping the check. [community]

### 5.4 Bad-routing worlds and how people fix them
- **ellacharmed's routing fixes** (ModTheSims, 2011–2013) for EA worlds: Sunset Valley, Bridgeport, Appaloosa Plains, Moonlight Falls, Lunar Lakes and others. They repaint Sim/camera non-routable terrain over blank spots, fix broken road intersections and disconnected roads, and ship either a full `.world` or the changed resources to import with S3PE. [community]
  - Bridgeport: https://db.modthesims.info/d/452601/bridgeport-world-with-routing-fixes-applied.html
  - Moonlight Falls: https://modthesims.info/d/486830/resources-for-moonlight-falls-world-s-various-fixes.html
- The Moonlight Falls fix lists the resource types that hold world routing: **`0x05CD4BB3` World Routing**, `0x05DA8AF6` World boundaries, `0x03D86EA4` world lot information, `0x04A4D951` World Detail, `0x4D1A5589` object instances, `0x05ED1226` Reference Store; road intersections add `0x9063660E` Roads & Walkways and `0x01661233` models. [verified on the page]
- Since patch 1.42, these fixes **only apply to new games**; existing saves keep the world's routing data they started with (move households with Porter). [community, from the wiki FAQ]
- Isla Paradiso, Bridgeport and Roaring Heights are the worlds most often named as badly routed. [community]
- The process is CAW: "rebuild routing data", repaint non-routable areas, re-export. [community]

### 5.5 Tunables to investigate
`Sims3.ini` (in `Game\Bin`) has a `[Timeslices]` section that nobody seems to have documented: [verified values; meaning unknown]

```
[Timeslices]
SimService = 10,10,25,50
JobManager = 10,10,25,50
ResourceSystem = 10,10,25,50
TextureCompositor = 10,10,25,50
```

The four numbers probably are per-frame time budgets in milliseconds per game speed or load state. `SimService` and `JobManager` are exactly the subsystems that matter for simulation lag. The string `Timeslices` is referenced at `0x00EC6DD8` and `SimService` at `0x005EBCE8` / `0x005F0D21`. No community source mentions these keys. [inferred meaning]

---

## 6. Threading

What the exe tells us (strings plus xrefs, [verified] unless marked):

| Thread / system | Evidence | Notes |
|---|---|---|
| **Main / render thread** | D3D9 present path; S3SS Precise hooks the post-present inactive-window limiter. | Renders frames; signals the sim thread in S3SS's "tick once per frame" mode. |
| **Simulation thread** | `Sims3::MonoEngine::MonoScriptHost::Simulate` (S3SS), `[Script] Multithreaded=1`, internal call `GameUtils_IsSingleThreadedMode`, `Misc/Camera/SimThreadCameraMessage`. | Runs Mono, every script task, GC collection and finalizers, and (likely) route planning. Separate from rendering when `Multithreaded=1`. [mechanism partly inferred] |
| **Job workers** `JobThread%i` | Format string at `0x00FD9870`, used at `0x00599D89`; `JobManager`, `JobRunner_Loop Begin/End` (UTF-16, used at `0x00893FD4`), `JobManager/DepsToCancel`. | A native job pool with dependencies. Which work goes here (routing? culling? compositing?) is the key open question. |
| Resource streaming | `ResourceSystem/AsyncLoader/JobsToCancel`, `Timeslices/ResourceSystem`. | Async loads. |
| Render culling | `Multithreaded Cull` (UTF-16 setting, used at `0x006EFDA1`, `0x006F1913`). | An engine option for threaded culling. Current value unknown. |
| Script-side threaded objects | `Scripting/MonoScriptHost/ThreadedObjects` (used at `0x00D7E3A0`). | Objects shared with other threads. |
| Cross-thread calls | `Sims3::Utility::PostRemoteMethodCall` (S3SS), `Physics/PostMessageToSimulator/*`. | Engine message passing between threads. |
| File watchers | S3SS: "several dozen" threads created with 1 MB stacks "for watching file changes". | Address-space waste, not CPU. |
| Audio | RWAudio "Dac thread" strings. | Realtime audio mixing. |
| Video | `MoviePlayer2 Decode Thread`. | Intro and TV videos. |
| Online (dead services) | `BlazeWal*Thread`, `Crossroads*Thread`, `SIGS/ThreadJobManager/mpThreadPool`, `IGACacheRefresherThread`, `LoginJobThread`, `NamedPipeListenerThread`. | Mostly idle; S3SS's Online Nuke stops one download. |

- The engine has built-in profiling hooks reachable from script: `GameUtils_DumpProfilerReport`, `GameUtils_DumpProfilerScopeAverages`, `GameUtils_AddOrConfigureProfiler`, `GameUtils_DumpMemoryReport`, `Simulator_DoCPPProfile`, `ProfilerUtils_BeginEventImpl/EndEventImpl`. Whether they do anything in the retail build is unknown. [verified strings; function unknown]
- `GameUtils_WaitForFrameEvaluation` suggests script can block until the renderer finishes a frame. [inferred]
- I found **no published community analysis** of main-thread versus worker-thread time in TS3. The closest is LazyDuchess's `TS3ThreadMonitor` tool (no published results) and S3SS's GC profiling remark. [verified absence in searched sources]
- Known lag-spike sources reported by players: story progression (EA's and NRaas'), autonomy pushes in big towns, ghost or invisible Sims from broken relationship links, too many objects or Sims and pets, derelict cars (Overwatch reports thousands removed nightly), stereos left on, and the memories system from patch 1.22. [community; MTS wiki "Intermittent Freezes": https://modthesims.info/wiki.php?title=Game_Help:Intermittent_Freezes]

---

## 7. Other well-known stutter fixes

- **Simulation lag versus FPS stutter.** The MTS wiki defines freezes as "Sims being frozen on certain actions and yet the background is moving, or sound continues playing". That is the simulation thread falling behind while rendering continues. FPS stutter is the render thread hitching (shader compiles, texture uploads, lot streaming). They need different fixes. [community]
- **`resetsim *` cheat**: clears stuck Sims temporarily. Players report it relieves rerouting load but does not fix the cause. [community]
- **NRaas Overwatch** settings that matter for stutter: Stuck Check, Stuck Check Auto-reset, Reset Check, Cleanup Elevators, Cleanup Vehicles, Turn Off Stereos/Televisions, nightly alarm hour. Defaults are in `PersistedSettings.cs` (section 5.3). [verified defaults]
- **Story progression**: NRaas StoryProgression or EA's own; heavy settings and uncapped population add sim-thread load. Only one story-progression system should run. [community]
- **Memories**: turn off or limit the memories system (Generations-era bloat). [community]
- **Custom content**: too many, broken or patch-incompatible `.package` files cause lag; merge packages. [community]
- **"NoBugs"/Simlogical**: I found no stutter-specific fix under those names in this search. [unverified]
- **Error 12** links to stutter only indirectly: when the address space is nearly full, Boehm collects more often and allocations slow down. [inferred]

---

## 8. Symbols, databases and the macOS 64-bit build

- **No public Ghidra or IDA database, symbol map or RTTI dump** for TS3W.exe turned up on GitHub or in web searches. [verified absence in the searches run]
- **RTTI is stripped**: only two type descriptors (`type_info` and one `EA` type) are in the exe. [verified]
- **Free substitutes for symbols:**
  1. **About 1,780 Mono internal-call strings** of the form `ScriptCore.<Class>::<Class>_<Method>`, each registered with a native function pointer through `mono_add_internal_call`. Find the registration loop and you get a name for every script-to-engine entry point, including all 96 `Route_*` and 62 `RouteManager_*` calls. [verified strings; mechanism verified in MonoPatcher]
  2. **Thousands of allocation and debug tags** like `Routing/PathPlanner/mQuadTrees` or `Lot/GetObjectsIntersectingObjectFootprint/objSet`, each passed by the function that allocates. [verified]
  3. **S3SS's and LazyDuchess's names** (sections 1 and 3).
  4. Mono and Boehm are open source: compiling Mono 1.2.3.1 and matching it with Ghidra function IDs (or BinDiff) would name most of the runtime. [inferred]
- **macOS 64-bit build (2020)**: EA's help page confirms a native 64-bit Metal build (not a remaster). I found no report that it ships symbols or that anyone has used it for symbol recovery. It is still worth checking its Mach-O for unstripped symbols or Objective-C/C++ type names, since Mac builds often keep more. https://help.ea.com/en/articles/the-sims/the-sims-3/mac-64-bit-version [verified page; symbol question open]

---

## 9. The DirectX path, other wrappers, and `ddraw.dll`

- TS3 renders with **Direct3D 9**. [verified: D3D9 hooks in S3SS and TS3FrameratePatch]
- **DXVK** is covered in docs/01. On Linux, a Bottles guide recommends `dxvk-gplasync` with `dxvk.enableAsync = true` to reduce shader-compile stutter. That fork exists for Windows too, but async shader compilation can cause brief visual pop-in. [community]
- **dgVoodoo2** can translate D3D9 to D3D11 or D3D12. I found no TS3-specific reports. Its developer confirmed D3D12-backend stutter in another game caused by OS scheduler behaviour. [community; no TS3 data]
- **D3D9On12** (Microsoft's D3D9-on-D3D12 mapping layer) has no TS3-specific reports either. [no data]
- Neither wrapper moves managed texture memory out of the 32-bit process the way DXVK does, so DXVK stays the pick for Error 12. [inferred]
- **`ddraw.dll` in `Game\Bin` is not stock.** On this install it is Ultimate ASI Loader (ThirteenAG) and loads `TS3Patch.asi` and `D3DShaderReplacer.asi` (section 0). TS3 does not need DirectDraw itself. [verified]

---

## Named anchors for Ghidra

All addresses are for the Steam build 1.67.2.024037 (`TS3W.exe`, image base `0x00400000`). "xref" means a code location that pushes the string's address.

| Name | What it is | How to find it |
|---|---|---|
| `MonoScriptHost::Simulate` | Main simulation loop; runs script, GC and finalizers | Contains the `GC_try_to_collect` call at **0x00D819AA**, finalizer threshold at 0x00D81A1D, finalizer loop at 0x00D81A37, `IdleSimulationCycle` call at **0x00D81FDE** |
| `GC_try_to_collect` | Boehm explicit collection | Target of the `E8` call at 0x00D819AA |
| `mono_gc_invoke_finalizers` | Drains the finalizer queue on the sim thread | Target of the `E8` call just before 0x00D81A37 (pattern `E8 ?? ?? ?? ?? 85 C0 75 F7 89 1D`) |
| `sFramesSinceFinalizeFinished` | Counter that triggers the blocking finalizer loop at 200 | `A3 imm32` operand in pattern `03 C5 3D C8 00 00 00 A3 ?? ?? ?? ?? 75` |
| `GC_stop_world` | Boehm thread suspension | Contains `3D 00 01 00 00 7C` at **0x00E511F5**; string `GC_stop_world() called before GC_thr_init()` |
| Boehm `GC_init` env reads | Startup GC tuning | xrefs to `GC_PAUSE_TIME_TARGET` (0x00E4E21D), `GC_ENABLE_INCREMENTAL` (0x00E4E3DF), and the other `GC_*` strings |
| `ScriptHostBase::IdleSimulationCycle` | Sim tick pacing | Called at 0x00D81FDE; script host fields `+0xA60` (`mbIdlingEnabled`), `+0xC08` (running flag) |
| `SleepEx` wrapper | Shared `SleepEx(*arg, TRUE)` helper patched by Smooth Patch | **0x004E1320**, pattern `8B 44 24 04 8B 08 6A 01 51 FF 15`; IAT `0x00F951F0` = `KERNEL32!SleepEx` |
| Inactive-window frame limiter | Post-present ~30 FPS cap | **0x00EC9FBA**, pattern `80 BE 8D 00 00 00 00 5E 74 15 …` |
| `_beginthreadex` call site | File-watcher thread creation (1 MB stacks) | **0x004DB355** (`FF 15` at pattern +11) |
| `SetThreadIdealProcessor` callers | Hard-coded thread placement | xrefs to the `SetThreadIdealProcessor` import |
| `JobManager` / `JobThread%i` | Native worker pool | String 0x00FD9870, xref **0x00599D89**; `JobRunner_Loop Begin` (UTF-16) xref 0x00893FD4 |
| `[Timeslices]` reader | Per-subsystem time budgets | `Timeslices` (UTF-16) xref **0x00EC6DD8**; `SimService` xrefs 0x005EBCE8, 0x005F0D21 |
| `[Script] Multithreaded` reader | Sim-thread on/off | `Multithreaded` (UTF-16) xref 0x00EC9C77 |
| `Multithreaded Cull` | Render culling option | xrefs 0x006EFDA1, 0x006F1913 |
| `Routing::PathPlanner` ctor / world init | Routing system setup | `Routing/PathPlanner` xref 0x0062E963; `Routing/Init-PathPlannerWorld` xrefs 0x00829987, 0x0082A895, 0x0083504C |
| `RoutingJob` | Plan request (start/goal sets) | `Routing/RoutingJob/goalPts` xref 0x0081B683 |
| A* search | Core pathfinding | Allocation tags `Routing/AStarSearch*`, `Routing/cAStarGraphNode/Allocator`, `Routing/OpenList/mBuckets`, `Routing/PlanPath/temp` |
| World street routing | Street/sidewalk planner | Tags `Routing/WorldPlanPath/StreetRun`, `Routing/RoutingDataCT/*`; reads `[Routing: World Routing Tuning]` keys such as `WR LONG Street cost` |
| `Route_Plan` and the other route internal calls | Script → native entry points | String `ScriptCore.Route::Route_Plan` passed to `mono_add_internal_call`; the pointer pushed next to it is the native function |
| `mono_add_internal_call` | Registration of all 1,780 internal calls | Called with every `ScriptCore.*::*` string; MonoPatcher signature (1.69) as a starting point |
| Mono yield check | EA's script-task yielding | `Attempting to yield in a non-yielding context!` xref 0x00E593BB |
| `"ret: more values on stack"` print | Noisy Mono debug path | String 0x0108E820, xref **0x00E59225** |
| Mono version | Runtime identification | `Mono 1.2.3.1` xref 0x00E82161 |
| `Lot::AddLotObjectsToScene` | Lot object streaming | **0x00AC1130** |
| `WorldManager::Update` | World/lot streaming update | **0x00C6D570**; `+0x258` skip-streaming flag |
| `PostRemoteMethodCall` | Cross-thread method calls | 0x00ABE9C0, 0x00ABEA50, 0x00B35DB0 (pattern in section 1.1) |
| VariableRegistry register function | Engine live tunables | vtable slot `0x3C`; `Debug/VariableRegistry/Variable` xref 0x005A175C |
| RefPack decompressor | Resource decompression | **0x004EB3B0** |
| `MSVCR80` allocator imports | CRT heap | `malloc`, `free`, `_aligned_malloc` in the import table |

---

## Open questions for reverse engineering

1. **Is route planning synchronous on the sim thread?** Follow `Route_Plan` from its internal-call registration into native code. Does it build a `RoutingJob` and hand it to `JobThread%i`, then block, or run A* inline? If inline, moving planning to workers (with the script task yielding until the result is ready) is our biggest possible stuck-Sim win.
2. **What does `[Timeslices]` control?** Decode the reader at 0x00EC6DD8 and the `SimService` users. If the values are per-frame budgets, we can measure stutter against different settings with no patch at all.
3. **How expensive is a failing plan?** Profile `Route_Plan`/A* with x32dbg or Frida in a save with known stuck Sims (Bridgeport elevators) against a clean save. Count plans per second and time per plan.
4. **Can we cache or rate-limit repeated failures natively?** For example, remember (Sim, goal, routing-data version) → failure for N sim-minutes so the script loop does not replan every tick. Compare with doing it in script via MonoPatcher or a `PostPlanCallback`-style hook.
5. **GC behaviour.** Launch once with `GC_PRINT_STATS=1` (stderr redirected) to log collection counts and pause times. Test `GC_INITIAL_HEAP_SIZE` (fewer early collections). Check whether incremental mode (`GC_ENABLE_INCREMENTAL`, `GC_PAUSE_TIME_TARGET`) is safe with this Mono and EA's task stack switching before trying it.
6. **Which Boehm version is linked?** Match functions against Boehm 6.8 and 7.x sources to know which tuning knobs and parallel-mark options exist.
7. **What runs on `JobThread%i` today, and how many workers are there?** Use LazyDuchess's `TS3ThreadMonitor` approach (hook `CreateThread`/`SetThreadPriority`) plus per-thread CPU sampling.
8. **What does `[Script] Multithreaded=0` change?** Find the reader at 0x00EC9C77. It may confirm the sim/render split and explain the community claim that turning it off reduces hiccups.
9. **Is the vanilla `LotManager.RunStuckCheck` effective?** Decompile `Sims3GameplaySystems.dll` (from the game's script packages) to compare EA's stuck check with Overwatch's.
10. **Does `Multithreaded Cull` default to on?** Check the setting registry and measure its effect.
11. **Does the "ret: more values on stack" path still fire on Steam 1.67, and how often?** Port MonoPatcher's NOP to the Steam xref at 0x00E59225 and measure.
12. **Do the engine's profiler internal calls (`DumpProfilerReport`, `DoCPPProfile`) still work in retail?** If yes, they give named scope timings for free.
13. **Does the macOS 64-bit build keep symbols?** If so, use it to name routing and job-system functions, then port the names by BinDiff.
14. **Docs numbering:** PLAN.md reserves `docs/03-baseline-results.md`; this file uses `03` as requested. Rename one of them when the baseline results arrive.

---

## Sources
- S3SS source: https://github.com/sims3fiend/Sims3SettingsSetter (commit 5eb2c65)
- LazyDuchess repos: https://github.com/LazyDuchess/TS3FrameratePatch, https://github.com/LazyDuchess/TS3ThreadMonitor, https://github.com/LazyDuchess/MonoPatcher, https://github.com/LazyDuchess/TS3Patcher
- NRaas source: https://github.com/Chain-Reaction/NRaas (`NRaasOverwatch/OverwatchSpace/Alarms/StuckCheck.cs`, `PersistedSettings.cs`, `NRaas/Tasks/ResetSimTask.cs`, `NRaasOverwatch/OverwatchSpace/Alarms/CleanupElevators.cs`, `NRaasGoHere/GoHereSpace/Helpers/RoutingComponentEx.cs`)
- MTS wiki, World Routing Lags: https://modthesims.info/wiki.php?title=Game_Help:World_Routing_Lags
- MTS wiki, Intermittent Freezes: https://modthesims.info/wiki.php?title=Game_Help:Intermittent_Freezes
- CAW routing tutorial: https://modthesims.info/t/439329
- ellacharmed world fixes: https://db.modthesims.info/d/452601/bridgeport-world-with-routing-fixes-applied.html, https://modthesims.info/d/486830/resources-for-moonlight-falls-world-s-various-fixes.html, https://ellacharmed.wordpress.com/2020/08/21/how-to-fix-worlds-in-the-sims3/
- Bridgeport elevator lag (NRaas staff): https://steamcommunity.com/app/47890/discussions/0/3133918821790545121
- Sims 3 Performance Patch: https://www.nexusmods.com/thesims3/mods/235 (403 to automated fetch)
- EA Mac 64-bit page: https://help.ea.com/en/articles/the-sims/the-sims-3/mac-64-bit-version
- dgVoodoo D3D12 stutter (other game): https://www.vogons.org/viewtopic.php?p=1058838
- Local files (read-only): `TS3W.exe` strings and bytes, `Default.ini`, `Sims3.ini`, `TS3Patch.txt`, `D3DShaderReplacer.cfg`, `skuversion.txt`, DLL version resources.
