# Native symbols (TS3W.exe, Steam 1.67)

Every native address the project has named, grouped by subsystem. All addresses are virtual addresses in the Steam 1.67 `TS3W.exe` (SHA-256 `10b2423e2368e161774462b0ef1960231ad0af298d564a7bb6a6e78c5e880609`, image base `0x400000`, no ASLR, Large Address Aware). They do not apply to the disc/EA App build. The names are ours (or, where noted, community names from S3SS, S3MM and MonoPatcher); EA shipped no symbols.

The same list, plus the 1,706 functions named after the script internal calls bound to them, is in [`tools/ghidra/symbols/ts3w-1.67.symbols.csv`](../../tools/ghidra/symbols/ts3w-1.67.symbols.csv). [`tools/ghidra/ImportSymbols.java`](../../tools/ghidra/ImportSymbols.java) applies it to a Ghidra project (see [README](README.md)).

**Columns.** Kind: `fn` = function entry, `site` = an instruction inside a function (call site, patch site, assert), `data` / `vtable` / `str` = globals. Thread: `sim` = the simulation (script) thread, `main` = the main/render thread, `any` = called from several threads, `worker` = an engine worker thread; `?` = not confirmed. Source links to the doc where the address was found or verified.

**Check** (run against the exe bytes and our Ghidra database when this file was generated):
- `bytes`: the bytes listed in the docs or our code (a prologue, an instruction, a data value) match the exe at that address.
- `call`: the `E8` call at that address goes to the function named in the signature column.
- `fn`: Ghidra's analysis has a function starting there.
- `doc`: only our docs say so. Most `doc` rows are call sites, data globals and functions that Ghidra's analysis missed (it ran out of memory on two analyzers); they were read in decompiles or disassembly listings but are not checked by the generator.

**Known label fixes.** The first Ghidra pass labelled `0x81B2A0` as `Route_SearchAStar` and `0x661B50` as `RoutingJob_Init`. Both were wrong ([docs/11](../11-route-planner-re.md)): `0x81B2A0` is `RoutingJob_Execute` (vtable slot 1 of the RoutingJob), and `0x661B50` is the planner wrapper with retries. The world A* is `0x676260`. The symbol file uses the corrected names, and importing it renames the old labels.

**Internal calls.** Script methods marked `[DllImport("Sims3Common.dll")]` are resolved by name through `{fn, name}` tables in `.data` (for example `"ScriptCore.Route::Route_Plan"` -> `0x765160`). [`tools/internal-calls-map.ts`](../../tools/internal-calls-map.ts) extracts all 1,758 bindings (1,714 distinct addresses) from your own exe. Only the ones that matter for our work are listed below (prefix `IC_`); the CSV has all of them.

See also: object layouts in [structs.md](structs.md), every hook and patch in [patch-points.md](patch-points.md), the managed side in [managed-methods.md](managed-methods.md).

## Sim loop and stages

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x4E1320` | `Sleep_Wrapper_SmoothPatchTarget` | fn | cdecl(DWORD* ms) | any | Shared SleepEx(*ms, TRUE) wrapper (sim pacing, 20 ms = 50 TPS); patched by S3SS Smooth Patch and TS3Patch.asi | [03 §1.1](../03-prior-art-re.md) | bytes |
| `0x7694B0` | `ScriptHostBase_IdleSimulationCycle` | fn |  | sim | Tick pacing; S3SS Smooth Patch (Precise) redirects the call to it | [03 §1.1](../03-prior-art-re.md) | fn |
| `0xD70730` | `ScriptHost_RunCatchUpSteps` | fn |  | sim | Catch-up sim steps after a slow iteration (up to 10) | [04 F2](../04-re-findings.md) | bytes |
| `0xD74800` | `ScriptHost_OneShotObjects` | fn | thiscall | sim | OneShotObjects stage (map at host+0x16A0) | [04 F17](../04-re-findings.md) | bytes |
| `0xD7F8B0` | `ScriptHost_ThreadedObjects` | fn | thiscall | sim | ThreadedObjects stage (map at host+0x16C0) | [04 F17](../04-re-findings.md) | bytes |
| `0xD7FC60` | `ScriptHost_TaskObjects` | fn | thiscall | sim | Walks the host TaskObjects map (Mono Task objects: one-shot functions, immediate interactions, story progression, finalizers, load/save); stalls of 0.27-6.9 s seen | [04 F17](../04-re-findings.md) | bytes |
| `0xD80BC0` | `ScriptHost_ScriptTick` | fn | thiscall, 1 arg (n) | sim | Runs the script task tick; S3PP calls it ProcessTaskState | [04 F17](../04-re-findings.md) | bytes |
| `0xD81840` | `MonoScriptHost_Simulate` | fn | thiscall (host) | sim | Main sim loop, one iteration per tick: script tick, TaskObjects, OneShot/Threaded stages, GC step, finalizer drain, catch-up steps, locomotion and animation passes, idle cycle. Body 0xD81840-0xD82100; Ghidra analysis did not create it | [04 F2](../04-re-findings.md), [F17](../04-re-findings.md) | bytes |
| `0xD819AA` | `Simulate_call_GC_try_to_collect` | site | E8 rel32 | sim | Per-iteration GC step; S3SS "Chunky Patch" NOPs it | [03 §1.1](../03-prior-art-re.md), [13 Q3](../13-gc-tuning.md) | bytes |
| `0xD81A1D` | `Simulate_finalizer_200_frames` | site |  | sim | cmp 0xC8: after 200 frames with a full finalizer batch, drain all; S3SS Finalizer Throttle | [13](../13-gc-tuning.md), [03 §1.1](../03-prior-art-re.md) | doc |
| `0xD81A37` | `Simulate_finalizer_drain_loop` | site |  | sim | jnz back to mono_gc_invoke_finalizers; S3SS Finalizer Throttle NOPs it | [13](../13-gc-tuning.md), [03 §1.1](../03-prior-art-re.md) | doc |
| `0xD81A70` | `Simulate_anim_pass_SACS` | site |  | sim | Calls 0x571AD0 (SACS driver pass over animated actors) | [22](../22-parallel-animation.md) | doc |
| `0xD81C73` | `Simulate_anim_pass_update` | site |  | sim | Calls 0x571B90 (animation update pass) | [22](../22-parallel-animation.md) | doc |
| `0xD81FDE` | `Simulate_call_IdleSimulationCycle` | site |  | sim | S3SS Smooth Patch (Precise) redirect site | [03 §1.1](../03-prior-art-re.md) | doc |

## Script host, scheduler and tasks

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x5992D0` | `JobManager_ReadTimeslices` | fn |  | main | References the [Timeslices] names [inferred name] | [04](../04-re-findings.md) | fn |
| `0x599CF0` | `JobManager_JobThread` | fn | thread proc | JobThread%i | Engine worker pool thread; no routing jobs run here | [18](../18-route-search-off-thread.md) | fn |
| `0x893FB0` | `JobRunner_Loop` | fn |  | worker | Job loop; jobs: vtable+4 Execute, job[3] name, job[4] cancel | [18](../18-route-search-off-thread.md) | fn |
| `0xD70820` | `ScriptHost_Sleep` | fn |  | sim | Host sleep reached from the yield opcode (-> host vtable +0x20) | [18](../18-route-search-off-thread.md) | fn |
| `0xD70F60` | `ScriptHost_MarkYield` | fn | thiscall (this = host+8) | sim | Sets yieldReturn on the running task root state (vtable slot 0x108B7AC) | [18](../18-route-search-off-thread.md) | doc |
| `0xD717B0` | `ScriptHost_GcStopFunc` | fn | cdecl -> int | sim | Stop function for GC_try_to_collect: true when elapsed > budget 0x11922A4 | [13 Q3](../13-gc-tuning.md) | fn |
| `0xD726F0` | `ScriptHost_HandleMapLookup` | fn |  | sim | Script-host handle map lookup (2.4% of sim samples together with 0x939100) | [20](../20-offload-candidates.md) | fn |
| `0xD75FA0` | `ScriptHost_InvokeDelegate` | fn | thiscall (this = host+8), hidden out ptr + 3 args, ret 10h | sim | Host delegate call that native code uses to call script: boxes args, clears and restores the running task id host+0xBD0 around mono_runtime_invoke. Vtable slot 0x108B7C4. ts3native proxies it to the sim thread | [18](../18-route-search-off-thread.md) | bytes |
| `0xD76200` | `InvokeDelegate_clear_task_id` | site |  | sim | Zeroes host+0xBD0/+0xBD4 before the invoke | [18](../18-route-search-off-thread.md) | bytes |
| `0xD7620C` | `InvokeDelegate_call_runtime_invoke` | site |  | sim | call 0xEA1400 (mono_runtime_invoke) | [18](../18-route-search-off-thread.md) | bytes |
| `0xD7621F` | `InvokeDelegate_restore_task_id` | site |  | sim | Restores host+0xBD0/+0xBD4 | [18](../18-route-search-off-thread.md) | bytes |
| `0xD78760` | `ScriptHost_LoadImage` | fn |  | main | Name from S3MM/MonoPatcher signatures | [06](../06-test-harness-prior-art.md) | fn |
| `0xD79CA0` | `ScriptHost_FindClass` | fn |  | sim | Name from S3MM/MonoPatcher signatures | [06](../06-test-harness-prior-art.md) | fn |
| `0xD7DB80` | `ScriptHost_CreateMonoClass` | fn | thiscall(ns, cls) | sim | Instantiates a managed class from native code (MonoPatcher route) | [06](../06-test-harness-prior-art.md) | fn |
| `0xD7E010` | `MonoScriptHost_ctor` | fn |  | main | Names the TaskObjects/OneShotObjects/ThreadedObjects maps | [04 F17](../04-re-findings.md), [18](../18-route-search-off-thread.md) | fn |
| `0xD7E560` | `MonoScriptHost_Init` | fn | thiscall | main | InitializeScriptHost: Mono init, GC_enable_incremental; MonoPatcher hooks it to load plugins. Ghidra analysis did not create it | [06](../06-test-harness-prior-art.md), [13](../13-gc-tuning.md) | doc |
| `0xD7E602` | `ScriptHostInit_call_GC_enable_incremental` | site |  | main | Unconditional call 0xE4E450: incremental GC is always on | [13](../13-gc-tuning.md) | bytes |
| `0xD7E60C` | `ScriptHostInit_call_MonoRuntimeInit` | site |  | main | Only caller of the Mono runtime init chain (-> 0xE5A680) | [15 Phase 1](../15-native-patches.md) | doc |
| `0xD7EED0` | `ScriptHost_InvokeDirect_A` | fn |  | sim | Direct interpreter wrapper (not through runtime_invoke) | [15 Phase 3](../15-native-patches.md) | fn |
| `0xD7F000` | `ScriptHost_RunTask` | fn |  | sim | Resumes a task: sets host+0xBC8 (state slot) and +0xBD0 (task id), runs interpreted code until it yields or ends, then reads yieldReturn | [04 F6](../04-re-findings.md), [18](../18-route-search-off-thread.md) | fn |
| `0xD7F210` | `ScriptHost_InvokeDirect_B` | fn |  | sim | Direct interpreter wrapper (not through runtime_invoke) | [15 Phase 3](../15-native-patches.md) | fn |
| `0xD8A8E0` | `ScriptHost_NativeToManaged` | fn |  | sim | Another native->managed entry; runs on a fresh interpreter state (a yield there aborts) | [18](../18-route-search-off-thread.md) | fn |
| `0xD9B130` | `ScriptHost_GlueCaller` | fn |  | sim | Script-host glue that calls 0xD8A8E0 [inferred name] | [18](../18-route-search-off-thread.md) | fn |
| `0xDA5970` | `ScriptHost_ICallTrampoline` | fn |  | sim | Internal-call trampoline (~1.5% incl. its ntdll lock calls) | [20](../20-offload-candidates.md) | fn |
| `0x108B7AC` | `vt_MonoScriptHost_MarkYield_slot` | data |  |  | Vtable slot holding 0xD70F60 | [18](../18-route-search-off-thread.md) | doc |
| `0x108B7C4` | `vt_MonoScriptHost_InvokeDelegate_slot` | data |  |  | Vtable slot holding 0xD75FA0 | [18](../18-route-search-off-thread.md) | doc |
| `0x11D4BBC` | `g_ScriptHostForWorldEvents` | data |  |  | Script host used by world event delegates (vtable +0x134 delegate, +0x138 method) | [18](../18-route-search-off-thread.md) | doc |
| `0x11EE514` | `g_MonoScriptHost` | data | MonoScriptHost* |  | Script host singleton (ctor 0xD7E010); fields in structs.md | [18](../18-route-search-off-thread.md) | doc |

## Mono interpreter and runtime

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0xE52B40` | `interp_get_yieldReturn` | fn |  | sim | Reads pState+0x149 | [18](../18-route-search-off-thread.md) | fn |
| `0xE52B50` | `interp_set_yieldReturn` | fn |  | sim | Sets pState+0x149 | [18](../18-route-search-off-thread.md) | fn |
| `0xE52C70` | `interp_box_return` | fn |  | sim | Boxes a non-void return with mono_object_new(domain) (faults with domain NULL off-thread) | [15 Phase 3](../15-native-patches.md) | fn |
| `0xE52D70` | `interp_alloc_task_state` | fn |  | sim | Allocates the 0x150-byte interpreter state (pState) per task | [04 F6](../04-re-findings.md), [18](../18-route-search-off-thread.md) | fn |
| `0xE53EB0` | `ves_exec_method_with_context` | fn |  | sim | Interpreter main loop; ~29% of sim-thread samples (C# itself) | [04 F6](../04-re-findings.md), [20](../20-offload-candidates.md) | fn |
| `0xE5637D` | `interp_jit_cache_check_1` | site |  | sim | MonoPatcher ForceJIT patch site (90 90) | [06](../06-test-harness-prior-art.md) | doc |
| `0xE59225` | `interp_ret_more_values_print` | site |  | sim | xref of "ret: more values on stack" (MonoPatcher silences it at 0xE59218) | [03 anchors](../03-prior-art-re.md), [06](../06-test-harness-prior-art.md) | doc |
| `0xE593BB` | `interp_yield_nonyielding_check` | site |  | sim | xref of "Attempting to yield in a non-yielding context!" | [03 anchors](../03-prior-art-re.md) | doc |
| `0xE59751` | `interp_op_yield` | site |  | sim | EA yield opcode: host sleep, then assert yieldReturn on the current state | [18](../18-route-search-off-thread.md) | doc |
| `0xE59763` | `interp_yield_assert_a` | site |  | sim | interp.c:3595 pState->yieldReturn assert | [18](../18-route-search-off-thread.md) | doc |
| `0xE5976D` | `interp_yield_assert_b` | site |  | sim | Pushes 0x108E704 "pState->yieldReturn" | [18](../18-route-search-off-thread.md) | doc |
| `0xE597BA` | `interp_restore_cur_state_a` | site |  | sim | Restores [0x11F4170] on return | [18](../18-route-search-off-thread.md) | doc |
| `0xE5986C` | `interp_restore_cur_state_b` | site |  | sim | Restores [0x11F4170] on return | [18](../18-route-search-off-thread.md) | doc |
| `0xE59FD0` | `interp_mono_invoke_with_state` | fn |  | sim | Interpreter invoke entry (S3MM name); reads TLS [0x11F4160] | [04 F6](../04-re-findings.md), [06](../06-test-harness-prior-art.md) | fn |
| `0xE5A600` | `interp_runtime_invoke` | fn | cdecl(method, obj, params, exc) | sim | Interpreter runtime_invoke (interp.c), builds a fresh state; hooked through slot 0x1195B60 by ts3native | [15 Phase 3](../15-native-patches.md), [18](../18-route-search-off-thread.md) | bytes |
| `0xE5A680` | `mono_interp_init` | fn |  | main | Installs runtime_invoke; reached only from 0xD7E60C | [15](../15-native-patches.md) | fn |
| `0xE5CA90` | `mono_interp_GenerateCode` | fn | (MonoMethod*, ...) | sim | Transforms IL to interpreter code; MonoPatcher swaps header ilBegin/codeSize here | [06](../06-test-harness-prior-art.md) | fn |
| `0xE648DC` | `interp_jit_cache_check_2` | site |  | sim | MonoPatcher ForceJIT patch site (EB) | [06](../06-test-harness-prior-art.md) | doc |
| `0xE68590` | `mono_assembly_open_full` | fn |  | main | Embedding API (S3MM signature) | [06](../06-test-harness-prior-art.md) | fn |
| `0xE69650` | `thunk_GC_gcollect` | fn |  | sim | Jump to GC_gcollect | [13](../13-gc-tuning.md) | fn |
| `0xE6FA90` | `mono_class_from_name` | fn |  | any | Embedding API (S3MM signature) | [06](../06-test-harness-prior-art.md) | fn |
| `0xE70330` | `mono_class_get_method_from_name_flags` | fn |  | any | Embedding API (S3MM signature) | [06](../06-test-harness-prior-art.md) | fn |
| `0xE75260` | `mono_domain_get` | fn |  | any | TlsGetValue([0x11959DC]) | [06](../06-test-harness-prior-art.md), [15](../15-native-patches.md) | fn |
| `0xE786C0` | `icall_GC_InternalCollect` | fn |  | sim | System.GC.Collect icall (table 0x10B5838 index 121) -> 0xE69650 -> GC_gcollect | [13](../13-gc-tuning.md) | doc |
| `0xE82161` | `mono_version_string_xref` | site |  |  | xref of "Mono 1.2.3.1" | [03 anchors](../03-prior-art-re.md) | doc |
| `0xE82550` | `mono_add_internal_call` | fn | (name, fn) | main | Registers Namespace.Class::Method icalls | [06](../06-test-harness-prior-art.md) | fn |
| `0xE82680` | `mono_lookup_internal_call` | fn |  | any |  | [06](../06-test-harness-prior-art.md) | fn |
| `0xE84780` | `mono_image_open_from_data_full` | fn |  | main | Embedding API (S3MM signature) | [06](../06-test-harness-prior-art.md) | fn |
| `0xEA0FB0` | `mono_object_get_virtual_method` | fn |  | sim |  | [04 F17](../04-re-findings.md) | fn |
| `0xEA1080` | `runtime_invoke_not_supported` | fn |  | any | Stub in the slot before init | [15 Phase 3](../15-native-patches.md) | fn |
| `0xEA1290` | `mono_field_set_value` | fn |  | any | Embedding API (S3MM signature) | [06](../06-test-harness-prior-art.md) | fn |
| `0xEA1350` | `mono_field_get_value` | fn |  | any | Embedding API (S3MM signature) | [06](../06-test-harness-prior-art.md) | fn |
| `0xEA1400` | `mono_runtime_invoke` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0xEA1470` | `mono_install_runtime_invoke` | fn |  | main |  | [15 Phase 3](../15-native-patches.md) | fn |
| `0xEA2AD0` | `mono_class_vtable` | fn |  | sim | Faulted at 0xEA2B03 with domain NULL (route worker crash) | [15 Phase 3](../15-native-patches.md) | fn |
| `0xEA30F0` | `mono_array_new` | fn |  | any | Embedding API (S3MM signature) | [06](../06-test-harness-prior-art.md) | fn |
| `0xEA36F0` | `mono_object_new` | fn |  | sim | = mono_object_new_specific(mono_class_vtable(d, k)) | [15 Phase 3](../15-native-patches.md) | fn |
| `0xEA3820` | `mono_object_isinst` | fn |  | sim | S3PP MonoIsinstCache detours it | [17 §8](../17-heavy-store-content.md) | fn |
| `0xEA40B0` | `mono_string_new` | fn |  | any | Embedding API (S3MM signature) | [06](../06-test-harness-prior-art.md) | fn |
| `0xEA8720` | `mono_metadata_type_equal` | fn |  | sim | Type compare, 2.2% of sim samples | [17 §8](../17-heavy-store-content.md), [20](../20-offload-candidates.md) | fn |
| `0xEBBDE0` | `g_hash_table_lookup` | fn |  | sim | S3PP MonoTypeEHashProbe detour | [17 §8](../17-heavy-store-content.md) | fn |
| `0xEBBF00` | `g_hash_table_remove` | fn |  | sim | S3PP MonoTypeEHashProbe detour | [17 §8](../17-heavy-store-content.md) | fn |
| `0xEBBFE0` | `g_hash_table_destroy` | fn |  | sim | S3PP MonoTypeEHashProbe detour | [17 §8](../17-heavy-store-content.md) | fn |
| `0xEBC060` | `g_hash_table_insert` | fn |  | sim | S3PP MonoTypeEHashProbe detour | [17 §8](../17-heavy-store-content.md) | fn |
| `0x108E704` | `str_pState_yieldReturn` | str |  |  | "pState->yieldReturn" (assert text) | [18](../18-route-search-off-thread.md) | doc |
| `0x10B5838` | `g_corlibICallTable` | data |  |  | mscorlib icall table | [13](../13-gc-tuning.md) | doc |
| `0x11959DC` | `g_domainTlsIndex` | data | DWORD |  | TLS index read by mono_domain_get (NULL on non-Mono threads) | [15 Phase 3](../15-native-patches.md) | doc |
| `0x1195B60` | `g_runtimeInvokeSlot` | data | fnptr |  | runtime_invoke slot; holds 0xE5A600 after mono_interp_init (0xEA1080 before) | [15 Phase 3](../15-native-patches.md), [18](../18-route-search-off-thread.md) | doc |
| `0x1195B7C` | `g_monoThreadTlsIndex` | data | DWORD |  | MonoThread TLS | [15 Phase 3](../15-native-patches.md) | doc |
| `0x11F4160` | `g_interpTlsIndex` | data | DWORD |  | TLS index of the interpreter ThreadContext; the sim thread is the one with the slot set | [04 F6](../04-re-findings.md) | doc |
| `0x11F4170` | `g_curInterpState` | data | pState* |  | Innermost running interpreter state (saved/restored by 0xE53EB0) | [04 F6](../04-re-findings.md), [18](../18-route-search-off-thread.md) | doc |

## Boehm GC

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x4F30E0` | `Timer_Get` | fn | (mode) | any | Timer read with a mode argument [inferred] | [04](../04-re-findings.md) | fn |
| `0x4F32C0` | `Timer_Now` | fn |  | any | QPC/rdtsc timer used by the GC stop function; units probably us [inferred] | [13](../13-gc-tuning.md) | fn |
| `0xD70900` | `GC_win32_get_mem` | fn |  | any | Boehm heap memory comes from here, not from the EA allocator | [13](../13-gc-tuning.md) | fn |
| `0xE498B0` | `GC_min_bytes_allocd` | fn |  | sim | Only reader of the free-space divisor | [13](../13-gc-tuning.md) | fn |
| `0xE49980` | `GC_stopped_mark` | fn |  | sim | Stop-the-world mark; outside the per-frame call it counts as a forced GC | [13](../13-gc-tuning.md) | bytes |
| `0xE49B10` | `GC_heap_section_limit_a` | site |  | sim | Heap-section limit 0x300 ("Too many heap sections") | [13](../13-gc-tuning.md) | doc |
| `0xE49DA0` | `GC_collect_a_little_inner` | fn |  | sim | After 40 aborted attempts finishes with never_stop (unbounded pause) | [13](../13-gc-tuning.md) | fn |
| `0xE49F10` | `GC_collect_a_little_mark` | fn |  | sim | GC_mark_some x25 with stop checks [inferred name] | [13](../13-gc-tuning.md) | fn |
| `0xE49FB0` | `GC_collect_a_little_step` | fn |  | sim | One per-frame step (then 0xE49F10) [inferred name] | [13](../13-gc-tuning.md) | fn |
| `0xE4A050` | `GC_try_to_collect` | fn | cdecl(stop_fn) -> int | sim | Per-frame incremental collection with EA stop function 0xD717B0 | [04 F2](../04-re-findings.md), [13 Q3](../13-gc-tuning.md) | bytes |
| `0xE4A0B0` | `GC_gcollect` | fn |  | sim | Full collection, never_stop | [13](../13-gc-tuning.md) | fn |
| `0xE4A120` | `GC_collect_or_expand` | fn |  | sim | EA edit: in incremental mode collects before expanding; 8 MB growth clamp | [13](../13-gc-tuning.md), [15 Phase 1](../15-native-patches.md) | fn |
| `0xE4A128` | `GC_collect_or_expand_incr_jnz` | site | jnz (75 78) | sim | NOP for the stock growth policy (experimental, not done) | [13](../13-gc-tuning.md) | bytes |
| `0xE4A1D8` | `GC_growth_clamp_1` | site | cmp eax,0x800 ; jbe | sim | Growth step 8 MB (imm at +1); ts3native 32 MB | [15 Phase 1](../15-native-patches.md) | bytes |
| `0xE4A1FF` | `GC_growth_clamp_2` | site | cmp eax,0x800 ; ja | sim | Growth step 8 MB (imm at +1) | [15 Phase 1](../15-native-patches.md) | bytes |
| `0xE4A206` | `GC_growth_clamp_3` | site | mov eax,0x800 ; push eax | sim | Growth step 8 MB (imm at +1) | [15 Phase 1](../15-native-patches.md) | bytes |
| `0xE4A2B0` | `GC_allocobj` | fn |  | Mono threads | Small-object refill; forced collections start here when out of free blocks | [13](../13-gc-tuning.md) | fn |
| `0xE4A320` | `GC_try_to_collect_inner_never_stop` | fn |  | sim | Forced full collection path [inferred name] | [13](../13-gc-tuning.md) | fn |
| `0xE4B3F0` | `GC_invoke_finalizers` | fn |  | sim |  | [13](../13-gc-tuning.md) | fn |
| `0xE4B8D0` | `GC_hot_2` | fn |  | sim | GC hot function in samples [role inferred] | [20](../20-offload-candidates.md) | fn |
| `0xE4BA90` | `GC_alloc_large` | fn |  | Mono threads |  | [13](../13-gc-tuning.md) | fn |
| `0xE4C3E0` | `GC_mark_hot` | fn |  | sim | Hottest GC function in samples (6.1%) [role inferred: mark] | [04 F5](../04-re-findings.md), [20](../20-offload-candidates.md) | fn |
| `0xE4D270` | `GC_per_frame_path_a` | fn |  | sim | Per-frame GC path, caller of 0xE4C3E0 | [04 F5](../04-re-findings.md) | fn |
| `0xE4D460` | `GC_per_frame_path_b` | fn |  | sim | Per-frame GC path, caller of 0xE4C3E0 | [04 F5](../04-re-findings.md) | fn |
| `0xE4D9F0` | `GC_add_roots` | fn |  | any | S3PP LocalizedStringCache calls it | [17 §8](../17-heavy-store-content.md) | fn |
| `0xE4E160` | `GC_init` | fn |  | main (lazy, first GC use) | Reads GC_* env vars once (MSVCR80 getenv); guarded by GC_is_initialized | [13 Q1](../13-gc-tuning.md), [15 Phase 1](../15-native-patches.md) | fn |
| `0xE4E168` | `GC_init_initial_heap_imm` | site | mov ebp,0x2800 | main | Initial heap in 4 KB blocks (0x2800 = 40 MB); ts3native patches the imm at +1 | [15 Phase 1](../15-native-patches.md) | bytes |
| `0xE4E450` | `GC_enable_incremental` | fn |  | main | Sets GC_incremental and GetWriteWatch dirty bits | [13](../13-gc-tuning.md) | fn |
| `0xE4E4C0` | `GC_init_public` | fn |  | main | Init allocation lock, then jmp GC_init (callers 0xE66242, 0xE75C22) | [15 Phase 1](../15-native-patches.md) | fn |
| `0xE4EB70` | `GC_check_win32` | fn |  | main | Sets GC_no_win32 only on Win9x/Win32s | [13](../13-gc-tuning.md) | fn |
| `0xE4EEF0` | `GC_heap_section_limit_b` | site |  | sim | Heap-section limit 0x300 | [13](../13-gc-tuning.md) | doc |
| `0xE4EFB0` | `GC_gww_dirty_init` | fn |  | main | GetWriteWatch dirty-bit setup | [13](../13-gc-tuning.md) | fn |
| `0xE50380` | `GC_reclaim_all` | fn | (stop_fn) | sim | Lazy sweep, abortable | [13](../13-gc-tuning.md) | fn |
| `0xE51070` | `GC_new_thread` | fn |  | main | Thread table 0x1221000 (256 x 0x14) gets one entry | [15 Phase 3](../15-native-patches.md) | fn |
| `0xE511A0` | `GC_stop_world` | fn |  | sim |  | [13](../13-gc-tuning.md) | fn |
| `0xE511F5` | `GC_stop_world_thread_loop_cmp` | site | cmp eax,0x100 ; jl | sim | S3SS GC_stop_world Optimization site | [03 §1.1](../03-prior-art-re.md) | bytes |
| `0xE51290` | `GC_start_world` | fn |  | sim |  | [13](../13-gc-tuning.md) | fn |
| `0xE513D0` | `GC_thr_init` | fn |  | main | Only caller of GC_new_thread: one registered thread | [15 Phase 3](../15-native-patches.md) | fn |
| `0xE697F0` | `mono_gc_invoke_finalizers` | fn |  | sim | At most 50 finalizers per call (0x1194848) | [04 F2](../04-re-findings.md), [13](../13-gc-tuning.md) | bytes |
| `0x11922A4` | `EA_gc_frame_budget` | data | int |  | Per-frame GC budget, starts at 1000, +-100 steps | [13](../13-gc-tuning.md) | doc |
| `0x1192324` | `EA_gc_budget_max` | data | int |  | 1500; ts3native optional BudgetCeiling | [13](../13-gc-tuning.md), [15 Phase 1](../15-native-patches.md) | bytes |
| `0x11923A4` | `EA_gc_budget_min` | data | int |  | 500 | [13](../13-gc-tuning.md) | bytes |
| `0x11947E8` | `EA_gc_counter_min` | data | int |  |  | [13](../13-gc-tuning.md) | doc |
| `0x11947EC` | `EA_gc_counter_max` | data | int |  |  | [13](../13-gc-tuning.md) | doc |
| `0x1194810` | `GC_free_space_divisor` | data | int |  | Hard-coded 5 | [13](../13-gc-tuning.md) | doc |
| `0x1194814` | `GC_time_limit` | data | int |  | Set by GC_PAUSE_TIME_TARGET | [13](../13-gc-tuning.md) | doc |
| `0x119481C` | `GC_rate` | data | int |  | 25 mark steps per call | [13](../13-gc-tuning.md) | doc |
| `0x1194820` | `GC_max_prior_attempts` | data | int |  | 40 | [13](../13-gc-tuning.md) | doc |
| `0x1194848` | `GC_finalizers_per_call` | data | int |  | 50 | [13](../13-gc-tuning.md) | doc |
| `0x11949B8` | `GC_large_alloc_warn_interval` | data | int |  |  | [13](../13-gc-tuning.md) | doc |
| `0x11EE528` | `EA_gc_stop_gate` | data | int |  | Gates the stop function | [13](../13-gc-tuning.md) | doc |
| `0x11F2F90` | `GC_gc_no` | data | int |  |  | [13](../13-gc-tuning.md) | doc |
| `0x11F2F98` | `EA_gc_budget_counter` | data | int |  | Adapts the budget (range +-20) | [13](../13-gc-tuning.md) | doc |
| `0x11F2F9C` | `GC_incremental` | data | int |  |  | [13](../13-gc-tuning.md) | doc |
| `0x11F2FB4` | `GC_finish_attempts` | data | int |  |  | [13](../13-gc-tuning.md) | doc |
| `0x11F2FD0` | `GC_fo_entries_last` | data | int |  |  | [13](../13-gc-tuning.md) | doc |
| `0x11F3040` | `GC_fo_entries` | data | int |  |  | [13](../13-gc-tuning.md) | doc |
| `0x11F30A8` | `GC_dont_gc` | data | int |  |  | [13](../13-gc-tuning.md) | doc |
| `0x11F30B4` | `GC_print_stats` | data | int |  |  | [13](../13-gc-tuning.md) | doc |
| `0x11F30D0` | `GC_is_initialized` | data | int |  |  | [15 Phase 1](../15-native-patches.md) | doc |
| `0x11F40E4` | `GC_no_win32` | data | int |  |  | [13](../13-gc-tuning.md) | doc |
| `0x1221000` | `GC_threads` | data | GC_thread[256], 0x14 each |  |  | [15 Phase 3](../15-native-patches.md) | doc |
| `0x12225A0` | `GC_heapsize` | data | size_t |  |  | [13](../13-gc-tuning.md) | doc |
| `0x12225A4` | `GC_max_heapsize` | data | size_t |  | Set by GC_MAXIMUM_HEAP_SIZE | [13](../13-gc-tuning.md) | doc |
| `0x12225B4` | `GC_large_free_bytes` | data | size_t |  |  | [13](../13-gc-tuning.md) | doc |
| `0x12225C4` | `GC_words_allocd` | data | size_t |  | Words allocated since the last GC (x4 = bytes) | [13](../13-gc-tuning.md) | doc |

## EA allocator (GeneralAllocator)

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x4E3DB0` | `EA_AllocObject_A` | fn | (size, ...) | any |  | [08](../08-resource-offload-design.md) | fn |
| `0x4E3E40` | `ICoreAllocator_Alloc` | fn |  | any | -> 0x4E69D0 | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E3E90` | `ICoreAllocator_AllocAligned` | fn |  | any | -> 0x4E6A20 | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E3EF0` | `ICoreAllocator_Free` | fn |  | any | -> 0x4E5920 | [15 Phase 8](../15-native-patches.md) | doc |
| `0x4E3F90` | `EA_AllocTagged` | fn |  | any | Tagged allocation entry (tag strings such as "Routing/...") | [08](../08-resource-offload-design.md), [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E40C0` | `MemoryManHeap_Create` | fn |  | main | "Services/MemoryMan/MemoryManHeap" | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E41F0` | `EA_AllocObject_B` | fn | (size, ...) | any |  | [08](../08-resource-offload-design.md) | fn |
| `0x4E4770` | `GeneralAllocator_request2size` | fn |  | any | (n + 0x1B) & ~15, min 16 | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E48C0` | `GeneralAllocator_InitLock` | fn |  | any | CRITICAL_SECTION + recursion count, spin 10 | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E48F0` | `GeneralAllocator_ArenaCore_A` | fn |  | any | VirtualAlloc cores; S3PP AllocatorArenaTuning immediates at 0x4E49E5/0x4E4A1C | [15 Phase 8](../15-native-patches.md), [17 §8](../17-heavy-store-content.md) | fn |
| `0x4E4B30` | `GeneralAllocator_TrimCheck` | fn |  | any | -> 0x4E4F90 decommit | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E4F90` | `GeneralAllocator_Decommit` | fn |  | any | VirtualFree(MEM_DECOMMIT) | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E50F0` | `GeneralAllocator_DirectAlloc` | fn |  | any | Flag bit 3: straight to VirtualAlloc | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E51B0` | `GeneralAllocator_FreeCore` | fn |  | any |  | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E5710` | `GeneralAllocator_InitBins` | fn |  | any | Sets max_fast = 0x40 | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E5920` | `GeneralAllocator_Free` | fn | thiscall(ptr), ret 4 | any | Lock, core 0x4E51B0, unlock (307 call sites) | [15 Phase 8](../15-native-patches.md) | bytes |
| `0x4E5960` | `GeneralAllocator_Consolidate` | fn |  | any | malloc_consolidate over all fastbins before any request >= 512 bytes | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E5AA0` | `GeneralAllocator_ArenaCore_B` | fn |  | any | S3PP AllocatorArenaTuning immediates at 0x4E5BDA/0x4E5BE4 | [15 Phase 8](../15-native-patches.md), [17 §8](../17-heavy-store-content.md) | fn |
| `0x4E5CB0` | `GeneralAllocator_Shutdown` | fn | thiscall | any | Frees the heap cores (also via thunk 0x4E69C0) | [15 Phase 8](../15-native-patches.md) | bytes |
| `0x4E5E40` | `GeneralAllocator_MallocCore` | fn | (n, flags) | any | dlmalloc 2.7 derivative core; 6.9% of pooled sim samples (9.9% BP) | [15 Phase 8](../15-native-patches.md), [20](../20-offload-candidates.md) | fn |
| `0x4E63F0` | `GeneralAllocator_MemalignCore` | fn |  | any | = MallocCore when align <= 16 and offset 0 | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E6710` | `GeneralAllocator_ReallocCore` | fn |  | any |  | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E69C0` | `GeneralAllocator_Shutdown_thunk` | fn |  | any | jmp 0x4E5CB0 | [15 Phase 8](../15-native-patches.md) | fn |
| `0x4E69D0` | `GeneralAllocator_Malloc` | fn | thiscall(size, flags), ret 8 | any | Lock, core 0x4E5E40, unlock (179 call sites) | [15 Phase 8](../15-native-patches.md) | bytes |
| `0x4E6A20` | `GeneralAllocator_Memalign` | fn | thiscall(size, align, offset, flags), ret 10h | any | Lock, 0x4E63F0, unlock | [15 Phase 8](../15-native-patches.md) | bytes |
| `0x4E6A70` | `GeneralAllocator_Realloc` | fn | thiscall | any | -> 0x4E6710 | [15 Phase 8](../15-native-patches.md) | fn |
| `0x89F4E0` | `AudioAllocator_Create` | fn |  | main | "Audio/Allocator", own heap with an OOM callback | [15 Phase 8](../15-native-patches.md) | fn |
| `0x931020` | `DefaultAllocator` | fn |  | any | Default ICoreAllocator (vtable 0xFB8B38) | [15 Phase 8](../15-native-patches.md) | fn |
| `0xFB8B38` | `vt_ICoreAllocator_default` | vtable |  |  | slot +4 aligned Alloc 0x4E3E90, +8 Alloc 0x4E3E40, +0xC Free 0x4E3EF0 | [15 Phase 8](../15-native-patches.md) | doc |
| `0x11CB864` | `g_MemoryManHeap` | data |  |  | Shared MemoryMan heap | [15 Phase 8](../15-native-patches.md) | doc |
| `0x11D8628` | `g_SpeedTreeAllocBytes` | data |  |  | SpeedTree own byte counter (SpeedTree code 0x7AF190-0x7BC0D0) | [15 Phase 8](../15-native-patches.md) | doc |

## Routing: entry points and plan chain

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x65E0D0` | `PathPlanner_PlanHierarchical` | fn |  | sim | World, lot and water legs | [11](../11-route-planner-re.md), [15 Phase 9](../15-native-patches.md) | fn |
| `0x661B50` | `PathPlan_WithRetries` | fn |  | sim | Planner wrapper that retries (internal flag 0x800000, nudged start). The Ghidra project label "RoutingJob_Init" is WRONG | [11](../11-route-planner-re.md) | fn |
| `0x74B2C0` | `RouteHandle_Lookup` | fn |  | sim | Handle -> route via table [0x11D40C4]; rejects handles without tag 0x25 | [18](../18-route-search-off-thread.md) | fn |
| `0x74D040` | `IC_Route_GetOption` | fn | cdecl(handle, optLo, optHi, options2) -> AL | sim | Calls 0x81C880 with ecx = route+0x158; ts3native uses unused bit 58 as a handshake | [15 Phase 5](../15-native-patches.md), [18](../18-route-search-off-thread.md) | bytes |
| `0x750400` | `IC_Route_DestroyRoute` | fn | cdecl | sim | A finalizer can call it while a worker still uses the route | [18](../18-route-search-off-thread.md) | fn |
| `0x752620` | `Route_MapStatusToResult` | fn |  | sim | Path status -> RoutePlanResultType (+ blocker id/location); 7/8 = no path | [11](../11-route-planner-re.md), [15 Phase 2](../15-native-patches.md) | fn |
| `0x7538C0` | `IC_Route_IsPointRoutable` | fn | cdecl | sim | Does not reach the locked search | [04 F1](../04-re-findings.md) | bytes |
| `0x7606E0` | `Route_PlanCommon` | fn |  | sim | Builds goal points and the 18-dword result (default mType 2 = Failed); x/z bounds checks; its goal step 0x75C2D0 runs OUTSIDE the routing lock | [11](../11-route-planner-re.md), [18](../18-route-search-off-thread.md) | fn |
| `0x7608AD` | `PlanCommon_call_RoutingOptionsTest` | site |  | sim | call 0x81C880 with ecx = route+0x158 | [15 Phase 2](../15-native-patches.md) | doc |
| `0x760DE1` | `PlanCommon_call_PlanLocked` | site |  | sim | One of three callers of 0x82C6E0 | [11](../11-route-planner-re.md), [15 Phase 2](../15-native-patches.md) | bytes |
| `0x76156D` | `PlanCommon_default_fail` | site |  | sim | Returns the default Failed result | [11](../11-route-planner-re.md), [15 Phase 4](../15-native-patches.md) | doc |
| `0x7641D0` | `Route_StartPointsFromPosition` | fn |  | sim | Start points from the Sim position | [04 F1](../04-re-findings.md), [11](../11-route-planner-re.md) | fn |
| `0x7642C0` | `Route_StartPointsBuild` | fn |  | sim | "Routing/RoutingHost/startPoints" | [04 F1](../04-re-findings.md), [11](../11-route-planner-re.md) | fn |
| `0x765160` | `IC_Route_Plan` | fn | cdecl([esp+4] hidden RoutePlanResult*, [esp+8] route handle) -> eax = result* | sim | ScriptCore.Route::Route_Plan; -> 0x7641D0 -> 0x7606E0. ts3native async detour | [04 F1](../04-re-findings.md), [11](../11-route-planner-re.md), [18](../18-route-search-off-thread.md) | bytes |
| `0x765240` | `IC_Route_PlanFromPoint` | fn | cdecl | sim | -> 0x7642C0 | [04 F1](../04-re-findings.md), [11](../11-route-planner-re.md) | bytes |
| `0x765490` | `IC_Route_Replan` | fn | cdecl | sim | -> 0x7641D0 | [04 F1](../04-re-findings.md), [11](../11-route-planner-re.md) | bytes |
| `0x765550` | `IC_Route_ReplanFromPosition` | fn | cdecl | sim | -> 0x7642C0 | [04 F1](../04-re-findings.md), [11](../11-route-planner-re.md) | bytes |
| `0x819AE0` | `RoutingJob_dtor` | fn | thiscall | sim |  | [11](../11-route-planner-re.md) | fn |
| `0x819D50` | `RoutingJob_ctor` | fn | thiscall | sim | Stack object, vtable 0x1021258 | [11](../11-route-planner-re.md) | fn |
| `0x81B2A0` | `RoutingJob_Execute` | fn | thiscall | sim | Option bits -> internal flags, calls 0x661B50; obstacle replan at 0x81C67A. The Ghidra project label "Route_SearchAStar" is WRONG | [11](../11-route-planner-re.md) | bytes |
| `0x81C67A` | `RoutingJob_ReplanToFindObstacle` | site |  | sim | Codes 4/5/8 + ReplanToFindObstacleWhenPathPlanFails: a second full search | [11](../11-route-planner-re.md) | doc |
| `0x81C880` | `RoutingOptions_Test` | fn | thiscall(ecx = options)(lo, hi, which) | sim | Tests RouteOption bits in the options words | [11 Q2](../11-route-planner-re.md) | fn |
| `0x81D440` | `RoutingJob_CopyOptions` | fn |  | sim |  | [11](../11-route-planner-re.md) | fn |
| `0x8292D0` | `RoutingWorld_Get` | fn |  | sim | Returns the routing world [inferred] | [11](../11-route-planner-re.md), [18](../18-route-search-off-thread.md) | fn |
| `0x82C420` | `RoutingWorld_ApplyPendingUpdates` | fn |  | sim | Checks [world+0x95C]+0x10E before every plan | [11](../11-route-planner-re.md), [18](../18-route-search-off-thread.md) | fn |
| `0x82C6E0` | `Route_PlanLocked` | fn | thiscall (ecx = RoutingWorld), 10 stack dwords, ret 28h, result in AL | sim | Takes world+0x918, pending updates 0x82C420, RoutingJob ctor/Execute/dtor, success = status <= 2, unlocks. ts3native detour (cache, worker, budget) | [04 F1](../04-re-findings.md), [11](../11-route-planner-re.md), [15 Phase 2](../15-native-patches.md) | bytes |
| `0x8ABE50` | `Route_AddRef` | fn | thiscall | sim | lock xadd [ecx+0xC],1 (route vtable +0x10) | [18](../18-route-search-off-thread.md) | fn |
| `0x8ABE60` | `Route_Release` | fn | thiscall | sim | route vtable +0x14 | [18](../18-route-search-off-thread.md) | fn |
| `0xC7BF70` | `World_FindPlaceOnRoadOffScreen_plan` | fn |  | sim | Calls 0x82C6E0 directly | [11](../11-route-planner-re.md) | fn |
| `0xC7C6E3` | `FindPlaceOnRoad_call_PlanLocked_a` | site |  | sim |  | [11](../11-route-planner-re.md), [15 Phase 2](../15-native-patches.md) | bytes |
| `0xC7C9D5` | `FindPlaceOnRoad_call_PlanLocked_b` | site |  | sim |  | [11](../11-route-planner-re.md), [15 Phase 2](../15-native-patches.md) | bytes |
| `0x1004124` | `g_RouteHandleTableLock` | data |  |  |  | [18](../18-route-search-off-thread.md) | doc |
| `0x1021258` | `vt_RoutingJob` | vtable |  |  | slot 1 (0x102125C) = Execute 0x81B2A0 | [11](../11-route-planner-re.md) | doc |
| `0x11D40C4` | `g_RouteHandleTable` | data |  |  |  | [18](../18-route-search-off-thread.md) | doc |

## Routing: searches (world A*, lot/portal, AStarSearch singleton)

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x61BB40` | `AStarNode_AddRef` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x61BF60` | `AStarSearch_ctor` | fn |  | sim | The only AStarSearch (Q), size 0x9C90; only caller 0x62E860 | [18](../18-route-search-off-thread.md) | fn |
| `0x61C020` | `AStarNodeTable_Clear` | fn |  | sim | Heap corruption seen here in async crash 3 | [18](../18-route-search-off-thread.md) | fn |
| `0x61C2C0` | `AStarNodeTable_Create` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x61C9B0` | `AStarSearch_StampReader` | fn |  | sim | Reads 0x11580C4 | [18](../18-route-search-off-thread.md) | fn |
| `0x61E030` | `AStarSearch_NodeState` | fn |  | sim | Reads the per-node A* state | [18](../18-route-search-off-thread.md) | fn |
| `0x61E320` | `AStarSearch_Reset` | fn |  | sim | Called from the unlocked goal step 0x63D2F0 (async crash 3) | [18](../18-route-search-off-thread.md) | fn |
| `0x61E360` | `AStarSearch_Init` | fn |  | sim | Increments the generation stamp 0x11580C4 | [18](../18-route-search-off-thread.md) | fn |
| `0x61E420` | `AStarSearch_dtor` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x61E500` | `QuadTreeCellAStar` | fn |  | sim | "Routing/AStarSearch" | [20](../20-offload-candidates.md) | fn |
| `0x61E8A0` | `AStarSearch_Run` | fn |  | sim | Room-to-room A* on Q | [18](../18-route-search-off-thread.md) | fn |
| `0x61EB20` | `AStarSearch_deleting_dtor` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x627780` | `RouteCost_Dispatch` | fn |  | sim | Reaches the cost functions [inferred name] | [20](../20-offload-candidates.md) | fn |
| `0x6320D0` | `RoomGraph_NodeFor` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x632E40` | `InRoomPlan` | fn |  | sim | "Routing/portalPlan" | [11](../11-route-planner-re.md) | fn |
| `0x6339F0` | `RoomToRoomAStar` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x634500` | `PortalEdgeCost` | fn |  | sim | Portal-edge cost; can call script (portal lock query) | [18](../18-route-search-off-thread.md) | fn |
| `0x63E340` | `LotPortalAStar` | fn |  | sim | Portal graph A*, no budget | [11](../11-route-planner-re.md), [20](../20-offload-candidates.md) | fn |
| `0x63EC40` | `LotPortalSearch` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x63F6E0` | `LotPlan_Inner` | fn |  | sim |  | [11](../11-route-planner-re.md), [18](../18-route-search-off-thread.md) | fn |
| `0x640E00` | `LotSearch_Entry` | fn |  | sim | From 0x65E0D0 and 0xC75920 to 0x63EC40 | [18](../18-route-search-off-thread.md) | fn |
| `0x6410B0` | `LotPlan` | fn |  | sim | Lot leg; one retry per goal room on failure | [11](../11-route-planner-re.md) | fn |
| `0x64F1E0` | `PortalLockCache_Find` | fn |  | sim | Key portal id + rooms | [18](../18-route-search-off-thread.md) | fn |
| `0x64F2C0` | `Portal_LockState` | fn |  | sim | Cached in a hash map (0x64F1E0), else asks script | [18](../18-route-search-off-thread.md) | fn |
| `0x64F550` | `Portal_IsLocked` | fn |  | sim | state == 1 | [18](../18-route-search-off-thread.md) | fn |
| `0x64F570` | `Portal_IsDiscouraged` | fn |  | sim | state == 3 | [18](../18-route-search-off-thread.md) | fn |
| `0x6645D0` | `AStarNodeTable_FindOrCreate` | fn |  | sim | 10007-bucket table | [18](../18-route-search-off-thread.md) | fn |
| `0x664B10` | `AStarNode_Get` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x6734E0` | `RouteCost_Default` | fn | cdecl, 8 args, float in ST0 | sim | Default travel mode | [15 Phase 4](../15-native-patches.md) | doc |
| `0x673850` | `RouteCost_Mode2` | fn | cdecl, 8 args, float in ST0 | sim | Travel mode 2 | [11](../11-route-planner-re.md), [15 Phase 4](../15-native-patches.md) | fn |
| `0x673B60` | `WorldAStar_OpenPop` | fn |  | sim | Pop/sift | [11](../11-route-planner-re.md) | fn |
| `0x674250` | `RouteCost_Mode3` | fn | cdecl, 8 args, float in ST0 | sim | Travel mode 3 | [11](../11-route-planner-re.md), [15 Phase 4](../15-native-patches.md) | fn |
| `0x674E40` | `RouteCost_Mode1` | fn | cdecl, 8 args, float in ST0 | sim | Edge cost / heuristic, travel mode 1 | [11](../11-route-planner-re.md), [15 Phase 4](../15-native-patches.md) | fn |
| `0x675800` | `RouteCost_Mode4` | fn | cdecl, 8 args, float in ST0 | sim | Travel mode 4 | [11](../11-route-planner-re.md), [15 Phase 4](../15-native-patches.md) | fn |
| `0x675E90` | `WorldAStar_NodePoolGrow` | fn |  | sim | Allocates 0x4E28 bytes (200 x 5) per block, no limit | [11](../11-route-planner-re.md) | fn |
| `0x6761F0` | `WorldAStar_OpenPush` | fn |  | sim | EASTL vector heap push | [11](../11-route-planner-re.md) | fn |
| `0x676260` | `WorldAStar_Search` | fn |  | sim | World A* ("Routing/PathContainer"): binary heap, closed bitset, node pool; no node, time or cost budget | [11](../11-route-planner-re.md), [15 Phase 4](../15-native-patches.md) | fn |
| `0x6762B4` | `WorldAStar_recursive_call` | site |  | sim |  | [11](../11-route-planner-re.md) | doc |
| `0x676340` | `WorldAStar_cost_fn_site_1` | site | mov [esp+0x60], imm32 | sim | Five sites 0x676340/4A/54/5E/68 pick the cost function; ts3native budget swaps the imm | [15 Phase 4](../15-native-patches.md) | bytes |
| `0x67634A` | `WorldAStar_cost_fn_site_2` | site | mov [esp+0x60], imm32 | sim |  | [15 Phase 4](../15-native-patches.md) | bytes |
| `0x676354` | `WorldAStar_cost_fn_site_3` | site | mov [esp+0x60], imm32 | sim |  | [15 Phase 4](../15-native-patches.md) | bytes |
| `0x67635E` | `WorldAStar_cost_fn_site_4` | site | mov [esp+0x60], imm32 | sim |  | [15 Phase 4](../15-native-patches.md) | bytes |
| `0x676368` | `WorldAStar_cost_fn_site_default` | site | mov [esp+0x60], imm32 | sim |  | [15 Phase 4](../15-native-patches.md) | bytes |
| `0x676495` | `WorldAStar_loop_head` | site |  | sim |  | [11](../11-route-planner-re.md) | doc |
| `0x676683` | `WorldAStar_wall_check_1` | site |  | sim | Compares the edge cost with the 1e8 wall 0xFEF678 | [15 Phase 4](../15-native-patches.md) | doc |
| `0x6768A6` | `WorldAStar_wall_check_2` | site |  | sim |  | [15 Phase 4](../15-native-patches.md) | doc |
| `0x676B24` | `WorldAStar_wall_check_3` | site |  | sim |  | [15 Phase 4](../15-native-patches.md) | doc |
| `0x676BE9` | `WorldAStar_loop_backedge` | site |  | sim | 14 bytes to 0x676BF6; candidate budget patch site (not used) | [11](../11-route-planner-re.md) | doc |
| `0x676BF7` | `WorldAStar_open_empty_exit` | site |  | sim | Returns 0 (no path) | [11](../11-route-planner-re.md), [15 Phase 4](../15-native-patches.md) | doc |
| `0x676C14` | `WorldAStar_goal_popped` | site |  | sim |  | [11](../11-route-planner-re.md) | doc |
| `0x78F2B0` | `World_FireEvent` | fn |  | sim | Delegate table 0x11D6A78 + N*0x24 -> script host -> runtime_invoke (LotManager.PortalLockedCallback, event 59) | [15 Phase 3](../15-native-patches.md), [18](../18-route-search-off-thread.md) | fn |
| `0x791C00` | `World_FirePortalLockedQuery` | fn |  | sim | Marshals 6 args (lotId, routingObjectId, portalObjectId, rooms, initialLocked) | [18](../18-route-search-off-thread.md) | fn |
| `0xFEA5C0` | `vt_AStarSearch` | vtable |  |  |  | [18](../18-route-search-off-thread.md) | doc |
| `0xFEF678` | `k_RouteWallCost` | data | float 1e8 |  | Blocked-edge constant | [11](../11-route-planner-re.md), [15 Phase 4](../15-native-patches.md) | doc |
| `0x11580C4` | `g_AStarGeneration` | data | WORD |  | Search generation stamp (3 xrefs) | [18](../18-route-search-off-thread.md) | doc |
| `0x11D6A78` | `g_WorldEventDelegates` | data | entries of 0x24 |  | Written by IC World_RegisterCallbackDelegateImpl 0x7861A0 | [18](../18-route-search-off-thread.md) | doc |

## Routing: water JPS

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x61F750` | `WaterJps_Step` | fn |  | sim | Index arithmetic step (row-wrap quirk) | [15 Phase 9](../15-native-patches.md) | fn |
| `0x61F7E0` | `WaterJps_ParentDir` | fn |  | sim |  | [15 Phase 9](../15-native-patches.md) | fn |
| `0x61F830` | `WaterJps_HeapPush` | fn |  | sim | "PathPlan/tOpenListContainer" | [15 Phase 9](../15-native-patches.md) | fn |
| `0x61F890` | `WaterJps_NeighbourCoord` | fn |  | sim |  | [15 Phase 9](../15-native-patches.md) | fn |
| `0x61F9A0` | `WaterJps_IsOpen` | fn |  | sim | g1 && !g2, bounds checked | [15 Phase 9](../15-native-patches.md) | fn |
| `0x61F9E0` | `WaterJps_IsMarked` | fn |  | sim | g2[n] | [15 Phase 9](../15-native-patches.md) | fn |
| `0x61FA10` | `WaterJps_HasForced` | fn |  | sim | Forced-neighbour test; calls the callback | [15 Phase 9](../15-native-patches.md) | fn |
| `0x61FE10` | `WaterJps_Jump` | fn | cdecl, 9 args, plain ret | sim | One cell per call, recursive; ~92% of the JPS time | [15 Phase 9](../15-native-patches.md) | fn |
| `0x61FFA0` | `WaterJps_EdgeCost` | fn |  | sim |  | [15 Phase 9](../15-native-patches.md) | fn |
| `0x620120` | `WaterJps_Heuristic` | fn |  | sim | Octile | [15 Phase 9](../15-native-patches.md) | fn |
| `0x620560` | `WaterJps_Search` | fn | (g1, g2, w, h, costScale, &start, &goal, &outPath, cb) | sim | JPS A* on a byte grid; 7.8% of Isla Paradiso sim samples | [15 Phase 9](../15-native-patches.md), [20](../20-offload-candidates.md) | fn |
| `0x6207FB` | `WaterJps_call_Jump` | site | E8 rel32 -> 0x61FE10 | sim | The search's only outside call of the jump; ts3native retargets it | [15 Phase 9](../15-native-patches.md) | bytes |
| `0x656A70` | `WaterPath_Plan2` | fn |  | sim | "Routing/waterPath" | [11](../11-route-planner-re.md) | fn |
| `0x66DC50` | `WaterRoute_NearStartOrGoal` | fn | thiscall (vtable slot 1) | sim | Cell within 3 cells of start (+0x10) or goal (+0x18), Chebyshev; pure | [15 Phase 9](../15-native-patches.md) | doc |
| `0x671930` | `WaterPath_Plan` | fn |  | sim | "PathPlan/path", "Routing/waterPath" | [11](../11-route-planner-re.md), [15 Phase 9](../15-native-patches.md) | fn |
| `0x6EB480` | `WaterJps_HeapPop` | fn |  | sim |  | [15 Phase 9](../15-native-patches.md) | fn |

## Routing: lot graph and planner state

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x6255B0` | `LotState_Unlocked` | fn |  | sim | More unlocked lot state [inferred] | [PLAN](../../PLAN.md) | fn |
| `0x625940` | `LotTable_Rehash` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x62AA60` | `LotGraph_ctor` | fn |  | sim | About 0x180 bytes | [18](../18-route-search-off-thread.md) | fn |
| `0x62B930` | `LotGraph_dtor` | fn |  | sim | Frees the temporary | [18](../18-route-search-off-thread.md) | fn |
| `0x62E420` | `LotGraph_Reset` | fn |  | sim | Lot removal path | [18](../18-route-search-off-thread.md) | fn |
| `0x62E5F0` | `LotTable_InsertCopy` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x62E680` | `LotTable_Insert` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x62E860` | `PathPlanner_Init` | fn |  | sim | P init (callers 0x829900, 0x82A810, 0x834FC0) | [18](../18-route-search-off-thread.md) | fn |
| `0x62EAB0` | `LotTable_GetOrInsert` | fn |  | sim | Returns the per-lot graph entry+0x10, inserting a default one on a miss (shared by all callers) | [18](../18-route-search-off-thread.md) | fn |
| `0x63CB90` | `LotGraph_Build` | fn |  | sim | Sets entry+0x8C built | [18](../18-route-search-off-thread.md) | fn |
| `0x63D2F0` | `RoomGraph_FirstUse` | fn |  | sim | Builds room records and resets Q (unlocked) | [18](../18-route-search-off-thread.md) | fn |
| `0x63D7B0` | `LotGraph_RebuildRooms` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x63D860` | `RoomGraph_Refresh` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x63DCB0` | `RoomGraph_ConnectedRooms` | fn |  | sim | From IC World_GetConnectedRoomsImpl 0x7929F0 | [18](../18-route-search-off-thread.md) | fn |
| `0x63F490` | `LotPlanner_GoalRooms` | fn |  | sim |  | [18](../18-route-search-off-thread.md) | fn |
| `0x663B20` | `QuadTreeCellPos_A` | fn |  | sim | "Routing/QuadTreeCellPos" | [20](../20-offload-candidates.md) | fn |
| `0x66A040` | `QuadTreeCellPos_B` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x66B090` | `QuadTreeCellPos_C` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x75C2D0` | `Route_GoalStep` | fn |  | sim | Goal step of every plan; runs outside world+0x918 | [18](../18-route-search-off-thread.md) | fn |
| `0x829BC0` | `RoutingWorld_GetLotPlanner` | fn |  | sim | Returns [world+0x95C] when world+0x974 is set (the lot path planner P) | [18](../18-route-search-off-thread.md) | fn |
| `0x829D50` | `RoutingWorld_RemoveLot` | fn |  | sim | Takes world+0x918 | [18](../18-route-search-off-thread.md) | fn |
| `0x82B810` | `RoutingWorld_RebuildLot` | fn |  | sim | Resets Q twice | [18](../18-route-search-off-thread.md) | fn |
| `0x834FC0` | `RoutingWorld_Save` | fn |  | sim | Routing world save under the routing lock; also a P init caller | [15 Phase 7](../15-native-patches.md), [18](../18-route-search-off-thread.md) | fn |

## Locks

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x4E16F0` | `Lock_Acquire` | fn | thiscall(const DWORD* deadline), ret 4 -> 0 ok / -2 timeout | any | Engine lock (EnterCriticalSection / WaitForSingleObject wrapper); deadline 0 = try, 0xFFFFFFFF = infinite, else absolute GetTickCount | [04 F1](../04-re-findings.md), [18](../18-route-search-off-thread.md) | bytes |
| `0x4E17B0` | `Lock_Release` | fn | thiscall | any |  | [04 F1](../04-re-findings.md) | fn |
| `0x82C731` | `PlanLocked_lock_site` | site |  | sim | Passes ecx = world+0x918 to Lock_Acquire | [18](../18-route-search-off-thread.md) | doc |
| `0x82EDC1` | `ObstacleQuery_lock_site` | site |  | sim | Passes ecx = world+0x918 to Lock_Acquire | [18](../18-route-search-off-thread.md) | doc |
| `0xFB2CD0` | `g_ResourceLock` | data |  |  | Global resource lock held across GetResource incl. the file read | [08](../08-resource-offload-design.md) | doc |
| `0x1021CF0` | `g_RoutingWorldPtr` | data |  |  | NOT the lock: pointer to the routing world; the lock is world+0x918 | [18](../18-route-search-off-thread.md) | doc |

## Locomotion and route following

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x82EDB0` | `RoutingWorld_ObstacleQuery` | fn |  | sim | Takes world+0x918 every iteration ("Routing/PortalCheckCache") | [18](../18-route-search-off-thread.md) | fn |
| `0x83CED0` | `Locomotion_Update` | fn |  | sim | Called from Simulate when [0x11D9140] is set; 2.4% of sim samples | [18](../18-route-search-off-thread.md), [20](../20-offload-candidates.md) | fn |
| `0x846B70` | `RouteFollow_A` | fn |  | sim | Route follow/steering | [11](../11-route-planner-re.md) | fn |
| `0x847390` | `ObstacleAvoidance` | fn |  | sim | "Locomotion/IgnoreList" | [20](../20-offload-candidates.md) | fn |
| `0x85B470` | `RouteFollow_B` | fn |  | sim | Route follow/steering | [11](../11-route-planner-re.md) | fn |
| `0x85D980` | `ObstacleAvoidance_B` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x85F5A0` | `RouteFollow_EditPath` | fn |  | sim | Route edits via IC_Route_SetAGS, InsertCustomPathAtIndex, ... | [11](../11-route-planner-re.md), [18](../18-route-search-off-thread.md) | fn |
| `0x85FD10` | `Locomotion_ObstacleCheck` | fn |  | sim |  | [11](../11-route-planner-re.md), [18](../18-route-search-off-thread.md) | fn |
| `0x85FFE0` | `Locomotion_ObstacleList` | fn |  | sim | "Locomotion/ObstacleList" | [18](../18-route-search-off-thread.md) | fn |
| `0x957210` | `Locomotion_UpdateAgent` | fn |  | sim |  | [18](../18-route-search-off-thread.md), [20](../20-offload-candidates.md) | fn |
| `0x11D9140` | `g_LocomotionEnabled` | data |  |  |  | [18](../18-route-search-off-thread.md) | doc |

## World queries (spatial, line of sight, object lookup)

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x67E2B0` | `RayTriangle_Int16` | fn |  | sim | Floor/terrain height ray test; 1.9% of sim samples | [20](../20-offload-candidates.md) | fn |
| `0x67E740` | `TerrainRay_C` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x67EA80` | `TerrainRay_B` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x67EDE0` | `TerrainRay_A` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x6809F0` | `BBox_Test` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x6814B0` | `LineOfSight` | fn |  | sim | 0.4% | [20](../20-offload-candidates.md) | fn |
| `0x681B40` | `FootprintQuadtree_Leaf` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x681CF0` | `FootprintQuadtree_Recurse` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x681E00` | `FootprintQuadtree_Query` | fn |  | sim | 1.3% | [20](../20-offload-candidates.md) | fn |
| `0x7499B0` | `Query_GetObjectsOnLot_impl` | fn |  | sim | GetObjectsOnLot/CountObjectsOnLot; 1.1% native + 1.7% managed | [20](../20-offload-candidates.md) | fn |
| `0x76C5C0` | `Simulate_CanSeeSky_A` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x76C8A0` | `Simulate_CanSeeSky_B` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x80E060` | `Matrix_CopyVirtual` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0x935770` | `Object_GetComponent` | fn |  | sim | Popcount sparse array | [20](../20-offload-candidates.md) | fn |
| `0x939100` | `ObjectId_Lookup` | fn |  | sim | Fixed 1,033 buckets (key % 0x409), chained; 2.4% | [20](../20-offload-candidates.md) | fn |
| `0xA8DF80` | `Lot_RoomLookup` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0xAB7ED0` | `Lot_TileLookup` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0xAC1130` | `Lot_AddLotObjectsToScene` | fn |  | main | S3SS lot streaming object throttle replaces it | [03 anchors](../03-prior-art-re.md) | fn |
| `0xC2A570` | `WorldCellGrid_Raycast` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0xC5FA60` | `SceneNode_FindRecursive` | fn |  | sim |  | [20](../20-offload-candidates.md) | fn |
| `0xC622D0` | `World_LotStreaming` | fn |  | main? |  | [18](../18-route-search-off-thread.md) | fn |
| `0xC68990` | `World_LotLookup` | fn |  | sim |  | [11](../11-route-planner-re.md), [18](../18-route-search-off-thread.md) | fn |
| `0xC6A540` | `Terrain_GetHeight` | fn |  | sim |  | [11](../11-route-planner-re.md), [18](../18-route-search-off-thread.md) | fn |
| `0xC6BCC0` | `WorldManager_LotRebuildCaller` | fn |  | main? | World manager caller of lot rebuild; also World_Save step 5 (dirty lots, "World/WorldManager/DirtyLots") | [18](../18-route-search-off-thread.md), [25](../25-save-world-write.md) | fn |
| `0xC6D570` | `WorldManager_Update` | fn | thiscall | main | Lot streaming; +0x258 skip-streaming flag (S3SS) | [03 anchors](../03-prior-art-re.md), [18](../18-route-search-off-thread.md) | fn |

## Resources, DBPF and decompression

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x4331F0` | `zlib_inflate` | fn |  | any | zlib 1.1.3 | [15 Phase 6](../15-native-patches.md) | fn |
| `0x4A1BD0` | `ResourceConfig_ParseDirectives` | fn |  | main | PackedFile, DirectoryFiles, Priority | [08](../08-resource-offload-design.md) | fn |
| `0x4A2A70` | `ResourceConfig_PatternList` | fn |  | main |  | [08](../08-resource-offload-design.md) | fn |
| `0x4A7030` | `DBPF_PackedFile_Decompress2` | fn |  | any |  | [15 Phase 6](../15-native-patches.md) | fn |
| `0x4A7360` | `DBPF_PackedFile_Decompress` | fn |  | any |  | [08](../08-resource-offload-design.md), [15 Phase 6](../15-native-patches.md) | fn |
| `0x4A7910` | `DBPF_PackedFile_WriteRecord` | fn |  | sim |  | [15 Phase 7](../15-native-patches.md) | fn |
| `0x4A7AD0` | `DBPF_HeaderValidate` | fn |  | any |  | [08](../08-resource-offload-design.md) | fn |
| `0x4A8380` | `DBPF_PackedFile_ReadDecompress` | fn |  | any |  | [08](../08-resource-offload-design.md) | fn |
| `0x4A8500` | `DBPF_PackedFile_Commit` | fn |  | sim | vtable +0x1C | [15 Phase 7](../15-native-patches.md) | fn |
| `0x4A8F70` | `DBPF_PackedFile_ctor` | fn |  | any | Object 0x398 bytes, vtable 0xFB2600 | [08](../08-resource-offload-design.md) | fn |
| `0x4A9150` | `DBPF_PackedFile_WriteCompact` | fn |  | any | Through a TempDBPF_ file | [08](../08-resource-offload-design.md) | fn |
| `0x4A94C0` | `DBPF_PackedFile_OpenRecord` | fn |  | any | slot 0x34 | [08](../08-resource-offload-design.md) | fn |
| `0x4AAF50` | `DBPF_IndexCompressed_ctor` | fn |  | any |  | [08](../08-resource-offload-design.md) | fn |
| `0x4ABD70` | `DBPF_IndexMap_A` | fn |  | any | Modifiable index, 0x24-byte nodes | [08](../08-resource-offload-design.md) | fn |
| `0x4AC240` | `DBPF_IndexMap_B` | fn |  | any |  | [08](../08-resource-offload-design.md) | fn |
| `0x4AD020` | `DBPF_IndexCompressed_Load` | fn |  | any | 16-byte entries | [08](../08-resource-offload-design.md) | doc |
| `0x4AFD20` | `ResourceMan_GetManager` | fn |  | any | Returns [0x11C78D8] | [08](../08-resource-offload-design.md) | fn |
| `0x4AFDA0` | `ResourceMan_FindDatabase` | fn |  | any | slot 0x44 | [08](../08-resource-offload-design.md) | doc |
| `0x4AFFC0` | `ResourceMan_Manager_GetPackageSource` | fn |  | any | slot 0x40 | [08](../08-resource-offload-design.md) | doc |
| `0x4B0070` | `ResourceMan_Manager_FindCache` | fn |  | any | slot 0x58 | [08](../08-resource-offload-design.md) | fn |
| `0x4B00C0` | `ResourceMan_Manager_CacheResource` | fn |  | any | slot 0x5C | [08](../08-resource-offload-design.md) | fn |
| `0x4B0480` | `ResourceMan_Manager_GetResource` | fn | (useCache, key, out res, ...) | any | Holds the global resource lock 0xFB2CD0 for the whole call | [08](../08-resource-offload-design.md) | fn |
| `0x4B0750` | `ResourceKey_Read` | fn |  | any | Key {instance, type, group}, 16 bytes | [08](../08-resource-offload-design.md) | doc |
| `0x4B0810` | `ResourceMan_GetResource_slot` | fn |  | any | vtable 0x18 -> 0x4B0480 | [08](../08-resource-offload-design.md) | fn |
| `0x4B1AE0` | `ResourceMan_GetKeyList` | fn | (out keys, filter) | any | slot 0x20 | [08](../08-resource-offload-design.md) | fn |
| `0x4B2D00` | `ResourceMan_RegisterDatabase` | fn | (add, db, priority) | main | slot 0x34 | [08](../08-resource-offload-design.md) | fn |
| `0x4B33B0` | `ResourceMan_Manager_ctor` | fn |  | main | vtable 0xFB2DA0 | [08](../08-resource-offload-design.md) | fn |
| `0x4DB355` | `FileWatcher_beginthreadex_site` | site |  | main | S3SS Oversized Thread Stack Fix site | [03 §1.2](../03-prior-art-re.md) | bytes |
| `0x4DB4C0` | `FileStream_Open` | fn |  | any | CreateFileW wrapper | [08](../08-resource-offload-design.md) | fn |
| `0x4DB56C` | `FileStream_Open_flags_branch` | site |  | any | S3SS CreateFileW Random Access site | [03](../03-prior-art-re.md), [08](../08-resource-offload-design.md) | doc |
| `0x4DB850` | `FileStream_Read` | fn |  | any |  | [08](../08-resource-offload-design.md) | fn |
| `0x4EB3B0` | `RefPack_DecompressCore` | fn | cdecl(dst, dstCap, src, srcLen) -> size or 0 | any | S3SS RefPackDecompressor replaces it | [08](../08-resource-offload-design.md), [15 Phase 6](../15-native-patches.md) | bytes |
| `0x4EC010` | `RefPack_DecompressWrapper` | fn |  | any | Codec Decompress slot (vtable 0xFB9018 slot 2); checks the 0x10FB signature | [08](../08-resource-offload-design.md), [15 Phase 6](../15-native-patches.md) | fn |
| `0x4EC065` | `RefPack_call_DecompressCore` | site | E8 46 F3 FF FF | any | The only call to the core; ts3native retargets the rel32 | [15 Phase 6](../15-native-patches.md) | bytes |
| `0x4EC080` | `RefPack_HeaderCheck` | fn |  | any |  | [15 Phase 6](../15-native-patches.md) | doc |
| `0x4EC0A0` | `RefPack_Compress` | fn |  | any |  | [08](../08-resource-offload-design.md) | fn |
| `0x4EC200` | `RefPack_CompressSlot` | fn |  | any |  | [15 Phase 6](../15-native-patches.md) | doc |
| `0x596450` | `ResourceConfig_LoadResourceCfg` | fn |  | main | Also 0x5961C0 | [08](../08-resource-offload-design.md) | fn |
| `0x7274D0` | `AsyncLoader_Init` | fn |  | main | MemoryUsageLimit stored at +0x38 | [08](../08-resource-offload-design.md) | fn |
| `0x728690` | `AsyncLoader_ctor` | fn |  | main |  | [08](../08-resource-offload-design.md) | fn |
| `0x72ACC0` | `KeyFilter_Parse` | fn |  | main |  | [08](../08-resource-offload-design.md) | doc |
| `0x72B0A0` | `MemoryDB_Decompress` | fn |  | any |  | [15 Phase 6](../15-native-patches.md) | fn |
| `0x72C980` | `PackedStream_Decompress` | fn |  | any |  | [15 Phase 6](../15-native-patches.md) | fn |
| `0x731AE0` | `Preloader_Init` | fn |  | main |  | [08](../08-resource-offload-design.md) | fn |
| `0x7324E0` | `ResourceCache_Find` | fn |  | any |  | [08](../08-resource-offload-design.md) | doc |
| `0x732910` | `ResourceCache_ctor` | fn |  | main | vtable 0xFFDD94; CacheBudget at +0x8C | [08](../08-resource-offload-design.md) | fn |
| `0x732A00` | `ResourceCache_Add` | fn |  | any | [inferred name] | [08](../08-resource-offload-design.md) | fn |
| `0x732B60` | `ResourceCache_Touch` | fn |  | any | Moves bytes between the flush list and held [inferred name] | [08](../08-resource-offload-design.md) | fn |
| `0x733140` | `ResourceCache_RegisterDebugVars` | fn |  | main |  | [08](../08-resource-offload-design.md) | fn |
| `0x738670` | `ResourceSystem_Init` | fn |  | main | Creates ResourceCache and AnimationCache | [08](../08-resource-offload-design.md) | doc |
| `0xFB2600` | `vt_DatabasePackedFile` | vtable |  |  |  | [08](../08-resource-offload-design.md), [15 Phase 7](../15-native-patches.md) | doc |
| `0xFB9018` | `vt_RefPackCodec` | vtable |  |  |  | [15 Phase 6](../15-native-patches.md) | doc |
| `0xFFDD94` | `vt_ResourceCache` | vtable |  |  |  | [08](../08-resource-offload-design.md) | doc |
| `0x11C78D8` | `g_ResourceManager` | data |  |  | Live vtable 0xFFE250 (subclass); base 0xFB2DA0 | [08](../08-resource-offload-design.md) | doc |

## CAS, thumbnails and compositing

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x4AC9C0` | `CASPartSearch_Leaf` | fn |  | sim | Under CreatePartSearchImpl | [20](../20-offload-candidates.md) | fn |
| `0x5A44A0` | `CASCache_GetCachedPart` | fn |  | any |  | [08](../08-resource-offload-design.md) | fn |
| `0x5AE170` | `CASPartCacheService_OpenCachePackage` | fn |  | main |  | [08](../08-resource-offload-design.md) | fn |
| `0x5AEDF0` | `CASPartCacheService_Init` | fn |  | main | Asks for every CASP key in every package | [08](../08-resource-offload-design.md), [20](../20-offload-candidates.md) | fn |
| `0x5BC6D0` | `CASCompositorCache_Fn` | fn |  | any | Contains the S3SS WorldCache uncap site 0x5BC8B4 | [08](../08-resource-offload-design.md) | fn |
| `0x5CB3B0` | `BuildSkinJob_RunJob` | fn |  | worker |  | [21](../21-cas-offload.md) | fn |
| `0x5E7480` | `CASThumbnail_BuildKey` | fn |  | sim |  | [21](../21-cas-offload.md) | fn |
| `0x5EB9C0` | `SimCompositorCache_Open` | fn |  | main |  | [08](../08-resource-offload-design.md) | fn |
| `0x5EBC50` | `SimService_Process` | fn |  | worker? | CAS SimService; vanilla OOM crash at 0x5EBE31 [name inferred] | [04](../04-re-findings.md), [05](../05-baseline-results.md) | fn |
| `0x608510` | `TextureCompositorQueue` | fn |  | worker |  | [21](../21-cas-offload.md) | fn |
| `0x6CBF90` | `CompositorCache_Open` | fn |  | main |  | [08](../08-resource-offload-design.md) | fn |
| `0xB389A0` | `ObjectDesigner_RebuildPresets` | fn |  | sim |  | [21](../21-cas-offload.md) | fn |
| `0xB38A90` | `ObjectDesigner_ThumbnailRequests` | fn |  | main |  | [21](../21-cas-offload.md) | fn |
| `0xB396E0` | `ObjectDesigner_SetCASPart` | fn |  | sim | Takes the global ObjectDesignerService lock | [21](../21-cas-offload.md) | fn |
| `0xD4E950` | `FrameDatabase_EventList` | fn |  | sim |  | [21](../21-cas-offload.md) | fn |
| `0xD5C830` | `ThumbnailManager_Placeholder` | fn |  | sim |  | [21](../21-cas-offload.md) | fn |
| `0xD62200` | `ThumbnailRenderer_RTT` | fn |  | main |  | [21](../21-cas-offload.md) | fn |

## Save path

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x4DD030` | `CopyFolderRecursive` | fn |  | sim | CopyFileW per file | [15 Phase 7](../15-native-patches.md) | fn |
| `0x742A50` | `IC_LoadSaveManager_SaveGame_Impl` | fn | cdecl | sim |  | [15 Phase 7](../15-native-patches.md) | fn |
| `0x742A95` | `SaveGame_call_NativeSave` | site | E8 -> 0xAB1030 | sim | ts3native save timing hook | [15 Phase 7](../15-native-patches.md) | bytes |
| `0xAAC110` | `Save_WritePackage` | fn |  | sim | S3 header, listeners, world (0xC6D460), routing world, post-save listeners; Mono callbacks | [15 Phase 7](../15-native-patches.md) | fn |
| `0xAACE80` | `Save_RenameLegacy` | fn |  | sim |  | [15 Phase 7](../15-native-patches.md) | fn |
| `0xAAE950` | `Save_FillInfo` | fn |  | sim | Paths; info+0x430 = <name>.sims3\<world>.nhd | [15 Phase 7](../15-native-patches.md) | fn |
| `0xAAF4B0` | `FlushFolderFiles` | fn |  | sim | FlushFileBuffers per file | [15 Phase 7](../15-native-patches.md) | fn |
| `0xAAF5F0` | `Save_WriteMarkerAndFlush` | fn |  | sim | Writes 1a5tf1l3.dat, flushes the folder | [15 Phase 7](../15-native-patches.md) | fn |
| `0xAAFC50` | `Save_CopyFolder` | fn | (from, to) | sim | Delete the target (retry 10 x 500 ms), copy file by file | [15 Phase 7](../15-native-patches.md) | fn |
| `0xAB0E80` | `Save_CopyWorkingCopy` | fn | (name, 0) | sim | CurrentGame.sims3 -> <name>.sims3 | [15 Phase 7](../15-native-patches.md) | fn |
| `0xAB0F80` | `Save_BackupOld` | fn |  | sim | <name>.sims3 -> .backup | [15 Phase 7](../15-native-patches.md) | fn |
| `0xAB1030` | `Save_Native` | fn | thiscall, 3 args | sim | Whole native save (0xAB1030-0xAB14AB) | [15 Phase 7](../15-native-patches.md) | fn |
| `0xAB10F6` | `Save_call_FillInfo` | site | E8 -> 0xAAE950 | sim |  | [15 Phase 7](../15-native-patches.md) | bytes |
| `0xAB12AF` | `Save_call_WritePackage` | site | E8 -> 0xAAC110 | sim |  | [15 Phase 7](../15-native-patches.md) | bytes |
| `0xAB1319` | `Save_travel_stop` | site |  | sim | A travel save ends here | [15 Phase 7](../15-native-patches.md) | doc |
| `0xAB134E` | `Save_call_Backup` | site | E8 -> 0xAB0F80 | sim |  | [15 Phase 7](../15-native-patches.md) | bytes |
| `0xAB1363` | `Save_call_CopyNew` | site | E8 -> 0xAB0E80 | sim |  | [15 Phase 7](../15-native-patches.md) | bytes |
| `0xAB1410` | `Save_call_Finalize` | site | E8 -> 0xAAF5F0 | sim |  | [15 Phase 7](../15-native-patches.md) | bytes |
| `0xC6D460` | `World_Save` | fn |  | sim | Lots, objects, script state (OBJS). Order: World.ini resource, listeners, 3 package copies, dirty lots (0xC6BCC0), lot index (0xC61DC0), lots (0xC62860) | [15 Phase 7](../15-native-patches.md), [25](../25-save-world-write.md) | fn |
| `0x4DDBD0` | `Stream_WriteU32` | fn | cdecl(stream, value, order) | any | Byte swap unless order == 1, then stream vtable +0x38 Write(ptr, 4) [verified]; `0x4DDCC0` has the same body | [25](../25-save-world-write.md) | fn |
| `0x4DEFF0` | `MemStream_Write` | fn | thiscall(src, n) | any | memcpy into a buffer that grows by a float factor [verified] | [25](../25-save-world-write.md) | fn |
| `0x5680A0` | `SaveCtx_WriteU32` | fn | thiscall(ctx, value) | sim | Stream_WriteU32 on ctx+8 with order ctx+0x10, error into the ctx [verified] | [25](../25-save-world-write.md) | fn |
| `0x7D8290` | `SaveCtx_BeginResource` | fn |  | sim | Opens a resource by key in the save package [inferred from callers] | [25](../25-save-world-write.md) | fn |
| `0x7D7840` | `SaveCtx_EndResource` | fn | fastcall(ctx) | sim | Stream close (+0x20) and release (+0x08); RefPack compression runs under it [body verified; compression from stacks] | [25](../25-save-world-write.md) | fn |
| `0x7D8F00` | `Package_CopyResources` | fn | (dst, src, filter, ...) | sim | Dispatches on package kind ids 0x3DB0260 / 0x2A43BD44 / 0x3C4B9A4 [verified] | [25](../25-save-world-write.md) | fn |
| `0xC63D90` | `World_SaveIni` | fn |  | sim | Writes a resource named by the World.ini string [verified string] | [25](../25-save-world-write.md) | fn |
| `0xC61DC0` | `World_SaveLotIndex` | fn | thiscall(world, ctx, ...) | sim | Version 2, lot count, per lot 64-bit id + lot vtable +0x40 value; then the world lot id [verified] | [25](../25-save-world-write.md) | fn |
| `0xC62860` | `World_SaveLots` | fn | thiscall(world, ctx, ...) | sim | Lot vector world+0x9C, lot vtable +0x1C each, world lot world+0xB0, script state 0x937100 [verified] | [25](../25-save-world-write.md) | fn |
| `0xACB8A0` | `Lot_Save` | fn | thiscall(lot, ctx, a, b) | sim | Per lot: header, imposter (0xAE0540), routing (0xAB1E60), layers (0xAC5E30), objects (0xAB49C0); ~3.6 of the ~4.1 s native world write on BP [verified + measured] | [25](../25-save-world-write.md) | fn |
| `0xAB1E60` | `Lot_SaveRouting` | fn | thiscall(lot, ctx, ...) | sim | Calls RoutingWorld_SaveLot when ctx ok and the routing world exists [verified] | [25](../25-save-world-write.md) | fn |
| `0x82C820` | `RoutingWorld_SaveLot` | fn | thiscall(world, lotId lo, hi, ctx, flag) | sim | Under the routing lock: resource 0x046A7235 (0x63C690, 0x657390) and 0x07CD07EC (0x662A20) [verified] | [25](../25-save-world-write.md) | fn |
| `0x63C690` | `LotRouting_SaveLevels` | fn |  | sim | Optional lot rebuild (0x82B810), save mode on P (0x62C670), per level L (lot graph +0x74) LotLevel_SaveFootprints, then the footprint save list (0x62B2A0) [verified] | [25](../25-save-world-write.md) | fn |
| `0x63C850` | `SaveLevels_call_SaveFootprints` | site | E8 -> 0x6663C0, ecx = L | sim | Hook point for the owner-index option | [25](../25-save-world-write.md) | doc |
| `0x6663C0` | `LotLevel_SaveFootprints` | fn | thiscall(L, ctx) | sim | Scans the whole footprint map [[L+0x20]+0x24] (P's, world-wide) per level, keeps entries owned by L, refcount +1/-1 on every entry: O(levels x entries), ~1.2 s per BP save [verified + measured] | [25](../25-save-world-write.md) | fn |
| `0x664EA0` | `FootprintQuadTree_SaveNode` | fn | cdecl, recursive | sim | Inner nodes: mask + 4 children; leaves: entry indices from the per-level save map [verified] | [25](../25-save-world-write.md) | fn |
| `0x62B1E0` | `PathPlanner_FootprintSaveIndex` | fn | thiscall(P, footprint) | sim | Find-or-assign the footprint's save index (P+0x9C map, counter P+0xA4, list P+0xA0) [verified] | [25](../25-save-world-write.md) | fn |
| `0x62C670` | `PathPlanner_SetSaveMode` | fn | thiscall(P, on) | sim | Creates/frees "Routing/PathPlanner/FootprintSaveMap" and "FootprintLoadMap" [string refs verified, meaning inferred] | [25](../25-save-world-write.md) | fn |
| `0x62B2A0` | `PathPlanner_WriteFootprintSaveList` | fn |  | sim | Uses "Routing/PathPlanner/FootprintSaveList" [string refs verified, meaning inferred] | [25](../25-save-world-write.md) | fn |
| `0xD38B00` | `HashMapU32_Find` | fn | thiscall(map, out, key*) | any | EASTL hash map lookup, node next at +8 [verified] | [25](../25-save-world-write.md) | fn |
| `0xB34440` | `HashMapU32_Insert` | fn | thiscall(map, out, kv*) | any | Insert, allocates a 12-byte node through 0x4E3F90, rehash 0x769AD0 [verified] | [25](../25-save-world-write.md) | fn |
| `0x5492E0` | `List_Find4Key` | fn | cdecl(head, key*) | any | Linked list search on a 16-byte key, next at +0x10 [verified]; minor in saves | [25](../25-save-world-write.md) | fn |
| `0xAC5E30` | `Lot_SaveLayers` | fn |  | sim | Deck foundation layer, data layers and ~15 sub-writers [verified strings] | [25](../25-save-world-write.md) | fn |
| `0xAB49C0` | `Lot_SaveObjects` | fn | thiscall(lot, ctx, ...) | sim | Lot vtable +0x28 = 0xC6F520 [verified] | [25](../25-save-world-write.md) | fn |
| `0xC6F520` | `Lot_SaveObjectGroup` | fn |  | sim | Resource 0x4D1A5589, per object 0x939220; calls ScriptCore.LoadSaveManager.SaveObjectGroup [verified string] | [25](../25-save-world-write.md) | fn |
| `0x769CB0` | `Script_BeginSaveObjects` | fn |  | sim | Calls ScriptCore.LoadSaveManager.BeginSaveObjects [verified string] | [25](../25-save-world-write.md) | fn |
| `0xAE0540` | `Lot_BuildImposter` | fn |  | sim | Wrapper of 0xADAF60; called by Lot_Save when lot+0x364 is set [verified] | [25](../25-save-world-write.md) | fn |
| `0xADAF60` | `LotImposter_Build` | fn |  | sim | Creates the imposter job (JobManager 0x599160 vtable +0x10) and waits through 0xAD97E0 [verified] | [25](../25-save-world-write.md) | fn |
| `0xAD97E0` | `Job_WaitOrPump` | fn | fastcall(job) | sim | Pumps frames itself on the thread 0x7D99D0 returns, else JobManager_Wait [verified] | [25](../25-save-world-write.md) | fn |
| `0x59A500` | `JobManager_Wait` | fn | fastcall(job) | any | Takes the JobManager lock, 0x59A220 [verified] | [25](../25-save-world-write.md) | fn |
| `0x59A220` | `JobManager_WaitLocked` | fn | thiscall(mgr, job) | any | Wait node ("Services/JobManager/WaitNode"), drops the lock, Semaphore_Wait until job state 0/1/6/7 [verified] | [25](../25-save-world-write.md) | fn |
| `0x4E2760` | `Semaphore_Wait` | fn | thiscall(sem, deadline*) | any | WaitForSingleObject with count; S3SS hooks it. In saves: the imposter job waits, ~0.13 s time-weighted (not 0.9 s) [verified + measured] | [25](../25-save-world-write.md) | fn |
| `0x939220` | `Object_SaveRecord` | fn | thiscall(obj, ?, ctx) | sim | One object's entry inside the lot's `0x4D1A5589` resource: a masked id list, three resource keys, then `0x935B50` (component/script part). Unchanged objects still change bytes here because the stored script indexes shift [verified decompile; index meaning inferred from save diffs] | [26](../26-incremental-save.md) | fn |
| `0x937100` | `ScriptState_SaveSections` | fn | thiscall(state, ctx) | sim | Writes a 0x101 tag, a section count, then each section through its vtable `+0x20` with a back-patched length; called from World_SaveLots after all lots [verified] | [26](../26-incremental-save.md) | fn |
| `0xC2AEF0` | `World_SaveTerrainData` | fn | thiscall(?, ctx, ...) | sim | Resource type `0x9063660D` ("Terrain/Data" string): world fields, per-lot terrain blocks and lists [verified decompile; name inferred] | [26](../26-incremental-save.md) | fn |
| `0x788170` | `IC_World_MarkLotWorthDirty` | fn | internal call (lotId lo, hi) | sim | Looks the lot up (`0xC62D40`) and calls `0xAB2400` on it: a lot-value recompute flag, not a save flag [verified decompile; meaning inferred] | [26](../26-incremental-save.md) | fn |

## Rendering and frame loop

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x593590` | `SimRender_TripleBufferSync` | fn |  | sim | EnterCriticalSection wait; 0.6% | [20](../20-offload-candidates.md) | fn |
| `0x593E60` | `RenderMessageBuffer_Post` | fn |  | sim |  | [21](../21-cas-offload.md), [22](../22-parallel-animation.md) | fn |
| `0x60E2C0` | `GraphicsDevice_Get` | fn |  | main |  | [04 F19](../04-re-findings.md) | fn |
| `0x611620` | `Device_BeginScene` | fn |  | main | device vtable +0xA4 | [04 F19](../04-re-findings.md) | fn |
| `0x611680` | `Device_Present` | fn |  | main | EndScene +0xA8, Present +0x44 | [04 F19](../04-re-findings.md) | fn |
| `0x611760` | `Device_EndSceneAndPresent` | fn |  | main | -> 0x611680 | [04 F19](../04-re-findings.md) | fn |
| `0x692F70` | `PostRenderMessage` | fn |  | sim |  | [21](../21-cas-offload.md), [22](../22-parallel-animation.md) | fn |
| `0x936C90` | `GlobalHandlerChain` | fn |  | sim | Over DAT_011DC2A8[] | [22](../22-parallel-animation.md) | fn |
| `0xEC6370` | `LoadingScreen_Present_B` | fn |  | main | Calls 0x611760 directly | [04 F19](../04-re-findings.md) | fn |
| `0xEC9CD0` | `LoadingScreen_Present_A` | fn |  | main | Calls 0x611760 directly | [04 F19](../04-re-findings.md) | fn |
| `0xEC9F00` | `Frame_RenderAndPresent` | fn | thiscall(bool present), ret 4 | main/render | BeginScene, render, StreamHost flush, EndScene+Present when present != 0, inactive-window sleep. 0xEC9F00-0xEC9FDE | [04 F19](../04-re-findings.md) | bytes |
| `0xEC9FBA` | `Frame_inactive_window_limiter` | site |  | main | S3SS Smooth Patch (Precise) anchor | [03 anchors](../03-prior-art-re.md), [04 F19](../04-re-findings.md) | bytes |
| `0xECA030` | `Frame_RenderCallback` | fn |  | main | Run by 0x9DE140, present = 0 | [04 F19](../04-re-findings.md) | fn |
| `0xECA2A0` | `WndMsg_Render` | fn |  | main | Window-message path (via 0xECA350), present = 1 | [04 F19](../04-re-findings.md) | fn |
| `0xECA960` | `MainLoop` | fn |  | main | Runs until state 0x28D869F | [04 F19](../04-re-findings.md) | fn |
| `0xECAAFC` | `MainLoop_call_Frame` | site |  | main | present = 1 once per iteration | [04 F19](../04-re-findings.md) | doc |
| `0xECBD00` | `App_RunLoop` | fn |  | main |  | [04 F19](../04-re-findings.md) | fn |
| `0xECBE30` | `App_Run` | fn |  | main | __tmainCRTStartup -> here -> 0xECBD00 | [04 F19](../04-re-findings.md) | fn |

## Animation

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x54C720` | `Pose_Evaluate` | fn |  | sim |  | [22](../22-parallel-animation.md) | fn |
| `0x565A80` | `AnimController_ctor` | fn |  | sim | id = counter 0x114F590++ without a lock | [22](../22-parallel-animation.md) | fn |
| `0x566070` | `Track_Evaluate` | fn |  | sim | Writes global 0x11CD7D8 | [22](../22-parallel-animation.md) | fn |
| `0x568710` | `Clip_FireEvents` | fn |  | sim | Calls every registered delegate OnEvent | [22](../22-parallel-animation.md) | fn |
| `0x5689A0` | `Clip_WalkEvents` | fn |  | sim |  | [22](../22-parallel-animation.md) | fn |
| `0x568E10` | `ClipController_Advance` | fn |  | sim | vtable 0xFC1B48 +8; fires clip events synchronously | [22](../22-parallel-animation.md) | fn |
| `0x571A50` | `Animation_DrainDeferred` | fn |  | sim |  | [22](../22-parallel-animation.md) | fn |
| `0x571AD0` | `Animation_SACSPass` | fn |  | sim | Per actor SACS driver 0x94E9C0 | [22](../22-parallel-animation.md) | fn |
| `0x571B90` | `Animation_UpdateActors` | fn |  | sim | Per-actor loop: time step 0x942C00, rig pose 0x940430; 3.3% | [20](../20-offload-candidates.md), [22](../22-parallel-animation.md) | fn |
| `0x5748A0` | `RigPosePool_Alloc` | fn |  | sim | Critical section 0x11CD8D8 | [22](../22-parallel-animation.md) | fn |
| `0x5833C0` | `TrackManager_BlendTracks` | fn |  | sim | Pose evaluation (the only parallel-safe slice) | [22](../22-parallel-animation.md) | fn |
| `0x583AB0` | `TrackManager_OnEventSwitch` | fn |  | sim | Parent/Unparent, Sound, Effect, Visibility | [22](../22-parallel-animation.md) | fn |
| `0x583D00` | `TrackManager_Update` | fn |  | sim | Advance all tracks | [22](../22-parallel-animation.md) | fn |
| `0x805870` | `WalkerClipMonitor_OnEvent` | fn |  | sim | vtable 0x1020348 +0x10 | [22](../22-parallel-animation.md) | fn |
| `0x940430` | `RigPose_Update` | fn |  | sim | "Animation/RigPose" | [20](../20-offload-candidates.md), [22](../22-parallel-animation.md) | fn |
| `0x942C00` | `Actor_TimeStep` | fn |  | sim |  | [22](../22-parallel-animation.md) | fn |
| `0x942C50` | `Actor_PoseSubSteps` | fn |  | sim | Calls 0x940430 in fixed sub-steps | [22](../22-parallel-animation.md) | fn |
| `0x947590` | `Locomotion_Notify` | fn |  | sim | Into the routing/locomotion library | [22](../22-parallel-animation.md) | fn |
| `0x94E9C0` | `SACS_DriverUpdate` | fn |  | sim | Pushes Scripting/EventQueue events | [22](../22-parallel-animation.md) | fn |
| `0x959670` | `Transform_Publish` | fn |  | sim | -> 0x958430 -> 0x692F70 | [22](../22-parallel-animation.md) | fn |
| `0xFC1B48` | `vt_ClipController` | vtable |  |  |  | [22](../22-parallel-animation.md) | doc |
| `0xFCBA70` | `vt_TrackManager` | vtable |  |  |  | [22](../22-parallel-animation.md) | doc |
| `0x1020348` | `vt_WalkerClipMonitor` | vtable |  |  |  | [22](../22-parallel-animation.md) | doc |

## Strings and localization

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x59AF50` | `STBL_Find` | fn |  | any | S3PP StblBucketLayoutTuning range | [17 §8](../17-heavy-store-content.md) | fn |
| `0x59C5B0` | `LocStrSvc_GetString_A` | fn |  | any | S3PP LocalizedStringCache detours it | [17 §8](../17-heavy-store-content.md) | fn |
| `0x59F6C0` | `STBL_Clear` | fn |  | any |  | [17 §8](../17-heavy-store-content.md) | fn |
| `0x59F750` | `STBL_FindOrInsert` | fn |  | any |  | [17 §8](../17-heavy-store-content.md) | fn |

## Online features

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x87A330` | `SimpleFileDownload` | fn |  | worker | 0.5-1+ s per request, synchronous | [16](../16-online-features.md) | fn |
| `0x9B0720` | `VersionCheck` | fn |  | main | Skipped with -noversioncheck | [16](../16-online-features.md) | fn |
| `0x9F53E0` | `gSOAP_wininet_plugin` | fn |  | worker |  | [16](../16-online-features.md) | fn |
| `0x9F5470` | `Net_InternetGetConnectedState` | fn |  | any | Only calls InternetGetConnectedState | [16](../16-online-features.md) | fn |
| `0xECB600` | `App_StartupFlags` | fn |  | main | Sets the RADSDK enable flag | [16](../16-online-features.md) | fn |
| `0xF73760` | `UPnP_Discovery` | fn |  | worker | Skipped with -noupnp | [16](../16-online-features.md) | fn |

## Misc

| Address | Our name | Kind | Signature / convention | Thread | Notes | Source | Check |
|---|---|---|---|---|---|---|---|
| `0x58D9B0` | `SigAbrtHandler_int3` | site |  | any | Game SIGABRT handler hits int3 here after an interpreter assert | [18](../18-route-search-off-thread.md) | doc |
| `0xF7F371` | `entry` | fn |  | main | PE entry point | [15 Phase 1](../15-native-patches.md) | fn |
