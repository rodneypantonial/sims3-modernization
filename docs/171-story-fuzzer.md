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
