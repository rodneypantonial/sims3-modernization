# 56. Mono interpreter state written per call (T-MONO-a, input to `[MonoThread]` steps 2 and 3)

Static analysis only: Ghidra headless on a copy project (copied from the UI4 project, symbols CSV imported first), Steam `TS3W.exe` 1.67, image base `0x400000`. The game was not run. Scope: every global or static that the embedded Mono (ranges `0xE20000-0xEF0000` and `0xD82000-0xDC0000`) writes while it executes a method, plus the EA host fields written per call. It checks the list in docs/30 (P1-P4, §5) and adds what that list misses.

Tags: **[verified]** = read in the disassembly, an operand scan or a Mono assert string for this note; **[inferred]** = reasoning or Mono 1.2.3 source knowledge (mint interpreter, `metadata/*.c`), not confirmed in this exe; **[community]** = stock Mono/Boehm design.

## Method

1. **Operand scan** (scratch Ghidra script): every instruction in the two ranges whose memory operand is a direct address in a non-executable block, split by write / read / address-taken, with the containing function. Accesses through a computed pointer (struct fields, `[reg+disp]`) do not show; host fields were scanned separately by displacement (`+0xBC8`, `+0xBD0`, `+0xBD4`, whole exe).
2. **Reachability:** functions reachable by direct calls (inside the ranges) from the interpreter entries `ves_exec_method_with_context` `0xE53EB0`, `interp_mono_invoke_with_state` `0xE59FD0`, `interp_runtime_invoke` `0xE5A600` and the host wrappers `0xD7EED0`, `0xD7F000`, `0xD7F210`: 846 functions. Indirect calls (opcode handlers through tables, internal calls, vtables) are not followed, so the reachable set is a lower bound; the per-range lists below cover the rest.
3. **TLS and lock census:** every call to `Tls*`, `*CriticalSection` and `Interlocked*` imports in the ranges, with the key or section pushed before it.
4. A second reachability run from the 13 P3 entry points (docs/30 §12.1) over the GC range, to find GC writers that the P3 lock would not cover.

## Summary

- **262 globals** are written by code directly reachable from the interpreter entries [verified, operand scan]: 154 in the Boehm GC, 85 in Mono metadata/runtime code (class, object, domain, marshal, loader, reflection), 19 in eglib-level code (logger, SHA-1), and **4 in the interpreter itself**. Of the 262, about 54 are written only by init-time functions (`GC_init`, `GC_thr_init`, write-watch init, size-map init).
- **The interpreter core is clean apart from P4** [verified]: `0xE53EB0` writes exactly two globals, `[0x11F4170]` and `[0x11F416C]` (docs/30 P4); `0xE59FD0`, `0xE5A600` and `0xE52B90` write no global at all, only the TLS slot `[0x11F4160]`. There is no global "current exception" or "last error": exceptions live in the interpreter frame and `ThreadContext` (per thread), last error and loader error are TLS slots.
- **Host range `0xD82000-0xDC0000`:** 1,134 globals written; 1,131 are single lazy stores (`mov [g],reg` once, after a lookup: binding-time caches of classes, methods, fields) in 273 functions. Only **3 are per-call state**, all missing from docs/30: the native-to-managed call-record list `[0x11EE5A4]` / `[0x11944C8]` / `[0x11944CC]`, and the IC nesting counter `[0x11F2D84]`.
- **Missing from docs/30** (details in §4): (1) `host+0xBD4` and four more writers/readers of `host+0xBC8`; (2) the call-record list of `ScriptHost_NativeToManaged` `0xD8A8E0`; (3) the EA IC nesting counter `[0x11F2D84]`; (4) the trampoline thunk hash `[0x11F4128]`, created and filled without a lock from inside `0xE53EB0`; (5) the GC typed-descriptor path `0xE50F90` (writes the extended descriptor table), which no P3 hook covers; (6) unlocked `mono_stats` counters `0x1220F40-0x1220F74`; (7) the full TLS key list (three more per-thread slots: loader error `[0x1220F38]`, marshal `[0x11F43C4]`, `[0x11F43E0]`); (8) lazily created hash tables in reflection and marshal (`[0x11F46F0]`, `[0x11F43D8]`); (9) the SHA-1 static workspace `[0x11F47F8]`; (10) the EA pair `[0x11F4158]`/`[0x11F415C]` reset each Simulate tick.
- **Effect on the plan:** P4 needs no new site. Step 2 (P3) should add `0xE50F90` to its hook list or the warm-up rule must cover descriptor creation. Step 4 (worker entry) needs three new "never" rules: no entry through `0xD8A8E0`, no EA internal call (now with a concrete counter to check in Verify mode), and every internal-call/P/Invoke signature the worker uses must be called once on the sim thread first (thunk hash).

## 1. TLS keys (per thread already)

All allocated once at init; every use is `TlsGetValue`/`TlsSetValue` on the calling thread [verified, import census].

| Key global | Allocated in | Role | Users | `[MonoThread]` |
|---|---|---|---|---|
| `[0x11F4160]` | `mono_interp_init` `0xE5A680` | interpreter `ThreadContext` [verified, docs/29] | get: `0xE52546`, `0xE52B60`, `0xE53136`-`0xE531D7`, `0xE53748`; set: `0xE52B90`, `0xE59FD0`, `0xE5A680` | leave; a worker starts with NULL and `0xE52B90`/`0xE59FD0` make a context [verified, docs/30 §5] |
| `[0x11959DC]` | `0xE75BF0` (domain init) | current `MonoDomain` [verified, docs/30] | get `mono_domain_get` `0xE75260`, set `0xE75270`, free `0xE75210` | worker sets it once (docs/30 §5 step 2) |
| `[0x1195B7C]` | `0xEBB180` (`mono_thread_init`) | current `MonoThread`, always NULL [verified, docs/30 §12.5] | 14 get sites in `0xEB9F40`-`0xEBB3D0`, one NULL store `0xEBADF0` | leave; no `[ThreadStatic]` on any thread |
| `[0x1220F38]` | `0xE84880` (`mono_loader_init`) | loader error slot [verified, docs/29 §5] | get/set `0xE848B0`, `0xE848C0`, `0xE85550`-`0xE862B0` | leave |
| `[0x11F43C4]` | `0xE99CB0` (marshal init) | "loading type info" marker of `mono_marshal_load_type_info` `0xE92150` [verified: its assert `!mono_marshal_is_loading_type_info (klass)`] | `0xE92150`, `0xE8E170`, `0xE920B0` | leave |
| `[0x11F43E0]` | `0xE99CB0` | `last_error_tls_id` (P/Invoke `SetLastError`) [inferred: two 2-instruction set/get stubs at `0xE8DA17`/`0xE8DA34` and `0xE8DE50`] | those stubs | leave |

SEH: GC mark code writes `fs:[0]` (`0xE4D460`), which is per thread by definition [verified].

## 2. Interpreter and EA host state written per call

| Address | Role | Per thread? | Writers | Readers | Step 2/3 action |
|---|---|---|---|---|---|
| `[0x11F4170]` | current interpreter frame (`g_curInterpState`) | global | `0xE53EB0` entry `0xE53ED7`, exits `0xE597BA`, `0xE5986C` [verified] | `0xE53EB0`, `0xE534E0` only; no reference outside Mono [verified] | **P4: TLS**, as docs/30 §4 |
| `[0x11F416C]` | base of the frame-info array (`g_curInterpFrameArray`) | global | `0xE53EB0` (3 sites, docs/30 table) [verified] | `0xE53EB0`, `0xE534E0` [verified] | **P4: TLS** |
| `host+0xBC8` | running task (`MonoScriptHost`) | global (one host object) | `ScriptHost_RunTask` `0xD7F019`, `ScriptHost_InvokeDelegate` `0xD76200`/`0xD7621F` (save, set, restore), `0xD7D593`/`0xD7D5B6` (`0xD7D2F0`, swap), ctor `0xD7E1F1`, and the same offset in `ScriptHost_OneShotObjects`/`ThreadedObjects`/`TaskObjects`/`0xD7FE80` (other host objects of the same layout [inferred]) [verified bytes] | `ScriptHost_MarkYield` `0xD70F6A`, `0xD70E90`, `0xD761EB`, `0xD7D585` [verified] | **leave, never on a worker**: worker must not call any of these (add `0xD75FA0`, `0xD7D2F0` to docs/30 §5 step 5) |
| `host+0xBD0` | running invoke / task pair, word 1 | global | `ScriptHost_InvokeDirect_A` `0xD7EF9A`/`0xD7EFB3`, `ScriptHost_RunTask` `0xD7F023`, `ScriptHost_InvokeDirect_B` `0xD7F24C`/`0xD7F270`, ctor, sibling objects [verified] | same functions, `0xD7EE2B` area [verified] | leave (docs/30 §5 step 5) |
| `host+0xBD4` | word 2 of the pair (docs/29 named it, docs/30 did not) | global | same as `+0xBD0`: `0xD7EFA0`/`0xD7EFB9`, `0xD7F02C`, `0xD7F252`/`0xD7F276` [verified] | `0xD7EE3E`, `0xD7EF88`, `0xD7F246` | leave |
| `[0x11EE5A4]` | head of the native-to-managed call-record list (`g_n2mCallList`) | global, unlocked | `0xD8A830` (pop or allocate, `0xD8A855`/`0xD8A8C3`), `ScriptHost_NativeToManaged` `0xD8A9A5` (move to `next`), resets to the sentinel in `0xD9A520`, `0xD9B0AB` [verified] | `0xD8A830`, `0xD8A8E0` | **new rule: worker never enters through `0xD8A8E0`**; if a later step needs it, make the head per thread |
| `[0x11944C8]`/`[0x11944CC]` | list sentinel / tail of all records; each record owns two `mono_gc_malloc` buffers (0x150 and 0x28 bytes) [verified] | global | `0xD8A830` (`0xD8A88E`, `0xD8A895`) | `0xD8A830` | as above |
| `[0x11F2D84]` | EA internal-call nesting counter (`g_icNestDepth`) [role inferred] | global, non-atomic `add`/`sub` | 17 EA functions, 83 `add`/`sub` sites (`0xE29AE0` and `0xE358A0` are IC bodies registered through `0xD9A860` [verified]; `0xE37D20`, `0xDA9460`, `0xDC94B0`, `0xDB94D0`, ... [IC role inferred]) | 94 sites in the host range plus over 200 further on test `> 0` (`ScriptHost_GlueCaller` `0xD9B18D`, `0xDC6630`, `0xDD4FC1`, ...) [verified] | leave; worker calls no EA IC (docs/30 §5). Verify mode: snapshot it on the sim thread around a worker run; a change means a worker reached an EA IC |
| `[0x11EE6CC]` | IC transition callback, bound once (docs/29 §5.1) | global | `0xDA594E` (bind) | `ScriptHost_ICallTrampoline` `0xDA5970` | leave (bind time) |
| 1,131 others in `0xD82000-0xDC0000` | binding-time lazy caches (class, method, field handles) | global | one `mov [g],reg` each, 273 functions [verified pattern] | the same functions | leave; idempotent aligned stores. The worker uses no EA binding |

## 3. Mono runtime state reached from a method call

Grouped; each row lists the globals the operand scan found.

| Address(es) | Role | Locked? | Writers | Step 2/3 action |
|---|---|---|---|---|
| `[0x11F4168]` | interpreter `RuntimeMethod` hash (`mono_interp_get_runtime_method`), created lazily | yes, critical section `0x11F4130` around create and insert [verified `0xE53070`-`0xE53123`] | `0xE5309F` | leave |
| transform output (`MonoMethod+0x14`) | lazy compile | yes, `0x11F4174` [verified, docs/29] | `0xE643B0` | leave; warm-up on the sim thread anyway |
| **`[0x11F4128]`** | `tramp.c` thunk hash (native-call trampolines for internal calls / P/Invoke) | **no**: created with `0xE45E50` (`g_hash_table_new`) on first use, looked up `0xE45F00`, inserted at `0xE51D2B` [verified] | `0xE51400` (strings "Tramp thunk was not found ...", "Can't trampoline 0x%x"), called from `0xE53EB0` and `0xE643B0` [verified] | **new**: the first call of each native signature writes the hash from the interpreter, outside any lock. Rule: warm-up calls every IC/P/Invoke the worker uses on the sim thread first (docs/30 §5 step 6 covers it only if the warm-up runs the same code paths). Safer option for step 3: a lock around `0xE51400` (it is called from two sites) |
| `[0x11F46F0]` | reflection lazy hash | no (lazy create, then insert) | `0xEAC710`, `0xEAD420`, `0xEAD5B0` and 3 more | leave; reflection is banned on the worker (docs/30 §5) |
| `[0x11F43D8]` | marshal wrapper cache (delegate wrappers) | create unlocked, users take `marshal_mutex` `0x11F43E4` [inferred] | `0xE885B0`, `0xE885F0`, `0xE95A80` | leave; no delegates on the worker |
| `[0x11F4264]`, `[0x11F4274]`/`[0x11F4278]` | interface-id bitmap; generic array interface cache | no | `0xE6A1E0` ("Interface: assigned id"), `0xE6A4A0` [verified strings] | leave; class init stays on the sim thread (warm-up) |
| `[0x11F41F0]`/`[0x11F41F4]` | cached `TypeResolve` field / `DoTypeResolve` method (appdomain.c) | no | `0xE64BD0`, `0xE64C40` [verified strings] | leave (lazy, idempotent) |
| `[0x11F4190]` (0x48 bytes), `[0x11F41E4]` | one-time copy of a setup block plus done flag | yes, domain lock [verified `0xE65F20`] | `0xE65F6B` | leave |
| `[0x11F42A8]`-`[0x11F42B4]` | domain table and root domain | `0x11F4290` [verified] | `0xE74F60`, `0xE75340`, `0xE75BF0` | leave (domain create only) |
| `[0x11F4538]`/`[0x11F453C]` | `mono_debugger_lock` depth and pending flag | yes, `0x11F4520` [verified, mono-debug-debugger.c strings] | `0xEA0510`, `0xEA0580`, `0xEA0600` | leave |
| `0x1220F40`-`0x1220F74` (14 dwords) | `mono_stats` counters (new objects, class init, generic instances) [inferred from the `add [g],1` next to each allocation] | no, plain `add` | `0xEA2690`, `0xEA2770`, `0xEA2E70`, `0xEA3F00`, `0xE6EE20`, `0xE6BAB0`, `0xE9E640`, ... | leave: a race only loses counts; nothing reads them in game code [verified: 0 readers] |
| `[0x11F3028]` | `GC_finalize_on_demand` | no | `0xE69570`, `0xE78E20` (save, set 1, restore) | leave (always 1, docs/30 §12.3) |
| `0x11F47F8`-`0x11F4834` (64 bytes) | SHA-1 transform static workspace (assembly strong-name hashing) [inferred: byte swap and `rol 5` rounds in `0xEBCA20`] | no | `0xEBCA20`, via `0xEBE400` from `0xEA6890`/`0xEA7340` | leave; no assembly loading on the worker |
| `[0x11F47EC]`, `[0x1195F74]`/`[0x1195F78]` | logger mask and level (`MONO_LOG_MASK`/`MONO_LOG_LEVEL`) | no | `0xEBC210`-`0xEBC3A0` | leave (once) |
| `[0x11F47DC]` | thread interruption request count | interlocked [verified] | `0xEBB2B0`, `0xEBB3D0` | leave |
| `[0x11F4164]`, `[0x11F4148]` | interpreter init/cleanup state | n/a | `0xE5A680`, `0xE53470` (shutdown) | leave |

Critical sections found by the census (for reference): `0x11F4130` (runtime-method lookup), `0x11F4174` (transform), `0x11F41F8` (assembly.c), `0x11F4290` (domain), `0x11F4380`, `0x11F43A8` (`loader_mutex`), `0x11F43E4` (marshal), `0x11F4520` (debugger), `0x11F4574`, `0x11F45A0`, `0x11F4698`, `0x11F476C`, `0x11F4788`, `0x11F47B0` (threads.c), `0x1222580` (GC stop world, `gc.log`), `0x1234320` [verified by import census]. The Mono `GCHandle` table lock (docs/30 §3, "not read") is not among the directly pushed sections; `0xE78630`/`0xE786F0` pass theirs in a register. Still open.

## 4. Boehm GC globals and the P3 lock

154 GC globals are written from the interpreter's reach (allocation counters, free lists, heap sections, mark state, finalizer and disappearing-link tables, write-watch state, thread table) [verified]. The second reachability run from the 13 P3 entries reaches 168 GC functions. GC writer functions **not** reached from any P3 entry:

| Function | Globals | Reached from | Action |
|---|---|---|---|
| `0xE50F90` (`GC_make_descriptor` [inferred]) -> `0xE50460` (`GC_add_ext_descriptor` [inferred]), `0xE50EC0` (`GC_init_explicit_typing`, once [inferred]), which also calls `0xE4E030`/`0xE4E090` (new kind / new mark proc, strings "Too many kinds", "Too many mark procedures" [verified]) | `[0x11F4110]`, `[0x11F4114]`, `[0x1222470]` (extended descriptor table, size, next free), `[0x11F410C]`, `0x1222400`-`0x1222474`, `[0x1194860]`-`[0x11948C8]` | `0xEA1CB0` (object.c, computes a class's GC descriptor when its vtable is created) [verified caller] | **step 2: add a P3 detour on `0xE50F90`**, or keep vtable creation on the sim thread by warm-up (a worker instantiating a new generic type would race the table) |
| `0xE4DFD0`, `0xE4DFE0` (`GC_dont_gc` inc/dec [inferred]) | `[0x11F30A8]` | no direct caller (pointer use) | leave; check in step 2 if Mono's `mono_gc_disable` reaches them |
| `0xE4DFC0` | `[0x11949C0]` | `0xE69570` (Mono GC init) | leave (init) |
| `0xE4EF50` | `[0x11F40EC]` | host `0xD80750` | leave (setup) |
| `0xE50160` -> `0xE4F2A0` | `[0x12225B8]`, `[0x11F4100]`/`[0x11F4104]` | no direct caller | open; check before step 2 |
| `0xE51E70` | `[0x11F4158]`/`[0x11F415C]` (EA pair, zeroed; read by `0xE51E60`) | `MonoScriptHost_Simulate` | leave (sim thread, tick boundary) |
| `0xE51400` | `[0x11F4128]` | interpreter | see §3 |
| `0xE50D50` (typed mark proc, sets `[0x11F307C]` mark-stack overflow) | `[0x11F307C]` | registered as a mark procedure by `0xE50EC0`; runs inside a collection | covered (collection holds the P3 lock) |

Also `[0x11F2F98]` (`EA_gc_budget_counter`) is written by the GC inside the lock and by `MonoScriptHost_Simulate` outside it [verified]. Simulate writes it at a tick boundary; with a worker active, a worker collection could interleave. Low risk (a budget counter); step 2 can move the Simulate write under the lock in `MtTick` if it matters.

## 5. Compared with docs/30

| docs/30 item | This scan |
|---|---|
| P4: `[0x11F4170]`, `[0x11F416C]`, sites and only reader `0xE534E0` | confirmed; no other global in the interpreter core [verified] |
| `[0x11F4160]` per thread | confirmed; plus three more TLS slots (§1), all per thread |
| domain TLS, no `MonoThread` | confirmed |
| host `+0xBC8`/`+0xBD0` via `0xD7EED0`, `0xD7F000`, `0xD7F210` | **incomplete**: `+0xBD4` too; `+0xBC8` also written by `ScriptHost_InvokeDelegate` `0xD75FA0` and `0xD7D2F0`, read by `ScriptHost_MarkYield` `0xD70F60` and `0xD70E90` |
| P3 entry set (13) | **gap**: `0xE50F90` typed descriptor path; `0xE50160`, `0xE4DFD0/E0` to check |
| locks `0x11F4130`, `0x11F4174`, `0x11F4290`, loader `0x11F43A8` | confirmed; full list in §3 |
| GCHandle lock "not read" | still open |
| not in docs/30 | call-record list `[0x11EE5A4]`/`[0x11944C8]`/`[0x11944CC]`; IC counter `[0x11F2D84]`; thunk hash `[0x11F4128]`; reflection/marshal lazy hashes; `mono_stats`; SHA-1 workspace; EA pair `[0x11F4158]` |

## 6. Limits

- Direct addresses only: a global reached through a pointer held in a register (for example a struct copy into a global via `rep movsd`) shows only when the destination is a direct operand; table-indexed writes (`[g + reg*4]`) are listed under the table base.
- Reachability follows direct calls only. Opcode handlers inside `0xE53EB0` are direct code, but internal calls and Mono vtable calls are indirect, so the per-range scans (all writers in `0xE52000-0xEBC000` and `0xD82000-0xDC0000`) are the safety net.
- Roles marked [inferred] use Mono 1.2.3 source names; confirm before code depends on them.
