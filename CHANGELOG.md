# Changelog

Player-facing changes per release. Each `## vX.Y.Z` section becomes the GitHub release notes for that tag (`.github/workflows/build.yml`).

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
