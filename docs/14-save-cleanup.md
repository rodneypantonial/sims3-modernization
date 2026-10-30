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

## 7. Next cleanups (built, untested)

Measured ranking: `docs/09` §6. The two candidates below come next after relationships and MiniSims. Everything else measured is native (CAS SimService cache, REFS), regenerated on demand (Sim portraits), not growing in 14 days (urnstones, photos, households), or already covered by Overwatch (inventories, dead Sims).

What the existing mods do here [verified, NRaas `master`]: Overwatch has no life-event or memory cleanup. ErrorTrap's `DerefLifeEventManager` only handles references to disposed objects (`mActiveNodes`, `mOwnerDescription`). MasterController's Remove Memories (not installed) is manual and deletes whole memories. Nothing trims empty lists or caps snapshots, so we would not duplicate anything.

### 7a. Empty life-event lists (built 2026-10-13, untested; counter [52])

**Built**: `Fixes.RunLifeEventTrimOnce()` and `Fixes.LifeEventCounts()` in `mods/ts3fixes/src/SaveCleanup.cs`, called from the 2:00 alarm after `RunSaveCleanupOnce` when `kSaveCleanup` / switch `savecleanup` is on; same skip rules (counter [43]). Counter [52] (agent `savecleanup_lifeevent_lists`; the array grew to 53 because [46] went to the watchdog idle cases); last line in AppDomain data `ts3fixes.lifeeventtrim`. Repro `bp-lifeevent-trim` in `tools/harness/repros.json` (step 3 uses `set_skill` Logic 10 on the active Sim). Re-checked in the decompiled code: no `Simulator.Sleep` in `ProcessLifeEvent` (line 1640), `CreateSubject` (1254), `GetActiveNode` (1861), `ShouldAddLifeEvent` (1967) or `LifeEventActiveNode.Create` (648); the method's only yields are at 409, 755, 960 (other tasks) and 1230 (`AddLifeEvent`, after the list is used) [verified]. `AddCustomLifeEvent` (1195) and `RemoveActiveNode` (1911) also use `TryGetValue`; `ClearInvalidActiveNodes` (2255) already removes keys the same way.

Why: `LifeEventManager.ProcessLifeEvent` (`Sims3.Gameplay.ActorSystems/LifeEventManager.cs` line 1640) does `mLifeEvents[data.Id] = new List<LifeEvent>()` the first time a life-event id is checked for a Sim, **before** the probability and dedup checks decide whether to add anything. `GetActiveNode` (line 1861) does the same for `mActiveNodes`, but only adds the node when `Actor.IsSelectable` (line 1883), so for NPCs the list always stays empty [verified]. The only cleanup is `Purge()` (line 2276), and it runs only for Sims dead 10+ days (`PurgeIfNecessary`, line 2235) [verified]. Measured in BP after 14 days: 27,904 such lists, 26,364 of them empty. With their backing arrays and dictionary entries they take 0.87 MB of the 2.9 MB script-state growth, and they are about 18% of all persisted objects (`docs/09` §6) [verified].

Rule: for every `SimDescription` in `Household.EverySimDescription()`, remove each key of `mLifeEvents` and `mActiveNodes` whose list has `Count == 0`. Both fields are private, so read them through reflection, like `sMiniSims`. Collect the keys first, then remove them. Nothing else is touched: non-empty lists, `LifeEvent` objects, snapshots, `mLifeEventsBeingProcessed` / `mLifeEventsBeingDelayed` (which hold `LifeEvent`s, not lists), `mCurrentNumberOfVisibleLifeEvents`.

Why it is safe: every reader uses `TryGetValue` and creates the list again when it is missing (lines 1652-1666, 1863-1881) [verified]. `GetVisibleLifeEvents` iterates values, so an empty list adds nothing. Export and import walk the dictionaries (`ExportContent`, line 2107), so fewer keys only means less data. The list from `TryGetValue` is used in the same call with no `Simulator.Sleep` before `AddLifeEventToQueue` adds to it, and script tasks are cooperative, so a cleanup alarm cannot run in between [verified for `ProcessLifeEvent`; `CreateSubject` and `ShouldAddLifeEvent` were not read for yields, inferred]. The worst case is that the game re-creates a list we removed: a few bytes, no behaviour change.

Expected effect: about -26,000 lists and -26,000 arrays (about -0.75 MB script, about -0.15 MB on disk after compression) on BP, plus faster load and save and fewer objects on the Mono heap [inferred; measure]. Lists come back for ids that fire again, so run it daily with the 2:00 cleanup. Same skip rules as §2 (travelling, vacation, saving, no active household).

Optional second step, not in the first build: drop invisible `LifeEvent`s (`!IsVisible`) of Sims that are not selectable and not in the active household, older than N sim days. NPC events are never shown; they only stop the same event from being added twice (`IsEquivalent && !IsTimedOut`, line 1656). BP had 1,661 of them (137 KB script). The risk is that an NPC who later joins the active household loses hidden history [inferred]. Leave this out until 7a is measured.

Repro (`tools/harness/repros.json`), id `bp-lifeevent-trim`, save `Cleanup-Test-BP` (Baseline-BP copy; 27,904 lists):
1. Wait 90 s after load, as in `bp-save-cleanup`. Run `reflect_call Ts3Fixes.Fixes.LifeEventCounts` (new helper): `lists=<total> empty=<empty> events=<LifeEvent objects> visible=<sum of mCurrentNumberOfVisibleLifeEvents for the active household> snaps=<active-household events with HasSnapshot>`.
2. Run `reflect_call Ts3Fixes.Fixes.RunLifeEventTrimOnce`, then run it again: the second run should remove about 0.
3. Trigger one memory for the active household with an agent action (for example a skill level-up or first kiss with an existing agent trigger; add one if missing, per the repro rules) and check that `visible` grows by 1.
4. `save_game` to `Cleanup-Test-BP-le`, then a 5-minute stuck watch over all Sims at speed 3, crossing one midnight.

Pass: run 1 removes N > 20,000 lists; `events`, `visible`, `snaps`, `hh` and `sims` are unchanged; run 2 removes under 100; the new memory appears in the scrapbook; no ErrorTrap or script errors; the save works. Offline: `node tools/harness/save-sources.ts Cleanup-Test-BP Cleanup-Test-BP-le` (with `FILTER=LifeEvent`) shows `List<LifeEvent>` and `List<LifeEventActiveNode>` down by about 26,000, and OBJS script state about 0.75 MB smaller.

### 7b. Memory snapshot cap (opt-in, default off; built 2026-10-13, untested; counter [53])

**Built**: `Fixes.RunSnapshotCapOnce(int cap)` in `mods/ts3fixes/src/SaveCleanup.cs`, its own tunable `Instantiator.kSnapshotCap` (int, default 0 = off; tuning XML in `build-fixes.ts`), run from the same 2:00 alarm before the `kSaveCleanup` part and independent of it; same skip rules (counter [43]). The agent runs it with `reflect_call` `args: [N]` (no agent switch: `patches` switches are bool). Counter [53] (agent `savecleanup_snapshots`; the array grew to 54); last line in AppDomain data `ts3fixes.snapshotcap`; `LifeEventCounts` now also prints `snapmax` (most capped pictures of one active-household Sim). Repro `bp-snapshot-cap` (`Cleanup-Test-BP`, cap 1: BP has only 4 pictures in `TravelDB.package`; use cap 10 on a `Baseline-SV` copy for the 29-picture case above). What it does differently from the design below: custom memories (id 0, pictures the player took with "Take memory picture", `AddCustomLifeEvent`, line 1179) are never counted or capped; a picture held by more than one `LifeEvent` (any household including previous travellers, plus urnstone Sims) is skipped, not cleared; the oldest are found with `List.Sort()` because `LifeEvent.CompareTo` orders by `mTimestamp` (line 232) and `mTimestamp` itself is private.

Safety re-check in the decompiled code [verified]: the key lives only in `LifeEvent.mSnapshot` (line 72) and is read through `Snapshot`, which falls back to the stock image (lines 104-117). Script code never builds an `EventWithScreenshot` (no `new EventWithScreenshot` anywhere in the decompiled DLLs), so every picture comes from `GetScreenshot` / `World.CapturePhoto` (line 1827) or a custom grab (line 1181), one per `LifeEvent`. The UI reads the key each time it shows a memory: `ScrapbookDialog.cs` line 570, `LifeEventDialog.cs` lines 219 and 311 (`HasSnapshot` gates the full-size view). `ShareableLifeEvent` copies the key (line 477) only for the moment of an online share; it is not persisted. Photos, paintings and the photo album are separate objects with their own keys; no other script code deletes or reads memory keys (`DeleteTravelDBResource` callers: `LifeEvent.Forget`, `TimeStatue`, `DataDisc` only). `Purge` (line 2276) calls `Forget`, which only deletes when `HasSnapshot`, so a capped memory is never deleted twice. Export for travel (line 308) writes the key only when `HasSnapshot`; travelling is skipped anyway. Open risk [inferred]: if the scrapbook is open while the 2:00 alarm runs, its cached `mMemories` may show a blank picture until it is reopened.

Why: the largest disk grower. In SV, one 2-Sim active household made 29 snapshots (3.82 MB, about 130 KB each) in 14 sim days, in `TravelDB.package` (type `0x00B2D882`, group `0x0269D005`) [verified]. Only selectable Sims get snapshots (`AddLifeEvent`, line 1221) [verified]. The game's only cap is `kMaximumNumberOfMemories = 400` visible memories per Sim (line 1017). That is up to about 50 MB per Sim, and the game only warns at the limit [verified].

Rule (tunable `kSnapshotCap`, for example 60 per Sim, 0 = off): per active-household Sim, sort visible `LifeEvent`s with `HasSnapshot` by `mTimestamp`. For each one older than the newest N, take `key = mSnapshot`, call `UpdateSnapshot(ResourceKey.kInvalidResourceKey)`, then `World.DeleteTravelDBResource(key)`, but only when no other `LifeEvent` of any Sim (households and urnstones) still holds that key. A shared screenshot is possible through `EventWithScreenshot`, which is passed to more than one Sim [inferred]. The memory stays, with its text, date and nature. The scrapbook shows the event type's stock image, because `LifeEvent.Snapshot` falls back to `LifeEventData.DefaultSnapshot` when `HasSnapshot` is false (lines 104-117) [verified]. Deleting through `DeleteTravelDBResource` is what `LifeEvent.Forget` does itself (line 169) [verified], so the image does not stay orphaned in the save.

Risk: it removes player content (old memory pictures), so it is opt-in and should be tested on a copy. It has not been checked whether shared or blogged memories (`Share`, `ShareableLifeEvent`) keep their own copy of the key. Players who do not want memories at all should simply turn memories off in the options (`docs/09` §4a.3).

Repro, id `sv-snapshot-cap`, save a copy of `Baseline-SV` (29 snapshots). Set the cap to 10 through the agent switch, run the cap once, then run it again (about 0 the second time), and `save_game` to `Cap-Test-SV-after`. Pass: `snaps` <= 10 per Sim, `visible` unchanged; the scrapbook opens and shows stock images for the capped memories; no errors. Offline: `save-sources.ts` shows `dds g269d005` in `TravelDB.package` dropping from 29 to about 20 (2 Sims x 10) and the file shrinking by about 1 MB.

Wiring for both (main session): the same pattern as §4. Add `RunLifeEventTrimOnce`, `LifeEventCounts` and `RunSnapshotCapOnce` to `SaveCleanup.cs`. Call the trim from the existing 2:00 alarm when `kSaveCleanup` is on. Add a separate tunable `kSnapshotCap` (default 0). Counters: `c[52]` (lists removed, built); the snapshot cap needs a new slot `c[53]` (46-51 went to the watchdog idle cases).

## 8. Aggressive tier and the spread pass (opt-in, built 2026-10-30, not run)

For long multi-generation saves. Switch `savecleanupaggressive`, tunable `kSaveCleanupAggressive` (default **off**, changes save data), on top of `kSaveCleanup`: when it is on, the `kSaveCleanup` parts run too. Two more tunables: `kAggressiveSnapshotKeep` (10 newest pictures per Sim outside the active household) and `kAggressiveQuietDays` (56 sim days, about two years of four 7-day seasons [inferred: the Seasons default length]). Code: `mods/ts3fixes/src/SaveCleanupSpread.cs`; rules without game types in `SaveCleanupCore.cs` (self-check `tools/selftest/savecleanup`).

### 8.1 Spread pass (all parts, also the old ones)

The 2:00 alarm no longer runs the cleanup in one tick. It starts one script task (`SaveCleanupSpreadRun`) that runs each part over its items in slices of about 2 ms (`EaSpreadCore.kSliceUs`, measured with a `StopWatch`, one slice per tick, `Simulator.Sleep(0)` between slices), the way `deadrefsweep` and `easpread*` do. Before each next slice it re-checks the world state: no world, travelling, vacation, saving, not live mode, no active household, or an active-household member with an invalid description stop the pass (counters [43] and [415]). Every item is re-tested at its own turn, and each step is wrapped so one bad item does not stop the pass. A part's item list is gathered in that part's first slice; a gather is a list copy of keys or descriptions.

Parts in order, each only when its switch is on:
1. Snapshot holders (`kSnapshotCap > 0` or aggressive): which picture keys more than one life event holds, built Sim by Sim (before: one walk over every life event in one tick).
2. Active-household snapshot cap (`kSnapshotCap`, §7b).
3. Aggressive: pictures of Sims outside the active household (§8.2.1).
4. Relationship pairs per key Sim: rule §2a, plus §8.2.4 when aggressive.
5. Other-world MiniSims (§2b): the keep set is built in the gather, each MiniSim tested at its turn.
6. Empty life-event lists per Sim (§7a).
7. Stale route callbacks per Sim (`PortalAdaptorLeak.cs`).
8. Aggressive: per-Sim history (§8.2.2), dead outfits (§8.2.5), cleanse the dead (§8.2.3).

The `Run*Once` methods stay as one-tick versions for the test agent. The #91 pre-save pass (`DeadRefSavePass`, docs/205) already does only a cheap final part at save; it is owned by `deadrefsweep` and not changed here.

### 8.2 What the aggressive tier removes

"Outside the active household" (`OutsideActive`): a valid description that is not in the active household and not a playable ghost, has no selectable or destroyed Sim object, is not travelling or at boarding school, and is either dead or in some household. "Buried" (`BuriedSim`): the description of a grave or urn that is dead, not a playable ghost, and has no Sim object and no household.

#### 8.2.1 Memory pictures of Sims outside the active household (counter [416])

Only selectable Sims get pictures (`AddLifeEvent`, §7b [verified]), so the Sims outside the active household that have any are former members: Sims who moved out, households the player stopped playing, and the recently dead. Each keeps its newest `kAggressiveSnapshotKeep` own pictures; older ones are cleared exactly as §7b does (`UpdateSnapshot(kInvalidResourceKey)`, then `World.DeleteTravelDBResource`). The memory itself stays; custom memories (id 0) and shared keys are never touched. The active household keeps all its pictures (its own cap stays `kSnapshotCap`).

Dead Sims: EA's daily `LifeEventManager.PurgeLifeEvents` (`Sims3.Gameplay.ActorSystems/LifeEventManager.cs` line 2215) already walks every household Sim and every urnstone and purges all memories (`Forget` deletes the picture) 10 days after death; a playable ghost resets the death time (`PurgeIfNecessary`, line 2235) [verified]. So for the dead this part only acts within those 10 days.

#### 8.2.2 Per-Sim history (counter [418], entries)

For every Sim outside the active household (households and urnstones):
- `ReadBookDataList` cleared. Readers use `ContainsKey`/`TryGetValue` or the count, and reading again adds the entry back (`Sims3.Gameplay.Objects/ReadBookData.cs` line 63) [verified]. Effect: an NPC counts as not having read a book; two readers use the count (`Sim.cs` line 18335 answer chance, `VaultOfAntiquity` line 426) [verified].
- `OpportunityHistory`: the private completed and user-rejected dictionaries cleared (`Sims3.Gameplay.Opportunities/OpportunityHistory.cs` line 109). Current and last opportunities stay. Effect: a former member who rejoins the active household can get one-time opportunities again [inferred].
- `SpoiledGiftHistory` set to null for the dead and for teens and older: what `AgingState` does at the teen age-up (`AgingState.cs` line 467); giving a gift creates it again (`Sim.cs` line 8764) [verified].
- Dead only: `GameObjectRelationships` (minor pets: birds, terrarium animals) cleared; EA itself drops entries whose object is gone (`SimDescription.cs` line 3219) [verified].

Never touched here: skills, traits, careers, genealogy, relationships (except §8.2.4), `mInventoryItemsWhileInPassport`.

#### 8.2.3 Cleanse the dead (counters [420], [421])

What EA does [verified]: when a Sim dies, `GrimReaperSituation` keeps the grave in a random mausoleum only when the Sim was protected for the player or had a significant relationship with the active household; otherwise it destroys the grave, which disposes the Sim (`Sims3.Gameplay.Services/GrimReaperSituation.cs` line 1325, `AddGraveToRandomMausoleum` line 2622). Story-progression deaths go to a mausoleum too (`Sims3.Gameplay.StoryProgression/KillSim.cs` line 209). `Urnstone.Dispose` disposes the dead description when it has no Sim object and the game is not in Edit Town (`Sims3.Gameplay.Objects/Urnstone.cs` line 2197). `GameObject.Destroy` takes an object out of its parent inventory first (`Sims3.Gameplay.Abstracts/GameObject.cs` line 4350). So mausoleum graves are the ones tied to the player when the Sim died; generations later nobody may be left.

Rule: a buried Sim whose grave sits in a mausoleum inventory (`Inventory.ParentInventory(stone).Owner is IMausoleum`) is disposed with `stone.Destroy()`, the call EA's own story code uses, when all of these hold: no parent, child or sibling is alive (`Genealogy.IsAlive`, which also reads MiniSims) or a playable ghost; no spouse, parent, child or sibling link to an active-household member; neither is the other's partner; no relationship entry with an active-household member; not in Edit Town. Graves placed in the world or held in a Sim's or a household's inventory are never touched. Unknown genealogy counts as family.

NRaas Overwatch has the same cleanup ("Cleanse The Dead", off by default, docs/09 §2.1). When the `NRaasOverwatch` assembly is loaded, this part stands down for the whole pass (counter [421]); we never change Overwatch's settings. Overwatch's source was read for reference only (`game-copy/nraas-src/NRaas/Helpers/CleanseTheDead.cs`); no code was taken.

#### 8.2.4 Quiet friend-or-weaker NPC pairs (counter [417])

On top of §2a: a pair of NPCs that both pass the §2a Sim checks (alive, in a household that is not the active one, not selectable, no Long Distance Friend trait, not at boarding school, not travelling) is removed with `Relationship.RemoveRelationship` when its state is Stranger, Acquaintance, Disliked, Distant Friend or Friend; it was never romantic; it has none of the §2a ties (same household, partner, spouse, blood, step or future family, in-law, working relation); and the two have not talked for `kAggressiveQuietDays` sim days (`WhenLastTalked`), or never talked and the state is that old (`WhenStateStarted`). Good Friend and up, Enemy, Old Enemies and every romance state stay. Effect: two townies who were friends long ago start again as strangers when they next talk (`Relationship.Get(..., createIfNone: true)`, §2a) [verified]. Counter [417] counts pairs only this rule removed; [40] counts all.

#### 8.2.5 Outfits of the buried (counter [419]) and their inventory (counter [422])

What a dead Sim shows [verified]: a wandering or summoned ghost is the description instantiated with its default outfit (`SimDescription.Instantiate(Vector3)` uses `mDefaultOutfitKey`, `Sims3.Gameplay.CAS/SimDescription.cs` line 4787; `Urnstone.cs` lines 655, 1113, 2352), switched from Naked to Everyday when needed (line 2389); the ghost look is a native shader state (`World.ObjectSetGhostState`), not an outfit. The grave's portrait uses Everyday index 0 (line 628). Resurrection (`GhostToSim`, line 2576) clears the ghost state and keeps the outfits. A Sim with no outfit in a category does not switch to it (`Sims3.Gameplay.Actors/Sim.cs` line 61843), and outerwear is made on demand (line 62516) [verified].

Rule, buried humans only: every outfit of Formal, Sleepwear, Swimwear, Athletic, Singed, Martial Arts, Career, Aux Career, Makeover, Skinny-dipping Towel, Massage, Child Imagination and Outerwear (regular and maternity lists) is removed with the game's `RemoveOutfit(category, index, release: true, maternity)`, except the default outfit. Naked, Everyday (all of them), Special and Supernatural (occult forms) stay. `RemoveOutfitInternal` releases an outfit only when no other category still holds the same key (`Sims3.Gameplay.CAS/SimDescriptionCore.cs` line 1094) [verified]. Effect: a resurrected Sim wears Everyday until the player makes new outfits in CAS [inferred: no EA code was found that rebuilds missing categories on resurrection]. Expected gain: fewer outfits in the native CAS SimService cache (`0x342779A7`, §1) [inferred; measure with save-stats].

Inventory [verified]: a Sim's inventory belongs to its Sim object, not to the description. When a Sim of the active household dies, `GrimReaperSituation.FinalizeDeath` moves its inventory to the oldest living (or playable ghost) household member (`Sim.MoveInventoryItemsToAFamilyMember`, `Sim.cs` line 60982; items marked `INotTransferableOnDeath` are destroyed by EA there); a playable ghost sent back to its grave does the same (`Urnstone.ReturnToGraveCleanup`, line 3650). A buried Sim has no Sim object, so it holds no inventory. The only inventory a description can still hold is `mInventoryItemsWhileInPassport` (items parked during a passport trip, `Sims3.Gameplay.Passport/Passport.cs` line 128). It is never deleted or moved: counter [422] counts buried Sims that still hold such a list, so we learn whether it happens at all.

### 8.3 What it never touches

Everything in §3 except what §8.2 lists, and always: living Sims of the active household and its playable ghosts; any selectable Sim; graves placed in the world or held in any inventory other than a mausoleum; dead Sims with a living or ghost parent, child or sibling, or any family, partner or relationship tie to the active household; Good Friend and stronger, enemies and every romance; Naked, Everyday, Special and Supernatural outfits and the default outfit; skills, traits, careers, genealogy; inventories. Nothing runs while travelling, on vacation, saving, outside live mode, or before the load fix-up.

### 8.4 Counters and checks

[411] passes finished, [412] slices, [413] longest slice us, [414] total ms, [415] passes refused or stopped (and items that threw), [416]-[422] as above (agent names `savecleanup_spread_*`, `savecleanupaggressive_*`). Agent check `Ts3Fixes.Fixes.SaveCleanupAggressiveCheck`: `count` (candidates per part, no change), `run` (start the pass now), `cleanse` (§8.2.3 alone in one tick, also with Overwatch loaded; scratch test saves only).

### 8.5 Test plan

No multi-generation save existed. Repro `bp-multigen-build` builds the scratch save `MultiGen-Test-BP` from `Baseline-BP` with the test agent: short lifespan, children aged up, elders made friends of the active Sim and aged to death (so EA keeps their graves in the mausoleum), a birth, 5 minutes at ultra. Repro `bp-multigen-aggr` saves the same state with the cleanup off (`MultiGen-BP-off`), runs the pass twice (the second should remove about 0), runs `cleanse`, then the save matrix (new, overwrite, unpaused, build mode, reload, play, save after reload). Queue:

`node tools/harness/queue.ts tools/harness/queues/save-cleanup-aggressive.json`

Save size: `node tools/harness/save-stats.ts MultiGen-BP-off MultiGen-BP-aggr`. Hitches: job `bp-multigen-ab` (in-load A/B, 2-minute slices), judged with `tools/harness/hitch-ab.ts`: no new frames >= 100 ms from the cleanup and `savecleanup_spread_max_slice_us` <= ~3000.

Risks [inferred]: rotation players are covered by §8.6 (played households get the lighter tier), but only within `kPlayedGenerations` of a living member: older generations of a played family, and households the player never made active, get the full aggressive tier; §8.2.2 changes NPC behaviour slightly (book counts; one-time opportunities for returning members); §8.2.4 drops old friendships between townies; §8.2.5 resurrected Sims need new outfits; the cleanse removes family-tree entries of Sims nobody is linked to any more (the daily genealogy purge keeps only 1 link of the dead anyway, docs/09 §3).

### 8.6 Played households and the lighter tier (rotation play)

EA keeps no "was ever played" flag on `Household` or `SimDescription` (only `IsNeverSelectable`; no field named played, ever-active or was-active in `Household.cs` or `SimDescription.cs`) [verified]. So we keep our own set (`mods/ts3fixes/src/PlayedHouseholds.cs`).

Store (rule: our data never goes into a save): a sidecar text file per save, kept by ts3native (`native/ts3native/played.cpp`), at `<Documents>\Electronic Arts\The Sims 3\ts3native\played\<key>.txt` (the same user folder as the saves, Mods and `ts3native-crashes`; `<Documents>` from the shell folder registry key, so OneDrive-moved folders work). One decimal household id per line; `#` lines are comments; a header names the save key, world and write time. Removing the mod, or deleting the file, leaves every save untouched; deleting the file only forgets which households were played. Script code cannot write files, so ts3fixes talks to the store over the `[UserDiag]` text channel (top byte `0x87`, docs/188): text lines `played: open <key> <world>`, `played: saveas <key> <world>`, `played: add <id>`, `played: ask <id>`, and op 6 for the answer (store ready after open/saveas; played or not after ask). Writes go to a temp file renamed over the old one. A missing file is an empty set, a corrupt line is skipped, a file over 256 KB or unreadable is an empty set; each case is logged (`played: <key>: ...` in `ts3native.log`), never a crash. Without the store (no ts3native, an older build, `[UserDiag] Enabled=0`) op 6 answers 0: ts3fixes keeps this session's households in memory only and the aggressive tier stands down for every pass (counter [415], one log line); the basic `kSaveCleanup` parts still run.

Key: `SaveCleanupCore.PlayedKey` of the save's file name (`LoadSaveManager.GetCurrentSaveGameInfo().ID`, folders cut, characters outside `[A-Za-z0-9._-]` replaced by `_`, at most 100) [inferred: that the ID is the save's file name; the first run's log line shows it]. A game with no save yet uses `unsaved_<world>`, kept in memory only. After every save (the `OptionsModel.SaveGame` hook), at every add and at each pass start the key is read again; when it changed (Save As, the first save of a new game) the set is written under the new key (`played: saveas`), so the old save keeps its file and the new one gets a copy. A save renamed outside the game starts with an empty set. Other worlds and other saves never share a file.

Recording: at every world load, and on `EventTypeId.kHouseholdSelected`, which `PlumbBob.CheckForChangeInActiveHousehold` sends whenever the active household changes (Edit Town household switch, selecting a Sim of another household, load, return from travel) (`Sims3.Gameplay.Core/PlumbBob.cs` line 430) [verified], the active household's id is added. Counters [423] set size, [424] ids added this session; each new id logs `savecleanup: played household added hh=<id>` in `ts3native.log`, and each load logs the set size. Recording runs even while every cleanup switch is off, so the set is complete when a player turns the cleanup on later.

Generation depth: at the start of each pass, a breadth-first walk over genealogy parent and child links from every living member of every played household (the active one included) gives each Sim its number of steps to the nearest one, up to `kPlayedGenerations` (default 2: parents and grandparents, children and grandchildren; a sibling is 2 steps, via a parent). Every member of a played household counts as 0. Sims within that depth get the lighter tier; older generations (for example a great-grandparent's grave) get the normal aggressive tier.

Lighter tier (played Sims within the depth, other than active-household members, who keep full protection):
- the `kSaveCleanup` rules (§2a weak pairs, §2b, §7a) as before;
- pictures capped at `kPlayedSnapshotKeep` (40) instead of `kAggressiveSnapshotKeep` (10) (counter [425]);
- no history trim (§8.2.2), no quiet-friend rule (§8.2.4: a pair is kept when either Sim is played), no cleanse (§8.2.3) (counter [426] counts each skip);
- dead outfits (§8.2.5) still apply, the same as for the active household's own dead (buried Sims have no household, so this rule never looked at households).

Per-action log: every cleanup action on a Sim writes one `ts3native.log` line: `savecleanup: <part> tier=<basic|cap|light|aggr|dead> hh=<household id or 0> played=<0|1> gen=<steps or -1> near=<0|1> sim=<id>` (pairs log both Sims). MiniSim and route-callback removals are not per Sim of a household and are not logged per action. The queue fails any `tier=aggr` line with `near=1` (`logForbid` in every job of `save-cleanup-aggressive.json`). Repro `bp-multigen-rotation` switches to another household and back with the agent's `switch_household {household: "@other" | "@previous"}`, saves, reloads (the set must still hold both ids) and runs the pass.
