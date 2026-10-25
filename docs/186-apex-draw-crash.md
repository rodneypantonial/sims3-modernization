# 186. Apex Radiance draw-pass crashes during a lot-streaming camera pan (2026-10-24)

Static triage only (2026-10-24). No game or queue run was made for this doc; a separate live "Apex alone" run (ts3native parked) was going on at the same time, and its result is not in here. Sources:

- EA's reports `xcpt <machine> 26-10-24 18.35.02.txt`, `18.37.37`, `18.40.01`, `18.45.49` and `19.26.31` (registers, printed call stack, 512-528 bytes of stack data, module list)
- capstone on the Steam 1.67 exe copy; Ghidra headless (`DecompileTo.java`, `XrefsTo.java`) on the read-only TS3 project
- the Apex Radiance v2.15.3 binary that was tested (`ApexRadiance.asi`, loaded at `0x68490000` in all five reports), disassembled at the frame address. The Apex source is their public repo at HEAD 2026-10-23, which is also v2.15.3 (`apex_version.h`)
- our ts3native log of the 19.26 run (`probe-out/apex/triage-B-noprof-ts3native.log`), the runner summaries in `probe-out/apex/`, `tools/hook-sites.ts`, docs/68, `S3SS.toml`, `ApexRadiance.toml`

Tags: **[verified]** = read in the disassembly, a report, a log or source; **[inferred]** = reasoning, still to confirm.

## Summary

- **What crashes:** the main-thread draw of render-set entries in `Renderer_DrawMainRenderSets 0x6F0DB0`. In all five crashes the game's draw code reads or writes through a NULL or garbage sub-object of the entry it draws (model resource, shader binding, effect instance). It is not one bad function: there are four fault sites in three draw paths [verified].
- **Apex's frame:** `ApexRadiance.asi+0x292BE4` is the return address inside `RigTracker::ModelDrawHook` (`features/rig_tracker.cpp`), a detour of the SceneModel part draw `0x6F6250` that only saves and clears its own rig global, raises a depth counter, stores the thread id, calls the game's function with the same `this` and arguments, then restores. It passes no Apex-owned or cached pointer into the draw [verified: binary matches the source byte for byte]. It is live in 2 of 5 crashes (18.35, 19.26). In the other three the same word at stack `0x1AAE0C` is residue from an earlier model draw at the same depth [verified: frame layout]. So Apex's frame shows only that a model was being drawn; it is not the cause by itself.
- **Ours:** no feature that ts3native 0.36.37's release ini turns on has a hook in the renderer, the scene graph, the effects system or the material code, and none frees or parks a render object. Each feature's verdict is in §3 [verified for the code paths, inferred for timing effects].
- **Verdict: Apex side or a game race that Apex's camera-motion features expose, not ours [inferred].** The pattern (lot streaming plus a camera pan, Apex on, several different NULL sub-objects of drawn objects) matches Apex's default-on features that change the order of scene and lot work only while the camera moves: Spread New Objects Over Frames (`SceneNodeBudget`, which defers the scene's pending-node drain) and Lot Lighting While Moving (`LotLightingMotion`, which also cuts a streaming lot's build slice to 6 ms). Neither is proven to be the cause. The open "Apex alone" run and two one-switch runs (§5) decide it.

## 1. The five crashes

All five run on the main thread (stack around `0x1AAxxx`, the frame loop `0x50B2F2` -> `0x50EDAE` -> `0x7EBBDD` -> `0x6F2052` -> `Renderer_DrawMainRenderSets 0x6F0DB0`). Free address space was 1152-1395 MB, so this is not memory exhaustion [verified reports].

| Report | Fault | Faulting function, field | Drawn through | Apex frame live |
|---|---|---|---|---|
| 18.35.02 (ts3native 0.36.8, P5 ini) | read `0xE0` at `0x68EEF0` | `0x68EEF0` returns bit 5 of `[this+0xC0]`; `this = 0x20` (a NULL base plus `0x20`), loaded from the local `[esp+0x20]` at `0x6F6B57` in the model part draw `0x6F6250` | draw-list virtual call `0x6EE872` (`call [vtbl+0x18]` in `0x6EE5E0`) -> Apex hook -> `0x6F6250` | yes: `ebp 0x1AAE08` = `0x6F6250`'s frame, `[ebp+4]` = asi+`0x292BE4` |
| 18.37.37 (0.36.8) | read `0x44` at `0x6C93D0` | getter of `[this+0x44]`, `this = 0`: at `0x6FA2CA` the object draw `0x6FA0B0` asks `0x6DB810` for the resource behind its member `+0x210` and gets 0, then reads it at `0x6FA2DB` | `0x6EE872` -> `0x6FA0B0` (another drawable's slot `+0x18`; it calls `0x6F6250` itself later) | no: `[esp] = 0x6FA2E0` and `ebp 0x1AAE20` belong to `0x6FA0B0`; asi word at `0x1AAE0C` is residue |
| 18.40.01 (0.36.8) | write `0x2AC` at `0x71D56E` | Swarm effects list draw `0x71D440` (one caller, `0x71D677`): render entry `edi` (0x50 bytes, array `[this+0x3110]`), its effect instance `[edi+4] = 0`, store of `[edi+0x14]` into instance `+0x2AC` | `0x6F0E73` -> `0x71D440` (no draw-list virtual call) | no (residue). Frida agent also in the process (stack residue) |
| 18.45.49 (0.36.8) | same as 18.40, `0x71D56E` | same | same | no (residue) |
| 19.26.31 (0.36.37 release ini) | read `0xF` at `0x68FDB4` | shader parameter binding update `0x68FD80`: `0x68E8F0` looks up key `[ctx+0x30]` in the binding's table `[this+0x50]` / count `[this+0x54]`; with key 0 it returns the table pointer, here `0xFFFFFFFF`, then reads `+0x10`. The binding object (`this = 0x17001AC8`, from the effect's list `+0x40` walked by `0x6DAC50`) holds garbage: freed or overwritten | Apex hook -> `0x6F6250` -> material draw `0x6F4D40` (vtable slot at `0xFF988C`, "pickdefault"/"shadererror" fallbacks) -> `0x6DAC50` (effect state 2/3) -> `0x68FD80` | yes: asi+`0x292BE4` live under `0x6F4DA7` |

Notes [verified unless marked]:

- `0x6DAC50` uses `ebp` as a general register, so EA's printed stack for 19.26 is incomplete; the frames above are from the call sites.
- The 19.26 stack data below the live frames holds return addresses of the lighting code (`0x6A067B`, `0x6A4C5D`, `0x6A6583`, `0x6A79BF`, `0x6AD3FD`, `0x6B4349`, `0x6B9129`, `0xADCB42`): residue of the lot and room light update that ran earlier in the same frame on this thread. This fits the "within 0.5 s of Apex lamp log lines" note, and shows nothing more.
- 18.28.54 (same day, not part of this set) is another thread (`esp 0x4C92FDF4`), `0x5EE23C`, no Apex word: not triaged here.
- The fault sites are all game code; none is in Apex or ts3native.

## 2. Apex's frame `asi+0x292BE4`

The function at `asi+0x292BB0` (relocated `0x68722BB0`), from the binary: `inc [depth]`, read `fs:[0x24]`, save `[rig]` and set it to 0, store the thread id, call `[oModelDraw]` with `ecx` and the two stack arguments unchanged, `dec [depth]`, put `[rig]` back, `ret 8` [verified]. That is `RigTracker::ModelDrawHook` in `features/rig_tracker.cpp` exactly (`g_rig`, `g_depth`, `g_drawThread`, `oModelDraw`), a detour of `FUN_006f6250` (vtable `0xFF97F8` slot `+0x18` = `0xFF9810`; direct callers `0x6F847E`, `0x6FA33A`, `0x6FA3B3`) [verified: xrefs]. Its companion `BinderThunk` (call site `0x6F68C5`) records the rig pointer the game binds; `CurrentRig()` / `CurrentMode()` give it to Apex's lamp code during the draw, and `ReadMode` reads it under `__try` [verified source].

So, for question 1 of the task: **Apex does not call into the draw path with objects it keeps.** The hook neither keeps nor passes a light or lot pointer; the objects that fault are the game's own (entry -> model / material / effect). Apex's lamp code reads the rig the game itself just bound, inside the same draw. Whether Apex keeps cached light or lot pointers elsewhere (level light share, lot light bridge, room light queue) that could go stale was not followed: none of those functions is on any of the five stacks [verified stacks], and the faulting objects are not lights.

## 3. Can ts3native 0.36.37 (release ini) reach these objects?

Source: the 19.26 ts3native log lists every installed hook; `node tools/hook-sites.ts` lists every literal address in our source. Enabled hooks lie in routing (`0x61C0xx`-`0x6583E0`, `0x74D040`-`0x7929F0`, `0x829410`-`0x8361B0`), physics realm gate sites (`0x6819B0`-`0x686FD0`, docs/33: the routing physics world, not drawing), lock and host call (`0x4E16F0`, `0x4E17B0`, `0xD75FA0`), object destroy (`0x939420`), world lot index (`0xC68680`, `0xC688A0`), save (`0xAAC110` and 5 save-write call sites), the decompress call `0x4EC065`, the water JPS call `0x6207FB`, the GC immediates `0xE4A1D8`-`0xE4E168`, and the two transitions `0xEC1180`/`0xEC11B0`. Our only literals in `0x6E0000`-`0x720000` belong to `verify`, `jobqueue`, `steerro`, `rtoro` and `unseen`, all off in this ini [verified log + tool output].

| Feature (release ini) | What it touches | Can it free, destroy or delay-destroy what the renderer or Apex touch? | Tag |
|---|---|---|---|
| GC (`InitialHeapMB`, growth step) | three immediates in Mono's GC heap sizing | no native render object; it changes only when Mono collects. A finalizer that releases a native object runs at the game's own point, as without us | cannot reach (code) [verified]; timing only [inferred] |
| WaterJps | call `0x6207FB` in the route search's water grid, sim/worker thread | no object lifetime | cannot [verified] |
| Decompress `RefpackHook` | call `0x4EC065`: decodes RefPack into the game's buffer, any thread | writes the decoded bytes of every resource (models and materials too) and frees nothing. Wrong bytes could in theory corrupt a model, but the decoder is deterministic: a wrong decode would fail the same resource every time, not only during a pan with Apex; this run shows `fails 0`, earlier verify runs 0 mismatches. S3SS's `RefPackDecompressor` is on too | cannot free; corrupt data not supported [inferred] |
| SaveWrite | 5 save-write call sites (the write-phase timing site stood down for Apex `MemoryGuard`) | save path only; the job does not save | cannot [verified sites; no save inferred from the job steps] |
| RouteAsync, RouteGate (25 sites incl. `ObjectService_DestroyById 0x939420`, `Realm_AddShape`/`RemoveShape`/`LooseQuadTree_Reinsert`), Discourage (`DestroyApply`, `DeadSweep`), DeferFootprint | routing planner, routing world `+0x918`, physics realm shapes; sim and route worker threads | the gates wait, then run the game's own function; `0x939420` destroys exactly as the game does, only later on the sim thread (`DestroyPark=0`: no park in this ini; the log has no "destroy park" line). `DestroyApply` replays queued routing footprint removes before that destroy. A later destroy keeps an object alive longer; it never frees a render object early | cannot free early [verified]; a later destroy only shifts timing [inferred] |
| RefcountGuard | release calls `0x82F098` / `0x82F45C` in the routing obstacle query | keeps the reference until the query's caller returns: an object lives longer, not shorter | cannot free early [verified] |
| LotRemoveGuard | `LotRouting_RemoveObjectFootprints 0x635700` (lot-0 bound check) | routing footprints only | cannot [verified] |
| OptsInit | `RoutePlanOptions` constructor `0x652BF0` (two hub fields start at 0) | route options only | cannot [verified] |
| crash log (first-chance logger) | logs the first 16 exceptions | changes no state | cannot [verified] |

Evidence beyond the code: four of the five crashes ran the old 0.36.8 with the P5 ini and one ran 0.36.37 with the release ini, two different sets of routing features, with the same kind of fault on the same draw pass [verified reports + log]. No earlier crash in docs/67 or elsewhere in `docs/` faults at these four sites [verified grep]. The Apex first-chance AVs at `asi+0xDCD78` on the sim thread in our 19.26 log (11 logged; the logger keeps only the first 16 exceptions; Mono IC frames `0xD7D5B1`, `0xC8DC48`) were handled: the process ran on for minutes after them [verified log]. They are not the crash.

## 4. Which Apex features fit

What the five faults have in common: the main thread draws an entry whose model resource, shader binding or effect instance is NULL or garbage, during a camera pan over lots that stream in. The game draws what the cull and sort jobs put into the render sets (docs/46); an object whose scene node or lot data is half built or half torn down at draw time gives exactly these faults [inferred]. Apex features on by default in these runs (the 19.26 run is the runner's `file-list-cache-1-off` variant: `[patches.FileListCache] enabled = false`, the rest at defaults [inferred from the variant name and the toml the runner writes]) that change ordering only while the camera moves:

| Apex feature (default on) | What it changes while the camera moves | Fit |
|---|---|---|
| Spread New Objects Over Frames (`SceneNodeBudget`, `features/scene_budget.cpp`) | `Scene::BeginFrame`'s pending-node drain (`0x6EBC49` -> `0x6E4130`) stops after 512 nodes or 2 ms; the rest stays queued for later frames (up to 1.5 s). The drain runs each node's `vfunc+0x48` update and its bounds / spatial insert. Apex guards node lifetime (destructor, `AddNode`, holder teardown hooks) | strongest [inferred]: nodes stay in the render path with their update not yet run, which is a state the game never has after `BeginFrame` (it always drains everything). Their own notes say the guarantee for recorded nodes rests partly on an inferred claim (no other `SetOwner(0)` caller) |
| Lot Lighting While Moving (`LotLightingMotion`) | scales the lot lighting budget (`0xADB95D` -> `0xADB120`), and a streaming lot's build slice (`0xAEA680`) drops from 20 ms to 6 ms | possible [inferred]: lots stay half built over more frames while the camera passes over them |
| Wall Shading While Moving | wall AO step `0x68B810` returns early | weak: lighting data only, no draw object lifetime |
| Faster Game File Lookups / Remember Missing Files (`ResourceLookupCache`) | answers `ResourceMgr::FindProvider` from a table | weak: re-checked per answer; a wrong answer would give wrong data, not a NULL sub-object mid-draw. 18.37's NULL comes from a resource getter (`0x6DB810`), so it stays on the list |
| Rig tracker (on the stack) | none (pass-through, §2) | none |

Other modules present in every run: S3SS 1.6.3 (`Mimalloc`, `LotStreamingOptimizations`, `GCFinalizeThrottle`, `RefPackDecompressor` on) and S3PP [verified `S3SS.toml`, runner summary]. They are present in the "Apex alone" run too, so that run separates ts3native from Apex, not Apex from S3SS. Mimalloc turns a stale read into garbage or an unmapped page sooner than EA's heap would (docs/67 §5.3) [inferred].

## 5. Next steps

1. Read the running "Apex alone" result (ts3native parked, same job). A crash with the same signature = Apex side confirmed (ours ruled out by test, not only by code); no crash in 2-3 loads = interaction, then rerun with ts3native and Apex `SceneNodeBudget` off.
2. One-switch runs with ts3native parked, the same job and save, 2 loads each: Apex `SceneNodeBudget` off; then `LotLightingMotion` off. Five crashes in six loads so far means 2 clean loads per arm already say a lot (if the rate holds, 2 clean loads by chance has a probability of about 3%).
3. If 1 confirms Apex: send the issue below. Effort: reading the result ~10 min; each arm 2 loads x ~8 min plus restarts (~25 min); issue text is ready.

## 6. Draft issue for the Apex Radiance repo (not sent)

Title: Main-thread draw crashes (NULL model / shader binding / effect instance) during a camera pan over streaming lots, v2.15.3

Body:

> **Build:** Apex Radiance 2.15.3 (the release asi), TS3 Steam 1.67.2, S3SS 1.6.3 (Mimalloc, LotStreamingOptimizations on), S3PP. Apex settings at defaults except `[patches.FileListCache] enabled = false` in one of the runs.
>
> **Repro:** load a town save, then pan the camera over 8 lots (15 passes of 40 s) while they stream in. 5 crashes in 6 loads, all ACCESS_VIOLATION on the main thread in `Renderer_DrawMainRenderSets 0x6F0DB0`, minutes into the pan (one run: about 4 minutes).
>
> **Fault sites (TS3W.exe):**
> - `0x68EEF0` reads `[this+0xC0]` with `this = 0x20`, called from the SceneModel part draw `0x6F6250` (`0x6F6B5B`); the stack has `ApexRadiance.asi+0x292BE4` (RigTracker `ModelDrawHook`) as `0x6F6250`'s caller.
> - `0x6C93D0` reads `[0+0x44]`, called at `0x6FA2DB` in `0x6FA0B0` after `0x6DB810` (resource of member `+0x210`) returned 0.
> - `0x71D56E` writes `[0+0x2AC]` (twice): Swarm effect list draw `0x71D440`, render entry's instance `[entry+4]` is 0.
> - `0x68FDB4` reads `[0xFFFFFFFF+0x10]`: `0x68FD80` on a shader binding whose table `[this+0x50]` holds `0xFFFFFFFF`, reached from `0x6F6250` -> material draw `0x6F4D40` -> `0x6DAC50`; the stack has `ApexRadiance.asi+0x292BE4` again.
>
> **What we checked:** the RigTracker frame is a pass-through (same `this`, same arguments) and is live in 2 of the 5 crashes; in the other 3 its address is only stack residue. Our own plugin hooks nothing in the renderer, scene graph, effects or materials. The faults look like objects drawn while their scene node or lot is half built or half torn down, which made us look at Spread New Objects Over Frames (the deferred pending-node drain while the camera moves) and Lot Lighting While Moving (the 6 ms lot build slice). We have not proven either one.
>
> **Questions:** can a node that `SceneNodeBudget` leaves queued be culled into a render set and drawn before its `vfunc+0x48` update runs, and is that state safe for models whose LOD or material changed? Does the shortened streaming-lot build slice leave lots drawable in between?
>
> We can run builds with single features turned off on the same save if that helps.

(Before sending: add the "Apex alone" result and the one-switch results from §5, and attach only the five fault lines above, not the EA reports: those carry the machine and user name.)

## 7. Ledger

Rows I-186-1-1 .. I-186-1-6 in docs/51 §322.

## 8. Crash-hunt build: `[CrashForensics] DrawGuards=1 OurEvents=1` (ts3native 0.36.44, counters v115)

Goal: name the culprit in the crashing run itself, instead of bisecting features. Built, selftests pass, not run in game. Build folder `native/ts3native/out-drawfx/` (`ts3native.asi` + `ts3native.ini` = the release ini `out-rel015/ts3native.ini` plus a `[CrashForensics]` section `Enabled=1 DrawGuards=1 OurEvents=1`; `Guards` and `HeapCheck` keep their default 1, so the docs/157 §11.7 guards at `0x5EE1E0` / `0x7B0A00` ride along).

### 8.1 Draw guards (check only)

Each guard sits right before the instruction that faulted, not on a function entry: Apex Detours the entry of `0x6F6250` and retargets the call `0x6F68C5` (docs/68, `apex-sites.csv` `0x6F6250` C0), so we use call sites inside the draw functions [verified capstone, Steam 1.67]:

| # | Site | What is checked | Crash it matches |
|---|---|---|---|
| 0 model | call `0x6F6B5B` -> `0x68EEF0` (retarget) | `this` (ecx) in `[0x10000, 0xFFFF0000)`; parent = `0x6F6250`'s model (ebx) | 18.35 (`this = 0x20`) |
| 1 resource | call `0x6FA2DB` -> `0x6C93D0` (retarget) | the resource `0x6DB810` returned for member `+0x210` (ecx); parent = `0x6FA0B0`'s object (edi) | 18.37 (`0`) |
| 2 effect | `0x71D562` 9-byte `jmp` (the two moved instructions run in the stub) | the entry's effect instance `[edi+4]` (eax); parent = render entry (edi), list = esi | 18.40, 18.45 (`0`) |
| 3 binding | call `0x68FDA4` -> `0x68E8F0` (retarget) | the binding table `[edi+0x50]` (`>= 0xFFFF0000` or `1..0xFFFF` = bad) and count `[edi+0x54]` (`> 0x100000` = bad, an inferred bound); parent = binding (edi) | 19.26 (table `0xFFFFFFFF`) |

`0x68E8F0(key, table, count)` returns the table itself for key 0 with a non-zero count [verified disassembly], which is how 19.26 read `[0xFFFFFFFF+0x10]`. No entry check at the material draw `0x6F4D40`: guard 3 sits deeper on the same path and sees the binding itself.

Good path: 2-4 compares and a `jmp`, no counter, no store: ~1-2 ns per call [inferred]. With ~50k binding updates per frame at 30 fps that is <= ~2 ms/s on the main thread, likely much less [inferred, not measured]. Use it for crash hunting, not for timing runs (`tool-costs.json` `CrashForensicsDraw`).

### 8.2 Our-events ring

`crashev.h`: lock-free, 8192 entries {seq, a, x, y, thread, tag, job running, QPC}, ~30-60 ns per event, nothing logged per event. What goes in (a / x / y per tag are listed in `kCeTag`):

- `destroy-in` / `destroy-run` / `destroy-out`: `ObjectService_DestroyById 0x939420` through our `[RouteGate]` stub: object pointer (looked up on the sim thread with the game's `FindByIdAddRef 0x9395A0` and released at once, as `P5cPark` does), id, job wait in us; `destroy-out` when DestroyById returns (needs `Enforce=1`, a release frame bit `RG_REL_CE`)
- `rc-hold` / `rc-rel`: RefcountGuard keeping and giving back `+0x918` query releases
- `fp-queue` / `fp-apply`: design D / U / phase 2 / private remove records queued and applied (object pointer at defer time, id, kind, applied, job running)
- `disc-destroy`: Discourage DestroyApply before a destroy; `lotrm-flush`: LotRemoveGuard flushing a pending remove
- `decompress`: every RefpackHook output (destination, bytes, capacity); resource keys are not known at that call
- `save`: SaveWrite backup job start / end / move and the flush worker start / end
- `job-start` / `job-end`: the route worker's `+0x918` hold (handle, kind, lock; job us)
- `draw-bad`: a guard hit (never matched as an owner)

Not recorded: GC heap growth (`[GC]` only patches three immediates in Mono's heap sizing; no code of ours runs when it grows).

### 8.3 What the log shows

Install (start-up): `crash forensics: docs/186 §8: our-events ring on (8192 events), draw guards model on resource on effect on binding on`, plus `OK   crash forensics draw guard ...` per site. A `SKIP ...: call at 0x... is not a call to 0x...` means another module already patched that call; the other guards still install.

Guard hit (first 8 per site, before the game uses the pointer):

```
crash forensics: DRAW BAD at <site>: pointer 0x..., <parent> 0x... vtable 0x... (TS3W .rdata | NOT a TS3W vtable), thread T, ebp, esi; <parent> words: <16-20 dwords>
crash forensics:   member +0x210 words / effects list ... entry index N / context ... key ..., table count N   (site specific)
crash forensics:   pointer in P5 ring: ...        (docs/157 §11.7 ring: our P5 EA news / frees)
crash forensics:   pointer in ours: <tag> a 0x... (+off) x y, N ms ago, thread T, job running|idle, seq S | not in our-events ring
crash forensics:   <parent> in P5 ring: ... / <parent> in ours: ...
crash forensics:   stack code words: 0x6F6B60[+0x0] ... ApexRadiance.asi+0x292BE4[+0x..] ...
```

Then the game runs on and faults as before; the access violation then gives the docs/157 `DUMP` lines and, with `OurEvents=1`:

```
crash forensics: ours: <register|fault address> 0x...: <ring entry that covers it>      (only registers inside a ring span)
crash forensics: ours: route job RUNNING|idle (handle, lock), draw guard hits a/b/c/d, N events recorded
crash forensics: ours: <tag> (newest first: seq a x y thread job age-ms)   + up to 64 lines per tag
```

Reading it: the culprit is ours if the bad pointer or its parent lies in a `destroy-out` / `fp-apply` / `decompress` span written shortly before (ms), or if every guard hit comes with `job RUNNING` or right after a `job-end`. No ring match, parents with valid TS3W vtables, and Apex's `ModelDrawHook` among the stack code words point back to Apex or the game. Limits: object ids, lot ids and scene nodes are not decoded (the parent's raw words are logged for a later decode); a `destroy-*` span is a fixed 0x400 bytes [inferred object size]; stack code words include residue and data that happens to fall in `.text`.

Tests: `TestCrashForensicsDraw` (`crashfx-test.h`) runs every guard on dummies (bad and good pointers, the effect stub's moved stores, cdecl arguments), the ring's wraparound and spans, and the dump on a fake access violation. Effort so far ~1.5 h; one game run per load (same SV pan job as §1).

## 9. Round 2: who clears the Swarm entry, and `[CrashForensics] EffectLog=1` (ts3native 0.36.45, counters v116)

### 9.1 What round 1 caught

Log `probe-out/apex/drawfx-1-ts3native.log` (Apex 2.15.3 + release ini + §8 forensics, crash 2 min into the pan): `DRAW BAD` at the effect site, render entry `0x40F0F8F0` = index 87 of the entry array of the effects list `0x34DF3E80` (vtable `0xFFC820`), instance 0, then the write fault `0x2AC` at `0x71D56E` [verified log]. The entry's words read: +0x00..+0x17 all 0, +0x20/+0x24 = `AAA74050 4DED00F4`, the bounding box at +0x28..+0x3C intact, byte +0x4E = 0, +0x4F = 0x10 [verified log]. That is exactly what the remove `0x71FF10` leaves behind (§9.2): owner, instance, ids +8..+0x14 and the active byte cleared; the object id at +0x20 and the box kept [verified disassembly]. **The object id `0x4DED00F4AAA74050` is in our-events ring as `destroy-in` 26.0 ms before the crash** (seq 166696, sim thread, no job, together with its neighbour `...AAA74040`) [verified log]: the entry belonged to an effect attached to an object the sim thread had just destroyed. Neither the null pointer nor the entry is in the P5 or our-events rings; the route job was idle, P5 off.

### 9.2 The Swarm render list (SwarmRenderer, vtable `0xFFC820`) [verified, capstone + Ghidra, Steam 1.67]

| Role | Function | Thread | What it does |
|---|---|---|---|
| add | `0x71FD20`, vtable slot `0xFFC850` (+0x30), thiscall(owner, desc*, int* index), ret 0xC | main (Swarm effects run on main, docs/44 §7) [inferred for every caller] | pops a free index from the vector `+0x3120`/`+0x3124` or grows the 0x50-byte entry array `+0x3110`/`+0x3114`; owner +0, instance +4 (model looked up by `0x71A640`), ids +8/+0xC (registered with the tracker `[+0x38B4]` slot +0xC), object id +0x20/+0x24 (from desc +0x24), +0x4E = 1, +0x4F = 0x10 |
| remove | `0x71FF10`, slot `0xFFC854` (+0x34), thiscall(index), ret 4 | whoever calls it | pushes the index on the free vector, unregisters the ids, releases the instance (vfunc +4), clears owner, instance and +0x4E. **The bucket lists are not touched.** Known caller: the Swarm model effect teardown `0x8F3630` (vtable `0x103EDCC` slot +8; removes its indexes `+0x220` and `+0x224`) |
| gather | `0x722190` = the renderer drawable's scene node update, vfunc +0x48 (slot `0xFFC290` of vtable `0xFFC248`) | the thread that drains the scene's pending nodes: Scene::BeginFrame on main | calls `0x7212E0`, which empties and rebuilds the per-bucket node lists `+0x38EC`..`+0x38F0` (12-byte circular list heads; node {next, prev, index}) from the entries with +0x4E set, owner and instance non-zero and a valid box; bucket count into `+0x3980`; then tail-jumps to MarkDirty `0x6FAC70`, so the drawable is queued again for the next drain |
| draw | `0x71D660`, slot `0xFFC260` (+0x18 of `0xFFC248`; the draw-list call `0x6EE872`) -> `0x71D440` | main, step 8 (docs/46) | walks one bucket list; a node index `>= [+0x3980]` names entry `index - [+0x3980]`, read by index with no check of +0x4E (`0x71D562`) |

No lock in any of the four [verified for their bodies: no `lock` prefix, `xchg` or call of the EA lock `0x4E16F0`/`0x4E17B0` in `0x71FD20`, `0x71FF10`, `0x7212E0`, `0x71D440`; callees not checked]. EA keeps it consistent by ordering, all on the main thread: script ICs post Swarm messages (`IC_Swarm_StopVisualEffect 0x776E40` builds message `0xB35DAF22` and posts it through `0x692F70` [verified]; start = `0x735DAC73`), the SwarmService handler `0x71DA00` (slot `0xFFC414`) applies them in the services stage (create `0xB390246D` builds a `Renderer/SwarmObject` scene node, vtable `0xFFC0D0`; destroy `0xE73A6138` -> `0x718840`) [verified], then BeginFrame's pending-node drain runs the drawable's +0x48 and rebuilds the lists, then the passes draw (docs/46 §4 order) [order verified in docs/46; that every remove happens in the services stage is inferred]. So in the game, the lists are rebuilt after every remove of the frame and before the draw.

**The handoff that can break:** the gather is a scene node update, so it runs only when the pending-node drain reaches the renderer's drawable. Apex Radiance's `SceneNodeBudget` (default on) replaces the BeginFrame drain call `0x6EBC49` with a budgeted copy while the camera moves: it stops after 512 nodes or 2 ms and leaves the rest queued for later frames (up to `maxDeferMs`, then the budget grows) [verified, Apex source `features/scene_budget.cpp`]. A frame in which the drawable is left queued draws with the lists of an earlier gather; any remove in between (the services stage of this frame) leaves a bucket node pointing at a cleared entry: `[edi+4] = 0`, the write `0x2AC` at `0x71D56E`. Lot streaming during a pan is exactly when the drain has thousands of new nodes. This fits round 1 (cleared entry, object destroyed 26 ms = about one frame earlier, main thread, no lock, Apex on, pan) [inferred: the log of §9.4 tests it]. Without Apex the drain always runs to the end in BeginFrame. A second, smaller path stays open: a Swarm effect's last reference dropped on another thread (refcounts are atomic; the destructor runs where the count hits 0) would call `0x71FF10` off main [inferred, not seen in the code read].

### 9.3 Apex and ts3native touch points

- Apex `SceneNodeBudget` redirects `0x6EBC49` (`apex-sites.csv` SceneDrainCall) and so decides when `0x722190` runs; the five other drains (`0x6DF9B5`, `0x6EDBC7`, `0x6EF07D`, `0x6F226B`, `0x6F3CF1`) still drain everything [verified Apex source]. Apex's frame profiler hooks the Swarm service update `0x71E640` (timing only). Apex's `RenderObjectMap` indexes the scene router map (`0x6FDFB0` find, `0x6FE1F0` insert, `0x6FDF40` erase), which the Swarm message handler uses for every message (`0x6FE3B0`) [verified: Apex header + `0x71DA00` decompile]; those three addresses are not in `apex-sites.csv` (the scan predates the feature or missed it) [verified grep]. A wrong answer there would apply a Swarm message to the wrong scene object; not seen, kept as a candidate. Apex patches none of the four slots or functions of §9.2 [verified source grep].
- ts3native release features: none calls a Swarm function or touches `0x71xxxx`-`0x722xxx` (docs/186 §3). The `[RouteGate]` destroy gate `0x939420` runs the game's own `DestroyById` on the sim thread, later than the game by the job wait (no job in round 1: 0 us); it does not change which thread or stage removes a Swarm entry: an object's effects are stopped by script and Swarm messages, applied on main [inferred]. What it can change is how many destroys (and so effect removes) fall into one frame; the log counts them. The gate's own `FindByIdAddRef 0x9395A0` + release on the sim thread is a balanced pair on the object, not on the effect.
- Round 1's crash rate (3 of 4 loads with ts3native, 0 of 4 Apex alone) is not explained by the mechanism; §9.4's counters per run (stale draws, removes per frame) compare the two.

### 9.4 `EffectLog=1` (probe only)

Four vtable slot swaps, each only while the slot still holds the game's function (`OK`/`SKIP crash forensics effect log ...` at start-up), record into a 4096-entry ring {list, index, entry, owner, instance, object id, thread, return address + next code word on the stack, gather number, QPC}:

- add (after the game's add: the index it chose), remove (before the clear: the entry as it was), gather (removes since the previous gather, time since it, draws since it, and who ran it: `0x6E41xx` = the game's drain, `ApexRadiance.asi+...` = Apex's budgeted copy, other TS3W = one of the other drains), draw: a counter only, unless removes happened since the last gather: then `fxStaleDraw` counts and the first such draw per gather walks the bucket lists for nodes whose entry is free (dangling) and logs (first 16):

```
crash forensics: Swarm draw on stale lists: N removes since the last gather (X ms ago, gather G), D dangling bucket nodes, thread T (main)
```

- the effect site's `DRAW BAD` (§8) now also prints the newest remove and add of that entry and the newest gather of its list:

```
crash forensics:   effect log: entry remove index 87 entry 0x... owner ... instance ... object 4DED00F4AAA74050, X ms ago, thread T (main|sim|route worker|other), gather G, from <caller> <next code word>
crash forensics:   effect log: newest gather list 0x..., removes since N, gap us U, draws D, X ms ago, thread T (main), gather G, from 0x6E41xx | ApexRadiance.asi+0x...
crash forensics:   effect log: removes since the last gather R, now gather G, stale draws S, removes off main M
```

- the crash dump adds one line with the hit counts of all four draw guard sites (every site, whatever faulted) and the effect part: counters, the newest 24 removes, 8 gathers, 8 stale draws, 8 adds. Every `DRAW BAD` beyond the first 8 per site now gets one short line (up to 256 per site), so several races show side by side.

Round 1 stays as it was (DrawGuards on all four sites, our-events ring, P5 ring and guards, dump); EffectLog only adds. Counters v116: `fxReady` (bits 1 add, 2 remove, 4 gather, 8 draw), `fxRecs`, `fxRem`, `fxRemOffMain`, `fxStaleDraw`, `fxGatherGapMaxMs` (longest gather gap with a draw in it). Cost: ~50-80 ns per add/remove/gather (QPC + a 48-word stack scan), one interlocked add per draw call; a few hundred events a second: < 0.1 ms/s on main [inferred, not measured]; crash hunting only (`tool-costs.json` `CrashForensicsEffect`).

**Reading the next log.** Mechanism confirmed if the crashing entry's remove came after the newest gather (`removes since` > 0, remove younger than the gather), `fxStaleDraw` > 0 with dangling nodes, and the gathers before the crash were run by `ApexRadiance.asi` with gaps over a frame (`fxGatherGapMaxMs` well above 33). Then Apex's fix is to treat the Swarm renderer drawable (or any node whose +0x48 rebuilds draw lists) as never deferred; the issue in §6 gets this paragraph. A remove on a non-main thread (`fxRemOffMain` > 0, thread sim/other) points to a refcount path instead (ours to look at if the thread is the sim and the caller sits under our destroy gate). Stale draws in a run without Apex would mean the game itself removes after the gather (then the next round logs the services / drain order per frame).

Build: `native/ts3native/out-fxlog/` (`ts3native.asi` 0.36.45 + `ts3native.ini` = `out-rel015/ts3native.ini` plus `[CrashForensics] Enabled=1 DrawGuards=1 OurEvents=1 EffectLog=1`). Tests: `TestCrashForensicsFx` (`crashfx-test.h`): a fake SwarmRenderer and fake add/remove/gather/draw behind the hooks (records, remove-since-gather, stale draw with one dangling node once per gather, free-slot remove not counted, remove on a second thread counted off main, slot swap match and mismatch, wraparound), round 1's effect and model guards firing with EffectLog on, the dump on a fake `0x71D56E` fault. Ledger §326. Effort ~2.5 h (static ~1.5 h, build ~1 h); one game run per load, same SV pan job.

## 10. Round 3: who empties the fur model's LOD slot, and `[CrashForensics] ObjResLog=1` (ts3native 0.36.46, counters v117)

### 10.1 What round 2 caught

Logs `probe-out/apex/fxlog-1-ts3native.log` (load 1, 10-min pan, no crash) and `fxlog-2-ts3native.log` (load 2, crash) [verified logs]:

- Load 2 crashed at the **resource** site, not the effect site of round 1: `DRAW BAD at object draw 0x6FA0B0`, object `0x3BC9F6C0` vtable `0x00FF9B50`, member `+0x210` words `386EA490 00000000 00000000 00000000`, then the read fault `0x44` at `0x6C93D0`. Neither pointer is in the P5 or our-events rings (802,201 events); route job idle; no Apex frame among the stack code words (`0x6FA2E0`, `0x6EE874`, `0x7050BD`, `0x702467`, `0x6F12F2`). Same site as report 18.37 (§1).
- Effect list: in both loads every gather came from `ApexRadiance.asi+0x294143`; load 2 had 103 stale-list draws (2-384 removes since the last gather, 17-40 ms after it, 31-152 dangling bucket nodes, all removes on main), longest gather gap with a draw in it 413 ms. Load 1 had the same kind of stale-list lines and no crash. So I-186-9-4's preconditions show up in both loads; that path did not crash load 2.

So at least two crash paths are live. Neither rules out the other, and both run through the same handoff (§10.3).

### 10.2 `ApexRadiance.asi+0x294143`

The gather `0x722190` is the Swarm drawable's scene node update (vfunc +0x48). The only Apex code that calls a node's +0x48 is `UpdateTail` in `features/scene_budget.cpp` (the `FnUpdate` call through `vtable + kUpdateSlot`), called by `BudgetedDrain` from `Hook_SceneDrain`, Apex's handler for the redirected BeginFrame drain call `0x6EBC49` [verified source]. `Hook_SceneDrain` takes that path only when the camera is moving (`LotLightingMotion::SampleCameraMoving()`), the scene is drained every frame, the backlog has waited less than 3 x `maxDeferMs` (500 ms default) and the development checks pass; otherwise it calls the game's drain (`next`), whose node calls return into `0x6E41xx` [verified source]. So `+0x294143` is the return address of the +0x48 call in `UpdateTail` (inlined or not), and "every gather from Apex" means the whole pan ran through the budgeted copy [inferred: the 2.15.3 binary is not on disk now to confirm the offset; Apex's frame profiler only times the drain, the game's node calls return into TS3W]. The copy stops after `nodesPerFrame` (512) nodes or `msPerFrame` (2 ms) and leaves the rest queued, whatever the node. Apex's design note (header of `scene_budget.cpp`) proves node lifetime safe while queued; it does not cover draw state that those node updates refresh for the same frame.

### 10.3 The object and its member `+0x210` [verified, capstone + Ghidra, Steam 1.67, unless marked]

- Vtable `0xFF9B50` = **SceneModelFur**: constructor `0x6FA020` (base SceneModel constructor `0x6F5B40`, base vtable `0xFF97F8`), allocated as `Renderer/SceneModelFur` (`0x700EE0`) or `UI/ThumbnailManager/SceneModelFur` (`0xD54D60`), 0x2E0 bytes. Its draw (slot +0x18) is `0x6FA0B0`: with the fur flag `+0x2DC` set and the fur pass bits of the draw context, it takes `0x6DB810` of `+0x210` and calls `0x6C93D0` on the result **without a NULL check**, then falls through to the base draw `0x6F6250`. The base draw takes `0x6DBA50` of `+0x210` and does check for 0 (`0x6F6323`).
- `+0x210` is the SceneModel's **model handle** (constructor `0x6DD580`, 0x74 bytes, destructor `0x6DD350`): five tagged resource slots `+0` model, `+4` pending model, `+8` current LOD (`0x6DB810` returns it when its low two bits are clear), `+0xC` loading LOD, `+0x10` override (low bits set = a key still loading; `[0x11D3C84]` vfunc +0x34 requests a load into a slot, +0x3C cancels it); `+0x1C` a version every release bumps; `+0x28..+0x34` the model key (`+0x30` non-zero once set); `+0x5E` a ready byte. The crash state (`+0` loaded, `+4..+0x10` empty) is exactly what the LOD unload leaves.
- **Who empties `+8`:**
  - `0x6DBCE0` LOD unload: clears `+0xC`, `+8`, `+0x10`, `+4`, bumps the version, clears `+0x5E`, keeps `+0`; with a key at `+0x30` it jumps to the full release instead. Only caller: `0x6F734E` in **UpdateLOD `0x6F7300`** (SceneModel vfunc +0x40; via `0x6F8530` / `0x6F85D0` in subclass vtables), the branch taken when it is asked to drop the model (second argument 0, flag bits `+0x29C` 1 and 2).
  - `0x6DBB50` full release (all five slots, model too): from SetModel `0x6DCC90` (call `0x6DCC93`; its callers `0x6F48A6`, `0x71ABD5`), reload `0x6DCD20` (`0x6DCD23`, `0x6DCD34`; reached from UpdateLOD's load request `0x6DCD90` at `0x6F7331` when a key is set, and from `0xB3200A`, `0xB371B4`), the handle destructor (`0x6DD353`). The material substitution passes `0x6DC150` / `0x6DC320` also rewrite a slot with what a substitution callback returns.
- **Who fills `+8`:** `0x6DC5B0` (LOD select and swap-in: `+8` = the loaded `+0xC`, then `0x6DC320`), called from the SceneModel **node update `0x6F7B40`** (vfunc +0x48, call `0x6F7B5D`), which then rebuilds the draw flags `+0x40` (clears `0x5A`, sets 0x40 / 2 / 0x10 from the handle's state) and records the version at `+0x2A0`. Other callers of `0x6DC5B0`: `0x6DCC17`, `0x6DCE89`, `0x6DD0DF`, `0x6F540D` (not hooked).
- **Threads:** none of this takes a lock. The node update runs in a pending-node drain (BeginFrame, main), the draw on main; UpdateLOD's virtual caller is not traced [inferred: main, the scene's LOD / visibility pass].
- **What keeps the stock game safe:** after an unload, UpdateLOD compares the handle version with `+0x2A0`, and since it changed, calls MarkDirty `0x6FAC70` (the node goes into its owner's pending list) [verified `0x6F7387`-`0x6F739E`]. The next BeginFrame drain runs `0x6F7B40` before the passes draw, so the draw flags drop (or `+8` is refilled) before the fur draw can read `+8` [inferred: that the draw list honours the `+0x40` flags; the fur draw itself does not check]. The reload path bumps the version the same way.

**The handoff that can break (same as §9.2):** with Apex's budgeted drain the node can stay queued for several frames (up to 3 x 500 ms) while the object is still drawn with the flags of its last node update; a fur model whose LODs were dropped or reloaded in that window draws with `+8 = 0`: read `0x44` at `0x6C93D0`. Both crash paths (Swarm gather `0x722190`, SceneModel node update `0x6F7B40`) are vfunc +0x48 node updates whose work the draw of the same frame depends on [inferred: §10.4 tests it]. Other paths stay open: SetModel / reload from game code (`0x6F48A6`, `0x71ABD5`, `0xB3200A`, `0xB371B4`) between the drain and the draw, and an unload whose node update did run while the draw list still held the object. None of our features calls these functions or touches SceneModel state (§3); RefpackHook decodes bytes before the resource exists (the handle stores only the finished resource pointer), `[GC]` patches Mono only.

### 10.4 `ObjResLog=1` (probe only)

Seven call-site retargets (`PatchCalls` byte-checks each `E8` target; `SKIP` if another module patched it): unload `0x6F734E`, load request `0x6F7331`, full release `0x6DCC93` / `0x6DCD23` / `0x6DCD34` / `0x6DD353`, node update `0x6F7B5D`. None is an Apex, S3SS or S3PP address (`hook-sites.ts --check`, docs/68). Each hook records, then calls the game's function unchanged:

- ring of 8192 entries {handle, `+8` before (and after), `+0`, version, key type, thread, caller + next code word, QPC}: unload and load request (caller = UpdateLOD's caller), full release (caller = which of the four sites), node update only when it changes `+8` (`lod-set`, caller = the drain that ran it: `0x6E41xx` the game's, `ApexRadiance.asi+...` Apex's budgeted copy);
- a 4096-slot table: the newest node update per handle (QPC, drain caller, `+8` left, version), written on every node update.

At the resource `DRAW BAD` (§8 site 1), after round 1's lines:

```
crash forensics:   model handle words: <8 dwords>; version V (object saw V'), key type K, ready byte R; object flags +0x40 F +0x44 G, +0x29C B, fur on 1; scene node QUEUED (its node update 0x6F7B40 has not run since MarkDirty) | not queued (link L/P), holder 0x..., last drain processed N nodes
crash forensics:   model handle log: newest node update X ms ago from 0x6E41xx | ApexRadiance.asi+0x..., left +8 0x..., version V
crash forensics:   model handle log: lod-unload | release | load-request | lod-set handle 0x... (object 0x...): +8 0x... [-> 0x...], +0 ..., version, key type, X ms ago, thread T (main|sim|other), seq, from <caller> <next code word>   (newest 8 of this handle)
```

The Swarm `stale lists` line (§9.4) now ends with `drawable node QUEUED (its gather is waiting for a drain) | not queued, holder 0x..., last drain processed N nodes` (`mhFxQueued` counts the queued ones), and the crash dump adds the model handle counters and the newest 12 events per kind. The handle words, flags and queue state print at the resource DRAW BAD even with `ObjResLog=0` (reads only). Rounds 1 and 2 stay as they were.

Counters v117: `mhReady` (bits 1 unload, 2 load request, 4 full release, 8 node update), `mhRecs`, `mhUnload`, `mhRelease`, `mhLodSet`, `mhFxQueued`. Cost: ~40-60 ns per SceneModel node update (QPC, two guarded reads, a table write), ~50-80 ns per unload / load / release / LOD swap; with an estimated 2-5k node updates per second while lots stream, ~0.1-0.3 ms/s on main [inferred, not measured]; crash hunting only (`tool-costs.json` `CrashForensicsObjRes`).

**Reading the next log.** Fur path confirmed if the resource DRAW BAD shows the node `QUEUED`, the handle's newest `lod-unload` or `release` younger than its newest node update, the drains before it from `ApexRadiance.asi`, and `last drain processed` at the budget (512 x grow). Swarm path confirmed if stale-list lines say `drawable node QUEUED` (`mhFxQueued` near the logged stale lines). Then one Apex fix covers both: never defer nodes whose +0x48 update refreshes draw state for the same frame (Swarm drawable vtable `0xFFC248`, SceneModel `0x6F7B40` and its subclasses), or run the full drain while such a node is in the backlog; the §6 issue draft gets this. Other outcomes: node `not queued` and the newest node update younger than the unload = the draw list kept an object whose flags say not ready (game side; next round reads the draw-list build `0x7050BD` / `0x702467`); a `release` from `0x6DCC98` / `0x6DCD28` on a non-main thread = a SetModel race; any record with our code words = ours.

Build: `native/ts3native/out-objres/` (`ts3native.asi` 0.36.46 + `ts3native.ini` = `out-rel015/ts3native.ini` plus `[CrashForensics] Enabled=1 DrawGuards=1 OurEvents=1 EffectLog=1 ObjResLog=1`). Tests: `TestCrashForensicsObjRes` (`crashfx-test.h`): fake SceneModel object and fake unload / load / release / node update behind the hooks (records with `+8` before and after, keyed unload counted as a release, the node update thunk's `[ebp+4]` caller and the update table, no ring entry for an unchanged `+8`), round 1's resource guard firing on the emptied handle with the node queued, round 2's stale Swarm draw with the drawable queued, the dump, wraparound; round 1 and 2 tests unchanged. Ledger §327. Effort ~2 h (static ~1 h, build ~1 h); one game run per load, same SV pan job.

**Sent 2026-10-24 22:10 PHT** as Apex issue #37 (https://github.com/loinyx/Sims3-ApexRadiance/issues/37): the Swarm stale-list mechanism with the round-3 effect log (objres load 3), rates with/without our plugin, suggestion and question about `+0x294143`. The other three draw-pass sites are not in it yet.
