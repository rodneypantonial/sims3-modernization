# 153. Routing community mods vs our fixes

Date 2026-10-22. Static only (package contents, EA originals, decompiled code); no game run. Ledger §226.

Question (Rodney): the installer's routing mods (docs/07 §2.4) are in every test run. Do they work with our fixes, where are they redundant or harmful next to ours, and should a player keep them? We never ship or reimplement them; the outcome per mod is **keep**, **keep but note X**, or **drop when using ours**.

Method [verified, all read-only]: each installed `.package` listed with the repo's DBPF reader (`tools/extract-assemblies.ts` `readIndex`/`refpack`); every resource matched by type and instance against the Steam install (`GameData`, `EP*`/`SP*`, and `Game\Bin\Gameplay\GameplayData.package`, which holds the class tuning XML and ITUN) and diffed; the one script assembly decompiled with `ilspycmd` and compared with EA's `SimRoutingComponent` (`game-copy/decompiled`) and with simler90 UPDATE242's replaced `Sims3GameplaySystems.dll` (extracted from the installed package). Collision scan: every type+instance of these mods against all 171 installed packages. `tools/offload-override-scan.ts` run read-only over the install. No mod code is quoted here; behaviour is described in our own words (PLAN.md licensing rule).

## 1. Summary

| Installer id | Package | Contents (type count) | What it changes vs EA | Routing effect | Conflicts with ours | License | Recommendation |
|---|---|---|---|---|---|---|---|
| `houseboat-fix` | `fp.houseboatfix-1.0.0.1a.package` (FoulPlay) | S3SA `fp.houseboatfix.dll` (1), `_XML` MonoPatcher init flag (1), NMAP | `[ReplaceMethod]` on 3 `SimRoutingComponent` methods; only `EnterHouseboatThroughGangway` behaves differently: the land-only planning switch is not set when the router is a Sim swimming in the ocean, wading, or a boat [verified] | Fewer failed gangway plans for Sims/boats that start in water [inferred]; the replaced method is still the S2 houseboat fixup source of docs/151 (IP 35 ms/s), same cost as EA's [verified, docs/151 frames] | **Redundant with simler90 UPDATE242** (installer default `simler90-core`), whose `EnterHouseboatThroughGangway` has the identical condition [verified decompile]. No clash with ts3fixes or ts3native | GitHub repo has no license file (`gh api`: `license` null) = all rights reserved | **drop when using ours** (simler90 covers it; also frees the MonoPatcher dependency if nothing else needs it) |
| `routing-toddler` | `TSS_ToddlerRoutingFix.package` (TheSweetSimmer) | `_XML` class tuning `Sims3.Gameplay.Actors.Sim` (1), NMAP | Whole copy of EA's Sim tuning with 4 values changed: walk-fall chance 40 -> 0, cry chance 50 -> 0, cry minutes 15 -> 0, cry consider time 3 -> 1 [verified diff] | Toddler routes > 5 m no longer get the 40% fall: no soft route interrupt, no idle, no cry that ends the route and fails the interaction [verified code `SimScriptAdaptor`]; fewer route interrupts and re-routes for toddlers [inferred, small] | None: no hook of ours on this path; simler90 leaves the fall code unchanged [verified]; no other installed package overrides Sim tuning [verified collision scan] | MTS, terms not recorded = all rights reserved | **keep but note**: replaces the whole Sim class tuning file, so any later mod that tunes `Sim` conflicts with it (load order decides) |
| `routing-sims-less-space` | 11 `OhRudi__<pack>__Sims_need_less_Space.package` | FTPT (jig footprints) 105, NMAP | First footprint polygon of each jig (social, homework, performer, sparring, beach, etc.) scaled by 0.5 about the jig origin (area x0.25); bounding box and flags unchanged; on multi-area footprints only the first area; slot areas scaled in 6 files [verified, 104 of 105 changed, 1 identical to EA] | Jigs fit into tighter spots, so fewer failed jig placements and failed socials [community, inferred]; may add route searches because more socials succeed and each routes to its slot [inferred]. Our native code reads footprints from the game and has no jig-size constants [verified grep docs/89, docs/98, `footprint.cpp`] | None found statically | MTS 683612, terms not recorded = all rights reserved | **keep but note**: Sims can stand closer to walls/objects than EA intended (clipping); net route cost unmeasured, see job `bp-footprints-off` |
| `routing-pets-less-space` | `OhRudi__Routing Fix__Pets_need_less_space.package` | FTPT 60, NMAP | Same scaling on pet/horse jigs (first area only on the 15 multi-area footprints; slot areas in 3) [verified] | As above for pets and horses [inferred] | None found | MTS 683307 = all rights reserved | **keep but note** (as above) |
| `routing-sloped-terrain` | `Nikel23 - Interact on sloped terrain.package` | OBJD 125, NMAP | One byte in each of 125 jig object definitions 0 -> 2; EA sets the same bit on 77 of 1,170 `DeltaBuild0` OBJDs, all things placed on uneven ground (spawners, plants, seeds, `InsectJig`, chimney foundations), so it is the allow-on-slope placement flag [inferred, strong] | Jig placement accepts sloped terrain: fewer failed outdoor socials on hills [community, inferred] | None found | MTS 529671 = all rights reserved | **keep** |
| `routing-ocean-play` | `GoAndPlayInOceanFix.package` | ITUN `Terrain+GoAndPlayInOcean+Definition` (1), NMAP | Fun is no longer advertised (EA 25 -> 0; actual 0 -> 25), Childish trait bonus removed, winter penalty -200 added [verified diff] | Autonomy picks ocean play only for the beach/resort commodities, not for fun from anywhere: fewer long inland-to-ocean routes (water routing) [inferred] | None: not an `[AutShadow]` allow-listed definition [verified, 0 hits in allow-list/tier-2/closure CSVs]; no scoring function added | MTS 686121 = all rights reserved | **keep** |

No conflict between the mods and our fixes was found. The one real overlap is House Boat Fix vs simler90 (both in the installer default set): the same change twice.

## 2. Compatibility checks [verified unless marked]

- **Resource collisions:** none of the 294 resources above is shipped by any other of the 171 installed packages (type+instance, any group; name maps ignored).
- **ts3fixes hooks** (`mods/ts3fixes/src`: RouteBackoff, RoutableCache, SafeSpotCap, ReplanDefer/Spread, D1 `legreplanskip`, D3 `bouncerskip`, D5 `idlememo`, OverwatchStuck, GoHome, StationsHook, PortalAdaptor, Unseen*): none hooks `EnterHouseboatThroughGangway`/`ExitHouseboatThroughBoat`/`EnterHouseboatThroughBoat`, the toddler fall helper, `GoAndPlayInOcean`, or reads footprint/OBJD data. `RouteSourcesCore` puts frames named `CheckAndUpdateForHouseboats` or `*HouseboatThroughGangway` in the S2 bucket, so House Boat Fix's frames and EA's are counted the same (L39). Trip shortcuts (`Fixes.cs` `HouseboatOk`) skip houseboat lots whatever the mod.
- **simler90 UPDATE242** (replaced `Sims3GameplaySystems`/`Sims3GameplayObjects`): House Boat Fix's MonoPatcher `ReplaceMethod` patches simler90's copy of the methods. simler90's `EnterHouseboatThroughGangway` already has the same swim/wade/boat condition (`simler90Extras.Other`), its other two methods equal EA's, so with both installed the game runs FoulPlay's body with the same behaviour. If simler90 changes these methods again, House Boat Fix would silently undo that. Toddler fall code is EA's in simler90.
- **Native routing assumptions** (docs/89 UpdateSimFootprint, docs/98 in-room quadtree, `footprint.cpp`, read-only clones): all read the game's own footprint entries; no constant tied to an EA jig size. Scaled polygons inside an unchanged bounding box only make the polygon tests pass more often [inferred: insertion by bounding box, cell count unchanged].
- **`[AutShadow]` allow-list** (docs/128, docs/132): `offload-override-scan.ts` lists no type from `fp.houseboatfix` (0 CSV rows); the other mods carry no assemblies; the one ITUN is not on the allow-list and gives no scoring function.
- **A/B baselines:** all six are installed in every harness run, both arms of every pair. Our on/off comparisons stay valid; absolute route counts differ from a player without them. With House Boat Fix dropped nothing changes (simler90 has the same code) [inferred until `ip-houseboatfix-off`]. The footprint mods can move BP crowd route counts either way; `bp-footprints-off` measures it.

## 3. Per mod

### 3.1 House Boat Fix (FoulPlay) — drop when using ours
- Contents: assembly `fp.houseboatfix` (`[Plugin]` class calling MonoPatcher's `PatchAll`), three `ReplaceMethod`s on `SimRoutingComponent`: `ExitHouseboatThroughBoat`, `EnterHouseboatThroughGangway`, `EnterHouseboatThroughBoat`; `_XML` with a MonoPatcher `init` flag.
- Change: in EA's gangway entry, a start point connected to the houseboat by land turns off water and boat planning. The mod keeps them on when the router is a Sim in the ocean (swimming posture in the ocean container, or wading), or a boat. The other two bodies are EA's (option and slot values compared by enum value) [verified].
- Gameplay: Sims swimming or boating to a docked houseboat get a gangway route they can actually follow, instead of a land-only plan from a water start that fails and is retried (the "freeze every few seconds" on the release page) [community + inferred].
- Ours: simler90 UPDATE242 has the same condition. docs/151 S2 (houseboat fixup re-plans, IP 35 ms/s) is EA's fixup design and costs the same with either copy; D2 is still spec only.
- Verify: `ip-houseboatfix-off` vs `ip-mods-on` (§5).

### 3.2 Toddler Routing Fix (TheSweetSimmer) — keep but note
- Contents: one `_XML`, instance `0xA29571C6F77AA18D` (`Sims3.Gameplay.Actors.Sim` class tuning), 716 lines; the other 712 lines equal EA's `GameplayData.package` copy.
- Change and behaviour: EA rolls a 40% chance per toddler walk route over 5 m to switch to the falling walk style and register a callback near the end; the callback soft-stops the route, idles 1-3 sim-minutes and, half the time, starts crying and ends the route (the interaction fails, the queue moves on). With the mod the chance is 0, so none of that happens.
- Routing: fewer route interrupts for toddlers; no new searches. Too rare in the baseline saves to measure; no job.
- Note: it overrides the whole Sim tuning file, not just four values.

### 3.3 Sims need less Space (OhRudi, 11 packs) — keep but note
- Contents per pack: FTPT jig footprints (Ambitions 2, BaseGame 23, Generations 8, IntoTheFuture 10, IslandParadise 16, LateNight 6, Seasons 16, Showtime 11, Supernatural 5, UniversityLife 7, WorldAdventures 1; the Generations pack has one footprint identical to EA).
- Change: polygon coordinates halved about the jig origin (area x0.25), bounding box left at EA size; on the 20 multi-area footprints only the first area. Example: a base-game jig rectangle goes from 0.89 x 1.97 m to 0.44 x 0.98 m.
- Gameplay: socials, homework, dances, fights and performances start in tighter spaces; Sims may overlap furniture or walls more [community, inferred].
- Routing: fewer jig placement failures; possibly more successful socials and so more routes. Sign unknown: `bp-footprints-off`.

### 3.4 Pets need less Space (OhRudi) and Interaction on sloped terrain (Nikel23)
- Pets: the same halving on 60 pet/horse jig footprints (first area only on the 15 jigs with several areas). Keep but note, as 3.3.
- Sloped terrain: 125 jig OBJDs (social jigs, pets, horses, pack jigs) get one flag byte 0 -> 2, at the same offset in every file (130 bytes before the resource-list offset). Keep.

### 3.5 Autonomous Play In The Ocean Fix — keep
- Contents: one ITUN for `Sims3.Gameplay.Core.Terrain+GoAndPlayInOcean+Definition` (EP10 tuning).
- Change: advertised Fun 25 -> 0 (actual Fun gain moves to 25), Childish bonus removed, -200 in winter. The beach/resort commodities (200, locked) are EA's.
- Routing: low-fun Sims anywhere in Isla Paradiso no longer pick a walk or swim to the ocean; ocean play stays where the beach/resort commodities apply [inferred]. Fewer long water routes, the expensive kind in IP (docs/151). Verify: `ip-oceanplay-off`.

## 4. Player-facing text (draft for docs/PLAYERS.md "Other mods"; not applied)

- **House Boat Fix (FoulPlay):** not needed with our install. simler90's Gameplay Core Mod, which our installer puts in, already contains the same houseboat gangway fix. Having both does no harm today, but if simler90 improves that code later, House Boat Fix would undo it. Remove it unless you play without simler90's core mod.
- **Toddler Routing Fix (TheSweetSimmer):** keep. Toddlers stop falling (and crying) mid-walk, so their walks are no longer cut short. It replaces the game's whole Sim tuning file, so do not combine it with another mod that changes Sim tuning.
- **Sims need less Space / Pets need less Space (OhRudi):** keep if you like the result. Social and pet interactions fit into tighter spots, so fewer "can't find room" failures; Sims and pets may stand a bit closer to walls and furniture than the game intended. Works alongside our routing fixes.
- **Interaction on sloped terrain enabler (Nikel23):** keep. Sims can chat and play on gentle hills instead of giving up. Works alongside our fixes.
- **Autonomous Play In The Ocean Fix:** keep (Island Paradise). Sims no longer walk across town to play in the ocean just because they are bored; they still do it at beaches and resorts. Fewer long trips also means less routing work.

## 5. Day verify jobs (not run)

`tools/harness/queues/routing-mods-compat.json` (queue `fixes.uninstall` takes installer ids; all six are recorded in the install-fixes state, so uninstall/reinstall per job works). Release defaults, ts3fixes `routesrc` on (observe only), 5 min, seed 12345, counters only (noisy machine OK). Mods load at game start, so each comparison is two jobs, not `--ab`.

| Job | Arm | Compare with | Counters that show it |
|---|---|---|---|
| `ip-mods-on` | all mods | - | `routesrc_buckets` per source, `routesrc_houseboat_legs`, route plans/fails, watchdog resets 46-49 |
| `ip-houseboatfix-off` | without `houseboat-fix` | `ip-mods-on` | S2 plans/fails and legs within noise = redundant (expected); S2 fails or stuck Sims up = keep it |
| `ip-oceanplay-off` | without `routing-ocean-play` | `ip-mods-on` | more plans/us in `other`, more water routes = the mod saves route work |
| `bp-mods-on` | all mods, `rare-big-crowd` | - | as `ip-mods-on` plus RouteBackoff counters |
| `bp-footprints-off` | without `routing-sims-less-space`, `routing-pets-less-space`, `routing-sloped-terrain` | `bp-mods-on` | fails up = the mods help; plans up with the mods on and fails flat = they add searches (note it) |

Wall clock: 5 jobs x ~12 min = ~1 h, one day slot.

## 6. Day job results (2026-10-22, chain117, 5 of 5 PASS, ledger §248)

Single 5 min loads, seed 12345, every job noise `high` (other apps 42% of game CPUs on the last): indicative only. From `probe-out/profile/routing-mods-compat-2026-10-22-*-300-seconds.csv`, `-300-hitches.jsonl`, `-stuck.json`, `-houseboats.json`. `routesrc` per-source counters and houseboat legs were not captured in the job rows, so S2/houseboat share is not compared.

| Job | Plans (per s) | Fails (%) | Sim-thread route ms/s | Hitches (>= 300 ms) | Stuck Sims | Houseboats moved |
|---|---|---|---|---|---|---|
| `ip-mods-on` | 1,460 (5.0) | 192 (13.2%) | 101 | 825 (22) | 0 | 0/10 |
| `ip-houseboatfix-off` | 1,309 (4.6) | 185 (14.1%) | 112 | 941 (35) | 0 | 0/10 |
| `ip-oceanplay-off` | 1,324 (4.6) | 162 (12.2%) | 123 | 940 (45) | 0 | 0/10 |
| `bp-mods-on` | 4,426 (15.2) | 161 (3.6%) | 61 | 1,483 (5) | 0 | - |
| `bp-footprints-off` | 4,197 (14.4) | 133 (3.2%) | 52 | 1,612 (5) | 1 (library visit, not routing) | - |

Verdict:
- **No mod is clearly worse with ours.** Every mods-on arm has the same or fewer hitches and the same or lower route ms/s than its off arm in IP; BP differences are within ~15%, inside the noise of one load.
- **House Boat Fix default-off (2026-10-22): no regression seen.** Without it: 0 stuck Sims, fail rate 14.1% vs 13.2%, plans and route time within noise, no boat moved in either arm. Decision stands (simler90 covers it). Not proven for the gangway path itself: no S2 counters.
- **Ocean-play fix:** off arm has more route ms/s (123 vs 101) and hitches but fewer plans and fails: no support for "saves route work" beyond noise; keep, nothing to warn about.
- **Footprint mods (less-space x2, sloped terrain):** off arm has 5% fewer plans, 0.4 points fewer fails, one stuck Sim (visiting a library, unrelated). No sign they add failed placements; no player warning needed. The screenshot check for Sims inside walls was not done.
- Repeat a pair on a clean night if a decision depends on it (IP differences are within 20%).

## 7. Sources
- Packages: `<Documents>\Electronic Arts\The Sims 3\Mods\Packages` (read-only); EA originals: Steam install `GameData\Shared\Packages\FullBuild0/DeltaBuild*`, `SP*\...\DeltaBuild_p04`, `Game\Bin\Gameplay\GameplayData.package`.
- Code: `game-copy/decompiled/Sims3GameplaySystems/Sims3.Gameplay.Actors/SimRoutingComponent.cs` L2380-2700, `Sims3.Gameplay.Routing/SimScriptAdaptor.cs` L20-47 and L195-212, `RouteInterruptAction.cs`; `SimIFace/Sims3.SimIFace/Route.cs` (route option values), `Slot.cs`, `PathType.cs`.
- docs/07 §2.4 (installer ids, MTS pages), docs/128 (House Boat Fix compatible with `[AutShadow]`), docs/132, docs/151 (S2 houseboat fixup cost).
