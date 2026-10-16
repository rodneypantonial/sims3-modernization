# 66. Address-space budget (4 GB, LAA)

What fills TS3W.exe's 4 GB address space at the main menu and in a typical Bridgeport (BP) live session, by region type and by owner, with the top shrink candidates. Data and static work only: no game run for this doc. Every number comes from existing files (`probe-out/queue/*.jsonl`, `probe-out/*/memory.csv`, `probe-out/phase0/*`, installed ini files) or from earlier docs, and is tagged.

Rebuild the measured tables from the files on disk:

```
node tools/harness/addr-budget.ts                      # markdown tables
node tools/harness/addr-budget.ts --json               # raw medians
node tools/harness/addr-budget.ts --probe-out <dir>    # e.g. the main checkout's probe-out from a worktree
```

Setup behind the data: Steam 1.67, DXVK 3.1.1 x32, S3SS 1.6.3 (Mimalloc, OversizedThreadStackFix on, WorldCacheSizePatch off), S3PP 2.1 (StblBucketLayoutTuning and AllocatorArenaTuning on, ThreadStackDiet off), ts3native `[GC] InitialHeapMB=352`, `GrowthStepMB=32`, Rodney's CC, Frida probe attached [verified: `probe-out/queue/bin-before/*.ini`, `S3SS.toml`].

## 1. Summary

- At the main menu 1.81 GB is committed and the largest free block is 1.69 GB. In BP live mode it starts at 2.18 GB committed / 1.53 GB largest free and after 7-30 min at speed 3 it is 2.28 GB / 1.34 GB [measured, medians]. The 30-min soak (M3) went from 1,485 to 1,229 MB largest free [measured, PLAN M3]. Error 12 is not close in these sessions; for scale, vanilla without DXVK was 3,490 MB committed and 176 MB largest free after a load (docs/05) [measured].
- Image (426 MB) is the same at the menu and in live mode and cannot be shrunk by us. Mapped memory (96-215 MB, p10-p90) is mostly DXVK's texture window.
- Private memory is the budget that moves: ~1.49 GB at the menu, ~1.84 GB at live start, ~2.04 GB after a session. Of that, ~900 MB has a known owner (Boehm GC heap 352-360, ResourceCache held ~360, stacks ~140, plus small fixed tables). About **940-1,135 MB has no owner yet**: the rest of EA's GeneralAllocator heaps, S3SS mimalloc arenas, string tables, the CAS part cache, SpeedTree and the DXVK/NVIDIA driver's private memory [computed remainder]. That remainder is also where the session growth (+195 MB start to end) lands.
- Not worth shrinking (already measured): ResourceCache budget (Phase 0: `CacheBudget` 0, 50 or 200 MB all hold ~400 MB), Sims (M11: ~0.15 MB each), camera pan/lot streaming (M10: +22 MB, returned), AnimationCache (3-10 MB), glyph cache (<= 1 MB), catalog (~8 MB).

## 2. Region types (measured)

Medians over the queue jobs that have `/memory` before and after: 1 menu job (`lane-g menu-memory`), 9 BP jobs (verify, lotstream, lane-G soak/save/travel; "start" = right after the load, "after" = 7-30 min at speed 3). The probe walks the whole range with `VirtualQuery` (`tools/probe/agent.js` `addressSpace()`); image/mapped/private include reserved pages, so `commit + reserved-only = image + mapped + private` and `that + free = 4,096 MB`.

| Region (MB) | Main menu | BP live, start | BP live, after 7-30 min | Tag |
|---|---|---|---|---|
| Committed | 1,808 | 2,184 | 2,284 | [measured] |
| Reserved, not committed | 326 | 264 | 313 | [measured] |
| Image (exe + DLLs + asi) | 426 | 426 | 426 | [measured] |
| Mapped (file mappings) | 219 | 178 | 145 | [measured] |
| Private (heaps, stacks, GC, driver) | 1,489 | 1,844 | 2,039 | [measured] |
| Free | 1,962 | 1,646 | 1,472 | [measured] |
| **Largest free block** | **1,693** | **1,533** | **1,340** | [measured] |
| Fragmentation (free - largest free) | 269 | 114 | 132 | [measured] |
| Threads (Frida-visible) | ~137 | ~140 | ~140 | [measured, memory.csv] |

Spread over all 147 `memory.csv` runs (9,197 samples, 5,017 in live mode, mixed worlds and configs, DXVK in all): live largest free p10 1,243 / median 1,462 MB; mapped p10-p90 96-215 MB, max 256; image 383-461 MB (older builds, other mod sets); one run (2026-10-11 18:01 PHT) is the docs/05 vanilla run at 3,507 MB commit and 176 MB largest free [measured]. "Menu-like" samples (commit 1.6-1.95 GB, private < 1.6 GB) have median largest free 1,821 MB, so the single menu job is on the low side [measured].

## 3. Owners

### 3.1 Image, 426 MB

| Owner | MB | Tag |
|---|---|---|
| `TS3W.exe` (SizeOfImage; Mono and Boehm are linked in) | 14.4 | [verified PE header] |
| Mods and wrappers in `Game\Bin`: DXVK `d3d9.dll` 7.5, `Sims3SettingsSetter.asi` 1.6, `Sims3Performance.asi` 0.6, `ts3native.asi` 0.2, `MonoPatcher.asi` 0.1, `wininet.dll` proxy 2.9 | ~13 | [verified headers; loaded inferred] |
| `EAWebkit.dll` | 3.7 | [verified header; loaded inferred] |
| NVIDIA 32-bit Vulkan driver: `nvoglv32.dll` 25.8, `nvgpucomp32.dll` 85.3 | ~111 | [verified file sizes; loaded inferred] |
| `d3dx9_31.dll`, Windows system DLLs, Frida agent (probe only) | ~285 | [computed remainder] |

No module list is in the existing data, so the split is from file headers. The image total does not change between menu and live mode and none of it is ours to shrink. A `/modules` probe readout (name, base, size) would turn the remainder into numbers and show how much the Frida agent adds to every measurement.

### 3.2 Mapped, 145-219 MB

| Owner | MB | Tag |
|---|---|---|
| DXVK `MemoryFilePool`: system copies of `MANAGED` textures and static buffers in 16 MiB file-mapping chunks, at most `d3d9.textureMemory` = 100 MB mapped at a time | <= 100 | [verified DXVK source, docs/40, docs/08] |
| Other DXVK mappings, the exe's own `CreateFileMappingA` at `0xEA4E9F`, NLS/locale and font files | rest | [inferred, no per-owner data] |

Mapped is highest at the menu (219) and lower later in live mode (145), so the menu UI textures are the likely peak [inferred].

### 3.3 Private, BP live (~1,844 MB at start, ~2,039 MB after)

| Owner | MB | Tag |
|---|---|---|
| Boehm GC heap (Mono). One contiguous block at Mono start from ts3native `InitialHeapMB=352`, grows in 32 MB steps, never returned | 352 at the menu, ~360 in BP (151 MB of it free at 6 min) | [measured config; heap size measured docs/04 F15] |
| ResourceCache held bytes (`vfMemResCacheBytes`, Verify.Memory) | median 359 (305-421 over the verify jobs); phase0 388-500 | [measured] |
| AnimationCache (budget forced 15 MB) | 3-10, median 4 | [measured, phase0 `animBytesMB`] |
| Thread stacks: ~140 threads x 1 MB reserve (PE `SizeOfStackReserve` 1 MB, commit 4 KB); S3SS gives file-watcher threads 64 KB, ts3native workers 256 KB | ~140, mostly reserved-only | [threads measured, sizes verified; total inferred] |
| Package indexes (EA ~15, CC ~3.5) | ~18.5 | [computed, docs/08] |
| Catalog: every OBJD decoded once at catalog load, 576 B each x 14,577 | ~8 | [inferred, docs/08 H1, docs/39] |
| Shaders kept: SM3 + shadow mode 1 subset, 8.7 MB bytecode + 3.8 MB DBUF; DXVK SPIR-V per created shader on top | ~12.5 + DXVK | [measured file scan, docs/53; kept subset inferred] |
| Glyph cache (UI font atlas, 4 pages x 512 x 512 x 1 B) | <= 1 | [verified bound, docs/54] |
| String tables (STBL bucket arrays) | ~100 stock with the store set; S3PP `StblBucketLayoutTuning` (on here) cuts most of it (S3PP on: -71 MB commit, +64 MB largest free) | [inferred docs/54; S3PP effect measured docs/17 §9] |
| S3SS mimalloc arenas (all MSVCR80 `malloc` users) | 64 MiB reserve per arena, 128 MiB from the 9th (the one-time 128 MB block on the first CC outfit build) | [arena sizes verified in source; count and owner of the 128 MB block inferred, PLAN M9] |
| EA GeneralAllocator (dlmalloc 2.7 cores via `VirtualAlloc`, S3PP cores 4 MB reserve / 1 MB commit) | holds the ResourceCache contents and most engine objects | [verified code, docs/15 §8; size not measured] |
| CAS part cache (built for every CASP at startup, 5,662 EA + 2,016 CC) | not measured | - |
| SpeedTree (`vfMemSpeedTreeBytes` reads -1: counter not wired) | small per model, up to 8 MB per cluster batch buffer | [inferred, docs/34] |
| DXVK and NVIDIA driver private memory | not measured | - |
| **Unattributed remainder** (private - GC - ResourceCache - AnimationCache - stacks - indexes - catalog - shaders - glyphs) | **~940 at start, ~1,135 after** | [computed] |

Caveat on the ResourceCache row: a texture reports its pixel bytes to the cache only when it has a system copy (`0x619DB0`, docs/40), and under DXVK that copy lives in the mapped `MemoryFilePool`, not in private memory. So part of the 359 MB is mapped, and the remainder above is a lower bound [inferred].

### 3.4 Reserved but unused, 264-326 MB

Thread stack reserves (~130 MB of the ~140), the uncommitted tails of mimalloc arenas and EA cores, and Boehm's reserve. The biggest reserved-only blocks in the verify runs are 54-55 MB, 29.6, 18.7, 15.6 and 8 MB with no image owner [measured `biggestReservedOnly`]; the 54 MB one sits low in the address space and fits a partly committed 64 MiB mimalloc arena [inferred].

### 3.5 Fragmentation

The largest free block is one high region (base `0x9EFC0000`-`0xA4E51000` in the verify runs) [measured]. Free outside it is 114-132 MB in live mode and 269 MB at the menu. In the M3 soak the largest block fell 256 MB in 30 min while private rose 302 MB: growth lands in the big block, it does not scatter [measured, PLAN M3]. Allocations that need a fresh contiguous region are the ones Error 12 kills first: Boehm growth (32 MB steps), mimalloc arenas (64/128 MiB), DXVK chunks (16 MiB) [verified sizes].

## 4. Top 5 shrink candidates

| # | Candidate | Expected saving | Risk | How to measure |
|---|---|---|---|---|
| 1 | **Right-size the Boehm initial heap** (`InitialHeapMB` 352 -> peak heap + margin, e.g. 256) | up to ~96 MB committed and largest free block (F15: 151 MB of the 360 MB heap free at 6 min) [inferred] | **Medium:** forced GCs come back if the heap is too small; F15 measured 128 -> 1 forced GCs with 352, so this trades against stutter (the priority). Only worth it if Error 12 shows up | First read the real peak: add `GC_heapsize` (`0x12225A0`) and free bytes (`0x12225B4`) to Verify.Memory and log them over a 30-min BP soak. Then a restart A/B (the heap size is fixed at start): BP profile 10 min x 2 per setting, forced GC count, hitches/s and `/memory` largest free |
| 2 | **Smaller thread stacks** (~140 threads at 1 MB reserve): S3PP `ThreadStackDiet` 512 KB, or our own `_beginthreadex`/`CreateThread` stack-size cap for the engine's worker threads | ~70 MB reserved-only, mostly largest free block [community figure, docs/07] | **Low-medium:** a deep-recursion thread overflows its stack = crash. Today off because S3SS's fix covers the file-watcher threads | Add a per-thread stack readout (TEB stack base vs `AllocationBase`) to the probe to get the real reserve per thread. Then the `menu-memory` job with `ThreadStackDiet=1` vs 0 (reserved-only, largest free), plus a 30-min soak and the save matrix for crashes |
| 3 | **mimalloc `arena_reserve` 64 -> 32 MiB** (S3SS hard-codes 64 MiB; the 9th arena becomes 128 MiB) | <= 64 MB of largest free block early in a session (the 128 MB block on the first CC outfit build) [inferred, PLAN M9] | **Low** (more arenas, tiny bookkeeping), but it needs an S3SS option: ask upstream, we do not patch their binary | `/mallochook` once plus `/allocations?minMB=32` diff in the `store-items-memory` repro (already prints the count): the 128 MB block should become 64 MB |
| 4 | **DXVK `d3d9.textureMemory` 100 -> 64 MB** (`dxvk.conf`) | <= ~36 MB mapped, and a lower mapped peak (max 256 MB seen) [inferred] | **Low crash risk, possible stutter:** more chunk map/unmap when the texture working set is larger than the window | Restart A/B per world (dxvk.conf is read at start): `mappedMB` and largest free from `/memory`, hitches/s from the profile. Stop if hitches rise |
| 5 | **Find the session grower** inside the unattributed ~1 GB (+195 MB private start to end; M3 +302 MB in 30 min) | unknown, up to ~200 MB if one owner leaks or over-caches [inferred] | None for the measurement; the fix depends on the owner | Periodic `/allocations?minMB=4` snapshots during `bp-m3-soak30` (every 5 min), grouped by allocation base and type, to split Boehm growth steps vs new mimalloc arenas vs EA cores vs driver blocks. Wire `vfMemSpeedTreeBytes` (reads -1) and add a CAS part cache byte counter while at it |

Ranked by expected MB times confidence. Candidates 1 and 2 are the only ones with a known size above 50 MB; 5 is the largest unknown.

## 5. Open measurements

- Module list (`/modules`) to split the 285 MB image remainder and size the Frida agent's own cost.
- Boehm `GC_heapsize` readout (candidate 1) and per-thread stack reserve (candidate 2).
- Menu-side owners: ResourceCache held and the CAS part cache at the menu (the menu job has no native counters).
- Largest free block during a save (M3 open item).
