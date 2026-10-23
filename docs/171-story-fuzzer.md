# 171. Story fuzzer: seeded parallel stories that find hitches players hit

Written 2026-10-23. **Contract for the build (§1-§5); the agents fill §6+.** Goal (Rodney): find hitches we have no repro for yet by
playing the game the way players do, many Sims at once, normal and ridiculous, across ages and generations, while the measurement
tool (docs/161, 162, 166, 167) records every real hitch with its cause. Scripted and seeded, no model in the loop: a run costs ~0 tokens
and any found hitch replays from its seed and turns into a forced repro (docs/169).

## 1. Pieces and owners

| Piece | Files (owner only) | What |
|---|---|---|
| A game commands | `mods/ts3agent/src/plugin/Handlers/StoryHandlers.cs` | the new agent commands of §3 |
| B engine | `tools/harness/story-fuzzer.ts`, `tools/harness/story-census.ts` | census (§4), scheduler, actor tracking, prepare actions, `story.jsonl` log (§5), profile-world `--story` hook |
| C content | `tools/harness/stories/*.json` | personas, category weights, templates (§2), per-world weight files |
| D replay and reports | `tools/harness/story-report.ts`, small hook in `freeze-report.ts` | hitch-to-story join, seed replay, repro draft |

## 2. Template schema (`tools/harness/stories/templates/*.json`, one array per file)

```
{ "id": "kitchen-fire", "category": "normal" | "lifestage" | "chaos" | "occult" | "world",
  "weight": 1.0, "actors": [{ "role": "cook", "age": ["YoungAdult","Adult"], "occult": null, "pick": "driven" | "townie" | "new" }],
  "needs": [{ "kind": <§4 kind>, "value": "...", "prepare": true }],
  "steps": [{ "cmd": <agent command>, "args": {...}, "actor": "cook", "target": { "find": <§4 kind>, "value": "..." },
              "waitS": 20, "waitUntil": {...}, "optional": true }],
  "cooldownMin": 30, "maxConcurrent": 1, "note": "..." }
```
`{"cmd": "interact", "actor": "x", "target": ..., "label": "Read"}` runs `do_interaction` with the pie-menu label; `{"cmd": "any_interaction",
"actor": "x", "target": ...}` picks a random enabled entry from `list_interactions` (seeded). Step args may use `$actor.<role>`, `$target`, `$lot`.

## 3. New agent commands (A)

| Command | Args | Does (game's own calls; document the call in the handler) |
|---|---|---|
| `make_occult` | `{sim, type: Vampire|Werewolf|Fairy|Witch|Genie|Mummy|Ghost|Zombie|Mermaid|PlantSim|ImaginaryFriend|Robot}` | the game's own transform path per occult; refuse cleanly when the EP is absent |
| `start_fire` | `{object | sim | x,z}` | the game's fire spawner on an object/spot (a real fire with firefighters) |
| `pregnancy_advance` | `{sim, to: "pregnant"|"labor"|"birth"}` | start a pregnancy with a partner, jump its timers |
| `set_aging` | `{enabled, lifespan: short|normal|long|epic, speed?}` | the game options for aging and story progression |
| `world_census` | `{}` | §4 census answer (lots by venue, rabbit holes, objects by kind, Sims by age/occult/household, world name, EPs, season, moon) |

## 4. Census and requirement kinds

`world_census` answer is cached in `tools/harness/stories/census/<save>.json`. Requirement kinds: `world` (name), `ep`, `venue`
(community lot venue type), `rabbithole` (bookstore, library, school, city hall, spa, stadium, grocery, consignment, ...), `shop`
(register / stall objects), `object` (type: stove, pool, pool ladder, book, skill book, instrument, easel, chess, gym, telescope,
garden plot, crystal ball, dive spot, houseboat dock ...), `skillclass`, `age`, `occult`, `season`, `moon`, `pet`. `prepare: true`
lets the engine create the need when cheap (buy_object + place_object on a driven lot, spawn/make_sim, test_env season/moon);
otherwise the story is skipped and listed in the run's coverage table.

## 5. Event log (`<tag>-story.jsonl`, one line per step)

`{ "t": epochMs, "seed": n, "story": id, "run": k, "step": i, "cmd": ..., "actor": name, "simId": ..., "target": ..., "lot": ...,
"ok": bool, "ms": n, "error"?: ..., "world": ..., "save": ... }` plus `{"kind": "census"|"prepare"|"skip", ...}` lines. D joins
hitches to the steps whose windows overlap them (real-hitch classifier: story steps are harness-triggered = scenario cost, reported
per story, not dropped).

Safety: every run uses a scratch copy of the save (never a Baseline save); deaths, births, fires and aging change it. Never focus
the game window. Driven Sims 8-12 at a time; world stays the real save population.

## 6. Commands built (piece A, 2026-10-23; compiled, NOT run in game)

`mods/ts3agent/src/plugin/Handlers/StoryHandlers.cs` (registered by `RegisterStory()` in `HarnessHandlers.cs`). Game calls read from the
decompiled script DLLs (static, [verified] in the IL; behaviour in game [inferred] until a run, ledger §276).

| Command | Game call | Limits |
|---|---|---|
| `make_occult {sim, type, death?}` | `OccultManager.AddOccultType(type, addOutfit: true, isReward: false, fromRestore: false)`, the body of EA's DEBUG cheat `Sim.DEBUG_ModifyOccultTypes` ("Occult... > Add..."); EP gate `OccultManager.IsOccultTypeSupportedWithCurrentInstall`. Ghost: `SimDescription.IsGhost = true` + `SetDeathStyle(death, IsSelectable)` + `Urnstone.SimToPlayableGhost(sim, isCASGhost: true)` (the CAS ghost path in `CASLogic`/`BinCommon`; puts a grave in the Sim's inventory). Zombie: `BuffManager.AddElement(BuffNames.Zombie, Origin.FromFullMoon)` (as `zombies`), needs EP7 | clean error when the EP is missing, the Sim already has that occult, or `OccultBaseClass.CanAdd` says no (age, species, clashing occult; ImaginaryFriend may refuse without a doll); instant: no bite, potion or transform animation; `Frankenstein` (SimBot), `TimeTraveler`, `Unicorn` (pets) also accepted |
| `start_fire {object \| sim \| x,z}` | `FireManager.AddFire(pos, allowOffHomeLot: true)` -> `Fire.CreateFire(lotId, LotLocation)` (BurstIntoFlame, spread, "Talk About Fire" topic, `kHadFireAtHome`) | needs an active Sim with a home lot; no fire off lots or on tiles that cannot burn (`SpreadChanceForTile` 0: water, pool); firefighters come only through the lot's smoke detectors (the answer gives `smoke_detectors`) or a phone call |
| `pregnancy_advance {sim, partner?, to}` | `Pregnancy.Start(female, maleDescription)`, then `Pregnancy.HourlyCallback()` once per pregnancy hour, as EA's DEBUG "Pregnancy... > Have Baby" steps it; `labor` = hour `kHourToStartContractions` (70, read by reflection), `birth` = `kHoursOfPregnancy` (72, `HaveTheBaby`) | partner defaults to `SimDescription.Partner`; the woman needs a home lot and household room; plant Sims get a forbidden fruit instead; on vacation or in the University world the hour does not move (`reached: false`); all hours run in one tick, so the 24 h `ShowPregnancy` and the labor interactions queue at once |
| `set_aging {enabled?, lifespan?, speed?, story?}` | the Options dialog's setters (`OptionsModel`): `AgingManager.Singleton.Enabled`, `SetSimDaysPerAgingYear(0..4 = short, medium, normal, long, epic)`, `SimDaysPerAgingYear = speed`, `StoryProgressionService.SetMode(Enabled \| Disabled)` | runtime only: no option file is written and the next game start uses the player's settings; a save keeps them (scratch saves only) |
| `world_census {samples?: 20}` | reads `LotManager.AllLots` (`CommercialLotSubType` per community lot, `IsDivingLot`; `kEP10_Port` = houseboat docks), `Queries.GetObjects<RabbitHole>()` by class, one pass over `Queries.GetObjects<GameObject>()` with kinds by class or interface name (stove `IStove`, pool `Pool`, pool_ladder, book, skill_book `BookSkill`, bookshelf, instrument `MusicalInstrument` + `Violin`, easel, chess `IChessTable`, gym `IGymObject`, telescope `ITelescope`, garden_plot `Plant`, crystal_ball, register `IShoppingRegister`, stall `BaseFoodStand` / `FVShoppingRegister` / `LemonadeStand`, houseboat, smoke_detector), `SimDescription.GetSimDescriptionsInWorld()` by age, species and occult (+ `IsGhost`, `IsZombie`), `Household.sHouseholdList`, `GameUtils.GetCurrentWorld`, `GameUtils.IsInstalled` per EP/SP, season, weather and moon from `test_env {mode: read}` | reads only; one full object scan (cost unmeasured, I-171-6: call once per run and cache, §4); per-kind `samples` ids include inventory items (`inventory: true`); Sims list capped at 800; dive spots are diving lots, not objects |

## 7. Engine (piece B, 2026-10-23; self-check + dry run, no game run)

Files: `tools/harness/story-fuzzer.ts` (scheduler, actors, prepares, log, replay), `tools/harness/story-census.ts` (census cache
and need status), the `--story` hook in `tools/harness/profile-world.ts` (one block after the `--repro` checks plus one line in the
repro runner).

```
node tools/harness/profile-world.ts --tag sv-story --minutes 30 --story '{"seed":7,"stories":40}' [--no-env]
node tools/harness/profile-world.ts --tag sv-replay --minutes 6 --seed 12345 --story '{"seed":7,"replay":"sv-story","from":300,"to":600}'
node tools/harness/story-fuzzer.ts --check [--content DIR]        # §2 schema of templates, personas, weights
node tools/harness/story-fuzzer.ts --dry-run --seed 7 [--config FILE|JSON] [--census FILE] [--content DIR] [--minutes 30] [--env-free]
node tools/harness/story-fuzzer.ts --replay TAG --from S --to S [--seed N]   # standalone on the running game, no profiler
node tools/harness/story-census.ts --save Baseline-SV [--refresh]  # world_census into stories/census/<save>.json
node tools/harness/story-fuzzer.ts ; node tools/harness/story-census.ts   # self-checks (fixtures, fake game, no game)
```

- **Config** (JSON or file, all optional): `seed` (default profile-world `--seed`, else 1), `stories` (cap N), `minutes` (cap,
  else the window), `driven` [8,12], `parallel` 4 stories at once, `gapS` [3,10] between starts, `cameraP` 0.3 and `modeP` 0.05 per
  start, `maxPrepares` 20, `maxSpawns` 6, `only` (ids or prefixes), `categories` (override weights), `persona` (force one),
  `content` (dir), `replay`/`from`/`to` (below). Config and templates are checked before the game is touched; a bad one fails the job.
- **Hook**: `--story` runs after any `--repro` steps (not with a loop repro), turns the stack samplers off (they suspend the sim
  thread) and writes `<tag>-story.jsonl`; every agent call also lands in `<tag>-calls.jsonl` as before. The 30 s `test_env` guard
  undoes winter and full moon, so season/moon needs are `preparable` only with `--no-env` (else `missing`).
- **Scratch guard**: `game_state` save name; unless it is already `Story-*`, `save_game {name: Story-<save>-s<seed>}` first (the
  game then writes only there); if that fails the run stops (`refusing to change a Baseline save`). Template `save_game` steps get
  the scratch name; `load_save`, `load_world`, `fresh_world`, `quit_game`, `enter_live_mode` are refused by the schema check.
- **Census** (`story-census.ts`): `world_census` once per base save, cached in `stories/census/<save>.json` (git-ignored: Sim
  names); values compared without case and punctuation; `any`/`*` = any; EP codes `EP1..EP11` also count as `WorldAdventures`,
  `Ambitions`, `LateNight`, `Generations`, `Pets`, `Showtime`, `Supernatural`, `Seasons`, `UniversityLife`, `IslandParadise`,
  `IntoTheFuture` [verified static, §10]. A's answer is mapped by `fromAgent` (§10); internal shape: `{world, eps, season, moon, lots: [{id, name,
  venue?, household?, pos?}], rabbitholes: [{type, id, lot?}], objects: {kind: count | [{id, lot?}]}, shops, skillclasses: [name |
  {name, rabbithole}], sims: [{id, name, age, occult?, species?, household, in_world}]}`. A need is `available`, `preparable`
  (object = `catalog_search` + `buy_object {near: home, free}`; age/occult/pet = `make_sim` (+ `make_occult`); season/moon =
  `test_env fixed`, moon only `full`/`firstquarter`/0-7) or `missing` (one `skip` line, coverage table).
- **Driven Sims**: the active household's in-world humans plus seeded townies (`list_sims world`) up to a seeded 8-12;
  `spawn_sims` when short. Each gets a persona whose `household.ages` holds its age. Roles bind to free driven Sims by age,
  occult and pick (`townie` = not the household); none fits or `pick: new` = `make_sim` (budgeted). A Sim is in one story at a time.
- **Scheduler**: a free lead Sim (seeded), then a template among the eligible ones (needs not missing, `maxConcurrent`, wall-clock
  `cooldownMin` since its last end, first role fits the lead; `actors: []` = a world story without a lead), weighted by weight x
  category x lead persona and world `categoryBias` x longest-prefix `templateBias` (persona, world). Between starts: a camera jump
  to a census lot with `pos` (`camera {target, pos}`) and, rarely, build mode on for 5-15 s and off (`enter_live_mode` if off
  fails); a mode switch waits for in-flight story calls and holds new ones. Stories run in parallel; one story's steps run in
  order, each through `repro-loop.ts` `runStep` (fixed `waitS`, default 10, or `waitUntil`). A failed non-optional step ends the story.
- **Steps**: `sim` = the step actor and `target` = the resolved target unless the args set them; `$actor.<role>`, `$target`, `$lot`
  in args and string targets. `{find}` targets: census ids (objects, rabbit holes, venue lots, Sims by age/occult/pet, a skill
  class's rabbit hole), else `list_objects {query}`. `interact`: `list_interactions`, a seeded pick among enabled entries whose
  label starts with the step label (case-insensitive), then `do_interaction` with that exact label; no match = failed step, counted
  under "label never matched" in the coverage table. `any_interaction`: a seeded pick among all enabled entries without a picker.
- **Randomness**: the scheduler draws from `rng(seed)`, story run k from `rng(seed, k)`; the dry run is exact per seed; in game the
  start order also depends on reply timing, so a hitch is replayed from the log, not regenerated.
- **Log** (§5): step lines `{t (start, epoch ms), ms (call time, wait not counted), seed, story, run, step, cmd, actor, simId,
  target, lot, ok, args (as sent), label?, error?, world, save}`; `kind` lines `scratch`, `census`, `skip` (once per template and
  reason), `prepare` (with the `calls` made), `between` (camera, build_mode, with args) and a final `coverage` (per template runs,
  ok/failed steps, skip reasons, label misses). Lines are written when a call ends: sort by `t` for the start order.
- **Replay** (for story-report.ts, D): `{replay: <tag or story.jsonl>, from, to}` (seconds after the log's first line) repeats the
  logged calls: all ok prepares that started before `to` (made Sims' new names mapped into later args), then the step and between
  lines that started in the window, at their original offsets. No draw is repeated (choices are in the args); the game RNG is
  profile-world `--seed`. Lines as above plus `replay: {of, t}`.
- Dry run on C's 110 templates with the fixture census (seed 7, 30 min): 72 stories, 204 steps, schema clean; skips as expected
  (EPs absent from the fixture, missing rabbit holes); its label misses come from the fake game's 3 interactions, not the game.
- Not done: no game run; Sims are addressed by full name (`Util.FindObject` accepts names [verified: Util.cs]; a duplicate name picks one [inferred]).

## 8. Story catalogue (piece C, 2026-10-23)

Files: `tools/harness/stories/personas.json` (11 personas: household shape, `categoryBias`, `templateBias`), `categories.json`
(default weights: normal 0.45 + world 0.10 = everyday play, lifestage 0.25, chaos 0.10, occult 0.10), `worlds/*.json` (Sunset Valley,
Bridgeport, Isla Paradiso, Moonlight Falls: what each is good for, plus biases; a world absent from the census never matches),
`templates/*.json` (110 templates). Bias keys are template id prefixes, the longest matching prefix wins; persona, world and
category factors multiply.

Conventions the engine (B) must accept beyond §2:
- a step `target` may be the string `"$actor.<role>"` (a Sim target) instead of a `{find, value}` object;
- `ep` values: `WorldAdventures`, `LateNight`, `Pets`, `Showtime`, `Supernatural`, `Seasons`, `UniversityLife`, `IslandParadise`,
  `Generations`, `IntoTheFuture` (map the census EP names onto these); `venue`/`rabbithole`/`object` value `any` = any match;
- `interact` labels are English pie-menu text, matched case-insensitively as a prefix (`"Have"` = any "Have ..." meal); most are
  `optional` because labels differ per EP and age; args of existing commands use each handler's `Args` names (`sim`, `other`, ...);
- `$target` in args = the resolved step target (lot for venue/rabbithole/shop, object id for object), `$lot` = the actor's lot.

| Category | Count | Templates |
|---|---|---|
| normal: daily | 10 | daily-work-day, -school, -cook-family-meal, -quick-meal, -clean-house, -pay-bills, -sleep-night, -shower-bathroom, -autonomy-hour, -tv-and-computer |
| normal: skills | 14 | skill-read-book, -skill-book, -learn-recipe, -instrument, -painting, -chess, -gym, -garden, -fishing, -telescope, -homework, -class-rabbithole, -logic-chess-park, -max-cheat-burst |
| normal: social | 11 | social-chat, -flirt-date, -date-restaurant, -woohoo, -propose-marriage, -wedding-party, -house-party, -make-enemy, -phone-call, -invite-over, -visit-community |
| normal: shop, build, CAS, camera | 12 | shop-bookstore, -grocery, -consignment, -buy-place-furniture; build-mode-walls, -mode-flip, -sell-replace; cas-plan-outfit, -outfit-swaps; cam-far, -populated, -pan-follow |
| normal: pets, travel | 7 | pet-adopt, -train-play, -horse-ride, -stray-strays; travel-vacation, -university, -future |
| world | 15 | world-ip-dive, -ip-houseboat-move, -ip-boat-travel, -ip-sea-crowd, shop-ip-stall, world-sv-park, -sv-night-seacheck, -nightlife-bar, -nightlife-club, -bp-celebrity, -bp-vampire-night, -mf-hangout, -mf-elixir-shop, -strand-stuck, -portal-trip |
| lifestage | 15 | life-try-for-baby, -pregnancy-to-birth, -hospital-birth, -toddler-skills, -baby-care, -teen-prom-driving, -age-up-cake, -elder-day, -old-age-death, -ghost-haunt, -adoption, -make-family-member, gen-short-lifespan-days, gen-family-line, social-birthday |
| chaos | 13 | chaos-pool-drown, -kitchen-fire, -fire-spot, -burglar-night, -thunderstorm, -starvation, -electrocution, -brawl-crowd, -meteor, -blizzard-heatwave, -vfx-swarm, -zombie-night, social-fight |
| occult | 13 | occult-vampire-bite, -werewolf-full-moon, -fairy-zombie-moon, -witch-spells, -genie-wishes, -mummy-curse, -ghost-haunting, -mermaid, -plantsim, -imaginary-friend, -robot, -mixed-household, -zombie-cure |

Gaps (stories that need a command we lack; today they run a weaker stand-in or wait for a random roll):
- `spawn_burglar` (chaos-burglar-night uses a guessed cheat name plus the night roll); `spawn_meteor` and a lightning strike on a
  spot (chaos-meteor, chaos-thunderstorm only set weather); `set_celebrity` (world-bp-celebrity tries a best-effort `reflect_call`);
  `fire_event {prom|graduation|festival}` (life-teen-prom-driving); `adopt {species, age}` through the game's adoption flow (pet-adopt
  and life-adoption use `make_sim` or the phone); `throw_party {type}` without the phone dialog; `set_autonomy {sim, on}` per Sim
  (chaos-starvation calls `autonomy` with guessed args); moon phase in `test_env` if it cannot set one (moon `prepare`).
- Pie labels are unverified until a first run: each run's coverage table should list `interact` steps that never matched, so the
  labels can be fixed from `list_interactions`.

## 9. Reports and replay (D: `tools/harness/story-report.ts`, built 2026-10-23, self-check only, no game run)

`node tools/harness/story-report.ts <tag> [--baseline <tag>[,<tag>]] [--top 10]` reads `probe-out/profile/<tag>-story.jsonl` and the
profile files `freeze-report.ts load()` reads. All times are s since the story log's first line.

- **Join.** Every Simulate iteration and publish gap >= 300 ms. The real-hitch rule is `worst-hitches.ts exclusion()`: first minute, profiler edge,
  sampler and render-paced gaps are dropped. Probe-call and repro-triggered windows are dropped too, unless a story step
  overlaps them; story steps are scenario cost and are kept per story (§5). A step's window is `[t, t + ms + 10 s]` (epoch ms),
  the same post window as a repro step. **B must log `t` as the step's start.** An event inside the windows of parallel stories counts
  in each one's row. Events outside every step window are kept as natural play. QPC is mapped to epoch with the frames.csv
  offset. Runs without frames.csv get no join (shortcut).
- **Cause columns** come from the longest iteration in the event: `cause()` class / lead bucket, slowest task (docs/166), the lock holder (`holders`,
  docs/165 §8) and the longest route-job wait key. The sync plan logged in the event gives the reason (`route async: sync plan`),
  and the top phase of its docs/164 `route phases:` split line is added in brackets. The frame service is the longest service of the slowest presented
  frame inside (docs/167 §7).
- **Tables:** by story template, by template + step (`story | step | cmd`), NEW causes, worst N, and coverage. Coverage lists, per story,
  runs, steps ok / failed, skips with their missing needs (`missing`/`needs`/`need`/`reason` of the `skip` line) and prepares.
- **NEW causes:** a facet value the baseline never had. With `--baseline <tags>`, every facet (class, lead, task, holder, wait,
  sync reason, phase, service) is judged against those runs' events. Without it, the docs/161 §3 list is used: class, sync reasons
  NoStart / Foreign / M1Pending, and the 13 route-job wait keys. That list names no tasks, holders, phases or services, so those facets are
  not judged.
- `freeze-report.ts <tag>` adds the two by-story tables when `<tag>-story.jsonl` exists.
- `--replay <tag> --from <s> --to <s> [--lead 120]` prints the seed, save and the steps of `[from - lead, to]`, the command
  `story-fuzzer.ts --seed n --replay <tag> --from s --to s`, and a queue job (`run.profile.story = {seed, replay, from, to}`,
  `asi: tested`; set it to the build the run used). **Contract for B:** `--replay` runs the seeded scheduler from the start and
  executes only the steps logged in `[from, to]`. The lead brings the state that earlier steps set up.
- `--draft-repro <tag> <hitch s>` takes the event nearest `<hitch s>` (within 5 s). It prints a `repros.json` entry to stdout:
  the cause in `bug`, a 75 s lead-in, and a docs/169 loop with an arm step (`patches {TODO_switch: "$on"}`), the story steps of the 120 s before the event
  (same story runs; `interact` becomes `do_interaction`; failed steps and `any_interaction` picks are flagged), `arms` ABBA and `until.perArm: 10`.
  The draft is unverified. The shortcut step comes once the cause is known: replace the story steps with the direct trigger.
- Self-check: `node tools/harness/story-report.ts --self-check` (fixture story log, hitches, freezes, sync and phase lines, frames).

## 10. Integration (A + B + C + D, 2026-10-23; self-checks + dry run, no game run)

- **Census (B reads A):** `story-census.ts` `fromAgent` maps A's `world_census` answer (§6) onto the §7 internal shape: `world` =
  `world_file` (also the `WorldName` enum, e.g. `NewDowntownWorld` = Bridgeport), `moon` = `moon_phase` (number only),
  `season` only when it is a season name (not "Seasons (EP8) not installed"), `lots` = `community_lots` (venue without the
  `CommercialLotSubType` prefix: `kEP3_DanceClubRave` -> `DanceClubRave`, `kEP10_Diving` -> `Diving`, `kEP10_Port` -> `Port`) plus the
  households' home lots, `rabbitholes` = `rabbit_hole_list` (class names), `objects` = `{kind: {count, samples}}` (count from `count`;
  samples with `inventory: true` are never targets), `shops` = the `register` and `stall` kinds, `sims` occult `"A,B"` -> list,
  `in_world` = `instantiated`, humans without `species` (as `list_sims`). `FIXTURE` is now an answer in A's exact shape (decimal ids,
  every kind key) and drives both self-checks and the dry run.
- **EP order [verified static]:** `ProductVersion` bits EP1..EP11 in release order, matching the `CommercialLotSubType` prefixes
  (`kEP1_Market`, `kEP2_FireStation`, `kEP3_DanceClub*`, `kEP5_HorseRanch`, `kEP6_Bistro`, `kEP7_WerewolfBar`, `kEP8_Festival`,
  `kEP9_Dormitory`, `kEP10_Port`, `kEP11_*`); A's own `make_occult` gates Zombie on EP7. SP codes pass through unnamed.
- **A changes:** census kind `fishing_spot` (`FishingSpot`), community lots carry `pos` (lot centre: the between-story camera
  jumps had no target without it), `sim_visit_lot` accepts `lot:<id>` (the engine's venue and object-lot targets).
- **Template fixes (C):** rabbit holes by real class name, combos included (`BusinessOffice` -> `BusinessAndJournalismRabbitHole`,
  `School` -> `SchoolRabbitHole`, `Restaurant` -> `Bistro`/`Diner`/...); venues by subtype family (`Park`, `Bar`, `DanceClub`,
  `Hangout` lists); `Gym` and `Consignment` are venues (consign/buy steps target a `register`); `dive spot` / `houseboat dock` ->
  venues `Diving` / `Port`; needs the census cannot see (sink, mailbox, phone, portal) dropped (their targets fall back to
  `list_objects`); `skillclass` need -> any rabbit hole (no class data in the census). Arg names: `cheat {command}`,
  `autonomy {enabled}`, `outfit_change {category: formal|athletic}` (no `sim`: it picks Sims itself), `ride {}` (the active Sim
  rides `@made`), `sell_object` by `target`; pool-ladder removal uses `sell_object` (`remove_objects` only removes
  `place_objects` objects); dropped: `camera_jumps dwellS`, `make_sim active_household`, `camera {sim}` steps (camera takes
  vectors), `own_vehicle boat` (no boat kind) and the celebrity `reflect_call` (no static path). Every template command exists and
  every arg it sends is read by its handler, except the engine's injected `target` where the args already use `$target`/`$lot`.
- **Log/join (B/D):** step `t` is now when the logged call goes out (after `list_interactions` and any build-mode hold), `ms` its
  call time; D's windows `[t, t + ms + 10 s]` read that. D's coverage reads B's skip `why`. B's `--replay` repeats the logged
  calls (no scheduler re-run): D's "contract for B" above is met by the logged args instead.
- **Queue:** `run.profile.story` -> `profile-world.ts --story '<json>'`; story config `censusOnly` (scratch copy + fresh census,
  no stories). `tools/harness/queues/story-smoke.json`: census-only job, then one 10-min run, seed 1, Baseline-BP-Spring scratch
  copy, tool-check measurement settings on asi.0332.
- (Superseded by §11: `story-smoke.json` is now one job; 109 templates.)
- Checks: `story-fuzzer.ts --check` 110 templates clean; self-checks story-fuzzer, story-census, story-report, freeze-report,
  queue pass; dry run seed 7, 30 min on the fixture: 73 stories, 203 steps, 80/110 templates eligible (skips = EPs, venues and
  rabbit holes absent from the fixture), every failed step a fake-game label miss; agent builds with StoryHandlers.cs.

## 11. Believability lint and smoke queue (2026-10-23; self-checks + dry runs + agent build, no game run)

`node tools/harness/story-lint.ts [--seeds 5]` lints the content and dry-runs it; `--self-check` asserts each rule on fixtures, that
`story-census.ts KIND_NAMES` equals `StoryHandlers.cs KindTypes`, and that the repo content is clean. `story-fuzzer.ts loadContent`
runs the static rules, so `--check`, `--dry-run` and `profile-world --story` refuse content with findings (`believability: ...`).
Game rules are TS3 play knowledge [community-known, not read from the DLLs].

| Rule | Rejects |
|---|---|
| age | an action the role's ages cannot do: cooking Teen+, Quick Meal Child+, WooHoo / Try for Baby / proposals / weddings / adoption / bills / parties YA+, school Child/Teen, driving and prom Teen, toddler teaching targets Toddlers, `pregnancy_advance` mother YA/Adult and father YA+, `age_up` never on an Elder unless `confirm_elder_death`, career join Teen+ |
| pair | romance, fights and romantic `modify_relationship` states across the Teen / adult line; pregnancy and Try for Baby without one `gender: Female` and one `gender: Male` role; a pregnancy with no partner role |
| occult | a role occult or `make_occult` type without its EP need (Vampire: LateNight or Supernatural; Werewolf/Fairy/Witch/Zombie: Supernatural; Genie: Showtime; Mummy: WorldAdventures; Mermaid: IslandParadise; PlantSim: Seasons; ImaginaryFriend: Generations; Robot: IntoTheFuture; Ghost: none) or at an age it cannot have |
| prepare | `prepare: true` on venue/rabbithole/shop/ep/world/skillclass; an object need or target the census never counts (a prepare would buy another every run); a non-optional `{find}` target with no need (the story would fail instead of skipping or preparing); moon values `test_env` cannot set |
| standin | no non-optional step besides scene setup (`set_speed`, `auto_dialog`, `household_funds`, `modify_relationship`, ...): a run whose actions all fail would still count as that story |
| persona | ages and occults that do not exist, category keys, `templateBias` keys (persona and world) that match no template or no template the persona's ages can lead |
| run | (dry runs, `lintRun`) one Sim in two running stories, as actor or Sim target |

Content fixed for the first lint (56 findings; 2 closed by the rule itself: "any object" always exists): 27 key steps made non-optional (failed label = failed story, counted in coverage),
Teen removed from adult romance/fight/party roles, toddlers removed from Blow Out Candles, `gender` on every pregnancy role, a
father for `life-hospital-birth`, EP Generations + City Hall needs for `life-teen-prom-driving`, needs for sink, mailbox, phone,
fridge, homework, shop register and venue targets, `plant` -> `garden plot`, the witch broom step (name search only) dropped,
`pet-stray-strays` got a camera sweep, `social-party` bias keys -> `social-house-party`, `world-bp-celebrity` removed (no celebrity
command: it was a plain venue visit).

Engine and agent changes the rules needed: role `gender` (fits by census `female`; `make_sim` gets the role's gender); a made occult
Sim is made at the role's age (it was made Adult and relabelled); `{find: age|occult|pet}` targets skip Sims of other running stories
and are claimed until the story ends; census kinds `fridge` (IFridge), `bed` (IBed), `crib` (ICrib), `shower` (IShower), `toilet`
(IToilet), `sink` (Sink), `tv` (ITelevision), `computer` (IComputer), `mailbox` (IMailbox), `trash_can` (Trashcan), `table`
(ITableDining), `bar` (Bar, IBarProfessional), `dance_floor` (DanceFloor), `cake` (ICake), `phone` (Phone), `homework` (Homework)
[type names verified in the script DLL metadata; counts in game open].

Smoke queue `tools/harness/queues/story-smoke.json`: one 10-min job, seed 1, tool-check measurement settings on asi.0332. queue.ts
copies `Saves/Baseline-BP-Spring.sims3` to `Story-Baseline-BP-Spring-s1.sims3` (replaced each job) and loads the copy, so the
Baseline file is only read; the census is taken on the first run. Pass = exit 0, no crash, and story-report.ts `smokeFails`: the
story log exists, >= 1 story step ok, the join has a frames.csv offset. Validated by `queue.ts --dry-run` and self-checks only.

Open: interact labels are still unverified until a game run (coverage lists misses); the run rule sees logged calls only, not the
wait after a story's last step. (The "parent" role open item is closed in §12: role `family`.)

## 12. Day planner, multi-day runs and death guard (2026-10-23; self-checks + dry runs + agent build, no game run)

Rodney's request: driven Sims live a believable day around the game's own schedule, stories only in their free time, runs over
several sim days, and no death unless a template asks for one. Files: `tools/harness/story-day.ts` (planner, scheduler loop, guard
decisions, pass rule, day lint, fake game), hooks in `story-fuzzer.ts` (config `days`, role `family`, template `death`), five
commands in `StoryHandlers.cs`, the `Day plan` section in `story-report.ts`, rules in `story-lint.ts`, `queue.ts` pass rule.

```
node tools/harness/profile-world.ts --tag bp-days --minutes 25 --story '{"seed":1,"days":{"tickS":5,"nightSkip":true}}'
node tools/harness/story-fuzzer.ts --dry-run --seed 1 --config '{"days":true}' --minutes 25    # plan on the fake game
node tools/harness/story-day.ts                                                                # self-check
tools/harness/queues/story-days.json                                                           # one 25-min job
```

**Commands** (game calls read from the decompiled script DLLs [verified static]; behaviour in game [inferred] until a run):

| Command | Game call | Notes |
|---|---|---|
| `story_day_state {sims}` | `SimClock` day/hour, `GameUtils.IsOnVacation`; per Sim `CareerManager.OccupationAsCareer` / `School` (`StartTime`, `FinishTime`, `DaysOfWeekToWork`, `IsWorkDay`, `IsDayOff`, `HasVacationOrLeave`, `ShouldBeAtWork`, `IsAtWork`, `CarpoolTime`), `RetiredCareer`, `Motives.GetValue`, `SkillManager.List`, `DreamsAndPromisesManager.LifetimeWishNode` / `GetPromisedNodes` (skill = a `Skill.Name` inside the wish text), guard facts: `SwimmingInPool` posture, buffs OnFire / Drowning / Fatigued / SingedElectricity / Starving, trait Unlucky, current interaction, `AgingManager.Enabled` + `SimDescription.AgingEnabled` + `AgingManager.NumDaysBeforeAging`, `Urnstone.KillSim` queued, `IsDead`, `IsGhost` | reads only; one call per tick for all driven Sims; `IsAtWork` is also true on the way (`IsSimGoingToWork`) |
| `go_to_work {sim, school?}` | `InteractionQueue.Add(Career.CreateWorkInteractionInstance())`, what the game's own work alarm `Career.RegularWorkDayGoToWorkHandle` queues (`WorkInRabbitHole` / `GoToSchoolInRabbitHole`) | only in `ShouldBeAtWork` / `IsAllowedToWork` and not already going; the carpool and bus stay the game's |
| `sim_fix_need {sim, motive}` | `Autonomy.FindBestActionForCommodity(motive, BuffAutoSolve)` at `NonCriticalNPCBehavior`, the search `BuffManager.CreateInteractionForAutoSolveBuff` runs | Energy = bed, Hunger = food, ... |
| `sim_pursue {sim, skill}` | `SkillManager.GetStaticSkill(skill).Commodity` (`CommodityKind.Skill<Name>`), `SkillManager.SimCanLearnSkill`; the best home-lot interaction whose tuning outputs the skill (or the fridge's non-snack `Fridge_Have <recipe>` for Cooking) passing a user-directed `Test`, at `UserDirected` (§16 #8); none = `Autonomy.FindBestActionForCommodity(commodity, Generic)` | homework = `SkillHomework`; a skill the Sim cannot learn or no object for it = failed pursuit, carried |
| `sim_practice {sim, skills}` | the same home-lot scan for many skills (read only) | the day planner's census, once per Sim per planned day |
| `death_rescue {sim, kind}` | starve: `Motives.SetValue(Hunger/VampireThirst, 0)`, remove Starving, eat search; drown: remove Drowning + Fatigued, Energy/Hunger to 0, `GetOutOfPool` (internal; its `Singleton` by reflection) on the posture's pool; burn: remove OnFire (`BuffOnFire.OnTimeout` is the Burn kill); electrocution: cancel the current Repair/Upgrade, remove SingedElectricity (the repair failure path kills only a Singed or Unlucky Sim: `TV.cs` and its siblings); freeze: `Motives.SetValue(Temperature, 0)`; old_age: `AgingManager.CancelAgingAlarmsForSim` (the call `AgeTransitionWithoutCake` makes after an elder dies) | answer `dying` = KillSim already queued (too late) |

**Planner** (`story-day.ts`, all asserted in its self-check):
- Day type per Sim per calendar day: `vacation` (on vacation), `workday` / `schoolday` (career or school `IsWorkDay` and not
  `IsDayOff`, so a vacation day or leave is a day off), `retired` (RetiredCareer, no job), `weekend` (Saturday/Sunday), else `dayoff`.
- Plan (seeded per Sim and day): sleep by age (child 20:30, teen and elder 22:00, adult 23:00 +-30 min; wake 6:30-7:30, at
  least 1 h before work), work/school `[start - 0.5 h, finish]` (overnight shifts end at midnight in that day's plan: shortcut),
  homework 30 min after school (1 h), pursuits (2 h, 30 min free before each) from the backlog front, at most 1 on a work/school
  day and 3 on a free day, the rest free. Babies and toddlers get no plan (parents' stories care for them).
- Backlog per Sim, built once: lifetime wish skill, promised wish skills by points, the two highest unmaxed skills, one seeded
  hobby by age; at day end the planned pursuits not done move to the front (carried), done ones to the back. So a working Sim's
  evening pursuit that failed runs first on its next day off.
- Ticks every `tickS` (5 s wall): the death guard first, then a new day's plan (`dayplan` line), block changes (`block`), work
  (attendance = `IsAtWork` or in the rabbit hole while `ShouldBeAtWork`; `go_to_work` from 15 min after start, 3 tries), on
  entering a block: bedtime `sim_fix_need Energy`, `sim_pursue` for pursuit and homework blocks; in free/pursuit blocks one low
  motive per tick (Hunger <= -30, Bladder/Energy <= -40, Hygiene/Fun/Social <= -50) -> `sim_fix_need`, 1 sim h cooldown.
- Stories: a Sim in a work, school, sleep, homework or pursuit block is `blocked`; the scheduler takes only free Sims as leads
  and actors, and starts after the planner's first tick. A story already running keeps its Sim; work is still pushed.
- `nightSkip` (default on): while every planned Sim sleeps or works, `set_speed skip` (the game's own Skip speed, as when the
  household sleeps), back to `ultra` when one wakes. [inferred: profile-world's autopilot may reset Skip to ultra; then the run
  covers fewer sim hours and the `>= 2 sim days` rule tells.]

**Death guard.** Deaths only when a template says so: template field `death: "<role>"` (chaos-pool-drown, chaos-starvation,
chaos-electrocution, chaos-meteor, life-old-age-death) takes that role's Sim out of the guard for the rest of the run (its death
may come after the last step). Every other planned Sim is checked each tick (`guardDecide`, thresholds [inferred], the game kills
at the floor): drowning (swimming with Drowning or Fatigued, or Energy/Hunger <= -70: `Motive.cs` kills a starving swimmer by
Drown), starving (Hunger or VampireThirst <= -85, or Starving at <= -70), burning (OnFire), electrocution (Singed or Unlucky
while repairing/upgrading), freezing (Temperature <= -85, Seasons), old age (Elder, aging on for the game and the Sim, <= 1 day
to the end). One rescue per Sim and kind per 30 s, each a `guard` line (sim, danger, why, did). A death is a `death` line
(`scripted` = a death template owns the Sim). **Flies:** TS3 has no death by flies [verified: `SimDescription.DeathType` has no
such member and no `Kill(...)` call uses one]; nothing to guard. Other game deaths (meteor, shark, jetpack, mummy curse, ...)
come from events the guard cannot see coming: they show up as unscripted `death` lines.

**Pass rule** (`dayFails`, queue.ts for jobs with `story.days`): the `days` summary line exists, >= 2 calendar sim days seen, 0
unscripted deaths, every Sim with a work/school day due (seen `ShouldBeAtWork`) was at work that day, and pursuits done per
Sim-day on free days (dayoff, weekend, vacation, retired) > on work/school days (both must occur; retired and unemployed Sims
give free days whatever the weekday).

**Lint.** Static: a `kill_sim` or elder-death `age_up` step must name its victim in `death`; a role named `parent*` next to a
Baby/Toddler/Child/Teen role needs `family`. Day log (`lintDays`, story-lint's dry run and story-report): >= 3 rescues per Sim per
sim day = unbelievable schedule (flag in the report); a story whose first step started on a Sim that had been at work, school or
asleep for >= 15 s. **Report:** `story-report.ts <tag>` adds `### Day plan`: day type table (Sim-days, pursuits planned / done /
per Sim-day / carried / backlog), attendance and rescues per Sim, rescue and death lists, the pass verdict.

**Parent fix.** Role `family: "<earlier role>"`: the Sim comes from that role's household (census `household`; a made Sim of the
active household gets the active household); none there = the story is skipped (`none fits in <role>'s household`), never a
stranger. Set on life-toddler-skills, life-baby-care, skill-homework.

**Checks run:** `story-day.ts` self-check (fake game: Friday 06:00, 2 sim min per s, 25 min = 3 calendar days, 49.9 sim h; free
1.39 vs work 1.00 pursuits per Sim-day; 15 injected dangers rescued, 0 deaths); story-fuzzer, story-lint, story-report,
story-census, queue self-checks; `story-fuzzer.ts --check`; lint CLI clean; `queue.ts --dry-run queues/story-days.json`.
Not run in game: every command, thresholds, Skip speed under the autopilot, the speed at which 25 min covers 2 sim days.

## 13. First live run (story-smoke 2026-10-23, BP, seed 1; fixes built offline, no game run since)

`probe-out/queue/chain145.log`, `<tag>-story.jsonl`, `-activity.jsonl`, `-upkeep.log` of `story-smoke-2026-10-23-bp-story-smoke`.
FAIL "sim clock stuck at day 1 08:00 for ~60 s", 29 stories, 75 log lines; ledger §292.

| Failure | Cause | Fix |
|---|---|---|
| clock stall | story `travel-university` #24 took the household to the University world at 02:55:07 [verified: activity lists University townies from 459 s, upkeep "no active household", clock day 1 08:00 = the new world]; the age-up and Buy Books stories had failed before any dialog. After arrival the game stayed script-paused 60 s with nothing answered: the autopilot task did not survive the switch [inferred] | travel templates skipped unless config `travel: true` (run alone); `autopilot` restarts a task idle > 2 s (`restarted` in its answer); upkeep logs dialog titles and adds `{kind: "dialog"}` lines to the story log; the clock guard prints `pending_dialogs` and `sub_state` at the trip |
| "no sim named 'Marie Desmarais'" x3 (+ Morrigan after the trip) | Marie is in household `Tourist` (no home lot): her SimDescription leaves every household while she is despawned [verified: census + activity] | townies only from households with a home lot (census `homeLot`); "no sim named", "not in the world", "has no motive", greyed or empty pies = a `skip` line `not available`, the story ends unfailed, a vanished Sim leaves the driven set |
| pie labels (Chat, Fish, Take Shower, Quick Meal, Sleep, Work Out, Buy Books, Paint, Adopt, WooHoo) | (a) entries carry their menu path ("Friendly... / Chat") [verified: `BuildMenu`], (b) busy Sims: Caroline at work, Victoria (household Ritter) a bouncer all run, Bobbie (Gamboa, no lot) a mixologist [verified: activity], (c) targets on other households' lots (census samples are any lot) | a label matches the entry or any path segment; `list_interactions` with disabled entries: a greyed match or no enabled entry = not available; object targets: the actor's own home lot, then lots no household lives on; `list_objects` on the home lot as fallback. Labels themselves unverified until the next run (I-171-30) |
| Wogan Hemlock has no motive Hunger | vampire: VampireThirst | `set_motive` and `sim_fix_need` map Hunger to VampireThirst; other missing motives = not available |
| restore_outfit: wears no agent parts | wear_parts never dressed her | no-op success |
| no object skill book to target | census counted 180 skill books, all 20 samples in inventories | an object kind whose samples are all in inventories counts 0 (need prepares or skips; `skill-homework` now skips: homework lives in inventories); a target with nothing placed runs the template's object prepare (`buy_object` id) once |
| travel-vacation: missing argument 'world' | template | `world: "france"` |

Open: `pregnancy_advance` NullReferenceException (Katelyn, partner Bobbie), `cas_open` / `set_skill` timeouts were the trip's loading screen. Next: rerun `queues/story-smoke.json` and read the `not available` and `label never matched` counts.

## 14. Story-smoke rerun crash: 0x575651 after a pet `make_sim` (2026-10-23; static + logs, fixes built, no game run)

Sources: `probe-out/queue/chain148.log` (asi.0332, BP, change set 87c8880d5982), `xcpt <pc> 26-10-23 11.20.26.txt` (the game's own report in `Documents\Electronic Arts\The Sims 3\`), `probe-out/profile/story-smoke-2026-10-23-bp-story-smoke-story.jsonl`. Disassembly of the Steam `TS3W.exe` copy with capstone (no decompiled code here). Ledger §294.

**Fault [verified: registers + disassembly].** ACCESS_VIOLATION reading `0x30` at `0x575651`, 03:20:26 UTC. `0x575650` is a vector size helper (`(end - begin) / 0x70`, reads `[ecx+4]`/`[ecx+8]`); `ecx = 0x28`, `eax = 0`, so its caller passed `null + 0x28`. Caller frame `0x950F56` is in `0x950DD0`, the Sim component's message handler (vtable slot `0x1044060`), branch for message `0x7334B554`: it loads the message's `+0x14` pointer and calls `0x575650` on `+0x28` of it **without a null check**; the sibling branch for message `0xD07EF517` checks `+0x14` first (`0x950DE5`). `ebp = 0x7334B554` in the dispatcher frame `0x44D78C` (EAMessage handler loop, docs/61) confirms the message id. The game's stack scan also lists `0x59ED59` (an update list) and three addresses outside any module (scan noise).

**Message origin [verified: xrefs].** `0x7334B554` is built only in `0x950C00` (one caller, `0x5A8300`), the body of the script IC `CASUtils_SetBodyShapeInGameObjectImpl 0x5A82B0` (docs/data/ic-safety.csv): it writes the four body-shape floats, then fills message `+0x14` from a component's virtual `+0x20` (component lookup `0x935770` by `[0x11DC484]`) and posts it with `0x692F70` (sim-to-render message buffer, docs/22). When that virtual returns null the message still goes out with `+0x14 = 0`, and the handler faults. Script side: `SimDescription.UpdateBodyShape` -> `SetBodyShapeInGameObject` -> the IC, called from `SimUpdate` for every instantiated Sim, pets included.

**Trigger [inferred].** Story `pet-adopt` #12 ran `make_sim {species: Dog}` at 03:20:24.19 (667 ms, ok), the fault came ~2 s later; nothing else made or reshaped a Sim in between (03:20:25 `household_funds` only). The agent's `make_sim` defaulted age to `Toddler`, so this was a **Toddler Dog**: `Sim.MakePet` does not check the age, and pets have only Child/Adult/Elder. The likely null is the new pet's model/rig object for an age that has none (not checked further). It was the first pet `make_sim` without an age in every logged run (4 Adult Horses ran clean). The second candidate is the build-mode switch: the `between` action at 03:20:04 entered build mode and asked for live mode 10 s later, but `build_mode` only queues the switch: at 03:20:14 `outfit_change` still saw `BuildModeState`. `make_sim` itself checked `LiveModeState` (it already had `RequireLive`) and passed at 03:20:24, so the game was live again by then; the build-mode exit 10 s earlier is not ruled out. Repros `pet-toddler-crash` and `pet-in-build-mode` (repros.json, not run) tell them apart.

**Fixes (agent built in the worktree, no game run).**

| Layer | Change |
|---|---|
| ts3agent `make_sim` | pets default to Adult; any pet age other than Child/Adult/Elder is refused (`a Dog cannot be Toddler`) |
| ts3agent `RequireLive` | also on `buy_object`, `sell_object`, `move_object`, `place_object`, `replace_object`, `set_transform`, `remove_objects`, `age_up`, `place_sim` (`make_sim`, `make_occult`, `pregnancy_advance`, `start_fire`, `spawn_sims`, `place_objects`, `kill_sim`, `zombies`, `own_vehicle` had it). Rule (Rodney): Sims are created only in live mode or in CAS's own flow, objects only in live mode or through the game's own build/buy placement |
| story-fuzzer `between` | owns the game: waits for everyone's in-flight calls, `build_mode on`, waits for `BuildModeState` (30 s), holds 5-15 s, `build_mode off`, then waits for `LiveModeState` (asks `enter_live_mode` again every ~10 s, 60 s cap). Not live after 60 s = the run stops (`game not back in live mode`) instead of running stories in build mode. Logs `hold` so `--replay` sends `off` at the same offset and waits for live too |
| story-fuzzer mode steps | a step that opens build/buy/CAS (`build_mode on`, `buy_open`, `cas_open`) takes the same lock for its story (AsyncLocalStorage run number: its own calls pass, other stories and the day planner wait) until a closing step (`build_mode off`, `buy_close`, `cas_close`) is confirmed live, or the story ends (then live is forced) |
| story-fuzzer | `not in live mode` = not available (skip, not a failure) |
| templates | `pet-adopt`: Dog Adult, Cat Child; `shop-buy-place-furniture`, `build-sell-replace`: `buy_object`/`sell_object` after `buy_close` |
| story-lint | Sim/object commands between an open and a close of build/buy/CAS, and pet `make_sim` ages, are errors |

Self-checks: `story-fuzzer.ts` (fake game now tracks the sub state with a 3 s switch delay: with `modeP: 1` no Sim/object call lands outside live mode, every `between` returns, a game stuck in build mode stops the run), `--check`, `story-lint.ts --self-check`, `repro-loop.ts`, `queue.ts --self-check`; agent package built.

`pregnancy_advance` NullReferenceException (§13): not the same cause. It is a managed exception the agent caught and answered, no native fault, and it ran in live mode; its stack was not kept in any log still on disk. Next story run logs it again (the error text carries the stack).
## 15. Gameplay realism (2026-10-23; static, self-checks, agent build; no game run)

Rules from the player's side, the story constraints derived from them, the per-command table and the checks are in
docs/179 (ledger §296). What changed here: every template declares `realism` (natural | cheat | stress; computed from
`docs/data/gameplay-preconditions.csv` by `tools/harness/realism.ts`, may only be raised); story-lint rejects a missing or
too-low label, steps in a game mode their command does not allow, townie roles that act, travel roles under YA, and `age_up` after
Blow Out Candles; the dry-run rule `lintModes` rejects any step or prepare sent inside a between-story Build-mode window. Config
`realism` (default `["natural", "cheat"]`) picks the labels a run may start; stress templates (8) are skipped by default.
Templates fixed: double age-up (2), townie orders (2), PlantSim EP (University Life), Teen at University, cheat setup a player needs
none of (9). Now 56 natural, 45 cheat, 8 stress.

## 16. Realism items from the first story-days run (2026-10-23; static, self-checks, agent build; no game run)

Source: `probe-out/profile/story-days-2026-10-23-bp-story-days-story.jsonl` (BP, seed 1, 7 calendar sim days, 145 sim h, 10
driven Sims of which 2 household) and `probe-out/queue/chain149.log`; rules from docs/179. Ledger §298.

| # | Finding in the run [verified: story.jsonl / chain149.log] | Change |
|---|---|---|
| 1 | 8 of 10 driven Sims were townies: they got pie orders, a day plan and the death guard (R-HH-4: a player orders only the active household) | config `drive` (default `household`): leads and every role a step orders (`actorOf`: the step's actor or `$actor.<role>` as `sim`) come from the active household (plus Sims a prepare makes into it); townies with a home lot fill only roles no step orders (partners, targets); `drive: "world"` = the old behaviour (townies ordered, `spawn_sims` fill), a stress mode; a `stress` template may still put a townie in an acting role. No household switch: the run plays the save's active household (the game's own switch, Edit Town, would only come between days) |
| 2 | object prepares were `buy_object {free: true}` in Live mode (a cheat); `shop-buy-place-furniture` sent `product: "random"`, which the agent never accepted (`unknown product 'random'`) and topped up funds with `household_funds add` | prepares take the mode lock, `buy_open` (Buy mode on the home lot, R-MODE-3), `household_funds` (read) caps `catalog_search`'s `max_price`, `buy_object` paid from household funds, `buy_close`, live again (`ensureLive`); a story already in Buy mode buys without a second open. `product: "random"` = an affordable catalog product picked by the engine. The two shop templates now buy and sell between `buy_open` and `buy_close`, no fund top-up. Agent: `buy_object`/`sell_object` lose the `RequireLive` the crash fix (§14) put in the handler; their gate stays `kLiveOrBuy` (the CSV `gate` column already said live-or-buy). story-lint allows buy/sell only inside Buy mode, not Build or CAS |
| 3 | `travel_vacation` / `travel_university` / `travel_future` went with the agent's active Sim, not the template's YA+ role | the engine sends `sims: [<role's Sim>]` for vacation and University; `travel_future` takes `sim` (agent: `Util.FindSim` when given, else the active Sim); the three travel templates name their role (`travel-future` gained a YA+ `traveller`). Travel stays off unless `travel: true` (§13) |
| 4 | the 30 s `test_env` guard turned winter back to spring 15 times in the run (`test_env guard: winter or extreme cold -> spring`) | queue `run.profile.envGuard: false` -> `profile-world --no-env-guard`: `test_env` corrects the save once at the start, then seasons, weather and moons pass as in play (R-EP-3); season/moon prepares become possible (`envFree`). `queues/story-days.json` sets it; short timing jobs keep the guard |
| 5 | Hyacinth Atkins (Elder, a townie) got 32 `old_age` rescues on day 18: `CancelAgingAlarmsForSim` does not change `NumDaysBeforeAging`, so the guard fired every 30 s; the elder's daily death roll (`AgingManager.SimIsCloseToAging`, `GetChancePerDayElderWillDie`) re-adds the alarm [verified: decompiled `AgingManager`] | old age is natural play (R-LIFE-1): the guard leaves it alone unless the elder is in a running story (then one rescue per sim day, so the story is not cut); an old-age death is logged `natural: true` with `cause: "OldAge"` and counts in `deaths.natural`, not as unscripted, so the pass rule and the report accept it. Agent: `story_day_state` guard facts gain `death_type` (`SimDescription.DeathStyle` once dead, else the queued `Urnstone.KillSim`'s `simDeathType`; `AgeTransitionWithoutCake` kills elders with `OldAge`) |
| 6 | Morrigan Hemlock (Criminal, 23:00 shift, carpool 22:00) missed day 17: story `social-visit-community` #21 sent her to a community lot at ~16:00 (sim_visit_lot) and was still running the next morning (Friendly Introduction ~07:30 day 18); `go_to_work` at 23:00 was queued behind the story's own interactions; the carpool comes to the home lot; the day planner's work block ended at midnight, so day 18 had no block to see the shift that started late | ours (the planner): (a) a running story ends for a Sim whose day plan reaches its work or school block (skip line `work time: <Sim> leaves the story`); (b) an overnight shift still running when a day's plan is made is a work block `[0, finish]` followed by up to 6 h sleep; (c) attendance and `go_to_work` count the after-midnight part for the day the shift started |
| 7 | pursuits done 0.11 per workday Sim-day, 0.55 per free Sim-day: of 128 planned pursuits 55 were never tried (a pursuit ran only on the tick its block was entered and the Sim was free: a story held the Sim then, or the block passed between ticks) and 54 of 73 tries failed with `the game's autonomy found nothing for skill X` (Cooking 13, Charisma 10, Photography 9, Logic 5, ...; 8 of the 10 Sims were townies, often away from their own lot [inferred, not checked per try]); the failed skill then went back to the backlog front every day (carried 109) | (a) the earliest pursuit or homework of the day not tried yet whose block has begun runs in the next free, pursuit or homework tick (late, logged `late: true`); (b) a pursuit the game finds nothing for: up to 2 other backlog skills not planned today (`instead: <skill>`; a player picks another hobby when the house has no object); (c) failed skills go to the backlog's back, not the front. Household-only driving (1) keeps the Sims at home with their own objects. Cooking advertises no `SkillCooking` commodity in Generic searches [inferred from 13 of 13 failing; cooking skill comes from meals] |
| 8 | story-days rerun: pursuits still mostly `found nothing` (Cooking 5+3, Charisma 5+4, Fishing 5, Athletic 4, Photography 2), free 0.13 vs work 0.30 per Sim-day (pass rule fails). Why [verified, static: ITUN of `GameplayData.package` + `gameplay.package` (4,128 read with `tools/extract-assemblies.ts` `readIndex`/`refpack`) and `Autonomy.cs`]: `FindBestActionForCommodity` scores only the commodity map (`CommodityChange.ValidForCommodityInteractionMap`: advertised > 0) of the current lot (world lot = closest lot); Mirror and VanityDresser `PracticeSpeech` advertise `SkillCharisma` 420 but are `DisallowAutonomous`; the fridge's `Fridge_Have` advertises Hunger 75 and `Fridge_Prepare` trait commodities, `SkillCooking` sits only on the pan step (`FryingPan_CookOnStove`) and store objects; `FishHere` (300) is on fishing spots, not home lots; nothing advertises `SkillPhotography` (camera `TakePhoto` is player-aimed); Athletic needs `AthleticGameObject` `WorkOut` equipment (`JogHere` is a terrain spot). No flag (BuffAutoSolve etc.) changes this | (a) `sim_pursue` the player's way: the home lot's objects scanned for interactions whose tuning outputs the skill (advertised or actual > 0), plus the fridge's non-snack `Fridge_Have <recipe>` for Cooking; the best one whose user-directed `Test` passes is queued at UserDirected (`how: home object`), else the old autonomy search (`how: autonomy`); (b) new `sim_practice {sim, skills}` census, once per Sim per planned day: the day plans only supported skills, topped up with supported hobbies to the day's cap (free day 3, workday 1); (c) a backlog skill with no object and a `BUY` query (easel, chess, instrument, gym, computer, stove) is bought once per run in Buy mode, paid, through the object-prepare flow (`buyInBuyMode`, mode-lock owner -2), at most 2 per run, logged `kind: buy`; else another skill. Charisma (mirror), Gardening, Fishing, Photography are never bought. Ledger §301 |

**Checks.** `story-fuzzer.ts` self-check (household mode: only household Sims act, townies as partners; `drive: world` orders
townies; prepares buy in Buy mode, paid, `max_price` = funds; travel sends the role's Sim), `--check`, `story-day.ts` (overnight
block, failed-to-back carry, old-age rescue only in a story and once per day, natural death passes, stand-in pursuits after a
failure, no story step on a Sim more than one tick into its work block: 1 story left for work in the fake run), `story-lint.ts
--self-check` (buy/sell in Buy mode ok, `make_sim` in Buy and `buy_object` in Build refused), `realism.ts --self-check`,
`story-report.ts --self-check`, `queue.ts --self-check` (`envGuard`), `queue.ts --dry-run queues/story-days.json`
(`--no-env-guard`), agent built (`build/ts3agent.package`). Not run in game: everything above; the next `story-days` run shows
the household-only plan (2 BP Sims), `buy_object` inside Buy mode, seasons passing, and old-age deaths as natural.

## 17. Off-camera life, story progression on, household rotation (2026-10-23; static, self-checks, agent build; no game run)

Study and verdict in docs/180 (ledger §300): EA's built-in story progression (simler90's patched copy on this install), not NRaas
StoryProgression or MasterController (neither installed; NRaas SP slices its work at 100 ms every 10 sim minutes and has an unyielding
world-wide object scan). Story queues (`story-days`, `story-smoke`) drop `-disablestoryprogression`; timing queues keep it.

- `days.town` (default on): agent `town_snapshot` once per calendar day, diffed into a `town` line (births, arrivals, deaths, gone,
  moves, jobs, promotions, marriages, partners of Sims outside the active household); a `clock` line per sim hour.
- `story-report.ts`: `### Off-camera life` table, rotation list, and hitches attributed to story progression (sim time 1:00-1:30 from
  the `clock` lines, or a StoryProgression script task).
- `days.rotate: N` (off by default, 1 in `story-days.json`): between sim days, after the running stories end (never cut), agent
  `switch_household` (Edit Town's Change Active Household; refused on vacation, University, future and where Edit Town is refused)
  under the mode lock; the new household becomes the driven set. Config check accepts `town` and `rotate`.
- Templates (natural, labels unverified): `social-phone-invite-over`, `social-phone-outing`, `social-phone-party`.

Checks: `story-day.ts` (townDiff unit, fake run: Landgraab baby, one marriage, one gone, clock lines; rotate run: switch only with no
story running, steps after it only on the new household), `story-report.ts --self-check` (Off-camera table, rotation, 1 AM hitch),
`story-fuzzer.ts` + `--check`, `story-lint.ts` (+ `--self-check`), `story-census.ts`, `realism.ts --self-check` (152 commands),
`queue.ts --self-check`, `--dry-run queues/story-days.json`.

### 17.1 Review of story-days 2026-10-23 13:16 PHT (chain152; logs, self-checks, agent build; no game run)

Ledger §303. Three report items from `story-days-2026-10-23-bp-story-days`:

- Rotation did happen: 7 `kind: rotate` lines (each attempt logs `ok` and, when refused, `error`), 6 switches (Belle, Custard,
  Shutter, Atkins, Pala, Slayer), day 17 refused ("stories still running after 180 s: next day"); the report lists them under the
  Off-camera table. The queue log printed only successes: a refused attempt now prints too, and the `days:` line adds the rotation
  count.
- The raw id in "missed work/school on day 18" was Caroline Custard: rotated in at 12:15, after her Journalism carpool had left
  (12:00 PM, shift 13:00); the summary named Sims through the current driven set, so rotated-out Sims showed as ids. Now the
  summary uses the name stored per Sim, and a work/school day is due only if the planner had the Sim by shift start - 1 h (the
  carpool time); a Sim handed over later is not counted missing.
- The Off-camera table counted EA's stray pet pool (PetHousehold, 48 in / 48 out in 7 days). `town_snapshot` now flags pool
  households (`pool`: pet, alien, mermaid, travel; service and tourist were already skipped), `townDiff` tallies their churn in
  `pools` (an adoption out of the pool stays a move), and the report shows one `Pool households` line; for older logs it filters
  `(PetHousehold)` by name.

Checks: `story-day.ts` (pool diff unit, attendance run: handed over after the carpool not due, names after rotation),
`story-report.ts --self-check` (pet pool out of the table, pool line, old-log fallback), `story-fuzzer.ts --check`, `story-lint.ts`,
`realism.ts --self-check`, `queue.ts --self-check`.
