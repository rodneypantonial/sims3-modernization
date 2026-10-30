# 206. Worker window options: where the `[AutShadow]` worker can run

Written 2026-10-30. Static analysis and offline tools only: no game run, no queue. **Option 2b built 2026-10-30 (ts3native 0.36.125, `[AutShadow] Satb`, off): §7; S0 probe queue `satb-s0.json` not run.** It answers the go-live finding of docs/122 §14.20 (commit f24fcd58): the worker reads managed state in place, which is allowed only in the quiescent gap between a `MonoScriptHost_Simulate` iteration's sim pump and the next iteration's first script stage. That gap is 8.4-10.2 us on average (max 45-56 us), while a worker job takes 3.3-4.6 ms (BP/IP) [measured, mono-cost11]. The post-to-resume span is a whole tick (~26-32 ms), but tick t+1's script tasks write managed objects the worker reads. Rodney asked for three options, each with a verdict, numbers, cost, exactness and risks, and one recommendation.

Inputs: docs/124 (§2 timeline, §3.4, §14; there is no §15/§16 on master yet, docs/122 §14.20-§14.21 point at a planned §15), docs/122 §14.20-§14.21, docs/123 (world image), docs/121, docs/126, docs/127 (closures), docs/131 (glue race set), docs/41 and docs/36 (frame and sim loop), docs/82 and docs/143 (locomotion), docs/22 (animation), docs/61 and docs/73 (events), docs/160 and `probe-out/queue/simbudget-v024-report.txt` (sim budget, v0.2.4 release, 2026-10-29), docs/57, docs/198 and docs/30 (GC), docs/130 and `docs/data/mint-opcodes.csv` (interpreter). New tool: `tools/worker-read-set.ts` -> `docs/data/worker-read-set.csv`. Disassembly with capstone over the Steam 1.67 `TS3W.exe` copy in `game-copy/bin`; managed code from the decompiled class library in `game-copy/decompiled` (read, not copied).

Tags: **[verified]** = seen in the disassembly, the decompiled class library or a measurement named here; **[inferred]** = reasoning still to confirm (ledger docs/51 §398, I-398-*); **[design]** = a choice made here. Effort in CLAUDE.md units: wall-clock hours for Claude with agents; game jobs listed apart.

## Summary

| # | Option | Window it gives | Extra sim-thread cost | Exact? | Build | Game jobs | Verdict |
|---|---|---|---|---|---|---|---|
| 1 | Native-only span inside the iteration | BP ~3.3 ms, IP ~2.2 ms, SV ~1.4 ms per tick at most, and it ends at the first managed callback | ~0 | yes, with stop-on-callback | 4-8 h | 2-3 | **reject**: shorter than the job on IP/SV and not callback-free on any world |
| 2a | Full copy of the read set at the cut | post-to-resume (~26-32 ms) | 2-6 ms per cut (instance walk) on top of today's 0.7-1.3 ms | yes for the copied fields, identity and address hashes break | 20-30 h | 6-8 | **reject** |
| **2b** | **Copy-on-first-write (undo log) from the cut to the worker's end** | **post-to-resume (~26-32 ms)** | **~1-2.5 ms/s at Sample=1** [inferred] | **yes by construction** if every post-cut writer logs first; checked by Mode R and an audit mode | **14-24 h** (+3-5 h audit) | **6-10** | **recommended** |
| 3 | Copied heap / copy-on-write pages | post-to-resume | per-cut process clone tens of ms, or 1.5-30 ms of page faults per job | yes | 20-35 h | 4-6 | **reject**: costs more than the ~4 ms a job moves |

- **Option 1 is not a managed-free span** [verified]. Inside one iteration the native span (SACS pass, services, pre-locomotion step, `Locomotion_Update`, `Animation_UpdateActors`) calls C# synchronously: route events 57/58 (`OnRoutingEvent`, `ObstaclesEncountered`) from locomotion through `World_FireEvent`, and every SACS event push through the script host's delegate slot into `EventQueue.OnNativeEvent`, from both the SACS pass and the pre-locomotion step. The second one refutes docs/22's "C# drains that queue later" (I-398-1). Route events return a bool the native caller uses, so they cannot be deferred; the guard (stop at the first managed entry) is exact but leaves only the gap before the first callback.
- **Option 2b** keeps today's in-place reads and makes them see the cut. While a job runs, the sim thread logs the old value of every managed word it is about to overwrite (35 store opcodes in 24 mint handlers, a handful of runtime and IC writers), and the worker's 42 load opcodes (29 handlers) plus the 13 superinstructions that contain a load check the log after the live load. The handlers are swapped into the jump table at the cut and back when the worker ends, so the sim pays the barrier only during the ~5 ms a job runs, ~28 ms of every second at Sample=1. The window becomes the whole post-to-resume span; overruns go to ~0 at today's job times (max 5-9 ms).
- **Option 3 fails on cost, not on design.** Two views at the same address need two processes (one address = one mapping per process), a process clone per cut costs tens of milliseconds plus a copy-on-write fault for every page the sim then writes, and an in-process page-protect scheme pays ~3-10 us per first write to each page. Write watch is already on (Boehm sections are `MEM_WRITE_WATCH`, docs/198) but it is owned by the incremental GC and only tells which pages changed afterwards; page-level conflicts with the worker's reads are expected in nearly every job. docs/123's world image stays the right tool for offline fixtures and for the 2b audit, not per tick.
- **Read set of the worker** (`docs/data/worker-read-set.csv`): the pilot union closure (950 methods) reads 510 fields, 275 of them written outside constructors and load code (246 instance fields on 109 types, 29 statics: Sim, SimDescription, Autonomy, Lot, Occupation, Household, `MetaAutonomyTuning`, `LotManager.sLots`, `SimClock`, `RandomGen.gRandomGen` ...). The class-hierarchy upper bound (every override within 5 calls, 22,329 methods) reads 8,637 fields, 3,563 mutable; 7,467 game methods write one of those. A static read set is therefore no basis for a barrier filter; 2b logs by address at the opcode level, so mods and virtual dispatch are covered without it.
- **Recommendation:** build 2b, after one cheap measurement job that counts the sim's stores and distinct words between cut and worker end (the barrier's real cost); keep `WindowWaitUs` as the Mode R test path until 2b passes Mode R with the sim running concurrently. Next step and gates in §5.

## 1. The window problem in numbers

| Quantity | BP | IP | Source |
|---|---|---|---|
| Quiescent window (pump to next Simulate entry), mean / max | 8.4-9.5 us / 45-56 us | 8.9-10.2 us | docs/122 §14.20 [measured] |
| Worker job, mean | ~3.3 ms (mono-cost6 2.9) | 4.36-4.59 ms | docs/122 §14.20, docs/124 §14.2 [measured] |
| Tick (post-to-resume span) | 28.9 / 31.9 ms | 26.5 / 29.1 ms | docs/122 §14.20 [inferred from `simTicks`] |
| Requests at Sample=4 | 835 per 10 min (1.39/s) | 856 (1.43/s) | mono-cost6 [measured] |
| Requests at Sample=1 (go-live) | ~5.6/s | ~5.7/s | x4 [inferred] |
| Net today with the wait (Mode R) | -10.2 / -10.9 ms/s | -6.6 / -5.1 ms/s | `mono-cost-report.ts` worker-window line [inferred] |

Any option has to give the worker at least the job length between the cut and the point where the sim needs the answer (the manager task's resume, docs/124 §2.2), without the worker reading anything the sim wrote after the cut.

## 2. Option 1: a native-only span inside the iteration

### 2.1 Order of one sim iteration [verified, disassembly of `0xD81840`]

`MonoScriptHost_Simulate 0xD81840` is the sim thread's loop (loop-back `0xD81FF6` -> `0xD818C0` while `host+0xC08 == 1`). One iteration, step path, by call site:

| Call site | Callee | Kind |
|---|---|---|
| `0xD818FF` | `ScriptHost_ScriptTick(0)` `0xD80BC0` | script |
| `0xD81948` | `ScriptHost_TaskObjects` `0xD7FC60` (Task-type script tasks) | script |
| `0xD819AA` | `GC_try_to_collect` (incremental step, ~1 ms budget) | GC |
| `0xD81A09` / `0xD81A30` | `mono_gc_invoke_finalizers` | script (finalizers) |
| `0xD81A41` | `0x769420` | native |
| `0xD81A70` | `Animation_SACSPass 0x571AD0` | native, **calls C#** (§2.2) |
| `0xD81A81` | sim frame begin `0x76C5C0` | native |
| `0xD81AA2` | `ServiceList_SimUpdate 0x59ED70` (UTFWin visuals, audio, camera, UI service) | native, C# through UI callbacks [inferred] |
| `0xD81AF7` / `0xD81B16` | pacing sleeps (pause path) | idle |
| `0xD81BB5` | `0x76C5E0` | native |
| `0xD81C45` | pre-locomotion step `0x7FD520` | native, **calls C#** (§2.2) |
| `0xD81C5C` | `Locomotion_Update 0x83CED0` | native, **calls C#** (§2.2) |
| `0xD81C73` | `Animation_UpdateActors 0x571B90` | native, reaches the script host (§2.2) |
| `0xD81C8E` | `ServiceList_SimUpdate` | native |
| `0xD81CF5` | `ScriptHost_ScriptTick(n)` | script |
| `0xD81D06` / `0xD81D0D` | `OneShotObjects 0xD74800`, `ThreadedObjects 0xD7F8B0` (Sim tasks: interactions, autonomy) | script |
| `0xD81D14`, `0xD81D29` | `0xD7FE80`, `ScriptHost_RunCatchUpSteps 0xD70730` | script/host |
| `0xD81DF5` | sim frame publish `0x76C8A0` | native |
| `0xD81FC1`, `0xD81FDA` | `[host+0xB78]` callback, sim pump (cut site 2, docs/122 §14.21) | native, jobs |
| `0xD81FDE` | `IdleSimulationCycle 0x7694B0` (frame limiter) | idle |

This matches docs/82 §"Sim iteration order" and docs/36 §3.1. The only native stretch long enough to matter is **`0xD81A41` .. `0xD81CF5`** (after finalizers, before the second script stage). The tail after `ThreadedObjects` (publish, pump, limiter) is the ~10 us gap that docs/122 measured; on this build the limiter rarely sleeps because the sim is the bottleneck.

### 2.2 Does the span run managed code or write managed objects?

**Yes, synchronously, on every world.**

1. **Route events from locomotion [verified].** `World_FireEvent 0x78F2B0` looks the event up in a 36-byte-per-event table at `0x11D6A78`; if a script delegate is registered it calls the script host's world object slot `+0x134` (call at `0x78F2FC`), else a method lookup (`0x7896D0`) and slot `+0x138` (`0x78F363`) [verified disassembly]. Both slots resolve to `ScriptHost_InvokeDelegate 0xD75FA0` and `0xD7D2F0`, i.e. into Mono (docs/102 static, docs/106 live). Locomotion fires events through the agent vtable `+0x64` (`0x8476AD`, in `ObstacleAvoidance`) and `+0x60` (`0x9573DB`, in `Locomotion_UpdateAgent`), which reach `World_FireEvent` for events `0x39` and `0x3A` (docs/82 §1 [verified there]). In `SimIFace` the `WorldEvent` enum puts `OnRoutingEvent` at 57 (`0x39`) and `OnRoutingEvent_ObstaclesEncountered` at 58 (`0x3A`); `World` registers both with `RegisterCallbackDelegate`, and `LotManager` hooks `OnRoutingEventHandler` / `OnRoutingEvent_ObstaclesEncountered_Handler`, which allocate an event object and call the Sim's `RoutingComponent.RoutingDelegate` / `RoutingEncounteredObstaclesDelegate` [verified, decompiled class library]. Those delegates write routing-component and route-follow state (docs/141 §4 row 2: replans start there). Both handlers **return a bool** that the native caller uses.
2. **SACS events from the SACS pass and the pre-locomotion step [verified].** A direct-call walk (6 hops, scratch script) from `Animation_SACSPass 0x571AD0` and from `0x7FD520` reaches `0x742710` via `0x94E9C0` (SACS driver update) -> `0x804580` -> `0x801D90`. `0x742710` is the walker event-queue push of docs/22 §3 row 1b. When the queue's script delegate (`[this+8]`) is set, it builds four arguments and calls the same script host slot `+0x134` (`0x7428B0`). On the managed side that delegate is `EventQueue.OnNativeEvent` (registered by `EventQueue_CreateEventQueueImpl`): it allocates an `Event`, runs the permissive (synchronous) handler if one is set, then the `OnEvent` handler or `Queue.Enqueue` [verified, decompiled `ScriptCore.EventQueue`]. `StateMachineClient` installs the synchronous handler when any synchronous SACS handler exists; 5 gameplay source files register one [verified: grep]. So every script clip event writes the managed heap from inside the animation pass. **This refutes docs/22's inference** "C# drains that queue later": the push itself calls C# (I-398-1).
3. **Rig pose path [verified call, slot not read].** `Animation_UpdateActors 0x571B90` -> rig pose `0x940430` -> `0x5813C0` -> `0x581270` -> `0x5809D0` calls `ScriptHost_GetWorld 0x7692B0` and then a slot of it (I-398-2).
4. **Services [inferred].** `UIService_SimUpdate 0x7F1530` calls the window manager on the sim thread (docs/43); `UIManager.SetUICallbacks` registers managed delegates for window events, and docs/43 attributes ~115 of 150 UTFWin samples to calls that come in through the UI service slot or managed callbacks. Not traced to a C# call here.
5. **Native writes into managed objects without a callback: none found.** EA's only field-store helper `0xE69820` has two callers, `0xD79AF0` and `ScriptHost_InvokeDelegate` (docs/118 D11 [verified there]); object positions are not mirrored into managed fields, scripts read them through ICs [inferred]. The span's managed writes come through the callbacks above.

`stall-split.ts` already groups these managed stacks as the owner "native event callbacks" (`OnNativeEventDelegate`, `OnRoutingEvent`, `OnObjectLotUpdate`, `OnPortalLocked`, `UIEventCallback`): 30.5 / 24.1 / 27.6 ms/s on BP / IP / SV in the v0.2.4 budget [measured]. Which share of that runs inside the native span is not split in any run [to measure].

### 2.3 Span length per world [inferred from the v0.2.4 sim budget]

Span per iteration ~ (locomotion + animPose + animSacs + the steering and avoidance key waits taken inside locomotion + services) / it/s:

| World | it/s | loco + anim (ms/s) | locomotion key waits (ms/s) | services (ms/s, docs/36 ~14 incl.) | **span per tick** | job | fits? |
|---|---|---|---|---|---|---|---|
| BP | 46.3 | 69.3 + 48.0 + 0.1 | 18.4 + 7.1 | ~10 | **~3.3 ms** | ~3.3 ms | about half the jobs |
| IP | 61.2 | 58.2 + 31.3 + 0.1 | 34.5 | ~10 | **~2.2 ms** | 4.4-4.6 ms | almost none |
| SV | 91.4 | 56.9 + 26.8 + 0.2 | 27.9 + 4.2 | ~10 | **~1.4 ms** | (not measured) | none at IP's job size |

Per-tick native work does not shrink when Mode R lengthens the tick, so these per-tick values also hold at mono-cost tick rates [inferred]. Catch-up iterations repeat the step path and give longer spans, but they are the frames that are already late.

### 2.4 Moving the cut to the start of the span

Structurally it works [inferred from verified order]. Autonomy searches run in Sim tasks inside `ThreadedObjects` (docs/64: autonomy search 7-11% of ThreadedObjects samples [measured there]), i.e. after the span. A request posted at B13 in `ThreadedObjects` of tick t would be cut at `0xD81A41` of t+1 (after `ScriptTick(0)`, `TaskObjects`, GC and finalizers), the worker would run across the span and must be stopped at `ScriptTick(n)` `0xD81CF5`, and the manager task resumes in `ThreadedObjects` of t+1, after the span, where docs/124's consumer runs. The cut would even be fresher than today's (it includes t+1's `TaskObjects`). `0xD81A37` is an S3SS inline patch site and `0xD81C5C` belongs to `[LocoWindow]` (docs/122 §12.4 hook owners), so the new cut needs a callee-entry hook, not an inline patch in the loop [inferred, `hook-sites.ts --check` before building].

### 2.5 Defer or guard the callbacks?

- **Route events cannot be deferred**: their bool return feeds the native follower in the same call. **SACS events** could be queued natively and replayed before `ScriptTick(n)`, but only if no synchronous handler exists on that client (5 source files register them) and nothing native in the span depends on the managed side effect; the order against other native events would change. Not exact in general.
- **Guard = stop.** The existing `InvokeHook`/`HostCallHook` stop flag (docs/122 §10 risk 3) stops the worker at the first managed entry in the span. That is exact (the worker never reads after a managed write), but the usable window becomes "cut to the first callback". With several walking Sims and animated interactions per tick, at least one route or SACS event per tick is likely [inferred, I-398-3], so most windows would be well under the span.
- A "wait for the worker before each callback" variant makes the sim pay the worker's remaining time, which is the Mode R wait again.

### 2.6 Verdict

**Reject.** Even the full span (BP ~3.3, IP ~2.2, SV ~1.4 ms) is shorter than the job on IP and SV, and the span is not managed-free: the first route or SACS event ends it. Exact only with stop-on-callback, which makes it short. Cost if built anyway: move the cut (callee-entry hook at the span start), stop at `ScriptTick(n)` and on any `InvokeDelegate`, `KeepPartial` per meta object: ~4-8 h, 2-3 jobs (one probe job to count managed entries per span would settle I-398-3 first). Expected net: near zero on IP/SV, partial on BP. Risks: hook collisions inside `0xD81840` (S3SS, `[LocoWindow]`, `[MonoThread]` slot stub), and the worker running while locomotion moves native state, which the cut-time IC rows already isolate.

## 3. Option 2: the worker sees only cut-time managed state

### 3.1 What the worker reads [verified as a static result, `tools/worker-read-set.ts`]

The tool takes the docs/126 pilot union (allow-list v0 definitions, the 7 `HowMuchItWantsSimToCome` implementors, the meta base path; depth 12, the pilot cuts) and a class-hierarchy run that adds every game override within 5 calls of a root (docs/127's CHA bound), collects every `ldfld`/`ldflda`/`ldsfld`/`ldsflda` of the closure from the call-graph index, and classes each field by its writers in the whole index: `no-IL-writer` (native, reflection or persist loader only), `ctor-only`, `load-time` (constructors, class constructors, load/parse/startup methods by name [inferred]) or `mutable`.

| Run | Methods | Fields read | Mutable (instance / static) | ctor-only | load-time | no-IL-writer |
|---|---|---|---|---|---|---|
| Pilot union (base) | 950 | 510 | 246 / 29 on 109 types | 192 | 21 | 22 |
| CHA upper bound | 22,329 | 8,637 | 3,198 / 365 on 1,567 types | 4,694 | 223 | 157 |

Base mutable fields by type (top): `Autonomy` 17, `SimDescription` 13, `Sim` 11, `MetaAutonomyTuning` 11, `GameObject` 9, `Lot` 9 (+7 in `Lot+SavedData`), `Occupation` 8, `Household` 7, `MiniRelationship` 5, `MiniSimDescription` 5, `Dictionary`2` 5; statics include `LotManager.sLots`/`sWorldLot`, `MetaAutonomyManager` maps, `Household.sNpcHousehold` and siblings, `SimClock.sTicksAdvanced`, `RandomGen.gRandomGen`, `AutonomyRestrictions.sLevel`. The CHA closure also has 1,360 array element loads, 727 `ldlen` and 5,074 calls into collection getters (enumerators, `List.get_Count`, `Dictionary.TryGetValue`). 7,467 game methods write a mutable field of the CHA set (Sims3GameplaySystems 4,170, Sims3GameplayObjects 2,262, StoreObjects 396, SimIFace 195, UI 174, mscorlib 167). The docs/127 typed closures per definition are 796-1,030 methods (v0), so the real set sits between the two rows, nearer the base [inferred].

### 3.2 Variant 2a: copy everything the worker might read at the cut

A conservative copy has to cover every instance of the read types the worker could reach: all Sims and their descriptions, autonomy and occupation objects, all lots (BP 82, IP 119), every `GameObject` the meta search can touch, the relationship and household tables, and the collection arrays behind them. The mutable bytes are small (~0.1-1 MB [inferred]), but finding them is a pointer walk over thousands to tens of thousands of objects at ~100-300 ns per cache miss: **~2-6 ms per cut** [inferred], against a cut that today costs 0.7-1.3 ms and has to stay at <= ~25% of the scoring it moves (docs/124 §14.3a). Two ways to make the worker read the copy, both bad: shadow objects at new addresses break identity, `ReferenceEquals` and the address-based hashes (docs/123 N5); an address-keyed redirect keeps identity but still pays the full walk every cut. **Reject.**

### 3.3 Variant 2b: copy on first write (snapshot at the beginning) [design]

Instead of copying what the worker might read, the sim thread saves what it overwrites, and only while a job runs.

- **Log.** A fixed open-addressing table keyed by the 8-byte-aligned address, value = the 8 bytes before the first post-cut write; one page bit per 4 KB of Boehm heap (~94k pages for ~368 MB, ~12 KB bitmap) marks pages with entries. Single writer (sim), lock-free readers (worker): the entry's value is written before its key. Sized for the measured worst case plus margin (e.g. 64k entries, ~1 MB); overflow marks the job stale (fallback to the game's B13), never a wrong answer. Cleared per job (page bits and used slots).
- **Sim-side barrier (log, then store).** Every store path that can change cut-reachable managed memory after the cut:
  - interpreter: the 35 store opcodes in 24 mint handlers (`stfld.*`, `stsfld`/`.vt`, `stind.*`, `stelem.*` incl. `stelem.any.vt` and `stelem.ref`, `stobj`/`.vt`, `initobj`, `cpblk`, `initblk`; `docs/data/mint-opcodes.csv`) [verified list]; none of the 22 EA superinstructions stores to the heap (their stores are `stloc` / `stinarg`) [verified, superop sequences]; `ldflda`/`ldelema` produce addresses, so their writes arrive through `stind`/`stobj` (heap-range check there);
  - mscorlib internal calls that write existing objects: `Array.Copy`/`FastCopy`, `Array.Clear`, `Buffer.BlockCopy`, `Array.SetValue*`, `Interlocked.*` (list from `game-copy/analysis/internal-calls-map.csv` at build time) [design];
  - EA internal calls with `ref`/`out`/array arguments that point into the GC heap: the existing guard thunks (17 ns per call, docs/124 §14.2) check the argument addresses against the heap sections and log the ranges before the body [design];
  - EA's field-store helper `0xE69820` and `ScriptHost_InvokeDelegate`'s argument writes (new boxes only, kept for completeness) [design];
  - not needed: the GC (it writes headers, kept outside the heap sections, docs/198 §3.2, and free blocks, which no cut-reachable path reaches while the log keeps old pointers alive), allocation (fresh memory is not cut-reachable), strings (immutable).
- **Worker-side read (live load, then log).** The 42 load opcodes in 29 handlers (`ldfld.*`, `ldsfld*`, `ldelem.*`, `ldind.*`, `ldobj`) and the 13 superinstructions that contain a load (`0x1AB`, `0x1AD`, `0x1AF`, `0x1B0`, `0x1B3`, `0x1B4`, `0x1B6`, `0x1B7`, `0x1BB`, `0x1BC`, `0x1BE`, `0x1BF`, `0x1C0`) [verified list] do the original load, then check the page bit, and on a hit look the address up and take the logged value. Runtime code that reads arrays natively on the worker (`Array.Copy` from `List.ToArray`/`CopyTo`) patches the copied range from the log afterwards.
- **Swap only while a job runs.** The mint jump table is shared by both threads and can be rewritten entry by entry while code runs (docs/139 summary, rank 2). At the cut the sim swaps in the barrier and redirect handlers; each handler branches on the thread (sim: barrier on stores, plain loads; worker: plain stores to its own objects, redirect on loads); at the worker's end the original entries go back. Outside jobs the cost is zero.
- **Ordering [inferred, x86 TSO, I-398-4].** The sim writes the log entry, then the field; the worker loads the field, then the log. Stores become visible in order and loads are not reordered with older loads, so a worker that sees the new field value also sees the log entry; one that sees the old value had the cut value anyway.
- **GC.** The log holds old pointers, so its table is pushed as a root by the P2 root-scan hook (docs/30 §2) [design]; objects that became unreachable after the cut stay alive until the job ends. Boehm stops the worker in a collection like any registered thread (docs/30 §1: `GC_stop_world` calls `SuspendThread` per thread-table entry), so the sim's per-tick GC step can run during a job.
- **Native state.** Unchanged from docs/122: every IC the worker asks is answered from rows filled at the cut, or is pure (docs/121, docs/131 verdict "snapshot/memo design required"). The cut stays where it is (sim pump, cut site 2).

### 3.4 Cost [inferred, I-398-5]

- Sim stores: store handlers are 2.4% of interpreter handler self samples (366 of 15,425 in `mint-opcodes.csv`), loads 18.3%. With interpreter self ~25% of sim-thread samples (docs/139 §4.5) and ~3-5 ns per store handler, that is ~1.2-2 M managed stores per second of sim time.
- Barrier active ~5 ms per job (the worker's run plus margin), ~5.6 jobs/s at Sample=1: ~28 ms of every second. Stores in that time: ~35-55k/s; if a third are first writes to a word at ~40-60 ns and the rest repeats at ~10-15 ns: **~0.8-1.5 ms/s**. Thread branch on loads in the same time (~7.6x the stores, ~1 ns each): ~0.3-0.4 ms/s. Swapping ~60 table entries twice per job and clearing the log: < 0.1 ms/s. Total **~1-2.5 ms/s**.
- Worker: one page-bit test per load and a hash probe on dirty pages: job time +5-15%, off the sim thread. The window is ~26-32 ms, so a 4-5 ms job still finishes with ~5x margin.
- Against today: no window wait (BP -10, IP -5..-7 ms/s now), no overrun fallbacks; the docs/124 §14.3 net rows apply minus ~1-2.5 ms/s.

### 3.5 Exactness

**Exact by construction under one condition: every write by another thread to memory that was reachable at the cut goes through a logging path before it lands.** Then every word the worker reads is either unchanged since the cut (live value) or was logged on its first change (logged value), and objects created after the cut are unreachable from it (any field pointing to them was logged with its old pointer). The writer list in §3.3 is finite and auditable; what can break it is an unlisted native writer (an EA IC body writing a managed field it got as a raw pointer, a runtime helper not in the list).

Checks: (1) Mode R stays the gate, now with the sim running t+1's script concurrently: A1-A3 = 0 means no unlogged write reached a read; (2) an audit mode for test builds [design]: at a sampled cut, the docs/123 capture code writes the heap sections to a file (a stall of a fraction of a second; no address space used), the worker records (address, value) of every read, and an offline tool compares them with the image, naming the address of any unlogged writer; (3) the `FreshEvery` re-score (docs/122 §14.21) keeps measuring staleness, which 2b does not change.

### 3.6 Effort, jobs, risks

| Step | Hours | Jobs |
|---|---|---|
| S0 store-rate probe: counting-only store handlers swapped in for the job span (stores, distinct words, distinct pages per job), no log | 1-2 | 2 (BP, IP; can ride mono-cost14) |
| Barrier handlers, table swap, thread branch, log, P2 root push | 4-6 | - |
| Runtime and IC writers (internal calls, guard-thunk range logging, helper) | 3-5 | - |
| Worker redirect: 29 load handlers + 13 superops, native array reads | 4-6 | - |
| Selftests: ts3native concurrent writer/reader fixture; monohost two-thread run with a mutating sim thread (G-A2 style) | 3-5 | - |
| Mode R with the sim running (WindowWaitUs=0, worker consumed at the next sim pump), 2 clean runs per world (crash rule) | - | 4-6 |
| Audit mode (optional, docs/123 capture reuse) | 3-5 | 2 |
| **Total** | **~15-24 h** (+3-5 audit) | **6-10**, no clean night (cost is judged in docs/124 S4) |

Risks, in order: (1) an unlisted native writer: wrong scores, caught by Mode R and the audit, not by the barrier itself; (2) **two threads interpret at the same time in game for the first time**: the quiescent window kept the sim out of the script while the worker ran, so the M5 race set of docs/131 (348 globals, 85 open: load-time singletons, lazy first fills to pre-warm, the temp string refcount `[0x11CB920]` kept memo-only) is now live; G-A2 passed offline only; (3) hook overlap on the mint jump table and handlers (docs/139 fast paths, MonoPatcher, S3PP, Apex: `hook-sites.ts --check`); (4) log overflow on heavy ticks (fallback, counted); (5) worker slowdown from the redirect checks; (6) a job still running at the resume (max 5-9 ms vs ~26 ms: rare; fallback).

## 4. Option 3: a copied heap or copy-on-write pages

Facts [verified, docs/198 §3.2, docs/57 §6]: every Boehm heap section is one `VirtualAlloc` through EA's `GC_win32_get_mem 0xE4EEF0` with `MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH` and `PAGE_EXECUTE_READWRITE`; the initial section is 352 MB, growth sections up to 32 MB (~368 MB in play, docs/123 §1); headers live outside the sections; the incremental GC is on (`GC_enable_incremental` from `MonoScriptHost_Init`) and its `GC_read_dirty 0xE4EFB0` calls `GetWriteWatch` over every section.

| Variant | What it needs | Cost | Verdict |
|---|---|---|---|
| 3a Second view at the same addresses in this process | impossible on Windows: one address range maps one view per process | - | no |
| 3b Second view at other addresses (heap re-created as a pagefile-backed section, `MapViewOfFileEx`) | worker pointers would all be wrong (absolute addresses in every object); plus ~368 MB of address space for the view, against a largest free block of ~1.1 GB when healthy (Error 12) | - | no |
| 3c Process clone per cut (`PssCaptureSnapshot` with a VA clone, i.e. a fork) and the worker in the clone | a fork of a ~3 GB 32-bit process copies page tables and marks every private page copy-on-write: tens of ms per cut [inferred], then the parent takes a copy-on-write fault on the first write to every shared page; the clone has no threads, its locks are frozen in whatever state the cut left them; at ~5.6 cuts/s this is hundreds of ms/s | no (docs/123's world image is this idea run once per capture, with a few seconds of stall) |
| 3d In-process page protection: write-protect the heap at the cut, a vectored handler copies a page to a shadow on the sim's first write, the worker reads shadow pages for faulted pages | per first write to a page ~3-10 us (exception dispatch, 4 KB copy, `VirtualProtect`); the sim's ~5-9k stores per job plus allocation touch ~500-3,000 pages [inferred]: **~1.5-30 ms per job**, against ~4.4 ms of scoring moved; re-protecting costs as much again; kernel writes into protected managed buffers (file reads into `byte[]`) fail with `ERROR_NOACCESS` instead of faulting | no |
| 3e Write watch as an optimistic check: worker reads live, the consume compares the pages the worker read with the pages written since the cut | write watch is reset by Boehm's `GC_read_dirty` on the GC's own schedule (a second reader has to wrap it and keep its own union); it only says a page changed, never the old value; the worker's read pages (Sim, autonomy, lot objects, collection arrays) and the sim's written pages (the same hot objects, plus every page that receives new small objects) overlap in nearly every job [inferred] | no: fallback storm |

**Verdict: reject per tick.** Every variant either cannot give same-address reads or costs more than a job moves. What 3 adds that 2b lacks is only page-granular capture, which 2b's audit mode gets from the docs/123 capture code without per-tick cost. docs/123's world image (~13-22 h, 2 capture jobs) stays the offline fixture and audit tool.

## 5. Recommendation and next steps

**Build option 2b** (copy on first write between the cut and the worker's end), measured first:

1. **S0 probe (1-2 h, 2 jobs, can ride the next mono-cost queue):** counting-only store handlers swapped in from the cut to the worker's end; counters `satbStores`, `satbWords`, `satbPages`, `satbUs` per job. Gate: first-write words per job <= ~20k and the inferred barrier cost (§3.4 model with the measured counts) <= ~3 ms/s at Sample=1. Stop rule: more than ~5 ms/s, or log size beyond ~4 MB per job: 2b is not worth it; go to docs/139 fast paths as docs/124 §14.4 S1 already says for a failed cut gate.
2. **Build 2b** (§3.6) behind `[AutShadow] Satb=1` (off), with `WindowWaitUs` kept for Mode R comparisons until step 3 passes.
3. **Mode R with the sim running**: worker results consumed at the next sim pump after it finishes (not at the next Simulate entry), A1-A3 = 0 on BP and IP, `stop`/overrun ~0, two clean runs per world.
4. Then docs/124 S2-S6 as planned (B14 consumer, G7 dry apply, G9 cost ABBA), with the cut gate G-C unchanged.

Option 1 is not worth building; a single probe counting managed entries per native span (I-398-3) is only needed if someone wants the number. Option 3 stays rejected; its useful part is the 2b audit.

## 6. Ledger (docs/51 §398)

| ID | Claim | Tag |
|---|---|---|
| I-398-1 | The SACS walker event push `0x742710` calls the script host slot `+0x134` with the queue's delegate, i.e. `EventQueue.OnNativeEvent` runs synchronously inside `Animation_SACSPass` and the pre-locomotion step; docs/22 §1/§3 "C# drains that queue later" is refuted | verified (disassembly + decompiled class library) |
| I-398-2 | The rig pose path `0x5809D0` calls a script host slot from `Animation_UpdateActors` | verified call, slot and managed target not read |
| I-398-3 | At least one managed callback (route event 57/58 or SACS event) fires in most native spans on BP/IP | inferred |
| I-398-4 | Log-then-store on the sim and load-then-check on the worker give cut values under x86 TSO | inferred |
| I-398-5 | 2b barrier cost ~1-2.5 ms/s at Sample=1 (stores 2.4% of handler samples, ~35-55k stores/s while a job runs) | inferred, S0 probe |

## 7. Implementation (ts3native 0.36.125, `[AutShadow] Satb`, built and proven offline, not run in game)

Rodney approved 2b on 2026-10-30. Code: `native/ts3native/satb.cpp` (included by `autshadow.cpp`; monohost gets it the same way), the flow in `autshadow-run.cpp`, the guard-thunk IC check in `autshadow.cpp` `AsGuardEntry`, the GC root push hook `g_mtPushExtra` in `monothread.cpp`. Handler facts: `docs/data/satb-handlers.csv` (130 rows, every one from the disassembly) [verified]. Ledger docs/51 §399. Everything is off by default (`Satb=0`).

### 7.1 Facts the design had to change [verified, docs/data/satb-handlers.csv]

| docs/206 §3 said | Found | Consequence |
|---|---|---|
| 35 store opcodes in 24 handlers | 37 in 26: `strmfld`/`strmfld.vt` (0x64/0x65) were missing | both wrapped |
| 42 loads in 29 handlers | 41 in 28 (ldfld 11/9, ldrmfld 2/2, ldsfld 4/4, ldind 11/10, ldobj, ldelem 11/1, ldelem.any.vt); plus hidden loads in `con.callvirt`/`con.boxcallvirt` (0xED/0xEE: the `this` slot holds an address), `getchr`/`strlen` (strings are written in place by `InternalStrcpy`) | all redirected; `con.callvirt` is emulated up to the callvirt join `0xE54ABF` |
| a page bitmap of the Boehm heap marks shared memory | the interpreter's frame arena chunks are GC objects (`0xD718A0`, `GC_malloc_explicitly_typed`) from one shared, unlocked pool; primitive statics live in the domain mempool (not a heap section) | no address range separates shared from private: the barrier logs every word a store writes, except the sim's own stack and current arena chain (`SatbSimPrivate`, a cost filter); a chunk the worker takes during a job (it may hold log entries from the sim's earlier use) is excluded on the worker side (`SatbGrowWrap` on P3's grow slot `g_mtTramp[13]`) |
| objects freed during the job could be reused | `GC_all_interior_pointers` = 1 (`[0x11949B4]`) | the used log entries (word address + old value) are pushed as roots at every stopped mark: logged words' objects and old pointers stay alive until the job ends |
| superinstructions need their own load handlers | EA's peephole writes only the first word (`0xE5C8F0`); operands and later words stay in place | on the worker a load superinstruction (13) dispatches its base sequence's first opcode at the same ip |
| GC headers are outside the sections | (unchanged) | the GC never writes cut-reachable words |

### 7.2 What runs where

| Path | Sim thread (barrier) | Worker (redirect) |
|---|---|---|
| mint store opcodes (37) | stub: decode target and size (`SatbStoreRange`), log each word not yet logged, then the game's handler | scalar kinds emulated: a word the sim logged takes the worker's bytes in its log entry (the worker's view; the sim's memory keeps the sim's value), other words live, then a re-check (closes the race with a concurrent first write); vt/blk/ref kinds with a logged word: skipped, object aborted, job stopped |
| mint load opcodes (41 + 0xED/0xEE + getchr/strlen) | thread test, then the game's handler (counted in `satbLoads`) | emulated: live bytes, compiler barrier, then each logged word's value; x87 `fld`/`fstp` for r4/r8 as the handlers do; `stackval_from_data` `0xE521A0` for typed cases; null / out-of-range / class-init / thread-static paths go to the game's handler |
| load superinstructions (13) | game's handler | base sequence |
| mono runtime icalls (static table `0x10B5838`, 45 entries, patched before Mono resolves them) | writers log the destination range (Array.Clear/FastCopy/SetValue, Buffer.BlockCopy/SetByte, String.InternalStrcpy x4/InternalCopyTo, Interlocked.*, InitializeArray, Marshal.Write*); unsized writers (MonoField.SetValue, Marshal.copy_from_unmanaged, PtrToStructure) stop the worker first | readers patched from the log after the copy (FastCopy, Clone, BlockCopy, GetGenericValue, InternalStrcpy, CopyTo); GetValue, MemberwiseClone, MonoField.GetValue: the object gives up on a logged word |
| EA field store `0xE69820` (`*(arg2) = arg3`) | logs the word | a logged word goes into the entry |
| EA ICs with byref/array parameters (180, `native/ts3native/satb-ics.h` from `tools/satb-ic-classes.ts`) | an argument dword pointing outside the sim's stack/arena = the body may write shared memory with an unknown size: stop the worker first | answered from snapshot rows as before (docs/122) |

Unknown sizes, a full log (65,534 words) and single stores over 1,024 words: the sim sets the stop flag and waits for the worker to leave (`SatbSimWait`, 100 ms cap, then the job is dropped as stale) before the write lands. The run ends `STOPPED`, so ts3fixes' KeepPartial keeps exactly the objects finished before the stop. The worker never reads a word whose cut value was lost.

### 7.3 Modes and flow

- **`Satb=1` probe (S0).** No concurrency: the worker runs in the window as in mono-cost13 (`WindowWaitUs`). At the window close the store stubs go in for the rest of the tick; a high-resolution timer thread snapshots the counts one job length (the last job's run time) after the span start (`satbJob*`), the whole tick is counted too (`satbStores`, `satbWords`). The worker's own stores and loads are never counted. Read with `mono-cost-report.ts`: "Satb probe per job span" and the go-live model line with the §5 verdict.
- **`Satb=2` act.** `AsSimLeave` (cut site 2): cut, reference, `SatbBegin` (epochs move on, stubs in, IC check on), submit. `AsSimEnter` (next Simulate entry): no wait, no stop, no epoch drop; the job keeps running while the sim runs the tick (`satbConc`). The worker's job hook swaps the stubs back at its end (the sim pays the barrier only while the job runs); the next end of frame after that finishes the run (`SatbEnd`, roots off). A job still running after 8 ends of frame gets the stop flag. Any Satb event (stop-and-wait, give-up, conflict that could not be emulated) turns `AS_LAST_OK` into `AS_LAST_STOPPED`.
- Jump-table entries are checked against the game's handlers at install (`kSatbOps`); another hook on any of them makes Satb refuse (`satbMode` -1).

### 7.4 Offline proof (monohost, `MONOHOST_SATB=1`, 2026-10-30) [measured]

| Check | Result |
|---|---|
| ts3native selftest `TestSatbLog` | first values kept, partial words merged, overflow at 65,534, 8/16-bit epoch wraps; writer (log, then store) vs reader (load, then merge) 4 M stores, ~1 M reads, 0 wrong |
| `AS-satb-exact` (act): the worker scores `SatbWorld` (every field/static/array/byref/ldobj/remoting/collection read, plus List copy, Clone, Array.Copy, BlockCopy) while the main ("sim") thread mutates it with every store kind and the runtime writers and collects every 8 mutations | 120 of 120 jobs concurrent, ~7,000 mutations during jobs, **A0 6,000 of 6,000 bit-equal to the reference at the cut, A1-A3 0**, ~1.5 M log hits, ~0.46 M worker copy fixes, 0 give-ups, 6,000 worker stores kept in log entries (NumberFormatInfo flags both threads write) |
| `AS-satb-negative`: the same flow, no barrier | A2 ~2,000 of 2,000 (the test sees the writes) |
| `AS-satb-overflow`: a 70,000-word write during a job | overflow 1, the sim waited once, A5, A1-A3 0 |
| `AS-satb-probe`: 20 jobs in the window, then the next tick | 20 spans timed, ~550 stores / ~370 words per span, 0 worker redirects, A1-A3 0 |
| G-A2, M5 and the rest of the host selftest | 42 of 42 PASS (the host harness had drifted from ts3native; fixed in the same commit) |

### 7.5 Cost [measured, monohost, best of 20, other load on the machine]

Sim thread with a span on: 4.9-8.5 ns per store at ~18% first writes, ~5-18 ns per load through the thread test (noisy). The report models `stores x 4 + words x 6 + loads x 9 ns` per job span plus half a job per unlogged event, times the go-live cut rate (`metaTicks`/s). The game numbers come from the S0 probe.

### 7.6 Next steps and gates

1. **Queue `satb-s0.json`** (BP + IP, 2 x 10 min, asi `game-copy/ts3native.asi.satb1`, ts3fixes package mc13): `node tools/harness/queue.ts tools/harness/queues/satb-s0.json`, then `node tools/harness/mono-cost-report.ts --tag satb-s0`. Gate (§5): <= 3 ms/s and <= 20k words a job; stop rule > 5 ms/s or a log > 4 MB (then no act queue). Also read `satbIcShared` and `satbRtWaits` per span: each is a stop-and-wait in act mode.
2. **Only after S0 PASS:** an act queue (`satb-r1`, not written yet on purpose): mono-cost13 keys with `Satb=2`, `WindowWaitUs=0`; gates A1-A3 = 0 on BP and IP, `satbWGiveUp` and `satbStale` small (they are fallbacks, not errors), two clean runs per world (crash rule), then docs/124 S2-S6.
3. **Audit mode: not built.** A read-record audit (worker records its reads, checks them against the log and live memory at the job end) was tried and gives false positives from the worker's own memory: objects it allocates and drops during the job (a concurrent sim collection reuses them), its own frame arena and words it writes after reading. A sound audit (here or with the docs/123 world image) needs the worker's allocations recorded (a post-call hook on the P3 lock stubs for the allocation entries) [design]; until then Mode R (`Satb=2`, A1-A3) is the gate.
4. **Races (S4):** §8.

### 7.7 Risks

1. An unlisted native writer into managed memory (a runtime icall or EA body not in §7.2): wrong worker reads; Mode R A1-A3 would show it, the barrier would not.
2. Concurrent interpretation makes docs/131's open glue races live (§8).
3. Worker-side native reads not redirected: mono string icalls that scan chars (`InternalIndexOf` and friends) on a StringBuilder buffer the sim rewrites in the same job; `ValueType.InternalEquals`/`GetHashCode` (boxes are immutable unless written through `unbox` + store, which the barrier logs).
4. Interlocked 64-bit icalls run under a critical section, not a lock prefix (not atomic against the 32-bit ones) [verified]; harmless today (no managed thread pairs), a note for the M6 worker.
5. The callvirt inline cache is written with two plain stores (`0xE54B1D`, `0xE54B22`); a concurrent reader can see the new class with the old target: fixed (§8, X01).
6. Probe accuracy: the span starts at the Simulate entry after the window, which is where a concurrent job would also begin to overlap the sim's script; the job length used is the worker's own run time.

## 8. Races under concurrent interpretation (S4, static, 2026-10-30)

`docs/data/satb-races.csv` (246 rows: docs/131's 85 open glue races, the 127 rows docs/120 §3.3 covered by warm-up, the arena/P4/Boehm rows, and 22 items outside the race set) classified for `Satb=2`, where the sim interprets while the worker runs: **97 guarded, 136 safe, 13 needs-fix**. The 77 guarded open rows are reached only from EA IC bodies, which never run on the worker (the guard thunk answers from snapshot rows) [verified]; most safe rows are init-only writes, `mono_stats` counters or idempotent pointer caches.

| # | Hazard | Risk | Status (0.36.125) |
|---|---|---|---|
| X01 | callvirt inline cache: pair written and read with two plain stores, so a torn read runs another class's override [verified]; not in docs/131's globals-only race set | high | **fixed**: the worker's callvirt / con.callvirt / con.boxcallvirt never read or write the cache (handler tail from `0xE54ABF` in `SatbWCallvirtTail`, lookup `0xE51FC0`, continue at `0xE54B28`); monohost exact |
| X03 | F1 overflow (depth 32 or allocation failure) falls back to the game's shared call-record chain `[0x11EE5A4]`, now the sim's [verified] | medium | **fixed** in `monothread-m5.cpp` `MtN2mRefuse`: during an act span the call returns the preallocated exception and the object gives up |
| X11 | worker cold paths when the warm-up missed something: transform `0xE643B0`, thunk hash `0xE51400` (`[0x11F4128]`, unlocked, the sim inserts in play [verified]), vtable creation `0xEA1E30` (publication before the end of creation [verified]), class init `0xEA3370` (a `.cctor` on the worker), image load `0xD6FCB0` (temp string refcount `[0x11CB920]`, O15); also interface ids, generic caches, an id counter (M022/023/026/027/057, M099, X10) [inferred] | medium | **detected**: act-only entry detours (`SatbInstallCold`) make the object give up and the job stop (`satbWGiveUpOp` = 0xC000 + site); the call itself still runs once on the worker. The agent proposed raising a managed exception at the entry; not done: a raise from inside the call push path (transform) can leave a half-built frame for the handler search [inferred]. Primary protection stays the warm-up (Mode R's reference run covers the same code first). Open: `0xE6EE20`, `0xE82680`, `0xE78F60` entries; a lock around `0xE51400` on both threads |
| X13 | special-static hash `domain+0x88`: a mod class with `[ThreadStatic]` first used during a job inserts while the worker looks up unlocked [inferred] | low | **fixed** on the sim side: vtable creation on the sim during a span waits for the worker first (cold site 2) |
| X06 | live check of the IC nesting counter `[0x11F2D84]` (before/after a worker run) sees the sim's own changes | diagnostic | open (verify.cpp keeps only the never-negative readout) |
| - | 64-bit Interlocked under CS `0x11F476C` | - | guarded (serialized); torn 8-byte plain reads are patched from the log |

Corrections to docs/131 (from the CSV): the race set cannot see per-method state (the callvirt cache); four rows labelled P3 are not under the P3 lock (`[0x11F2F98]`, `GC_dont_gc` `[0x11F30A8]`, `0x1222470`, `0x1222408`; all benign today: only the sim writes them); `[0x11EE514]` is not reachable from the worker; I-131-4 (only Boehm calls `0xD70900`) verified (single caller `0xE4EEFB`).

Before an act queue: S0 PASS, then Mode R with `Satb=2` (§7.6) and these counters read per run: `satbWGiveUp` with `satbWGiveUpOp` (0xC000+ = a cold path the warm-up missed; 0xD8A8E0 = F1 overflow), `satbWaitTimeouts` (a class-init lock between the threads), `satbStale`.
