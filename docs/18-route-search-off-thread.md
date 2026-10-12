# 18. Route search off the sim thread: research and design

Written 2026-10-13 for PLAN item 2c. Static analysis only, on `game-copy/bin/TS3W.exe` (Steam 1.67) with Ghidra headless (read-only project), dumpbin, and the decompiled C# in `game-copy/decompiled/`. The game was not run for this note.

Tags: **[verified]** = seen in disassembly or decompiled code (address given). **[community]** = from another project (linked). **[inferred]** = reasoning still to confirm by a probe or a run.

Scratch outputs (session scratchpad, not committed): `roots.txt` (who reaches the routing lock), `d18.c` (locomotion and lock functions), `d18job.c` (JobRunner loop), `m4-m6.txt` and `mono*.txt` (planner to interpreter call paths: none).

## Short answer

- Planning is fully synchronous, and the engine has **no async mode to switch on**. RoutingJob looks like an engine Job (same vtable shape as the JobManager jobs), but it is only ever built on the stack and run inline [verified].
- The planner runs native code only: no direct call path from the plan entry points into the Mono interpreter [verified for direct calls]. A worker thread therefore needs **no Mono thread attach** and is not touched by the GC [inferred].
- The routing world is guarded by one recursive lock, `RoutingWorld+0x918`. The sim thread takes it every iteration for each walking Sim's obstacle check (locomotion update inside `MonoScriptHost::Simulate`) [verified path]. A search running on a worker under that lock therefore overlaps only with the sim-thread work done **before the next lock use**. Long searches would still stall the next iteration.
- **Recommended design: async plan for the Sim's own task (B+C below).** A C# hook on `Route.Plan`/`PlanFromPoint`/`Replan`/`ReplanFromPoint` asks native for a background plan, then the Sim yields with `Simulator.Sleep(0)` until the result is ready. A ts3native worker runs the original internal call under the game's own lock. Expected gain: about 15-70 ms/s of sim-thread time in Bridgeport and 35-125 ms/s in Isla Paradiso, depending on how often the lock is free (to be measured first). It does not remove the long-search hitches by itself.
- **First step (one day):** a lock and overlap probe (Frida) plus an "offload-sync" mode in ts3native that runs each plan on a worker while the sim thread waits. This proves the planner runs safely on another thread and measures the overlap we could gain, without changing game behaviour.

---

## Q1. Call path, synchronous wait, and existing async machinery

### Call path [verified]

```
C#  interaction / GoHome / RoutingComponent.DoRoute ...
    -> Sims3.SimIFace.Route.PlanToPoint|PlanToLot|PlanToSlot|... (SimIFace Route.cs ~947-1110, all end in Plan())
    -> ScriptCore.Route.Plan() (ScriptCore/Route.cs:900)    also PlanFromPoint :928, Replan :955, ReplanFromPoint :985
         AboutToPlanCallback; mRoutePlanResult = Route_Plan(handle); start points; PostPlanCallback
    -> [DllImport "Sims3Common.dll"] = internal call
native
    IC_Route_Plan 0x765160     cdecl: [esp+4] = hidden RoutePlanResult* (0x48 bytes, default mType 2),
                               [esp+8] = route handle; handle -> route via 0x74B2C0 (table [0x11D40C4]);
                               route vtable +0x10 / +0x14 called around the plan (per-route lock or hold, inferred)
    -> Route_StartPointsFromPosition 0x7641D0 (PlanFromPoint/ReplanFromPosition: 0x7642C0)
    -> Route_PlanCommon 0x7606E0
    -> Route_PlanLocked 0x82C6E0 Lock_Acquire(world+0x918) ; 0x82C420 pending world updates ;
                                 RoutingJob on the stack 0x819D50 ; Execute 0x81B2A0 ; dtor 0x819AE0 ; Lock_Release
    -> PlanPath 0x661B50 -> Planner 0x65E0D0 -> world A* 0x676260 / lot 0x63E340 / water 0x671930
```

Other internal calls: `IC_Route_PlanFromPoint 0x765240`, `IC_Route_Replan 0x765490`, `IC_Route_ReplanFromPosition 0x765550`, `IC_Route_DestroyRoute 0x750400` (`internal-calls-map.csv`).

### Does the script wait? Yes [verified]

`ScriptCore.Route.Plan` stores the result and returns it in the same call. Every caller reads `PlanResult` at once, for example `RoutingComponent.DoRoute` (RoutingComponent.cs:395) takes an already planned route and checks `r.PlanResult.Succeeded()` first. The sim thread is blocked for the whole search, and so is every other script task, because they all run cooperatively on that one thread (docs/04a section 7).

### Is there an async mode we could switch on? No [verified]

| Clue | What it is | Usable? |
|---|---|---|
| RoutingJob vtable `0x1021258`: slot 0 = destructor, slot 1 = Execute `0x81B2A0`; fields `[3]` and `[4]` set in the ctor `0x819D50` | Same shape as the jobs `JobRunner_Loop 0x893FB0` runs (`vtable+4` = Execute, `job[3]` = name, `job[4]` = cancel token, refcount at `+4`) [verified loop; RoutingJob as a JobManager job is inferred] | The ctor is called only from `Route_PlanLocked`, on the stack, and the vtable is referenced only by ctor and dtor (`xrefs-job.txt`). Nothing queues it [verified]. |
| Status at `RoutingPathImp+0x94` written with atomic stores (1 = running) | Leftover of a design where another thread polls | Only the sync path writes it [verified, docs/11] |
| `RoutePlanResultType.FailedBecausePlannerAborted` | A job-cancel result | Never produced natively; only C# `IsDestObjectInvalid` sets it [verified] |
| `JobManager` / `JobThread%i` (`0x599CF0`) | Engine worker pool | Runs other jobs (store download etc.); no routing job [verified xrefs] |
| `[Script] Multithreaded=1` | Script on its own thread, separate from rendering | Not planner related [community/inferred, docs/03] |
| `Route.AboutToPlanCallback` / `PostPlanCallback` | Observe-only delegates | Cannot defer a plan [verified] |

Conclusion: EA built the planner as a job and then ran it inline. We have to supply the worker and the waiting ourselves.

---

## Q2. What the planner reads, and the lock

### The lock (corrects docs/11 Q4) [verified]

- The lock object is **`RoutingWorld+0x918`**, passed in `ecx` to `Lock_Acquire 0x4E16F0` (seen at `0x82EDC1`, `0x82C731`, `0x82C148`, `0x8343A3`, `0x833587`).
- **`0x1021CF0` is not the lock.** It is a pointer to the 64-bit constant `0xFFFFFFFF...` that every one of these calls pushes as the timeout (infinite). The 29 "lock users" in docs/11 were found by that constant. For the routing range `0x829370-0x834860` they do take `world+0x918`; the outside callers listed there were not re-checked one by one.
- `Lock_Acquire` is a recursive CRITICAL_SECTION or mutex with an optional timeout (docs/11).

### Who takes it, and on which thread

| Caller | Thread | Tag |
|---|---|---|
| `Route_PlanLocked 0x82C6E0` (every plan) | sim (inside a script internal call) | verified |
| **Locomotion obstacle check, every iteration**: `MonoScriptHost_Simulate 0xD81840` calls `0x83CED0` (locomotion update, if `[0x11D9140]` is set; simulate.c:217) -> `0x957210` per agent; agents in state 1 or 4 run `0x85FFE0` ("Locomotion/ObstacleList") -> `0x85FD10` -> `0x82EDB0` (RoutingWorld obstacle query, "Routing/PortalCheckCache", takes `world+0x918`) | sim, before the script phase | verified path; how often per iteration is inferred |
| Footprint add/remove (`IC_RouteManager_*Footprint`), `IC_Route_IsPointObstructedBySim`, `IC_RouteManager_IsOkToIdleAtCurrentLocation`, `IsRouteToObjectUnobstructed` | sim (script internal calls) | verified |
| Route edits `0x85F5A0` via `IC_Route_SetAGS`, `InsertCustomPathAtIndex`, `InsertRouteSubPathsAtIndex` | sim | verified |
| Object system `0x946160`/`0x947xxx` (footprints of moved objects), `0xB57F20` | not resolved (virtual/root functions) | inferred sim or main |
| World/lot streaming: `0xC622D0` (data-referenced near `WorldManager::Update 0xC6D570`), `0xC6B090`, lots `0xAAC110` (save), `0xAB1E60`, `0xAB4920`, `0x82C420` via `0xACB8A0` | probably main thread (S3SS: lot streaming is posted across threads) | inferred |

So the lock already guards the routing world against at least one other thread (lot streaming). That suggests the routing world was meant to be shared across threads under this lock [inferred].

### What the search reads

| State | Guarded by `world+0x918`? | Tag |
|---|---|---|
| Routing world graph, footprints (static, private, discourage), portal data, pending updates (`0x82C420` applies them first) | yes: every writer found takes it | verified writers in the routing range; others inferred |
| PathContainer: node pool, closed bitset, open heap (one per planner, `planner+0x10`) | yes, and it also means **only one search at a time** | verified (docs/11) |
| Lot lookup `0xC68990`, terrain height `0xC6A540` | lot table edits by streaming take the lock; terrain is static in live mode | inferred |
| Start points | built by `0x7641D0` from the follower's position, outside `PlanLocked` but inside the internal call | verified |
| Goal points, RoutingOptions (`route+0x158`) | owned by the route object; only script code for that route edits them | verified layout (docs/15) |
| Sim positions for dynamic obstacles | Sims' dynamic footprints change through `UpdateDynamicFootprint` (RouteManager, locked); raw object positions are not | inferred |
| Mono / managed objects | none: no direct call path from `IC_Route_Plan`, `PlanFromPoint`, `Replan`, `ReplanFromPosition`, `PlanCommon`, `PlanLocked`, `Execute` or `PlanPath` to the interpreter (`0xE53EB0`, `0xE59FD0`, `0xD7F000`) within 14-16 call levels | verified for direct calls; virtual calls not covered |
| Memory | EA allocator (`0x4E3F90`, named tags); JobManager threads use it too | thread-safe inferred |

### Hazards of running the search on another thread

1. **Lock contention**: while the worker holds `world+0x918`, the sim thread blocks at the next locomotion obstacle check, footprint update or synchronous plan. This decides the gain.
2. **Lock order / deadlock**: today the planner and its callers run on one thread, so recursive locks never conflict. On a worker, if the planner needs a lock the waiting sim thread holds (object manager, resource cache), it deadlocks [inferred risk; the offload-sync test in step 1 exposes it].
3. **Route lifetime**: a Sim reset or deletion while a plan is pending can finalize the C# `Route` and call `Route_DestroyRoute 0x750400` while the worker still uses it. A hook there must wait for that route's job.
4. **Unguarded reads**: object positions and lot data not written under the lock could change mid-search. The worst expected outcome is a path that is stale by one tick, which route following already handles by replanning [inferred].
5. **Thread affinity**: profiler scopes, TLS or asserts in the planner are unknown. Step 1 checks this.
6. **World unload / travel / save**: pending jobs must be drained before the routing world goes away (hook the world-unload path or simply wait in `DestroyRoute` and refuse new jobs while no world is loaded).

---

## Q3. Designs, ranked by gain per effort and risk

Numbers used (docs/04 F13, F16; all fixes on): route search **BP 97-108 ms/s, SV 77 ms/s, IP 184 ms/s** of sim-thread time. Base IP profile: `ReplanFromPosition` 10.9% self, 613 replans in hitches cost 50 s over 570 s.

Two unknowns set the gain of any off-thread design:
- **E**: share of route time from callers that may yield (the Sim's own task, `Simulator.CheckYieldingContext(false)` true). Estimated 0.6-0.85 [inferred: interactions, GoHome, visits and DoRoute replans dominate the samples].
- **O**: share of a search that overlaps with sim-thread work before the sim thread next needs the lock. Estimated 0.3-0.8 [inferred; measured in step 1].

| Rank | Design | Changes | Gain (sim-thread ms/s) | Effort | Risk |
|---|---|---|---|---|---|
| 1 | **B+C: async plan for the Sim's own task** | C# hooks on the four plan methods + native worker | E x O x route ms/s: BP 18-73, SV 14-52, IP 33-125 | M (3-4 days after step 1) | Medium-high |
| 2 | D2: search budget for unseen townies (docs/11 Q3) | Native code cave at the A* back-edge `0x676BE9` + open-list drain; signal bit from C# | Cuts the long tail (single searches up to 188 ms in BP; IP 25 ms average); also bounds what B+C must wait for | M | Medium (wrong failures for long trips) |
| 3 | D3: negative cache (built, docs/15) and C# backoff (done) | Test only | Failures only (BP 1.6%, SV 7%, IP 12% of plans, the costly ones) | S | Low |
| 4 | D4: fewer replans while following, for unseen townies (`RoutingComponent.DoRoute:540-571`, station/houseboat replans in `SimRoutingComponent:2304-2694`) | C# patch host | Part of IP's replan cost (base: ~88 ms/s of replans inside hitches) | S-M | Low-medium |
| 5 | Lock-free obstacle check: let `0x82EDB0` use try-lock and reuse last tick's obstacles when the worker holds the lock | Native detour | Turns O towards 1 for B+C; removes the stall of long searches | M | High (Sims may briefly overlap) |
| 6 | A: speculative planning (predict GoHome/visit routes and plan ahead on the worker) | Native + C# predictor | Unclear: routes must match exactly at use time | L | High |
| 7 | D1: cache successful routes / precomputed lot-to-lot paths | Native | Start points differ every time; the planner is already hierarchical (world, lot, water legs) | L | Medium |

### Design B+C in detail (recommended)

**Where:** at the internal-call level (`IC_Route_Plan 0x765160` and its three siblings), not at `Route_PlanLocked`.
- Reason: at `PlanLocked` the start/goal vectors are locals of `PlanCommon` (dead after return) and the out list `route+0xE0` is emptied by `PlanCommon` on every call. So we would have to deep-copy inputs and splice path lists. At the internal-call level the worker runs the whole original call, and the result is a plain 0x48-byte struct.
- The worker runs more code there (start points, `PlanCommon`, result mapping `0x752620`), but none of it calls into Mono (verified, direct calls), and the follower is idle while it runs.

**Protocol** (no new interop: C# already uses `Route.SetOption`/`GetOption`, which read the same option words `route+0x158+0x28..` that native sees; `Route_GetOption 0x74D040` calls `0x81C880` with `ecx = route+0x158` [verified]):

1. C# hook (patch host, instance stub as for `RouteBackoffHook`) on `ScriptCore.Route.Plan`, `PlanFromPoint`, `Replan`, `ReplanFromPoint`. Async only when: the follower is a Sim, `Simulator.CurrentTask == follower.ObjectId`, `Simulator.CheckYieldingContext(false)` is true, and the async switch is on.
2. C# sets `RouteOption.DebugPathPlan` (hi word bit `0x1`; not tested in `0x81B2A0`, docs/11 Q3; other native readers still to rule out) and calls the original.
3. Native hook on the internal call sees the bit:
   - no job for this route: start one on the worker and return `mType = FailedBecausePlannerAborted` (pending);
   - job running: return pending;
   - job done: copy its stored result into the caller's result struct, clear the bit, return the real result.
4. C# loop: while the result is pending (and `IsDestObjectInvalid` did not set it), `Simulator.Sleep(0)`, then call the original again. Then run the original method's tail (start points, `PostPlanCallback`).
5. Bail-outs: Sim reset, exit reason or 200 polls: clear the bit and call once more. Native then waits for the job and returns the result synchronously.
6. If ts3native is absent or off, nobody reads the bit, the first call returns a real result and the loop never starts. The fallback is free.

**Native side:** one worker thread (own `CreateThread`, 1 MB stack, normal priority), one job at a time (the planner is single-search anyway), a table `route pointer -> {state, result[0x48], key}`. The worker calls the original internal call through the trampoline. `IC_Route_DestroyRoute 0x750400` hook: wait for that route's job first. Counters added to `ts3native_counters`: jobs, pending polls, wait-in-destroy, sync fallbacks, worker busy ms.

**Mono and GC:** the worker never touches managed memory, so no `mono_thread_attach`, and Boehm's stop-the-world does not need to stop it [inferred]. The waiting C# frame keeps the `Route` wrapper alive, so no finalizer runs while pending, except on a Sim reset (covered by the destroy hook).

**What callers see:** a 1-3 tick delay (20-60 ms real time at speed 1) before a Sim starts walking. Vanilla Sims already pause while route following yields. Callers that try several destinations in a row (`PlanToUnusedPart`, `Lot.PlanRouteIntoLot`) pay one tick per try [inferred].

**Expected gain:** see the table. Not removed: an iteration that starts while a long search still holds the lock waits in the locomotion obstacle check. Long-search hitches need rank 2 or 5 as well.

---

## Q4. First step (one day): lock/overlap probe + offload-sync mode

Goal: measure **O** and **E**, and prove the planner runs correctly on another thread, **with no change to what the game does**.

### 1a. Frida probe `routelock` (tools/probe, measurement only)

| Hook | Read |
|---|---|
| `Route_PlanLocked 0x82C6E0` entry | `ecx` = RoutingWorld; lock = `ecx+0x918`; start/end time of each plan, thread id |
| `Lock_Acquire 0x4E16F0` when `ecx == world+0x918` | thread id, time spent inside (wait), count per iteration |
| `Lock_Release 0x4E17B0` (same `ecx`) | hold time per thread (track recursion depth) |
| `MonoScriptHost_Simulate` loop / `0x83CED0` entry | iteration boundaries |

Output per run: lock acquisitions per iteration by thread (sim vs others), for each plan, **the gap between its return and the sim thread's next routing-lock acquisition**; `min(plan length, gap)` is the overlap a worker would get (histogram, and the sum per second = O x route ms/s), and plan length percentiles.

For **E**, add counters to the existing C# `RouteBackoffHook.PlanHook`: plans with `CheckYieldingContext(false) && CurrentTask == follower`, and the rest (a 3-line change, read with the existing patch counters).

### 1b. ts3native 0.3.0: `[RouteWorker] Mode=0|1` (off by default)

- `Mode=1` (offload-sync): `PlanLockedHook` hands its arguments to a worker thread and waits on an event. The worker calls the original (`g_planOrig`). The sim thread does nothing else meanwhile, so game behaviour is unchanged except for the thread the search runs on.
- A writable `mode` field in `ts3native_counters` lets the probe switch it at runtime for an in-load A/B.
- Watchdog: if a wait exceeds 2 s, log the worker's state (deadlock evidence) and keep waiting (no unsafe recovery).
- Counters: offloaded plans, total handoff overhead (µs), longest wait.
- Hook point: the existing detour at `0x82C6E0` (prologue `81 EC D8 00 00 00`), same `__fastcall` signature as `PlanLockedHook`. (Step 2 moves to the internal-call level; offload-sync at `PlanLocked` already covers the planner and lock part, which carry the most risk.)

### Test plan (game tests stay serial; back up saves first)

1. Install ts3native 0.3.0 with `Mode=0`; restart; check `ts3native.log` and `/native` (`enabled`, mode 0).
2. Repros, mode switched to 1 by the probe: `sim_route_to` reachable short, reachable cross-lot, unreachable (roof or closed room), and in Isla Paradiso a cross-water trip. Expect: same results as mode 0 (success/failure, Sim arrives), 0 crashes, 0 watchdog lines.
3. Soak: `profile-world.ts --ab` on `Baseline-IP` and `Baseline-BP` (10 min, slices of 2-3 min) with the switch toggled, plus `routelock` running. Expect: iterations/s and route ms/s unchanged within noise, handoff overhead < 2 ms/s, 0 stuck Sims beyond the usual, no hang.
4. Read the probe: O per world, E from the C# counters. Go on to design B+C only if E x O x route ms/s is worth it (rule of thumb: >= 30 ms/s in IP). If O is low because the locomotion check takes the lock in every iteration, plan rank 5 (try-lock obstacle check) together with B+C, or do rank 2 (budget) first.
5. Log the run in `docs/data/change-ledger.jsonl` and update PLAN.md (item 2c) with the numbers.

### Step 2 (after step 1, 3-4 days)

Async protocol as above: native job table and `IC_Route_*Plan*` hooks (`0x765160`, `0x765240`, `0x765490`, `0x765550`), `IC_Route_DestroyRoute 0x750400` wait, C# hooks on the four plan methods with the yield loop, a runtime switch `async` for in-load A/B, and a repro per caller type (walk, go home, visit lot, replan during following via a blocked doorway). Before building, scan the native readers of the RouteOption hi word to confirm that `DebugPathPlan` is unused.

## Open questions

- How often does the sim thread take the routing lock per iteration, and does any other thread hold it for long? (step 1a)
- Does anything native read `DebugPathPlan`? (scan before step 2)
- Do the lock users outside `0x829370-0x834860` (object system, lots, world) run on the main thread? A thread id in step 1a answers this.
- Is the route vtable `+0x10/+0x14` pair a lock or a reference hold? If it is a lock, the worker holds it for the whole plan and any script read of that route waits; harmless while the Sim waits.

Not checked: virtual-call paths from the planner (direct calls only), the JobManager submit function (our own thread is simpler, so it is not needed), and whether the planner uses thread-local storage (step 1b tests it in practice).
