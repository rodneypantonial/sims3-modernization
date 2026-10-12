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

---

## 5. Create-a-Sim and many installed items

Added 2026-10-13, research only (game not launched). Question: which community fixes, mods, settings and tools target (1) slow, laggy or crashing Create-a-Sim (opening, category switches, thumbnails, lots of CC and store content) and (2) the game struggling with many installed items (load time, catalog size, launcher/DCCache, unowned store items, engine limits)? Items already covered in sections 1 to 4, docs/07 and docs/01 are only referenced here.

### 5.1 What the game does (our own checks)

- **CAS store "featured items":** every CAS category refresh calls the native `UserToolUtils.GetFeaturedCASItemsListFiltered(...)` **before** it checks `InGameBrowser.Enable`, and drops the result when the store is off (`StoreUIModel.GetCASFeaturedStoreItems`, called from `CASClothingCategory`, `CASHair`, `CASMakeup`, `CASTattoo`) [verified, decompiled `Sims3GameplaySystems` and `UI`]. With `enableingamestore = 0` and the S3SS featured-items patch the list should be empty, so this costs one cheap native call per refresh [inferred]. Unowned store items are not shown in CAS once the store is off [verified code path], so no extra "hide store items" mod is needed.
- **Built-in filters:** the Build/Buy catalog and CAS both have a content-type filter (base game, pack, store, custom content: `CatalogProductFilter.Mode.Store` / `.CustomContent`, `mContentTypeFilter` in CAS) [verified, decompiled `UI`]. It narrows what is listed; whether it also skips thumbnail work is [inferred, unmeasured].
- **CAS config knobs:** `Default.ini [CAS]` sets `CompositorCacheSize = 100 MB` and `SimCompositorCacheSize` / `WorldCompositorCacheSize` / `SimWorldCompositorCacheSize = 500 MB`; this is the cap behind the 524 MB `simCompositorCache.package` in section 1 [verified file]. `GraphicsRules.sgr` sets `CASRenderTargetSize` 512 / 1024 / 2048, `CASLightMapFilteringEnabled`, `CASSinglePassShaderEnabled` and `CASSimpleHairShaderEnabled` per quality tier [verified file]. All these key names exist as UTF-16 strings in `TS3W.exe` [verified]. Whether the exe reads the `Default.ini [CAS]` keys is **not** verified: `CacheBudget` in the same file is ignored (docs/08), so the same may apply here.
- **Live user folder today:** `simCompositorCache.package` 12 MB, `compositorCache.package` 0.6 MB, `SavedOutfits` empty, `Collections` 90 KB, `FeaturedItems` and `IGACache` empty [verified, after the last `cache-clear`]. Saved styles and presets are not a factor on this install.
- **S3SS has no CAS-specific patch.** Its README lists none; `Uncompressed Sim Textures` makes live-mode Sim textures match CAS quality and costs memory (not in our `s3ss-config`, so off) [verified README and `fixes.json`]. S3PP has no CAS patch either; its `LocalizedStringCache` speeds up every UI label lookup, CAS tooltips included [community, S3PP review].
- **Engine limits:** no documented limit on the number of CAS parts or installed items was found [community; EA forum, Steam and MTS searches came back empty]. The known hard limits are per package (index count < 0x7ffffff, size < 4 GB, section 1) [verified]. In practice the limit is address space (Error 12): every CASP is enumerated into `CASPartCache` at startup (docs/08 §2.5) [verified], and CAS composites and textures add to that while CAS is open [inferred]. The launcher's Installed Content view is reported to load at most 500 items at a time; its tabs (Sims, Lots, Objects, Patterns) keep it responsive [community, MTS 525052].

### 5.2 Community fixes for Create-a-Sim

| Fix | What it changes | Symptom | Reported gain | Fit with our stack | Download | Ours? |
|---|---|---|---|---|---|---|
| **Smooth Patch package** (LazyDuchess) | Loads CAS clothes as you scroll instead of in a staggered batch; Create-a-Style no longer locks while patterns load; smoother drag and drop; CAS music and voice fixes (`kCASMusicFix` tuning) [verified, MTS page text] | Slow CAS clothing and hair lists, frozen Create-a-Style | "Greatly improving CAS performance", no numbers [community] | **Its faster clothing loading switches itself off inside MasterController CAS** [verified, MTS page text]. Fine with S3SS (package only, no ASI) | https://modthesims.info/d/658759 | **Yes** (`smoothpatch-package`) |
| **Catalogue Lag and Memory Leak Fix** (Sonja92yu) | LAYO edit: fewer invisible windows in Build/Buy, CAS and Create-a-Style | Catalog and CAS lag, UI memory leak | No numbers [community] | Fine | Nexus 242 | **Yes** |
| **S3SS tick limiter** (Precise) | Caps ticks so CAS does not spin | CAS lag, heat | [community] | — | — | **Yes** |
| **NRaas MasterController "Compact View"** (MC CAS) | Shows one tile per CAS item instead of one per preset (usually three) | Slow category loads in CAS with lots of CC | "Speeds up catalog load times remarkably", "solved my lag to a large extent" [community, EA forum threads 2056260 and 328865]; the 2014 MTS tips thread lists "limit CAS items to one preset each" [community, MTS 525052] | Needs MC **Integration** to replace EA CAS with MC CAS [community]. That **turns off Smooth Patch's fast clothing loading** [verified, MTS page]. NRaas plus simler90: NRaas won't support the save (docs/07) | https://www.nraas.net/community/MasterController (manual, Cloudflare) | MC is optional in the manifest; Integration and Compact View are **not** covered anywhere |
| **Let CAS thumbnails finish** | Wait after adding CC until `CASThumbnails.package` is built | Endless "processing" when finishing a family | Fixes the hang [community, Steam 3003304734674860838] | — | — | Section 1 already |
| **Enter CAS right after loading** | Use CAS while address-space use is lowest | CAS crash in long sessions | [community, EA moderator] | Fine | — | **New** (advice) |
| **Clean saved styles and custom presets** | Fewer entries in CAS and Create-a-Style | Slow CAS / CASt | [community, MTS 525052] | Fine | — | Not needed here: `SavedOutfits` is empty [verified] |
| **Hide unused CAS parts or presets** | Sets the CASP "hidden in CAS" flag or deletes CASPs (s3pe, Kuree's EasyCASP Editor); Face Preset Editor has a "Disabled" option for face presets | Too many items per CAS category | "Great tool to hide CAS items you don't want to load" [community, MTS 525052]; EasyCASP Editor would not run for one user in 2018 [community, MTS 619631] | CC only. Store `.ebc` is encrypted; never edit store content (section 1). Hidden parts are probably still enumerated into `CASPartCache` [inferred] | EasyCASP: simlogical den forum topic 1589; Face Preset Editor: https://modthesims.info/d/664206 | **New** (advice) |
| **Clear CAS caches after content changes** | Rebuilds `CASPartCache`, compositor caches, thumbnails | Blank CAS thumbnails, CAS crash after CC changes | [community, EA forums] | — | — | **Yes** (`cache-clear`) |
| **Add a missing THUM resource** to a CC item (s3pe) | Ships a 256×256 thumbnail so the game doesn't render one | Blank or slow thumbnail for one CC item | [community, sims3tutorialhub] | — | — | Per-item creator fix, not ours |
| **Cap FPS at 30 for Create-a-Style** | Lower frame cap | CASt lag that builds up | [community, Steam 133258092239738197] | S3SS already caps at 60 | — | Covered (S3SS `frameRateLimit`) |
| **Disable MC's clothing filter** | Shows every outfit category | Clutter, not speed | [community] | Needs MC CAS (see above) | — | Not a performance fix |

### 5.3 Community fixes for many installed items

| Fix | What it changes | Symptom | Reported gain | Fit with our stack | Download | Ours? |
|---|---|---|---|---|---|---|
| **CC Magic sets** | Groups CC into sets you can switch on and off; can merge; shows names and thumbnails | Too much CC loaded at once | "Game slows noticeably above ~200 CC files" (old guide, not measured) [community, CC Magic guide]; merged files sometimes don't show up [community, Steam 1457328927848038488] | Unencrypted CC only; cannot touch store `.ebc` (section 1). Our Phase 0 found no load cost from 143 packages [verified] | https://modthesims.info/d/461888 | **New** (advice, optional) |
| **Delphy's Dashboard** | Flags conflicting and duplicate `.package` files | Crashes from bad or duplicate CC | Good at conflicts, not at corrupt CC [community, EA moderator] | Overlaps our `check-conflicts.ts` | https://modthesims.info/t/353597 | Covered by `check-conflicts.ts` |
| **Launcher tabs; subfolders for Downloads/Exports** | Keeps the launcher from listing everything | Slow launcher with many items (500-item view) | [community, MTS 525052] | We bypass the launcher anyway | — | Covered (bypass) |
| **Built-in catalog and CAS content filters** | Show only base game, one pack, store or CC | Finding things in a big catalog | UX only [verified code] | — | In game | Built in |
| **LazyDuchess Build/Buy Search** | Adds a search box (name and description) to the Build/Buy catalog | Finding items among thousands | UX only [community, simscommunity.info] | Unknown whether it needs MonoPatcher or touches the catalog layouts the Catalogue Lag Fix edits [inferred risk] | https://www.patreon.com/posts/ts3-catalog-mod-82175020 | **New** (optional, UX) |
| **Recategorization mod** (phantom99, Nexus 128) | Fixes the categories of EA's Buy and CAS items, plus small fixes | Items in wrong categories | UX only [community, search summary; page 403 to us] | Edits EA catalog resources; overlap with the Catalogue Lag Fix possible [inferred] | https://www.nexusmods.com/thesims3/mods/128 | **New** (optional, UX) |
| **Turn memories off** | Fewer memory objects and photos | Save bloat | [community] | — | — | Yes (optional `enablememories = 0`) |
| **Disable unused packs** (launcher pack selection, "Any Game Starter", game selectors) | Runs a subset of EPs/SPs | Load time, memory with all packs | "EA told people to disable packs for performance" [community, Steam 1473095965298580683]; no numbers | Against this project's target (all EPs/SPs) | — | **Not a candidate** |
| **"Decrapified" pre-installed store packs** (e.g. nausallien) and Recompressorizer Redux + kt store fixes on store content | Redistributed, re-packed EA store content | Launcher problems with store content | [community] | Redistributes EA files: same rule as the G4TW repacks (docs/07 §2.6) | — | **Excluded** |
| **Old core mods (XCAS-style, AwesomeMod)** | Replaced CAS and catalog code | — | — | Conflict with simler90 (docs/07) | — | **Excluded** |
| **NRaas Mover** | Household moving rules | Not CAS or catalog related | — | — | — | Not relevant |
| **S3SS `Uncompressed Sim Textures`** | Live-mode Sim textures at CAS quality | Looks, not speed; more memory | [verified README] | Only with DXVK | — | Off, keep off |

### 5.4 New candidates ranked (gain per effort, then risk)

| Rank | Candidate | Expected gain | Effort | Risk | Duplicate / conflict |
|---|---|---|---|---|---|
| 1 | **Measure CAS first** with the store set: CAS open time, category switch time, hitches > 50 ms, address space (section 4 M6 already plans the repro actions) | Shows whether CAS lag remains on our stack after Smooth Patch, Catalogue Lag Fix and S3SS | Low (agent actions + `repros.json` entry) | None | — |
| 2 | **MasterController Integration + Compact View**, A/B against Smooth Patch's CAS loading | "Remarkable" faster category loads [community]; may beat or lose to Smooth Patch's scroll loading, which MC CAS turns off [verified] | Low (MC is already optional in the manifest; add Integration) | Medium: MC CAS replaces all CAS interactions; NRaas won't support saves with simler90 | **Conflicts with `smoothpatch-package`'s fast clothing load** inside MC CAS |
| 3 | **CAS config experiment:** lower `GraphicsRules.sgr` `CASRenderTargetSize` (2048 → 1024); test whether `Default.ini [CAS]` `SimCompositorCacheSize` is read at all (500 MB → 100 MB) | Less CAS memory or disk churn [inferred]; first prove the keys are read (`CacheBudget` lesson) | Low (same patching as `cache-budget`) | Low (CAS preview quality) | None |
| 4 | **Advice block for heavy-CC players:** hide unused CC CASPs and presets (s3pe, EasyCASP Editor, Face Preset Editor), CC Magic sets, enter CAS early in a session, let thumbnails finish | Fewer items per category [community] | Very low (doc) | None (CC only, never store `.ebc`) | — |
| 5 | **Script micro-fix:** skip `GetFeaturedCASItemsListFiltered` and `IncViewCountStoreFeaturedItem` when the store is off (our `stutter-fixes`, MonoPatcher) | Near zero with S3SS's featured-items patch [inferred] | Low | Very low | Only if rank 1 shows the call inside a hitch |
| 6 | LazyDuchess Build/Buy Search; Nexus 128 recategorization | UX in big catalogs, no speed | Low | Low to medium (unknown overlap with the Catalogue Lag Fix) | Run `check-conflicts.ts` against `catalogue-lag-fix` first |

Not candidates: disabling packs (against the target), pre-installed store repacks and recompressing store content (redistribution), XCAS/AwesomeMod (core-mod conflict), S3SS `Uncompressed Sim Textures` (memory), NRaas Mover (unrelated), a separate "hide unowned store items" mod (store-off settings already do it, §5.1).

### 5.5 Sources for section 5

- Our repo: `game-copy/decompiled/Sims3GameplaySystems/Sims3.Gameplay.UI/StoreUIModel.cs`; `game-copy/decompiled/UI/Sims3.UI.CAS/CASClothingCategory.cs`, `CASHair.cs`, `CASPuck.cs`, `CASController.cs`; `game-copy/decompiled/UI/Sims3.UI/CatalogProductFilter.cs`; `game-copy/bin/Default.ini`, `GraphicsRules.sgr`, `TS3W.exe` (UTF-16 strings); `tools/setup/fixes.json`; user folder listing (2026-10-13).
- Smooth Patch page (CAS loading, MC CAS incompatibility, `kCASMusicFix`): https://modthesims.info/d/658759
- S3SS README: https://github.com/sims3fiend/Sims3SettingsSetter ; S3PP review: https://simscommunity.info/2026/07/16/sims-3-performance-patch-mod-review/
- MTS performance tips thread (MC one preset per item, EasyCASP Editor, launcher 500 items, clean presets): https://modthesims.info/t/525052
- MasterController Compact View and Integration (search summaries; pages 403 to our fetch): https://forums.ea.com/discussions/the-sims-3-en/any-way-to-add-clothes-categorizersfilters-into-cas/2056260 ; https://forums.ea.com/discussions/the-sims-3-en/why-must-these-files-re-load-all-the-time/328865/replies/328866 ; https://steamcommunity.com/app/47890/discussions/0/3362406825542633227
- CAS thumbnails and CAS crashes: https://steamcommunity.com/app/47890/discussions/0/3003304734674860838 ; https://forums.ea.com/discussions/the-sims-3-technical-issues-en/re-custom-content-showing-up-blank-in-cas/8290460 ; https://www.tumblr.com/sims3tutorialhub/tagged/G ; https://answers.ea.com/t5/Technical-Issues-PC/Sims-3-Crashes-Unexpectedly-in-CAS/td-p/8493010
- Create-a-Style lag: https://steamcommunity.com/app/47890/discussions/0/133258092239738197
- Hiding CAS parts and presets: https://modthesims.info/t/619631 ; https://modthesims.info/d/664206 ; https://simblr.cc/ts3/tutorial/6662-basic-editing-of-cas-cc-in-s3pe-for-beginners.html
- CC Magic: https://modthesims.info/d/461888 ; https://steamcommunity.com/app/47890/discussions/0/1457328927848038488
- Build/Buy Search: https://simscommunity.info/2023/05/18/sims-3-build-search-mod-lazyduchess/ ; recategorization: https://www.nexusmods.com/thesims3/mods/128
- Pack selection: https://steamcommunity.com/app/47890/discussions/0/1473095965298580683 ; pre-installed store packs: https://www.tumblr.com/nausallien ; Recompressorizer and kt store fixes: https://steamcommunity.com/app/47890/discussions/0/3377008022021249317

## 6. Phase 0 result: store content vs none (2026-10-12)

`offload-phase0.ts --only nocc-200,store-200 --minutes 5 --save Baseline-BP.sims3`; both variants start right after `cache-clear` (cold caches), 5 min at speed 3 with autopilot, 30 samples each, no probe errors. `store-200` = Rodney's CC plus the Store Updates repacks (test-only, `store-repacks-test`). Raw data: `probe-out/phase0/2026-10-12T16-14-02/` [verified].

| | no CC, no store | CC + store repacks | change |
|---|---|---|---|
| launch to live mode | 80 s | 125 s | **+45 s (+56%)** |
| committed address space (median / max) | 2,281 / 2,339 MB | 2,377 / 2,452 MB | +96 / +113 MB |
| largest free block (median / min) | 1,469 / 1,293 MB | 1,421 / 1,347 MB | -48 MB median |
| ResourceCache bytes (median / peak) | 388 / 421 MB | 482 / 513 MB | +94 MB |
| sim minutes advanced in 5 min | 3,208 | 2,926 | -9% |

Reading:
- **Load time is the big cost** of a heavy store set (+56%); address space grows by only ~100 MB and 1.3 GB stays free, so Error 12 is not close in a 5-minute session [verified for this run]. Longer sessions and saves still need the M-items of §4.
- **The ResourceCache passes its 200 MB budget** in both runs (421 and 513 MB); the budget is not what limits it, as Phase 0 found before [verified].
- The sim ran ~9% slower with the store set; one run each, so treat as a hint, not a result [inferred].
- Next: split load time (launch -> menu vs menu -> live, M1) and warm vs cold caches (M2) to find where the 45 s go; the Performance Patch run (M8) needs its manual Nexus download (mods/235, not in `~/Downloads/ts3-fixes`).

### 6b. Load split, cold vs warm (2026-10-12, `--only nocc-200,store-200,store-200 --minutes 2`)

`cycle.ts` timings per stage; "menu scan" = from the agent answering until the "Mod Scripts Found" dialog appears and the menu is quiet (the game is still scanning content) [verified]. Data: `probe-out/phase0/2026-10-12T16-30-07/`.

| | launch -> menu | menu scan | world load | to live | total | ResourceCache peak |
|---|---|---|---|---|---|---|
| no CC, cold | 46 s | 11 s | 22 s | 5 s | **84 s** | 389 MB |
| CC + store, cold | 50 s | 23 s | 32 s | 8 s | **113 s** (+29 s) | 501 MB |
| CC + store, warm | 51 s | 25 s | 28 s | 0 s | **104 s** (+20 s) | 518 MB |

- Store content costs ~+12 s in the menu scan and +6 to +10 s in the world load; launch to menu barely moves (+4 s).
- Warm caches save ~9 s, mostly in the world load; clearing caches every launch is not worth it (confirms docs/17 M2).
- The earlier 80 vs 125 s (§6) included different dialog timing; these split numbers are the reference.

## 7. Health check results (2026-10-13)

Candidate 2 now exists as a separate read-only script, `node tools/setup/content-health.ts [--user <path>] [--json] [--self-test]` (not yet wired into `install-fixes.ts check`). It never deletes or moves files. Both runs below are [verified, script output]; the live caches were empty because a `cache-clear` ran just before.

| Check | Live user folder | Backup `D:\Sims3-backup\2026-10-11\Documents-The Sims 3` |
|---|---|---|
| DCCache | 1 `.ebc` file, ~0 MB, no `dcc.ent` | 1 `.dbc` (11 KB), no `dcc.ent`; far under `DBCacheMaxSizeMB` 200 |
| `.sims3pack` in Downloads | none | 6 `dynamicchallenge_*.Sims3Pack` files (0.3 MB) |
| Sync conflict copies | none | 18 (`*-<pc>`, `*-LAPTOP-G3`), 415 MB, mostly `simCompositorCache-` (251 MB) and `compositorCache-<pc>` (99 MB) |
| `DCCache` line in `Mods/Resource.cfg` | no | **yes** (`PackedFile DCCache/*.dbc`) |
| Caches at their cap | none | none (the live names are tiny; the big files are the conflict copies) |
| Mods | 172 packages, 5.2 GB; 5.1 GB of that is the 4 store repacks | same set |
| Never loaded (deeper than Resource.cfg) | 1: `Packages/ModTheSims/Body Shop/Clothing/Female/Mixed Sets - Young Adult/BraBikiniNude_breastfix.package` (6 levels, the cfg mounts 5) | same file |
| Duplicate file names | 1: `Nikel23 - Interact on sloped terrain.package` in `Packages/` and in `Packages/ModTheSims/.../Recategorisers and Hiders/` | none |
| Packages > 2 GB | none (largest repack 1.98 GB) | none |
| Excluded store repacks (docs/07 §2.6) | 4 flagged (test-only `store-repacks-test`, expected) | 4 flagged |
| SigsCache / DCBackup | both present, ~0 MB | both present, ~0 MB |

Reading: on this install only the duplicate Nikel23 package and the one too-deep package matter in game; the backup's 415 MB of conflict copies and its DCCache line are what a player restoring that folder would carry over.
