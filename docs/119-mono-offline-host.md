# 119. Mono offline host: startup map, IC surface and feasibility (M1)

Step M1 of the Mono offload plan (docs/118: M0 = embedding-API glue map, M1 = this note, docs/120 = GC multi-thread verification). Question: what would a standalone host process, with no TS3W.exe game running, need in order to load the game's script assemblies in the same embedded Mono 1.2.3 runtime and run sim code paths, first the autonomy scoring entries of docs/75, for tests and record/replay?

Static analysis only: Ghidra headless on a copy project (`game-copy/ghidra-mono-host`, copied from `game-copy/ghidra`), Steam `TS3W.exe` 1.67, image base `0x400000`; `tools/mono-host-ics.ts` (new) over the `managed-callgraph.ts` index; a read-only listing of the installed `Game` folder. The game was not run. Claim ids I-119-* are in docs/51 §143.

Tags: **[verified]** = read in the disassembly or decompiler output, an xref or string scan, or tool output, for this note; **[inferred]** = Mono 1.2.3 source knowledge or reasoning, not confirmed instruction by instruction.

## Summary

- **Startup is one host method, `MonoScriptHost_Init` `0xD7E560`** (vtable `vt_MonoScriptHost` `0x108B690`, slot 4) [verified: only a data reference at `0x108B6A0`; it calls `mono_interp_init` at `0xD7E60C`]. docs/29 §4 said `0xD7E540`, vtable `0x108B818`, slot 0; that is a scalar deleting destructor. Corrected in docs/29.
- **Every assembly, mscorlib included, comes from S3SA package resources, never from files.** Mono's `mono_image_open_full` `0xE84140` has one EA change: on an image-cache miss it calls EA's `0xD6FCB0` instead of opening the file. `0xD6FCB0` turns the path into a resource key and opens the S3SA resource through the resource manager and `mono_image_open_from_data` `0xE84860` (copy = 1) [verified calls; key derivation inferred]. The Steam install has no `lib\mono`, no `etc\mono` and no `mscorlib.dll` file [verified listing]. No EA assembly hook is installed (stock preload, search, postload and load hooks only) and the bundled-assembly table is never written [verified].
- **Mono's allocator is EA's heap.** eglib `g_malloc` `0xD70A10` (102 call sites), `g_malloc0` `0xD70A30` (163) and `g_free` `0xD70A60` (656) call EA memory manager `0x4E69D0` / `0x4E51B0` [verified]. A host that maps TS3W.exe must start EA's heap first, or redirect these three functions.
- **IC surface:** 1,758 EA bindings (docs/62) + 2 registered by `0xD7E560` itself + 4 by `mono_interp_init` + the stock Mono tables for mscorlib (329 runtime ICs referenced by mscorlib, System and System.Xml, docs/78). Offline class of the 1,758 EA bindings, by name rule plus the docs/62 verdict: **reads-world 653, static-data 403, writes-world 332, engine-only 265, managed-reentry 69, pure 36** (§2.2).
- **The autonomy closure is small.** From the 12 docs/75 top-20 rows that resolve to methods (21 roots; their `share_of_scoring` values add to 0.81, with overlap, since base-path rows sit under definition rows), depth 12 reaches 2,743 methods and 108 IC bindings: 43 pure (all Mono runtime), 27 reads-world, 20 static-data, 12 engine-only, 5 managed-reentry, 1 writes-world. Depth 20: 5,985 methods, 309 bindings. Per-IC table: `docs/data/mono-host-ics.csv` [verified: tool output].
- **Static constructors are mostly safe.** 45 of 9,840 game-assembly `.cctor`s reach an EA IC within 6 calls; almost all through `Localization.LocalizeString` (static-data), and only `Sims3.UI.UIManager` reaches engine init (`UIManager_InitUIHierarchyImpl`, `SetUICallbacks`) [verified: tool output, static closure].
- **Verdict: feasible as "TS3W.exe as a library"** (map the exe into a 32-bit host at its own base `0x400000` and call its Mono by address), not as a rebuilt stock Mono. Effort: **M1 host ~6-12 h**, **M2 stubs for the autonomy set ~4-8 h** (§4); 0 game runs. The big risk is not the runtime but the managed world: an offline host has no Sims, lots or objects until M3 can build or restore them.

## 1. Startup sequence in TS3W.exe

### 1.1 Host object

| Step | Address | What | Tag |
|---|---|---|---|
| Factory | `0x76B610` -> `0xD7F2B0` | creates the script host; `0xD7F2B0` calls the ctor | verified xrefs |
| Ctor | `MonoScriptHost_ctor` `0xD7E010` | 0x17C8-byte host (docs/18, `[0x11EE514]`); allocates the image map (`Scripting/MonoScriptHost/Images`), method cache, enum cache, task maps, object maps (string names) | verified strings |
| Services | `0xD77710` (no direct callers: table or vtable entry) | first `0xD75970` (builds `SignedAssemblyKeyHashSet`, 10 keys, plus `LoadImage` strings), then creates `TaskControlHost` `0xD839F0`, `SerializerHost` `0xD831C0`, `ReflectionHost` `0xD82B70` | verified calls and strings; role of the key set inferred (allowed assembly keys) |
| Init | `MonoScriptHost_Init` `0xD7E560` | §1.2 | verified |

### 1.2 `MonoScriptHost_Init` `0xD7E560`, in order

Pointer convention: Init's `this` is host `+8` (it passes `this-8` to callbacks), so offsets below are from the host start.

1. Debug variables: `Scripting` group and three commands (`Dump Mono task list`, `Enable task profiling`, `Scan for new scripts`) through DebugVarMan `0x5A00A0` [verified strings]. Not needed offline.
2. `GC_enable_incremental` `0xE4E450` [verified call; docs/13].
3. **`mono_interp_init("MonoEngine")` `0xE5A680`**, result = root domain at host `+0xACC`; Init returns 0 if it is NULL [verified]. Inside:
   1. `g_set_prgname` `0xE46FF0`.
   2. **Directories** `0xE66B00`: exe path from `GetModuleFileNameW`, two `dirname`s (`Game\Bin` -> `Game`), assembly root `<Game>\lib` (`[0x11F421C]`) and config dir `<Game>\lib\..\etc` via `mono_set_config_dir` `0xE9FE90`, which prefers `getenv("MONO_CFG_DIR")` [verified strings `\`, `lib`, `..`, `etc` and the getenv]. Neither directory exists in the Steam install [verified].
   3. Trace flags, TLS slot `[0x11F4160]` (`TlsAlloc`), the RuntimeMethod section `0x11F4130` and hash `[0x11F4164]`, interpreter callbacks into `object.c` / `marshal.c` (`0xEA09F0`, `0xEA1470`, `0xEA09D0`, `0xEA09B0`, `0xEA18D0`, `0xE84FD0`, `0xE64B80`) [verified calls; roles per docs/29, docs/58].
   4. **`mono_init_internal`** `0xE76910` -> `0xE75BF0` (`domain.c`): runtime version (the "runtime version supported by this application is unavailable" fallback path exists), **corlib** through `mono_assembly_load_corlib` `0xE68E10` ("mscorlib.dll" in the assemblies path, then `<root>\mono\<version>\mscorlib.dll`), then `mono_defaults` class lookups with asserts [verified strings and calls]. The file path is never read from disk: see §1.3.
   5. `mono_icall_init` `0xE823F0`, then 4 interpreter ICs (`StackFrame::get_frame_info`, `StackTrace::get_trace`, `Mono.Runtime::mono_runtime_install_handlers`, `Delegate::CreateDelegate_internal`) and the JIT icall `mono_thread_interruption_checkpoint` [verified strings].
   6. **`mono_runtime_init`** `0xE66240`: GC base init `0xE4E4C0` (docs/29: `GC_init`, registers the calling thread), stock assembly hooks: preload `0xE65A70` (normal and refonly, `0xE67380` / `0xE673D0`), search `0xE65AE0` (`0xE671D0` / `0xE67230`), postload search `0xE651E0` (`0xE67290` / `0xE672F0`), load `0xE65350` (`0xE67140`); `AppDomainSetup` / `AppDomain` objects, the cctor tables `0xEA07D0`, preallocated OutOfMemory / NullReference / StackOverflow exceptions, GCHandle roots `0xE78DA0` [verified]. Each hook installer has exactly one caller, `0xE66240`: EA adds no hook [verified xrefs].
4. `[0x1234360]` set; a 0x1A0-byte block at host `+0xB84` [verified; role open].
5. **ICs:** `ScriptCore.Simulator::MonoScriptHost_GetEnumSimpleName` and `...Hash` directly, then for each **service provider** in the list at host `+0x1608` (the services of §1.1 and the subsystem hosts), provider vfunc `+0x10` returns a zero-terminated `{function, name}` table that goes to `mono_add_internal_call` `0xE82550` [verified loop]. `0xE82550` has 7 call sites: 3 here, 4 in `mono_interp_init` [verified].
6. **Assemblies** `0xD7DD10`: the resource manager (`0x4AFD20`, vfunc `+0x20`) lists keys of type **`0x073FAA07` (S3SA)**; two passes over the keys with a per-key check (vfunc `+0x44`, then `+0x0C`) decide the order [verified; meaning of the checks inferred: override or mod keys]. Each key goes to `0xD7C5A0`: look up the host image map (`+0xAD0`), else `0xD78760` opens the resource and calls `mono_image_open_from_data` `0xE84860`; if the image has no assembly yet, `mono_assembly_load_from_full` `0xE68290`. When the image named `SimIFace` loads, `Sims3.SimIFace.Simulator` is cached in `[0x1220F34]` [verified `_stricmp` and lookup]. Then `0xD79AF0` builds an `Assembly[]` and invokes the method at host `+0x1628` (`Simulator.SignalAssembliesLoaded`) if bound; on the first Init it is bound only after this step, so that call is for `Scan for new scripts` reloads [verified order; effect inferred].
7. **Managed method handles** via `0xD7C660` (namespace, class, method; cached in the host method map): `ITask` `get_ExecuteType`, `set_ObjectId`, `Simulate`, `Stop`; `Simulator.SignalAssembliesLoaded`; `LoadSaveManager` `BindLoadedObject`, `DisposeLoadedObject`, `LoadObjectStack`, `SaveObject`, `SaveObjectStack`; `Simulator` `RefObj`, `DerefObj`; `ScriptProxy` `get_Target`, `Load`, `PostLoad`, `CreateLogic`; `IDisposable.Dispose` (host `+0x1618..+0x1658`) [verified strings]. Class `ScriptCore.ScriptProxy` is required: Init fails without it [verified].
8. **Static fields the native side touches directly:** the address of `ScriptCore.Simulator.sSimTickCnt` and of `Sims3.Gameplay.Utilities.SimClock.sSimTickCnt` (class vtable static data + field offset) go to host **`+0xC20`** / **`+0xC24`** (stores `0xD7E93D` / `0xD7E98D`; the earlier `+0xC28`/`+0xC2C` was off by 8: the ctor zeroes those two separately at `0xD7E28E`) [verified address arithmetic]. The only writer through them is `MonoScriptHost_Simulate` `0xD81840`: one 64-bit store to each per call (`0xD81D33`-`0xD81D4B`, value from `0xD70730`); the only other users are the ctor init `0xD7E26B` and a getter `0xD71C20` (vtable `0x108B844`) [verified 2026-10-20: linear sweep of `.text` for non-stack `+0xC20`/`+0xC24` operands, 7 sites; I-119-8]. A host that runs scripts without `Simulate` must write both ticks itself.
9. For each provider: vfunc `+0x0C` may name a ScriptCore class, which `0xD7DB80` instantiates (`mono_object_new` `0xEA36F0` plus its `.ctor()` through `mono_runtime_invoke` `0xEA1090`): managed service objects are constructed during Init [verified calls]; then provider vfunc `+0x04` [verified call; role open].
10. A conditional subscription (`0x7D99D0` vs `0x7D99F0`, `0x599160` vfunc `+0x14` with `0x80000004`) [verified; role open].

### 1.3 Where images come from

| Path | Behaviour | Tag |
|---|---|---|
| `mono_assembly_open_full` `0xE68590` | stock: bundled-assembly check (`[0x1220FD4]`, `0xE67560`), then `mono_image_open_full` | verified |
| bundled assemblies `[0x1220FD4]` | 3 reads (`0xE67560`, `0xE68590`), **no writer**: always empty | verified xrefs |
| `mono_image_open_full` `0xE84140` | image hash by canonical name under `0x11F4380`; on a miss calls **EA `0xD6FCB0`** (also from `0xE84280`, the refonly twin) | verified |
| EA `0xD6FCB0` | splits the path (`0x4F42B0`, `0x4ED980`), hashes the file name (`0x7CFE40` -> `0x4AEB90`), then `0xD79C60` on the global host: image map lookup, else `0xD78760` (resource manager open, `mono_image_open_from_data` `0xE84860`) | verified calls; key = name hash, inferred |
| `machine.config` | Mono's config code calls EA `0xD6FC40` (one caller, `0xE77FDB`), which builds the path with EA's path service | verified |
| S3SA resources | only in `Game\Bin` packages; exactly the 10 assemblies in `game-copy/assemblies` (mscorlib, System, System.Xml, SimIFace, ScriptCore, Sims3Metadata, UI, Sims3GameplaySystems, Sims3GameplayObjects, Sims3StoreObjects) | docs/17, docs/104 |
| Domain | one root domain `"MonoEngine"`; no second domain created by EA code | verified for Init; other creators not searched |

So the only image-loading glue an offline host must replace is `0xD6FCB0` (name -> bytes); everything above it is stock Mono.

### 1.4 What the host must provide (M1 checklist)

| Need | Game | Offline host |
|---|---|---|
| Runtime code | statically linked in TS3W.exe (`0xE45000`-`0xEC0000` range, docs/29) | map TS3W.exe (§3.2) |
| CRT and EA heap | process start-up | run the exe's CRT init pieces and EA heap init, or redirect `0xD70A10` / `0xD70A30` / `0xD70A60` (plus any other EA glue docs/118 lists) to the host's heap |
| Image open | `0xD6FCB0` -> S3SA | detour `0xD6FCB0` to a name -> file map over `game-copy/assemblies` (`tools/extract-assemblies.ts`) |
| `machine.config` path | `0xD6FC40` | stub (empty or a host path) |
| Runtime init | `mono_interp_init` `0xE5A680` | call it directly |
| ICs | providers | `mono_add_internal_call` `0xE82550` with M2 stubs for all 1,760 names (default: fail loudly) |
| Assemblies | `0xD7DD10` | `mono_image_open_from_data` + `mono_assembly_load_from_full` per file, or `mono_assembly_load` by name through the detour |
| Host statics | `sSimTickCnt` pointers, service objects | write the tick ourselves; construct only the services the tested code calls |
| Invoke | ITask / `mono_runtime_invoke` `0xEA1090` | `mono_runtime_invoke` on the test entry |

## 2. Internal-call surface

### 2.1 Registered ICs

| Source | Count | Tag |
|---|---|---|
| EA binding table (providers) | 1,758 (1,729 + 29 SACS, docs/62) | docs/62 |
| `MonoScriptHost_Init` direct | 2 (`GetEnumSimpleName`, `GetEnumSimpleHash`) | verified |
| `mono_interp_init` | 4 + 1 JIT icall | verified |
| Mono static tables (`0x10B4C68`, `0x10B4D28`, docs/58) | stock 1.2.3 corlib icalls; 329 referenced by the game's mscorlib, System, System.Xml (docs/78) | docs/78 |

Some EA natives are declared twice in managed code (`ScriptCore.UTFConnectorSimulator`, `ScriptCore.UTFConnectorUIManager`, `Sims3.UI.LocalizedStringService` next to the `ScriptCore` types), so binding counts in the CSV can list one native twice; the CSV matches them to the `ScriptCore` row by method name [verified: tool join].

### 2.2 Offline classes (whole EA table)

Classes, for a host with no game running:

| Class | Meaning | Host action |
|---|---|---|
| `pure` | Mono runtime IC, or EA SAFE with no sim state | runtime has it; EA ones reimplement trivially |
| `static-data` | install or session data: localized strings, `EAText` number/money/time formatting, world name and type, product and pack queries, CAS outfit data, `DeviceConfig` reads, XML resources | record once per session (or read the packages) |
| `reads-world` | reads sim or world state (objects, lots, rooms, routing, queries, RNG, time) | replay from a recording (M3) |
| `writes-world` | changes sim or engine state (verb `Set/Add/Remove/Start/Stop/Create/Destroy/Load/...`, or a queued void command) | stub that records the call; fail if a test expects an effect |
| `engine-only` | UI, camera, audio, visual effects, thumbnails, video, debug draw; `DeviceConfig` writes | no-op or fixed value |
| `managed-reentry` | docs/62 UNSAFE: yields, running task id, synchronous managed callbacks (`World_FireEvent`, object registration) | the host's own scheduler shim |

Counts over the 1,758 EA bindings (`node tools/mono-host-ics.ts --all`) [verified: tool output; class by rule, not by hand]:

| Subsystem (IC class) | ICs | Offline classes |
|---|---|---|
| World | 372 | reads 208, writes 132, reentry 27, pure 3, engine 2 |
| CASUtils | 205 | static 195, reentry 7, engine 3 |
| SocialFeatures | 205 | reads 185, pure 12, writes 8 |
| UIManager | 110 | engine 110 |
| Route | 96 | writes 48, reads 47, reentry 1 |
| GameUtils | 86 | static 79, reentry 7 |
| UserToolUtils | 66 | static 62, reentry 4 |
| RouteManager | 62 | writes 44, reads 15, reentry 2, pure 1 |
| DownloadContent | 61 | reads 49, writes 7, pure 3, reentry 1, engine 1 |
| CameraController | 44 | engine 44 |
| OnlineFeatures | 39 | static 38, reentry 1 |
| ThumbnailManager | 39 | engine 39 |
| LookAtTask | 32 | writes 18, reads 14 |
| SACS | 29 | reads 17, writes 10, reentry 2 |
| Simulator | 29 | writes 12, reads 10, reentry 5, pure 1, static 1 |
| StreamHost | 27 | reads 23, writes 3, pure 1 |
| Audio | 25 | engine 25 |
| Animation | 22 | writes 17, reads 5 |
| DeviceConfig | 22 | static 16, engine 3, others 3 |
| Objects | 21 | reads 12, writes 6, reentry 3 |
| VisualEffect | 20 | engine 20 |
| 23 smaller classes | 146 | mixed |
| **Total** | **1,758** | reads 653, static 403, writes 332, engine 265, reentry 69, pure 36 |

The rule reads docs/62's verdict for `engine-only` (MAIN) and `managed-reentry` (UNSAFE), and the method name for the rest, because docs/62's `writes` column also lists EA allocator and string-buffer globals (for example `EAText_GetMoneyString` "writes" `0x11CB920`), which are not world state [verified rows]. Hand-checking each autonomy-set IC is M2's first step.

### 2.3 The autonomy closure

Roots (`tools/mono-host-ics.ts`, from `docs/data/autonomy-entry-list.csv` ranks 1-20): base paths `Autonomy.CheckAvailability`, `InteractionDefinitionUtilities.CommonTests` / `SpecialCaseTests`, `Autonomy.CalculateScoreForObjectInteraction`, `GameObject.get_Parent`, `Autonomy.CalculateScoreAndAddToCandidates` (C2 self); definitions `MetaInteractionScoredByVenue`3` (plus `Lot` / `RabbitHole.HowMuchItWantsSimToCome`), `VisitCommunityLot`, `VisitLot`, `GoToGarden`, `Sim+CallPet` (own `Test` and `CalculateScore`). Rows 5 (316 unresolved candidates) and the 7 definitions with neither own `Test` nor `CalculateScore` add no roots; the tool does not walk base classes for them (shortcut, see the script). Edges as `managed-callgraph.ts closure` (call, resolved callvirt, declared virtual body, small interface sets, newobj; no delegates).

| Depth | Methods | IC bindings | pure | static-data | reads-world | engine-only | managed-reentry | writes-world |
|---|---|---|---|---|---|---|---|---|
| 12 | 2,743 | 108 | 43 | 20 | 27 | 12 | 5 | 1 |
| 20 | 5,985 | 309 | 65 | 72 | 57 | 68 | 10 | 37 |

[verified: tool output; `min_depth` per row in the CSV]. Depth-12 highlights:

- **reads-world (27):** object transform (`Objects_GetPosition`, `GetForward`, `GetTransform`), slots (`Slots_GetParentIdImpl`, `GetPositionOfSlotImpl`), queries (`Query_GetObjects`, `GetObjectsOnLot`, `CountObjectsOnLot`), world (`World_GetLotLocationImpl`, `GetRoomIdAtPosImpl`, `IsPositionOutsideImpl`, `HasAnyRoofOrFloorAbovePosImpl`, `GetDoorRoomsImpl`, `ObjectGetHiddenFlags`, lot names, sunrise/sunset, moon phase, sea level, `IsObjectOnScreenImpl`), `Simulator_GetTaskImpl`, `Simulator_GetTickCountImpl`, `Random_NextDoubleImpl` (docs/27 seed). This is the M3 recording list.
- **managed-reentry (5):** `Simulator_AddObjectImpl` (depth 6), `Simulator_GetCurrentTaskImpl`, `World_CanSeeObjectImpl` (docs/62: UNSAFE) and the two `UTFConnectorSimulator` twins. The host's shim answers "no current task" and records `AddObject`.
- **writes-world (1):** `WeatherControl_SetSettings` (depth 8). Deeper (13-20) the writes are `LookAt_*`, `SACS_Set*`, `Simulator_DestroyObjectImpl`, footprint and object-manager edits: paths that do things, not scoring, that a scoring test should never reach; a stub that fails loudly shows when one does.
- **YIELD:** `Simulator_SleepImpl` first appears at depth 15 (docs/78 §4 found the same path through `Relationship` construction).

## 3. Managed code that touches native state at startup

### 3.1 Static constructors

`node tools/mono-host-ics.ts --summary --cctors`: of 9,840 `.cctor`s in the game assemblies (mscorlib, System, System.Xml excluded), **45** reach an EA IC within 6 calls [verified: tool output]. By class (a cctor counts once per class): reads-world 34, static-data 33, writes-world 4, engine-only 2, managed-reentry 1.

- **Localized strings** (most of the 45): interaction `.cctor`s that build a localized name (`Food`, `Sim+SpriteSocials`, `TravelUtil`, `StocksManager`, casino tables, ...) reach `LocalizedStringService_GetLocalizedStringByString`; through the localization helper the closure also reaches `World_GetWorldNameKey` and `OnlineFeaturesHost_GetPersonaName` (name substitution tokens). A string table stub (or recorded strings) covers them.
- **UI:** `Sims3.UI.UIManager` reaches `UIManager_InitUIHierarchyImpl` and `SetUICallbacksImpl` (writes and managed callbacks). A host that never touches `UIManager` never runs it; if a scoring path touches it, the stub must be a no-op that returns success.
- `BlueprintController`, `BuyController`: `Simulator_LoadXmlAsDocument` (static-data). `HouseboatUtils`: `NameGuidMapService_GetGuidByName`, product queries. `ElectroDanceSphere+Spin`: `Random_NextDoubleImpl`.

A `.cctor` runs on first use of its type on whichever thread gets there first (docs/58), so the host runs into these only for types the tested code touches; an unexpected one shows as a call to a fail-loudly stub.

### 3.2 Native-side startup state a bare host lacks

- **Host statics:** `sSimTickCnt` pointers (§1.2 step 8) and the service objects of step 9 (`TaskControlHost`, `SerializerHost`, `ReflectionHost` and the subsystem hosts). Managed code reaches services through ICs, so a host that stubs the ICs does not need the native services [inferred].
- **Per-class script bindings** `0xD9xxxx`-`0xE3xxxx` (docs/29 §4.2): they resolve managed class, method and field handles into native globals when the host binds script classes. They are native callers into managed code, not ICs; an offline host that only calls managed code does not need them [inferred].
- **The managed world itself:** Sims, lots, objects, `SimClock`, `Autonomy` managers are built by the game's world and save load (`LoadSaveManager.LoadObjectStack` etc., bound in step 7). Nothing in the runtime start-up creates them. This is M3's problem and the main risk (§4.3).

## 4. Feasibility verdict and effort

### 4.1 Options

| Option | What | Verdict |
|---|---|---|
| A. Rebuild stock Mono 1.2.3 | build the 2007 source, load the game's assemblies | **no**: EA changed this runtime (interpreter yield support, thunk table, image-open glue, dormant sections, docs/29, docs/58); the game's mscorlib must match the runtime's icall tables; the result would not be "the same runtime" for record/replay [inferred] |
| B. TS3W.exe as a library | a 32-bit host built at a different base (for example `/BASE:0x30000000 /DYNAMICBASE:NO`) loads TS3W.exe at `0x400000`, never runs `WinMain`, and calls Mono by address | **yes, first choice** |
| C. Headless TS3W | boot the real game with an ASI that stops after `MonoScriptHost_Init` and runs tests in-process | fallback if B's start-up fights back; needs the game boot (~1-2 min) per run, not a game session |

Facts for B [verified unless marked]: TS3W.exe has relocations stripped (characteristics `0x123`, no `.reloc`), so it must sit at `0x400000`; sections `.text`, `.rdata`, `.data`, `.tls` (9 bytes), `.rsrc`, no SteamStub section; Large Address Aware set. `LoadLibraryExW` of the exe from a host whose own image is elsewhere should map it at its preferred base as `MEM_IMAGE` with imports resolved and no entry point call [inferred: standard loader behaviour for an EXE loaded as a DLL; first M1 test]. `MEM_IMAGE` matters for Boehm: its Win32 root scan walks image sections; a hand-mapped private copy would need explicit `GC_add_roots` for `.data` (docs/120 should confirm) [inferred].

### 4.2 Effort (CLAUDE.md units: wall-clock for Claude with agents)

| Step | Work | Estimate |
|---|---|---|
| M1.1 | host exe (C, MSVC x86), load TS3W.exe at `0x400000`, static TLS slot, the exe's CRT init pieces or redirects, EA heap init or `g_malloc`/`g_free` redirect | 2-5 h |
| M1.2 | detours: `0xD6FCB0` (name -> bytes from `game-copy/assemblies`), `0xD6FC40` (config path); call `mono_interp_init("MonoEngine")`; load the 10 assemblies | 1.5-3 h |
| M1.3 | register 1,760 default stubs (generated from `ic-safety.csv`, fail loudly with the name), invoke a pure managed method, then a `.cctor`-heavy type | 1.5-3 h |
| M1.4 | selftest in `tools/selftest` style; doc update | 1 h |
| **M1 total** | | **~6-12 h**, 0 game runs |
| M2 | hand-check the depth-12 autonomy ICs (65 EA bindings: 20 static-data, 27 reads-world, 12 engine-only, 5 reentry, 1 write), write stubs and recording hooks (record side runs in ts3native, so its ABI must match M3) | **~4-8 h**, plus 1 short game run to record a first static-data set |
| M2 deeper | depth 20 (247 EA bindings) | ~+6-10 h, only if M3/M4 need it |

### 4.3 Risks

1. **World state (largest):** scoring needs live managed objects. Options for M3: run the game's own load path in the host (needs most ICs as real implementations: not offline any more), restore a managed heap image (Boehm heap + statics: hard, address-bound), or build minimal managed objects from a recording with uninitialised-object construction. Unknown cost; decide in M3 before building more than M1.
2. **Start-up dependencies of TS3W.exe code:** EA heap, CRT state (`_initterm` tables run arbitrary game constructors), `.tls` users; may pull in more EA systems than expected. Mitigation: redirect the allocator and run no CRT initialisers except the ones Mono needs; option C as fallback.
3. **GC roots and threads** in a mapped exe (docs/120): the host thread must be the one that calls `GC_init` (`mono_interp_init` does) and stay the only managed thread.
4. **Classification error:** the name rule may put a write in reads-world or the other way round; the fail-loudly default turns that into a visible stop, not a wrong answer.
5. **Unresolved virtual calls** (9-35% per assembly, docs/78 §3): closures understate the IC set; the per-run stub log is the real list.
6. **Version drift:** none for game data (Steam 1.67 only, memory "Steam only"); the host must refuse any other exe hash.

## 5. Next step

**M1.1 done 2026-10-20: GO** (§8). Pre-M1.2 investigation done the same day (§9): no blocker; the revised M1.2-M1.6 plan is §9.7. M1.2 and M1.3 done the same day (§10); next M1.4. Then the M3 world-state decision.

Original plan: build M1.1 first as the go/no-go: a host that loads TS3W.exe at `0x400000`, starts the heap, and gets `mono_interp_init` to return a domain with corlib loaded through the `0xD6FCB0` detour. If that works in the first ~3 h, continue M1.2-M1.4; if CRT or heap start-up pulls in too much, switch to option C. In parallel, M3 needs a design decision on how the managed world gets into the host (§4.3 risk 1).

## 6. Tools and files

- `tools/mono-host-ics.ts`: closure from the docs/75 rows, join with `ic-safety.csv`, offline class; `--all` (whole table by subsystem), `--cctors` (static constructors), `--depth`, `--top`. Needs the git-ignored `game-copy/analysis/callgraph/index.json` (`node tools/managed-callgraph.ts build`).
- `docs/data/mono-host-ics.csv`: one row per IC binding reached at depth 20 (`min_depth` <= 12 is the core set): name, address, class, depth, docs/62 verdict and confidence, reason.
- Regenerate: `node tools/mono-host-ics.ts --depth 20 --index <repo>/game-copy/analysis/callgraph/index.json > docs/data/mono-host-ics.csv`.

## 7. Limits

- Static only; the call order inside `0xD7DD10` and the provider vtable roles are read from one function each, not traced at run time.
- `0xD6FCB0`'s key derivation (which hash, which group) was not decoded; the host does not need it (it maps names to files).
- Closure counts depend on the docs/78 index (no argument or branch tracking, unresolved virtual calls followed to the declared body only).
- The offline class is a rule, not a hand check (§2.2).

## 8. M1.1 result (2026-10-20): GO

Host: `native/monohost/monohost.cpp` (build `native/monohost/build.cmd`, MSVC x86, static CRT, own image at `0x30000000` fixed). Run: `native\monohost\out\monohost.exe <repo>\game-copy\monohost\TS3W.exe <repo>\game-copy\assemblies`; log next to the exe (`monohost.exe.log`). The exe copy sits alone in `game-copy/monohost/` so the game folder's `ddraw.dll` and `.asi` files are not pulled in. Like `steerro-test`, the host restarts itself suspended with `0x400000`-`0x125A000` reserved, then frees the reservation right before loading. It refuses any exe whose `TimeDateStamp` (`0x52DEC247`) or `SizeOfImage` differs from Steam 1.67. Two clean runs, ~70 ms each, exit 0. The game and Steam were not started.

| Step | Result | Tag |
|---|---|---|
| 1. Map TS3W.exe | `LoadLibraryExW` (exe manifest activated for the side-by-side `MSVCR80`/`MSVCP80`) maps it at `0x400000` as `MEM_IMAGE` and does not run the entry point. **It does not snap the imports or set up static TLS** for an EXE: IAT slot `0xF95140` (`GetModuleFileNameW`) still held the hint/name RVA `0xD42D7E`, and the first run faulted when `mono_interp_init` called through it. The host now resolves the imports itself (24 DLLs, 664 functions, by name) and gives the 8-byte `.tls` block index 1 in a new TLS array for the host thread (no TLS callbacks exist) | verified (log, fault) |
| 2. Heap | No EA heap init. `jmp` patches send eglib `g_malloc` `0xD70A10`, `g_malloc0` `0xD70A30` (EA's takes `(n, size)`), `g_free` `0xD70A60`, `g_strdup` `0xD70A80`, `g_realloc` `0xD71860` and the `GeneralAllocator` methods alloc `0x4E69D0`, free `0x4E5920`, realloc `0x4E6A70` (thiscall) to the host CRT. One EA alloc came with flags `0x400000` (size 12); ignored, no fault. Correction: `g_free` calls `0x4E5920`, not `0x4E51B0` (§1.4 and the summary use the older name) | verified (disassembly, log) |
| 3. Image loader | `0xD6FCB0` (cdecl, path -> `MonoImage*`) detoured to a basename -> file lookup in `game-copy/assemblies`, then `mono_image_open_from_data` `0xE84860` (copy = 1); `0xD6FC40` (machine.config path) points at a missing file. Requests seen: `MonoEngine` (the domain name, tried as an assembly, miss is harmless), `<host dir>\lib\mono\1.0\mscorlib.dll` (served), the GAC policy file `policy.1.0.mscorlib.dll` (miss), and while ScriptCore loads `.\Sims3Common`, `.\Sims3Common.dll`, `.\UTFConnector.dll` (misses, harmless) and `<cwd>\SimIFace.dll` (served) | verified (log) |
| 4. Runtime | `mono_interp_init("MonoEngine")` returns a domain; `mono_defaults.corlib` `[0x1220E40]` and `object_class` `[0x1220E44]` are set. `GC_enable_incremental` was not called (the game calls it first, §1.2 step 2) | verified (log) |
| 5. ScriptCore | `mono_assembly_load_from_full` `0xE68290` on the served image; `mono_class_from_name` `0xE6FA90` finds `ScriptCore.Route`; `mono_class_get_method_from_name_flags` `0xE70330` finds the private static `GetNumPointsForZeroToDiameterApprox(float, float)` (pure arithmetic, no calls, no `.cctor`) | verified (log) |
| 6. Invoke | `mono_runtime_invoke` `0xEA1090`: `(100, 1)` -> 7920 and `(1, 0)` -> 1, the values the C# body gives, no exception. The boxed value sits at `+4`: this build's `MonoObject` has no synchronisation word | verified (log) |

What this settles: option B works without EA's heap, CRT initialisers, `WinMain`, the resource manager or `MonoScriptHost_Init`; the EA glue that `mono_interp_init` and a simple invoke reach is covered by the eight allocator patches and two detours. Not yet exercised: class registration `0xD790A0` on a game type with EA bindings, the IC binder `0xD9A7B0`, trampolines `0xD9AAF0`/`0xD9AB80`, yields, any IC, the other seven game assemblies, a GC cycle (nothing allocated enough to trigger one), and incremental GC.

Limits: one thread; the host-built TLS array only covers that thread; the `0x400000` flag on EA allocs is ignored (no alignment > 8 seen); the exe hash check is the PE timestamp and size, not a content hash.

## 9. Pre-M1.2 investigation (2026-10-20)

Rodney asked for the full picture of what M1.1 did not exercise before M1.2 starts. Method: Ghidra headless on the copy project `game-copy/ghidra-mono-host` (read-only), static values read from the PE file, the managed index (`game-copy/analysis/callgraph/methods.csv`), the decompiled script assemblies (read only), and one probe mode in the host to confirm the static findings: `monohost.exe <exe> <assemblies> probe` (steps P1-P5; two clean runs plus one default-mode run, all exit 0, under 1 s each). The game and Steam were not started. Claim ids I-119-13 to I-119-24 are in docs/51 under the M1.1 additions.

### 9.1 Internal calls

| Finding | Tag |
|---|---|
| **EA's ICs are P/Invokes.** The script assemblies declare them as `[DllImport("Sims3Common.dll")]` (1,638), `[DllImport("Sims3Common")]` (148) and `[DllImport("UTFConnector.dll")]` (148), not as `InternalCall` methods. EA's `mono_lookup_pinvoke_call` `0xE84D60` never loads a DLL: it asks `mono_lookup_internal_call` `0xE82680` with the managed `Namespace.Class::Method` name; on a hit it clears the P/Invoke flag and marks the method an internal call (`iflags | 0x1003`); on a miss it reports `EntryPointNotFoundException` with the entry name | verified (decompile, managed declarations) |
| **Registration** is name -> pointer in Mono's `icall_hash` `[0x11F436C]` through `mono_add_internal_call` `0xE82550`, filled once in `MonoScriptHost_Init` from each provider's `{function, name}` table (§1.2 step 5). Lookup is lazy: at the first call of each method (native wrapper `0xE991B0`, or transform `0xE643B0` for methods already marked IC), then cached in the method. Stubs must be registered before the first managed call that reaches an IC; there is no init-time binding pass to replay | verified |
| **Unregistered IC in this build:** `0xE82680` returns 0 without a warning for non-corlib names (the "cant resolve internal call" text only prints for classes in the corlib static table); the native wrapper then throws **`System.EntryPointNotFoundException`** in managed code at the call. No native fault, but a managed `try/catch` in game code can swallow it | verified (decompile; probe P2 on `Random_NextDoubleImpl`) |
| **The name list:** the exe holds 1,777 `ScriptCore.<Class>::<Method>` strings; 1,772 match a managed extern. 164 game externs have **no** name in the exe, so they are unregistered in the game too (146 `UTFConnector*` twins, 5 `SocialFeatures`, 3 `Juice`, 2 `ProfilerUtils`, 4 `Sims3.UI.LocalizedStringService`, 4 others): calling them throws in the game as well, and a host must leave them unregistered to match. 5 exe names have no managed extern (dead registrations). docs/62's 1,758 is the provider-table count; the string scan finds a few more. **Re-check 2026-10-20 (I-119-15):** recount 164 (ASCII and UTF-16; three names a `\0`-bounded scan misses sit after a non-zero byte and are present). No name is built at run time: `mono_add_internal_call` `0xE82550` is the only writer of `icall_hash` `[0x11F436C]`, and its 7 callers pass string literals (`0xD7E676`, `0xD7E685`, four in `mono_interp_init` `0xE5A74A`-`0xE5A77A`) or run the provider loop `0xD7E6C7`, whose names come from 47 static `{function, name}` tables (1,776 pairs in `.rdata`/`.data`) | verified (string scan vs `methods.csv`; registration path re-read) |
| **Cleanest stub registration:** scan the mapped exe for the name strings (no CSV dependency, always matches the exe), allocate one RX block of 10-byte thunks `push index; jmp entry`, and register each name with its thunk, so one handler knows the name from the index. Probe P3 registered 1,775 this way (2 strings fail the scanner's string-start check) and a stubbed `Simulator_GetTickCountImpl` reached the handler by name. ICs use the mint trampoline's cdecl convention (caller cleans), so a stub need not know the argument count; it must know the return type (`float`/`double` returns come from x87, so a stub that returns 0 in `eax` leaves an empty FP stack). M1.3 takes return types from the managed signatures | verified (probe); return-type rule inferred |
| **Real IC bodies are not an option without the engine:** for example `Simulator_SleepImpl` `0x76CB00` reads the app singleton `[0x11D4BBC]` (zero in a bare host) and calls through it | verified (decompile, static value) |

### 9.2 The other game assemblies

| Finding | Tag |
|---|---|
| Probe P1 loaded System.Xml, System, Sims3Metadata, UI, Sims3GameplaySystems, Sims3GameplayObjects and Sims3StoreObjects (SimIFace was already loaded through ScriptCore's reference), 0-16 ms each, all status 0; `Sims3.Gameplay.Autonomy.Autonomy` resolves | verified (probe) |
| References: ScriptCore -> SimIFace, System.Xml; SimIFace -> System.Xml; Sims3Metadata -> SimIFace, System.Xml; UI -> SimIFace, Sims3Metadata, System.Xml; GameplaySystems -> UI, SimIFace, Sims3Metadata, System.Xml; GameplayObjects -> GameplaySystems, SimIFace, UI, Sims3Metadata; StoreObjects -> GameplaySystems, GameplayObjects, SimIFace, UI. Mono resolves them on demand through the image detour, so load order only matters for duplicates | verified (project references of the decompiled assemblies, probe log) |
| **Duplicate images:** later reference loads ask for System.Xml and System through GAC-style paths (`lib\mono\gac\System.Xml\1.0.5000.0__b77a5c561934e089\System.Xml.dll`); the M1.1 detour opens the file again and Mono gets a second image of an already-loaded assembly. The game avoids this because `0xD79C60` checks the host image map first. M1.2's detour needs the same name -> image cache | verified (probe log) |
| The `Sims3Common` / `UTFConnector.dll` requests of I-119-12 come from the ModuleRef table (the DllImport module names), which Mono opens as modules when the image loads; P/Invoke resolution itself never uses them (§9.1) | ModuleRef source inferred (stock 1.2.3); P/Invoke part verified |
| Per-assembly EA work in the game is only open + `mono_assembly_load_from_full` (`0xD7C5A0`), then `Simulator.SignalAssembliesLoaded`, which only raises `sOnAssembliesLoaded` (empty unless a script subscribes). Class work happens later, per class (§9.4) | verified (§1.2 step 6, decompiled SimIFace) |

### 9.3 A GC cycle

| Finding | Tag |
|---|---|
| What the collector needs is set up statically or by Mono's own init: the root hook `[0x11949CC]` = `0xE4EF90` -> `0xD71820` (stack of the calling thread, from the current page to the end of its region) is a static value; `mono_gc_base_init` `0xE69570` sets the warn proc (the static value before that is EA's `0x5BFB90`), `GC_finalize_on_demand` = 1 and the typed descriptors; `GC_init` runs in `mono_runtime_init`. Heap growth `0xD70900` skips EA's memory tracker because `[0x11DC18C]` is zero in a bare host; `GC_collect_or_expand` `0xE4A120` uses the never-stop function, not EA's timer. EA's stop function `0xD717B0` (frame-time budget) is used only by `MonoScriptHost_Simulate`. Nothing that EA or the CRT initialises is needed | verified (decompile, static values) |
| Probe P5: 20,000 garbage objects, `System.GC.Collect()` and `GetTotalMemory` run without a fault, in both runs; an object referenced only from the host's `.data` keeps its vtable word. Whether an object referenced only from CRT-heap memory is reclaimed was inconclusive (it stayed intact; Boehm scans the stack conservatively and stale slots can keep it). docs/120's rule stands: only writable `MEM_IMAGE` sections and the collector's stack are roots, so the host keeps managed references in `.data`, GC handles or `GC_malloc_uncollectable` memory, never in `malloc` memory | verified (no fault, root kept); reclaim inconclusive |
| **Finalizers never run in the host** unless it calls `mono_gc_invoke_finalizers` `0xE697F0` (callers: `MonoScriptHost_Simulate` twice and `0xE790E0`). The game drains the queue from Simulate (docs/36); the host should call it at its tick boundary to match | verified (xrefs) |
| The host skips `GC_enable_incremental` (its only caller is Init), so it collects stop-the-world and non-incrementally; reachability is the same, collection timing and finalizer order are not. That matters only for record/replay of finalizer or weak-reference effects | verified (xref); effect inferred |
| One managed thread only: `GC_init` registered the host thread; managed code on another thread is out of scope until M5 (docs/120) | verified (docs/120) |

### 9.4 Class registration `0xD790A0` and binder `0xD9A7B0`

| Finding | Tag |
|---|---|
| **`0xD790A0` is not an engine registration.** It is EA's interface-offset layout for `mono_class_init` (5 call sites in `class.c`: `0xE6EE20` twice, `0xE6CCD0`, `0xE6E050`, `0xE6A480`): it walks the interfaces of the class and its parents, records the highest interface id (`class+0x46`), and writes a sorted (interface id, slot) table into the image mempool (`class+0x4C`, count `+0x48`), using an EASTL hash map (heap tag `Scripting/MonoScriptHost/Mono`). It reads no engine global; its only EA dependency is the EASTL allocator `0x4E3F90` / `0x4E3F70` -> `0x4E5920`, which M1.1 already redirects. It has run for every class initialised so far (corlib, ScriptCore, SimIFace, `ScriptCore.Simulator` in P3/P4); the one flagged 12-byte EA allocation in the M1.1 log matches its hash-node size | verified (decompile, xrefs); allocation match inferred |
| **`0xD9A7B0` is EA's native-method binder.** For a non-abstract method it builds the key string `<return> <Class>:<Method>(<args>)` (`0xD9A530`), hashes it and looks it up in a hash map at `0x119448C`; on a hit it copies three function pointers into the runtime method (`+0x24`, `+0x28` = native body, `+0x2C` = one-shot init). The interpreter then runs EA's native body instead of the method's IL (for example `0xDA5970`, docs/29 §5.1). The 1,085 entries come from `0xE3E570` (1,085 calls to `0xD9A860`), whose only caller is the services method `0xD77600` (secondary host vtable `0x108B690` slot 1) | verified (decompile, xrefs, slot value) |
| **In a bare host the binder always misses:** the map's bucket pointer is statically the shared empty bucket `0x114C900` with count 1, and `0xE3E570` never runs, so every managed method runs its IL. That is why M1.1 worked; it also means the host does not run the code the game runs for those 1,085 methods | verified (static values, xrefs); IL equals native body inferred |
| If a host calls `0xE3E570`: the native bodies' init callbacks resolve IC pointers by name at bind time into globals (for example `[0x11EE6CC]`); an unregistered IC there gives a NULL pointer and a call to address 0, not an exception; native bodies also use the EA IC nesting counter `[0x11F2D84]` and `setjmp` guards. `0xD77600` also seeds EA's RNG from `[Config] SimulatorRandomSeed` (docs/27) and creates the args allocator and the three service hosts | verified (decompile) |
| **Engine singletons on the class path:** none in `0xD790A0` or the binder. They sit behind the ICs (§9.1) and behind the managed service objects: 44 ScriptCore/SimIFace/UI classes register themselves with `AppDomain.SetData("<Name>", this)` in their constructors (47 sites), and SimIFace, UI and GameplaySystems read 41 keys back (`gSimulator` comes from `AppDomain.CurrentDomain.GetData("Simulator")` in the `Sims3.SimIFace.Simulator` `.cctor`). In the game, Init step 9 constructs them. In a bare host they stay null, so the first wrapper call (`Simulator.Sleep`, `World.*`, ...) throws `NullReferenceException`. Probe P4: `new ScriptCore.Simulator()` runs without any IC call, and so does `ScriptCore.Simulator`'s `.cctor` (P3) | verified (decompiled sources, probe) |
| `.cctor`s that reach engine init: still only `Sims3.UI.UIManager` (§3.1); with stubs registered, an unexpected one shows by name in the stub log | static, from §3.1 |

### 9.5 Yields

| Finding | Tag |
|---|---|
| **Managed gate first:** `Sims3.SimIFace.Simulator.Sleep` calls `CheckYieldingContext`, which asks `CurrentTask.IsValid` (IC `Simulator_GetCurrentTaskImpl`) and throws `System.Exception("Attempting to yield in a non-yielding context")` when there is no task. Probe P4: with the stub returning 0, `Sleep(1)` throws exactly that, before any native yield code | verified (probe, decompiled source) |
| **Native chain:** `Simulator_SleepImpl` `0x76CB00` -> app singleton `[0x11D4BBC]` vfunc `+0x17C` -> script host vfunc `+0x20`. The interpreter's yield opcode handler (case `0x1A6` of `0xE53EB0`) asks `0xE53550` whether the task may yield (task state `+0x148` set and no `.ctor`/`.cctor` frame on the interpreter stack); if so it calls `0xD70820` -> `[0x11EE514]` (the `MonoScriptHost`) vtable `0x108B818` slot `+0x20` = `0xD77B20`, which files the sleep under the running task id (`host+0xBD0`) and calls the host's `+8` interface `+0x11C`; then it asserts the yield mark (`state+0x149`, set by `ScriptHost_MarkYield` `0xD70F60` -> `0xE52B50`), saves IP and stack into the task state and returns to the task runner. If the task may not yield, it throws `NotSupportedException` (`0xE773E0`) | verified (decompile); which IL the transform turns into opcode `0x1A6` was not traced |
| **No scheduler in the host:** `[0x11EE514]` is zero (bss) until the factory `0xD7F2B0` runs, so a native yield would fault at `0xD70830`; with stubs and no task the managed gate throws first, so a test never gets there | verified (static value, xrefs) |
| **How a host would drive tasks:** the game creates a task state with `0xE52D70(method, object, args, ...)` (`GC_malloc(0x150)`, sets `+0x148` yieldable and `+0x14A` first run) and runs and resumes it with `0xE59FD0(state)` from `ScriptHost_RunTask` `0xD7F000` after setting `host+0xBC8`/`+0xBD0` (docs/18, docs/56). A host scheduler needs a fake host object at `[0x11EE514]` whose vtable `+0x20` records `(task, ticks)` and whose `+0xBC0`/`+0xBC8`/`+0xBD0` fields carry the running state; stubs for `Simulator_GetCurrentTaskImpl` (running task id), `Simulator_SleepImpl` (record the wake tick, set the state's yield mark), `Simulator_WakeImpl` and `Simulator_AddObjectImpl`; then `0xE52D70` for `ITask.Simulate` of a task object and `0xE59FD0` per host tick until the task ends. The sim's own entry, `MonoScriptHost_Simulate` `0xD81840` (ScriptTick, TaskObjects, GC step, finalizers; docs/36 §3.1), is far too engine-bound to call | inferred design from verified pieces |

### 9.6 Risk table

| # | Risk | Area | Likelihood | Impact | Mitigation |
|---|---|---|---|---|---|
| 1 | Game code catches `EntryPointNotFoundException` (unregistered IC) and carries on with a wrong answer | 9.1 | medium | wrong results, silent | register a stub for every exe name before the first managed call; stubs fail loudly (log name, `RaiseException`) unless hand-written |
| 2 | A stub returns 0 for a `float`/`double` IC: x87 stack underflow, NaN | 9.1 | high if not handled | wrong values | return type per IC from `methods.csv` signatures; FP stubs load 0.0 |
| 3 | Duplicate images for one assembly (GAC-path requests) | 9.2 | seen in P1 | type identity bugs (two `System.Xml` classes) | name -> image cache in the detour, like `0xD79C60` |
| 4 | Service objects missing: `NullReferenceException` in every SimIFace wrapper | 9.4 | certain without them | tests stop early | construct the 44 SetData classes (their constructors must be IC-free; P4 shows `Simulator` is) |
| 5 | IL vs EA native bodies (1,085 methods) differ in effect or speed | 9.4 | low-medium | record/replay mismatch, timing | M1.2 option: call `0xE3E570` in a probe with a binder log to list which bodies the autonomy closure hits; keep IL by default |
| 6 | Finalizers and incremental GC differ from the game | 9.3 | certain | finalizer/weak-ref timing in replays | call `0xE697F0` at each host tick; optional `GC_enable_incremental` |
| 7 | Managed refs kept in `malloc` memory get collected | 9.3 | medium | use-after-free in the host | rule: GC handles, `.data` or uncollectable memory only |
| 8 | The yield path needs host fields beyond those mapped (`host+0xBC0..0xBD4`, `+8` interface `+0x11C`) | 9.5 | medium | task driver faults | build the fake host with all mapped fields; the fault log names the offset; fallback: tests avoid yielding paths (scoring reaches `Sleep` only at depth 15, §2.3) |
| 9 | `0xD77600` RNG seeding skipped | 9.4 | certain | none for M1 (`Random_*` are stubs); matters for M3 replay | M3 records `Random_NextDoubleImpl` results (docs/27) |

### 9.7 Revised M1.2-M1.6 plan (wall-clock for Claude with agents, 0 game runs)

| Step | Work | Estimate |
|---|---|---|
| M1.2 | detour name -> image cache (incl. GAC paths), load all 10 assemblies in dependency order; construct the 44 SetData service objects (check each constructor's closure for ICs first); optional probe: call `0xE3E570`, log binder hits by method name | 1-2 h |
| M1.3 | fail-loud stub table from the exe strings (P3 code), return type per IC from `methods.csv`, override table for hand-written stubs (`GetCurrentTask`, tick count, ...); leave the 164 unregistered externs unregistered; `.cctor` sweep over the 45 IC-reaching types and the depth-12 autonomy types, listing the ICs each hits | 2-3 h |
| M1.4 | GC: host tick = collect + `0xE697F0`; stress test with a managed object graph held through GC handles; find `GC_gc_no` to prove collections ran | 1-2 h |
| M1.5 | task driver: fake host at `[0x11EE514]`, `0xE52D70` / `0xE59FD0`, Sleep/Wake/GetCurrentTask stubs; test a managed task that sleeps twice and resumes | 3-6 h |
| M1.6 | selftest in `tools/selftest` style, docs | 1 h |
| **Total** | | **~8-14 h**, 0 game runs; M2 (§4.2) follows unchanged; the M3 world-state decision stays the main unknown |

No blocker was found: every area either already works in the bare host (other assemblies, class init, a GC cycle, the binder's clean miss) or fails in a named, managed way (unregistered IC, missing service object, yield outside a task).

## 10. M1.2-M1.3 result (2026-10-20)

Host: `native/monohost` mode `m13` (`monohost.exe <exe copy> <assemblies> m13 <hostdata> [lenient]`); `<hostdata>` comes from `node tools/managed-callgraph.ts hostdata --out game-copy/monohost/hostdata.txt` (return type of every managed extern, and every type with a `.cctor` in the seven game assemblies, by TypeDef row). Runs: two strict, one lenient, plus one default and one `probe` run as regression checks. All exit 0, ~2 s each, single thread. The game and Steam were not started. Claim ids I-119-25 to I-119-32 are in docs/51.

### 10.1 M1.2: assemblies and service objects

| Step | Result | Tag |
|---|---|---|
| Image cache | The `0xD6FCB0` detour now returns the image it already served for the same file name, as `0xD79C60` does in the game (no addref there either). Later reference loads by GAC path (`lib\mono\gac\System\1.0.5000.0__b77a5c561934e089\System.dll`, same for System.Xml: 10 requests in a run) get the loaded image; the first word of the image (`ref_count` in the stock layout) stays 1. 10 distinct images for 10 files | verified (log) |
| All 10 assemblies | mscorlib (init), ScriptCore (M1.1 step), SimIFace (reference), then System.Xml, System, Sims3Metadata, UI, Sims3GameplaySystems, Sims3GameplayObjects, Sims3StoreObjects: status 0 each; one type per game assembly resolves by TypeDef token through `mono_class_get_full` `0xE6F840` | verified (log) |
| Service objects | 36 classes built with `mono_object_new` + `.ctor()`: the 35 ScriptCore classes that call `AppDomain.SetData` in their constructor (UTFConnector twins left out: no exe class names, unnamed ICs) and `Sims3.SimIFace.Automation`. Strict stubs: **32 construct without any IC call**; 4 call one IC each: `ThumbnailManager` (`ThumbnailManager_SetCallbacks`), `DownloadContent` (`DownloadContent_SetSimSavedCallbackImpl`), `UserToolUtils` (`UserToolUtils_SetBuildBuyGenericCallbackImpl`), `LoadSaveManager` (`Simulator_AddObjectImpl`). With typed zeros (lenient) all 36 construct | verified (log) |
| Engine state for services | None of the 36 needs engine state to be built: three ICs hand native code a managed callback (a no-op in the host), and `Simulator_AddObjectImpl` is the reentry IC the host shim must record (§2.3). They go into the M2 allow-list as no-ops. `Sims3.UI.LocalizedStringService` and `Sims3.Gameplay.Gameflow` also call `SetData` but are built by managed code, not by Init; the four `.cctor` registrars (`EventQueueInitializer`, `StopWatch`, `RouteManager`, `Swarm`) run in the sweep | verified (log); "built by managed code" inferred from the caller list |
| Wrappers after services | `Sims3.SimIFace.Simulator.Sleep(1)` throws `System.Exception: Attempting to yield in a non-yielding context` (managed gate, not a null service); `GetUsedBytes()` reaches its stub by name (`NotImplementedException: monohost stub: ScriptCore.Simulator::Simulator_GetUsedBytesImpl`) | verified (log) |

Not done: the optional `0xE3E570` binder probe (§9.7) and a check that every one of the 41 `GetData` keys finds its object.

### 10.2 M1.3: typed fail-loud stubs

| Item | Result | Tag |
|---|---|---|
| Registration | 1,775 exe name strings, one `push index; jmp entry` thunk each (P3 code). Return kind from hostdata: void 632, 32-bit int/ref/bool/enum 923, 64-bit 73, float 59, double 1, struct 82, no managed extern 5 (the dead registrations of §9.1, which throw if ever reached). The 164 unnamed externs stay unregistered, as in the game | verified (log) |
| Fail loud | First call per IC is logged by name; then the stub raises `NotImplementedException("monohost stub: <name>")` with `mono_get_exception_not_implemented` `0xE77360` and `mono_raise_exception` `0xEA18F0` (whose `ex_handler` `[0x1195B64]` is set by `0xEA18D0` in `mono_interp_init`). The exception arrives in managed code at the call; no native fault. Addresses from the `exception.c` string order and the call pattern `call 0xE770E0; push eax; call 0xEA18F0` | verified (log, bytes) |
| Typed defaults | Allow-list (strict too): `Simulator_GetCurrentTaskImpl` -> 0 (no task), `Simulator_GetTickCountImpl` -> host tick. `lenient` gives every non-struct stub a typed zero. Checked: `Random_NextDoubleImpl` returns a boxed `0.0` from `fldz` (x87 path), `GetTickCount` with tick `0x1000004D2` returns both halves (edx:eax path). Struct returns (82: `ResourceKey` 42, `Vector3` 14, ...) always throw: their return convention in the mint trampoline was not checked | verified; struct convention open |

### 10.3 `.cctor` sweep

Every type with a `.cctor` in SimIFace, ScriptCore, Sims3Metadata, UI and the three gameplay assemblies (9,840; 14 open generic definitions skipped) is initialised in hostdata order through `RuntimeHelpers.RunClassConstructor`, so `mono_runtime_class_init` runs inside an IC (a throwing `.cctor` comes back as a managed `TypeInitializationException`; an initialised class is not run again). The type handle is `klass + 0x98` (`MonoClass.byval_arg`: found by its self pointer, type byte `0x12` for a class, `0x11` for `Vector3`). The sweep takes ~1.7 s. Per-type rows that are not plain `ok` in either mode: `docs/data/mono-host-cctor-sweep.csv` (55 rows: strict and lenient result, stubs reached, inner exception).

| Mode | ok | ok, reached a stub | exception | fault | types reaching a stub |
|---|---|---|---|---|---|
| strict | 9,771 | 1 | 54 | 0 | 45 |
| lenient | 9,772 | 19 | 35 | 0 | 47 |

[verified: two strict runs with the same counts; one lenient run]

- **45 strict hits match the static count of §3.1 (45 `.cctor`s reach an EA IC within 6 calls).** Stubs reached: `LocalizedStringService_GetLocalizedStringByString` 27, `NameGuidMapService_GetGuidByName` 7, `Random_NextDoubleImpl` 4, `StopWatch_CreateStopwatch` 3, `Simulator_LoadXmlAsDocument` 2, `Animation_SetPMAAnimationFinishedCallback` 1, `UIManager_SetUICallbacksImpl` 1. The other 9 strict exceptions are knock-ons (a `.cctor` that uses an already failed type).
- **Autonomy types:** `Sims3.Gameplay.Autonomy.Autonomy` and `Motives` fail strict through `StopWatch_CreateStopwatch` (profiling stopwatch); `AutonomyTuning` pulls in `Sims3.UI.UIManager`'s `.cctor` (`UIManager_SetUICallbacksImpl`, `InitUIHierarchyImpl`, `InitUserToolHelperImpl`, `LoadUIImageImpl`, `Simulator_LoadXmlAsDocument`, `Simulator_AddObjectImpl`, `StopWatch_*`, `Command_RegisterCommandImpl`). With typed zeros all three initialise, so the scoring path needs no-op UI and stopwatch stubs first.
- **Lenient leftovers (35):** 31 `NullReferenceException`s, all localized-name `.cctor`s (interaction definitions, `Food`, `TravelUtil`, `StocksManager`, casino tables, ...): the string stub returns `null` and the caller dereferences it; 4 are the same failures one level up. A string-table stub (static-data, §2.2) fixes them; a non-null placeholder string would do for tests that do not compare names.
- Attribution is first-touch: a `.cctor` that another one already ran is credited to the first (for example `UIManager` under `AutonomyTuning`). Order is TypeDef order per assembly.

### 10.4 Blockers and next step

No blocker. Open for M2: hand-written stubs for the sweep and service set (UI and callback-registration no-ops, stopwatch handles, `GetGuidByName`, `LoadXmlAsDocument`, a string table or placeholder, seeded `Random`), and the struct-return convention for the 82 struct ICs (only needed once one is allow-listed). **Next: M1.4** (§9.7): host tick = collect + `mono_gc_invoke_finalizers` `0xE697F0`, a GC stress test with a managed graph held by GC handles, and `GC_gc_no` to prove collections ran.

## 11. M1.4-M1.6 result (2026-10-20)

Host: `native/monohost` mode `selftest` (`monohost.exe <exe copy> <assemblies> selftest <hostdata>`), plus our own managed test assembly `native/monohost/HostTest.cs`, which `build.cmd` compiles to `out\MonoHostTest.dll` with the .NET Framework 4 `csc` against the game's `mscorlib.dll` and `SimIFace.dll` copies (metadata version `v2.0.50727`, the same as the game's assemblies; nothing EA in the source). The host serves it from its own folder through the image detour. Runs: selftest three times (23 of 23 checks pass each time, exit 0, ~2.5 s), plus `default`, `probe` and `m13` strict once each as regression checks (all exit 0; strict sweep counts unchanged). The game and Steam were not started. Claim ids I-119-33 to I-119-44 are in docs/51.

### 11.1 M1.4: GC in the host

| Item | Result | Tag |
|---|---|---|
| Host tick | `mono_gc_collect(0)` `0xE69650` (jumps to `GC_gcollect` `0xE4A0B0`), then `mono_gc_invoke_finalizers` `0xE697F0` (`GC_should_invoke_finalizers` `0xE4B3E0`, then `GC_invoke_finalizers` `0xE4B3F0`; returns the count). Stop function of `GC_gcollect` is `0x5BFB90` (`xor eax, eax; ret`, the never-stop function shared with EA's warn proc by identical-code folding) | verified (bytes, run) |
| `GC_gc_no` | `[0x11F2F90]`: `mono_gc_collection_count` `0xE69660` is `mov eax, [0x11F2F90]; ret`; `add [0x11F2F90], eax` (eax = 1) at `0xE49EE5` in the collector. 300 host ticks raised it by exactly 300, and managed `GC.CollectionCount(0)` returns the same value (301 with P5-style earlier collections) | verified (bytes, run) |
| Heap globals | `GC_heapsize` `[0x12225A0]` (`GC_get_heap_size` `0xE4DD30`); `mono_gc_get_used_size` `0xE69670` = heap size minus free bytes (int64) | verified (bytes) |
| Stress test | A 2,000-node graph (child arrays, strings, a next chain) whose only root is a normal `GCHandle` (the host keeps the handle as an int) stays intact across 300 collections, checked every 50 ticks; a second graph held only by a weak handle is gone after the first collections; each tick allocates ~2,000 garbage nodes + arrays + strings and 50 objects with a finalizer | verified (run) |
| Finalizers | 15,000 finalizers run by the host's drain; 14,997 of 15,000 test objects finalized (the last tick's few stay queued or conservatively reachable) | verified (run) |
| Bounded heap | `GC_heapsize` 40,960 KB at tick 50 and at every later tick; used 3,736-3,740 KB at tick 50 and its max after. ~2 ms per tick for allocate + full collection + drain | verified (run) |

**Root correction (important for M5 and `[MonoThread]`):** `mono_gc_base_init` sets `[0x11949A8]` = 1 at `0xE69598` (stock Mono's `GC_no_dls = TRUE`), and `GC_push_roots` `0xE4D910` calls `GC_register_dynamic_libraries` `0xE4A8E0` only when `[0x11949A8]` is 0 (test at `0xE4D915`). So **writable image sections are not roots**: in the host, the root-set table `0x122FB14` (count `[0x11F3084]`) held three 5-byte ranges (`0x11F42B0`, `0x1195A14`, `0x1195A2C`: Mono's own registered variables) after collections, and a task state kept only in the host exe's `.data` was collected before its second resume (§11.2). docs/120 §1.1 and I-120-5 say the opposite and are wrong for this build; probe P5's "A kept" (§9.3) came from a stale stack slot. Roots are: the explicit root sets, uncollectable blocks, the finalizer and disappearing-link tables, the collector's registers and its own stack (`[0x11949CC]` hook), and whatever a host pushes from that hook. The game runs the same `mono_gc_base_init`; whether EA's own init adds root sets was not checked [inferred: same flag in the game]. Rule for hosts and hand-offs: keep managed references in GC handles, managed statics or uncollectable memory, or push them from the root hook; never rely on a native global.

### 11.2 M1.5: task driver and the yield contract

How a yield works in this build (all from bytes, then confirmed by the driver):

1. **Sleep is an interpreter intrinsic, but only after EA's assembly load has run.** When SimIFace loads, `0xD7C5A0` stores the class `Sims3.SimIFace.Simulator` in `[0x1220F34]` (`mono_class_from_name` on `"Sims3.SimIFace"`, `"Simulator"`, at `0xD7C635`). The transform (`0xE5DD1C`) then turns a call to a method of that class named `Sleep` into opcode `0x1A6` and `CheckYieldingContext` into `0x1A7` (no args, throws) or `0x1A8` (bool arg, pushes the answer). The managed bodies of those two never run in the game. In a host that loads assemblies through `mono_assembly_load_from_full` directly, `[0x1220F34]` stays 0, `Sleep` runs its IL, and `CheckYieldingContext` asks the `GetCurrentTask` IC (§9.5 row 1). The selftest sets `[0x1220F34]` right after ScriptCore and SimIFace load, before any `Simulator` method is transformed.
2. **May this state yield?** `0xE53550(state, frame)`: the state's `+0x148` flag (set by `0xE52D70`, clear for `runtime_invoke`'s stack state) and no `.ctor`/`.cctor` frame up to the resume depth. If not, opcode `0x1A6` throws `NotSupportedException("Attempting to yield in a non-yielding context!")` (`0xE773E0`); `0x1A7` does the same unless `[0x11F414C]` (yielding disabled) is set.
3. **Yield:** opcode `0x1A6` pops the tick count and calls `0xD70820(ticks)`: ticks clamped to `0xFFFFFC17`, then `[0x11EE514]` main vtable `+0x20` (thiscall, `ret 4`). The game's body `0xD77B20` files `(task id host+0xBD0, ticks)` in the sleep map at `host+0x1728` (nothing for 0 ticks) and calls the `host+8` vtable `+0x11C` = `0xD70F60`, which sets `+0x149` on `*[host+0xBC8]` (the running task's root state) when `host+0xBC8` and the id at `host+0xBD0` are non-zero. Back in the opcode, `+0x149` of the current state must be set (assert), then IP and stack go into the state (`0xE53620`) and the interpreter returns.
4. **Run and resume:** `0xE52D70(method, obj, args, exc_slot)` = `GC_malloc(0x150)` (normal, collectable), `[0]` method, `[1]` object, `[2]` args, `[4]` = `exc_slot`, `+0x140`/`+0x144` = -1, `+0x148` yieldable, `+0x14A` first run. `0xE59FD0(state)` runs or resumes; after it returns, `0xE52B40(state)` (`+0x149`) set = sleeping, clear = ended: the boxed return value is at `+0x20` (as `runtime_invoke` `0xE5A600` reads it) and a managed exception is in `*exc_slot`. `0xE53E80(state)` releases it (`0xE52DD0`, then clear + `GC_free` when `+0x148` is set).

**Host contract (what `monohost` does, mirroring `ScriptHost_RunTask` `0xD7F000`):**

| Piece | Host side |
|---|---|
| Fake host | 0x1800-byte block at `[0x11EE514]`; main vtable `+0x20` = `FakeSleep`, `host+8` vtable `+0x11C` = EA's `0xD70F60`; every other slot of both vtables (64 + 128) is a trap that logs the slot and ends the run (exit 90): nothing else was reached |
| `FakeSleep(ticks)` | records wake tick = host tick + max(ticks, 1) for the task whose id is in `host+0xBD0`, then calls `0xD70F60` with `ecx = host+8`; returns `false` like `0xD77B20`. A task runs at most once per host tick, so `Sleep(0)` means the next tick [inferred for the game] |
| Per run | `host+0xBC8` = address of the slot that holds the task's state pointer, `host+0xBD0` (8 bytes) = task id; `0xE59FD0(state)`; clear both fields; then `0xE52B40` as above |
| GC | task states live in host memory, which is not a root (§11.1): the host pushes its task table from the `[0x11949CC]` hook (`GC_push_all_stack` `0xE4CEE0` on the table, after the original hook). Without it the state was freed and reused before the 2nd resume (fault in the return-value boxing `0xE52C78`, state `+0x24` = 0); with it, collections every tick change nothing |
| `GetCurrentTask` stub | returns the 64-bit id in `host+0xBD0` (0 outside a task) |
| After an exception | the interpreter leaves the dead state in `[0x11F4170]` (current state; the `longjmp` skips the restore); the driver puts back the value it saw before the run |

**Driver results (selftest, three runs):**

| Check | Result |
|---|---|
| `SleepLoop(1)` through `runtime_invoke` | `NotSupportedException` "Attempting to yield in a non-yielding context!" (the intrinsic, not the managed `System.Exception` of §9.5) |
| `SleepLoop(5)` as a task, host GC every tick | runs at tick offsets 0, 1, 3, 6, 10, 15 (sleeps 1, 2, 3, 4, 5); step counter 1, 2, 3, 4, 5, 1005 after the runs; return value 1006 boxed at `+0x20`; a 50-node local graph and the loop variables checked after every resume; no exception, no fault |
| `Simulator.CurrentTask` in the task | `0xABCD00000011` each time (both halves) |
| Yield inside a `.cctor` frame (as a task) | refused: the task ends after one run with `TypeInitializationException` (inner `NotSupportedException`), no sleep recorded |

Not exercised: two tasks interleaved, `Simulator.Wake`, `AddObject` task creation, the first-run `0xD7DB80` path of `0xD7F000` (record byte `+0xD`), yielding-disabled `[0x11F414C]`, and the game's sleep map (`host+0x1728`, `+0x1748`).

### 11.3 M1.6: selftest

`out\monohost.exe <repo>\game-copy\monohost\TS3W.exe <repo>\game-copy\assemblies selftest <repo>\game-copy\monohost\hostdata.txt` (build first with `native\monohost\build.cmd [<assembly folder>]`). Prints one `PASS <id> ...` or `FAIL <id> ...` line per check and a closing `SELFTEST OK|FAILED: n passed, m failed`; exit code = failed checks (0 = all pass), or a step code (40-60, 100-103, 140) when a step stops early, 90 when a fake host slot outside the contract is called, 3 on an unhandled fault, 4 on the 120 s timeout. Log: `out\monohost.exe.log`; the strict sweep CSV is rewritten next to it. Checks: `M1.1-invoke`; `M1.2-assemblies`, `-services` (>= 32 of 36 strict), `-sleep-gate`, `-image-cache`; `M1.3-stubs` (>= 1,770), `-fail-loud`, `-fp-zero`, `-int64`, `-sweep` (0 faults, >= 9,700 ok); `M1.4-testasm`, `-collections`, `-gc-no`, `-handle-graph`, `-weak`, `-finalizers`, `-heap-bounded` (no growth over 25 % after tick 50); `M1.5-invoke-no-yield`, `-yield-resume`, `-result`, `-current-task`, `-host-fields`, `-cctor-no-yield`. Not in `tools/selftest` (that is the TypeScript tool suite); this one needs the game copy and MSVC, so it is a manual regression step for monohost changes.

### 11.4 Next

M2 (§4.2) as planned, starting with the hand-written stubs from §10.4 (stopwatch handles, `GetGuidByName`, `LoadXmlAsDocument`, placeholder strings, UI and callback no-ops) so that `Autonomy`, `Motives` and `AutonomyTuning` initialise strict; not started in this step. The task driver is ready for M2's interaction tests; M3 must decide how game tasks are created (`AddObject` path, `0xD7FC60` virtual `Simulate` lookup) and record `Random` (§9.6 row 9).

## 12. M2 hand-written stubs (2026-10-20)

Host: `native/monohost`, used by `selftest` (from the services step on) and by `m13 <hostdata> m2`. Each stub is keyed by the IC name after `::` (the `UTFConnector*` twins match too) and answers only while M2 is on; every other IC still fails loud. Runs: selftest three times (28/28 each, exit 0, with the M5 checks of docs/120 §8), plus one `m5concurrent` run. The game and Steam were not started. Claim ids I-119-45 to I-119-48 are in docs/51 §152.

| Stub | Answer | Tag |
|---|---|---|
| UI and callback registrations: `Animation_SetPMAAnimationFinishedCallback`, `UIManager_SetUICallbacksImpl`, `InitUIHierarchyImpl`, `InitUserToolHelperImpl`, `GetMainWindowImpl`, `LoadUIImageImpl`, `Simulator_AddObjectImpl`, `Command_RegisterCommandImpl`, `ThumbnailManager_SetCallbacks`, `DownloadContent_SetSimSavedCallbackImpl`, `UserToolUtils_SetBuildBuyGenericCallbackImpl`, `GameUtils_GetCommandLineSwitch` | typed zero (no-op, `false`, handle 0) | verified (run) |
| `StopWatch_CreateStopwatch` | 1, 2, 3, ... (distinct non-zero handles); the other 10 `StopWatch_*` ICs return typed zeros | verified (run) |
| `NameGuidMapService_GetGuidByName` | FNV-1a 64 of the name's UTF-16 code units (the string argument is found by its vtable's class = `System.String`) | verified (run); not the game's GUIDs |
| `LocalizedStringService_GetLocalizedStringByString(Key)` | the key string itself (non-null placeholder) | verified (run) |
| `Simulator_LoadXmlAsDocument` | `null`. An empty `XmlDocument` made `Sims3.UI.BlueprintController` and `BuyController` throw `NullReferenceException` (they test for `null`, then read `DocumentElement`); with `null` they initialise. No tuning XML is extracted yet | verified (run) |
| `Random_NextDoubleImpl` | seeded 64-bit LCG (fixed seed), top 53 bits in [0, 1), returned on the x87 stack | verified (run); not the game's stream (docs/27) |

With M2, all 36 service objects construct (32 strict without it).

**`.cctor` sweep, strict + M2 stubs** (9,840 types, 14 open generics skipped; three runs, same counts):

| Mode | ok | exception | fault | types reaching a stub |
|---|---|---|---|---|
| strict (§10.3) | 9,771 (+1 ok-stubs) | 54 | 0 | 45 |
| lenient (§10.3) | 9,772 (+19 ok-stubs) | 35 | 0 | 47 |
| **strict + M2** | **9,822** | **4** | 0 | 47 |

- **`Sims3.Gameplay.Autonomy.Autonomy`, `Motives` and `AutonomyTuning` initialise** (check `M2-autonomy-cctors`: `RunClassConstructor` again after the sweep returns without an exception). `AutonomyTuning` needed `UIManager_GetMainWindowImpl` as well (reached only once `UIManager`'s `.cctor` gets past `SetUICallbacksImpl`).
- **The 4 left** all throw `NullReferenceException` inside their own `.cctor`: `Sims3.Gameplay.Actors.Sim+GiveGift`, `Sim+AskForHand`, `Sims3.Gameplay.Academics.AskAboutDegree` (no IC reached) and `EnquireAboutGrade` (after the string stub). None is on the autonomy path. Cause not traced; most likely a service the game builds in managed start-up code, such as `Sims3.UI.LocalizedStringService` (§10.1: built by managed code, not by Init), still null in the host [inferred].
- Stub answers in one selftest: zero 28, stopwatch 6, GUID 18, string 240, `LoadXmlAsDocument` 3, random 8.
- Per-type rows: `out\monohost.exe.sweep-m2.csv` (not committed; regenerate with the selftest).

Selftest checks added: `M2-autonomy-cctors`, `M2-sweep` (0 faults, <= 20 exceptions); `M1.2-services` now wants 36 with M2. Not done: real tuning XML (extract the XML resources from the packages and serve them by name), real GUIDs (record from a game session, M3), the game's RNG stream.
