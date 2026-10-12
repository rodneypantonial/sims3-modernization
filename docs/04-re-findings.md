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

## F5. Measured: where sim-loop time goes on Bridgeport [measured 2026-10-12]

`tools/frida/stutter-profile.js`, 10 min on `Baseline-BP` with all fixes, speed 3, autopilot on (raw data `probe-out/profile/bp-600-*`, not committed):

- 31 loop iterations/s; **4.0 hitches/s** (iterations of 50 ms or more), 2,354 in total, worst 1,014 ms.
- **Route search: 95 ms per second** (26.5 plans/s, 15,456 plans, single searches up to 188 ms). Only **1.6% fail**, but failures cluster (47–58 per minute in minutes 1, 4 and 7, which also had the highest route time).
- GC: mark 16.8 ms/s plus forced collections 15.9 ms/s (176 forced, up to 149 ms); finalizers 1.8 ms/s.
- Dominant cost per hitch: route 270, GC 89, **unaccounted 1,995**. The profiler's script hook (`0xD80BC0`) sees only 12 ms/s, less than the route time that runs inside scripts, so it does not cover most script execution; the unaccounted part is script and native work inside the iteration that no hook times.

Consequences: (1) a route-*failure* cache alone targets ~2% of plans; the cost is mostly successful searches, so the routing fix should move searches off the sim thread or make fewer of them; (2) the largest bucket is still unidentified: next, sample the sim thread's stack (`/simstack`) during hitches to rank the unaccounted time.

## F6. Scripts run in the Mono *interpreter*; it is the largest sim-thread cost [verified 2026-10-12]

- TS3W.exe embeds only Mono's interpreter ("mint"): strings `..\..\mono\mono\interpreter\interp.c`, `mintops.c`, `transform.c` ("transform.c: Unimplemented opcode ..."), `MINT_JMP ...`; no `mini`/JIT strings at all. All C# game and mod code is interpreted.
- `FUN_00e53eb0` (4,400 lines decompiled, `game-copy/analysis/decomp/gc-hot.c`, references `interp.c`) is the interpreter's main loop (`ves_exec_method_with_context`). Called via `0xe59fd0` and `0xd7f000`.
- Sampling the sim thread's top frame through `/simstack` (676 samples over 3 min, Bridgeport, x3; `probe-out/profile/simstack-bp.jsonl`): **29.7% in `FUN_00e53eb0`**, 6.1% in `FUN_00e4c3e0` (GC area, called from the per-frame GC path `0xe4d270`/`0xe4d460`), 5.0% in `FUN_004e5e40`, 7.5% in ntdll (waits/locks), the rest spread thin. The interpreter's callees (internal calls, allocation) come on top of the 29.7%.
- This explains F5's "unaccounted" hitch time: it is mostly interpreted script work that the profiler's hooks do not time.

Consequences: the biggest stutter lever is script execution cost. Options, roughly by effort: (1) find the hot C# methods (managed sampler: read the interpreter's `ThreadContext.current_frame` chain on the sim thread and name each frame's method) and slim those down through the patch host or move them to native internal calls; (2) cut how often the hot paths run (autonomy, routing callbacks); (3) speed up the interpreter itself (dispatch, hot opcode paths) or compile hot methods natively, a large project. A full JIT swap (newer Mono) is not realistic for a 32-bit exe with EA's embedding.

### F6a. Toward a managed sampler (partial, 2026-10-12)

- Verified: the interpreter's ThreadContext is in the sim thread's TLS slot whose index is at `[0x11f4160]` (96 on this run); `ThreadContext[2]` is the current call frame and `frame[0]` its parent (decompile of `0xe53eb0`, where `param_2[2] = frame` and `frame[0] = previous`). Probe endpoint `/interp` reads the chain with the thread suspended.
- Verified: `[0x11f4170]` holds the current script *task* (saved and restored around each interpreter run); its `+0` is the task's base `MonoMethod` (e.g. `Sims3.Gameplay.ActorSystems.SimBuilderTask.Simulate`, `ScriptCore.ScriptProxy.Simulate`), `+0x140` its call depth.
- MonoMethod layout here: klass `+8`, name `+0x18`; MonoClass name/namespace `+0x34`/`+0x38` [verified live]. The interpreter fetches a callee's runtime info from `MonoMethod+0x10` (decompile line ~228) [inferred].
- Not yet working: mapping a frame's `frame[1]` to its MonoMethod (`/rmname` scans memory for `MonoMethod+0x10 == frame[1]` and found none, ~35 s per scan). Either `frame[1]` is not that pointer or the layout differs. Next: decompile the call opcode path (where `frame[1]` is assigned from the callee) or hook the method-entry point to count entries per MonoMethod.
- Task-level sample (885 samples, 3 min, Bridgeport x3; reads of `[0x11f4170]` while the thread runs, so roughly 5% unresolved): **67% `ScriptCore.ScriptProxy.Simulate`** (the wrapper that runs every game object's and Sim's script task), 2.1% `SimBuilderTask`, 1.0% `StoryProgressionTask`, 0.9% `ScriptProxy.CreateLogic`, ~20% no script task running. Splitting the 67% by `ScriptProxy.mTarget` failed with unsuspended reads (the proxy pointer races); it needs the reads done inside the suspended window.

## F7. Managed (C#) profile of the sim thread [measured 2026-10-12]

Tool: probe `/managedstack` (interpreter frame chain read with the thread suspended: `frame[1]` = interpreter info, `info+8` = MonoMethod, back pointer at `MonoMethod+0x14`; script thread = the one whose TLS slot `[0x11f4160]` is set) and `tools/probe/managed-sample.mjs` (5 Hz). 885 samples over 3 min, `Baseline-BP`, all fixes, x3; 18.5% of samples had no script running. Raw: `probe-out/profile/managed-bp.*`.

Inclusive (anywhere on the C# stack): `ScriptProxy.Simulate` 66.8%; `Sim.Simulate`/`Sim.DoInteraction` 34.8% (`InteractionQueue.ProcessOneInteraction` 33.2%); **routing `SimRoutingComponent.DoRoute` 16.6%** (`Route.Plan` 11.5%, native `Route_Plan` 9.9% self); `Lot.Simulate` 12.9% → `Lot.UpdateReactions` 8.9% → **`ReactionBroadcaster.PulseSims` 7.1%, `GetInRadiusSims` 4.2%**; `SimUpdate.Simulate` 10.2%; **`GoHome.Run` 7.2%** and `GoToVirtualHomeInternal.Run` 4.4%; **walk-style rules** `SimWalkStyleRules.GetAdjustedWalkStyleForRoute` 6.0%, `PostProcessWalkStyle` 5.4%, `WalkStyleComparer.Compare` 4.7%, `SortedList`2.Find` 4.9%; `Queries.GetObjects` 6.0%; `EventTracker.SendEvent` 4.6%; `Autonomy.Update` 5.4%.

Self (innermost): native `Route_Plan` 9.9%, **`System.Enum.Equals` 5.1%** (boxed enum compare through `DefaultComparer`, very slow interpreted), `Query_GetObjectsInRange` 2.6%, `Route_ReplanFromPosition` 2.0%, `Object.GetType` 1.9%, `Query_GetObjectsOnLot` 1.8%.

Fix candidates, by expected gain per effort [inferred, to verify one at a time]:
1. Walk-style rules: a `SortedList<WalkStyle,…>`/`WalkStyleComparer` lookup per route that ends in `Enum.Equals` via the default comparer. An int-based comparer or a cached result per Sim and route type could remove most of ~6%.
2. NPC go-home loops (7–11%): per-Sim backoff after failures, or send NPCs home off-screen without world-scale planning (docs/04a).
3. Reaction broadcasters (7%): `PulseSims`/`GetInRadiusSims` scan Sims around each broadcaster on every lot update; throttle the pulse rate or cache the Sim list per lot per tick.
4. Route search itself (~10% self): fewer plans (1, 2) first, then searches off the sim thread.

### F7b. Same profile across worlds [measured 2026-10-12]

`tools/harness/profile-world.ts --tag <world>` runs the same 10 min for every world (x3 with autopilot, keep_alive and declines every 30 s, the native stutter profiler throughout, the managed sampler in minutes 1–4, the native sampler in minutes 5–8); `tools/probe/profile-compare.mjs bp sv …` prints the table. Sampling does not inflate hitches: on Sunset Valley the rate was 1.5/s before sampling, 1.7/s and 2.0/s during the two samplers, 1.8/s after.

| | Bridgeport (`Baseline-BP`) | Sunset Valley (`Baseline-SV`) | Isla Paradiso (`Baseline-IP`) |
|---|---|---|---|
| iterations/s | 31.2 | 63.3 | 31.7 |
| hitches/s (≥50 ms) / worst | 4.0 / 1,014 ms | 1.9 / 2,210 ms | 3.5 / 1,170 ms |
| route ms/s, plans/s, fail % | 94.9, 26.5, 1.6% | 82.2, 10.1, **7.0%** | **246.4**, 9.7, **12.0%** |
| ms per plan | 3.6 | **8.1** | **25.4** |
| GC mark / forced ms/s, forced max | 16.8 / 15.9, 149 ms | 11.2 / 6.6, 72 ms | 14.1 / 13.2, 162 ms |
| interpreter top frame (native samples) | 29.7% | 28.5% | 23.0% |
| no script running (managed samples) | 18.5% | 31.6% | 18.2% |
| `Sim.Simulate` (incl.) | 34.8% | 26.2% | 43.5% |
| `RoutingComponent.DoRoute` / native `Route_Plan` self | 16.5% / 9.9% | 7.3% / **11.5%** | 21.4% / **13.3%** (+ `Route_ReplanFromPosition` **10.9%**, `Route_PlanFromPoint` 2.8%) |
| walk-style rules / `Enum.Equals` self | 6.0% / 5.1% | 2.4% / 2.5% | 2.7% / 2.3% |
| `Lot.UpdateReactions` / `PulseSims` | 8.9% / 7.1% | 1.9% / 1.4% | 6.6% / 5.7% |
| `GoHome.Run` / `GoToVirtualHomeInternal` | 7.2% / 4.4% | 5.2% / 1.2% | **11.1%** / 1.1% |
| `Queries.GetObjects` / `EventTracker.SendEvent` | 6.0% / 4.6% | 1.1% / 0.8% | 3.1% / 0.6% |

Reading [measured, interpretation inferred]:
- Sunset Valley is lighter overall (twice the iterations, half the hitches) but its route searches are slower and fail more: fewer plans, each 2.2× longer, 7% failing. Failed searches are the expensive ones: one iteration with 4 failed `Route_Plan` calls took 1,010 ms. On this world a **route-failure cache or per-Sim backoff** pays more than on Bridgeport.
- Bridgeport's extra cost is population-driven script work (reaction broadcasters, object queries, events, walk-style rules), consistent with a dense downtown.
- Common to both: native `Route_Plan` is the largest single self cost (10–12%); NPC go-home is 6–12%; the largest hitches (0.5–2.2 s) carry little route or GC time, so they are interpreted script work no hook times (F5).

Isla Paradiso (`Baseline-IP`: new save from `IslaParadiso.world`, Dregg household on the "Atlantis" houseboat, aged 14 sim days at x3 in 36 min, no crash) [measured 2026-10-12]:
- **Routing dominates: 246 ms of route search per second (2.6× Bridgeport), 25 ms per plan, 12% failing.** Native route calls are ~29% of all script samples (`Route_Plan` 13.3%, `Route_ReplanFromPosition` 10.9%, `Route_PlanFromPoint` 2.8%, `Route_Replan` 1.7%). In hitches alone: 1,555 `Route_Plan` (21% failed, 57 s), 613 `ReplanFromPosition` (50 s), 273 `PlanFromPoint` (22 s) over 570 s.
- Who routes (task frame above the route call, % of samples): `GoHome.Run` 8.0%, rabbit-hole trips 6.4%, `VisitCommunityLot`/`VisitLot`/`GoToCommunityLot`/`GoToLot` 7.2%, `StandAndWaitController` 1.5% (blocked Sims waiting and replanning), group walks 2.4%. `GoHome.Run` inclusive is 11.1%, the highest of the three worlds.
- **NPC houseboats move on their own:** 3 of the 6 occupied houseboats (The Romantic, The Angler, Scott) changed position in 10 min (`houseboats` agent command, start/end snapshot). Scott is the household players name as the worst lot.
- Reading [inferred]: cross-water trips make every plan long and failure-prone, and Isla Paradiso sends more Sims on them (go-home, lot visits). The off-screen go-home (PLAN fix b) and a route-failure backoff should pay most here; houseboat placement off-screen (fix b2) is now worth building, after measuring how much of the replan time is boat-related.
