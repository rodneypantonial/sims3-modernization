# 07. Known fixes catalog (input for the one-shot installer)

Researched 2026-10-11 for: Steam build **1.67** (`Version.tag` says `LatestBase = 1.67.2.024037` [verified, local]), all expansions and stuff packs, Windows 11, RTX 3070 Laptop GPU (`Options.ini` `lastdevice = 0;10de;24dd` [verified, local]), i7-11800H.

Builds on `01-existing-fixes-comparison.md` (what each engine fix does), `03-prior-art-re.md` (S3SS internals, local install findings) and `06-test-harness-prior-art.md` (MonoPatcher, No Intro). Those details are not repeated here. Machine-readable manifest: `tools/setup/fixes.json`.

Tags: **[verified]** checked today against the project's own page, release API or source. **[community]** reported by a maintained guide or the mod page but not checked by us. **[inferred]** our reasoning; confirm in the baseline.

The main community reference is the Steam guide **"The Sims 3 Performance & Bug Fix Guide 2026"** by Anime_Boom (https://steamcommunity.com/sharedfiles/filedetails/?id=1131162350, 2,645 ratings, changelog entries up to 2026-08-09; it targets Steam 1.67 on Windows 10/11). Its order: ASI loader, S3SS, DXVK, MonoPatcher, NRaas, simler90 core mod, Sonja92yu performance mods, routing fixes, then optional per-pack bug fixes. [verified, page read]

---

## Summary table

D = in the default set, O = optional, X = excluded (not needed or not allowed). "Download" says whether the installer can fetch the file itself: **GH** = GitHub release (direct, with sha256 from the API), **MTS** = ModTheSims `getfile.php` link (works without login [community], but the link changes with every update, so the installer reads it from the page), **Nexus** = needs a free account and a click (manual), **NRaas** = nraas.net (behind Cloudflare, returned HTTP 403 to scripted fetches today [verified]; manual).

| ID | Fix | Author | Version (date) | Download | Target | Set |
|---|---|---|---|---|---|---|
| `asi-loader` | Ultimate ASI Loader (Win32) | ThirteenAG | v9.7.4 (2026-08-16) | GH | Bin `wininet.dll` | D |
| `dxvk` | DXVK x32 `d3d9.dll` | doitsujin | v3.1.1 (2026-09-15) | GH | Bin | D |
| `dxvk-conf` | `dxvk.conf` for TS3 | (ours) | n/a | generated | Bin | D |
| `s3ss` | Sims 3 Settings Setter | sims3fiend | 1.6.3 (2026-07-20) | GH | Bin | D |
| `s3ss-config` | S3SS patch selection (`S3SS.toml`) | (ours) | n/a | generated | Documents | D |
| `smoothpatch-package` | Smooth Patch tuning package only | LazyDuchess | 2.1 (zip 2024-11-05, page 2026-01-27, unsupported) | MTS | Mods\Packages | D |
| `monopatcher` | MonoPatcher library | LazyDuchess | 0.3.0 (2025-04-04) | GH | Bin + Mods\Packages | D |
| `nraas-errortrap` | NRaas ErrorTrap (Steam 1.67 build) | NRaas (Twallan, Chain-Reaction) | unknown | NRaas | Mods\Packages | D |
| `nraas-overwatch` | NRaas Overwatch | NRaas | unknown | NRaas | Mods\Packages | D |
| `simler90-core` | simler90's Gameplay Core Mod (Steam file) | simler90 | UPDATE242 (2026-09-20) | MTS | Mods\Packages | D |
| `catalogue-lag-fix` | Catalogue Lag and Memory Leak Fix | Sonja92yu | unknown | Nexus 242 | Mods\Packages | D |
| `houseboat-fix` | House Boat Fix | FoulPlay | v1.0.0.1a-testing (2026-09-02) | GH | Mods\Packages | D |
| `routing-sloped-terrain` | Interaction on sloped terrain enabler | (MTS 529671) | unknown | MTS | Mods\Packages | D |
| `routing-sims-less-space` | Sims need less Space | (MTS 683612) | unknown | MTS | Mods\Packages | D |
| `routing-pets-less-space` | Pets need less Space | (MTS 683307) | unknown | MTS | Mods\Packages | D |
| `routing-toddler` | Toddler Routing Fix | TheSweetSimmer | unknown | MTS | Mods\Packages | D |
| `routing-ocean-play` | Autonomous Play In The Ocean Fix | (MTS 686121) | unknown | MTS | Mods\Packages | D |
| `resource-cfg` | Standard `Mods\Resource.cfg` with Overrides | community | n/a | generated | Mods | D |
| `nointro` | No Intro (all intro videos incl. Maxis logo) | ours (`tools/harness/nointro.ts`) | n/a | generated | Mods\Packages | D |
| `options-ini` | Performance options in `Options.ini` | community | n/a | generated | Documents | D |
| `cache-clear` | Clear caches after any mod change | community | n/a | action | Documents | D |
| `laa-check` | Confirm LAA bit on TS3W.exe (no patch) | n/a | n/a | check | Bin | D |
| `s3pp` | Sims 3 Performance Patch (beta) | Sonja92yu | 1.1 beta (2026-07) | Nexus 235 | Bin | O |
| `nraas-mastercontroller` | NRaas MasterController | NRaas | unknown | NRaas | Mods\Packages | O |
| `nraas-register` | NRaas Register | NRaas | unknown | NRaas | Mods\Packages | O |
| `nraas-saver` | NRaas Saver | NRaas | unknown | NRaas | Mods\Packages | O |
| `nraas-traveler` | NRaas Traveler | NRaas | unknown | NRaas | Mods\Packages | O |
| `nraas-storyprogression` | NRaas StoryProgression | NRaas | unknown | NRaas | Mods\Packages | O (not with simler90) |
| `routing-guitar-less-space` / `routing-bass-less-space` | Instrument space fixes | (MTS 671269 / 671217) | unknown | MTS | Mods\Packages | O |
| `fewer-memories` | No (or fewer) automatic memories | (MTS 446281) | 2014-01-16 | MTS | Mods\Packages | O |
| `world-fix-*` | ellacharmed world routing fixes (12 EA worlds) | ellacharmed | 2011–2014 | MTS / WordPress | world file replacement | O (new game only) |
| `tiny-ui-fix` | Tiny UI Fix (UI scaling) | just-harry | v1.5.3 (2026-03-14) | GH | generates a package | O |
| `less-banding` | TS3 Banding Fix | LazyDuchess | 1.0 (2024-12-18) | GH | see notes | O |
| `regul-save-cleaner` | Regul Save Cleaner (tool, not a mod) | Regul | unknown | MTS 652440 | none (external tool) | O |
| — | Alder Lake patcher / IntelFix | LazyDuchess | 1.0.1 | GH | | X (11th gen; S3SS covers it anyway) |
| — | LAA / 4GB patch | NTCore | | | | X (bit already set) |
| — | GraphicsRules/GraphicsCards edits | community | | | | X (DXVK spoofs a GTX 580) |
| — | Smooth Patch ASI (`TS3Patch.asi` + its `wininet.dll`) | LazyDuchess | | | | X (S3SS replaces it) |
| — | "Sims 3 Store Fixes 08-02-2013", "Store Updates 001-051 v1.4" | Games4theworld repacks | | | | X (see section 2.6) |

---

## 1. Engine and exe level (Game\Bin)

### 1.1 Ultimate ASI Loader — `asi-loader` [verified release]
- **Fixes:** nothing by itself; loads every `*.asi` in Bin (S3SS, MonoPatcher, S3PP).
- **Source:** https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4, asset `Ultimate-ASI-Loader.zip` (Win32), sha256 `952cebfc30d525afc2bdbaca954329d405ded3aa688a83027354dae14dfd5c5f`, published 2026-08-16. The x64 zips are useless here. License MIT, so redistribution is allowed [verified].
- **Install:** take the DLL from the zip (it ships as `dinput8.dll` [community]) and save it as `Game\Bin\wininet.dll`. The Steam guide and the S3SS README both use the `wininet.dll` name [verified]. Don't use `d3d9.dll` (DXVK owns that name).
- **Conflicts:** exactly one loader. This install already has **Ultimate ASI Loader 2021 as `ddraw.dll`** (03 §0 [verified]); MonoPatcher's zip and Smooth Patch's zip each carry their own `wininet.dll`; S3SS also accepts dxwrapper. The Steam guide switched to dxwrapper in January 2026 and back to Ultimate ASI Loader on 2026-04-11 [verified changelog]. The installer must back up and remove `ddraw.dll` (after checking its version resource says `Ultimate ASI Loader`) and never copy a bundled `wininet.dll`.

### 1.2 DXVK — `dxvk`, `dxvk-conf` [verified]
- **Fixes:** keeps managed texture memory out of the 32-bit address space (the biggest Error 12 fix), and reports a known GPU so the game picks proper settings.
- **Source:** https://github.com/doitsujin/dxvk/releases/tag/v3.1.1, asset `dxvk-3.1.1.tar.gz`, sha256 `40565b4a724aadc4433fa4e010b4b23916d9b1f1baeee64e17186db94f54e608`, 2026-09-15. Zlib license (redistribution allowed with notice) [verified].
- **Install:** only `dxvk-3.1.1/x32/d3d9.dll` into Game\Bin.
- **Built-in profile:** DXVK's `config.cpp` matches `\\TS3(W)?\.exe$` and sets `d3d9.customVendorId = 10de`, `customDeviceId = 1080`, `customDeviceDesc = Geforce GTX 580` (comment: "Worse shadow quality on unknown AMD cards") [verified, source]. So no GraphicsRules/GraphicsCards edits are needed; the Steam guide dropped its GPU-edit and VRAM steps in February 2026 for that reason [verified changelog].
- **`dxvk.conf` (generated):** leave `d3d9.textureMemory` at its default (the guide also removed its memory-editing step on 2026-07-01 [verified]). Contents we write:
  ```ini
  d3d9.samplerAnisotropy = 16
  # Frame cap lives in S3SS (see 1.3); set d3d9.maxFrameRate only if the S3SS limiter is off.
  # dxvk.hud = fps,memory   (enable for measurement runs)
  ```
  Add `dxvk.hideIntegratedGraphics = True` only if DXVK picks the Intel iGPU (02 §4).
- **Conflicts:** any other `d3d9.dll` (ReShade installed as d3d9, dgVoodoo2, D3D9On12 tricks). `D3DShaderReplacer.asi` (present locally, 03 §0) hooks D3D9 too; keep it out of the default set [inferred]. If S3SS's overlay crashes at first render with DXVK, set `disable_overlay = true` in `[qol.ui]` [verified README].
- **Not used:** `dxvk-gplasync` (async shader fork, possible pop-in) [community]; not needed once the shader cache is warm.

### 1.3 Sims 3 Settings Setter — `s3ss`, `s3ss-config` [verified]
- **Fixes:** the ASM patch set (allocator, thread stacks, RefPack SIMD, tick limiter, crash logs, startup dialog, lot streaming); internals in 03 §1.
- **Source:** https://github.com/sims3fiend/Sims3SettingsSetter/releases/tag/1.6.3, asset `Sims3SettingsSetter.asi`, sha256 `69ce87ee84528748ee1f19c1f7cb2183e8e8cb7f7cae57cdb64098a7ebcdc13a`, 2026-07-20. **No license file** (all rights reserved): the installer downloads from GitHub, it must not re-host the file.
- **Config file:** `Documents\Electronic Arts\The Sims 3\S3SS\S3SS.toml`. From the source [verified]: each patch is `[patches.<InternalName>]` with `enabled = true|false` plus its settings by name; QoL is `[qol.ui]` (`toggle_key`, `disable_hooks`, `disable_overlay`, `font_scale`), `[qol.memory_monitor]` (`enabled`, `warning_threshold` default 3.5, `warning_style` `overlay|modal`), `[qol.borderless_window]` (`mode`).
- **Patch selection** (internal name → display name; * = on by default in S3SS):

| Internal name | Display name | Default set | Why |
|---|---|---|---|
| `CPUOptimization`* | CPU Thread Optimization | on | Fixes hard-coded `SetThreadIdealProcessor`; also the Alder Lake fix |
| `StartupWarningDialogFix`* (+ Hide Dialogue) | Startup Warning Dialog Fix | on | Without it, Windows 11 25H2 can hang on a black screen when a core mod's version check fires [verified README; matches 2026 Tumblr reports, community] |
| `ExpandedCrashLogs`* | Expanded Crash Logs | on | Diagnostics, no cost |
| `Mimalloc` | Mimalloc Allocator | on | Memory, restart needed |
| `OversizedThreadStackFix` | Oversized Thread Stack Fix | on (off if `s3pp` is used, see section 5) | 80–170 MB address space |
| `RefPackDecompressor` | RefPack Decompressor Optimization | on | Author: "most impactful" |
| `SmoothPatchPrecise` | Smooth Patch (Precise Flavour) | on, `tickRateLimit = 480`, `frameRateLimit = 60` | Replaces LazyDuchess's ASI. The Steam guide uses 960 TPS; we start at the default 480 and test 960 later [community] |
| `TimerOptimization` | Timer Optimization | on | 1 ms timer |
| `CreateFileRandomAccess` | CreateFileW Random Access | on | I/O hint, restart needed |
| `GCFinalizeThrottle` | GC Finalizer Throttle | optional (experimental) | Stutter; test against baseline |
| `LotStreamingOptimizations` | Lot Streaming Optimizations | optional (experimental) | Stutter on lot loads |
| `AdaptiveWait` | Adaptive Thread Waiting | optional (experimental), never with S3PP `ShortWaitSpin` | Both spin short waits [inferred] |
| `GCTryToCollect` | Chunky Patch | off | Raises memory use |
| `WorldCacheSizePatch` | WorldCache Size Uncap | off | Can crash when set too high |
| `SmoothPatchClassic` | Smooth Patch (Original Flavour) | off | Never together with Precise |
| `UncompressedSimTexturesPatch` etc. | Graphics patches | off | Cosmetic, cost memory |
| `StoreFeaturedItemsDisablePatch` | Disable Store Featured Items Download | optional | Stops `FeaturedItems` growth |

  `[qol.memory_monitor] enabled = true`. Exactly one frame cap: the S3SS Precise limiter (the Steam guide moved the cap from DXVK to S3SS on 2026-03-05 [verified changelog]). This changes 02 §4, which capped in DXVK; either works, but never both.
- **Needed on Steam 1.67:** yes. Its patterns match the Steam exe (03 §1 [verified]).

### 1.4 Smooth Patch package — `smoothpatch-package` [verified page]
- **Fixes:** the script side of Smooth Patch 2.x: faster CAS clothing loads, CAS audio fixes, smoother Create-a-Style drag and drop. S3SS's README says to keep using it next to S3SS's tick limiter [verified].
- **Source:** https://modthesims.info/d/658759/smooth-patch-2-1.html, file `ld_TS3Patch.zip` (uploaded 2024-11-05); page marked **Unsupported as of 2026-01-26**, pointing users to S3SS [verified]. Take only `Packages\ld_SmoothPatch.package` (46,524 bytes, XML tuning) → `Mods\Packages`. Do **not** install `TS3Patch.asi`, `TS3Patch.txt` or the bundled `wininet.dll`; the Steam guide dropped the whole mod on 2026-01-01 [verified]. Keeping the package is S3SS's advice, not the guide's; low risk [inferred].
- **License:** MTS default (no re-hosting).

### 1.5 MonoPatcher — `monopatcher` [verified release]
- **Fixes:** nothing by itself; lets script mods replace game methods at runtime instead of shipping a core mod. Required by `houseboat-fix`, the badge file of `catalogue-lag-fix`, and several optional fixes (e.g. Watery Grave Plaque Edit).
- **Source:** https://github.com/LazyDuchess/MonoPatcher/releases/tag/0.3.0, asset `MonoPatcher.zip` (811,083 bytes), 2025-04-04; the API exposes **no digest** for this older release. No license file.
- **Install:** `ld_MonoPatcher.package` into a subfolder of `Mods\Packages`; `MonoPatcher.asi` into Bin; skip the zip's `wininet.dll`. Needs the latest Visual C++ x86 redistributable [community, Steam guide]. Its signatures match Steam 1.67 (06 §0.2 [verified]). The guide says the fixes it lists only need the `.package`; drop the `.asi` if it crashes [community].

### 1.6 Sims 3 Performance Patch — `s3pp` (optional) [community]
- **Fixes:** load time (script type-lookup index `MonoTypeEHashProbe` + `SimulatorStageProfiler`, mods index warm-up), address space (`StblBucketLayoutTuning` ~290 MB, `ThreadStackDiet` ~70 MB by giving ~150 threads 0.5 MB stacks, `AllocatorArenaTuning`), smaller caches (`LocalizedStringCache`, `MonoIsinstCache`), `ShortWaitSpin` [community, review at simscommunity.info 2026-07-16 quoting the mod page].
- **Source:** https://www.nexusmods.com/thesims3/mods/235, files `Sims3Performance.asi` + `Sims3Performance.ini` → Bin. Free Nexus account, manual download. Our scripted fetch got HTTP 403, so version 1.1 beta (July 2026) comes from 01, not re-checked. Source code reportedly included [community].
- **Conflicts:** see section 5. Known risk: wrong or blank UI labels from `LocalizedStringCache`.

### 1.7 Not needed (excluded)
- **LAA:** already set in the Steam exe (Characteristics `0x123`, 03 §0 [verified]); the Steam guide notes it has been there since Late Night patch 1.17.60 [community]. The installer only checks the bit (`laa-check`) and warns if a Steam "Verify" ever cleared it.
- **Alder Lake patcher / IntelFix:** 11th-gen CPU, and S3SS's CPU Thread Optimization now covers 12th–14th gen (the guide removed its Alder Lake section on 2026-03-31 and says to delete any old `IntelFix.dll`) [verified changelog].
- **GraphicsRules.sgr / GraphicsCards.sgr edits:** replaced by DXVK's built-in profile (1.2).
- **Smooth Patch ASI:** replaced by S3SS Precise. The local `TS3Patch.asi` patches the same `SleepEx` wrapper and must be removed (03 §0 [verified]).
- **"Max memory"/HeapSize ini edits:** myth (01 §H).

---

## 2. Script and core mods (Mods\Packages)

### 2.1 simler90's Gameplay Core Mod — `simler90-core` [verified page]
- **Fixes:** hundreds of EA script bugs across the base game and packs, plus EA story progression (an "unofficial patch").
- **Source:** https://modthesims.info/d/659969/simler90-s-gameplay-systems-core-mod.html. Latest **UPDATE242**, updated 2026-09-20. Steam file: `simler90GameplayCoreMod-UPDATE242-Steam.7z` → `simler90GameplayCoreMod-UPDATE242-Steam.package` (12,807,314 bytes). Separate Retail, EAapp and Mac builds exist; **the Steam one is required**, because the core mod embeds the platform's `Sims3GameplaySystems.dll`/`Sims3GameplayObjects.dll`. Source code is shipped too.
- **Requirements/compat (from the page):** game 1.67 or later; "not compatible with any other mod that also modifies the GameplaySystems.dll or GameplayObjects.dll". Policy: may be modified and re-released on non-pay sites; no explicit permission to re-host the original, so download from MTS.
- **Is there an "NRaas-compatible variant"?** **No.** The page lists only Retail/Steam/EAapp/Mac builds and source [verified]. NRaas staff don't support saves that use it [community, Steam guide + MTS comments]; some users can't run it with NRaas, most report it works with ErrorTrap + Overwatch [community].
- **Required config:** NRaas Overwatch alarm hour 3 → 5 (City Hall → NRaas → Overwatch → Settings → Set Alarm Hour), once per new save, so the two nightly jobs don't overlap [community, Steam guide]. This is an in-game setting the installer can only print as a reminder.
- **Overlaps:** "Random Sim Fixes" is now covered by it (guide 2026-08-04) [verified changelog]; NRaas StoryProgression is redundant (simler90 fixes EA's SP, NRaas replaces it) [community, MTS comments].
- **New save:** removing a script mod later can damage saves that used it; the guide recommends a fresh save [community].

### 2.2 NRaas — `nraas-errortrap`, `nraas-overwatch` (default); MasterController, Register, Saver, Traveler, StoryProgression (optional)
- **Fixes:** ErrorTrap catches script exceptions and repairs corrupt references on load; Overwatch resets stuck Sims, cleans cars, elevators, stereos and orphaned data nightly (03 §5.3). Register can stop NPC classes (wild horses in Isla Paradiso cause lag) [community]; Traveler is the usual fix for travel EPs (WA, UL, ITF) [community]; Saver auto-saves.
- **Source:** https://www.nraas.net/community/ErrorTrap, `/Overwatch`, `/MasterController`, `/Register`, `/Saver`, `/Traveler`, `/StoryProgression`. Manual: Cloudflare blocks scripted downloads (403) [verified]. **Only ErrorTrap has per-version builds**: pick the 1.67 Steam one, not 1.69 [community]. Current version numbers: unknown (site not readable). Source: https://github.com/Chain-Reaction/NRaas (no license, no releases).
- **Notes:** one story-progression system only. NRaas files must all come from the same download period (shared NRaas core code) [community].

### 2.3 Catalogue Lag and Memory Leak Fix — `catalogue-lag-fix` [community]
- **Fixes:** Build/Buy, CAS and Create-a-Style creating too many invisible UI windows, which slows the catalogues and leaks memory.
- **Source:** Sonja92yu, https://www.nexusmods.com/thesims3/mods/242 (account, manual; 403 to us). Files `S3DBG_CatalogueLagMemoryLeakFix_min.package` (a LAYO-only mod, no code) and optional `S3DBG_RestoreEPBadges.package`, which restores EP badges the LAYO edit hides and **needs MonoPatcher**. The Steam guide links `mods/247`; search results show the mod as 242 — confirm the ID by hand.
- **Conflicts:** any other UI/LAYO mod overriding the same layouts [inferred].

### 2.4 Routing and stuck-Sim fixes
All from the Steam guide's "Step 8 - Routing Fixes (Essential)" [community]; all `.package` → Mods\Packages; versions not recorded on the guide.

| ID | Fix | URL | Pack | Default |
|---|---|---|---|---|
| `routing-sloped-terrain` | Lets Sims interact on gently sloped terrain instead of failing the route | https://modthesims.info/d/529671/interaction-on-sloped-terrain-enabler.html | Base | D |
| `routing-sims-less-space` | Smaller footprint for some interactions, fewer route failures | https://modthesims.info/d/683612/fix-sims-need-less-space.html | Base | D |
| `routing-toddler` | Toddler fall animation no longer cancels the whole queue | https://modthesims.info/d/685924/toddler-routing-fix.html | Base | D |
| `routing-pets-less-space` | Smaller pet interaction footprint | https://modthesims.info/d/683307/fix-pets-need-less-space.html | Pets | D |
| `routing-ocean-play` | Stops autonomous ocean play outside beach/resort lots | https://modthesims.info/d/686121/autonomous-play-in-the-ocean-fix.html | Island Paradise | D |
| `houseboat-fix` | Houseboat routing that froze the sim every few seconds in Isla Paradiso | https://github.com/FoulPlay/houseboatfix/releases/tag/v1.0.0.1a-testing, `fp.houseboatfix-1.0.0.1a.package`, sha256 `c530f2f09526f1342c83b87993a502dce82fa80df8c447e908ffff943541bf66` [verified] | Island Paradise | D (needs MonoPatcher; tag says "testing") |
| `routing-guitar-less-space` | Guitar needs less space | https://modthesims.info/d/671269/fix-guitar-needs-less-space-for-playing.html | Base | O |
| `routing-bass-less-space` | Bass needs less space | https://modthesims.info/d/671217/fix-bass-needs-less-space-for-playing.html | Late Night | O |

Lag sources covered elsewhere: elevators and derelict cars by Overwatch's cleanup alarms; ghosts and broken relationships by ErrorTrap; the Late Night "Elevator Shaft Placement FIX" (https://modthesims.info/d/695629) is a build-mode snap fix, not a lag fix.

### 2.5 Smaller EA-bug fixes (catalog only, not in the manifest)
The Steam guide lists about 55 cosmetic or single-bug fixes by pack (sections "Base Game Bug Fixes" to "Store Content Bug Fixes"). They don't touch performance or stability, so the installer leaves them out; install by hand from the guide. The ones from Rodney's old Mods folder map as follows [community]:

| Old mod | Current pick | Note |
|---|---|---|
| Eyeball UV fix | S-Club "ts3 EA Eyeball UVFix" (TSR 1280158, female only) | cosmetic |
| Read-to-sleep fix | Generations "Read Toddler to Sleep - Fixed" (danjaley Tumblr) | simler90 already fixes the **child** read-to-sleep bed bug; this one is the toddler interaction. Possible overlap [inferred] |
| Money tree fix | "Money Tree Bug Fix (Gatherer Trait)" (simsasylum.com topic 7933) | Supernatural |
| Other UV/mesh fixes | Base Game Halfwalls FIXED (MTS 597434), Tileable Items Shader FIX (655529), EA Eyeshadow Texture Fix | cosmetic |
| Random Sim Fixes | drop | covered by simler90 since 2026-08 |
| No More UL NPC Protests | drop | patched by EA (guide, 2026-07-26) |
| "Reduce/Remove Lag caused by Houseboats" | drop | replaced by `houseboat-fix` |
| UNI "FIX Missing Science Professor" | **avoid** | the guide reports it deletes all pre-made Sims from new and old saves |

### 2.6 Store content, and the "Store Fixes / Store Updates" packages (excluded)
- "Sims 3 Store Fixes 08-02-2013" and "Store Updates 001-051 v1.4" are **Games4theworld repacks** (credits: kissing_toast, notouching; guide last updated 2014) that fix *unofficially obtained* store content [community, Scribd copy of the G4TW guide]. They are not for bought content, can override EA store resources, and must not be redistributed. **Exclude, and the installer should flag them if found in Mods.**
- **For bought store content on Steam:** EA's launcher stopped delivering store content patches to 1.67 in December 2017. A full patch is `Documents\...\The Sims 3\ContentPatch\contentPatch.package` (about 7,976 KB); the fix is to restore that folder from an old install or backup [community, EA/Steam forums]. Locally, `ContentPatch\` only holds an empty `Download\` folder [verified, local]. Only relevant if Rodney owns store items with known bugs; content after January 2014 isn't affected. Not automatable without an EA-built file, so it stays out of the manifest.

---

## 3. World data fixes (ellacharmed) — optional, new game only

- **What they fix:** non-routable terrain paint, broken intersections and disconnected roads, which make the game constantly recalculate routes ("World Routing Lags"; 03 §5.4).
- **Index:** https://modthesims.info/wiki.php?title=Game_Help:World_Routing_Lags/Downloads [verified]. Each world has a "Resources" thread (to import into an edited world with S3PE) and a ready `.world` thread:

| World | `.world` download | Resources |
|---|---|---|
| Sunset Valley | https://modthesims.info/d/451817 | /d/450599 |
| Champs les Sims, Shang Simla, Al Simhara (WA) | https://modthesims.info/d/450569 | /d/448919 |
| Twinbrook | https://modthesims.info/d/456220 | /d/454885 |
| Bridgeport | https://modthesims.info/d/452601 | /d/451614 |
| Appaloosa Plains | https://modthesims.info/d/461424 | /d/460168 |
| Starlight Shores | https://modthesims.info/d/482952 | /d/481992 |
| Moonlight Falls | https://modthesims.info/d/488897 | /d/486830 |
| Sims University | https://ellacharmed.wordpress.com/2013/12/30/sims-uni/ | /d/517885 |
| Isla Paradiso | https://modthesims.info/d/514909 | /d/513265 |
| Riverview | https://modthesims.info/d/514904 | /d/495650 |
| Hidden Springs | http://ellacharmed.wordpress.com/2013/12/21/hidden-springs/ | /d/517205 |
| Lunar Lakes | https://modthesims.info/d/516858 | /d/516857 |
| Sunlit Tides | https://modthesims.info/d/516760 | /d/516342 |
| Monte Vista | https://modthesims.info/d/516849 | /d/516808 |

  Not covered: Roaring Heights, Aurora Skies, Barnacle Bay (listed without link), Dragon Valley, Lucky Palms, Midnight Hollow, Dragon Falls [community, EA forum]; Oasis Landing, Brightchester and Granite Falls aren't on the list.
- **Install:** replace the EA `.world` (keep the original renamed `.orig`) in the pack's `GameData\Shared\NonPackaged\Worlds\` folder [verified on the Bridgeport page, Program Files layout]; on Steam the installer should find the file by name under the Steam root [inferred]. Store worlds go through `InstalledWorlds` instead [inferred].
- **Since patch 1.42 the fixes only apply to a new game**; existing saves keep the routing data they started with. Use NRaas Porter to move households [verified, wiki FAQ].
- **Risks:** they date from 2011–2014 (Bridgeport notes compatibility with 1.26), so they may lack later EA world updates [inferred]. The Steam guide deliberately leaves them out: worlds saved in CAW with ghosts, mummies or SimBots get corrupted Sim data [community]. Islands users report the Isla Paradiso fix helps roads but not the port or residential lots [community].
- **Recommendation:** not default. Better as our own later work (step 4): a routing-data rebuild with a clean population, then measure.

---

## 4. Config and housekeeping

### 4.1 `Mods\Resource.cfg` — `resource-cfg` [community]
The local file only maps `Packages` up to 3 levels and has no `Overrides` [verified, local]. Write the standard framework layout (the one in MTS's mods-folder download that the Steam guide uses), with Overrides above Packages:
```
Priority 500
PackedFile Packages/*.package
PackedFile Packages/*/*.package
PackedFile Packages/*/*/*.package
PackedFile Packages/*/*/*/*.package
PackedFile Packages/*/*/*/*/*.package
Priority 1000
PackedFile Overrides/*.package
PackedFile Overrides/*/*.package
PackedFile Overrides/*/*/*.package
```
Exact priority values are community convention; what matters is Overrides > Packages [inferred]. Don't add `DCCache/*.dbc`.

### 4.2 No Intro — `nointro`
Use our own `tools/harness/nointro.ts` (overrides all 14 intro videos including the Maxis logo with empty VP6 stubs; already installed as `Mods\Packages\ts3-nointro.package` [verified, local]). Community equivalent: "No Intro with Maxis logo" https://modthesims.info/d/497903 (`nointromaxis.zip`, 2013). The framework's `nointro.package` misses the 1.50 Maxis logo [community]. Use only one.

### 4.3 `Options.ini` — `options-ini` [community: Steam guide "Bonus Step"; keys verified in the local file]
`Documents\Electronic Arts\The Sims 3\Options.ini`, section `[options]`. Default changes:
- `enableinteractiveloading = 0` — interactive loading screens make loads longer.
- `enableingamestore = 0` — the in-game store "uses quite a bit even when not used".
- `enabletelemetry = 0`, `receiveconnecttns = 0` — dead online services; the guide says online options cause lag.

Optional, because they change gameplay: `enablememories = 0` (or the `fewer-memories` mod), lower `maxactivelots`. The game may rewrite `Options.ini` after a driver update, so the installer should re-apply on each run [community].

### 4.4 Cache clearing — `cache-clear` [community]
After any mod change delete `CASPartCache.package`, `compositorCache.package`, `scriptCache.package`, `simCompositorCache.package`, `socialCache.package`, `WorldCaches\*`, `Thumbnails\*`, `DCCache\dcc.ent`, `DCCache\missingdeps.idx`, `FeaturedItems\*` (list from 01 §H), plus `SigsCache\*` and `DCBackup\*` except `ccmerged.package` (docs/17 candidate 6). The guide warns not to clear on every launch: a warm cache performs better.

### 4.5 Windows-side items (print as reminders, not automated)
High-performance GPU for TS3W.exe (laptop iGPU), Steam overlay off, OneDrive not syncing Documents (save failures), no emoji/special characters in the Windows user name (Mods folder won't load) [community]. Launcher bypass: Steam launch option `"Game\Bin\TS3W" -%command%` (can empty the Edit Town library) [community].

---

## 5. Conflicts and choices

| Overlap | Choice | Confidence |
|---|---|---|
| S3SS `SmoothPatchPrecise` vs LazyDuchess `TS3Patch.asi` vs S3SS `SmoothPatchClassic` | S3SS Precise only; remove `TS3Patch.asi`; keep `ld_SmoothPatch.package` | [verified] same `SleepEx` wrapper |
| ASI loaders: local `ddraw.dll` (UAL 2021), `wininet.dll` (UAL 9.7.4), MonoPatcher's/Smooth Patch's `wininet.dll`, dxwrapper | One loader: UAL 9.7.4 as `wininet.dll` | [verified] guide; double loading [inferred] |
| DXVK vs other `d3d9.dll` wrappers (ReShade-as-d3d9, dgVoodoo2) | DXVK | [verified] DXVK is the only one that frees texture address space (03 §9 [inferred]) |
| Frame cap: DXVK `maxFrameRate` vs S3SS Precise `frameRateLimit` vs Nvidia cap | S3SS only | [community] |
| S3SS `OversizedThreadStackFix` (64 KB on file-watcher threads) vs S3PP `ThreadStackDiet` (0.5 MB on ~150 threads) | Default: S3SS only (S3PP is optional). With S3PP: first run with S3SS's stack fix off (02 §7), then try both, because the Steam guide calls S3PP "fully compatible" with S3SS | [inferred]; guide [community] |
| S3SS `AdaptiveWait` vs S3PP `ShortWaitSpin` | Never both | [inferred] |
| simler90 core vs any other core mod (anything replacing `Sims3GameplaySystems.dll`/`Sims3GameplayObjects.dll`, e.g. old XCAS-style mods) | simler90 only | [verified, mod page] |
| simler90 vs NRaas StoryProgression | simler90 (it fixes EA SP); don't install NRaas SP | [community] |
| simler90 vs NRaas ErrorTrap/Overwatch | Keep both; Overwatch alarm hour 5; NRaas won't support the save | [community] |
| Core mod platform build | Steam build of simler90; 1.67 build of ErrorTrap | [verified page / community] |
| Fixes needing MonoPatcher | Install MonoPatcher whenever `houseboat-fix` or `catalogue-lag-fix` badge file is on | [community] |
| World fixes vs existing saves | New game only | [verified, wiki] |
| "Random Sim Fixes", UL protests fix, old houseboat lag mod | Drop (superseded) | [verified changelog] |

---

## 6. Recommended default set

Applied in this order (later steps depend on earlier ones):
1. **Back up** `TS3W.exe` → `TS3W.exe.orig` and the whole `Documents\Electronic Arts\The Sims 3`. `laa-check`.
2. **Clean up legacy Bin files:** move `ddraw.dll` (if it's UAL), `TS3Patch.asi`, `TS3Patch.txt`, any old `IntelFix.dll` and any stray `wininet.dll` to a backup folder. Leave `D3DShaderReplacer.asi` out (its mod needs it; not part of our base).
3. Bin: `asi-loader`, `dxvk` + `dxvk-conf`, `s3ss`, `monopatcher` (`.asi`).
4. Documents: `s3ss-config`, `resource-cfg`, `options-ini`.
5. Mods\Packages: `nointro`, `smoothpatch-package`, `monopatcher` (`.package`), `nraas-errortrap`, `nraas-overwatch`, `simler90-core`, `catalogue-lag-fix`, `houseboat-fix`, `routing-sloped-terrain`, `routing-sims-less-space`, `routing-pets-less-space`, `routing-toddler`, `routing-ocean-play`.
6. `cache-clear`. Print the manual reminders (Overwatch alarm hour 5 per new save, GPU preference, Steam overlay off).

Fully automatic (GitHub, sha256 known): UAL, DXVK, S3SS, houseboat fix. Automatic without hash: MonoPatcher (GitHub, no digest; pin the size 811,083 and record our own hash on first download). Semi-automatic (MTS `getfile.php`, scrape the current link from the page): simler90, Smooth Patch, routing mods. Manual (user downloads, installer verifies the file name and places it): NRaas, Nexus.

## 7. Optional set

S3PP (beta), S3SS experimentals (`GCFinalizeThrottle`, `LotStreamingOptimizations`, `AdaptiveWait`, `StoreFeaturedItemsDisablePatch`), NRaas MasterController, Register, Saver, Traveler, guitar/bass space fixes, fewer automatic memories, world fixes (new game only), Tiny UI Fix (only above 1080p; `Options.ini` is 1920×1080 today), LazyDuchess Banding Fix, Regul Save Cleaner (external tool for bloated saves, https://modthesims.info/d/652440). NRaas StoryProgression only if simler90 is **not** installed.

Each optional item goes in as its own row of the baseline table (one change at a time).

## Open points
- NRaas current version numbers and Nexus 235/242 versions: read by hand (both sites block scripts).
- Confirm the Ultimate ASI Loader zip's Win32 file name (`dinput8.dll` [community]) when first unpacked.
- Measure S3SS stack fix vs S3PP stack diet, and 480 vs 960 TPS.
- Check that the routing mods don't override the same tuning as simler90 (S3PE diff of resource keys) [inferred risk].

## Sources
- Steam guide: https://steamcommunity.com/sharedfiles/filedetails/?id=1131162350 (and its Tiny UI guide id=3323606378)
- GitHub release API (2026-10-11): doitsujin/dxvk, sims3fiend/Sims3SettingsSetter (+ source, commit on `main`), ThirteenAG/Ultimate-ASI-Loader, LazyDuchess/MonoPatcher, LazyDuchess/AlderLakePatcher, LazyDuchess/TS3-Banding-Fix, FoulPlay/houseboatfix, just-harry/tiny-ui-fix-for-ts3, sims3fiend/Sims3MonoModder
- DXVK profile: https://github.com/doitsujin/dxvk/blob/master/src/util/config/config.cpp
- simler90: https://modthesims.info/d/659969 ; Smooth Patch: https://modthesims.info/d/658759 ; Tiny UI Fix: https://modthesims.info/d/681624 ; No Intro: https://modthesims.info/d/497903
- S3PP review: https://simscommunity.info/2026/07/16/sims-3-performance-patch-mod-review/
- World routing wiki: https://modthesims.info/wiki.php?title=Game_Help:World_Routing_Lags (and `/Downloads`); ellacharmed: https://ellacharmed.wordpress.com/2020/08/21/how-to-fix-worlds-in-the-sims3/
- Store patches: https://forums.ea.com/discussions/the-sims-3-technical-issues-en/patch-install-status-as-of-dec-2017--store-patch-download-removed-/1994139 ; G4TW guide copy: https://www.scribd.com/document/366796512/Installation-Guide-pdf
- Local, read-only: `Documents\Electronic Arts\The Sims 3\Options.ini`, `Version.tag`, `Mods\Resource.cfg`, `ContentPatch\`
