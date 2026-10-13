# Known object layouts and globals (TS3W.exe, Steam 1.67)

All addresses are for the Steam build (image base `0x400000`, no ASLR; SHA-256 in [README](README.md)). Offsets are in bytes from the object start. Field names are ours.

Confidence marks:
- **[verified]**: read in the decompile, the disassembly or the exe bytes.
- **[measured]**: read live from the running game with the probe (`tools/probe`).
- **[inferred]**: reasoned from code or naming, not confirmed.

Doc links point at the write-up that has the evidence.

---

## 1. MonoScriptHost (the script host)

Global pointer `[0x11EE514]`, ctor `0xD7E010`. `this` of the scheduler seams (`0xD80BC0`, `0xD7FC60`, `0xD74800`, `0xD7F8B0`) and of the task runner `0xD7F000`. A second vtable sits at `host+8` (slot `0x108B7AC` = yield mark `0xD70F60`, slot `0x108B7C4` = host delegate call `0xD75FA0`), so code that is called through that vtable sees `this = host+8`.

| Offset | Type | Meaning | Confidence | Source |
|---|---|---|---|---|
| +0x8 | vtable ptr | secondary interface (yield mark, delegate call) | [verified] | [docs/18](../18-route-search-off-thread.md) "Crash: async route assert" |
| +0xA60 | bool | `mbIdlingEnabled` | [verified, community + bytes] | [docs/03](../03-prior-art-re.md) |
| +0xAC0 | dword | read before the idle call (Smooth Patch Precise anchor) | [verified bytes] | [docs/03](../03-prior-art-re.md) |
| +0xBC8 | ptr | pointer to the running task's interpreter-state slot (`*(host+0xBC8)` = the task's root pState); set per task by `0xD7F000` | [verified] | [docs/18](../18-route-search-off-thread.md), [docs/04 F17](../04-re-findings.md) |
| +0xBD0 / +0xBD4 | u64 | ObjectGuid of the running task (0 = no task). The host delegate call saves, zeroes and restores it around its invoke | [verified] | [docs/18](../18-route-search-off-thread.md) |
| +0xC08 | bool | "simulation running" flag (1 while `Simulate` runs) | [verified, community + bytes] | [docs/03](../03-prior-art-re.md) |
| +0xC14 | dword | catch-up backlog counter; a backlog above 19 is reset | [verified] | [docs/04 F2](../04-re-findings.md) |
| +0x16A0 | map | `Scripting/MonoScriptHost/OneShotObjects` | [verified] | [docs/04 F17](../04-re-findings.md) |
| +0x16C0 | map | `ThreadedObjects` | [verified] | [docs/04 F17](../04-re-findings.md) |
| +0x16E0 | map | `TaskObjects` (8-byte ObjectGuid key, per task context) | [verified] | [docs/04 F17](../04-re-findings.md) |

Related globals: `0x11EE528` gates the GC stop function (see §5) [verified use, meaning inferred]; `[0x11D4BBC]` = script host pointer used by the portal-lock callback path (vtable +0x134 delegate, +0x138 method) [verified, docs/18].

### Task context (per TaskObjects entry)

Allocated by `0xE52D70` (0x150 bytes, the same object as the interpreter state in §2).

| Offset | Type | Meaning | Confidence | Source |
|---|---|---|---|---|
| +0x0 | MonoMethod* | task's base method (resolved with `0xEA0FB0`, `mono_object_get_virtual_method`) | [verified] | [docs/04 F17](../04-re-findings.md) |
| +0x4 | MonoObject* | managed task object (its +0 is the MonoVTable, whose +0 is the klass) | [inferred] | [docs/04 F17](../04-re-findings.md) |

---

## 2. Mono interpreter state ("pState", 0x150 bytes)

EA's own Mono 1.2 interpreter (no JIT). One state per task (`0xE52D70`); `runtime_invoke 0xE5A600` and the native->managed entry `0xD8A8E0` each build a fresh one on the stack.

| Offset | Type | Meaning | Confidence | Source |
|---|---|---|---|---|
| +0x0 | MonoMethod* | base method of the state (a task's `Simulate`, e.g. `ScriptCore.ScriptProxy.Simulate`) | [measured] | [docs/04 F6a](../04-re-findings.md) |
| +0x4 | MonoObject* | object the state runs on (for tasks) | [inferred] | [docs/04 F17](../04-re-findings.md) |
| +0x140 | int | resume point (-1 = not resuming); also read as call depth by the sampler | [verified] | [docs/18](../18-route-search-off-thread.md), [docs/04 F6a](../04-re-findings.md) |
| +0x148 | byte | free on exit | [verified] | [docs/18](../18-route-search-off-thread.md) |
| +0x149 | byte | **yieldReturn**: set by the yield mark on the task's root state; asserted by the yield opcode (`interp.c:3595`) on the current state | [verified] | [docs/18](../18-route-search-off-thread.md) |
| +0x14A | byte | fresh | [verified] | [docs/18](../18-route-search-off-thread.md) |
| +0x14B | byte | allocated on the stack | [verified] | [docs/18](../18-route-search-off-thread.md) |

### Interpreter globals

| Address | Type | Meaning | Confidence | Source |
|---|---|---|---|---|
| `0x11F4160` | dword | TLS index of the interpreter ThreadContext (96 in one run); the script (sim) thread is the one whose slot is set | [verified, measured] | [docs/04 F6a](../04-re-findings.md) |
| `0x11F4170` | pState* | current (innermost) interpreter state; `ves_exec 0xE53EB0` saves it, stores its own, restores it on return | [verified] | [docs/18](../18-route-search-off-thread.md) |
| `0x1195B60` | fn ptr | runtime_invoke slot, written by `mono_install_runtime_invoke 0xEA1470`: `0xEA1080` before interp init, then the interpreter's `0xE5A600` (installed by `mono_interp_init 0xE5A680`) | [verified] | `native/ts3native/ts3native.cpp`, [docs/15 Phase 3](../15-native-patches.md) |
| `0x11959DC` | TLS index | Mono domain TLS (empty on a non-Mono thread, which is why `mono_class_vtable` crashes there with domain NULL) | [verified] | `native/ts3native/ts3native.cpp`, [docs/15](../15-native-patches.md) |

A yield is legal only when `[0x11F4170] == *[host+0xBC8]` and `host+0xBD0` holds a task id [verified, docs/18].

### ThreadContext and call frames

| Object | Offset | Meaning | Confidence | Source |
|---|---|---|---|---|
| ThreadContext (in TLS slot `[0x11F4160]`) | +0x8 | current call frame | [verified] | [docs/04 F6a](../04-re-findings.md) |
| Frame | +0x0 | parent frame | [verified] | [docs/04 F6a](../04-re-findings.md) |
| Frame | +0x4 (`frame[1]`) | interpreter method info | [measured] | [docs/04 F7](../04-re-findings.md) |
| Method info | +0x8 | MonoMethod* | [measured] | [docs/04 F7](../04-re-findings.md) |

Frames live on the thread's stack; the probe copies the first 0x30 bytes of each while the thread is suspended.

---

## 3. MonoMethod and MonoClass (Mono 1.2 as embedded)

| Object | Offset | Type | Meaning | Confidence | Source |
|---|---|---|---|---|---|
| MonoMethod | +0x0..+0x27 | - | 40-byte header; the patch host copies a replacement's header over the original, keeping +0x18 | [verified] | [docs/04 F8](../04-re-findings.md) |
| MonoMethod | +0x8 | MonoClass* | declaring class | [measured] | [docs/04 F6a](../04-re-findings.md) |
| MonoMethod | +0x10 | ptr | callee runtime info the interpreter fetches | [inferred] | [docs/04 F6a](../04-re-findings.md) |
| MonoMethod | +0x14 | ptr | interpreter info back pointer; must be 0 (not yet compiled) for a startup method swap to take | [measured] | [docs/04 F7, F8](../04-re-findings.md), [docs/17 §8](../17-heavy-store-content.md) |
| MonoMethod | +0x18 | char* | method name | [measured] | [docs/04 F6a](../04-re-findings.md) |
| MonoClass | +0x34 | char* | class name | [measured] | [docs/04 F6a](../04-re-findings.md) |
| MonoClass | +0x38 | char* | namespace | [measured] | [docs/04 F6a](../04-re-findings.md) |
| MonoDomain | +0x58 | int | `domain_id` (read by `mono_class_vtable 0xEA2AD0` at `0xEA2B03`) | [verified] | [docs/15 Phase 3](../15-native-patches.md) |

Names in metadata end at the first non-printable byte (no reliable NUL), so readers stop there.

---

## 4. Engine lock object (`Lock`)

`Lock_Acquire 0x4E16F0` (thiscall, one arg: pointer to an absolute `GetTickCount` deadline, `0xFFFFFFFF` = infinite, 0 = one try; ret 4), `Lock_Release 0x4E17B0`. Recursive.

| Offset | Type | Meaning | Confidence | Source |
|---|---|---|---|---|
| +0x20 | int | recursion count | [verified] | [docs/18](../18-route-search-off-thread.md) |
| +0x24 | byte | 1 = CRITICAL_SECTION path, 0 = mutex path (TryEnter + `Sleep(1)` loop for timeouts) | [verified] | [docs/11 Q4](../11-route-planner-re.md), [docs/18](../18-route-search-off-thread.md) |

Return -2 = timed out [verified, ts3native.cpp].

---

## 5. Boehm GC globals (Boehm 6.x win32, incremental)

| Address | Type | Meaning | Confidence | Source |
|---|---|---|---|---|
| `0x12225A0` | dword | `GC_heapsize` (bytes) | [verified, measured] | [docs/13](../13-gc-tuning.md) |
| `0x12225A4` | dword | max heap size (set by `GC_MAXIMUM_HEAP_SIZE`) | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x12225B4` | dword | largest free bytes | [verified, measured] | [docs/13](../13-gc-tuning.md) |
| `0x12225C4` | dword | words allocated since the last GC (x4 = bytes) | [verified, measured] | [docs/13](../13-gc-tuning.md) |
| `0x11F2F90` | dword | `GC_gc_no` (collections) | [verified, measured] | [docs/13](../13-gc-tuning.md) |
| `0x11F2F98` | int | EA budget-adapt counter (range ±20, limits `0x11947E8` / `0x11947EC`) | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x11F2F9C` | dword | `GC_incremental` (1: set unconditionally via `0xE4E450`) | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x11F2FB4` | dword | finish attempts of the current cycle | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x11F2FD0` / `0x11F3040` | dword | last / current finalizer-table entries (trigger only in the non-incremental branch, inactive) | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x11F30A8` | dword | `GC_dont_gc` | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x11F30B4` | dword | `GC_print_stats` | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x11F30D0` | dword | GC initialized (set at the end of `GC_init 0xE4E160`) | [verified] | `native/ts3native/ts3native.cpp` |
| `0x11F40E4` | dword | `GC_no_win32` (Win9x only) | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x1194810` | dword | `GC_free_space_divisor` = 5 (stock Boehm 3) | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x1194814` | dword | min-bytes-allocated divisor input (`0xE498B0`) | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x119481C` | dword | `GC_rate` = 25 mark steps per check | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x1194820` | dword | `max_prior_attempts` = 40 (then forced finish, unbounded pause) | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x1194848` | dword | finalizers per call = 50 | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x11949B8` | dword | large-alloc warning interval | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x11922A4` | dword | per-frame GC budget (starts 1000, ±100 steps; units likely µs) | [verified; units inferred] | [docs/13](../13-gc-tuning.md) |
| `0x11923A4` | dword | budget floor 500 | [verified] | [docs/13](../13-gc-tuning.md) |
| `0x1192324` | dword | budget ceiling 1500 (patch site of `[GC] BudgetCeiling`) | [verified] | [docs/13](../13-gc-tuning.md), [docs/15 Phase 1](../15-native-patches.md) |
| `0x1221000` | table | GC thread table, 256 x 0x14 bytes; only the `GC_init` thread is registered | [verified] | [docs/15 Phase 3](../15-native-patches.md) |

Immediates patched by ts3native: initial heap `mov ebp,0x2800` (blocks of 4 KB = 40 MB) at `0xE4E168`; growth step `0x800` blocks (8 MB) at `0xE4A1D8`, `0xE4A1FF`, `0xE4A206` [verified, docs/15 Phase 1].

---

## 6. Route object (managed `Route` handle's native side)

Seen as `esi` in `Route_PlanCommon 0x7606E0` and as the arguments of `Route_PlanLocked 0x82C6E0`.

| Offset | Type | Meaning | Confidence | Source |
|---|---|---|---|---|
| vtable +0x10 / +0x14 | fn | called around every route internal call; AddRef/Release (`0x8ABE50`: atomic add on +0xC) | [verified for +0x10; pair role inferred] | [docs/18](../18-route-search-off-thread.md) |
| +0xC | int | refcount (atomic) | [verified] | [docs/18](../18-route-search-off-thread.md) |
| +0x3C | dword | **agent class**: 0x800 flier, 0x900 walking dog, 0xA00 little dog, 0xB00 leading horse, 0x4400 shark (set by PlanCommon from the options) | [verified] | [docs/11](../11-route-planner-re.md), [docs/15 Phase 2](../15-native-patches.md) |
| +0x40 / +0x44 | u64 | **follower ObjectGuid** (blocker test in `0x752620` skips it). Early docs called it "lot id" | [code verified, meaning inferred] | [docs/15 Phase 2](../15-native-patches.md) |
| +0xE0 | EASTL list | output path list (emptied by PlanCommon on every call) | [verified] | [docs/15 Phase 2](../15-native-patches.md) |
| +0x158 | RoutingOptions | options block (see below) | [verified] | [docs/11](../11-route-planner-re.md) |

### RoutingOptions (at route+0x158; `this` of `0x81C880` and `Route_GetOption 0x74D040`)

| Offset | Type | Meaning | Confidence | Source |
|---|---|---|---|---|
| +0x28 / +0x2C | u64 | `RouteOption` lo/hi. Hi bit 0x1 = `DebugPathPlan` (used by ts3native as "budget this route"); bit 58 (hi 0x04000000) = ts3native async handshake (no enum name, never set by the game) | [verified] | [docs/11](../11-route-planner-re.md), [docs/18](../18-route-search-off-thread.md) |
| +0x30 / +0x34 | u64 | `RouteOption2` lo/hi (`RouteAsShark` etc.) | [verified] | [docs/11](../11-route-planner-re.md) |

### Route points and results

| Object | Offset / size | Meaning | Confidence | Source |
|---|---|---|---|---|
| Route point | 64 bytes; +0 x, +4 y, +8 z (float) | start/goal records; x/z bounds-checked in PlanCommon | [verified] | `native/ts3native/ts3native.cpp` |
| RoutePlanResult | 0x48 bytes | hidden first argument of `IC_Route_Plan 0x765160` (also returned in eax) | [verified] | [docs/18](../18-route-search-off-thread.md) |
| RoutingPathImp | +0x94 | path status, atomic stores (1 = running; 7/8 = no path, mapped to `FailedBecauseNoPathToDestination` by `0x752620`) | [verified] | [docs/11](../11-route-planner-re.md) |
| RoutingPathImp vtable | +0x20 status; +0x160 / +0x164 blocker id, type, location | [verified for +0x20; +0x164 layout unverified] | [docs/15 Phase 2](../15-native-patches.md) |
| RoutingJob | +0x40 = RoutingOptions copy (`this` of `0x81C880` in the job); +0xC0 agent class | [verified] | [docs/11](../11-route-planner-re.md) |

---

## 7. Routing world

`ecx` of `Route_PlanLocked 0x82C6E0`.

| Offset | Type | Meaning | Confidence | Source |
|---|---|---|---|---|
| +0x7A0 / +0x7A4 | dword | water grid width / height | [verified] | [docs/15 Phase 9](../15-native-patches.md) |
| +0x7B0 + 16*i | EASTL vector<u8> | water grids by boat/swim type (g1: walkable water) | [verified] | [docs/15 Phase 9](../15-native-patches.md) |
| +0x7C0 + 16*j | EASTL vector<u8> | cost-layer grids (g2: always jump points) | [verified] | [docs/15 Phase 9](../15-native-patches.md) |
| +0x918 | Lock | **routing lock**: taken by `Route_PlanLocked` and every routing-world writer found (`0x82EDC1`, `0x82C731`, `0x82C148`, `0x8343A3`, `0x833587`; range `0x829370-0x834860`) | [verified] | [docs/18](../18-route-search-off-thread.md) |
| +0x95C | ptr | lot path planner P (returned by `0x829BC0` when +0x974 is set) | [verified] | [docs/18](../18-route-search-off-thread.md) |
| [+0x95C]+0x10E | byte | pending-update flag checked by `0x82C420` before each plan (same field as P's dirty flag) | [check verified, meaning inferred] | [docs/11](../11-route-planner-re.md), [docs/15](../15-native-patches.md) |
| +0x974 | byte | planner present | [verified] | [docs/18](../18-route-search-off-thread.md) |
| +0x976 / +0x97E | byte | flags behind PlanLocked's early-outs | [verified] | [docs/15 Phase 2](../15-native-patches.md) |

---

## 8. Lot path planner (P), lot graph, A* search (Q) and nodes

All from the async-plan crash analysis [docs/18 "thread-private scratch (option B)"](../18-route-search-off-thread.md).

### P, lot path planner (one per routing world)

| Offset | Meaning | Confidence |
|---|---|---|
| +0x10 | PathContainer (node pool, closed bitset, open heap) | [verified] |
| +0x48 / +0x4C / +0x50 | lot table: hash table / buckets / bucket count (keyed by 8-byte lot key; find-or-insert `0x62EAB0`, writes on a miss) | [verified] |
| +0xBC | Q (the A* search), refcounted; ~20 load sites in `0x625000-0x63FFFF` | [verified] |
| +0x10E | dirty flag (lazy lot build) | [verified] |

Lot table entry: entry count at table +0xC; the entry's lot graph is at `entry+0x10` [verified].

### Lot graph (entry+0x10, ~0x180 bytes, ctor `0x62AA60`)

| Offset | Meaning | Confidence |
|---|---|---|
| +0x74 | vector of per-level cell graphs L (`[L+0x28]` = Q, used by `0x664B10`) | [verified; `[L+0x28] == Q` inferred] |
| +0x8C | lot built flag (set by `0x63CB90`) | [verified] |
| +0x90 / +0x94 | vector of 0x44-byte room records (+0 built flag, +0x24 portal index list, +0x34 connectivity lists) | [verified] |
| +0x16C | word: outside room index | [verified] |

Lot record (portal-lock flag): `+0x8F` = "ask script about portal locks" (set by `Route.SetPortalLockStatus`, cleared when the callback returns 0 or > 3) [verified, docs/18].

### Q, `cAStarSearch` (the only instance, 0x9C90 bytes, ctor `0x61BF60`, vtable `0xFEA5C0`)

| Offset | Meaning | Confidence |
|---|---|---|
| +0x4 | intrusive refcount | [verified] |
| +0x14 | path list ("Routing/AStarSearch/mpPath") | [verified] |
| +0x18 | bucketed open list ("Routing/OpenList") | [verified] |
| +0x24 | iteration count (cap 64000 or 0x40002) | [verified] |
| +0x28..+0x9C84 | node hash table, 10007 buckets | [verified] |
| +0x9C84 | end sentinel | [verified] |
| +0x9C88 | node count | [verified] |

Global `0x11580C4` (word): search generation stamp, incremented once per search by `0x61E360` [verified].

### A* node (created by `0x61C2C0`)

| Offset | Meaning | Confidence |
|---|---|---|
| vtable +0x24 | dtor; +0x3C reset (on stamp mismatch) | [verified] |
| +0x1C | parent (refcounted via `0x61BB40`) | [verified] |
| +0x20 | g | [verified] |
| +0x24 | f | [verified] |
| +0x28 | int refcount (plain add/sub, not atomic) | [verified] |
| +0x28 / +0x2C | entry point (shares +0x28 per docs/18 table; layout to recheck) | [verified as written, overlap unresolved] |
| +0x30 | low word = generation stamp; bits 20-21 open/closed; bit 22 pinned | [verified] |

Graph and cell objects: word refcount at +4, not atomic [verified].

### World A* (`0x676260`) frame

Cost function pointer in `[esp+0x60]`, loaded from one of five `mov dword [esp+0x60], imm32` at `0x676340`, `0x67634A`, `0x676354`, `0x67635E`, `0x676368` (cost functions `0x674E40`, `0x673850`, `0x674250`, `0x675800`, `0x6734E0`: travel modes 1-4, default). Cost fn is cdecl, 8 args, float in ST0; arg 5 = 1 for an edge cost, 0 for the heuristic. Blocked-edge constant 1e8 at `0xFEF678`. Cost-parameter sets at container +0x28 / +0x4C (container at `[...+0x24]`) [verified, docs/11, docs/15 Phase 4].

### Water JPS callback object (arg `cb` of jump `0x61FE10`)

| Offset | Meaning | Confidence | Source |
|---|---|---|---|
| +0x0 | vtable | [verified] | `native/ts3native/waterjps.cpp` |
| +0x10 / +0x14 | start x, y (cells) | [verified] | same |
| +0x18 / +0x1C | goal x, y (cells) | [verified] | same |

The game's callback `0x66DC50` answers "within 3 cells of start or goal" [verified].

---

## 9. EA GeneralAllocator heap (dlmalloc 2.7 derivative, 0x518 bytes)

[docs/15 Phase 8](../15-native-patches.md), `native/ts3native/alloc.cpp`. All [verified: decompile].

| Offset | Meaning |
|---|---|
| +0x0 | initialized |
| +0x4 | `max_fast` (0x40) with bit 0 = fastbins hold chunks |
| +0x8..+0x24 | fastbins |
| +0x28 | bins (unsorted bin +0x30: fd +0x38, bk +0x3C) |
| +0x430 | binmap |
| +0x440 | top |
| +0x444 | last remainder |
| +0x448 | list of VirtualAlloc'd cores |
| +0x488 / +0x48C | count / bytes of direct-mapped blocks |
| +0x4AC / +0x4B0 | out-of-memory callback |
| +0x4E4 | lock: pointer to CRITICAL_SECTION (+0x18 recursion count, spin count 10, `0x4E48C0`) |
| +0x518..+0x6A0 | debug variant table only |

Chunk: 16-byte header `{prev_size, size|flags, fd, bk}`, user pointer = chunk + 0x10, sizes multiples of 16 (`request2size 0x4E4770`: `(n + 0x1B) & ~15`, min 16). Size-word flags: bit 0 previous in use, bit 1 direct VirtualAlloc, bit 2 / bit 31 in a fastbin (bit 2 also debug tag). A chunk is in use when the next chunk's bit 0 is set. Request flag bit 3 = direct VirtualAlloc. Default allocator `0x931020` (ICoreAllocator vtable `0xFB8B38`: +4 aligned Alloc, +8 Alloc, +0xC Free) on the shared MemoryMan heap `[0x11CB864]`.

---

## 10. Resource manager and caches

[docs/08 §2](../08-resource-offload-design.md), probe `/rescache` (`tools/probe/agent.js`).

### Manager (singleton `[0x11C78D8]`, getter `0x4AFD20`, ctor `0x4B33B0`)

| Offset | Meaning | Confidence |
|---|---|---|
| +0x0 | vtable: `0xFFE250` live subclass, `0xFB2DA0` base ctor's | [measured] |
| +0x30 / +0x34 | DBArray (package databases) begin/end | [verified] |
| +0xA0 / +0xA4 | cache array of 8-byte `{cache*, priority}` entries (`FindCache 0x4B0070`) | [verified] |
| vtable +0x54 | register cache (used by `ResourceSystem_Init 0x738670`) | [inferred] |

### ResourceCache (ctor `0x732910`, vtable `0xFFDD94`)

| Offset | Meaning | Confidence |
|---|---|---|
| +0x20 / +0x24 | key map buckets / bucket count (EASTL hash_map) | [verified] |
| +0x28 | entry count | [verified] |
| +0x8C | `CacheBudget` (Default.ini 200 MB) | [verified] |
| +0x90 | overflow budget (10 MB) | [verified] |
| +0x94 | bytes held | [verified, measured] |
| +0x98 | bytes on the flush list (moved back to +0x94 on a hit, `0x732B60`) | [inferred] |

The animation cache is the same class with a fixed 15 MB budget [verified]. Measured: ~390-410 MB held in Bridgeport whatever the budget [measured, docs/08 Phase 0].

### Other resource objects

| Object | Offset | Meaning | Confidence | Source |
|---|---|---|---|---|
| AsyncLoader (init `0x7274D0`) | +0x38 | `[ResourceSystem] MemoryUsageLimit` | [verified store; users unknown] | [docs/08](../08-resource-offload-design.md) |
| Preloader (`0x731AE0`) | +0xC | `HoldOntoResources` (default true) | [verified] | [docs/08](../08-resource-offload-design.md) |
| WorldManager (`Update 0xC6D570`) | +0x258 | "skip lot streaming" flag (S3SS map-view patch) | [community, verified bytes] | [docs/03](../03-prior-art-re.md) |
| String table service (S3PP LSC) | +0x48 | change marker the S3PP cache keys on | [community source] | [docs/17 §8](../17-heavy-store-content.md) |

### DBPF facts

| Fact | Confidence | Source |
|---|---|---|
| Header check `0x4A7AD0`: magic `DBPF`, major < 3, index count < 0x7FFFFFF, index inside the file | [verified] | [docs/08 §2](../08-resource-offload-design.md) |
| In-memory index: 16 B per entry, +8 B when a size is >= 0xFFFF, +16 B per type/group pair (~17.6 B average) | [verified layout] | [docs/08](../08-resource-offload-design.md) |
| Compression field `0xFFFF` = RefPack; signature `0x10FB` checked by `0x4EC010` before the core `0x4EB3B0` | [verified] | [docs/08](../08-resource-offload-design.md), [docs/15 Phase 6](../15-native-patches.md) |
| Packages are not memory-mapped: `ReadFile` into a heap buffer, decode into a second, factory builds the object, buffers freed | [verified] | [docs/08](../08-resource-offload-design.md) |
| Save working copy DBPF vtable `0xFB2600`: +0x18 open, +0x1C commit (`0x4A8500`), +0x08 close, +0x3C write a record | [verified] | [docs/15 Phase 7](../15-native-patches.md) |

---

## 11. Save info (native save `0xAB1030`)

| Offset | Meaning | Confidence | Source |
|---|---|---|---|
| +0x430 | wide path `Saves\<name>.sims3\<world>.nhd` (filled by `0xAAE950`) | [verified] | [docs/15 Phase 7](../15-native-patches.md) |

---

## 12. Animation (per-actor update, `0x571B90`)

[docs/22](../22-parallel-animation.md).

| Object | Offset | Meaning | Confidence |
|---|---|---|---|
| Actor list entry | +0x18 | intrusive link (entry = link - 0x18) | [verified] |
| Actor list owner | +0x28 | flags; bit 3 = iterating | [verified] |
| Deferred id list | +0x18..+0x1C | object ids drained by `0x571A50` | [verified] |
| Track | +0x18 / +0x19 | finished / removed | [verified] |
| Animation component | +0x4 | owner object | [verified] |
| Animation component | +0x24 | rig state (floats +0x0..+0xB8: transform cache +0x0..+0x1C, root motion +0x30..+0x5C, +0x60..+0x7C; idle counter +0xB4; rig pointer at +0xB0) | [verified] |
| Animation component | +0x28 / +0x2C | optional extra (calls `0x5813C0` when set) / flags (bit 31) | [verified] |
| AnimRequest (SACS PlayAnimation) | +0x50 flags, +0x68 blendIn, +0x6C blendOut | [community] |

---

## 13. Graphics device and frame

| Object | Offset | Meaning | Confidence | Source |
|---|---|---|---|---|
| Graphics device | +0x8D | "window not foreground or device lost" (BeginScene `0x611620` returns false, rendering skipped) | [verified] | [docs/03](../03-prior-art-re.md), [docs/04 F19](../04-re-findings.md) |
| D3D device vtable | +0xA4 BeginScene, +0xA8 EndScene, +0x44 Present | [verified] | [docs/04 F19](../04-re-findings.md) |
| GlobalLightSettings | +0x270 / +0x290 `mBradyBunchBlue` | [community] | [docs/03](../03-prior-art-re.md) |

---

## 14. Object-id hash (`0x939100`)

Fixed 1,033 buckets, `key % 0x409`, chained; reached by ~300 internal calls [verified, docs/20 item 5].

---

## 15. `ts3native_counters` (our export, layout v10, 83 x int32)

Exported by `ts3native.asi` (`Process.findModuleByName('ts3native.asi').findExportByName('ts3native_counters')`). Writable runtime switches marked **W**. Source: `native/ts3native/ts3native.cpp`, [docs/15](../15-native-patches.md). All [verified: our source].

| Offset | Field | Offset | Field |
|---|---|---|---|
| +0x00 | version | +0x6C | **W** asyncMode |
| +0x04 | enabled (route detour) | +0x70 | asyncReady |
| +0x08 | calls | +0x74 | asyncStarted |
| +0x0C | hits | +0x78 | asyncRefused |
| +0x10 | misses | +0x7C | asyncCollected |
| +0x14 | stored | +0x80 | asyncDropped |
| +0x18 | otherFails | +0x84 | asyncUs |
| +0x1C | windowMs | +0x88 | asyncMaxUs |
| +0x20 | **W** workerMode | +0x8C | asyncPumpUs |
| +0x24 | workerReady | +0x90 | asyncPumpWaits |
| +0x28 | workerCalls | +0x94 | saveReady |
| +0x2C | workerUs | +0x98 | **W** saveMode |
| +0x30 | workerMaxUs | +0x9C..+0xBC | saveJobs, savePromoted, saveFallbacks, saveCount, saveLastUs, saveLastWriteUs, saveLastBackupUs, saveLastCopyUs, saveLastFinalUs |
| +0x34 | waitUs | +0xC0 | decompressReady |
| +0x38 | fallbacks | +0xC4 | **W** decompressMode |
| +0x3C | watchdogs | +0xC8 | **W** decompressVerify |
| +0x40 | **W** budgetMode | +0xCC..+0xEC | decompressCalls, Fails, Mismatches, MaxUs, InMB, OutMB, Ms, MainCalls, MainMs |
| +0x44 | budgetReady | +0xF0 | hostCalls |
| +0x48 | budgetCalls | +0xF4..+0x100 | planOffCalls, planOffFails, planOnCalls, planOnFails |
| +0x4C | budgetCapped | +0x104 | allocReady |
| +0x50 | budgetCapNodes | +0x108 | **W** allocMode |
| +0x54 | budgetCapUs | +0x10C..+0x12C | allocHitsK, MissesK, CachedK, PassedK, Threads, Flushed, Dropped, Shutdowns, OomRetries |
| +0x58 | budgetMaxNodes | +0x130 | waterJpsReady |
| +0x5C | budgetMaxNodesOk | +0x134 | **W** waterJpsMode |
| +0x60 | budgetMaxUsOk | +0x138 | **W** waterJpsVerify |
| +0x64 | invokeHook | +0x13C..+0x148 | waterJpsCallsK, CellsK, Mismatches, Fallbacks |
| +0x68 | invokes | | |
