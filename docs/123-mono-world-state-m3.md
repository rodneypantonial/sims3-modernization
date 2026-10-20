# 123. Mono M3: how the offline host gets a game world

Written 2026-10-20. Design only: no build, no game run. It answers the open M3 decision of docs/119 (§4.3 risk 1, §11.4): how `native/monohost` gets a managed world to run sim code on, with autonomy meta scoring (docs/118 §1, docs/122) as the first user. Inputs: docs/118 (plan, M3 = live record/replay, gate G3), docs/119 §8-§11 (what the bare host already does), docs/120, docs/121, docs/122 (pilot, Mode R), docs/27 (RNG), docs/59, docs/62 + `docs/data/ic-safety.csv`, `docs/data/mono-host-ics.csv`, docs/09 (save contents), docs/26 (persist internals), docs/66 (address-space budget), and the decompiled `ScriptCore` persistence classes (read only, nothing copied here).

Tags as in AGENTS.md: **[verified]** = seen in the binary, the decompiled class library or a measurement, for this note or in the cited doc; **[inferred]** = reasoning still to confirm (ledger docs/51 §154, I-123-*); **[design]** = a choice made here. Effort in CLAUDE.md units: wall-clock hours for Claude with agents; game runs listed apart. Mono stays parked behind routing (memory "Mono parked"); this is the decision, not a start.

## Summary

- **The problem.** Scoring reads managed objects directly (motives, traits, relationships, lot and venue fields, tuning, statics; docs/59 §2.2), not only through ICs. A bare host has none of them (docs/119 §3.2, §9.4 row 6). Whatever gives the host a world must give it *those objects*, plus answers for the ~38 PROXY ICs of meta scoring (docs/121).
- **(A) Record/replay of IC answers alone does not give a world:** it replays the native boundary, but the managed reads inside scoring never cross it. Useful as a thin trace (first differing IC), not as the world source.
- **(B) Loading a save in the host** is cheap for the raw object graph (the `OBJS` stream is read by managed code over ~15 `StreamHost` read ICs [verified: decompiled `LoadSaveManager`, `StreamHost`]) but not for a scoring-ready world: binding to native proxies and the post-load fix-ups (56 load/startup handlers in GameplaySystems [verified: grep count]) need a loaded native world. The state is also the save's, not a live cut, so there is no Mode R reference to compare against.
- **(C) A synthetic world** built by script APIs reaches CAS, genetics and object-creation ICs by the hundred and has no game reference at all. Fine for unit fixtures, not for M3.
- **(D) Hybrid (B) + (A)** (load the save, replay the IC answers the game gave during its own load of the same save) can work but costs ~40-70 h, and one property breaks every fresh-heap replay: **hash codes are addresses in this build** (`Type.GetHashCode` is the `MonoType` pointer [verified, docs/26 §9.1]; object hashes are address-based [inferred: no sync word, docs/119 §8 step 6]), so hash-container iteration order differs between game and host.
- **New option (E), recommended: a world image.** At a docs/122 cut, a helper suspends the game, copies its private memory and TS3W's writable sections to a file, and `monohost` restores them **at the same addresses** in a fresh process. The managed heap, Mono's own structures, the RNG state, statics, mod patches to managed code and the address-based hashes are then the game's, bit for bit. A scoring run in the host is equal to a sim-thread run on the same cut **by construction**, which is exactly the docs/122 Mode R premise, offline. Native IC answers come from the game's own read-only IC bodies running on the frozen copy, or from the docs/122 snapshot shims (both, compared, is a free test of the shims).
- **What (E) unlocks:** offline Mode R for the pilot (iterate shim and driver bugs without game runs: each image is a regression fixture), docs/118 G3 per captured cut, M4 GC stress on a real 350 MB heap, G-A2 (two threads) on real data, and offline inspection of the managed heap behind a crash. Not unlocked: running the sim forward in time (the engine is not running; one image = one instant).
- **Cost:** **~13-22 h** build, **2 capture jobs** (BP, IP; not timing runs: each capture stalls the game a few seconds), no clean night. Image ~1.8-2.1 GB raw per capture [inferred from docs/66 private commit], ~0.4-0.8 GB compressed.
- **Main risks:** address collisions in the host process (mitigation: restore into a child created suspended, before its loader allocates); locks owned by suspended game threads (reset list + watchdog); TLS indices; native code patched by mods. Fallback if the image gate fails twice: no offline world; Mode R stays in game only (docs/122 as designed). Nothing on the pilot's critical path depends on (E).

## 1. What a world has to provide for scoring

| Need | Where it lives | Source |
|---|---|---|
| Managed objects scoring reads directly: `Sim`, `SimDescription` (traits, motives, skills), `Relationship`, `Household`, `Lot` and venue fields, `MetaAutonomyTuning`, `InteractionTuning`, managers and statics | Boehm heap (352-360 MB in BP live, docs/66 §3.3 [verified there]) and managed statics | docs/59 §2.2 read set, docs/121 §4 |
| Answers of the ~38 PROXY ICs (object rows, lot geometry, per-lot lists, world constants, session caches, tooltip text) | native engine state | docs/121 §2, §8; docs/122 §2.2 |
| The script RNG at the right position | Mersenne Twister at `MonoScriptHost+0xC30` (624 words + index) | docs/27 §1.1 [verified there] |
| Runtime state the interpreter and class code reach: transformed methods, vtables, interned strings, binder map `0x119448C` (EA native bodies), lazy caches | Mono heap (eglib on EA's allocator, docs/118 D1) and TS3W `.data`/`.bss` | docs/118 §4.3, docs/119 §9.4 |
| The request and a reference answer | ts3fixes rooted statics (`AutShadow.sReq`, `sRef`, docs/122 §1.2) | docs/122 |

Determinism needs for any replay into a different process (they decide the options below):

| # | Source of divergence | Status |
|---|---|---|
| N1 | Script RNG (MT stream position; `Random_NextDoubleImpl`) | seed alone is not enough (draw count before the window varies, docs/27 §1.2); needs the MT state or recorded draws |
| N2 | `new System.Random()` seeded from `Environment.TickCount` (21 live-play sites, none per tick, docs/27 §2) | record `TickCount` or avoid those paths |
| N3 | Task scheduling order (which task resumes per tick, sleep-map ties) | needs recorded native-to-managed calls (`ITask.Simulate` resumptions) |
| N4 | GC timing: finalizers drained in `Simulate` (docs/119 §9.3), weak references (3 files use `WeakReference` [verified: grep]) | the host's collections never match the game's (conservative stack, other allocation pattern) |
| N5 | **Address-based hashes:** `Type.GetHashCode` = `MonoType` pointer (interpreted, not an IC: cannot be replayed through the IC boundary) [verified, docs/26 §9.1]; `Object.InternalGetHashCode` [inferred address-based, I-123-3]. Iteration order of `Hashtable`/`Dictionary` keyed by objects or types follows | any fresh heap diverges here |
| N6 | EA native method bodies (1,085 binder entries; 78 in the closure, docs/122 §12.1): a bare host's binder misses and runs IL instead (docs/119 §9.4) | equal only if each IL body equals its native body |
| N7 | Native state read by ICs while other threads move it (animation, locomotion, streaming; docs/121 §1 item 3) | needs a consistent instant |

## 2. Options

### 2.1 (A) Record/replay of IC answers

**What.** ts3native records every EA IC call (index into the 1,775-name table, argument digest, return value, out-params) and every native-to-managed call (method handle, receiver, args: task resumptions, `BindLoadedObject`, callbacks) around chosen entry points in a live session; the host registers replay stubs (the docs/119 §9.1 thunk table already knows each IC by index) and answers in order.

**Format [design].** One stream per session, framed per cut: header (tick, MT index, world epoch, build id); records `varint seq delta | u16 IC index | u8 kind | u64 arg digest | return bytes by kind (4/8/x87 float/double/struct size) | out-param bytes`; managed references as stable ids (`ObjectGuid` when the object has a proxy, else "new object #n" in creation order); native-to-managed records `seq | method token (MVID + token) | receiver id | arg digest`. zstd per frame.

**Size [inferred, I-123-4, I-123-5].** One meta search: ~200-2,000 IC calls x ~12-20 B = **~4-40 KB**; the sampled pilot rate (2.5-7.5 searches/s, docs/122 §2.3) gives **~0.6-18 MB/min**. The whole boundary (every IC on the sim thread, all callbacks): rate unmeasured, estimated 50k-300k calls/s, **~60-360 MB/min** raw.

**Determinism.** N1 (record draws), N2 (record `TickCount`), N3 (record resumptions), N6 (IL vs native) are manageable; **N4 and N5 are not**: a fresh heap gets other addresses and other collection points. Worse, the replay only covers what crosses the boundary: managed fields that scoring reads (motives, relationships) were never answers of any IC, so a replay with no managed world has nothing to read.

| Unlocks | Cost | Game runs | Risks | Verdict |
|---|---|---|---|---|
| Replay of code that builds all its managed inputs itself (rare in sim code); as a **trace**, the "first differing IC" diagnostic of G3 and G-A4 | full boundary: 25-45 h (per-signature marshalling of 1,775 ICs, callbacks, task order); trace only, on top of docs/122's guard thunks: **2-4 h** | 2-4 | N4/N5 divergence; ref-returning ICs need an identity map | **no as the world source; yes as a trace** inside the recommendation |

### 2.2 (B) Restore the managed heap from a save

**What the game does [verified: decompiled `ScriptCore.LoadSaveManager`, `ScriptObjectGroup`, `PersistedClass`].** Native world load calls managed `LoadSaveManager.BeginLoadObjects(stream, nativeReader)` (a bound method handle, docs/119 §1.2 step 7), which reads the save's script-state resource (`.nhd` type `0x06B981ED` "OBJS", 5.5-8.5 MB and 170k-300k records in our saves, docs/09 §6 [verified there]; `tools/harness/save-sources.ts` already parses it) with the managed `PersistReader`/`PersistedClass` over `FastStreamReader`, whose bytes come from the `StreamHost_Read*` ICs. Native objects inside the stream come back as handles through `LoadSaveManager_LoadNativeObject`. Native then calls `BindLoadedObject(proxy, index)` for every scripted native object, `LoadObjectStack` for saved task stacks, and the managed side runs `IPersistPostLoad` and the gameplay load sequence (`GameStates`, world-load-finished and startup handlers, per-object `OnLoad`/`OnStartup`). 203 `PersistableStatic` uses in 107 files [verified: grep] persist some statics; the rest are rebuilt by those handlers. `LoadSaveManager_LoadGame_Impl` itself is UNSAFE (docs/62).

**In the host.** The reader part is easy: the `OBJS` bytes from the save via `tools/dbpf.ts`, ~15 `StreamHost` read ICs answered from a byte buffer, `LoadNativeObject` answered with opaque handles (I-123-6). That yields the persisted object graph, unbound. The hard part is the rest: proxies need the native object list (ids, classes) of the world file; the fix-ups reach world ICs (lots, terrain, positions, routing, catalog) that only a loaded world answers (I-123-7).

| Unlocks | Cost | Game runs | Risks | Verdict |
|---|---|---|---|---|
| Raw graph: offline queries over households, relationships, SimDescriptions (a save inspector); with fix-ups: a world at load time | reader: 3-5 h; full load path: 30-60 h, IC surface unknown (hundreds) | 0 for the reader; 2-4 to learn the load surface | the state is the save's, not a cut: no Mode R reference; fix-up surface; N4/N5 on the fresh heap | **no** for M3 (keep the reader idea for save tooling) |

### 2.3 (C) Synthetic world built by managed code in the host

**What.** Create Sims, households and lots through script APIs (`SimDescription`/genetics, `Sim` instantiation, lot setup) with stubbed engine answers, or build objects without constructors and set fields by reflection.

| Unlocks | Cost | Game runs | Risks | Verdict |
|---|---|---|---|---|
| Hand-made fixtures for unit tests of single scoring functions and shims; no game data needed | 20-40 h for a few scenarios (CAS, outfit, genetics and object-creation ICs; every scenario is new code) | 0 | no game reference, so no bit-equal check and no Mode R; fixtures drift from real worlds; uninitialised objects miss invariants the real load sets | **no** (docs/122 T1-T4 already cover shim tests with synthetic rows) |

### 2.4 (D) Hybrid: (A) for native answers, (B) for managed objects

**What.** Load save S in the host as in (B); replay the native answers and native-to-managed calls the game produced while loading S (a load-phase record, (A)), so the game's own fix-ups run in the host; then replay the IC answers of the first sampled meta searches after load and compare with the game's scores.

| Unlocks | Cost | Game runs | Risks | Verdict |
|---|---|---|---|---|
| A post-load world built by the game's own code; in principle replay forward in time (more records) | 40-70 h (full-boundary recorder incl. load, both directions, identity map for native-created managed objects) | 4-8 | load records are large (load is IC-heavy; unmeasured); N4/N5 make iteration order and finalizer effects diverge, so "first difference" hunts never end; only states right after a load | **no**; fallback only if a time-evolving offline sim is ever wanted |

### 2.5 (E) World image: restore the game's memory at the same addresses [design]

**Why it works.** The host already maps TS3W.exe at its fixed base `0x400000` and runs its Mono (docs/119 §8). Everything the managed world consists of is ordinary memory of that process: the Boehm heap sections, the Mono structures on EA's allocator (eglib thunks, docs/118 D1), TS3W `.data`/`.bss` (icall hash, `mono_defaults`, binder map, GC globals), and the MonoScriptHost object (RNG at `+0xC30`). Restored at the same virtual addresses, all pointers stay valid, hashes stay the same (N5 gone), the binder map runs the same native bodies (N6 gone), the RNG is at the same position (N1 gone), and native engine objects are frozen at one instant (N7 gone). N2-N4 do not apply to a one-shot run from a fresh restore. Private memory is ~1.84-2.04 GB in BP live (docs/66 §2 [verified there]); 32-bit, so it fits a Large-Address-Aware host.

**Capture (game side).** At a docs/122 cut (return of `MonoScriptHost_Simulate` `0xD81840`) with a capture request pending:
1. The sim thread posts "capture" to a helper process (new small C tool next to monohost) and waits.
2. The helper suspends every other game thread, writes the image, and records thread contexts and TEB TLS slots of the sim thread.
3. With the others still suspended, the sim thread runs the **reference**: the scoring driver (docs/122's `AutShadow.ScoreMeta`, or its first slice if M3 runs before the pilot) on the request; results go to a side file `<image>.ref` with the IC trace of (A). Watchdog 2 s: if the reference blocks on a lock a suspended thread owns, the helper resumes the others and the capture is marked `loose` (its reference may then see moved native state).
4. Resume. Stall per capture: dump write of ~2 GB, est. 2-6 s [inferred, I-123-11]. Capture jobs are not timing runs.

**Image contents [design].** All `MEM_PRIVATE` committed regions; TS3W's image regions (writable sections, plus the dumped `.text` as an option, see risks); the image regions of our modules and other ASIs only if the TS3W `.text` patches jump into them; no `MEM_MAPPED` and no other `MEM_IMAGE` (system DLLs, d3d9/DXVK, drivers). Header: region table (VA, size, protection, owner), module list with bases and timestamps, the sim thread's context, stack range and TLS slot values, the TLS index values stored in `[0x11959DC]` (domain) and `[0x11F4160]` (interpreter context) (docs/118 D9), the exe hash, ts3native/ts3fixes build ids. Own format (header + raw regions, zstd per region) rather than a minidump: the restore needs exact owners and no OS-only streams [design].

**Restore (host side, `monohost image <file>`).**
1. Launcher creates a child `monohost` **suspended** (only ntdll and the exe are mapped then), allocates every image region in the child at its VA with `VirtualAllocEx` and writes it, then resumes the child. The child's own heap and stack land in what is left, so collisions can only be with the few regions present at creation (I-123-2). Same pattern as M1.1's reserve-then-free of `0x400000`-`0x125A000` (docs/119 §8), moved one level up.
2. The child maps TS3W.exe at `0x400000` (clean `.text` from `game-copy/monohost`, or the dumped `.text`), resolves imports itself (M1.1 code), then overwrites the writable sections with the dumped bytes.
3. TLS: claim the game's TLS indices in the child (set the bits in the PEB TLS bitmap or `TlsAlloc` up to them) and write the sim thread's slot values for the indices TS3W uses (I-123-10).
4. Locks: reset the known locks that a suspended thread may have owned at the dump (Mono loader lock `0x11F43A8`, the GeneralAllocator / S3PP mimalloc locks, any `CRITICAL_SECTION` whose owner is not the sim thread) to free; kernel handles inside them are dead in the child but unused while uncontended (single thread) (I-123-9).
5. GC: the root hook pushes the calling thread's stack (docs/118 D4); the child pushes the restored sim-thread stack range as well, so the game's roots at the cut are the child's roots. Collections stay off for scoring runs (one-shot, no need); M4 turns them on (I-123-8).
6. ICs: every EA IC entry gets the docs/122 guard thunk: **real body** for the read-only allow-list (docs/62 SAFE plus docs/121 class a), **snapshot shim** when testing the pilot's shims, **fail loud** for the rest. Writes from real bodies (door lazy cache, handle refcount, docs/121 §8.4) land in the child's private copy and vanish with it.
7. Run the driver on the request read from the restored `AutShadow.sReq`; compare with `<image>.ref` bit for bit; on a difference, compare the child's IC trace with the recorded one and name the first differing IC.

| Unlocks | Cost | Game runs | Risks | Verdict |
|---|---|---|---|---|
| **Offline Mode R** (pilot shim and driver bugs fixed against fixtures, not jobs); docs/118 **G3** per cut; **M4** GC stress on a real heap; **G-A2** with real data; offline managed-heap inspection behind a crash (crash-time image), e.g. which Sim's queue or which lot list is broken; real-body vs shim answers on the same instant (tests docs/122 §2.2 rows) | **13-22 h** (§4) | **2 capture jobs** (BP, IP), 0 clean nights | collisions, locks, TLS, patched `.text`, image size (§5) | **yes, recommended** |

## 3. Recommendation

**(E) world image, with (A) only as the per-run IC trace.** It is the one option where equality with the game holds by construction instead of by an ever-growing list of replayed inputs, and it is the only one that gives the pilot an offline Mode R. It costs a third of (D) and does not touch the pilot's critical path: if the image gate fails, the pilot runs Mode R in game as docs/122 already plans.

What changes in docs/118: M3 becomes "world image + reference per cut" instead of "record/replay over a 10-minute load"; G3 is redefined in §4.2. M4 runs on images. The trace format of §2.1 is what the docs/122 guard thunks write in Mode R anyway (no second recorder).

## 4. M3 steps and gates

### 4.1 Steps

| Step | What | Static or game | Effort |
|---|---|---|---|
| M3.0 | Static checks: the region classes of a live process (owner of each private region; which are Boehm sections via the GC heap-section table); TLS indices TS3W and Mono use; lock list (Mono loader lock, allocator locks, EA `CRITICAL_SECTION` globals); `tools/hook-sites.ts` patch sites of S3SS, S3PP and ts3native intersected with the native bodies of the IC allow-list (decides clean vs dumped `.text`) | static | 1-2 h |
| M3.1 | Capture: ts3native `[WorldImage]` switch (off by default) with a harness trigger; the helper tool (suspend, region writer, thread context, zstd); `<image>.ref` writer and the (A) trace from the docs/122 guard thunks or, before the pilot, from a minimal ts3fixes driver that is the first slice of `AutShadow.ScoreMeta` | build | 3-5 h |
| M3.2 | Restore: launcher + child (`monohost image`), region writer, TS3W mapping and section overwrite, TLS, lock reset, GC root push of the restored stack | build, offline | 5-8 h |
| M3.3 | Run and compare: guard thunks with the three IC policies, driver invoke through `0xE5A600`, bit compare, first-differing-IC report | build, offline | 2-4 h |
| M3.4 | Selftest checks in the monohost style (`M3-restore`, `-statics`, `-ref-equal`, `-trace-equal`, `-shim-vs-real`) and docs | offline | 1-2 h |
| M3.5 | Capture jobs: `bp-worldimage`, `ip-worldimage` (5 min each, 5 captures per job at sampled cuts, `aut-meta-burst` repro so meta searches fire) | game: 2 jobs | 1 h of work + 2 jobs |
| **Total** | | | **~13-22 h**, 2 game jobs, 0 clean nights |

### 4.2 Gates

| Gate | After | Pass when | Fail means |
|---|---|---|---|
| G3-0 | M3.0 | every region class has an owner rule; lock and TLS lists complete for the code the driver reaches | extend the lists before building |
| G3-1 | M3.2 | a restored image runs managed code: three statics read in the child equal the values ts3native logged at capture (`SimClock` ticks, the number of lots in `LotManager`, the MT index at `+0xC30`) | collision, TLS or lock gap: the log names the region, index or lock; two failed tries = stop (E), pilot keeps in-game Mode R |
| G3-2 | M3.3 | one cut: every (meta object, ad) score bit-equal with `<image>.ref`, and the IC trace equal | the first differing IC names the missing input (real-body allow-list, lock, patched `.text`) |
| **G3** (replaces docs/118 G3) | M3.5 | 10 cuts per world (BP, IP), all `strict` (not `loose`): 0 mismatches, real-body and shim answers equal where both run | fix, then recapture; `loose` captures do not count |
| G4 (docs/118, now on images) | M4 | forced collection every N allocations during the replay on an image: 0 mismatches, 0 faults | a root is missed: fix before M5 in game |

## 5. Risks of (E)

| # | Risk | Effect | Mitigation |
|---|---|---|---|
| 1 | A region collides with something the child has at creation (ntdll, PEB, the exe, the initial stack) | restore fails | restore before the child's loader runs (suspended child); the launcher's own exe at a high fixed base; collision list in the log (I-123-2) |
| 2 | A suspended thread owned a lock at the dump | deadlock in the reference (game) or the child | game: 2 s watchdog, `loose` mark; child: reset list from M3.0 plus a watchdog that names the lock address (I-123-9) |
| 3 | TS3W `.text` was patched by S3SS/S3PP/ts3native; a clean `.text` runs vanilla native bodies, a dumped one jumps into mod code | answer differs from the game's, or a jump into unmapped memory | M3.0 intersection; if empty, clean `.text`; else restore the dumped `.text` and those modules' images with their imports rebound (M1.1 import code) |
| 4 | Image size (~2 GB raw) and capture stall (2-6 s) | disk use, capture jobs perturbed | zstd; keep only the fixtures in use; captures only in capture jobs, never in timing runs (memory: clean night testing) |
| 5 | Native state of other threads mid-update at suspend | IC real bodies read a torn structure | suspend at the cut, where the sim thread owns the world; real bodies limited to the read-only allow-list; shim-vs-real compare flags it |
| 6 | Kernel objects (events, files, sockets) are dead in the child | a real IC body that waits or does I/O hangs or fails | such ICs are not on the real-body allow-list (docs/62 classes); fail loud |
| 7 | The game's roots at the cut include more than the sim-thread stack (other threads' stacks registered with the GC) | M4 frees a live object | push every restored thread stack the GC knows (I-120-1 says which threads are registered) |
| 8 | Images contain personal data (Sim and household names, Steam persona name) | leak if published | images and refs live in `game-copy/` (git-ignored); never committed (CLAUDE.md repository rule) |

## 6. Ledger and next step

Open claims: docs/51 §154, I-123-1 to I-123-12; M3.0 claims docs/51 §155, I-123-13 to I-123-22.

**Next (when Mono is unparked):** M3.0 is done (§7, G3-0 passed); M3.1 and M3.2 are built and proven offline on a synthetic image (§8); next the first real capture (§8.5), then G3-1 on it, then M3.3. M3 needs no part of M5; the reference driver is the docs/122 driver's first slice, so M3.1 and the pilot's S-steps share code.

## 7. M3.0 results (2026-10-20, static)

Static only: capstone over the Steam 1.67 `TS3W.exe` copy (`game-copy/bin`, PE sections, import table, every function of `game-copy/analysis/functions.csv`), the docs/68 address table, `docs/data/autonomy-proxy-ics.csv` (41 IC bodies with an address: the docs/121/122 union) and the sizes of docs/66. No game run, no capture; no region-level memory map exists in `probe-out` (only `memory.csv` totals), so sizes are docs/66's owner rows. Analysis scripts stayed in the session scratchpad (one-off).

**Static closure used below** [verified tool output]: direct `call`/out-of-function `jmp` from the 41 IC bodies, depth 12, Mono range `0xE20000`-`0xEC0000` treated as one block: **1,278 functions**, **660 indirect call sites not followed**. The 78 binder bodies of docs/122 §12.1 are not walked again (verified there: no engine state, corlib ICs only).

### 7.1 Region owners and the rule per class

PE layout [verified]: `.text` `0x401000` (`0xB93971`), `.rdata` `0xF95000` (`0x1B0389`, holds the IAT `0xF95000`-`0xF95AC0` and the TLS directory `0x11415DC`), `.data` `0x1146000` (`0xEE384`, ~0.93 MB incl. `.bss`), `.tls` `0x1235000` (9 bytes), `.rsrc` `0x1236000`.

| # | Class | Owner (docs/66 sizes, BP live start / after) | Rule | Why |
|---|---|---|---|---|
| R1 | TS3W `.text` | exe, 11.6 MB | **rebuild: hybrid** (§7.4) | clean bytes plus the dumped in-place patches; detours into mod modules reverted |
| R2 | TS3W `.data`/`.bss`, `.tls` | exe, ~0.93 MB | **restore**, then fix: 7 TLS index globals (§7.2), mod-module pointer slots (§7.4) | Mono globals, icall hash, binder map, GC globals, EA singletons |
| R3 | TS3W `.rdata`, `.rsrc` | exe | **rebuild from clean** (child resolves imports, M1.1) | read-only; S3SS IAT wait slots and our `[MonoThread]` vtable slot `0x108B6B0` live here and must *not* come back |
| R4 | other `MEM_IMAGE` | system DLLs, MSVCR80/MSVCP80, `d3dx9_31`, DXVK `d3d9` 7.5, NVIDIA ~111, `EAWebkit` 3.7, ASIs (S3SS 1.6, S3PP 0.6, ts3native 0.2, MonoPatcher 0.1), `wininet` proxy 2.9; ~412 MB | **skip**; the child loads its own copies of TS3W's imports, no ASI | pointers into them are reverted (code) or fail loud (data) |
| R5 | `MEM_MAPPED` | DXVK `MemoryFilePool` <= 100 MB, the exe's mapping at `0xEA4E9F`, NLS/fonts; 145-219 MB | **skip** | texture system copies and files; no allow-listed IC reads them [inferred: none of the 41 bodies reaches `MapViewOfFile`, 1 site in the exe] |
| R6 | Boehm GC heap | Mono, 352-360 MB | **restore** (required) | the managed world |
| R7 | EA GeneralAllocator cores (dlmalloc; S3PP cores 4 MB reserve / 1 MB commit) | ResourceCache contents ~359, Mono eglib structures, package indexes ~18.5, catalog ~8, STBL tables, CAS part cache, engine objects; size inside the ~940-1,135 MB unattributed rest | **restore** (required) | native IC answers and Mono's own structures; the allocator's code is TS3W `.text`, its state is in these cores and `.data`, so the child keeps allocating in the game's allocator |
| R8 | S3SS mimalloc arenas (every MSVCR80 `malloc` user) | 64 MiB per arena, 128 MiB from the 9th | **restore as data**; CRT `free`/`realloc`/`_aligned_free` of a restored block becomes a no-op / copy (§7.3 W2) | S3SS's mimalloc code is not in the child: a clean MSVCR80 `free` on a mimalloc block corrupts the child's CRT heap |
| R9 | ntdll process heap and other `HeapCreate` heaps | system, small | **restore as data**, never freed into (W2; the exe has 1 `HeapAlloc` site) | the child's own process heap is created by its loader after the restore, elsewhere |
| R10 | sim-thread stack | committed part only (~0.1-1 MB) | **restore** (required) | GC roots and frames at the cut; the reference request lives in rooted statics, not on this stack |
| R11 | other thread stacks | ~140 x 1 MB reserve, mostly reserved-only | **restore committed pages** (cheap), reserve only the rest | M4 needs the stacks of GC-registered threads (I-120-1); not needed by scoring runs |
| R12 | TEB, PEB, `KUSER_SHARED_DATA`, ApiSet, activation-context data | system | **skip**; record only the sim TEB's TLS slot values and stack bounds in the header | the child owns its own; a collision is logged (I-123-2) |
| R13 | DXVK / NVIDIA driver private memory | part of the unattributed rest | **restore** (cost only), trim later | no owner tag per region today; not reached by allow-listed bodies [inferred] |
| R14 | reserved-only ranges | Boehm reserve, mimalloc arena tails, EA core tails, stack reserves; 264-326 MB | **re-reserve** (`MEM_RESERVE`, no bytes) | allocators that grow in place (Boehm 32 MB steps, mimalloc, EA 4 MB cores) expect their reservation |
| R15 | kernel objects whose handle values sit in restored memory | events, mutexes, semaphores, files, thread handles (Boehm thread table, EA thread objects), EA `Lock` in mutex mode | **rebuild lazily / fail loud** through IAT wrappers (§7.3 W3) | a dead handle value can alias a live child handle with the same number |
| R16 | GPU state | D3D9/Vulkan device, textures, VRAM | **not restorable** | no allow-listed body reaches D3D [inferred]; fail loud |

Restore total: private ~1.84-2.04 GB minus stack and arena reserves (I-123-1 unchanged).

### 7.2 TLS

All `TlsAlloc` call sites in the exe [verified: 7 sites, 6 direct + 1 through a register]; every `TlsGetValue`/`TlsSetValue` site (31 / 19) uses one of these keys [verified]:

| Index global | Allocated in | Owner | Sim-thread value at a cut |
|---|---|---|---|
| `[0x114C908]` | `0x4E1200`, `0x4E1270` (same global, first caller wins) | EA job system: current-thread handle (docs/101 #7) | pointer to the sim thread's EA thread object (restored heap) |
| `[0x11F4160]` | `mono_interp_init` `0xE5A680` | interpreter `ThreadContext` | NULL outside a runtime invoke (`0xE5A600` clears it, docs/57 §4) [inferred at the `0xD81840` return] |
| `[0x11959DC]` | `0xE75BF0` | Mono current domain | root domain |
| `[0x1220F38]` | `mono_loader_init` `0xE84880` | loader error | NULL |
| `[0x11F43E0]`, `[0x11F43C4]` | `0xE99CB0` (two calls through `esi`) | marshal: P/Invoke last error and type-info marker (docs/120 §3) | as captured |
| `[0x1195B7C]` | `0xEBB180` | current `MonoThread` | NULL (docs/120) |

Static TLS: none in use. `.tls` is 9 zero bytes, the TLS directory's index variable `[0x1220AD0]` has no code reference and the callback array at `0xF97EC4` is empty [verified]. Mod TLS (ts3native `__declspec(thread)`, `[MonoThread]` P4 slots, `[Alloc]` caches, S3SS, S3PP, MSVCR80's own) is not reached once their detours are reverted (§7.4).

**Rule [design, replaces the bitmap claim of §2.5 step 3]:** the child resumes, its loader takes whatever indices it needs, then the launcher thread calls `TlsAlloc` 7 times, writes the 7 new indices over the restored globals above and `TlsSetValue`s the sim thread's captured slot values (read at capture from TEB `TlsSlots[i]`, or `TlsExpansionSlots` for `i >= 64`). No index has to match the game's (I-123-14).

### 7.3 Locks

**EA `Lock` [verified: disassembly `0x4E16F0`, `0x4E17B0`, init `0x4E17E0`].** `Lock_Acquire` is `thiscall(lock, const int* timeout)`, `ret 4`; the object holds a `CRITICAL_SECTION` (or, when byte `+0x24` is 0, a mutex handle) at `+0`, a recursion count at `+0x20` and the mode byte at `+0x24`. CS mode: `EnterCriticalSection` (infinite timeout) or a `TryEnterCriticalSection` + `Sleep(1)` loop up to the timeout (`-2` on timeout); mutex mode: `WaitForSingleObject` / `ReleaseMutex`. **798 call sites in 705 functions; 1,160 `Lock_Release` sites**; 20 global lock objects are passed as `mov ecx, imm` (101 sites), the rest are members.

Lock list for the code the driver reaches:

| Lock | Kind | Reached from the 41 IC bodies (min depth) | Source |
|---|---|---|---|
| EA allocator lock, `CRITICAL_SECTION*` at allocator `+0x4E4` (NULL = unlocked heap) | CS | `0x4E5920` Free (5), `0x4E6A20` Memalign (7), `0x4E69D0` Malloc (11), `0x4E4F90` (7), `0x4E5E40` (12) | [verified static] |
| EA `Lock` `0x11EB5B0` (free list `0x11EB5A0`, `0xAE00A0`) | EA Lock | yes | [verified static; docs/121] |
| EA `Lock` `0x11D3C98` (`GameUtils_IsWorldInstalled` lazy fills) | EA Lock | yes | [verified static; docs/62] |
| routing steering `+0x918` | EA Lock | `0x82F470` (10), `0x831710` (11) | [verified static; docs/81] |
| member `Lock` at `0x731F60` (`ecx = ebx`) | EA Lock | 5 | [verified static; owner not named] |
| member `Lock` at object `+0xA0` | EA Lock | `0xB971F0`, `0xB96380` (x2), `0xB99150` (11) | [verified static] |
| member `Lock` at `0xC68680` | EA Lock | 12 | [verified static] |
| member `CRITICAL_SECTION` at object `+0xF0` | CS | `0xAB52F0`, `0xAB5290` (11-12) | [verified static] |
| Mono: `0x11F43A8` loader lock (63 callers of `0xE85020`), `0x11F4130` runtime-method lookup, `0x11F4174` transform, `0x11F41F8` assembly, `0x11F4290` domain, `0x11F4380` image hash, `0x11F43E4` marshal, `0x11F4520`, `0x11F4574`, `0x11F45A0`, `0x11F4698`, `0x11F476C`, `0x11F4788`, `0x11F47B0` (threads.c); dormant `0x11F44EC`, `0x1220FE0`, `0x11F42D4`, `0x11F4314` | CS | interpreter (whole Mono range) | [verified, docs/56 and docs/120 census; globals initialised in `0xE5A680`, `0xE643A0`, `0xE66BC0`, `0xE75BF0`, `0xE82DD0`, `0xE84880`, `0xE99CB0`, `0xEA4DF0`] |
| Boehm `GC_write_cs` `0x1222580` (stop world, log), `GC_allocate_ml` `0x1234320` (never entered) | CS | collections only (off in scoring runs) | [verified, docs/57] |
| EA global CS `0x11CE588` (init `0x5C3A00`) | CS | not in the closure | [verified init site] |
| Managed monitors (`Monitor.Enter`/`Exit` binder bodies) | binder | 2 of the 78 bodies | single-threaded child: no wait possible [inferred] |

Exe-wide counts [verified import census]: `InitializeCriticalSection` 76 sites (13 with a global address, the rest heap members), `InitializeCriticalSectionAndSpinCount` 2, `EnterCriticalSection` 424, `TryEnterCriticalSection` 19, `CreateMutexA/W` 5, `CreateEventA/W` 9, `CreateSemaphoreA` 1.

**Completeness.** The 660 unresolved indirect sites and member locks reached only through them can add locks the table misses. So the reset is **not list-based [design]**; three IAT wrappers in the child do it for any lock at first touch, which also covers locks of EA code a static list cannot name:

- **W1 critical sections:** TS3W's `EnterCriticalSection`, `TryEnterCriticalSection` and `LeaveCriticalSection` imports point to a wrapper. On first touch of a CS: `OwningThread` = captured sim-thread id -> rewrite to the child thread id (a lock the sim thread really held at the cut, recursion kept); another captured thread id -> log `lock 0x... owned by game thread N at the cut` and reset (`LockCount = -1`, `RecursionCount = 0`, `OwningThread = 0`); always `LockSemaphore = NULL` (the game's event handle is dead). Then call the real function. EA `Lock` recursion `+0x20` is reset with the CS when the owner was another thread.
- **W2 CRT frees:** TS3W's MSVCR80 `free`, `realloc`, `_aligned_free` (53 + 2 + call sites through registers) check the pointer against the restored region table: inside -> `free` is a no-op, `realloc` = fresh `malloc` + copy of `min(new size, bytes to region end)`; outside -> the real CRT.
- **W3 handles:** `WaitForSingleObject(Ex)`, `WaitForMultipleObjects`, `SetEvent`, `ResetEvent`, `ReleaseMutex`, `ReleaseSemaphore`, `CloseHandle`, `ReadFile`/`WriteFile` on a handle the child did not create -> log the caller and fail loud (watchdog abort). EA `Lock` mutex mode lands here.

Expected at a `0xD81840` cut [inferred, I-123-17]: the sim thread holds none of the listed locks; other threads may hold the EA allocator lock (render, streaming and audio threads allocate), so W1 must exist before the first allocation in the child.

### 7.4 Patched code on the allow-list path: clean vs dumped `.text`

Intersection of docs/68's installed patch addresses (449: S3SS 22, S3PP 21, MonoPatcher 7, ours 416 incl. call targets) with the closure, plus every site in the Mono range [verified tool output]:

| Owner | Sites in the closure (min depth) | Kind | In the child |
|---|---|---|---|
| S3PP | `0x59C5B0` LocalizedStringCache (2) | detour into S3PP | revert; vanilla lookup gives the same string [inferred] |
| S3PP | `0x59AF50`.. StblBucketLayoutTuning (~50 sites, 3) | imm | **keep dumped**: the STBL tables in R7 have S3PP's bucket layout; vanilla immediates would misread them [inferred from docs/17] |
| S3PP | `0xEA3820` `mono_object_isinst` cache; `0xEBBDE0`/`0xEBBF00`/`0xEBBFE0`/`0xEBC060` EHash probe | detour | revert (a cache; the probe passes through when the profiler is not armed) |
| S3PP | `0x4E49E5`.. AllocatorArenaTuning (EA core growth, not in the closure); `0xE4D9F0` `GC_add_roots` (read, Mono range) | imm / read | keep dumped. LSC's `GC_add_roots` ranges point into `Sims3Performance.asi`: drop root ranges outside restored regions before any collection (M4) |
| MonoPatcher | `0xE5CA90` `GenerateCode` | detour | revert; methods first transformed in the child get vanilla IL; MonoPatcher-patched methods are skipped by the docs/122 §12.4 per-method mod guard |
| MonoPatcher | `0xE59218`, `0xE5637D`, `0xE648DC` | imm | keep dumped |
| ts3native | `0x4E16F0`/`0x4E17B0` Lock_Acquire/Release (`[RouteAsync]`, `[RouteGate]`, `[Verify]`), `[Alloc]` `0x4E69D0`/`0x4E6A20`/`0x4E5920` (off by default), `[IcLockCheck]` `0xE85020`/`0xE85030`, `[HeightCache]` `0xC2A570` (9), P5b gates `0x76F15D` (1), `0x62F782` (2), `0x62FB74` (10), `0x7460F0` (1), `World_FireEvent` `0x78F2B0`, `[Verify]` count detours | detour / call into ts3native | revert; vanilla bodies are the single-threaded originals the read-only designs copy (uncontended `+0x918` in the child) |
| ts3native | `[GC]` imm `0xE4E168`, `0xE4A1D8`.. | imm | keep dumped (growth step only) |
| S3SS | none in `.text` of the closure; `MSVCR80` mimalloc and IAT wait slots are outside the exe's `.text` | CRT / IAT | W2; the clean `.rdata` IAT drops the wait slots |

**Decision [design]: hybrid `.text`.** Start from the dumped `.text`; diff it against the clean copy; every differing range that holds a `jmp`/`call rel32` (or a `push`+`ret`) whose target lies outside TS3W's image is a detour into a module the child does not map: restore that range from the clean bytes. Every other difference (immediates, NOPs, in-image retargets) stays dumped. The same rule runs over `.data`: a dword whose dumped value points into a captured module's image range and whose clean value differs is reverted and logged; known cases are `[MonoThread]` GC push hook `0x11949CC` and `[RouteWorker]` `runtime_invoke` `0x1195B60` [verified docs/68]. This needs no per-owner list, so late patches (S3PP's worker-thread installs, MonoPatcher's on-demand ForceJIT) are covered by construction. A wholly clean `.text` would break STBL lookups; a wholly dumped one jumps into unmapped modules.

### 7.5 Gate G3-0 verdict

**Pass (static).** Every region class has a rule (§7.1, 16 classes). TLS is complete (7 dynamic keys, static TLS unused, verified over every call site). The lock list is complete for direct calls (§7.3) but not for the 660 indirect sites, so the design replaces "reset list" with the W1-W3 first-touch wrappers, which need no list; the watchdog of §2.5 step 4 still names any lock or handle they miss. The code-patch decision is the hybrid `.text` of §7.4.

Changes to the M3 steps: M3.1 writes into the header the sim TEB slot values for the 7 keys, every thread id and the module ranges; M3.2 adds W1-W3, the hybrid `.text`/`.data` revert pass and the TLS rewrite (+1-2 h, M3.2 now 6-10 h; total ~14-24 h). No blocker for M3.1-M3.2. Open: I-123-13 to I-123-22 (docs/51 §155).

## 8. M3.1 capture and M3.2 restore: build and synthetic proof (2026-10-20, offline)

Built offline; no game or Steam run. The proof uses a **synthetic image**: `monohost` captures its own process after the M1.1 init (Mono up on the mapped Steam 1.67 TS3W.exe, ScriptCore loaded, one managed call made) and restores that image into a fresh child. This runs the restore path on TS3W's own Mono without the game.

### 8.1 What was built

| Part | Files | What it does |
|---|---|---|
| Format + request | `native/worldimage/wimage.h` | Image = folder: `header.txt` (text records: target, threads with context/TEB/stack bounds, TLS keys with index, allocated bit and sim-thread value, lock owners, modules with base/size/stamp, region table with class and file offset, stats, final `loose`), `mem.bin` (stored bytes, 64-bit offsets), `user.txt` (requester's values), `image.ref` (reference). Parser `WiLoad`/`WiRead`/`WiUser`; `WiRequestCapture` runs on the sim thread: writes `user.txt`, starts the helper, waits for "dumped", runs the reference callback, signals "refdone". Region classes `exe`, `priv`, `noread`, `reserve`, `system` map §7.1 R1-R14 [design] |
| Helper (M3.1) | `native/worldimage/worldimage.cpp`, `build.cmd` | `worldimage capture`: suspends every thread of the target (thread snapshot repeated until no new thread), records contexts, TEBs, stack bounds, the 7 TLS keys (PEB bitmaps `+0x44`/`+0x154`, TEB `+0xE10`/`+0xF94`), owners of the 16 lock globals (I-123-17), modules, every private region (TEB/PEB allocations classed `system`, not stored) and the TS3W image regions; resumes only the sim thread for the reference; **2 s watchdog** resumes the others and marks the image `loose`. Shortcut: raw `mem.bin`, no zstd yet (add when real images need the disk) |
| ts3native (M3.1) | `native/ts3native/worldimage.cpp`; 2 lines in `ts3native.cpp`; `[WorldImage]` in `ts3native.ini` | `Enabled=0` by default. Cut = return of `0xD81840`: the Simulate vtable slot `0x108B6B0` (chained after `[MonoThread]`, which installs first because it insists on the clean slot value) points to a stub that swaps the caller's return address for its own, so the cut code runs right after Simulate returns, whatever its argument count. Every 30 ticks it looks for `<OutDir>\request` (harness/probe trigger), deletes it and captures into `<OutDir>\wi-<UTC>-<n>`; `MaxCaptures=5`; `user.txt` = build, tick, host pointer, MT index `[host+0x15F0]` (I-123-23); `image.ref` = "ref none" until M3.3. No counters (log lines) |
| Restore (M3.2) | `native/monohost/monohost-image.cpp` (included by `monohost.cpp`; small edit in `wmain`) | `monohost <exe> <asm> image <dir>`: the parent creates the child suspended, reserves the exe range, lists what the child holds at creation, reserves each captured allocation at its address and writes its committed regions (protection restored); the **sim stack moves** to a free range when it collides (data only: a copy serves as a root range); other collisions are skipped and listed in `<dir>\restore.txt`. The child maps the clean TS3W.exe (M1.1 code), runs the **section pass** (writable sections dumped + pointer revert, executable sections hybrid, `.rdata`/`.rsrc` clean), installs **W1-W3** in 27 TS3W import slots (W1 also wraps `InitializeCriticalSection*` and `DeleteCriticalSection`; W3 records handles from TS3W's `Create*` calls), **rewrites the TLS keys** (`TlsAlloc` + the sim thread's value), sets `GC_dont_gc` `[0x11F30A8]` +1 and hooks `[0x11949CC]` to push the restored sim stack after the collector's own. Synthetic images only: re-applies monohost's allocator redirects with restore-aware `free`/`realloc` (a game image keeps EA's allocator and its restored cores). A 60 s child watchdog logs EIP, exe return addresses and stack words pointing into `.data` or restored memory (lock candidates), exit 94. `monohost <exe> <asm> imagecap <dir> [helper]` makes the synthetic image |

Module rule of both passes [design, refines §7.4]: a captured module is **present** when the child has a module with the same file name at the same base and the same `SizeOfImage` (system DLLs are, so pointers into them stay). A `.text` diff run holding `call`/`jmp rel32` or `push imm32; ret` to a target outside TS3W and outside every present module is reverted to clean bytes; a `.data` dword that differs from clean and points into a non-present module is reverted. One false-positive class was found and filtered: `CRITICAL_SECTION.SpinCount` `0x020007D0` (dynamic-spin flag + 2000) fell inside `d3dx9_31.dll`, which the child loads at another base (all 19 reverts of the first run); filter: high word `0x0200` with a CS `LockCount` in `(-0x10000, 0]` 16 bytes before [verified: synthetic run log].

### 8.2 Fake-process selftest (helper)

`native\worldimage\build.cmd` builds the helper and runs `worldimage selftest`: two test processes request their own capture (1 MB pattern region, 4 MB reserve, a guard page, two TLS keys, a CS held by a spinning worker). **16/16 PASS** [verified, 3 runs]: header parses; pattern bytes equal; reserve without bytes; guard page `noread`; TLS keys allocated with sim value `0x1234ABCD` and 0 for the worker-only key; CS owner = worker id; exe regions present; TEB/PEB allocations `system`, none stored; strict capture: worker counter still during the reference (`loose 0`); blocking reference: watchdog fired at ~2.02 s, worker resumed, reference finished (`loose 1`).

### 8.3 Synthetic proof (monohost)

Capture side: M1.1 init, `ScriptCore.Route.GetNumPointsForZeroToDiameterApprox(100, 1)` = 7920 (boxed result kept as a heap object to check later); a second thread **holds the Mono loader lock `0x11F43A8`** at the cut, a lock owned by a thread that does not exist in the child. Capture: 5 threads, 95 regions, 57 MB stored, 0 unreadable pages, stall 140-190 ms, `loose 0`.

Restore side: 4 fresh capture + restore pairs and one restore of an image from an older monohost build, all `IMAGE OK: 9 passed, 0 failed`, exit 0 [verified: logs]:

| Check | Result |
|---|---|
| Regions | 23-25 allocations restored (43 MB) in 16-31 ms; the sim stack (allocation `0xB0000`, 1 MB) collided with the child's initial stack and was moved; 3-5 small collisions skipped (`0x70000` reserve, `0x1C0000`, `0x1280000`-`0x1420000`), all ranges the child holds at creation; the managed world did not need them (I-123-24) |
| `M3-text` | 10 differing runs = monohost's own 10 allocator/loader `jmp`s: kept when the image came from the same build (present module), all 10 reverted when it came from an older build (`SizeOfImage` differs) and then re-applied: both branches ran; `.data`: 0 reverts after the SpinCount filter |
| `M3-tls` | 6 of 7 keys rewritten (`[0x114C908]` never allocated in monohost: no EA job system) |
| `M3-stack` | 256 bytes at the captured ESP equal in the moved copy; `[0x11949CC]` = the restore push hook |
| `M3-statics` (G3-1 style) | `mono_defaults.corlib`, `GC_gc_no`, `GC_heapsize` equal to capture |
| `M3-domain` | current domain through the rewritten TLS key = the captured root domain |
| `M3-heap-object` | the boxed 7920 from the capture side intact on the restored Boehm heap (vtable equal) |
| `M3-lookup`, `M3-W1` | class `ScriptCore.Route` and the method found again through Mono's restored loader, equal pointers. First try **hung** in the loader lock: W1's "restored" test covered private memory only, not TS3W `.data` where the Mono locks live; fixed (W1 covers the exe range); log `W1: lock 011F43A8 owned by game thread N at the cut: reset (caller 00E8502B)` |
| `M3-invoke` | the same managed call on the restored heap = 7920 = `image.ref` (the reference the capture side ran with the other threads suspended) |
| W2 / W3 | 0 hits (nothing on this path frees a restored block or touches a captured handle) |

Not covered: Boehm collections on the restored heap (off; M4), EA's GeneralAllocator state and the MSVCR80 W2 path (monohost redirects them to its CRT), any IC body (M3.3), images over 2 GB, and S3SS/S3PP/ts3native detours (the synthetic `.text` holds only monohost's).

### 8.4 Ledger

docs/51 §158: I-123-23 to I-123-27.

### 8.5 What the first real capture job needs

1. **Deploy:** `worldimage.exe` next to `ts3native.asi` (or `Helper=`), `[WorldImage] Enabled=1`, `OutDir=<repo>\game-copy\worldimage`; a ts3native build with this section (version set at merge). ~2 GB free disk per capture (raw).
2. **Trigger:** a harness step that writes `<OutDir>\request` once the world is loaded and meta searches fire (`aut-meta-burst` repro); a `bp-worldimage` job (5 min, capture job, not a timing run). **Built 2026-10-20 (§8.6)**; no `aut-meta-burst` repro exists, so the job settles with the autopilot on instead.
3. **Read from the first image** before any restore: stall and size (I-123-1, I-123-11), lock owners at the cut (I-123-17), TLS values (I-123-14), TEB/PEB `system` classing on the game, which modules are non-present in the child (ASIs, DXVK).
4. **G3-1 on it:** `monohost image` with game checks instead of the synthetic Route checks: `SimClock` ticks and `LotManager` lot count need managed static reads in the child plus the matching values in ts3native's `user.txt` (**built 2026-10-20, §8.6**); MT index from `user.txt` (I-123-23). The child then runs EA's restored allocator for the first time (I-123-25).
5. Collisions with the child's at-creation ranges: harmless in the synthetic run; one on a Boehm section or an EA core fails G3-1 and needs the launcher to move its own at-creation ranges (I-123-24).

### 8.6 First capture job and G3-1 checks (built 2026-10-20, offline; no game run)

| Part | Files | What it does |
|---|---|---|
| G3-1 values at the cut | `native/worldimage/wimage.h` (`kWiStatics`, `WiFindClass`, `WiReadStatic`), `native/ts3native/worldimage.cpp` | Before the helper is started, on the sim thread right after Simulate returns, ts3native resolves each class with EA's `ScriptHost_FindClass` `0xD79CA0` (thiscall `host, ns, name`, I-123-29) and reads the static with pure memory reads (field from `mono_class_get_field_from_name` `0xE6F720`; static data through `class+0xB8` runtime info, domain id `+0x58`, `vtable+0xC`; I-123-28): `SimClock.sSimTickCnt`, `sStartTime` (`DateAndTime.Ticks`), `sTicksAdvanced`, `LotManager.sLots.used_slots` (lot count), `Autonomy.sPublicMetaObjects._size`, `AutonomyRestrictions.sLevel` (I-123-30). `user.txt` gets `g31.<key> <hex>` (or `none`), `g31k.<key> <MonoClass*>` and `g31.currentticks` (sum of the three `SimClock` fields = `SimClock.CurrentTicks`, I-123-31); the log line adds `G3-1 statics n of 6 read, CurrentTicks t`. `[WorldImage]` stays `Enabled=0`; `TS3NATIVE_VERSION` unchanged (set at merge) |
| G3-1 in the child | `native/monohost/monohost-image.cpp` (`GameStaticsG31`) | A game image (no synthetic `method` key) now runs, after the restore checks: `G3-1-<key>` for the 6 statics (same reads through the restored loader and domain, equal to the cut), `G3-1-mtindex` (`[host+0x15F0]` vs `user.txt mtindex`, I-123-23), `G3-1-invoke` (`SimClock.get_CurrentTicks` on the restored interpreter = `g31.currentticks`), `G3-1-findclass` (EA lookup through the restored host returns the captured class; last, since it allocates through EA's restored allocator, I-123-25). Exit code = failed checks; G3-1 passes when all are PASS. Images from a ts3native without these values get the restore checks only |
| Harness | `tools/harness/queue.ts` (`run.worldimage`), `tools/harness/queues/worldimage.json` | `run: {worldimage: {captures, settleS?, gapS?, speed?}}`: needs `WorldImage.Enabled=1` and `MaxCaptures >= captures`; `OutDir` must be under `game-copy/` (default `<repo>\game-copy\worldimage`), `Helper` defaults to `<repo>\native\worldimage\out\worldimage.exe` (run in place: nothing extra is copied into Bin; a missing helper refuses before the game is touched). After the load: `OK   world image` must be in `ts3native.log`, autopilot on, settle, then per capture write `<OutDir>\request` and wait for the new `wi-*` folder's `header.txt` to get its `loose` line. Fail fast: every 30 s the game must be up and the capture must have moved (request taken, folder made, `mem.bin`/`header.txt` grown); 2 misses = FAIL with the reason (request not taken = sim not ticking or section inactive). Row `worldimage`: per capture folder, stall ms (ts3native log line), dump ms, stored MB, `loose`, threads, modules, TLS keys allocated, lock owners at the cut (I-123-17), G3-1 statics read `n/6`. Jobs `bp-worldimage`, `ip-worldimage`: 2 captures each, settle 60 s, gap 60 s, asi `tested` |

Rules for these jobs: correctness only, never timing runs (each capture stalls the game seconds); ~2 GB disk per capture (raw `mem.bin`), ~8 GB for both jobs; images hold the game's whole memory (personal data included) and stay in `game-copy/` (git-ignored), never published. Deploy: build the helper (`native\worldimage\build.cmd`) and the ts3native with this section in the main checkout before the queue; the tested asi is copied as usual. After the job: `monohost <TS3W.exe copy> <assemblies> image <game-copy\worldimage\wi-...>` per image; `IMAGE OK` with the four G3-1 check families passes G3-1.

Offline status: ts3native, helper (selftest 16/16) and monohost build; `queue.ts --self-check` ok (worldimage parsing, ini defaults, header summary); `queue.ts tools/harness/queues/worldimage.json --dry-run` lists both jobs. Not run: the static reads in the game, `GameStaticsG31` on a real image (no image yet). Ledger: docs/51 §161 I-123-28 to I-123-31.
