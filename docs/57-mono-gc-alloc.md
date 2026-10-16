# 57. GC and managed allocation map for a second managed thread (T-MONO-b)

Static map of every Boehm GC allocation entry, its callers in Mono and EA host code, the GC lock, the root scan, the finalizer queue, stop-the-world and write barriers, checked against the docs/30 P3 design (gated GC lock: 12 detours plus the finalizer queue pop). Ghidra headless on a copy project (`game-copy/ghidra-monob`, copied from `ghidra-ui4`, symbols CSV imported first), Steam `TS3W.exe` 1.67, image base `0x400000`. The game was not run. Boehm GC 7.1 sources were used from memory for names only.

Tags: **[verified]** = read in the disassembly or the Ghidra reference manager for this note; **[community-reported]** = stock Boehm or Mono behaviour, not checked in this exe; **[inferred]** = reasoning, to confirm where stated.

## Summary

- **Four public allocation entries, all lock-free fast paths** [verified]: `GC_malloc` `0xE4BE30` (kind 1, `GC_objfreelist` `0x12226E8`), `GC_malloc_atomic` `0xE4BDC0` (kind 0, `0x1222EEC`), `GC_malloc_uncollectable` `0xE4BF90` (kind 2, `0x12236F0`; docs/30 called it `mono_gc_malloc`), `GC_malloc_explicitly_typed` `0xE50DE0` (free lists via `[0x1222404]`). All four fall back to `GC_generic_malloc` `0xE4BCE0`. No Mono or host code pops a free list itself: the free-list bases are referenced only by these functions, `GC_push_roots` and the kind table `0x1194868` [verified].
- **The GC lock exists but is never taken** [verified]. `GC_allocate_ml` is `0x1234320`: `GC_init_public` `0xE4E4C0` initialises it (`InitializeCriticalSectionAndSpinCount(.., 4000)` through `GetProcAddress`, else `InitializeCriticalSection`), and those two instructions are its only references. `LOCK()`/`UNLOCK()` are compiled out: the Boehm block calls `EnterCriticalSection` only on `GC_write_cs` `0x1222580` (`GC_stop_world`, the log writer `0xE4DD50`), and it has no `xchg`, `lock`-prefixed or `cmpxchg` instruction, so there is no spin lock either.
- **docs/30 P3 is incomplete. Two writer groups are missing, and both are reached by the first proof:**
  1. **`GC_free` `0xE4BEB0`** pushes onto the same free lists that `GC_malloc` pops, with no lock. The interpreter's task teardown calls it (`0xE52DD0`, `0xE53E80`, `0xE535E0` through the thunk `0xD71810`), and so do host functions. P3 found its entries by scanning for writers of `GC_words_allocd`, and `GC_free` does not write that counter, so the scan missed it.
  2. **EA's interpreter frame arena** `0xD71900` / `0xD71A20` / `0xD71B70`. These are host functions that keep interpreter frames in 2 KB GC-typed chunks taken from a **global, unlocked** free-chunk list `[0x11EE508]` (list nodes in `[0x11EE504]`). `ves_exec_method_with_context` calls `0xD71900` when a frame does not fit in the current chunk, which happens on the first method entry of every invoke, and calls `0xD71A20` on exit. A worker and the sim thread would both edit these lists.
- **Smaller sites to add or to keep behind the worker rules:** `GC_add_roots` `0xE4D9F0` (lazy one-time root registrations), `GC_disable`/`GC_enable` `0xE4DFE0`/`0xE4DFD0` (non-atomic counter, only from GC handle allocation), and typed-descriptor creation `0xE50F90`/`0xE50460` (vtable setup only).
- **Mono's GC handle table has no lock** [verified]. `0xE78780` (handle allocation) and its siblings `0xE78AE0`/`0xE78C80` contain no `EnterCriticalSection` call. That answers the docs/30 §3 open point: "no `GCHandle` or weak reference on the worker" is a hard rule, not a precaution.
- **Confirmed from docs/30:**
  - Stop-the-world suspends every registered thread except the caller, holding `GC_write_cs` while it does.
  - The root scan sees only the collector's own stack until P2.
  - Finalizers never run inside a collection, so the `0xE4B3F0` replacement is the right shape.
  - There are no write barriers. Incremental mode uses `GetWriteWatch`; heap sections are allocated with `MEM_WRITE_WATCH` (`0x203000` at `0xD70906`), and write watch tracks writes from every thread [community-reported, Win32].
- **Root scan and TLS:** the interpreter context TLS `[0x11F4160]` is not a GC root. The runtime-invoke path `0xE5A600` that docs/30 §5 picks keeps its task state and context on the calling thread's stack and clears the TLS slot on exit [verified], so P2's stack scan covers a worker's frames. Other invoke paths could leave a GC-heap context that only TLS points to.

## 1. Allocation entries [verified unless tagged]

| Entry | Name | Kind / free list | Lock | Callers (unique functions) |
|---|---|---|---|---|
| `0xE4BE30` | `GC_malloc` | 1, `0x12226E8[lg]`, `lg = GC_size_map 0x12246F8[lb]` | none | 31: Mono object/array/string paths `0xEA2690`, `0xEA2770`, `0xEA2E70`, `0xEA7990`, `0xEA81A0`-`0xEA9180`, `0xEB1410`, `0xEB16D0`; `mono_gc_alloc_fixed` `0xE697D0` [name inferred]; eglib `g_hash_table_insert` `0xEBC060`, `0xEBBCB0`, `0xEBBD10`; interpreter `ves_exec_method_with_context` `0xE53EB0` (`0xE589E1`), `interp_alloc_task_state` `0xE52D70`, `interp_mono_invoke_with_state` `0xE59FD0` (4 sites); host `0xD71A20`, `0xD71B70`, `0xD718A0`, `0xD747C0`, `0xD75E80`, `0xD7BD40`, `0xD7EED0`, `0xD7F8B0`, `0xD7FC60`, `0xD7FE80` |
| `0xE4BDC0` | `GC_malloc_atomic` | 0, `0x1222EEC` | none | `0xEA2770`, `0xEA14B0`, `0xEA1510`, `0xEA2E70`, `0xEA3F00` (strings [inferred], 16 callers), Boehm `GC_add_ext_descriptor` `0xE50460` |
| `0xE4BF90` | `GC_malloc_uncollectable` | 2, `0x12236F0`; adds to `GC_non_gc_bytes` `0x11F2F8C`, sets the mark bit on the large path | none | host only: `0xD71800` (0x28 bytes, from `0xE59FD0`), `0xD714B0` (0x2C, from interpreter `0xE53620`), `0xD71900` (arena list nodes), `0xD8A830`, `MonoScriptHost_Init` `0xD7E560`, `0xD70CE0` (2 sites) |
| `0xE50DE0` | `GC_malloc_explicitly_typed` | `[0x1222404]` lists, kind `[0x1222464]`, descriptor in the last word | none | `0xEA1490`, `0xEA2690`, `0xEA2770`, `0xEA2E70`, `0xE697D0`, host `0xD71900`, `0xD718A0` |
| `0xE4BCE0` | `GC_generic_malloc` | any; calls `GC_print_all_errors` `0xE4F2E0`, `GC_notify_or_invoke_finalizers`, then inner / large | none | only the four above |
| `0xE4BBB0` | `GC_generic_malloc_inner` | refill via `GC_allocobj` `0xE4A2B0` | none | `0xE4BCE0`, finalizer tables `0xE4AAC0`, `0xE4AC60`, `0xE4BCA0`, typed init `0xE4DFF0` |
| `0xE4BEB0` | **`GC_free`** | pushes onto `GC_obj_kinds[k]` list, `GC_mem_freed` `0x12225D4` += size, **sets** the mark bit; large objects go to `0xE49730` | none | interpreter `0xE52DD0` (3 sites), `0xE53E80`, `0xE535E0` and `0xE52DD0` via thunk `0xD71810`; host `0xD71A20`, `0xD71B70`, `0xD714C0`, `0xD80750`, `0xD70DC5`; Boehm `0xE4ABE0`, `0xE4F2E0` |

The callers of the Mono wrappers fan out widely: `mono_object_new_specific`-style `0xEA3140` alone has 49 callers, among them about 35 EA script-binding functions in `0xD9xxxx`-`0xE3xxxx` that create managed objects from native code. Everything below those wrappers reaches the four entries.

**EA edit in the fast paths** [verified bytes, purpose inferred]: every fast path calls `0xE4C1E0` after the pop. It clears the new object's mark bit and stores `GC_gc_no` `[0x11F2F90]` at block header `+0x18`. `GC_free` sets the bit again. Stock 7.1 does neither. It is a read-modify-write on the block's mark word, so it has to sit under the same lock as the pop. It does, as long as the entry is detoured.

## 2. The lock [verified]

| Object | Address | Use |
|---|---|---|
| `GC_allocate_ml` | `0x1234320` | Initialised once in `0xE4E4C0`; no other reference in the exe. Never entered. |
| `GC_write_cs` | `0x1222580` | Initialised in `GC_init` `0xE4E175`; taken by `GC_stop_world` `0xE511A0` (enter `0xE511D5`, leave `0xE5127B`) and `GC_write` `0xE4DD50`. |
| Mono `gc.c` sections | `0x11F42F0`, `0x11F42D4`, `0x11F4314` | Initialised by `mono_gc_init` `0xE78DA0` [name inferred]. Finalizer registration `0xE78630`/`0xE786F0`/`0xE78E20` locks a section reached through an object field (`[[esi]+8]`, a per-domain lock [inferred]). |
| interpreter | `0x11F4130` | Guards lazy init of the runtime-method hash `[0x11F4168]` in `0xE53070`, which also calls `GC_add_roots` once. |

So EA kept Boehm's thread table and stop-the-world but compiled the allocation lock out. P3 could reuse `0x1234320`, which is already initialised, has a spin count and nothing else uses. A section of our own is just as cheap and does not depend on init order, so the design keeps its own (`g_gcLock`).

## 3. Paths that skip any lock: EA edits and host code [verified]

### 3.1 `GC_free`

See §1. A worker reaches it through task teardown on every invoke that set state `+0x148`. The stack state of `0xE5A600` leaves `+0x148` at 0, but `0xE53E80`, `0xE535E0` and the large-object path are reachable from interpreted code [inferred]. The sim thread frees through host paths at any time. **Add it to P3.**

### 3.2 Interpreter frame arena (EA host, `0xD718A0`-`0xD71BD6`)

- The task state (`interp_alloc_task_state` `0xE52D70`, `GC_malloc(0x150)`, or the 0x150-byte stack copy in `0xE5A600`) holds a chain of arena chunks at `+0x13C`. On method entry, `ves_exec_method_with_context` bumps the chain's current chunk inline by the method's frame size (`0xE563B2`-`0xE563D1`, per state, no shared data). If there is no chunk or it is full, it calls `0xD71900(state, size)` (`0xE563DE`). On exit it resets the bump pointer inline, or calls `0xD71A20(state, ptr)` when the frame crosses a chunk (`0xE5445A`, `0xE5981A`).
- `0xD71900` takes a 2 KB (`0x7FC`) chunk from the global circular list `[0x11EE508]`. When the list is empty, `0xD718A0` refills it with 64 chunks: `GC_malloc(0xC)` nodes plus `GC_malloc_explicitly_typed(0x7FC, [0x11F424C])`. List nodes come from `[0x11EE504]` or `GC_malloc_uncollectable(8)`. `0xD71A20` and `0xD71B70` (release a state's whole chain, from `0xE52DD0` and host `0xD84444`) put chunks back on `[0x11EE508]` and free odd-sized ones with `GC_free`. `0xD80750` (a host vtable method, `0x108B6A4`; shutdown [inferred]) also rewrites both lists.
- No lock anywhere in these functions. Two threads in the interpreter corrupt the doubly linked lists, and the result is a lost or doubly used chunk: an interpreter frame shared between two threads [inferred].
- **Add `0xD71900`, `0xD71A20` and `0xD71B70` to P3.** Only the slow path is detoured. The inline bump in the interpreter stays lock-free and per state, so the lock cost is one take per chunk crossing, not per call.
- A second user of these wrappers sits in code that Ghidra has no function for: `0xD83E22`-`0xD848D6` (calls to `0xD71900`, `0xD71B70`, `0xD714B0`, `0xD714C0`, `0xD71800`, `0xD75E80`). It is host task code, so sim thread only [inferred].

### 3.3 Other unlocked writers of GC state

| Site | Writes | Reached from | Treatment |
|---|---|---|---|
| `GC_add_roots` `0xE4D9F0` -> `GC_add_roots_inner` `0xE4D5F0` | static root table `0x122FB14`, count `[0x11F3084]` | `0xE53070` (lazy, once, under `0x11F4130`), domain init `0xE75BF0`, `mono_gc_init` `0xE78DA0`; Boehm init `0xE4EE10`, `0xE4A870` | Add to P3 (8-byte prologue, cheap); the warm-up on the sim thread also makes the lazy site run there first |
| `GC_disable`/`GC_enable` `0xE4DFE0`/`0xE4DFD0` | `GC_dont_gc` `[0x11F30A8]` `+=1`/`-=1`, not atomic | thunks `0xE696A0`/`0xE696B0`, only from `0xE78780` (GC handle table grow) | Worker rule: no GC handles. Optional detour (7-byte instruction + `ret`). |
| `GC_make_descriptor` `0xE50F90`, `GC_init_explicit_typing` `0xE50EC0`, `GC_add_ext_descriptor` `0xE50460` | typed-descriptor tables, `GC_eobjfreelist`, `GC_explicit_kind` | `0xEA1CB0` (class GC descriptor at vtable setup) | Worker rule: warm-up creates vtables on the sim thread (docs/30 §5 step 6) |
| Mono GC handles `0xE78780`, `0xE78AE0`, `0xE78C80` | Mono's handle arrays plus Boehm links (`0xE696C0` -> `0xE4AAC0`, `0xE696E0` -> `0xE4ABE0`) | `GCHandle`, weak refs | Worker rule (now required: Mono takes no lock here) |
| `GC_set_warn_proc` `0xE4DFC0`, `GC_new_proc` `0xE4E090` [name inferred] | warn hook `[0x11949C0]`, mark procs `0x12225E8` | Mono init `0xE69570` | init only |

Read-only helpers a worker may call without the lock [inferred: they read headers that only change under the lock or in a collection]: `GC_base` `0xE4DC30`, `GC_get_heap_size` `0xE4DD30`, `GC_get_free_bytes` `0xE4DD40`, `GC_should_invoke_finalizers` `0xE4B3E0`.

## 4. Root scan and thread table

- `GC_push_roots` `0xE4D910` (one caller, the mark state machine) [verified]:
  1. the static roots `0x122FB14` (12-byte entries, `[0x11F3084]` of them);
  2. the mark bits of the kinds' free-list arrays (`GC_base` + `GC_set_mark_bit` for each `GC_obj_kinds` entry);
  3. the finalizer and disappearing-link tables `0xE4A9A0` (when finalizers or links exist);
  4. the collector's registers `0xE4BA10`;
  5. the tail jump through `[0x11949CC]` -> `0xE4EF90` -> `0xE51350` -> `0xD71820`, EA's `GC_push_other_roots`, which pushes only the current thread's stack (`VirtualQuery`, `GC_push_all_stack` `0xE4CEE0`).

  This matches docs/30 §2 and the §13 thunk correction.
- Thread table `0x1221000` (256 x 0x14), `GC_new_thread` `0xE51070`, stop/start `0xE511A0`/`0xE51290`: unchanged from docs/30 §1 [verified again: the world-stopped flag `[0x11F4120]` is written only by stop/start; the max index `[0x11F4124]` is written only by `GC_new_thread`].
- **TLS is not a root** [community-reported Boehm behaviour; no push of any TLS slot in `0xE4D910`, verified]. `interp_mono_invoke_with_state` `0xE59FD0` reuses the context in `[0x11F4160]` when the slot is set (`0xE5A0C7`). When it creates the context, it is a `GC_malloc(0x18)` at `0xE5A0EA`, or `alloca` when state `+0x14B` is set (`0xE5A0D2`). It is stored with `TlsSetValue` at `0xE5A150`, and the slot is reset to NULL at exit when the context is the one this state created (`0xE5A4BB`-`0xE5A4CA`). `interp_runtime_invoke` `0xE5A600` builds its state on the stack with `+0x14A = +0x14B = 1` [verified], so the context is `alloca`'d and the TLS slot is cleared. The worker's whole interpreter state then lives on its own stack plus arena chunks reachable from it, and P2's stack push covers it. **Keep the worker on `0xE5A600`** (docs/30 §5 step 5). A path that leaves a `GC_malloc`'d context reachable only from TLS would be freed under the worker [inferred].

## 5. Finalizer queue [verified, matches docs/30 §3, §12.2-12.3]

- Registration: `GC_register_finalizer` `0xE4AE30` -> `0xE4AC60`, from Mono `0xE78630` (object allocation paths via `0xE78F60`), `0xE786F0` (an icall), `0xE78E20`. Links: `0xE4AAC0`/`0xE4ABE0` via `0xE696C0`/`0xE696E0`. The unregister path frees the entry with `GC_free` (`0xE4ABE0` calls `0xE4BEB0`), another reason to lock `GC_free`.
- Queue `[0x11F303C]`: filled by `GC_finalize` `0xE4AE60` (reads and writes), popped by `GC_invoke_finalizers` `0xE4B3F0`, read by `0xE4B480`, `0xE4B4C0`, `0xE4B3E0`, and pushed as a root by `0xE4A9A0`. Nothing else touches it. `GC_finalize_on_demand` `[0x11F3028]` = 1 (`0xE69570`; saved and restored in `0xE78E20`); the notifier `[0x11F3044]` has one read and no writer. Finalizers run only from `mono_gc_invoke_finalizers` `0xE697F0` on the sim thread.

## 6. Stop-the-world and write barriers

- **EA's build suspends threads** [verified]. `GC_stop_world` takes `GC_write_cs`, then for every in-use entry other than the caller checks `GetExitCodeThread` (`[0xF951FC]`) and calls the two imports loaded into EBX/EBP (`[0xF95398]`, `[0xF95154]`; `SuspendThread` per docs/30 §1). `GC_start_world` resumes through `[0xF95150]`. Only registered threads are suspended, so before P1 only the `GC_init` thread is.
- Deadlock surface while a worker is suspended [inferred]: the collector holds `g_gcLock`, so the worker is never suspended inside an allocation. A collection itself calls no host lock except the heap-growth path, `GC_win32_get_mem` `0xD70900`. That path calls `VirtualAlloc`, plus an EA memory-tracker call `0x9336F0` when `[0x11DC18C]` and a flag are set, which may lock. Mono's `g_malloc` goes to the EA arena `0x4E69D0` through `0xD70A10`/`0xD70A30`, and the collector does not call it. Keep finalizers on the sim thread (no change).
- **No write barriers** [verified for the GC side]. No barrier entry exists among the Boehm functions or the Mono `boehm-gc.c` wrappers `0xE69400`-`0xE69A00`. Mono 1.2.3 predates `mono_gc_wbarrier_*` [community-reported]. Incremental dirty bits come from `GetWriteWatch` (`GC_gww_dirty_init` `0xE4EFB0`) on sections allocated with `MEM_WRITE_WATCH` (`0xD70908`: `push 0x203000`). Write watch is per region, not per thread [community-reported, Win32], so a worker's pointer stores are seen by the next stopped mark.

## 7. P3 check: confirmed, corrected, added

docs/30 §3 and §12.1 list 12 detours plus the `0xE4B3F0` replacement.

**Confirmed (bytes and roles)** [verified]:
- `0xE4BE30` `GC_malloc`, `0xE4BDC0` `GC_malloc_atomic`, `0xE50DE0` `GC_malloc_explicitly_typed`, `0xE4BCE0` `GC_generic_malloc`.
- `0xE4A050` `GC_try_to_collect` (only caller `MonoScriptHost_Simulate`) and `0xE4A0B0` `GC_gcollect` (host `0xD702A0`, thunk `0xE69650`).
- `0xE4E450` `GC_enable_incremental` (`MonoScriptHost_Init` only).
- `0xE4AAC0`, `0xE4ABE0`, `0xE4AE30`, `0xE4A3F0` (only caller host `0xD702A0`).
- The `0xE4B3F0` replacement, with `0xE697F0` correctly left alone.

The prologue lengths in §12.1 match the bytes read here.

**Corrected:** `0xE4BF90` is `GC_malloc_uncollectable` (kind 2, uncollectable free list, `GC_non_gc_bytes`), not `mono_gc_malloc`. It has host callers only. Keep the detour.

**Not needed as separate detours:** `GC_allocobj` `0xE4A2B0`, `GC_alloc_large` `0xE4BA90`, `GC_collect_or_expand` `0xE4A120` and the host thunk `0xD70800` (`jmp 0xE4A320`) are reached only from inside detoured entries [verified callers].

**Add** (prologues verified; no code reference targets a byte inside any copied range, checked with the reference manager):

| Entry | Role | Copied bytes | Length | Resume |
|---|---|---|---|---|
| `0xE4BEB0` | `GC_free` | `56` / `8B 74 24 08` | 5 | `0xE4BEB5` |
| `0xD71900` | arena grow (EA host) | `8B 44 24 04` / `55` | 5 | `0xD71905` |
| `0xD71A20` | arena pop (EA host) | `8B 44 24 04` / `56` | 5 | `0xD71A25` |
| `0xD71B70` | arena release all (EA host) | `8B 44 24 04` / `56` | 5 | `0xD71B75` |
| `0xE4D9F0` | `GC_add_roots` | `8B 44 24 08` / `8B 4C 24 04` | 8 | `0xE4D9F8` |
| `0xE4DFE0`, `0xE4DFD0` (optional) | `GC_disable`, `GC_enable` | `83 05 A8 30 1F 01 01` / `83 2D A8 30 1F 01 01` | 7 | `ret` at `+7` (or replace whole with a locked C function) |

The new P3 is **17 detours** (12 + 5; 19 with the optional pair) **plus the `0xE4B3F0` replacement**. The lock must stay recursive, because `0xD71900` -> `0xD718A0` -> `GC_malloc` and `0xD71A20` -> `GC_free` nest inside one another. `0xD80750` (shutdown [inferred]) also writes the arena lists. It runs on the sim thread, and the worker must be stopped before it runs.

**Rules that stay** (now verified as necessary): no GC handles or weak references on the worker (Mono's handle table has no lock), no first-time vtable or class setup on the worker (descriptor tables), and invoke only through `0xE5A600` (TLS context is not a root).

**Selftest addition** for docs/30 §9: two threads hammering a fake arena list through hooked `0xD71900`/`0xD71A20`-shaped functions, and `GC_free`/`GC_malloc` on one fake free list, with the lock on and off.

## 8. Names

73 rows were appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv`, all tagged `<docs/57>`: 53 functions and 20 data items, no address reused, re-imported cleanly into the copy project. They cover the allocation entries, `GC_free`, the EA fast-path edit, the root and finalizer functions, the descriptor functions, `GC_enable`/`GC_disable`, the arena functions and lists, the Mono handle and weak-link wrappers, `GC_allocate_ml` and `GC_write_cs`.

## Open points

- Which code in `0xD83E22`-`0xD848D6` uses the arena wrappers (host task resume [inferred]). It is not reached from `0xE5A600`.
- Whether the EA memory tracker `0x9336F0` locks, which matters only for heap growth while a worker is suspended.
- The lock Mono's finalizer registration `0xE78630` takes (`[[esi]+8]`): a per-domain section [inferred]. It is off the worker by rule.
