# S3PP features vs our fixes: inventory, verdicts, installer keys, A/B jobs

Written 2026-10-16. Static only: no game run, no queue started. Follows the decision in PLAN.md ("Decision (Rodney, 2026-10-16): prefer our fixes; no S3PP fork"): where one of our fixes covers the same problem as an S3PP feature and wins its A/B, the installer turns that S3PP feature off in `Sims3Performance.ini` and ships ours; every S3PP feature we do not replace stays as it is.

Sources:
- S3PP 2.1 binary and ini (`game-copy/s3pp/bin-2.1/`): feature list, ini keys and defaults, README claims. The 2.1 `.asi` holds the same nine patch class names as the 1.1 source (`...Patch` strings), and the 2.1 ini is byte-identical to the 1.1 ini [verified: string scan of the `.asi`, file diff]. One key exists in the 2.1 binary but not in the ini: `StblDryRun` (with `RunStblEaDryRun`, the EA App 1.69 path) [verified strings; meaning inferred from the name and the README's EA App note].
- S3PP 1.1 source (`game-copy/s3pp/src-1.1/`, GPLv3): behaviour only. Nothing from it is copied here beyond ini key names, addresses and short pattern facts needed to explain a conflict.
- Installed ini: `probe-out/queue/bin-before/Sims3Performance.ini` (the copy the queue takes before a job): all nine keys at the ini default except `ThreadStackDiet = 0` and `ShortWaitSpin = 0` [verified]. Written by `tools/setup/install-fixes.ts` from `tools/setup/fixes.json` `s3pp.config.ini` (static key overrides after the copy) [verified].
- Hook sites: docs/68 §1.2 and §4. Our fixes: docs/15, `mods/ts3fixes/README.md`, PLAN.md "Next round: fixes from the mapping waves" (`#n` below). Measurements: docs/17 §9, docs/29 §5, PLAN.md (label check), `docs/data/results/2026-10-16-queue-s3pp-cache.md`.

**The 2026-10-16 `s3pp-cache` queue is not usable:** both arms (`bp-s3pp-cache-on`, `-off`) hung ~15 s into the native sampler window, in the same way with S3PP's Mono caches on and off, so the hang is not S3PP's [measured]. It gives no number for any feature below.

---

## 1. Feature table

Defaults are the S3PP ini defaults; "Ours" is what we install today. All nine features install from S3PP's own worker thread after ts3native's `DllMain`, by a unique pattern at offset 0 plus an expected-bytes check, through MS Detours (docs/68 §1.2, §5) [verified].

| # | Feature (ini key, `[Patches]`) | Default / ours | What it does | Hook sites, Steam 1.67 (docs/68) | Measured effect | Our fix on the same problem | Overlap / conflict | Verdict |
|---|---|---|---|---|---|---|---|---|
| 1 | Script type index `MonoTypeEHashProbe` (tuning `MonoTypeEHash*`, 11 keys) | 1 / 1 | A per-thread pointer index in front of Mono's `g_hash` lookups on the MonoType -> System.Type table (tables whose compare function is `mono_metadata_type_equal`), used only while the thread is inside a profiler stage. The hooked insert/remove/destroy bump a per-table generation; the index is used only when generation, bucket array, bucket count and entry count all match, and a rebuild is dropped if the generation moved while it ran [verified source] | detours `0xEBBDE0` lookup, `0xEBBF00` remove, `0xEBBFE0` destroy, `0xEBC060` insert; reads `0xEA8720` | One BP run with S3PP on: `0xEA8720` self **0.00%** vs 2.25-4.11% in 7 runs without; `0xEBBDE0` inclusive 1.94% vs 3.85-8.42% (docs/29 §5) [measured; 361 samples, route worker also on, not isolated]. Load time on our store save: no change for the whole S3PP bundle (docs/17 §9) [measured]; author: 18 -> 4 min on a very large save [community] | None, by decision: docs/29 §5 says do not write our own (~20-30 ms/s already S3PP's) | No shared bytes. Disabled silently when the profiler (#2) fails to arm, which our `[Verify]` hook on `0xD7F8B0` causes (docs/68 §4 row 1) [verified source logic]. Planned `[MonoThread]` worker: not in a stage, so S3PP passes its lookups through; the generation recheck covers inserts from that thread [inferred] | **Keep S3PP** |
| 2 | Stage profiler `SimulatorStageProfiler` (tuning `SimulatorSlowStageMs=250`) | 1 / 1 | Marks which sim stage the sim thread is in (needed by #1) and logs `[SIMSTALL]` lines for single stage calls over the threshold; no per-stage totals (docs/28) [verified source] | detours `0xD7FC60` TaskObjects, `0xD74800` OneShotObjects, `0xD7F8B0` ThreadedObjects, `0xD80BC0` ProcessTaskState; patterns 60-66 bytes from each entry [verified source] | 15 TaskObjects stalls of 0.27-6.9 s in one store run (docs/17 §9, docs/04 F17) [measured, log only] | Timing only: Frida `stutter-profile.js` (times the same seams through S3PP's wrapper), probe `--taskstall` (`0xD7FC60`), `[Verify]` threadedObjs (`0xD7F8B0`); planned per-task timer at the ThreadedObjects call sites `0xD7FA74`/`0xD7FB53` (PLAN, docs/64) | `[Verify]` on `0xD7F8B0` = **High** (docs/68 row 1): S3PP arms none of its four seams and #1 goes dark. `--taskstall` already refuses (row 6). The planned call-site timer at `0xD7FA74`/`0xD7FB53` is outside the 62-byte ThreadedObjects pattern (ends `0xD7F8ED`), so it does not break S3PP [verified arithmetic] | **Keep S3PP**; our probes yield (§2) |
| 3 | Type-check cache `MonoIsinstCache` (`MonoIsinstCacheCap=131072`, `...ReportSec=600`) | 1 / 1 | Caches `mono_object_isinst` answers keyed on (object class, target class), bool value, no invalidation, cleared at 75% full, under an SRW lock [verified source] | detour `0xEA3820` | `0xEA3820` inclusive 0.28% with S3PP vs 0.92-2.93% without (same single run as #1) [measured, not isolated] | None | None [verified docs/68]. Header swaps in ts3fixes do not change class hierarchies (docs/17 §8.3) | **Keep S3PP** |
| 4 | Text lookup cache `LocalizedStringCache` (`LocalizedStringCacheCap=16384`, `...ReportSec=600`) | 1 / 1 | Caches the result of `LocStrSvc_GetString_A` per 64-bit key in 16,384 slots (one critical section), keeps the cached `MonoString`s alive through `GC_add_roots` on its slot array, cleared at 75% full or when the service pointer or `*(service+0x48)` changes [verified source]. `+0x48` is the head of the per-locale table list (docs/54), which need not change when a table is added, so a key looked up before its table loads can stay "missing" until the next clear [inferred] | detour `0x59C5B0` (pattern 12 bytes); calls `GC_add_roots 0xE4D9F0`, found by a pattern at offset 0 (34 bytes) | Label check BP: S3PP on and off give the same hash, 14 empty, 0 mismatch, 0 placeholder (PLAN, 2026-10-13/15) [measured]. Speed: not measured; the whole lookup IC is 0.32% managed (docs/54) [measured] | **#29** (planned, not built): one merged key -> entry index rebuilt when `LocStrSvc_RebuildTableList 0x59BDE0` returns, answering `0x59C5B0`, first-table-wins order, cannot go stale (docs/54 §5 #1). ts3fixes `kSimNameCache` (built, off): caches Sim names above the IC (managed) | **#29: High, same entry `0x59C5B0`** (docs/68 row 3): ours installs first, S3PP's then fails silently. **`[MonoThread]` step 2 `GC_add_roots` detour: Medium** (row 4): writes inside S3PP's 34-byte search window, so LSC does not install. `kSimNameCache`: no shared bytes; both can be on, but its A/B must hold LSC fixed | **Prefer ours once A/B wins** (§3 job C). Off switch: `LocalizedStringCache=0` |
| 5 | Text-table slimming `StblBucketLayoutTuning` (hidden: `StblDryRun`) | 1 / 1 | Shrinks every STBL hash table from 7,919 buckets to 509 by rewriting immediates (docs/68: ~50 sites; bucket modulus, sentinel, entry count, node array, string buffer offsets, object size) in Find, Clear, FindOrInsert, the constructor, serialize and both GetString bodies; byte-level self-test first, rollback on failure; refuses to install once the string service exists [verified source] | imm sites `0x59AF50`-`0x5A0xxx` (Find `0x59AF50`, Clear `0x59F6C0`, FindOrInsert `0x59F750`); one site is the sentinel read inside `GetString_A 0x59C5B0`'s body [verified source site list; Steam address of that site not resolved here] | ~29,640 bytes saved per table (log `savedPerTable`, docs/17 §9) [measured]; whole S3PP bundle: -71 MB committed, +64 MB largest free block (docs/17 §9) [measured]; per-feature share not measured; author: ~290 MB on a big save [community] | None replaces it. #29 builds its index from these tables | No shared bytes with any of our sites. **Layout dependency:** if #29 (or any probe) walks STBL buckets directly, the bucket count and offsets differ with this feature on (509 / sentinel `0x81C` / entry count `0x820` / node array `0x828` vs 7,919 / `0x7BBC` / `0x7BE4` / `0x7BF0`) [verified source constants]. #29 must read the layout at run time (from the patched Find modulus immediate) or build through `STBL_Find`/the node array, and must not detour into the bytes of the sentinel site in `0x59C5B0` | **Both compatible** (keep on; #29 layout-agnostic) |
| 6 | Arena growth tuning `AllocatorArenaTuning` (`ArenaReserveMB=4`, `ArenaCommitMB=1`) | 1 / 1 | Changes EA GeneralAllocator core sizes from 16 MB reserve / 4 MB commit to 4 / 1 MB: four immediates in two setup functions; no detour [verified source] | imm `0x4E49E5`, `0x4E4A1C` (in `0x4E48F0`), `0x4E5BDA`, `0x4E5BE4` (in `0x4E5AA0`); patterns 14-20 bytes | Not measured alone (inside the -71 MB bundle) | `[Alloc]` (built, parked, `Enabled=0`; double-free bug, docs/67): per-thread cache in front of the same allocator's locked entries | `[Alloc]` detours `0x4E69D0`, `0x4E6A20`, `0x4E5920`, `0x4E5CB0`; nearest S3PP window ends `0x4E5BF4`, so no shared bytes [verified arithmetic]. Different problems (lock cost vs reserved space); combined effect on fragmentation unknown [inferred] | **Both compatible** |
| 7 | Package index warm-up `PackageIndexWarmup` | 1 / 1 | A below-normal-priority thread reads the DBPF index blocks of `.package` files under the `Mods` roots named in `Mods\Resource.cfg`, plus `.dbc` in `DCCache`, into the OS file cache while the menu is up; no game memory touched [verified source] | none (file reads) | Not measured alone; bundle: no load-time change on our save (docs/17 §9) [measured] | #9 key-to-package map (in-memory probe, sim thread), #14 parallel package index load (planned): different layer | None in code. Measurement confound: warms the OS cache, so a cold vs warm load split must have both arms in the same state (docs/17 §8.3) [inferred] | **Keep S3PP** |
| 8 | Thread stack diet `ThreadStackDiet` (`ThreadStackDietReserveKB=512`, `...ReportSec=600`) | 1 / **0** | Detours `kernel32!CreateThread`; only when the caller passes stack size 0 it substitutes a 512 KB reservation; explicit sizes are never changed [verified source] | detour on `CreateThread` (kernel32, not the exe) | Not measured; S3PP README: ~70-95 MB [community] | None built. docs/66 rank 2: "S3PP `ThreadStackDiet` or our own cap", undecided. S3SS `OversizedThreadStackFix` (not ours) passes an explicit small stack at `0x4DB355` | Off since 2026-10-12 because S3SS "does it". From the sources the two compose: S3SS's call passes an explicit size, which TSD leaves alone, so TSD would only add the other default-stack threads [inferred: MSVCR80 `_beginthreadex` passes its size to `CreateThread`]. Our own threads (`[SaveWrite]`, `[SaveFlush]`, `[SaveCompress]`) pass 256 KB explicitly, so TSD never touches them [verified `savewrite.cpp`, `savecompress.cpp`] | **Keep S3PP's switch, off for now**; turn on only after the docs/66 #2 A/B (§3 job D) |
| 9 | Short-wait spin `ShortWaitSpin` (`ShortWaitSpinUs=100`) | 1 / **0** | Patches the exe's `WaitForSingleObject[Ex]` import slots to spin up to 100 µs before sleeping; 2.1 skips itself when another module already owns a wait import [verified source; 2.1 string] | IAT slots (no `.text` bytes) | Not measured | None. S3SS `AdaptiveWait` owns the same IAT slots (docs/68 §1.1) | Same slots as S3SS: never both (docs/07) | **Keep off** (S3SS covers it; not ours to replace) |

Not features: `[Tuning]` keys only size or report the patches above; `MonoTypeEHashDiagnostics=0` is a log switch.

### Verdict counts

| Verdict | Count | Features |
|---|---|---|
| Keep S3PP (we have nothing) | 4 | `MonoTypeEHashProbe`, `SimulatorStageProfiler`, `MonoIsinstCache`, `PackageIndexWarmup` |
| Both compatible (ours stays alongside) | 2 | `StblBucketLayoutTuning` (with #29), `AllocatorArenaTuning` (with `[Alloc]`) |
| Prefer ours once its A/B wins | 1 | `LocalizedStringCache` (#29) |
| Off, covered or pending (not ours) | 2 | `ThreadStackDiet` (pending docs/66 A/B), `ShortWaitSpin` (S3SS `AdaptiveWait`) |

---

## 2. Conflicts and the ini key per conflict

| Conflict (docs/68 row) | Risk | S3PP key to set when ours is on | Rule |
|---|---|---|---|
| `[Verify]` threadedObjs detour `0xD7F8B0` vs `SimulatorStageProfiler` (1) | High: S3PP's type index (#1) goes dark without an error in our log | none: ours yields | `[Verify]` skips `0xD7F8B0` while `Sims3Performance.ini` has `SimulatorStageProfiler=1` (missing ini or key = 1, S3PP's default). A job that needs our own ThreadedObjects timing sets `s3ppIni` `Patches.SimulatorStageProfiler: 0` and `Patches.MonoTypeEHashProbe: 0` together (README: both on or both off), and its timings are then not comparable with S3PP-on runs [inferred] |
| #29 merged string index on `0x59C5B0` vs `LocalizedStringCache` (3) | High | `LocalizedStringCache=0` | ts3native reads the S3PP ini at `DllMain` and refuses #29 unless the key is 0 (missing ini = S3PP default 1 = refuse) |
| `[MonoThread]` step 2 `GC_add_roots 0xE4D9F0` entry detour vs `LocalizedStringCache` search window (4) | Medium: LSC does not install | `LocalizedStringCache=0`, unless the hook is made byte-preserving (caller-side, docs/68 row 4) | Same refuse rule as #29. If #29 already shipped with LSC off, nothing more to do |
| Probe `--taskstall` `0xD7FC60` vs `SimulatorStageProfiler` (6) | Low, test only | none | Already refuses; take stage stalls from S3PP's `[SIMSTALL]` log |
| #29 index builder vs `StblBucketLayoutTuning` table layout (new, §1 row 5) | High if hard-coded: wrong buckets or reads past a 509-bucket table | none (keep STBL on) | #29 reads the active layout at run time and works with both 7,919 and 509 buckets; its offline test (emulator harness, docs/63) covers both layouts |
| `ThreadStackDiet` vs S3SS `OversizedThreadStackFix` (docs/07) | Low by source reading | (decided by job D) | Keep 0 until job D; the old "would conflict" note in docs/17 §8.2 is inferred and the sources say they compose |
| `ShortWaitSpin` vs S3SS `AdaptiveWait` | Same IAT slots | keep `ShortWaitSpin=0` | 2.1 also skips itself when S3SS got there first [verified string] |

### Installer change list (`tools/setup/fixes.json` / `install-fixes.ts`)

Today `s3pp.config.ini` is a static map applied after the copy. Changes, in the order the fixes land:

1. **Keep** `ThreadStackDiet: 0` and `ShortWaitSpin: 0` (no change until job D).
2. **When #29 ships on by default:** add `LocalizedStringCache: 0` to `s3pp.config.ini`. If #29 ships default off, the static map cannot express "only when ours is on": add a conditional override (e.g. `configWhen: { "native-patches": { "Sims3Performance.ini": { "LocalizedStringCache": 0 } } }`, applied when that fix id is installed with its section enabled) and have uninstalling ours put the key back to 1. ts3native refuses #29 when the key is not 0, so a wrong ini costs only the speed-up, never both hooks at once.
3. **When `[MonoThread]` step 2 ships with an entry detour on `GC_add_roots`:** same `LocalizedStringCache: 0` (shared with item 2).
4. **`[Verify]` / verify queue:** no installer change; `[Verify]` skips `0xD7F8B0` when the profiler key is 1 (ts3native change, docs/68 row 1). Every verify job checks `Sims3Performance.log` for `four scheduler seams armed` (docs/68 §6.7).
5. **After job D, if the stack diet wins:** set `ThreadStackDiet: 1` (keep S3SS's fix on).
6. **Checks after install (all runs, stop-on-error rule):** `Sims3Performance.log` shows `[MTEH] ... armed`, `four scheduler seams armed`, `[STBL509] active`, the `[ISC]`/`[LSC]` reports for the keys that are 1, and no `[TSD] ... armed` or `[ShortWaitSpin] spinning` while those keys are 0.

---

## 3. A/B queue jobs (`s3ppIni`)

Format as in `tools/harness/queues/s3pp-cache.json`; `s3ppIni` keys are `Section.Key` (`queue.ts` refuses others) and the queue puts the ini back after each job. **Gate:** every `profile` job waits until the native sampler hang of 2026-10-16 is triaged (PLAN: both `s3pp-cache` arms hung ~15 s into the 180 s window). Jobs without the sampler (`menuMemory`, `soak`, `repro`) can run now. None of these are started here.

**A. S3PP Mono caches, rerun of `s3pp-cache.json`** (decides #1/#3 "keep S3PP" with a number; docs/29 §5, docs/23 #5). Unchanged jobs `bp-s3pp-cache-on` / `bp-s3pp-cache-off` (`Patches.MonoTypeEHashProbe: 0`, `Patches.MonoIsinstCache: 0`), after the sampler fix. Expect `0xEA8720` self near 0 and `0xEBBDE0` inclusive <= 2% with the caches on.

**B. Per-feature address space** (fills docs/66 rows; splits the -71 MB bundle). Menu only, no sampler:

```json
[
  { "id": "menu-s3pp-all", "note": "S3PP as installed: address-space baseline for the split", "asi": "tested", "save": "menu",
    "run": { "menuMemory": { "waitS": 90 } }, "timeoutMin": 15 },
  { "id": "menu-s3pp-nostbl", "note": "StblBucketLayoutTuning off: its share of committed / largest free", "asi": "tested", "save": "menu",
    "s3ppIni": { "Patches.StblBucketLayoutTuning": 0 }, "run": { "menuMemory": { "waitS": 90 } }, "timeoutMin": 15 },
  { "id": "menu-s3pp-noarena", "note": "AllocatorArenaTuning off: its share of committed / largest free", "asi": "tested", "save": "menu",
    "s3ppIni": { "Patches.AllocatorArenaTuning": 0 }, "run": { "menuMemory": { "waitS": 90 } }, "timeoutMin": 15 }
]
```

**C. #29 vs `LocalizedStringCache`** (the one "prefer ours" decision; after #29 is built and passes its offline test). Three arms on one save, labels then cost. `<StringIndex>` stands for #29's ts3native section, named when it is built:

```json
[
  { "id": "bp-strings-s3pp", "note": "S3PP LSC on, #29 off: labels + string IC cost", "asi": "tested", "save": "Baseline-BP",
    "run": { "repro": "s3pp-label-check" }, "timeoutMin": 25 },
  { "id": "bp-strings-ours", "note": "#29 on, S3PP LSC off: labels must match bp-strings-s3pp", "asi": "<#29 build>", "save": "Baseline-BP",
    "ini": { "<StringIndex>.Enabled": 1 }, "s3ppIni": { "Patches.LocalizedStringCache": 0 },
    "run": { "repro": "s3pp-label-check" }, "timeoutMin": 25 },
  { "id": "bp-strings-none", "note": "neither: game baseline for both", "asi": "<#29 build>", "save": "Baseline-BP",
    "s3ppIni": { "Patches.LocalizedStringCache": 0 }, "run": { "repro": "s3pp-label-check" }, "timeoutMin": 25 }
]
```

Then the same three arms with `"run": { "profile": { "minutes": 10 } }` (after the sampler fix) for the `IC_LocalizedStringService_*` managed self % and #29's tables-probed counter, plus one travel or world switch in the ours arm (the case S3PP's cache can get stale on). Ours wins if labels are identical (hash, empty, mismatch, `hash_empty`) and its IC cost is at or below `bp-strings-s3pp`. Ship = installer item 2.

**D. Thread stack diet** (docs/66 rank 2). Memory first, then stability:

```json
[
  { "id": "menu-tsd-on", "note": "ThreadStackDiet on (S3SS stack fix stays on): reserved-only and largest free vs menu-s3pp-all", "asi": "tested", "save": "menu",
    "s3ppIni": { "Patches.ThreadStackDiet": 1 }, "run": { "menuMemory": { "waitS": 90 } }, "timeoutMin": 15 },
  { "id": "bp-tsd-soak30", "note": "ThreadStackDiet on: 30-min soak, no 0xC00000FD", "asi": "tested", "save": "Baseline-BP",
    "s3ppIni": { "Patches.ThreadStackDiet": 1 }, "run": { "soak": { "minutes": 30 } }, "timeoutMin": 50 }
]
```

If both pass, the save matrix with `Patches.ThreadStackDiet: 1` (save-path rule), then installer item 5. Check the log for `[TSD] ... armed`.

**E. `[Alloc]` with and without the arena change** (only after the docs/67 double-free fix; "both compatible" check, not a replacement):

```json
[
  { "id": "bp-alloc-arena-on", "note": "[Alloc] on, S3PP arena 4/1 MB (installed)", "asi": "<fixed alloc build>", "save": "Baseline-BP",
    "ini": { "Alloc.Enabled": 1 }, "run": { "soak": { "minutes": 30 } }, "timeoutMin": 50 },
  { "id": "bp-alloc-arena-off", "note": "[Alloc] on, S3PP arena back to 16/4 MB: largest free and alloc counters", "asi": "<fixed alloc build>", "save": "Baseline-BP",
    "ini": { "Alloc.Enabled": 1 }, "s3ppIni": { "Patches.AllocatorArenaTuning": 0 }, "run": { "soak": { "minutes": 30 } }, "timeoutMin": 50 }
]
```

No A/B is needed for `PackageIndexWarmup` or `ShortWaitSpin` (no fix of ours on the same problem).

---

## 4. S3PP behaviour worth adopting as our own design (ideas only)

Our own implementations later; nothing here is to be copied from the GPLv3 source.

1. **Self-test, then roll back** (STBL, MTEH): after patching, run the patched code once on a synthetic input and restore the original bytes if the answer is wrong. ts3native checks bytes before writing but has no post-patch check; the emulator harness (docs/63) is the offline half of this, a start-up self-test would be the in-game half.
2. **Generation stamps on hooked mutators** (MTEH): every hooked insert/remove/destroy bumps a counter; a cache answers only while generation and table shape match, and a rebuild is thrown away if the counter moved meanwhile. Fits #22 (object id hash) and any cache in front of an unlocked game table, and the worker side of `[MonoThread]`.
3. **Change only the default case** (ThreadStackDiet: only stack size 0): a narrow rule for docs/66's own stack cap if we write one.
4. **Refuse a late install** (STBL refuses once the string service exists): a patch that changes a data layout must check that no object of the old layout exists yet. Relevant to anything we might install after start-up, which docs/68 §6.5 already forbids.
5. **Stall log without a debugger** (`[SIMSTALL]` from a polling thread every 250 ms): a player-side stall report that does not need Frida, for bug reports from other players.
6. **Bounded cache with periodic stats** (LSC, ISC: fixed cap, clear at 75% full, one stats line every 10 min): simple memory bound plus a field report of hit rates.
7. **Warm only what the game will read, at low priority** (PackageIndexWarmup reads `Resource.cfg` roots and `DCCache`, index blocks only, below-normal priority): a model for #14 (parallel package index load).

Where S3PP is weaker and ours should differ: LSC's invalidation keys on the table-list head, which need not change when a table is added; #29 rebuilds on the table-list rebuild instead (docs/54).
