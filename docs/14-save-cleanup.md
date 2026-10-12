# 14 Save cleanup (opt-in): weak NPC relationships and stray MiniSims

Implements ranks 1 and 2 of `docs/09-save-bloat-research.md` §4b as an **opt-in** part of `mods/ts3fixes` (`SaveCleanup.cs`, tunable `kSaveCleanup`, default **off**). Also records what resource type `0x342779A7` is (the biggest grower in the Isla Paradiso save-stats run, PLAN item 6).

Markers: **[verified]** read in decompiled code / the exe / a save file; **[community]** source URL; **[inferred]** not yet measured.

Paths are relative to `game-copy/decompiled/Sims3GameplaySystems/` unless they say otherwise.

## 1. Resource type 0x342779A7: the CAS SimService state (native Sim outfit/build cache)

- **Not referenced by any script code** (grep of all decompiled assemblies for `342779a7` / `875002279`: no hit) [verified].
- **Referenced by the native CAS SimService in TS3W.exe**: the constant appears in `FUN_005ed3e0`, `FUN_005ef7f0`, `FUN_005efb50`, `FUN_005efd60` (`game-copy/analysis/functions.csv`); strings used in the same code: `CAS/SimService`, `CAS/SimService/BuildState`, `CAS/SimService/ILoadSave_Load_Outfit`, `CAS/SimService/CachedBuilds`, `CAS/SimService/CachedBuildListCustom`, `CAS/SimService/OutfitCompositorInventory`, `CAS/SimService/OutfitCompositorsCrossReferenced` [verified: byte search for `A7 79 27 34` in `game-copy/bin/TS3W.exe`, then the string references in those functions]. A fifth hit at `0xAAB2FB` is a type switch elsewhere.
- **Content** (scratch script `peek-type.ts`, read-only on `Baseline-IP*.sims3`): every resource starts `08 00 00 00` (a version). Instances fall in three streams: `0x10001` (one blob of ids/hashes), `0x20001` (one blob, mostly floats), `0x3xxxx` (chunks of ~860 KB uncompressed holding CAS material parameter lists: `CasSkinOverlayTintable`, `HairUniversal`, `Face Overlay`, `Logo 3 Texture`, `Materials\Miscellaneous\solidColor_1`, ...) [verified]. Chunk `0x3000N` count grows as the stream grows; the last chunk is partial. So "one new resource every ~2 sim days" is one stream crossing another chunk boundary, not a new object.
- Fresh -> day 7 -> day 14 (Isla Paradiso): `0x10001` 955 KB -> 1.50 MB -> 1.69 MB, `0x20001` 1.18 -> 2.12 -> 2.33 MB, `0x3xxxx` 7 -> 12 -> 13 chunks (uncompressed) [verified]. Every chunk's bytes change between saves: the service rewrites the whole stream on each save [verified].
- modthesims' type table lists `0x342779A7` without a tag or description, found in "Sunset Valley.world" and "Saved game" (https://modthesims.info/wiki.php?title=Sims_3:PackedFileTypes) [community]. The same table gives `0x05ED1226` = REFS "Reference Store", `0x06B981ED` = OBJS "Script state dump", `0x0580A2CE` = SNAP "PNG Image Small Image of Sims" [community].
- **What it is** [inferred from the strings and content]: the native SimService's saved cache of built Sims: outfit compositor and material parameters (and their keys and morph data) for every Sim and outfit it has built. It grows with the number of distinct Sims and outfits the world has built (townies, tourists, new outfits).
- **Trim?** No, not directly. It is native state rewritten on every save and has no script API. Deleting it offline risks a failed load or Sims rebuilt with wrong outfits, and was not tested. Fewer SimDescriptions and outfits (StoryProgression caps, `docs/09` §4a.6) should shrink it [inferred]. Test that by comparing its size with `sims=` from `SaveCleanupCounts` across checkpoints. Our relationship and MiniSim cleanup is not expected to change it.
- `0x05ED1226` (REFS, Reference Store, +0.24 MB): the world's reference store. Also native, not trimmed.

## 2. What the cleanup removes

Runs at 2:00 sim time every day (`AlarmManager.Global.AddAlarmDay`, `AlarmType.NeverPersisted`, so the alarm is never saved). That is after the game's own relationship decay (`SocialComponent.cs` line 2381, 0:00) and daily genealogy purge (`Autonomy.cs` line 677, about 0:06), and before Overwatch's nightly run (3:00 or 5:00). On demand: `Ts3Fixes.Fixes.RunSaveCleanupOnce()` (returns the summary line).

Whole run skipped (counter [43]) when: no world loaded or the game is shutting down, `GameStates.IsTravelling` or `GameStates.IsOnVacation` (`GameStates.cs` line 537; vacation, University, Into the Future), a save is in progress (`OptionsModel.SaveGameInProgress`, `Sims3.Gameplay.UI/OptionsModel.cs` line 155), or there is no active household. Script tasks are cooperative, so a running alarm callback cannot overlap a save either way.

### 2a. Weak NPC to NPC relationships (counter [40])

Why: `Relationship.DecayAll()` (`Sims3.Gameplay.Socializing/Relationship.cs` line 1142) drops a pair once liking has been 0 for `NumDaysAfterWhichZeroRelationshipIsDropped` days and `CanRelationshipDecayAway()` (line 1263) says yes (Stranger/Acquaintance, different households, not blood related, no working relation). For NPC pairs the decay step is multiplied by `kNPCRelationshipDecayMultiplier = 0.001` (`SocialComponent.cs` line 212), so their liking never reaches 0 and the pair stays forever [verified].

A pair is removed only when **all** of these hold (both Sims checked):
- valid description, alive, has a household, and that household is not the active one; not selectable; not travelling (`GameStates.IsIdTravelling`).
- no Long Distance Friend trait, not in boarding school (the same exemptions as `DecayAll`).
- `LTR.CurrentLTR` is Stranger or Acquaintance (so never Friend and up, Disliked/Enemy, Romantic Interest, Ex, ExSpouse, Partner, Fiancee or Spouse).
- `|liking| <= 10`, inside the band where Stranger/Acquaintance already live.
- never romantic: `!AreRomantic() && !WasRomantic()` (no romance visibility state at all).
- not in the same household, neither is the other's `Partner`, not spouses in genealogy, not `IsSufficientlyRelatedToRuleOutRomance` either way (blood, step or future descendant; `Genealogy.cs` line 821), not parent-in-law or sibling-in-law.
- `!Occupation.HaveWorkingRelation(a, b)` (coworkers, boss, teacher, and so on).
- state held at least `SocialComponent.MinHoursToHoldRelationshipState` (24 h), and not talked for 2 sim days (`WhenLastTalked`, set by every social through `UpdateSTCAndLTR`, line 968).

Removal uses the game's own `Relationship.RemoveRelationship(r)` (line 1591), which drops both directions. That is what `DecayAll` does inline for its own drops (end of `DecayAll`). Candidates are collected first and removed after the loop; each one is re-checked to be present in both directions so a missing reverse entry cannot throw.

Why it is safe: it is the game's own decay rule (`CanRelationshipDecayAway`), applied as if NPC decay had run at player speed, with extra guards (romance, partner, step, in-law, quiet days). A pair that is needed again is recreated on the next social through `Relationship.Get(..., createIfNone: true)` at Stranger level [verified, `Relationship.cs` line 1308]. The only thing lost is a few points of liking between two NPCs who are near strangers.

### 2b. MiniSimDescriptions from other worlds (counter [41])

Why: `MiniSimDescription.sMiniSims` (`Sims3.Gameplay.CAS/MiniSimDescription.cs` line 85) gets every Sim of every world visited (`AddMiniSims`, line 546) and is pruned only by the genealogy purge and pet/urnstone cleanups (docs/09 §1) [verified]. Overwatch removes only MSDs with no links at all (docs/09 §2.1).

An MSD is removed only when **all** of these hold:
- `HomeWorld` is not the current world, and the MSD is not `Instantiated`.
- its id is not a SimDescription in this world (`SimDescription.GetSimDescriptionsInWorld()`: households plus urnstones), not an active-household member, not in `GameStates.TravelerIds`.
- `ProtectionFlags == None` (no opportunity or occupation protection), `PartnerId == 0`, not a playable ghost.
- no genealogy links: no spouse, parents, children or siblings.
- not in a household with any kept Sim, and no mini relationship to any kept id. Kept ids are this world's Sims, the active household, and every Sim that an active-household member's own MSD has a mini relationship with, which covers both directions.
- every mini relationship it has is Stranger, Acquaintance or Undefined, with `RomanceState == None`.

Removal uses the game's own `MiniSimDescription.RemoveMSD(id)` (line 595): it removes the reverse mini relationship from every other MSD, clears its own, nulls its genealogy and drops it from `sMiniSims`. `sMiniSims` is private, so it is read through reflection; it is never written directly.

Why it is safe: the removed Sims have nothing that points at them except plain stranger or acquaintance entries between other-world Sims. On the next trip to their world, `AddMiniSims` rebuilds an MSD for every Sim there that still exists [verified, line 546]. A removed Sim does not vanish from its world: that world's own save (`.nhd` per world) still holds its full SimDescription [inferred from docs/09 §1 "Visited worlds"; confirm with a trip].

## 3. What it never touches (docs/09 §4c)

- Anything in, or linked to, the active household: relationships, MSDs of members and of everyone a member's MSD knows, genealogy.
- Family: blood, step, in-law, spouse, partner, future descendants; any MSD with any genealogy link.
- Friend, romantic, ex, enemy and disliked relationships; any relationship with a working relation; any relationship talked to in the last 2 days.
- Dead Sims and playable ghosts, urnstones, households, SimDescriptions, inventories, memories, photos and other images. It never disposes a SimDescription.
- MSDs with protection flags (opportunities, occupation jobs), partners or household ties; travellers.
- Nothing runs while travelling, on vacation or saving.

## 4. Wiring into the mod (main session)

1. Copy `SaveCleanup.cs` into `mods/ts3fixes/src/`. Apply the two-line `Instantiator.cs` change from the scratch copy: tunable `[Tunable] public static bool kSaveCleanup = false;`, and `Fixes.StartSaveCleanup()` in the world-load handler next to `StartWatchdog()`.
2. `tools/setup/build-fixes.ts` TUNING_XML: add `<kSaveCleanup value="False" />`. In `mods/ts3fixes/README.md`, add a row (off: once a sim day at 2:00, drops weak NPC-NPC relationships and unlinked other-world MiniSims).
3. Test agent `PatchHandlers.cs`: add `"savecleanup"` to the switch names. Note the agent reports an unset switch as on, which is wrong for this opt-in fix. Add counters `savecleanup_pairs` = c[40], `savecleanup_minis` = c[41], `savecleanup_runs` = c[42], `savecleanup_skipped` = c[43]. The last summary line is also in AppDomain data `ts3fixes.savecleanup` and in `ts3fixes.recent`.
4. `SaveCleanupOn()` deliberately does not go through `ShortcutOn`, whose unknown-name default is "on".

## 5. How to measure

Counts (reflection through the agent):
- `reflect_call Ts3Fixes.Fixes.SaveCleanupCounts` returns `rel=<Sims with relationships> pairs=<pairs> mini=<MiniSims> hh=<households> sims=<SimDescriptions>`.
- Without the mod helper: `reflect_get Sims3.Gameplay.Socializing.Relationship.sAllRelationships.Count` (outer only), `reflect_get Sims3.Gameplay.CAS.MiniSimDescription.sMiniSims.Count`, `reflect_get Sims3.Gameplay.CAS.Household.sHouseholdList.Count`, `reflect_call Sims3.Gameplay.CAS.Household.EverySimDescription` then `.Count`.
- `reflect_call Ts3Fixes.Fixes.RunSaveCleanupOnce` returns `save cleanup: -N pairs, -M minis | before ... | after ...`.

Repro entry (`repro-entry.json` in the scratch folder, for `tools/harness/repros.json`): id `bp-save-cleanup`, save `Cleanup-Test-BP`, a copy of an aged Bridgeport save that the main session creates. Steps: counts, cleanup, cleanup again (should remove about 0), `save_game` to `Cleanup-Test-BP-after`. Then a 5-minute stuck watch over all Sims. Pass:
- the first run removes N > 0 pairs, while `hh` and `sims` stay unchanged;
- the second run removes about 0;
- the save works, and there are no stuck Sims and no new ErrorTrap or script errors;
- reloading `-after` gives counts equal to the "after" values.

For disk impact, compare `save-stats.ts Cleanup-Test-BP Cleanup-Test-BP-after`; the relationship and MSD data sit inside `0x06B981ED` (OBJS). For time, compare the 0:00 day-change hitch (`DecayAll` walks every pair) before and after.

## 6. Risks and open checks

- Effect size is unknown: `DecayAll` already drops zero-liking pairs once a Sim decays to 0 by other means, and we do not yet know how many NPC pairs a long save has. Measure first (`pairs=` on an aged save).
- MSD pruning: not yet tested with a trip back to a pruned world (World Adventures, University, Into the Future). Run a travel test before recommending it on.
- A removed relationship loses the "learned info" (`InformationLearnedAboutSim`) the two NPCs had about each other. This is not visible to the player.
- Genie-wish relationships (`mIsGenieWishRelationship`, private) are not checked directly. `GenieWishForLove` moves the pair to Partner, Fiancee or Spouse (`Relationship.cs` line 1038), which our LTR check already excludes.
- One sweep over all pairs at 2:00 adds a hitch about the size of `DecayAll`'s [inferred]. Measure it with the hitch log.
