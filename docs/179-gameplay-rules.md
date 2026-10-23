# 179. Gameplay rules: what a player can do, and what our tests may do

Written 2026-10-23 (static only: decompiled scripts read in `game-copy/decompiled`, no game run). Rodney: our tests must follow what a
real player can do. The trigger was a Sim made while the game was in Build mode (crash `0x575651`). No player can do that: Sims are
created only in live play (birth, adoption, Edit Town) or in Create A Sim, and objects only in live play or through Buy/Build mode.

Order of work (Rodney): §1 rules from the player's side, §2 story constraints derived from them, §3 command table and enforcement.
Sources: **[verified]** = read in the decompiled scripts (type.method named, no code copied), **[community]** = TS3 player knowledge
(wiki, forums) not read in the scripts, **[inferred]** = our reading, still to confirm. Rule ids (`R-...`) are used by
`docs/data/gameplay-preconditions.csv`.

## 1. Gameplay rules (player's view)

### Game modes

| Id | Rule | Source |
|---|---|---|
| R-MODE-1 | In-world sub-states: LiveMode, BuildMode, BuyMode, the CAS family (CASFullMode, CASDresserMode, CASMirrorMode, CASTattooMode, CASStylistMode, CASTackMode, CASCollarMode, CASSurgeryFace/BodyMode, CASMermaidMode, CABotMode, CABEditMode), ShoppingMode, CareerSelectionMode, EditTown, PlayFlow, BlueprintMode. Main menu = no world. | [verified: `InWorldState.SubState`] |
| R-MODE-2 | Build, Buy and CAS stop the clock (game speed locked at Pause until exit); Edit Town pauses too. Nothing simulates while a player is in them, and the player can give no Sim an order there. | [verified: `LotManager.EnterBuildBuyMode`, `CASModeState`, `EditTownState`] |
| R-MODE-3 | The Build/Buy button opens the **active Sim's home lot** wherever the Sim is; it never opens a community lot or a lot the household does not own (other ways in: Build/Buy on this lot (cheat), resort front desk, interior designer, show stage). Disabled only on a vacation base camp home. Entry refused during a fire, a burglar, the repo man, a move in progress or a meteor (unless the always-allow cheat). | [verified: `LotManager.GetBuildBuyLot`, `InWorldState.CanEnableBuildBuy`, `InWorldSubState.IsBuildBuyValid`] |
| R-MODE-4 | Speed: pause, 1, 2, 3; the game offers Skip only while the whole household sleeps or is away. | [community] |
| R-MODE-5 | Edit Town: move, evict, split households, place/bulldoze lots, change lot type, lock lots, CAS a household. Refused during a move, a death (reaper), a social worker visit, an age-up, a UFO, travel, or whenever Build/Buy would be refused. Changing the active household is disabled on vacation, at university and in the future. | [verified: `EditTownModel`, `InWorldSubState.IsEditTownValid`, `EditTownPuck`] |
| R-MODE-6 | Map view is a camera mode (about 1 s toggle), not a game state. | [verified: `CameraController.EnableCameraMapView`] |

### Households

| Id | Rule | Source |
|---|---|---|
| R-HH-1 | At most 8 humans, 6 pets, 10 in total; unborn babies count. Deer and raccoons never join. Twins (5%) and triplets (1%) are clamped to the free space. | [verified: `Household.CanAddSpeciesToHousehold`, `Pregnancy.GetNumForBirth`] |
| R-HH-2 | New members come from birth, adoption (phone services: Baby, Toddler, Child; adopter YA+, household not full), pet adoption (phone, computer, newspaper), strays, Ask to Move In (liking >= 50, not at University), marriage (move-in), or Edit Town CAS. Moving (the in-game Move dialog, story progression) needs a lot with a fridge and a bed spot per Sim; moving a new household in (new game, Edit Town) checks only the price, furnished or unfurnished (docs/181 §3.1). | [verified: `SocialWorkerAdoptionSituation`, `PetAdoption.CanAddPetInHouse`, `SocialTest.TestAskToMoveIn`, `Lot.CanSupportPopulation`, `GameplayMovingModel.IsLotValid`, `BinCommon.PayForLot`] |
| R-HH-3 | CAS can make Toddler and older (humans) and puppy/kitten/foal and older (pets); a Baby only comes from birth or adoption. | [community] |
| R-HH-4 | A player directs only the Sims of the active household (portraits); other Sims act by autonomy, invitation or story progression. | [community] |
| R-HH-5 | Money moves by play (jobs, sales, bills) or by cheats `kaching` (1000), `motherlode` (50000), `familyFunds`. Moving money caps at 50000 (100000 rich). | [verified: `Commands` cheat table, `Cheats.kKachingAmount`, `kMotherloadAmount`; `GameplayMovingModel`] |

### Life cycle

| Id | Rule | Source |
|---|---|---|
| R-LIFE-1 | Days per stage (normal): Baby 3, Toddler 7, Child 14, Teen 14, YA 14, Adult 35, Elder >= 20; lifespan setting short..epic (Options, any time). Cake age-up for every age but Elder (aging on, or Forever Young); without a cake the birthday fires between 13:00 and 22:00. Elders die of old age by a daily chance (normal 9%). | [verified: `AgingManager`, `Cake.CanSimAgeUp`] |
| R-LIFE-2 | WooHoo and Try for Baby: YA, Adult, Elder, needs a first kiss and liking > 30. Romance: Teen only with Teen. Driving: Teen+ (with Generations: YA+ or a Teen with lessons). Marriage: no age test in script (social data). | [verified: `Sim.CanGetRomantic`, `SimDescription.CanDrive`; marriage ages: community, YA+] |
| R-LIFE-3 | Try for Baby: one man and one woman; the woman not Elder, not already pregnant, has a home lot (being on it is not checked), household room counting the baby. Refused at University, for Grim Reaper, Mummy, Robot and a lamp-bound Genie. Pregnancy: bump at 24 h, contractions (hospital) at 70 h, birth at 72 h. | [verified: `Pregnancy.CanTryForBaby`/`CanGetPreggers`, `Pregnancy` tunables] |
| R-LIFE-4 | A pregnant Sim may travel on vacation until she shows (24 h); University refuses any pregnancy; work is skipped once she shows. | [verified: `TravelUtil.CanMoveWorldsInternal`, `Occupation`] |
| R-LIFE-5 | Pets: young, Adult, Elder; adoption by phone/computer/newspaper, strays, pet pregnancy. | [verified: `PetAdoption`, `StrayPets`] |
| R-LIFE-6 | Death types the game has: old age, drowning, starving, electrocution, burning, mummy curse, meteor, thirst, freezing, shark, scuba drown, jetpack, haunting curse, jelly bean, transmuted and more (27). No vanilla kill command. | [verified: `SimDescription.DeathType`; no kill: community] |
| R-LIFE-7 | Fires start from play: cooking failure (25% with no Cooking skill, Natural Cook immune), overcooking, birthday cake (2%), fireplace, bonfire, leaf pile, fireworks, failed tinkering, science, solar panel, windmill, meteor, spicy food, wand, shock (5%). Firefighters come through a smoke detector or a phone call. | [verified: `FireManager.AddFire` callers, `Food.CheckForRandomFailure`, `FireManager.CheckForSmokeDetector`] |

### Daily life

| Id | Rule | Source |
|---|---|---|
| R-DAY-1 | Careers YA+; part-time jobs (and the mausoleum) Teen+; join through the rabbit hole, newspaper or computer; work hours from career data; school for Child and Teen. | [verified: `Career.CareerAgeTest`, `PartTimeJob`, `CareerManager`] |
| R-DAY-2 | Needs fall with time; players feed, rest and clean their Sims by clicking objects, or let autonomy do it. Changing a need bar directly is a cheat. | [community] |
| R-DAY-3 | Skills rise by use (objects, classes, books); no vanilla command sets a level. | [community] |
| R-DAY-4 | Cooking ages (Teen+ for full meals): not in script (interaction data); toddlers and babies are fed. | [community] |

### World and lots

| Id | Rule | Source |
|---|---|---|
| R-WORLD-1 | Community lots (venue types), rabbit holes and homes; a household builds and buys only on its own home lot (R-MODE-3). | [verified: `LotManager.GetBuildBuyLot`] |
| R-WORLD-2 | Players visit lots with Go Here / Visit, by car, taxi or bike; townies go by autonomy and story progression. | [community] |

### EP systems and occults

| Id | Rule | Source |
|---|---|---|
| R-OCC-1 | How a player gets each occult without cheats. CAS (new Sim): Vampire (Late Night, Supernatural), Werewolf, Witch, Fairy, Ghost (Supernatural), Genie (Showtime), Mermaid (Island Paradise), Plumbot (Into the Future, bot workshop). In play: Vampire bite and Werewolf bite (Teen+; younger Sims turn at age-up), Mermaid kelp, Genie lamp, Mummy (sleep a day in the cursed sarcophagus, World Adventures), Ghost (death; playable via science lab or philosopher's stone), Zombie (full-moon buff, wand), PlantSim (forbidden fruit, **University Life**, not Seasons), Imaginary Friend (doll, Generations), SimBot (inventing; always Elder), Plumbot (Adult). Witches and fairies only by CAS, birth or story progression. | [verified: `OccultVampire.CanBecomeVampire`, `BuffOnceBittenTwiceShy`, `MermadicKelp`, `GenieLamp`, `SarcophagusOfTheKingsCursed`, `Urnstone.GhostToPlayableGhost`, `LunarCycleManager`, `OccultPlantSim` (EP9), `ImaginaryDoll`, `OccultFrankenstein`, `OccultRobot.CanBecomeRobot`; CAS list: community] |
| R-EP-2 | Full-moon zombies come from a nightly roll (Supernatural), two per night picked from townies. | [verified: `LunarCycleManager` (docs/117)] |
| R-EP-3 | Season and weather follow the calendar; season lengths are an option; a player changes weather only with the Weather Stone or the weather machine reward (Seasons). | [verified: `WeatherStone.SummonWeather`, `WeatherMachine`] |
| R-TRAV-1 | Vacation (World Adventures) and the future (Into the Future): adults lead; children and teens go with an adult; not visibly pregnant. Build/Buy works at a vacation home (not a base camp). | [verified: R-LIFE-4, R-MODE-3; ages: community] |
| R-TRAV-2 | University: YA+ students only; no pregnancy; no Try for Baby, Ask to Move In or marriage there. | [verified: `TravelUtil` University branch, `Pregnancy`, `SocialTest`; YA+: community] |

### Cheat-only and volume

| Id | Rule | Source |
|---|---|---|
| R-CHEAT-1 | Cheats a player can use: `testingcheatsenabled` (motive drag, debug interactions such as Edit in CAS (not Baby or pregnant), Trigger Age Transition, add occult, have baby), `kaching`/`motherlode`/`familyFunds`, `moveobjects`, `freeRealEstate`, `buydebug`. Mods (MasterController, NRaas) add skill, relationship and kill commands. | [verified: `Sim.AddCheatInteractions`, `Sim.EditSimInCAS`, `Commands`; mod list: community] |
| R-VOL-1 | One click = one order for one Sim: no player sends 30 Sims to one spot in one tick, changes 6 outfits at once, or jumps the camera across 8 lots in a few seconds. Such steps are stress tests. | [inferred] |

## 2. Story constraints (derived)

1. **Mode.** Every Sim or object action runs in Live mode (R-MODE-2). Buying, selling and moving objects is Buy mode or our live
   stand-in for it, on the active household's home lot only (R-MODE-3). Between-story Build mode holds every story call and every
   prepare until the game is back in Live (the crash case). A story opens Build/Buy/CAS only as its own scene and closes it again.
2. **Who acts.** Steps are ordered for Sims of the active household (R-HH-4). Townies are targets and act by autonomy; a template
   in which a townie role acts is a stress test.
3. **New Sims.** A new Sim is a CAS creation (Toddler+), an adoption (Baby, Toddler, Child; household room; adult YA+) or a birth;
   always in Live mode, never past 8 humans / 6 pets / 10 (R-HH-1..3). A new Sim with a CAS occult is natural; turning an existing
   Sim instantly is a cheat (R-OCC-1).
4. **Order.** Romance before WooHoo, engagement before a wedding, a cake before Blow Out Candles, and a cake ages once: no
   `age_up` after Blow Out Candles (R-LIFE-1). Travel only YA+ leaders, University only YA+ (R-TRAV-1/2).
5. **Realism label.** Each template declares `realism`: `natural` (a player could do every step without cheats), `cheat` (at
   least one step is a cheat a player can type, or a forced jump such as pregnancy timers, motive bars, relationship values),
   `stress` (volume or speed no player produces, or no player equivalent at all). The label is computed from the CSV and may only
   be raised, never lowered. Runs take `natural` and `cheat` by default; `stress` only with config `realism: [..., "stress"]`.

## 3. Command table and enforcement

- **Table:** `docs/data/gameplay-preconditions.csv`, one row per agent command (150, all registered ones): valid `states`, the
  `player_action` it stands for, `realism` (natural / cheat / stress / harness / cas), `preconditions`, `source` (handler + rule
  ids), `gate` (what the agent enforces now).
- **Labels:** `tools/harness/realism.ts` reads the CSV; `--repros` labels every repro (worst step; `free: true` and career
  promotion raise a step to cheat); `--self-check` asserts every registered command has a row and no extra rows, the `gate`
  column equals the agent's gates, fixtures (the build-mode `make_sim`), and that no repro sends a step in a mode its row does
  not allow. Repro labels: `docs/data/repro-realism.csv` (148: natural 34, cheat 56, stress 58).
- **Lint** (`story-lint.ts`, run by `story-fuzzer.ts loadContent`, so `--check`, `--dry-run` and `profile-world --story` refuse
  violations): `realism` declared and not below the computed label; step modes; townie actors; travel ages; cake + `age_up`;
  dry-run rule `lintModes`: no step or prepare starts inside a between-story Build-mode window. PlantSim EP fixed to UniversityLife.
- **Agent gates** (`mods/ts3agent/src/plugin/Handlers/GateHandlers.cs`, registered last in `RegisterAll`): 31 commands that had no
  mode check now refuse with `not available: <state> (LiveModeState only)` (Sim orders, motive/skill/trait/buff/relationship/career
  /age edits, strands, travel, camera jumps, VFX) or `(LiveModeState or BuyModeState only)` (buy, sell, move, place, replace
  object). `RequireLive` (38 handlers already gated, incl. `make_sim`, `spawn_sims`, `make_occult`, `pregnancy_advance`,
  `start_fire`) now says the same `not available: <state> (LiveModeState only)`. 69 commands gated in all. Built in the worktree
  (not run in game).

## 4. Audit findings (2026-10-23)

**Templates (109).** Fixed: `life-age-up-cake` and `social-birthday` aged a Sim twice when the cake worked (cake step dropped from
the first; `age_up` dropped from the second, Bake Birthday Cake now required); `social-chat` and `chaos-brawl-crowd` gave orders to
a townie (now the driven Sim acts); `occult-plantsim` needed Seasons (EP9 University Life); `travel-university` allowed a Teen;
cheat scene setup dropped where a player needs none: `set_motive` before Quick Meal, Group Meal and Shower, `household_funds` before
books, groceries, classes, adoption and bills, `household_funds` + `spawn_sims` around a house party. Labels now: 56 natural,
45 cheat, 8 stress. Cheat by design: 10 social templates set relationship values (`modify_relationship`), 3 pregnancy jumps, 7
instant occults on existing Sims, `set_skill` (3), motive bars (sleep, autonomy hour, school, drown, starve), career join/promote,
`test_env` weather (2), `start_fire` (2), `kill_sim`, a guessed burglar cheat, `age_up` (2). Stress (skipped by default):
`build-mode-flip` and the two sea-crowd stories (`spawn_sims`), `cas-outfit-swaps`, `chaos-vfx-swarm`, two zombie spawns,
`world-strand-stuck`.

**Engine (story-fuzzer.ts).** (a) Driven Sims include seeded townies (docs/171 §7): a player directs only the active household
(R-HH-4); fixed in docs/171 §16: config `drive` defaults to `household`, `world` is the stress mode. (b) Object prepares called
`buy_object {free: true}` in Live mode: a Buy-mode stand-in without paying (cheat); fixed in docs/171 §16 (Buy mode, paid). (c) Between-story Build mode: the dry-run rule
finds no step or prepare in a Build window on the fake game (two configs, 40 windows); in game the build-mode `make_sim` crash
is the other agent's fix (StoryHandlers / story-fuzzer). (d) Travel steps sent the active Sim, not the role; fixed in docs/171 §16.

**Repros (148).** No step is sent in a mode its command does not allow (only `place_objects`/`remove_objects` in Live: a stress
stand-in for Buy mode, labelled stress). 34 natural, 56 cheat (`reflect_call` triggers 37, `test_env` 9, `place_sim` 5, ...),
58 stress (`spawn_sims` 44, `crowd_go` 27, `place_objects` 10, ...). Repros are forced by design (docs/169); the label says which.

## 5. Open

- Not run in game: gates, lint changes, the rewritten templates. Next story run: read the `not available` counts.
- Rules marked [community] (CAS occult list, Baby not in CAS, marriage YA+, University YA+, cooking Teen+, active household only)
  are ledger items I-179-1..6 (docs/51 §296).
- Done in docs/171 §16 (ledger §298): `drive: household` (default), paid Buy-mode prepares, travel by role, `envGuard: false` for
  story runs (seasons pass), old age as natural death. Next toward natural-only runs: the remaining cheat setup steps (§4).
- Traits and households: our own CAS households (traits, lifetime wishes, favourites, expected consequences, `make_household`)
  are in docs/181.
