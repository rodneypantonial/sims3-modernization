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

Self-test (`selftest.cpp`, `TestRouteWorker`): mode 0 runs on the caller; mode 1 runs on the worker thread with the caller's x87 control word (24-bit precision set on purpose) and returns each call's own result; a plan started on the worker falls back to a direct call; `doPlan = 0` stays on the caller; with the cache on, a no-path failure found on the worker is stored and the repeat is a hit that never reaches the worker.

Probe for the same step: `GET /routelock?start=1` (opt-in, `stop=1` unhooks) hooks `Route_PlanLocked`, `Lock_Acquire 0x4E16F0` / `Lock_Release 0x4E17B0` filtered on `world+0x918`, and `0x83CED0` (iteration count). It reports acquisitions, wait and hold per thread, plan length percentiles, and the overlap **O**: for each plan, `min(plan length, gap until the sim thread next acquires the routing lock)`. `Lock_Acquire` is engine-wide, so every acquire enters the Frida script; `acquireHooksPerSec` shows that cost. Measure O with `workerMode = 0`.

Unverified: the planner has no thread affinity (TLS, profiler scopes, asserts); the calling thread never already holds `world+0x918` when it calls `Route_PlanLocked` (if it did, the worker would deadlock on the lock, and the watchdog would show it); an access violation inside the search on the worker is not caught by any handler the sim thread may have.

## Phase 4: route search budget (v0.4.0, `[RouteBudget] Enabled=0` by default)

docs/11 Q3 / docs/18 rank 2: one route request can no longer search without limit. Built and self-tested 2026-10-13; not run in game yet.

**Hook** [verified in `astar-676260.asm` and the bytes of `game-copy/bin/TS3W.exe`]: the world A\* `0x676260` loads its cost function into `[esp+0x60]` with one of five `mov dword [esp+0x60], imm32` (`C7 44 24 60 imm32`) at `0x676340/4A/54/5E/68` (travel modes 1-4 → `0x674E40`, `0x673850`, `0x674250`, `0x675800`; default `0x6734E0`). It calls it as cdecl with 8 args, float in ST0. Arg 5 is 1 for an edge cost, which the caller compares against the 1e8 wall (`0x00FEF678`) at `0x676683`, `0x6768A6` and `0x676B24`; it is 0 for the heuristic, which runs once per generated node right before the node is allocated and pushed (plus once per search for the start). The patch swaps the five immediates for stubs (`push orig; jmp CostGate`). This needs no code cave at the back-edge and no open-list drain (the docs/11 Q3 plan).

**How the cap ends a search**:
- The budget is armed per `Route_PlanLocked` request (`CallPlan`, on the thread that runs the original: the caller's or the worker's). It counts heuristic calls (= nodes generated) across every A\* run in the request, its retries included.
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

**Fail safe**: all five sites are verified before any write (a mismatch logs `SKIP route budget` and nothing is patched). With no budget armed on the current thread, a stub passes straight through to the original (one call and a thread-id check per cost call). A search not started from `Route_PlanLocked` is never capped.

Self-test (`TestRouteBudget`): one changed site → nothing written; switch off → passes through; cap above the search → same result and counters; cap of 300 → the next edge is a wall and the fake A\* fails through its own exit with 300 expansions; a search outside a request is untouched; `OnlyFlagged`; time cap.

**Test plan** (serial, saves backed up):
1. `Enabled=1, Mode=1, MaxUs=2000000`. Check `OK route budget` in the log and `budgetReady = 1` in `/native`. Play 10 min on `Baseline-IP`, then read `budgetMaxNodesOk` / `budgetMaxUsOk` (calibration) and confirm `budgetCapped = 0`.
2. Repro `sim_route_to` to an unreachable point (closed room or roof) with `MaxNodes` set below the measured failure size: `budgetCapped` +1, the Sim fails as before (one route-fail reaction) and stays controllable; a reachable walk right after succeeds. Then a long legitimate trip (IP cross-water, `bp-route-cache` repro) with the default cap: it must still succeed (`budgetCapped` unchanged).
3. `profile-world.ts --ab routebudget` on `Baseline-IP` and `Baseline-BP`: route ms/s, hitches ≥ 50 ms, fail % and stuck Sims, on vs off.

**Risks** [inferred]: a reachable destination that needs more than the cap now fails, and RouteBackoff then blocks that unseen townie from it for 30 sim-minutes (a long trip for a played Sim fails instead of being slow). A time cap depends on load: a slow machine or a descheduled thread fails more routes. The capped search still pops every queued node, which is cheap but not free. Lot searches stay unbounded.

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
