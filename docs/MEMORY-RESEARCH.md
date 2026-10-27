# Memory and Error 12 research: start here

This page is the entry point for anyone who wants to help with The Sims 3's memory problems (Error 12, crashes on save or travel with heavy custom content). It sums up what we know, what we measured, which tools exist, how to run them, and what is still open. Every claim links to the doc that holds the evidence.

Tags used below, as in the rest of the repo: **[measured]** = read from a game run, **[verified]** = checked in the disassembly or the code, **[inferred]** = our best reading, not yet confirmed.

## 1. The problem in plain English

- `TS3W.exe` is a **32-bit program**. With the Large Address Aware flag it can use at most 4 GB of *address space*, no matter how much RAM the PC has. 32 GB of RAM does not help the game itself (only the Windows file cache).
- Error 12 and most "out of memory" crashes happen when the game asks for a **contiguous block** of address space and no free gap is big enough. So the number that matters is not "free memory" or RAM use, it is the **largest free block** (`big` in our logs).
- Address space is used by: the exe and DLL images (~426 MB, fixed), mapped files (mostly DXVK's texture window), and private memory (game heaps, Mono's garbage-collected heap, thread stacks, caches, the GPU driver). See [docs/66 §1-3](66-address-space-budget.md).
- **Fragmentation vs RAM:** saving RAM or "commit" without giving back address space does nothing for Error 12. Many ideas fail this test (see §4). Any memory idea should say up front whether it frees *address space* or only RAM.
- DXVK is the single biggest win already known: on a fresh save the largest free block goes from 176 MB (vanilla, crashes) to ~1,850 MB [measured, [docs/05](05-baseline-results.md)]. Everything below is about the remaining slow loss during play, which is what heavy-CC players still hit.

## 2. What we found so far

### 2.1 Where the address space goes [measured, docs/201 §13]

From the native allocation-site tool on Bridgeport with ~2,000 synthetic CC items ([docs/201 §13](201-alloc-sites.md)):

- **No per-object leak and no "swiss cheese" fragmentation.** The largest free block is one high gap; every new reservation over ~8 MB is cut from its bottom edge. Every other free hole is under ~9 MB.
- The space goes to **permanent reservations that grow to each new peak and are never given back**:
  - **EA heap cores** (EA's GeneralAllocator): +96 MB over three CC cycles. The peaks come from resource streams (`EAStreamMemory`), package decompression (`PackedStream`) and animation clips (`ClipResource`). Cores are never released [verified, docs/201 §12.2].
  - **NVIDIA/DXVK driver reserve:** 128 MB blocks from `nvoglv32.dll` (NVIDIA's 32-bit Vulkan driver under DXVK), up to 2 x 128 MB per session, the largest single step. Accepted as the price of DXVK (which saves ~1.75 GB); not investigated further.
  - **Mono's garbage-collected heap (Boehm):** +35 MB.
  - **The `Scripting/Simulator` Mono sub-heap:** +24 MB over CC cycles, ~+13 MB per travel trip (1 trip measured). This one is in our area.
  - **Untracked 16-32 MB private blocks** (no known owner yet), in Build mode and at the save after travel; plus +64 MB untracked at load.
- **Buy mode cycles lose nothing**; Build mode cycles lose 32/16/16 MB, all to untracked blocks.
- **Heap-level leak candidate:** `CAS/GeometryResource` grows ~5 MB per live CC cycle without settling [inferred: only 3 cycles measured].
- **Travel:** one round trip cost ~21 MB, mostly at the save after coming home, not the ~80 MB first guessed [measured, 1 trip of 3].
- Map views (DXVK texture data) are a bounded cache with no trend [measured].

### 2.2 Mono / managed side [measured, docs/199 §7.2]

- The managed heap does **not** leak in CC cycles: live managed bytes are ~80-85 MB and grow ~+1 MB per cycle after the first fill; no class grows across cycles ([docs/199 §7.2](199-type-growth-and-sqrt-heap.md)). The Mono heap stays at its start size (353 -> 360 MB), so managed growth costs no address space.
- "Destroyed but still alive" game objects rise by a few per cycle (tens in total, game objects, not CC): no measurable address-space share [inferred].
- **First real lever: `[GC] InitialHeapMB=160`** instead of our default 352. One load each: ~160 MB less heap reserved and ~**+200 MB largest free block**, with ~1.5x forced collections and no more GC-led hitches in that run [measured, one load per arm, docs/199 §7]. Not yet a default: needs a second load per arm and a natural-play hitch read.

### 2.3 CC volume and repeat cycles

- **Per-CC load cost:** first reads put it at roughly 15-25 MB of address space per 1,000 synthetic CC items at load [rough, from the CC smoke and stress runs; the `cc-count-load` queue that measures it properly has not run yet, [docs/200 §12](200-cc-build-edit-town-stress.md)].
- **Removal never gives the largest free block back:** removing all placed CC returns 0-25 MB of free space and none of the largest block [measured, docs/199 §7]. Largest free drops in one-time jumps of 160-220 MB per session, not a steady per-item leak.
- **Steady-state test, same items vs fresh items (6 cycles each, [docs/200 §11](200-cc-build-edit-town-stress.md)):** both jobs passed on 2026-10-26; largest free before -> after the job: same set 1,485 -> 1,209 MB (-276), fresh set 1,429 -> 1,086 MB (-343) [measured, queue summary only: [results](data/results/2026-10-26-queue-steady-state.md)]. The per-cycle read (`memory-report.ts --steady lss|lsf`) is **not done yet**: that is what tells a per-instantiation leak (both arms) from a cache keyed by item (fresh only).
- Makeup and object design presets: flat [measured, [results](data/results/2026-10-26-queue-cc-makeup-presets.md)].

### 2.4 Ideas tested and dropped

| Idea | Why dropped |
|---|---|
| `[GC] SqrtHeapLimit` (MemBalancer-style square-root heap rule) | Never acts: the live managed heap (80-93 MB) is far below the floor, so the rule never grows or shrinks anything [measured, docs/199 §7]. |
| Giving Boehm heap pages back (decommit) | Saves RAM / commit only; address space is unchanged. Releasing whole sections has ~0 yield [verified, [docs/198](198-gc-heap-return-and-rctrim.md)]. |
| ResourceCache budget changes | The cache holds ~360-400 MB whatever the budget [measured, docs/66 §1]. `[ResourceCache] TrimAfterCleanup` (docs/198 §4) is built, off, and not measured. |
| Managed class leak hunting for Error 12 | No class leak found; Mono heap flat (§2.2). Error 12 work stays native. |

### 2.5 Side finding: a bug in EA's embedded Mono

While chasing a crash in a crowd test ("Crash S", [docs/67 §22](67-crash-triage.md)), we found that EA's Mono interpreter's `initobj` opcode zeroes **12 bytes into 8-byte `Int64` / `UInt64` / `Double` slots** (e.g. in `List<T>.RemoveAt`), overwriting the next 4 bytes of the stack frame [verified, docs/67 §22.9]. Fix `[ArenaGuard] InitobjFix` (ts3native 0.36.102, off until its runs pass). Worth knowing if you see random managed corruption.

## 3. Tools

All memory tools are in `ts3native` (the native ASI plugin) behind `[MemDiag]` switches in `ts3native.ini`, plus report scripts in `tools/`. Test-run tools are **off by default**; only `Light` is on in the release ini.

| Tool | What it does | Doc |
|---|---|---|
| `[MemDiag] Light=1` (on in releases) | `diag: mem` lines at load, save, every 5 min and exit: commit, private, free, **largest free (`big`)**, images, maps, stacks, Mono heap, ResourceCache, EA direct blocks; `diag: memsus` suspect counters and destroyed-but-alive objects by type | [docs/191](191-memory-leak-plan.md), [docs/188](188-user-diagnostics.md) |
| `[MemDiag] AllocSites=1` | Native address space by call site: EA heap entries + ntdll reserve / map hooks; top live sites, growth since the last same mark, who splits the largest free block, untracked blocks (`usum` / `untr`) | [docs/201](201-alloc-sites.md) |
| `[MemDiag] TypeGrowth=1` | Mono heap census per class at each GC (Cork-style), with holders; test runs only (graph passes cost 200-415 ms) | [docs/199](199-type-growth-and-sqrt-heap.md) |
| `tools/memory-report.ts <ts3native.log>` | Per-load and over-time report, leak / cache / flat verdict per metric; `--steady <prefix>` for repeat cycles; `--self-check` | [TESTING-AND-BUG-HUNTING](TESTING-AND-BUG-HUNTING.md) |
| `tools/mem-report.ts` | Merge `diag: mem` lines into a small database and print growth per field / per event | [docs/191](191-memory-leak-plan.md) |
| `tools/cc-count-report.ts` | Address-space cost per 1,000 CC items at load, by owner, from the `cc-count-load` queue | [docs/200 §12](200-cc-build-edit-town-stress.md) |
| `tools/cc-gen.ts`, `cc-gen-script.ts`, `cc-lot-gen.ts` | Synthetic CC generators (objects, CAS parts, build items, scripted CC, CC library lots) from **your own** game install. Output stays in the git-ignored `game-copy/cc-synth/` and is never published (EA-derived bytes) | [TESTING-AND-BUG-HUNTING](TESTING-AND-BUG-HUNTING.md#synthetic-custom-content-for-stress-tests), [docs/200](200-cc-build-edit-town-stress.md) |
| Static scans | `tools/script-leak-scan.ts` (script DLLs + mods), native cache candidates | [docs/192](192-script-leak-scan.md), [docs/193](193-native-leak-scan.md) |

**Stress repros and queues** (`tools/harness/queues/`): each drives the game with the test agent through full / empty cycles with a forced GC after every mark, so growth that survives removal stands out.

| Queue | Measures |
|---|---|
| `alloc-sites4.json` | Live CC cycles, 3 travel trips, Buy/Build cycles with AllocSites v4 (names untracked blocks) |
| `steady-state.json` | Same items vs fresh items, 6 cycles each |
| `cc-count-load.json` | Load cost with no CC, 1k, 2k synthetic items and a real CC set |
| `cc-build-stress.json`, `cc-build-items.json` | Live, Buy/Build and Edit Town cycles; build patterns, a 3-floor building, doors and stairs |
| `cc-script.json` | Scripted CC (one script assembly per item) |
| `cc-lots.json` | CC library lots placed and bulldozed in Edit Town |
| `mem-exp.json`, `typegrowth2.json` | `InitialHeapMB=160`, SqrtHeapLimit, TypeGrowth |

Run results land in [`docs/data/results/`](data/results/) (`*-queue-alloc-sites*`, `*-steady-state`, `*-cc-*`, `*-mem-*`, `*-typegrowth2`). Those files hold the pass/fail table and before/after commit / private / largest free per job; the detailed numbers come from the job's `ts3native.log` through `memory-report.ts`.

## 4. How to run them

1. **Game and setup:** The Sims 3 **Steam, patch 1.67** (our native code checks the exe and switches itself off on anything else), DXVK x32 `d3d9.dll`, the community baseline layer. Details: [docs/REQUIREMENTS.md](REQUIREMENTS.md), [docs/BUILD.md](BUILD.md), [docs/02](02-baseline-install-guide.md).
2. **Harness:** read [docs/TESTING-AND-BUG-HUNTING.md](TESTING-AND-BUG-HUNTING.md) first (probe, test agent, queue runner, repros), then [docs/TESTING.md](TESTING.md). Build the test agent with `node tools/harness/build-agent.ts`.
3. **Test saves:** the queues load fixed saves (e.g. a Bridgeport save). Make your own with `tools/harness/new-save.ts` / `age-save.ts` and name how you made it ([docs/23 #1](23-ideas-and-roadmap.md)); saves are never committed.
4. **CC sets:** generate with `node tools/cc-gen.ts ...` into `game-copy/cc-synth/<set>`; a queue job's `ccSet` field moves the set into `Mods/Packages/cc-synth` for that job and back after. After an interrupted run: `node tools/harness/queue.ts --restore-content`.
5. **Run:** `node tools/harness/queue.ts tools/harness/queues/<queue>.json` (dry run first: it prints every job). Back up `Documents\Electronic Arts\The Sims 3\` before any test build.
6. **Read:** `node tools/memory-report.ts <job ts3native.log>` (add `--steady <mark prefix>` for cycle tests), `node tools/cc-count-report.ts` for the CC count queue.

Only address-space effects count as gains: report `free`, `big` (largest free) and which owner moved, at the same mark in each cycle, after the forced GC.

## 5. Who owns what

- **Apex Radiance** ([loinyx/Sims3-ApexRadiance](https://github.com/loinyx/Sims3-ApexRadiance)) owns the engine side: EA's allocator, the resource cache, the memory budget and visuals. Fixes there (EA heap cores, ResourceCache, `CAS/GeometryResource`) go to Apex as data and PRs, not into our plugin. Our overlap map with Apex: [docs/187](187-apex-compat-map.md).
- **This project** owns the Mono / script side: the Boehm heap (`InitialHeapMB`), the `Scripting/Simulator` sub-heap, script and mod leaks (mod fixes go upstream as patches, [docs/184](184-nraas-source-crossref.md)), and our own threads and caches.
- **Third party:** the NVIDIA/DXVK driver reserve (accepted cost).
- **Unknown:** the untracked 16-32 MB blocks: naming them is task 1 below.

## 6. Open questions and good first tasks

1. **Name the untracked 16-32 MB blocks** (Build mode, save after travel, +64 MB at load). AllocSites v4 prints `untr` rows with type, owner guess and position; `alloc-sites4.json` ran on 2026-10-26 (all 3 jobs PASS, [results](data/results/2026-10-26-queue-alloc-sites4.md)) and its logs are **not read yet**. Read the `untr` / `usum` lines and name the owners.
2. **Steady state, same vs fresh:** read the per-cycle `--steady lss` / `lsf` results of the 2026-10-26 run (§2.3) and say: leak per instantiation, cache per item, or settling.
3. **`InitialHeapMB=160` second load + hitch read:** repeat both arms (352 vs 160) with a second load each, plus a natural-play hitch read (`hitch-ab.ts`), before it becomes a default.
4. **`cc-count-load` with real CC:** run `cc-count-load.json` (none / 1k / 2k synthetic / a real CC set) and turn the rough 15-25 MB per 1,000 items into a per-owner number.
5. **`CAS/GeometryResource` over 6+ cycles:** cache that settles or a real leak? Only 3 cycles so far (~5 MB per cycle). If it leaks, the data goes to Apex.
6. **Travel residue owner:** the 3-trip travel job ran in `alloc-sites4`; check whether the `Scripting/Simulator` sub-heap (+13 MB) and the 16 MB untracked block at the save after home repeat per trip, and who owns them.
7. **DXVK settings for the driver reserve** (`dxvk.conf` chunk / mapped memory) [inferred knob, not checked]: optional, the reserve is accepted.

Before you start, read [CONTRIBUTING.md](../CONTRIBUTING.md) (workflow, results format, PR evidence) and the background docs: [docs/66](66-address-space-budget.md) (address-space budget), [docs/190](190-memory-metrics-spec.md) (metrics spec), [docs/191](191-memory-leak-plan.md) (leak plan), [docs/192](192-script-leak-scan.md) / [docs/193](193-native-leak-scan.md) (static leak scans), [docs/198](198-gc-heap-return-and-rctrim.md), [docs/199](199-type-growth-and-sqrt-heap.md), [docs/200](200-cc-build-edit-town-stress.md), [docs/201](201-alloc-sites.md), [docs/08](08-resource-offload-design.md) / [docs/39](39-resource-system.md) (resource system). Ask in an issue before a big change so two people do not do the same work.
