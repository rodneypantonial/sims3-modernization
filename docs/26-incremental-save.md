# 26. Incremental ("dirty-only") save: can a save write only what changed?

Research question: would saving get much faster if the game wrote only what changed since the last save or load, instead of serializing the whole world every time? Target: Steam 1.67 `TS3W.exe` (32-bit) with the Mono 1.2 script assemblies. Read-only research: decompiled C# (`game-copy/decompiled/ScriptCore`), Ghidra decompiles of the native save path (shared with [25-save-world-write.md](25-save-world-write.md)), and offline diffs of real saves. No game run.

Marks: **[verified]** read in the decompiled code, the disassembly or the save files; **[measured]** from save files or profiles; **[inferred]** reasoned, not confirmed; **[community]** from modding wikis.

Timing context ([15-native-patches.md](15-native-patches.md) "Where the save time goes"): a ~12 s save of `Baseline-BP` (35 MB) = script serialization ~6.3 s, native world write ~4 s, lock/job wait ~0.9 s (docs/25: really ~0.13-0.24 s of imposter jobs), RefPack compression ~0.6 s, disk write/copy/flush ~0.8 s.

## Summary and verdict

**Verdict: a true incremental save is not worth building. It would save at most ~1.5-2.5 s of the ~12 s, only on the second and later saves of a session, at very high effort and with real save-corruption risk. Cheaper fixes already in flight get more (~4-5 s).**

Why:

1. **The script state is one global graph, not per-lot pieces.** All persisted C# objects of the world go into one resource (`0x06B981ED` OBJS, ~9 MB uncompressed in BP) written by one `ScriptCore.PersistWriter`. Each object gets a global index on first visit; class and resource-key tables are global too [verified]. The per-lot `ScriptObjectGroup` is just one record in that graph.
2. **One change renumbers everything after it.** Between two saves of the same load 2 sim days apart (Isla Paradiso), **0.1%** of the 208k OBJS records were byte-identical at the same index, and only **4.4%** matched anywhere in the file [measured]. The native per-lot object lists (`0x4D1A5589`) store those script indexes, so they change on every lot even when the lot did not [measured].
3. **Nothing is reusable across a load.** Two saves around a reload share almost no per-lot bytes (BP: 0 of 12.3 MB of per-lot data reusable), because native per-lot data also carries ids that change on load [measured; cause inferred]. So the first save after every load must be a full save.
4. **Within one load, about half the lots do not change.** In IP over 2 sim days, 62-64 of 139 lot object lists changed only in their script indexes, and **42%** of the `.nhd` bytes on disk were in resources identical to the previous save [measured]. Those are mostly small, empty or unowned lots: they hold only 26% of the per-lot bytes.
5. **There is no change tracking to build on.** The game keeps dirty flags for thumbnails, lot imposters, lot value, routing rebuild after building, and a few caches, but nothing for script objects, Sims, households or relationships [verified, §2]. Script objects live in an interpreter-only Mono VM. Field-level tracking means hooking every field store in the interpreter. Event-level tracking misses everything that changes with no interaction: motives every tick, aging, relationship decay, story progression, plant growth, dirt, spoilage, weather [verified examples, §6].
6. **The script half is a graph walk, not a write.** ~2.5 s of the 6.3 s is identity lookups in `ReferenceHashtable` and ~1.3 s is reflection (docs/15). A dirty-only writer still has to find every reachable object (new ones, deleted ones, re-parented ones), so it pays most of that walk unless reachability also gets tracked.

**What to do instead, ranked by gain/effort/risk (BP, ~12 s):**

| # | Option | Gain | Effort | Risk | Note |
|---|---|---|---|---|---|
| 1 | Faster identity table for the writer (`ReferenceHashtable` replacement) | up to ~2 s | low-medium (in progress in `mods/ts3fixes`) | low: same output bytes | biggest single lever, every save |
| 2 | Footprint save owner index ([25](25-save-world-write.md) option 1) | ~1.2 s | medium | low-medium | every save; also the biggest part of any "skip unchanged lots" gain |
| 3 | Cached reflection: per-save cache of the writer's per-Type answers (§8; built as ts3fixes `kCachedReflection`, off by default, unmeasured) | ~0.5-1 s | medium | low: same bytes (startup probe compares bytes) | every save |
| 4 | Fewer persisted objects (save cleanup, life-event trim) | ~18% of script objects in BP | done / opt-in ([14](14-save-cleanup.md)) | low | every save |
| 5 | **Compressed-bytes cache:** if a resource's uncompressed bytes hash the same as in the previous save of this load, reuse its compressed bytes | ~0.25 s (42% of bytes in IP; 0 on the first save after a load) | low-medium (native, at the resource close) | low: output bytes identical | built, off by default: `[SaveCompress] CacheMB` (§8) |
| 6 | Skip unchanged lots in the native world write (reuse last bytes, patch script indexes) | ~0.4-0.8 s on top of #2 | high | high: save-format coupling | only after #2; needs stable script numbering (#7) |
| 7 | Stable script numbering + dirty tracking + own OBJS writer (true incremental) | ~1-1.5 s more | very high | high: silent save corruption | not recommended |

The smallest safe step that tells us whether #6/#7 could ever pay off is measurement only (§7.4).

## 1. Save structure

### 1.1 Files and resources

A save folder `<name>.sims3` holds `<World>_0x<id>.nhd` (the world: lots, objects, script state), `<World>_0x<id>ExportDB.package`, `TravelDB.package` (memory snapshots and travel data), `Meta.data` and `1a5tf1l3.dat` [verified, file listing]. BP sizes: `.nhd` 31.9 MB, ExportDB 0.5 MB, TravelDB 0.9 MB.

`.nhd` contents by type (BP, `Baseline-BP`, on-disk size; names from the modthesims type table [community]):

| Type | Count | Disk | What | Unit |
|---|---|---|---|---|
| `0x01661233` MODL | 67 | 6.6 MB | object models | world (copied, unchanged in every pair we compared) |
| `0x046A7235` | 82 | 6.5 MB | lot locational data = routing footprints and quadtrees (docs/25) | per lot |
| `0x342779A7` | 16 | 4.3 MB | native CAS SimService build cache (docs/09) | global, 16 chunks |
| `0x4D1A5589` OBJN | 97 | 3.5 MB | per-lot object list | per lot (+ world lot etc.) |
| `0x06B981ED` OBJS | **1** | 2.9 MB (9.0 MB raw) | **all script state** | **one global blob** |
| `0x05ED1226` REFS | 180 | 1.8 MB | reference store | per lot + others |
| `0x312E7545` | 492 | 1.8 MB | walls | per lot / level |
| `0x0580A2CE` | 101 | 1.6 MB | Sim portraits | per Sim |
| `0x0563919E` | 82 | 1.4 MB | lot wall/floor compositor | per lot |
| `0xB125533A`, `0x06DC847E`, `0xB1422971`, `0x05FF6BA4`, ... | hundreds | ~1.8 MB | terrain paint, lot geography and small per-lot layers | per lot |
| `0xCF84EC98` | 1 | 0.2 MB | state machine saved state | global |

[verified: counts and sizes read with `tools/harness/save-diff.ts` and a type summary; names community]

ExportDB (`0x025ED6F4`, `0x033A1435`, `0x0341ACC9`) and TravelDB did not change between saves of the same load in any pair we checked (they change with travel, household export and memories) [measured].

### 1.2 How the native world write is split

`World_Save 0xC6D460` writes a few world resources, copies unchanged world resources from the base world package (`Package_CopyResources 0x7D8F00`, three filters), then saves **every lot** in the world's lot vector through `Lot_Save 0xACB8A0`, then the world lot, then the script-state sections (`0x937100`) [verified, details in docs/25 §1]. Per lot: header, optional imposter rebuild, routing footprints (`0x046A7235`), layers (walls, floors, terrain paint, compositor `0x0563919E`), and the object list (`Lot_SaveObjectGroup 0xC6F520`, resource `0x4D1A5589`). So the native side is **per lot, one resource per lot and type**, and every lot is written on every save; no lot is skipped [verified].

Inside the object list, each object entry (`Object_SaveRecord 0x939220`) writes a masked id list, three resource keys and a component part (`0x935B50`) [verified decompile]. Before the objects, the lot record stores the return value of the C# `LoadSaveManager.SaveObjectGroup` call, made once per lot when `lot+0x75` is set [verified decompile].

### 1.3 How script state is split

Not at all, on disk. `ScriptCore.LoadSaveManager` drives one `PersistWriter` for the whole save [verified, `LoadSaveManager.cs`, `PersistWriter.cs`]:

- `BeginSaveObjects` creates the writer once.
- Per lot, native calls `SaveObjectGroup(lotId)`: it registers the lot's `ScriptObjectGroup`, runs the `ObjectGroupSaving` listeners and the group's `PreSave` (which saves running task stacks through `SaveObjectStack`), then `AddObject(group)` and returns the group's **global** index.
- Per object, `SaveObject(objectId)` returns `AddObject(proxy.Target)`, also a global index.
- `EndSaveObjects` adds the `TaskStateCollection` and calls `End`, which serializes records in index order. Any object first reached while serializing a record is appended to the end of the list and serialized later in the same loop, so the order is: roots in lot order, then a breadth-first walk over references.
- Index assignment: `GetObjectIndex` looks the object up in `ReferenceHashtable mVisited`; on a miss `TryAdd` hands out the next number. Class indexes (`PersistedClassDB`, first use) and resource keys (`mResKeyToIndexTable`, first use) are numbered the same way.
- Layout of the result: header (version 1280, "OBJS" tag, class count, record count, offsets), the records, a u32 offset per record, the class table, the resource-key table [verified writer; matches the reader and `tools/harness/save-sources.ts`].

`IPersistGroupAffinity` (implemented by `ScriptObjectGroup`, `Sim`, `Availability` and a few autonomy types) only matters when some groups are **not** being saved: an object whose group is not in the save set is skipped or written through its group marshaler [verified]. In a full save every lot group is added, so this never splits the output. It looks like support for partial saves (travel or lot export) [inferred].

Record shape (IP day 13, 208k records): median 14 bytes, p90 69 bytes; 71% object records, 28% arrays [measured]. Most records are small collections and references, so most bytes are indexes.

## 2. Change tracking that already exists

| What | Where | Meaning | Usable for incremental save? |
|---|---|---|---|
| Lot thumbnails dirty | `ThumbnailManager.UpdateDirtyLotThumbnails` (internal call `0x778CC0`), `EditTownModel.UpdateDirtyLotThumbnails` / `SetDelayDirtyLotThumbnails`; save step 1 regenerates them [verified] | a lot's picture must be redrawn | No: rendering only; one thumbnail per save always changes (`0xD84E7FC6` changed in every pair) [measured] |
| Lot imposter rebuild | `lot+0x364`, checked in `Lot_Save` (docs/25) [verified] | distant-view model must be rebuilt | No: visual; closest thing to a per-lot "changed by building" flag |
| World "dirty lots" | `world+0x46` and the list "World/WorldManager/DirtyLots", processed and cleared by `WorldManager_LotRebuildCaller 0xC6BCC0` at save time [verified] | lots whose routing/terrain must be rebuilt after build or edit-town changes [inferred from the rebuild calls] | No: set by building, not by play; a lot can change a lot in play without being on it |
| Lot value dirty | `World.MarkLotWorthDirty` -> internal call `0x788170` -> lot lookup and `0xAB2400` [verified decompile] | lot value must be recomputed | No |
| Lot metrics | `Lot.mLotMetrics.Dirty`, `MakeLotMetricsDirty` [verified] | cached C# lot statistics | No: cache invalidation |
| Household name changes | `Household.mDirtyNameSimIds` [verified] | Sims whose names changed | No |
| CAS sim builder | `CASUtils.SimBuilderGetIsDirty` / `GetIsRigDirty` [verified] | CAS editing state | No |
| Interaction scorer | `InteractionScorer.mUseValuesDirty` [verified] | autonomy cache | No |
| Script objects, Sims, households, relationships, motives, inventories | none found (`dirty`, `changed`, `modified` searches over all decompiled assemblies) [verified] | - | - |

"Dirty" in most other hits is cleanliness (`CleanableComponent`, `BuffDirtySurroundings`). So the game has **no** change tracking for the state that makes up the save; the only per-lot flags are about building and rendering.

## 3. What two consecutive saves share

Tool: `tools/harness/save-diff.ts A B` (new, read-only): matches resources by type/group/instance across both saves, decompresses them and compares bytes; prints per type identical/changed counts, the share of on-disk bytes that are identical, and for the OBJS blob the common prefix and suffix. `DETAIL=1` lists every changed resource with differing byte counts.

| Pair | Gap | `.nhd` on disk identical | OBJS | Per-lot data |
|---|---|---|---|---|
| `Test-SaveProf.sims3.backup` -> `Test-SaveProf` | 71 s, game paused [inferred] | 31.68 of 31.71 MB | identical (9.25 MB) | identical; only the lot thumbnail differs |
| `Test-SW-1.sims3.backup` -> `Test-SW-1` | 47 s, paused [inferred] | all but the thumbnail | identical | identical |
| `Baseline-IP-day11` -> `day13` | 2 sim days, same load, speed 3 | 10.48 of 24.69 MB (42%) | all records changed | 0563919e: 119/119 identical; 046a7235: 62/119; OBJN: 1/139 identical (the rest index-only or real changes); REFS: 233/259 |
| `Baseline-IP-day3` -> `day5` | 2 sim days, same load | 10.02 of 23.68 MB (42%) | all changed | similar |
| `Baseline-BP.sims3.backup` -> `Baseline-BP` | 27 min apart, across a load [inferred from the diff pattern] | 11.96 of 31.71 MB (38%), almost all of it the world-copied MODL resources | all changed | **every** per-lot resource changed |
| `Cleanup-Test-BP` -> `Cleanup-Test-BP-le` | different loads + cleanup mod | similar | all changed | every lot changed |

[measured]

What this shows:

- **Deterministic output.** With no simulation between them, two saves are byte-identical, OBJS included [measured]. Hashtable order and similar do not randomize the output, so caching by content hash is sound.
- **Script indexes are global and shift.** In IP same-load lot object lists that otherwise did not change, the only differences are u32 values that step by one per object (for example a pair like 0x1775/0x1737, 0x1776/0x1738, ...) and are lower by the same offset after the change. They are the OBJS indexes returned by `SaveObject`/`SaveObjectStack` [measured; meaning verified against `LoadSaveManager.SaveObject` returning `AddObject`]. A new or removed object anywhere earlier in the visit order shifts every later index.
- **OBJS has almost no reusable records.** Comparing record i with record i: 0.1% (IP 11->13), 6.8% (IP 3->5), 5.8% (BP backup->save) identical. Content match anywhere in the file: 4.4% / 16.1% / 13.7%. Class tables differ too (2112 vs 2060 classes; class indexes are first-use order) [measured]. Even an unchanged object's record changes because its references and its class index changed.
- **Across a load nothing per lot is reusable.** In the BP pair, the wall/floor compositor `0x0563919E` (unchanged in 2 sim days of the same load) differs in ~40% of its bytes on every lot, and OBJN entries hold different large 32-bit values per object [measured]. These look like ids re-issued on load [inferred].

## 4. Would a partly reused save still load?

- **Format checks are weak.** `PersistReader.Load` checks only the version and the "OBJS" tag, then reads the counts and tables [verified]. There is no checksum over the blob. A wrong index loads silently as the wrong object, which is worse than a failed load.
- **Cross references are by index everywhere.** Records reference records by index; native OBJN entries reference OBJS records by index (`BindLoadedObject(proxy, index)`); saved task stacks are indexes into the `TaskStateCollection`; resource keys are table indexes [verified]. Reusing one lot's bytes requires every one of those to point at the same object as before.
- **Null records are legal.** The writer emits a null record (tag 0) when an object fails to serialize, and the reader turns it into null [verified writer; reader inferred]. So a stable-numbering writer could keep freed slots as nulls without a format change [inferred].
- **Post-load order.** `ExecutePostLoads` calls `IPersistPostLoad` (40 implementers) in table order [verified]. A writer that keeps old indexes changes that order (new objects at the end instead of in visit order). Some post-load code may depend on it [inferred risk].
- **Task stacks.** Running microthreads are saved by `ScriptObjectGroup.PreSave` -> `SaveObjectStack` with method checksums (`TaskStateCollection`) [verified]. Any Sim doing anything changes these on every save, so the lots with Sims on them are never clean.
- **Version stamps.** The OBJS version is fixed (1280); per-class field lists are stored in the class table, so a reused record is only valid together with the class table it was written with [verified].

Conclusion: a partly reused save loads correctly only if the reused parts keep exactly the same indexes for every referenced object, class and resource key. With the stock writer that holds only when nothing changed at all.

## 5. Where the time would go in an incremental save

| Part (BP) | Full save | What an incremental save could skip (2nd+ save of a load, ~1-2 sim days of play) |
|---|---|---|
| Script serialization | ~6.3 s | Without dirty tracking: nothing (the walk is needed to find new/removed objects and to compare). With stable numbering + dirty tracking: the unchanged lot-object subgraph. Lot-object classes are ~9-13% of records and ~20% of OBJS bytes (BP 1.84 of 9.0 MB, IP 1.72 of 5.5 MB) [measured by class]; about half of the lots are unchanged, and those are the small ones. Upper bound **~0.6-1 s** [inferred] |
| Native world write | ~4 s | Unchanged lots (~45% of lots, ~26% of per-lot bytes in IP): **~0.4-0.8 s** after the footprint fix, up to ~1.2 s before it [inferred]. The quadratic footprint scan (~1.2 s) is cheaper to fix directly (docs/25) |
| Compression | ~0.6 s | resources identical to last save: ~42% of bytes in IP -> **~0.25 s** [inferred] |
| Disk, copy, flush | ~0.8 s | nothing: the DBPF file is rewritten and copied whole |
| Wait | ~0.2 s | nothing |
| **Total** | ~12 s | **~1.5-2.5 s, only for the second and later saves of a load** |

## 6. Hierarchical dirty tracking (user proposal)

Proposal: hook interactions and object changes, log which objects actually change, group objects into a tree (world -> lot -> object group -> object) so a whole lot is checked as one unit, propagate "changed" up the tree, and save only changed subtrees.

**Verdict: workable only with strict limits, and not worth it on its own.** The natural unit (the lot) exists natively, and about half of the lots stay unchanged during play. But (a) the script state is one global graph whose indexes shift on any change, so no lot can reuse its bytes without first replacing the C# writer's numbering, (b) there is no reliable way to know a lot is unchanged without either hooking every field write or re-serializing it, and (c) the unchanged lots are the small ones: about a quarter of the per-lot bytes and, by inference, a similar share of the per-lot time. Realistic gain: ~1-2 s of ~12 s on the 2nd+ save of a load, after a large, risky rewrite. Options 1-3 in the summary table give more for less.

### 6.1 Natural units

- **Lot = native unit, yes.** `World_SaveLots 0xC62860` loops over all lots; `Lot_Save 0xACB8A0` writes one set of resources per lot (instance id = lot id) [verified]. A lot is a clean boundary for native data.
- **The existing lot dirty flags mean something else.** `world+0x46` + "World/WorldManager/DirtyLots" are routing/terrain rebuild requests after building (processed by `0xC6BCC0`, which calls `RoutingWorld_RebuildLot` and clears the list) [verified]; who sets them was not traced, the rebuild calls point to build/edit-town changes [inferred]. `lot+0x364` is the imposter rebuild flag. `MarkLotWorthDirty` is for lot value. None of them is set by Sims using objects, motives, aging or relationships [inferred from their callers' purpose].
- **Script state is not written per lot.** One `SaveObjectGroup` call per lot adds the lot's group to the one global writer; the group record holds references to its objects, their guids and task indexes (`ScriptObjectGroupData`) [verified]. One OBJS resource for the whole world.

### 6.2 Change detection options and their cost

| Option | How | Cost | Coverage |
|---|---|---|---|
| (a) Per-field write tracking | Hook the interpreter's store opcodes (`stfld`, `stsfld`, `stelem*`, `stind*`, `stobj`, `cpobj`, `initobj`) to set a dirty bit per object (side table keyed by object address or a header bit), plus every native internal call that writes into managed objects (fast-field readers, `Array.Copy`, `Buffer.BlockCopy`, string builders) | Mono 1.2 here is interpreter only; field stores are among the hottest opcodes. One extra test-and-store per store: a few percent of all script time, every tick [inferred]. Patching the interpreter's opcode handlers is deep native work (docs/re interp notes) | Complete only if every native writer is found. Missing one = silently stale data in the save |
| (b) Event-level hooks | Mark a lot/object dirty from interactions, buy/build, object create/destroy, inventory moves | Cheap | **Incomplete by design.** Changes with no interaction: motive decay on every Sim every tick (`Autonomy.Motive`, 4,202 records in BP), aging, daily relationship decay (`Relationship.DecayAll`), story progression moving and creating Sims and households, plant growth, food spoilage, `CleanableComponent` dirt, weather and seasons, alarms and their timers, autonomy state, saved task stacks of running microthreads [verified that these exist; e.g. docs/09 §1] |
| (c) Fingerprint per unit at save time | Hash the lot's native data or object positions/states and compare with the last save | For native data: hashing the produced bytes is cheap (~20 ms for 60 MB), but producing them is the cost. A fingerprint that does not produce the bytes (positions, counts) misses state changes. For script data: hashing means walking the same graph with the same reflection, i.e. the cost we want to avoid | Exact only when computed from the real output |
| (d) "Not visited since last save" | Skip lots no Sim entered and the player did not edit | Cheap to track (lot enter/leave events, build mode) | Wrong in this game: all lots are simulated in the open world, objects on empty lots still age/decay/spoil, story progression moves Sims into homes, and the lot's script records contain references (Sims, households, global managers) whose indexes shift anyway [inferred] |

### 6.3 The reuse blocker: object indexes

- Indexes are global and assigned in visit order (`mVisited.TryAdd` -> next number), with lots visited in lot-vector order and everything else reached breadth-first [verified]. One added or removed object renumbers every object visited after it; the IP diffs show exactly that [measured].
- A lot's group references objects outside the lot: objects hold references to Sims, `SimDescription`s, households, lots, global managers and event listeners (`EventSystem.DelegateListener`, 2,741 records in BP), and a Sim standing on the lot is in the lot group [verified types; extent inferred]. The first lot that reaches a Sim pulls the Sim's whole graph (relationships, life events, careers) into its index range.
- Could a group's bytes be reused? Only if (1) every object in the group and every object it references keeps its index, (2) the class table keeps every class index the bytes use, (3) the resource-key table keeps its indexes. That needs a writer with **stable numbering across saves**: keep last save's object -> index map (as a side table, not the visited set), give new objects new indexes at the end, write freed slots as null records, and keep the class and key tables append-only. That is a rewrite of `PersistWriter` (in ts3fixes or native), plus a rule for when to compact (numbering only grows during a load; a full save after each load resets it).

### 6.4 Numbers for the tree design

- **Lots that stay unchanged:** IP, 2 sim days of play in one load (about 48 min at speed 1 [inferred from 1 sim minute per real second]): 62-64 of 139 lot object lists changed only in script indexes; in those lots every other per-lot resource was identical. They hold 3.5-3.8 of 13.5 MB of per-lot data on disk (26-28%) [measured]. BP has no same-load pair with play yet; its 82 lots are bigger and busier, so expect a similar or lower share [inferred].
- **Share of save time they cost:** native per-lot write ~3.6 s (docs/25) x ~26-45% (by bytes or by lot count) = ~0.9-1.6 s, of which ~1.2 s x share is the footprint scan that docs/25 option 1 removes anyway; so ~0.4-0.8 s remains for the tree design. Script side: the unchanged lots' objects are a small part of the ~20% of OBJS bytes that lot objects make up; ~0.3-0.6 s, only with stable numbering [inferred]. Total **~1-1.5 s** of ~12 s, on 2nd+ saves only.
- **Effort:** native hook in `Lot_Save` to replay stored resources; stable-numbering writer; patching script indexes inside replayed OBJN records (the record layout is only partly known); dirty tracking that is provably complete. **Risk:** a wrong skip writes stale or dangling indexes, which load silently into wrong objects (no checksum).

### 6.5 Smallest safe first step

Measure before building anything; nothing below changes save data:

1. **Offline:** save twice in one BP load with ~30 min of normal play between (`Save As` to two names), then `node tools/harness/save-diff.ts <first> <second>` and the per-lot classification (`tools/harness/save-diff.ts` with `DETAIL=1`; the per-lot "index-only" rule used here is described in §3). This gives the real unchanged-lot share for BP.
2. **In game, log only:** a ts3native counter at `SaveCtx_EndResource 0x7D7840` that hashes each resource's uncompressed bytes per key, compares with the previous save of this load, and logs per lot "identical / changed" plus the time spent in each `Lot_Save`. That yields the exact seconds an incremental native write could skip.
3. If (2) shows more than ~1 s after the footprint fix, the first real step is the **compressed-bytes cache** (summary option 5): identical output, no format change, and it uses the same per-key hashes.

## 7. Evidence details

### 7.1 Code read

- `ScriptCore/LoadSaveManager.cs`: `BeginSaveObjects`, `SaveObject`, `SaveObjectStack`, `SaveObjectGroup`, `EndSaveObjects`, `BindLoadedObject`, `LoadObjectStack` [verified].
- `ScriptCore/PersistWriter.cs`: `AddObject`, `End` (header, records in index order, offset table, class table, key table), `GetObjectIndex` (visited lookup, group affinity, `IPersistPreSave`), `GetResourceKeyIndex` [verified].
- `ScriptCore/PersistReader.cs`: version and tag checks, `ExecutePostLoads` order [verified].
- `ScriptCore/ScriptObjectGroup.cs`, `ScriptObjectGroupFormatter.cs`, `ScriptObjectGroupData.cs`: per-lot group = object set + guids + task indexes, written as one record [verified].
- Native: `Lot_SaveObjectGroup 0xC6F520`, `Object_SaveRecord 0x939220`, `ScriptState_SaveSections 0x937100`, `World_SaveTerrainData 0xC2AEF0`, `IC_World_MarkLotWorthDirty 0x788170`, `WorldManager_LotRebuildCaller 0xC6BCC0` (Ghidra headless decompile, depth 0; output kept in `game-copy/analysis/decomp/`, not committed) [verified]. Rows added to [re/native-symbols.md](re/native-symbols.md) and [re/managed-methods.md](re/managed-methods.md).

### 7.2 Save diffs

Commands (read-only):

```
node tools/harness/save-diff.ts Test-SaveProf.sims3.backup Test-SaveProf
node tools/harness/save-diff.ts Baseline-IP-day11 Baseline-IP-day13
DETAIL=1 node tools/harness/save-diff.ts Baseline-BP.sims3.backup Baseline-BP
```

OBJS record comparison (record i vs record i, then content match anywhere, by SHA-1 of each record) and the per-lot "index-only" rule (every differing byte lies inside a u32 that is below 400,000 in both saves) were run as scratch scripts on the same readers (`tools/extract-assemblies.ts` `readIndex`/`refpack`, `tools/harness/save-sources.ts` `readObjs`).

### 7.3 Open points

- Who sets `world+0x46` and fills "World/WorldManager/DirtyLots" (writers of that list not traced).
- Exact layout of an OBJN object entry (which u32 is the object index and which the task index); needed only if option 6 is ever built.
- A BP same-load pair with normal-speed play (step 6.5.1).
- RefPack determinism (needed for option 5 to write byte-identical saves): the paused pair `Test-SW-1` differs on disk in 39,566 bytes, about the size of the one changed lot thumbnail (stored uncompressed, ~39 KB), so all other compressed resources came out identical [measured]. Same compressor build only; confirm once more after any compression change in `native/`.

## 8. Compressed-bytes cache (summary option 5)

Asked 2026-10-15: can the save reuse last save's compressed bytes for records whose uncompressed bytes did not change, and is that byte-for-byte what the game would write? Static check first (8.1, 8.2), then the build (8.3).

### 8.1 Which records are recompressed unchanged

- **Every save record passes the codec slot once, with its whole uncompressed bytes.** Records of class `0x12E4A892` commit themselves on close (`0x4AD9F0` -> `package->vt+0x3C` = `FUN_004A8E00`), `FUN_004A8910` reads the whole record stream into one buffer and `FUN_004A7030` calls codec slot 1 (`0xFB901C` -> `0x4EC200`) with that buffer [verified, docs/15 phase 10]. So the slot sees one input per record per save, and a record with the same uncompressed bytes gives the same input.
- **Volume per save** [measured]: compression is 0.85-0.9 s per BP save (v12 counters, PLAN.md); one BP save-matrix run (`docs/data/results/2026-10-15-queue-lane-g-2026-10-15.md`) logged `compressDirectCalls +13352`, `compressDirectMs +4092`, `compressDirectMB +484` over its saves, i.e. about 2,900 calls and 105 MB per 0.9 s save [inferred], in line with the 139 MB of `.nhd` record data offline (docs/15). Whether any record is compressed twice in one save (which would hit even on the first save) is not traced; the new counters show it.
- **What is unchanged between two saves** (§3) [measured]:
  - Back to back, paused: everything except the lot thumbnail (stored uncompressed), so ~100% reuse.
  - Same load, 2 sim days of play (IP): 42% of the `.nhd` disk bytes. Wall/floor compositor `0x0563919E` 119/119, routing footprints `0x046A7235` 62/119, REFS 233/259, all non-OBJN layers of the ~45% unchanged lots; ExportDB and TravelDB unchanged. Never reused: OBJS (one 9 MB record, ~40 ms alone) and OBJN (script indexes shift).
  - Across a load: no per-lot resource is reusable (ids change on load). MODL is copied from the world package (`Package_CopyResources 0x7D8F00`); whether the save recompresses it is not read [open].
- **Expected reuse** [inferred from the above]: ~0 on the first save after a load; ~40% of the compressed bytes on the 2nd and later saves of a load with play; ~100% for saves with no play in between (the save matrix's back-to-back case). In time: ~0.35 s of the ~0.9 s per BP save on 2nd+ saves with play, up to ~0.85 s with no play between, minus the hash (~30-40 ms per save at 3.3 GB/s, 8.3). This matches summary option 5 (~0.25 s on the older 0.6 s estimate).

### 8.2 Can the codec call be answered from a cache, byte-for-byte?

- **The call sees the input.** Slot `0x4EC200` (thiscall, `ret 14h`) gets `(src, len, dst, cap, flags)`; mode = flags bit 1 ? 1 : bit 16 ? 2 : 0; `dst` 0 with bit 0 returns the bound without compressing; else it returns `0x4EC0A0(dst, cap, src, len, *(codec+4), mode)` [verified disassembly, docs/15].
- **The output depends only on (input bytes, length, mode).** The compressor reads only its arguments and writes only `dst` and its scratch (no global data in `0x4EB340-0x4EC282`) [verified scan]; scratch contents do not change the output (507-653 records per save compressed with junk and with zeroed scratch: identical) [verified offline]; the codec object only supplies the allocator; `cap` is never checked. So a stored output for the same (bytes, length, mode) is exactly what the game would write, in any save and even across loads, as long as the compressor is the hash-checked Steam 1.67 code (`ApplySaveCompress` refuses anything else).
- **Only risk: a hash collision** (two different inputs with the same length, mode and 64-bit hash) would write another record's bytes. The hash runs two different bijective chains over every 32-bit word (4 lanes each), so inputs that differ in one word never collide, and other differences collide with probability about 2^-64 [inferred: no proof for structured data]. `Verify=1` compresses anyway and compares every hit; the save matrix runs with it first.
- **Verdict: yes.** Build it in the slot hook's direct path (the one every save record takes today), keyed by (length, mode, hash), storing the compressed bytes, LRU-bounded by `CacheMB`.

### 8.3 What we built (ts3native 0.10.3, `[SaveCompress] CacheMB=0` default off; built and selftested 2026-10-15, not run in game)

- Where: `ScSlotHook` (codec slot `0xFB901C`), outside a batch, for compress calls (`src` and `dst` set). Bound calls (`dst` 0, bit 0), size-only calls and the phase 10 batch path are unchanged. Needs `[SaveCompress] Enabled=1` (the slot hook and its install checks); `Mode` does not matter, since no batch runs in game.
- Lookup: hash the input (8.2; 3.3 GB/s offline on the i7-11800H [measured, selftest]), find (length, mode, hash) in a 16k-bucket chained table. Hit with output <= `cap`: copy the stored bytes to `dst`, move the entry to the LRU front, return its length; the game's compressor does not run. Miss: the game's slot runs and its output is stored (if `0 < r <= cap` and `r <= CacheMB/4`). Least recently used entries go when the held bytes exceed `CacheMB`. One SRW lock; the game's compressor and the hash run outside it.
- `Verify=1` (the existing `compressVerify` switch): every call runs the game's slot; a stored entry is compared with its output; a difference counts `cacheMismatches`, logs `save compress cache MISMATCH` (first 5), drops the entry, and the game's bytes are used.
- Memory: up to `CacheMB` of compressed bytes plus ~40 bytes per entry, held for the session (address space). One BP save is ~33 MB compressed, so `CacheMB=48` holds a whole save.
- Counters v17 (+0x27C, docs/registry.md): `cacheReady`, `cacheMode` (switch), `cacheHits`, `cacheMisses`, `cacheMismatches`, `cacheEvicted`, `cacheEntries`, `cacheKB`, `cacheHitMB`, `cacheHashMs`. Per save log (`[SaveWrite] Enabled=1`): `save compress cache: hits H (MB in), misses M, mismatches X, hash T ms, held N entries K KB, evicted E`; the existing `save compress: ... outside a batch (MB in, ms)` line then shows the time saved directly.
- Selftest (`selftest.exe`, fake compressor from `savecompress-fake.h` tests): 40 records twice, the second pass all hits with identical bytes and no game slot call; another mode, a too-small `cap` and a one-word change are misses; a corrupted entry under Verify counts one mismatch, is dropped and the game's bytes come back; a good entry under Verify is a hit; `cacheMode` 0 bypasses; 240 records under a 4 MB budget evict, never exceed the budget and keep the newest entry.

### 8.4 What the save matrix must test

Back up the saves first; `Enabled=1 CacheMB=48 Verify=1`, `[SaveWrite] Enabled=1`.
1. Back-to-back (paused, two saves): second save `hits` ~ all calls, `mismatches 0`; `.nhd` of both saves identical apart from the thumbnail (`save-diff.ts`).
2. Control: first save after load (expect few hits), then play ~30 min and save again (expect ~40% of MB as hits), `mismatches 0` in both.
3. Load each saved game: loads, same household and Sims; record by record the stored bytes decode to the same data as a `CacheMB=0` save at the same moment (pause, save with `cacheMode` 1, set it to 0 through the probe, save again, compare).
4. Travel save and autosave paths (all saves go through the slot), and a load of another save between two saves (cache entries survive; hits stay correct since keys are content only).
5. `Verify=0`: compare `compressDirectMs` per save with `CacheMB=0` for the timing; `cacheKB` stays <= 48 MB; no address-space failures in a long session (`[SaveCompress]` is held all session).

## 9. Cached reflection (option 3)

Question: which reflection runs per object or per field during a save, and which of its results stay the same per Type, so a cache can skip it with byte-identical output? Sources: decompiled `ScriptCore/PersistWriter.cs`, `PersistedClassDB.cs`, `PersistedClass.cs`, `Persist.cs`, `BCLDefaultFormatter.cs`, `LoadSaveManager.cs`, mscorlib `System/Type.cs`, `System.Collections/Hashtable.cs` [verified, code read], and the managed sampler capture of the BP save behind docs/15 (`probe-out/saveprof-managed-3.jsonl`, 2026-10-14, fast identity table off; 2,978 samples over 13.2 s, ~4.4 ms each; most samples hold only the leaf method, so callers are named from the code, not the stacks).

### 9.1 What runs, and how often

One save = one `PersistWriter` (`LoadSaveManager.BeginSaveObjects`), and each writer makes its own `PersistedClassDB` [verified]. So every per-Type result is rebuilt on every save: nothing is kept between saves today.

| Work | Where | How often | Samples (~s) | Stable per Type? |
|---|---|---|---|---|
| `Persist.GetPersistedTypeCode(type, db)`: `Reflection_GetTypeClassification` (internal call), `Type.GetTypeCode` (`UnderlyingSystemType`, `IsSystemType`, `TypeHandle.Value`, all interpreted, then an internal call), for classes two `Reflection_IsTypeAssignableFrom` calls, then `db.GetPersistedClass` | `GetObjectIndex` (each new object), `SaveBaseObject` (each object), `WriteTypedField`/`WriteField` (each element of lists, dictionaries, typed fields), `WriteElementType`, `SaveObjectAsType` (arrays, enums), `PersistedClass.InitFieldList` (each field) | several per object | see next row | yes, within one DB (8.2) |
| `PersistedClassDB.GetPersistedClass(type, req)`: `Hashtable` `mClasses[type]`, whose hash is `Type.GetHashCode()` = `(int)_impl.Value` (interpreted `RuntimeTypeHandle.get_Value`) | from `GetPersistedTypeCode` above, `GetObjectIndex` (group affinity check), `SaveObjectAsType` `ObjectInst` and `EnumInst` (each object and each enum value) | 2-4 per object | together with the row above: `Hashtable.get_Item` 118, `GetHash` 16, `Type.GetHashCode` 15, `RuntimeTypeHandle.get_Value` 93, `Type.get_IsSystemType` 42 = **284 (~1.25 s)** [measured leaves; caller split inferred] | yes, within one DB (8.2) |
| `Object.GetType` | `GetObjectIndex` (twice), `SaveBaseObject`, `SaveObjectAsType` | 3-4 per object | 123 (~0.54 s) | the call itself is per object; not cached |
| `MonoType.GetElementType` | arrays: `GetPersistedTypeCode`, `SaveObjectAsType` `ArrayInst`, `WriteElementType` | per array | 29 (~0.13 s) | partly (the type code path) |
| `MonoField.GetValue` (+ `GetValueInternal`, `IsStatic`) | `SaveObjectAsType` `ObjectInst`, fields the native fast path does not take | per object x slow field | 43 (~0.19 s) | no: reads the value |
| `PersistedClass.InitFieldList`: `MonoType.GetFields`, per field `GetCustomAttributes` (`IsFieldPersistable`), `GetEvent` (private delegate fields), `GetTypeClassification`, `Serializer_LookupFieldOffsets` | first `GetPersistedClass(type, true)` per type per save | per type per save | 66 (~0.29 s) | across saves too [inferred] (8.4) |
| `PersistedClass.Init`: base-type `GetCustomAttributes(PersistableAttribute)`, `GetConstructor`, formatter construction | first `GetPersistedClass` per type per save | per type per save | ~10 (~0.04 s) | across saves too [inferred]; not worth it |

So the "~1.3 s of reflection" in docs/15 is mostly not `GetFields`/`InitFieldList` (once per type, ~0.3 s) but the per-object type questions (~1.25 s), which repeat the same answer for the same Type thousands of times in one save.

### 9.2 Why the per-object answers can be cached exactly

For one `PersistedClassDB`, after a `GetPersistedClass(T, req)` or `GetPersistedTypeCode(T, db)` call has returned [verified, code read]:

- `GetPersistedClass(T, false)`: the entry exists, `Init` ran once (`mPersistabilityTested`), `mPersistable` is final, so a later call returns the same object (or null) and changes nothing. With `req = true` the class index was assigned and the field list built, so later `req = true` calls are pure too. A null answer (not persistable) is returned before the `req` branch, so it stands for both.
- `GetPersistedTypeCode(T, db)`: depends only on T (classification, type code, assignability) and on `GetPersistedClass(T, ...) != null` (or the element type's code for arrays). The first call already did the side effects (class entry created; for enums the class index assigned through `GetPersistedClass(T, true)`), so later calls are pure. The `typeFlags` out value is the classification of T. `Persist.RequireAllPersistable` and `EnableTypeRemapping` do not change either answer (the first only switches `Yes`/`Enforced` in `InitFieldList`, the second only affects loading).
- Exception: **calls made while `PersistedClass.Init` runs.** `Init` sets `mPersistabilityTested = true` first and decides `mPersistable` later, and it can call back into the DB (`BCLDefaultFormatter.CreatePersistFormatter` asks `pclassDB.IsPersistable` for each generic argument; `CheckGroupAffinity` and custom formatters run constructors). A nested call for a type whose `Init` is still running can see "tested, not persistable" and return null where every later call returns the class. Such answers must not be cached.
- `mClasses` values are never replaced, and `mIndex`/`mFields` are only written by the DB and `Init`/`InitFieldList`/`Read` (`Read` only on the loader's own classes) [verified].

So a cache per DB that is filled only after the game's method returned, never while an `Init` runs, and keyed by Type identity returns exactly what the game would, and the class index order (the only order-dependent output: the class table and the indexes in records) is unchanged, because every first call still runs the game's method in the same order [inferred from the code; checked by the startup probe and the offline model test, 8.3].

### 9.3 What was built: ts3fixes `kCachedReflection`

`mods/ts3fixes/src/CachedReflection.cs` and `TypeCache.cs`, switch `cachedreflection` (tuning `kCachedReflection`, default off), counters [71] caches started with the switch on (one per save), [72] answers from the cache, [73] answers stored ([registry.md](registry.md)).

- Hooks (method header swap, as RefTable.cs): `Persist.GetPersistedTypeCode(Type, ref ReflectionTypeFlags, PersistedClassDB)` (the 2-argument overload calls it), `PersistedClassDB.GetPersistedClass(Type, bool)`, and `PersistedClass.Init(Type, PersistedClassDB)` only to count running Inits (`TypeCache.sBusy`).
- `TypeCache`: one open-addressing table per DB keyed by the Type object's identity (`RuntimeHelpers.GetHashCode`, like `FastRefTable`), holding the type code + flags and the class answer with "seen for `req` false / true" bits. A new DB starts a new cache and reads the switch, so a switch flip applies at the next save. Calls without a DB (the loader) and a null type always run the game's method. A second Type object for the same type (not expected in Mono [inferred]) would only miss.
- Startup probe: serializes a test graph (persistable class and struct with int, string, enum, struct, `ResourceKey`, enum array, object ref, `[Persistable(false)]` field, boxed values, `List<T>`, `Dictionary<string, T>`, `List<object>`) through the game's own `PersistWriter` with the cache forced off and then on, and requires identical bytes and cache hits. Any failure or a missed patch leaves the fix off (log line `CachedReflection probe ...` in `ts3fixes.applied`). Not yet run in game.
- Offline check: `node tools/selftest/typecache/run.ts` drives a model of the DB (entry creation, `Init` that marks tested before deciding and calls back, index on first `req`, field list with nested calls) plain and through the hook logic: 120 random sequences give identical answers and index order; with the `Init` guard removed the model finds a mismatch in 40 of 40 sequences, so the guard is needed [measured offline].

Expected gain [inferred, not measured]: the hit path is one identity probe plus a few field reads instead of ~5 interpreted calls, 2-4 internal calls and a `Hashtable` probe, so most of the ~1.25 s goes: **~0.6-1.0 s per BP save**. Not cached: `Object.GetType` calls, field value reads, and `GetPersistedClassRef` (group objects, `Type` values, array element types).

### 9.4 Next step, not built: field lists across saves

`InitFieldList` (~0.29 s) and `Init` (~0.04 s) rerun per type on every save because the DB is new each time. Their reflection inputs are stable for the whole session [inferred: types and attributes do not change after load]. A cross-save cache would hook `PersistedClass.InitFieldList(PersistedClassDB)` and keep, per Type, the `GetFields(Instance | Public | NonPublic)` array already filtered by `IsFieldPersistable(GetEventForField(f) ?? f) != No`, with each field's `FieldType`, classification and unnullified type. The hook must still, per field and in the same order, call `Persist.GetPersistedTypeCode(unnullifiedType, ref flags, db)` and, for value-type `ObjectInst` fields, `db.GetPersistedClass(unnullifiedType, false)` (both have class-index side effects), and still call `Serializer_LookupFieldOffsets`, then write `mFields`, `mFastFields`, `mMaxFastFieldsSize` of the internal `PersistedClass` (through `FieldInfo.SetValue`, once per type per save, or a mirrored field layout). Gain at most ~0.25 s (`GetFields` 39 + attributes 13 + `GetEvent` 1 of 66 samples); do it only if a measurement after 8.3 still shows `InitFieldList` near the top.

### 9.5 What the save matrix must test

`node tools/harness/save-matrix.ts` now switches `cachedreflection` like `fastreftable` (ready only when the startup probe passed). Before turning the default on:

- `identical`: off vs on in one paused load, OBJS and every stable resource byte-identical, BP and IP, with and without `fastreftable`.
- Second and later saves in one load (a new DB and cache per save; types first met in a later save, e.g. after buying or placing new objects, opening CAS, or a new household moving in).
- Saves right after travel and back (other-world types, `MiniSim` data), Save As, edit-town save, quit-and-save.
- Reload each on-save and play: load, then save again and compare with an off save of the same load.
- Mods that add persisted types (NRaas suite) loaded, so custom formatters and generic types go through the cache.
- Counters per save: [71] +1 per save (more means another DB was used in between: cost only), [72] hits much larger than [73] stored; and script serialization time off vs on (docs/15 save timing) to replace the 8.3 estimate.
