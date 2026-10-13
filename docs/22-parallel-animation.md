# 22. Animation update: can per-actor pose evaluation run on worker threads?

Offload candidate 2 from docs/20 (§3 row 4, §4 rank 2): animation update `0x571B90` -> rig pose `0x940430`, 3.3% of sim-thread samples (~33 ms/s), called straight from `MonoScriptHost_Simulate`. Research only, 2026-10-13. The game and the probe were not touched.

**Method.** Ghidra headless, read-only, against the TS3 project (`DecompileTo.java` depth 2 on `0x571B90` and depth 2 on `0x567A50`; a range decompile of `0x564000-0x584000` (727 functions); caller trees, string and scalar xrefs (clip-event type hashes `0xC1E40001-0xC1E4000A`); vtable dumps at `0xFC1A60`, `0xFC1B3C` and `0xFCBA70`). Scripts and output stayed in the session scratchpad. Tags: **[verified]** = read in the decompile or xrefs, **[inferred]** = reasoning not yet confirmed by disassembly or measurement.

## 0. Short answer

- **No, not as a parallel-for over `0x940430`.** Per-actor pose evaluation is not a pure function. Inside the per-actor step the engine **fires animation clip events synchronously** (parent/unparent, sound, VFX, visibility, script, destroy prop). It also calls into the routing/locomotion library, posts the new transform to the sim-to-render buffer, and allocates new controllers that bump a **non-atomic global id counter**. Shared objects use **non-atomic refcounts** [verified].
- **No direct Mono call was found** on this path. Script clip events go into the SACS walker's native event queue, and C# drains that queue later [verified for the push, inferred for "no Mono call anywhere below"]. Mono is therefore not the blocker. The blockers are clip events, routing and refcounts.
- **The only slice that could run in parallel is the pose evaluation itself:** the blend of active tracks (`0x5833C0`) and the root-motion math that follows it. Running that slice on workers means splitting the 847-line `0x940430` into serial, parallel and serial phases. Because one 18 KB stack frame spans all three phases, the split is a C++ reimplementation of the function, not a hook.
- **Expected gain is small and unproven:** at most ~10-20 ms/s of the ~33, realistically less [inferred]. The profile's leaf samples in this family are tiny predicates (`0x582830`, `0x566010`, `0x56BC80`), which points at many cheap per-actor visits and virtual calls, not heavy pose math.
- **Recommendation: park it**, below the read-only query caches (docs/20 rank 3) and the cheaper water-search work. First measure the split cheaply (§6). If the eval slice turns out large, the safer lever is a serial one: skip pose *evaluation* for actors that are off-screen and not moving, while still running *advance* every tick so events keep firing.

## 1. Loop structure [verified]

`MonoScriptHost_Simulate` (`0xD81840`) runs two animation passes over the same actor list:

| Site | Pass | Per actor |
|---|---|---|
| `0xD81A70` | `0x571AD0` | SACS state-machine driver update `0x94E9C0` (if the actor has the SACS component `DAT_011E90F0`; this is where SACS pushes `Scripting/EventQueue/Event`s), then time step `0x942C00`, then `0x942C50`, which calls rig pose `0x940430` (in fixed sub-steps of `DAT_010434C4` while tracks are active) |
| `0xD81C73` | `0x571B90` | time step `0x942C00`, then rig pose `0x940430` if the actor has a rig (`+0x24` -> `+0xB0`) and `dt > 0`; a catch-up loop re-runs the pose in fixed sub-steps when an actor is still active (`0x93E190` -> `0x582830` "any track active") |

Both passes share the same shape:
- **Intrusive linked list** of actor entries (link at entry `+0x18`, entry = link − `0x18`). The cursor is stored **in the list object** (`param_1[4]`, i.e. `+0x10`) rather than in a local, and an "iterating" flag (`+0x28 |= 8`) is set during the walk. Together these let the current entry unlink itself or others during the walk. **The engine expects the per-actor step to change the list** (for example a destroy-prop clip event, §3).
- Per actor, the owning object's refcount is bumped with a `LOCK`-prefixed increment around the work. These are the only atomic refcounts on the path.
- After the walk:
  - `0x577060` -> `0x576FB0` walks a **global hash table** (`PTR_DAT_0114F694`/`DAT_0114F698`) of tracks marked "finished" (`+0x18`) or "removed" (`+0x19`) during the step, runs their callbacks (`0x576B30`/`0x576B70`) and erases them.
  - `0x571A50` drains a **deferred list** of object ids (`+0x18..+0x1C`): per id it looks up the object (`0x939560`), calls `0x941BC0` and `0x939420`.
- So EA already defers *some* work to after the loop: track completion and the deferred-id list. Clip events are not deferred.

## 2. What `0x940430` does per actor, in order [verified]

18 KB stack frame (`__alloca_probe`; locals down to `-0x46A9`). `this` = the actor's animation component: `+0x4` owner object, `+0x24` rig state (pose/transform floats `+0x0..+0xB8`), `+0x28` optional extra, `+0x2C` flags.

1. Clears the rig state's per-step root-motion fields. If no tracks are active (`0x5833A0` == 0 and flag bit 31 clear), increments an idle counter (`+0xB4`); after `_DAT_0117B7E8` idle steps it calls `0x940310` (puts the actor to rest, posts a message via `0x692F70`). Then it goes to the epilogue.
2. Optionally calls `0x959170(1)` on the owner's transform component (`DAT_011E9158`).
3. **Advance all tracks:** `0x583D00` = TrackManager update (scratch list `"Scratch/Animation/TrackManager/TracksToRemove"`, EASTL allocator `0x4E3F90`). For each track it calls `vtable+0x8` Advance(dt), then `+0x2C`/`+0x30` to collect "has root motion" / "changed" flags. Tracks that are not alive are copied to a scratch list and removed.
4. **Routing notify:** if the owner has the component `DAT_011E8FA8` with `+0x2C` set, calls `0x947590`, which goes into the routing/locomotion library (`0x836200`, `0x829A60`, `0x833560`).
5. Reads the transform (`0x7F1620`, `0x580430`), caches it in rig state `+0x0..+0x1C`.
6. **Pose evaluation** (when the advance reported changes): `0x54C720`, then `0x5833C0` = blend each track's `vtable+0xC` Evaluate into a pose buffer (`0x554EE0`, `0x554D10`), then:
   - the actor's rig-pose buffer comes from a **pool** `0x5748A0` ("Animation/RigPose", 0x19B30 bytes, freelist `_DAT_011CD8C4` under `EnterCriticalSection(0x011CD8D8)`) and is built by `0x574F50`;
   - the root bone delta is extracted (`0x54C570`) into rig state `+0x60..+0x7C`.
7. Root-motion math: quaternion slerp, `acos`/`sin`, `rsqrt` normalisation into rig state `+0x30..+0x5C`. There is a function-local static initialised lazily (`_DAT_011DC4A0` bit 0 guards `_DAT_011DC490`).
8. Checks `0x5826B0`, `0x5EB7C0`, `0x5EC9E0`, `0x55DA60`, then **publishes the new transform**: `0x959670` -> `0x958430` -> `0x692F70` -> `0x5934C0`/`0x593E60` (the sim-to-render message buffer, same family as the triple-buffer sync `0x593590`). Global handler chain `0x936C90` over `DAT_011DC2A8[]`.
9. Epilogue: if `+0x28` is set, calls `0x5813C0(dt)`. Clears the per-step rig fields.

## 3. Side effects inside the per-actor step, and whether each is thread-safe

| # | Effect | Where | Thread-safe? |
|---|---|---|---|
| 1 | **Clip events fired synchronously during Advance.** `ClipController` (primary vtable `0xFC1B48`, `+0x8` Advance = `0x568E10`, `+0x4` = `0x569070`) -> `0x5689A0` walks the clip's event list between old and new time -> `0x568710` calls every registered delegate's `vtable+0x10` OnEvent(event, id). Delegates are registered by `0x566170`/`0x566290` ("Animation/DelegateRegistration"). Other controllers (vtables `0xFC1E6C`, `0xFC21CC`) fire the same way via `0x56FA30` [verified] | step 3 | **No** |
| 1a | Listener **TrackManager** (vtable `0xFCBA70`, `+0x10` = `0x583C60`) -> `0x583AB0` switch: Parent/Unparent `0x582930` (allocates "Animation/ParentEvent/SceneEntityParentRecord", reparents scene entities, looks up *other* objects by id), Sound `0x583050` (audio component `0x9442D0`), Effect/StopEffect `0x5835C0` (VFX scene entity, "Animation/EffectEventHandler/SceneEntity"), Visibility `0x583120` (calls `IC_World_ObjectDetachAutoDestroyEffects`, posts messages) [verified] | step 3 | **No.** Changes shared scene state and other objects, in event order |
| 1b | Listener **SACS WalkerClipMonitor** (vtable `0x1020348`, `+0x10` = `0x805870`): Script event (`0xC1E40004`) allocates `Scripting/EventQueue/Event` (`0x7425A0`) and pushes it to the walker queue (`0x801E70` -> `0x801D90` -> `0x742710`, the ScriptCore `EventQueue` C# drains later). DestroyProp (`0xC1E40009`) destroys a prop object (`0x581810`, `0x581D80`). Slot `+0x0` `0x8055D0` and `0x805490` push "AnimationDoneEvent" events [verified] | step 3 | **No.** Unlocked vector push; object destruction changes the actor list (hence the cursor design in §1) |
| 2 | Non-atomic refcounts (`obj[1]++`, `--` then `vtable[0](1)` at zero) on controllers, clips and nodes in `0x567A50`, `0x566650`, `0x566840`, `0x5665F0`, `0x56BF10`, `0xCF3C70`, `0x55CFA0` [verified]. Some of these objects are shared between actors (clip resources from the resource cache) [inferred] | steps 3, 6 | **No** for any object reachable from two actors |
| 3 | New controllers during Advance: `0x567A50` builds a `HoldPoseController` (`0x56FE70`). Every controller constructor (`0x565A80`, 22 callers) does `id = _DAT_0114F590++` without a lock [verified] | step 3 | **No** (duplicate ids) |
| 4 | Track "finished/removed" marks: hash lookup in the global table `0x114F694` plus a byte write on the entry (`0x576CB0`, `0x576D00`, `0x576D40`) [verified] | step 3 | Probably yes (read-only lookup, per-entry byte) if nothing inserts concurrently [inferred] |
| 5 | Routing/locomotion notify `0x947590` -> `0x829A60`/`0x833560` [verified] | step 4 | **No.** The routing library has unlocked shared state (PLAN, "Async route plan PAUSED") |
| 6 | Track Evaluate `0x566070` writes the global `_DAT_011CD7D8` (no direct reader found by xref, so likely debug state) [verified write, inferred unused] | step 6 | Racy, harmless if truly unused; make it thread-local to be safe |
| 7 | RigPose pool `0x5748A0` (critical section) and EA allocator `0x4E3F90` (per-heap lock) [verified] | steps 3, 6 | Yes (adds lock contention, like docs/20 row 2) |
| 8 | Lazy static `_DAT_011DC4A0` (constant 1.0 / 0.0) [verified] | step 7 | Benign race; pre-initialise to remove it |
| 9 | Transform publish `0x959670` -> `0x692F70` -> render message buffer `0x593E60`, global handler chain `0x936C90` [verified call chain; locking of `0x593E60` not checked] | step 8 | **Assume no.** Also order-dependent: a prop parented to a Sim's slot needs the parent's new transform [inferred] |
| 10 | Pure math on the actor's own rig state (`+0x0..+0xB8`) and stack locals [verified] | steps 6-7 | **Yes** |
| 11 | Mono / script callbacks | none found on this path; script events are queued (1b) [verified for 1b; no `mono_*` callee seen in the depth-2 trees, inferred beyond that] | n/a |

## 4. Verdict

- A parallel-for that runs `0x940430` per actor on workers, with the sim thread joining afterwards, is **unsafe**. Rows 1, 1a, 1b, 2, 3, 5 and 9 all run inside it. Deferring only the event emission (hooking `0x568710` to queue `(listener, event, id)` per actor and replaying after the join) would leave rows 2, 3, 5 and 9. It would also reorder events relative to the routing notify and the transform publish, which the parent/visibility handlers may rely on [inferred].
- **The workable design is a phase split:**
  - A, serial: steps 1-5 (advance with events, routing notify);
  - B, parallel: step 6-7 math per actor (`0x5833C0` blend + root-motion math), with the RigPose pool and allocator as the only shared calls;
  - C, serial in list order: step 8-9 (publish, `0x5813C0`), then EA's own post-loop work.
- Phase B still runs each track's `vtable+0xC` Evaluate. Those must be checked one by one (`0x566070` already writes a global, row 6) and must not touch refcounts on shared clips. The evaluate side of `0x5833C0` takes no refcounts in the decompile, but the per-controller Evaluate vtables were not all read.
- **Gain** [inferred]:
  - The whole family is ~33 ms/s, and Phase B is only part of it.
  - Phase A must stay serial, and every active actor pays for it.
  - The idle path (step 1) and the list walk are serial too.
  - If B is half the family, 8 workers save at most ~14 ms/s, minus fork/join (two passes per tick, so ~2 joins per tick) and pool/allocator lock contention.
  - Best realistic guess: **5-15 ms/s**, and only on lots with many animating Sims.
- **Code size:**
  - a C++ reimplementation of `0x940430` (847 decompiled lines, SIMD math that must match bit for bit) split into A/B/C: ~700-1,000 lines;
  - a worker pool with fork/join on the sim thread and a per-actor phase buffer: ~200 lines;
  - plus a hook on both passes (`0x571AD0` and `0x571B90`, and the sub-step loop in `0x942C50`), with a runtime switch for A/B.
  - Effort **L-XL**.
- **Risks (high):**
  - silent pose divergence from a math mismatch;
  - hidden shared state in per-controller Evaluate vtables;
  - non-atomic refcounts on shared clips (heap corruption, the same failure class as the async route crash);
  - parent/child transform order;
  - S3SS or other mods that hook the same functions.

## 5. Cheaper alternatives (serial, lower risk)

1. **Skip Evaluate for actors that need no pose** [inferred]: off-screen, not moving, no root-motion track. Keep Advance every tick so clip events, track completion and SACS timing are unchanged. Root motion (rig state `+0x60..+0x7C`, extracted in step 6) feeds movement, so actors with a root-motion track or a routing component must still evaluate. This is a serial patch around step 6 with the same ceiling as the parallel version and none of the threading risk.
2. Make the idle path cheaper. Resting actors still pay two list walks, two `0x942C00` calls, `0x93E190`/`0x582830` and the `0x940430` prologue every tick. Only measurement (§6) can say whether this matters.

## 6. What to measure before any build

Probe-only, in a test run (not done here):
- per-tick count of actors visited per pass;
- time split of `0x940430` into Advance (`0x583D00`), Evaluate (`0x5833C0`) and publish (`0x959670`), for example with Frida enter/leave timing on those three functions;
- the share of actors that reach step 6 at all;
- clip events fired per second through `0x568710`.

If Evaluate is under ~1% of sim-thread time, drop the idea. If it is above ~2%, prototype alternative 5.1 first.
