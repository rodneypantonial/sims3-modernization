# Changelog

Player-facing changes per release. Each `## vX.Y.Z` section becomes the GitHub release notes for that tag (`.github/workflows/build.yml`).

## v0.1.4 (2026-10-20)

Background route planning: Sims plan their walks and drives on a second CPU core while the game keeps running.

### Download

- `ts3native-v0.1.4.zip`: put `ts3native.asi` and `ts3native.ini` in `The Sims 3\Game\Bin\` (replace the v0.1.3 files; **use the new `ts3native.ini`**, the old one keeps background routing off). Needs an ASI loader, as before. Built as ts3native 0.36.0.
- `ts3fixes.package`: put it in `Documents\Electronic Arts\The Sims 3\Mods\Packages\` (replace the v0.1.3 file). Background routing needs this package: it is what hands a Sim's route plan to `ts3native`. Its settings are the same as v0.1.3 (no default changed).

### New in `ts3native` (on by default)

- **Route planning in the background (`[RouteAsync]`, with `[RouteGate]` `Sites=1` `Enforce=1` and `[Discourage]`).** When a Sim plans a route from its own task, the game's own planner now runs on a worker thread while the simulation goes on; the Sim waits tick by tick for its answer. Routing locks keep the main thread off the routing data while a plan runs (`[RouteGate]`), and discourage-area changes made during a plan are applied in order right after it (`[Discourage]`, design D with the D-X destroy and dead-object fixes). Measured on Bridgeport, same build, seeded 10-minute runs, async on vs off: simulation time spent on routing **119 -> 50 ms/s**, **+10-12% simulation iterations/s**, **-17% hitches**; the worst hitches (p99) unchanged within noise (PLAN.md "Async routing (H6) state"; runs: `docs/data/results/2026-10-16-queue-h6-m1.md`, `docs/data/results/2026-10-17-queue-h6-m1.md`; realistic test environment env-off/env-on pairs for Bridgeport and Isla Paradiso: `docs/data/results/2026-10-17-queue-clean-night.md`, build 50bb1d4, jobs `bp-env-off`, `bp-env-on`, `ip-env-on`, `ip-env-off`, all PASS). Replans (`[RouteAsync] Kinds`) stay on the main thread. To turn it off: `[RouteAsync] Enabled=0`.
- **Already on since earlier builds, now listed:** `[LotRemoveGuard]` (flushes a lot's deferred footprint removals before the game walks them; prevents a crash at `0x63576A`) and `[OptsInit]` (the route planner's search options start at 0 instead of stale stack values; fixes an uninitialised read in the game).

### Shipped but off (switches for testers)

- **Routing snapshot (`[RouteSnap]`, P3/P4 copy, P5 / P5b / P5c answers).** The code is in the build; `ts3native.ini` keeps `Enabled=0`, `Mode=0`, `Lots=none`. It is not part of the measured gains above and has not passed the release checks; turn it on only to help test, with backups.
- Every other experimental section stays off as in v0.1.3 and the list below: `[ThreadStack]`, `[AutShadow]`, `[MonoThread]`, `[WorldImage]`, `[SteerRO]`, `[SlotEnabledRO]`, `[LotFrontDoorRO]`, `[ObstructedRO]`, `[RouteCache]`, `[RouteWorker]`, `[RouteBudget]`, `[Alloc]` and the rest.

### Tests (release validation, `tools/harness/queues/release-validate.json`)

Results: `docs/data/results/2026-10-20-queue-release-validate.md`. All jobs on the release build (sha256 in `docs/RELEASE.md`) with the release `ts3native.ini`, no per-job overrides; the save matrix runs the fixes as shipped (`--fixes shipped`).

| Group | Jobs | Cases | Result |
|---|---|---|---|
| Play, crash-free (12 min seeded) | 3 | Bridgeport, Isla Paradiso, Sunset Valley | 3/3 PASS (BP 16,608 background plans, 0 watchdogs, 0 stuck Sims) |
| Saves | 3 | Bridgeport, Sunset Valley, Isla Paradiso: 15 saves each with background routing on | 45/45 saves written (result 0); 3/3 reloads PASS (one save per world made unpaused with background routing on, loaded and played 3 min: `docs/data/results/2026-10-20-queue-release-reload.md`). No save code changed since v0.1.3, so the full save matrix was not rerun |
| Travel round trips + travel save | 6 | France, China, Egypt, University, Future (queued and direct start) | 4/6 PASS; Future: both trips arrived without a crash, the harness could not queue the trip home (known harness gap, not a game fault) |
| Rare routing scenarios (docs/117) | 21 | winter pond, thunderstorm, full moon werewolf, route bits, ghost walk, occult walks, death walk, big crowd, IP water, world walk (University, France), owned vehicles, stroller, horse ride, zombies, broom flight | 20/21 PASS, 0 crashes; stroller: the harness could not create a stroller object (setup step, not a game fault) |

33 jobs run: 27 PASS, 0 crashes. Failures are harness-side only (Future trip home x2, stroller setup) plus the 3 earlier save-matrix jobs that could not switch experimental save fixes (replaced by the saves row).

### Recommended setting (optional)

- **NRaas Overwatch: turn off "Clean up Inventory".** Once per game day Overwatch's nightly cleanup looks up the name of every inventory item and the game freezes for about half a second (docs/96). In game: City Hall > NRaas > Overwatch > Settings > Clean up Inventory = off. For new saves the installer entry `overwatch-no-invcleanup` sets it. Trade-off: broken or orphaned inventory items are no longer removed. Not yet measured; see queue job `bp-overwatch-cleanup-ab`.

### New in `ts3fixes` (experimental, off by default)

- **SafeSpot placement reuse (`kSafeSpotReuse`, switch `safespotreuse`).** A townie placed by a trip shortcut at a spot already verified on that lot no longer runs another native placement search (`FindGoodLocation`, up to five per child at the Isla Paradiso school run); it gets the spot and facing stored from the first search while the spot's cached routability verdict holds. Experimental: off in the tuning; not tested in a live game yet and no gain is claimed (queue job `ip-safespot-ab`).
- **Spring wildflowers without the hitch (`kWildflowerTask`, switch `wildflowertask`).** With Seasons, each spring the game grows wildflowers on up to ~32 lots, one lot per sim hour, and tries up to 20 spots per lot inside one alarm call: 0.3-1 s freezes (docs/110). With this on the tries run one per sim tick in their own task: same lots, flowers, try limit and outcome; a flower may appear a fraction of a second later. A save while a try is pending drops that one flower. A measure mode (`wildflowerprobe`) logs tries and times per fire without changing anything. Experimental: off in the tuning; not tested in a live game yet and no gain is claimed (queue `wildflower.json`).
- **Door lock table for background routing (`kDoorMirror`, switch `doormirror`).** Groundwork for answering door locks without asking the game's scripts during a route search (docs/105 §10.5): ts3fixes keeps a per-lot table of every door's lock settings and marks a lot out of date when a door's lock is imported (placing a lot from the library or bin). At each route plan, the start and goal lots' tables are sent to ts3native's routing snapshot when its copy is out of date. ts3native does not use them in route searches yet, so this changes nothing in play. Off by default.

### New in `ts3native` (experimental, off by default)

- **Routing snapshot shadow, part A (`[RouteSnap] Mode=1`, test builds only).** Step P4 of the planned routing snapshot (docs/94 §18): after a background route job finishes, the route thread can plan the same request again on a private copy of the routing data and compare the two answers; the game's own answer is always the one used. Since 0.29.0 the copy is the routing snapshot lot copy below (`Lots=replay`), brought to the moment each background plan starts (docs/94 §21); since the private pools build below the copy no longer shares the game's memory pools. Off in `ts3native.ini` (`Mode=0`).
- **Private memory pools for the routing snapshot (`[RouteSnap] Lots=replay`, test builds only).** The game's routing quadtree and map memory pools have no lock, and many game paths use them outside the routing lock (docs/94 §16.6). The routing snapshot lot copy and the shadow plans on it now take their quadtree, map, A\*, goal and cell nodes from pools of their own, so they can no longer corrupt the game's pools or keep using memory the game frees when it resets its route planner (docs/94 §16.7). The same build fixes the order of lots in the copy's lot table after a lot is re-copied. Earlier `Lots=replay` and `Mode=1` test builds should not be run any more. Since 0.31.5 a shadow plan that frees a node of the game's pools, or a thread that frees a node of the copy's A\* pools without owning it, leaks that node instead of returning it to a pool another thread uses at the same moment (docs/67 §8). Off in `ts3native.ini`.
- **Background route plans that check line of sight stay on the main simulation thread (`[RouteAsync]`).** Plans that ask for physics-based line of sight change which floor of a lot is shown while they run; with background routing on, that could leave the wrong floor showing after a floor change. Those plans are no longer moved to the background (docs/94 §17.9).
- **Routing snapshot lot copy (`[RouteSnap] Lots=replay`, test builds only).** 0.29.0: no longer fails on one kind of object footprint (the copy used to be rebuilt and fail every quarter second), leaves a lot out instead of giving up when it meets an unknown footprint kind, and copies the snapshot's terrain cost maps in larger pieces again (cheaper). Step P3-B of the planned routing snapshot (docs/94 §16): keeps a private copy of every lot's routing footprints and quadtrees up to date by replaying the game's own changes in order on a background thread, and checks it against the game's data; optional `Prebuild=1` builds every lot's routing graph on the loading screen. Nothing reads the copy yet and it is off in `ts3native.ini`. The same build fixes three findings of the world-image test: the tuning value set by the game during play, a full key table that made Isla Paradiso's cost-map updates expensive, and smaller cost-map tiles.
- **Routing snapshot world images (`[RouteSnap] Enabled`, test builds only).** Step P3 of the planned routing snapshot (docs/94 §14): keeps an up-to-date copy of the game's world-level routing data (waypoint graph, water and cost grids, footprint grid, terrain mask, tuning) and checks it against the game's own data. Nothing reads the copy yet, so play does not change. Off in `ts3native.ini` (`Enabled=0`).
- **Routing change counters (`[RouteSnap] Count`, test builds only).** A measurement switch for the planned routing snapshot (docs/94 phase P1): it counts every write to the game's routing data and what caused it, and changes nothing in play. Off in `ts3native.ini` (`Count=0`); only test runs turn it on. The same switch now also names the remaining causes (furniture placement, platforms, trees, terrain lookups, deferred updates) and measures how many changes per second the planned snapshot would have to copy and what that would cost (docs/94 §15).
- **Read-only steering (`[SteerRO]`).** While a background route job holds the routing lock, walking Sims can check their steering against a read-only copy instead of waiting for the job. Experimental: switched off in `ts3native.ini` (`Enabled=0`); no speed gain is claimed yet, and the first live test was correct but slower.
- **Background replans (`[RouteAsync] Kinds`).** Sims' route replans (Replan, PlanFromPoint, ReplanFromPoint) can be planned on the background route thread like their first plans, while the game keeps running. Experimental: switched off in `ts3native.ini` (`Kinds=0`), needs the matching ts3fixes; not tested in a live game yet and no gain is claimed.
- **Read-only lot front door (`[LotFrontDoorRO]`).** The lookup of a lot's front door can be answered without waiting for a background route job. Experimental: switched off in `ts3native.ini` (`Enabled=0`); not tested in a live game yet and no gain is claimed.
- **Read-only "is a Sim standing here" check (`[ObstructedRO]`).** After a route, the check for Sims standing on the goal can be answered without waiting for a background route job. Experimental: switched off in `ts3native.ini` (`Enabled=0`); not tested in a live game yet and no gain is claimed.
- **Read-only slot position (`[SlotEnabledRO]`).** When a Sim asks for a routing slot's position while a background route job runs, the lookup runs under a shared lock instead of waiting for the job. Experimental: switched off in `ts3native.ini` (`Enabled=0`); not tested in a live game yet and no gain is claimed.

## v0.1.3 (2026-10-16)

Hotfix: the v0.1.2 speed-ups were shipped switched off.

### Download

- `ts3fixes.package`: put it in `Documents\Electronic Arts\The Sims 3\Mods\Packages\` (replace the v0.1.2 file). **Please update if you installed v0.1.2.**
- `ts3native-v0.1.3.zip`: same as v0.1.2 (no changes); keep your v0.1.2 files or replace them.

### Fixed

- **Faster saving and the lighter Sim panel are now really on.** In v0.1.2 the package's settings file still listed these four fixes as off, and the game uses that file over the built-in defaults, so the v0.1.2 package ran with them off. v0.1.3 ships the settings file with `kFastRefTable`, `kMotiveBarImageCache`, `kBuffGridInPlace` and `kSimNameCache` on. Nothing else changed; the tests listed under v0.1.2 apply.

## v0.1.2 (2026-10-16)

Faster saves and a lighter Sim panel.

### Download

- `ts3fixes.package`: put it in `Documents\Electronic Arts\The Sims 3\Mods\Packages\` (replace the v0.1.1 file).
- `ts3native-v0.1.2.zip`: put `ts3native.asi` and `ts3native.ini` in `The Sims 3\Game\Bin\` (replace the v0.1.1 files). Needs an ASI loader, as before. The new native save options below are off unless you switch them on in `ts3native.ini`.

### New in `ts3fixes.package` (on by default)

- **Lighter Sim panel.** The needs bars, the moodlet grid and Sim names in the panel no longer rebuild from scratch every time a need changes: the bars keep their pictures while the moodlet is the same, the moodlet grid updates its cells in place, and names come from a small cache. Only the panel changes; nothing in the game itself or in your save. Tested in one load per town with the fixes switching on and off every 3 minutes: Bridgeport PASS (30.2 vs 27.5 game updates per second and 4.1 vs 5.1 hitches per second with the fixes on vs off; one run, so treat it as a hint, not a measurement). Isla Paradiso PASS (44.0 vs 43.0 updates per second, 2.5 hitches per second either way).
- **Faster saving.** The game keeps a list of every object it has already written into the save, and in big towns that list got slow past about 16,000 objects. It now stays fast. The save file comes out the same (script data byte-identical with the fix on or off). Saves: Bridgeport 12.7 -> 10.3 s (-19%); Sunset Valley -17% to -24% over 3 paired saves (11.5 -> 8.7 s, 11.3 -> 9.3 s, 10.7 -> 8.8 s); Isla Paradiso -14% and -23% over 2 pairs (11.8 -> 10.1 s, 12.8 -> 9.8 s). One Isla Paradiso all-off vs all-on pair came out equal (11.0 vs 11.1 s): single saves there vary by up to 2 s, so treat the per-world numbers as ranges. It passed every save test below.

### New in `ts3native` (off, opt-in)

- **Faster lot footprint lookup when saving (`[FootprintIndex]`, off).** Saving a lot no longer scans every footprint in the town to find its owner. Same save result (checked with the built-in verify: 330 levels, 82 lots, 0 fallbacks). Reason for off: the gain (about 0.4 s, estimated) does not show above normal save-to-save variation, so it is not worth a new default yet.
- **Save writes to disk later (`[SaveFlush]`, needs `[SaveWrite]`, off).** The game waits less for the disk at the end of a save. Fixed during testing: a flush could fail on `Meta.data` when another program held the file for a moment (error 32); it now retries. Reason for off: no gain on an NVMe SSD; it should help slow or spinning disks (not measured), so it is for players who want to try it.
- **`[SaveCompress]` (stays off).** Measurement only: it reports compression time per save (0.85-0.9 s in Bridgeport) and changes nothing. Compressing in parallel is not possible without changing the save layout.

All four on together: Bridgeport save 13.1 -> 10.1 s (-23%).

### Save tests

Each world: one paused load, the fixes switched at runtime, every save compared resource by resource with the fixes off (`tools/harness/save-matrix.ts`). Cases: control, identical (all off vs all on), per-fix, overwrite, back-to-back, unpaused, build mode, verify counters, reload.

| World / case | Date | Result |
|---|---|---|
| Bridgeport, 9 cases | 2026-10-14 | 8/9 PASS (build mode: the game refuses to save there, so nothing to compare; reload: off and on saves both load with 0 script errors). Verify counters failed on a wrong check in the test tool, fixed since and passed in the worlds below |
| Sunset Valley, 9 cases | 2026-10-15 | 9/9 PASS (after the `Meta.data` retry fix; the run before it had 1 flush fail per save) |
| Isla Paradiso, 9 cases | 2026-10-15 | 9/9 PASS (one per-fix run failed because the town changed between saves while paused, with the off save differing too; rerun PASS) |
| Vacation travel (Isla Paradiso -> France -> home) | 2026-10-15 | PASS |
| University and future travel | 2026-10-16 | **Open.** Our test tool could not start these trips yet (the Sim dropped the queued trip before leaving), so no save ran; the save code was never reached. Vacation travel uses the same travel save and passed. A direct-trip test is in progress for v0.1.3. |

28 cases run: 27 PASS, 1 test-tool fail (fixed). University and future travel open (see above).

## v0.1.1 (2026-10-14)

Two more `ts3native` fixes switched on after in-game testing. `ts3fixes.package` is unchanged from v0.1.0.

### Download

- `ts3native-v0.1.1.zip`: put `ts3native.asi` and `ts3native.ini` in `The Sims 3\Game\Bin\` (replace the v0.1.0 files). Needs an ASI loader, as before.
- `ts3fixes.package`: same as v0.1.0; keep yours or replace it.

### New in `ts3native` (on by default)

- **Faster unpacking of game files.** The game unpacks compressed textures, objects and other data all the time while loading and playing. A faster unpacker gives exactly the same results (checked against the game's own unpacker on every file of a full Bridgeport load: 0 differences; about 144,000 files unpacked over 4 test sessions without a problem). The gain is small: less work on load screens and when new items stream in.
- **Saving freezes the game a little less.** When you save over an existing save, the game first makes a backup copy of the old one. That copy now happens in the background while the game writes the new save, instead of the game stopping to do it. Saves and backups come out the same and load normally.

### Tested in game, no change needed

- The swimming and houseboat leftover fix from v0.1.0: confirmed working (with the fix, leftover route data is cleaned up; without it, it piles up).
- The smoother NRaas Overwatch nightly check from v0.1.0: confirmed working.

### Still being tested (off, not part of this release)

Faster Create-a-Sim hair lists, a faster memory allocator, a route cache and a few smaller caches. They did not show a clear win yet, so they stay off.

### Where these fixes come from

Same as v0.1.0: all code in this release is our own, nothing copied from other mods, no EA files. S3SS also speeds up the same unpacking step with its own decoder; ours is separate code and leaves S3SS's patch in place.

## v0.1.0 (2026-10-14)

First release: our two fixes for The Sims 3 (Steam 1.67, all expansions). Less stutter, more game time per real second, fewer stuck Sims.

### Download

- `ts3fixes.package`: the script mod with the stutter fixes. Put it in `Documents\Electronic Arts\The Sims 3\Mods\Packages\` (needs the usual `Mods\Resource.cfg`; script mods enabled in the game options).
- `ts3native-v0.1.0.zip`: the native plugin. Put `ts3native.asi` and `ts3native.ini` in `The Sims 3\Game\Bin\`. Needs an ASI loader (for example Ultimate ASI Loader as `wininet.dll`).

Close the game before installing. To undo, delete the files again.

### What you will notice

Measured over 10 minutes at speed 3 on the same saves, with and without `ts3fixes`:

| World | Game time per real second | Stutters (frames of 50 ms or more) |
|---|---|---|
| Bridgeport | +61% | -39% |
| Sunset Valley | +85% | -47% |
| Isla Paradiso | +133% | -51% |

### Fixes in `ts3fixes.package` (on by default)

- **Townies you can't see skip the walk.** A townie going home, to work, to school, to a shop or to a park, far from the camera and from your Sims, arrives without walking or driving the whole way. The game still runs the last few steps and the arrival, so what the Sim does next is unchanged. Your household and anyone you can see always walk.
- **No pointless route retries.** An unseen townie whose route failed waits 30 sim-minutes before trying the same trip again, skips the route-fail animation nobody sees, and keeps its planned route instead of re-rolling for a subway or hover train detour.
- **Homeless townies leave quietly.** Unseen townies leaving town fade out straight away instead of first walking to a random lot.
- **Stuck Sims get unstuck.** A Sim frozen in place, blocked in a doorway, trapped on a spot it can't walk off, in a hidden room, or failing its routes over and over gets the game's own reset or is sent home. At most once or twice per Sim per day.
- **Smoother nights with NRaas Overwatch.** Overwatch's nightly stuck check is spread over several moments instead of one ~300 ms freeze.
- **No more leftovers from swimming and houseboats.** A Sim swimming out to sea or boarding a houseboat no longer leaves dead route data behind, which piled up in Isla Paradiso saves.
- **Cheaper background work.** Music, fires and smells far from the camera check for nearby Sims less often, and walk styles are looked up faster.

This release contains only these stable fixes. Features still in testing stay switched off.

### Fixes in `ts3native` (on by default)

- **Fewer memory clean-up pauses.** The game's script memory starts bigger and grows in larger steps, so it pauses to clean up far less often (128 forced clean-ups down to 1 in 6 minutes in Bridgeport).
- **Faster boat and swimming routes.** Route searches over water use a faster version of the game's own search: about 4x faster in an offline test, with exactly the same results (5.4 million in-game searches in Isla Paradiso compared with the game's own code: 0 differences).

Only these two are switched on in the shipped `ts3native.ini`; everything else in it stays off until it is verified in game.

### Where these fixes come from

- **All code in this release is our own.** It was written from scratch in this repo. No code from another mod was copied, ported or repackaged, and no EA game files or decompiled EA code are included.
- **Other mods are not part of this release.** Community fixes (DXVK, Sims 3 Settings Setter, NRaas, the Smooth Patch, and others) stay with their authors: get them from their own pages. The repo's installer sets them up only so developers and testers get the same test setup we measure with. `docs/01-existing-fixes-comparison.md` lists them with credits.
- **New, as far as we know:** the unseen-townie shortcuts, route retry backoff, the houseboat and swimming leak fix, the background-work throttles, and the water route speed-up. We know of no other mod that does these.
- **Our own version of a known idea:** the stuck-Sim checks do a job NRaas Overwatch's Stuck Check also does (we studied what it does and wrote our own code), and the memory setting builds on a known Boehm GC heap tweak. These are credited, not claimed as new.
