# 17. Heavy store content and big CC folders: causes, community fixes, candidates

Written 2026-10-13. Research only: the game was not launched or attached. Inputs: docs 01, 07, 08 (sections 0, 2, 5, 7 and the Phase 0 results), `tools/setup/fixes.json`, read-only string and ini checks on `game-copy/bin`, a read-only index scan of the Store repacks in `D:\Sims3-backup\2026-10-11`, and community pages (sources at the end).

Tags: **[verified]** we saw it in our docs, binaries, files or code. **[community]** a mod page, guide or forum says so (linked). **[inferred]** our reasoning, still to be measured.

---

## 0. Short answer

- **What really grows with lots of store content:** the CAS part list and catalog entries built at startup, string tables, the content-tool caches (DCCache, thumbnails, compositor caches), and per-package lookup work. **What does not grow much:** raw package bytes and package indexes. Packages are read on demand, not memory-mapped, and an index costs about 17.6 bytes per resource (docs/08 §0) [verified].
- **What we already have:** DXVK, S3SS (stacks, mimalloc, RefPack SIMD, featured-items patch), Catalogue Lag Fix, `cache-clear`, the in-game store and telemetry off in `Options.ini`, a standard `Resource.cfg`, a conflict checker, and the harness `warm-cache.ts` [verified, `fixes.json` and `tools/`].
- **Best new candidates, ranked by gain per effort and risk (detail in section 3):**
  1. Performance Patch `StblBucketLayoutTuning` (+ `PackageIndexWarmup`) as the default for heavy-content setups.
  2. A content health check in the installer: DCCache size and corruption, `.sims3pack` files left in Downloads, stray OneDrive conflict copies of the cache files, and a `DCCache/*.dbc` line in `Resource.cfg`.
  3. Convert non-store `.sims3pack` files to `.package` (Delphy's Multi Sims3Pack Extractor), as advice plus detection.
  4. Merge many small packages (only if Phase 0 shows that the package count costs time).
  5. Our own native fix for the linear package lookup (docs/08 H3), if per-package cost turns out to matter.

---

## 1. Symptoms, causes and known fixes

| Symptom | Cause (engine, launcher, script) | Known community fixes | What the fix actually changes | Reported or measured gain | Ours? |
|---|---|---|---|---|---|
| **Long load times** (startup and world load) | (a) Every `PackedFile` match becomes one database; on every cache miss, `GetPackageSource` **probes every registered package in priority order** [verified, docs/08 §2.2]. (b) `CASPartCacheService::Init` enumerates **every CASP in every package** at startup and builds `CASPartCache.package` [verified, docs/08 §2.5]. (c) Script-type lookups during save load (Mono) [community, S3PP]. (d) Disk reads of package indexes [verified mechanism]. | Merge packages; Performance Patch `MonoTypeEHashProbe` and `PackageIndexWarmup`; do not clear caches on every launch | Merging cuts probes per miss and file handles, but not memory. `PackageIndexWarmup` reads the Mods packages' tables of contents into the Windows file cache while the main menu is up. `MonoTypeEHashProbe` adds an index for script type lookups. | S3PP author: "18 min to 4 min", mostly from the type index, not from warm-up; warm-up "helps most on hard drives" [community, S3PP page as quoted in the review]. Merging: forum reports range from "big cut" to "zero benefit" [community, r/thesimscc]. **Ours:** 143 small CC packages added nothing measurable at load (docs/08 Phase 0) [verified]. | Cache rule yes; `s3pp` is optional, not default; `warm-cache.ts` is harness-only |
| **Error 12 / crash on save** (address space) | Live decoded resources (meshes, CAS parts, string tables, catalog data), not raw package bytes [verified, docs/08 §3]. The ResourceCache is ~400 MB of in-use data, and its budget barely matters (Phase 0) [verified]. String tables: ~29 KB of layout per table [community figure] × 3,187 English tables in the Store repacks gives **~90 MB** [inferred]. | DXVK; thread stack fixes; S3PP `StblBucketLayoutTuning`; fewer CC items | DXVK moves textures out of the process. The STBL fix right-sizes the per-table hash layout, and the text stays byte-identical [community]. | DXVK 1.75 GB [verified, docs/05]. STBL: ~290 MB on a 10,000+ table save [community]; for our store set ~90 MB [inferred]. | DXVK and stacks yes; STBL no (optional `s3pp`) |
| **Catalog / buy-mode lag** | Catalog UI creates too many invisible windows (LAYO) [community]; catalog entries grow with OBJD count (store set: 3,773 OBJD) [verified count]; thumbnails missing from CC are generated and cached in `Thumbnails\ObjectThumbnails.package` [verified file names in exe and user folder; generation inferred] | Catalogue Lag and Memory Leak Fix (Nexus 242); clear `Thumbnails\*` after content changes; in-game store off (hides unowned store previews) | LAYO-only layout edit; thumbnail rebuild | "Too many UI windows", leak fixed [community, mod page as quoted in docs/07]; no numbers published | Yes (`catalogue-lag-fix`, `cache-clear`, `options-ini`) |
| **CAS lag / slow CAS loading** | Startup CASP enumeration (store set: 1,969 CASP, +35% on EA's 5,662) [verified counts]; `CASThumbnails.package` built while CAS is open; compositor caches (`simCompositorCache.package` grows to its 500 MB file cap: **524 MB today** in the live user folder) [verified]; no tick cap causes CAS spin [community] | Smooth Patch package (faster CAS clothing loads); S3SS tick limiter; wait for thumbnails to finish before using CAS [community, EA forum]; clear caches after content changes | Script-side CAS loading tweaks; tick cap; cache rebuild | No numbers published | Yes (`smoothpatch-package`, S3SS Precise, `cache-clear`) |
| **Stutter in live mode** | Global resource lock held during the read, decompression and factory on every synchronous `GetResource` [verified, docs/08 §2.2]; each miss probes all packages [verified]; lot streaming | S3SS RefPack SIMD and lot streaming; merging (fewer probes) | Faster decompression inside the lock; fewer probes | RefPack is "most impactful" [community, S3SS author] | Yes (S3SS) |
| **"Mod Scripts Found" / load hangs** | The native dialog fires when script assemblies (S3SA) are present in Mods [verified behaviour in our harness, `cycle.ts`]. Load hangs are usually a broken or outdated script mod, or a stale `scriptCache.package` [community, EA forums]. The Store repacks carry only 66 S3SA (1.7 MB) [verified scan], so heavy **store** content is not a big script-load cost. | Delete `scriptCache.package`; 50/50 mod bisect; S3SS `StartupWarningDialogFix` (the Win11 25H2 black-screen hang) | Cache rebuild; isolates the bad mod; skips the hanging dialog path | n/a | Yes (`cache-clear`, S3SS) |
| **DCCache and `.sims3pack` bloat (`.dbc` files)** | The launcher installs `.sims3pack` content into `DCCache\dcdbN.dbc` (CC) and `.ebc` (encrypted EA store content); the exe globs `dcdb*.*bc` and knows `dcdbt.ebc`, `dcc.ent`, `missingdeps.idx` and `DCBackup\ccmerged.package` [verified exe strings]. `Default.ini` sets `DBCacheMaxSizeMB = 200` [verified] (the source of the forum "keep `.dbc` under 200 MB" rule; whether the engine honours it is [inferred], since `CacheBudget` in the same file is ignored, docs/08). A `.dbc` goes through the same database path as a `.package` (docs/08 §5) [verified]. Uninstalling does not shrink a `.dbc`; one corrupt bundle stops the launcher or the content after it [community]. | Convert `.sims3pack` to `.package` (Delphy's Multi Sims3Pack Extractor, CC Magic for unencrypted ones); move DCCache aside and reinstall in small batches; empty `DCBackup` except `ccmerged.package`; never touch `.ebc` (store) files | Takes CC out of the launcher's bundles; same engine cost per resource after conversion [inferred] | "Sims3packs cause horrendous lag" [community, r/Sims3, anecdotal]; mechanism says the main gain is launcher reliability and no `missingdeps` check, not in-game speed [inferred] | Only `dcc.ent` and `missingdeps.idx` are cleared; no detection |
| **Launcher slow with many installed items** | `Sims3LauncherW.exe` lists every installed item from DCCache/Downloads; more items mean more corrupt-bundle risk [community, EA forums] | Bypass the launcher (start `TS3W.exe` directly, Steam launch option); keep Downloads empty after installing; reinstall store content in stages (worlds, lots, items) | Skips the launcher's content scan | "Launcher unreliable with lots of content" [community] | Reminder only (docs/07 §4.5) |

### Store content specifics

- **Bought store content on Steam** installs through the launcher as `.sims3pack` into `.ebc` bundles (encrypted) and worlds into `InstalledWorlds` [community; `.ebc` handling verified in exe strings]. CC Magic and the extractors **cannot convert encrypted store files** [community, EA forum]. Users also report broken interactions after converting store items that carry scripts or animations [community, r/Sims3]. Our advice for store content: keep it in DCCache, keep bundles healthy, don't convert.
- **Rodney's test set** (the Store repacks, test-only, `store-repacks-test`): 3 packages plus Store Fixes, 205,676 resources: 3,773 OBJD, 1,969 CASP, 64,622 STBL (all languages; ~3,187 English, docs/08), 66 S3SA, 3,353 thumbnails, 20,477 textures [verified scan]. It is **already merged** (about 2,000 store items in 3 files), so it measures per-resource cost, not per-package cost.
- **Featured store items:** S3SS `StoreFeaturedItemsDisablePatch` and `enableingamestore = 0` stop the `FeaturedItems` download and the store previews in the catalog [verified in our config].

### Merging and the size limit

- **Engine:** an index offset is a u32, so a package must stay under **4 GB** [verified layout]; the header check rejects index counts ≥ 0x7ffffff [verified, docs/08 §2.3]. Packages are not mapped, so a big merged file costs no address space [verified]. Rodney's 1.98 GB repacks were in his Mods folder [verified files].
- **Tools:** there is no published s3pe limit; s3pe runs out of memory when merging too much at once, so the advice is to merge in batches [community, MTS 430124]. A 2 GB practical ceiling for .NET tools is [inferred] (signed 32-bit sizes).
- **Risks:** merging hides which file is broken [community]; it can change override order when files with different priorities (Overrides vs Packages) are merged [inferred]; `CCMerger` (LazyDuchess) is a **Sims 2** tool and does not apply [verified page].
- **Gain:** fewer databases means fewer probes per miss and fewer handles [verified mechanism]. Our Phase 0 found no measurable load effect from 143 packages [verified]. The gain for players with thousands of loose packages is unmeasured [inferred].

### Cache files to delete (after content changes only)

`cache-clear` already deletes `CASPartCache`, `compositorCache`, `scriptCache`, `simCompositorCache`, `socialCache`, `WorldCaches\*`, `Thumbnails\*` (`CASThumbnails`, `ObjectThumbnails`, `DownloadsThumbnails`), `DCCache\dcc.ent`, `missingdeps.idx` and `FeaturedItems\*` [verified, `fixes.json`]. The community also lists `SigsCache\` (launcher issues) and the `DCBackup\` contents except `ccmerged.package` [community, EA forums]. Not in our list yet; low value, low risk.

**Side finding:** the 2026-10-11 backup holds `compositorCache-<pc>.package` (99 MB), `simCompositorCache-<pc>.package` (251 MB), and `CASThumbnails-` / `ObjectThumbnails-<pc>.package`. The exe only names the unsuffixed files [verified strings]. The `-<PC name>` suffix is OneDrive's conflict-copy pattern [inferred], so these files are dead weight on disk and **not** the game's live caches. docs/08 §1 treats them as live caches and should be read with that in mind.

---

## 2. What we already install vs. what is new

| Item | Status |
|---|---|
| DXVK, S3SS (stacks, mimalloc, RefPack, featured items, startup dialog), Catalogue Lag Fix, Smooth Patch package, `cache-clear`, `options-ini` (store off), `resource-cfg` (no DCCache line), `check-conflicts.ts` | **Installed** (default set) [verified] |
| S3PP (`StblBucketLayoutTuning`, `PackageIndexWarmup`, `MonoTypeEHashProbe`) | In the manifest as optional `s3pp`, never measured: **candidate** |
| `warm-cache.ts` (Windows file cache warm-up) | Harness only, not a player fix [verified] |
| Content health check (DCCache size and corruption, Downloads leftovers, OneDrive copies, `DCCache` line in `Resource.cfg`, `.sims3pack` files) | **New** |
| `.sims3pack` → `.package` conversion advice | **New** (advice, not automated) |
| Package merger (our own, priority-aware) | **New**, only if measurement justifies it |
| Native lookup index (docs/08 H3) | **New**, research |
| `SigsCache`, `DCBackup` clean-up | **New**, small |

---

## 3. New candidates ranked (gain per effort, then risk)

| Rank | Candidate | Expected gain | Effort | Risk | Decided by (section 4) |
|---|---|---|---|---|---|
| 1 | **S3PP default for heavy content** (`StblBucketLayoutTuning` + `MonoTypeEHashProbe` + `PackageIndexWarmup`; S3SS stack fix off on the first run) | Address space: ~90 MB for the store set [inferred], up to ~290 MB on big saves [community]. Load time: possibly large (type index) [community]. | Low (manifest entry exists, manual Nexus download) | Medium (beta; blank or wrong labels from `LocalizedStringCache`, which can be switched off on its own) | M3, M5, M8 |
| 2 | **Content health check** in `install-fixes.ts check` | Prevents launcher hangs, missing content and corrupt-bundle failures; frees disk | Low (file stats, DBPF header check with our reader) | Very low (read-only, warns only) | none needed |
| 3 | **`.sims3pack` → `.package` advice** (non-store CC only, Delphy extractor) | Launcher reliability; in-game gain unknown | Low (doc) | Low for CC; **do not** apply to store `.ebc` | M7 (optional) |
| 4 | **Priority-aware package merger** (our tool, batches ≤ ~1 GB, never across Overrides/Packages) | Fewer probes per miss; only matters with thousands of packages | Medium (we have a DBPF reader and conflict checker) | Medium (override order, harder debugging) | M4 |
| 5 | **Native package lookup index** (one hash from key to package instead of probing every package; docs/08 H3) | CPU per miss, shorter lock holds | High (hook in `GetPackageSource` `0x4affc0`, invalidation when packages register) | Medium | M4, M6 |
| 6 | `SigsCache` / `DCBackup` added to `cache-clear` | Small (launcher issues) | Very low | Low (keep `ccmerged.package`) | none |

Not candidates: lowering `CacheBudget` (Phase 0: no gain), a 64-bit cache helper (dropped, docs/08), "RAM booster" tools and HeapSize ini edits (myths, docs/01), raising `DBCacheMaxSizeMB` (no evidence it helps).

---

## 4. What Phase 0 store runs should measure

Planned: variants `nocc` / `cc` / `store` × `CacheBudget` 200/50/0, recording launch-to-live time, address space and ResourceCache fill. Phase 0 already showed the budget barely matters, so **one budget (200) is enough for `store`**. The time is better spent on the variants below.

| # | Measure | Why it decides | How |
|---|---|---|---|
| M1 | **Split load time**: launch → main menu, menu → live | Startup enumeration (CASP, catalog, STBL) vs world load; tells S3PP's type index apart from CC cost | Timestamps in `cycle.ts` (dialog seen, menu ready, live) |
| M2 | **Cold vs warm caches**: first launch after `cache-clear` vs second launch | Cost of rebuilding `CASPartCache`, thumbnails and compositor caches; confirms "don't clear every launch" | Run `store` twice, the first after `cache-clear` |
| M3 | **Address space at menu and at live**, plus the largest free block **during a save** | Error 12 margin with store content; baseline for S3PP's STBL gain | `/memory` CSV; add a sample at the save |
| M4 | **Per-package vs per-resource cost**: a synthetic variant `pkgs-N` with N = 2,000 tiny dummy packages (one unused resource each, ~0 data) vs `store` (3 packages, 205k resources) | If `pkgs-N` costs load time or ms per `GetResource`, merging and the lookup index (candidates 4 and 5) are worth it; if not, drop them | Generate the dummy packages with our DBPF writer; count `GetResource` calls and ms, and `GetPackageSource` time per call |
| M5 | **Bytes by allocation tag**, especially STBL and `CAS/*` | Predicts S3PP's STBL gain before installing it; shows CASP data size | Allocation-tag hook (docs/08 §7 item 2) |
| M6 | **Lock hold time and hitches > 50 ms** while the buy catalog and CAS are browsed | Catalog and CAS lag with store content; stutter from probes | Agent actions: open buy catalog, page through, open CAS, switch categories (new repro actions per our repro rule) |
| M7 | Optional: the same CC as `.package` vs as a `.dbc` bundle | Confirms "sims3pack causes lag" or not | Only if we have non-store `.sims3pack` test files (none in the backup: `dcdb0.dbc` is 11 KB) |
| M8 | `store` + S3PP (one change) | Measured S3PP gain on load time and address space | After M1–M3 for `store` |
| M9 | Store items **in use**: buy and place 20 objects, wear 20 CAS parts | MB per used item (docs/08 open item) | Agent buys and places; diff `commitMB` |

Exit criterion: we know whether package count (M4) or resource count drives load time and lookup cost, how much STBL and CAS data cost in MB (M5), and S3PP's real gain (M8). Then candidates 4 and 5 are either justified or dropped.

---

## Sources

- Our repo: `docs/08-resource-offload-design.md` (§0, §2, §3, §5, §7, Phase 0 results), `docs/07-known-fixes-catalog.md`, `docs/01-existing-fixes-comparison.md`, `tools/setup/fixes.json`, `tools/harness/cycle.ts`, `tools/harness/warm-cache.ts`, `tools/extract-assemblies.ts` (`readIndex`); `game-copy/bin/TS3W.exe` UTF-16 strings (`dcdb*.*bc`, `dcdbt.ebc`, `ccmerged.package`, `DCBackup`, `missingdeps.idx`, `dcc.ent`, `CASThumbnails.package`, `ObjectThumbnails.package`, `DownloadsThumbnails.package`, `SigsCache`), `game-copy/bin/Default.ini` (`DBCacheMaxSizeMB = 200`, `PackageThumbnails`), user folder file sizes (live and 2026-10-11 backup), index scan of `Mods\Packages\Sims 3 Store\*.package` (scratch script, headers and indexes only).
- Performance Patch features, quoted from the Nexus page: https://simscommunity.info/2026/07/16/sims-3-performance-patch-mod-review/ ; mod page https://www.nexusmods.com/thesims3/mods/235 (HTTP 403 to our fetch).
- Merging: https://modthesims.info/t/430124 (merge in batches); https://teddit.bsalzberg.com/r/thesimscc/comments/1ilkh9b/merging_cc_definitely_does_help (mixed reports; page not readable to our fetch, summary from search); CCMerger (Sims 2 only): https://db.modthesims.info/d/648473/ccmerger-1-4-1-merge-your-packages.html
- Sims3pack conversion: https://steamcommunity.com/app/47890/discussions/0/4407417835482925886 (Delphy's Multi Sims3Pack Extractor); https://redlib.hbubli.cc/r/Sims3/comments/1j2nln1/i_want_to_scream ("horrendous lag", store caveat); https://forums.ea.com/discussions/the-sims-3-technical-issues-en/lots-missing-cc/8281430 (CC Magic cannot process encrypted store files; search summary).
- DCCache, launcher and `.dbc`/`.ebc`: https://answers.ea.com/t5/The-Sims-3/Sims-3-Launcher-not-responding-when-putting-custom-content-in/td-p/7351010 ; https://forums.ea.com/discussions/the-sims-3-en/launcher-crash-when-i-scroll-through-installed-content/542891/replies/542892 ; https://forums.ea.com/discussions/the-sims-3-technical-issues-en/re-too-much-cc/8179491 (all HTTP 403 to our fetch; content from search summaries, treat as [community]).
- Delphy's Dashboard (conflict detector): https://modthesims.info/t/353597
- CAS thumbnails wait and CC lag threads: https://forums.ea.com/discussions/the-sims-3-technical-issues-en/at-what-point-will-sims-3-start-lagging/8252060 ; https://steamcommunity.com/app/47890/discussions/0/3003304734674860838 (search summaries).
