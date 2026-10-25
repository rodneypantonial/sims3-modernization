# 18. Route search off the sim thread: research and design

Written 2026-10-13 for PLAN item 2c. Static analysis only, on `game-copy/bin/TS3W.exe` (Steam 1.67) with Ghidra headless (read-only project), dumpbin, and the decompiled C# in `game-copy/decompiled/`. The game was not run for this note.

Tags: **[verified]** = seen in disassembly or decompiled code (address given). **[community]** = from another project (linked). **[inferred]** = reasoning still to confirm by a probe or a run.

Scratch outputs (session scratchpad, not committed): `roots.txt` (who reaches the routing lock), `d18.c` (locomotion and lock functions), `d18job.c` (JobRunner loop), `m4-m6.txt` and `mono*.txt` (planner to interpreter call paths: none).

## Short answer

- Planning is fully synchronous, and the engine has **no async mode to switch on**. RoutingJob looks like an engine Job (same vtable shape as the JobManager jobs), but it is only ever built on the stack and run inline [verified].
- ~~The planner runs native code only~~ **Wrong (crash 2026-10-13):** the planner calls script code through `mono_runtime_invoke` during a plan, via a virtual call (no direct path exists) [verified crash stack]. A worker must hand those calls back to a Mono thread; see "Crash 2026-10-13" below.
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
| 2 | D2: search budget for unseen townies (docs/11 Q3). **Built 2026-10-13 as ts3native 0.4.0 `[RouteBudget]` (docs/15 phase 4), not run in game yet**: cost-function stubs instead of the back-edge cave, so no drain is needed; caps every request (100 ms default), because "unseen" is not decidable natively (`OnlyFlagged` waits for a C# caller to set `DebugPathPlan`) | Native code cave at the A* back-edge `0x676BE9` + open-list drain; signal bit from C# | Cuts the long tail (single searches up to 188 ms in BP; IP 25 ms average); also bounds what B+C must wait for | M | Medium (wrong failures for long trips) |
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

**Mono and GC:** ~~the worker never touches managed memory~~ the planner makes managed calls (crash 2026-10-13). In offload-sync the waiting sim thread runs them (ts3native 0.4.1). In this async design the sim thread is not waiting, so each managed call would have to wait for the Sim's next yield, or the callback must be identified and its answer computed before the plan starts. The callback is now known (portal-lock query, see "Managed calls during a plan"); its answer depends on the Sim and door state at plan time. The waiting C# frame keeps the `Route` wrapper alive, so no finalizer runs while pending, except on a Sim reset (covered by the destroy hook).

**What callers see:** a 1-3 tick delay (20-60 ms real time at speed 1) before a Sim starts walking. Vanilla Sims already pause while route following yields. Callers that try several destinations in a row (`PlanToUnusedPart`, `Lot.PlanRouteIntoLot`) pay one tick per try [inferred].

**Expected gain:** see the table. Not removed: an iteration that starts while a long search still holds the lock waits in the locomotion obstacle check. Long-search hitches need rank 2 or 5 as well.

---

## Q4. First step (one day): lock/overlap probe + offload-sync mode

**Built 2026-10-13 (not run in game yet):** 1a as `GET /routelock?start=1` in `tools/probe` (agent.js `routeLock`); 1b as ts3native 0.3.0 `[RouteWorker] Enabled/Mode` (docs/15 phase 3), runtime switch `GET /native?routeworker=0|1`, `profile-world.ts --ab routeworker`. Differences from the plan below: an `Enabled` key starts the worker and installs the detour (`Mode` is only the start value of the switch, so `Enabled=1, Mode=0` allows the in-load A/B); counters are in microseconds (`workerUs`, `workerMaxUs`, `waitUs`; handoff overhead = `waitUs - workerUs`); the job also carries the caller's x87 control word and MXCSR. The E counters in the C# `RouteBackoffHook` are not built yet.

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

## Crash 2026-10-13 (step 1b, first in-game use) and fix

Bridgeport, `workerMode` 1, first `sim_route_to` plans: ACCESS_VIOLATION reading 0x58 at 0x00EA2B03 within about 1 s (`xcpt <pc> 26-10-13 01.11.31.txt`). Details and addresses in docs/15 Phase 3 "Crash 2026-10-13 and fix".

- Chain [verified, disassembly + return addresses in the stack data]: planner -> (virtual call) -> `mono_runtime_invoke` -> `[0x1195B60]` = interpreter runtime_invoke 0xE5A600 -> `ves_exec_method` 0xE59FD0 (domain from `mono_domain_get()` = TLS 0x11959DC, NULL on the worker) -> box return value 0xE52C70 -> `mono_object_new` 0xEA36F0 -> `mono_class_vtable` 0xEA2AD0 reads `domain->domain_id` (+0x58) -> fault.
- The managed callee returns a non-void, non-reference type (bool, int, float or struct) [verified from the boxing switch]. Which method: see "Managed calls during a plan" below (the portal-lock query, `LotManager.PortalLockedCallback`, returns `uint`).
- Boehm here registers one thread only (`GC_new_thread` 0xE51070 has a single caller, `GC_thr_init`) [verified]. No game thread wrapper registers threads, so "create the worker like the game's threads" does not exist, and attaching the worker to Mono would run script code on a second OS thread for the first time.
- Fix options ranked by safety: (1) **chosen**: managed calls from the worker run on the waiting caller thread (hook the runtime_invoke slot, a data pointer; no code patch); same thread and order as without the worker. (2) Move only the A* inner loop to the worker: needs the callback site first, and the A* may itself be where the callback is. (3) `GC_new_thread` + domain TLS on the worker: Boehm-safe, but thread-static and `MonoThread` state stay missing [inferred risky].
- Consequence for step 2 (async B+C): see the note under "Mono and GC" above.

## Managed calls during a plan (2026-10-13, static analysis)

Ghidra headless on `game-copy/bin/TS3W.exe` (scripts kept in the session scratchpad: `RouteManagedCalls.java`, `WorldEvents.java`) plus the decompiled C# in `game-copy/decompiled/`.

**One bridge found: `World.OnPortalLocked` (WorldEvent 59)** [verified, static]:

- Native chain (direct calls, the earlier 30-level search missed it because the last hop is virtual): `Route_PlanLocked 0x82C6E0` -> `0x81B2A0` -> `0x661B50` -> `0x65E0D0` -> `0x640E00` -> `0x63EC40` (lot/portal search) -> **`0x634500`** (portal-edge cost) -> `0x64F550` / `0x64F570` (is locked == 1 / is discouraged == 3) -> **`0x64F2C0`** (portal lock state) -> **`0x791C00`** (marshals 6 args: lotId, routingObjectId, portalObjectId as u64, roomFrom, roomTo as short, initialLocked as bool) -> **`0x78F2B0`** (fire World event N: delegate table `0x11D6A78 + N*0x24`, written by `World_RegisterCallbackDelegateImpl 0x7861A0`) -> script host `[0x11D4BBC]` vtable `+0x134` (delegate) or `+0x138` (method) -> `mono_runtime_invoke` -> `[0x1195B60]`. The script-host hop is a virtual call [the host -> runtime_invoke step is inferred from the host vtable; the event table, 59 and the 6 args are verified].
- Managed callee: `LotManager.PortalLockedCallback(ulong lotId, ulong routingObjectId, ulong portalObjectId, short roomFrom, short roomTo, bool initialLocked) : uint` -> `Lot.OnPortalPathPlan` -> each `PortalPathPlan` handler. Every non-world lot adds `Sim.OnPortalPathPlan` at load (`Lot.cs:4031`), and adding the first handler calls `Route.SetPortalLockStatus(lotId, true)` (the native lot flag at lot record `+0x8F`). The return `uint` is boxed by the interpreter, which matches the crash (box -> `mono_object_new` with NULL domain).
- `Sim.OnPortalPathPlan` does: tomb mummy / proprietor checks, `Door.IsAllowedThrough(roomFrom, roomTo, sim)` (and the same for firefighter pole, box stall, horse jump, elevator), toddler/pregnant check for diving boards, `Ladder.DiscourageSimFromUsing`. It allocates (`new PortalPlanEventArgs`, `GetInvocationList()`, `LockedDoorsDuringPlan.Add(door)`), so **a Boehm GC can run inside the callback, in the middle of a plan** [verified from the C#]. With the route worker, a GC there stops/scans only the registered caller thread, not the worker's stack (docs/15 Phase 3 "Still open" 3) [inferred].
- No other World event with a planner caller: of the ~70 events fired through `0x78F2B0`, only 59 is reached from `Route_PlanLocked` by direct calls (`OnRoutingEvent` 57, `ObstaclesEncountered` 58, `IsOkToTurnFaceToDestination` 60 are fired from elsewhere) [verified for direct-call paths only; another virtual hop to a script host call is not excluded].

**How often** [inferred from `0x64F2C0`]:

- Only for portal edges (doors, stairs, ladders, pool/elevator portals) on a lot whose `+0x8F` flag is set, i.e. every non-world lot that has finished loading; world-level legs never call it.
- One managed call per (portal, room pair, direction) the lot search costs. `0x64F2C0` stores the answer in a hash map (`0x64F1E0`, key portal id + rooms; the low byte holds one direction, the next byte the other) and returns the stored value afterwards; each cost evaluation asks up to 4 times (locked/discouraged x 2 directions), all but the first per direction are map hits. Which object owns the map (per plan or per agent, and when it is cleared) is not traced; that the result depends on the Sim suggests per plan.
- If the callback returns 0 (`NoListeners`) or > 3, the lot flag `+0x8F` is cleared and that lot is not asked again.
- So a plan across a house asks roughly once per door/stair the lot search touches: estimated 2-30 calls per plan with lot legs, 0 for world-only legs. Each is a full Mono interpreter call (delegate invoke, two `ObjectFromId`, `IsAllowedThrough`), likely tens of microseconds [not measured].

**Cost with the route worker (offload-sync)**: every such call is a thread round trip (worker -> caller -> worker: 2 event signals and 2 wakes, likely 10-50 us each on Windows) on top of the call itself. For 2-30 calls per plan that is up to ~1.5 ms per plan, while the sim thread waits anyway. Measure with the `invokes` counter against `workerCalls` (0.4.1+). For the async design (step 2) the answer must come from the sim thread: either precompute the lock state of every portal on the lots in the plan before handing off (needs the portal list per lot), or run the lot search on the sim thread and offload only the world A\* `0x676260`, which has no managed calls by this analysis [inferred].

## Async plan (step 2, ts3native 0.5.0 + ts3fixes, built 2026-10-13, not run in game)

Opt-in: `[RouteAsync] Enabled=0` by default; runtime switch `asyncMode` (`/native?routeasync=0|1`, `profile-world.ts --ab routeasync`). The C# side is always present and asks native; with ts3native absent or off it costs one `GetOption` call per eligible plan.

### Can the callers take a deferred result natively? No: async lives at the script level [verified]

- `ScriptCore.Route.Plan`/`PlanFromPoint`/`Replan`/`ReplanFromPoint` store the internal call's result and return it at once (Route.cs:900-1000). 137 game source files call `.Plan()`/`.Replan()` 278 times and read `PlanResult` right after. No caller has a "plan in progress" or retry-later branch; `FailedBecausePlannerAborted` (the only abort result) is set only by `IsDestObjectInvalid` and every caller treats it as a failure (route-fail behaviour, replans, interaction exit).
- So returning "pending" from native to unchanged callers would turn every pending plan into a route failure. The wait has to happen inside `Route.Plan` itself, on the Sim's own task: the ts3fixes hook (already on `ScriptCore.Route.Plan` for the backoff) sleeps the task with `Simulator.Sleep(0)` until native has the result. **Chosen: script-level yield + native worker** (design B+C above), smallest version: only `Route.Plan` (the funnel of every `PlanTo*` helper); `PlanFromPoint`, `Replan`, `ReplanFromPoint` stay synchronous.

### How it works

| Step | C# (`RouteBackoffHook.PlanAsync`, mods/ts3fixes/src/RouteBackoff.cs) | Native (ts3native.cpp) |
|---|---|---|
| Eligible? | follower is a Sim, `Simulator.CurrentTask == sim.ObjectId`, `CheckYieldingContext(false)`, switch `asyncroute` (default on) | |
| START | `GetOption(bit 58)` | `Route_GetOption 0x74D040` detour: bit 58 (`0x0400000000000000`, no enum name, never set by the game) = START. Mode 1, slot free, caller may yield (`CanYield`, see "Crash: yield in a nested call"): store handle + FPU state, signal the worker, answer true. Slot busy with another route, or the caller may not yield: false (counter `asyncRefused`), C# plans synchronously |
| Wait | `GetOption2(bit 58)` = READY; false: `Simulator.Sleep(0)` and ask again (cap 1000 polls) | worker runs the **whole original `IC_Route_Plan 0x765160`** (start points, `Route_PlanCommon`, `Route_PlanLocked` under `world+0x918`, result mapping) into the slot. READY answers true when done and arms "collect" for this handle; while running it also runs a pending managed call (below) |
| Result | original `Route.Plan` once: `IsDestObjectInvalid`, `AboutToPlanCallback`, internal call, start points, `PostPlanCallback`, as without us (**correction, docs/185 §2:** `AboutToPlanCallback` now runs after the job, so the job misses its route options and forbidden portals, and it clears `LockedDoorsDuringPlan` the job filled) | `IC_Route_Plan` detour: armed for this handle: copy the stored 0x48-byte RoutePlanResult (`asyncCollected`). Not armed (poll cap, a plain plan of the same route): wait for the job, drop it, plan synchronously (`asyncDropped`) |
| Always | `finally`: `GetOption2(bit 58 \| bit 62)` = CANCEL (disarm) | an uncollected result (Sim reset while sleeping, destination gone) is dropped by the next START |

The route's native data is held by the worker for the job: route vtable `+0x10`/`+0x14` around every route internal call are AddRef/Release (`0x8ABE50`: `lock xadd [ecx+0xC], 1`; `0x8ABE60`: release) [verified], so a `DestroyRoute` during the job cannot free the route under the worker; the last Release may then run on the worker.

### Managed calls and the routing lock (why it cannot deadlock on the known paths)

The planner's one managed call (portal-lock query, World event 59) must run on the Mono sim thread, which is no longer waiting. Two places run it there:

1. **Lock_Acquire 0x4E16F0 detour** (prologue `53 55 56 8B F1 80 7E 24 00`). The routing world lock `world+0x918` is a game `Lock` (CRITICAL_SECTION or mutex, flag `+0x24`, recursion `+0x20`; `[deadline]` = absolute GetTickCount or `0xFFFFFFFF`; `-2` = timed out) [verified, disassembly]. While a job runs, a blocking acquire on the job's owner thread becomes a try-lock loop (deadline 0 = one TryEnter / 0 ms wait inside the original) that runs the worker's managed call in between (1 ms waits). This covers the per-iteration locomotion obstacle check, footprint updates and the route handle table lock `0x1004124`. Counters `asyncPumpWaits`, `asyncPumpUs`; a 2 s watchdog logs and keeps waiting.
2. **READY poll**: runs a pending managed call without waiting, so a job progresses at least once per tick even when the sim thread never touches a busy lock.

Other plans on the sim thread while a job runs (another Sim's synchronous plan, Overwatch, our SafeSpot check) reach `Route_PlanLocked` -> `RunPlan`, which first waits for the job (running its managed calls) and then plans as usual; a plan made **inside** the job's managed call goes to the worker, nested in the job's search exactly as it is nested without us (`g_inInvoke`).

### Risks

- **Routing lock `world+0x918` (the gain limit):** the worker holds it for the whole search; the sim thread's next locomotion check waits for it (pumped). Gain = sim-thread work done before the next lock use (O in Q3); long searches still stall the next iteration. Measure `asyncPumpUs` against `asyncUs`: if pump time is close to job time, async saves little and the try-lock obstacle check (rank 5) or the budget (rank 2) must come first.
- **World mutation during the search:** every routing-world writer found takes `world+0x918`, so it waits for the job; the search sees one consistent world. Not guarded: object/Sim positions read outside the lock and the route object itself (another script calling `SetOption`/`GetPath` on this route while its owner sleeps would race the worker; only the owner's task uses its route in the cases seen) [inferred].
- **Stale results:** the result reflects the world when the job started (portal answers up to one tick later), applied 1+ ticks later. The Sim does not move meanwhile (its task is in our loop); other Sims and doors may have, which route following already handles by replanning, as for any path that ages while walking [inferred].
- **Managed call needing a lock the worker holds:** if the portal callback (run on the sim thread for the worker) itself took `world+0x918`, the worker would wait for the callback and the callback for the lock: deadlock (logged by the watchdog, not recovered). The same risk exists in offload-sync 0.4.1; its first in-game run tells.
- **Unknown bit 58:** nothing names it in `RouteOption`, and START/READY/CANCEL never reach the game's `GetOption` (we answer them); only a native reader of a *set* bit 58 would matter, and we never set it [verified that the C# enum skips it; native readers not scanned].
- **Save, travel, world unload during a job:** not handled specially; a writer of the routing world waits for the job (the sim thread pumps). Untested.
- **Dead time:** each async plan costs the Sim at least one tick (20-60 ms real time at speed 1) before it walks; callers that plan many candidates in a row (`PlanToUnusedPart`, `Lot.PlanRouteIntoLot`) wait one tick per plan.

### Verified vs inferred

- Verified: callers need a synchronous result (C#); IC_Route_Plan, Route_GetOption, Lock_Acquire prologues and calling conventions (dumpbin); route `+0x10/+0x14` = AddRef/Release; no lock in the event-fire path `0x78F2B0`; the protocol, lock pump, collect/drop/refuse and the queueing behind a job in the selftest (`TestRouteAsync`, fakes for the three game functions; 20/20 runs).
- Inferred, to be measured: that the sim thread does useful work while the worker searches (O), that the portal callback takes no routing lock, that Sleep(0) yields exactly one tick in this context, and the real gain.

### Crash: yield in a nested call (2026-10-13, 0.5.0) and fix (0.5.1)

First in-game use of `[RouteAsync]`: the game aborted with `` * Assertion at ..\..\mono\mono\interpreter\interp.c:3595, condition `pState->yieldReturn' not met `` (the yield opcode's check at `0xE59763`/`0xE5976D` pushes `0x108E704` "pState->yieldReturn"; the game's SIGABRT handler hits `int3` at `0x58D9B0`) [verified crash]. `PlanAsync` called `Simulator.Sleep(0)` while `Route.Plan` ran inside a native->managed call made during the Sim's task. `CheckYieldingContext(false)` only tests `YieldingDisabled` and a current task, so it passed.

How EA's yield works [verified, disassembly]:

| Piece | Address | What it does |
|---|---|---|
| Interpreter state ("pState", 0x150 bytes) | `0xE52D70` allocates one per task; `runtime_invoke 0xE5A600` builds a fresh one on its stack; `0xD8A8E0` (another native->managed entry, called from the script-host glue around `0xD9B130`) does too | `+0x148` free on exit, `+0x149` **yieldReturn**, `+0x14A` fresh, `+0x14B` stack-allocated, `+0x140` resume point (-1 = not resuming) |
| Current state | `ves_exec 0xE53EB0` saves `[0x11F4170]`, stores its pState there, restores it on return (`0xE597BA`, `0xE5986C`) | the innermost running interpreter state |
| Task runner | `0xD7F000` (TaskObjects), `this` = MonoScriptHost `[0x11EE514]` (ctor `0xD7E010`) | per task: `host+0xBC8` = pointer to the task's pState slot, `host+0xBD0/+0xBD4` = task ObjectGuid; then `0xE59FD0(pState)` (which clears `yieldReturn` on a resume) |
| Mark yield | `0xD70F60` (host sub-object at `host+8`, vtable slot `0x108B7AC`) | if slot and task id are set: `yieldReturn(*slot) = 1` via `0xE52B50`. Only the task's **root** state is marked |
| Yield opcode | `0xE59751` | calls the host sleep (`0xD70820` -> host vtable `+0x20`), then asserts `yieldReturn` on the **current** state |
| After the run | `0xD7F000` reads `yieldReturn` (`0xE52B40`) of the root state: set = task sleeps, else it ended |

So a yield is legal only when the current state is the task's root state: `[0x11F4170] == *[host+0xBC8]` with a task id in `host+0xBD0`. Any native->managed call during the task (runtime_invoke from a native callback, `0xD8A8E0`) runs on its own state, whose `yieldReturn` nothing sets: the assertion. Managed code cannot see this (no script API reports the nesting), so the test is native.

Fix (ts3native 0.5.1, ts3fixes):

- `CanYield()` in ts3native: exactly the rule above (host, slot, task id, current == root). `AsyncStart` refuses START when it is false (counter `asyncRefused`), so `PlanAsync` plans synchronously there.
- New query `GetOption2(bit 58 | bit 61)` = NOYIELD (`0x2400000000000000`, no enum name): true = a yield here would abort. ts3fixes `MayYield()` = `CheckYieldingContext(false)` and not NOYIELD, asked on a handle-0 `ScriptCore.Route` (the game's `Route_GetOption` answers false for handle 0: `0x74B2C0` rejects handles without the `0x25` tag). Without ts3native (or an older one) it answers false, so the old rule applies. The `Route_GetOption` detour is now installed whenever ts3native runs on TS3W.exe, also with `[RouteAsync]` off, so the guard works there; START additionally needs `asyncReady` (all three async detours), since `asyncMode` can be switched on at runtime.
- `MayYield()` guards the other two yield sites from our hooks: the Overwatch stuck-check hook (`OverwatchStuck.cs`, docs/04 F18) and the stuck watchdog's idle-spot plans (`RouteFixes.cs`, our own task, so it should always say yes). Where it says no they run without yielding, as before the fix existed.
- Not taken: counting nested `runtime_invoke`s in `InvokeHook`. A managed exception thrown through runtime_invoke without an `exc` out parameter leaves by `longjmp` (`0xE59FD0`), skipping the decrement, and `0xD8A8E0` does not pass through the slot; the state comparison has neither gap.

Tests: selftest `TestRouteAsync` (fake host, slot and current state: root state may yield and START works; nested state, no task id, no current state, no host: NOYIELD true and START refused with `asyncRefused`; other option bits pass through). In game (not run yet): the `bp-route-async` repro and the Overwatch repro `bp-overwatch-stuckcheck` with ts3native 0.5.1; pass = no assertion, `asyncRefused` above 0 only where plans came from nested calls.

### Crash: async route assert, hang, orphan job (2026-10-13, 0.7.0) and fix (0.7.1)

Symptoms with `[RouteAsync]` on: the interp.c:3595 `pState->yieldReturn` assertion again (from a Sim's own top-level task, so `CanYield()` was not the gap), a hang, and a job left running after its Sim was reset.

Cause [verified, disassembly]:

- The planner calls script through the **host delegate call `0xD75FA0`** (MonoScriptHost vtable slot `0x108B7C4`, same vtable as the yield mark `0xD70F60`, so `this` = host+8; thiscall, hidden out pointer + 3 args, `ret 10h`; no direct callers). Around its `mono_runtime_invoke` (`call 0xEA1400` at `0xD7620C`) it saves the running task's id `[ebx+0xBC8]/[ebx+0xBCC]` = **host+0xBD0/+0xBD4**, zeroes it (`0xD76200`: `89 BB C8 0B 00 00 89 BB CC 0B 00 00`, edi = 0) and restores it (`0xD7621F`: `89 B3 C8 0B 00 00 89 8B CC 0B 00 00`, followed by a `je` on the flags of `cmp eax,edi`).
- Up to 0.7.0 only the runtime_invoke slot was proxied, so `0xD75FA0` itself ran **on the worker**. In async mode the sim thread runs script meanwhile, and the worker zeroed that task's id under it. `0xD70F60` marks a yield only when `[this+0xBC0]` (slot, host+0xBC8) and `[this+0xBC8]|[this+0xBCC]` (task id) are set, so the task's yield got no mark and asserted. The same call also boxed the callback arguments (`mono_value_box`) on the worker, a thread Boehm does not scan [inferred risk: a GC between boxing and the call could hand the callback garbage; a 0 return clears lot flag +0x8F at `0x64F2C0`].
- Hang: `ts3native.asi+0x24CB` = `InvokeHook`'s `WaitForMultipleObjects` on the worker inside `0xD75FA0`, waiting for the sim thread to serve the invoke; `LockAcquireHook` pumped only `INFINITE` waits, so a finite-deadline `Lock_Acquire` on the sim thread did not serve it.
- Orphan job: START fired (the C# switch defaults on, a plan right after load), the caller vanished (Sim reset); CANCEL only cleared `g_collect`, so the job kept running with nobody to collect it.

Fix (ts3native 0.7.1, counters v8):

1. **`0xD75FA0` detour** (prologue `55 8D 6C 24 98 81 EC 84 00 00 00`, checked before patching): on the worker, while a sync caller waits or an async job runs, the whole host delegate call (boxing, EA's task-id guard, the invoke) goes to the caller/owner thread, where it runs without the worker (counter `hostCalls`). Chosen over call thunks at `0xD76200`/`0xD7621F` that skip the clear/restore on the worker: one checked detour on the existing proxy plumbing instead of two patched sites and naked thunks (the restore site must also keep the flags for its `je`), and it moves the boxing off the worker too. Without this detour `workerReady` stays 0 and async stays off.
2. `ServeInvoke` (caller/owner side of every proxied call) saves host+0xBD0/+0xBD4, zeroes it, runs the call, restores it: EA's guard on the thread that runs the script (also for runtime_invoke calls that do not come through `0xD75FA0`).
3. CANCEL of the caller's own job waits for it (serving its managed calls) and drops it (`asyncDropped`, state idle). With `asyncMode` switched to 0 mid-job, the owner's next GetOption (START/READY/CANCEL) or `IC_Route_Plan` drains and drops it; READY then says done and `Route.Plan` plans inline.
4. `LockAcquireHook` pumps every non-zero deadline on the owner during a job. A finite deadline (absolute GetTickCount) that passes ends with one original call with that deadline, so the game gets the original's own result (`-2` = timed out).
5. Counters `planOffCalls/planOffFails/planOnCalls/planOnFails`: Route_PlanLocked requests and failures with worker/async mode off vs on (on = `workerMode` or `asyncMode` 1 and ready), for A/Bs.

Tests: selftest (11/11 runs): a host call from a worker plan runs on the caller with the task id cleared there and restored after, other threads pass through; per-mode plan/fail counts; CANCEL of a running job drops it (another route's CANCEL does not); mode flip drained by READY and by `IC_Route_Plan`; the lock pump on a finite deadline serves the managed call and takes the lock, and a passed deadline returns `-2` while the job still holds the lock. In game: not run yet.

### How to test in game (serial, saves backed up)

1. Install: close the game, `node tools/setup/install-fixes.ts install --only native-patches` (ts3native 0.5.1), rebuild/install ts3fixes as usual, set `[RouteAsync] Enabled=1` in `Game\Bin\ts3native.ini` (Mode 1), leave `[RouteWorker]`/`[RouteBudget]`/`[RouteCache]` at 0. Check `ts3native.log` for `OK detour Lock_Acquire`, `IC_Route_Plan`, `Route_GetOption` and `route async settings: Mode=1`.
2. Only after the offload-sync check (PLAN "Next" 1) is clean: `repro.ts run bp-route-async`; then `/native` and ts3fixes counters [52-54].
3. Soak: `profile-world.ts --ab routeasync` on Baseline-BP and Baseline-IP (10 min, 2-3 min slices). Pass: 0 extra stuck Sims, no hang, watchdogs 0, hitches and iterations/s not worse; read `asyncUs` vs `asyncPumpUs`.

## Open questions

- How often does the sim thread take the routing lock per iteration, and does any other thread hold it for long? (step 1a)
- Does anything native read `DebugPathPlan`? (scan before step 2)
- Do the lock users outside `0x829370-0x834860` (object system, lots, world) run on the main thread? A thread id in step 1a answers this.
- Is the route vtable `+0x10/+0x14` pair a lock or a reference hold? If it is a lock, the worker holds it for the whole plan and any script read of that route waits; harmless while the Sim waits.

Not checked: the JobManager submit function (our own thread is simpler, so it is not needed), and whether the planner uses thread-local storage (step 1b tests it in practice).

## Async plan: thread-private scratch (option B) (2026-10-13, static analysis)

Question: after crash 3 (goal step outside `world+0x918`, free in `0x61C020`), can the worker get thread-private search objects so it never shares mutable routing scratch with the sim thread? Sources: Ghidra headless decompile/disassembly of `0x62EAB0`, `0x75C2D0`, `0x63F490`, `0x63D2F0`, `0x63D7B0`, `0x63CB90`, `0x6339F0`, `0x6320D0`, `0x664B10`, `0x6645D0`, `0x61BF60`, `0x61E320`, `0x61E360`, `0x61E030`, `0x61E8A0`, `0x61C020`, `0x62E420`, `0x829D50`, `0x82B810`, plus xrefs.

**Verdict: dead end as designed.** The "per-lot search object" is not scratch. It is a shared lot graph cache that is built lazily on first use. The A* scratch is one object for the whole game, and it stores each search's state inside nodes that point to shared lot cells. Those cells have non-atomic refcounts, and the search uses a global generation counter. A private A* object alone fixes only the `0x61C020` clear race. The worker would still need `world+0x918` around the goal step, and once it holds that lock, private scratch gains nothing. The smaller fix is a lock: every entry into the lot planner, on the sim thread and on the worker, takes `world+0x918` (the "routing gate" in PLAN). That leaves overlap only with sim work that does no routing.

### (1) What 0x62EAB0 returns, and the real scratch

| Object | What it is | Tag |
|---|---|---|
| **P**, lot path planner | One per routing world: `0x829BC0` returns `[world+0x95C]` if `world+0x974` is set. `0x75C2D0` gets it as `0x829BC0(0x8292D0())`. `this` of `0x63F490`, `0x63D7B0`, `0x63D2F0`, `0x6339F0`, `0x6320D0` | verified (`0x8292D0` = routing world getter is inferred) |
| `0x62EAB0` | **Find-or-insert** in a hash table at `P+0x48` (buckets `P+0x4C`, bucket count `P+0x50`, entry count `+0xC`), keyed by the 8-byte lot key. On a miss it builds a default lot entry on the stack (`0x62AA60`), inserts a copy (`0x62E680` -> `0x62E5F0`, may rehash via `0x625940`) and frees the temporary (`0x62B930`). Returns `entry+0x10`. It is not a search object, and on a miss it **writes the shared table** | verified |
| Lot graph (`entry+0x10`, about 0x180 bytes, ctor `0x62AA60`) | Allocator tags "Routing/PathPlanner/mPortalList", "mPortalsToAdd/Remove", "mQuadTrees", "LotConnectivity", "LotEdgeWaypointIndices", "LotEdgeFlags". `+0x8C` = lot built flag (set by `0x63CB90`), `+0x90/+0x94` = vector of 0x44-byte room records (`+0` built flag, `+0x24` portal index list, `+0x34` connectivity lists), `+0x74` = vector of per-level cell graphs L, `+0x16C` (word) = outside room index | verified for the fields used |
| **Q**, `cAStarSearch` | The **only** instance in the game: allocated with size `0x9C90` and constructed by `0x61BF60` (its only caller is `0x62E860`, the P init, called from `0x829900`, `0x82A810`, `0x834FC0`). Stored at `P+0xBC`, intrusive refcount at `Q+4`. Vtable `0xFEA5C0`, dtor `0x61E420`, deleting dtor `0x61EB20`. `Q+0x14` path list ("Routing/AStarSearch/mpPath"), `Q+0x18` bucketed open list ("Routing/OpenList"), `Q+0x24` iteration count (cap 64000 or 0x40002), `Q+0x28..+0x9C84` node hash table (10007 buckets) with end sentinel `+0x9C84`, count `+0x9C88` | verified |
| Nodes | Created on demand by `0x6320D0` -> `0x664B10` -> `0x6645D0` (find in the 10007-bucket table, else `0x61C2C0` creates and inserts). `0x664B10` takes the table from `[L+0x28]` of the cell graph, not from `P+0xBC`. Because `0x61BF60` has one caller, `[L+0x28]` is Q itself | table call verified; `[L+0x28] == Q` inferred (one ctor, same layout) |
| A* state | **Stored inside the nodes** (`0x61E030`): `+0x30` low word = search generation stamp, bits 20-21 = open/closed state, bit 22 = pinned position; `+0x1C` parent (refcounted via `0x61BB40`), `+0x20` g, `+0x24` f, `+0x28/+0x2C` entry point. A stamp mismatch resets the node through vtable `+0x3C` | verified |
| Generation stamp | Global word **`0x11580C4`**: incremented once per search in `0x61E360` (A* init), read in `0x61E030` and `0x61C9B0` | verified (3 xrefs) |
| Refcounts | Nodes: int `+0x28` (dtor through the vtable at `+0x24`); graph and cell objects: word `+4`. All plain `add`/`sub`, many inlined (for example `0x6339F0`, `0x664B10`, `0x61B030`, `0x61B1E0`, `0x61C020`) | verified |
| Q users | About 20 `mov reg,[P+0xBC]` sites in `0x625000-0x63FFFF` (`0x625x` 2, `0x627x` 3, `0x62Ax` 2, `0x62D/E/F` 3, `0x632x` 1, `0x633x` 4, `0x63Dx` 4, `0x63Fx` 1), plus `[L+0x28]` in `0x664B10`. Resets (`0x61E320`) from `0x63D2F0`, `0x63D7B0`, `0x62E420`, `0x635B70`, `0x625400` and `0x625420` (the last two called from `0x65E0D0`) | count verified by a pattern scan; the role of each site not checked |

Can the worker get its own Q? Mechanically yes: allocate `0x9C90` bytes with the EA allocator `0x4E3F90` and call `0x61BF60`; release through the `Q+4` refcount / `0x61EB20`. But a detour on `0x62EAB0` cannot redirect it, because that function returns the shared lot graph, not Q. Redirecting needs a patch at each of the ~20 `[P+0xBC]` loads and at `0x664B10` (`[L+0x28]`), each replaced by "worker ? private Q : original", plus a per-Q stamp instead of `0x11580C4` (3 sites). Cloning the lot graph per thread is not realistic: it is built lazily from world and lot data and rebuilt by world updates (see below).

What stays shared even with a private Q:

- **Lot cells and portals** that private nodes point to: the word refcounts at `+4` and the int refcounts at `+0x28` are changed without atomics from both threads. That is the bug class of crash 3: a lost update, then a premature free or a leak. Making them atomic means patching every inlined increment and decrement in the planner: dozens of sites, not counted.
- **Generation stamp** `0x11580C4`: without the per-Q patch, a search on one thread bumps the stamp and the other thread's nodes reset mid-search. The result is lost g costs and parents, so a wrong path or a search that runs to the 64000 cap; the memory is private, so no corruption [inferred effect].

### (2) Shared writes a plan makes beyond scratch

| Write | Where | Lock today | Tag |
|---|---|---|---|
| Lot table insert on miss (plus rehash) | `0x62EAB0` -> `0x62E680` -> `0x625940`, `P+0x48` | none in the goal step; callers under `PlanLocked` hold `world+0x918` | verified |
| Lazy lot build: rooms, portals, `entry+0x8C` built, `P+0x10E` dirty flag | `0x63CB90` (via `0x636330`, `0x635920`, `0x63CAF0`), first use of a lot | none in the goal step | verified |
| Lazy room connectivity: room record `+0` built, `+0x24/+0x34` lists; runs a room-to-room A* (`0x6339F0` -> `0x61E8A0` on Q) and resets Q | `0x63D2F0`, first use of each room | none in the goal step (`0x63F490` from `0x75C2D0`). Also reached from `IC_World_GetConnectedRoomsImpl 0x7929F0` -> `0x63DCB0`, and from `0x63F450`, `0x63D880`, `0x63DF60`, `0x640E00` (called from `0x65E0D0`, `0xC75920`) | paths verified |
| Q node table create and clear | `0x6645D0`/`0x61C2C0` (create), `0x61C020` (clear via `0x61E320`, `0x61E420`) | none | verified |
| Generation stamp | `0x61E360` | none | verified |
| Lot rebuild (all rooms, resets Q twice) | `0x82B810` -> `0x63D7B0`, from `0x82C820` (pending updates under `PlanLocked`), `0x63C690`, `0x657390`, `0xC6BCC0` (world manager) | the `PlanLocked` path holds `world+0x918`; the other callers not checked | partly verified |
| Lot removal (flushes lot streams, resets Q) | `0x829D50` (takes `world+0x918`) -> `0x62E420`, from `0xAB1EB0` | `world+0x918` | verified |
| Room graph refresh | `0x63D860` from `0x835110`, `0x835240`, `0xACE4A0` | not checked | xrefs verified |

EA has no lock of its own around P, Q or the lot graph. The only lock in this code is `world+0x918`, taken by the world-side entry points (`PlanLocked`, `0x829D50`) but not by the goal step or the script queries above. All of these are lazy caches: written on first use, then mostly read. Most plans therefore write nothing besides Q and the stamp. The rare writes (first plan into a lot or room, the first plan after a rebuild) are exactly the ones that corrupt memory when two threads hit them at once.

### (3) Invalidation

Lot graphs have no version counter. Invalidation means clearing the built flags (`entry+0x8C`, room record `+0`) and freeing and rebuilding the data in place (`0x63D7B0`, `0x63CB90`), plus Q resets (`0x61E320`) that drop every node. The only counter is the per-search stamp, which marks node state, not graph validity. So a private Q holding references to cells would see those cells freed under it by a rebuild on the sim thread, unless the worker holds `world+0x918` for the whole goal step and every sim-thread caller that rebuilds holds it too. Private copies cannot stay valid on their own.

### Effort and risks of option B

- Code: allocate and free a private Q per worker; patch ~21 load sites (`[P+0xBC]` and `[L+0x28]`) and 3 stamp sites; take `world+0x918` around the goal step on the worker; add the same lock to every unlocked sim-thread entry into P (`0x75C2D0`/`0x63F490`, `0x7929F0`, `0x63F6E0`, `0x633FA0`, `0x634500`, `0x635080`, `0x655EE0`, `0x65E0D0`, `0xC75920`, `0x835110`, `0x835240`, `0xACE4A0`); and make the refcounts on shared cells atomic (unbounded). Roughly 30+ patch sites before the refcount work.
- Remaining risks: refcount races on shared cells; the lock state of the rebuild callers not checked (`0x657390`, `0xC6BCC0`, the `0x63D860` callers); the world A* `0x676260` and the water JPS `0x620560`/`0x61FE10` keep their own state (not reviewed here); the role of each `[P+0xBC]` load not verified.
- Once the goal step must run under `world+0x918` anyway, a private Q adds nothing: the lock alone already serializes Q.

### Recommendation

Drop option B. If async plans are revived, use the gate instead: the worker takes `world+0x918` for the whole job (start points, goal step, `PlanLocked`), and the unlocked sim-thread entries into P listed above take it too. The lock is recursive, so nesting under `PlanLocked` is safe, and the existing `LockAcquireHook` pump serves the worker's managed calls while the sim thread waits. That is about 10 small detours and no change to EA data structures. The gain is then limited to sim-thread work that touches no routing lock, as PLAN already says; measure `asyncPumpUs` against `asyncUs` before investing more. The PLAN direction (cheaper searches: water JPS precompute, route budget, fewer replans) remains the better bet.

## H6 plan (2026-10-15)

Static design for PLAN lane H item H6 ("route search async") after H2 (docs/28: route search is the top offload candidate, 95/82/246 ms/s BP/SV/IP base, 108/77/184 with all fixes [measured]), H4 (docs/29: no C# may run off the sim thread [verified]) and H5 (`[HeightCache]` built, off). Acceptance follows the PLAN offload rule: stable (no crashes, no new stuck Sims, same results) and at most ~2% loss in sim iterations/s, measured as sim-thread route ms/s moved off. Sources: the sections above, docs/28, docs/29, and one Ghidra headless run on a copy of the h1 project (`game-copy/ghidra-h6`, script `LockReach` kept in the session scratchpad: forward BFS over direct calls, depth 9, from `IC_Route_Plan 0x765160`, the goal step `0x75C2D0` and the locomotion obstacle query `0x82EDB0`, listing every `Lock_Acquire 0x4E16F0` site with its `ecx` and the shared functions reached).

### Short answer

- Native async stays possible only with a **routing gate**: the worker holds `world+0x918` for the whole job, and every sim-thread entry into routing state that today runs without that lock takes it too. Private copies of the per-lot state were ruled out (option B above).
- So the gain is limited to sim-thread work that touches no routing state before the job ends. That window was never measured. **Measure it first (G11) with the probe that is already built, and build nothing until it clears the gate below.**
- Managed callbacks keep the 0.7.1 path (the whole host call `0xD75FA0` and the runtime_invoke slot run on the sim thread). The gate adds one rule: while the sim thread serves a callback for the job, it borrows the job's lock instead of waiting for it.
- First proof: a **Verify mode** (observe only) that counts entries into the planner's shared state made by a thread that does not hold `world+0x918`. Crash 3 had exactly this cause, and Verify must read 0 before async is switched on.
- **S0 (static, done 2026-10-15) changes two things.** (a) The planner is entered without `+0x918` from 74 `IC_*` entries and from native sim-tick and build-tool code, so a short list of mid-level gates cannot cover it. The gate moves down to the 10 planner leaf functions, plus 2 coarse gates (list in "S0 results"). (b) The plan reads the **physics loose quadtree** (`0x83B3A0`), and object moves on the sim thread write it with no lock at all. On the worker, `0x83B3A0` must be proxied to the sim thread, like the managed calls. Lock order: no reversed order found [verified, direct calls].

### (1) Shared state the planner touches

New from the Ghidra run [verified, direct calls]: the goal step `0x75C2D0` (outside `Route_PlanLocked`) takes `world+0x918` itself, but only in pieces: in `0x82EDB0` (`lea ecx,[esi+0x918]` at `0x82EDD0`) and `0x82F0B0` (`lea ecx,[edi+0x918]` at `0x82F0D0`). Between those calls it runs the lot planner (`0x63F490` -> `0x62EAB0`, `0x63D2F0` -> `0x61E320` -> `0x61C020`) with no lock. The locomotion obstacle query `0x82EDB0` reaches the same lot table `0x62EAB0` (via `0x631F10` -> `0x62FB10`) under its own lock. So the sim thread's per-tick routing touches the same data as a job.

| State | Address | Written by the plan? | Lock today | Gate treatment | Tag |
|---|---|---|---|---|---|
| Routing world graph, footprints, portals, pending updates | world (`0x8292D0`), `0x82C420` | pending updates applied at plan start | `world+0x918` | job holds it | verified (docs/18 Q2) |
| Lot planner P (one per world) | `[world+0x95C]` (`0x829BC0`) | yes: lot table insert and rehash on a miss, lazy lot/room builds | none in the goal step | job holds `+0x918`; sim-thread entries gated | verified (option B) |
| Per-lot table (the "per-lot search state") | find-or-insert `0x62EAB0`, `P+0x48` | yes, on a miss | none from `0x75C2D0`; `+0x918` from `0x82EDB0` | gate | verified |
| A* scratch Q (the only instance), node table | `P+0xBC`, `[L+0x28]`; reset `0x61E320`, clear `0x61C020` | yes, every search | none | gate (the crash 3 site) | verified |
| Search generation stamp | global word `0x11580C4`, `0x61E360` | yes, every search | none | gate | verified |
| Cell and portal refcounts | word `+4`, int `+0x28`, plain `add`/`sub` | yes | none | gate (one thread at a time is the only safe use) | verified |
| Route handle table | `0x74B2C0` (`Lock_Acquire` at `0x74B2E4`, `ecx` = table lock, docs: `0x1004124`) | no | own lock | unchanged | verified site |
| Route object, options, result | route `+0x158`, `+0xE0`; AddRef/Release `+0x10`/`+0x14` | yes (own route) | refcount | unchanged (owner Sim sleeps) | verified |
| Lot lookup / world lot manager | `0xC68680` (`lea ecx,[ebx+0x228]` at `0xC6869C`), reached from height `0xC6A540` and pending updates `0x82C420` -> `0xC68960` | no (read) | own lock | lock order `+0x918` -> `+0x228` | verified site; owner object inferred |
| Height raycast | `0xC6A540`/`0xC6A250` -> `0xC2A570` -> kd-trees; rebuild `0xC2A840` | no; our `[HeightCache]` writes its table here | none | worker bypasses `[HeightCache]` (calls the original) | path verified; "pure" verified (docs/28 N10) |
| Physics loose quadtree ("Physics/Realm/LooseQuadTree", not a routing structure) | read: `0x83B3A0` -> `0x682590` -> `0x681E00` -> `0x681CF0`. `0x83B3A0` has 3 callers: the goal step `0x75C2D0`, the planner `0x654CC0` (from `0x65E0D0`, inside `PlanLocked`) and `IC_RouteManager_IsOkToIdleAtCurrentLocation` | no (read); written by shape add `0x686ED0` -> `0x6823E0`, remove `0x686F50` -> `0x682080` and move `0x6871C0` -> `0x687130` -> `0x6819B0` (remove + reinsert), all on the calling thread | **none**: 264 callers of the writers, none inside a `+0x918` window | worker proxies `0x83B3A0` to the sim thread (S0 results) | verified (decompile, direct calls) |
| Lazy lot build helpers | `0x63CB90` -> `0x80EED0` -> `0x80EDC0` (`ecx` = `0x11D9068`), `0x807400` (`0x11D8F90`); `0x666C50` -> `0x665C50` -> `0x61ED80` (`0x11CEFA8`); `0x628840` -> `0x931210` (`ecx` = `[esi+0x10]`) | yes (lot build) | their own leaf locks: three free-list pools and a debug log writer (S0 results) | lock order `+0x918` -> these; none of them calls out while held | verified (decompile) |
| RoutingJob dtor path | `0x819AE0` -> `0x59A990`/`0x59A650`/`0x59A540` (three `Lock_Acquire` sites) | no | `0x59A990`: atomic refcount drop, then destroy (`0x59A650`) under the object's lock `[obj+8]` | unchanged | verified; `0x59A650`/`0x59A540` release in their callers, those windows not checked |
| Portal-lock callback into C# | `0x64F2C0` -> `0x791C00` -> `0x78F2B0` (World event 59) -> host `0xD75FA0` -> runtime_invoke slot `[0x1195B60]` / `0xEA1400` | managed: `LotManager.PortalLockedCallback`, allocates | none | proxied to the sim thread (0.7.1) plus the borrow rule | verified (docs/18 above) |
| Allocator | `0x4E3F90` -> lock wrapper `0x4E69D0`; `[Alloc]` thread cache | yes | own lock / per thread | unchanged | verified |
| World A\*, water JPS, `0x6255B0` | world A* `0x676260`, water JPS `0x620560`/`0x61FE10` (and our `[WaterJps]` precompute), `0x6255B0` | own state: no global writes and no `lock`-prefixed ops within 3 call levels; global reads only (allocator `0x11CB864`, `0x11587D0`, `0x114C900`) | A* and JPS: reached only through `PlanLocked` -> `Route_SearchAStar` (`+0x918`). `0x6255B0` (small leaf, reads `[arg+0x94]`): also reached without the lock from the goal step (`0x63F490`) and from `IC_World_FindPlacesOnShoreline` -> `0xC75920` -> `0x640E00` | covered by the job's lock and the leaf gates | verified (direct calls); "state is per call or per object" inferred |

Lock order (S0, all `Lock_Acquire 0x4E16F0` sites in the exe, direct calls) [verified]: no function holds another lock while it calls anything that reaches `+0x918` or the planner core. The two hits the scan reported were `+0x918` itself, set up with `add reg,0x918`: `0x818EA0` (world update, `add edi,0x918` at `0x818F36`) and `0x831530` (`add ecx,0x918` at `0x831559`). Every other lock reached from `IC_Route_Plan`, the goal step or `0x82EDB0` is a leaf: acquire, a short operation, release, no call in between that reaches routing. The order is therefore always `+0x918` first. Limits: indirect (vtable) calls are not followed; a held window is taken as acquire to the next release in address order.

### S0 results (2026-10-15, static)

Method: two Ghidra headless scripts on a copy of the h6 project (`game-copy/ghidra-s6`, scripts in the session scratchpad). (1) Every `Lock_Acquire` site in the exe, with `ecx` traced back through register moves. (2) For each non-`+0x918` site, the calls made before the next release, checked against every function that reaches `+0x918` or the planner core (reverse closure, depth 14). (3) A reverse walk from the planner core that stops only at call edges inside a `+0x918` window. Direct calls only.

**Lock order [verified].** No reversed order (see the paragraph above). 30 functions take `+0x918`: 28 via `lea`/`mov`, plus `0x818EA0` and `0x831530` via `add`.

**What the other locks guard [verified, decompile].**

| Lock (`ecx`) | Site | Guards | Calls out while held |
|---|---|---|---|
| `0x11D9068` | `0x80EDC0` | free-list pool of footprint polygon blocks (head `0x1165D48`, "Routing/UnnamedFootprintPolygon") | pool refill `0x7272E0` only |
| `0x11D8F90` | `0x807400` | free-list pool of 0x28-byte nodes (head `0x1165B80`); its vtable calls run before and after the lock | allocator only |
| `0x11CEFA8` | `0x61ED80` | free-list pool (head `0x11581DC`) | refill `0xC37710` only |
| `[esi+0x10]` | `0x931210` | debug log writer (`"%c%04x:%-.40s\n"`, on when flag `+0x56F` is set) | the log sink (vtable) |
| `[ebx+0x228]` lot manager | `0xC68680` | lazy rebuild of the lot manager's cell index (dirty flag `+0x1CC`, from getter `0xC68960`); acquire and release in the same function | own helpers (`0xC66700`, `0x810DF0`, `0xB7B9A0`, `0xC68080`, `0x8621C0`), none reach routing |
| `[obj+8]` | `0x59A990`/`0x59A650`/`0x59A540` | destroy on last release (RoutingJob dtor path) | none reaching routing |
| table lock `[ecx+8]` (`0x1004124`) | `0x74B2C0` | route handle table lookup (handles `0x25xxxxxx`) | one vtable call (`+0xCC`, lookup, not followed) |

All are thread-safe leaf locks, so the worker may take them while it holds `+0x918`.

**`0x82F0B0` [verified site, role inferred from tags].** Called by the goal step. Under `+0x918` it finds the lots of the start and goal points (`0xC68990` twice) and runs `0x631E20` ("Routing/PortalCheckCache", "Routing/GetFootprintsWithinNode/FootprintSet"), which collects the footprints and portals in the routing nodes between the two points into the caller's set. It is the goal step's footprint gather, taken with its own short lock hold.

**Who enters the planner without `+0x918` [verified, direct calls].** The reverse walk from the core (`0x62EAB0`, `0x61E320`, `0x61E360`, `0x61C020`, `0x63F490`, `0x63D2F0`, `0x63CB90`, `0x63D7B0`, `0x63D860`) finds 32 protected edges (inside `+0x918`: `PlanLocked`, `0x82EDB0`, `0x82F0B0`, `0x829D50`, `0x830940`, `0x831710`, `0x8321F0` and others), but 79 distinct unprotected callers of the core. Above them are 74 `IC_*` entries: all `IC_Route_Plan*`/`Replan*`, `IsPointRoutable`, `IsPointObstructedBySim`, `GetObstructionAtPoint`, `FindObstaclesForSimAlongRoute`, `RoutingBlockerCheck2d`, the `IC_RouteManager_*` portal and footprint lock-key setters, `IC_World_FindGoodLocation*`, `FindPlaceOnRoad*`, `TestObjectPlacement*`, `GetConnectedRooms`, `Slots_CanAttachToSlot*`, save, bulldoze and others. Native entries reach it too: the script tick (`MonoScriptHost_Simulate 0xD81840` -> `0x83CED0` -> ... -> `0x85F5A0` -> `0x62EF30` -> `0x62EAB0`), the world manager (`0xC6BCC0`, `0xC6B090`), and build/buy tool code (`0xB6D8F0`, `0xB57F20`, `0xCDxxxx`/`0xD1xxxx` chains). The option-B list (`0x63F6E0`, `0x633FA0`, ...) is a small part of this.

**Gate-site list for `[RouteGate]`.** Gate at the leaves, not at the entries. Each detour checks: no job running, or this thread already holds `+0x918` -> call the original (fast path). Otherwise take `+0x918` with the pumping wait, call, release. Jobs start only on the sim thread from a top-level `Route.Plan`, so no sim-thread planner sequence is in flight when a job starts. Waiting at the first leaf touch is therefore enough for mutual exclusion [inferred from the yield guard]. Prologues [verified bytes]:

| Site | Role | Prologue bytes | Stolen instructions (length) |
|---|---|---|---|
| `0x62EAB0` | lot table find-or-insert (every lot-planner caller goes through it) | `55 8B EC 83 E4 F0` | `push ebp; mov ebp,esp; and esp,-0x10` (6) |
| `0x63F490` | goal-step lot planner | `83 EC 54 8B 44 24 5C` | `sub esp,0x54; mov eax,[esp+0x5C]` (7) |
| `0x63D2F0` | lazy room connectivity | `55 8B EC 83 E4 F8` | `push ebp; mov ebp,esp; and esp,-8` (6) |
| `0x63CB90` | lazy lot build | `83 EC 5C 53 8B 5C 24 64` | `sub esp,0x5C; push ebx; mov ebx,[esp+0x64]` (8) |
| `0x63D7B0` | lot rebuild (all rooms) | `8B 44 24 08 55` | `mov eax,[esp+8]; push ebp` (5) |
| `0x63D860` | room graph refresh | `8B 44 24 08 8B 54 24 04` | `mov eax,[esp+8]; mov edx,[esp+4]` (8) |
| `0x61E320` | Q reset | `53 8B 5C 24 08` | `push ebx; mov ebx,[esp+8]` (5) |
| `0x61E420` | Q reset (clear path, from `0x61EB20`) | `53 56 8B F1 8B 46 18` | `push ebx; push esi; mov esi,ecx; mov eax,[esi+0x18]` (7) |
| `0x61E360` | search generation stamp | `66 83 05 C4 80 15 01 01` | `add word ptr [0x11580C4],1` (8, absolute address, relocates as is) |
| `0x61C020` | Q node table clear | `B8 70 9C 00 00` | `mov eax,0x9C70` (5; the `call` to the stack probe at +5 stays in place) |
| `0x75C2D0` | coarse: goal step (keeps the sync goal step atomic) | `55 8B EC 83 E4 F0` | `push ebp; mov ebp,esp; and esp,-0x10` (6) |
| `0x7929F0` | coarse: `IC_World_GetConnectedRoomsImpl` | `83 EC 14 53 33 DB` | `sub esp,0x14; push ebx; xor ebx,ebx` (6) |

Worker-side proxy (not a gate): `0x83B3A0`, `83 EC 10 53 55` (`sub esp,0x10; push ebx; push ebp`, 5). On the worker, run it on the sim thread through the same request slot as `ServeInvoke`. The sim thread serves it while it waits in the pumping acquire, and at that point it is not moving physics shapes. Gating the quadtree writers instead would stall the sim thread on every object move during a job.

**Not covered statically.** Reads of P or Q that do not pass through one of the leaves first (the ~21 `[P+0xBC]`/`[L+0x28]` load sites); other physics or routing reads reached through vtables; `0x682590` callers other than `0x83B3A0` (`0x82A8F0`, `0x824FA0`, `0x82E340`) were not traced from the plan. S2 Verify adds entry checks on all 12 gate sites and counts worker entries into `0x681E00` that do not come through the proxy.

### (2) Narrowest safe design: async with a routing gate

| Part | What it does | Crash it avoids |
|---|---|---|
| Job scope | Worker runs the whole original `IC_Route_Plan 0x765160` (as 0.7.1) inside our own `Lock_Acquire(world+0x918)` ... `Lock_Release`. The lock is recursive, so the inner acquires in `0x82EDB0`, `0x82F0B0` and `PlanLocked` nest | 3 (goal step outside the lock) |
| Gate sites (sim thread) | Detours take `world+0x918` around every entry into P that runs outside the lock today. After S0 these are the 10 planner leaves plus the coarse gates `0x75C2D0` and `0x7929F0` (the table in "S0 results"), with a fast path when no job runs or the thread already holds the lock. The option-B mid-level list is dropped: 74 `IC_*` and several native entries bypass it. While a job runs, the existing `LockAcquireHook` pump serves the worker's managed calls and the `0x83B3A0` proxy during the wait | 3 |
| Physics quadtree proxy (new, S0) | On the worker, `0x83B3A0` runs on the sim thread (same slot as `ServeInvoke`). Its writers (shape add, remove, move) have no lock and run on the sim thread every tick | a new race: quadtree node vectors reallocated under a worker read |
| Managed calls | Unchanged from 0.7.1: the whole host call `0xD75FA0` (boxing, task-id guard, invoke) and every runtime_invoke from the worker run on the sim thread; `ServeInvoke` saves, zeroes and restores host `+0xBD0/+0xBD4` there | 1 (NULL domain on the worker), 2 (task id zeroed under a running task) |
| Borrow rule (new) | While the sim thread runs `ServeInvoke` for the job, the worker is blocked in `InvokeHook`. A sim-thread `Lock_Acquire(world+0x918)` or gate entry there is counted as nested in the job: our hook skips the real acquire and the matching release (per-thread depth). The behaviour is as before, when the callback ran nested in the plan on one thread. Without this rule a callback that touches routing deadlocks | the deadlock risk listed in "Risks" above |
| Yield guard | Unchanged: `CanYield()` (current interpreter state == the task's root state), NOYIELD query, only `Route.Plan` from the Sim's own top-level task goes async | 2 |
| Our caches on the worker | `[HeightCache]`: the worker calls the original `0xC2A570` (the kd-tree read is pure). `[RouteCache]`, `[WaterJps]`: reached only under `+0x918` (inside `PlanLocked`), and under the gate only one thread holds it. `[Alloc]`: per thread already | new race in our own code |
| Scope limits | Jobs only in live mode with a world loaded; START refused during save, travel, build/buy and world unload (drain the job first, as CANCEL already does) | world teardown under a job |

Not chosen: private per-thread search state (option B: 30+ patch sites plus unbounded refcount work, and the lock is still needed); offloading only the world A\* `0x676260` (smaller job, but it runs inside `PlanLocked` under the same lock, so the window is the same, and its state was not reviewed); a try-lock locomotion check (rank 5 in Q3: raises the window, but Sims may overlap; keep it as a later step if G11 shows locomotion is what ends the window).

### (3) Gating measurement and acceptance

**G11, routing-lock free window** (measure before building; worker and async off):

- Per tick: the share of tick time in which no thread holds `world+0x918` and no gate site is running. Per synchronous plan: `gap` = time from the plan's return to the sim thread's next routing touch (an acquire of `+0x918` or a gate-site entry). The movable route time is `sum(min(plan length, gap))` per second, in ms/s, split by world.
- Tool: the `/routelock` probe (built 2026-10-13, never run; docs/15 phase 3) reports this for `+0x918` already. It does not see the unlocked gate sites, so its number is an upper bound. S2's native counters give the exact one.
- Probe cost fix (2026-10-16, after queue g11 failed: JS callbacks on the engine-wide `Lock_Acquire` slowed the sim to 18-26% of normal and hung the probe ~4 min in): the hot hooks are Frida CModule C callbacks. C filters `Lock_Acquire`/`Lock_Release` on the learned lock, keeps each thread's `+0x918` depth and gate depth in a TLS slot, and writes events (tid, kind/site, QPC, wait) to a 2^18-record ring that JS drains every 25 ms into the same `g11Tracker`. All 12 gate sites stay hooked (incl. `searchStamp`, `qNodeClear`). C folds what the accounting does not need one by one: a nested acquire is one record (count, wait), a nested release none, a gate entry under `+0x918` or inside another gate entry only counts per site (`entries`, `unlocked`, `unlockedSim`; never `unlockedOuter`). The G11 numbers are unchanged by this, except one edge [inferred, not seen]: if a sim-thread `IC_Route_Plan` returned while its thread still held `+0x918`, a folded gate entry under that hold no longer ends the window (the next acquire, unlocked gate entry or plan does). Report `ring` = `{events, lost, folded, maxBacklog, drainMsPerSec}`; `lost > 0` makes the queue verdict FAIL. Dummy test `python tools/probe/routelock-test.py` [verified on a 32-bit dummy]: ~0.4 us per hooked call with 2 threads contending (the old JS callbacks: ~14 us), drain capacity ~370k records/s, 0 lost at 400k hooked calls/s.
- Go to async only if the movable time is **>= 30 ms/s in IP and >= 15 ms/s in BP** (the docs/18 rule of thumb). If locomotion ends most windows, do the try-lock obstacle check together with async, or drop H6 for cheaper searches (PLAN direction).

**Acceptance (async run, `--ab routeasync` in one load per world, BP/SV/IP):**

- Moved off: sim-thread route ms/s with async on vs off (`stutter-profile` route timer), cross-checked as `asyncUs - asyncPumpUs` (job time minus the time the sim thread waited for it). It must be > 0, and the target is the G11 estimate.
- Stable: 0 crashes, 0 interp.c asserts, 0 watchdog lines, 0 Verify violations (`gateUnlocked`), no new stuck Sims (log only), plan failure rate `planOnFails/planOnCalls` within noise of `planOffFails/planOffCalls`, `asyncDropped` explained by resets.
- Speed: iterations/s at most ~2% below off. A larger loss needs a plan to win it back (PLAN rule).

### (4) Steps

| Step | What | Effort | Risk |
|---|---|---|---|
| S0 | **Done 2026-10-15** ("S0 results"): no reversed lock order; gate moved to 12 leaf and coarse sites; physics quadtree writers have no lock, so the worker proxies `0x83B3A0`; `0x82F0B0` is the goal step's footprint gather | 0.5 d | none (Ghidra only) |
| S1 | Run `/routelock` (worker off) on Baseline-BP and Baseline-IP, 10 min each, for the G11 upper bound: `node tools/harness/queue.ts tools/harness/queues/g11.json` (run kind `routelock`; the report's `g11` block also counts unlocked entries at the 12 S0 gate sites and ends the window there; job = the whole `IC_Route_Plan`). Probe safety: try the hooks on a dummy first; `Lock_Acquire` is engine-wide, so read `acquireHooksPerSec` and compare it with the timer | 0.5 d, one game slot | low (Frida cost may skew the timers) |
| S2 | **Verify mode** `[RouteGate] Verify=1` (ts3native, observe only): `Lock_Release 0x4E17B0` detour plus owner/depth tracking for `+0x918` in `LockAcquireHook`; entry checks on the 12 S0 gate sites count `gateUnlocked` (thread, top return addresses) when the caller does not hold `+0x918`; a worker-thread check on `0x681E00` counts quadtree reads that do not come through the proxy; exact `gap` counters for G11. Run with worker Mode 0 and Mode 1 (sync). Expect non-zero today (the goal step); that list fixes the gate sites. **S2a built 2026-10-15, untested in game** (ts3native 0.10.3, `[RouteGate] Enabled=0`, `native/ts3native/routegate.cpp`): owner of `+0x918` from `LockAcquireHook` (success returns the recursion count) and a `Lock_Release` detour (owner cleared before the outermost release); pass-through stubs on all 12 S0 sites count per site the outermost entries (no other gate frame open on the thread; the stub swaps the return address, dead frames are dropped) by a thread that does not own `+0x918` (`rgUnlocked`), and the same during a worker or async job (`rgJob`); counters v16 at `+0x204`, first 32 site/caller pairs logged; all 14 prologues checked first, nothing installed on a mismatch; selftest covers recursive holds, outermost-only, another thread's hold, jobs, dead frames, a full frame stack. **S2b (open):** the worker-thread `0x681E00` quadtree read check and the exact G11 `gap` counters (time from a plan's return to the sim thread's next routing touch) | 1-1.5 d + selftest | low (no behaviour change) |
| S3 | Gate on, still sync (`[RouteWorker] Mode=1`): job lock around `IC_Route_Plan`, gate detours, borrow rule, `[HeightCache]` bypass on the worker. Pass: Verify `gateUnlocked` = 0 during jobs, same plan results and counters as Mode 0, 0 crashes over a 10 min soak per world | 1 d | medium (deadlock from lock order or a callback) |
| S4 | Async on (`[RouteAsync]` 0.7.1 path + gate). Repros: `bp-route-async`, `bp-overwatch-stuckcheck`, Sim reset during a job, save and travel requested during a job (START refused, job drained) | 1 d | medium-high (the window, stale paths, unknown unlocked state) |
| S5 | `--ab routeasync` on BP/SV/IP, apply the acceptance rule, record in the change ledger and PLAN | 0.5 d, three game slots | low |

Total about 4.5-5 days, with a stop after S1 (G11 too small) and after S2 (more unlocked entries than a short list of detours can cover).

### Top risks

1. **Small window [inferred]:** locomotion takes `+0x918` every tick for each walking Sim, and other script tasks in the same script phase route too (footprints, `IsPointObstructedBySim`, plans). The job may only overlap with the rest of that phase. S0 makes this more likely: 74 `IC_*` entries (placement tests, slot checks, obstruction queries) and the script tick reach the gated leaves, and each one ends the window. G11 decides this before any build, and its gap counter must count entries at the 12 S0 sites, not only `+0x918` acquires.
2. **Deadlock:** a callback served on the sim thread that takes a lock the worker needs [inferred], or a caller that takes a second lock before `+0x918`: none on direct-call paths, and the other locks are leaves [verified, S0]. Vtable calls were not followed. The borrow rule covers the callback case. The 2 s watchdog only logs.
3. **Unlocked state:** the physics quadtree has no lock and is written by every object move [verified, S0]. The worker must proxy `0x83B3A0`, and Verify must show no other worker reads of `0x681E00`. World A\* and water JPS are reached only under `PlanLocked` and write no globals [verified, direct calls]. P/Q reads that skip the gated leaves are still possible (S2 can only see entries).
4. **Stale results:** a plan reflects the world when its job started and is used 1+ ticks later. Route following already replans when a path goes stale [inferred].
5. **Our own detours:** `[HeightCache]` and any later cache on routing paths must be worker-aware. S3 bypasses the height cache on the worker.

### Verified vs inferred

- Verified: lock sites and `ecx` values listed in (1) (Ghidra, direct calls, depth 9); the goal step runs the lot planner between its own short `+0x918` holds; locomotion reaches the lot table under `+0x918`; height and physics quadtree reached from the goal step and from the planner (`0x654CC0`); S0: no reversed lock order on direct calls, the other locks are leaf pools/log/table locks, the planner leaves are entered without `+0x918` from 74 `IC_*` and native entries, the quadtree writers run with no lock, gate-site prologue bytes; the 0.7.1 managed-call path; the three crash causes (sections above, PLAN).
- Inferred: lock order also holds through vtable calls; the borrow rule is safe (the worker is blocked for the whole served call); the size of the window; that height reads are safe on the worker while the cache is bypassed (pure kd-tree reads; rebuilds happen only outside live mode).
