# Reverse-engineering findings: stutter

Written 2026-10-11 against the Steam build of TS3W.exe (SHA-256 `10b2423e…0609`, PE timestamp 2014-01-21, LAA set, no ASLR, image base `0x400000`).

The focus is **in-game stutter**, not load times. See `03-prior-art-re.md` for community work and `04a-script-layer-stutter.md` for the C# side.

Confidence tags: **[verified]** means read in the decompiled code or the bytes. **[inferred]** means reasoned from code but not yet confirmed by measurement. **[to measure]** means it needs a run of the game.

---

## How this was done (reproducible)

| Step | Tool / script | Output (all under the git-ignored `game-copy/`) |
|---|---|---|
| Copy game files | `cp` from `D:\SteamLibrary\…\Game\Bin` | `bin/`, `packages/` |
| Extract script DLLs from `.package` files (DBPF, RefPack, S3SA decryption) | `node tools/extract-assemblies.ts` | `assemblies/` (10 DLLs) |
| Decompile C# | `ilspycmd -p` | `decompiled/` (11,071 files) |
| Ghidra import + analysis (16 GB heap; X86Analyzer and StackVariableAnalyzer still hit `OutOfMemoryError`, so some functions are missing) | `analyzeHeadless` | `ghidra/TS3.gpr` |
| Name the 1,757 native functions bound to script `DllImport`s | `node tools/internal-calls-map.ts` → `analysis/internal-calls-map.csv`, then `tools/ghidra/ApplyInternalCallNames.java` | functions named `IC_<Class>_<Method>` |
| Decompile, string xrefs, call paths | `tools/ghidra/run-headless.sh DecompileTo.java / StringXrefs.java / CallPaths.java …` | `analysis/decomp/*.c`, `analysis/*.txt` |

**How script calls reach the engine.** Script methods are `[DllImport("Sims3Common.dll")]` stubs; no such DLL ships. TS3W.exe resolves them by name through `.data` tables of `{void* fn, char* name}` pairs, with each run ended by a null pair (for example `"ScriptCore.Route::Route_Plan"` → `0x765160`). Proof: in 41 of 42 runs a code pointer sits before the first name, and no run has one after its last name. [verified]

> **Correction (2026-10-11):** the first version of this document read the pairs as `{name, fn}`, which gave every name the *next* entry's function (spotted by the harness research in `06`). The names and addresses below are corrected, and F1's conclusion was re-checked under the correct names: it still holds.

---

## F1. Route planning is synchronous on the sim thread, under a global lock [verified chain, inferred thread]

Call chain from script to the A* search, read in Ghidra:

```
IC_Route_Plan 0x765160 / IC_Route_Replan 0x765490            -> 0x7641d0
IC_Route_PlanFromPoint 0x765240 / IC_Route_ReplanFromPosition 0x765550 -> 0x7642c0 ("Routing/RoutingHost/startPoints")
  -> 0x7606e0
  -> 0x82c6e0: lock(0x1021cf0) ; setup 0x819d50 ; SEARCH 0x81b2a0 ; 0x819ae0 ; read result ; unlock
                (lock = 0x4e16f0: EnterCriticalSection / WaitForSingleObject wrapper; unlock = 0x4e17b0)
  -> 0x81b2a0 path search (uses "Routing/RoutingJob/startPts|goalPts", "Routing/RoutingPathImp")
```

- No `JobManager` / `JobThread%i` call appears in this chain. The planner runs to completion inside the script call and returns the result. [verified]
- Scripts are single-threaded cooperative tasks (see 04a), so every `Route.Plan` stalls **all** script simulation until the search ends. [inferred]
- `IC_World_FindPlaceOnRoadOffScreen` reaches the same search. `Route_IsPointRoutable` (`0x7538c0`) does not. [verified]
- Open question: which other threads take lock `0x1021cf0` (contention)? To check with `StringXrefs`/xrefs on the lock.

**Why this is the stuck-sim stutter.** Several script paths retry routing without real backoff (04a): NPCs that fail autonomy re-queue immediately (`Sim.CanRunAutonomyImmediately` returns `!IsSelectable` after a failure, `Sim.cs:56248`) [verified]. `GoHome` makes up to 3 world-scale plans per attempt. A failed route target is blocked for only one sim minute. Waiting sims replan every 2–3 cycles for up to 50 sim minutes. On a world with bad routing data, failed searches are also the most expensive ones, because A* explores the whole reachable area before giving up. [inferred]

## F2. The simulation loop: GC runs every frame, with an adaptive ~1 ms budget [verified]

`MonoScriptHost::Simulate` is at `0xD81840` (Ghidra's analysis missed it; it is created on demand). Each loop iteration:

1. Runs the script tick (`0xD80BC0`).
2. Calls `GC_try_to_collect(stop_fn = 0xD717B0)` (`0xE4A050`, call site `0xD819AA`). The stop function aborts the collection when the elapsed timer exceeds `DAT_011922a4`.
3. Adjusts the budget `DAT_011922a4` by ±100 per frame, within the range `[DAT_011923a4 = 500, DAT_01192324 = 1500]`, starting at 1000. The units are probably µs.
4. Runs `mono_gc_invoke_finalizers` (`0xE697F0`). If 50 or more finalizers are pending (`DAT_01194848`) for 200 frames in a row, it **drains the whole queue in one frame**.
5. Catch-up: when the sim falls behind, it runs up to **10 sim steps in one frame**. A backlog above 19 is reset (`param_1+0xc14`).

**Why it matters [inferred, to measure].** Boehm's `GC_try_to_collect` is not incremental here. An aborted collection throws its mark work away, so a 1.5 ms cap wastes up to 1.5 ms per frame without ever finishing on a big heap. The heap then grows until an allocation forces a full, non-abortable, stop-the-world collection. On a heavily modded save with a large heap, that shows up as a periodic big hitch. The finalizer drain and the 10-steps catch-up are two more sources of single-frame spikes.

## F3. Boehm can log its own pauses [verified strings, to measure]

The exe contains `GC_PRINT_STATS`, `gc.log`, `GC_INITIAL_HEAP_SIZE`, `GC_MAXIMUM_HEAP_SIZE`, `GC_ENABLE_INCREMENTAL` and `GC_PAUSE_TIME_TARGET`. With `GC_PRINT_STATS=1` set for TS3W.exe, Boehm should write every collection and its duration to `gc.log` (likely in `Game\Bin`). This is the cheapest first measurement: it needs no patch.

## F4. The engine's own budget and profiling hooks [verified names, meaning inferred]

- `Sims3.ini` `[Timeslices]`: `SimService`, `JobManager`, `ResourceSystem` and `TextureCompositor` all = `10,10,25,50`. The `JobManager` and `SimService` code references these names (`0x5992d0`, `0x5ebc50`).
- Script-visible profiler hooks exist: `ProfilerUtils_BeginEvent/EndEvent/Marker/Counter`, `Simulator_EnableYieldAnalysis`, and `StopWatch_*`. Whether release builds strip these is still to check. If they are live, they are free instrumentation points.
- `GameUtils_IsSingleThreadedMode` (`0xEC1500`) compares two thread-related values; its meaning is not decoded yet. `Sims3.ini` has `[Script] Multithreaded=1`, which suggests the sim already runs on its own thread, separate from rendering. [inferred]

---

## Big wins, ranked (for stutter)

Rank = expected stutter gain × confidence ÷ effort. "Not done by" lists S3SS, the Performance Patch and NRaas.

| # | Win | Mechanism | Not done by | How | Effort | Confidence |
|---|---|---|---|---|---|---|
| 1 | **Route-failure storm control** | Stop repeated synchronous A* searches that are bound to fail (F1) | S3SS (none), NRaas (only resets after >10 fails / 30 min) | Native hook on `0x82c6e0` or the `IC_Route_*` entries: a negative cache keyed by (router object, goal set, world routing epoch) that returns "fail" at once for N sim-minutes. Invalidate on build/buy, `RouteManager_MarkRoomForUpdate`, portal or footprint changes. No core-mod conflicts with NRaas. | M | High that it removes repeat cost; size of the gain is [to measure] |
| 2 | **GC pause shape** | Avoid aborted-mark churn and the later forced full collection (F2) | S3SS Chunky (skips the GC entirely, so memory grows), Finalizer Throttle | First measure with `gc.log` (F3). Then try, one at a time: `GC_INITIAL_HEAP_SIZE` (fewer collections), a different budget window (`0x11923a4`/`0x1192324`), and collecting only when the heap grew by X since the last full collection. | S to M | Medium |
| 3 | **Per-search time budget and yield** | Bound the worst case of one A* search | none | Hook the search: past a node or time budget, return "fail, retry later" (the script already handles route failure). Combine with #1. | M | Medium |
| 4 | **Spike smoothing in the loop** | Finalizer drain-all after 200 frames; up to 10 catch-up sim steps per frame (F2) | S3SS Finalizer Throttle (partly) | Spread the finalizer drain over several frames; cap catch-up at 2–3 steps. Byte patches in `0xD81840`. | S | Medium [to measure] |
| 5 | **Script-side retry backoff** | NPC "can't go home" and autonomy immediate-retry loops (04a) | NRaas Overwatch (reset only) | A script core mod changes `Sims3GameplaySystems`, which conflicts with other core mods. Prefer #1 (native) and keep this as plan B. | M | High on the mechanism |
| 6 | **Bad-routing worlds** | World routing data (`0x05CD4BB3`), terrain paint, intersections | ellacharmed's per-world fixes (new games only) | Data fixes per world; a tool to detect unroutable areas from repeated failures logged by #1. | L | Community |
| 7 | **Rendering stutter** | Shader compile hitches, D3D9 driver overhead | DXVK | DXVK (baseline layer 3), with its state cache warmed. Already planned. | S | High |

## Measurements that will decide the order

1. **`gc.log`**: set the environment variable `GC_PRINT_STATS=1` for one session. Count collections per minute and the longest pause, on the heavy save.
2. **Route profiler (Frida)**: time every call to `0x82c6e0` and count successes and failures per sim. Write a per-second histogram. Correlate with frame-time spikes.
3. **Frame times**: PresentMon or CapFrameX for 5 minutes at speed 3 on the heavy save. Count frames over 50 ms. This is what our patches must reduce.
4. **Tick-loop timer (Frida)**: time the script tick, `GC_try_to_collect`, the finalizers and the number of catch-up steps per frame inside `0xD81840`.

## Open questions

- Who else takes the routing lock `0x1021cf0`? Do the car/steering jobs on `JobThread`s block the sim thread through it?
- Are the `ProfilerUtils_*` and `Simulator_DoCPPProfile` hooks live in the release build?
- Is `GC_ENABLE_INCREMENTAL` usable with this Mono 1.2.3.1 build on Windows (write-watch dirty bits), or does it crash?
- What is the units/timer mode of `FUN_004f30e0(2)` (µs vs ticks)? This sets the real GC budget.
- Do the remaining Ghidra `OutOfMemoryError`s hide important functions? Rerun with `GHIDRA_HEADLESS_MAXMEM=24G`, or analyse in the GUI.
