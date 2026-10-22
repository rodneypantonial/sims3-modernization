# 154. Route job loop: state machines and a bounded model check (deadlocks, lost lends)

2026-10-22. Static only: our source at `1ff0a60` (ts3native 0.36.23 + TailUnlock) and the disassembly notes in docs/60, docs/86, docs/94 §21-§22, docs/134, docs/149 and docs/150 §11. No build, no game run. Tool: `tools/model/p5-lock-model.ts` (no deps, `node tools/model/p5-lock-model.ts --self-check`). Ledger: docs/51 §243.

Question: can the async route job loop (ServeAsync, the start handshake, the P5b / P5c / TailUnlock lend windows, H2's start hold, the drains) deadlock, lose a job or lose a lend, let the sim write while the worker reads live state, or let a save / destroy / close body run beside a job?

## 1. Result

21 model configurations, 300-66,000 states each, all as expected (`--self-check ok`). The base loop, H2, P5b, P5c and TailUnlock as designed are clean in the model: no deadlock, no lost job, every lend returned at `J_DONE`, the three Dekker pairs hold, and the replay consumer and a non-admitted main thread never get the lent lock. Five counterexamples, none in the paths that run every tick:

| # | What | Where | Verdict | Fix (proposed, not applied) |
|---|---|---|---|---|
| CE-1 | A job starts after `RT_CLOSING` drained, and runs beside the unlocked part of the world teardown | `ts3native.cpp:2127` checks `rtClosing` / `g_rgLock` only before the start handshake; `rtsites.cpp:250-256` sets `rtClosing` without a `g_rgOffSimOpen` frame | **real race**, narrow (claim-to-`A_RUNNING` gap: H2 hold up to 250 ms, `P5PreLock`, the acquire). Teardown steps that take `+0x918` still wait for the job. **Fixed** (§7) | A: re-read `rtClosing`, `rtBusy` and `j->lock != g_rgLock` after `A_RUNNING` (`:2148`), back off as `J_SKIPPED` |
| CE-2 | Deadlock: a managed call from a P5 clone run (tripwire `P4_AB_MANAGED`) is proxied to the sim while the run stays armed; a sim D2 entry (destroy, save, close, coarse gate) spins in `P4NrEnter` until `SnLive()` is false and never serves it | `ts3native.cpp:1987-1992` (InvokeHook: `P4Abort`, then `Proxy`), `p4.cpp:325-340` (`while (SnLive()) Sleep(0)`), `rtsites.cpp:169-171` | **real deadlock behind a tripwire** (Mode=2, with or without ReleaseLock). Door asks are safe: `P4DoorEnter` disarms before its call (`p4.cpp:421`) | B: `P4NrEnter` serves the job's managed calls while it spins on the owner thread (`WaitForSingleObject(g_invReady, 0)` -> `ServeInvoke`, as the READY poll does at `:2882`) |
| CE-3 | Torn read: a P5c-admitted sim writer reaches a D2 entry inside its write; `P5cHandBegin` hands the lent lock back mid-write; the worker retakes, the answer is stale, and the game's own plan reads the half-written state. Second shape: the job ends, the next queued job starts before `P5cHandEnd` has the levels back, and reads it too | `p5c.cpp:210-237`, `p5.cpp:221-240`, `ts3native.cpp:2142` | **real if the hand-over ever fires** (it is the counted tripwire `p5cHandOver`, FreeLock off after the first); docs/94 §22.11 covers deadlock, not write atomicity | C: on hand-back (1) hold `g_rgOffSimOpen` until `P5cHandEnd` has every level back, (2) mark the job so P5Plan skips the game plan after the retake and the job ends `J_SKIPPED` (the Sim plans synchronously after its writer finished) |
| CE-4 | Lost lend: an unwind through passes 16-19 skips `TuClose`; ServeAsync then releases a lock it does not own | `tailunlock.cpp:103-123` (close only on the normal returns), `ts3native.cpp:2190` | **latent**: needs a C++ or SEH unwind caught inside `Route_PlanLocked` [unknown whether the game has one there]; a crash otherwise. **Fixed** (§7) | D: backstop in ServeAsync after `CallIc`: `if (t_tuOpen) { count; TuClose(); }` (and the same for `t_p5b == P5B_OPEN`) |
| CE-5 | Deadlock by lock order: a tripwire retake of `+0x918` while the worker holds `WM+0x228` (inside the `#5` lot-grid stub or `0xC68680`), against the sim's admitted steering that holds the lent `+0x918` and waits for `WM+0x228` in a dirty lot-index rebuild | `tailunlock.cpp:93-100` (`TuTrip` from `LockAcquireHook :2903`), `p5b.cpp:148-174` (`P5bRetake` from `P4Abort` livelock), docs/60 §1.4 #5, docs/94 D1 | **model artefact today**: TU needs a `+0x918` acquire under `WM+0x228` in passes 16-19 (none in 166 functions, direct calls, I-150-11-4; 15 unresolved indirect leaves); P5b needs a live-lock tripwire under `0x82C420` in a clone run; both also need a dirty lot index during the window | E (optional): inside a window, when `WM+0x1CC` is set, take the hold back before `WM+0x228` (game order `+0x918` -> `WM+0x228`); minimum: the retake watchdog lines name a `WM+0x228` owner |

Mutation checks: a gate that reads `A_RUNNING` before publishing `g_rgOffSimOpen` is caught (sim write during a live read), and a `WaitJobDone` without `G0wDrop` deadlocks against H2 without the watchdog and is clean with it. So the checks bite.

## 2. State machines

Notation: `file:line` is our source; game addresses carry [verified] (disassembly in the cited doc) or [inferred].

### 2.1 Locks and shared words

| Name | What | Taken by | Rule |
|---|---|---|---|
| `+0x918` | routing lock, recursive `CRITICAL_SECTION` at `+0`, count `+0x20`, flag `+0x24`; `Lock_Acquire 0x4E16F0` returns the count, `Lock_Release 0x4E17B0` [verified prologues, `ts3native.cpp:2993-2996`, `routegate.cpp:47-48`] | 30 game sites (docs/60 §1.2 [verified]); worker: ServeAsync's hold + `Route_PlanLocked 0x82C6E0`'s own (count 2 at pass 16, I-150-11-1 [verified]) | every game acquire goes through `LockAcquireHook :2901` -> `LockAcquireAsync :2940` -> `P5bLock p5b.cpp:80` |
| `g_rgOffSimOpen` | count of open non-worker routing frames | gates `routegate.cpp:284`, QT writers, destroys, saves, the pump `:2962`, DiscDrain `discourage.cpp:169`, P5cFlush `p5c.cpp:161` | Dekker with `A_RUNNING`: frame `++` then read state; worker writes `A_RUNNING` then reads the count (`:2146-2148`) |
| `g_async.state` | `A_IDLE` / `A_RUNNING` | worker only | "a job body is live"; stays `A_RUNNING` through a lend |
| `g_jobs[i].state` | `J_FREE -> (J_PEND) -> J_QUEUED -> J_RUNNING -> J_DONE / J_SKIPPED -> J_FREE` | sim: queue `:2786`, take back by CAS `:2471-2472`, free; worker: CAS `Q->R :2125`, `R->K :2130`, `R->D :2210` | the CAS pair makes a cancel and a claim exclusive |
| `g_asyncReady` | auto-reset event | sim `:2792`; worker waits `:2218` | ServeAsync rescans until nothing is queued, so a set during the scan is kept: no lost wakeup (model checks it) |
| `g_p5bWin` / `g_p5bIn` | lend window open / disallowed acquirers inside the game's `Lock_Acquire` | worker opens (`p5b.cpp:123`, `tailunlock.cpp:45`); acquirers count (`p5b.cpp:96`) | Dekker: window published, then the in-count read; acquirer counts, then reads the window |
| `g_p5cJob` | P5c admission for plain sim writers | worker `p5c.cpp:43,51` | set before the window, cleared after the retake |
| `g_g0Win` / `g_g0wIn` | H2 start hold / sim inside `Locomotion_Update` | sim thunk `locowin.cpp:45-55` | Dekker with `A_RUNNING` (`:2142-2148`); drops at `WaitJobDone` and sync worker plans (`locowin.cpp:29`); 250 ms watchdog (`:2036`) |
| `g_invReady` / `g_invDone` | managed-call proxy | worker `Proxy :1971`; sim `ServeInvoke :1938` | the sim serves only inside its waits: WaitAsyncDone, WaitJobDone, the pump, RunPlan, the READY poll |
| `g_rpUse` | SRW, clone exclusive | replay consumer `replay.cpp:3141`; worker `PcAcquire p4clone.cpp:843` (bounded try) | neither waits for `+0x918` while holding it (`replay.cpp:3154`) |
| QL `g_ql` | SRW | worker exclusive in W1-W6 outside clone runs (`steerro.cpp:124`); sim shared by try only (`steerro.cpp:686`, `slotro.cpp:71`, `avoidro.cpp:93`) | the sim never waits for QL and drops it before any `Lock_Acquire` (`QlSharedTrip`); no wait edge |
| `WM+0x228` | world manager lot-index lock [verified docs/60 §1.1] | worker `#5` stubs around `0xC688A0` / `0x82C420` (also in clone runs, docs/94 D1); anyone rebuilding a dirty index in `0xC68680` | game order `+0x918` then `WM+0x228` [inferred from the callers]; see CE-5 |
| EA heap CS (`+0x4E4`, docs/131) | allocator | everyone | leaf: nothing waits for a job or `+0x918` while holding it [inferred, docs/131 §9]; modelled as atomic |

### 2.2 Sim thread

Sim-side `+0x918` acquire classes (P5b admission `p5b.cpp:69`):

| Class | Example | Off window | In a P5b / TU window | P5c window |
|---|---|---|---|---|
| steer | `0x82EDB0` / `0x82F0B0` (`t_steerRet`, `rtsites.cpp:243`) | pump if a job is busy | admitted | admitted |
| plain writer | USF `0x833560`, footprint writers, discourage | pump | waits | admitted (`P5B_FREE`) |
| gate wait | `RgEnter` with a job running (`routegate.cpp:295`, `t_waitKey`) | pump | read leaves only with ReleaseGates | as P5b |
| borrow | any acquire inside a callback for the job (`:2910`) | count only, no real acquire | same (also in a window: the lock is free, but every non-admitted thread is held off and the worker waits in `Proxy`) | same |
| our drains | DiscDrain, DiscOnDestroy, P5cHandEnd, FbApplyPending (try) | after the job wait | n/a (no job) | n/a |

Transitions (one atomic step each in the model):

| From | Event | To | Source |
|---|---|---|---|
| tick | START (READY channel) | job `J_QUEUED`, `g_asyncReady` | `:2711`, `:2768-2792` |
| tick | READY poll, job queued/running | serve one pending managed call, "not yet" | `:2881-2883` |
| tick | collect `IcCollect` | `Q`: take back + sync plan; `R`: `WaitJobDone`; `D`/`K`: take result or plan sync | `:2625-2668` |
| any | `Lock_Acquire(+0x918)` | owner and job busy: try, then pump (`g_rgOffSimOpen++`, 1 ms waits serving calls, retry) until the try wins or no job is left; else blocking `P5bLock` | `:2940-2985` |
| any | gate entry | `g_rgOffSimOpen++`, read `A_RUNNING`; running: acquire (pump) and hold for the frame | `routegate.cpp:284-306` |
| any | destroy | `P5cPark` (window + P5c, or DestroyPark) else D2 (`P5cNrEnter`) and `RtWaitJob`; frame `g_rgOffSimOpen` | `rtsites.cpp:161-171, 204-215` |
| any | save / BRD / lot save | `rtBusy++`, `g_rgOffSimOpen++`, D2, `RtWaitJob`, body, both `--` at leave | `rtsites.cpp:258-264` |
| any | closing | `DiscDrain`, `rtClosing = 1`, D2, `RtWaitJob`; **no frame** | `rtsites.cpp:250-256` |
| pass | `Locomotion_Update` (`0xD81C5C` [verified]) | `g_g0Win = 1` (H2), `g_g0wIn = 1`; leave clears both | `locowin.cpp:36-62` |
| wait | `WaitJobDone` | `G0wDrop`, `P5cHandBegin`, serve calls until the job is not `Q`/`R`, `P5cHandEnd` | `:2440-2456` |
| wait | `WaitAsyncDone` | `P5cHandBegin`, serve calls until `A_IDLE`, `P5cHandEnd` | `:2424-2437` |
| wait | `P4NrEnter` (D2) | spin `Sleep(0)` while `SnLive()`; **serves nothing** | `p4.cpp:325-340` |
| wait | RunPlan, own job running | `P5cPlanHere` if the lent lock is ours, else `WaitAsyncDone` | `:2250-2262` |

### 2.3 Route worker (ServeAsync)

| From | Event | To | Source |
|---|---|---|---|
| idle | `g_jobReady` | sync plan (`ServePlan`) | `:2218-2222` |
| idle | `g_asyncReady` | scan | `:2218-2222` |
| scan | oldest `J_QUEUED` / none | claim / return (P4 idle shadow) | `:2116-2124` |
| claim | CAS `Q -> R` fails | scan (cancelled) | `:2125` |
| claim | world check fails (lock, `rtClosing`, `rtBusy`, pending, dirty index) | `J_SKIPPED` | `:2127-2133` |
| claim | ok | `P5PreLock` (bounded wait for the consumer), `TwArm(true)` | `:2136-2137` |
| spin | `g_rgOffSimOpen == 0` and no H2 hold (or watchdog) | acquire `+0x918` (blocking) | `:2142-2143` |
| held | `A_RUNNING` published, re-read frames and H2 | frame or hold: `A_IDLE`, release, `SetEvent(g_asyncDone)`, spin; else run | `:2146-2153` |
| run | `CallIc`: start points, `Route_PlanLocked` (second level), result mapping | live reads; managed calls proxied (`Proxy :1971`) | `:2173` |
| run | P5 Mode=2 (`P5Plan p5.cpp:180`) | §2.4 window, then answer or game plan | `p5.cpp:199-240` |
| run | TailUnlock, post pass `0x64E380` | §2.4 window around passes 16-19 | `tailunlock.cpp:103-123` |
| run | FbJobCapMs (foreign jobs) | A* deadline from t0; ends the search early, no lock change | `:2163-2170`, docs/134 §11 |
| done | release, `A_IDLE`, `TwArm(false)`, `J_DONE`, `SetEvent(g_asyncDone)`; then the P4 shadow on copies | scan | `:2190-2214` |

### 2.4 Lend windows

One shared window word (`g_p5bWin`, `g_p5bLock`, `g_p5bN`); P5b/P5c and TailUnlock never open it in the same job (`tailunlock.cpp:39-40`: refused in a clone run or with P4 installed, and P5 needs P4).

| State | Event | Next | Source |
|---|---|---|---|
| closed | P5b open (clone run, taken job) / TU open (pass 16) | publish `g_p5bWin`; up to 200 us for `g_p5bIn == 0`, else refuse; check `RecursionCount == [+0x20]` in 1-16; release every level | `p5b.cpp:111-145`, `tailunlock.cpp:32-62` |
| open | admitted sim acquire | sim owns it (counted) | `p5b.cpp:83-89` |
| open | disallowed acquire | try = -2; wait = sleep while the window is open, then the game's wait (the worker's hold again) | `p5b.cpp:95-107` |
| open | clone run ends / pass 19 / a pass fails | retake: try loop (only an admitted holder can have it; 2 s watchdog log), then the other `n-1` levels recursive, close | `p5b.cpp:148-174`, `tailunlock.cpp:66-91` |
| open | door ask | retake (`P5B_ASK`), `P4Disarm`, ask (proxied), `P4Arm`, reopen | `p4.cpp:410-434`, `p5b.cpp:188-196` |
| open | tripwire: managed call, event, live `+0x918`, lazy stop (P5b); worker `+0x918` acquire, gate, routing site (TU) | retake, no reopen | `p4.cpp:248`, `p5b.cpp:198-205`; `:2903`, `routegate.cpp:245`, `tailunlock.cpp:93-100` |
| open (P5c) | sim holder waits for the job / clone run | `P5cHandBegin`: release its levels, FreeLock off; after the wait retake through the pump | `p5c.cpp:210-237` |
| open (P5c) | destroy | park one reference, run now; flush only at no-job points (owner, `A_RUNNING` false under `g_rgOffSimOpen`) | `p5c.cpp:96-192` |
| closed (P5c) | answer | refused if any recorded write since the START cut (`P5cStale`), game plan instead | `p5c.cpp:54-63`, `p5.cpp:233-238` |

### 2.5 Other actors

- **Replay consumer** (`replay.cpp:3139`): `g_rpUse` exclusive, then a `+0x918` try (non-admitted in a window: fails); busy = release `g_rpUse`, `Sleep(1)`, again. The worker waits for it only bounded (`P5PreLock p5.cpp:141`, `PcAcquire p4clone.cpp:820-858`).
- **Main / EA threads**: world init `0x829410` and the debug frame `0x818EA0` take `+0x918` (docs/60 §1.2 [verified]); never admitted; `P5bLock`'s in-count keeps a blocking waiter from getting a window (model: `p5b-main-rp`, `tu-main`).
- **Sync worker plans** (`[RouteWorker] Mode=1`, default 0): not in the model. A sync worker plan made while the sim holds `+0x918` would wait for a worker that is itself blocked in ServeAsync's acquire (`g_jobReady` is served only after ServeAsync returns). G10-C keeps the gate-10 frame's plan on the sim (`:2263`); a plan under a plain sim hold is not known to exist (`Route_PlanLocked` takes the lock itself [verified docs/60 §1.2]) [inferred, unmodelled].

## 3. The model

`tools/model/p5-lock-model.ts` enumerates every interleaving (explicit-state BFS) of: the sim (a script of up to K ops chosen from start, collect, steer, writer, gate, destroy, save, close, pass with nested ops), the route worker (§2.3 / §2.4 step by step), and optionally the replay consumer and a main thread. One step = one interlocked operation or one guarded wait; spins are steps enabled only when their condition holds, so a spin with no exit shows up as a deadlock. Two job slots, one world, infinite deadlines.

Checks per state: sim write classes (live, steering cache, callback, lifetime) against the worker's phase (live, clone, tail, proxy); a live read of a sim write that is half done; a save / destroy / close body beside a job body; a non-main thread getting the lent lock; a release by a non-owner (lost lend); window open at `J_DONE`; the H2 watchdog firing when the config says no watchdog. Terminal states: no job `QUEUED` with the worker asleep, no `RUNNING`, every word back to 0.

| Config | Feature | States | Result |
|---|---|---|---|
| base | A10, gates, save, destroy | 28,639 | clean |
| base-main-rp | + main thread, replay consumer | 65,950 | clean |
| h2 | H2 hold, pass with steer / gate / collect | 24,715 | clean |
| close / mut-close-recheck | closing (fix A in the code since §7; the mutation is the old code) | 2,386 / 798 | clean / CE-1 |
| p5b | P5b, door ask, tripwire | 12,372 | clean |
| p5b-main-rp | Dekker vs a blocking main waiter, consumer | 20,522 | clean |
| p5b-mgd-d2 / fixed | managed-call tripwire + D2 | 388 / 6,861 | CE-2 / clean |
| p5c | P5c writers, park, stale | 13,603 | clean |
| p5c-handback / fixed | writer with a nested D2 | 366 / 4,609 | CE-3 / clean |
| tu, tu-main | TailUnlock, WM in window, FbJobCap | 7,083 / 1,679 | clean |
| tu-throw / mut-tu-nobackstop | unwind in passes 16-19 (fix D since §7) | 341 / 172 | clean / CE-4 |
| tu-wm-trip / fixed, p5b-wm-trip | tripwire under `WM+0x228` | 341 / 451 / 362 | CE-5 / clean / CE-5 |
| mut-gate-order, mut-h2-nodrop, h2-watchdog | mutations and control | 154 / 703 / 4,932 | caught / caught / clean |

## 4. Counterexamples in detail

**CE-1 (close race).** Trace: `S:START j0`, `W:claim`, `W:world check` (rtClosing 0), `S:rtClosing = 1`, `S:RtWaitJob` (A_IDLE, returns), `W:acquire`, `W:A_RUNNING`, `W:re-read frames` (none), `S:unlocked teardown` while the job runs. In the code the gap is everything between `:2127` and `:2148`. What protects the game today: teardown steps that take `+0x918` wait for the job, destroys keep their own frame and wait, and P5 jobs re-check `rtClosing` in `P5Plan` (`p5.cpp:193`). What does not: unlocked teardown (docs/60 §1.5 lifecycle, `RT_TEARDOWN` sets `g_rgLock = NULL` only when no job is live, `rtsites.cpp:236-241`). Fix A is two lines in the start loop (same Dekker order as `g_rgOffSimOpen`: `rtClosing` is set with `InterlockedExchange` before `RtWaitJob` reads the state).

**CE-2 (managed-call tripwire vs D2 spin).** Trace: job running, `S:destroy` enters D2 (`P5cNrEnter`; nothing lent), `W:clone run`, `W:managed call: P4Abort -> P5bAbort` (retake), `W:proxied (clone run still armed)`; the sim spins in `P4NrEnter` (`SnLive()` true), the worker waits in `Proxy` with no timeout. Same with ReleaseLock off. Door asks avoid it only because `P4DoorEnter` calls `P4Disarm` before the proxied call. The tripwire is counted (`snCloneManaged`, `m2Trips`, P5 off after `kP5Trips`) but the counting never runs if the game hangs on the first one. Fix B keeps the clone armed (the rest of the run must still read the copy) and lets the spinning sim serve the call; the callback then borrows the job's hold as in any job.

**CE-3 (hand-back mid-write).** Trace: `S:writer acquires` (P5c admitted), `S:first half written`, `S:nested D2 entry: hand back`, `W:clone run ends`, `W:retake`, `W:P5cStale` (stale), `W:game plan reads live` with the writer half done. The hand-back is P5c's designed exit from the deadlock (docs/94 §22.11), counted and turning FreeLock off, so this happens at most once per session; but that once, the job's fallback plan and any queued job that starts before `P5cHandEnd` read a world the sim is in the middle of changing (for USF `0x833560`: footprints removed, not yet re-inserted, docs/89). Fix C: `P5cHandBegin` also does `InterlockedIncrement(&g_rgOffSimOpen)` (released in `P5cHandEnd` after the last level), and sets a per-job flag that makes `P5Plan` return a failed plan without `PlanLockedBody` and ServeAsync mark the job `J_SKIPPED` (collect then plans synchronously, after the writer). The IC's own tail after `Route_PlanLocked` still runs under the hold; that it reads only job-private data is [inferred].

**CE-4 (unwind without TuClose).** Only if something catches an exception between pass 16 and ServeAsync; otherwise the worker dies and the game crashes anyway. Fix D costs one branch.

**CE-5 (WM+0x228 order).** The worker's retake (`P5bRetake` / `TuClose`) spins for `+0x918` with no timeout. If it runs inside a `WM+0x228` scope, and the sim's admitted steering holds the lent lock and needs `WM+0x228` (only when the lot index is dirty: `WM+0x1CC`, set by lot load, houseboat create, `0xA48CC0`, docs/94 N2), neither moves. Both triggers are tripwires that the static closures say never fire (TU: I-150-11-4; P5b: a clone run reaching the live `+0x918` is a design breach). Kept as a model artefact; fix E or at least the diagnostic.

## 5. What the model leaves out

- Finite deadlines (`Lock_Acquire` with a tick deadline, the pump's last try), ReleaseGates admission of gate-0 hits and gate-12 reads, M1Retry `J_PEND`, `kDoneKeepMs` reclaim, A10 kind mismatch, the P4 shadow after `J_DONE` (copies only), FbApplyPending.
- Sync worker plans (§2.5), more than two job slots, more than one world, the sim's own non-routing work.
- The game: every game function is one abstract read or write. Lock order in the game outside `+0x918` / `WM+0x228` is taken from docs/60 §1.2 ("no reversed order", [verified] for `+0x918`) and is not re-derived.
- A clean config proves the absence of these counterexamples within its bound (K sim ops, 2 jobs), not in general.

## 6. Next

1. Fix A and fix B are small and close the two reachable problems; both belong in the next native build with a selftest each (TestFallback-style: a START claimed, then `rtClosing` set before `A_RUNNING`; a fake clone run that proxies a managed call while a D2 entry spins).
2. Fix C before FreeLock goes into a release config. (Fix A and fix D: built, §7.)
3. Re-run `tools/model/p5-lock-model.ts --self-check` after any change to ServeAsync, P5b / P5c, TailUnlock or the drains; add a config when a new lend or wait site appears.

## 7. Fix A (CE-1) and fix D (CE-4) built (2026-10-22)

Branch `worktree-agent-ac69be24e3fdb153f`, parked asi `game-copy/ts3native.asi.ce1fix` (sha256 `1346F13CAD658FD6284F07B7A26E1A7554649CD714C191A917A431B1B8DD705F`; version not bumped; the integrator sets it). No game run.

**CE-1, race confirmed in the source [verified].** `ServeAsync` refuses a claimed job on `j->lock != g_rgLock || k.rtClosing || ...` (`ts3native.cpp:2141`), then spends an unbounded gap (`P5PreLock`, the H2 hold up to 250 ms, the `+0x918` acquire) before it publishes `A_RUNNING` (`:2160`), and after it re-reads only `g_rgOffSimOpen` and the H2 hold. `RT_CLOSING` (`rtsites.cpp:250-256`) sets `rtClosing` and calls `RtWaitJob`, which returns at once while the state is not `A_RUNNING` (`rtsites.cpp:76-79`); it opens no `g_rgOffSimOpen` frame. So a close that lands in the gap is drained, returns to the game, and the job then runs its IC beside the teardown.

Fix A (`ts3native.cpp:2161-2182`): after `A_RUNNING` (`InterlockedExchange`, full barrier) the worker re-reads `j->lock != g_rgLock || k.rtClosing`. `RT_CLOSING` writes `rtClosing` with `InterlockedExchange` before `RtWaitJob` reads the state, so either the drain sees `A_RUNNING` and waits for the job, or the worker sees `rtClosing` and backs out: `A_IDLE`, release, `TwArm(false)`, `asyncSkipped` +1, `J_SKIPPED`, `g_asyncDone`, exactly the early refusal's exit, so the collect plans synchronously (`ts3native.cpp:2898`). `rtBusy` is not re-read: saves, BuildRoutingData and lot saves hold a `g_rgOffSimOpen` frame for their whole body, which the existing re-read already covers (a skip there would only drop jobs that are fine after the save).

Test: `selftest.cpp` `TestLocoWindow` case 9 forces the interleaving with H2: a job is claimed inside a locomotion pass (Mode 1), passes the world check and spins in `G0wHolds`; the pass then runs `RT_CLOSING`'s body (`rtClosing` = 1, `RtWaitJob` returns without waiting) and ends; the job starts. Old code: `CE-1 FAIL: job state 3, IC ran 1 time(s) after the closing drain` (assert). Fixed: `J_SKIPPED`, IC not run, the route's plan is the game's sync answer, `+0x918` free.

**CE-4, fix D.** `LendBackstop()` (`tailunlock.cpp`): if a TailUnlock window (or a P5b window, after the clone run) is still open on the worker, take every level back (`TuClose`, `P5bEnd`), one log line. Called from `LockReleaseHook` when the worker releases the job's lock with `t_tuOpen` set (`routegate.cpp:139`: covers a handler inside `Route_PlanLocked`, whose own release would otherwise leave a lock it does not own) and after `CallIc` in `ServeAsync` (`ts3native.cpp:2203`). No new counter (counter layout unchanged). Test: `tailunlock-test.h` CE-4 job: fake pass 17 raises inside the window, caught in the worker's Route_PlanLocked frame, then its release: with the backstop the worker owns both levels first (count 1 after the release, no open window); with `LendBackstop` disabled the selftest fails (`TailUnlock CE-4 FAIL: threw in a window 1, bad 1`). The lock level an unwind skips in the game itself (Route_PlanLocked's own release, if the handler is above it) stays the game's.

Model: fix A and fix D are now the default transitions in `tools/model/p5-lock-model.ts` (`close`, `tu-throw` clean); the old code is kept as mutations `mut-close-recheck` (CE-1) and `mut-tu-nobackstop` (CE-4); `--self-check ok`, 22 configs.
