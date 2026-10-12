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

`ts3native.ini` `[RouteCache]`: `Enabled=0` (no detour at all when 0), `WindowMs=2000` (100–60000), `MaxEntries=4096` (256–65536). When the cache is on, `ts3native.log` stays open and gets `route cache: calls N hits N misses N stored N otherFails N` about every 60 s, written by whichever game thread calls the planner first after the minute is up.

Export for test tools: `ts3native_counters`, a data export of 8 int32:

| Offset | Field |
|---|---|
| +0x00 | version (1) |
| +0x04 | enabled (1 when the detour is installed) |
| +0x08 | calls |
| +0x0C | hits |
| +0x10 | misses (keyed calls that ran the original) |
| +0x14 | stored |
| +0x18 | otherFails |
| +0x1C | windowMs |

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
