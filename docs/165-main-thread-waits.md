# 165. Main-thread lock waits and hook cost (static + one capture)

Where the main (render) thread sits when its stacks show a `ts3native.asi` frame, which lock it waits on, and how much of the frame is ours. Static analysis + existing data only; no game run.

Data: `probe-out/profile/freeze-measure-2026-10-22-bp-freeze-pan-mainstack-mainstack.jsonl` (2,864 main-thread samples over 585 s, ~4.9/s), job `bp-freeze-pan` of `tools/harness/queues/freeze-measure.json` (release defaults + all-on candidate set + `[Harness] ForegroundRender=1` + `[RouteJobPhases] SyncPlans=1`), `game-copy/ts3native.asi.0329` (sha256 `19466ae1...`). Frame rate per minute 12-48 FPS, mean 27.7 FPS = **36.1 ms per frame**; ms/frame below = sample share x 36.1 ms [inferred: uniform sampling]. The simstack file of this run is empty (no samplers), so no holder thread can be read from the same instants.

## 1. The offsets are three frames of one pass-through chain [verified]

The asi was rebuilt from the same source (worktree at `6e97d4d4`, whose `native/` equals `d2ae28e6`, the asi.0329 commit) with `/MAP`: its `.text` is **byte-identical** to `asi.0329` (491,425 bytes, 0 differences), so the map symbols apply exactly. Disassembly of `asi.0329`:

| asi offset | function + offset | what it is |
|---|---|---|
| `+0x27152` | `P5bLock+0x52` | return address of `call [g_lockOrig]` on P5bLock's first branch: `!(g_p5bOn \|\| g_tuOn) \|\| lock != g_rgLock` (p5b.cpp line 84). Every sample here is a lock that is **not** `world+0x918` (or P5b and TailUnlock both off) |
| `+0x1cd9c` | `LockAcquireAsync+0x3cc` | return address of `call P5bLock` at `0x1cd97`, reached from `0x1ca0c` (`*deadline == 0`) or `0x1cd0d` (thread is not `g_async.owner`, or no job): ts3native.cpp line 3149 `return P5bLock(lock, edx, deadline)` |
| `+0x1cf97` | `LockAcquireHook+0x1d7` | return address of `call LockAcquireAsync` (the no-sync-plan-session branch; `+0x1cf6d` is the SyncPlans one) |
| `+0x596f0` | `__except_handler4` (CRT) | **not a frame**: the handler pointer of `LockAcquireAsync`'s SEH registration record (`push 0x100596f0` at `+0x1c9da`); `{next, handler}` looks like `{saved EBP, return}` to the EBP walker in `tools/probe/agent.js walk()`, so it marks "LockAcquireAsync is on the stack" |

So the chain on the main thread is: game caller -> `Lock_Acquire 0x4E16F0` detour -> `RjpStub14` (`[RouteJobPhases]` site `RJS_LOCK`, thread check, pass) -> `LockAcquireHook` -> `LockAcquireAsync` -> `P5bLock` -> `g_lockOrig` (trampoline into `Lock_Acquire` body) -> `EnterCriticalSection` (`Lock_Acquire+0x1d` = `0x4E170D`, return of `call [0xF9512C]` on the INFINITE branch). The main thread is never `g_async.owner` (the sim thread), so it always takes the line-3149 pass-through: **no gate, no pump, no H2/TailUnlock/P5b wait, no `+0x918` admission runs on the main thread** [verified from the branch structure; `RgEnter` gates sit only at sim-thread steering sites and no main-thread sample has a gate frame].

## 2. The "callers" are allocator name strings, not code [verified]

`TS3W+0xbfd74c`, `+0xba1ac0`, `+0xbfd2dc`, `+0xbfdd34`, `+0xbb2cd0` (VA `0xFFD74C`, `0xFA1AC0`, `0xFFD2DC`, `0xFFDD34`, `0xFB2CD0`) lie in `.rdata` (`0xF95000-0x1145389`), not `.text` (ends `0xF94971`). Each is a `-1` dword followed by an EA allocator category name:

| VA | string | pushed by |
|---|---|---|
| `0xFFD74C` | `ResourceLoad/PackedStream/CompressionRefpack` | 8 sites `0x72C6CE`-`0x72D492` (packed-stream refpack decode) |
| `0xFA1AC0` | `Services/EAMessage/Queue` | 11 sites `0x44C6BA`-`0x44D606` |
| `0xFFD2DC` | `ResourceLoad/AsyncLoader/RecordGeneric` | 13 sites `0x727BE1`-`0x7291ED` |
| `0xFFDD34` | `Cache size` (UTF-16) | 9 sites `0x731F74`-`0x732EB7` (resource cache) |
| `0xFB2CD0` | `ResourceSystem/ResourceMan/Manager` | 13 sites `0x4AFDCE`-`0x4B17EC` |

They are allocation-name arguments left on the stack, so they say **what the main thread was doing**: resource-system work (ResourceMan lookups, async loader records, refpack decode, cache). `TS3W+0xb0011` (`0x4B0011`) is a real return address in the ResourceMan manager code: `call 0x4E17B0` (Lock_Release), then a virtual `call [eax+0x34]` with `(0, 1, 6, 1, 0)`, then `push 0xFB2CD0`. `TS3W+0x3345d0` (`0x7345D0`) is a **function entry** (`push ebx; push ebp; mov ebp,[esp+0x10]`), i.e. a function pointer value on the stack next to the cache code `0x734710`/`0x7347C0`. `d3d9.dll+0x236552` right after `Lock_Acquire+0x1d` is chain junk: `Lock_Acquire`, `LockAcquireHook` and `P5bLock` keep no EBP frame (the hook uses EBP as a register), so the walk after them follows stale saved-EBP values; the `TS3W!FUN_004e2f30`/`4e2c50`/`4e2e30`/... frames it reaches are EA thread start code (`_beginthreadex`, `ResumeThread`, `SetThreadIdealProcessor` imports), not lock callers.

## 3. Which lock, how long, whose cost

Leaf classification (first non-ntdll frame and the ntdll PC; ntdll offsets mapped to this machine's `SysWOW64\ntdll.dll` exports):

| bucket (leaf) | share | ms/frame | ours? |
|---|---|---|---|
| game/CRT code (incl. 355 samples with a stale ts3native word deeper) | 42.8% | 15.5 | game |
| **CS fast path**: PC right after `lock btr` in `RtlEnterCriticalSection+0x25`, `lock cmpxchg` in `RtlLeaveCriticalSection+0x2e`, CS owner helpers | 17.8% | 6.4 | game (lock traffic) |
| S3SS frame limiter wait (`Sims3SettingsSetter.asi+0x57699`) | 9.1% | 3.3 | S3SS (idle budget) |
| other ntdll (mostly more CS enter/leave PCs, heap) | 8.1% | 2.9 | game |
| EA allocator code (`0x4E5E40` MallocCore, `0x4E69D0`/`0x4E5920`) | 6.9% | 2.5 | game (Apex's area) |
| **PC inside ts3native** | 5.4% | 2.0 | **ours** (see below) |
| blocked (`ZwWaitForAlertByThreadId`) in other waits | 2.4% | 0.9 | game |
| d3d9 wait + run | 3.5% | 1.3 | DXVK |
| **blocked in an EA `Lock_Acquire`** (through our hook) | 1.5% | 0.5 | game's lock, we pass through |
| `Lock_Acquire`/`Lock_Release` own code | 1.3% | 0.5 | game |
| blocked on the allocator CS `+0x4E4` (from `0x4E5920`/`0x4E69D0`/`0x4E6A20`, no hook) | 1.2% | 0.4 | game |

- **The waits are the game's own** [verified path, holder inferred]. Real blocking (`ZwWaitForAlertByThreadId` leaf) in `Lock_Acquire` is 1.5% (~0.5 ms/frame), on EA Lock objects that are not `+0x918` (§1); with the allocator CS it is 2.7% (~1 ms/frame). A resource-system allocator name (ResourceMan, AsyncLoader, Cache, Refpack) sits on the stack in 197 of the 287 samples that wait in or enter a lock through `Lock_Acquire`/our hook (69%; ResourceMan alone 143), so the lock is a resource-system lock [inferred]; the holder (async loader thread, sim thread, JobThread0) cannot be read from this run (no simstack). These waits exist without ts3native.
- **The "35% with a ts3native frame" is mostly not our time**: 355 of the 1,005 are samples whose leaf is game code (stale words in the chain), 265 are the CS fast path reached through the hook, 155 have the PC in our code, and only 10 are a real block with our frame visible.
- **Our cost = hook overhead, not a wait** [inferred, sampling]: PCs in `LockAcquireAsync` 46, `LockAcquireHook` 40, `LockReleaseHook` 22, `P5bLock` 18, `RjpStub14` 13 = 139 samples = **4.9%, ~1.75 ms of a 36 ms frame**; plus `RefpackFast` 16 (0.6%, our decoder that replaces EA's slower one: a saving, not a cost). The hook layers add ~25% to each main-thread lock call (hook 4.9% vs CS fast path + Lock_* own code 19.1%), which means the main thread calls `Lock_Acquire`/`Release` at a very high rate in resource work. Per acquire the pass-through runs: `RjpStub14` (TEB read, 3 compares), `QlSharedTrip` (2 TLS reads), `t_tuOpen`/`t_snapRun`/borrow checks, key computation (4 TLS reads), `SypLockBegin` (global + `GetCurrentThreadId`), then **`LockAcquireAsync`'s full SEH prologue** (security cookie, `fs:[0]` link, 0x4C-byte frame) for a `__try` that only the 2 s watchdog uses, a second `GetCurrentThreadId`, then `P5bLock` (2 global compares) and the trampoline; `LockReleaseHook` adds 4 checks per release.
- Player effect: ~1.75 ms per frame is ~5% of frame time at 27.7 FPS: steady, not a hitch. It does not explain frames of 50-80 ms; those are the game's own work (42.8% plain code, resource system, allocator) and Present/DXVK.

## 4. Fix options, ranked by player-visible effect

| # | option | effect (est.) | risk | cost |
|---|---|---|---|---|
| F1 | **Fast path at the top of `LockAcquireHook`**: `if (lock != g_rgLock && GetCurrentThreadId() != g_async.owner && !t_slHold && !t_qlShared && !t_snapRun) return g_lockOrig(lock, edx, deadline);` (same for `LockReleaseHook`: `lock != g_rgLock && lock != g_async.lock && !t_borrow && !t_tuOpen`). Moves the `__try` watchdog block of `LockAcquireAsync` into a separate non-inlined function so the common path has no SEH prologue | ~-1.2..-1.5 ms/frame (the hook drops from 4.9% to ~1%) on every thread, main and loaders too [inferred] | low: for a non-owner thread and a lock other than `+0x918` the current code already ends in `g_lockOrig` unchanged; the only state it touches there is `QlSharedTrip` (kept by the `t_slHold`/`t_qlShared` test). Selftests cover the owner paths unchanged | ~30-45 min + selftest |
| F2 | Release builds without `[RouteJobPhases]` must not install `RjpStub14` (already so: the stub is test-only); measurement runs pay +0.45% | 0.16 ms/frame in measurement runs only | none | 0 (note in freeze-measure reports) |
| F3 | Naked asm pre-filter in front of the hook (`cmp ecx,[g_rgLock]; jne orig` + TEB tid compare), no C frame at all | another ~0.2-0.4 ms/frame beyond F1 [inferred] | low-medium (asm, prologue rules of the detour chain with S3SS/Apex: docs/68 `hook-sites.ts --check`) | ~1 h |
| F4 | Stop hooking `Lock_Acquire` globally; retarget only the `+0x918` call sites (docs/152 takers) | removes all hook cost | high: the owner-thread pump (HA_OTHERLOCK, any lock while a job runs) and P5b/TailUnlock admission need every acquire; dozens of sites | days; not worth it after F1 |
| F5 | Exempt the main thread from gates / try-lock on the main thread | **0**: the main thread never reaches a gate, a pump or a `+0x918` wait (§1) | - | do not build |
| F6 | The game's own lock traffic and waits (CS fast path 17.8%, allocator 6.9%, blocked 2.7%: ~12 ms/frame together) | biggest pool, but EA code: per-thread allocator cache (`[Alloc]`, off), ResourceMan lock split | allocator and resource system are Apex Radiance's area (issue #32): hand over the numbers, no fix here | - |

## 5. Cheapest probes

1. **Offline (no game), F1 sizing**: a selftest micro-benchmark, uncontended fake lock, 10^6 acquire+release through `LockAcquireHook`/`LockReleaseHook` vs `g_lockOrig`/`g_rgRelOrig` from a non-owner thread: ns per pair now and after F1.
2. **Main-thread lock counters** (`[MainLockProbe]`, off; `[HitchAttr]` only covers the sim thread / job owner, `HaNote` is called only from owner-thread waits): in the hook, for `GetCurrentThreadId() == g_mainTid` only: count acquires; first `g_lockOrig(lock, edx, &kTry)` (one `TryEnterCriticalSection`, `Lock_Acquire`'s `deadline 0` path), on `-2` time the real acquire with QPC and add to a 16-entry table keyed by `(_ReturnAddress(), lock)` (count, us, max us; first-come slots, overflow counter); `lock == g_rgLock` split out. Counters: `mlAcq`, `mlContended`, `mlWaitUs`, `mlMaxUs`, `mlRgAcq`, 16 x {key, lock, n, us}; reserve the next counter version after v97 in `docs/registry.md` at build time. Report per minute next to `frames.csv` (frames > 50 ms with their lock wait). One 10 min `bp-freeze-pan` run gives the acquire rate (to turn the F1 benchmark into ms/frame), the real callers (the stacks cannot), and whether any lock wait lines up with long frames. Cost on the main thread: one extra `TryEnter` per acquire (same semantics: Enter after a failed TryEnter).
3. Re-run the same job after F1: the ts3native PC share on the main thread should fall from 4.9% to <= 1% and frame time p50 by ~1-1.5 ms (clean night only, docs/162).

## 6. Sampler note

`walk()` in `tools/probe/agent.js` follows EBP and accepts any word in a loaded module as "code", including `.rdata` (strings) and SEH handler pointers. Main-thread stacks through FPO code (our hooks, `Lock_Acquire`) lose the real caller after 2-4 frames. Reports that count "samples with a ts3native frame" overstate our share; use the leaf (PC) for cost and treat deeper frames as context. A `.text`-only filter for TS3W words would remove the string pointers.

## 7. Addendum 2026-10-22: F1 built, and gap #2 (who holds the lock) as `[HitchAttr] Holders` (ts3native 0.36.25, `asi.0330`, not run)

**F1** (`ts3native.cpp` `LockAcquireHook`, `routegate.cpp` `LockReleaseHook`):

- `LockAcquireHook` is now a small `noinline` front: TEB thread id (`fs:[0x24]`), then `lock != g_rgLock && tid != g_async.owner && tid != g_rjpSyncTid && !(t_slHold | t_qlShared)` goes straight to `g_lockOrig`. Everything else runs the old body, now `LockAcquireFull(lock, edx, deadline, ret)`, with the hook's return address passed in, so the wait keys and `G11Touch` stay the same. The fast path skips `QlSharedTrip`, the TailUnlock/clone/borrow checks, the key's 4 TLS reads, `SypLockBegin` (`GetCurrentThreadId`), `LockAcquireAsync` (second `GetCurrentThreadId`, SEH prologue), `P5bLock` and the post-acquire `+0x918` checks. On the full path each of these either needs `+0x918`, the job owner, the sync-session thread or a QL/SlotEnabledRO hold, or ends in `g_lockOrig` unchanged, so the outcome is the same. `g_rjpSyncTid` moved from `routephases.cpp` to just before the hook because the fast path reads it. It was not in the sketch in §4, but without it `SypLockBegin` would push frames for the sync-session thread.
- `LockReleaseHook`: `lock != g_rgLock && lock != g_async.lock` goes straight to `g_rgRelOrig`. Every later check needs one of those two locks, and the `t_snapRun` pass-through ends in the same call.
- The watchdog's guarded `OwningThread` read moved into `LockOwnerTid` (`noinline`), so `LockAcquireAsync` has no SEH frame on the owner/`+0x918` paths either. The behaviour is unchanged.
- Selftest `TestLockFastPath` (`lockfast-test.h`) checks the following. A non-918 lock taken by a non-owner calls the original once and touches no counter. A QL-shared hold still trips `ltQlHeldBlock`. `+0x918` still counts `rgAcquires`, sets `rgOwnerTid` and clears it. The owner takes the full path. The H6 borrow is still returned as 2 and released without the lock.

**Micro-benchmark** (selftest, fake `Lock_Acquire` on a `CRITICAL_SECTION`, 10^6 pairs, best of 3; machine noisy, 3 runs each):

| | hook ns/pair | original ns/pair | hook overhead |
|---|---|---|---|
| before F1 (master `31e01af3`) | 26.3-30.8 | 22.2-24.8 | +2.6..+8.2 (median +4.4) |
| after F1 | 22.9-23.9 | 21.6-22.7 | +1.0..+1.6 |
| after F1, `[HitchAttr] Holders=1` | 25.5-31.2 | same | +3..+9 |
| 4 threads on one lock (both) | 121-184 | 112-179 | within noise (the CS contention dominates) |

The hot-cache benchmark puts the old overhead at only ~4 ns per pair. The game sample share (4.9%, §3) implies far more per call: cold TLS/code lines on the main thread, and `RjpStub14` in that measurement run [inferred]. The player effect still has to come from the §5.3 rerun (clean night): ts3native PC share on the main thread <= 1%.

**Gap #2: `[HitchAttr] Holders=1`** (`hitchattr.cpp`, default 0, `HoldersMinMs=20`, counters v98 `ho*` at +0x4214, export `ts3native_lockholders`):

- When on, the hook calls `HoAcquire`. Every thread's outermost acquire (`r == 1`) writes `{lock, tid, return address}` into a 1024-entry direct-mapped side table. The outermost release (count at `+0x20` == 1) clears the thread's own entry before the release.
- A sim or main thread acquire of a lock other than `+0x918` that another thread holds proceeds as follows. The holder thread comes from `CRITICAL_SECTION.OwningThread` at `+0` [inferred, as the watchdog reads it]. The holder's site comes from the table and must match both lock and tid, otherwise it is 0 (`hoNoSite`). The acquire is then timed with QPC. A wait >= MinMs writes a 32-byte record to the 64-entry ring and a ts3native.log line (the first 200, then every 100th).
- Holder roles: sim (`0x11D8A8C`), main (`g_mainTid`), route worker, replay consumer, and other (EA job threads, loaders, audio: the tid is shown).
- `+0x918` waits stay with `[HitchAttr]` keys. The acquire itself goes through the same `LockAcquirePlain`, so with Holders on the only change is the timing and the side table.
- `stutter-profile.js` reads new ring records at each iteration boundary into the hitch line's `holders`: up to 5, longest first, with `who`, `ms`, `lock`, `by`, `tid`, `site`, `at`. `worst-hitches.ts cause().holder` turns the sim thread's longest wait (else the main thread's) into "sim waited N ms on lock L held by <role> <tid> from <site>" and prints it in the worst-iterations table.
- Selftest `TestLockHolders` is a real two-thread case. A holder thread posing as the route worker takes the lock through the hook and keeps it 40 ms. The "main" thread waits 43-49 ms, and the record names the holder tid, role and site exactly. An uncontended pair sets and clears the entry with no record. A thread that is neither sim nor main writes no record.
- Cost while on: ~3-4 ns per pair on every thread (table write and clear), plus 2 QPC reads per contended sim/main acquire. It is a probe for measurement runs, not a release setting.
- shortcut: the holder is read once at wait start, so a lock handed through several holders during one wait names the first. A lock held since before the switch was on has no site.

Checks: full selftest, `steerro-test` default + `STEERRO_PHASES` + `STEERRO_SYNCPHASES` ok, `hook-sites.ts --check` 0 missing, `check-docs-code.ts` ok, `worst-hitches.ts --self-check` and `freeze-report.ts --self-check` ok. `asi.0330` = `asi.0329` + F1 + Holders, sha256 `49e006433331ebcdd913f8486e04a46d350dd50a5456500e9fe698bf013479ca`. Next: §5.3 rerun on a clean night with `asi.0330`; one run with `Holders=1` to name the holders behind the main/sim waits.

## 8. Addendum 2026-10-22: why `[HitchAttr] Holders` logged holder site 0, fixed in 0.36.26 (`asi.0331`, not run)

The first `Holders=1` run (`asi.0330`, `freeze-measure-2026-10-22-bp-freeze-pan`) logged four waits. Two of them had holder site 0:

| wait | lock | waiter site | holder | holder site |
|---|---|---|---|---|
| main 38 ms | `0C8D0C28` | `0x4B0021` | thread 41184 | 0 |
| sim 41 ms | `0C9ADF00` | `0x4A3883` | main | `0x729E2C` |
| main 33 ms | `0C9ADF00` | `0x728DC3` | sim 24004 | 0 |
| sim 30 ms | `0C9ADF00` | `0x728C4A` | main | `0x729E2C` |

**The locks** [verified, call at key - 5 and the lock operand, as in I-155-4]:

- `0C9ADF00` = AsyncLoader `[this+0x40]` (docs/39 §2 `g_AsyncLoaderLock`). It is taken at `0x728C30` (key `0x728C4A`), `0x728DB0` (key `0x728DC3`) and `AsyncLoader_RequestResource 0x729E10` (key `0x729E2C`). Key `0x4A3883` is the scoped-lock constructor `0x4A3870`: it stores the lock pointer and calls `Lock_Acquire`, so that key names no caller.
- `0C8D0C28` = ResourceMan `[this+0x48]`, the `GetPackageSource 0x4AFFC0` lock. Key `0x4AFFD6` is the function's first acquire. Key `0x4B0021` is its re-acquire inside the probe loop: the function releases the lock around each probe and takes it back (docs/39 §1), so this lock changes hands often. The other thread took it at `0x8BA736` (key `0x8BA73B`, `[esi+0x48]`; the 22:05 run recorded that site).

**Every acquire goes through `Lock_Acquire 0x4E16F0`** [verified]:

- No inlined copy in the exe follows `EnterCriticalSection`/`TryEnterCriticalSection` with the `+0x20` count increment.
- `Lock_Acquire` returns the new count `[lock+0x20]`: 1 for the outermost acquire, n > 1 for a recursive one, -2 for a timeout, -1 for a failed mutex wait.
- `Lock_Release 0x4E17B0` decrements the count before `LeaveCriticalSection`.
- Byte `+0x24` picks the kind. Nonzero means a `CRITICAL_SECTION` at `+0`. Zero means a mutex handle at `+0` waited with `WaitForSingleObject` (`0x4E175F`). A mutex lock has no `OwningThread`, so Holders never records it.
- Our full path returns the original's value. P5b, TailUnlock and the H6 borrow (which returns 2) touch only `+0x918`, which Holders skips.
- With Holders on, `LockAcquireHook` calls `HoAcquire` before the F1 test, so no acquire skips the table (question 4: no).

**Causes of site 0:**

- **A. The holder wrote its entry after its own record** [verified, code order + log order]. A sim or main acquire that itself waited first ran `HoNote`: QPC, the ring and the `Log` file write. Only after that did it write its table entry. On a lock that two threads pass back and forth, the other thread arrives during that log write. It reads `OwningThread` = the new holder, finds no entry, then waits the holder's whole hold. The log shows this order: "sim waited 30 ms ... held by main" then "main waited 33 ms ... held by sim from 0" on the same AsyncLoader lock.
- **B. The waiter read the table once, at wait start** [verified in code; that it caused the live thread-41184 case is inferred]. At a hand-off the new holder has `OwningThread` set but has not yet written its entry; the window is the return through the hook. `GetPackageSource`'s release and re-acquire loop makes a hand-off coincide with the main thread's next acquire.
- **C. Clear on release** [verified in code]. The holder's entry was cleared before the real release. A holder that releases and takes the lock again leaves no entry until its new write, and the waiter saw nothing.
- **D. Direct-mapped collisions** [inferred, no live count]. Any other lock in the same slot, taken during the hold, overwrote the entry. Its release then cleared the slot.

**Fix (ts3native 0.36.26, `hitchattr.cpp`, counters still v98):**

- The entry is written right after the acquire returns. For a waiting sim or main acquire, that is before `HoNote` (A).
- The waiter reads the holder's entry at wait start and again when its acquire returns, before writing its own. It prefers the second read: by then the holder has released, so its entry is complete (B).
- Entries are never cleared. `HoRelease` is gone, and `LockReleaseHook` has no Holders test at all (C).
- The table is 4-way set-associative: 256 sets of one 64-byte line. A lock reuses its own way; otherwise a per-set FIFO picks the victim. A held lock loses its entry only when 4 other locks of its set are taken during the hold (D).
- With `Holders=0`, the cost is unchanged in the acquire hook (one global test) and one test lower in the release hook.

**Shortcuts:**

- A lock handed through several holders during one wait still names the first holder, with that holder's site only while its entry is still in the set.
- In the instant before a holder writes, the start read can find that same thread's previous site for that lock. The end read replaces it once the holder has released.

**Selftest (`lockfast-test.h` `TestLockHolders`):** one holder mode per cause, each with a real two-thread wait as main.

- Mode 1 holds the lock through `g_lockOrig` and writes its entry 15 ms into main's wait (A/B). The record names the late site. 0.36.25 logged 0.
- Mode 2 takes and releases through the hooks, then takes the lock again without a write (C). The record names the first site. 0.36.25 cleared it.
- Mode 0 is the plain case. The waiter site equals the entry main wrote before its record.
- Five fake locks in one set (D): four stay, a re-acquire keeps its way, and the fifth evicts the oldest.
- The uncontended case shows that a recursive acquire does not rewrite the entry and that the entry stays after release.

Checks: full selftest (twice), `steerro-test` seeds 1-2 x 3000 + `STEERRO_PHASES` + `STEERRO_SYNCPHASES`, `waterjps-test`, `decompress-test`, `hook-sites.ts --check` 0 missing, `check-docs-code.ts` ok. Bench with `Holders=1`: 24.1 ns per non-918 pair (hook 21.8, original 20.9).

`asi.0331` = `asi.0330` + this fix, sha256 `380f90054a65f4a5de49dce0843aa4e24f8e170205f4c962dedfaca41498f152`.

Next: one `Holders=1` run on `asi.0331`. Expect `hoNoSite` at 0 or near it, and every holder to name a site. Ledger §265.