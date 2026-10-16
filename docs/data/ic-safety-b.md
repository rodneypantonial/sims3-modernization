# IC thread safety, part b (T-IC-b): method note for `ic-safety-b.csv`

Static only: Ghidra headless on a private copy (`game-copy/ghidra-icb`, copied from `ghidra-ui4`, `tools/ghidra/symbols/ts3w-1.67.symbols.csv` imported first). The game was not run. Steam TS3W.exe 1.67, image base `0x400000`.

## Scope

1,073 bindings (1,035 distinct native bodies; identical bodies are folded, so some rows share an address) from `game-copy/analysis/internal-calls-map.csv` (`tools/internal-calls-map.ts`). Every class **except** the T-IC-a set: `World`, `Simulator`, `Objects`, `Route`, `RouteManager`, `RouteEvent`, `Queries`, `Animation`, `SACS` (state machines), `LookAtTask`, `LookAtMgr`. `Slots` (16) is covered here although it is object-adjacent; its rows say "boundary class".

Classes covered: CASUtils 205, SocialFeatures 205, UIManager 110, GameUtils 86, DownloadContent 61, UserToolUtils 66, CameraController 44, ThumbnailManager 39, OnlineFeatures 39, StreamHost 27, Audio 25, DeviceConfig 22, VisualEffect 20, LoadSaveManager 17, Slots 16, VideoRecorder 13, StopWatch 11, EATrace 8, LocalizedStringService 7, DataFactory 7, CacheManager 7, ProfilerUtils 5, DebugDraw 5, CommandSystem 5, EAText 4, CTProductModularObject 4, AutomationUtils 4, and seven classes with 1-2 ICs.

## Verdicts

The question is what a second managed thread (docs/30 `[MonoThread]`) may do with each IC. Today every IC runs on the sim thread, because Mono runs only there (docs/29, docs/43).

| Verdict | Meaning | Count |
|---|---|---|
| SAFE | No global or object-field writes, no unresolved virtual call, no script-handle lookup, no lock, no post in the closure. Mostly empty stubs (93 bodies of 8 bytes or less) and getters of init-time config. A managed allocation still needs docs/30 P3 | 151 |
| PROXY | Reads or writes sim-owned native state with no covering lock, or acts on an object behind a script handle (the handle table is locked by `g_ScriptHandleLock`, the object is not). Forward to the sim thread: synchronous for getters, queued for setters | 877 |
| UNSAFE | Yields or waits on the calling script task (host vtable `+0x11C` MarkYield, `+0x120` wait, `ScriptHost_MarkYield 0xD70F60`), reads the current task id (host `+0x118`), or calls back into managed code synchronously (`mono_runtime_invoke`, `ScriptHost_InvokeDelegate`, `World_FireEvent`). Also Mono reflection (docs/30). A worker must not call these; a forwarded call would have to run inside a sim-thread script task | 41 |
| MAIN | Device work on the calling thread with no owner check (`Texture_Create2D`, `Texture_LockRect`, `Device_ApplyGammaRamp`, `ThumbnailManager_CreateThumbnailTexture`). This is already a race from the sim thread (docs/43). From a worker, use the sim path as today; the real fix is a hand-off to the main thread | 4 |

Confidence: high 200, medium 336, low 537. "low" almost always means the work happens behind virtual calls the scan cannot follow (see Limits). A found path to a yield, callback or device call is positive evidence, so UNSAFE and MAIN rows are medium or high.

Notable rows:
- **MAIN:** `CASUtils_SimBuilderShareOutfitImpl` and `DownloadContent_SaveCustomSim` (thumbnail texture, docs/43), `CASUtils_AddObjectToBabyMakerSceneWindowImpl` and `CASUtils_IsSceneLoadedImpl` (`Texture_Create2D`/`LockRect`, `Device_ApplyGammaRamp`).
- **UNSAFE that are also MAIN hazards** (13 rows, the note says "MAIN hazard too"): all 12 `ThumbnailManager_Generate*`/`StitchPhotoboothPictures`/`UpdateDirtyLotThumbnails` bodies and `LoadSaveManager_SaveWorld` create the thumbnail texture on the calling thread **and** yield the task.
- **UNSAFE, task context:** CAS `SimBuilderBuildModel`/`ProcessModel`/`ClearParts`/`ResetAnimation`/`InitScene`, `SetOutfitInGameObject(Delayed)` (post plus task wait, docs/42), `EventQueue_Create/ReleaseEventQueue`, `GameUtils_WaitFor*`, `StartupYield`, `OnlineFeaturesHost_WaitForDownload`, `UIManager_CaptureSceneAsImage`.
- **UNSAFE, synchronous script callbacks:** `UIManager_GridGenericImpl` (InvokeDelegate), `LoadSaveManager_LoadGame_Impl`, `Slots_CanAttachToSlot(WithOffset)`, `DownloadContent_ImportLotContentsImpl` and four `UserToolUtils` ICs (`World_FireEvent`).
- **PROXY with a real lock:** SocialFeatures (`0x105E578`, `0x105E7C8`), DownloadContent (`0x10468AC`), ThumbnailManager (`g_ThumbnailManagerLock`), resource fetches (`g_ResourceLock` via `ResourceMan_*`). The lock covers that service's list, not the callers' objects; they stay PROXY.
- **Corrected by hand** (overrides, the note says "scan said ..."): the four `Audio_*Sound*` ICs matched host `+0x118` but the call is on the audio object [verified decompile of `IC_Audio_StartSoundImpl 0x863B60`]; they are PROXY (audio command buffer, sim-side producer, docs/35). `Reflection_*` set to UNSAFE from docs/30.

## Method

1. `tools/ghidra/ICSafety.java <ics.csv> <out.tsv> 6`: per IC body, BFS over direct calls and tail jumps to depth 6 (cap 4,000 functions). It records the first path to each flagged function:
   - thread checks: `GetSimThreadId 0x7D99F0`, `GetMainThreadId 0x7D99D0`, `Device_IsOwnerThread 0x60E820`, reads of `0x11D8A88`/`0x11D8A8C`, `GetCurrentThreadId`;
   - posts: `0x7D98C0` dispatch, JobManager submit/wait `0x59A350`/`0x59A500` and `[0x11CE2CC]`, `PostRenderMessage 0x692F70`, `RenderMessageBuffer_Post 0x593E60`, `RemoteCall_Post*`, `Lot_PostRemoteCall*`, `PhysicsSimMsg_ctor`, `EASemaphore_Post`, `UTFWin_QueuePendingTexture`, `SetEvent`/`PostMessage*`, and MessageServer (`MessageServer_Get 0x44C470` / `g_MessageServer`) virtual slots `+0x18`..`+0x24`;
   - locks: every call of `Lock_Acquire 0x4E16F0` / `(Try)EnterCriticalSection`, with the lock global found in the 8 instructions before the call ("member-lock" when it is a field);
   - managed: `mono_runtime_invoke 0xEA1400`, interpreter invoke `0xE5A600`/`0xE59FD0`, `ScriptHost_InvokeDelegate`, `InvokeDirect_A/B`, `NativeToManaged`, `ScriptHost_RunTask`, `World_FireEvent 0x78F2B0`;
   - allocations: `mono_string_new`, `mono_array_new`, `mono_object_new`, the object paths `0xEA2690`/`0xEA2710`/`0xEA3140`, `GC_allocobj`, `GC_alloc_large`; "mono-runtime" = any other call into `0xE40000`-`0xEB0000`;
   - device: `Device_*`, `Texture_*` (not `Texture_ClampToCaps`), `VertexBuffer_*`, `IndexBuffer_*`, `ThumbnailManager_CreateThumbnailTexture`. `GraphicsDevice_Get` alone only reads the device pointer and is not MAIN.
   - Script host virtual calls: in a function that calls `ScriptHost_GetWorld 0x7692B0` or reads `g_ScriptHostForWorldEvents 0x11D4BBC`/`g_MonoScriptHost 0x11EE514`, each `call [reg+disp]` (also through a `mov reg,[reg+disp]` just before) is read as a slot of the MonoScriptHost vtable `0x108B690` [verified DumpPtrs: `+0xC8` ScriptHandles_GetOrCreate, `+0xCC` HandleToPtr, `+0x118` task id `0xD70E90`, `+0x11C` MarkYield, `+0x120` wait `0xD77F30`, `+0x134` InvokeDelegate]. Slots `>= +0x48` are followed as call edges; lower ones are recorded only (they collide with other objects' calls, and `+0x10`/`+0x20` are Init/Simulate).
   - Globals: data references into writable non-code blocks, split read/write. `hw` counts stores through non-stack pointers (object fields, out parameters).
   - Stop points (recorded, not descended): allocator, `ResourceMan_*`/`ResourceCache_*` (taken as `g_ResourceLock`, docs/39), Mono/GC range, device functions, posts, managed entries.
2. Rules, in order: yield/task wait or RunTask -> UNSAFE; managed callback -> UNSAFE; host `+0x118` task id -> UNSAFE; device call without owner check -> MAIN; nothing but allocator/atomic/Mono flags, no writes, no stores, no unresolved virtual calls, no handle lookup, no lock, no post -> SAFE; everything else -> PROXY. Then the hand overrides above and doc notes (docs/35, 42, 43, 49, 54).
3. Confidence: high when the closure has no unresolved virtual call and at most 40 functions; medium with up to 4; low otherwise or when the cap was hit.

## Limits

- **Virtual calls are not followed** except the script host and MessageServer slots. Most UIManager, Camera, Audio, VisualEffect and CAS ICs look up a handle or a service singleton and do the work through its vtable, so "no flag" means "none in the direct-call graph". This is why most PROXY rows are low confidence; none of them was upgraded to SAFE on that basis.
- The host-slot match is per function, not per register: a `+0x118`/`+0x11C`/`+0x120` call on another object in the same function is a false positive (the Audio case). The other UNSAFE task rows were checked against docs/42 or the decompile (EventQueue: the call is on the host object [verified]); the thumbnail and Wait rows are [inferred from the scan path].
- `hw` treats out-parameter stores as writes, so getters with out parameters land in PROXY. This only errs toward PROXY.
- "SAFE" is about the native side. A worker also needs docs/30 P1-P4 for any managed allocation or interpreter frame, and copied input.
- Unbound helper bodies and ICs folded with others carry every binding's name in the note ("body shared by N ICs").
