# 118. Mono offload plan (M0-M6) and the M0 embedding glue map

Written 2026-10-20. Static only: Ghidra headless on the `game-copy/ghidra-monoc` copy project (read-only), the symbols csv, `tools/managed-callgraph.ts` on the extracted script DLLs. The game was not run. Addresses are for the Steam `TS3W.exe` 1.67, image base `0x400000`. Tags as in AGENTS.md: [verified] = seen in the disassembly or strings, [inferred] = shape or reasoning, still to confirm.

The plan was agreed on 2026-10-19 and not written down until now. Mono work stays parked behind routing (PLAN.md): the static steps (M0, the static closure, M1 mapping) run now; everything that needs the game waits.

## 1. Goal

Run **autonomy scoring** (~110 ms/s of sim-thread time across the per-pair test, score and the meta phase; docs/28, docs/59 §3.3, docs/75) on a second core, through a second Mono thread inside the game's own embedded runtime. The first live target is the meta-scoring phase (80% of manager-task scoring, docs/75 §1). The offload rule applies: a step may ship at no gain or at most ~2% loss if it is stable (CLAUDE.md, docs/23 #17).

Background: the runtime is EA's branch of Mono 1.2.3 with the mint interpreter (docs/29). A second managed thread is blocked by the GC (docs/30 P1-P3, docs/57), the interpreter's frame globals (docs/30 P4, docs/56), unlocked caches and the loader (docs/58), and above all by shared game state and the native calls scoring makes (docs/59, docs/62).

## 2. Steps

Effort is wall-clock time for Claude with agents and Rodney's help (CLAUDE.md rules). Game runs and clean nights are listed apart.

| Step | What | Inputs | Outputs | Static or game | Effort |
|---|---|---|---|---|---|
| **M0** glue map | Every Mono function that EA code calls, matched to Mono 1.2.3, plus EA patches and deviations inside the runtime | Ghidra project, symbols csv, docs/29/56/57/58 | §4 here, `docs/data/mono-glue-map.csv` | static | **done** (this doc) |
| **C** closure, static part | For the autonomy entry list (docs/75): managed call graph to internal calls; each IC's docs/62 class; then the native closure of each non-SAFE IC (docs/62 §8-9 scan widened) | `tools/managed-callgraph.ts --ics`, `docs/data/ic-safety.csv` | §5 here (top 5 meta definitions + base path done); widened native closure: next | static | 1-2 h left |
| **C** closure, live part | Probe records which ICs scoring really calls (names, counts, thread) for one 10-minute load | the static set from C | IC hit list per definition; diff against §5 | game: 1 live job | 1-2 h + 1 job |
| **M1** offline host mapping | What a host process needs to load `TS3W.exe`'s runtime without starting the game: init order, EA callbacks the runtime calls (§4.3), allocator, image loading from packages, IC table | M0, docs/29 §1.2 | docs/119 | static | docs/119 (other lane) |
| **M2** IC stubs | Offline replacements for every IC in the closure: recorded answers for PROXY ICs, the game's own body for SAFE ones, Mono runtime ICs as they are | C, M1 | stub table + offline host that runs `Test()` / `CalculateScore` of one definition | static + build, no game | 4-8 h |
| **M3** record/replay | Record IC arguments and results plus the scoring inputs in game; replay them in the offline host and compare scores | M2, a probe | replay report: scores equal per pair (or the first differing IC) | game: 2 live jobs | 3-6 h + 2 jobs |
| **M4** single-thread GC stress | Offline host replays M3 with forced collections every N allocations; checks roots (uncollectable cells, `push_other_roots` hook, task state) | M3 data | zero mismatches and zero faults over a fixed number of collections | offline, no game | 3-5 h |
| **M5** second Mono thread | docs/30 P1-P4 (thread registration, multi-stack scan, gated GC lock, interpreter frame globals) plus the two TLS slots of §4.4; first in the offline host (worker scores while the main thread allocates and collects), then in game | docs/30, docs/57, docs/120, M4 | `[MonoThread]` worker that runs replayed scoring with zero mismatches | offline first, then game: 2-4 live jobs | 6-12 h + jobs |
| **M6** shadow in game | Worker scores the allow-listed meta definitions in parallel with the sim thread's own scoring; results compared, never used | M5, docs/59 §3.6 allow-list, docs/104 mod guard | mismatch rate, worker cost, sim-thread cost (`--ab`) | game: 3-5 live jobs + 1 clean night A/B | 4-8 h + runs |

## 3. Gates

| Gate | After | Pass when | Fail means |
|---|---|---|---|
| G0 | M0 | every Mono entry the closure's ICs and the host invoke path use is in the glue map with a verified or prior match | map the rest before M1 builds on it |
| G1 | M1 | the offline host runs `mono_interp_init` and one static managed method from `ScriptCore` (no game state) | the host needs more of EA's runtime than §4.3 lists: extend docs/119 |
| GC | C (live) | the live IC hit set is a subset of the static set of §5 | the static closure misses dispatch (virtual, delegate, reflection): widen with docs/103 class hierarchy first |
| G2 | M2 | 100% of the ICs in the live hit set have a stub | stub the missing ones; no partial replay |
| G3 | M3 | replayed scores equal the recorded ones for every pair of a 10-minute load (floats bit-equal; RNG paths replayed from the record) | first differing IC or static read names the missing input |
| G4 | M4 | zero mismatches and zero faults with a forced collection every N allocations over the whole replay | a root is missed: fix before any second thread |
| G5 | M5 | offline: worker replay with concurrent main-thread allocation, zero mismatches over the whole replay; in game: `[MonoThread]` probe runs clean twice (crash rule) | stop; the GC or interpreter state is not thread-safe yet |
| G6 | M6 | shadow mismatch < 0.1% of pairs, no crash in two clean runs, sim-thread cost <= ~2% | stay on the sim thread; record why |

Go-live (scores used from the worker) is a separate decision after G6, with the docs/59 §3.5 staleness rules.

## 4. M0: the embedding glue map

### 4.1 Method

- Range of the Mono build: `0xE450C0`-`0xEC0000` [verified]: eglib starts at `0xE450C0` (first function with `mono\eglib\src\garray.c`; the code below it up to `0xE3C000` carries no Mono source paths), Boehm from about `0xE48E80`, then `mono\mono\interpreter` (`0xE51400`-) and `mono\mono\metadata` up to `mono-sha1.c` / `monobitset.c` at `0xEBE000`-`0xEBFFFF`; from `0xEC0000` the strings are EA's `allocator.h`.
- `tools/ghidra/BoundaryCalls.java` over the range: every reference from outside into a function entry inside. A function counts as an **embedding entry point** when code outside the range calls it (data references, mostly the IC registration tables, are not counted). 123 entry points, 4,271 call sites.
- Each entry matched by assert strings and source paths in the binary, by structure (field offsets and calls that only one Mono 1.2.3 function has), or taken from earlier docs. Mono source was not fetched; matches by structure rest on the 1.2.3 layout of `MonoMethod`, `MonoType` and `MonoDefaults`, which the binary confirms where an assert names the field (for example `ac->rank` at `+0x10`, `parent` at `+0x24`).
- Callers grouped by address range: **IC bodies** `0xDC0000`-`0xE450C0`, **host glue** `0xD82000`-`0xDC0000` (IC trampolines, native-to-managed calls), **host core** `0xD72000`-`0xD82000` (`ScriptHost_*`: tasks, invoke, delegates, class registration), **host memory** `0xD70000`-`0xD72000` (GC, frame arena, error report).

### 4.2 Coverage

`docs/data/mono-glue-map.csv` (124 rows: the 123 entries plus `mono_get_corlib`, which the exception helpers use internally): address, Mono function, match, evidence, external call sites, caller functions, caller ranges, example callers.

| Match | Rows | Meaning |
|---|---|---|
| verified | 39 | strings or structure checked in this pass |
| prior | 34 | verified in docs/29, 56, 57, 58 or the symbols csv |
| EA | 11 | EA addition with no upstream counterpart (task state, yield flag, frame lookup, counters) |
| inferred | 40 | shape only: mostly the 12 `mono_defaults` type accessors, field and method accessors, debug-helpers and three unmatched helpers |

Who calls what [verified: call counts]:

| Caller range | Call sites | Entries | Main entries |
|---|---|---|---|
| IC bodies | 2,917 | 10 | `mono_get_exception_null_reference` `0xE770E0` (671 sites in IC bodies, 1,493 in all), `mono_domain_get` `0xE75260`, `mono_object_new` `0xEA36F0`, the EA bind helper `0xE520E0`, `mono_get_exception_index_out_of_range` `0xE772A0`, `mono_array_new`, `mono_value_box`, `mono_object_isinst`, `mono_array_element_size` |
| host glue | 978 | 41 | the same allocation and exception helpers from the IC trampolines; `mono_method_get_header` `0xE85390`, `mono_method_signature`, class init and vtable for static field access, `mono_ldstr`, `mono_class_get_full`, `mono_ldtoken`, `mono_lookup_internal_call`, stackval conversion `0xE521A0`/`0xE52350` |
| host core | 277 | 76 | runtime and delegate invoke, task state, `mono_class_from_name`, `mono_class_get_method_from_name_flags`, assembly load, the type accessors (marshalling), and a field-constant reader (`0xD7A240`/`0xD7A4D0`: `mono_class_get_fields`, `mono_class_get_field_token`, `mono_metadata_get_constant_index`, blob heap) |
| host memory | 47 | 32 | Boehm directly (`GC_malloc`, `GC_free`, `GC_malloc_uncollectable`, `GC_malloc_explicitly_typed`, `GC_gcollect`, `GC_apply_to_each_object`, `GC_push_all_stack`), loader error TLS, `mono_method_full_name` for error reports |

Init and loading [verified]: `MonoScriptHost_Init` `0xD7E560` calls the interpreter's own **`mono_interp_init` `0xE5A680`** once (TLS slot `[0x11F4160]`, runtime hooks, `mono_icall_init`, the four mint ICs and the interruption checkpoint, as upstream mint does), then `GC_enable_incremental`, `mono_add_internal_call` and static field setup. Assemblies come only from memory: `ScriptHost_LoadImage` `0xD78760` calls `mono_image_open_from_data` `0xE84860`, and `0xD7C5A0` calls `mono_assembly_load_from_full` `0xE68290`. No EA code calls a file-based loader.

### 4.3 EA patches and deviations from stock Mono 1.2.3

These matter for the offline host (M1, docs/119) and the second thread (M5, docs/120).

| # | What | Evidence | Tag | Matters for |
|---|---|---|---|---|
| D1 | **eglib instead of GLib.** Upstream 1.2.3 links GLib; this build carries `mono\eglib\src\*.c` (`0xE450C0`-`0xE48A00`). eglib's `g_malloc`, `g_malloc0`, `g_free`, `g_strdup`, `g_realloc` are EA thunks into the `GeneralAllocator` at `[0x11D4BC8]`: `0xD70A10`, `0xD70A30`, `0xD70A60`, `0xD70A80`, `0xD71860` (255 call sites from Mono to `0xD70A60` alone) | thunk bytes [verified]; "1.2.3 used GLib" [inferred: Mono history] | M1: the host must provide this allocator or patch the 5 thunks. M5: the allocator's thread safety decides whether Mono's own `g_malloc` paths are safe on a worker [inferred] |
| D2 | **Boehm messages and aborts go to EA.** `0xD707E0` (30 Boehm call sites) formats the message and then writes to address 0, a deliberate fault; eglib `g_log` goes through `0xEBBA90` to EA formatting | bytes [verified]; that all 30 callers are abort or error paths [inferred] | crash triage: a fault at a write to 0 inside `0xD707E0` is a Boehm abort, not a stray pointer |
| D3 | **Boehm lock calls compiled to empty shared functions.** 36 call sites from Boehm (and `0xE529A0`, `0xE65FE0`) go to `0x5BFB90` (`xor eax,eax; ret`) and 42 to `0xC0FD60` (`ret`); both are linker-folded empty bodies (the symbols csv names them after unrelated IC bodies) | bytes [verified]; role (lock/notify hooks of a threaded Boehm) [inferred] | M5: matches docs/30 blocker 3 (no allocation lock); the call sites are candidate detour points next to docs/57's 17 |
| D4 | **GC hooks into EA.** `GC_win32_get_mem` is EA's `0xD70900`; the other-roots hook `[0x11949CC]` = thunk `0xE4EF90` -> `0xD71820` (stack of the calling thread only, docs/29 §3.1); `GC_collect_or_expand` reaches the EA "never stop" variant `0xE4A320` through EA thunk `0xD70800`; `GC_init` calls `0xD70810`, which sets `[0x11EE528]` = 1 | calls and bytes [verified]; the flag's role [inferred] | M1 (host needs them), M4, M5 (P2 replaces the root hook) |
| D5 | **Interpreter calls into EA.** Yield `ScriptHost_Sleep` `0xD70820`; frame arena `0xD71900`/`0xD71A20`/`0xD71B70` and context cells `0xD71800` (docs/57 §3.2); `Class_GetInterfaceOffset` `0xD70870` used by 9 Mono functions (interpreter, class setup, `mono_class_is_assignable_from`); EA binder `0xD9A7B0` from transform `0xE643B0` and `0xE520E0`; trampoline memory `0xD9AAF0`/`0xD9AB80` (`VirtualAlloc`) from `tramp.c`; transform calls `0xD89930` | calls [verified] | M1: all of these must exist in the host; M5: each is a shared-state point a worker reaches |
| D6 | **`mono_class_init` calls EA class registration `0xD790A0`** (`Scripting/MonoScriptHost/Mono` string, 1,660 bytes, calls back into `mono_class_init`) | call [verified] | M5: class init on a worker runs EA code; whether it runs inside the loader lock is open (I-118-4) |
| D7 | **Image open calls EA `0xD6FCB0`** (FNV-64 hash, `ScriptHost_GetWorld`) from `0xE84140`/`0xE84280` | call [verified]; purpose (resolving referenced assemblies from packages) [inferred] | M1: the host needs a stand-in that finds referenced images |
| D8 | **EA additions inside `interp.c`.** Task state alloc/free `0xE52D70`/`0xE53E80`, invoke with state `0xE59FD0`, yield flag `ThreadContext+0x149` (`0xE52B40`/`0xE52B50`), counter pair `[0x11F4158]` (`0xE51E60`/`0xE51E70`), frame lookup by depth `0xE534E0`, bind helper `0xE520E0` (resolves the runtime method, runs a deferred class init held at `+0x2C`) | calls and offsets [verified]; no upstream counterpart [inferred] | M2/M3: replay must enter through `0xE59FD0` or `0xE52B60` like the host; M5: docs/56 P4 globals |
| D9 | **No EA call into the thread API.** No caller outside the runtime for `mono_thread_attach`, `mono_domain_set` or `mono_gchandle_*` (`0xE78780`-`0xE78BF0`). The domain lives in TLS key `[0x11959DC]` (`mono_domain_get` `0xE75260`), the interpreter context in `[0x11F4160]` | absence of external callers [verified]; "only the init thread has both slots set" [inferred] (I-118-1) | M5: a worker must set both TLS slots itself (and register with the GC, docs/30 P1); thread-static fields go through `0xEBAB30` (docs/56) |
| D10 | **Managed references kept by EA** live in `GC_malloc_uncollectable` cells and on the scanned stack, not in GC handles | callers of `0xE4BF90` [verified]; no gchandle use [verified] | M4: the root set to check |
| D11 | **Store helper `0xE69820`** `(obj, slot, value)` writes `*slot = value`; the shape of a later Mono's `mono_gc_wbarrier_set_field`, called by EA twice (`0xD79AF0`, `ScriptHost_InvokeDelegate`) | bytes [verified]; API identity [inferred] | hints that the branch postdates the 1.2.3 tag, as docs/29 §1.1 suspected |
| D12 | **Symbols csv fix:** `0xEA1400` is `mono_runtime_delegate_invoke` (looks up `Invoke`, calls the slot), not `mono_runtime_invoke`; `mono_runtime_invoke` is `0xEA1090` (`jmp [0x1195B60]`). Corrected in `tools/ghidra/symbols/ts3w-1.67.symbols.csv`. docs/18, docs/29 §5 and docs/62 §9 name `0xEA1400` as runtime invoke; their claims still hold (it ends in the same slot) | disassembly [verified] | naming only |

`mono_defaults` sits at `0x1220E40` [verified: `mono_get_corlib` `0xE75690` returns `[0x1220E40]`, the corlib image the exception helpers pass]. The 12 type accessors `0xE756A0`-`0xE75760` read `+0x04`..`+0x3C` in the 1.2.3 field order (`void` and `uintptr` accessors are absent, likely stripped as unused) [inferred, I-118-5].

### 4.4 What M0 means for M1 and M5

- **M1 (docs/119):** an offline host needs, besides the runtime range, the EA pieces of D1, D4, D5, D6 and D7: the `GeneralAllocator` (or the 5 eglib thunks patched to the CRT), the GC heap source `0xD70900`, the root hook, the class registration `0xD790A0`, the binder `0xD9A7B0` and the IC registration path (`0xE3E570` through `0xD9A860`, docs/29), and an image source for `0xD6FCB0`. The rest of EA's script host can stay out.
- **M5 (docs/30, docs/120):** beyond P1-P4, a worker needs TLS keys `[0x11959DC]` (domain) and `[0x11F4160]` (interpreter context) set (D9); D3's empty lock sites are where a threaded Boehm takes its lock; D6 means class init on a worker reaches EA code; thread-static fields (`0xEBAB30`) need a managed thread object for the worker.

## 5. Closure step, static part: top meta definitions

Roots as in docs/78 §4 (the definition's `Test` and the inherited `MetaInteractionScoredByVenue`3.CalculateScore`, depth 12), with the two hand-checked guards cut (`SeasonsManager.LazyAllocateManager`, `Relationship.Get`), plus the shared base path (`Autonomy.CheckAvailability`, `InteractionDefinitionUtilities.CommonTests`) and the venue score body `Lot.HowMuchItWantsSimToCome`. New flag: `managed-callgraph.ts query|def ... --ics` lists every native binding reached with its docs/62 verdict [verified: tool output].

| Root (docs/75 rank) | Closure | PROXY ICs | Mono runtime ICs | Worst tag |
|---|---|---|---|---|
| `Core.VisitCommunityLot+Definition` (4) | 915 | 29 | 31 | STATIC_WRITE (RNG) |
| `Core.VisitLot+Definition` (7) | 640 | 18 | 20 | STATIC_WRITE (RNG) |
| `Autonomy.GoToGarden+Definition` (12) | 460 | 14 | 16 | STATIC_WRITE (RNG) |
| `Core.GoToCommunityLot+Definition` (venue family) | 445 | 13 | 16 | STATIC_WRITE (RNG) |
| `RabbitHoles.AutonomouslyGoForMealInRestaurant+Definition` | 466 | 14 | 16 | STATIC_WRITE (RNG) |
| base: `Autonomy.CheckAvailability` (1) | 425 | 11 | 19 | STATIC_COLL (`GameUtils.WorldNameToType`) |
| base: `InteractionDefinitionUtilities.CommonTests` (3) | 298 | 12 | 15 | DELEGATE (`Predicate`1`) |
| venue body: `Lot.HowMuchItWantsSimToCome` | 376 | 12 + 1 SAFE | 16 | STATIC_COLL |

Union over all roots: **36 PROXY ICs, 1 SAFE, 31 Mono runtime ICs, no UNSAFE, MAIN or YIELD IC** [verified: tool output, with the two guards cut by hand as in docs/78].

- PROXY ICs by group: world and lot queries (`World_GetLotLocationImpl`, `World_GetRoomIdAtPosImpl`, `World_IsPositionOutsideImpl`, `World_HasAnyRoofOrFloorAbovePosImpl`, `World_GetDoorRoomsImpl`, `World_ObjectGetHiddenFlags`, `World_GetObjectTombRoomIdImpl`, lot name keys, sea level, moon phase, sunrise and sunset), object queries (`Query_GetObjects`, `Query_GetObjectsOnLot`, `Query_CountObjectsOnLot`, `Objects_GetPosition`, `Slots_GetParentIdImpl`, `Slots_GetPositionOfSlotImpl`), task lookup (`Simulator_GetTaskImpl` and its `UTFConnector` twin), world identity (`GameUtils_GetWorldName`/`Type`, `IsWorldInstalled`), product checks (`UserToolUtils_*`), text (`EAText_Get*String`, `LocalizedStringService_GetLocalizedStringByString`), `DeviceConfig_GetOption`, `ScriptProxy_GetResourceKeyImpl`, `OnlineFeaturesHost_GetPersonaName`, and `Random_NextDoubleImpl`.
- Mono runtime ICs: `Object.GetType`, `InternalGetHashCode`, `MemberwiseClone`, array access and copy, `String` allocation, split, join and copy, `Math` (`Floor`, `Pow`, `Round2`, `Sqrt`), `Type` equality and assignability, `Type.internal_from_handle`, `AppDomain.GetData` / `getCurDomain`, `CultureInfo` format construction.

**What blocks running these off the sim thread** [inferred from the tags, by owner]:

1. **Game state through 36 PROXY ICs** (docs/62 §3): each needs a snapshot answer or a proxy call to the sim thread (docs/59 §3.4). World/lot geometry and object queries dominate; text and product checks are near-static and cheap to cache.
2. **The RNG** (`RandomGen.gRandomGen` static write and `Random_NextDoubleImpl`): reached from the base meta score (pet liking branch) and from token localization. Needs a per-worker seeded RNG, which changes outcomes, or these paths stay on the sim thread (docs/59 §3.6 rule 3, docs/27).
3. **Static collections** `GameUtils.WorldNameToType` (lazy fill) and `Situation.sAllSituations` (read while the sim may change it): snapshot or a read lock.
4. **Delegates and open dispatch:** `ScoringFunctionDelegate` (tuning scoring functions), `Predicate`1`, `Lot+OnAllowedInRoomCheck`, and the interface call `IMetaObject.HowMuchItWantsSimToCome` with many implementors; the closure cannot see their bodies (docs/78 §5, docs/103). The live part of the closure step (§2) closes this gap.
5. **Mono runtime ICs and runtime state:** `GetType` goes through `mono_type_get_object` (`0xEA82D0`, critical section around a hash) [inferred]; `Type` ICs reach class init and vtables (loader lock gaps, docs/58 §3); `CultureInfo` and `AppDomain.GetData` touch lazy runtime caches; every `String`/array IC allocates (docs/30 P3, docs/57). These are the M5 prerequisites, not new blockers.
6. Not a blocker for these roots: no UNSAFE or MAIN IC and no yield once the two guards are cut; the guards themselves must be enforced at run time (docs/78 §1).

## 6. Risks

| Risk | Effect | Mitigation |
|---|---|---|
| Shared game state read while the sim writes (docs/59 §2.3) | wrong scores or faults | snapshot or proxy per PROXY IC; staleness rules (docs/59 §3.5); M6 shadow first |
| Closure incomplete (11.7% unresolved call sites, delegates, reflection) | worker reaches an unknown IC | live IC hit list (C live part), mod guard (docs/104), worker refuses unknown ICs and falls back |
| RNG on the worker | changed outcomes, save divergence | keep RNG paths on the sim thread or replay the RNG order; never ship a seeded change silently |
| GC with two threads (docs/30, 57, 120) | freed live objects, heap corruption | M4 before M5; two clean runs per change (crash rule) |
| EA code reached from Mono (D5, D6, D7) | hidden shared state | treat each as UNSAFE until mapped; M1 lists them |
| Offline host fidelity | replay passes, game fails | G3 compares against recorded game values; M6 is the real check |
| Mods patching interpreter internals (MonoPatcher, S3PP) | hooks clash | docs/68 hook map before M5 builds; mod guard |
| Cost | worker gain < sync cost (docs/59 §3.3 break-even) | measure f per docs/75 §5 in M6; stop if below break-even |

## 7. Ledger and next step

Open claims are in docs/51 §142 (I-118-1 to I-118-8).

**Next static step:** the widened native closure for the 36 PROXY ICs (docs/62 §8-9 scan, depth 4) to see which ones a snapshot can answer and which need a proxy; then docs/119 (M1) builds its host list on §4.3-4.4.

## 8. Offload size estimate 2026-10-25

Read-only, existing data, no game run. **The docs/160 sim-budget queue was never run** (dry run only), so there is no per-world Mono/IC split from it; the "~400 ms/s Mono" figure is not from a sim-budget run. Source here: every `probe-out/profile/*-bp-|-ip-|-sv-*-managed.jsonl` from 2026-10-20..25 (102,696 samples: BP 49,016, IP 31,412, SV 22,268; mixed fix states, A/B arms pooled, managed sampler 5 Hz in minutes 1-4). Share of samples = share of wall time, 1% ≈ 10 ms/s (docs/75 §2). "IC" = leaf frame `ScriptCore.*` except `ScriptProxy`.

**8.1 Autonomy scoring on the sim thread** (stack has `Autonomy.CalculateScore`) [measured share, ms inferred]:

| World | Script on stack | of which managed leaf ("Mono") | IC leaf | Scoring | scoring in `AutonomyManager` task | meta part | Scoring share of Mono (managed leaf) |
|---|---|---|---|---|---|---|---|
| BP | 700 | 327 | 373 | **~64-66** (IC 29) | ~53 | ~44 | 11% |
| IP | 708 | 317 | 391 | **~54-56** (IC 10) | ~45 | ~35 | 14% |
| SV | 626 | 377 | 249 | **~87-90** (IC 18) | ~82 | ~46 | 19% |

Higher than docs/75's pooled 47 ms/s (older runs: BP 40, IP 48). Movable [inferred]: pilot (meta, docs/126 62% pooled; here ~0.95 x meta) BP ~42, IP ~34, SV ~44; tier 2 adds ~71-82% of the rest (docs/126 27-31 of 38%): BP +16-18, IP +14-16, SV +32-37. **Caveat:** docs/127's virtual closure drops tier-2 "ready" from 191 to 23 of 270 and blocks all v0 until H11, so tier 2 is an upper bound.

**8.2 What stays on the sim thread** (docs/124 §8.3, docs/122 §2.3) [inferred]: re-score of top-N + bands 10-25% of moved, cut at `Sample=1` 3-9, request build 1-3, guards ≤1, `[MonoThread]` fixed cost 7-25 (shared once with the docs/129 loads). Net:

| World | Autonomy pilot | + tier 2 | docs/129 five (net of ≤2 cut) | Fast paths (docs/139: sub-run fusion 2-8 + call/return 15-30, stay on sim thread) | **Total net** |
|---|---|---|---|---|---|
| BP | -6..+27 | +5..+43 | ~+25..+35 (37 movable) | +17..+38 | **~+47..+116** |
| IP | -12..+20 | -2..+34 | ~+25..+35 | +17..+38 | **~+40..+107** |
| SV | -5..+29 | +19..+62 | ~+25..+35 (not measured on SV) | +17..+38 | **~+61..+135** |

Gross moved before sim-side costs: BP ~95, IP ~85, SV ~115 ms/s. The low ends need everything (M5, pilot, tier 2 with H11, the five candidates, the call/return path): ~80-130 h of agent work and many game jobs. The fast paths alone (no second thread) give +17..+38 at much lower risk.

**8.3 Hitches** [measured counts, cause inferred]: `hitch-ab.ts --causes` on the v0.1.4/v0.1.5 release-validate and soak runs (70 real minutes, 7 runs): of 64 iterations >= 300 ms, 31 are script-led (ThreadedObjects 11 + TaskObjects 8 with no managed sample, Overwatch spread hook 6, `Sim` task 3, `WorldLot`, `OneShotFunction`, `Dispose` 1 each). **None is named as the `AutonomyManager` task**; the >= 1 s ones (7) are all sync `Route.Plan` fallbacks (Foreign 4, NoStart 3). docs/161 §3 (292 min): with a sample, `Sim.Simulate` 8, `FunctionTask` 7, `SimUpdate` 1, `Lot` 1. Local autonomy never yields (docs/04a §4), so a single search spike could hide in the unsampled 19 or in `Sim`-task iterations (side `FindBestAction*`), but the data does not show it. Autonomy offload is a steady ms/s offload (docs/23 #17), not a hitch fix.

**8.4 Against routing:** P6's whole-problem model (docs/189) cuts IP sim wait behind jobs ~74 -> ~15-25 ms/s (~50-60 less) [inferred], about the size of the whole net Mono offload on IP's low end, and routing also owns every >= 1 s freeze. Order stays: routing (P6, fallbacks) first, then the fast paths (docs/139), then M5 + pilot when a second load shares the `[MonoThread]` fixed cost. Measure first: run `sim-budget.json` (docs/160) to replace 8.1's sample shares with timed buckets.

## Reproduce

```sh
# Ghidra (read-only, copy project): Mono range boundary and per-entry sketch
analyzeHeadless <repo>/game-copy/ghidra-monoc TS3 -process TS3W.exe -noanalysis -readOnly -postScript BoundaryCalls.java <out>/boundary.tsv 0xE49000 0xED0000
# closure ICs (§5)
node tools/managed-callgraph.ts def Sims3.Gameplay.Core.VisitLot+Definition --cut "Sims3.Gameplay.Seasons.SeasonsManager::LazyAllocateManager(,Sims3.Gameplay.Socializing.Relationship::Get(" --ics --index <repo>/game-copy/analysis/callgraph/index.json --asm <repo>/game-copy/assemblies
```
