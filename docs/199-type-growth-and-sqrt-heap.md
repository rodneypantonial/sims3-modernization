# 199. Boehm heap experiments: Cork type growth and the MemBalancer square-root heap limit

Status: 2026-10-26. Built in ts3native 0.36.81 (`native/ts3native/memexp.cpp`, selftest `memexp-test.h`, counters v137), both switches **off** by default; test asi `game-copy/ts3native.asi.memexp`; queue `tools/harness/queues/mem-exp.json` (dry run only, **not run**). No game run. Static work on copies of Steam 1.67 `TS3W.exe` (Ghidra headless on the `game-copy/ghidra` project, read only; bytes from `game-copy/bin`). Companion to docs/191 (memory plan, step 6: managed census), docs/198 (heap return: Boehm never unmaps), docs/13 (GC triggers), docs/57 / docs/130 §2 (GC map), docs/66 §3.3 / §6.2 (`[GC] InitialHeapMB=352`, `GrowthStepMB=32`). Ledger docs/51 §356.

Tags: **[verified]** read in the disassembly or decompile for this note; **[inferred]** reasoning, to confirm by the queue; **[paper]** taken from the cited paper.

## 1. What and why

- **TypeGrowth** answers docs/191's open "Mono class census" item: which managed classes keep growing across collections, and which classes hold them. Method: Cork (Jump & McKinley, "Cork: Dynamic Memory Leak Detection for Garbage-Collected Languages", POPL 2007; extended "Detecting Memory Leaks in Managed Languages with Cork", Software: Practice and Experience 2009, https://www.cs.utexas.edu/~mckinley/papers/cork-spe-2009.pdf). Cork summarises the live heap per type at each full collection, keeps a type points-from graph and ranks types by how steadily their volume grows (ratio ranking).
- **SqrtHeapLimit** tests whether a principled heap limit cuts the forced full collections docs/13 names as the GC hitch source (EA's `GC_collect_or_expand` collects instead of growing whenever anything was allocated since the last GC). Method: MemBalancer (Kirisame, Shenoy, Panchekha, "Optimal Heap Limits for Reducing Browser Memory Use", OOPSLA 2022, https://arxiv.org/pdf/2204.10455): heap limit `M = L + sqrt(L g / (c s))` [paper Eq. 2] with live bytes `L` after a major GC, allocation rate `g`, GC speed `s` (bytes collected per GC second, GC time modelled as `L / s`) and one tuning constant `c` (the GC-time fraction gained per extra byte at the optimum).

## 2. Hook points [verified unless tagged]

| Site | What | How we use it |
|---|---|---|
| `[0x1234360]` | `GC_notify_event` pointer (Mono libgc event hook): called with 1 after `GC_stop_world` in `GC_stopped_mark 0xE49980`, 2 at its end (also on an aborted mark), **3 at the top of `GC_finish_collection 0xE49A50`** (before `GC_finalize 0xE4AE60` and `GC_start_reclaim 0xE502F0`), 4 at its end; `push n; call eax; add esp,4` (cdecl) | our `MxNotify` handles event 3 |
| `0xD7E62A` | `mov [0x1234360], 0xC0FD60` in MonoScriptHost init; `0xC0FD60` is a bare `ret` | `PatchImm32` on the immediate (`0xD7E630`) to our function; if init already ran and the slot holds `0xC0FD60`, the slot itself is written |
| `0xE49A50` callers | `GC_try_to_collect_inner 0xE49FB0` (full/forced path, right after a completed `GC_stopped_mark`) and `0xE49F10` (the incremental path defers the finish to the next per-frame call: `0xE49DA0` bumps `GC_gc_no` and sets `[0x11F2F94]`, `0xE49F10` then calls `0xE49A50` when `[0x11947E4] != -1`) | event 3 fires once per **completed** cycle, incremental or forced. In this build the incremental GC never idles (docs/13), so "every full GC" = every completed cycle |
| `0xD819AA` | per-frame `call GC_try_to_collect 0xE4A050` (`E8 A1 86 0C 00`, preceded by `push 0xD717B0` stop function) | `PatchCalls` retarget to `MxStep`: timing only (GC work time for `s`) |
| `0xE4A120` | `GC_collect_or_expand(needed blocks, ignore_off_page)`, cdecl, first instruction `cmp dword [0x11F2F9C],0` (7 bytes, absolute operand) | detour (`InstallDetour`, 7 bytes): the SqrtHeapLimit decision, and timing of forced collections |
| `0xE49C80` | `GC_expand_hp_inner(blocks)` cdecl, min 0x40 blocks, honours `GC_max_heapsize` | called by our detour to grow |
| `0xE4B850` | `GC_apply_to_all_blocks(fn, data)`: walks the bottom-index list `[0x11F3054]` (`+0x1000` next, `+0x1008` key), entries `< 0x1000` are free/continuation skips, a header whose `+0x10` (`hb_map`) equals `GC_invalid_map [0x11F30D8]` is a free block | `MxBlocks` re-implements the same walk (so the selftest can run it on a fake heap) |
| headers | `top_index 0x1233318[addr>>22]` -> bottom `[(addr>>12)&0x3FF]`; `+0` size in words (docs/198 §3.2: 6.x layout), `+0xC` descriptor (0 = pointer-free), `+0x10` map (byte offset -> word displacement, 0 = object start), `+0x14` kind (2/3 = uncollectable: marks not cleared), `+0x18` EA's `GC_gc_no` stamp, **`+0x1C` mark bits** (one per word, set only at object starts: `GC_mark_from 0xE4C3E0` sets `hdr[7 + w>>5]`), `+0x9C` EA's free bits (set on free-list objects; the marker skips an object whose bit is set there, the alloc fast path `0xE4C1E0` clears it) | marked object = `+0x1C` bit at its first word; small objects `<= 0x200` words, larger = one object per block run, mark bit 0 (`GC_reclaim_block 0xE50160`) |
| `0xE4C0F0` | `clear_marks_for_block` (via `0xE4C270` at cycle start): stamps `+0x18`, clears `+0x1C` (0x80 bytes) except uncollectable kinds | marks stay valid from mark end until the next cycle start, so event 3 sees them complete even on the deferred-finish path |
| `GC_heapsize [0x12225A0]`, `GC_words_allocd [0x12225C4]`, `GC_words_allocd_before_gc [0x12225C0]` (finish adds `[0x12225C4]` to it after event 3), `GC_incremental [0x11F2F9C]`, `GC_dont_gc [0x11F30A8]`, `GC_gc_no [0x11F2F90]` | allocation total = `([0x12225C0] + [0x12225C4]) * 4` bytes (monotone [inferred]) | `g` |
| MonoObject `+0` vtable, MonoVTable `+0` klass, MonoClass `+0x34` name / `+0x38` namespace, `+0xB8` runtime info (u16 max domain id, vtables from `+4`) | docs/104 §5.1, docs/134, I-123-28 | a word at object `+0` counts as a vtable only if its klass's runtime info lists it (exact identity, no false class) |

`node tools/hook-sites.ts --check`: `0xD819AA` and `0xE4A050` overlap only Apex's developer-mode Frame Profiler (off); `0xE4A050` is also detoured by `[MonoThread] GcLock` (off) [verified]. Our retarget goes to whatever the call targets (byte-checked), so a P3 entry detour still runs. S3SS's Chunky patch NOPs `0xD819AA`; then `PatchCalls` refuses and the log says GC time is undercounted. No other tool hooks `0xE4A120` or the notify slot.

## 3. TypeGrowth design

**Where to collect.** Two options:

1. *Piggyback on the mark phase* (Cork's way: the collector counts each object as it marks it). Here that means a detour inside `GC_mark_from 0xE4C3E0`'s inner loop, the hottest code of the GC, run in 25-step slices from the per-frame budget; every marked object would pay the hook, also in the passes we do not log, and the incremental mark has no single "end" to emit a consistent summary. Rejected.
2. *Separate walk over the marked blocks after mark completion* (chosen). At event 3 the mark bits are complete and stable (no cycle starts before the finish and the sweep), the world is effectively stopped for managed code (single registered thread; M5 off), and a walk costs nothing in passes we skip (`TypeGrowthEvery`). It costs one extra read of every live object's first word plus, for the graph, every word of every live pointer-carrying object.

**Pass** (`MxTypeGrowth`, sim thread inside the collection, every `TypeGrowthEvery`-th completed cycle):

1. Census: for each in-use block, each set mark bit -> object; uncollectable kinds are counted in the marked total but not in the census (host roots, stale marks). Word 0 -> class index via a per-pass vtable hash (64 K slots, 3/4 fill cap) and a persistent klass table (16 K classes). Every pointer read in the vtable check goes through a 4 KB page-readability cache (VirtualQuery once per page per pass), the whole walk under SEH (`tyFaults`).
2. Cork ratio ranking per class [paper, SPE Fig. 5]: growth phase when this pass's bytes > max seen x (1 - f), f = 15%; rank += phases x (this/last - 1) on growth, -= phases x (last/this - 1) on a small shrink; a drop below (1 - f) of the max resets. Candidate = phases >= 2 and rank > `TypeGrowthRankMin` (paper default 100; ranks accumulate over many collections, so a 20 min run may show none: the top list shows rank and phases anyway).
3. Targets: up to `TypeGrowthTopK` (8) classes, ranked candidates first, then the largest growth since the last pass.
4. Points-from walk (one hop of Cork's graph, only into the targets): every word of every marked object in a block with a non-zero descriptor that points **at the start** of a marked object of a target class adds the target's size to edge (source class -> target). Words outside the walk's heap bounds, unaligned, into free/uncollectable blocks, interior pointers and unmarked targets are skipped. If a target's largest holder is an array class (`X[]`), a second walk adds that array class as a target (List<T> -> T[] -> List<T>).
5. Lines: `typegrowth: #N gc= objs= MB= marked= classes= census=<ms> graph=<ms> top=[*]Ns.Class:KB:+dKB:rank:phases,...` (12 by rank) and per target `typegrowth: holders #N Ns.Class KB= <- Holder:KB ... other:KB`. Class names only.

**Conservative caveats.** Boehm marks from a conservative stack and conservative NORMAL-kind objects, so (a) a dead object can be "live" because an integer looks like a pointer to it (counts and ranks slightly high, never low), (b) graph edges may be false: our walk scans every word of typed objects too (not only the descriptor's pointer words), so an integer field equal to a target's address makes a false edge. Only object-start hits count, which removes most false hits [inferred]. An object pointed to by several holders is counted once per edge (Cork's edge volume semantics, not dominator size). Objects allocated between mark end and a deferred finish have no mark bit and are missed until the next pass.

**Cost [inferred, to measure].** Census: one cache-missing read per live object; at ~200 MB live and tens of bytes per object that is millions of reads, tens to a few hundred ms per pass. Graph: every word of the live pointer-carrying heap plus a header lookup per heap-range hit, comparable to a non-incremental full mark (hundreds of ms). Both run inside the collection on the sim thread, so a pass is a hitch. **Test runs only** unless the queue shows <= 2-3% of sim-thread time with `TypeGrowthEvery` large enough; `tyLastUs`/`tyMaxUs`/`tyGraphLastUs` and the `census=`/`graph=` fields give the number; the off arm (`--ab typegrowth` slices) gives the sim-thread share.

## 4. SqrtHeapLimit design

**Measurements per completed cycle (event 3):**

- `L` = marked bytes: popcount of each block's `+0x1C` mark words x object size (large: bit 0 x size); uncollectable blocks included. When TypeGrowth ran that pass its census total is used. Cost: ~92 K headers x 32 mark words at a 360 MB heap, a few ms [inferred; `sqWalkUsLast`/`walkMs`].
- GC time since the previous completed cycle: per-frame `GC_try_to_collect` calls (retarget at `0xD819AA`) + forced paths through `GC_collect_or_expand` (detour), minus our own walk time. Not counted: `GC.Collect()` icall collections (no in-play callers, docs/13 Q2).
- `s` = L / GC seconds, smoothed with β = 0.5, numerator and denominator separately [paper §5].
- `g` = allocated bytes / wall seconds since the previous cycle, smoothed with α = 0.95 **per second** (α^dt per update, so the paper's ~13 s half-life holds at any GC cadence). Deviation: the paper also updates `g` once a second on a heartbeat thread to decay the limit while idle; here updates come only with collections (the incremental GC never idles, and the heap cannot shrink anyway).
- `M = L + max(sqrt(L g / (c s)), SqrtMinExtraMB)`, clamped to [`InitialHeapMB` (floor: the initial section is never smaller), `SqrtCeilingMB` (1024)]. `c` = `SqrtC` per MB (default 0.0005: at L = 250 MB, g = 10 MB/s, s = 750 MB/s the extra is ~58 MB [inferred numbers]). The paper adds a 10 MB nursery term for V8; Boehm here has no nursery, so it is left out.

**Driving the heap.** EA's `GC_collect_or_expand` (incremental mode): if allocation happened since the last GC (`GC_words_allocd != 0`) and GC is enabled, run `0xE4A320` (finish the cycle or a full stop-the-world collection) and return; only otherwise expand (docs/13 Q2). Our detour (mode 1): when `heap + needed*4 KB <= M`, call `GC_expand_hp_inner` with `min(M - heap, GrowthStepMB)` blocks (at least `needed`; retry with `needed` on failure) and return 1, so the allocation proceeds without a forced collection; else the game's code runs unchanged. Above `M` nothing changes: the forced collection happens as today. Mode 2 (observe) computes and logs `M` but never grows. The free-space divisor and the per-frame budget are not touched.

**Shrink side.** docs/198 §3: the build has no unmap code and release of heap sections is rejected; decommit would only save RAM. So the heap is never reduced: `wouldFree` (heap above `M`) is logged only.

**Line** (at most every `SqrtLogSec`, default 10 s): `gcsize: gc= mode= live= limit= heap= alloc=<MB/s> speed=<MB/s> gcMs=<this cycle> grewMB=<since last line> forced=<collect_or_expand calls passed to the game since last line> wouldFree= walkMs=`.

**Risks.** A bigger heap costs address space (Error 12 budget, docs/66) and makes `GC_clear_marks` at each cycle start slower (proportional to heap size, docs/13 open question). The ceiling bounds the first; the queue's largest-free and per-cycle GC ms read both. Growing never changes object lifetimes, so correctness risk is the detour itself (selftest: grow / at-limit / observe / nothing-allocated paths).

## 5. Switches, counters, tools

- ini `[MemDiag] TypeGrowth=0`, `TypeGrowthEvery=1`, `TypeGrowthTopK=8`, `TypeGrowthRankMin=100`; `[GC] SqrtHeapLimit=0` (1 act, 2 observe), `SqrtC=0.0005`, `SqrtCeilingMB=1024`, `SqrtMinExtraMB=16`, `SqrtLogSec=10`. Hooks are installed only when one key is non-zero.
- Counters v137 at `0x52F0` (28 int32): runtime switches `tyMode` (`--ab typegrowth`, `0x52F0`) and `sqMode` (`--ab sqrtheap`, `0x52F4`), `mxReady`, `ty*` pass cost and counts, `sq*` live / limit / heap / rates / grows / forced (docs/registry.md v137; `tools/probe/agent.js`; `tools/harness/native-switches.ts`).
- `node tools/memory-report.ts <ts3native.log>` prints a type-growth section (passes, pass ms, candidates, top classes with first -> last KB and holders) and a GC-size section (live / limit / heap, rates, growth by the rule, forced count, GC ms per cycle).
- Selftest `TestMemExp`: fake bottom index with a free block and a continuation entry, small / large / uncollectable / pointer-free blocks, three classes with runtime info plus a fake vtable that the check must reject; checks marked bytes, census, Cork rank, holders (interior and pointer-free words not counted), the square-root formula and clamps, `MxGrowBlocks`, the detour's four paths, and two whole event-3 passes.

## 6. Queue `tools/harness/queues/mem-exp.json` (not run, ~75 min: 4 x 15 min play + 4 loads, no smoke)

Memory work runs as stress cycles, not natural soaks (Rodney 2026-10-26). Every job runs repro `live-cc-cycle` (docs/200: 3 cycles of 400 synthetic objects placed over the town and up to 150 Sims dressed, then removed and undressed; 100 inventory objects added and removed; `mem_gc` before / after each `live-full` / `live-empty` / `inv-full` / `inv-empty` mark) with `ccSet game-copy/cc-synth/cc-2k`. Timing minutes 45 <= 60, so `queue.ts` derives no measurement smoke. Before a real run: `node tools/harness/queue.ts --restore-content` if the dry run reports a leftover content stash, and make cc-2k with `tools/cc-gen.ts` if missing.

| Job | Setup | Reads |
|---|---|---|
| `bp-cc-typegrowth` | live-cc-cycle 15 min, `TypeGrowth=1` on every GC, no `--ab` slicing (2-min slices would leave cycle marks without a pass), `noiseCheck` false | pass cost from `census=` / `graph=` ms and `tyLastUs` / `tyMaxUs`; classes that grow cycle over cycle at the same mark and survive removal (`-empty` marks), and their holders |
| `bp-cc-sqrtheap-observe` | live-cc-cycle 15 min, `SqrtHeapLimit=2` | baseline L / g / s / limit, forced count, gcMs, heap, hitches |
| `bp-cc-sqrtheap-on` | live-cc-cycle 15 min, `SqrtHeapLimit=1`, same save, seed and repro | vs observe: `hitch-ab.ts` GC pauses (p99 / p99.9 / max, counts >= 100/300/500 ms), forced collections, gcMs per cycle, heap MB and largest free per cycle mark |
| `bp-cc-smallheap` | as `-on` plus `[GC] InitialHeapMB=160` (Error 12 arm). 160: below BP live (~209 MB = 360 heap - 151 free at 6 min, docs/66), above the 40 MB clamp; `InitialHeapMB` is also the rule's floor (§4), so the heap starts small and grows only to M | vs `-on` (352 start): largest free block and address-space free (`diag: mem` lines) per cycle mark; then GC pause hitches, heap MB / `grewMB`, forced count. Pass: no crash, no failed expansion (no own counter: a failed rule grow falls through as a forced collection, so `forced` > 0 with heap < limit is suspect) |

The act vs observe pair runs as two loads: a grown heap never shrinks, so an in-load A/B would leave the off slices on the bigger heap.

## 7. Results

Queue run 2026-10-26 00:02-01:02 UTC, asi da4bd7ca90e2 (0.36.81), BP, repro `live-cc-cycle`, one load per job (results `docs/data/results/2026-10-26-queue-mem-exp.md`). Only Error 12 effects (address space) count as gains. Fields with `?` in `diag: mem` are unvalidated (`big`, `eaDir`, `gcHeap`, `gcFree`, `gcN`); `big` agrees with the validated `diag: memory at mark` walk line. `gcsize: live=` (mark-bit popcount) is selftest-checked only.

**Jobs [measured].** `bp-cc-typegrowth` FAIL and `bp-cc-sqrtheap-observe` FAIL: the same repro failure at cycle 1 `buy_object` (~9-10 min, profiler 195 / 194 of 900 s): the inventory refused 4 of 25 pool products; both started before fix ea8cbfb8 (00:17 UTC). `bp-cc-sqrtheap-on` PASS. `bp-cc-smallheap` FAIL on the job check only (`gcsize ... grewMB=[1-9]` 0 lines: the rule never grew, see below); no crash, all 3 cycles done.

**Per cycle (gc-after of each mark, MB) [measured].**

| Job | mark | free | big | priv | eaDir? | miCom | gcHeap? |
|---|---|---|---|---|---|---|---|
| sqrtheap-on (352) | load | 1694 | 1565 | 1589 | 173 | 67 | 353 |
| | cycle 1 live-full / -empty / inv-empty | 1546 / 1560 / 1563 | 1419 / 1419 / 1419 | 1812-1820 | 187-188 | 112-113 | 360 |
| | cycle 2 | 1477 / 1493 / 1471 | 1371 (all) | 1863-1892 | 187-205 | 124-129 | 360 |
| | cycle 3 | 1464 / 1465 / 1474 | 1339 (all) | 1885-1892 | 184-188 | 131 | 360 |
| | end (t=1200 s) | 1333 | 1211 | 1896 | 185 | 128 | 360 |
| smallheap (160) | load | 1829 | 1773 | 1421 | 174 | 62 | 194 |
| | cycle 1 | 1693 / 1717 / 1710 | 1647 / 1644 / 1647 | 1629-1641 | 184-192 | 108-109 | 200 |
| | cycle 2 | 1634 / 1647 / 1659 | 1578 / 1576 / 1552 | 1686-1702 | 191-207 | 120 | 200 |
| | cycle 3 | 1640 / 1644 / 1636 | 1552 (all) | 1706-1712 | 190-195 | 125 | 200 |
| | end | 1509 | 1408 | 1712 | 192 | 131 | 200 |

Largest free drops every cycle, shrinking steps: on -146 (load to cycle 1, includes first play), -48, -32; small -126, -95, 0; then both lose another ~130-145 MB between the last mark (t~1005 s) and the end sample (cause not read). Removal (`-empty`, `gc-after`) gives back 0-25 MB of free and never any largest-free block: what the cycles take stays.

**Small start heap vs 352 MB [measured, one load each, load-to-load noise ~30 MB on big].** Heap 194 -> 200 MB vs 353 -> 360 MB (-160 MB). Load: free +135, big +208 MB; end: free +176, big +197 MB. Expansions: 160 -> 193 -> 196 -> 200 by the game's own growth path (rule `grewMB` 0 in every line). Forced collections: `gcsize forced` sum 46 vs 12; profiler `gcForced` 75 vs 48, forced ms 5430 vs 3815 (+42%). GC-led hitch iterations (gc >= half of a >= 100 ms iteration) 25 vs 25; GC >= 300 ms in one iteration 10 vs 9, max 371 vs 336 ms. `hitch-ab.ts`: only 1.4 / 1.9 non-repro minutes (stress run), p99 144 / 159, max 408 / 364 ms: no usable natural-play read. Failed expansion: no line with `forced > 0` and heap < limit. **Verdict: `InitialHeapMB=160` is the first measured Error 12 lever here: ~160 MB of address space and ~200 MB of largest free block back, with ~1.5x forced collections and no more GC-led hitches in this run.** Next: repeat both arms (second load each) and a natural-play hitch read before a default change.

**SqrtHeapLimit on vs observe.** Live managed heap is only L = 80-93 MB (`gcsize live`), so M = L + sqrt term stays below the floor: limit = 352.0 in every line of both jobs (160.0, max 165.4 in smallheap). Mode 1 never grew the heap: on = observe in effect (heap 360, gcMs per cycle mean 587 vs 580). No gain to read; the rule only matters with a floor below L + extra. docs/66's "~209 MB live" was heap minus free (includes unswept garbage), not marked bytes [measured contradiction].

**TypeGrowth [measured].** 40 passes, all during load (gc 2-45); then no `typegrowth:` line for the ~96 later cycles (gcN 45 -> 141): every later pass faulted, see §7.1. Cost per pass: census median 46 ms with ~36 K objects, 2.2-8.1 s once the world is loaded (540-900 K objects), graph 57-234 ms: far over any player budget; GC count at load end 45 vs ~330 in the other jobs. No cycle data, so no class growing across cycles. End of load, largest targets: `Vector2[]` 583 KB (held by `List<T>`, `DesireCurve`), `ObjectGuid[]` 347 KB (held by `List<T>` <- `DreamNodeInstance`), `Int32[]` 321 KB; ranks are load growth, not leaks.

**Destroyed-but-alive (`memsus alive`) [measured].** Rises ~+4-8 per cycle and does not fall: on 0 -> 5 -> 14 -> 23 -> 31 (end, freed 124), small 4 -> 10 -> 16 -> 22 -> 25; top types are game objects (`Sim`, `CarUsed1`, `Diploma`, `PhoneSmart`, plants), not CC. Tens of objects: no measurable address-space share [inferred].

### 7.1 TypeGrowth stop and cost: fixed in 0.36.83 (built, not run)

**Why passes stopped [verified, log + disassembly].** The job log has 8 first-chance `C0000005` at `ts3native+0x3052F` after pass #40 (and 4 between #26 and #27, where gc 28-31 have no line); the crash log keeps only the first 16 exceptions, so later ones are not listed. `+0x3052F` is `movzx eax, word [ecx]` in `MxVt` (read of the runtime info's max domain id) with `ecx = 0xFFFFFFFF`: a first word whose "klass" `+0xB8` held `0xFFFFFFFF`. `MxReadable(0xFFFFFFFF, 4)` computed its last page as `(a + n - 1) >> 12 = 0` (wrap past 4 GB), ran no loop and said readable. The `__except` in `MxOnGc` caught it, counted `tyFaults` and dropped the pass without a line. Once such an object is live, every pass faults: the event still fired (no reentrancy guard, no disable path, `tyMode` unchanged).

**Why a pass cost 2-8 s [inferred, host bench].** `MxReadable` asked `VirtualQuery` once per 4 KB page, the page cache was cleared every pass, and objects whose first word is not a vtable (pointers, ints, floats) point all over the 4 GB. One query costs ~7 us in the game (the memdiag walk: 31 ms for 4,590 regions), so ~0.3-1 M distinct pages = 2-8 s. Evidence: the graph walk, which resolves the same first words through the warm cache the census just built, took 57-90 ms on the same heaps; census per object grew from 1.2 us (36 K objects) to 10 us (703 K).

**Fix (ts3native 0.36.83, asi `game-copy/ts3native.asi.typegrowth2`).** `MxReadable` rejects ranges that wrap; one `VirtualQuery` fills the whole region's pages in the cache (at most one query per region touched); pass faults are logged (`typegrowth: pass fault in census|graph|marked bytes`, first 8); the holders graph runs only every `[MemDiag] TypeGrowthGraphEvery`-th pass (default 10, 0 = never), the census and Cork rank every pass. Host bench (`TestMemExpBench`, 6,000 blocks, 564 K live objects, 4,000 classes, 30% non-vtable first words over 4 GB): census 40-47 ms warm (101 ms on the first touch of fresh pages), 1,728 `VirtualQuery` calls, graph ~250 ms on its dense random fake (57-234 ms in the game), so ~25 ms amortised per pass. Game objects 700-900 K: census ~50-80 ms per pass expected [inferred]. Queue `tools/harness/queues/typegrowth2.json` (one job `bp-cc-typegrowth2`, live-cc-cycle 15 min, cc-2k-v2, ~25 min; forbids pass faults and census >= 200 ms; dry run OK except cc-2k-v2 being stashed by the running queue).

### 7.2 Results bp-cc-typegrowth2 [measured]

Job `bp-cc-typegrowth2` (queue `typegrowth2`, 2026-10-26 10:45 PHT, ts3native 0.36.83, asi sha `05551d7f15ff`, live-cc-cycle 15 min, cc-2k-v2, Bridgeport, `TypeGrowth=1` every GC, top 8, graph every 10, rank min 100). PASS, no crash, saves unchanged. Inputs: `probe-out/queue/typegrowth2-2026-10-26.jsonl`, `probe-out/queue/typegrowth2-2026-10-26-bp-cc-typegrowth2-0245-ts3native.log`, `node tools/memory-report.ts` (+ `--steady live|inv`), a scratch parser joining `typegrowth:` passes to the `memsus` marks by `gcCnt`.

**Every pass ran [measured].** 319 `typegrowth: #N` lines, gc 2 -> 320 with no gap (the job's after-sample says gcCount 320), 194 `holders` lines on the 24 graph passes (#80, #90 ... #310, every 10th from the first loaded pass), 0 `typegrowth: pass fault` lines. The §7.1 wrap fix holds: passes continue through load (first pass over 500 K objects: #82, gc 83) and all three CC cycles.

**Pass cost [measured].** Census on the loaded heap (238 passes, 0.54-1.78 M objects, 10-10.6 K classes): median 98.9 ms, p95 132, max 187 ms; 0 passes >= 200 ms, so the queue's forbid rule (census >= 200) passed. That is above the inferred 50-80 ms of §7.1, because the heap holds ~1.7-1.8 M objects, about 2x the 700-900 K the estimate assumed; per object ~0.057 us, inside the host bench's range. Graph (every 10th pass, 23 loaded passes): median 232 ms, max 286 ms; census + graph >= 200 ms on 20 of 24 graph passes, max 415 ms per pass (`memory-report.ts`). The 200 ms rule only checks the census; a graph pass is a player-visible hitch, so TypeGrowth stays a test-run tool (off in the release ini) and `TypeGrowthGraphEvery` should stay >= 10.

**Managed growth per cycle at the same mark, after the forced GC [measured].** Values from the last pass at or before the `gc-after` mark that follows each mark:

| mark | cycle 1 | cycle 2 | cycle 3 |
|---|---|---|---|
| live-empty: marked MB / objects / classes | 79.8 / 1,653 K / 10,272 | 82.9 / 1,730 K / 10,487 | 83.2 / 1,728 K / 10,572 |
| inv-empty: marked MB / objects / classes | 78.4 / 1,616 K / 10,304 | 82.8 / 1,729 K / 10,508 | 84.1 / 1,749 K / 10,574 |
| live-full: marked MB | 81.1 | 83.5 | 82.9 |

Live managed bytes after removal: +3.1..+4.4 MB from cycle 1 to 2, then +0.3..+1.3 MB from cycle 2 to 3 (settling, two steps only; `--steady` says "too few cycles"). The Mono heap stays 353 -> 360 MB over the run with ~165 MB free inside it (`gcHeap`, `gcFree`), so the managed growth is absorbed by the heap's own free space and costs no address space. In the same run largest free fell 1,403 -> 1,211 MB at the `-empty` marks (cycle 1 -> 3, `--steady`) and the resource cache `rc` went 142 -> 449 MB: the address-space loss is native, not managed. Class count grows +215 then +85 per cycle (new generic and array classes seen by the census; their metadata lives in Mono's mempools, not the GC heap; size not measured) [inferred].

**Classes that grow and survive removal: none found [measured, with a tool limit].** The classes in the ranked top list at the six `-empty` marks are flat: `ObjectGuid[]` 351 -> 352 KB (holder `List<T>` <- `DreamNodeInstance` 321 KB, `SocialNodeInstance` 33 KB), `ITrackedStat[]` 46 -> 47 KB (`List<T>`), `IPortalConnectionObject[]` 38 KB (`List<T>`), a `List<T>` 335 KB (held by dream nodes), `CheckWhat[]` 192 KB (`List<T>`), `CASPart[]` 599-601 KB (in the top list from #264, between cycles 2 and 3; flat after), `MonoGenericCMethod` 44 KB (non-object holders). `Sim[]` 709 -> 760 KB and `AlarmHandle[]` 128 -> 159 KB grew during load and left the top list. All of these are load-time fills, not leaks. **Limit:** the top list cannot show a class that only starts growing during the cycles. Cork ratio ranks accumulate from the near-empty heap at load (0.6 -> 80 MB), so load-era classes reach ranks of 4 x 10^4 - 6 x 10^5 and only 9 distinct classes appear in the top list after pass #200; the log line is also capped at ~480 characters, so 7 of the 8 top entries are readable. A cycle-level class leak below ~1 MB per cycle (the total's own step) is therefore not ruled out per class, but its size is bounded by the totals above, and it cannot cost address space while the heap stays at 360 MB [inferred from the totals]. A per-mark class diff (census snapshot at each `-empty` mark, ranked by bytes added since the previous same mark) would close the per-class question if ever needed; not worth building for Error 12.

**"mono ledger INCOMPLETE" [verified, harness code].** Not a TypeGrowth result. `queue.ts` (~line 2033) prints the docs/51 Mono ledger block whenever the counters dump has an `autshadow.ledger` object; counters v137 always carries it, and this job ran with `[AutShadow]` off, so the ledger reads `on 0x0`, 0 scopes, and `ledgerReport()` returns `ok: false` -> "INCOMPLETE". Every ledger line is 0 / "no data". False alarm from the harness check (it should test `on & 1`), not a probe failure.

**Verdict.** TypeGrowth works on the loaded game (no faults, census < 200 ms) and answers docs/191's managed-class question for CC cycles: no managed class leak that matters for Error 12; Mono heap flat, managed live +~1 MB per cycle after the first fill. Next memory work stays native (resource cache, EA heap cores, allocator reservations: docs/201 AllocSites queue).

## 8. Not built

- Cork's slice back to allocation sites (needs allocation-site tagging in the Mono alloc paths).
- More than one hop of holders (beyond the array step) and dominator sizes.
- MemBalancer's 1 s heartbeat (idle decay of `g`); heap decommit / release (docs/198).
- A precise graph using the typed descriptors (pointer words only).
