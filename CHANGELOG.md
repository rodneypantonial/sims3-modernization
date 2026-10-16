# Changelog

Player-facing changes per release. Each `## vX.Y.Z` section becomes the GitHub release notes for that tag (`.github/workflows/build.yml`).

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
