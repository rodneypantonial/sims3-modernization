# 67. Crash triage: the 2026-10-14 Swarm wild call, the 2026-10-15 ModelBuilder teardown, the 2026-10-18 lighting room hash (§5), the 2026-10-19 lot-0 footprint remove (§6), the 2026-10-19 Frida handler call to NULL beside the int3 sampler (§7) the 2026-10-19 A\* node on a free list (§8), the 2026-10-19 shared empty hash bucket (§9) and the 2026-10-19 room list free and orphaned EA heap lock (§10)

Static only: Ghidra headless on a private copy of the UI4 project (`game-copy/ghidra-crash`, symbols CSV imported first), the two EA crash reports (`<Documents>/Electronic Arts/The Sims 3/xcpt <machine> 26-10-14 11.58.36.txt` and `... 26-10-15 15.38.26.txt`; no `.mdmp` exists for either, only for 2026-10-11), `crash-ledger.ts triage`, a byte scan of the installed ASI files, and a read of `native/ts3native/alloc.cpp` and the agent's `TravelHandlers.cs`. The game was not run. Times are UTC; the report file names keep the local clock.

Tags: **[verified]** = read in the disassembly, the crash report, the PE header or our source; **[inferred]** = reasoning, still to confirm.

## 1. Summary

| | Crash A (2026-10-14 03:58 UTC) | Crash B (2026-10-15 07:38 UTC) |
|---|---|---|
| Report | ACCESS_VIOLATION writing `0x00694690` (TS3W image, `PAGE_EXECUTE_READ`), EIP `0x080469DB` "unknown module" | ACCESS_VIOLATION reading `0x0000000C` at `0x005CEB9A` |
| Thread | main (frame loop `0x59ED59` -> `0x71E640` Swarm service) [verified stack] | main (`0x59ED59` -> `SimService_Update 0x5F0E50`) [verified stack] |
| Where | `Swarm_EntryOwner_Reset 0x930380` -> `Swarm_ReleaseEntryRange 0x91A440` -> virtual Release on a dead object -> wild call into heap | `SimService_ReleaseRequestRefs 0x5F0820` -> `RefCounted_AtomicRelease 0x692720` -> `ModelBuilder_DeletingDtor 0x5DE360` -> `ModelBuilder_Dtor 0x5DC450` -> `ModelBuilder_PartRecord_Dtor 0x5CEB60` |
| What is bad | the object at entry `+0x38` holds text (`<NoText></`), not vtable pointers: use after free | a part record's inline ref list holds `0x40A00000` (float 5.0) instead of a pointer; its "vtable" is 8 |
| Mod or our code patching the path | none [verified for ts3native source; byte scan for the others] | none [same] |
| Active change set | `5fa9c931fe86`, **ts3native `[Alloc]` on**, run right after a Frida `--taskstall` run | `365b9c27d8ba`, matrix-tested asi, `[Alloc]` off [inferred from PLAN; current ini `Enabled=0`] |
| Lead | `[Alloc]` per-thread cache: a double free becomes two live owners; same-thread back-to-back double free makes a self-looping bin [verified code read] | CAS model build released on main while its records were already torn or reused; shared refs from sim-thread ThumbnailManager/CAS internal calls [inferred] |

`0x694690` is **not** patched code and nobody hooks it. It is the address of a small device callback thunk (`mov ecx,[esp+4]; jmp 0x6945C0`) that `FrameDb_RegisterDeviceCallback 0x6946A0` stores in the graphics device's callback list [verified]. The value `0x694690` therefore lives in heap data; garbage code that took that dword as a pointer and wrote through it produced the "write to code page" [inferred]. The PLAN note ("check which mod patches that block") can be closed: no mod patches there.

## 2. Crash A: wild call from the Swarm entry release

### 2.1 Stack, decoded [verified: report + disassembly]

| Return address | Function | What it does at that point |
|---|---|---|
| `0x59ED59` | main loop | |
| `0x71E6D2` | `0x71E640` (Swarm service range `0x719000-0x722000`, docs/44 §7) | `call [[this+0x1C]]+0x48` = `0x9160B0` |
| `0x9162EB` | `0x9160B0` (Swarm engine, next to `Swarm_ApplyQuality 0x9164F0`) | |
| `0x91C3D1` | `0x91BAB0` | |
| `0x91A84B` | `0x91A7C0` | walks a pending list, calls slot +4 of the secondary base = `0x930380` |
| `0x930488` | `Swarm_EntryOwner_Reset 0x930380` | releases `+0x22C`/`+0x234`, calls virtual +4 on every 0x58-byte entry's `+0x38`, then `Swarm_ReleaseEntryRange(begin,end)` on the vector at `+0x21C` |
| `0x91A46E` | `Swarm_ReleaseEntryRange 0x91A440` | per entry: `EA_Free(+0x48)`, then `call [[+0x38]]+0x3C` |
| (stale `0x9158AD` at `esp-4`) | `RefCounted_ReleaseAt8 0x915890` | `--[obj+8]`; at 0: `call [[obj+4]]` slot 0 with arg 1 (deleting dtor) |
| EIP `0x080469DB` | anonymous | faulting write |

Registers at the fault: `esp = 0x1AFCE8`, `[esp] = 1`, `[esp+4] = 0x91A46E`, `[esp-4] = 0x9158AD`, `edx = 0x9158AD`, `ecx = 0x3AF674A0`, `esi`/`edi` = current entry and end of the entry vector (7 entries left) [verified]. The pattern fits `RefCounted_ReleaseAt8` reaching refcount 0, pushing 1 and calling the deleting destructor through `[obj+4]`, with the first garbage instruction popping its return address into `edx` [inferred, strong: it explains `[esp]=1`, the stale `0x9158AD` and `edx` together]. `ecx` = `obj+4`; its memory reads `bc 74 f6 3a  b8 2b fe 00 ... "t>\r\n  <NoText></"` [verified], i.e. the object's storage now belongs to an XML text buffer: the entry at `+0x38` points to freed and reused memory.

### 2.2 Where `0x080469DB` is

- Not a module: no image covers `0x08000000-0x08FFFFFF` in the module list [verified].
- Not a hook stub: the same 64 KB area holds ordinary game objects in the outer frames of the same stack (`0x08007860`, `0x08016200`, `0x08039920`, `0x08044710`, `0x0804BC80`) [verified stack data], so EIP sits inside early heap memory [inferred, strong].
- Heap is executable here: `TS3W.exe` has `DllCharacteristics = 0` (no `NX_COMPAT`) [verified PE header], so DEP is off for the process by default, and S3SS reports 393 MB of "Writeable Code" [verified report]. A bad vtable call therefore runs heap bytes instead of faulting at once, and the fault happens wherever the garbage first writes to a read-only page.
- No installed ASI holds `0x91A440`, `0x930380`, `0x915890`, `0x694690`, `0x6945C0`, `0x6946A0` or `0x693780` as a literal [verified byte scan of `Sims3Performance.asi`, `Sims3SettingsSetter.asi`, `MonoPatcher.asi`, `ts3native.asi`, `D3DShaderReplacer.asi`]. S3SS/S3PP can find sites by signature, so the scan does not exclude them alone; but no hook is on the stack and the call came through the game's own vtable dispatch [verified]. ts3native hooks `0x693780` only in the 0.10.7 `[Verify]` build (`vfFdbHolds`), which did not exist on 2026-10-14 [verified source].
- D3DShaderReplacer is not loaded in either report [verified module lists].

### 2.3 Root cause hypothesis

1. **`[Alloc]` cache aliasing (leading).** The run had `[Alloc]` on (PLAN; ledger: `ts3native.ini` changed just before). `EaFreeHook` decides "in use" from the next chunk's bit 0 and the chunk's own flags. A chunk it caches keeps exactly that state, so a **second `Free` of the same pointer passes the same test and is pushed again** [verified: `alloc.cpp` `EaFreeHook`]. On the same thread with no free in between, `*(p-4) = b->head` makes `p` link to itself: every later `Malloc` of that size class on that thread returns the same `p`, and `b->count` underflows because `AcPop` checks `head`, not `count` [verified]. Across threads, two threads each hand out `p` once. Either way two owners share one chunk: one writes text into a Swarm object, the next Release calls through text. The stock heap may tolerate or corrupt differently on a double free; the cache turns it into silent aliasing [inferred].
2. **Plain game use-after-free in Swarm.** A Swarm entry keeps a raw pointer at `+0x38` to a refcounted object that someone else already freed; the cache's LIFO same-size reuse makes the freed memory come back within frames instead of minutes [inferred]. Swarm is driven by `IC_Swarm_*` on the sim thread and updated on main (docs/44 §7), so a cross-thread stop/destroy is a candidate.
3. **Frida leftovers** from the previous `--taskstall` session: unlikely, different process, and no Frida script names any address on this stack [verified grep of `tools/`].

### 2.4 Fix or workaround

- Keep `[Alloc] Enabled=0` (default, current ini) until the cache is fixed [verified ini].
- **Built 2026-10-16 (ts3native 0.10.9, not run in game):** `[Alloc] Dedupe=1` walks the bin (at most `count` links) before a push and drops a Free of a chunk already there (counter `allocDupFrees`, v23; runtime switch `allocDedupe`); `AcPop` treats `count == 0` as empty; `AcEmpty` walks at most `count` links, so an old looped bin cannot hang a flush. Same-thread only; the cross-thread marker is left until `allocDupFrees` shows dups. Selftest covers both same-thread cases (dup at the head and behind another link). Repro `alloc-dupfree` (no VFX action yet).
- Cache fix (code change for a later task): in `EaFreeHook`, reject a pointer that is already cached: walk the bin (at most `PerBin` = 32 links) before pushing, count it in a new counter, and pass nothing to the game's Free (the first free still owns it); in `AcPop`, treat `count == 0` as empty. A cross-thread double free needs a marker in the chunk (for example a tag word in the unused header slot, cleared on pop) or a global set; measure the cost before choosing.

### 2.5 Repro and confirmation

- Selftest first (offline): free the same pointer twice into the hook and assert one push, then the same across two threads. Today's code fails the first assert [inferred from the code read].
- In game: `[Alloc] Enabled=1` plus a counter `allocDupFrees` (duplicate seen in the bin walk) on a Swarm-heavy repro (agent action that starts and stops many VFX: fireworks, fire, spellcasting; add a `repros.json` entry). A non-zero count confirms that the game double frees and that the cache would alias.
- Debug variant: poison cached chunks (fill user bytes with `0xDD` on push, check on pop, count mismatches). Any mismatch is a write after free into a cached chunk and names the size class.
- Ledger: triage lists this crash under `5fa9c931fe86`; a crash-free long `[Alloc]` run after the fix, with the counters above at 0 or handled, closes it.

## 3. Crash B: ModelBuilder teardown on the University enrolment path

### 3.1 Stack, decoded [verified: report stack data + disassembly]

| Return address | Function | What it does |
|---|---|---|
| `0x59ED59` -> `0x608666` | main loop, `TextureCompositorQueue_Update 0x608630` (stale frame) | |
| `0x5F0EE9` | `SimService_Update 0x5F0E50` (main-thread service slot, docs/42) | |
| `0x5F0AD3` | `SimService_ReleaseRequestRefs 0x5F0820` | clears `+0x20`, calls its Release (vtable +4) |
| `0x692745` | `RefCounted_AtomicRelease 0x692720` | `lock xadd [this+4],-1`; at 0 calls vtable +8 with 1 |
| `0x5DE368` | `ModelBuilder_DeletingDtor 0x5DE360` | |
| `0x5DC7D9` | `ModelBuilder_Dtor 0x5DC450` (vtable `0xFE3DC8`, ctor `0x5D7C20`) | last loop: `ModelBuilder_PartRecord_Dtor` on every 0x74-byte record in `[+0xC,+0x10)` |
| EIP `0x5CEB9A` | `ModelBuilder_PartRecord_Dtor 0x5CEB60` | second loop: for each pointer in the inline vector `[+0x28,+0x2C)`: `mov eax,[ecx]; mov edx,[eax+4]; call edx` |

At the fault: record `ebp = 0x33E7C4B8`, vector begin `edi = 0x33E7C4FC` (= record `+0x44`, its own inline buffer), end `ebx = record+0x4C` (two slots), first slot `ecx = 0x40A00000` (float 5.0), `[0x40A00000] = 8`, so `[8+4]` faults at `0x0000000C` [verified]. The record header (begin/end) is consistent but the slot holds a float, not an object pointer. The first loop over `[+0x4C,+0x50)` ran without a fault.

**What is "null":** nothing is null. The object pointer is a float that was written into the slot; the vtable read through it returned 8 [verified].

### 3.2 Mods and our code on the path

- No hook on the stack; ts3native has no address in the CAS range `0x5CB3B0-0x5F16F0` [verified grep of `native/ts3native`]; the installed ASI files hold none of `0x5CEB60`, `0x5DC450`, `0x5DE360`, `0x692720` as a literal [verified scan].
- The agent is managed code on the sim thread; the crash is on the main thread [verified stack]. The `dialog: true` path of `travel_university` calls the game's own `AcademicCareer.EnrollInAcademicCareer` (opens the enrolment dialog) and then `TravelUtil.TriggerTravelToUniversityWorld` [verified `TravelHandlers.cs`]. The agent only triggered the vanilla dialog path; it touches no CAS objects.
- ts3fixes: nothing in the CAS model builder [not checked line by line; no native code].

### 3.3 Root cause hypothesis [inferred]

The ModelBuilder is refcounted with an atomic release and is shared: `RefCounted_AtomicRelease` has callers in SimService (main), ThumbnailManager `0xD4D9F0` (UI thumbnails), `IC_CASUtils_SetModelInGameObjectDelayedImpl` (sim thread) and lot remote calls [verified callers]. The enrolment dialog shows Sim portraits, so thumbnail model builds for the household are in flight; closing it releases them while SimService finishes the build on main [inferred]. A record whose ref list holds a float means the record was torn down or reused before this final teardown: either a second teardown of the same records (a release path that ran the record dtors without clearing the vector), or the record memory reused by a writer of floats (vertex/morph data) after an earlier free. docs/43 already lists the ThumbnailManager internal calls running on the sim thread with no owner check (PLAN crash hardening item 4); this crash is a second symptom on that boundary.

### 3.4 Fix or workaround

- Workaround in place: `travel_university` now defaults to the no-dialog path (`DegreeManager.SetEnrollmentData`, the enrolment event, `TriggerTravelToUniversityWorld`) [verified source]; keep `dialog: true` out of queues until 3.5 is done.
- Native fix candidate (after the probe): in `ModelBuilder_PartRecord_Dtor`, skip and count slots that are not heap pointers (outside the EA heap ranges or misaligned) instead of calling through them. It hides the symptom only; the owner fix is to serialise ThumbnailManager CAS work with SimService (same thread or one lock).

### 3.5 Repro and confirmation

- Repro: `travel_university {dialog: true}` answered with cancel (and once with accept) N times on a household with two or more YAs, `[Alloc]` off. Add a `repros.json` entry; if it crashes again at `0x5CEB9A`, the dialog path is confirmed.
- Probe: count `ModelBuilder_Ctor 0x5D7C20` / `ModelBuilder_DeletingDtor 0x5DE360` per thread, and `RefCounted_AtomicRelease` calls reaching 0 by return address; a release to 0 on the sim thread or from `0xD4D9F0`, or two dtor runs on one object, confirms the race or the double teardown. The existing `[Verify]` thumbnail probes (thread of thumbnail create) give the first half.
- Ledger: triage lists this crash under `365b9c27d8ba` (no ts3native change just before) [verified triage run].

## 4. Open items

- No `.mdmp` for either crash, so the garbage bytes at `0x0804695B` and the exact Swarm object type stay unknown. Keep EA's minidump for the next crash (copy it out of `<Documents>` before the next run) [inferred: the game writes one only sometimes].
- Name the Swarm object types: the entry vector at `+0x21C` (0x58-byte entries) and the class with Release at vtable `+0x3C`.
- Confirm whether EA's `GeneralAllocator_Free 0x4E5920` detects a double free on its own (decides how bad the stock behaviour is without the cache).

## 5. Crash C (2026-10-18 16:11:26 UTC): lighting room hash use after free, 9 s after a direct future-world arrival

Static only (2026-10-19): capstone on the Steam 1.67 exe copy, EA's report `<Documents>/Electronic Arts/The Sims 3/xcpt <machine> 26-10-19 00.11.26.txt` (no `.mdmp`), the job log `probe-out/queue/travel-all-2026-10-18-bp-travel-future-direct.log`, the ts3native and probe logs of the same job, `S3SS.toml`, the change ledger (`5c328696e19f`) and our source. The game was not run.

### 5.1 Facts

| | |
|---|---|
| Report | ACCESS_VIOLATION reading `0x44973010`, State `MEM_FREE`, EIP `0x006A81D6`, main thread (stack `0x1AF9C4`, the same range as crash A's main-thread stack) [verified] |
| Job | `bp-travel-future-direct` (queue `travel-all.json`): agent `travel_future {prepare, direct}`, left NewDowntownWorld 16:10:56, arrived in FutureWorld 16:11:17, autonomy back on 16:11:17, crash 16:11:26, process gone 16:11:31 [verified job log] |
| Change set | `5c328696e19f`: `ts3fixes.package` `dae594e7da39 -> 2b3834b912c1` (door adapter build, `doormirror` off), new `ts3agent.package`, ts3native pinned `tested` 0.10.1 (save compress, footprint index, decompress, flush only) [verified ledger + ts3native log] |
| Modules | S3SS 1.6.3 (`Mimalloc` on, `LotStreamingOptimizations` on), S3PP, MonoPatcher, ts3native, DXVK, the Frida agent of `ts3probe serve` (no opt-in watch started) [verified report, `S3SS.toml`, probe log] |
| Same EIP elsewhere | none: the other nine `xcpt` files, `probe-out` and docs/67 §1-4 have no fault at `0x6A81xx` or `0x6C5Cxx` [verified grep] |
| Script errors | one NRaas ErrorTrap report at 16:11:05 (during the load): "Destroyed Object Found" corrections only, no exception [verified] |

### 5.2 Where it faulted [verified: disassembly + report registers and stack]

EA's printed call stack (`0x1AFAA0`, `0x608BDD`, `0x608BEE`) is stale stack data. The real frame: `0x6A8190` pushes 0x20 bytes, and `[esp+0x20]` = `0x6C5CCD`, the return address after the only call to `0x6A8190` (at `0x6C5CC8`).

| Frame | Function | Role |
|---|---|---|
| `Renderer_PreRenderUpdate 0x6EBB70` | (docs/44 §1) | main-thread frame entry |
| `LightManager_FrameUpdate 0x6B0500` | (docs/44 §2) | calls `0x6C5E20` on the light update tree at `+0xD4` |
| `0x6C5E20` | LightUpdateTree update | two passes over the lots, then a tail jump to `0x6C5C20` |
| `0x6C5C20` | LightUpdateTree pick-next-room (tag `Lighting/LightUpdateTree/RankArray`) | only when no room is in progress (`+0x74` = 0); for each entry of the tree's lot hash `+0x58` (tag `Lighting/LightUpdateTree/mLots`, constructor at `0x6C5340`) and each level -4..7, takes the level's LightingSolver from the lot object (`+0x10 + (level+4)*0x1A4`) and calls `0x6A8190`; then sorts the pairs (`0x6C58B0`) and starts the best room above a threshold (`0x69E860`: room state 2 -> 3) |
| `0x6A8190` | LightingSolver collect-ranked-rooms | walks the solver's room hash at `+0x234` (buckets) / `+0x238` (bucket count) (tag `Lighting/LightingSolver/Rooms`, constructor `0x6A8489`); per node: room = node `+0x10`, rank = `0x69E770(room)` (non-zero only in state 2), push (room, rank); next node = node `+0x80` |
| EIP `0x6A81D6` | read of node `+0x10` | node `esi = 0x44973000` lies in released memory |

Registers: `esi = 0x44973000` (the node), `ebx = 0x77A3057C` (current bucket slot, holds `0x40A0FF90`), `ebp = 0x1AF9FC` (the caller's inline pair vector: begin `0x1AFA10`, end `0x1AFAA0`, so 18 rooms were already collected). The bucket slot's head is not `esi`, so `esi` came from the `+0x80` link of a node that is still mapped: a node was freed without being unlinked from its chain, or its predecessor is stale too [verified registers; reading inferred].

### 5.3 Whose code is on the path

- **ts3native 0.10.1:** no hook in `0x69E000-0x6CA000` or on `0x6B0500` / `0x6EBB70`; its hooks are the save compress commit `0x4A850F` and codec slot, the footprint index `0x666488` / `0x82C940` / `0xAAC320`, the decompress and the save flush [verified ts3native log + source grep]. The decompress reported 0 mismatches [verified log].
- **ts3fixes door adapter (`DoorMirror.cs`):** a managed postfix on `CommonDoor.ImportContent` that runs the game's body first, then only marks its own table invalid and sends `GetOption2` messages on a handle-0 route; the game answers false for handle 0 (docs/18) and 0.10.1 has no door channel. It touches no native lighting or room data [verified source]. `doormirror` was off, so no table was built.
- **Agent:** `FutureDirect` (our copy of `TimePortal.TravelToFuture`'s boarding plus `GameStates.TravelToVacationWorld`, docs/77 §4.2) ran 50 s before the crash, in the old world; after arrival the harness only turned autonomy back on [verified job log + `TravelHandlers.cs`]. The boarding calls match the game's interaction; the order differs only in `SetDepartureTimePortal` (ours before the switch call, the game's after it) and in skipping the walk and animations [verified source compare].
- **Frida probe:** only opt-in watches hook game code; none was started in this job [verified probe log + `agent.js`].
- **S3SS:** `Mimalloc` replaces the game heap; mimalloc returns a segment to the OS once its last block is freed, which is what turns a stale read into `MEM_FREE` [inferred: mimalloc design; the stock EA heap would usually keep the page mapped and the stale read would pass]. `LotStreamingOptimizations` (throttled lot LoD transitions, map view blocker in `WorldManager::Update 0xC6D570`) changes when lots and their lighting load right after arrival [inferred: not read].

### 5.4 Verdict

- Freed object: a node of one lot level's LightingSolver room hash (key = room id, LightSolverRoom pointer at `+0x10`), read by the main-thread light update [verified].
- Who freed it: not found statically. Candidates: a room erase that left the chain linked, or the solver of a lot unloaded or rebuilt right after arrival while the light update tree still lists it [inferred].
- Owner: **game (EA lighting); none of our code runs on the faulting path or writes its data** [verified for ts3native, ts3fixes and the probe; inferred for the agent as trigger]. The rule "our build crashed = our bug until proven" is not closed: the direct trip is our own sequence and the only difference from the portal run of the same queue (16:04 job: arrived 16:07:50, ran 20 s and saved). One run per mode is not enough.
- No fix written: nothing of ours is on the path. A guard (SEH around `0x6A8190`) would protect one reader only; the same hash is walked by `0x6A7CF0`, `0x6A7FF0`, `0x6A80E0`, `0x6A86F0` and `0x6A88B0` [verified `+0x234` readers], so it is not worth building before a repro.

### 5.5 Repro and next steps

- Queue `tools/harness/queues/future-lighting-uaf.json`: three direct trips, two portal trips, one direct trip with S3SS `LotStreamingOptimizations` off; same `tested` asi and ini as `bp-travel-future-direct`. Repro entry `future-direct-lighting-uaf` (`repros.json`) is the short manual version.
- Reading: a crash at `0x6A81D6` (or in another `+0x234` reader) in direct jobs only = our direct sequence leaves lighting state behind (fix in `FutureDirect`, start with the `SetDepartureTimePortal` order); in both modes = game bug, and the S3SS-off job says whether lot streaming triggers it; no crash in 6 runs = rare game race, log it and move on.
- If it repeats: a Frida watch on frees of room hash nodes (allocator tag `Lighting/LightingSolver/Rooms`) per solver names the eraser.
- Effort: 6 game runs of ~4-5 min (~30 min with restarts), then 1-2 hours of analysis if it repeats; a guard, if still wanted, ~2 hours.


**Repro 2026-10-19 13:23-13:43 PHT (`future-lighting-uaf.json`, pinned build, 6 jobs):** 3 direct trips, 2 portal trips, 1 direct trip with S3SS LotStreamingOptimizations off: **0 lighting crashes in 6**. Every job FAILed the same non-crash way: the trip home was not queued (time portal state Inactive, the known future travel_home failure). Verdict per the plan above: a rare game race, logged; no guard. If it shows up again, use the Frida free-watch on `Lighting/LightingSolver/Rooms` nodes.

## 6. Crash D (2026-10-19 00:21:02 UTC): lot-0 footprint remove runs past its range, `bp-snap-p4` on ts3native 0.31.2

Static only (2026-10-19): EA's report `<Documents>/Electronic Arts/The Sims 3/xcpt <machine> 26-10-19 08.21.02.txt` (no `.mdmp`), the job's ts3native log (`probe-out/queue/snap-p4-2026-10-19-bp-snap-p4-0016-ts3native.log`), capstone on the Steam 1.67 exe copy and a map file of an identical 0.31.2 rebuild. Full write-up: docs/94 §21.11; ledger docs/51 §104.

| | Crash D |
|---|---|
| Report | ACCESS_VIOLATION reading `0x00000007` at `0x0063576A` (`LotRouting_RemoveObjectFootprints 0x635700`), sim thread, 5.1 min into a 10-min shadow run |
| Change set | `617090506a92`: ts3native 0.31.2 (`a3260bd7…`) and its ini (`[RouteSnap] Enabled=1 Lots=replay Mode=1`, async routing, discourage deferral) [verified ledger] |
| Stack | managed `RemoveDiscourageFootprint` IC (`SnIc19`) → `DiscRemoveHook` → `0x834860` → `0x635700` (lot 0) [verified] |
| Where | the lot-0 walk over map M stepped twice over an FME whose remove was pending in `P+0x3C` and ran from the range end to the bucket sentinel (`esi = 0xFFFFFFFF`) [verified disassembly; reproduced on the game's code in `steerro-test` G2] |
| Whose code | the game's loop; our `0x635700` stub only counts (contract checked) [verified]. Why the remove was pending: inferred (deferral while the Q was iterated; a clone-run deferral into the live maps is one candidate) |
| Verdict | our build's run, so treated as ours: 0.31.4 flushes the id's pending removes before a lot-0 remove (`lrPendFlush`, `lrPendBusy`) and stops clone-run deferrals (`pcDeferStop`). Also fixed: 11 first-chance AVs per load in `SnCloneProbe` that hid later exceptions from the crash log |
| Next | `snap-p4` BP and IP on 0.31.4; two clean runs to call it fixed |
| Other builds (0.31.5) | the 0.31.4 guard sat in the `[RouteSnap]` stub only. The deferral that sets up the walk is the game's own, and no stock, shipped-ini, design-D or async path was shown to create it [inferred, docs/94 §21.12]. All the same, 0.31.5 adds `[LotRemoveGuard]` (default on): the same flush before every lot-0 `0x635700` on every build. Tested offline on D's own lot-0 call (`steerro-test` G2/D) |

## 7. Crash E (2026-10-19 01:04:33 UTC): a second Frida agent's exception handler calls NULL while the int3 sampler runs, `bp-rare-winter-pond`

Static only (2026-10-19): EA's report `<Documents>/Electronic Arts/The Sims 3/xcpt <machine> 26-10-19 09.04.33.txt` (no `.mdmp`), the job's logs (`probe-out/queue/rare-scenarios-2026-10-19-bp-rare-winter-pond*.log`, `-0102-ts3native.log`, `probe-out/profile/...-360.log`), capstone on the Steam 1.67 exe copy and on this machine's `SysWOW64\ntdll.dll` (same base `0x770B0000` as in the report). Ledger docs/51 §107.

| | Crash E |
|---|---|
| Report | ACCESS_VIOLATION reading `0x00000000` with EIP 0; stack top `0x4445958A` in the **second** `frida-agent.dll` (base `0x443F0000`, RVA `0x6958A`), then `0x7712289F` in ntdll [verified] |
| Build | `game-copy/ts3native.asi.snapprobe2` (`ed59c43e15c0`, the `[ProbeSnap]` pass-2 indirect-call sampler, `DelaySec=60`), not the tested 0.24.0 asi; same asi and ini as the PASS of 2026-10-18 16:59 UTC [verified queue rows] |
| Thread | sim thread 31192 (ts3native's route gate lines name it) [verified] |
| What was being handled | ESI points at an EXCEPTION_RECORD: code `0x80000003` (breakpoint) at `0x00634B4C`; EBX points at its CONTEXT (flags `0x1007F`) [verified report register memory]. `0x634B4C` is a `call edx` site in `vcallsites2.h`: the sampler's own int3 [verified] |
| Path | ntdll's vectored handler loop (decode, CFG check, `call eax` right before `0x7712289F`) called the second agent's handler, and that handler called a NULL pointer [verified disassembly of the local ntdll; the NULL inside Frida is not analysed] |
| Timing | the second agent is the stutter profiler (`frida_tools.repl`, spawned at the profile start 01:04:31); its log ends "hooks installed", then "Process terminated". Crash 2 s after the spawn, while the sampler was in its second window (window 1 logged 17,640 int3 hits) [verified logs] |
| Not the cause | `test_env` (same second, but its reply came back OK; the second `fetch failed` is the dead process); the ts3agent and harness merges (no code of theirs on the stack); agent.js (first agent, base `0x67FC0000`, not on the stack) [verified] |
| Verdict | ours (our harness combination): a Frida agent attaching while our int3 sampler raises thousands of breakpoints a minute on the sim thread can run its exception handler before it is ready [inferred: race inside Frida's agent start; one crash in about 40 ProbeSnap profile jobs with the same attach] |
| Fix | `queue.ts` passes `--no-prof` to `profile-world.ts` for every job with `ProbeSnap.Enabled=1`: no stutter profiler session (no second agent attach or unload) beside the sampler. ProbeSnap jobs only need the native log; they lose the `<tag>-<secs>-*` hitch files |
| Verify | rerun `rare-scenarios.json --only bp-rare-winter-pond` 2x (and the rest of the queue as usual): no crash, the job log shows no stutter profiler files. A crash with no second agent loaded = refuted |
| Effort | ~1 h analysis + 10 min fix; live: 2 runs of ~8 min |

## 8. Crash F (2026-10-19 01:07:10 UTC): an A\* graph node on a pool free list while the sim thread's search holds it, `bp-snap-p4` on ts3native 0.31.4

Static only (2026-10-19): EA's report `xcpt <machine> 26-10-19 09.07.10.txt`, `probe-out/queue/snap-p4-2026-10-19-bp-snap-p4-0105-ts3native.log` and `-0105-shadow.jsonl`, capstone on the Steam 1.67 exe copy. Ledger docs/51 §107.

| | Crash F |
|---|---|
| Report | ACCESS_VIOLATION reading `0x00000000` with EIP 0; return address `0x0061E6E0`, then `0x633599`, `0x640759` (stack refs also `0x664B50`, `0x632DFF`, `0x64E926`, `0x6265FE`, `0x4E3F84`) [verified] |
| Thread | sim thread 24572 (main 29240, worker 28288 per the route gate lines) [verified] |
| Where | in-room A\* `0x61E500` (called from `0x633594`): the node it takes from the open list (`esi = 0x49193A08`) gets flag `0x200000` at `+0x30`, then a virtual call through slot `+0x10`. The node's first dword is `0x9C5C3F18`, a heap address, and `[0x9C5C3F18+0x10] = 0`: call to NULL [verified disassembly + registers] |
| What the node is | `+4` reference count word 2, `+8..+0x10` an empty vector whose allocator name at `+0x14` is `0xFEA058` = "Routing/cAStarGraphNode/mNeighborNodes"; the constructor `0x61B380` writes exactly that layout with vtable `0xFEA080`. `0x9C5C3F18` holds the same layout. A node whose vtable word points at another node of its kind is a node on a pool free list (the deleting destructors `0x61B520` / `0x61AD00` push with node `+0` = old free head) [verified] |
| Our code on the path | the A\* family pools (`0x11CEF50`, `0x11CEF68`, `0x11CEF90`, `0x11CF448`) have their pop and push sites redirected by `pools.cpp` in this build (`plReady 7`); P4 clone runs ran on the worker during the 12 s before the crash (89 shadow records, copies filled) [verified log, counters at start] |
| Cause | the node was pushed to a free list while the sim thread's search still held it: an unlocked free list written by two threads [inferred]. In 0.31.4 two pushes do that by design and are only counted: a P4 clone run (worker, no `+0x918`) freeing a game node pushes it into the game's pool (`rpLivePool`), and a thread that owns no clone work freeing a private A\* node pushes it into the private pool the clone run pops (`plForeign`). The counters at the crash are lost (no end dump), so which one fired is not known |
| Verdict | ours (0.31.4 RouteSnap P4 + private pools; crash D of §6 was the same job on 0.31.2 at a different site) |
| Fix (0.31.5, counters v57 unchanged) | those two pushes go to a sink pool object nobody pops: the node leaks instead of entering a free list another thread uses; the first 8 are logged (`private pools: pool N node ... leaked to the sink`), the counters still count them. Tree pools keep their old foreign path. Selftest updated (`TestPools`), `selftest ok` |
| Verify | build 0.31.5, copy to `game-copy/ts3native.asi.snap0315` (the queue points there), run `snap-p4.json` (BP then IP) 2x each. Pass = no crash; read `rpLivePool` / `plForeign` and any sink lines. Sink lines + no crash = cause confirmed. No sink line and a crash at the same place = refuted: next suspect is a non-clone worker job and a sim-thread search on the game's A\* pools at once (check the `+0x918` hold on the `0x633594` path) |
| Effort | ~1.5 h analysis + 30 min fix; live: 4 runs of ~13 min |
| Second cause (2026-10-19, ts3native 0.32.0) | I-94-220: before 0.32.0 a P4 clone run's planner took P from the live W (`[W+8]` = live P), so the worker ran `LotPlan_Inner` -> `InRoomPlan` -> the in-room A\* `0x61E500` on the live P's `cAStarSearch` (`P+0xBC`) without `+0x918`: crash F's own frames (`0x65F0EA` -> `0x65F11C` -> `0x6410FF` -> `0x640754` -> `0x633594` [verified]). Two threads on one open list fit "a node on a free list while a search holds it" better than the pool pushes [inferred]. 0.32.0 gives the copy its own planner W' (docs/94 §21.13); ledger I-67-11 (docs/51 §108). Verify: `snap-p4` BP and IP on 0.32.0, 2x each |

## 9. Crash G (2026-10-19 02:10:03 UTC): a W' cache node in the game's shared empty bucket array, `bp-snap-p4` on ts3native 0.32.0

Static only (2026-10-19): EA's report `xcpt <machine> 26-10-19 10.10.03.txt` (no `.mdmp`), `probe-out/queue/snap-p4-2026-10-19-bp-snap-p4-0208-ts3native.log` and `-0208-shadow.jsonl`, `chain43.log`, capstone on the Steam 1.67 exe copy, and a map file of a 0.32.0 rebuild (`.text` byte-equal to `game-copy/ts3native.asi.snap0320`, sha256 `874706ad…`). Write-up: docs/94 §21.13 (crash G note); ledger docs/51 §110.

| | Crash G |
|---|---|
| Report | ACCESS_VIOLATION reading `0xBF5D9B01` at `0x009A32C0`, main thread 38212, 2 min after load (`ebp = 0x0114C900`, `ebx = 1`) [verified] |
| Before it | the second P4 clone run (plan 3, the first one that was not start == goal) faulted on the worker 40660 writing `0x64B413C8` at `0x004E5220`, inside EA's heap free; `P4 FAULT` turned P4 off. Both faults are in the same log second [verified] |
| Where (main) | `0x5E5E20`, a hash find: bucket array `[ecx+4]` = `0x114C900`, count 1, i.e. an **empty** EASTL hash on the game's shared empty bucket array. It found a non-zero bucket 0 and followed it into a node that was no longer one [verified disassembly + registers] |
| Where (worker) | `0x650B70` (planner on W, `this` = W') ends by freeing its local hash's nodes (`0x643580` → `0xD2BC10` → `0x4E3F70`). That hash was empty too, so it walked the shared bucket 0 and freed a node with a bad chunk header [verified call chain; the node's origin inferred] |
| Cause | `PcWp` (0.32.0) made W' from the live W's bytes and reset the `+0xC0` / `+0xFC` caches to the shared empty array (count 1, size 0), but kept the live **next-resize** words `+0xD8` / `+0x114`. The insert `0x657800` rehashes only when the new size passes that word (`0x4E09B0`, `jbe` at `0x4E09C3`). With the live threshold it did not rehash and linked its node into `0x114C900` bucket 0. From then on every empty hash in the game held that node: lookups on the main thread read it, and the next empty hash destroyed on any thread freed it [mechanism verified on the game's code offline; the W' cache insert in this run inferred from the frames] |
| Sink lines (pools 2, 3, 5, thread 40660) | the worker after the fault: `PcRelease` (DISCARD) → `PcAstarDrop` ran the private A\* object's deleting dtor `0x61EB20` (the route gate line `Q reset (clear path) ... from 0x61EB28 thread 40660`) after the run had left the pool job, so each private node was a "foreign" push and leaked to the sink. Same on every WRAP. A leak, not the crash [verified code path + log line] |
| Consumer AVs (thread 36616) | the replay consumer (`RpThread` → `RpDrainBody` → `RpApply` → `RpQKey`, map file): the "Q the clone does not have" diagnostic read `+0x1C` of a freed Q. The records were the consumer's own replays (self-records, fixed in 0.32.1) on a clone Q freed by the second load-time build. Caught, but they used 8 of the 16 first-chance log slots [verified frames; Q origin inferred] |
| EIP 0 on the main thread | logged after the fatal fault, deeper on the same stack (`esp 0x1AECBC` < `0x1AFB0C`), game frames `0x4D7001`, `0x58D945`: inside the game's own exception handling, not startup and not ours [inferred] |
| Verdict | ours (0.32.0 W') |
| Fix (0.32.2, counters v57 unchanged; includes 0.32.1) | W' starts with `+0xD8` and `+0x114` at 0, as the W ctor `0x657C50` leaves them; `PcAstarDrop` runs the dtor as P4 pool job work (`g_p4PoolsEnterJob` / `LeaveJob`); `RpQKey` checks the Q is readable first |
| Tests (offline) | `steerro-test` K6: live W with grown caches (`+0xD8` / `+0x114` = 0x61), then the game's insert `0x657BC0` into W'+0xC0 after every acquire; on 0.32.0 code: `K6 FAIL (crash G): ... linked node ... into the shared empty bucket array 0x114C900`; on 0.32.2: 26 inserts per seed pair, array untouched, and W' dropped on another thread with its filled cache. selftest `PcCheckWp` (W' thresholds 0; failed on 0.32.0) and `TestPools` (the A\* teardown's private node goes back to the private pool; on 0.32.0 it went to the sink, `plForeign` +1). Seeds 1 and 2 x 3000 `steerro-test ok`, no K3/K6 WARNING; `hook-sites --check` 446 addresses, 0 missing |
| Verify | park 0.32.2 as `game-copy/ts3native.asi.snap0322`, `snap-p4.json` BP then IP, 2x each. Pass: no crash, no `P4 FAULT`, no sink lines, `snPcAcquired` > 0 with shadow records past plan 3 |
| Effort | ~2 h analysis (logs, map rebuild, disassembly), ~0.75 h fix and tests, ~0.5 h docs: ~3.25 h; live: 4 runs of ~13 min |

## 10. Crash H (2026-10-19, `bp-snap-p4` on ts3native 0.32.4): a clone run's room build frees our heap block through EA's free, then the game hangs on the EA heap lock

Static only (2026-10-19): `probe-out/queue/snap-p4-2026-10-19-bp-snap-p4-0252-ts3native.log`, the job log `snap-p4-2026-10-19-bp-snap-p4.log`, the probe log, capstone on the Steam 1.67 exe copy. No EA report and no `.mdmp` (the process did not crash). Write-up: docs/94 §21.13.2; ledger docs/51 §114.

| | Crash H |
|---|---|
| Run | BP, world loaded 10:53:53 PHT; the first real P4 compares (3 shadow records, all M0, job 21.0 ms vs game 20.9 ms) [verified logs] |
| Fault | `P4 FAULT` on the worker 37208, plan 5 kind 0: C0000005 reading `0xAE286F64` at `0x004E5200` [verified log] |
| Where | `0x4E5200` is in EA's chunk free `0x4E51B0`, called from the general free `0x4E5920` (frame `0x4E5945`) through `0x4E3F70` (`0x4E3F84`). `eax` = chunk `0xA2258B60`, so the freed pointer is `0xA2258B70` (`ebx`); `edx` = its size word at `ptr-0xC` = `0x8C02E400`, `esi` = size `0x0C02E400` (192 MB): not an EA chunk header. The large-chunk path reads the next chunk's header at chunk + size + 4 and faults [verified disassembly + registers] |
| Who freed it | `0x5D1E4A`: the end of a `vector<u16>` grow (`0x5D1D60`): new buffer from `0x4E3F90`, then the old `begin` to EA's free. Caller `0x63D516` in `RoomGraph_FirstUse 0x63D2F0`: the push of a portal index into the room record's `+0x24` list (room record = a 0x44-byte entry of lot graph `+0x90`). Up the stack `0x63F488` (`LotPortalSearch 0x63EC40`) and `0x63FB5B` (`LotPlan_Inner 0x63F6E0`) [verified disassembly; the deeper refs from `0xA8E1F0` on are stale stack] |
| Why it is not an EA block | `replay.cpp` `RpVecCopy` copied the clone's room lists into our private heap (`RpAlloc`: `HeapAlloc`, 16-byte aligned, size at `ptr-8`, raw pointer at `ptr-4`) with capacity = size, so the first push past the copied size grows and hands our block to EA's free. `ptr-0xC` is the NT heap entry header when the raw block is 8 mod 16 (this run: garbage size, fault), or our zeroed padding when it is 0 mod 16: size 0, EA's small-bin path then writes into our block and links it into its bin 0 without a fault [verified `RpAlloc` layout + `0x4E51D3..0x4E51F2`] |
| Why a clone run builds a room | `RoomGraph_FirstUse` builds a room whose built byte `+0` is 0: never built, or reset by `0x630F70` (a footprint change clears only that byte and the lot's `+0x8C`; the room's lists stay) [verified]. The cut (`PcPrepare`) builds only each lot's outside room, so other unbuilt rooms reach the clone run. The build empties `+0x24`, frees every `+0x34` element's buffer (`0x61EE20`), resizes `+0x34` (`0x622A30`) and pushes into both [verified] |
| Second defect on the same path | room `+0x34` is a vector of 16-byte vectors (8-byte entries), not flat data (I-94-133 refuted): the flat copy kept the inner buffers **live**, so on a room reset by `0x630F70` the clone run's `0x61EE20` EA-freed blocks the live room still holds (the sim's next rebuild of that room frees them again). In this run that free came before the fault if the room had been built before [mechanism verified; occurrence not known] |
| Why the game died | EA's alloc and free hold the heap's lock object `[[0x11CB864]+0x4E4]` (a `CRITICAL_SECTION`, depth word `+0x18`) around the call (`0x4E5924..0x4E594E`, `0x4E69D4..0x4E6A05`). `P4Guarded`'s `__except` unwound past `LeaveCriticalSection`, so the worker kept the lock and the next allocation on any other thread waited for ever. The log ends after the fault lines (the A\* drop on the worker, which owns the lock, still ran); the harness saw the heartbeat frozen for 60 s and stopped the job ("game died at 02:55:30 UTC"), no xcpt file [lock path verified; deadlock inferred from the frozen heartbeat] |
| Same in crash G | crash G's worker fault at `0x4E5220` was inside the same free and left the same lock held [verified site]; there the main thread's AV came first |
| Verdict | ours (0.29.0 room copy `RpVecCopy` for `+0x24` / `+0x34`; the P4 fault path since 0.29.0) |
| Fix (0.32.7, counters v57 unchanged) | `replay.cpp` `RpRoomListsCopy`: `+0x24`, `+0x34` and every `+0x34` element's buffer copied as EA blocks (`0x4E3F90` with the vector's own `+0xC` word, as the game's grow does), empty lists as NULL; `RpLotFree` frees them with EA's free. `p4.cpp` `P4EaHeapUnlock`: after a fault or lazy stop in a clone run, every hold of the EA heap lock the worker still owns is released (logged `P4 FAULT: EA heap lock released`). A fault inside EA's free can still leave a half-done free; this one wrote nothing before its read |
| Tests (offline) | `steerro-test` K7: the game's `RoomGraph_FirstUse` on each clone lot's unbuilt room (built byte 0, one portal, empty `+0x24`, one `+0x34` list whose buffer sits on a `PAGE_NOACCESS` page during the call); the fake EA free refuses and counts a non-EA block. With the 0.32.6 copy: `K7 FAIL (crash H): ... touched a live room buffer`; 0.32.7: 10 rooms built per seed pair, no foreign free, live rooms untouched. Selftest: the fault variant takes a fake EA heap lock before faulting; after the run the lock is free. Seeds 1 and 2 x 3000 `steerro-test ok`, no K3/K6 WARNING; `hook-sites --check` 446 addresses, 0 missing |
| Verify | park 0.32.7, `snap-p4.json` BP then IP, 2x each. Pass: no `P4 FAULT`, no hang, shadow records past plan 5. A `P4 FAULT` line followed by `EA heap lock released` and a game that keeps running = the fault path works; then triage the new fault |
| Effort | ~1.5 h analysis (logs, disassembly), ~1 h fix and tests, ~0.5 h docs: ~3 h; live: 4 runs of ~13 min |

## 11. Crash I (2026-10-22 01:11:05 UTC): a route-event handler with a NULL vtable in the follower's event list, `bp-destroypark-offfirst` on ts3native 0.36.14

Static only (2026-10-22): EA report `xcpt <pc> 26-10-22 09.11.05.txt` (registers, stack scan), `probe-out/queue/clean-night-routing-extra-2026-10-22-bp-destroypark-offfirst{.log,-0102-ts3native.log}`, `probe-out/profile/...-offfirst-ab-native.jsonl`, capstone on the Steam exe copy. Ledger docs/51 §216.

### 11.1 Fault

| | Crash I |
|---|---|
| Run | BP, seed 12345, `[Discourage] DestroyPark=1`, `--ab destroypark` off-first (warm-up on 0-120 s, off 120-240, on 240-360, on 360-480, off 480-600); profile start 01:03:54 UTC [verified job log] |
| When | 01:11:05 UTC = 431 s into the profile, **71 s into slice 4 (`dpMode` 1, ON)**, not the last OFF slice [verified: job log `destroypark on` at 01:09:54, ab-native row 3 `dpMode` 1]. The OFF slice before had emptied the graveyard (`p5cFlushed` = `p5cParked` = 127 at 240 s): no ON-to-OFF leftover is involved [verified] |
| Fault | sim thread 25936: `ACCESS_VIOLATION` reading `0x10` at `0x84EAC3` `mov eax, [edx+0x10]`; `edx` = 0 = the vtable word of `ecx` = `0x3D90B098` [verified registers + disassembly] |
| Code | `0x84EA00` (follower `this` = `esi` `0x32548A20`): `0x50ABE0` splices the agent's buffer (`agent+0x68`, `ebx` `0x9786C818`) back into the follower's event list at `this+0x1C`, then walks it: node `edi` `0x97CE7490` -> element `eax` = `[node+8]` `0x3ACB1460` (refcount `+4`, plain dec, deleting destructor at vtable `+0`; byte `+0x24` one-shot; `[node+0xC]` type 3 / 9 / 10 feed the follower counters `+0x174` and level `+0x248` +-1) -> handler `ecx` = `[element+0x1C]` -> `call [[handler]+0x10](element)` (still valid?). The handler's first dword is 0: a freed (or never constructed) block. The element holds the handler raw: no reference is taken here or in `0x849570` / `0x853500`, the two other walkers of the same list [verified disassembly] |
| Path | `Locomotion_Update` `0x83CF52` -> `Locomotion_UpdateAgent 0x957210` (`0x957292`) -> agent step `0x956850` (`0x956C67`) -> `0x9564A0` (`0x95669A`) -> `0x84EA00` [verified EA call stack + call sites]. The previous loop iteration removed an element (return address `0x84EC8E` of its node free still on the stack at `0x43CEFC68`), so the bad element is the second one walked this tick [verified stack scan] |
| Swap window | The agent step first moves the follower's list out (`0x84E900` at `0x956968`: follower `+0x1C` -> `agent+0x68`), runs the follow step `0x84D7E0` (`0x95667F`, many unresolved vcalls), then `0x84EA00` moves it back. In between, the follower's own list is empty [verified disassembly] |

### 11.2 What DestroyPark changes

With the switch on, a destroy during a running job parks one reference (`P5cPark`) and `DestroyById` runs at once (events 5/6, owner callback `0x68EC50` -> `+0x3C`, update-list removal `0x938CA0`, flag `obj+0x28 |= 0x400000`, hash erase, its releases); the deleting destructor (`Object_Destruct`, component destructors) waits for `P5cFlush` [verified code]. In 0.36.14 `P5cFlush` ran at the START / READY / CANCEL polls (`GetOptionHook`, yielding script tasks only: a non-yielding caller plans inline), at save / closing / teardown, **and at every `DestroyById` entry with no job running**. `DestroyById` has 18 callers, several native and mid-update: the locomotion effect list `0x84CE30` (called by `0x84EA00` itself at `0x84EE51` and by the follow family `0x84F2B0` / `0x8504D0` / `0x850E20` / `0x851280`), `Animation_DrainDeferred`, lot unload [verified docs/88 §2 + call sites]. So in 0.36.14 any parked object's destructor could run inside a native caller of `DestroyById`, a place where vanilla never frees a foreign object (vanilla frees an object at its own destroy, or where its last holder releases it). docs/88 §5.1 already banned flush points that can sit inside a native caller mid-update (gate entries); the destroy entry was let through on the argument that an extra holder delaying the free is a vanilla state, which covers *when* the free happens but not *where* [design flaw, verified against the doc].

Counters of the crashed run: 279 parks, all flushed by 360 s, `p5cCountLow` 0 (no double release), `vfRtDestroyOffSim` 0; `p5cAgeMaxUs` 170,093 us in slice 3 (T-G3's 100 ms exceeded: one object parked across more than one job) [verified ab-native]. The on-first twin (`bp-destroypark-onfirst`, same build, 509 parks, ages <= 90 ms) **passed**: no crash [verified jsonl row].

### 11.3 Cause (inferred; two candidates)

| # | Mechanism | For | Against / open | Tag |
|---|---|---|---|---|
| B | **Nested flush**: a parked object's destructor runs at a `DestroyById` entry inside a native caller (the follow step inside the swap window, or a later native destroy), frees the handler (or what owns it), and the element, out of the follower's list at that moment, escapes whatever cleanup the destructor does | the flush is the only new free site; the swap window is exactly where an owner-side cleanup would miss an element | no direct-call chain from `0x84D7E0` to `0x939420` within 8 hops (only through its unresolved vcalls); the handler's class and its cleanup path are not identified | **inferred** |
| A | **Zombie window**: between the destroy body and the deferred destructor (up to 170 ms, several sim ticks) the object is out of the hash and its owner already notified, but its components live and tick; a new element pointing at it is created then, and the destructor (relying on the owner-side cleanup that already ran) does not remove it | the window is new (vanilla's extra holders release within one call); 170 ms age seen in this run | needs a subsystem that registers references to an object after its owner callback; not shown | **inferred** |
| V | vanilla bug unrelated to the park | - | first `0x84EAC3` among the crash reports kept; crash rule: ours until proven | not supported |

What tells them apart: whether the faulting handler is (inside) an object our flush released, and from which flush site. 0.36.19 logs that (below); B is also removed by the fix, so a recurrence with a ring hit at a poll site = A.

### 11.4 Fix and probe (0.36.19, counters unchanged)

- `rtsites.cpp`: the `RT_DESTROY` entry releases parked references **only while the world closes** (`rtClosing`: `ObjectService_DestroyAll` and the shutdown destroys after `RT_CLOSING`). Remaining release points: START / READY / CANCEL polls (yielding script tasks, the IC points from which vanilla's script `Destroy()` frees), `RT_SAVE`, `RT_CLOSING`, `RT_TEARDOWN`. Removes candidate B; the graveyard still empties at the READY poll of the job it parked under (a full graveyard keeps the wait, as before). Applies to P5c's FreeLock windows too (same graveyard).
- `p5c.cpp`: ring of the last 64 releases (object, vtable, count before, flush caller, time); `XcptLog` matches an access violation's registers against it (nearest released object within 0x400 bytes) and logs `P5c: fault register ... released N releases ago by the flush from ...`. A hit with the fix in = candidate A (or a cause B did not cover); no hit = the faulting block was not one our flush released directly (a separately allocated component, or a vanilla free).
- Not done: naming the handler class (`[element+0x1C]` vtable) statically; a live probe of the element's constructor would name it.

| | |
|---|---|
| Tests (offline) | selftest `TestDestroyPark` §2 rewritten: after a job, a native-caller destroy entry releases nothing (3 parked objects stay, canaries intact), the next poll releases them oldest first, the ring names object C for a fault register at C+0x18 and nothing for a stack address; a destroy entry while closing still releases. `selftest ok`; `hook-sites --check` 527 addresses (no new site; 4 pre-existing not in docs/68) |
| Build | `game-copy/ts3native.asi.destroypark-fix`, ts3native 0.36.19 (this branch's 0.36.18 + fix), sha256 `6b1a0cdd37519be7f71fbddfaa26969acc254b7b211c239ee0747a1a755be516` |
| Verify | `tools/harness/queues/destroypark-fix-verify.json` (off-first + on-first as in clean-night-routing-extra.json, clean night). Fixed = 2 clean runs of the queue. Pass also `p5cAgeMaxUs` < 100 ms, `p5cFlushed` within 512 of `p5cParked`, `p5cCountLow` 0 |
| Effort | ~1.5 h analysis, ~20 min fix and tests, ~20 min docs; live 2 x ~25 min |

## 12. Crash J (2026-10-22 ~15:12 PHT): TS3W.exe gone ~10 s into every `[RouteJobPhases]` job (`asi.phases`, `asi.0324`)

- **Symptom**: all 6 jobs with `[RouteJobPhases] Enabled=1` (`route-phases-2026-10-22-*`, `cellmemo-probe-2026-10-22-*`, `routealloc-2026-10-22-*`) lost the process ~10 s after profiling began: no crash report, no xcpt file, `ts3native.log` ends in normal route gate lines. The same `asi.0324` with the switch off (tailunlock, dedupe) passed.
- **Cause [verified]**: `RjpPop` (routephases.cpp) matched the leave thunk's `at` against frame slots top-down with `slot < at` = unwound. 12 of the 15 timed entries end in `ret N`, so `at` = slot + N and the function's own frame looked unwound: the parent's frame was popped and its return address used, so the thunk returned into the parent's caller with the callee's esp. The first nested ret-N return in a worker job hit it; whatever ran next on the corrupted stack ended the process before our filter could report (exact last instruction not captured live [inferred]: wrong esp into the caller, /GS or SEH-chain failure). Offline repro: steerro-test `STEERRO_PHASES=1` on the old code faults at `0x625420` in K8's first plans; the selftest's ret-N pair fails its drop count.
- **Fix**: `RjpPop` searches from the outermost frame for the first `slot <= at` (loadprof's `LpLeave` rule; an outer frame's slot is above the callee's arguments). Branch-into scan of all 15 stolen prologues: none (I-150-2 closed). docs/150 §7 fix note, ledger §250.

| | |
|---|---|
| Tests (offline) | selftest `TestRouteJobPhases` with a `ret 4` site inside a `ret 8` site (old: assert); steerro-test `STEERRO_PHASES=1` seeds 1-3 x 3000 ok (11 of 15 sites, 2560 jobs per seed, `rjpDropped` 0; old: AV); `hook-sites --check`, `check-docs-code` ok |
| Build | `game-copy/ts3native.asi.0326` = master 53bdb56d + fix, sha256 `C13367080FED1473EB925AA7F7B85A8FEBD77B62279E6031A73A51BB4566B63C` |
| Verify | `route-phases.json`, `cellmemo-probe.json`, `routealloc.json` on 0326 (day, 5 min). Fixed = 2 clean runs with the switch on |

## 13. Crash K (2026-10-23 15:30:43 UTC): the sim thread's in-room A\* inside `IC_World_FindPlacesOnShoreline` without `+0x918` while a route job runs, `ip-boat-exit-plandrain-ab` on ts3native 0.36.36 (`asi.0342`)

Static only (2026-10-23): EA's report `xcpt <machine> 26-10-23 23.30.43.txt`, `probe-out/queue/overnight-1023-2026-10-23-ip-boat-exit-plandrain-ab-1454-ts3native.log`, capstone on the Steam 1.67 exe copy, `routegate.cpp`. The PLAN notes of 2026-10-23 call it "crash-F frames without P4" (§8 has the same frames). Ledger docs/51 §309.

| | Crash K |
|---|---|
| Report | ACCESS_VIOLATION reading `0x00000000` with EIP 0; call stack `0x0061E6E0`, `0x00633599`, `0x006343B0` (then two non-code words) [verified report] |
| Run | repro `ip-boat-exit-plandrain-loop` (IP, houseboat exits, placement batches, 3 foreign `ReplanFromPosition` plans), `[Discourage] PlanDrain` A/B; `route snapshot P4 off`, so worker jobs plan on the live P/Q and the live A\* pools `0x11CEF50` / `0x11CEF68` / `0x11CEF90` / `0x11CF448`; last job logged: `long job 236 ms, A10 foreign START, IC kind 3` [verified log] |
| Where | `0x61E6D9 mov eax,[edx+0x10]; call eax` in the in-room A\* `0x61E500` on the open list's front node (`Q+0x18`): the node's vtable slot is 0 [verified disassembly]; node reset or freed under the search by a second thread [inferred, as §8] |
| Caller chain | `IC_World_FindPlacesOnShoreline 0x78C440` (call at `0x78C54A`) -> `0xC75920` (call `0xC7630A`) -> `LotSearch_Entry 0x640E00` -> `0x633FA0` -> `InRoomPlan 0x632E40` -> in-room A\* `0x61E500` on `Q = [P+0xBC]` (`0x633579`), the game's only `cAStarSearch` [verified: E8 scan of every `0x640E00` caller (3 inside `Route_PlanLocked`, plus `0xC7630A`), `0xC75920`'s only caller `0x78C54A`, IC table reference; docs/62 `ic-safety.csv` row 1441: PROXY, sync, UNSAFE]. The boat exit reaching FindPlacesOnShoreline fits the repro [inferred: the upper frames are lost] |
| Why unlocked | no `[RouteGate]` site covered `0x78C440` / `0xC75920` / `0x640E00`. Only the leaves `0x63D2F0` (site 2) and `0x628A90` (site 12) lock or block job starts, each for its own call; between leaves and during `0x61E500` the sim held nothing and the worker could START a job on live P/Q [verified routegate.cpp `RgEnter` / `RgLeave`] |
| Our code on the path | none patches the faulting path; the race needs our async route jobs (any owner) on live P/Q [verified] |
| Verdict | ours: `[RouteAsync]` jobs and a sync IC on the same live planner, not PlanDrain [inferred] |
| v0.1.4 exposure | shipped config (`Kinds=0`, `Enforce=1`, P4 off): own-Sim `Route.Plan` jobs run on live P/Q and can overlap a shoreline IC, so v0.1.4 can crash here too, rarely; 2 of 706 logged runs ever show these frames. v0.1.5 candidates (`ForeignKinds=4`, `FallbackAsync`, `M1Retry`) put more and longer plans on the worker [inferred] |
| Fix (ts3native 0.36.37, counters v108) | gate site 16 (`kRgSites` index 15) at `0x78C440` (prologue `55 8B EC 83 E4 F0`, 6 bytes [verified]), coarse like `IC_World_GetConnectedRoomsImpl 0x7929F0`: an outermost non-worker entry during a job waits for `+0x918` through `LockAcquireHook` (which serves the job's host calls, docs/159) and holds it for the IC; with no job its frame keeps job starts out (`g_rgOffSimOpen`) until the IC returns. On with `[RouteGate] Enabled=1` like the other 15 sites (all or nothing), no new switch. Counters `rgUnlocked3` (+0x45C4) and `rgJob3` (+0x45C8). P4 D2 (rebuild writer window) stays on sites 13 and 14 only |
| Cost | the IC was seen in 76 of 629 logged runs (first-seen lines, count per run unknown); worst case one sim wait up to a job's length (`FbJobCapMs` 300) [inferred] |
| Deadlock check | same model as the existing coarse gates: the wait is `LockAcquireHook`, which pumps the job's host calls; the IC body never waits on the worker [inferred] |
| Tests (offline, not built yet) | selftest `TestRouteSites` (site 16 counts in v108, `rgJob3` during a job), `TestRouteEnforce` (site 16 waits for the job's hold and holds `+0x918`; no job = off-sim frame), the stub loop in `TestRouteGate`; `hook-sites --check` |
| Verify | build (`build.cmd out-crashh`), selftest ok, then repro `ip-boat-exit-defer-loop` 2 clean runs on the new build with release defaults; pass = no crash and `rgUnlocked_findPlacesOnShoreline` > 0 (the site is reached; `rgJob_findPlacesOnShoreline` > 0 also shows a wait happened), and `rgGateWaitMaxUs` not above the job cap. Fixed = 2 clean runs (crash-cause-before-fix rule) |
| Effort | ~1 h analysis + ~45 min fix; live: build ~10 min, 2 runs of ~13 min |
