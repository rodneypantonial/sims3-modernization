# 25. The native world write of a save (~4 s)

What the first ~4 s of a save do on the sim thread, why they are slow, and what we could change. Follows the measurement in [docs/15 "Where the save time goes"](15-native-patches.md) (Bridgeport `Baseline-BP`, 35 MB save, ~12 s save, 2026-10-14). Static analysis only: Ghidra headless decompiles and disassembly of the Steam 1.67 exe, plus the existing sample file `probe-out/saveprof-native-2.jsonl`. No game run.

Marks: **[verified]** read in a decompile or the disassembly, **[measured]** from the sample file, **[inferred]** reasoned, not confirmed.

## Summary

- **The ~4 s is almost all per-lot saving.** `World_Save 0xC6D460` writes a few small world resources, then saves every lot through `Lot_Save 0xACB8A0`. Samples under `Lot_Save` cover ~3.6 s of the 4.1 s window [measured, time-weighted].
- **Biggest single cost (~1.2 s, ~30%): the routing footprint save is quadratic.** For each floor level of each lot, `LotLevel_SaveFootprints 0x6663C0` walks the **whole** footprint map of the routing world's path planner and keeps only the entries owned by that level. Cost = (levels in the world) x (footprint entries in the world). The hot instructions are the loop itself (`0x6664D6`, `0x6664E2`, `0x66659A`, `0x6665BD`): load the entry, bump its refcount, compare the owner, drop the refcount [verified disassembly + measured leaf PCs]. Not a write-path problem: byte swapping and the stream writer are cheap here.
- **Other costs:** object groups per lot (`Lot_SaveObjectGroup 0xC6F520`, ~0.76 s, many small allocations and the allocator lock), RefPack compression when each resource is closed (~0.56 s), resource lookups (~0.24 s), lot imposter rebuilds and their job wait (~0.37 s together), other (~0.8 s).
- **The "~0.9 s lock wait" in docs/15 is a sampling artifact.** `FUN_004E2760` is a semaphore wait inside the JobManager wait (`JobManager_WaitLocked 0x59A220`), reached from the lot **imposter** build (`LotImposter_Build 0xADAF60`). The sampler takes a blocked thread's stack every 2-7 ms but a running thread's only every ~35 ms, so counting samples over-weights waits ~10x. Time-weighted, the waits are ~15 episodes of ~10 ms, **~0.13 s** (~0.24 s with S3SS's wait hook frames) [measured].
- **Best option:** an owner index for the footprint map (one pass per save or per lot, then each level reads only its own entries). Same bytes written, sim thread only, expected **-0.7 to -1.0 s** per save on BP. Everything else is worth less than 0.4 s each or needs our own package writer.

## Method and the sampler bias

`saveprof-native-2.jsonl`: 324 samples of the sim thread, stack-scan unwinder (only leaf frames and return addresses that fit the call graph are trusted). The native world phase is t = 1.8-5.9 s (143 samples). Sample gaps are uneven: median ~35 ms when the thread runs, 2-7 ms when it is blocked in `WaitForSingleObject` [measured]. All shares below weight each sample by the gap to the next sample, not by count. Count-based shares (as in docs/15) overstate the wait (32 of 143 samples, but 131 ms of 4,104 ms).

| Part of the 4.1 s window | Time-weighted | How it was classified |
|---|---|---|
| Footprint save (`0x6663C0` and its map inserts `0xB34440`, quadtree `0x664EA0`) | ~1.23 s | frame in the top 10 |
| Object groups (`0xAB49C0` -> `0xC6F520` -> `0x939220` / `0x935B50`) | ~0.76 s | frame in the top 14 |
| RefPack compression (`0x4EC0A0` -> core `0x4EBB90`) | ~0.56 s | top 4 frames |
| Resource lookups / reads (`0x4A8910`, `0x4A94C0`, `0x4ECE50`) | ~0.24 s | top 6 frames |
| Imposter job wait (semaphore, incl. S3SS hook frames) | ~0.24 s | leaf |
| Imposter build (`0xADAF60`, `0x59A2xx`) | ~0.13 s | top 10 frames |
| Lot data layers (`0xAC5E30`, `0xAD3C20`) | ~0.10 s | top 14 frames |
| Other (allocator, hash lookups `0x576C30`, `0x5492E0`, `0xD38B00`, render-side frames) | ~0.84 s | rest |

Leaf PCs inside `0x6663C0`: `0x6664E2` 368 ms, `0x6664D6` 250 ms, `0x66659A` 146 ms, `0x6665BD` 75 ms (~0.84 s in the scan loop alone) [measured]. `GeneralAllocator_MallocCore 0x4E5E40` leafs (~0.6 s) are spread over the footprint map inserts, object-group writes, resource reads and stream growth; the hot PCs `0x4E6066` / `0x4E60C3` are dlmalloc bin walks [verified disassembly].

## 1. What `World_Save 0xC6D460` writes, in order [verified decompile]

Called from `Save_WritePackage 0xAAC110` after the S3 header and the pre-save listeners (`0xAABB80`); `0xAABBC0` (post-save listeners) runs after it.

1. Region setup (`0xCEF5B0` / `0xCEFFD0`), undone at the end (`0xCF1A80`).
2. `World_SaveIni 0xC63D90`: a small resource named after the `World.ini` string.
3. World listeners (`world+0x58`) through `0xC64A60`.
4. Three `Package_CopyResources 0x7D8F00` calls from the world's base package (`world+0x84`) into the save, filtered by three filter objects. It dispatches on package kind ids (`0x3DB0260`, `0x2A43BD44`, `0x3C4B9A4`) [verified]; what the filters select is not read [inferred: unchanged world resources].
5. `WorldManager_LotRebuildCaller 0xC6BCC0` (only when the dirty flag `world+0x46` is set): dirty lots ("World/WorldManager/DirtyLots") get a routing rebuild (`RoutingWorld_RebuildLot 0x82B810`) and more; then dispatches a job and clears the list.
6. `World_SaveLotIndex 0xC61DC0`: one resource with version 2, lot count, and per lot the 64-bit lot id plus one value from the lot's vtable slot `+0x40`; then the world lot (`world+0xB0`) id.
7. `World_SaveLots 0xC62860`: for each lot in the vector `world+0x9C`, the lot's vtable slot `+0x1C` (save); then the world lot `world+0xB0`; then script state (`0x937100`; with `world+0x45` set, inside resource `0xCF84EC98`). Script-mode setup goes through `0x936DB0`.

The per-lot save reaches `Lot_Save 0xACB8A0` (direct callers at `0xACBF6A` and `0xACC45E`; the vtable slot is inferred from the stacks). Per lot, in order [verified decompile]:

| Step | Function | What it writes |
|---|---|---|
| a | `0xACB2C0`, `0xA8CB60` | lot header resources |
| b | `Lot_BuildImposter 0xAE0540` -> `LotImposter_Build 0xADAF60` | only when `lot+0x364` is set: rebuilds the lot imposter (the distant-view model/texture) through a JobManager job and **waits** for it; logs "Error during construction of imposter for lot %I64u" on failure |
| c | `Lot_SaveRouting 0xAB1E60` -> `RoutingWorld_SaveLot 0x82C820` | under the routing lock (`world+0x918`, infinite deadline): resource type `0x046A7235` = `LotRouting_SaveLevels 0x63C690` + `0x657390`; resource `0x07CD07EC` = `0x662A20` when the lot has a routing record |
| d | `0xC6DE80`, then `Lot_SaveLayers 0xAC5E30` | deck foundation layer (`0x93D6D6`), data layers (`0x2E2C5C`, "Lot/SaveDataLayers/Tmp1"), ~15 sub-writers, `0xAD3C20` (`0x563919E`), `0x333E35` / `0x333E36` |
| e | `Lot_SaveObjects 0xAB49C0` -> lot vtable `+0x28` = `Lot_SaveObjectGroup 0xC6F520` | resource `0x4D1A5589`: object list; per object `0x939220` (flags, resource keys, `0x935B50`); calls the script method `ScriptCore.LoadSaveManager.SaveObjectGroup` once per lot when `lot+0x75` is set |
| f | `0xC6ED40` / `0xC6EFA0` | tail resources |

Every resource is opened with `SaveCtx_BeginResource 0x7D8290` (key) and closed with `SaveCtx_EndResource 0x7D7840`, which calls the stream's close (`+0x20`) and release (`+0x08`) [verified]. The RefPack compressor runs under that close (stack `0x7D7840` -> `0xC40C80` -> `0x4E4230` -> `0x4DEFF0` ... `0x4EC0A0` -> `0x4EBB90`) [inferred from stacks; the close itself is a virtual call].

Scalars go through `Stream_WriteU32 0x4DDBD0` / `0x4DDCC0`: byte swap unless the context says native order, then the stream's virtual `Write(ptr, 4)` (`+0x38`), which lands in `MemStream_Write 0x4DEFF0` (memcpy into a buffer that grows geometrically by a float factor) [verified]. One virtual call per 4 bytes is wasteful but not quadratic; it does not show up as a leaf of its own.

## 2. The footprint save (`0x63C690` -> `0x6663C0`)

### What it is [verified]

- `LotRouting_SaveLevels 0x63C690` gets the lot's graph from the path planner P (`LotTable_GetOrInsert 0x62EAB0`), puts P into save mode (`PathPlanner_SetSaveMode 0x62C670`, which creates "Routing/PathPlanner/FootprintSaveMap" / "FootprintLoadMap"), then for each per-level cell graph L in the lot graph's vector (`+0x74`..`+0x78`) writes `L+0x44` and calls `LotLevel_SaveFootprints(L)` (call site `0x63C850`, `ecx = L`). Afterwards `PathPlanner_WriteFootprintSaveList 0x62B2A0` ("Routing/PathPlanner/FootprintSaveList") writes the collected footprints.
- If the lot's routing is flagged stale (`0x631A60` / `0x631010`), it first calls `RoutingWorld_RebuildLot 0x82B810`. No sample in this save was under `0x82B810` or `0x63D7B0` [measured], so on BP no lot was rebuilt during the save.
- `LotLevel_SaveFootprints 0x6663C0` (`this = L`): writes a version 1 and two level fields (`L+0x30` float, `L+0x2C`), then iterates the hash map at `[[L+0x20]+0x24]`. `[L+0x20]` is P: `PathPlanner_FootprintSaveIndex 0x62B1E0` is called with `ecx = [L+0x20]` and uses P's save map (`+0x9C`), its counter (`+0xA4`) and list (`+0xA0`) [verified]. So `[P+0x24]` is one map for the whole routing world, with entries of type "Routing/FootprintMapEntry" (created by `0x6376D0` / `0x665C50`, loaded by `0x666F90`).
- For every map entry: `refcount+1` (non-atomic add at `0x6664DE`), compare the owner field (`+0xC`) with L, and only on a match: write the footprint's global save index (from `0x62B1E0`, which assigns indices in first-seen order and appends the footprint to P's save list) and the entry's `+0x10` field, and insert the entry into a local hash map ("Routing/PathPlanner/FootprintMapEntrySaveMap", `HashMapU32_Insert 0xB34440`, one 12-byte allocation per entry, rehash `0x769AD0` as it grows) with a running index. Then `refcount-1` (and destroy at 0, which cannot happen here). Then `FootprintQuadTree_SaveNode 0x664EA0` writes L's quadtree (`L+0x24`) recursively: inner nodes as a mask with four children, leaves as a list of (entry index from the local map, 2 halfwords).

### Why it is slow

- **O(levels x entries).** Each level scans every entry in the world map; a typical lot has 2-4 levels (ground, upper floors, basement) and BP has a few hundred lots, while the map holds the footprints of every object, wall, floor and terrain piece in the world. Only a small fraction of the entries per scan match [inferred; counts not measured]. The leaf PCs (`0x6664D6` load of the entry pointer, `0x6664E2` right after the refcount add, i.e. cache misses on the entry) take ~0.84 s [measured].
- **Write traffic on a read.** The refcount add/sub dirties every entry's cache line on every scan, also for entries of other lots.
- **One allocation per matching entry** in the local save map (~0.1-0.2 s of allocator time under `0xB34440`, plus rehashes) [measured: allocator leafs with `0x6663C0+0x1C2` / `0xB34440` above them].
- Not the cause: `List_Find4Key 0x5492E0` (linked-list search on a 16-byte key) has ~70 ms of leafs in the whole window and sits in object/resource code (`0xF50390`, `0x549160`), not in the footprint save. Not O(n^2) at the volumes seen.

### Could the game rebuild it on load instead?

The resource `0x046A7235` holds per-level footprint membership and the quadtrees. The game has a full rebuild (`RoutingWorld_RebuildLot 0x82B810`, all rooms) and the loader (`0x666F90`, "FootprintMapEntryLoadList") reads the saved form. Whether the loader accepts a missing resource and rebuilds, and what that costs per load, is unknown [inferred]. Skipping it changes save data and moves cost to every load, so it ranks last.

## 3. The wait in `FUN_004E2760`

- `Semaphore_Wait 0x4E2760` [verified]: `WaitForSingleObject` on a Win32 semaphore with a count, deadline from a pointer (here the global `0xFD9744`, also passed to `Lock_Acquire` around it; same pattern as `0x1021CF0` for the routing lock, so probably "infinite" [inferred: value not read]).
- Caller: `JobManager_WaitLocked 0x59A220` ("Services/JobManager/WaitNode"): while the job's state (`+0x24`) is not finished (0, 1, 6, 7), it queues a wait node, drops the JobManager lock, waits on the node's semaphore, re-takes the lock [verified]. Wrapper `JobManager_Wait 0x59A500`.
- Who calls it during the save: `Job_WaitOrPump 0xAD97E0` from `LotImposter_Build 0xADAF60` (`Lot_Save` step b). `0xADAF60` creates the imposter job with the JobManager (`0x599160`, vtable `+0x10`), then `0xAD97E0` either pumps frames itself (render/update calls `0x6B0500`, `0x588E00`, `0xC7CEA0`, `0x6E8810`) when the current thread equals the thread returned by `0x7D99D0`, or waits [verified]. The sim thread is not that thread, so it waits.
- Who holds it: the thread that runs the imposter job and posts the semaphore. The pump branch implies the job needs the main/render thread (the imposter is rendered: "imposterTexture", "lotImposter") [inferred]. It is not the resource/streaming lock (`g_ResourceLock 0xFB2CD0`) and not the routing lock.
- Size: ~15 waits of ~10 ms, ~0.13 s, plus ~0.1 s in S3SS's hook on the same wait (`Sims3SettingsSetter.asi+0x4C5C0..0x4C68C` leaf frames) and ~0.13 s of build work [measured]. Which lots get an imposter rebuild (`lot+0x364`, and the checks `0xAB1890` / `0xAB1860`) was not traced.
- Avoidable? Partly: submit the imposter jobs of all lots first and wait once at the end (the render thread builds while the sim thread saves other lots), or skip lots whose imposter is unchanged. Both need hooks in `Lot_Save`; worth at most ~0.25 s.

## 4. What could run on worker threads

The game is paused while it saves, but the save itself changes state, and several structures have non-atomic refcounts.

| Work | Reads only stable data? | Verdict |
|---|---|---|
| Footprint map scan per level (`0x6663C0` loop) | The map is stable unless `0x82B810` rebuilds a lot mid-save (code path exists, not seen on BP). But the loop **writes** refcounts (non-atomic `add [edi+4],1`) | Not on a worker as is. A read-only owner index (no refcount touch) built on the sim thread under the routing lock is cheaper than any thread split |
| Footprint index assignment (`0x62B1E0`) and P's save list | No: global counter `P+0xA4` and list `P+0xA0`; indices depend on visit order and are written into the data | Must stay serial, in the game's order |
| Quadtree write (`0x664EA0`) | Reads L's tree and the local map | Could run per level in parallel into separate buffers, but it is cheap (~80 ms) |
| Object groups (`0xC6F520`) | Calls into Mono (`SaveObjectGroup`) | Must stay on the sim thread (dead-ends: Mono is single-threaded here) |
| Lot layers (`0xAC5E30`) | Lot-local arrays [inferred] | Could, but ~0.1 s |
| RefPack compression at resource close | The finished buffer is immutable after close | Yes in principle; file offsets and the index are assigned at close, so it needs a deferred writer (our own DBPF write) |
| Imposter build | Needs the render thread | Pipelining across lots, not a worker |

Per-lot parallel serialization of whole lots is not viable: lots share P's save list and index counter, the package writer's resource sequence and Mono.

## 5. Ranked options (designs only, nothing built)

| # | Option | Expected saving (BP, per save) | Risk | Notes |
|---|---|---|---|---|
| 1 | **Footprint owner index.** Replace the call at `0x63C850` (or detour `0x6663C0`) with our version of the same writer: on the first level of a save, walk `[P+0x24]` once (in the map's own iteration order, without touching refcounts) and bucket entries by owner; each level then writes only its bucket, in the same order, so the bytes are identical. Invalidate on any change to the map: hook the entry insert/erase (`0x665C50`, `0x6376D0` and the erase path, to find) to bump a generation counter, or, simpler and safe, rebuild the index whenever `0x82B810` ran or the map's entry count / bucket pointer changed | **-0.7 to -1.0 s** (the scan; per-lot variant: rebuild the index at each `0x63C690`, saves (1 - 1/levels per lot), ~-0.4 to -0.6 s) | Low-medium: must reproduce the writer exactly (fields, order, error flag `ctx+0x14`, index side effects of `0x62B1E0`); verify by byte-comparing resource `0x046A7235` of two saves of the same state with the switch off/on (`--ab`) | Sim thread only; no save-format change |
| 2 | **Presize / pool the per-level save map.** Size the local hash map from the bucket length (known after option 1) and allocate its 12-byte nodes from one block | -0.1 to -0.2 s | Low | Only with option 1 (same code) |
| 3 | **Batch imposter jobs.** Submit all lots' imposter jobs before the lot loop, wait for each one at its lot (or once at the end); or skip unchanged imposters | -0.1 to -0.25 s | Medium: imposter order and the render-thread pump path; visual check of distant lots after load | The "0.9 s wait" is really ~0.13-0.24 s |
| 4 | **Faster or parallel RefPack at resource close.** (a) Our own faster encoder (same format; S3SS already ships a SIMD RefPack for decompression, check for compression) on the sim thread; (b) compress on workers with a deferred write order | (a) -0.2 to -0.3 s, (b) up to -0.5 s in this phase (more over the whole save) | (a) medium: output must decode in the game; (b) high: needs our own package writer (docs/15 already judged this poor value) | Same lever also cuts the later resource-compression step |
| 5 | **Cheaper allocator for the save.** A thread-local bump arena for the save's short-lived nodes, or measure with S3SS's mimalloc hook on (the EA allocator is not affected by mimalloc; MSVCR80 malloc is) | -0.1 to -0.3 s | Medium: frees go through `0x4E3F70`; mixing allocators crashes | Spread over many callers; low precision |
| 6 | Skip `0x046A7235` and rebuild routing on load | up to -1.2 s here, + load time | High: save-format change, loader behaviour unknown | Research only; option 1 gets most of it without this |
| 7 | Parallel per-lot serialization | - | High | Not viable (section 4) |

Combined realistic target: options 1+2 alone take the native world write from ~4.1 s to ~3.0-3.3 s; with 3 and 4a, ~2.6-2.9 s.

## 6. Next steps

1. Count the work instead of inferring it: a probe on `0x6663C0` that logs, per call, the map entry count (`[[[L+0x20]+0x24]+0xC]`) and the matches, and the number of calls per save. This gives levels x entries directly and the expected saving of option 1.
2. Re-measure with the sampler time-weighted (or a fixed-interval timer) and correct docs/15's wait share.
3. Find the footprint map's erase path (callers of the `0x665C50` / `0x6376D0` creators' counterparts) for option 1's invalidation.
4. Trace which lots set `lot+0x364` (imposter rebuild) and whether `0xAB1890` filters unchanged lots.

## 7. Option 1 as built: `[FootprintIndex]` (ts3native, off by default, not yet run in game)

Source: `native/ts3native/footprint.cpp` (included by `ts3native.cpp`). Settings `[FootprintIndex] Enabled=0, Mode=1, Verify=0`.

**How.** The writer `0x6663C0` keeps running; only the map it walks changes [verified offline, see test]:
- `0x666488`: the two loads of the map pointer (`8B 53 20 8B 4A 24`, `mov edx,[ebx+0x20]` / `mov ecx,[edx+0x24]`) become `call FpScanStub; nop`. The stub loads the same values, asks `FpSelect(L, map)` and returns in `ecx` either the real map or a one-bucket map whose chain holds exactly L's entries in the real map's order (bucket array `[first, end marker]`, as the writer's bucket walk expects). The writer's own code then does the writes, the save-index calls (`0x62B1E0`), the per-level save map and the refcount bump/drop on L's entries, as before. Entries of other owners are not touched any more; their bump/drop was net zero. If any entry has refcount <= 0 (where the game's drop would destroy it) the real map is used for that lot.
- `0x82C940` (`RoutingWorld_SaveLot` -> `LotRouting_SaveLevels`): a thunk bumps a generation and jumps to `0x63C690`. The index is rebuilt at the first level of each lot, so after the lot's own rebuild (`0x82B810`, inside `0x63C690` before the level loop) and after anything that ran between lots (script save calls, imposters). Each level also compares the map header (pointer, buckets, bucket count, element count) with the build and rebuilds on a change.
- `0xAAC320` (`Save_WritePackage` -> `World_Save`, thiscall, `ret 8`): a thunk resets the counts, calls `World_Save`, logs one line and drops the index.
- Index: one walk of the real map per lot (no writes), entries chained per owner through an open-addressing owner table. Walk faults (SEH) or allocation failures fall back to the real map.
- Checks before writing anything: FNV-1a of `0x6663C0..0x666740` = `0xF2CDC1C1` (Steam 1.67), the 6 bytes at `0x666488`, and both `E8` call sites go to the expected targets. Any miss: `SKIP footprint index`, nothing written.
- Saving: one map walk per **lot** instead of one per **level**, and no refcount writes on other lots' entries. The log line gives levels N and lots C, so N/C shows the expected gain. If it is small, a per-save index with a mutation hook on the footprint map is the next step (not built: the map's insert/erase functions are not identified).

**Runtime switch:** exported `DWORD ts3native_footprintMode` (0 = the game's scan, 1 = index), start value `Mode`. `Verify=1`: every level also walks the real map and compares the owner's entries and order; a mismatch uses the real map and is counted.

**Log line per save** (`ts3native.log`): `footprint: levels N entries M matched K calls C ms T verify ok|mismatches X|off (mode m fallbacks F worldSave ms W)`. N = writer calls (levels), M = largest map size seen, K = entries handed to the writer, C = index builds (lots), T = time in our code, W = `World_Save` total.

**Offline test** (scratch, not in the repo; worth moving into `selftest.cpp` later): 200 random maps (1-300 buckets, up to 5,000 entries, up to 64 owners, null entries) walked with a C copy of the writer's bucket walk (`0x66648E..0x6665D3`): the index map yields the same entries in the same order for every owner; owners without entries yield none; a stale index (owner changed, same generation and header) is caught by Verify and falls back; a header change rebuilds; refcount 0 falls back; Mode 0 returns the real map. The scan stub (called with `ebx` = L), the lot thunk and the save thunk (ecx, stack arguments and return value passed through, one log line) are tested too. `cl /O2 /MT /W4 /WX` clean.

**In-game test** (serial, after `[SaveWrite]` results; back up `Saves` first): `Enabled=1 Mode=1 Verify=1`. Log: `OK   footprint index: 0x00666488, 0x0082C940, 0x00AAC320 (Mode=1 Verify=1)` then per save a `footprint:` line with `verify ok`. In one paused load: save with `ts3native_footprintMode` 0, then 1 (same name or two names), and compare the two saves with `tools/harness/save-diff.ts`; resource `0x046A7235` must be byte-identical (Verify only proves the selected entries, the diff proves the bytes). Then `Verify=0` and compare `worldSave ms` between modes. Reload the Mode 1 save and route Sims on several lots.

## 8. NRaas Saver's save path vs ours (player report 2026-10-24, check built, not run)

Report (Discord, 2026-10-24): "save is stuck" with v0.1.5 + NRaas Saver. Nothing is known yet beyond that line; this section says what Saver does differently from a player's save and what the check runs.

**What Saver V22 does** [verified: Chain-Reaction/NRaas `NRaasSaver/Saver.cs`, `SaverSpace/PersistedSettings.cs`]:
- Timer: `RealTime` style (default, 30 real minutes) = its own `AutoSave` Simulator task waiting on a real-time `StopWatch` with `SpeedTrap.Sleep()`, then until no modal window, not video recording, save not disabled and Live mode (Build/Buy only with `mPromptInBuildBuy`); `SimTime` / `SimHour` = `AlarmTask`s. On world load it also pauses the game once after 2 sim-minutes (`mPauseOnLoad`, default on).
- `Save()`: in Live mode, `mSwitchToMapView` (default off) toggles map view (`Camera.ToggleMapView`) and sleeps one tick; `mPauseOnSave` (default off) pauses. Then `Common.FunctionTask.Perform(OnSave)`: a new game task.
- `OnSave()`: save name = current `SaveName` without `.sims3`, trailing digits = cycle count, +1, back to 1 after `mSaveCycles` (default 4); `OptionsModel.SaveGame(synchronous: true, forceUseSaveName: true, forceSaveAs: saveAs)` called inline in that task. With an empty save name it saves under the world's caption (`saveAs` false). Its pre/post-save listeners stop the real-time timer and re-arm all timers after the save.

**What the game's `OptionsModel.SaveGame(true, ...)` does with that** [verified decompile, Steam 1.67 `Sims3GameplaySystems`]: sets the save-in-progress flag, runs the save body in the calling task (the same body the menu Save runs in its own task: `SaveGame(false, ...)` only queues it as a one-shot task); `forceSaveAs` goes through the name dialog path without showing it and deletes an existing file of that name first; the body locks the clock at Pause, sleeps one tick, shows the progress dialog, writes (`LoadSaveManager.SaveGame` -> `Save_WritePackage 0xAAC110` -> §1, routing lock in step c), restores the speed; error paths show a modal dialog (a "stuck" save can be a modal no one answers). So the write itself is the same as a player's save; the differences are where it starts: a FunctionTask at an arbitrary tick while the game runs (the clock is unpaused until the body's own lock), optional map view one tick before, and every N minutes without the player watching. Our running route jobs hold `+0x918` across a tick, so the save's step c waits for them (infinite deadline, docs/18 RouteGate save site); a wait that never ends would be the "stuck" save.

**Check** (queue `tools/harness/queues/saver-check.json`, shipped v0.1.5: asi `rel015` + `ts3fixes.v0.1.5`, BP, 30+ Sims routing):
- Job 1 `bp-saver-emulated-v015`, repro `saver-autosave-crowd-loop`: agent `saver_like_save` (`mods/ts3agent/src/plugin/Handlers/SaverHandlers.cs`) does `Save()` + `OnSave()` from its own game tasks with map view on and the game running, 9 times, while the crowd routes.
- Job 2 `bp-saver-real-v22-v015`, repro `saver-real-autosave-crowd`: the real `NRaas_Saver.package` V22 (fixes.json `nraas-saver`, installed for the job only) set at runtime to a 2 real-minute timer (`mRealMinutesBetweenSaves` + `RestartTimers`, as its option does), its other settings at their defaults (pause on load off for the run); until 8 Saver saves.
- Both: agent `save_watch` (a game task reading the save-in-progress flag every tick) records each save's duration, longest tick gap (the native write), modal seen, file written and ts3fixes `asyncroute_started` in the 5 s before / during the save; it fails the job when a save takes or stays open more than 120 s. Plus no crash, no hang (run guard), no `FAIL` line in `ts3native.log`, Baseline save unchanged. Scratch saves `SaverChk-E-*` / `SaverChk-R-*` (5 names each, overwritten per run).
- How to run: `fixesBuild` needs `build/ts3fixes.package` with the v0.1.5 tuning, and `cycle.ts` rebuilds it from `mods/ts3fixes` each job, so run from a worktree of this commit with `git checkout f5054207 -- mods/ts3fixes tools/setup/build-fixes.ts` (verified: builds to `ts3fixes.v0.1.5`) and a `game-copy` junction; put `NRaas_Saver.package` in `~/Downloads/ts3-fixes`. The ini is master's (sections added since v0.1.5 are off by default; the rel015 asi ignores keys it does not know).
- Not covered: Saver's Build/Buy prompt path, `SimTime`/`SimHour` styles, other NRaas mods' pre-save listeners beyond ErrorTrap/Overwatch already installed, Saver + pause on save (pass `pause: true` to `saver_like_save` for that).

## Evidence files (git-ignored)

- `game-copy/analysis/decomp/save-hot.c` (first decompiles), `worldsave-1.c` (`0xC6D460`, `0xAAC110`, depth 1), `worldsave-2.c` (48 functions of the per-lot chain), `probe2.txt` (disassembly of `0x6663C0`, `0x63C820`, `0x4E6040`, call references, footprint and imposter strings).
- `probe-out/saveprof-native-2.jsonl` (samples).
