# 67. Crash triage: the 2026-10-14 Swarm wild call and the 2026-10-15 ModelBuilder teardown

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
