# 64-bit resource offload helper: resource system map and design

Written 2026-10-11 against the Steam build of TS3W.exe (1.67, SHA-256 `10b2423e…0609`, image base `0x400000`, no ASLR). Static analysis only: Ghidra (read-only), the exe bytes, the decompiled C# scripts, and read-only scans of DBPF headers and indexes. The game was not touched.

Confidence tags: **[verified]** means read in the decompiled code, the bytes or the files. **[community]** means taken from a mod page, a README or other people's source. **[inferred]** means reasoned from code and still to be confirmed by measurement. **[to measure]** means it needs a run of the game.

---

## 0. Verdict first

The resource system does **not** grow the way the original idea assumed. Three findings change the plan:

1. **Package indexes are small.** The engine stores a read-only package's index in a compact table of about **17.6 bytes per resource** [verified layout, 17.6 B is our computed average]. Rodney's whole CC set (172 packages, 209,735 resources) needs about **3.5 MB** of index memory, and all EA packages together about **15 MB**. Moving indexes out of the process saves almost nothing. Phase 1 as first planned ("index offload") is not worth building on its own.
2. **Packages are not memory-mapped.** Every resource is read with `ReadFile` into a heap buffer, decompressed into a second heap buffer, turned into an engine object by a factory, and the buffers are freed [verified]. The only `MapViewOfFile` call in the exe belongs to Mono's assembly loader [verified]. So 5 GB of CC on disk does not cost 5 GB of address space; what costs address space is the decoded resources that stay alive.
3. **There is a bounded, configurable resource cache.** `ResourceSystem/ResourceCache` keeps decoded resources for reuse with a budget read from `[Resources] CacheBudget`, which `Game\Bin\Default.ini` sets to **209,715,200 bytes (200 MB)**, plus an overflow budget (`CacheOverflowBudget`, default 10 MB). A separate animation cache is fixed at 15 MB [verified values; eviction policy inferred]. This cache is the one big, tunable, in-process block the helper can take over.

What a 64-bit helper can realistically win is therefore **the resource cache budget (up to about 150 to 200 MB of address space, if the in-process budget is lowered and the helper serves misses fast), fewer large transient buffers, and less time spent holding the global resource lock** (stutter). It cannot move resources that live engine objects still reference (meshes of objects in the scene, CAS parts, string tables, catalog data): those are parsed C++ objects full of pointers and must stay in TS3W.exe.

The things that really grow with CC are the **catalog and CAS data built for every CC item at startup**, and the **string tables**. Those are reached through other fixes (section 5 and 8), not by an offload of raw resource bytes.

**Recommended first step:** Phase 0 measurement (section 7), starting with a no-code experiment that lowers the engine's own cache budget and watches address space and hitches. Build the helper only if that shows the cache is a large share of address space under CC and that a smaller cache causes measurable re-read stutter.

---

## 1. The user's CC set, measured

Read-only scan of `D:\Sims3-backup\2026-10-11\Documents-The Sims 3\Mods` (headers and indexes only, scripts in the session scratchpad, `cc-index-stats.ts`, `idxcost.ts`, `stbl.ts`) [verified]:

| Measure | CC set | All EA packages in the Steam install, for scale |
|---|---|---|
| Packages | 172 | 267 |
| Resources (index entries) | 209,735 | 916,386 |
| Size on disk | 5,187 MB | 20,531 MB |
| Sum of decompressed sizes | 15,446 MB | (EA `memSize` fields are not usable) |
| Index on disk | 6.4 MB | 27.9 MB |
| **Index in memory, engine layout** (16 B per entry, +8 B when a size is ≥ 0xFFFF, +16 B per type/group pair) | **3.5 MB** (17.6 B/entry; 26,544 size overflows, 7,821 type/group pairs) | **15.3 MB** |
| CAS parts (CASP `0x034AEECB`) | 2,016 | 5,662 |
| Catalog objects (OBJD `0x319E4F1D`) | 3,906 | 10,671 |
| String tables (STBL `0x220557DA`) | 66,271, of which 3,187 English | 786 |

Three packages make up 95% of the CC bytes: `Sims 3 Store Updates 001-020 v1.4` (1,961 MB, 85,823 resources), `021-040` (1,977 MB, 78,524) and `041-051` (1,182 MB, 39,031). Everything else is under 70 MB in total. Note that `docs/07` §2.6 already excludes these "Store Updates" packages from the fix set because they are repacks of store content; that is Rodney's call, but they dominate every number in this table.

By decompressed size the CC is textures `0x00B2D882` 13,336 MB (20,532 resources), GEOM 922 MB, bone deltas `0x00AE6C67` 291 MB, `0x01A527DB` 289 MB, MLOD 188 MB, animation clips 142 MB, MODL 57 MB, texture compositor 38 MB, CASP 32 MB, STBL 31 MB, S3SA 27 MB. None of that is loaded up front; it is loaded when a lot, a Sim or a catalog page needs it.

The game's own cache files in the same backup are DBPF packages too: `simCompositorCache-<pc>.package` 251 MB (644 entries, 404 textures) and `compositorCache-<pc>.package` 99 MB (2,141 entries). Their indexes are tiny; their content is read on demand like any other package [verified counts, inferred access].

---

## 2. Map of the resource system in TS3W.exe

All addresses are Steam 1.67. Names are ours unless they come from EA's own allocation tags (the strings passed to every allocation, such as `"ResourceSystem/ResourceMan/Mgr/DBArray"`), which name most structures for us. The solid ones are now in `tools/ghidra/anchors.csv`.

### 2.1 Discovery: `Resource.cfg`

| What | Address | Evidence |
|---|---|---|
| Load `Resource.cfg` (`%s/GameData/Win32/Resource.cfg`, `%sWin32/Resource.cfg`, `Mods\Resource.cfg`) | `0x596450` (also `0x5961c0`) | string xrefs [verified] |
| Parse directives `PackedFile`, `DirectoryFiles`, `Priority` | `0x4a1bd0` | the only code that references the three tokens [verified] |
| Glob patterns (`ResourceConfiguration/PatternList`) | `0x4a2a70` | allocation tag [verified] |

Each `PackedFile` pattern becomes one `DatabasePackedFile` per matching file, registered with the manager at the current `Priority` [verified tokens; per-file registration inferred from S3MM's database walk, which sees one database per package]. Rodney's backed-up `Mods\Resource.cfg` mounts `Overrides` (1000), `Packages` (500), `Test` (499), `Probation` (-50) and **`DCCache/*.dbc` (500)**, so installed `.sims3pack` content in the DCCache `.dbc` files goes through exactly the same path as `.package` files [verified file].

### 2.2 The manager

| What | Address | Evidence |
|---|---|---|
| Manager singleton getter (returns `[0x11c78d8]`) | `0x4afd20` | decompile; S3MM uses the same address [verified, community] |
| Manager constructor (vtable `0xfb2da0`) | `0x4b33b0` | tags `Mgr/FISet`, `Mgr/DBArray` (+0x30/+0x34), `Mgr/TFMap`, `Mgr/CArray` (+0xa0/+0xa4), `Mgr/OKNMap`, `Mgr/TNMap` [verified] |
| `GetResource(useCache, key, out res, …)` (slot `0x18` → `0x4b0810` → here) | `0x4b0480` | decompile [verified] |
| `FindDatabase` (slot `0x44`) → `GetPackageSource` (slot `0x40`) | `0x4afda0` → `0x4affc0` | decompile [verified] |
| `FindCache` (slot `0x58`), `CacheResource` (slot `0x5c`) | `0x4b0070`, `0x4b00c0` | decompile [verified] |
| `GetKeyList(out keys, filter)` (slot `0x20`) | `0x4b1ae0` | used by the Preloader and by `CASPartCacheService::Init` [verified call sites] |
| `RegisterDatabase(add, db, priority)` (slot `0x34`) | `0x4b2d00` | S3MM's vtable table [community], call in `0x5ae170` [verified] |
| Resource system init (caches, device drivers, auto-update DDFs) | `0x738670` | tags `ResourceCache`, `AnimationCache`, `RWFileSysAllocator`, `DBDFAutoUpdate` [verified] |

**The lookup path** (`0x4b0480`) [verified]:

1. Take the **global resource lock** `0xfb2cd0` (`Lock_Acquire` `0x4e16f0`). It is held for the whole call, including the file read, the decompression and the factory.
2. If `useCache`: `FindCache(key)` walks the cache array and asks each cache (`Cache::Find`, `0x7324e0`). A hit returns the existing object (refcount +1) and ends.
3. `FindDatabase(key)` → `GetPackageSource` walks the **DBArray in priority order and probes every database** (`Database::OpenRecord` with a "test only" flag) until one has the key. This is a **linear scan over all registered packages** per cache miss: about 440 hash probes with Rodney's CC plus EA. That is the verified mechanism behind the community advice to merge packages (CPU per miss, not memory).
4. Find the factory for the key's type (slot `0x2c`), open the record (`Database` slot `0x34`), let the factory build the object (`Factory` slot `0x1c`), then `CacheResource(res)`.

The key is `{instance_lo, instance_hi, type, group}`, 16 bytes; the manager reads the type at `key+8` [verified in `0x4b0750`, matches S3MM].

### 2.3 Packages, the index and reading a resource

| What | Address | Evidence |
|---|---|---|
| `DatabasePackedFile` constructor (object is `0x398` = 920 bytes; vtable `0xfb2600`) | `0x4a8f70` | `FUN_004e3db0(0x398, …)` / `FUN_004e41f0(0x398, …)` at both creation sites [verified] |
| Header check: `'DBPF'`, major < 3, index count < 0x7ffffff, index inside file | `0x4a7ad0` | decompile [verified] |
| Load the index into the compressed layout | `0x4ad020` | decompile, tag `ResourceLoad/PackedFile/IndexCompressed` [verified] |
| Index object (`IndexTable`, `SizeOverflowTable`, `TypeGroupTable`) | `0x4aaf50` | tags [verified] |
| Modifiable index (hash map of 0x24-byte nodes, for writable packages) | `0x4abd70`, `0x4ac240` | tags `IndexMap`, `IndexPair` [verified] |
| `OpenRecord(key, out record, mode, …)` (vtable slot `0x34`) | `0x4a94c0` | decompile [verified] |
| Read + decompress into a caller buffer | `0x4a8380` → `0x4a7360` | decompile [verified] |
| File open (`CreateFileW` wrapper; S3SS's random-access patch sits in its flag logic) | `0x4db4c0` | decompile; S3SS `0x4db56c` [verified, community] |
| File read (`ReadFile`) / seek (`SetFilePointerEx`) | `0x4db850` / `0x4db6b0`, `0x4db740`, `0x4db7d0` | IAT call sites [verified] |
| Write or compact a package through a `TempDBPF_` file (saves, caches) | `0x4a9150` | decompile [verified] |

**Index layout in memory** (`0x4ad020`) [verified]: for each entry 16 bytes `{instance_lo, instance_hi, offset, u16 fileSize, u16 memSize}`; when either size is 0xFFFF or more, the u16 pair becomes `0xFFFF` plus an index into an 8-byte overflow table. Entries are grouped by (type, group) in a 16-byte-per-pair table with a small hash. The raw on-disk index is read into a temporary buffer (`ResourceLoad/PackedFile/IndexImage`) and freed after conversion.

**Reading one resource** (`0x4a94c0`) [verified]:

- Look the key up in the package index.
- **Compressed** (compression field `0xFFFF` = RefPack): allocate `memSize` bytes (`ResourceLoad/PackedFile/RecordData`), allocate `fileSize` bytes (`…/Raw`), `ReadFile` the compressed bytes, decompress, free the raw buffer, and wrap the decompressed buffer in a memory record.
- **Uncompressed, ≤ 512 bytes**: read into a heap buffer.
- **Uncompressed, > 512 bytes**: return a streaming record that reads from the file on demand (vtable slot `0x74`).
- Open records are tracked per package (`RecordMultiMap`) and freed when the consumer releases them.

So the peak transient cost of one compressed resource is `fileSize + memSize`, all on the game's heap (with S3SS's mimalloc hook, on mimalloc). Large textures and meshes make these buffers several MB each [inferred from the size tables].

### 2.4 RefPack

| What | Address | Evidence |
|---|---|---|
| Decompress entry (checks the `0x10FB` signature, then calls the core) | `0x4ec010` | decompile; S3MM's `RefPack_Decompress` [verified, community] |
| Decompress core (S3SS replaces it with an AVX2/SSE2 version) | `0x4eb3b0` | S3SS pattern [community], decompile [verified] |
| Compress (writes the game's caches and saves) | `0x4ec0a0` | tag `Services/UTFFoundation/CompressionRefPack` [verified] |
| Other decompressors with their own error strings | `0x4350a0`, `0x43daa0` | `Decompression error` strings [verified, purpose not checked] |

### 2.5 Caches

| Cache | Where | Budget | Evidence |
|---|---|---|---|
| **ResourceCache** (generic decoded resources) | created in `0x738670`, class ctor `0x732910` (vtable `0xffdd94`), registered with priority 0 | `[Resources] CacheBudget` (ctor default 0; **`Default.ini` sets 209,715,200**) at `+0x8c`; `CacheOverflowBudget` (default `0xa00000` = 10 MB) at `+0x90`; current bytes at `+0x94` | decompile + `Default.ini` [verified] |
| **AnimationCache** | same class, priority 1000 | budget forced to `0xf00000` = 15 MB | decompile [verified] |
| Debug variables | `0x733140` registers `"Cache size"` (0 to 500,000,000) and `"Cache overflow size"` under the VarMan category `"Resource system"` | | decompile [verified]; whether S3SS's variable editor lists them is [to measure] |
| CAS part cache | `CASPartCacheService::Init` `0x5aedf0` asks the manager for **every CASP key in every package** (`GetKeyList` with type `0x034AEECB`), validates and fills `CASPartCache.package` (`0x5ae170`, its own `DatabasePackedFile`); in-memory lookups in `CASCache::GetCachedPart` `0x5a44a0` | none found | decompile [verified]; memory per part [to measure] |
| Compositor caches | `compositorCache.package` (`0x6cbf90`), `simCompositorCache.package` (`0x5eb9c0`), `CAS/CompositorCache/*` (`0x5bb6a0`–`0x5bc6d0`, MRU list) | `[CAS] CompositorCacheSize` 100 MB, `SimCompositorCacheSize`, `WorldCompositorCacheSize`, `SimWorldCompositorCacheSize` 500 MB each (`Default.ini`) | strings + `Default.ini` [verified]; these look like **on-disk** cache file limits [inferred] |
| Audio | `Audio/ResourceCacheMap` (`0x8c0720`), `BinaryResourceCacheSizeMB` (`0x8b68e0`) | not read | strings [verified] |
| DCCache index for `.sims3pack` | `ResourceSystem/DatabaseCache/*` (`0x984b60`, `0x98b5e0`, `0x98bd50`), `dcc.ent` (`0x98e580`), `missingdeps.idx`, `DCCache` path (`0x98c980`) | | strings [verified] |

How the generic cache behaves [inferred from `0x7324e0`, `0x732a00`]: it maps keys to resource objects and counts their size. A cache hit on an object that only the cache still holds takes it off the flush list. Objects nobody references go onto a flush list and are dropped when the cache is over budget. In other words, `CacheBudget` bounds **decoded resources kept around after use**, not resources in active use.

A side note: S3SS's "WorldCache 512 MB uncap" patch has its Steam address commented out, at `0x5bc8b4`. That address lies inside the `CAS/CompositorCache` function `0x5bc6d0`, and 512 MB is the `*CompositorCacheSize` value `524,288,000`. The patch most likely lifts a compositor cache file limit, not a world cache [inferred].

### 2.6 Asynchronous loading and the Preloader (what the `Sims3.ini` keys do)

| Setting | Read at | Meaning |
|---|---|---|
| `[ResourceSystem] MemoryUsageLimit = 20000000` | `0x7274d0` (AsyncLoader init, vtable `0xffd388` slot 1), stored at AsyncLoader `+0x38`; constructor default `0x1400000` (20,971,520) | It belongs to the **AsyncLoader** (`ResourceSystem/AsyncLoader`, `mIOPendingQueue`, `Requests`, `JobsToCancel`), not to the resource cache [verified]. The unit is bytes. Most likely a cap on bytes of asynchronous loads in flight [inferred; the users of `+0x38` are not decoded yet]. It is not per type, and nothing is evicted at the limit; requests presumably wait. |
| `[ResourceSystem] AllowAutoUnload`, `LogRWFSCalls`, `ReadQuantum`, `ThrottleRate`, `ThrottleInitialDelay` | `0x738670` | Other keys the engine reads [verified names]. `LogRWFSCalls` sounds like a file-system call log, which would be a free Phase 0 tool [to measure]. |
| `[Preloader] Enabled` | `0x731ae0` | Turns the Preloader on [verified]. |
| `[Preloader] HoldOntoResources` | `0x731ae0`, stored at `+0xc`, **default true** | The engine reads `HoldOntoResources` (plural). `Sims3.ini` says `HoldOntoResource` (singular), so **that line is ignored** and the default (true) applies anyway [verified spelling]. "Hold on" means preloaded resources stay referenced in `ResourceSystem/PreloadedResources` and are never evicted [inferred]. |
| `[Preloader] KeyFilter…` | `0x731ae0` → `0x72ad90` enumerates **every key in the section that starts with `KeyFilter`** and parses it into `ResourceSystem/KeyRanges`; the matching keys come from `Manager::GetKeyList` and go into `ResourceSystem/PreloadQueue` | [verified]. The format is `type:group:instance` (the three shipped lines preload two bicycle models and a car model, type `0x01661233` MODL). Whether ranges or wildcards (whole types) are accepted is in the parser `0x72acc0` [to decode]. |
| `[Preloader] ResourceList` | `0x731ae0` | A second way to list keys [verified name]. |

So the Preloader can pin chosen resources in memory at startup. That costs address space, so it is the opposite of what we need in-process; it only becomes interesting for warming the helper's cache (section 9).

### 2.7 Threads

The AsyncLoader and the `Timeslices/ResourceSystem` budget show that some loads are queued and finished over several frames [verified names]. But every synchronous `GetResource` (the path scripts and most engine code use) reads, decompresses and builds the object **inside the global lock on the calling thread** [verified lock span]. Whether the JobManager's `JobThread%i` workers decompress anything is not established: no `JobManager` call appears in the paths above [verified for these paths only].

---

## 3. What grows with CC, and by how much

| Item | Grows with | Per unit | Rodney's CC set | Confidence |
|---|---|---|---|---|
| Package index | resources | ~17.6 B (+ allocator headers, small hash tables) | **~3.5 MB** (EA: ~15 MB) | layout [verified], total [computed] |
| Package objects | packages | 920 B object + file handle + index object, roughly 1 to 2 KB | **~0.3 MB** for 172 packages | [verified size of the object; rest inferred] |
| Lookup cost per cache miss | packages | one hash probe per package | ~440 probes per miss (CC + EA) | [verified mechanism] |
| Transient read buffers | resource size | `fileSize + memSize`, freed after the factory runs | peaks of several MB per large texture or mesh; fragmentation risk | [verified mechanism, size inferred] |
| Generic ResourceCache | decoded resources kept after use | up to `CacheBudget` = 200 MB (+10 MB overflow) | fills faster with CC, capped at ~210 MB | [verified budget; fill level to measure] |
| CAS part data | CASP count | built for **every** CASP at startup | 2,016 CC parts on top of EA's 5,662 (+36%) | [verified enumeration; bytes to measure] |
| Catalog (buy/build) | OBJD and catalog resources | catalog entries, thumbnails while the catalog is open | 3,906 CC objects on top of 10,671 (+37%) | [inferred; to measure] |
| String tables | STBL tables in the game language | small text, but the Performance Patch reports an oversized layout per table (~290 MB saved on 10,000+ tables, about 29 KB each) | 3,187 English tables, 1.5 MB of text; at ~29 KB layout each that would be **~90 MB** | [community figure applied to our count; inferred] |
| Compositor caches | Sims and outfits shown | on-disk caches up to 100 MB / 500 MB; in-memory MRU | disk files 99 MB and 251 MB | [verified limits; in-memory share to measure] |
| Textures | everything shown | mostly moved out by DXVK | measured 3.49 GB → 1.74 GB committed on the plain save | [measured, `05`] |

The decoded resources that stay alive (meshes of the objects on screen, rigs, clips in use, CAS parts, string tables, catalog data) are the real growth. They are engine objects with internal pointers and must stay in TS3W.exe.

---

## 4. Prior art and overlap

| Work | What it does here | Overlap with the helper |
|---|---|---|
| **S3SS `RefPackDecompressor`** | Replaces the decompress core `0x4eb3b0` with AVX2/SSE2 code [community, source read] | Same data path. The helper would bypass it for served resources; keep S3SS on for the fallback path. Hook above it (`0x4a8380`), never on the same bytes. |
| **S3SS `CreateFileRandomAccess`** | Sets `FILE_FLAG_RANDOM_ACCESS` in the file wrappers (`0x404573` flags init, `0x4db56c` branch) [community, source read] | Only an I/O hint. Irrelevant for packages the helper serves; harmless otherwise. |
| **S3SS `WorldCacheSizePatch`** | JC→JMP on a 512 MB cache size check; Steam address commented out (`0x5bc8b4`) [community] | Most likely a compositor cache file limit (2.5). No overlap. |
| **S3SS mimalloc** | Replaces `MSVCR80` malloc/free [community] | Our transient-buffer change must allocate the same way the engine frees (through the engine allocator object, `param_1[4]` vtable `+8`/`+0xc`). |
| **Sims 3 Performance Patch (Nexus 235)** | `PackageIndexWarmup`: at the main menu, reads the Mods packages' tables of contents into the **Windows file cache** (load time, no address-space effect). `StblBucketLayoutTuning`: right-sizes the per-table layout of string tables, ~290 MB saved, text byte-identical. `ThreadStackDiet` ~70 MB. `LocalizedStringCache`, `MonoIsinstCache` [community, review quoting the mod page; source reportedly in the download, not read] | Index warm-up overlaps the helper's startup index read; with the helper running, it is redundant but harmless. STBL tuning is the single biggest CC-related memory win available and needs no helper. |
| **Sims3MonoModder (sims3fiend)** | Walks the manager's DBArray (`+0x30`/`+0x34`), knows the manager vtable slots, has a resource browser and its own DBPF reader [community, source read] | Our addresses agree with its Steam fallbacks (`0x4afd20`, `0x4ec010`). Reuse its vtable map; do not depend on it at runtime. |
| **LazyDuchess** | MonoPatcher (script method replacement), Smooth Patch, thread monitor; nothing in the resource system [community, `03` §3] | None. |
| "Resource offload" or "memory-mapped CC" tools | None found in searches of Nexus, MTS, GitHub and forums (2026-10-11) [verified absence in what was searched] | The helper would be new. |

---

## 5. Community fixes that already reduce CC cost

| Fix | Effect on the problem | Status in our setup |
|---|---|---|
| **DXVK** (`07` §1.2) | Texture memory out of the process: 1.75 GB measured | Installed; baseline layer 3. |
| **Performance Patch `StblBucketLayoutTuning`** | ~290 MB on big saves; scales with STBL count, so it scales with CC | Optional in `07` (beta). Strong candidate for heavy CC. |
| **Thread stack fixes** (S3SS or the Performance Patch, not both) | 70 to 170 MB | S3SS on. |
| **Merging packages** (S3PE / Sims 3 Package Merger) | Fewer databases: fewer probes per cache miss and fewer file handles; memory effect tiny (section 3) | Rodney's CC is already dominated by three merged packages. Merging the 169 small ones would cut the probe count, not memory. |
| **Cache clearing** (`07` §4.4) | Keeps `CASPartCache`, compositor caches and `DCCache\dcc.ent` consistent after CC changes | In the installer. |
| **Not installing `.sims3pack` into DCCache when a `.package` version exists** | The DCCache `.dbc` path is the same database path; no gain from either format | Advice only. |
| **Catalogue Lag and Memory Leak Fix** (Nexus 242, `07` §2.3) | A LAYO-only fix for catalog UI lag and a leak [community] | In the installer. |

---

## 6. Design of the 64-bit helper

### 6.1 What the helper is for, given the findings

The helper owns **bytes**, never engine objects:

1. a merged, read-only index of every read-only package the game mounted (key → winning package, offset, sizes), built in parallel at startup;
2. file handles to those packages, read with large, unbuffered or overlapped I/O;
3. RefPack decompression on a thread pool;
4. a large decompressed-bytes cache (an L2 behind the engine's ResourceCache), limited by Rodney's 32 GB of RAM, not by 4 GB of address space.

The game keeps its own index and objects (they are cheap). The address-space win comes from **lowering the engine's `CacheBudget`** once misses are cheap, and from **not allocating the raw compressed buffer** in-process. The time win comes from **shorter hold times on the global resource lock** (no disk read and no decompression while holding it).

### 6.2 Architecture

```
TS3W.exe (32-bit)                                   ts3res64.exe (64-bit helper)
 ├─ ts3offload.asi (our ASI)                         ├─ package table (path, size, mtime, handle)
 │   ├─ detours (6.3)                                ├─ merged index: key -> (pkg, offset, fileSize, memSize, comp)
 │   ├─ request ring client                          ├─ I/O threads (overlapped ReadFile)
 │   └─ fallback switch                              ├─ RefPack pool (N = cores - 2)
 │                                                   └─ decompressed cache (LRU, e.g. 4-8 GB)
 └──── shared memory ─────────────────────────────────────┘
        control section (1 MB): request/response slots, sequence numbers, heartbeat
        data window (8 MB, ring): decompressed payloads, copied by the game into its own buffer
```

- The ASI starts the helper at DLL attach (`CreateProcess`), puts it in a job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` so it can never outlive the game, and waits up to a few seconds for its heartbeat. If the helper does not come up, the ASI installs no detours.
- Package identity comes from the game, not from re-parsing `Resource.cfg`: the ASI detours package registration (`RegisterDatabase`, slot `0x34`, `0x4b2d00`) and sends `(database pointer, path, priority, read-only flag)` to the helper. The path comes from the database's `GetPath` (vtable `0x28`, per S3MM). Only databases of type `PackedFile` that are opened read-only are offered. Saves, `CASPartCache.package`, the compositor caches and anything the game writes stay entirely in-process.
- The helper checks file size and modification time when it opens a package and refuses entries whose file changed afterwards.

### 6.3 Hook points

Each detour is a plain 5-byte JMP to our code with a trampoline to the original (the S3SS style). Each one must return exactly what the original returns.

| # | Function | Detour does | Must return | Phase |
|---|---|---|---|---|
| H0 | `Manager::RegisterDatabase` `0x4b2d00` (slot `0x34`) | Report the database to the helper, then call the original | the original's result, unchanged | 1 |
| H1 | `DatabasePackedFile::ReadDecompress` `0x4a8380` (`__thiscall`: compression, offset, fileSize, memSize, out buffer) | If the package is offloaded: ask the helper for `(pkg, offset)`, `memcpy` the decompressed bytes from the data window into the engine's **already allocated** `memSize` buffer, skip the engine's raw buffer, read and RefPack | `1` on success; on any helper error, call the original (it returns `0` on its own failures) | 1 |
| H2 | `DatabasePackedFile::OpenRecord` `0x4a94c0`, the uncompressed > 512-byte branch (streaming record, slot `0x74`) | Optional: serve uncompressed resources from the helper too, by returning a memory record (slot `0x70`) built on a buffer we fill | a record object the engine can release normally | 2 |
| H3 | `Manager::GetPackageSource` `0x4affc0` | Optional: answer "which database has this key" from the helper's merged index instead of probing ~440 packages; fall back to the original walk when the answer is a database the helper does not know | the database pointer and its priority (out parameter), or 0 | 2 |
| H4 | ResourceCache budget at cache `+0x8c` (`0x732910`), `[Resources] CacheBudget` | Not a detour: lower the in-process budget (ini value, or a write after `0x738670` returns) once H1 makes misses cheap | n/a | 2 |
| H5 | `CASPartCacheService::Init` `0x5aedf0`, `CASCache::GetCachedPart` `0x5a44a0` | Research only: see Phase 3 | n/a | 3 |

The H1 argument order comes from its call sites in `0x4a94c0` (`compression, offset, fileSize, memSize, buffer`); Ghidra's own signature for `0x4a8380` is garbled (register arguments), so confirm the calling convention in the disassembly before writing the detour [verified call sites, convention to check].

H1 is deliberately low in the stack: the engine still finds the key in its own index, allocates its own buffer, builds its own record and object, and caches it its own way. Only the bytes come from elsewhere, which keeps the change small and easy to verify.

### 6.4 IPC

- **Transport.** One pagefile-backed section for control (1 MB) and one 8 MB data ring, both created by the helper and mapped once by the ASI. That is 9 MB of the game's address space, permanently. Payloads larger than the free ring space are streamed in chunks into the same destination buffer (the destination is already allocated by the engine).
- **Request.** `{seq, db id, offset, fileSize, memSize, compression}`; the helper looks it up by `(db, offset)`, so the engine's own index stays the authority on which resource it is.
- **Response.** `{seq, status, ring offset, length}`. Status codes: hit, miss-then-filled, not-offloaded, file-changed, error.
- **Synchronization.** A slot state word written with interlocked operations, a short spin (about 20 µs), then a named auto-reset event per direction. `WaitOnAddress` cannot be used: it only works within one process. Many game threads may call H1 at once (the AsyncLoader and the main thread), so the ring has 64 slots and each caller owns a slot for the duration of its request.
- **Latency budget.** A request that hits the helper's cache costs a ring round trip (tens of microseconds) plus a `memcpy` (about 0.1 ms per MB on this CPU). The original path costs a `ReadFile` (often from the Windows file cache, but still a kernel call and a copy) plus a RefPack decompression (several ms per MB without S3SS's SIMD version). A cold miss in the helper costs the same as the original plus the round trip. At 60 FPS the frame is 16.7 ms; the per-request target is under 0.5 ms for hits and never more than the original time plus 0.2 ms for misses [inferred targets, to measure].
- **Prefetch hint (later).** A one-way "hint" message type lets the ASI ask the helper to warm keys without waiting (section 9).

### 6.5 What stays in TS3W.exe

The manager, every package object and its index, all engine resource objects and their factories, the ResourceCache (smaller), the AnimationCache, CAS and compositor caches, all writes (saves, cache packages, `.sims3pack` installs into DCCache), the Preloader, and the AsyncLoader. The helper never interprets resource contents.

### 6.6 Failure modes

| Failure | Detection | Behaviour |
|---|---|---|
| Helper not running at start | no heartbeat within ~3 s | install no detours; the game runs vanilla |
| Helper crash mid-session | process handle signalled, or heartbeat older than 250 ms | flip the global "offload off" flag; every detour calls the original from then on; log once |
| One request times out | no response within 50 ms | call the original for that request; three timeouts in a row turn offload off |
| Payload mismatch (validation mode) | hash differs from the original path | turn offload off, log the key and package |
| Package changed on disk | size or mtime differs | helper answers "not offloaded" for that package |
| Ring full | no free slot after a spin | call the original (never block a game thread on the helper) |

Because H1 writes only into the buffer the engine allocated for that exact `memSize`, a helper bug can at worst produce wrong bytes in one resource, never a heap overflow, provided the ASI checks `length == memSize` before copying.

### 6.7 Verifying correctness

- **Validation mode** (Phase 1 default for the first runs): for every H1 call, run the original path into a scratch buffer as well, compare an xxHash64 of both, and count matches and mismatches per package. Turn it off once a full test run (load, CAS, buy mode, travel, save) shows zero mismatches.
- **Offline check**: a node script reads N random keys from Rodney's packages with our own DBPF reader and RefPack decoder and compares them with the helper's output, outside the game.
- **In-game check with `tools/probe`** (later, when the game is free): log `(key, length, hash)` of every resource read with the helper off, then with it on, and diff the two logs for the same scenario.

---

## 7. Phased plan

### Phase 0: measure (no helper, game needed)

Run with `tools/probe` when the game is free, on the vanilla user folder and then with Rodney's CC restored (same save, same camera script):

1. **Address space by category**: committed, reserved and free, and the largest free block, sampled every 10 s from launch to 5 minutes in live mode (what `05` already records), plus a VirtualQuery breakdown by allocation type (heap, mapped, image, D3D/DXVK).
2. **Bytes by EA allocation tag**: hook the engine's tagged allocator (`FUN_004e3f90` and the allocator vtables that take a tag string) and sum live bytes per tag prefix: `ResourceLoad/*`, `ResourceSystem/Cache/*`, `ResourceSystem/ResourceMan/PackedFile/*`, `CAS/*`, `UI/*`, others. This answers directly where CC memory goes.
3. **The ResourceCache itself**: read the manager's cache array (manager `+0xa0`/`+0xa4`) and each cache's budget (`+0x8c`), overflow budget (`+0x90`) and current bytes (`+0x94`), every 10 s.
4. **Resource traffic**: count calls, bytes and time in `0x4b0480` (GetResource), `0x4a8380` (read + decompress) and the time the lock `0xfb2cd0` is held, per second, and correlate with frame times over 50 ms.
5. **No-code experiment**: lower `CacheBudget` (Default.ini `[Resources]`, or the `"Cache size"` variable if S3SS's variable editor shows it) from 200 MB to 50 MB, then to 0, and repeat 1 and 4. Expected: address space drops by up to ~150 MB, re-reads and hitches rise [inferred]. This single experiment tells us whether a fast L2 is worth building.
6. **Free engine logs to try**: `[ResourceSystem] LogRWFSCalls = 1` (file-system call log, if it still works in release builds).

Effort: 1 to 2 days once the game is free. Exit criterion: we know how many MB of address space the ResourceCache, transient buffers, CAS data, string tables and catalog take with and without CC.

#### Phase 0 tooling (ready 2026-10-11, not run yet)

Run it after the baselines, with nothing else driving the game (it closes and relaunches the game per variant):

```
node tools/harness/offload-phase0.ts --dry-run                      # print the steps
node tools/harness/offload-phase0.ts --save Baseline-SV.sims3 --minutes 15
node tools/harness/offload-phase0.ts --only nocc-200,cc-200         # a subset
```

Variants: `nocc-200`, `nocc-50`, `nocc-0`, `cc-200`, `cc-50`, `cc-0` (CC off/on × `CacheBudget` in MB). Per variant: `cycle.ts --restart`, installer switches (`user-cc`, `cache-budget --cache-budget-mb N`, `cache-clear`), probe launch, `cycle.ts --save`, a crash-ledger snapshot noted `offload phase0 <variant>`, then N minutes at speed 3 with the in-game autopilot (holds speed, clears popups), keep_alive and declines. At the end the CC and the budget change are uninstalled (EA's `Default.ini` restored) and the game is closed. Output: `probe-out/phase0/<stamp>/<variant>.csv` (every 10 s) plus `<variant>-probe.log`.

Pieces, usable on their own:

- `node tools/setup/install-fixes.ts install --only user-cc` copies Rodney's CC from `D:\Sims3-backup\2026-10-11` (143 packages, ~27 MB): `Packages/ModTheSims`, `Packages/Performance Fixes`, `Overrides/Body Shop`, `Overrides/Game Mods`, relative paths kept. Excluded: the three "Store Updates" repacks and "Store Fixes" (`Packages/Sims 3 Store`), old script and core mods (root `Packages/`, `Overrides/` root, the Pure Scripting Mods folder, `ld_SmoothPatch*`), the old `Resource.cfg`. `uninstall --only user-cc` removes exactly those files. `check-conflicts.ts` on fixes layer + CC (isolated copy): 167 packages, **0 CLASH, 0 WARN**, 309 INFO overrides. Note: without the Store repacks the CC is small, so `cc-*` measures the ~27 MB content set, not the 5 GB one [measured sizes].
- `node tools/setup/install-fixes.ts install --only cache-budget --cache-budget-mb 50` sets `Default.ini [Resources] CacheBudget` (bytes = MB × 1,048,576; 0..476). Switching values keeps the first backup; `uninstall --only cache-budget` restores EA's 209,715,200.
- `python tools/probe/ts3probe.py get rescache` (read-only) lists the manager's caches.

What the CSV columns mean:

| Column | Source | Meaning | Confidence |
|---|---|---|---|
| `commitMB`, `largestFreeMB`, `privateMB` | `/memory` (VirtualQuery walk) | committed address space, largest free block (the Error 12 margin), private (heap) bytes | [verified, as in docs/05] |
| `cacheBudgetMB` | ResourceCache `+0x8c` | budget in effect (should equal the variant's value) | [verified, ctor `0x732910` reads `CacheBudget` into `[0x23]`] |
| `cacheBytesMB` | `+0x94` | bytes the cache counts as held | [verified field; `0x732a00`/`0x732b60` add and subtract resource sizes here] |
| `cacheFlushMB` | `+0x98` | bytes on the flush list (unreferenced, evictable); moved back to `+0x94` on a hit | [inferred from `0x732b60`] |
| `cacheEntries` | `+0x28` | entries in the key map (EASTL `hash_map` element count after buckets `+0x20`, bucket count `+0x24`) | [inferred from the ctor's EASTL-shaped init] |
| `animBytesMB` | AnimationCache (priority 1000) `+0x94` | the fixed 15 MB animation cache's fill | [verified field, same class] |

How `/rescache` finds them: manager singleton `[0x11c78d8]` (getter `0x4afd20`), checked against vtable `0xfb2da0`; cache array `+0xa0`/`+0xa4` of 8-byte `{cache*, priority}` entries (`FindCache` `0x4b0070`) [verified]; only objects with the ResourceCache vtable `0xffdd94` are read. That the array holds the ResourceCache and AnimationCache with priorities 0 and 1000 follows from `0x738670` registering them through the manager's vtable slot `+0x54` [inferred: the registering object is assumed to be the manager]. **To verify with the game:** `get rescache` returns two known caches, the generic one's budget is 209,715,200, bytes grow after loading a lot. If the manager check fails, decompile `0x738670`'s caller and the slot-`+0x54` target to confirm which object owns the array.

What to read from it: `cacheBytesMB` at 200 MB vs at 50/0 is the address space the helper could move out (compare `commitMB` between variants too); more `cacheFlushMB` churn and lower sim pace at small budgets mean re-reads cost time. What budget 0 means (cache nothing unreferenced, or unbounded) is [to measure]: `cacheBytesMB` answers it.

### Phase 1: helper with byte serving (H0, H1)

Helper process, IPC, package registration, merged index (parallel parse of all read-only packages), overlapped reads, RefPack pool, decompressed LRU cache, validation mode, fallback. No change to the engine's cache budget yet.

Gain on its own: shorter lock holds and less decompression on game threads (stutter), no raw buffer allocation in-process (about `fileSize` per in-flight read). Address space gain: small (transient only) [inferred].

Effort: about 60 to 100 hours (C++ ASI plus a C++ or Rust 64-bit helper, IPC, tests).

### Phase 2: shrink the in-process cache (H4, optionally H2 and H3)

Lower `CacheBudget` to the value Phase 0 finds best, with the helper's L2 absorbing the extra misses. Optionally serve uncompressed large resources (H2) and answer package lookups from the merged index (H3, CPU win with many packages).

Gain: up to ~150 to 200 MB of address space, depending on how full the cache really is with CC [to measure]. Effort: 20 to 40 hours, mostly measurement.

### Phase 3: CAS caches (research)

`CASPartCacheService::Init` builds data for every CASP at startup and keeps it. If Phase 0 shows this is large with CC, two options exist, both much harder than Phases 1 and 2: keep a compact proxy per part in-process and fetch the full part data from the helper on demand (needs the `CASPartDataResource` layout and every consumer of it), or filter CAS parts that the current save never uses (a gameplay change). Effort: unknown, likely 100+ hours; decide after Phase 0.

---

## 8. Risks and honest feasibility

- **The main premise was weaker than expected.** Raw package data and indexes are not where the address space goes. The helper's address-space value rests on the ResourceCache budget, which Phase 0 must confirm is actually full with CC. If the cache is mostly idle, the helper is a stutter and load-time tool, not an Error 12 fix.
- **Bigger, cheaper CC wins exist without a helper:** DXVK (done), the Performance Patch's string-table tuning (~290 MB community-measured, scales with CC string tables), thread stack trimming (done via S3SS), and simply lowering `CacheBudget` (zero code; costs some re-reads).
- **Detour safety.** H1 runs on several threads under the global lock. The ASI must never block on the helper for long while holding that lock (strict timeouts, fallback).
- **Version coupling.** Addresses are Steam 1.67 only. Use S3SS-style byte patterns for each hook so a mismatch disables the feature instead of crashing.
- **Conflicts.** S3SS's RefPack patch sits below H1 and stays useful for the fallback. Sims3MonoModder and S3SS both detour other manager parts; check for overlapping bytes before install.
- **Memory on the 64-bit side** is plentiful (32 GB), but the helper's cache should be capped (for example 4 to 8 GB) so it does not push the game's own pages out.

**Feasibility:** Phases 0 to 2 are technically straightforward: the hook points are clear, the data path is simple, and correctness is easy to check byte for byte. **Effort:** Phase 0 1 to 2 days, Phase 1 about 60 to 100 hours, Phase 2 about 20 to 40 hours, Phase 3 open-ended. **Expected address-space gain:** roughly 150 to 250 MB in the good case [inferred], which is real but smaller than DXVK (1.75 GB measured) and comparable to the string-table fix. Expected stutter gain: shorter global-lock holds during streaming [to measure].

---

## 9. Later, if the offload works

These are ideas for after Phases 0 to 2 succeed. None of them is part of the core plan.

- **Use all cores in the helper.** Parse package indexes in parallel at startup, run RefPack on a thread pool, and **prefetch** into the helper's cache so the engine's serial requests become cache hits. Prediction points: lot loads (S3SS shows lot streaming is driven by `WorldManager::Update` `0xC6D570`, whose `+0x258` flag skips streaming in map view, and `Lot::AddLotObjectsToScene` `0xAC1130`); CAS opening and outfit changes (`CASPartCacheService` and `CASCache::GetCachedPart` `0x5a44a0`); the catalog. A hint costs the game one ring write and never waits.
- **Whole-world prefetch into the helper.** The world file's lot information resource (`0x03D86EA4`) and the lot objects' instance lists (`0x4D1A5589`) name what each lot needs [community resource types, `03` §5]; the helper could warm those keys for all lots in the background. In-process this would be harmful (it pins memory); in the helper it only costs RAM.
- **`[Preloader]`.** `KeyFilter…` lines can pin chosen resources in-process from startup (section 2.6). With the helper, a better use is to feed the same key lists to the helper's prefetcher instead. Decode `0x72acc0` first to learn whether a filter can name a whole type.
- **`MemoryUsageLimit`.** It caps the AsyncLoader, not the cache. Raising it might let more asynchronous loads run at once (faster streaming, more transient memory); lowering it might reduce peaks. Try 10, 20 (default) and 40 MB with Phase 0's measurements 1 and 4, one at a time.
- **GPU options, honestly.** DXVK already keeps textures in VRAM and out of the 32-bit process, which is the only GPU-side memory win available. DirectStorage and RTX IO need Direct3D 12 and GDeflate-compressed assets; this game is Direct3D 9 with RefPack and DDS, so at most the helper could use a fast NVMe reader for its own reads. RefPack is LZ77-style and byte-serial, a poor fit for CUDA decompression. VRAM is not useful as storage for meshes, rigs and animations, which the CPU needs. GPUDirect Storage is not available on GeForce cards under Windows. [inferred from the formats and APIs; vendor claims community]
- **DXVK shader-compile stutter** (separate small item). Keep DXVK's defaults, which on current NVIDIA drivers use the graphics pipeline library to compile most pipelines ahead of use; check that `dxvk.enableGraphicsPipelineLibrary` is not set to `False` in `dxvk.conf`, and leave `dxvk.numCompilerThreads` at its default (all cores). The `gplasync` fork (`07` §1.2, not used) trades remaining hitches for brief pop-in. [community, to verify against the DXVK version in use]

---

## 10. Open questions

1. How full is the ResourceCache with CC after 30 minutes, and how much of its content is re-requested after eviction? (Phase 0, items 3 to 5.)
2. Who reads AsyncLoader `+0x38` (`MemoryUsageLimit`), and is it an in-flight cap? Decompile the AsyncLoader vtable methods `0x729cd0`, `0x729e10`, `0x728c30`.
3. What does the `KeyFilter` parser `0x72acc0` accept (ranges, wildcards)?
4. How much memory does `CASPartCacheService` keep per CAS part, and is the in-memory part data freed after the cache package is built?
5. Does the string-table layout fix in the Performance Patch touch the STBL factory we would see in the allocation-tag breakdown? Read its included source once downloaded.
6. Does `DumpResourceCacheSummary` (`IC` at `0xec14e0`) do anything in the release build, or is it a stub like the `"Dump Resources"` debug command (which points to a shared empty function)?

## Sources

- TS3W.exe 1.67 (Steam), Ghidra project `game-copy/ghidra` (read-only), `game-copy/bin/Default.ini`, `game-copy/bin/Sims3.ini`, `game-copy/analysis/functions.csv`.
- Read-only scans of `D:\Sims3-backup\2026-10-11\Documents-The Sims 3\Mods` and of the Steam install's packages (headers and indexes only).
- S3SS source: `patches/createfileW_patch.cpp`, `refpack_decompressor_patch.cpp`, `worldcache_uncap_patch.cpp` (https://github.com/sims3fiend/Sims3SettingsSetter).
- Sims3MonoModder source: `src/core/resource_system.h/.cpp`, `src/core/addresses.cpp` (sims3fiend).
- Sims 3 Performance Patch features as quoted in https://simscommunity.info/2026/07/16/sims-3-performance-patch-mod-review/ ; mod page https://www.nexusmods.com/thesims3/mods/235 (HTTP 403 to our fetch).
- Earlier project docs: `03-prior-art-re.md` (S3SS anchors, lot streaming), `05-baseline-results.md` (DXVK measurement), `07-known-fixes-catalog.md`.

### Phase 0 results, first run (2026-10-11, Bridgeport, all fixes, no CC, 10 min each at x3)

| Variant | Cache start / avg / peak / end | Entries (end) | Commit avg / peak | Min largest free | Sim days |
|---|---|---|---|---|---|
| nocc-200 | 341 / 415 / 460 / 405 MB | 117,712 | 1.99 / 2.11 GB | 1.58 GB | 3.28 |
| nocc-50 (see below) | 341 / 408 / 450 / 428 MB | 121,370 | 2.00 / 2.08 GB | 1.56 GB | 3.74 |

- **The budget switch did not take effect:** with `Default.ini` `CacheBudget` set to 50 MB, `/rescache` still read a budget of 200 MB (209,715,200) in the game. So nocc-50 is a repeat of nocc-200 and shows the measurement repeats within ~2% (avg 415 vs 408 MB). Next: find where the game really reads the budget (another ini or section, the user folder, or a hard-coded default; xrefs of the "CacheBudget" string and `ResourceCache_ctor 0x732910` callers), fix `cache-budget`, check the budget column before trusting a variant (offload-phase0 should abort when it does not match).
- The main cache holds ~400–460 MB, twice its budget, and grows with play (341 MB right after load). Whether that is in-use data (cannot move) or evictable data is still open: it needs a working budget 0 run.
- Stopped after two variants (end of session); nocc-0 and the CC variants were not run. Restored afterwards: `Default.ini` back to 200 MB, CC never installed.

**Correction (2026-10-12) [verified]:** the game takes the ResourceCache budget from `GraphicsRules.sgr` (`seti MemCacheBudgetValue 209715200`, 160 MB when RAM level is low; `setProp $ResourceGroup CacheBudget $MemCacheBudgetValue`, line 656), read by the ctor `0x732910` through the wide property name "CacheBudget" (pushed at `0x7329a0`). `Default.ini [Resources] CacheBudget` is ignored: with all 20 pack copies set to 50 MB, `/rescache` still showed 200 MB; with the `GraphicsRules.sgr` line set to 50 MB it showed 50 MB. The `cache-budget` installer entry now patches that line, and offload-phase0 skips a variant whose budget the game did not pick up. Earlier mentions of `Default.ini` as the source in this document are wrong.
