# 181. Story households: our own CAS households with traits, wishes and expected consequences

Written 2026-10-23 (static: game data read from `Game/Bin/Gameplay/GameplayData.package`, decompiled scripts in
`game-copy/decompiled`; no game run). Rodney: story runs should play our own households with their own personalities and traits
for more variety, and every trait must have both positive and negative consequences so a run shows both. Rules from docs/179;
the story engine is docs/171, story progression and household rotation docs/180. Ledger §302.

Sources: **[verified]** read in the game data or the decompiled scripts (class or method named, no code copied),
**[community]** TS3 player knowledge not read in the scripts, **[inferred]** our reading.

## 1. Research: traits, wishes and favourites

**Where the data lives [verified].** `TraitManager.CreateTraitTable` reads the XmlDbData table `Traits` (resource type
`0x0333406C`, instance FNV-64 of the name, in `GameplayData.package`; the Steam install keeps every pack's rows there) plus
`traits_store` (store traits, not on this install's game data: `Unforgiving`, `Pyromaniac`, `NaturalPerformer` and others that
EA's own CAS personas and wishes name; left out). Columns used: `Hex` (the `TraitNames` id the agent parses; differs from `Name`
for `XFactor`, `NightOwlTrait`, `SupernaturalFanTrait`, `BroodingTrait`, `GathererTrait`), `CodeVersion` (pack), `Category`
(`TraitGroup`), `AgeSpeciesVisible`, `SetNumbers`, `Points`, `RandomWeight`, autonomy commodities.

| Rule | Value | Source |
|---|---|---|
| Choosable in CAS | `Trait.IsVisible`: group Intellect/Social/Lifestyle/Physical and the pack installed; Reward* groups are lifetime rewards, group None is hidden (occult markers, career, store) | [verified `Trait.IsVisible`/`IsReward`/`IsHidden`, `TraitGroup`] |
| Counts (332 rows) | 99 CAS human, 31 CAS pet, 131 reward, 71 hidden | [verified Traits XML] |
| Per pack (CAS human) | base 64, WA 3, Ambitions 6, Late Night 2, Generations 2, Pets 4, Showtime 3, Supernatural 6, Seasons 2, University 3, Island Paradise 2, Into the Future 2 | [verified `CodeVersion`] |
| Ages | `AgeSpeciesVisible` codes B (Baby) P (Toddler) C T Y A E; pets `Cats`/`AllDogs`/`Horses`. CAS pools: Toddler 28, Child 74, Teen and YA+ 99 | [verified] |
| Slots per age | Toddler (and Baby) 2, Child 3, Teen 4, YA/Adult/Elder 5 (+1 University graduate, +1 social group) | [verified `TraitManager.NumTraitsForAge`, tunables `kTraitsFor*`] |
| Conflicts | two traits conflict when they share a `SetNumbers` value; 53 CAS human pairs (Neat/Slob, Good/Evil, Athletic/CouchPotato, Grumpy/HotHeaded, Loner/PartyAnimal, Lucky/Unlucky, AnimalLover/CatPerson/DogPerson/Equestrian, ...) | [verified `TraitManager.DoTraitsConflict`] |
| EA's valence | `Points` > 0 for 30 traits (an upside in EA's own scoring), < 0 for 26, 0 or empty for 43 (most pack traits) | [verified] |
| Random fill | the game's random Sims and descendants pick traits weighted by `RandomWeight` (we use the same weights); `TraitManager.AddRandomTrait` (in-play) picks uniformly | [verified `Genetics.AddWeights` callers, `AddRandomTrait`] |
| EA's CAS personas | table `Personas`: 36 five-trait sets (Airhead, Bully, Hermit, Villain, ...), some with store traits | [verified `CASLogic.CreatePersonasList`] |

**Lifetime wishes [verified].** The CAS list comes from the children of 10 feeder dream trees
(`DreamsAndPromisesManager.kCasFeederTreeIds`; base game, WA, Ambitions, Late Night, Pets, Showtime, Supernatural, University,
Island Paradise, Into the Future: 86 wishes). Each wish lists traits as required, excluded or matching; CAS drops a wish when a
required trait is missing or an excluded one present, ranks the rest by matches (a required trait counts twice) and shows the top
`kMaxMajorWishMatches` (`GetTopMajorDreamMatches`, `DreamNodeInstance.GetNumTraitMatchesForMajorDream`). A Sim gets a lifetime wish
only as Young Adult or older (`DreamsAndPromisesManager` YA+ checks). Seven wishes need an occult hidden trait (Zombie Master,
vampire, fairy, witch, werewolf); the generator skips them (occults are separate stories). Data: `docs/data/lifetime-wishes.csv`.

**Favourites, zodiac, voice [verified].** `FavoriteFoodType` (53 foods; Vegetarian moves the favourite to a meat-free food,
`CASLogic.ChangeFavoriteFoodToVegetarian`), `FavoriteMusicType` (27 stations), favourite colour = one of 20 named
`CASCharacter.kColors`. Zodiac: 12 signs, set in CAS (`SetSignOperation`, random on Randomize); in play only fire/air vs
earth/water matters (`SimDescription.HasCompatibleSign`, used by social scoring). Voice: `VoiceVariationType` A/B/C plus a pitch.
Starting funds of a CAS household: 15,000 + 5,000 per YA/Adult/Elder, 3,000 Teen, 2,000 Child, 1,000 Toddler, 500 Baby, 250
per dog or cat (`CASLogic.GetHouseholdStartingFunds`, tunables).

**What each trait does in play.** `docs/data/traits.csv` gives every CAS human trait a `positive` and a `negative` consequence
(our words, [community] TS3 knowledge) and a `refs` column with up to four script classes that test the trait (the place the
effect lives, from a scan of `TraitNames.<id>` uses in the decompiled scripts [verified that the class tests the trait, not the
exact effect]). Examples: Kleptomaniac + swipes objects for fun and money / - relationship hit and arrest risk if caught
(`SwipeSomethingAutonomous`, `Burglar`); Hot-Headed + wins fights / - fights and Furious (`BarBrawlSituation`, `MoodManager`);
Neat vs Slob (`CleanableComponent`, `BuffDisgusted`, `BuffSmelly`); Clumsy (`FirefighterPole`, `Hoverboard`, `IceCreamItem`);
Unlucky (`BuffFeelingUnlucky`, `UpgradableComponent`); Natural Cook never starts a cooking fire (docs/179 R-LIFE-7).
`story_persona` maps a trait to a docs/171 story persona (PartyAnimal -> party-animal, Workaholic -> career-climber, ...).

## 2. Households (`tools/harness/story-households.ts`, `tools/harness/stories/households/*.json`)

- **Shape:** 2..8 humans, at least one YA+, no Baby (CAS makes Toddler and older), no pets (they join by adoption in play);
  random structures: couple, couple with kids, single parent, multi-generation, roommates, siblings; parents are YA+ and older
  than their children (Teen children have an Adult parent); spouses, partners and roommates YA+ (R-LIFE-2); every Toddler, Child
  and Teen has a parent or sibling in the household.
- **Each Sim:** traits for its age and slot count, no conflicting pair, preferred traits first, the rest by `RandomWeight`; a
  lifetime wish (YA+) from the game's own top-match group; food (meat-free for vegetarians), music, colour, zodiac, voice; a
  story persona; a persona line ("Upside ... Downside ...") and `expect`: each trait's positive and negative consequence.
- **Balance rule:** every Sim with 3+ traits has a trait with EA Points > 0 and one with Points < 0, and 20..65 % of the
  household's scored traits are downsides (no house of saints, no house of pure chaos).
- **Named archetypes:** `chaotic-artists` (3 artist roommates), `strict-overachievers` (ambitious parents, gifted kids),
  `feuding-family` (hot-headed father, mean grandmother, kind kids), `lonely-genius` (reclusive YA genius and an elderly father),
  `party-house` (4 party roommates), `full-house` (8 Sims, the R-HH-1 cap); plus `random-101..106` (seed 1). Rebuild with
  `--write --seed N --random K`; `--show <id>` prints the persona lines.
- **Lint** (`--lint`, also in the self-check): R-HH-1 caps (8 humans / 6 pets / 10), pet ages Child/Adult/Elder, CAS ages, slot
  counts, CAS-visible traits for the age, conflicts, consequences present, wish only YA+ and valid for the traits, favourites are
  game values, relations by age, the balance rule, funds = CAS formula.
- **Tiers above 8:** dropped (2026-10-23, coordinator): NRaas MasterController is not installed and its "Allow More Than Eight"
  lifts the cap only for its own commands; more samples come from household rotation (docs/180 `days.rotate`).

## 3. Creation the player's way (`make_household`, `mods/ts3agent/.../HouseholdHandlers.cs`)

The player: Edit Town > create a household in CAS > place it on a vacant lot (pay the lot) > play it. The agent does the same with
the game's calls, Live mode only (Edit Town pauses the clock; the whole build is one call on the sim thread) and refused where Edit
Town is refused (`InWorldSubState.IsEditTownValid`, R-MODE-5):

1. Checks first, nothing made yet: 2..8 humans, no Baby, a YA+; each trait CAS-visible (`Trait.IsVisible`), for the age
   (`AgeSpeciesAvailabiltyFlag`), no conflict (`DoTraitsConflict`), not over the slots; the wish known
   (`DreamsAndPromisesManager.GetMajorDream`) and YA+.
2. Sims: `SimUtils.SimCreationSpec.Instantiate` (a child of a man and a woman in the household gets the game's genetics,
   `Genetics.MakeDescendant`), CAS trait slots (`TraitManager.RemoveAllElements` + `AddElement`), `LifetimeWish`, favourite
   food/music/colour, zodiac, voice, `Household.Add`.
3. Family: `Genealogy.AddChild` / `AddSibling` / `Marry`, then the CAS relationship step (the `CASLogic.LTRForceChangeState`
   sequence: partner, first kiss for romance, forced LTR state, liking in the CAS default range): Spouse, Partner, Friend.
4. Money and lot: `CASLogic.GetHouseholdStartingFunds`; lot given or picked as in §3.1; `BinCommon.PayForLot` (furnished or
   not), `Lot.MakeLotUnfurnished` when unfurnished, `BinCommon.CreateAndMoveIn` (internal class: called by reflection). Any
   failure before the move disposes what was made. `dry: true` runs every check and the lot pick, then discards.

### 3.1 Move-in the player's way (2026-10-23, after the first live run)

The first live run (`story-days-gen-2026-10-23-bp-story-days-fullhouse`) refused full-house (8 Sims, 26,000): "no vacant
residential lot with 8 beds and a fridge for 26000 simoleons (furnished)", and the run went on silently with the save's
household (25 min lost).

- **The game's rule [verified]:** the player's move-in (new game `PlayFlowModel.PlayBinHousehold` ->
  `BinCommon.PlaceFromExportBin`, Edit Town `EditTownPlaceTool.PlaceHousehold`) checks only the price, `BinCommon.PayForLot`:
  funds >= `Lot.Cost` furnished or `Lot.GetUnfurnishedCost` unfurnished (the furniture that does not stay after an eviction is
  deducted, then `MakeLotUnfurnished` removes it). No bed or fridge test: `Lot.CanSupportPopulation` (a fridge and a bed spot
  per Sim) is used only by the in-game Move dialog for the active household and by story progression moves. Beds count by
  `IBed.NumberOfSpots`, so a double bed is 2 (closes I-181-1's open question). Ledger §305.
- **Our choice (player-like, not a game rule):** before any story the household lives somewhere livable: a bed spot per
  non-Toddler Sim (a double per spouse/partner pair), a crib per Toddler, a fridge, a toilet, a shower or tub, a stove.
- **Lot pick (`make_household`):** vacant residential houses only (no apartment, dorm or empty lot). First the most expensive
  furnished lot that already has every essential; else the most expensive house whose price (furnished if the funds buy it,
  else unfurnished, whichever leaves the smaller total) plus the missing essentials fits the funds. Missing essentials = what the
  lot keeps (all objects furnished; what `MakeLotUnfurnished` keeps unfurnished) vs the list above. Products: the cheapest
  catalog product per essential, classified by the class of the object the game creates for it (`BedDouble`, `BedSingle`,
  `ICrib`, `Fridge`, `IToilet`, `IShower`/`IBathtub`, `Stove`; bot beds and bunk beds left out). The answer carries
  `furnished`, `lot_cost` (the price paid), `buy` (need, product, name, price) and `buy_cost`. None fits -> refused with
  `funds: ...` and the cheapest option.
- **Buying (`createHousehold`):** after `switch_household`, Live, `buy_open`, one `buy_object {product, on_lot: true}` per row
  (household funds; a free spot from the lot centre on the lot, never the inventory), `buy_close`; the `household` line logs
  `furnished`, `bought`, `funds_after`.
- **Fail fast:** with `household` not `save`, any failure throws `household creation failed: <reason>`; `loadStory` rethrows it
  and `profile-world.ts` prints `FAIL:` and exits 1 (the queue stops the job). `generated`: a `funds:` refusal first tries the
  next smaller household file; a named household fails.
- **Bad products (chain156, 2026-10-23):** full-house failed in `make_household` itself: classifying the Kids > Misc
  products created the Generations stroller (`319E4F1D:38000000:98A509`, `StrollerCheap`, in Buy mode's list) and the game
  returned no object (guid 0 = `ObjectManagerFailure`: made, refused by the object manager, destroyed; cause open), which threw
  out of `Essentials()`. Now: products come only from `Catalog()` (Buy mode's own `GetObjectProductListFiltered` list,
  `ShowInCatalog`, a buy category: CC only when Buy mode shows it); each create is caught and skipped (first 3 failures go into
  a `no <kind> in the catalog` refusal); up to 3 products per kind, cheapest first, and each `buy` row carries `alternates`;
  `createHousehold` buys with `buy_object {product, on_lot, kind}` (the created object must classify as that kind) and on any
  failure tries the next alternate, logging `bought[].failed`; the household fails only when no product of a kind can be
  bought. An incomplete essentials set is rebuilt at the next `make_household`. Ledger §306.
- **Cribs and placement (chain156 crib, chain157 beds, 2026-10-23):** chain156 found no crib: no Beds or Kids > Misc product
  created an `ICrib`, so Buy mode sorts cribs elsewhere, Kids > Furniture [inferred, I-181-10]. Products are now pre-sorted by
  their own data (`KindsFor`): Beds -> beds, Kids > Furniture (or Kids + Beds) -> crib, appliances -> fridge/stove, plumbing ->
  toilet/shower, Kids > Misc (strollers) never; only products whose group still lacks alternates are created, and the created
  class must be one of the product's kinds. chain157 failed `buy bed_double` with "no free spot" for 3 beds
  (`FindGoodLocationNearby` from the lot centre). Now both the buy and a pre-purchase fit test use the game's own in-house
  placement, `GlobalFunctions.PlaceObjectOnLot`'s loop (`Lot.GetRandomPosition` inside, `World.FindGoodLocation` with
  `StayInRoom`) [verified code path, I-181-11]: `make_household` places the plan's products one after another on the candidate
  (unfurnished: what `MakeLotUnfurnished` removes is ignored), destroys them, and on a misfit takes the next affordable lot (up
  to 8; `fit_skipped`); none fits -> `fit: ...` refusal, which `generated` treats like `funds:` (next smaller household). What
  the lot already has is counted by the same class checks (bed spots, `ICrib`, `EssentialKind`), so a furnished lot buys only
  what is missing.
- **`story.householdFunds` (2026-10-23):** optional money for the made household (default: the CAS formula), applied the way a
  player does it: the familyfunds cheat's effect (`Household.SetFamilyFunds`) after creation, before the lot pick, so the lot and
  the essentials use it. `make_household {funds}`; the `household` line logs `realism: cheat`, `funds_cheat`, `cas_funds`.
  `parseConfig` allows it only with a generated or named household and a `realism` list that includes `cheat`; `realism.ts`
  labels `make_household {funds}` cheat. `story-days-gen.json`: 30000 [I-181-12].
- Shortcut: random start positions inside rooms, not by room type (a fridge may stand in a bedroom); upgrade when a story needs
  room-correct placement. Open: live check (I-181-6, I-181-10, I-181-11). Ledger §307.

CSV row `make_household` (natural, live gate). Story config `household`: `save` (default until verified live) plays the save's
active household; `generated` picks a seeded household from `stories/households`, `<id>` a named one. `story-fuzzer.ts loadStory`
calls `createHousehold` before the stories: `make_household`, then `switch_household` (docs/180) to it, one `household` log line
(every Sim's traits, wish, persona, expected consequences); the household's Sims get their story persona (`personaOf`).

## 4. Report hook

`story-report.ts` adds "Household and traits": per Sim the traits, lifetime wish, stories run as actor, rescues, deaths, hitches
>= 300 ms in its story steps, and each trait's expected upside / downside, so consequences can be read against what happened.

## 5. Checks and open items

Checks (no game run): `story-households.ts --self-check` (99 CAS traits all with both consequences, known conflicts, toddler pool,
wish required/excluded, funds formula, 6 archetypes + 40 random seeds lint-clean, seeded, the lint catches conflicts / slot count
/ Baby / early wish / 9 humans / pet Toddler / teen spouse / non-CAS trait / a no-downside Sim, the report row, the
`createHousehold` hook against a stub agent including a refused call), `--lint` (12 files), `story-fuzzer.ts` self-check and
`--check`, `story-lint.ts`, `realism.ts`, `story-report.ts`, `story-day.ts`, `queue.ts` self-checks; agent built in the worktree (`build/ts3agent.package`, not run in game).

Open: not run in game (`make_household` dry then real on a scratch save, then a story-days run with `household: generated`);
whether `Lot.CanSupportPopulation` counts a double bed as two (I-181-1); trait effects marked [community] (I-181-2); store
traits are not on this install (I-181-3).
