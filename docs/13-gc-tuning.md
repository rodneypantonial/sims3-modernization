# 13. GC tuning: what triggers forced collections, and what we can change

Scope: the Boehm GC embedded with Mono in TS3W.exe (Steam, base 0x400000). Question: how to cut forced collections (`gcForced`, `gcForcedMs` in `tools/frida/stutter-profile.js`; measured 3-16 ms/s, up to 176 per 10 min, single ones 150-240 ms; docs/04 F5, F13).

Tags: **[code]** = verified in disassembly/decompile or the decompiled C#; **[community]** = reported by others; **[inferred]** = reasoning, to confirm by measurement.

Decompiles from this work: `game-copy/analysis/decomp/gc13/{core,more,more2}.c`, string xrefs in `gc13/xrefs.txt` (git-ignored).

## TL;DR

1. **The GC is incremental, and it is never idle.** `MonoScriptHost` init calls `GC_enable_incremental` unconditionally, and dirty-page tracking uses `GetWriteWatch`. The per-frame `GC_try_to_collect` steps an in-progress collection. When none is running, it **starts a new one at once**; nothing checks how much has been allocated. **[code]** This corrects docs/04 F2, which said the GC is not incremental and that aborted marks are thrown away. In incremental mode, aborted work is kept.
2. **Forced collections come from heap exhaustion.** EA changed `GC_collect_or_expand` so that in incremental mode it never grows the heap while anything has been allocated since the last GC. Instead it runs a full, unbounded, stop-the-world collection, or finishes the running cycle without a time limit. The heap grows only when a forced GC still leaves too little free space, and then by at most 8 MB per step. **[code]** So the heap stays tight, and every time allocation outruns the ~1 ms/frame incremental work, the result is a 100-240 ms hitch. **[inferred, consistent with F5/F13]**
3. **Scripts do not cause it.** There are only three `GC.Collect()` call sites, and none runs during play. **[code]**
4. **The best lever is more heap headroom.** Use `GC_INITIAL_HEAP_SIZE`, which is honoured; the default initial heap is only 40 MB. Then try a larger growth step and the budget ceiling. Measure the heap size first.

## Q1. Boehm settings this exe honours

`GC_init` = `FUN_00e4e160` reads these with MSVCR80 `getenv`, once. It runs from the first GC use, which at the latest is MonoScriptHost init. **[code]**

| Env var | Effect in this build | Global |
|---|---|---|
| `GC_PRINT_STATS` | any value sets `GC_print_stats` (log lines such as "Increasing heap size by %lu after %lu allocated bytes") | `0x11F30B4` |
| `GC_DUMP_REGULARLY`, `GC_FIND_LEAK`, `GC_ALL_INTERIOR_POINTERS`, `GC_PRINT_BACK_HEIGHT`, `GC_NO_BLACKLIST_WARNING` | debug flags. `GC_FIND_LEAK` also blocks incremental mode. Do not use them. | |
| `GC_DONT_GC` | disables collection entirely. Memory runs out within minutes. **Never.** | `0x11F30A8` |
| `GC_PAUSE_TIME_TARGET` | `atol`, must be >= 5. Sets `GC_time_limit` (default **5**, stock Boehm 50). In this build it is only read by `min_bytes_allocd` (`0xE498B0`), to choose between divisor×2 and divisor. EA's own stop function sets the real pause. | `0x1194814` |
| `GC_LARGE_ALLOC_WARN_INTERVAL` | warning throttle only | `0x11949B8` |
| `GC_INITIAL_HEAP_SIZE` | `atoi` in **bytes** (no K/M suffix; values <= 256 KB warn). Converted to 4 KB blocks and passed to one `GC_expand_hp_inner` call. **Default `0x2800` blocks = 40 MB** (`mov ebp,0x2800`, immediate at `0xE4E169`). If the allocation fails, the game exits with "Can't start up: not enough memory". | |
| `GC_MAXIMUM_HEAP_SIZE` | caps growth (`0x12225A4`) and sets `GC_max_retries=2`. At the cap, allocation returns NIL after 2 retries, so Mono gets an out-of-memory error: a **crash risk**. Do not use as a safety cap. | |
| `GC_ENABLE_INCREMENTAL` | redundant: incremental mode is already on (see below) | `0x11F2F9C` |

- **Not present:** `GC_FREE_SPACE_DIVISOR`, `GC_NPROCS`, `GC_MARKERS`, and every parallel-mark string. `GC_free_space_divisor` is hard-coded **5** at `0x1194810` (stock Boehm uses 3). **[code: string scan of the whole exe]**
- **No parallel marking.** There are no marker threads, and `GC_try_to_collect_inner` has no `GC_wait_for_reclaim`. **[code]** Mark work runs only on the thread that triggers it, which is the sim thread or the allocating thread.
- **Incremental mode is on by default.** At `0xD7E602`, inside the MonoScriptHost init function (starts at `0xD7E560`, not defined in Ghidra), there is an unconditional `call 0xE4E450` (= `GC_enable_incremental`). It sets `GC_incremental` (`0x11F2F9C`) = 1 and turns on `GetWriteWatch` dirty bits (`0xE4EFB0`, `GC_gww_read_dirty`). The `GC_no_win32` flag (`0x11F40E4`) is set only on Win9x/Win32s (`0xE4EB70`). **[code]**
- **Lineage:** Boehm 7.1-ish with EA edits. Evidence: `GC_collect_or_expand` has the 7.1 `GC_fo_entries > last + 500` test, plus GWW. Heap-section limit 0x300 (768), at `0xE49B10` and `0xE4EEF0` ("Too many heap sections" abort). With 8 MB steps that limit is out of reach below 4 GB. **[code]**

## Q2. What triggers a "forced" collection

Profiler definition: a `GC_stopped_mark` (`0xE49980`) that runs outside the per-frame `GC_try_to_collect` (`0xE4A050`). Callers that can produce one:

1. **Allocation, when the heap is out of free blocks (main source).** The path is `GC_allocobj` `0xE4A2B0` (small objects) or `GC_alloc_large` `0xE4BA90`, then `GC_collect_or_expand` `0xE4A120`:
   ```c
   if (GC_incremental) {                       // EA edit; stock 7.1 expands here
     if (!GC_dont_gc && GC_words_allocd != 0) { // anything allocated since last GC
       FUN_00e4a320();  // no cycle running: try_to_collect_inner(never_stop) = full STW GC
       return 1;        // else: finish the running cycle with never_stop + full sweep
     }
   }
   ... expand: blocks = heapsize/(4096*5) + needed; if > 0x800 -> clamp to 0x800 (8 MB, MAXHINCR)
   ```
   **[code]** In incremental mode the heap grows only on the second pass, when `words_allocd == 0` right after that forced GC and the GC still did not free enough. So the heap tracks live data closely, and the next exhaustion comes soon. **[inferred]**
2. **`System.GC.Collect()`.** The `InternalCollect` icall (table at `0x10B5838`, index 121) is `0xE786C0`, which jumps to `0xE69650`, which jumps to `GC_gcollect` `0xE4A0B0`. That is a full collection with `never_stop`. **[code]** The C# call sites (`game-copy/decompiled`) are:
   - `DynamicChallengeManager.DestroyChallenges()`: on world quit and on online logout. Not during play.
   - `ReferenceWatchdog.ValidateReferences()`: dead in release. `RegisterObject` is `[Conditional("EA_DEBUG")]` and `Initialize()` is empty.
   - `Commands.OnSacsDump`: the debug cheat `sacsdump`.

   **None runs during play, so a C# patch to remove them would gain nothing.** **[code]** Mods we did not decompile (NRaas etc.) were not checked.
3. **The finalizer-table trigger.** `GC_fo_entries > last + 500` (`0x11F3040` vs `0x11F2FD0`) exists only in the non-incremental branch, so it is inactive here. **[code]**
4. **Inside the per-frame call** (these count as per-frame in the profiler, not forced): after 40 aborted attempts at the final stop-the-world phase (`max_prior_attempts` = `0x1194820` = 40), `GC_collect_a_little_inner` (`0xE49DA0`) finishes with `never_stop`. That is an unbounded pause inside `GC_try_to_collect`, visible as `maxGcMarkMs`. **[code]**

## Q3. The per-frame GC call and its knobs

Each Simulate iteration (`0xD819AA`) calls `GC_try_to_collect(ScriptHost_GcStopFunc 0xD717B0)`. **[code]**

- **The stop function** returns true when `now - start > budget`. The budget is `0x11922A4`: starts at 1000, moves in ±100 steps, range [`0x11923A4`=500, `0x1192324`=1500]. The timer helper `0x4F32C0` is QPC/rdtsc-based; the units are probably µs **[inferred]**. The value is also gated by `0x11EE528`.
- **What one per-frame call does** (`0xE49FB0`, then `0xE49F10`):
  1. If a cycle is running, it loops: check the stop function, then do `GC_mark_some` ×25 (`GC_rate` = `0x119481C`) while the world keeps running. When marking is done, it tries the final stop-the-world mark (re-read dirty pages, rescan roots), which is also abortable.
  2. Otherwise it lazily sweeps (`GC_reclaim_all(stop)`, `0xE50380`, abortable).
  3. Otherwise it **starts a new cycle immediately**: promote black lists, clear all mark bits (unbounded, proportional to heap size), then a stop-the-world mark start. There is no `GC_should_collect` gate. **[code]**
- **The budget adapts through an EA counter** `0x11F2F98` (range ±20, limits `0x11947E8`/`0x11947EC`). `collect_a_little` increments it when a cycle needed >= 20 aborted finish attempts (or forced finish after 40), and decrements it when a cycle finishes in <= 10. Simulate turns +20 into budget +100 and -20 into budget -100. **[code]**
- **Finalizers:** `mono_gc_invoke_finalizers` (`0xE697F0` → `0xE4B3F0`) runs at most `0x1194848` = 50 per call. Simulate counts frames where 50 or more ran. At 200 such frames in a row it drains the whole queue in one frame (`0xD81A1D`, `0xD81A37`). That is the S3SS Finalizer Throttle target. **[code]**

**Patch safety.**
- The three budget globals and `max_prior_attempts` are plain `.data` dwords: file offset = VA - 0x400000 (`.data` raw == virtual), or writable at runtime without `VirtualProtect`. They are read only for timing comparisons, so changing them cannot corrupt the heap. **[code]**
- The 8 MB clamp is three immediates in `.text`: `cmp eax,0x800` at `0xE4A1D8` (imm `0xE4A1D9`), `cmp eax,0x800` at `0xE4A1FF` (imm `0xE4A200`), `mov eax,0x800` at `0xE4A206` (imm `0xE4A207`). Patch all three together. A larger value only changes how much address space each expansion takes. **[code]**
- Do not patch the stop function to "never stop" or remove the per-frame call (see Chunky below).

## Q4. What community fixes change

- **S3SS** (https://github.com/sims3fiend/Sims3SettingsSetter, README "Experimental Patches"; patch details in docs/03 §3, verified from source):
  - *GC Finalizer Throttle*: raises the 200-frame threshold to 0x7FFF and runs one batch of finalizers per frame instead of the drain loop. The README notes it "may slightly increase memory usage on very long play sessions" **[community]**.
  - *Chunky Patch*: NOPs the `GC_try_to_collect` call at `0xD819AA`, "relies on `GC_malloc` to trigger collection". By the code above, every collection would then come through `GC_collect_or_expand`, so each one is a full, unbounded stop-the-world collection. That means less constant per-frame cost but **more and larger forced hitches**. **[code path; effect inferred]** The author's claim that it "dominates simulation thread time" has no published numbers **[community]**.
  - *GC_stop_world early exit*: negligible, by the author's own account **[community]**.
  - *mimalloc*: replaces EA's native allocator. It does not touch the Boehm heap, which `GC_win32_get_mem` (`0xD70900`) allocates on its own **[code]**.
- **No public reports** found of anyone using `GC_INITIAL_HEAP_SIZE` or the other Boehm environment variables with TS3. I also found no measured player reports for the S3SS GC patches. **[searched: GitHub README, web search]**
- Our profiles still show per-frame GC mark time with S3SS installed, so Chunky is not enabled in our baseline. **[inferred]**

## Q5. Recommendations, ranked

Context: ~1.9-2.1 GB committed, largest free block ~1.5 GB. Boehm never returns heap to the OS, so heap size = peak. Every MB of headroom is address space that DXVK, textures and the world cache can no longer use (Error 12 risk).

| # | Change | How | Expected effect | Risk | Measure |
|---|---|---|---|---|---|
| 0 | **Measure the heap first** | `GC_PRINT_STATS=1` in the launch environment. Also add read-only per-second fields to the profiler: `GC_heapsize` `0x12225A0`, large free bytes `0x12225B4`, words allocated since GC `0x12225C4` (×4 = bytes), `GC_gc_no` `0x11F2F90`, budget `0x11922A4`, finish attempts `0x11F2FB4` | Tells us the heap size, allocation rate (MB/s) and cycle length. That decides the sizes for #1 and #2. | None | GC log + profile on BP/SV/IP, x3, 10 min |
| 1 | **Larger initial heap** | `GC_INITIAL_HEAP_SIZE=<bytes>` in TS3W.exe's environment, from the launcher or harness. Steam launch options do not set environment variables on Windows. An ASI could set it only through **msvcr80's** `_putenv`, before GC init. Alternative: patch the immediate at `0xE4E169` (blocks of 4 KB) in a copy, keeping `TS3W.exe.orig`. Try about peak heap + 128 MB, then + 256 MB. | More free space between cycles, so the incremental cycle finishes before allocation runs dry. Should cut `gcForced` most. It also reserves one contiguous block at start-up while the address space is still clean. **[inferred]** | Low. Too large fails at launch (exit(1), no save touched). The cost is only the extra headroom. | `gcForced`/`gcForcedMs` per s, hitches/s, committed memory and largest free block |
| 2 | **Larger growth step** | Patch MAXHINCR `0x800` → `0x2000` (32 MB) at the three immediates above | Fewer back-to-back forced GCs when the heap must grow (after load, travel, big lots) **[inferred]** | Low. Address space grows in 32 MB steps. | same, plus heap-size curve |
| 3 | **Budget ceiling** | `.data` writes: `0x1192324` 1500 → 2500 (maybe start `0x11922A4` at 1500) | Cycles finish in fewer frames, so less exhaustion. Since cycles never idle, this costs about +1 ms per sim iteration. **[inferred]** | Low (timing only). Costs sim-loop time. | `gcMark` ms/s vs `gcForcedMs`, iterations/s |
| 4 | **Use stock growth policy** (experimental) | NOP the `jnz` at `0xE4A128` (`75 78`), so incremental mode collects only when `GC_should_collect()` and otherwise expands | The heap sizes itself to about allocation ≥ scan/10 between GCs: far fewer forced GCs **[inferred]** | Medium. Heap can grow towards the address-space limit. Do not pair with `GC_MAXIMUM_HEAP_SIZE` (OOM → crash). | heap curve over 30+ min, plus everything above |
| 5 | Finalizer drain | Use S3SS *GC Finalizer Throttle*; do not duplicate it | removes rare whole-queue drain spikes | Low [community] | `fin` ms/s, max iteration |

**Not recommended:**
- Chunky Patch: all collections would become forced full collections.
- `GC_DONT_GC`.
- `GC_ENABLE_INCREMENTAL`: already on, no effect.
- `GC_PAUSE_TIME_TARGET`: no effect on pauses in this build.
- `GC_MAXIMUM_HEAP_SIZE`: crash at the cap.
- Removing script `GC.Collect`: no in-play callers.
- Parallel marking: not compiled in.
- Raising `max_prior_attempts` (40): it delays the finish, which means more exhaustion.

**Test protocol:** one change at a time against the F13 all-fixes baseline (same saves, x3, 10 min, `profile-world.ts`). Back up `Documents\Electronic Arts\The Sims 3\` before each binary change. Stop on any crash and triage it against the change ledger.

## Open questions

- The heap size and allocation rate on our saves (#0).
- Whether `0x11EE528`, which gates the stop function, is ever 0. If it is, per-frame GC runs unbounded.
- The cost of `GC_clear_marks` at each cycle start on a large heap: it is unbounded per frame **[inferred]**.
- Whether `IC_GameUtils_GetFrameNumber` really is the ICF-folded `GC_never_stop_func`. The name is a symbol-map artefact; it is passed wherever Boehm passes `never_stop`. **[inferred]**

Sources: decompiles above; `docs/03-prior-art-re.md` §3-4; `docs/04-re-findings.md` F2, F3, F5, F13; S3SS README (https://github.com/sims3fiend/Sims3SettingsSetter); Boehm GC 7.1 `alloc.c` (`GC_collect_or_expand`, `GC_try_to_collect_inner`, `GC_collect_a_little_inner`) for naming.
