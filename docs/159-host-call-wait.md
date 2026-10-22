# 159. Route job host-call wait: what the worker waits for, and what can disappear

Written 2026-10-22. **Static plus existing data only:** no game run, no queue, no build. Ledger rows: docs/51 §251. Question (docs/150 §7 results on `asi.0326`): the largest share of the route worker's `+0x918` hold is **host-call wait**, the time the worker holds the lock while the sim thread runs a managed call for it. Which calls are these, why does the worker keep the lock, where does the sim pick them up, and which fix removes wait instead of moving it to the next waiter?

Sources: `probe-out/queue/route-phases-2026-10-22-{bp,ip}-route-phases-verify-native.json` (07:57 / 08:02 UTC runs, `asi.0326`, release defaults + `[RouteJobPhases]`, 300 s, noisy machine: others on game CPUs 29% / 19%), `node tools/harness/route-phases-report.ts <native.json> --seconds 300`, the same runs' `-180-seconds.csv`, 57 managed-sampler files 2026-10-20..22 (`probe-out/profile/*-{bp,ip}-*-managed.jsonl`, 56,241 samples), `native/ts3native/ts3native.cpp`, `routephases.cpp`, `p4.cpp`, `simloop.cpp`, docs/18, docs/92, docs/105, docs/108 §7, docs/115, docs/150, the decompiled `SimRoutingComponent`.

Tags: **[measured]** = counters or samples of real runs; **[inferred]** = derived with the method named; **[verified]** = code or bytes read.

## Summary

- **Size** [measured, `asi.0326`, own START jobs only]: BP host-call wait **18.8 ms/s of a 73.5 ms/s hold (25.6%)**, 120.6 calls/s, 9.3 per job, mean 0.52 ms, max 96 ms; IP **11.1 of 64.0 ms/s (17.4%)**, 92.1 calls/s, 18.2 per job, mean 0.40 ms, max 60 ms.
- **Which calls** [measured]: every one is a **host delegate call `0xD75FA0`** (`hostCalls` 36,166 BP / 27,633 IP = the phase's entry count; `invokes` 0, so no plain `runtime_invoke`; `workerCalls` 0, so no sync plan on the worker; `rgBorrows` 0, so no call took `+0x918`). Statically the only managed exit of the four route ICs is **World event 59**, `World_FirePortalLockedQuery 0x791C00` called at `0x64F4D3` in `Portal_LockState 0x64F2C0` [verified, direct calls, docs/92 §4]. So in practice the host-call wait is the **door / portal lock question** (`LotManager.PortalLockedCallback` -> `Lot.OnPortalPathPlan` -> `Sim.OnPortalPathPlan`, `CommonDoor.IsAllowedThrough`, ...). A per-call kind or event id is **not recorded** by any counter (missing).
- **Asks per job are ~2.1x the per-plan count** [measured vs measured]: 9.3 / 18.2 per job against I-105-5's 4.30 / 8.78 event 59 asks inside `RoutingJob_Execute` per plan (docs/108 §7). The rest comes from the goal step (`Route_GoalStep 0x75C2D0 -> 0x63F490 -> LotPortalSearch`) and from extra lot searches in the candidate loop. Each of those has its own answer cache on its stack (`PortalLockCache_Find 0x64F1E0`, docs/92 §1), so **the same door is likely asked again within one job** [inferred].
- **Where the time goes** [inferred]: a dispatch costs the sim thread **~19 us (BP) / ~23 us (IP)** (managed sampler: 2.86 / 2.82 ms/s inside `PortalLockedCallback` over docs/115's 152 / 122 dispatches/s). Two event hand-offs add ~10-40 us [inferred, not measured]. So **~62-81% (BP) / ~48-73% (IP) of the wait is queue delay**: the sim thread is busy elsewhere or asleep and reaches none of the five serve points. Consistent with that: the sim's lock pump overlaps only 61% (BP) / 72% (IP) of the hold (`asyncPumpUs` 44.8 / 46.3 ms/s) [measured]. In the rest of the hold the worker spends ~2/3 of its time stalled on a call.
- **Why the lock stays held** (§3): the ask sits in the middle of the search (in-room pre-pass `0x632460`, `PortalEdgeCost 0x634500`), with frames holding lot-table, quadtree, portal and footprint pointers. The pre-pass writes per-Sim locked bits into the shared lot quadtree. Lending is also of little use: a sim that wants `+0x918` already serves the call at once in its pump, so a lend only lets steering skip ahead while the job stays stalled.
- **Ranked fixes** (§4, hold / sim wait at the docs/150 slope 0.67):
  1. **H2 within-job door answer memo** at `0x64F4D3` (P4's key): exact, about 2-3 h. BP hold -3.8..-10.3, sim wait **-2.5..-6.9 ms/s**; IP -2.2..-6.1 / -1.5..-4.1; plus sim CPU -0.6..-1.6 ms/s.
  2. **H1 serve in the frame-limiter window**: about 2-4 h. BP hold -2.3..-9.2, wait -1.6..-6.2.
  3. **H3 serve at script task switches**: riskier, 4-6 h.
  4. **H4 native door answers across jobs** (docs/105 evaluator): the largest gain, BP wait -7.5..-11, but 10-16 h and mod risk.

  Rejected: lending during the wait, and prefetching answers at START (moves +2.6..+22 ms/s of dispatch onto the sim).
- **Top candidate: H2.** Cheapest probe (§5): counters v96 `hc*` (wait split by serve site, queue time inside limiter sleeps, door-key repeats within and across jobs, answer changes on a repeat) behind `[RouteJobPhases] HostSplit=1`, queue `host-call-probe.json` (BP + IP, 5 min, day). Build H2 if the repeated asks' wait is >= 4 ms/s on BP with 0 answer changes.

## 1. The calls the worker proxies

| Item | Where | Tag |
|---|---|---|
| Who proxies | `ForCaller` (`ts3native.cpp:2080`): the route worker while a sync caller waits or a job is `A_RUNNING` | verified |
| Hook 1, plain managed invoke | runtime_invoke slot `0x01195B60` -> `InvokeHook` (`ts3native.cpp:2099`) | verified |
| Hook 2, host delegate call | detour of `0xD75FA0` (boxing, EA's task-id guard, invoke) -> `HostCallHook` (`ts3native.cpp:2108`) | verified |
| Worker side | `Proxy` (`ts3native.cpp:2083`): post `g_invReady`, wait for `g_invDone` (or `g_jobReady`: a plan the call sends back runs nested, `ServePlan`); the wait is the `[RouteJobPhases]` host frame (`RjpHostBegin`, `routephases.cpp:221`), nested plans are its children | verified |
| Sim side | `ServeInvoke` (`ts3native.cpp:2050`): zero host `+0xBD0/+0xBD4`, run the call, restore, count `hostCalls` / `invokes`, set `g_invDone`. It names the first 8 `MonoMethod`s of plain invokes only, so host calls are not named | verified |
| Calls in `asi.0326` | BP 36,166 host calls, 0 invokes, 0 worker sync plans, 0 borrows; IP 27,633 / 0 / 0 / 0 | measured |
| Managed exits of the route ICs | event 59 only (`0x64F4D3 -> 0x791C00 -> World_FireEvent 0x78F2B0 -> 0xD75FA0`), from the search and from the goal step; event 68 never fires (quiet flag, I-105-1) | verified, direct calls; virtual hops not excluded (docs/18) |
| Event 59 share of host calls | assumed 100%; the probe's `hcOtherCalls` checks it | inferred |
| P5 door asks (`P4DoorEnter`, `p4.cpp:429`) | same proxy path when P5 Mode 2 runs; not in the release config | verified |

Per job and per call [measured, `route-phases-report.ts`]:

| World | Jobs/s | Hold ms/s | Host wait ms/s | Share | Calls/s | Per job | Mean | Max | Pump overlap (`asyncPumpUs`) |
|---|---|---|---|---|---|---|---|---|---|
| BP | 13.0 | 73.5 | **18.8** | 25.6% | 120.6 | 9.3 | 0.52 ms | 96 ms | 44.8 ms/s (61% of hold), 8.8 waits/s, 5.1 ms mean |
| IP | 5.1 | 64.0 | **11.1** | 17.4% | 92.1 | 18.2 | 0.40 ms | 60 ms | 46.3 ms/s (72%), 5.0 waits/s, 9.3 ms mean |

Which handlers dominate on the sim side [measured, managed sampler, sim thread]: the samples under `PortalLockedCallback` are BP 101 / 35,297, IP 59 / 20,944. Their leaf frames are spread over `Sim.OnPortalPathPlan`, `GetInvocationList` (docs/115), `Lot.OnPortalPathPlan`, `CommonDoor.IsAllowedThrough`, `ObjectFromId`, `MemberwiseClone`, `ElevatorDoors.IsAllowedThrough` and `Lot.IsRoomHidden`; none dominates. Every managed stack ends at `OnPortalLocked_Delegate.Invoke`, so the sampler cannot see which native serve point ran the call.

**Missing data:** per-call queue vs service time, the serve site, the event id, door-key repeats. No counter has them. §5 adds them.

## 2. Where the sim thread picks a call up

Calls are served only at these points, all on the job's owner thread [verified]:

| Site | Where | When it runs | Expected latency |
|---|---|---|---|
| Lock pump | `LockAcquireAsync` (`ts3native.cpp:3069`, serve at `:3097`) | a contended `Lock_Acquire` (any lock, finite or infinite deadline) while a job is queued or running; for `+0x918` this is every steering, gate, writer or destroy acquire during a job | event wake (~us); the sim is blocked anyway |
| READY poll | `GetOptionHook` (`ts3native.cpp:2968`, serve at `:3011`) | the job's own Sim polls `READY`; serves **at most one** call per poll, then the task yields | one poll per script tick of that Sim |
| Job waits | `WaitAsyncDone` (`:2553`, from `rtsites.cpp:84`, `discourage.cpp:207`, `p5c.cpp:229`, `:2386`), `WaitJobDone` (`:2569`, from `:2602`, `:2784`) | collect, closing, D2 writers, drains | immediate |
| Sync plan wait | `RunPlan` (`:2373`, serve at `:2420`) | a sync plan handed to the worker (0 in `asi.0326`) | immediate |
| Borrow rule | `LockAcquireHook` (`:3039`) | a `+0x918` acquire inside a served call counts as nested in the job (`rgBorrows`, 0 in `asi.0326`) | - |

Not a serve point: the frame limiter. `MonoScriptHost_Simulate` calls the sim pump at `0xD81FDA` and then `IdleSimulationCycle 0x7694B0` at `0xD81FDE`, which reads the timers and sleeps (EA `SleepEx` wrapper `0x4E1320`, or S3SS Precise's own limiter). After that the loop-back is at `0xD81FF6` [verified, `simloop.cpp:3-70`, docs/03, docs/122 §14.11]. A call posted while the sim sleeps there waits out the sleep, then the gap until the next contended acquire or poll. BP runs 43.6 iterations/s and IP 53.0, close to the limiter's rate, so some iterations do sleep [measured iterations; idle share not measured].

Time split per call [inferred]:

| Part | BP | IP | Method |
|---|---|---|---|
| Service (the managed dispatch on the sim) | ~19 us, ~2.3 ms/s | ~23 us, ~2.1 ms/s | sampler ms/s inside `PortalLockedCallback` / docs/115 dispatch rate (n 101 / 59 samples, about +-20%) |
| Hand-off (two event wakes, worker and sim) | 10-40 us, 1.2-4.8 ms/s | 0.9-3.7 ms/s | typical Windows event wake to a waiting thread; not measured |
| **Queue delay** (sim not at a serve point) | **11.7-15.3 ms/s (62-81%)** | **5.3-8.1 ms/s (48-73%)** | the rest |

How queue delay turns into sim wait [inferred]: while the worker stalls on a call, it makes no progress. When the sim then reaches a `+0x918` acquire, it serves the call at once and then waits for the **whole rest of the job**, which it would have partly or fully missed had the job kept running. This is the docs/150 hold-to-wait slope (0.67) applied to queue time.

## 3. Why the worker keeps `+0x918` while it waits

1. **The ask is mid-search** [verified, docs/92 §1/§3]. `Portal_LockState` is called from `PortalEdgeCost 0x634500` (inside lot A\*) and from the in-room pre-pass `0x632460`. The pre-pass **writes** each room portal's tunnel-footprint locked bit (`+0xCC`, bit 8) for this Sim and removes and reinserts the footprint in the shared lot quadtree. The parked frames hold lot-table rows, quadtree nodes, portal records and cell node tables. A writer that ran in a lent window (`UpdateSimFootprint 0x833560`, destroys, lot applies, discourage drains) could free or move what those frames point at. P5b / TailUnlock admit only steering and are proven only for closed passes (docs/150 §11.1-11.2). There is no such proof for an arbitrary point inside A\*.
2. **The call itself may need the lock.** In a sync plan the callback runs on the thread that holds `+0x918`. Async, the borrow rule (`:3039`) keeps that true: an acquire inside the served call nests in the job. With the lock released, such an acquire, or a plan the call sends back (`Proxy`'s `ServePlan` loop), would really take the lock beside a parked job. That breaks the gate's ownership model (`rgOwnerTid`, docs/154 CE rules). The run shows 0 borrows, but mods' handlers are not bounded.
3. **A lend would not cut the sim's wait** [inferred]. When the sim wants `+0x918` during a host wait, it is in the pump and serves the call within microseconds. A lend would let steering run instead of serving, but the job stays stalled until the next serve point, and its rest is waited for at the next acquire. That moves the wait; it does not remove it.

## 4. Candidate fixes, ranked by ms/s that can disappear

Hold cuts are shares of the measured host wait; sim-wait cuts use the docs/150 slope 0.67 [inferred]. "Disappears" means no thread does the work later: the call is not made, or it is served in time the sim would have spent idle or serving anyway.

| # | Fix | What disappears | Hold cut BP / IP (ms/s) | Sim wait cut BP / IP | Risk | Hours |
|---|---|---|---|---|---|---|
| **H2** | **Within-job door answer memo.** A call-site stub at `0x64F4D3` (rel32, worker only, inside a job), keyed with P4's door key (`P4DoorKey`, `p4.cpp:423`: lot, Sim and portal owner ids, room pair, initialLocked); a repeat in the same job returns the first answer without a proxy round trip | whole calls: queue + hand-off + service, plus the sim's dispatch CPU | hit share 20-55%: **-3.8..-10.3 / -2.2..-6.1** | **-2.5..-6.9 / -1.5..-4.1**; sim CPU -0.6..-1.6 / -0.6..-1.6 | low: exact under async semantics (§4.1); one call site already used by P4 and the value probe (docs/68 chain rule) | 2-3 build + tests, 1 day job, 2 clean nights |
| H1 | **Serve in the limiter window.** A `SimPumpAdd` callback (`simloop.cpp:87`, runs right before `IdleSimulationCycle`) serves the job's calls and waits for `g_asyncDone` while a job runs, up to the iteration's idle budget, so the limiter sleeps less by the same time | queue delay that today falls in limiter sleeps (sim idle) | sleep share 20-60% of queue delay: -2.3..-9.2 / -1.1..-4.9 | -1.6..-6.2 / -0.7..-3.3 | low-medium: the same frame point as the docs/122 cut, right after EA's own remote-call pump; with EA's fixed `SleepEx` an overshoot adds frame time (bound it) | 2-4 + tests, needs a clean night for the share |
| H3 | **Serve at script task switches** (the per-task dispatch of the script host, `0xD7F000` family, docs/36) | queue delay while the sim runs script | -2..-8 / -1..-4 (busy share x density, both unknown) | -1.3..-5.4 / -0.7..-2.7 | medium-high: a new managed-entry point inside the script tick (docs/56 interpreter state); task id handling as `ServeInvoke` | 4-6 |
| H4 | **Native door answers across jobs** (docs/105 native evaluator over the ts3fixes door mirror; S-tier rows still asked) | 60-90% of calls on vanilla-handler lots | -11..-17 / -6.7..-10 | -7.5..-11 / -4.5..-6.7; sim CPU -1.7..-2.6 | high: b-submit rows (running interaction), mods replace handlers (GoHere, simler90: fallback by the handler-set guard, docs/92 §8 item 2), no exact proof yet (I-105-8 shadow open) | 10-16 + shadow job + nights |
| - | Lend `+0x918` during the wait | ~0 (§3.3) | - | moves to the next acquire | high (§3.1-3.2) | rejected |
| - | Prefetch all start/goal-lot answers at START | nothing (answers asked eagerly) | up to -18.8 | at the cost of +2.6..+22 ms/s sim dispatch CPU (20-100 asks per plan vs 9-18, docs/105 §5) | - | rejected: moves work onto the sim |
| - | Drain more than one call per READY poll | nothing: the worker blocks on each call, so at most one is ever pending | 0 | 0 | - | not a fix |

H1, H2 and H3 add up only to the host wait: H2 removes calls, and H1/H3 shorten the queue time of the calls that remain. With H2 at its midpoint and H1 at its midpoint, the total is ~-11 ms/s of hold on BP and ~-7 ms/s of sim wait [inferred].

### 4.1 Why H2 is exact

- Under async jobs the sim keeps running between two asks of one job, so today's answers in one job already come from different sim moments. A memo returns the answer from the first ask. That is exactly what a vanilla sync plan sees: every ask at one instant [inferred from docs/92 §5's "every answer is fixed for one plan"].
- Native side effects stay. The stub returns the same value at the same call site, so the caller's code after `0x64F4D3` still runs. This includes the clear of `lot+0x8F` for a 0 / > 3 answer at `0x64F4EE` (`replay.cpp:1275` notes) [verified site].
- Managed side effects of a skipped repeat are limited to the handler's `SimRoutingComponent.LockedDoorsDuringPlan.Add(door)` (docs/92 §5 row 4) and the `PortalPlanEventArgs` allocations. The list is a `List<Door>`, cleared before each plan. Its only readers are two nearest-door scans in the route-fail handler (`SimRoutingComponent`, nearest by distance with a strict compare), and a duplicate entry cannot change which door they pick [verified, decompiled C#]. So a skipped duplicate add changes nothing.
- The key must match P4's replay key (low word of the room args, low byte of initialLocked; the other bits are register leftovers, docs/94 §18.7). The probe's `hcDoorRepeatDiff` (same key, different answer) must be 0. Otherwise the key misses an input, and H2 stays unbuilt.

## 5. Cheapest probe (one day job pair, nothing acts)

Observe only, behind `[RouteJobPhases] HostSplit=1` (needs `Enabled=1`), counters **v96** (reserve in `docs/registry.md` at build; v96 is the next free there).

- **Stamps** (no new hook for these). The worker stamps QPC at `SetEvent(g_invReady)` in `Proxy` (a new `InvokeJob` field) and at the wake after `g_invDone`. `ServeInvoke` stamps pick-up and done and takes a **site id** argument from its six callers (pump `+0x918`, pump other lock, READY poll, `WaitAsyncDone`, `WaitJobDone`, `RunPlan`). Nested-plan time inside `Proxy` is kept apart.
- **Limiter overlap** (existing hooks). A `SimPumpAdd` callback stamps limiter entry. The `SimLoopAdd` callback at the loop-back stamps limiter exit, into a 4-entry ring written and read on the sim thread only. `ServeInvoke` adds the overlap of [post, pick-up] with the ring's intervals.
- **Door keys** (one call-site stub). Retarget the call at `0x64F4D3` with `PatchCalls`, on the worker inside a job only; chain behind P4's and the value probe's stubs as docs/68 rule 3 says. Hash `P4DoorKey`. A per-job 64-entry table gives repeat or first; a 256-entry ring with times gives the same key from another job within 1 s. Each repeat also records the host wait that ask paid (the stub sets a thread flag that `Proxy` reads).
- **Counters**:
  - `hcN[6]`, `hcQueueUs[6]`, `hcQueueMaxUs`, `hcServeUs`, `hcServeMaxUs`, `hcReturnUs`, `hcNestedUs`
  - `hcQueueSleepUs`, `hcSleepN` (limiter passes with a call pending)
  - `hcOtherCalls` (proxied calls without the door flag)
  - `hcDoorQ`, `hcDoorRepeatJob`, `hcDoorRepeatJobWaitUs`, `hcDoorRepeatDiff`, `hcDoorRepeatXJob`, `hcDoorRepeatXJobDiff`, `hcDoorFull`
- **Report.** `route-phases-report.ts` gets a "host-call split" section with:
  - per site: n, queue ms/s, mean;
  - service and hand-off ms/s;
  - queue in limiter sleep ms/s and its share;
  - door asks per job, repeat share, repeat wait ms/s (= H2's direct gain), answer changes;
  - cross-job repeat share (sizes H4);
  - `hcOtherCalls`.

  `--self-check` covers it.
- **Tests.** In the selftest `TestRouteJobPhases`: a fake proxy round trip per site; a fake limiter ring overlap; door-key repeat, diff and full. `steerro-test` STEERRO_PHASES with the stub (asks answered by K8's fake). `hook-sites --check`, `check-docs-code`.
- **Queue** `tools/harness/queues/host-call-probe.json` (to write with the build): `bp-hostcall-probe`, `ip-hostcall-probe`, day, 5 min, seed 12345, release defaults + `[RouteJobPhases] Enabled=1 HostSplit=1`. A noisy machine is fine for counts and the repeat share; the limiter share needs the external load check (others < 10%) or a clean-night rerun.
  - **Pass:** `rjpReady` 1, no crash, the summed split within 10% of the host phase (minus nested), `hcDoorRepeatDiff` 0.
  - **Decision:** build H2 if `hcDoorRepeatJobWaitUs` >= 4 ms/s on BP. Build H1 if `hcQueueSleepUs` >= 3 ms/s on a clean run. Consider H3 only if the busy-sim queue (queue minus sleep minus the pump sites) is >= 5 ms/s. Use the cross-job repeat share with 0 diffs to size H4 before docs/105's evaluator.
- **Cost:** about 1-2 h agent build and tests, one ~15 min day job pair; no clean night needed for the H2 decision.

## 6. What is measured vs inferred

| Item | Status |
|---|---|
| Host wait size, count, per job, max (own START) | measured (`[RouteJobPhases]`, one 5-min run per world, noisy) |
| All proxied calls are host delegate calls; no invokes, no worker sync plans, no borrows | measured (counters) |
| They are event 59 | verified static (direct calls); assumed live |
| ~2.1x asks per job vs per plan; repeats within a job | measured ratio; repeat share inferred |
| Service ~19-23 us per dispatch | inferred (sampler / docs/115 rate) |
| Queue delay 48-81% of the wait | inferred (rest after service + assumed hand-off) |
| Limiter sleep share of queue delay | unknown (probe) |
| Lock must stay held (mid-search pointers, pre-pass writes) | verified sites (docs/92); hazard inferred |
| Lending moves the wait | inferred (pump serves at once) |
| H2 exact; duplicate `LockedDoorsDuringPlan` adds harmless | verified readers (C#); key completeness open (probe `hcDoorRepeatDiff`) |
| Gains per fix | inferred (share x measured wait x slope 0.67) |

## 7. Built 2026-10-22: probe and H2 (`asi.0327`, counters v96, NOT run)

Ledger §255. Branch `worktree-agent-a63ff76b5fd8e7776`, `native/ts3native/hostcalls.cpp`; `game-copy/ts3native.asi.0327` (sha256 `EC958FA9E503218ADA0EC8E5AC325967804017584041DFD63F54E845B21C320C`) = master `c7859e55` + this; ts3native 0.36.23 (not bumped). Both switches off by default.

- **`[RouteJobPhases] HostSplit=1`** (needs `Enabled=1`), observe only. As §5 with these differences: the six serve sites are an argument of `ServeInvoke`; the stamps live in `InvokeJob` (post in `Proxy`, pick-up and done in `ServeInvoke`, wake after the wait loop; nested `ServePlan` time is taken out of the service); the limiter ring is a `SimPumpAdd` entry stamp and a `SimLoopAdd` exit stamp (4 entries, ~90 ms); a repeat's wait is timed by the door stub around its own ask (no `Proxy` flag needed). `hcOtherCalls` counts proxied calls the door stub did not make.
- **`[RouteAsync] DoorMemo`** = §4 H2, named so because `H2` is the locomotion-window switch. `0` not installed, `1` memo, `2` shadow. A door ask at `0x64F4D3` on the route worker inside a job (`t_hcJob`, set around `CallIc` in `ServeAsync`) whose `P4DoorKey` the job already asked returns the first answer from a 64-row per-job table. Passes through (asks as today, `dmFb_*`): any other thread or no job, a plan nested in a managed call (`g_invJob` set), a clone run, a full table. Shadow asks every repeat anyway and counts `dmMismatch`. Runtime switch `dmMode` +0x408C, `--ab doormemo`.
- **Stub chain** (docs/68 rule 3): behind P4's `P4DoorStub` when P4 owns the call (its jump slot `g_p4DoorJmp` now names `HcDoorStub`), else in `[ProbeValues]`' slot, else on the call's rel32. One stub serves both switches.
- **Counters v96** at +0x4088-0x411F: `hcReady`, `dmMode`, `hcN[6]`, `hcQueueUs[6]`, `hcQueueMaxUs`, `hcServeUs`, `hcServeMaxUs`, `hcReturnUs`, `hcNestedUs`, `hcQueueSleepUs`, `hcSleepN`, `hcOtherCalls`, `hcDoorQ`, `hcDoorRepeatJob`, `hcDoorRepeatJobWaitUs`, `hcDoorRepeatDiff`, `hcDoorRepeatXJob`, `hcDoorRepeatXJobDiff`, `hcDoorFull`, `hcDoorWaitUs`, `dmHits`, `dmMisses`, `dmFb[4]`, `dmShadow`, `dmMismatch` (offsets in docs/registry.md).
- **Tests.** Selftest `TestHostCalls`: a dummy call site and both chains; the memo, shadow and fall-through paths; per-job reset, full table, cross-job ring; stamps for all six sites; limiter overlap; a real two-thread proxy round trip. `steerro-test` with `STEERRO_PHASES=1` installs the stub in shadow mode on the mapped game image: 480 K8 plans equal and 0 mismatches, but K8's worlds reach **0 door asks**, so the live probe is the first real exercise of the ask path. `hook-sites --check` and `check-docs-code` are clean.
- **Report.** `route-phases-report.ts` prints a "host-call split (v96)" section with the probe verdict and the §5 gate (H2 when the repeat wait is >= 4 ms/s on BP with 0 answer changes; H1 when the limiter-sleep queue is >= 3 ms/s on a clean run).
- **Queue** `tools/harness/queues/host-call-probe.json`. Day: `bp-` / `ip-hostcall-probe`, 5 min, HostSplit=1 + DoorMemo=2. Night (clean, after both day jobs pass): `{bp,ip}-doormemo-{offfirst,onfirst}`, 10 min, DoorMemo=1 alone, `--ab doormemo`. The night jobs are read with `tools/harness/hitch-ab.ts`: per arm p99, p99.9, max iteration ms and counts >= 100 / 300 / 500 / 1000 ms, as both-order pair means.
- **Hitch expectation** [inferred, from §1 data]: one host wait is at most 96 ms (BP) / 60 ms (IP), and a job pays ~4.8 / ~7.3 ms in total. H2 removes 20-55% of that, about 1-5 ms per job. A >= 300 ms iteration would need several near-maximum waits stacked in one job. Those large waits are queue delay while the sim is already busy, so they are a symptom of a long iteration more than its cause. So H2 should change **few or no** >= 300 ms iterations directly: at most the ones sitting just above a threshold. It may trim p99 / p99.9. The night pairs decide this (I-159-13).
