# 29. Mono JIT feasibility and a second managed thread (PLAN lane H, H4)

Static analysis only (Ghidra headless on the `game-copy/ghidra-h1` copy project, string scan of the Steam `TS3W.exe`, decompiled `mscorlib.dll`). No game run. Addresses are for the Steam exe, image base `0x400000`. Tags: [verified] = seen in the binary or decompile, [community-reported], [inferred] = reasoning, not yet confirmed.

Questions: (1) which Mono is embedded, why there is no JIT, and what it would take to speed up script execution (~430 ms/s of sim-thread time: interpreter 28.5% + Mono runtime 14.8%, docs/28 N1+N2 [inferred: sample shares read as ms/s of a busy thread]); (2) can any C# run on a second thread (docs/28 open question 1).

## Summary

- **Mono version:** class libraries are **Mono 1.2.3** (`mscorlib` `Consts.MonoVersion = "1.2.3.0"`, `Environment.mono_corlib_version = 54`, metadata runtime string `v2.0.50727`, i.e. the 2.0 profile) [verified]. The runtime is an EA branch of Mono from the same era: the **old "mint" interpreter** (`..\..\mono\mono\interpreter\interp.c`, `mintops.c`, `transform.c`), `mono\eglib`, `mono\arch\x86\tramp.c`, and Boehm GC with a Boehm 6-style Win32 thread table (§3.1) [verified strings]. Exact upstream tag of the runtime is not printed anywhere [inferred: 1.2.x].
- **JIT verdict: not feasible.** There is no JIT in the exe (no `mini` source paths, no "JIT" string, only `tramp.c` emits x86 code, for native-call trampolines) [verified]. Swapping in a period JIT (option a) breaks on EA's task model (yieldable script tasks keep their interpreter frames in GC-heap memory and resume via `setjmp`, §1.3), on hundreds of host sites that read interpreter internals, and on the GC (§3). Estimated cost: person-months to years; risk: very high. Not recommended.
- **Best option: (b) + (c), incremental.** Native or cheaper-C# replacements for the hottest managed methods through the existing patch host (docs/04 F8), plus caches on the hottest Mono runtime helpers (type checks, IC trampolines, method lookup). Realistic total **~40-120 ms/s** of the ~430 ms/s, in steps of 5-30 ms/s that can each be A/B tested [inferred].
- **Second managed thread: not safe today, but no longer a hard wall.** The GC registers only the thread that initialised it, scans only the collecting thread's own stack, and allocates without a lock; the interpreter keeps the current task in a process global [verified]. The interpreter TLS slot itself is per thread and method compilation is locked [verified]. A native patch (register worker threads, replace the GC's `push_other_roots` hook, lock allocation) could make a worker GC-safe, ~1-2 weeks [inferred]. Shared game state (docs/28 blocker S) remains the larger cost for autonomy offload.

## 1. Embedded Mono and why there is no JIT

### 1.1 Version evidence

| Evidence | Value | Tag |
|---|---|---|
| `mscorlib` `Consts.MonoVersion` / `FxVersion` | `1.2.3.0` / `2.0.0.0` | verified (decompiled `mscorlib/Consts.cs`) |
| `System.Environment.mono_corlib_version` | 54 | verified |
| Metadata version string of `mscorlib`, `System`, `ScriptCore` | `v2.0.50727` | verified (BSJB header) |
| Runtime source paths | `mono\mono\interpreter\{interp,mintops,transform}.c`, `mono\mono\metadata\{appdomain,assembly,boehm-gc,class,gc,icall,loader,marshal,object,reflection,threads,...}.c`, `mono\mono\arch\x86\tramp.c`, `mono\eglib\src\*.c`, `mono\mono\utils\*` | verified (string scan) |
| JIT paths (`mono\mono\mini\*`), "JIT", AOT strings | none | verified (absence) |
| Corlib version check message ("expected interface version") | none: the check is removed or never built | verified (absence) |
| Interpreter flavour | old mint (2004-2008 line, before upstream removed it); not the 2017+ `mono/mini/interp` | verified path; dating community-reported (Mono history) |

### 1.2 Method compile path

- Interpreter main loop `0xE53EB0` (`ves_exec_method_with_context`): one `switch` on a 16-bit mint opcode, ~447 case labels, a single indirect jump per instruction (no threaded dispatch); "Unimplemented opcode" at `0xE59708` [verified].
- Lazy compile ("transform", IL to mint): `0xE643B0`, called from `0xE53EB0` (call opcodes), `0xE52B90` and `0xE52B60` (runtime-invoke entry; `0xE52B60` <- `0xD83680`). It takes the critical section `0x11F4174` around the work [verified]. Code generation proper is `0xE5CA90` (most `transform.c` asserts; MonoPatcher's `GenerateCode`, docs/03), result stored as the method's interpreter info (`MonoMethod+0x14`, docs/04 F8).
- Native calls (ICs, P/Invoke): `0xE51400` (`tramp.c`) builds an x86 trampoline per signature, reached from `0xE643B0` and `0xE53EB0` [verified]. This is the only runtime code generation in the exe [verified: only `tramp.c` and `mintops`/`transform` reference code emission strings].
- Interpreter init `0xE5A680`: `TlsAlloc` into `[0x11F4160]`, registers the interpreter's own ICs (`StackFrame::get_frame_info`, `StackTrace::get_trace`, `Delegate::CreateDelegate_internal`, `mono_thread_interruption_checkpoint`) through `mono_add_internal_call` `0xE82550` [verified].
- EA's interpreter binds call targets when a caller is transformed (docs/04 F8) [verified there]. That is already a call-site cache; plain "inline caches for calls" (docs/23 #15) would gain little on direct calls [inferred].

### 1.3 Why no JIT (and why adding one is so hard)

- Why EA chose it [inferred]: one script runtime for all platforms of the time (Mac PowerPC and Intel builds, where Mono 1.2's JIT support differed), predictable memory, and microthreads. Mint lets script tasks suspend mid-method; a JIT cannot do that without per-task native stacks.
- **Task resume `0xE59FD0`** [verified decompile]: each script task (`pState`, docs/18) owns a `ThreadContext` (`pState[5]`) and, unless flag `+0x14B` is set, its frame and argument storage are allocated with `GC_malloc` `0xE4BE30` instead of on the C stack. The current context is put in TLS slot `[0x11F4160]`, `setjmp` guards the run, and `0xE53EB0` executes. So a yielding task's interpreter state lives in GC-heap memory and survives between ticks. JIT-compiled frames live on the native stack and would need fibers or stack copying per task, and the GC would then have to scan every task stack (it scans one, §3.1).

## 2. Option comparison against ~430 ms/s

| Option | What | Cost | Risk | Gain [inferred] |
|---|---|---|---|---|
| (a) Period Mono with JIT at the embedding boundary | Build Mono 1.2.3 `mini` for Win32 x86, redirect the exe's `mono_*` call sites to it | Very high: Mono is statically linked (no exports, every API call is an internal call site); EA host code reads interpreter structures directly (`ThreadContext` chain, `[0x11F4170]`, `MonoMethod+0x10/+0x14`, `host+0xBC8/+0xBD0`, docs/04 F6a, docs/18); 1,759 ICs use the mint trampoline convention; yieldable tasks need a new microthread model (§1.3); two runtimes cannot share the one Boehm heap, and the exe's GC scans one stack (§3.1); mods that patch interpreter internals (MonoPatcher, our patch host F8) break | Very high: crashes, save compatibility (managed heap layout, `PersistWriter`), mod compatibility | On paper 200-300 ms/s (interpreter self time down 5-10x, runtime helpers partly); in practice unreachable |
| (b) Native or cheaper replacements for hot managed methods | Patch host (F8) swaps a C# method for a faster C# version or a stub that calls a new native IC | Low-medium per method; patches apply at startup only (F8) | Low-medium; per-method A/B (`--ab`) | 5-30 ms/s per method. Pure-compute candidates first: motive math (docs/28 S4, 4.2%), query helpers (S7), comparer/boxing hot spots like the fixed walk-style `Enum.Equals` (F11). Not autonomy `Test()` or interaction bodies (arbitrary script). Total ~20-60 ms/s |
| (c) Interpreter and runtime fast paths | Detours on runtime helpers with caches: type check `0xEA8720` (2.2% self), IC trampoline/transition `0xDA5970`, ntdll lock waits (~1.5%) on single-threaded paths, method/vtable lookups; later, hot opcode handlers | Medium; native, needs per-helper RE | Medium (cache invalidation is simple: classes never unload in this game [inferred]) | Targets N2 (~148 ms/s): ~20-60 ms/s. Dispatch rewrite of `0xE53EB0` (threaded code, superinstructions) is a binary rewrite of a 4,400-line function: high risk, ~10-30 ms/s |
| (d) Own template compiler for hot leaf methods | Compile mint code of non-yielding leaf methods to x86 that keeps locals in the same interpreter frame memory | High (weeks-months) | High | Possibly 2-3x on those methods; only after (b)/(c) show which methods matter |

**Recommendation:** (b) and (c) as small, measurable steps. First step: a native sample split of N2 (which runtime helpers, called from which opcodes or ICs), then a cache on the top one; in parallel, one (b) candidate per lane-G run.

## 3. Can C# run on a second thread?

### 3.1 GC thread support [verified unless tagged]

- The Win32 thread machinery is compiled in. docs/28 (H2) said the exe has none of Boehm's thread strings; that was a case mismatch: `"too many threads"`, `"SuspendThread failed"`, `"ResumeThread failed"`, `"GC_stop_world() called before GC_thr_init()"` and `"Failed to find stack base in GC_new_thread"` are present (`0x108E214`-`0x108E29C`). `"Collecting from unknown thread"` is absent.
- Thread table: 256 entries of 0x14 bytes at `0x1221000` (in-use, thread id, duplicated handle, stack base, suspended), max index `[0x11F4124]`.
- **Only one thread is ever registered.** `GC_new_thread` `0xE51070` (registers the *calling* thread) has a single caller, `GC_thr_init` `0xE513D0`, called only from `GC_init` `0xE4E160` (lazy, from the first allocation or collection). There is no `GC_CreateThread`, no `DllMain` attach path and no `GC_register_my_thread`.
- `GC_stop_world` `0xE511A0` / `GC_start_world` `0xE51290` suspend and resume every registered thread except the caller. With one entry, nothing is suspended unless a non-registered thread collects.
- **Only the collecting thread's stack is scanned.** `GC_push_roots` `0xE4D910` pushes static roots, the current registers (`0xE4BA10`) and then calls the hook `GC_push_other_roots` (`[0x11949CC]` = EA's `0xD71820`). `0xD71820` pushes from its own stack pointer to the end of the `VirtualQuery` region of that page, i.e. the current thread's stack only. No code reads the table's stack bases for pushing (refs to `0x122100C` are only `GC_new_thread`, `GC_stop_world` and `GC_get_next_stack` `0xE51360`, the root-exclusion helper), and the exe does not import `GetThreadContext`.
- **Allocation has no lock.** The `GC_malloc` small-object fast path `0xE4BE30` pops the free list without any lock; the critical section `0x1222580` is taken only by `GC_stop_world`.
- Marking runs only on the collecting thread; no parallel marker (docs/13).

### 3.2 Interpreter and host state

- TLS slot `[0x11F4160]` comes from `TlsAlloc` (`0xE5A680`), so every thread has its own value; a new thread starts with NULL and `0xE59FD0` creates a context. **Not a blocker** [verified].
- Transform (lazy compile) is serialized by critical section `0x11F4174` [verified]. Mono's metadata and loader code takes its own locks (ntdll lock time in docs/28 N2) [inferred: stock Mono 1.2 design].
- **Blocker:** the current interpreter state `[0x11F4170]` is a plain global, saved and restored by `0xE53EB0` on entry and exit and read by `0xE534E0` (frame lookup by depth `+0x140`) [verified]. Two threads in the interpreter overwrite each other's value.
- **Blocker:** the host's running-task slots `host+0xBC8` and `host+0xBD0/+0xBD4` are plain fields of the single `MonoScriptHost` (docs/18; a worker call that zeroed `+0xBD0` crashed) [verified there]. Yields, sleeps and host callbacks assume the sim thread.

### 3.3 Verdict

**No C# may run off the sim thread today** [verified, four independent reasons]: a worker is not suspended during collections, its stack and registers are never scanned (objects only it references can be freed), a collection it triggers scans only its own stack and misses the sim thread's, and its allocations race the sim thread on unlocked free lists. Even without GC, `[0x11F4170]` and `host+0xBD0` would be corrupted.

What a fix would take [inferred, design only]:
1. Call `GC_new_thread` `0xE51070` on each worker at start, so `GC_stop_world` suspends it (the machinery exists).
2. Replace the pointer at `[0x11949CC]` with a function that pushes the current stack (as `0xD71820` does) plus, for every other registered and suspended thread, its registers and stack from `GetThreadContext` (resolved with `GetProcAddress`) up to the table's stack base. This is what stock Boehm `GC_push_all_stacks` does.
3. Put a lock around the allocation entry points (`GC_malloc` family, Mono object allocation), or keep worker code allocation-free. Uncontended lock cost on the sim thread is small but must be measured.
4. Give the interpreter's `[0x11F4170]` a per-thread value (patch its few readers and writers to a TLS slot), and keep workers away from yields, sleeps and host callbacks.
5. Then the docs/28 S/E blockers: autonomy reads live world state that other tasks mutate (needs a snapshot). Cost estimate for steps 1-4: ~1-2 weeks plus a stress test; the snapshot work for autonomy is larger.

Test plan, when this is picked up: a dummy worker that allocates and drops managed objects in a loop while the sim thread runs and collects (probe-safety rule), first with steps 1-3, watching for heap corruption.

## 4. GC allocation off the sim thread

Question: does any thread other than the sim thread allocate from the Boehm heap today? With the lock-free fast path (3.1) that would be a latent heap-corruption bug and a suspect for crashes such as 2026-10-14 11:58 PHT (PLAN.md). Static analysis only (Ghidra headless on a copy of the H1 project, direct-call graph).

**Verdict: no other-thread allocation path found; not provable statically.** Every thread entry point and every one of our worker targets reaches the allocators only through indirect calls (runnable vtables, job callbacks), which the call graph cannot follow. Latent bug: **unknown, no evidence for it** [verified for direct calls; indirect dispatch unresolved].

### 4.1 Allocator set [verified]

- Core: `GC_malloc` fast path `0xE4BE30` (31 direct callers), `GC_allocobj` `0xE4A2B0` (1 caller, `0xE4BBB0`), `GC_alloc_large` `0xE4BA90` (callers `0xE4BB50`, `0xE4BCE0`: the `GC_malloc_atomic` / uncollectable / ignore-off-page family), `mono_gc_malloc` `0xE4BF90`. Mono's `mono_object_new` `0xEA36F0` (291 callers), `mono_string_new` `0xEA40B0` (60), `mono_array_new` `0xEA30F0` (109) all end there.
- Reverse closure of the three core functions over direct calls: 2706 functions. Its roots (functions with no direct caller, so entered through a pointer): 188 inside the Mono runtime (`0xE40000`-`0xEC0000`), 31 `MonoScriptHost` methods in `0xD7`/`0xD8` (vtables `0x108B690`/`0x108B818` set by the ctor `0xD7E010`, plus a class with a vtable near `0x108C618`), and about 1630 functions in `0xD90000`-`0xE40000`.
- The `0xD9`-`0xE3` roots are per-class script bindings: each resolves managed class, method and field handles into globals (sample `0xD9B0C0`), and they are registered into a table by `0xD9A860` from large init functions such as `0xE3E570` [verified for the sample and the registrar]. None of them is a Mono internal-call target (0 of 1663 in `internal-calls-map.csv`). They run when the host binds script classes, on the host's thread [inferred].

### 4.2 Thread entry points [verified]

| Creator | Start routine(s) | Direct path to an allocator |
|---|---|---|
| `CreateThread` in `0xF6F0F0`, `0xF6F570`, `0xF6F900`, and an undefined block at `0xF49820` | `0xF6F070`, `0xF76EE0`, `0xF6F790`, `0xF49730`, `0xF4A680`, `0xF4A9E0` (a third-party library block, `0xF4`-`0xF7`) | none (BFS depth 25; up to 131 functions reached, 28 indirect calls) |
| `_beginthreadex` in `0x4DB290` (file watchers, docs/03) | `0x4DB220`; callers `0x409820`, `0x4A43C0`, `0x737950` | none (30 indirect calls) |
| EA thread class `0x4E2F30` (9 callers, among them `JobManager_JobThread` `0x599CF0`, `0x401CA0`, `0x76B610`, `0x8B3C20`, `0xEDC5C0`, `0xF00D40`, `0xF1CF50`) and `0x4E31A0` (`0x4E9100`, `0x9E6350`) | generic starts `0x4E2E30` / `0x4E30A0`, which call the runnable through its vtable | none from the starts or any function-pointer argument found at the call sites (e.g. `0x4FBF60`, `0x58D770`, `0x9E5F70`, `0x4E2C10`) |

So all OS threads funnel through indirect dispatch before any allocator is reachable. Which runnable runs the sim loop (`MonoScriptHost_Simulate` `0xD81840`, vtable slot `0x108B6B0`, no direct callers) is not resolvable this way either.

### 4.3 GC_init thread

- `GC_init` `0xE4E160` is called explicitly during Mono start-up, not only lazily from the first allocation (so this is most likely the first call [inferred]): `mono_interp_init` `0xE5A680` -> `0xE66240` -> `0xE4E4C0` (the GC base-init wrapper; second path `0xE76910` -> `0xE75BF0` -> `0xE4E4C0`) -> `GC_init` -> `GC_thr_init` -> `GC_new_thread`. `mono_interp_init` is called at `0xD7E60C` inside `MonoScriptHost_Init` `0xD7E560` (vtable `vt_MonoScriptHost` `0x108B690`, slot 4 at `0x108B6A0`) [verified]. (Corrected 2026-10-20, docs/119 §1: this line said `0xD7E540`, vtable `0x108B818` slot 0; `0xD7E540` is a scalar deleting destructor that `0xD7E60C` does not lie in.)
- Which OS thread calls `0xD7E560` (main or sim thread) is **not resolvable statically** (vtable call). If it is the main thread while the sim runs on its own thread (`[Script] Multithreaded=1`), the sim thread is not in Boehm's table: every collection on the sim thread would then suspend the main/render thread in `GC_stop_world` (it is the one registered thread that is not the caller) and still scan only the sim stack (`0xD71820`) [inferred]. That would not corrupt the heap, but it would stall rendering during each collection and could deadlock if the main thread is suspended while holding a lock the collector needs [inferred, unconfirmed].

### 4.4 Our native workers (ts3native source) [verified]

- `SaveJobMain` and `FlushMain` (savewrite.cpp): Win32 file copy, hash and flush (`FlushTree` is our own reimplementation of `0xAAF4B0`); no game or Mono calls. The original `0xAAF4B0` only runs on the sim thread (no-thread fallback); it has no direct path to an allocator anyway (68 functions reached).
- `ScWorker` (savecompress.cpp): calls the RefPack compressor `0x4EC0A0` with our own allocator vtable; 8 functions reached, no allocator.
- Decompress (decompress.cpp): no thread of ours; the byte-loop decoder `0x4EB3B0` reaches nothing.
- Route worker `WorkerMain` (ts3native.cpp): runs `Route_PlanLocked` `0x82C6E0` (1344 functions reached directly, 875 indirect calls) and the async `IC_Route_Plan` `0x765160` (1409 reached, 959 indirect); **no direct path to an allocator**. The known managed exits (`mono_runtime_invoke` slot `0x1195B60` and host delegate call `0xD75FA0`, which boxes with `mono_value_box`) are already sent back to the caller thread (`ServeInvoke`). The residual risk is an indirect call from the planner that allocates without going through those two [unknown].

### 4.5 How to confirm at runtime

One ts3native check, no game-time cost when off:
1. At the first `MonoScriptHost_Simulate` entry (or any hook known to run on the sim thread), store the sim thread id; read Boehm's table entry 0 thread id at `0x1221004` (entry layout in 3.1) and log `gc registered tid=%u sim tid=%u main tid=%u` (main = the ASI-loading thread). This answers 4.3 in one load.
2. Counter `gcForeignAllocs`: a detour on the slow paths `GC_allocobj` `0xE4A2B0` and `GC_alloc_large` `0xE4BA90` (not the hot fast path) that compares `GetCurrentThreadId()` with the sim thread id, counts mismatches and logs the first few return addresses. A foreign thread that allocates often enough refills free lists, so it shows up there. Reserve the counter slot in docs/registry.md first.

## 5. Mono runtime split by helper (G9, 2026-10-15)

**Data:** the 16 non-empty `probe-out/profile/*simstack*.jsonl` files (10,251 samples: BP 4,077, SV 1,723, IP 4,451), split with `node tools/harness/sample-split.ts` (`--self-check` covers the attribution rules). No game run. 1% of samples is about 10 ms/s (docs/28).

**Method:** self = the leaf-most TS3W frame (samples whose leaf is in ntdll/MSVCR80/kernel32 go to that frame and count as "via OS/CRT"). Inclusive = function on any of the first 12 TS3W frames, once per sample. Caller = the next TS3W frame in a different function. "Mono runtime" = the docs/20 ranges minus the interpreter `0xE53EB0`, the Boehm block `0xE40000-0xE51400` and finalizers `0xE697F0`. This gives runtime self **11.6%** (BP 12.6, SV 11.1, IP 10.8), interpreter 27.7%, GC 11.8% [measured]. docs/20's 14.8% used nearest family over 12 frames, which also counts OS-leaf and unowned frames under runtime functions. That is why the totals differ. Inclusive numbers are approximate because part of each stack is scanned (docs/20 §6).

**Top 15 runtime helpers by self time** [measured; names from docs/06, docs/15, docs/17 and the exe bytes as tagged]:

| # | Helper | Self all | BP | SV | IP | via OS | Dominant caller (share) |
|---|---|---|---|---|---|---|---|
| 1 | `0xEA8720` `mono_metadata_type_equal` [verified, docs/17] | 2.14% | 2.80% | 1.16% | 1.91% | 0% | `0xEBBDE0` type-keyed ehash lookup (67%); chain `0xEBBDE0 <- 0xEBC180 <- 0xEA8A00` in 74 of 219 samples |
| 2 | `0xE59FD0` `interp_mono_invoke_with_state` [docs/06, from S3MM patterns] | 1.33% | 1.37% | 1.33% | 1.28% | 73% (not ntdll) | `0xD7F000` script host (82%) |
| 3 | `0xE85030` unlock of the global `CRITICAL_SECTION` `0x11F43A8` (`push 0x11F43A8; call [LeaveCriticalSection]`, 89 call sites) [verified bytes] | 1.18% | 0.88% | 1.63% | 1.28% | 93% (110/120 in ntdll) | `0xDA5970` IC transition (77%), itself under `0xDA7FB0 <- 0xE53EB0` |
| 4 | `0xE51FC0` | 0.56% | 0.47% | 0.64% | 0.61% | 0% | `0xE59FD0` (68%) |
| 5 | `0xEA3820` `mono_object_isinst` [verified, docs/17] | 0.48% | 0.47% | 0.58% | 0.45% | 2% | `0xD79800` (35%) |
| 6 | `0xEBBDE0` ehash lookup (S3PP MTEH anchor) [verified, docs/17] | 0.44% | 0.34% | 0.58% | 0.47% | 4% | `0xEBC180` (18%) |
| 7 | `0xE6FD00` | 0.36% | 0.39% | 0.52% | 0.27% | 0% | `0xEA3820` (49%) |
| 8 | `0xEA2AD0` `mono_class_vtable` [verified, docs/15] | 0.34% | 0.34% | 0.46% | 0.29% | 0% | `0xE59FD0` (77%) |
| 9 | `0xE521A0` | 0.28% | 0.34% | 0.12% | 0.29% | 10% | `0xE59FD0` (66%) |
| 10 | `0xE52630` | 0.25% | 0.29% | 0.29% | 0.20% | 23% | `0xE53EB0` (35%) |
| 11 | `0xE70D30` (only `bp-worker071`: 6.9% there, 100% in OS, under `mono_runtime_invoke` `0xEA1400`; route-worker proxy wait [inferred], not a helper) | 0.24% | 0.61% | 0% | 0% | 100% | `0xEA1400` (100%) |
| 12 | `0xEA3220` | 0.23% | 0.42% | 0.17% | 0.09% | 0% | `0xEA3820` (38%) |
| 13 | `0xE85020` lock of the same section (`call [EnterCriticalSection]`, 65 call sites) [verified bytes] | 0.22% | 0.22% | 0.17% | 0.25% | 74% | `0xDA5970` (83%) |
| 14 | `0xD8A8E0` native->managed entry (docs/18) | 0.21% | 0.27% | 0.06% | 0.22% | 55% | `0xDB3460` (23%) |
| 15 | `0xDA5970` IC transition | 0.16% | 0.15% | 0.17% | 0.16% | 81% | `0xDA7FB0` (75%) |

**Top inclusive runtime helpers** (nested, rows overlap) [measured]: `0xE59FD0` invoke 39.1% (all script work under it), `0xEBBDE0` lookup 4.6% (BP 5.1, SV 3.9, IP 4.4), `0xE53620` 4.1% (from the interpreter), `0xE85030` 4.0%, `0xD89CD0` 3.6%, `0xEA8A00` 3.0% (from `0xE52630`; most likely `mono_type_get_object`, the `MonoType -> System.Type` table docs/17 names [inferred]), `0xDA5970` 3.0% (from `0xDA7FB0`), `0xD8A8E0` 2.9%, `0xDA7FB0` 2.7%, `0xE6FD00` 2.7%, `0xEA2690` 2.3% (object allocation path), `0xE70180` 2.2%, `0xEA8720` 2.2%, `0xE7A590` 2.0%, `0xEA3820` 1.8%.

**S3PP already covers helpers 1, 5 and 6.** `MonoTypeEHashProbe` (MTEH) caches the `0xEBBDE0` lookups that compare with `0xEA8720`, and `MonoIsinstCache` (ISC) caches `0xEA3820` (docs/17 §S3PP). The installed `Sims3Performance.log` shows both armed today. Only one simstack run had S3PP loaded (`bp-worker071`, 361 samples, 19.7% of stacks with `Sims3Performance.asi` frames):

| BP run | Samples | `0xEA8720` self | `0xEBBDE0` incl | `0xEA3820` incl |
|---|---|---|---|---|
| 7 BP runs without S3PP (`bp-all`, `-detour2`, `-gc0`, `-gc1`, `-mod`, `-worker1`, `simstack-bp`) | 3,716 | 2.25-4.11% | 3.85-8.42% | 0.92-2.93% |
| `bp-worker071` (S3PP on) | 361 | **0.00%** | **1.94%** | 0.28% |

[measured; one run, ±~0.7 points at n=361, and that run also had route-worker mode on, so the cache effect is not isolated.]

**Recommendation.** The first helper to cache is the type-keyed hash lookup `0xEA8A00 -> 0xEBBDE0 -> 0xEA8720` (MonoType to System.Type). Expected gain is **~20-30 ms/s** (BP ~30, SV/IP ~20): `0xEA8720` self 2.1% plus most of the remaining lookup inclusive time, measured as 5.1% -> 1.9% in BP [inferred from the table above]. S3PP MTEH already does this, so **we should not write our own**. Confirm it with a BP run that has `/simstack` on and MTEH on vs off (S3PP ini `MonoTypeEHashProbe`, one load each; S3PP has no runtime switch for it [inferred]). Expect `0xEA8720` self near 0 and `0xEBBDE0` inclusive at <=2%.

**First target of our own (after S3PP):** the lock pair `0xE85020`/`0xE85030` on `CRITICAL_SECTION` `0x11F43A8` under the IC transition `0xDA5970`. It is 1.40% self (~14 ms/s; SV 1.8%, the highest), and 92% of its samples sit inside ntdll `LeaveCriticalSection`/`EnterCriticalSection`. An uncontended section costs tens of ns per pair, so either the lock is taken very often per IC call or another thread waits on it [inferred]. `0xDA5970` calls a registered callback `[0x11EE6CC]` (set at `0xDA594E`) once per call, from one of two call sites [verified bytes; see 5.1], and the lock frames sit directly under it [measured]. Before caching or eliding anything, read `0xDA5970`, find the callback target, and check with a ts3native counter for contention (owner thread of `0x11F43A8` when the sim thread waits). Expected 5-15 ms/s if the lock can be skipped on the sim thread [inferred]. The rest of the runtime self time is spread thin: no other helper is above 0.6% (~6 ms/s).

### 5.1 The lock under the IC transition (static, 2026-10-15)

Ghidra on a copy of the h1 project (`game-copy/ghidra-ic`), plus the existing simstack samples. No game run.

- **What `0x11F43A8` is: Mono's global `loader_mutex`** [verified]. `0xE84880` is `mono_loader_init`: `InitializeCriticalSection(0x11F43A8)` then `TlsAlloc` (the loader error slot, stored at `0x1220F38`). `0xE85020`/`0xE85030` are `mono_loader_lock`/`mono_loader_unlock`, 12 bytes each: `68 A8 43 1F 01` (push) `FF 15 2C 51 F9 00` / `FF 15 30 51 F9 00` (call `[EnterCriticalSection]`/`[LeaveCriticalSection]`) `C3`. The functions that use the section inline sit in `0xE85040-0xE85E70` and assert in `mono\metadata\loader.c` (`0xE85040` is `mono_method_signature`, it locks only when the signature is not cached yet). The lock protects the metadata loader's lazily filled state: method signatures, method/field lookups by token, the internal-call table (see below), class setup.
- **Every enter/leave site** [verified]: the wrappers have 65 lock and 89 unlock call sites in 62 functions, all inside Mono (`0xE65FE0-0xEB1E60`: class.c, loader.c, icall.c, reflection.c, marshal). Inline uses of the section: 20 `push 0x11F43A8` sites in `0xE85040`, `0xE85390`, `0xE85810`, `0xE85CB0`, `0xE85DF0` (loader.c), plus the init. No EA code (outside the Mono range) takes it directly.
- **The callback `[0x11EE6CC]`** [verified bytes, target inferred]. EA's script binding table `0xE3E570` registers `(hash 0x278643D5, bind 0xDA5910, invoke 0xDA5970, 0xDA5BB0)` through `0xD9A860`; `0xDA7FB0` is registered the same way (hash `0x3339ECB4`) and calls `0xDA5970` when `[0x11EE5B4]+0x28` is set. The bind function `0xDA5910` checks a class hash (`0xECDEB3B7`), resolves two methods of that class with `0xD9A890` (index 10 into `[0x11EE6C8]`, index `0x30` into `[0x11EE6D0]`) and stores `0xD89F00(method)` in `[0x11EE6CC]`. `0xD89F00` caches `0xE82680(method)` at `method+0x20`, and `0xE82680` is `mono_lookup_internal_call` (icall.c asserts, the "cant resolve internal call" text, takes the loader lock itself). So the callback is the **native C function behind an internal call of a script class, bound by name at run time**: its address is not in the exe as a constant, so static analysis cannot name it. `[0x11EE6CC]` is written only at `0xDA594E`. `0xDA5970` calls it once per invoke: at `0xDA5A0E` when `[0x11F2D84] < 1`, else at `0xDA59FA` inside a `setjmp` guard; it returns a bool and a list of (method, argument) pairs, and each pair is then run through `0xD8A8E0` (native to managed) or the method's `+0x28` thunk.
- **Where the lock time sits** [measured, existing samples]: 143 of 10,251 sim-thread samples have their leaf TS3W frame in a wrapper (125 of them with the PC inside ntdll). Above the wrapper's return address the next frame is `0xDA5A14` (return from the `0xDA5A0E` call) in 87 of 111 unlock and 16 of 20 lock samples, so the lock is taken by the callback (or a function it calls) and the sim takes the unguarded branch. The callback's own frame is missing from the stacks: the sampler walks EBP chains (`tools/probe/agent.js` `walk`), so the callback and anything between it and the wrapper keep no EBP frame. The only wrapper callers that are themselves registered as icalls (`0xEABA00`, `0xEABD90`, TypeBuilder setup in reflection.c) are not the callback [verified].
- **Which threads take it.** Static analysis cannot rule out other threads: any thread that runs Mono code takes it (the sim/script thread, Mono's finalizer thread, threads attached to Mono). Our route worker proxies managed invokes back to the caller thread (docs/18), so it should not [inferred].
- **Contention hypothesis** [inferred]: the unlock costs 5x the lock in self time (1.18% vs 0.22%) and 93% of it is inside ntdll. An uncontended `LeaveCriticalSection` is a few interlocked instructions and cheaper than the enter; a slow leave is what happens when threads wait and the leave has to wake one (keyed event syscall). So the lock is probably **contended**: another thread holds or waits on `loader_mutex` while the sim thread runs this icall. The other explanation is a very high call rate with no waiters, which would put more of the time in the enter. `[IcLockCheck]` (below) tells the two apart.

**Measurement hook (ts3native `[IcLockCheck]`, Enabled=0, counters v15 at +0x1DC).** Replaces both wrappers after checking their 11 code bytes, with the same lock: `TryEnterCriticalSection` first, a QPC-timed `EnterCriticalSection` only when that fails. Counters (written while the lock is held, so exact): `icLockEntersK` (enters / 1024), `icLockContended`, `icLockWaitUs`, `icLockMaxWaitUs`, `icLockOwnerChanges`, `icLockPrevTid`/`icLockLastTid`, `icLockLeaveWaiters` (unlocks with threads waiting, `-1 - (LockCount >> 2) > 0`), and `icLockCallback` (`[0x11EE6CC]` at the first enter after the bind). The log names the callback and the first 32 distinct caller addresses with their thread, which identifies the icall and the function under it. The 20 inline sites are not hooked, so counts are a lower bound. Selftest: uncontended and recursive enters, a contended enter against a second thread (wait >= 20 ms, two owner changes, one unlock with a waiter), the callback slot, and the K rollover [verified, `build.cmd`]. How to read it: `icLockContended` and `icLockLeaveWaiters` near zero with a high `icLockEntersK` = call rate (cache the icall's result or skip the lock on the sim thread); a high contended share with `icLockPrevTid` != sim thread = contention (find that thread from the log, then move or batch its loader work).

## Open points

- ~~Which runtime helpers make up N2's 14.8%~~: §5 (G9). `0x11F43A8` = `loader_mutex`, callback = an icall bound at run time (5.1). Still open: which icall (name it from the `[IcLockCheck]` log) and contended vs call rate (counters v15); names of `0xE51FC0`, `0xE6FD00`, `0xE53620`, `0xD89CD0`.
- Which OS thread calls `GC_init` (via `0xD7E560`), main or sim thread: open, see 4.3 and the runtime check in 4.5.
- Whether any EA code allocates from other threads today: no direct-call path found (section 4); indirect dispatch unresolved, runtime check in 4.5.
