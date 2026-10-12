# 09 Save bloat in long-running saves: what grows, what cleans it, what is left

Goal: know what makes a long-running save grow and fill with errors, what existing mods already clean, and where our own cleanup (a C# script patch in our mod) would add value.

Markers used on every claim:
- **[verified]**: read in the decompiled game code (`game-copy/decompiled/...`) or in the public NRaas source (https://github.com/Chain-Reaction/NRaas, `master`; the installed builds were not diffed against it).
- **[community]**: reported by players or mod authors; source URL given.
- **[inferred]**: our reasoning from code or reports, not yet measured. Confirm with the harness (section 5).

Paths below are relative to `game-copy/decompiled/Sims3GameplaySystems/` unless they start with `NRaas`.

## 0. Summary

- The biggest growth on disk is **images**: memory snapshots, photos, portraits and thumbnails, and **one extra world copy per visited world** (about 30 MB or more each) [community]. Plain "Save" over the same slot also grows and corrupts saves; "Save As" writes a fresh file [community]. Our own code cannot fix most of this, but an offline cleaner (Regul Save Cleaner) and two settings (memories off, Save As) do.
- The biggest growth in **script memory and script time** is the Sim graph: `Relationship.sAllRelationships` (decays almost never for NPC to NPC pairs), `MiniSimDescription.sMiniSims` (every Sim ever seen, kept forever), dead Sims in urnstones (full `SimDescription` each), and per-Sim histories [verified that these collections exist and are only partly pruned; size impact inferred].
- The game already prunes some of this: daily distant-genealogy purge, life events of the dead after 10 days, zero-liking stranger/acquaintance relationships after 1 day, invalid relationships on load [verified].
- NRaas Overwatch (installed) already covers broken relationships, orphan relationships, empty MiniSims and genealogy repair, and bad inventories, with defaults on. Its two real bloat cutters, **Cleanse The Dead** and **Clean up All Homeless**, are **off by default** [verified].
- Best gaps for our own cleanup, ranked: (1) NPC to NPC relationship pruning (low liking, never met in years), (2) MiniSimDescription pruning for Sims in uninstalled or never-revisited worlds with no family links, (3) memory snapshot cap per Sim (keep the newest N), (4) per-Sim history trimming (opportunity history, read books, spoiled gifts). Measure first.

## 1. What accumulates over sim time

Impact columns: **Disk** = save size, **RAM** = script heap / 4 GB address space, **CPU** = script time per tick or per day.

| Item | Where it lives (code) | Why it grows | Game-side pruning | Impact | Risk of removing |
|---|---|---|---|---|---|
| Memories (Generations life events) and their snapshots | `Sims3.Gameplay.ActorSystems/LifeEventManager.cs`: per-Sim `mLifeEvents` (`Dictionary<uint, List<LifeEvent>>`), nested `LifeEvent.mSnapshot` (`ResourceKey` of a screenshot) [verified] | Every milestone adds an event; with memories on, many get a screenshot stored as an image resource in the save [verified field; image storage community-reported] | `LifeEventManager.PurgeLifeEvents()` runs daily from `Autonomy.OnceADayCallback`; `PurgeIfNecessary()` purges only for **dead** Sims, `kGracePeriodBeforeDeletion = 10` days after death (line 1021) [verified]. Nothing prunes the living. | Disk: high. Called the top bloat cause and a performance hit by players: https://steamcommunity.com/app/47890/discussions/0/3829787744080973906, https://steamcommunity.com/app/47890/discussions/0/133259855834115634 [community] | Low for gameplay (memories are cosmetic). Players lose their scrapbook. Some dreams read life events [inferred]. |
| Photos and paintings (objects) | `Sims3GameplayObjects/Sims3.Gameplay.Objects.HobbiesSkills/Photograph.cs` (`mKeySims`, `mKeyInstances`, image per photo); `Sims3.Gameplay.Skills/PaintingSkill.cs` `sPaintingsOfSims` [verified] | Each photo is an object plus an image resource (type `0x00B2D882`, group `0x0269D005`) [community: https://modthesims.info/d/652440] | None | Disk: medium to high with photographers; RAM/CPU: low unless many are placed on lots [inferred] | Medium: they are player items. Only remove in NPC inventories and family inventories of NPC households. |
| Portraits and thumbnails | Save package image resources: family portraits `0x6B6D837E`, Sim portraits `0x0580A2CD/CE/CF`, lot thumbnails `0xD84E7FC6` [community: https://modthesims.info/d/652440]; `MiniSimDescription.GenerateCrossWorldThumbnail` [verified] | One per Sim and household ever seen, regenerated after changes | None found | Disk: medium [community] | Low: regenerated on demand. Offline tool only. |
| Visited worlds | One `.nhd` + `.package` per visited world inside the save folder, plus `TravelDB.package` [community: https://steamcommunity.com/app/47890/discussions/0/133259855834115634] | A full copy of each world's state is kept | None | Disk: "at least 30 MB per world" [community: https://steamcommunity.com/app/47890/discussions/0/3829787744080973906] | Never delete the world you live in; deleting vacation or University copies resets those worlds [community, same thread] |
| Sims from other worlds (MiniSimDescription) | `Sims3.Gameplay.CAS/MiniSimDescription.cs` `private static Dictionary<ulong, MiniSimDescription> sMiniSims` (line 85); each holds `List<MiniRelationship> mMiniRelationships` (line 94) [verified] | `AddMiniSim` / `AddMiniSims()` add every Sim in the world on travel (called from `GameStates.cs` lines 1665, 1784), on `SimDescription.Dispose(addMiniSim: true)`, on births and moves [verified]. Never removed just because they are old. | `RemoveMSD()` is only called from pet cleanup without Pets (`SimDescription.PostWorldLoadCleanUpPets`), urnstone disposal of pets, and `Genealogy.ClearAllGenealogyInformation()` (line 1250, reached from the daily distant-genealogy purge) [verified] | RAM: one object per Sim ever met, plus mini relationships. CPU: `FindMiniRelationship` is a linear scan of the list (line 1191) [verified]; cost grows with mini relationships [inferred]. Disk: medium after many trips [inferred] | Medium: removing one with family links breaks family trees in other worlds and travel return. Safe only when no genealogy, no mini relationships, no home lot, no job (this is exactly Overwatch's rule, section 2.1). |
| Living SimDescriptions (townies, NPC pools) | `Sims3.Gameplay.CAS/Household.cs` `public static List<Household> sHouseholdList` (line 426); `Household.EverySimDescription()`; services in `Sims3.Gameplay.Services/Service.cs` `mPool` (line 87) [verified] | Story progression and services create townies, immigrants, tourists; each is a full `SimDescription` with managers (skills, traits, careers, celebrity, opportunities, dreams) | None for living Sims except on load: `SimDescription.OnWorldLoadFinished()` drops descriptions with no household and no death, or not in their household's list (lines 2891 on) [verified] | RAM and CPU: high. Each Sim is in autonomy, aging, relationship decay and story progression loops [inferred]. Players report big worlds lag; MasterController Demographics is the usual count tool [community: https://forums.ea.com/discussions/the-sims-3-technical-issues-en/re-phone-calls-are-stopping-the-game/8300112] | High if done by hand: households, careers and role Sims depend on them. Use StoryProgression caps, not deletion. |
| Dead Sims | `Sims3.Gameplay.Objects/Urnstone.cs` `DeadSimsDescription` (a full `SimDescription` per grave or urn); `SimDescription.GetHomelessSimDescriptionsFromUrnstones()`, `DoesUrnstoneExistForSimDescriptionId()` (both scan all urnstones with `Queries.GetObjects<Urnstone>()`) [verified] | Every death adds one. Graves end up in the cemetery mausoleum inventory. | Life events purged 10 days after death (above). Genealogy of the dead kept only 1 link deep by the daily purge (`kPreservedGenealogyLinksForDeadSims = 1`, `SimDescription.cs` line 850) [verified] | RAM: one full SimDescription each. CPU: the urnstone scans above are O(graves) and run from relationship code (`MiniSimDescription.UpdateOtherMiniRelationship`, line 1218) [verified; cost inferred] | Medium: ghosts, family trees and "visit grave" break. Safe only for buried Sims with no living parents, children or siblings (Overwatch's rule). |
| Relationships | `Sims3.Gameplay.Socializing/Relationship.cs` `public static Dictionary<SimDescription, Dictionary<SimDescription, Relationship>> sAllRelationships` (line 244) [verified] | Every social and every story-progression interaction creates a pair, in both directions | `Relationship.DecayAll()` (line 1142, daily via `SocialComponent.RelationshipDecay` alarm) drops a pair only when liking is about 0 for `kNumDaysAfterWhichZeroRelationshipIsDropped = 1` day **and** it is Stranger or Acquaintance, different households, not blood relatives, no working relation (`CanRelationshipDecayAway`, line 1263). NPC to NPC decay is multiplied by `kNPCRelationshipDecayMultiplier = 0.001` (`SocialComponent.cs` line 212), so NPC pairs almost never reach zero [verified]. On load, pairs with invalid descriptions are removed (`SimDescription.PostLoadFixUp`, lines 3170-3190) [verified] | RAM: O(pairs). CPU: `DecayAll` walks every pair once a day and allocates a key list per Sim [verified]; a day-change hitch that grows with pairs [inferred] | Low for NPC to NPC Stranger/Acquaintance pairs with low liking; never touch family, romance, household or active-Sim pairs. |
| Family trees | `Sims3.Gameplay.Socializing/Genealogy.cs` `mChildren`, `mNaturalParents`, `mSiblings`, `mAncestors`, `mSpouse`, `mMiniSim` (lines 347-363) [verified] | Generations of descendants and ancestors | `SimDescription.PurgeDistantGenealogyRelations()` (line 6362) daily from `Autonomy.OnceADayCallback`: keeps 3 links from every living resident (`kPreservedGenealogyLinksForLivingSims = 3`) and 1 from the dead in this world; clears the rest and removes their MSDs [verified] | Already bounded by the game | Do not add more pruning; players care about trees. |
| Opportunity history | `Sims3.Gameplay.Opportunities/OpportunityManager.cs` `mCompletedOpportunities` (line 99), `OpportunityHistory.cs` (line 109) [verified] | Grows with every completed opportunity, per Sim | None found | Low [inferred] | Medium: some opportunities are one-time and would repeat. Overwatch already cleans broken entries (2.1). |
| Read books, spoiled gifts, travel buffs, object relationships | `SimDescription.cs`: `ReadBookDataList` (696), `SpoiledGiftHistory` (795), `TravelBuffs` (664), `GameObjectRelationships` (720), `mInventoryItemsWhileInPassport` (771) [verified] | Per-Sim lists that only grow | None found | Low each; adds up over hundreds of Sims [inferred] | Low for NPCs; medium for the active household (book re-reads, gifts). |
| Dreams and promises (wishes) | `Sims3.Gameplay.DreamsAndPromises/ActiveNodeBase.cs` `NodeSubject.PruneCache()` (line 314), called by `DreamsAndPromisesManager` (line 543) [verified] | Node subjects reference Sims and objects | Game prunes its own cache [verified] | Low | MasterController "Drop All Dreams" exists for broken ones [verified: `NRaasMasterController/MasterControllerSpace/Town/DropAllDreams.cs`]. |
| Celebrity data | `Sims3.Gameplay.CelebritySystem/CelebrityManager.cs` `mImpressAttempts` (`Dictionary<ulong, List<ImpressAttempt>>`, line 298), `mDisgracefulActionQueue` (294) [verified] | Per celebrity, per Sim tried | None found | Low [inferred] | Low to medium. Not a priority. |
| Alarms | `Sims3.Gameplay.Utilities/AlarmManager.cs` `mTimers` (`Dictionary<AlarmHandle, List<Timer>>`, line 234), `mGameObjectIndex` (236) [verified] | Leaked alarms on objects that were destroyed by errors | ErrorTrap and Overwatch reset broken owners [verified, 2.1/2.4] | CPU: per-tick alarm processing [inferred] | High to remove blindly. Count only. |
| Event listeners | `Sims3.Gameplay.EventSystem/EventTracker.cs` `sInstance.mListeners` (`Dictionary<ulong, Dictionary<ulong, List<EventListener>>>`, line 17) [verified] | Listeners that are never removed (from broken interactions, opportunities, mods) | None found | CPU: every `EventTracker.SendEvent` walks the matching lists [inferred] | High to remove blindly. Count only, find the leaking owner. |
| Inventories (NPC, service household, lost items) | `Household.CleanupBadlySavedInventories()` (`Household.cs` line 2376), `LotManager.CleanupObjectsNotOnLot()` (`LotManager.cs` line 2631) exist but have no caller in the decompiled scripts [verified, by grep] | Career limos, homework, gifts and junk pile up in NPC inventories | None effective | RAM/CPU: tens of thousands of limo objects reported [community: https://forums.ea.com/discussions/the-sims-3-technical-issues-en/re-phone-calls-are-stopping-the-game/8300112] | Low for NPC junk; Overwatch already does it (2.1). |
| Pregnancies, pets, horses, ghosts, occults | `SimDescription.Dispose` calls `ClearPregnancyData()`; `PostWorldLoadCleanUpPets`, `PostWorldLoadCleanUpSupernatural` (lines 3490, 3507) only run when the pack is missing [verified] | Not a growth source by itself; they are SimDescriptions like any other | As above | Covered by the SimDescription rows | Same as living/dead Sims. |
| Story-progression bookkeeping (EA) | `Sims3.Gameplay.StoryProgression/*` (actions such as `AddSim`, `CreateHousehold`, `GenerateOffspring`, `KillElderSim`) [verified] | EA story progression keeps creating households to keep demographics | Kills elders, emigrates households (EA tuning) | It is the source of townie growth [inferred] | Tune, do not delete. NRaas StoryProgression replaces it. |

## 2. What existing mods already clean

### 2.1 NRaas Overwatch (installed)
Menu: City Hall > NRaas > Overwatch. The nightly run starts at **Alarm Hour** (`kAlarmHour = 3`; our install guide moves it to 5, see `docs/07-known-fixes-catalog.md`). Defaults below are from `NRaasOverwatch/OverwatchSpace/PersistedSettings.cs` and `NRaasOverwatch/XML/OverwatchSpacePersistedSettings.xml` [verified].

Nightly alarms (`OverwatchSpace/Alarms/`):

| Option (menu name) | Default | What it does (from source) |
|---|---|---|
| Clean up Relationships (`CleanupRelationships.cs`) | **on** | For every key in `Relationship.sAllRelationships`, runs `Corrections.CleanupRelationship`; drops `mPartner` if the partner description is invalid; recreates a missing partner relationship [verified]. Repairs, does not prune healthy NPC pairs. |
| Clean up Inventory (`CleanupInventory.cs`) | **on** | `Inventories.CheckInventories(...)` removes broken and orphaned inventory items [verified]. Community reports it removed tens of thousands of career limos [community, EA link above]. |
| Clean up Genealogy (`Actions/CleanupGenealogy.cs`) | **on**, also runs on load | Repairs spouse/partner and parent links, and `CleanupMiniSims()` removes MiniSimDescriptions that (a) have no matching Sim, (b) have no name, or (c) have no home lot, no job, no mini relationships and no genealogy links (`Annihilation.RemoveMSD`) [verified]. This is the "clean up Mini Sim Descriptions" behaviour; there is no separate option of that name [verified by search of the repo]. |
| Cleanse The Dead (`CleanseTheDead.cs`) | **off** | Collects buried Sims (no `CreatedSim`, no household) in mausoleums or anywhere, **skipping any with a living parent, child or sibling** (`NRaas/Helpers/CleanseTheDead.cs` `HasLiveFamily`), then disposes the description and its urnstone. "Family Depth Compress Level" (`mCompressFamilyLevel`, default 0) limits it to Sims at or beyond that family depth; that setting is shown only in debug mode [verified]. |
| Clean up All Homeless (`KillAllHomeless.cs`) | **off** | `Annihilation.Cleanse` on every household that is not in world, not special, not passport, role, lunar zombie or active daycare [verified]. Very aggressive: deletes their relationships, genealogy, life events and MSDs. |
| Purge Genetic Hair | off | Clears `GeneticHairstyleKey` on Sims in world [verified]. Small. |
| Recover Missing Sims, Restart Digital Photos, Stuck Check, Reset Check, Clean up Vehicles/Elevators/Concert/Laundromat/Skill Modifiers/Outfits, Turn Off Stereos/TVs | mostly on | Stutter and stuck-Sim fixes; see `docs/03-prior-art-re.md` §5.3. `RestartDigitalPhotos` only restarts slideshows, it does not delete photos [verified]. |

On-load fixes (`OverwatchSpace/Loadup/`, always on, no setting) [verified]:
- `CleanupRelationships`: removes `sAllRelationships` rows whose Sim is not in `SimListing.AllSims` (orphan rows).
- `CleanupDnPExportData`: drops `DnPExportData` on residents.
- `CleanupOpportunityHistory`, `CleanupCompletedOpportunities`: fixes broken opportunity entries.
- `CleanupSimHousehold`, `CleanupServicePools`, `CleanupPetAdoption`, `CleanupInvalidGameObjectRelationships`, `CleanupSituations`, `CleanupTimers`, `CleanupReactionBroadcasters` and about 60 more one-shot repairs (full list: https://github.com/Chain-Reaction/NRaas/tree/master/NRaasOverwatch/OverwatchSpace/Loadup).

**Settings to turn on** (our recommendation): keep the defaults; optionally turn on **Cleanse The Dead** for saves with many generations (it already keeps anyone with living close family). Leave **Clean up All Homeless** off unless StoryProgression is not installed and homeless counts are high; check the count with MasterController Demographics first.

### 2.2 NRaas MasterController (not installed, optional)
Manual tools only; nothing runs on a schedule [verified, source tree]:
- Town > **Cleanse The Dead** (`MasterControllerSpace/Town/CleanseTheDead.cs`): same helper as Overwatch, but you pick graves from one mausoleum.
- Town > Manage The Dead / Reconstitute The Dead, Drop All Dreams, Clear Notices, Reset All (the "reset everything" step players recommend after installing ErrorTrap and Overwatch [community: https://forums.ea.com/discussions/the-sims-3-technical-issues-en/re-phone-calls-are-stopping-the-game/8300112]).
- Sim > Intermediate > **Remove Memories** (`Sims/Intermediate/RemoveMemories.cs`): deletes life events for one Sim or "apply all", with a choice per type [verified]. The in-game way to clear memory bloat.
- Sim > Advanced > **Total Annihilation** and Town > Total Annihilation: removes Sims and everything linked. **Not wanted** (per brief).

### 2.3 NRaas StoryProgression (not installed, optional)
- **Maximum Residents** (`ManagerSim.MaximumResidentsOption`, default **150** per species, 0 = unlimited) caps the town population; immigration stops above it [verified: `NRaasStoryProgression/StoryProgressionSpace/Managers/ManagerSim.cs` line 1422].
- `CleanHomelessScenario`: kills homeless Sims that fail `ManagerSim.ValidSim` [verified].
- `ServiceHouseholdCleanup` option (default on): empties the NPC service household's shared inventory [verified: `Scenarios/Households/ServiceCleanupScenario.cs`].
- Replaces EA story progression, which is the main source of new townies [inferred].
- There is no "dispose of unused SimDescriptions" option by that name; the closest are the population cap and homeless cleanup [verified by search of the repo].

### 2.4 NRaas ErrorTrap (installed)
- On load and as objects load: `ErrorTrapSpace/Checks/*` (CheckSim, CheckUrnstone, CheckGameObject, CheckVehicle and others) repair Sims with missing managers, run `Corrections.CleanupRelationship`, drop unhosted NPC parties [verified].
- About 650 `Dereferences/Performers/Deref*.cs` null out references to disposed objects, including `DerefAllRelationships`, `DerefRelationshipDictionary` [verified]. This is what stops "ghost" references from piling up, not size reduction.
- Our baseline load logged one ErrorTrap correction (`docs/05-baseline-results.md`).

### 2.5 NRaas Traveler (not installed)
- `TravelerSpace/Helpers/MiniSimsControl.cs` re-homes MiniSims from uninstalled worlds to `WorldName.UserCreated` and refreshes thumbnails; it **does not prune** MiniSims [verified].

### 2.6 Offline save cleaners
| Tool | What it removes | Source |
|---|---|---|
| Regul Save Cleaner (s3pi based, Windows/Mac, winget `Onebeld.RegulSaveCleaner`) | Checkboxes: old family portraits, Sim portraits, lot thumbnails, photos, generated images, memories (like Kuree's "Clear Memories"), experimental textures and unknown types. Author warns the game may fail to save after the experimental options; back up first. | https://modthesims.info/d/652440 [community] |
| Kuree's Save Cleaner (older, slower) | Memories and junk images; said to keep intentional photos and paintings | https://modthesims.info/t/525052 [community]; one user lost CC thumbnails with manual s3pe cleaning: https://forums.thesims.com/en_US/discussion/979586/file-bloat-help-on-restoring-a-couple-img-to-package-files [community] |

Player practice that matters as much as any mod [community: https://steamcommunity.com/app/47890/discussions/0/2425614539592304642, https://steamcommunity.com/app/47890/discussions/0/3829787744080973906]:
- Turn **memories off** (Options > General).
- Use **Save As** to a rotating set of names; avoid repeated Save and "Save and Quit". Error 12 on save is reported as running out of address space with a big save.
- Fresh save about 30 MB; a few generations without travel over 200 MB; each visited world adds 30 MB or more.

## 3. Game-side cleanup that already exists (all [verified])

| Routine | Trigger | Effect |
|---|---|---|
| `SimDescription.PurgeDistantGenealogyRelations()` → `Genealogy.PurgeDistantGenealogyRelations(roots)` | Daily, `Autonomy.OnceADayCallback` (`Sims3.Gameplay.Autonomy/Autonomy.cs` line 693); also console command (`Core/Commands.cs` line 686) | Breadth search from residents (depth 3) and dead in this world (depth 1); everything further gets `ClearAllGenealogyInformation()`, which also calls `MiniSimDescription.RemoveMSD` |
| `LifeEventManager.PurgeLifeEvents()` | Daily, same callback (line 704) | Clears life events and active nodes of Sims dead for 10+ days |
| `Relationship.DecayAll()` | Daily alarm `SocialComponent.RelationshipDecay` | Drops zero-liking Stranger/Acquaintance pairs after 1 day; NPC decay is 0.001x so NPC pairs rarely qualify |
| `SimDescription.OnWorldLoadFinished()` / `PostLoadFixUp()` | Every load | Disposes stray descriptions; removes relationships of invalid descriptions |
| `PostWorldLoadCleanUpPets`, `PostWorldLoadCleanUpSupernatural` | Load, only if the pack is missing | Removes pets / occult data |
| `Household.CleanupBadlySavedInventories`, `LotManager.CleanupObjectsNotOnLot` | **No caller found** in the decompiled scripts | Dead code, or called natively; worth a check before relying on them |

## 4. Recommendation

### 4a. Rely on existing mods (exact settings)
1. Overwatch defaults on: Clean up Relationships, Clean up Inventory, Clean up Genealogy, Recover Missing Sims, Instantation Check. Alarm Hour 5.
2. Overwatch **Cleanse The Dead: on** once the save has 3+ generations (keeps graves with living close family).
3. Memories **off** in game options for long saves; if already bloated, MasterController Remove Memories (apply all) or Regul Save Cleaner "memories" on a backup.
4. Regul Save Cleaner every few weeks on a copy: portraits, lot thumbnails, generated images. Photos only if the player agrees. Skip the experimental options.
5. Save As with rotating names.
6. If population grows past about 150 to 200 residents: NRaas StoryProgression with Maximum Residents set (default 150). Optional; it changes gameplay.

### 4b. Gaps our own cleanup could fill (benefit vs risk)
| Rank | Cleanup | Why nobody does it | Rule | Benefit | Risk |
|---|---|---|---|---|---|
| 1 | **NPC to NPC relationship pruning** in `Relationship.sAllRelationships` | Game decay is 0.001x for NPCs; Overwatch only repairs broken rows | Remove a pair when both Sims are not in the active household, `LTR.CurrentLTR` is Stranger or Acquaintance, abs(liking) below a threshold (for example 20), not blood related (`Genealogy.IsBloodRelated`), not same household, not `Occupation.HaveWorkingRelation`, not a genie wish relationship. Use `Relationship.RemoveRelationship(r)` so the game's own bookkeeping runs. | Medium to high: shrinks the largest Sim-pair structure and the daily `DecayAll` hitch [inferred, measure] | Low: mirrors the game's own `CanRelationshipDecayAway` rule with a looser liking threshold |
| 2 | **MiniSimDescription pruning** for other-world Sims | Overwatch removes only MSDs with no links at all | Remove when home world is not the current world, the MSD is not instantiated, no genealogy links, and the only mini relationships are to other pruned MSDs or to Sims with low liking; use `MiniSimDescription.RemoveMSD(id)` (it unlinks both sides) | Medium for players who travel a lot | Medium: travel return and cross-world family; test with WA/University/Into the Future trips |
| 3 | **Memory snapshot cap**: keep the newest N life events with snapshots per Sim, all for the active household | No mod caps; MC is manual all-or-type | Drop oldest `LifeEvent` entries of NPCs, keep events without snapshots | High on disk if memories stay on | Low (cosmetic). Releasing the image resource needs checking: dropping the key may leave the image in the save until a cleaner runs [inferred] |
| 4 | **Per-Sim history trimming for NPCs**: `ReadBookDataList`, `SpoiledGiftHistory`, completed opportunities of dead or non-resident Sims | Nobody | Clear for dead Sims and non-residents only | Low | Low |
| 5 | **Diagnostics only** for alarms and event listeners | Leaks need the owner fixed, not deletion | Count per owner type and report | Finds leaks | None (read only) |

Not worth doing ourselves: genealogy pruning (game does it daily), dead Sims (Overwatch Cleanse The Dead), inventories (Overwatch), images on disk (offline cleaner is safer than in-game deletion).

### 4c. Never delete
- Anything in the active household, or linked to it: its relationships, genealogy, MSDs of its relatives, its memories, its photos and paintings.
- Sims in households on a lot (`Household.LotHome != null`), role Sims and service pools (`Service.mPool`), tourists or travelers in transit (`GameStates.TravelerIds`, `GameStates.IsIdTravelling`), previous-traveler households (`Household.IsPreviousTravelerHousehold`).
- Playable ghosts, urnstones of Sims with living parents, children or siblings.
- Spouse, partner, parent/child, sibling relationships, romance or friend LTRs, and relationships with a working relation.
- Sims referenced by an active opportunity, situation, dream or quest (Into the Future descendants, University, World Adventures tombs).
- Never call `SimDescription.Dispose()` directly on a Sim with a `CreatedSim`; never edit `sAllRelationships` dictionaries without the matching reverse entry (use `Relationship.RemoveRelationship`).

### 4d. Measuring bloat with the harness
Per checkpoint (fresh load, then after 1, 7, 28 sim days), record:

| Metric | How |
|---|---|
| Save folder size, per file (`.nhd`, world `.package`, `TravelDB.package`) | File system after Save As to a new name |
| Bytes per resource type and group in the save `.package` (images `0x00B2D882` by group, portraits, thumbnails, SimDescription data) | Read the DBPF index with `readIndex` from `tools/extract-assemblies.ts`; group sums by type/group |
| Relationship pairs | `reflect_get` `Sims3.Gameplay.Socializing.Relationship.sAllRelationships.Count` (outer); total pairs need a sum over values, so add a small `reflect_call` helper or agent command that sums `Value.Count` |
| MiniSims | `reflect_get` `Sims3.Gameplay.CAS.MiniSimDescription.sMiniSims.Count` |
| Households / SimDescriptions | `reflect_get` `Sims3.Gameplay.CAS.Household.sHouseholdList.Count`; `reflect_call` `Sims3.Gameplay.CAS.Household.EverySimDescription` then `.Count` |
| Dead Sims | count of `Queries.GetObjects<Urnstone>()` (agent helper) |
| Life events and snapshots | sum over `Household.EverySimDescription()` of `LifeEventManager.mLifeEvents` counts and `HasSnapshot` (agent helper) |
| Alarms, listeners | `AlarmManager.Global.mTimers.Count`; `EventTracker.Instance.mListeners` total list length |
| Objects | `Queries.GetObjects<GameObject>().Length` and per-type top 20 |
| Time cost | Day-change hitch around `SocialComponent.RelationshipDecay` and `Autonomy.OnceADayCallback` (existing hitch log), save duration, load duration |

Plan: baseline a long save, then change one thing at a time (Overwatch Cleanse The Dead on, memories off, Regul cleaner, then each of our gaps) and compare against `docs/05-baseline-results.md`.

## 5. Open checks
- Confirm the installed Overwatch/ErrorTrap builds match the GitHub `master` source used here.
- Find who calls `Household.CleanupBadlySavedInventories` and `LotManager.CleanupObjectsNotOnLot` (native or not at all).
- Measure `sAllRelationships` pair count and `DecayAll` duration on a real long save before building rank 1.
- Check whether dropping a `LifeEvent.mSnapshot` key frees the image resource on next save.
