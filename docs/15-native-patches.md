# 15. ts3native.asi: our native patches

Our own ASI plugin for patches the C# mod cannot do. Source: `native/ts3native/`. Build: `native\ts3native\build.cmd` (VS 2022 Professional, `vcvars32.bat`, `cl /LD /O2 /MT`, 32-bit, static CRT). It also builds and runs `selftest.exe`, which checks the patch and detour helpers on local memory. Output: `native/ts3native/out/ts3native.asi` + `ts3native.ini` (git-ignored).

Tags as in docs/13: **[code]** = checked in the exe bytes or decompile, **[inferred]** = to confirm by measurement.

Target: TS3W.exe Steam 1.67, SHA-256 `10b2423e2368e161774462b0ef1960231ad0af298d564a7bb6a6e78c5e880609`. PE header of `game-copy/bin/TS3W.exe`: ImageBase `0x400000`, DllCharacteristics `0x0` (no DYNAMIC_BASE, so no ASLR), Characteristics `0x123` (relocations stripped). Sections: `.text` 0x401000, `.rdata` 0xF95000, `.data` 0x1146000. **[code]**

## How it works

- `DllMain(DLL_PROCESS_ATTACH)` runs all patches once. It does nothing unless the host is `TS3W.exe` loaded at `0x400000`.
- Each patch lists its sites with the full original instruction bytes. All sites of a patch are compared first; if any byte differs, that patch is skipped and the found bytes are logged. Only then are the immediates written, with `VirtualProtect` around each write and `FlushInstructionCache`. Memory is checked with `VirtualQuery` before it is read, so a wrong exe cannot crash the plugin.
- Settings: `ts3native.ini` next to the `.asi`. Log: `ts3native.log` next to the `.asi`, rewritten each launch (version, host, settings, each patch `OK`/`SKIP`/`FAIL` with old and new values).

## Phase 1: GC patches (docs/13)

| Patch | Site | Original bytes | Instruction | New (defaults) | ini |
|---|---|---|---|---|---|
| Initial heap | `0xE4E168` (imm `0xE4E169`) in `GC_init` `0xE4E160` | `BD 00 28 00 00` | `mov ebp,0x2800` (40 MB in 4 KB blocks) | `BD 00 00 01 00` = 0x10000 blocks = 256 MB | `InitialHeapMB` (40-1024, default 256) |
| Growth step 1/3 | `0xE4A1D8` (imm `0xE4A1D9`) in `GC_collect_or_expand` `0xE4A120` | `3D 00 08 00 00 76 2C` | `cmp eax,0x800 ; jbe` | `3D 00 20 00 00` = 0x2000 = 32 MB | `GrowthStepMB` (8-256, default 32) |
| Growth step 2/3 | `0xE4A1FF` (imm `0xE4A200`) | `3D 00 08 00 00 77 05` | `cmp eax,0x800 ; ja` | `3D 00 20 00 00` | same |
| Growth step 3/3 | `0xE4A206` (imm `0xE4A207`) | `B8 00 08 00 00 50` | `mov eax,0x800 ; push eax` | `B8 00 20 00 00` | same |
| Budget ceiling (off by default) | `0x1192324` (`.data` dword) | `DC 05 00 00` | 1500, max of the per-frame GC budget `0x11922A4` | e.g. `C4 09 00 00` = 2500 | `BudgetCeiling` (0 = unchanged, else 500-20000) |

Only the trailing opcode bytes (`76 2C`, `77 05`, `50`) are in the comparison, not written. The three growth sites are patched together or not at all. Values equal to the game default (40 MB, 8 MB) write nothing. `GC_INITIAL_HEAP_SIZE`, if set in the environment, still overrides our default because `GC_init` only uses `ebp` when the env var is absent (decompile `gc13/core.c`); the plugin logs a note when it sees the variable. **[code]**

### Is DllMain early enough? Yes.

- `GC_init` (`0xE4E160`) begins `cmp dword [0x11F30D0],0 ; push ebp ; mov ebp,0x2800 ; jnz <return>`: it reads the immediate on its single real run, guarded by `GC_is_initialized` (`0x11F30D0`), which it sets to 1 at the end. **[code]**
- It has no static or CRT-initializer caller. The only direct calls come from lazy GC entry points: `0xE4A06D` and `0xE4A0CD` (`GC_try_to_collect` / `GC_gcollect`), `0xE4BAB0` and `0xE4BC07` (large-object alloc), `0xE4E47E` (`GC_enable_incremental`, called from MonoScriptHost init at `0xD7E602`). The only `jmp`s to it come from the public wrapper `0xE4E4C0` (init the allocation lock, then `jmp GC_init`), which is called from `0xE66242` and `0xE75C22`; walking callers up, both lead to `0xE5A680` (Mono runtime init), called only from `0xD7E60C` inside MonoScriptHost init (`0xD7E560`). No absolute pointer to either function exists in the file. So the first GC init happens when MonoScriptHost starts Mono, well after `WinMain`. **[code: E8/E9 and pointer scan of the whole file]**
- Ultimate ASI Loader, as `wininet.dll`, loads plugins during process start-up, before the exe's entry point (`0xF7F371`) runs. **[community: UAL design; S3SS relies on the same timing]**
- Runtime proof per launch: the plugin reads `0x11F30D0` before patching. If it is already 1, the initial-heap patch is skipped and the log says "plugin loaded too late". The growth-step and ceiling patches still apply, as both are read on every use.

## Phase 2: route negative cache (v0.2.0, `[RouteCache] Enabled=0` by default)

Built and self-tested; **not yet run in the game**. Recommendation 1 of docs/11: a repeat of a request that just failed with "no path" returns a plain failure at once, before the RoutingWorld lock, so none of the native retries, the obstacle replan, or the C# fail-path plans run.

### Patch site and calling convention [code: dumpbin disassembly of `0x82C6E0`–`0x82C7FB`, call sites `0x760DE1`, `0xC7C6E3`, `0xC7C9D5`]

`InstallDetour` writes `jmp hook` over the 6 bytes `81 EC D8 00 00 00` (`sub esp,0xD8`; checked first, skipped and logged if different). The trampoline is that instruction plus `jmp 0x82C6E6`.

`Route_PlanLocked` is `__thiscall`: `ecx` = RoutingWorld (`0x8292D0()` at the PlanCommon call site). It takes 10 stack dwords and ends with `ret 28h`. The result is a bool in `AL`. The hook is `__fastcall` with a dummy `edx`, which gives the same stack layout and cleanup.

| Stack arg | Value at the `0x760DE1` call (Route_PlanCommon, `esi` = native route) | Meaning |
|---|---|---|
| 1 | `esi+0xE0` | out path list (EASTL list head). PlanCommon empties it before the call (`0x7606E0` prologue) |
| 2 | PlanCommon arg 2 | start points vector `{begin,end}`, 64-byte records |
| 3 | local vector (accepted points) | goal points vector, 64-byte records, x,y,z floats at +0 (x/z bounds-checked in PlanCommon) |
| 4 | `[esi+0x3C]` | agent class: 0x800 flier, 0x900 walking dog, 0xA00 little dog, 0xB00 leading horse, 0x4400 shark (set in PlanCommon from the options) |
| 5 | `1` | do-plan flag (byte); 0 = take the lock, run `0x82C420`, return 0 |
| 6 | `esi+0x158` | RoutingOptions; RouteOption lo/hi at +0x28/+0x2C, RouteOption2 lo/hi at +0x30/+0x34 (`0x81C880` is called with `ecx = esi+0x158` at `0x7608AD`) |
| 7, 8 | `&local`, `&local` | out dwords, zeroed by the caller |
| 9, 10 | `[esi+0x40]`, `[esi+0x44]` | **follower ObjectGuid** (64-bit). docs/11 called these "lot id": `0x752620` compares each blocker id with route+0x40/+0x44 to skip the follower itself, so they are the follower's id [code for the comparison, meaning inferred] |

`0xC7BF70` (off-screen road placement) calls it directly with agent class 0, follower 0 and a local list.

Correction to docs/11 Q4: the lock is the object at `world+0x918` (`ecx` for `Lock_Acquire 0x4E16F0`). `0x1021CF0` is a constant pushed as its argument.

### How a failure is returned and recorded [code]

- After `RoutingJob` Execute, the function reads the first path in the out list, calls its vtable `+0x20` (status), and returns `status < 3`.
- PlanCommon then calls `0x752620` if the list is non-empty. That maps status 7/8 to `FailedBecauseNoPathToDestination` (16) plus the blocker id, type and location (vtable `+0x160/+0x164`).
- If the function returns 0 with the list still empty, PlanCommon jumps to `0x76156D` and returns its default result: `mType = 2` (**Failed**), all other fields 0, so **no blocker**. The original's own early-outs (no start, no goal, world flags `+0x97E`/`+0x976`) take this path.
- C# side (SimRoutingComponent.cs:1265–1300): with blocker id 0 and location zero, `GetRouteFailPath` returns null, so `DoRouteFailureBehavior` runs no fail-path plans. It still plays the route-fail reaction when `DoRouteFail` and the frequency check allow it, with no target object. The interaction's normal failure branch runs, the same as after the C# `RouteBackoff` fast fail.

### Hook logic (`PlanLockedHook` in `ts3native.cpp`)

1. Count the call. If do-plan is 0, or starts or goals are empty, call the original unchanged.
2. Build the key, a 64-bit FNV-1a hash of:
   - the agent class;
   - all four option words;
   - the follower ObjectGuid;
   - the start count and each start point's x,y,z, floored to **0.5 m** cells, in order;
   - the same for the goals.
   The key is strict on purpose: a miss costs one normal plan, while a false hit returns a wrong failure. No option bits are masked out.
3. If the key failed less than `WindowMs` ago (real time, `GetTickCount64`): **return 0** without the lock, the original, or touching the out list. Callers see a plain Failed with no blocker.
4. Otherwise call the original through the trampoline. If it returned 0 and the first path's status is 7 or 8, store the key with `until = now + WindowMs`. Successes are never stored, and neither are other failures (status 3–6, empty list).

The table is a fixed open-addressed array: `MaxEntries` rounded up to a power of two, 256–65536 slots, allocated once. A key probes 8 slots. A store takes the matching slot, else the first free or expired slot, else evicts the oldest of the 8, so the table never grows. Lookups hold an SRW lock shared and stores hold it exclusive, only around the table, never around the original call. The counters use `Interlocked*`.

### Settings and counters

`ts3native.ini` `[RouteCache]`: `Enabled=0` (no detour when 0, unless `[RouteWorker] Enabled=1`), `WindowMs=2000` (100–60000), `MaxEntries=4096` (256–65536). When the cache is on, `ts3native.log` stays open and gets `route cache: calls N hits N misses N stored N otherFails N` about every 60 s, written by whichever game thread calls the planner first after the minute is up.

Export for test tools: `ts3native_counters`, a data export of 16 int32 (8 in layout version 1, ts3native 0.2.0):

| Offset | Field |
|---|---|
| +0x00 | version (2 since 0.3.0) |
| +0x04 | enabled (1 when the detour is installed) |
| +0x08 | calls |
| +0x0C | hits |
| +0x10 | misses (keyed calls that ran the original) |
| +0x14 | stored |
| +0x18 | otherFails |
| +0x1C | windowMs (0 when the cache is off) |
| +0x20..+0x3C | route worker, see Phase 3 |

Frida: `const c = Module.findExportByName('ts3native.asi', 'ts3native_counters'); c.add(0xC).readS32()`.

### Relation to the C# RouteBackoff (`mods/ts3fixes/src/RouteBackoff.cs`)

The C# hook on `ScriptCore.Route.Plan` fast-fails unseen townies on foot for 30 sim-minutes, per Sim and destination 1 m cell. Those requests never reach native code, so they are not counted twice. The native cache covers what the C# hook leaves alone: selectable and on-screen Sims, pets, horses, non-Sim routers, short hops, native callers (`0xC7BF70`), and repeats within 2 s whose C# key differs.

One interaction: a native hit reaches the C# hook as `Failed` with no blocker, which the C# hook also records (it accepts `Failed` and `FailedBecauseNoPathToDestination`). That only happens when a real no-path failure of the same request occurred less than 2 s earlier. If strict separation is wanted, the C# hook can record only `FailedBecauseNoPathToDestination`, since a real no-path failure always comes back as 16 from `0x752620`.

### Risks

- **Stale failures** [inferred]: a door unlocked, an object moved or sold, a wall removed, or a Sim that was blocking walks away. A request that would now succeed still fails for up to `WindowMs`. Mitigations:
  - The window is short (2 s real time).
  - The key includes the exact start cell, so a Sim that has moved is a new request.
  - There is no routing-world epoch yet. The pending-update flag `[world+0x95C]+0x10E` from docs/11 is unverified, so it is not used.
- **Transient Sim blockers** are cached too. The native hook does not decode the blocker type, because the `+0x164` argument layout is not verified. Sims that re-plan within 2 s after being blocked by another Sim get a plain failure instead of waiting their turn. Watch for this in the test.
- **0.5 m cells**: two starts in the same cell on opposite sides of a thin wall share a key, for 2 s.
- **Hash collision**: 64-bit keys, at most 65536 live entries; negligible.
- Another plugin that hooks `0x82C6E0` first changes the prologue. Ours then skips, logs `SKIP detour Route_PlanLocked`, and leaves `enabled = 0`.

### Test plan

1. Set `Enabled=1`, start the game, and check `ts3native.log` for `OK detour Route_PlanLocked at 0x0082C6E0`. Check the Frida export: `enabled = 1`, `windowMs = 2000`.
2. **Repro**: on a quiet lot, use the agent action `sim_route_to` to an unreachable point, e.g. inside a closed room with no door, or on a roof. Repeat 10 times in quick succession.
   - Expect `stored` +1 on the first try, then `hits` rising for repeats within 2 s.
   - The Sim plays its route-fail reaction at most once, then stands idle and stays controllable. Queue a reachable walk right after: it must succeed at once (different key).
3. **Stale-failure check**: make a point unreachable (lock a door for that Sim) and fail a route to it. Unlock the door, wait more than 2 s, and route again: it must succeed. Within 2 s, a plain failure is expected.
4. **Soak**: 30 min in the stutter save (Isla Paradiso or Sunset Valley) with `Enabled=0`, then the same with `Enabled=1`. Compare against the baseline table:
   - frame-time spikes and route time per `Route.Plan` from the probe;
   - the hit ratio from the counters;
   - `stuck` Sims: any Sim standing still with an interaction queued for more than 1 sim-hour.
   Also log any Sim that keeps re-queuing the same failed interaction. A plain failure that loops would show here.
5. If stuck Sims or loops show up, try `WindowMs=500` before turning the cache off.

## Phase 3: route worker, offload-sync (v0.3.0, `[RouteWorker] Enabled=0` by default)

docs/18 first step: prove the route search runs safely on another thread, with no gameplay change. Built and self-tested 2026-10-13; not run in game yet.

- **Hook**: the same `Route_PlanLocked 0x82C6E0` detour as the cache (`PlanLockedHook`). One detour serves both; it is installed when either `[RouteCache] Enabled=1` or `[RouteWorker] Enabled=1`. Order per call: the original's early-outs run on the caller; a cache hit returns on the caller; everything else (cache miss, or cache off) goes to `RunPlan`.
- **Threading**: one persistent worker thread (`CreateThread`, 1 MB stack reserve), started in `DllMain` (it runs once the loader lock is released). With `workerMode = 1`, `RunPlan` copies the 10 arguments, `ecx` and the caller's x87 control word and MXCSR into one job slot, signals the worker and blocks on an event. The worker sets the same FPU state, calls the original through the trampoline and signals back. One job at a time (a critical section); a call made on the worker itself runs there directly (`fallbacks`). Same arguments, same result, only the thread changes.
- **Watchdog**: every 2 s of waiting logs `route worker: thread N waited S s for worker thread W ...` and counts `watchdogs`; the caller keeps waiting (no unsafe recovery). Any watchdog line is deadlock evidence (docs/18 hazard 2).
- **Settings**: `Enabled` (start the worker, install the detour), `Mode` (start value of the runtime switch, 0 = plan on the calling thread, 1 = on the worker). With `Enabled=1, Mode=0` the detour only passes through until the switch is flipped.
- **Runtime switch**: `workerMode` at `ts3native_counters+0x20` is writable. Probe: `GET /native?routeworker=1` (or `0`); `profile-world.ts --ab routeworker` alternates it per slice.
- **Log**: about every 60 s, `route worker: mode M calls N worker N ms max N us wait N ms fallbacks N watchdogs N`.

| Offset | Field |
|---|---|
| +0x20 | workerMode (writable runtime switch) |
| +0x24 | workerReady (1 when the worker runs and the detour is installed) |
| +0x28 | workerCalls |
| +0x2C | workerUs (total time in the original on the worker) |
| +0x30 | workerMaxUs |
| +0x34 | waitUs (total caller wait; `waitUs - workerUs` = handoff overhead) |
| +0x38 | fallbacks |
| +0x3C | watchdogs |
| +0x64 | invokeHook (v4: 1 runtime_invoke slot hooked, -1 refused, 0 not yet) |
| +0x68 | invokes (v4: managed calls from the worker run on the caller) |

Self-test (`selftest.cpp`, `TestRouteWorker`): mode 0 runs on the caller; mode 1 runs on the worker thread with the caller's x87 control word (24-bit precision set on purpose) and returns each call's own result; a plan started on the worker falls back to a direct call; `doPlan = 0` stays on the caller; with the cache on, a no-path failure found on the worker is stored and the repeat is a hit that never reaches the worker.

Probe for the same step: `GET /routelock?start=1` (opt-in, `stop=1` unhooks) hooks `Route_PlanLocked`, `Lock_Acquire 0x4E16F0` / `Lock_Release 0x4E17B0` filtered on `world+0x918`, and `0x83CED0` (iteration count). It reports acquisitions, wait and hold per thread, plan length percentiles, and the overlap **O**: for each plan, `min(plan length, gap until the sim thread next acquires the routing lock)`. `Lock_Acquire` is engine-wide, so every acquire enters the Frida script; `acquireHooksPerSec` shows that cost. Measure O with `workerMode = 0`.

### Crash 2026-10-13 and fix (v0.4.1, still `Enabled=0` by default)

First in-game use (Bridgeport, `workerMode` switched to 1, agent `sim_route_to`) crashed within about 1 s: `xcpt <pc> 26-10-13 01.11.31.txt`, ACCESS_VIOLATION reading 0x58 at 0x00EA2B03, `ecx = edx = 0`, stack at 0x03B6xxxx.

- **0x00EA2B03** is `mono_class_vtable(domain, class)` (0xEA2AD0, assert `object.c:962 "class"`), at `mov ecx,[edx+0x58]` = `domain->domain_id` with **domain = NULL** [verified, disassembly]. Its caller 0xEA36FF is `mono_object_new` (0xEA36F0 = `mono_object_new_specific(mono_class_vtable(d,k))`) [verified].
- **0x00E5A668** is **not** Boehm: it is the return address in the Mono interpreter's runtime_invoke 0xE5A600 (`interp.c`; `sub esp,150h`, builds a MonoInvocation and calls `ves_exec_method` 0xE59FD0) [verified]. 0xE59FD0 reads `mono_domain_get()` (0xE75260 = `TlsGetValue([0x11959DC])`) into the interpreter context, then 0xE52C70 boxes the non-void return value with `mono_object_new(context->domain, ...)` [verified, return addresses 0xE5A175, 0xE52D22, 0xEA36FF in the stack data]. 0xE5A600 is only reached through the runtime_invoke slot `[0x1195B60]` (set by `mono_install_runtime_invoke` 0xEA1470 from `mono_interp_init` 0xE5A680; 0xEA1080 "not supported" stub before) [verified].
- **Cause**: the route planner calls managed (script) code through `mono_runtime_invoke` during a plan. On the worker, which was never attached to Mono, the domain TLS is NULL, so the first managed call faults. docs/18 checked direct calls only; this path goes through at least one virtual call (no direct-call path from 0x82C6E0 to any runtime_invoke user within 30 levels) [verified]. Thread: the worker [inferred: only an unattached thread has a NULL domain; every game thread that plans is a script thread].
- **Why not attach the worker**: Boehm here is the 6.x win32 variant; its thread table (0x1221000, 256 x 0x14) gets one entry only, from `GC_thr_init` 0xE513D0 -> `GC_new_thread` 0xE51070 (single caller) [verified]. The game has no thread wrapper that registers other threads, and Mono state (thread statics, `MonoThread` TLS 0x1195B7C, the interpreter context TLS 0x11F4160) is per thread. Running script code on a second OS thread is a new, untested mode for this runtime.
- **Fix (0.4.1)**: managed calls go back to the waiting caller thread. On the first mode-1 plan, `EnsureInvokeHook` checks `[0x1195B60] == 0xE5A600` and the prologue `81 EC 50 01 00 00 68 50 01 00 00`, then swaps the slot (`InterlockedCompareExchange`) to `InvokeHook`. Any other value: `invokeHook = -1`, logged, and every plan stays on the calling thread. `InvokeHook` passes all threads but the worker straight to the original. On the worker it posts the call and waits; the caller, which waits in `RunPlan` on `{jobDone, invReady}`, runs the original runtime_invoke on its own thread and signals back. A plan started from inside that managed call goes to the worker the same way (the worker waits on `{invDone, jobReady}`), so exactly one of the two threads runs at a time, as on one thread before. The first 8 distinct MonoMethod pointers are logged (`route worker: planner made a managed call, MonoMethod ...`) to name the callbacks.
- **Counters (v4)**: `+0x64 invokeHook` (1 hooked, -1 refused, 0 not yet), `+0x68 invokes` (managed calls run for the worker). Periodic log adds `invokeHook N invokes N`.
- **Self-test**: refused slot (plans stay inline, slot untouched); hooked slot passes other threads through; a worker plan's managed call runs on the caller with its result returned; worker plan -> managed call on the caller -> nested plan back on the worker.
- **Still open** [inferred]: (1) a managed call made by another route than runtime_invoke (the game's direct interpreter wrappers 0xD7EED0/0xD7F000/0xD7F210, or native glue that allocates with `mono_domain_get()` before invoking, e.g. `mono_string_new`) would still fault on the worker; the crash shows this plan's glue did not. (2) A managed exception that propagates (exc = NULL) longjmps over `RunPlan` on the caller while the worker holds the routing lock: hang, not corruption; the original would unwind through the planner too. (3) A boxed return value lives only on the worker's stack until the plan reads it; a GC in a later managed call of the same plan could free it (Boehm does not scan the worker). The callback is now identified (docs/18 "Managed calls during a plan": `LotManager.PortalLockedCallback` via World event 59 from the portal-edge cost `0x634500`); it allocates, so a GC inside a plan is possible, but `0x78F2B0` copies the boxed result right after the call [inferred].

Unverified: the planner has no thread affinity (TLS, profiler scopes, asserts); the calling thread never already holds `world+0x918` when it calls `Route_PlanLocked` (if it did, the worker would deadlock on the lock, and the watchdog would show it); an access violation inside the search on the worker is not caught by any handler the sim thread may have.

## Phase 4: route search budget (v0.4.0, `[RouteBudget] Enabled=0` by default)

docs/11 Q3 / docs/18 rank 2: one route request can no longer search without limit. Built and self-tested 2026-10-13; not run in game yet.

**Hook** [verified in `astar-676260.asm` and the bytes of `game-copy/bin/TS3W.exe`]: the world A\* `0x676260` loads its cost function into `[esp+0x60]` with one of five `mov dword [esp+0x60], imm32` (`C7 44 24 60 imm32`) at `0x676340/4A/54/5E/68` (travel modes 1-4 → `0x674E40`, `0x673850`, `0x674250`, `0x675800`; default `0x6734E0`). It calls it as cdecl with 8 args, float in ST0. Arg 5 is 1 for an edge cost, which the caller compares against the 1e8 wall (`0x00FEF678`) at `0x676683`, `0x6768A6` and `0x676B24`; it is 0 for the heuristic, which runs once per generated node right before the node is allocated and pushed (plus once per search for the start). The patch swaps the five immediates for stubs (`push orig; jmp CostGate`). This needs no code cave at the back-edge and no open-list drain (the docs/11 Q3 plan).

**How the cap ends a search**:
- The budget is armed per `Route_PlanLocked` request (`CallPlan`, on the thread that runs the original: the caller's or the worker's). The state is thread-local (`__declspec(thread)`, 0.4.2), so a plan on another thread neither reads nor overwrites it; a plan nested on the same thread (from a managed callback inside the search) gets its own budget and the outer one resumes after. It counts heuristic calls (= nodes generated) across every A\* run in the request, its retries included.
- The `MaxUs` clock starts at the first node (0.4.2), after `Route_PlanLocked` has taken the routing lock `world+0x918`, so lock waits do not count. It still includes managed callbacks made during the search (the portal-lock query, docs/18). The counters (`budgetCapUs`, `budgetMaxUsOk`) keep the whole request time, lock wait included.
- Over `MaxNodes`, or over `MaxUs` (checked every 16 nodes), every further edge cost returns 1e9. Nothing new is pushed, the queued nodes are popped (the goal check on pop still runs) and the loop leaves through its own "open list empty" exit `0x676BF7` (frees the heap, returns 0). That is the same exit as any failed search, so nodes go back to the pool as usual.
- Retries in the same request (`0x661B50` per-goal-lot plans, `0x81C67A` obstacle replan) find the budget spent and fail after one pop each.
- The result is the game's ordinary no-path failure (status 7/8 → `FailedBecauseNoPathToDestination` with the best node found as the blocker hint) [inferred from docs/11 Q1: not traced which code the planner returns for a world-leg failure]. No new result code.
- Not capped: the lot/portal A\* `0x63E340` and in-room `0x632E40` (other code), and the C# fail-path plans (`DoRouteFailureBehavior`, 2-4 more requests), each of which is a new request with its own budget.

**Script reaction** [verified in code]: a no-path failure takes each caller's normal failure branch. For unseen townies on foot, ts3fixes `RouteBackoff` then fast-fails the same Sim and destination for 30 sim-minutes (it records only `FailedBecauseNoPathToDestination`, and not when the blocker is a Sim or vehicle). The native route cache stores the capped failure for `WindowMs`.

**"Unseen townies only" cannot be decided natively.** The request has the agent class, follower ObjectGuid and options, but not selectable/on-screen/household state. `OnlyFlagged=1` caps only routes with RouteOption `DebugPathPlan` (hi word bit `0x1`, opts+0x2C), for a later C# caller (RouteBackoff) that sets the bit for unseen townies. Nothing sets it yet, so the default is to cap **all** searches with a generous cap.

**Settings**: `Enabled`, `Mode` (start value of the switch; 1 = cap), `MaxNodes` (0 = off; no calibrated default), `MaxUs` (default 100000), `OnlyFlagged` (0).
- **Default cap, 100 ms per request** (derived, not measured): 4× the Isla Paradiso mean plan (25.4 ms, F7b) and 28× Bridgeport's (3.6 ms). Bridgeport's longest single search was 188 ms, and failed requests cost up to ~250 ms each (4 failed plans in one 1,010 ms iteration). So the cap cuts the failure tail but leaves almost every successful plan alone.
- A node cap is better (deterministic, the same on the worker and under load), but no nodes-per-ms figure exists. Run once with the time cap and `budgetMode = 0`, read `budgetMaxNodesOk`, then set `MaxNodes` above it.

**Runtime switch and counters** (layout v3; `GET /native?routebudget=0|1`; `profile-world.ts --ab routebudget`):

| Offset | Field |
|---|---|
| +0x40 | budgetMode (writable switch) |
| +0x44 | budgetReady (stubs patched and detour installed) |
| +0x48 | budgetCalls (requests run under a budget) |
| +0x4C | budgetCapped |
| +0x50 | budgetCapNodes (sum of nodes when the cap tripped) |
| +0x54 | budgetCapUs (sum of capped request time: search, drain and retries) |
| +0x58 | budgetMaxNodes (any request) |
| +0x5C | budgetMaxNodesOk (largest request that found a path) |
| +0x60 | budgetMaxUsOk (longest request that found a path) |

The log line every 60 s is `route budget: mode … calls … capped … capNodes … capMs … maxNodes … maxNodesOk … maxUsOk …`. `budgetCalls` and the maxima count only requests run with the switch on, so calibrate with the switch on and a high `MaxUs` (for example 2000000), not with mode 0.

**Fail safe**: all five sites are verified before any write (a mismatch logs `SKIP route budget` and nothing is patched). With no budget armed on the current thread, a stub passes straight through to the original (one call and a thread-local flag check per cost call). A search not started from `Route_PlanLocked` is never capped.

Self-test (`TestRouteBudget`): one changed site → nothing written; switch off → passes through; cap above the search → same result and counters; cap of 300 → the next edge is a wall and the fake A\* fails through its own exit with 300 expansions; a search outside a request is untouched; `OnlyFlagged`; time cap; 30 ms before the first node (lock wait) is not capped by a 5 ms `MaxUs` but shows in `budgetMaxUsOk`; a capped plan on a second thread in the middle of ours leaves our budget intact (202 nodes, not capped) and ours still caps at 300; through the route worker the worker's own budget caps the plan and the caller's stays off.

**Test plan** (serial, saves backed up):
1. `Enabled=1, Mode=1, MaxUs=2000000`. Check `OK route budget` in the log and `budgetReady = 1` in `/native`. Play 10 min on `Baseline-IP`, then read `budgetMaxNodesOk` / `budgetMaxUsOk` (calibration) and confirm `budgetCapped = 0`.
2. Repro `sim_route_to` to an unreachable point (closed room or roof) with `MaxNodes` set below the measured failure size: `budgetCapped` +1, the Sim fails as before (one route-fail reaction) and stays controllable; a reachable walk right after succeeds. Then a long legitimate trip (IP cross-water, `bp-route-cache` repro) with the default cap: it must still succeed (`budgetCapped` unchanged).
3. `profile-world.ts --ab routebudget` on `Baseline-IP` and `Baseline-BP`: route ms/s, hitches ≥ 50 ms, fail % and stuck Sims, on vs off.

**Risks** [inferred]: a reachable destination that needs more than the cap now fails, and RouteBackoff then blocks that unseen townie from it for 30 sim-minutes (a long trip for a played Sim fails instead of being slow). A time cap depends on load: a slow machine or a descheduled thread fails more routes. The capped search still pops every queued node, which is cheap but not free. Lot searches stay unbounded.

## Phase 5: async route plan (v0.5.0, `[RouteAsync] Enabled=0` by default)

Built and self-tested 2026-10-13, not run in game. The sim thread no longer waits: a plan from a Sim's own yielding task runs on the worker while the task sleeps (ts3fixes `PlanAsync`). Three more detours (`Lock_Acquire 0x4E16F0`, `IC_Route_Plan 0x765160`, `Route_GetOption 0x74D040`), counters v5 (`asyncMode` writable at +0x6C ... `asyncPumpWaits`). Design, protocol, risks and test plan: docs/18 "Async plan".

## Phase 6: faster resource decompression (v0.7.0, `[Decompress] Enabled=0` by default)

Built and self-tested 2026-10-13, not run in game. Offload candidate #1 from docs/08: resource decompression. Source `native/ts3native/decompress.cpp` (included by `ts3native.cpp`), offline test `decompress-test.cpp`.

### Where decompression happens

| What | Address | Evidence |
|---|---|---|
| RefPack codec object (vtable `0xFB9018`: dtor, compress `0x4EC200`, decompress `0x4EC010`, header check `0x4EC080`, name "Refpack") | `0xFB9018` | vtable dump + decompile [code] |
| Decompress slot: checks the `0x10FB` signature and header size <= buffer, then calls the core | `0x4EC010` (`__thiscall`, `ret 14h`; only reference is the vtable) | decompile + bytes [code] |
| Decode core: cdecl `(dst, dstCap, src, srcLen)`, byte-by-byte copies, bounds-checked, returns the header size after the stop code or 0 | `0x4EB3B0`-`0x4EB74C` | decompile [code] |
| The only call to the core | `0x4EC065` `E8 46 F3 FF FF`, then `add esp,10h` | byte scan of the whole exe for `E8`/`E9` rel32 and absolute refs [code] |
| Users of the codec: PackedFile `0x4A7360`/`0x4A7030` (`ResourceLoad/PackedFile/CompressionRefpack`), MemoryDB `0x72B0A0`, PackedStream `0x72C980`, `0x72AFB0` | | string xrefs + decompile [code] |
| zlib 1.1.3 inflate (`0x4331F0`, "incorrect header check") with callers `0x435384`, `0x43D816`, `0x43DD0F` next to libpng-style errors ("Extra compressed data", "Decompression error") | `0x431A80`-`0x43DDA1` | strings + call scan [code]; PNG use [inferred] |

- **Packages use RefPack only.** All 267 installed `.package` files: 602,169 RefPack resources (11.0 GB compressed, 33.4 GB decompressed), 314,217 uncompressed, **no zlib (`0x5A42`)** [code, index survey]. Half the RefPack bytes are resources of 0.5-2 MB (textures, meshes); the largest is 5.5 MB.
- zlib has no script-reachable caller within 10 calls (`CallPaths` from all 1,716 `IC_*`) and appears in at most a few sim-thread samples per run. Not a target.
- **Which thread:** whichever thread asks for the resource; the decoder has no thread of its own and runs synchronously inside the caller (docs/08: under the global resource lock `0xFB2CD0`). In the 15 native sim-thread profiles in `probe-out/profile/*-simstack.jsonl` (5 Hz, 10-min runs in all three worlds, 9,890 samples), the decoder (return address `0x4EC06A`, with S3SS's code on top) is on the stack in **24 samples = 0.24%** of sim-thread time in live mode (0-4 per run) [code, measured]. Those stacks run through CAS outfit building (`IC_CASUtils_SetOutfitInGameObjectDelayed`) and the TaskObjects stage `0xD7FC60`, so the sim thread does decode synchronously, but rarely. World load, CAS and Buy were not sampled; the counters below measure them.
- **S3SS already replaces the core.** `RefPackDecompressor` (on in our install, docs/05) writes a `jmp` at `0x4EB3B0` to an SSE2/AVX2 version (source `patches/refpack_decompressor_patch.cpp`, read 2026-10-13 at commit 5eb2c65). It copies 16/32 bytes only for literal runs and matches with offset >= 16; short literals (1-3 bytes) and matches with offset < 16 stay byte loops.
- **No splitting.** One RefPack stream is strictly sequential (each match copies earlier output). Parallel work is only possible across resources (docs/08 helper pool). The largest resource decodes in about 2-4 ms, so no single decompression is a hitch on its own [inferred from the offline MB/s].

### What we built

- `RefpackRef`: exact C port of `0x4EB3B0` (same checks in the same order). Reference for the tests.
- `RefpackFast`: same result and the same failure cases, but "wild" copies away from the buffer ends (LZ4 style): 4 bytes for every 1-3 byte literal, 16-byte blocks for literal runs and matches with offset >= 16, 8-byte blocks for offset 8-15, `memset` for offset 1, byte loop for offset 2-7. The overrun stays inside `[dst, dst + min(cap, header size))` and inside the source, and the next command overwrites it. Near the ends it copies exactly. SSE2 only (every CPU that runs Windows 11).
- Hook: the rel32 of the call at `0x4EC065` points to `RefpackHook` (5 bytes verified first, else `SKIP` in the log). We never touch `0x4EB3B0`, so S3SS's `jmp` there stays and is what mode 0 runs; the two patches do not overlap, and the load order of the two ASIs does not matter.
- Runtime switch `ts3native_counters.decompressMode` (counters v7, appended after the v6 save fields: `decompressReady, decompressMode, decompressVerify, decompressCalls, decompressFails, decompressMismatches, decompressMaxUs, decompressInMB, decompressOutMB, decompressMs, decompressMainCalls, decompressMainMs`; `decompressMode` at +0xC4, `decompressVerify` at +0xC8). 64-bit byte/time sums and the per-thread split (first 8 threads) stay internal and go to `ts3native.log`. Mode 0 = the code at `0x4EB3B0` (S3SS or the game), 1 = `RefpackFast`. `decompressVerify=1` also decodes with `0x4EB3B0` into a scratch buffer, compares, counts mismatches and keeps that result (slow, first runs only).
- Log every 60 s (`ts3native.log`): `decompress: mode .. calls .. fails .. in .. MB out .. MB time .. ms max .. us`, then one line per thread (first 8 threads that decode; `(main)` = the thread that loaded the ASI). The probe's sim thread id tells which line is the sim thread.

Settings (`ts3native.ini`):

```ini
[Decompress]
Enabled=0   ; 1 = retarget the call at 0x4EC065
Mode=1      ; start value of the runtime switch
Verify=0    ; 1 = compare every decode with 0x4EB3B0 (slow)
```

### Offline results (2026-10-13, i7-11800H, 32-bit /O2 build) [measured]

`out\decompress-test.exe ["<game dir>"] [MB]` reads a byte-proportional sample of every installed package (read-only), decodes each resource with `RefpackRef` and `RefpackFast`, asserts the same result, the same bytes and 64 untouched guard bytes past the buffer, then times three passes each (best of 3). A third decoder can be compiled in with `/DEXTRA_DECODER` (we used an extract of S3SS's AVX2 path; it is not in the repo).

| 321,378 resources, 502 MB compressed, 3,707 MB out | all sizes | resources < 16 KB (629 MB out) |
|---|---|---|
| Game decoder (C port of `0x4EB3B0`, modern compiler) | 1,277 MB/s | 796 MB/s |
| S3SS `RefPackDecompressor` (AVX2) | 1,502 MB/s | 793 MB/s |
| **`RefpackFast`** | **2,766 MB/s (2.2x game, 1.8x S3SS)** | **1,318 MB/s (1.7x)** |

Identity: 0 mismatches over 321,378 resources; none ends short of its header size; the game's decoder rejects none. A first run sampled per package (98,159 resources, 840 MB out) gave 1.78x vs the port and 1.75x vs S3SS. The selftest adds 3,000 random valid streams with every command type (incl. overlapping matches and the 0-3 byte stop tail) plus 12,000 corrupted ones (flipped bytes, cut input, smaller and larger buffers): same result as `RefpackRef`, same bytes wherever the game's decoder writes, nothing past the buffer. The game's own machine code was also copied out of the exe for a direct benchmark, but it crashed outside the game (not chased), so the "game" row is our C port; the real 2009-compiler code is likely slower, not faster [inferred].

### Expected gain in game [inferred]

- **Live mode:** decoding is 0.24% of sim-thread time; 1.8x faster saves about 0.1% of sim time. Not a stutter fix on its own.
- **Loads, CAS, Buy:** decoded bytes per world load are not measured yet; the ResourceCache holds 389-501 MB after a load (docs/17 §6), so assume 0.5-1.5 GB decoded. At S3SS's 1.5 GB/s that is 0.35-1.1 s of an 84-113 s load; `RefpackFast` saves 0.15-0.5 s (under 1%), on whatever thread loads. Shorter decodes also shorten holds of the global resource lock.
- So this is a small, safe win, and the counters give the first real numbers (bytes and ms per thread during load, CAS and Buy). If they show more than a few seconds of decoding per load, the next step is the docs/08 helper pool (decode across resources on other cores), not a faster single decoder.

### How to test in game

1. Build, then `install --only native-patches`; in `Game\Bin\ts3native.ini` set `[Decompress] Enabled=1`, `Mode=1`, `Verify=1` for the first run. Check the log for `OK   decompress: call 0x004EC065 -> RefpackHook`.
2. Load `Baseline-BP`, play 2 min, open CAS and Buy (`store-cas-browse`). Expect `mismatches 0`, `fails` the same as with mode 0. Then `Verify=0`.
3. Load split: `node tools/harness/offload-phase0.ts --only store-200` once with `Mode=0` and once with `Mode=1` (same cache state: warm both or cold both), compare menu/world load seconds and the `decompress:` log lines (MB and ms per thread; which thread is `(main)` and which is the sim thread from probe `/simstack`).
4. CAS/Buy: repro `store-cas-browse` with mode 0 and 1 (the switch can be flipped in one load through the export, offset +8).
5. Stop on any `MISMATCH` line or a crash in `ts3native.asi`; triage against the change ledger.

### Risks

- A bug in the wild-copy bounds would corrupt or overrun resource buffers. Mitigations: all bounds checks of the original kept, overrun limited to `min(cap, header size)`, selftest fuzzing with guard bytes, 3.7 GB real-data identity check, `Verify=1` for the first in-game runs.
- If a stream ever ends short of its header size (none in the shipped packages; possible in broken CC), the bytes between the real end and the header size may hold scratch instead of untouched heap garbage. The game reads that buffer as the resource either way.
- Another plugin that rewrites the call at `0x4EC065` (none known; S3MM maps `0x4EC010` but a detour at its entry still reaches our call) would make ours `SKIP`.
- The per-call `QueryPerformanceCounter` pair and interlocked counters cost roughly 50 ns per decode (about 1% of a 4 KB decode).

## Phase 7: save (v0.6.0, `[SaveWrite] Enabled=0` by default)

Offload candidate #2. Built and self-tested 2026-10-13, not run in game. Code: `native/ts3native/savewrite.cpp`. Static analysis: Ghidra decompile of the functions below plus `dumpbin` of `FUN_00AB1030` (0xAB1030-0xAB14AB) and `IC_LoadSaveManager_SaveGame_Impl` (0x742A50).

### Where a save goes (sim thread, start to end)

| # | Step | Code | Kind | Off the sim thread? |
|---|---|---|---|---|
| 1 | Pause, `ProgressDialog`, `ThumbnailHelper.UpdateDirtyLotThumbnails`, lot + household thumbnails | C# `OptionsModel.SaveGame` | rendering, live state | no |
| 2 | `LoadSaveManager.SaveGame` -> `LoadSaveManager_SaveGame_Impl` 0x742A50 -> `FUN_00AB1030` (thiscall, 3 args) | native | synchronous; C# waits for the error code | - |
| 3 | Save info and paths (`FUN_00AAE950` x2): working copy `<TS3 user folder>\CurrentGame.sims3`, target `Saves\<name>.sims3`; info+0x430 = `Saves\<name>.sims3\<world>.nhd` | 0xAB10DB, 0xAB10F6 | paths | - |
| 4 | Old single-file save renamed `.legacy` (`FUN_00AACE80`) | 0xAB112D | rare | - |
| 5 | Open the working-copy DBPF (`World/LoadSaveManager/DBPF`, vtable 0xFB2600, open = +0x18) and write the game into it: `FUN_00AAC110` writes the "S3" header, the save listeners (+0xC), the world (`FUN_00C6D460`: lots, objects, script state via the `ScriptCore.LoadSaveManager` callbacks `SaveObjectGroup` / `SaveObject` / `EndSaveObjects` into a native stream = OBJS), the routing world save (`FUN_00834FC0` under the routing lock), then post-save listeners (+0x14) | 0xAB12AF | **live game state + Mono** | **no** |
| 6 | Package commit and close: vtable +0x1C = `FUN_004A8500(1)` writes pending resources (`FUN_004A7910`, vtable +0x3C per record) and the index; +0x08 closes | 0xAB12C1, 0xAB12FD | DBPF write; compression per resource [inferred, compressor not located] | not without rebuilding the DBPF writer |
| 7 | Travel save: stop here (`cmp [esp+315Ch],0; jne`) | 0xAB1319 | - | - |
| 8 | If `<world>.nhd` exists in the target: **backup** `FUN_00AB0F80` -> `FUN_00AAFC50(Saves\<name>.sims3, Saves\<name>.sims3.backup)`: delete the old backup (up to 10 tries, `Sleep(500)` between), recursive `CopyFileW` of the old save (`FUN_004DD030`), `FlushFileBuffers` on every copied file (`FUN_00AAF4B0`) | 0xAB134E | **pure file I/O on the old save**, which nothing writes during a save | **yes (built)** |
| 9 | **Copy the new save**: `FUN_00AB0E80(name, 0)` -> `FUN_00AAFC50(CurrentGame.sims3, Saves\<name>.sims3)`: delete the old save folder (same retry loop), copy the working copy file by file, flush every file | 0xAB1363 | file I/O, needs step 6 done | only with the sim thread waiting (no gain) |
| 10 | Failure path: working copy to `<name>.sims3.bad`, `.legacy` restore | 0xAB1375-0xAB13FF | error handling | - |
| 11 | Write `1a5tf1l3.dat` in the save folder and flush every file in it again (`FUN_00AAF5F0`) | 0xAB1410 | file I/O | no gain |
| 12 | C#: `UIManager.SetSaveGameMetadata`, `DynamicChallengeManager.Save`, close the dialog, restore speed | C# | - | - |

[verified code] for the order, the calls and the copy/flush/retry behavior; [inferred] for the CurrentGame.sims3 location (same base as `Saves`, from the same `vtable+0x24` user-folder call; the folder is gone after the game exits) and for where resource compression happens.

Size of what is copied: `Baseline-BP` is 33.3 MB (31.9 MB `.nhd`, 0.5 MB ExportDB, 0.9 MB TravelDB; docs/09 §6). So every ordinary save copies about **33 MB twice** (old save to `.backup`, working copy to the save) and flushes about 66 MB, plus a second flush pass in step 11.

**No save-time numbers exist yet** (test E has never been run). The new hooks measure it: one log line per save with total, package write (step 5, includes the script serialization), backup, copy and final flush.

### What is offloaded (Mode=1)

Step 8 copies the **old** save, so it does not depend on the package write. At step 3 (after `FUN_00AAE950` returns, non-travel, `<world>.nhd` exists) a worker thread copies `Saves\<name>.sims3` to `<TS3 user folder>\ts3native-backup.tmp` (plain Win32: `CopyFileW` per file, `FlushFileBuffers` per file, like the game), while the sim thread runs step 5. At step 8 the sim thread waits for the worker (expected to be done by then), deletes the old `.backup` exactly as the game does (file: `DeleteFileW`; folder: up to 10 tries 500 ms apart) and renames the temp copy to `<name>.sims3.backup` (`MoveFileExW`, `MOVEFILE_WRITE_THROUGH`). The game's copy is skipped and the hook returns 1, which is what the game's step returns after a good copy.

Data safety:
- Nothing is reported earlier than before. Steps 9 and 11 (the new save, its flush) still run on the sim thread before the C# call returns; only the backup of the old save changes thread, and it is complete and flushed before the rename that makes it the backup.
- The temp copy is used only if (a) every copy and flush succeeded, (b) the old save's file names, sizes and write times hash the same at job start, after the copy and again at step 8, (c) the copy hashes the same as the source (`CopyFileW` keeps write times), (d) the backup step's name is the same folder (`<name>` or `<name>.sims3`). Any miss: the temp copy is deleted and the game's own `FUN_00AB0F80` runs, so the outcome is the game's.
- The old `.backup` is deleted only after (a)-(d) passed. A failed rename after that leaves no backup for a moment and then the game's own copy makes it, which is the game's own order (it also deletes before copying).
- A save that never reaches step 8 (write error, travel, cancelled) waits for the worker at the end of `FUN_00AB1030` and deletes the temp copy, so the old `.backup` is untouched, as in the game.
- Paths come from the save info only when they look exactly right (info+0x430 readable, under 260 chars, ends in `.nhd`, the file exists, parent ends in `.sims3`, grandparent in `\Saves`); else no job.
- The temp folder sits next to `Saves` (same volume, so the rename is a metadata change) and outside it, so the game never lists it as a save.

Hook sites (`call rel32` rewritten; every site must be an `E8` to the expected function or nothing is written): 0x742A95 (`FUN_00AB1030`, total), 0xAB10F6 (`FUN_00AAE950`, job start), 0xAB12AF (`FUN_00AAC110`, write time), 0xAB134E (`FUN_00AB0F80`, backup), 0xAB1363 (`FUN_00AB0E80`, copy time), 0xAB1410 (`FUN_00AAF5F0`, final time). Calling conventions checked in the disassembly (`ret 0Ch`, `ret 0Ch`, `ret 8`, `ret 4`, `ret 8`, `ret`).

Settings: `[SaveWrite] Enabled=0` (1 = hooks + timing), `Mode=1` (start value of the runtime switch; 0 = time only, the game backs up). Counters v6 at the end of `ts3native_counters`: `saveReady`, `saveMode` (writable), `saveJobs`, `savePromoted`, `saveFallbacks`, `saveCount`, `saveLastUs`, `saveLastWriteUs`, `saveLastBackupUs`, `saveLastCopyUs`, `saveLastFinalUs`.

Self-test (`TestSaveWrite`, real temp folders): call-site patch refuses a wrong target; worker copy promoted (backup = old save, older backup gone, no temp left, game's backup not called); `<name>.sims3` name accepted; old save changed during the write -> fallback, older backup kept until the game's step; wrong folder name -> fallback; save that stops before the backup -> temp removed, backup untouched; travel save, `Mode=0` and a new save start no job.

### Expected gain [inferred]

The whole backup step leaves the sim thread when the package write takes longer than the copy, which it should (it serializes every script object; docs/09: 188k-303k OBJS records). Copying and flushing 33 MB on this laptop's NVMe should be about 0.1-0.5 s, more with Defender scanning the new `.nhd` or on an HDD. The delete of the old backup (and its retry loop, up to 5 s when a file in it is locked) still runs on the sim thread at step 8, as in the game. The new save's own copy (step 9) and the double flush stay. The numbers decide whether this is worth keeping: compare `backup` between Mode 0 and 1.

### Not offloaded, and why

- Step 5 (package write incl. script state): walks live Mono objects and game state; must stay on the sim thread.
- Step 6 (resource compression and index): runs inside the DBPF writer interleaved with its file offsets; offloading needs a new writer. Next: locate the compressor (vtable +0x3C path) and time it with the step-5 number before deciding.
- Step 9 (copy the new save): needs the finished package and must be complete and flushed before success is reported; on a worker the sim thread would only wait.
- Step 11: small.

Next best options (not built): (1) make step 8 a rename of the old save instead of a copy (`<name>.sims3` -> `<name>.sims3.backup`; step 9 then finds no folder to delete). Same crash windows as the game (the game also deletes the save folder before copying the new one), no 33 MB copy at all, no worker. (2) Skip the second flush pass in step 11 for files step 9 just flushed. Both change the game's file operations, so they wait for the step timings.

### In-game test plan

Back up `Documents\Electronic Arts\The Sims 3\Saves` first (whole folder). Then, serially:
1. `[SaveWrite] Enabled=1, Mode=0`. Load `Baseline-BP`, `tests-be.ts` test E (save as `TestE-<tag>`): check `OK   save write: 6 call sites` and one `save:` line in `ts3native.log`; record total, write, backup, copy, final. Save the same save again once (now a backup exists): the backup number is the cost being moved.
2. `Mode=1` (ini or the runtime switch), same steps: `savePromoted` +1 per save, `saveFallbacks` 0, `backup` near 0 when `worker` < `write`; `TestE-<tag>.sims3.backup` exists and `save-stats.ts` on it gives the same counts as on the previous save; no `ts3native-backup.tmp` left in the TS3 user folder.
3. Reload the saved game: loads, household and Sims as before; `save-stats.ts` counts on the save identical to a Mode 0 save of the same state.
4. Load the `.backup` folder as a save (copy it to a new `.sims3` name): it loads.
5. Repeat 1-2 on `Baseline-IP` and `Baseline-SV`; autosave and Save-and-Quit once each in Mode 1.

## Proposed install entry for tools/setup/fixes.json (not applied)

```json
{
  "id": "ts3native",
  "name": "ts3native.asi (our native patches)",
  "author": "this project",
  "category": "engine",
  "version": "0.1.0",
  "source": { "kind": "local-build", "path": "native/ts3native/out", "build": "native\\ts3native\\build.cmd" },
  "install": { "target": "bin", "files": ["ts3native.asi", "ts3native.ini"] },
  "requires": ["asi-loader"],
  "default": false,
  "notes": "Phase 1: GC initial heap 256 MB, growth step 32 MB (docs/13, docs/15). Check Game\\Bin\\ts3native.log after the first start. Remove both files to roll back."
}
```

The `local-build` source kind does not exist in the installer yet; it needs a small branch that copies from `native/ts3native/out` instead of downloading.

## Risks

- **Address space.** The 256 MB initial heap is one contiguous reservation at Mono start, and Boehm never gives heap back. That is up to 216 MB less for DXVK, textures and the world cache (Error 12). Measure committed memory and the largest free block against the baseline. If `GC_expand_hp_inner` cannot get the block, the game exits at start-up with "Can't start up: not enough memory" (no save touched). **[code]**
- **32 MB growth steps** add up to 24 MB extra peak per expansion. **[inferred]**
- **Other plugins** patching the same bytes: whichever runs second sees different bytes and skips (ours logs it). S3SS 1.6.3 does not patch these sites as far as docs/03 §3 lists. **[inferred]**
- The budget unit is probably microseconds (docs/13 Q3). Raising the ceiling costs sim-loop time each frame. **[inferred]**

### Phase 4 in game (2026-10-13): route budget stubs strand Sims - keep OFF

- Functional check: 50 plans through the budget, game stable, unreachable plans fail fast (218 nodes, 2.5 ms) [verified].
- 10-min Bridgeport profile with `[RouteBudget] Enabled=1` (stubs + detour installed; budgetMode alternated 1/0 every 2 min): **41 stuck Sims** on all trip kinds (visits, work, school, Go Home), although only 1 search was capped (`capNodes 16`, `capUs 989189`) [verified, `probe-out/repro/Repro-bp-budget-1724.json`].
- Control, same save and mods, `Enabled=0` (no stubs, no detour), 5 min: **0 stuck** [verified, `probe-out/bp-nobudget.log`].
- So the stubs themselves break routing even with the switch off (suspects: cost/FPU state, stale per-request timer for A* calls outside Route_PlanLocked); code review in progress. `[RouteBudget] Enabled=0` stays the default.
- Phase 3 (route worker) crashed the game on first use: ACCESS_VIOLATION reading 0x58 at 0x00EA2B03 (`mono_class_vtable` with a NULL domain) from 0x00E5A668 (Mono interpreter runtime_invoke, not Boehm) on the worker thread: the planner calls script code (xcpt 26-10-13 01.11.31). Fixed in 0.4.1 by running those managed calls on the waiting caller thread (Phase 3, "Crash 2026-10-13 and fix"); not retested in game; `Enabled=0` stays the default.
- Code review (2026-10-13): the five cost stubs keep the cdecl contract, registers and x87 stack exactly (wall path pushes one ST value like the original; arg5 offsets right; sites are load-only) [verified], so they do not explain the stuck Sims. Real bugs to fix before any retest: (1) `CallPlan` starts the budget clock before `Route_PlanLocked` takes the routing lock, so lock waits count against MaxUs (explains the one cap at 16 nodes / 989 ms) - start the clock at the first node; (2) `g_budget` is one unprotected global - make it thread-local. Next suspect for the 41 stuck Sims: the `Route_PlanLocked` detour itself; test it alone (`[RouteWorker] Enabled=1, Mode=0`, nothing else) for 10 min.
- **Fixed in 0.4.2** (built, self-test ok, not run in game; defaults unchanged, `[RouteBudget] Enabled=0`, `[RouteWorker] Enabled=0`): (1) `BudgetStep` starts the clock at the first node (`if (b.nodes++ == 0) QueryPerformanceCounter(&b.t0)`); `CallPlan` keeps its own request start for the counters. (2) `g_budget` is now `t_budget`, `__declspec(thread)` (implicit TLS in a `LoadLibrary`'d DLL works on Vista+, threads that already exist included). On the worker, `ServePlan` -> `CallPlan` arms the worker's copy; `CostGate` runs on the worker and reads the same copy. The `tid == me` guard is gone: a nested plan on the same thread saves the outer budget, runs with a fresh one, and restores it. Neither fix explains 41 stuck Sims with the switch off; the detour test above is still next.

## Phase 8: per-thread cache in front of EA's arena allocator (v0.8.0, `[Alloc] Enabled=0` by default)

Built and self-tested 2026-10-13, not run in game. Item 2 of docs/20 (the biggest native cost that is not route search: `0x4E5E40` is 6.9% of pooled sim-thread samples, 9.9% in Bridgeport, up to 16% incl. lock waits in `bp-detour2`/`bp-worker1`). Source `native/ts3native/alloc.cpp` (included by `ts3native.cpp`). RE from Ghidra headless decompiles of `0x4E4800`-`0x4E7000`, xrefs and the default allocator's vtable.

### What the allocator is [verified: decompile]

- EA's GeneralAllocator, a close derivative of **dlmalloc 2.7** (Doug Lea): heap objects of 0x518 bytes (a debug variant adds a table up to +0x6A0). Fields: `+0` initialized, `+4` `max_fast` with bit 0 = "fastbins hold chunks", `+8..+0x24` fastbins, `+0x28` bins (unsorted bin = `+0x30`, fd `+0x38`, bk `+0x3C`), `+0x430` binmap, `+0x440` top, `+0x444` last remainder, `+0x448` list of VirtualAlloc'd cores, `+0x488/+0x48C` count/bytes of direct-mapped blocks, `+0x4AC/+0x4B0` out-of-memory callback, `+0x4E4` lock (pointer to a `CRITICAL_SECTION` + recursion count at +0x18, spin count 10, `0x4E48C0`).
- Chunks: 16-byte header `{prev_size, size|flags, fd, bk}`, user pointer = chunk + 0x10, sizes multiples of 16 (`request2size` `0x4E4770`: `(n + 0x1B) & ~15`, min 16). Flags in the size word: bit 0 = previous chunk in use, bit 1 = direct `VirtualAlloc` block, bit 2 and bit 31 = in a fastbin (bit 2 also tags debug chunks). A chunk is in use when the **next** chunk's bit 0 is set (dlmalloc). Free chunks link through fd/bk at +8/+0xC (inside the header), so user bytes are not touched by a free except the last 4 (next chunk's prev_size) when it coalesces.
- Entry points, each "lock, core, unlock": `0x4E69D0` Malloc(size, flags) -> `0x4E5E40`; `0x4E6A20` Memalign(size, align, offset, flags) -> `0x4E63F0` (which calls `0x4E5E40(size, flags)` unchanged when align <= 16 and offset 0); `0x4E6A70` Realloc -> `0x4E6710`; `0x4E5920` Free -> `0x4E51B0`; `0x4E5CB0` Shutdown (also through the jmp thunk `0x4E69C0`). Flag bit 3 sends a request straight to `VirtualAlloc` (`0x4E50F0`). No MSVCR80 import anywhere in `0x4E4800`-`0x4E63E6`, so S3SS's mimalloc (MSVCR80 `malloc`/`free`) never sees these heaps (docs/17 §8.3).
- Heaps: the default allocator (`0x931020`, ICoreAllocator vtable `0xFB8B38`: slot +4 `0x4E3E90` aligned Alloc -> `0x4E6A20`, slot +8 `0x4E3E40` Alloc -> `0x4E69D0`, slot +0xC `0x4E3EF0` Free -> `0x4E5920`) on the shared "MemoryMan" heap (`DAT_011CB864`, shared block id `0xEE9B7413`); `"Services/MemoryMan/MemoryManHeap"` (`0x4E40C0`), `"Audio/Allocator"` (`0x89F4E0`, own heap with an OOM callback), SpeedTree (`0x7AF190`-`0x7BC0D0`, the densest direct callers, own byte counter at `0x11D8628`). Direct call sites: 179 Malloc, 60 Memalign, 7 Realloc, 307 Free, plus everything behind the ICoreAllocator vtable. Threads: any; the callers are frameless, so the sampler cannot name them (docs/20).

### Why it is slow

- **One lock per heap, shared by every engine thread** [verified: code]; lock waits measured at 1.0% of sim-thread samples pooled and 3.4% in `bp-worker1` (docs/20) [measured]. The route worker and the sim thread now allocate from the same heap at the same time, which fits the higher wait with the worker on [inferred].
- **Fastbins stop at 64-byte chunks** (`max_fast = 0x40`, set in `0x4E5710`) [verified], i.e. requests up to 52 bytes. Everything from 53 bytes up goes through the general path: each Malloc first sorts **every** chunk in the unsorted bin into its bin (loop `LAB_004e5f41`, with a linear walk to keep large bins sorted), then searches bins and the binmap [verified: code]. Every non-fast Free coalesces and pushes onto the unsorted bin, so a burst of frees is paid for by the next Malloc [verified: code].
- **Any request >= 512 bytes while fastbins hold chunks runs `malloc_consolidate` `0x4E5960`** over all fastbins (coalesce each chunk, push to unsorted), and the next unsorted loop sorts them all again [verified: code]. Interleaved small frees and >= 512-byte allocations (strings, vectors, route buffers) pay this again and again [inferred: the profiles attribute samples to `0x4E5E40` itself, and we have no split between the loops].
- Frees that leave a free block >= 64 KB check whether the top of a core can be decommitted (`0x4E4B30` -> `0x4E4F90`, `VirtualFree(MEM_DECOMMIT)`), recommitted later [verified: code; cost in game not measured]. No page commits on the normal Malloc path.
- Not a linear free-list search in the fast case: small bins are exact-size and O(1); the cost is the sorting and consolidation work plus the lock [verified: code].

### What we built

A thread-local cache of freed chunks in front of the locked entry points (tcache-like): **Free** of an in-use, not direct-mapped, not fastbin/debug-tagged chunk of 16..1024 bytes pushes it onto this thread's bin for (heap, chunk size); **Malloc** and **Memalign with align <= 16, offset 0** of the same chunk size pop it. No lock, no `0x4E5E40`, no unsorted sort, and fewer chunks in the fastbins, so fewer consolidations.

Semantics kept [verified: code + selftest]:
- A cached chunk stays in use from the heap's point of view: its header, the next chunk's in-use bit and every user byte stay as Free got them. The link lives at `p-4` (the chunk's `bk`, the word the fastbin uses too). Realloc, memalign, the debug layer, heap walks and coalescing of neighbours all see a live allocation.
- Only exact chunk sizes are reused, and `0x4E5E40` itself returns exact sizes (remainders of 16+ bytes are always split), so the usable size the caller gets is the same as the game would give.
- Alignment: every user pointer is 16-aligned; memalign requests with a larger alignment or an offset (ZoneObject: align 4, offset 16) go to the game.
- Flag bit 3 (direct `VirtualAlloc`) and sizes above 1 KB go to the game. A Free that fails the in-use check (e.g. a double free) goes to the game exactly as before.
- Heap shutdown: the `0x4E5CB0` detour records the heap in a 16-entry ring and bumps a generation; each thread, on its next call, forgets chunks of a shut-down heap (never freed into freed memory) and hands the others back. More than 16 shutdowns between two calls of one thread: it forgets everything (bounded leak).
- Thread exit: an FLS callback (where the CRT frees its per-thread data) hands the thread's chunks back through the game's Free and releases its quota.
- Out of memory: if the game's Malloc/Memalign returns NULL, this thread's chunks of that heap go back and the call is retried once.
- **Address space:** nothing is reserved. Cached chunks are already-committed heap memory: at most `ThreadKB` (64 KB) per thread, `PerBin` (32) per size, and `MaxMB / ThreadKB` threads (4 MB / 64 KB = 64 threads) get a cache at all; the rest run uncached. Per-thread bookkeeping is 2.1 KB from the process heap. The engine has no allocator budget counter for these heaps on the Malloc path (only direct-mapped bytes at `+0x48C`) [verified], so no accounting changes; heap reports count cached chunks as used.

Settings `[Alloc]`: `Enabled=0`, `Mode=1` (start value of the runtime switch `ts3native_counters.allocMode`, offset +0x108), `ThreadKB=64`, `MaxMB=4`, `PerBin=32`. Install is all four detours or none (prologue bytes of `0x4E69D0`, `0x4E6A20`, `0x4E5920` are `56 57 8B F9 8B B7 E4 04 00 00`, `0x4E5CB0` `55 8B EC 83 EC 14`; no jumps into them). Counters v9 (+0x104): `allocReady, allocMode, allocHitsK, allocMissesK, allocCachedK, allocPassedK` (units of 1024, published every 1024 calls per thread), `allocThreads, allocFlushed, allocDropped, allocShutdowns, allocOomRetries`; one `alloc cache:` log line per minute; probe `nativeCounters` reads them.

### Offline results (2026-10-13, i7-11800H, 32-bit /O2 build) [measured]

Selftest against a fake heap that lays chunks out like the game's: hit/miss per size and heap, memalign rules, every pass-through case, per-bin and per-thread caps, mode 0, shutdown (drop vs hand back), OOM retry, thread exit, quota refusal. A cache hit costs **~16 ns per free+malloc pair** (incl. `FlsGetValue`), against **~50-57 ns for an uncontended `CRITICAL_SECTION` around a trivial bump allocator**, i.e. before any of `0x4E5E40`'s own work. The game's own code cannot run outside the game, so there is no offline number for the real path.

### Expected gain in game [inferred]

Hit rate decides it and is unknown: game code tends to free and reallocate the same few sizes, so 60-90% hits on the sim thread is plausible. At that rate most of the allocator's 6.9-16% of sim-thread time and its lock waits go away: roughly **4-10% of sim-thread time**, more on the busy BP worker runs. Realloc (7 sites), aligned/offset memalign, > 1 KB requests and threads without a quota keep the old cost.

### How to test in game

1. Build, `install --only native-patches`, `[Alloc] Enabled=1`. Log must show `OK   alloc cache: mode 1, ...` and four `OK   detour EA heap ...` lines.
2. Bundle run in Bridgeport (`bp-route-cache`, 5 min): no crash, stuck count as baseline, `alloc cache:` line with `hits` well above `misses`, `oom 0`, `threads` well under the max.
3. A/B in one load: flip `allocMode` (+0x108 of `ts3native_counters`) 0/1 with `/simstack` on, compare the `0x4E5E40` + `0x4E69D0`/`0x4E6A20` share and lock-wait frames.
4. Address space: M3 largest free block at the save vs the same run with `Enabled=0` (expect at most a few MB difference). `store-200` load to make sure the busy load phase is fine.
5. Stop on any crash in `ts3native.asi` or a heap corruption crash in `0x4E5E40`/`0x4E51B0`; triage against the change ledger.

### Risks

- **Game memory bugs change shape:** a double free of a cacheable chunk is no longer caught by anything (the game's own fastbin did not catch it either) and can hand one chunk to two owners; a use-after-free still reads the same bytes (we write only the header word). A pointer freed through the wrong heap was already corruption; now it is reused by the wrong heap's Malloc instead.
- A heap shut down while another thread is still inside Malloc/Free of it was already a game race; our ring only covers shutdowns seen before the thread's next call.
- Up to `MaxMB` of free memory sits in caches; an out-of-memory retry flushes only the calling thread, so Error 12 could come slightly earlier on a process that is already at the edge (bounded by 4 MB).
- Thread-exit flush takes the heap lock inside the FLS callback (as the CRT does); a deadlock needs a lock holder that waits on that thread's exit.
- More than 4 heaps on one thread: the 5th is not cached (`allocPassedK`). Only `0x4E69D0`, `0x4E6A20` and `0x4E5920` are covered; code that calls `0x4E5E40`/`0x4E51B0` directly (debug layer, realloc internals) is unaffected and stays correct because cached chunks look allocated.
- S3PP `AllocatorArenaTuning` patches immediates in `0x4E48F0`/`0x4E5AA0` (core sizes), not these entries: compatible [verified sites]. Another mod detouring these entry points makes ours `SKIP` all four.

## Phase 9: faster water-grid jump-point search (v0.9.0, `[WaterJps] Enabled=0` by default)

Built and tested offline against the game's own code 2026-10-13, not run in game. Item 1b of docs/20: the water JPS `0x620560`/`0x61FE10` is 7.8% of Isla Paradiso sim-thread samples (0% in BP/SV). Source `native/ts3native/waterjps.cpp` (included by `ts3native.cpp`), offline test `waterjps-test.cpp`.

### What the code does [verified: decompile + disassembly]

- Caller chain: `0x65E0D0` -> `0x671930` ("PathPlan/path", "Routing/waterPath") -> `0x620560(g1, g2, w, h, costScale, &start, &goal, &outPath, cb)`. `g1` and `g2` are EASTL `vector<u8>` grids picked from the routing world (`+0x7B0 + 16*i` by boat/swim type, `+0x7C0 + 16*j`), `w`/`h` at world `+0x7A0`/`+0x7A4`. `g1 != 0` = water the route may use; `g2 != 0` = a cost layer: such cells are always jump points, and an edge touching one costs `costScale` (`DAT_00FBB870` = 40.0) per cell instead of `DAT_0107A538` = 1.0.
- `0x620560` is a plain JPS A*: a binary-heap open list ("PathPlan/tOpenListContainer", EASTL push/pop heap `0x61F830`/`0x6EB480`), a hash map cell -> {g, parent} ("PathPlan/extraData"), a `w*h` byte closed set allocated and zeroed per call ("PathPlan/closed"), octile heuristic `0x620120`, Euclidean/Manhattan edge cost `0x61FFA0`. For each popped node it computes the parent direction (`0x61F7E0`), the natural plus forced neighbours (`0x61FA10`), and calls the jump `0x61FE10` once per direction.
- The jump `0x61FE10` (cdecl, 9 args, plain `ret`): step one cell (`0x61F750`, index arithmetic: `cell - w`, `+1`, `+w`, ... so a step east off the last column lands on column 0 of the next row), `n / w` and `n % w` (two `div`s), bounds check, then `n == goal`, forced test `0x61FA10`, `g2[n]` (`0x61F9E0`); a diagonal step first runs both straight jumps (recursive calls), and then **recurses for the next cell**. So one stack frame per cell scanned. A flag argument (start cell not water) makes it return the first step without scanning.
- The forced-neighbour test `0x61FA10` first calls the route callback's vtable slot 1 (`0x66DC50`: is the cell within 3 cells of the start or the goal, Chebyshev), then up to four `0x61F890` (neighbour coordinate, a switch) + `0x61F9A0` (open = `g1 && !g2`, coordinate bounds check, no row wrap) pairs and three `g2` neighbour reads.

### Why it is slow [measured: IP samples]

Of the 367 Isla Paradiso sim-thread samples with the JPS on the stack, the leaf is the jump `0x61FE10` 105, `0x61F9A0` 86, `0x61FA10` 85, `0x61F890` 34, `0x61F9E0` 11, `0x61F750` 8, callback `0x66DCxx` 7: **~92% is the per-cell chain** (about ten calls, two divisions and an indirect call per cell). Open list, hash map and closed-set allocation are a few samples. Not a missing pruning or a repeated grid scan: the algorithm is ordinary JPS; the per-cell constant is the cost.

### What we built

`WaterJump`: the same jump as one loop, every helper inlined, no division per cell (x, y tracked next to the index, with the same row wrap), interior cells tested with plain index offsets and no bounds checks, the game's callback `0x66DC50` inlined (any other callback is still called once per forced test through its vtable). Straight scans are a loop; a diagonal step runs the two straight scans and continues; the stack use is fixed. Results are identical by construction and by test, including the row-wrap quirk.

Hook: only the search's own call at `0x6207FB` (`E8` to `0x61FE10`) is retargeted to `WaterJumpHook`; the game's function is untouched and stays the mode-0 path. Install checks the call bytes and an FNV-1a of `0x61F750..0x61FF93` (every function the jump runs, `0x5A2C1E2B`) and of `0x66DC50` (`0x585FD97D`); a mismatch skips the patch (or, for the callback only, calls it through the vtable). Passed to the game's jump: mode 0, `dir` byte > 7, `w < 2` (a diagonal step is +0 there and the game recurses until the stack overflows), `w * (h + 2) >= 2^31`, `cell >= w*h`.

Not done: a result cache or JPS+ jump tables. A jump depends on the goal and on the callback's start/goal boxes, so (cell, dir) results do not carry over between searches, and precomputed jump distances per grid would need an invalidation point for `+0x7B0`/`+0x7C0` (houseboats, terrain edits) that we have not found [inferred].

Settings `[WaterJps]`: `Enabled=0`, `Mode=1` (runtime switch `ts3native_counters.waterJpsMode`, +0x134), `Verify=0` (+0x138: mode 1 also runs the game's jump, counts and logs mismatches, uses the game's result). Counters v10 at +0x130: `waterJpsReady`, `waterJpsMode`, `waterJpsVerify`, `waterJpsCallsK`, `waterJpsCellsK`, `waterJpsMismatches`, `waterJpsFallbacks`; `agent.js` reads them for `version >= 10`.

### Offline results (2026-10-13, i7-11800H, 32-bit /O2 build) [measured]

`out\waterjps-test.exe ["<path>\TS3W.exe"] [grids]` copies `0x61F000..0x621000` and `0x66D000..0x66E000` from the exe (read-only) to the same addresses in the test process (the code only calls itself; its switch tables are in those pages) and runs the game's jump and `WaterJump` on the same inputs: random grids 2..161 x 1..160 with islands, open sea, sparse and patchy `g2`, every start cell (a sample on big grids) x 8 directions x flag 0/1 with junk in the flag's upper bytes, random or no goal, and three callbacks (none, the game's `0x66DC50`, another one through the vtable whose calls are counted). **1000 grids: 43.9M jumps, identical results and identical callback call counts.** Timing on a 512 x 512 sea with 300 islands and 0.1% `g2`, 400k jumps from random water cells (138 cells per jump on average), best of 3: **game 2.21 s, WaterJump 0.56 s, 3.9x**.

Found on the way: on a grid 1 cell wide the game's jump recurses forever (stack overflow), and on large open grids its recursion depth is the number of cells scanned (a row-wrapping scan can go `w*h` deep); the test runs on a 256 MB stack for that reason. Whether the game's water grids are ever wide open to the border is not known [inferred].

`selftest.exe` adds hand-checked jumps on tiny grids (goal, cost-layer neighbour, flag, the row-wrap case) and the mode-0/fallback wiring.

### Expected gain in game [inferred]

~92% of the JPS time is the per-cell chain and it runs 3.9x faster offline, so the JPS share drops by about 0.92 x (1 - 1/3.9) = 68%: in Isla Paradiso about **5 points of the 7.8%, ~50 ms per second of sim-thread time** (40-55 ms/s if the real grids behave differently from the synthetic one). Long boat/swim searches get shorter in the same proportion, which also helps the async route plan later (a shorter job). BP and SV have no water JPS samples: no change there.

### How to test in game

1. Build, `install --only native-patches`, `[WaterJps] Enabled=1 Verify=1`. Log must show `OK   water jps: call 0x006207FB -> WaterJumpHook, mode 1 verify 1, callback 0x0066DC50 inlined`.
2. Isla Paradiso bundle run with boats and swimming in it (route repro actions for a boat trip and a swim): `waterJpsCallsK` grows, `waterJpsMismatches 0`, `waterJpsFallbacks 0`, no `MISMATCH` lines.
3. Then `Verify=0` and an A/B in one load: flip `waterJpsMode` (+0x134) 0/1 with `/simstack` on and compare the `0x61FE10`/`0x61FA10`/`0x61F9A0` family share (docs/20 method).
4. Stop on any crash in `ts3native.asi` or in `0x620560`; triage against the change ledger.

### Risks

- A wrong jump point would change boat/swim routes (not memory safety: the loop only reads the two grids inside `w*h` and the callback's 16 bytes). Mitigated by the identical-result test on the game's own code and by `Verify=1` for the first runs.
- Another mod patching the jump's code or the call at `0x6207FB` makes ours `SKIP`.
- The counters use two interlocked adds per jump (~10-20 ns against ~1.4 us per jump).
