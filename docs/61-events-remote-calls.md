# 61. Events and remote calls: MessageServer, JobManager remote calls, EventTracker (T-EV)

Static only: Ghidra headless on a private copy of the UI4 project (`game-copy/ghidra-ev`, symbols CSV imported first), a byte scan of the Steam 1.67 `TS3W.exe`, and a read of the decompiled ScriptCore, SimIFace and Sims3Gameplay* assemblies. The game was not run. Image base `0x400000`, absolute addresses. Builds on docs/36 (JobManager, sim pump `0xD81FC3`), docs/28 §5 (lot streaming path), docs/38, docs/48 (render messages), docs/50 (lot add, event `0x14`), docs/59 (autonomy) and docs/29/30 (one managed thread).

Tags: **[verified]** = read in the decompile, disassembly or managed source; **[inferred]** = reasoning, still to confirm.

## 1. Summary

- **Two delivery systems, not one.** Cross-thread work goes either through **JobManager remote calls** (`RemoteCall_Dispatch 0x7D98C0`: a job with mask 4 = sim or 1 = main) or through an **EAMessage server** (`Post` to a queue drained by the owner thread's service update, or `Send` = synchronous call on the caller's thread). There are two EAMessage server instances of one class (`EAMessageServer_ctor 0x44CC70`, vtable `0xFA1AF8`): `g_MessageServer 0x11B1378` (queue drained on the **main** thread) and `g_ScriptMessageServer 0x11D4BC0` (queue drained on the **sim** thread) [verified].
- **Same-thread remote calls run inline, before anything already queued** [verified, `0x7D98E9`]: async or not, if the caller is already the target thread, `RemoteCall_Dispatch` calls the call object's `+0x10` at once and returns. A sim-thread re-post with `PostRemoteMethodCall` therefore never reaches the pump.
- **A re-posted *job* also runs in the same pump pass** [verified, `0x599E60`]: the sim pump loops until its own queue and the shared queue are empty, rechecking the queue head after every job, so a job submitted from inside a job lands at the tail and runs before the pump returns. A job re-post alone does **not** move work to the next pass; it needs a deadline (docs/36 C1) or our own deferred list (§4).
- **Ordering:** remote calls to one thread run in post order (priority 0 for all of them, priority insert keeps FIFO among equals); shared-queue jobs run only when the own queue is empty or when they have a strictly higher priority [verified]. Two exceptions: the inline same-thread case, and main-thread calls that get parked behind an in-flight rendering job (`RemoteCall_JobFn`) [verified shape].
- **Help-while-wait re-enters:** any thread with a JobManager record (main and sim both have one) that waits for a synchronous job runs its own queued jobs one at a time meanwhile (`JobManager_WaitLocked 0x59A220`) [verified]. So queued lot adds can run nested inside a sim-to-main synchronous call (lot load, lot removal, thumbnails), not only at `0xD81FC3`.
- **The lot-add script message scope has no listener.** `0x4C55E8C` / `0x4C55EFA` occur in the whole exe only as the two pushes at `0xAC1150`/`0xAC1155`; no handler table or `AddHandler` call names them, and no wildcard handler is registered on the script message server [verified: byte scan, scalar scan, 47 handler registration sites]. They are native EAMessage ids, not managed messages: no managed type references them [verified: grep]. At run time the scope is two hash lookups under the server lock [inferred: a mod could register at run time].
- **`EventTracker` (managed) has no lock and is re-entrant on one thread only.** Listener storage is a two-level dictionary (event id, then actor object id or 0), a single shared "active lists" stack defers adds/removes during iteration, and a catch-all removes any listener that throws [verified]. No managed code in the game assemblies takes a lock or creates a thread [verified: grep]. **Verdict: events must be raised on the sim thread; a second managed thread may only queue event requests for the sim thread to send** (§5).

## 2. Native semantics

### 2.1 Table

| Mechanism | Entry | Who delivers, on which thread | Same-thread call | Ordering | Lock |
|---|---|---|---|---|---|
| Remote call, async ("post") | `RemoteCall_Dispatch(selector, 1)` `0x7D98C0`, wrappers `Lot_PostRemoteCallAsync 0xABE9C0`, `RemoteCall_PostAsync 0xC649A0`, `PostRemoteMethodCall_*` | target record's pump: sim = `0xD81FC3` once per Simulate iteration (no deadline) and inside sim help-while-wait; main = `JobManagerService_Update 0x599A10` (timeslice) and main help-while-wait | **inline at once** (`+0x10`), returns the call's result byte `+0x08` | FIFO per target (priority 0); overtaken by inline same-thread calls | `g_jobManagerLock 0xFD9744` around queue ops; dropped while a job body runs |
| Remote call, sync ("send") | `RemoteCall_Dispatch(selector, 0)`, `RemoteCall_PostSync 0xC64950`, `Lot_PostRemoteCallSync 0xABE970`, `RunOnSimThreadSync 0x82AA70` / `RunOnMainThreadSync 0x82ACF0` | same pumps; the caller waits in `JobManager_Wait 0x59A500` | inline | as async; the caller runs its own queue while it waits | same |
| JobManager job (mask 1/2/4/-1) | `JobManager_CreateJob 0x599620` + `Job_Submit 0x59A350` | first record whose mask matches (`ReadyJob 0x599FC0`); -1 = shared queue (`JobThread0`, main pump, sim pump) | queued, **not** inline; a job submitted during the pump runs in the same pass | priority descending, FIFO among equals (`JobQueue_InsertByPriority 0x599920` walks back over lower priorities only); each ready job gets a 64-bit sequence number (`JM+0xA8`) | `0xFD9744` |
| EAMessage `Send` | vtable `+0x18` = `EAMessageServer_Send 0x44D5A0` (id, data, handler) | **caller's thread, synchronously**; with an explicit handler it calls only that one | n/a (always on the caller) | wildcard (-1) handlers first, then the id's handlers in priority order [verified insert by priority; tie order inferred LIFO] | `g_EAMessageServerLock 0xFA1AC0` while the handler list is copied to a stack array (options 1/2 on); handlers run **outside** the lock |
| EAMessage `Post` | `+0x1C` `0x44CEE0` (handler object) / `+0x20` `0x44CF80` (callback fn) | the owner's `ProcessQueue +0x38` (`0x44D170`): main for `g_MessageServer` (service slot `+0x1C` `0x588890`), **sim** for `g_ScriptMessageServer` (service slot `+0x20` `0x76B1A0`; `+0x1C` is the stub), plus direct flushes from lot code `0xACE4A0` and `0xAE0710` | queued even on the owner thread | FIFO; **option 4 is switched off by both inits** (`SetOption(4, 0)` in `0x5887F0`/`0x76B100`), so one `ProcessQueue` delivers only the messages queued when it started; anything posted during delivery waits for the next call | `0xFA1AC0` around push/pop; delivery outside it |
| Timed processing | `+0x34` `0x44D320` (min, max, ms) | repeats `+0x38` with a `GetTickCount` budget after `min` rounds | | | option 3 |
| Render messages | `PostRenderMessage 0x692F70` -> `RenderMessageBuffer_Post 0x593E60` | main, in the FrameDb drain (`RenderMsg_Dispatch 0x693170` -> `g_MessageServer` **Send**) (docs/48) | queued | append order | `FrameDb+0xD0` (post), `FrameDb+0x48` (drain) |

All [verified] except where a row says inferred. The script message server's service flag bit 1 (sim slot enabled) is not read; it must be set, or posted script messages would only be delivered by the two direct flushes [inferred].

### 2.2 Details worth knowing

- **Remote call objects** (`Services/RemoteMethodCall`, 0x20 bytes, vtable `0x10650C4`; remove variant `0x10650D8`): `+0x0C` target, `+0x10` member function, `+0x14` this-adjust, `+0x18`/`+0x19` two byte arguments, `+0x08` done/result byte. `RemoteMethodCall_Execute 0xABD3C0` calls the function and sets `+0x08` [verified]. The async path keeps the object alive through `Job_SetRefObject 0x599260` (AddRef on the job) and returns 1 at once [verified].
- **`RemoteCall_JobFn 0x7D9840`**: the `4` in its third argument is the job "execute" opcode that `ExecuteJob` always passes, not the thread mask. The flag it tests is bit 0 of the job's mask (`job+0x28`), i.e. **main-thread** calls. When `[0x11D1530]` is set (rendering jobs in flight [inferred]), it may add a dependency on a rendering job (`RenderingJobs_Pop 0x6E33F0`) and return; `ExecuteJob` then sees a dependency and moves the job back to the wait list, so it reruns after that rendering job [verified shape]. Sim-thread calls (mask 4) never take this path. This closes docs/36 §10 item 3 and means main-thread remote calls are **not** strictly FIFO while rendering jobs run.
- **Job re-arm states**: after a body returns, a job with new dependencies goes back to the wait list (state 2); a job left in state 5 is readied again (`ExecuteJob` -> `ReadyJob`) with a new sequence number [verified]. Both re-queue at the tail of the same queue, so in the sim pump they run again **in the same pass** [verified loop].
- **Pump loop** (`JobManager_RunJobsLocked 0x599E60`): per iteration, move the shared head into the own queue if the own queue is empty or the shared head has a strictly higher priority, run the own head, then return if `single`, if the deadline passed, or (nonblock) if both queues are empty. The sim pump passes deadline 0 and nonblock 1 [verified docs/36, re-read here].
- **EAMessage options** (`+0xB0..+0xB5`, all 1 after the ctor): 1 = lock, 2 = snapshot the handler list before calling, 3 = timed processing allowed, 4 = keep processing messages posted during `ProcessQueue` (**0** after init), 5 = AddRef/Release handlers and payloads around delivery [verified for 1-5 from the reads; names are ours]. Because handlers run from a snapshot outside the lock, a handler may send, post, add or remove handlers re-entrantly; a handler removed during a `Send` can still be called once from that snapshot [inferred from the copy].
- **One lock for every server**: `0xFA1AC0` is a static used by all `EAMessageServer` methods, so main-thread and sim-thread posts and sends contend on the same lock [verified]. Hold times are a list copy or a queue push/pop [verified shape].
- **docs/36 correction**: `0x76B1A0` (listed there as the `Services/Script` sim slot) is slot `+0x20` of the **ScriptMessageServer** service vtable `0x1007B74`; `ScriptServices_Register 0x76B540` creates both services [verified].
- **docs/42 correction**: `CAS_PostOutfitSwapMsg 0x5ADD40` posts `0x30290F0` to the **script** message server (`0x7692C0`, slot `+0x1C`), with its handler registered at `0xD77730`; the CAS callback is therefore delivered on the sim thread [verified site scan; docs/42 attributed it to `MessageServer_Get`].

### 2.3 Script message server traffic (static site scan)

Callers of `ScriptMessageServer_Get 0x7692C0` by vtable slot used next: 47 posts (`+0x1C`), 43 handler registrations (`+0x24`, 8 of them from id tables such as `0x1064D54` in the lot module), 26 removals (`+0x2C`), 17 sends (`+0x18`, including the lot-add scope, world load `0x38E3C0E`/`0x38E3C1E`, world shutdown `0x38E3C6C`, hand tool show/hide/detach, missing object `0x7E44AD3`), 2 direct `ProcessQueue` calls [verified; a site whose vcall slot sits beyond 25 instructions is not classified]. `g_MessageServer` (`0x44C470`, 379 call sites): mostly posts (95), registrations (62) and removals (40) [verified, same scan].

## 3. Lot add, end to end

1. Main: `Lot_SetActive 0xAC20E0` -> `Lot_PostRemoteCallAsync(selector 1, async 1)` -> job mask 4 on the sim record [verified].
2. Sim: pump `0xD81FC3` (or a sim help-while-wait) -> `ExecuteJob` -> `RemoteCall_JobFn` -> `RemoteMethodCall_Execute 0xABD3C0` -> `Lot_AddLotObjectsToScene 0xAC1130` [verified].
3. `0xAC1130`: if `lot+0xC9` is set, clear `lot+0xC1` and return; else `Send(0x4C55E8C, lot)` on the script server, `Lot_UpdateObjectSceneNode 0xABFAC0` for each object in `lot+0x14..+0x18`, `Send(0x4C55EFA, lot)` [verified]. Both sends find no handler (§1); per object, event `0x14` has no handler either (docs/50). **No managed code runs inside a lot add** [inferred from the empty handler sets].
4. Consequence: the per-activation cost is native (component lookup, LOD pick, deferred-LOD hash insert per object) times the object count, and several lots can land in one pass; the S3SS throttle re-posts with `PostRemoteMethodCall`, which runs inline on the sim thread (§1), so it does not spread work unless S3SS defers some other way [verified inline branch; S3SS internals not read].

## 4. Safe split recipe for fix #10 (lot-add budget)

Goal: at most N objects (or M µs) of lot add per sim pump pass, the rest in later passes, same results.

**Mechanism choice** [verified facts, design inferred]:

| Option | Spreads across passes? | Notes |
|---|---|---|
| `PostRemoteMethodCall` re-post from the sim thread | **no**: inline | what S3SS's throttle appears to do |
| New mask-4 job from inside the add | **no**, unless the pump has a deadline | the pump rechecks its queue after every job |
| Job state 5 / dependency re-arm | **no** | re-queued in the same queue |
| Post a callback to the script message server (`+0x20`) | yes: delivered at the next `ProcessQueue`, i.e. in a later `ServiceList_SimUpdate` | changes **where** the add runs (inside the service update, before the script tick, possibly between catch-up steps of one iteration), and the two lot-code flushes can deliver it early; not recommended |
| **Own deferred list, drained at the pump call site** | **yes**, one chunk per lot per pass | recommended |
| Pump deadline (docs/36 C1) plus mask-4 job continuation | yes, when the budget runs out | complements the list; also bounds CAS/store shared jobs |

**Recipe (own deferred list):**

1. Hook the caller, not `0xAC1130`: `RemoteMethodCall_Execute 0xABD3C0` with fn `[call+0x10] == 0xAC1130` (the `[LotStream]` hook already sits there, docs/28 §5.5). This coexists with S3SS's `0xAC1130` detour; if S3SS lot streaming is on, do not chunk (prologue check, as `[LotStream]` does).
2. Run the add for the first N objects yourself: same `+0xC9` check, `Send(0x4C55E8C, lot)`, `0xABFAC0` per object, `Send(0x4C55EFA, lot)`. **Keep the begin/end pair per chunk** so any handler a mod registers sees balanced scopes.
3. Store the remainder as (lot id `[lot+0x48]`/`+0x4C`, next index, a pass number) in a list owned by the sim thread. Store the **id**, not the pointer; re-resolve with `WorldManager_FindLotById 0xC60D30` before each chunk, because the lot can be detached or the world unloaded between passes (`WorldManager_DetachLot 0xC65830`, `WorldManager_Shutdown 0xC6B780`) [inferred hazard].
4. Drain the list at the pump call site `0xD81FC3` (already timed by `[LotStream]`), on the sim thread, **before** calling `RunJobs`: one chunk per pending lot, in list order. Do not drain from inside help-while-wait (the list is only touched at `0xD81FC3`), so continuations never run nested in a synchronous call.
5. Before each chunk, re-check the lot: skip and drop the entry if `lot+0xC9` is set or `lot+0xC1` is clear. Also drop the entry when a **remove** for that lot executes (hook `RemoteMethodCall_ExecuteRemove 0xABD3E0`, fn `0xAB4C00`): FIFO guarantees a remove posted after the add runs after the first chunk, but before the continuations; without the drop, objects would be added to an inactive lot after its remove [inferred hazard].
6. Re-read `lot+0x14`/`+0x18` for every chunk and clamp the index; if the object count changed since the first chunk, finish that lot in the current pass (objects shift in the vector, so an index cursor could skip one). `0xABFAC0` on an object twice is assumed harmless (deferred map get-or-insert, pending erase, empty event chain) [inferred; confirm with the per-object counters before relying on it].
7. Single-threaded mode (`IsSingleThreaded 0x7D9A10`, main == sim): no sim pump runs and remote calls execute inline in `WorldManager::Update`; do not chunk there.
8. Expect a longer window in which `lot+0xC1` is set but objects are missing from the scene; `WorldManager_ScoreAndStreamLots` and the throttle check `0xC69FF0` may treat a half-added lot as done [inferred]. Measure with `[LotStream]` (`lotAddMaxMs`, `pumpMaxMs`) before and after; the switch stays default-off (AGENTS.md).

Combine with the pump deadline (C1) only after this works alone: one change at a time.

## 5. Managed event model

### 5.1 `EventTracker` (Sims3GameplaySystems, `Sims3.Gameplay.EventSystem`) [verified]

- **Storage:** a singleton created in `PreWorldStartup`, cleared in `PreWorldShutdown`. Listeners live in a dictionary keyed by event type id; each value is a dictionary keyed by the listener's object id (the proxy id of its script object, or 0 for "any actor"), holding a list of listeners.
- **SendEvent** (2,336 call sites in 668 files across Gameplay Systems, Objects and Store; 320 `AddListener` sites): wraps the arguments in an `Event`, and if the tracker exists, processes it synchronously on the caller's thread and returns it; no caller uses the return value [verified grep]. Processing looks up the type, then visits the list for the event's actor id and then the list for id 0. For each list it pushes the list on a shared "active" stack, calls each listener's applicability test and handler, pops, then applies the pending adds/removes for lists that are no longer on the stack, and drops empty lists and types.
- **Re-entrancy:** a handler may send another event (nested processing pushes again), and may add or remove listeners; changes to a list that is being iterated somewhere up the stack are deferred to pending add/remove lists. The loop uses the count read before iteration. After every event it also drains `DreamsAndPromisesManager.sNeedToUpdateList` (guarded by a static bool so nested events do not re-enter that drain), forwards the event to `LifeEventManager.SendLifeEvent` and to `DynamicChallengeManager`.
- **Errors:** any exception from a listener's test or handler removes that listener silently.
- **Locks and threads:** none. No `lock`, `Monitor` or `Interlocked` in the event system, and no `lock`, `Thread` or thread-pool use anywhere in the decompiled game and script assemblies [verified grep]. Every send, including those from managed callbacks that native code invokes, runs on `Simulator`, the only thread that runs managed code (docs/29, docs/36) [verified there].
- **Yielding inside a handler** [inferred risk]: the active stack is global, not per task. If a handler yields the script task (sleep), another task's events push and pop on the same stack; if both yield inside handlers, pops cross over and deferred adds/removes can be applied to a list another task is iterating. Not traced which listeners yield.

### 5.2 "Script messages" of the lot-add scope

`0x4C55E8C`/`0x4C55EFA` are ids of the native script message server (`EAMessageServer`, §2), not of any managed message or event system; nothing in SimIFace or ScriptCore registers for EAMessage ids [verified grep for the ids in hex and decimal and for message-server bindings]. The managed `EventQueue` ICs (`IC_EventQueue_CreateEventQueueImpl 0x7426A0`) belong to SACS animation events, not to this server [verified by its managed users].

### 5.3 Second-thread verdict for events

**Do not raise `EventTracker` events from a second managed thread.** What would break [verified structure, consequences inferred]:

1. The dictionaries and lists are plain collections: a send on one thread concurrent with `AddListener`/`RemoveListener` or another send can corrupt buckets or change a list under an index loop.
2. The single active stack and the pending lists are shared: interleaved push/pop from two threads makes the "is this list being iterated" test wrong, so adds/removes hit a list mid-iteration.
3. Out-of-range or corrupt-state exceptions inside the loop are swallowed **and the listener at that slot is removed**: a race would silently delete wishes, opportunities, achievements or challenge listeners, with no crash to notice.
4. The Dreams and Promises drain is guarded by a plain static bool, so two threads can both run it or one can skip it; `LifeEventManager` and `DynamicChallengeManager` are touched on every event.
5. Listener bodies are arbitrary gameplay code (Sim state, UI notifications, internal calls with sim-thread affinity, script task operations).
6. Prerequisite missing anyway: Mono here scans one thread's stack (docs/29/30), so a second managed thread first needs the `[MonoThread]` work of docs/30.

**Allowed pattern:** a worker records "event requests" (type, actor id, target id, small payload) in its own lock-protected queue; the sim thread drains that queue at one fixed point (a script tick hook or a `OneShot`/`Threaded` object) and calls `SendEvent` there. Costs: listeners see the event later and in a different order relative to events sent in between; any caller that reads the `Event` object after sending (none use the return value; reading a pre-built `Event` afterwards was not checked) would see it unprocessed. That matches the docs/59 §1.6 rule that `Update` (B3, two `SendEvent` calls per Sim) must stay on the sim thread.

## 6. Names

34 new rows in `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/61>`): the `EAMessageServer` class (ctor, vtable, 14 methods, the shared lock), both service registrations, vtables, init/update slots, the script message server getter/setter/global, `Job_SetRefObject`, the two remote-call execute slots and one direct flush caller. No existing address renamed.

## 7. Method notes

- Both server globals were found from their getters' only writers; the allocator tags `Services/MessageServer`, `Services/ScriptMessageServer` and `Services/EAMessage/*` named the class (docs/33 §8).
- Slot use per call site: a scratch Ghidra script listed, for every call of a getter, the vtable displacement of the next indirect call and the immediate pushes in between (message ids). Kept in the session scratch folder, not committed.
- The "no listener" result for the scope ids is a byte scan of the whole file for the 4-byte ids plus the handler-registration scan; register tables were checked where the id comes from a table.

## 8. Open items

- Read the service flag bits (main/sim slot enabled) for `ScriptMessageServer` and `MessageServer` at run time.
- Callers of the two direct script-queue flushes (`0xACE4A0`, `0xAE0710`): which lot paths deliver posted script messages early.
- Whether any shipped mod (S3SS, NRaas native parts) registers EAMessage handlers at run time for the lot-add ids.
- Which event listeners yield inside their handler (§5.1 risk).
- Confirm §4 step 6 (double `0xABFAC0` harmless) with the `[LotStream]` per-object counters.
