# Patch points: every patch, detour and hook we use or tested

All native addresses are for the Steam 1.67 `TS3W.exe` (SHA-256 `10b2423e2368e161774462b0ef1960231ad0af298d564a7bb6a6e78c5e880609`, base `0x400000`). Expected bytes are what our code checks before it writes; every site whose bytes differ is skipped and logged (`ts3native.log`), so another mod that got there first makes ours stand down instead of crashing. The bytes below were re-checked against the exe when this file was written.

Status words: **on** (installed and enabled by default), **opt-in** (built, off by default; turn on in the ini or with a runtime switch), **paused** (built, kept off after a failed in-game test), **test-only** (probe/profiler, never shipped to players).

Names link to [native-symbols.md](native-symbols.md); why each failed approach was dropped is in [dead-ends.md](dead-ends.md).

## 1. ts3native (native ASI plugin, `native/ts3native`, version 0.9.0)

Loaded by Ultimate ASI Loader before the exe's entry point, so the GC patches land before `GC_init` runs. It refuses to do anything unless the host is `TS3W.exe` at `0x400000`. Installer entry `native-patches` is **off by default** until the milestone run; inside it, only `[GC]` is enabled in the shipped `ts3native.ini`. Detours are 5-byte `jmp` over a verified prologue; the trampoline runs the copied prologue, then jumps back. Counters are exported as `ts3native_counters` (layout in [structs.md §15](structs.md)).

### 1a. Immediate and data patches

| Patch | Address | Expected bytes | What it changes | ini / default | Status | Doc |
|---|---|---|---|---|---|---|
| GC initial heap | `0xE4E168` (imm at +1) in `GC_init` | `BD 00 28 00 00` (`mov ebp,0x2800`, 40 MB in 4 KB blocks) | Initial Boehm heap 40 -> `InitialHeapMB` (352). Too large for one free block makes `GC_init` exit, so the value is capped | `[GC] Enabled=1 InitialHeapMB=352` | on in the ini (forced GC 13.9 -> 0.0 ms/s, hitches -14%) | [15 Phase 1](../15-native-patches.md), [13](../13-gc-tuning.md) |
| GC growth step 1/3 | `0xE4A1D8` (imm at +1) in `GC_collect_or_expand` | `3D 00 08 00 00 76 2C` | 8 MB growth clamp -> `GrowthStepMB` (32); all three sites or none | `[GC] GrowthStepMB=32` | on in the ini | [15 Phase 1](../15-native-patches.md) |
| GC growth step 2/3 | `0xE4A1FF` (imm at +1) | `3D 00 08 00 00 77 05` | same | same | on | same |
| GC growth step 3/3 | `0xE4A206` (imm at +1) | `B8 00 08 00 00 50` | same | same | on | same |
| GC budget ceiling | `0x1192324` (.data) | `DC 05 00 00` (1500) | Per-frame GC budget ceiling | `[GC] BudgetCeiling=0` (0 = leave alone) | opt-in, not measured | [13](../13-gc-tuning.md) |

### 1b. Detours and call retargets

| Patch | Address | Expected bytes | What it does | ini / default | Status | Doc |
|---|---|---|---|---|---|---|
| `Route_PlanLocked` detour | `0x82C6E0` | `81 EC D8 00 00 00` (`sub esp,0D8h`; trampoline jumps back to `0x82C6E6`) | One hook for three features: negative cache of failed plans (strict request key), route worker (the original runs on our thread while the caller waits), search budget. `__thiscall`, 10 stack dwords, `ret 28h`, result in AL | `[RouteCache]`, `[RouteWorker]`, `[RouteBudget]`, all `Enabled=0` | cache opt-in (not run in game); worker mode 1 works but no gain (paused); budget paused (41 stuck vs 0) | [15 Phase 2-4](../15-native-patches.md), [18](../18-route-search-off-thread.md) |
| Route cost function sites (budget) | `0x676340`, `0x67634A`, `0x676354`, `0x67635E`, `0x676368` | `C7 44 24 60` + imm32 = `0x674E40`, `0x673850`, `0x674250`, `0x675800`, `0x6734E0` | Each imm is swapped for a stub that calls the original cost function, then returns the `1e9` wall once the per-thread budget is spent, so the A* fails through its own "open list empty" exit | `[RouteBudget] MaxUs=100000 MaxNodes=0` | paused | [15 Phase 4](../15-native-patches.md) |
| Host delegate call | `0xD75FA0` | `55 8D 6C 24 98 81 EC 84 00 00 00` | Calls the planner makes into script from the worker run whole on the waiting caller thread (EA's call clears the running task id at host+0xBD0, which broke the sim thread's task) | with `[RouteWorker]`/`[RouteAsync]` | follows the worker | [18 "Crash: async route assert"](../18-route-search-off-thread.md) |
| runtime_invoke slot | `[0x1195B60]`, expected value `0xE5A600` with prologue `81 EC 50 01 00 00 68 50 01 00 00` | (pointer swap, not code) | Managed calls made on the worker go back to the caller thread (the worker is not a Mono or Boehm thread) | installed on the first worker plan | follows the worker | [15 Phase 3](../15-native-patches.md) |
| `Lock_Acquire` | `0x4E16F0` | `53 55 56 8B F1 80 7E 24 00` | During an async job, a blocking wait on the sim thread pumps the job's managed calls instead of sleeping (no deadlock against the worker holding world+0x918) | `[RouteAsync] Enabled=0` | paused | [18 "Async plan"](../18-route-search-off-thread.md) |
| `IC_Route_Plan` | `0x765160` | `0F 57 C0 8B 0D C4 40 1D 01` | Hands a finished async result to the script's own `Route.Plan` call | `[RouteAsync]` | paused | same |
| `IC_Route_GetOption` | `0x74D040` | `8B 0D C4 40 1D 01` | Handshake on unused RouteOption bit 58: START / READY / CANCEL / NOYIELD | installed whenever ts3native loads (NOYIELD guards ts3fixes' other yields); START says no unless `[RouteAsync]` is on | on with the plugin | same |
| Save step call sites | `0x742A95` -> `0xAB1030`, `0xAB10F6` -> `0xAAE950`, `0xAB12AF` -> `0xAAC110`, `0xAB134E` -> `0xAB0F80`, `0xAB1363` -> `0xAB0E80`, `0xAB1410` -> `0xAAF5F0` | `E8` rel32 to the listed target (all six or none) | Times every save step; `Mode=1` copies the old save to a temp folder on a worker during the package write and renames it to `.backup` (hash checks, else the game's own copy) | `[SaveWrite] Enabled=0 Mode=1` | opt-in, not run in game | [15 Phase 7](../15-native-patches.md) |
| RefPack decoder | call at `0x4EC065` (to `0x4EB3B0`) | `E8 46 F3 FF FF` | The only call to the decode core goes to `RefpackFast` (same output, 2.2x a C port of the game's decoder). The core's own bytes are left alone, so S3SS's decoder still runs in mode 0 | `[Decompress] Enabled=0 Mode=1 Verify=0`; runtime `decompressMode` | opt-in; gain small (0.24% of sim time) | [15 Phase 6](../15-native-patches.md) |
| EA allocator cache | `0x4E69D0` Malloc, `0x4E6A20` Memalign, `0x4E5920` Free; `0x4E5CB0` Shutdown | `56 57 8B F9 8B B7 E4 04 00 00` (all three); Shutdown `55 8B EC 83 EC 14` | Per-thread, per-heap cache of freed 16-1024 byte chunks, no lock and no core on a hit; chunks of a dead heap are dropped | `[Alloc] Enabled=0 ThreadKB=64 MaxMB=4 PerBin=32`; runtime `allocMode` (+0x108) | opt-in; in-load A/B BP +19% it/s, hitches -28% (repeat with `--taskstall` before default on) | [15 Phase 8](../15-native-patches.md) |
| Water JPS jump | call at `0x6207FB` (to `0x61FE10`) | `E8` rel32 to `0x61FE10`, plus FNV-1a `0x5A2C1E2B` of `0x61F750..0x61FF93` and `0x585FD97D` of `0x66DC50` (0x60 bytes) | The search's only outside call of the jump goes to `WaterJump`, the same jump as one loop (3.9x faster offline, identical results on 43.9M jumps) | `[WaterJps] Enabled=0 Mode=1 Verify=0`; runtime `waterJpsMode` (+0x134) | opt-in, not run in game | [15 Phase 9](../15-native-patches.md) |

## 2. ts3fixes (player mod, managed method replacements)

`mods/ts3fixes` replaces game methods at startup: it copies the replacement's `MonoMethod` header over the game method's, before the interpreter has compiled callers (mid-game swaps miss them). A stub keeps the original callable. One `[Tunable]` per fix; the test agent can switch most fixes per session (`patches {name:false}`). The full table with hook classes and files is in [managed-methods.md](managed-methods.md#ts3fixes-method-replacements).

| Target method | Switch | Default | Conflicts |
|---|---|---|---|
| `Sims3.Gameplay.Core.GoHome.Run` | `gohome` | on | none known |
| `Sims3.Gameplay.Abstracts.RabbitHole.RouteNearEntranceAndEnterRabbitHole` | `rabbithole` | on | none known |
| `Sims3.Gameplay.Core.VisitCommunityLot.Run`, `VisitLot.Run`, `GoToLot.Run`, `GoToCommunityLot.Run` | `visit`, `visitlot`, `gotolot`, `gotocommunity` | on | none known |
| `SimRoutingComponent.WalkStyleComparer.Compare` | (tuning only) | on | none known |
| `Sims3.Gameplay.Autonomy.ReactionBroadcaster.PulseSims` | `broadcast` | on | none known |
| `Sim.GoToVirtualHome.GoToVirtualHomeInternal.Run` | `virtualhome` | on | none known |
| `SimRoutingComponent.SetRouteOptions`, `GetRouteFailPath` | `failreplan` | on | none known |
| `SimRoutingComponent.CheckAndUpdateRouteForStations` (static) | `stations` | on | none known |
| `ScriptCore.Route.Plan` | `backoff`; async handshake with ts3native | on (async off) | Route_GetOption detour (ts3native); NRaas Overwatch attaches to the plan callbacks, not this method |
| `NRaas.CommonSpace.Helpers.SimEx.IsPointInLotSafelyRoutable` (Overwatch) | `overwatchstuck` | on | Hooks NRaas code: re-applied at world load; breaks if Overwatch changes the signature |
| `Sims3.Gameplay.Routing.PortalAnimateThroughAction.DoPortal` | `portaladaptorleak` | on (untested in game) | House Boat Fix hooks the houseboat callback, not this method |
| `Sims3.UI.CAS.CASHair.PopulateTypesGrid`, `CASMakeup.PopulatePartsGrid` | `casgrid`, `casmemo` | off | Smooth Patch (managed, `ld_SmoothPatch`) replaces no method [verified, docs/21] |
| `Sims3.SimIFace.Queries.GetObjects(Type, ulong)`, `Simulator.DestroyObject` | `spatialcache` | off | none known |
| `Sims3.Gameplay.Actors.Sim.IsPointInLotSafelyRoutable` | `routablecache` | off | none known |
| `LotManager.RunStuckCheckHelper.RunStuckCheck` | `stuckcheckbusy` | off | none known |

`tools/setup/check-conflicts.ts` reports 0 method clashes for our install. Another MonoPatcher-style mod that replaces one of these targets would win or lose by load order; the `ts3fixes.applied` list shows which of ours took effect.

## 3. Probe, profiler and test hooks (test-only)

All Frida `Interceptor.attach` hooks (read-only, no behaviour change); each checks the first bytes and refuses a prologue that someone else already detoured.

| Tool | Address | Expected first bytes | What it measures |
|---|---|---|---|
| `tools/frida/stutter-profile.js` `simulate` | `0xD81840` | `81 EC 88 00 00 00 53 55` | Per sim iteration time, hitches |
| `scriptTick` | `0xD80BC0` | `55 8B EC 83 E4 F8 81 EC` | Script tick stage |
| `taskObjects` | `0xD7FC60` | `55 8B EC 83 E4 F8 83 EC` | TaskObjects stage |
| `oneShot` | `0xD74800` | `83 EC 10 53 55 56 57 8B` | OneShotObjects stage |
| `threaded` | `0xD7F8B0` | `55 8B EC 83 E4 F8 81 EC` | ThreadedObjects stage |
| `catchUpSteps` | `0xD70730` | `83 EC 1C 53 55 56 57 89` | Catch-up steps |
| `gcTryToCollect` | `0xE4A050` | `83 3D 94 30 1F 01 00 74` | Per-frame GC |
| `gcStoppedMark` | `0xE49980` | `51 E8 1A 78 00 00 A1 60` | Stop-the-world marks (forced GCs) |
| `gcInvokeFinalizers` | `0xE697F0` | `E8 EB 1B FE FF 85 C0 74` | Finalizer drain |
| `routeOuter` | `0x82C6E0` | `81 EC D8 00 00 00 53 56` | Locked plan (with ts3native loaded it times the detour in `ts3native.asi` instead) |
| `routeSearch` | `0x81B2A0` | `55 8B EC 83 E4 F0 81 EC` | RoutingJob Execute |
| `Route_Plan`, `Route_Replan`, `Route_PlanFromPoint`, `Route_ReplanFromPosition`, `Route_IsPointRoutable` | `0x765160`, `0x765490`, `0x765240`, `0x765550`, `0x7538C0` | `0F 57 C0 8B 0D C4 40 1D`, `8B 0D C4 40 1D 01 83 EC`, `55 8B EC 83 E4 F0 0F 57`, `55 8B EC 83 E4 F0 8B 0D`, `55 8B EC 83 E4 F0 8B 0D` | Plans by script entry point (`Route_Plan` is refused when the ts3native async detour sits there) |
| `tools/probe/agent.js` `/taskstall` | `0xD7FC60`, `0xD74800`, `0xD7F8B0` | as above | Stage stalls; reads the current task (host+0xBD0/+0xBC8) and the managed stack. Never together with the managed sampler (two samplers pausing the sim thread deadlocked the agent) |
| `/routelock` | `0x82C6E0`, `0x4E16F0`, `0x4E17B0`, `0x83CED0` | (attach) | Who holds world+0x918, wait/hold times, plan overlap |
| `/interp`, `/managedstack`, managed sampler | reads `[0x11F4160]` (TLS index), `[0x11F4170]` | (read only) | C# stack from interpreter frames |
| `/rescache`, `gc-read.mjs` | `[0x11C78D8]` (manager, vtable `0xFFE250`/`0xFB2DA0`); `0x12225A0`, `0x12225B4`, `0x12225C4`, `0x11F2F90`, `0x11922A4` | (read only) | Resource cache sizes; GC heap, free, allocation, collections, budget |
| `tools/harness/frame-times.js` (test B) | `0xEC9F00` | `83 EC 20 56 E8 B7 43 74` | Frame times (`present = 1` calls only) |

## 4. Third-party patches on the same exe (conflict map)

| Mod | Patch | Address | Overlap with ours |
|---|---|---|---|
| S3SS | Smooth Patch (Original) | `0x4E1320` (SleepEx wrapper) | none; same site as LazyDuchess `TS3Patch.asi`, so run only one of the two |
| S3SS | Smooth Patch (Precise) | call `0xD81FDE` (IdleSimulationCycle), limiter `0xEC9FBA` | inside `Simulate` and the frame function; our hooks are on other bytes |
| S3SS | Chunky Patch (disable per-frame GC) | `0xD819AA` | different site from ts3native GC; changes what the profiler's `gcTryToCollect` sees |
| S3SS | Finalizer Throttle | `0xD81A1D`, `0xD81A37` | none |
| S3SS | GC_stop_world optimization | `0xE511F5` | none |
| S3SS | RefPack decompressor | `0x4EB3B0` (core replaced) | ts3native retargets the call at `0x4EC065` instead, so both coexist; mode 0 runs S3SS's decoder |
| S3SS | Oversized thread stack fix | `0x4DB355` | none |
| S3SS | CreateFileW random access | `0x4DB56C` region | none |
| S3SS | Mimalloc | MSVCR80 `malloc`/`free` | none: EA's GeneralAllocator (`0x4E4800-0x4E63E6`) imports nothing from MSVCR80 |
| S3SS | Lot streaming | `0xAC1130`, `0xC6D570` | none |
| S3PP 2.1 | SimulatorStageProfiler | `0xD7FC60`, `0xD74800`, `0xD7F8B0`, `0xD80BC0` (detours) | same prologues as our profiler and `/taskstall`: turn it off in `Sims3Performance.ini` while profiling (our byte check refuses detoured prologues) |
| S3PP 2.1 | MonoTypeEHashProbe | `0xEBBDE0`, `0xEBBF00`, `0xEBBFE0`, `0xEBC060` | none |
| S3PP 2.1 | MonoIsinstCache | `0xEA3820` | none |
| S3PP 2.1 | LocalizedStringCache | `0x59C5B0` (calls `GC_add_roots 0xE4D9F0`) | none with ts3native GC sites; label check PASS |
| S3PP 2.1 | StblBucketLayoutTuning | `0x59AF50`-`0x5A0xxx` | none |
| S3PP 2.1 | AllocatorArenaTuning | `0x4E49E5`, `0x4E4A1C`, `0x4E5BDA`, `0x4E5BE4` (operands at +6) | different functions from the ts3native allocator detours |
| S3PP 2.1 | ThreadStackDiet, ShortWaitSpin | | kept off: they duplicate S3SS |
| MonoPatcher 0.3.0 | Script host init hook, IL swap, ForceJIT | `0xD7E560`, `0xE5CA90`, `0xE5637D`, `0xE648DC`, `0xE59218` | none native; managed replacements by other mods can fight ours (section 2) |
| NRaas (ErrorTrap, Overwatch) | managed only | | Overwatch's routability helper is hooked by ts3fixes (`overwatchstuck`) |
| Smooth Patch (LazyDuchess, managed `ld_SmoothPatch`) | managed only, replaces no method | | none |

Sources: [docs/03 §1](../03-prior-art-re.md) (S3SS anchors), [docs/17 §8](../17-heavy-store-content.md) (S3PP), [docs/06](../06-test-harness-prior-art.md) (MonoPatcher), [docs/21](../21-cas-offload.md) (Smooth Patch), [docs/04 F19](../04-re-findings.md) (frame hook).
