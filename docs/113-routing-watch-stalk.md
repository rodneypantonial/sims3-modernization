# 113. Routing watch + Stalker live probe (docs/101 U10, U1, U3)

Written 2026-10-18. Plan and tooling; the game has not been run for this doc. Results go in §6 when the queue has run. Ledger: docs/51 §53 (I-113-1 .. I-113-5).

> **Changes 2026-10-18:** `bp-watch-stalk` ran; §6 row filled, §7 reads every site the report listed (static, capstone on the Steam 1.67 image). Verdicts: U10 not refuted (both "live route" sites are freed-and-reused memory), U1 one new address `0x114C900` that needs nothing (zero stored over zero), U3 not seen in 24 plans. Ledger: docs/51 §76 (I-113-10 .. I-113-14).
>
> **Changes 2026-10-18 (later):** `ip-watch-stalk` ran; §6 row filled. §8: the one IP "live route" site `0x6BB9DC` is a store into a freshly allocated block (freed route memory again), so U10 stays not refuted; the probe now ends a watch at the Route destructor (§8.3), the report whitelists `0x114C900` (§7.2), and a worker mode follows `Route_PlanLocked` on the route worker with RouteAsync on (§8.4, jobs `bp-watch-stalk-worker` / `ip-watch-stalk-worker`, not run yet). Ledger: docs/51 §77 (I-113-20 .. I-113-25).

## 1. What is open and why a live probe

docs/108 §4 left three docs/101 §8 items to tools outside `[ProbeValues]`:

| Item | Question | Why it matters | Needs |
|---|---|---|---|
| U10 | Does anything read route `+0x1F4` besides the two IC writers? | docs/101 §5 calls it a dead replan counter from a static scan; a replacement route may then drop it | a hardware read watchpoint on live routes |
| U1 | Is the `.data`/`.tls` write set of a plan exactly docs/101 §2.2 (no global reached through a register-computed address)? | the G design (docs/101 §3) redirects only the §2.2 items | every memory write inside a plan |
| U3 | Does `0x66AB70` (the only user of the clip scratch `0x1158580`, docs/101 §2.2 #2) run inside plans at all? | #2 is one of the two must-redirect items | block coverage inside a plan |

docs/98 U7 (writers of `X+0x4C`) is not part of this probe: docs/111 reads `X+0x4C` at every in-room entry and on return, which decides it (I-111-5).

All three are observe-only. No ts3native change: a Frida script with hardware watchpoints and Stalker.

## 2. The probe: `tools/frida/watch-stalk.js`

Two phases in one load, never overlapping (a watch hit inside Stalker's code cache would report a cache address, not the reader):

**Watch (first half, default 3 of 6 minutes), U10.**
- Routes come from mid-function hooks right after the two `+0x1F4` increments, where `esi` is the route: `0x7654C1` (after the `add` at `0x7654BA` in `IC_Route_Replan 0x765490`) and `0x765590` (after `0x765589` in `IC_Route_ReplanFromPosition 0x765550`). Both 8-byte prologue checks are against the Steam 1.67 bytes [verified in game-copy], and no branch lands inside the 5 patched bytes (previous study).
- A route is taken only if its first word is the Route vtable `0x10047CC` (slot 0 `0x74B280`); a bad pointer is counted, not followed.
- A timer on the script thread arms up to 4 debug-register slots, one route each, as 4-byte read/write watchpoints on route `+0x1F4` (Frida 17 `Thread.setHardwareWatchpoint`). Debug registers are per thread, so every game thread is armed, and threads that appear later are armed on the next 250 ms tick.
- A watch ends when the route's vtable changes (freed or reused), after 30 s, or at the end of the phase; 40 routes and 5000 hits at most.
- A hit is a `single-step` exception at the instruction after the access. The handler reads DR6 from the saved CONTEXT to find the slot, records the address (module+offset), thread, whether the route's vtable was intact, and 3 backtraces per site, and returns true.
- Race fix found on the dummy: a thread that traps while the script thread is busy (unset, re-arm) waits in the handler and then resumes from the CONTEXT saved at the trap, which still holds the old debug registers. The handler therefore writes the intended Dr0-Dr3/Dr7 for that thread into the saved CONTEXT. Without it, unset watchpoints came back and the dummy died after the script unloaded. Traps from a slot already unset are counted as `stray`.

**Stalk (second half), U1 and U3.**
- Interceptor on `Route_PlanLocked 0x82C6E0`. Only on the sim thread (`[0x11D8A8C]`) and only inside a sampling window (5 s every 20 s, at most 3 plans per window, 24 per run) is the call followed with Stalker; `onLeave` unfollows.
- The JS transform puts a CModule C callout before every memory-writing instruction: capstone access `w`/`rw`, plus x87 stores and `cmpxchg`/`xadd`/`xchg`, which capstone marks as reads [verified on Frida 17.23.3]. `rep` string stores use `ecx` times the element size. The callout keeps every written address in `0x1146000-0x1235009` (`.data` + `.tls`) with its count, first writer and number of plans. `fs`/`gs` writes are only counted.
- Coverage: block starts in `0x66AB70-0x66AC53` compiled during a plan, plus a callout counter on `0x66AC37` (the load of `0x1158580`), per plan.
- `Lock_Acquire 0x4E16F0`, `Lock_Release 0x4E17B0`, `0x4E3F90`, `0x78F2B0` and `0x791C00` run natively (`Stalker.exclude`), so their writes are not recorded (the pool locks of §2.2 #4-#6 are listed as known for that reason).
- Cost guards: each followed plan recompiles its blocks (estimate 50 ms - 1 s stall on the sim thread). A window close forces an unfollow, a plan slower than 3 s stops stalking for the rest of the run, and 24 plans is the hard cap.

**Hook clash.** `tools/frida/stutter-profile.js` hooks `0x82C6E0` and `0x81B2A0` (inside the plan). Choice: **never run both**. `profile-world.ts --watchstalk` does not start the stutter profiler, and the strict prologue check refuses a patched prologue, naming a leading `e9` as another Frida hook. A shared hook would also let Stalker follow into the profiler's JS listeners.

**agent.js.** Its exception handler is installed only with `ts3probe.py --exception-stacks` (the queue does not pass it). When it is installed it may see a watch hit first: it records the first 3 per site (at most 10 per second) and returns false, so the hit still reaches this handler.

Output: `<tag>-watchstalk.json`. Progress line every 30 s. Fail-fast: two 30 s checks without a route replan (watch phase) or without a sim-thread `Route_PlanLocked` call (stalk phase) end the run with the reason.

## 3. How to run and read

- Dummy test first (probe-safety rule): `python tools/probe/watch-stalk-test.py` (32-bit `ping.exe`, ~2.5 min, prints `RESULT PASS`). PASS 2026-10-18.
- Report self-check: `node tools/harness/watch-stalk-report.ts --self-check`.
- Game: `node tools/harness/queue.ts tools/harness/queues/watch-stalk.json` (jobs `bp-watch-stalk`, `ip-watch-stalk`, Spring saves, seed 12345, RouteWorker and RouteAsync off so every plan runs on the sim thread). ~9 min per job (6 min window plus load and teardown), ~18 min for the queue.
- Read: `node tools/harness/watch-stalk-report.ts probe-out/profile/<tag>-watchstalk.json`. Pass for the run: reason `complete`, nothing refused, routes > 0, plans > 0, no `WARN` line.

## 4. Expected verdicts and rules

| Item | Expected (docs/101) | Report says | If not |
|---|---|---|---|
| U10 | hits only at `0x7654C1` and `0x765590` (the IC writers), plus zeroing on route reuse (`Route_ctor 0x75D4B0`, `0x760200`, `0x760370`) and accesses after the vtable changed (`freed`) | `U10: verified` | `U10: REFUTED` lists each unknown site. Look at the instruction before each address: a real read of a live route reopens docs/101 §5 |
| U10 caveat | at least one writer hit | `U10: no verdict` when routes were watched but no writer hit (the watch was never shown to fire) | rerun with longer holds |
| U1 | every written address is in docs/101 §2.2 (26 items) or a pool lock | `U1: verified on N sim-thread plans` | `U1: REFUTED` lists the new addresses with their writer: add each to §2.2 with a redirect plan |
| U3 | `0x66AB70` runs only through H13 (deferred footprint room-id refresh at quadtree unpin), so in some plans or none | `U3: verified ... in k of N plans` or `U3: not seen in N plans` | either answer closes U3; "not seen" with N ≥ 20 makes #2 low-priority for G |

## 5. Limits

- U1 is on sim-thread plans (RouteWorker off), not on the worker that docs/101 §8 names. The closure under `Route_PlanLocked` is the same code, but per-thread paths (TLS, job-system handle #7) can differ. A worker run needs the stalk phase pointed at the worker thread id instead: one parameter, untested. (Built: §8.4.)
- Sampled plans only (≤ 24 per world). Rare paths such as water plans, lazy builds or deferred quadtree work show up only if a sampled plan takes them. The report prints how many plans were followed.
- Excluded functions are not checked. Writes into `.data` by heap code called from the plan are recorded, because the address decides, not the module.
- Watch hits give the instruction after the access. A hit inside a function the report does not know is listed raw. The zeroing bound (0x600 bytes after each zeroing writer's start) is an inferred size.
- 4 routes at a time, 30 s each: a reader that runs only at save time or on route destruction is caught only if it happens during a watch.

## 6. Results

| Job | Run | U10 | U1 | U3 | Plans followed / max ms | Routes / hits / stray |
|---|---|---|---|---|---|---|
| bp-watch-stalk | 2026-10-18, `complete`, 357 s, nothing refused, 0 probe errors | report: REFUTED (2 sites); after §7: **no reader of a live route** | report: REFUTED (`0x114C900`); after §7: **benign, no redirect** | not seen (0 blocks, 0 `0x66AC37` loads) | 24 of 6396 sim-thread plans / 976 ms (median 99) | 40 / 610 / 2 |
| ip-watch-stalk | 2026-10-18, `complete`, 357 s, nothing refused, 0 probe errors | report: REFUTED (1 site, `0x6BB9DC`); after §8.2: **no reader of a live route** | report: REFUTED (`0x114C900`, 1,358 stores in 22 plans); whitelisted since §8, report now `verified` | not seen (0 blocks, 0 `0x66AC37` loads) | 24 of 2520 sim-thread plans / 1011 ms (median 125) | 40 / 342 / 6 |
| bp-watch-stalk-worker, ip-watch-stalk-worker | not run (§8.4) | | | | | |

Read with `node tools/harness/watch-stalk-report.ts probe-out/profile/watch-stalk-2026-10-18-bp-watch-stalk-watchstalk.json`. The raw JSON also lists `0x11580C4` (A\* stamp, docs/101 §2.2 #1) written by `0x61E360` in 16 of 24 plans: expected, so the report does not flag it. Watch phase: 214 `Replan` and 244 `ReplanFromPosition` increments seen, 31 watches ended on a vtable change, 8 on the 30 s hold.

## 7. Results BP 2026-10-18

Static reading of every site the report flagged, capstone on `game-copy/bin/TS3W.exe` (Steam 1.67). No game run, no native change.

### 7.1 U10: the two "unknown sites on live routes" are freed-and-reused memory [verified static]

| Site (report pc) | Access (instruction before the pc) | Function | What it touches |
|---|---|---|---|
| `0x5B8645` (x1, sim) | write at `0x5B863E`: `[edi+0xC]` = constant `0xFE2360`, with `edi` = `this+4` | `0x5B8620`, copy constructor of a 0x1C-byte element (base vtable `0xFE2378`, then a vector: begin, end, capacity, allocator at `+0x10`); called per element from `0x5B8740` (relocate a range into new storage, final vtable `0xFE2418` at `0x5B876E`) | the new element's allocator field, element start = watched route + `0x1E4` |
| `0x62035B` (x4, sim) | read at `0x620358`: `edx = [esi+0xC]`, the allocator passed to `0x4E3F90` when the vector grows | `0x6202E0`, insert into a vector of 8-byte items (grow path at `0x620349`); reached from the same copy loop (`0x5B865F` -> `0x5B84A0` -> `0xD77370`, backtrace frames `0x5B851A`, `0x5B8664`, `0x5B876E`) | the element's vector at route + `0x1E8`, its allocator word at route + `0x1F4` |

Neither is a route reader. The route was freed and its memory reused by an array of these 0x1C-byte elements:

- Elements at route + `0x1E4` would overwrite live route fields `+0x1E4` (byte), `+0x1E8`, `+0x1EC`, `+0x1F0`, `+0x1F4`, which `Route_ctor 0x75D4B0` sets one by one (`0x75D5D9..0x75D60B`). A live route cannot hold them.
- Why the probe still saw the Route vtable: `0x1E4 mod 0x1C = 8`, so route `+0` falls at offset `+0x14` of an earlier element in the same array. Neither `0x5B8620` nor `0x5B876E` writes `+0x14` (they write `+0`, `+4..+0x10`, `+0x18`), so the old vtable `0x10047CC` survives in the reused block and the `vtOk` check passed.
- Both sites hit the same element type: 0x5B8645 is its constructor storing the allocator word, 0x62035B is a later grow of its vector reading that word.

**Verdict U10:** not refuted. No access to `+0x1F4` of a live route outside the known writers was seen; the one writer hit (`0x765590`, x1) shows the watch fired; the zeroing site `0x75D5F7` (`Route_ctor`, x2) is the expected reuse case. docs/101 §5 (dead replan counter) stands for BP. **Probe weakness:** `vtOk` is not a liveness test, since a freed route keeps its vtable until something overwrites `+0`. A rerun that wants clean "live" counts should end a watch when the route is released (hook the release path or the heap free of that pointer) instead of checking the first word only. Not done here (no probe change requested).

The after-free hits (`0x63F02C` x481 and the rest) are, by the same reasoning, other users of the freed memory, not route readers; not read one by one.

### 7.2 U1: `0x114C900` is the shared empty hash-table bucket array; the write stores 0 over 0 [verified static, value inferred]

- **The writer.** The Stalker callout runs before the writing instruction and records that instruction's own address (`transform` in `watch-stalk.js` passes the instruction address `a` to the site table), so "pc after the access" applies only to watch hits, not to U1. `0xD2BC42` is itself a store: `mov dword ptr [ebx+edi*4], 0` [verified]. Attribution correct.
- **The function.** `0xD2BC10` (2 stack arguments, `ret 8`, `ecx` unused) walks a bucket array: for each bucket, it frees the node chain (next pointer at node `+0x18`, free `0x4E3F70` -> heap `[0x11CB864]`) and then stores 0 into the bucket. It is a hash table's "free all nodes" step, shared by 109 call sites (identical code folding).
- **The global.** `0x114C900` holds `00000000` and `0x114C904` holds `FFFFFFFF` in the image [verified]. 454 instructions in `.text` load its address as an immediate, and every one stores or passes the address; none writes to it directly [verified: raw 4-byte scan, each hit decoded]. The constructor pattern is "bucket count `+8` = 1, bucket array `+4` = `0x114C900`" (example `0x456243..0x45624A`). This is the EASTL-style shared empty bucket array: every hash table that never had an entry points at it, with one bucket and a terminator.
- **Which table in the plan** [inferred]. `LotPlanCtx_Init 0x6298F0` puts `0x114C900` at context `+0x20` with count 1 at `+0x24` (docs/112 §4.1). `0x628A60` clears that table (`DoFreeNodes(+0x20, +0x24)` at `0x628A6D`, then element count `+0x28` = 0, then frees the array if count > 1). Its callers include `LotPlan_Inner`'s range (`0x63FDE2`, `0x63FE40`) and `0x834D12`. A lot search context whose table stayed empty therefore clears the shared array: 351 stores in 24 plans, about 15 per plan, matches several lot contexts per plan. The probe did not record the caller, so the table is inferred.
- **What it means.** An empty table's only bucket is 0; clearing it stores 0. The value never changes: a table gets real buckets on its first insert (rehash first), and a nonzero value here would break every empty hash table in the game at once [inferred from the use pattern; no nonzero writer exists among the 454 references or in the 24 plans]. Two threads storing 0 into an aligned word that is always 0 is not a data race in effect (no torn value, no reader sees a change).
- **Class:** none of counter, cache, allocator state, Mono/runtime or stat. It is a constant sentinel that the library writes with the value it already holds.
- **P4 (clone run):** no redirect, no atomic, no thunk. Ignore it. The worker's lot contexts point at the same sentinel and store the same 0. If a write-set check is added to P4's shadow, whitelist `0x114C900` as "zero-store sentinel".
- **Why the census missed it.** Computed addressing: the store is `[ebx+edi*4]` in a generic helper that receives the bucket pointer from the table object, so no instruction in the closure has `0x114C900` as a store operand. docs/101 §2 listed it as "passed to callees but not written", which was right for the immediate operands and wrong for the callee body. This is the one case of the kind U1 asked about in BP's 24 plans, and it is harmless.

### 7.3 U3 [verified live, BP]

`0x66AB70` never ran: no block in `0x66AB70-0x66AC53` was compiled and the `0x66AC37` load counter stayed 0 in 24 followed plans. By §4's rule (N ≥ 20), the clip scratch `0x1158580` (docs/101 §2.2 #2) is low priority for G. Its thunk stays in docs/94 §12 because it is cheap and H13 can still reach it in rare plans.

### 7.4 What changes

- docs/101 §5 and the U10 row: unchanged, BP evidence added. §2.2 gains no item; the "not written" list now names `0x114C900` as a zero-over-zero writer (note in docs/101).
- docs/94 §12: no new redirect; risk 1 (U1 not proven live) is answered for BP sim-thread plans (note in §12.10).
- Still open: `ip-watch-stalk` (second world), U1 on the worker thread (§5), and a cleaner U10 liveness test. (All three addressed in §8; the worker runs are still to do.)

## 8. IP result, route release end of watch, worker mode (2026-10-18)

Static work and probe code only: no game run, no native change.

### 8.1 IP run

`ip-watch-stalk` (row in §6): 104 `Replan` and 269 `ReplanFromPosition` increments seen, 40 routes watched (36 ended on a vtable change, 4 on the 30 s hold, 8 more freed before a slot came free). Writer hits: `0x7654C1` x7, `0x765590` x3, so the watch fired. 342 hits, all but one after a vtable change. U1: the same two addresses as BP, `0x11580C4` (§2.2 #1, 22 plans) and `0x114C900` (§7.2, 1,358 stores in 22 of 24 plans), nothing new [verified live, IP]. U3: not seen in 24 plans, as in BP.

### 8.2 `0x6BB9DC` is a store into a freshly allocated block [verified static]

The report pc is the instruction after the access (§2). At `0x6BB9DC` sits a short `jmp`; the instruction before it, `0x6BB9D4`, stores a float from `0x11CDD98` to `[ebp+0x404]`. `ebp` is the result of the allocator `0x4E3F90` called with size `0x408` at `0x6BB9AD`, then constructed by `0x6BB080` (`ecx = ebp`); the store sets the last dword of that new block. The code around it (from `0x6BB913`) is a constructor (vtable `0xFF4218` at `[esi]`) that allocates this 0x408-byte helper when bit `0x20` of `+0x224` is set.

- Watched word route `+0x1F4` = block `+0x404`, so the watched route started at block `+0x210`, inside a block the allocator had just handed out: the route had been freed and its memory reused. A write, not a read, and not a route reader.
- `vtOk` was true because block `+0x210` still held `0x10047CC` at the trap (the helper's constructor had not written there yet): the §7.1 weakness again.
- Thread: not the sim thread (`sim` 0); the same thread as the other after-free hits `0x4E8D05`, `0x4E9297`, `0x4FD7E7` in this file [from the JSON].
- Side note: `0x6BB9E4` reads `+0x1F4` of the constructor's source argument (`edi`). If `edi` were a watched route the hit would be at `0x6BB9EA`; none was seen in BP or IP. Its class was not checked [inferred not a route].

**Verdict U10 (IP):** not refuted, as in BP. docs/101 §5 stands for both worlds.

### 8.3 End of watch at route release [verified static, probe tested on the dummy]

Release path of a Route (vtables set by `Route_ctor` at `0x75D4F9..0x75D506` and again by the dtor at `0x75D651..0x75D65E`: `0x10047CC` at `+0`, `0x10047AC` at `+4`, `0x100479C` at `+8`):

- Release: interface slot `0x100479C+8` = `0x9C0440` (`sub ecx,8`, `jmp 0x8ABE60`). `0x8ABE60` decrements the refcount at route `+0xC` and, at zero, calls slot 0 of the `+8` vtable with 1: `0x75D630` (`sub ecx,8`, `jmp 0x760350`).
- `0x760350` is the deleting destructor: `call 0x75D640` (the destructor body), then `0x4E3F70` (free) when the flag is 1.
- `0x75D640` has one direct caller, `0x760353`; `0x760350` is reached only by the jumps at `0x75D623` and `0x75D633`; no branch lands in `0x75D641..0x75D644` [verified: rel32 and rel8 scan of `.text`].

`watch-stalk.js` hooks `0x75D640` (prologue `83 EC 08 53 8B D9 8B 83`, no relative operand; strict check, refusal only drops this hook and keeps the vtable test). On entry with `ecx` = a watched route the slot is marked released: later hits are keyed `:freed` with `vtOk` false, and the next 250 ms tick unsets the slot with reason `released` (`watch.dropped.released`, `ended[].released`, `watch.dtors`). A route still waiting for a slot is dropped. The report prints the count. Dummy check: a route released through a stub with the same prologue while its vtable stays intact ends as `released`, and a third route takes its place.

Limit: a route destroyed by code that inlines the destructor body would be missed [inferred none: one caller]. The hook runs per route destruction, a low rate next to the replan hooks.

### 8.4 Worker mode (U1 on the route worker)

docs/101 §8 and P4 need the write set of plans on the route worker; §5 left it as "one parameter, untested". With `worker: true` the stalk phase follows `Route_PlanLocked` on the worker instead of the sim thread. Everything else is the same: callouts, `.data`/`.tls` range, coverage, `0x66AC37` counter, excluded functions, windows.

- **Where the worker calls it.** With `[RouteAsync]` on, the worker runs the whole original IC (`ServeAsync`, docs/60 S3b), whose `Route_PlanLocked` call enters `0x82C6E0`. ts3native's `InstallDetour` wrote `e9` to its `PlanLockedHook` there (6 stolen bytes, trampoline in `VirtualAlloc` memory) [verified source]. The strict prologue check therefore accepts, in worker mode only, an `e9` whose target lies in a module whose name matches `detourMod` (default `ts3native`); a jump into anonymous memory (another Frida hook) is still refused. Frida's Interceptor follows that jump and hooks the start of `PlanLockedHook` [inferred from Frida's redirect resolution], so `onEnter`/`onLeave` still bracket the whole call on the calling thread. Stalker then also runs ts3native's hook code; its writes fall outside `0x1146000-0x1235009` and are not recorded.
- **Worker thread id.** ts3native keeps `g_workerTid` private (no counter field) [verified: `ts3native.cpp`, `docs/registry.md`]. The probe takes `workerTid` from the parameters when given, else the first non-sim thread that calls `Route_PlanLocked`. Every non-sim caller is counted in `stalk.tids` (16 at most) and the report warns when there is more than one. Known non-sim callers are world load on main (before the window) and the routing debug display, which is skipped during jobs (docs/60 §2.2 #9, #10).
- **Caps (looser, `WORKER_CAPS`):** 6 plans per window, 48 per run, stop after one plan over 5 s (sim mode: 3, 24, 3 s); explicit parameters still win. Stalker stalls only the followed thread. The sim thread pays only where it waits for a running job anyway: gate entries on `+0x918`, which the worker holds across the whole IC with `[RouteGate] Enforce`, the destroy drain, and Mode 1 pumping. So the stall still reaches the sim thread there, and the caps stay, just looser.
- **Fail-fast:** in worker mode the stalk check counts worker calls: no worker `Route_PlanLocked` call for 60 s ends the run (`fail: no worker Route_PlanLocked call for 60 s`), which also catches async never starting.
- **Output:** `stalk.worker`, `planVia` (module and hook), `workerTid`, `workerCalls`, `tids`. Report: `node tools/harness/watch-stalk-report.ts --worker <file>` prints `U1 worker: verified on N worker plans (tid T, via ...)` or `REFUTED` with the new addresses. It picks the worker wording on its own from the file, and says "no verdict" for a sim-thread file given `--worker`.
- **Harness:** `run.profile.watchstalk: "worker"` in a queue adds `--watchstalk-worker` to `profile-world.ts`, which passes `{ worker: true }` to the script. Jobs `bp-watch-stalk-worker`, `ip-watch-stalk-worker` in `queues/watch-stalk.json` use the env-on async settings of `snap-p1.json` without `RouteSnap.Count` (RouteAsync Mode 1, RouteGate Enabled/Sites/Enforce, Discourage) on the `tested` build, 6 min, seed 12345.
- **Run:** `node tools/harness/queue.ts tools/harness/queues/watch-stalk.json --only bp-watch-stalk-worker,ip-watch-stalk-worker`, then `node tools/harness/watch-stalk-report.ts --worker probe-out/profile/watch-stalk-<date>-bp-watch-stalk-worker-watchstalk.json` (and the IP file). Pass: `complete`, nothing refused, routes > 0, worker plans > 0, no `WARN` line, and the log's first line names `plan via ts3native... detour`.
- **Limits:** only async jobs are followed. With `RouteWorker` sync mode the worker calls the trampoline, not `0x82C6E0`, so nothing would be seen (fail-fast says so). Whether `PlanLockedHook` on the worker reaches the original in the same call is inferred from the source, and the first run shows it (`workerCalls` > 0, plans with writes > 0).

### 8.5 Dummy test

`python tools/probe/watch-stalk-test.py` (run from PowerShell; Git Bash's python crashes with Frida). New checks: release through the dtor stub with the vtable intact ends the watch as `released`; worker mode follows only the second dummy thread (worker tid identified, sim plans counted but not followed, same write set and counter), worker caps applied while explicit parameters win; an `e9` into a module named by `detourMod` is accepted, one into anonymous memory refused. **PASS 3 of 3 runs, 2026-10-18.**

### 8.6 Incident: `bp-watch-stalk-worker` crash (2026-10-18 22:36:52 PHT) and fix

The first worker run killed TS3W.exe about 3 minutes in. The game's `xcpt` file is empty (0 bytes, no stack), there is no Windows Application-log entry, and the probe log stops after the 150 s progress line. The phase switch to the stalk phase was the first suspect. The evidence points to the build instead.

- **The crash came before the switch** [verified from the logs]. The probe's start line is stamped 14:33:55 UTC and its 30 s progress lines arrive exactly on time (14:34:25 ... 14:36:25 = 150 s). The `xcpt` file and ts3probe's `process-terminated` are at 14:36:52.4, so about 177 s in. `endWatch` (watch end, `startStalk`) is a 180 s timer. In the sim-thread run the same lines came exactly on time (`watch phase over` at start + 180 s). No stalk-phase line was printed. So the stalk-phase code (Interceptor through the `e9`, Stalker on the worker, CModule) never ran. The watch-phase code is identical to the two sim-thread runs that passed.
- **The job ran a stale ts3native, 0.10.1, with async routing but no route gate** [verified]. The job's `asi: "tested"` is `native/ts3native/out/ts3native.asi` in the main checkout. That file is a 2026-10-15 build that reports `ts3native 0.10.1`, while the source is 0.27.0. The job's ts3native log (`probe-out/queue/watch-stalk-2026-10-18-bp-watch-stalk-worker-1432-ts3native.log`) has no `route gate`, discourage or P10 lines; it has `route async settings: Mode=1` and 3,011 async jobs started. Before 0.11.0, `[RouteAsync]` started jobs without the gate or the docs/60 closures (commit d006645: "[RouteAsync] alone no longer starts jobs"). That is the unguarded worker-against-sim configuration that crashed three times before (docs/18, docs/23 #3). The job's `RouteGate.*` and `Discourage.*` ini keys meant nothing to that build. At `ts3native.asi+0x59e0`, the probe's `planVia` points at the start of 0.10.1's `PlanLockedHook` (`push ecx; push ebx; push ebp; push esi; push edi; mov ebp, edx`, 5 clean bytes), so the hook target was valid as well.
- **Other jobs on the same stale build** [verified, `probe-out/queue/*-ts3native.log` first lines]: every `asi: "tested"` job since 2026-10-17 ran 0.10.1. These include spring-saves, spring-update, clean-night clock, first-minute, wildflower, live-reads, f4b and the sim-thread watch-stalk runs. Results that rely on ts3native features newer than 0.10.1 need a rerun. Pure Frida observation runs are unaffected.
- **Cause [inferred, strong]:** the pre-gate async worker of the stale build. The probe's watch-phase hooks and watchpoints ran on that worker too. They add timing pressure but no new memory access, so the exact faulting access is unknown (no stack). The ledger has the rows (docs/51 §82).

**Fix.** `tools/harness/queue.ts` reads `TS3NATIVE_VERSION` from the source and the `ts3native <version> %04d` log format string from the tested DLL. When the two differ, the job refuses before it touches the game, and `--dry-run` marks it `** STALE **`. Rebuild the main checkout with `native/ts3native/build.cmd` before the rerun. `cycle.ts` still installs `out/` when it differs from Bin (unchanged), so manual cycles need the rebuild too. `watch-stalk.js` is unchanged.

**Probe worker path on the dummy (crash shape).** `watch-stalk-test.py` case 2d sets up the worker shape. A third dummy thread enters a hook in another module (spare bytes of `PING.EXE`'s header page) through an `e9`. The hook calls the plan stub and then a real wait (`SwitchToThread`). That thread gets watch hits in the watch phase and is inside the hook or its wait at the switch. The mode is `both` + `worker`, with no `workerTid` given. The stalk phase follows the thread through the jump. The old (unchanged) probe passes this case: complete, nothing refused, watch hits on the worker tid, the worker tid found and followed for 6 plans with the full write set, and all three threads alive after unload. So the dummy shows no probe hazard on this path. A separate experiment showed that Stalker on WOW64 raises no single-step exceptions around a followed syscall, which rules out the probe's single-step handler (it swallows strays) for this path.

**Dummy result:** PASS 5 of 5 in a row from PowerShell (2026-10-18 23:00-23:14 PHT), plus 1 earlier pass. One more earlier run failed only the pre-existing case 2 check "writer hits on the sim thread" (1 hit, threshold > 5; a timing flake in the sim-mode watch, not the worker case).

**Rerun:** safe once `out/` is rebuilt from the current source, so the queue no longer refuses. The worker stalk path then runs in the game for the first time. Keep the fail-fast and caps.
