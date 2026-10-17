# 75. Autonomy entry list: interaction definitions ranked by sampled scoring time (PLAN gap 12, step 12c)

2026-10-16. Managed sampler data already on disk plus the decompiled script assemblies. No game run, nothing built, `mods/` untouched. Input for 12e (allow-list pilot) and 12f (widening), and a correction to the stage C scope in docs/59 §3.

Tags: [measured] = counted in the managed samples below; [verified] = read in the decompiled script DLLs (`game-copy/decompiled/`, not copied here); [inferred] = reasoning from both, not checked live.

## 1. Answer

- **Scoring samples.** 1,033 of 21,746 managed samples (4.75%, ~47 ms/s) have `Autonomy.CalculateScore` (docs/59 C2, the per-pair test and score) on the stack [measured]. 841 of them run in the `AutonomyManager` task (3.87%, ~39 ms/s), 192 in side entry points (164 `FindBestAction*`, 22 posture transitions, 2 reactions) [measured]. The 39 ms/s is the C2 part of docs/59 §3.3's T (estimated ~44 ms/s with the B9-B14 driver) [measured share, ms inferred].
- **Meta autonomy is most of it.** 670 of the 841 manager-task scoring samples (80%) score meta objects (`ScoreInteractionsForMetaObject`: community lots, rabbit holes, garden), 164 (20%) score the local lot [measured]. Across all callers: meta 65%, local commodity 28%, solo 3.4%, posture 2.1%, social 1.1% [measured].
- **Only 44% of scoring samples can be charged to a definition type** from the stack: 56% sit in the shared base path (`CheckAvailability` alone 27%, `CommonTests` 11%), which every pair runs before the definition's own code [measured]. A further 10% are in a definition's own frame that the sampler names only as `Definition.Test`, and the callee frame does not narrow it to one type ("unresolved" rows).
- **Coverage of the resolvable own time** (350 samples, 33.9% of scoring) [measured]:

| Share of resolvable own time | Concrete types needed | Share of all scoring samples |
|---|---|---|
| 50% | 6 | 20.0% |
| 80% | 13 | 28.3% |
| 90% | 15 | 30.5% |
| 95% | 27 | 32.2% |
| 100% | 53 | 33.9% |

  The first 13 types are all meta definitions: the 6 venue-scored lot visits (`VisitCommunityLot`, its date variant, `VisitCommunityLotWith`, `VisitLot`, `VisitLotWithDate`, `GoToCommunityLot`), the 6 venue-scored rabbit-hole visits (restaurant, city hall, hospital, stadium, shopping, show venue) and `GoToGarden`. Local definitions form a long tail: no local type has more than 14 samples (1.4%).
- **For 12e:** the top ~20 pilot should be the meta definitions (§4), not local ones. **For the docs/59 §3.3 break-even:** with meta kept on the sim thread, as the docs/59 §3.2 stage C flow says, f is at most ~0.20 even if every local definition is allow-listed, which is below the best-case f_min of 0.26 (0.29 with the measured T). Stage C as written cannot break even. With the meta phase on the worker, ~15-20 meta types reach f ≈ 0.75-0.8 [inferred, §5].

## 2. Data and method

- **Samples:** all 26 `probe-out/profile/*-managed.jsonl` and `managed-*.jsonl` files (BP, IP, SV, worker, mainstack, S3PP cache and seeded-RNG runs, 2026-10-12 to 2026-10-16; 21,746 samples, 15,875 with a managed stack). One C# stack per line, innermost first (`tools/probe/managed-sample.mjs`). Managed samples from before the 2026-10-16 simstack fix are valid (the fix was to the native simstack sampler; docs/73 §2 and docs/74 §2 use them the same way). The appended s3pp-cache files are counted whole. 221 of the 1,033 scoring samples come from 2026-10-16 runs; their top shares match the older ones within sampling error (`CheckAvailability` 23% vs 28%, venue lot family 12% vs 13%, `VisitCommunityLot` 8% vs 7.5%) [measured].
- **Tool:** `node tools/autonomy-entry-list.ts --samples <main checkout>/probe-out/profile --src <main checkout>/game-copy/decompiled` writes `docs/data/autonomy-entry-list.csv` and prints the coverage table. It runs a self-check first.
- **Attribution per sample.** Scoring sample = stack contains `Sims3.Gameplay.Autonomy.Autonomy.CalculateScore`. Walking inward from it, the sample is charged to the **outermost frame of a concrete definition class** (any class that derives from an `*InteractionDefinition` root). Frames of the generic base classes (`InteractionDefinition<,,>`, `SoloSimInteractionDefinition<>`, `MetaInteractionDefinition<,,>` and similar) are skipped, since every pair passes through them. Without a definition frame: a tuning scoring function (`ScoringFunctionDelegate.Invoke` callee) gets its own row; otherwise the sample goes to a "base path" row named after the first non-base callee of C2.
- **Short names.** The sampler names nested types by their short name only (`Definition.Test`, `CallDefinition`1.Test`); about 4,500 decompiled definition classes are called `Definition` and have a `Test`. Candidates are narrowed by (1) the next callee frame: the candidate's method body must name it; (2) the search context: meta searches keep `MetaInteraction*` subclasses, others drop them, social searches keep social types. One candidate left: resolved. 2-4: an "ambiguous" row listing all of them. More: an "unresolved" row with the candidate count and callee.
- **Venue family.** `MetaInteractionScoredByVenue<,,>` overrides `CalculateScore` once for 15 meta definitions. Its rows are split by the callee (`Lot.` or `RabbitHole.HowMuchItWantsSimToCome`), and the CSV column `concrete_types` lists the subclasses whose target type matches (6 lot types, 6 rabbit-hole types) [verified in the class hierarchy].
- **CSV columns:** rank, definition type (nested types as `Outer+Inner`), kind (`definition`, `family`, `score-override`, `scoring-function`, `base`), assembly, samples, share of scoring samples, pre/post 2026-10-16 counts, resolution, `Test` overridden, score overridden (`yes`, `inherited` from a base with `CalculateScore`, `no`), pure-read guess, context split, top callees, concrete types (family rows).
- **Pure-read guess:** a one-level text scan of the type's own `Test` and `CalculateScore` bodies, using the docs/59 §2.4 classes (M managed reads, H hot-set ICs, P other PROXY queries, W writes or random, Y yields). Helpers in other classes are not followed, so this is a pre-filter only; 12e builds the real closures [inferred].
- **Error:** with n samples in a row, the relative error is about 1/√n: ±6% for the 280-sample row, ±25% at 16 samples; rows under ~5 samples are ordering noise [inferred from Poisson counting]. 1% of samples ≈ 10 ms/s of sim-thread time (5 Hz wall-clock sampler, docs/28) [inferred].

## 3. Top 50

Short names drop the `Sims3.Gameplay.` / `Sims3.Store.` prefix. Asm: GS = Sims3GameplaySystems, GO = Sims3GameplayObjects. "Pre / post" = samples from runs before / on 2026-10-16. All counts [measured]; resolution of short names and the guess column [inferred].

| # | Definition type (or bucket) | Kind | Asm | Samples | % C2 | Pre / post | Test ov. | Score ov. | Guess | Context |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | base path: `Autonomy.CheckAvailability` | base |  | 280 | 27.1 | 229 / 51 |  |  |  | meta 205; commodity 61; solo 11 |
| 2 | ``Interactions.MetaInteractionScoredByVenue`3`` (target Lot) | family | GS | 128 | 12.4 | 102 / 26 | no | yes | M | meta 128 |
| 3 | base path: `InteractionDefinitionUtilities.CommonTests` | base |  | 112 | 10.8 | 88 / 24 |  |  |  | meta 77; commodity 25; solo 8 |
| 4 | `Core.VisitCommunityLot+Definition` | definition | GS | 79 | 7.6 | 61 / 18 | yes | inherited | M | meta 79 |
| 5 | `Definition.Test` unresolved: 316 candidates, callee base `Test` | definition |  | 61 | 5.9 | 41 / 20 |  |  |  | commodity 61 |
| 6 | base path: C2 self | base |  | 56 | 5.4 | 46 / 10 |  |  |  | meta 30; commodity 18; solo 7 |
| 7 | `Core.VisitLot+Definition` | definition | GS | 41 | 4.0 | 28 / 13 | yes | inherited | M | meta 41 |
| 8 | base path: `Autonomy.CalculateScoreForObjectInteraction` | base |  | 30 | 2.9 | 23 / 7 |  |  |  | commodity 22; posture 5; solo 3 |
| 9 | base path: `GameObject.get_Parent` | base |  | 29 | 2.8 | 25 / 4 |  |  |  | meta 23; commodity 5; solo 1 |
| 10 | ``Interactions.MetaInteractionScoredByVenue`3`` (target RabbitHole) | family | GS | 28 | 2.7 | 25 / 3 | no | yes | M | meta 28 |
| 11 | base path: `InteractionDefinitionUtilities.SpecialCaseTests` | base |  | 22 | 2.1 | 15 / 7 |  |  |  | commodity 21; solo 1 |
| 12 | `Autonomy.GoToGarden+Definition` | definition | GS | 16 | 1.6 | 13 / 3 | yes | inherited | P | meta 16 |
| 13 | `Actors.Sim+CallPet+Definition` | definition | GS | 14 | 1.4 | 8 / 6 | yes | no | W | commodity 14 |
| 14 | `Definition.Test` unresolved: 6 candidates, callee `Sim.IsNeighborOfPlayerWhoIsLastOneAtHome` | definition |  | 11 | 1.1 | 5 / 6 |  |  |  | meta 11 |
| 15 | base path: `MetaInteractionDefinitionUtilities.SpecialCaseTests` | base |  | 10 | 1.0 | 8 / 2 |  |  |  | meta 10 |
| 16 | scoring function `SitHereForEating.EatHereScoringFunction` | scoring-function |  | 9 | 0.9 | 8 / 1 |  |  |  | commodity 9 |
| 17 | base path: `.ctor` (parameters struct) | base |  | 9 | 0.9 | 7 / 2 |  |  |  | commodity 6; meta 3 |
| 18 | base path: `Autonomy.UpdateCacheIfNeeded` | base |  | 7 | 0.7 | 5 / 2 |  |  |  | posture 7 |
| 19 | base path: `LiveDragHelperModel.get_CachedTopDraggedObject` | base |  | 7 | 0.7 | 6 / 1 |  |  |  | posture 6; meta 1 |
| 20 | base path: `Autonomy.CheckAvailabilityTooltip` | base |  | 6 | 0.6 | 5 / 1 |  |  |  | meta 6 |
| 21 | `Definition.Test` unresolved: 152 candidates, callee `ActionData.Test` | definition |  | 4 | 0.4 | 4 / 0 |  |  |  | social 4 |
| 22 | `Definition.Test` unresolved: 37 candidates, callee `InteractionQueue.HasInteractionOfType` | definition |  | 4 | 0.4 | 4 / 0 |  |  |  | commodity 4 |
| 23 | ``Objects.Electronics.Phone+Call+CallDefinition`1`` | definition | GO | 4 | 0.4 | 4 / 0 | yes | no | H | commodity 4 |
| 24 | `Socializing.InviteToJoinConversation+Definition` or `Socializing.SocialInteractionA+Definition` | definition |  | 3 | 0.3 | 3 / 0 |  |  |  | social 3 |
| 25 | base path: `Motives.FunInteractionTest` | base |  | 3 | 0.3 | 2 / 1 |  |  |  | commodity 3 |
| 26 | scoring function `GameObject.SitInteractionModifier` | scoring-function |  | 3 | 0.3 | 3 / 0 |  |  |  | commodity 3 |
| 27 | `Definition.Test` unresolved: 4,566 candidates, callee `Definition.Test` | definition |  | 3 | 0.3 | 3 / 0 |  |  |  | commodity 3 |
| 28 | ``Objects.HobbiesSkills.BandInstrument+JamWithInstrument`1+JamWithInstrumentDefinition`1`` | definition | GO | 2 | 0.2 | 2 / 0 | yes | no | P | commodity 2 |
| 29 | `Definition.Test` unresolved: 33 candidates, callee `ActionData.Get` | definition |  | 2 | 0.2 | 2 / 0 |  |  |  | commodity 2 |
| 30 | `Definition.Test` unresolved: 4,566 candidates, self | definition |  | 2 | 0.2 | 2 / 0 |  |  |  | commodity 2 |
| 31 | base path: `Sim.get_IsSelectable` | base |  | 2 | 0.2 | 1 / 1 |  |  |  | meta 2 |
| 32 | `Definition.Test` unresolved: 34 candidates, callee base `Test` | definition |  | 2 | 0.2 | 2 / 0 |  |  |  | social 2 |
| 33 | `Definition.Test` unresolved: 10 candidates, callee `Definition.CanSocializeWith` | definition |  | 2 | 0.2 | 2 / 0 |  |  |  | commodity 2 |
| 34 | `Definition.Test` unresolved: 7 candidates, callee `Sim.AbleToNap` | definition |  | 2 | 0.2 | 2 / 0 |  |  |  | commodity 2 |
| 35 | `Actors.Sim+Sim_ReadToSleep_Autonomous+Definition` | definition | GS | 2 | 0.2 | 2 / 0 | yes | no | M | commodity 2 |
| 36 | `Definition.Test` unresolved: 33 candidates, callee `Queries.CountObjects` | definition |  | 2 | 0.2 | 2 / 0 |  |  |  | solo 2 |
| 37 | `Definition.Test` unresolved: 36 candidates, self | definition |  | 2 | 0.2 | 1 / 1 |  |  |  | meta 2 |
| 38 | hot tub get in, skinny dip, `GetInPool`, `SwimHere` (4 candidates) | definition |  | 2 | 0.2 | 0 / 2 |  |  |  | commodity 2 |
| 39 | `Definition.CalculateScore` unresolved: 20 candidates | score-override |  | 2 | 0.2 | 1 / 1 |  |  |  | commodity 2 |
| 40 | `Actors.Sim+GoToVirtualHomeMeta+Definition` or `Core.GoHome+Definition` | score-override |  | 2 | 0.2 | 0 / 2 |  |  |  | meta 2 |
| 41 | ``Interactions.MetaInteractionScoredByVenue`3`` (target unknown) | family | GS | 1 | 0.1 | 1 / 0 | no | yes | M | meta 1 |
| 42 | `Objects.RabbitHoles.AutonomouslyGoForMealInRestaurant+Definition` | definition | GO | 1 | 0.1 | 1 / 0 | yes | inherited | M | meta 1 |
| 43 | `Definition.Test` unresolved: 42 candidates, callee `Inventory.Find` | definition |  | 1 | 0.1 | 1 / 0 |  |  |  | commodity 1 |
| 44 | `Definition.Test` unresolved: 445 candidates, callee `GameObject.LotCurrent` | definition |  | 1 | 0.1 | 1 / 0 |  |  |  | commodity 1 |
| 45 | base path: `Sim.get_Motives` | base |  | 1 | 0.1 | 1 / 0 |  |  |  | commodity 1 |
| 46 | ``Objects.HobbiesSkills.BandInstrument+Perform`1+PerformDefinition`1`` | definition | GO | 1 | 0.1 | 1 / 0 | yes | no | H | commodity 1 |
| 47 | werewolf scratch, freak out at object, scratch object (3 candidates) | definition |  | 1 | 0.1 | 1 / 0 |  |  |  | commodity 1 |
| 48 | `Actors.Sim+CallOverToDance+Definition` | definition | GS | 1 | 0.1 | 1 / 0 | yes | no | M | commodity 1 |
| 49 | frugal light off, terrain use stairs, cast spell (3 candidates) | definition |  | 1 | 0.1 | 1 / 0 |  |  |  | commodity 1 |
| 50 | `Definition.Test` unresolved: 10 candidates, callee `Sim.IsDying` | definition |  | 1 | 0.1 | 1 / 0 |  |  |  | commodity 1 |

Rows 51-68 (1 sample each) are in the CSV.

Notes on the top rows:
- **Rows 2, 4, 7, 10: the visit venues.** The venue family's score override asks the target how much it wants the Sim to come (`Lot.HowMuchItWantsSimToCome`: desired Sims per meta object, meta-autonomy tuning, today's scheduled gig, Sims going to or at the lot) [verified call chain, measured samples]. The visit `Test` bodies spend their time in shared visit tests (`Lot.GetHoursOfOperation`, a localized tooltip string, `IsNeighborOfPlayerWhoIsLastOneAtHome` which counts objects on a lot, `CommonVisitTest` with a callbox search) [verified call chain]. `GetHoursOfOperation` is already the subject of a stage-A memo in docs/74 §6.3.
- **Row 1: `CheckAvailability`** is 73% meta (205 of 280); its biggest callee chain is `CheckAvailabilityOnLot` → `Sim.IsAllowedInRoom` (137 samples in the stack-chain count), then the tomb-room check (35) [measured]. Meta pairs target a lot, so the allowed-room lookup runs per lot per Sim.
- **Row 5** (61 samples) is a definition that overrides the parameter-level `Test` and calls the base `Test`, sampled inside the base path. The stack carries no more information; 316 types fit. Only a counter keyed by runtime type can split it (§6).
- **Row 13: `CallPet`.** Its `Test` stores a result list in a field of the shared definition object (W class, not allow-listable as is) and builds it from `Queries.GetObjects<Sim>()`, every Sim in the world, per test [verified]. A per-tick cache would be a stage-A candidate; not designed here [inferred].

## 4. What it means for 12e (pilot of ~20 definitions)

Pilot list, in this order [measured ranking, inferred choice]:
1. The 6 venue-scored lot visits (row 2 `concrete_types`), which include rows 4 and 7.
2. The 6 venue-scored rabbit-hole visits (row 10).
3. `GoToGarden` (row 12).
4. The meta definitions that the sampler did not separate: the 6 candidates of row 14, `GoHome` and `GoToVirtualHomeMeta` (row 40), and the other meta definitions in the base game (`GoToLot`, `GoInsideLot`, `GoOutToYard`, `StayHome`, `TakeFoodToLot`, the passport and pro-sports rabbit-hole metas): about 42 classes in all derive directly from the two meta definition bases [verified by name scan], so the whole meta set is close to the ~20-type budget and can be closed as one batch.
5. Local definitions only after that: `CallPet` (W: expect a reject), the phone `CallDefinition`, the sit/eat scoring functions. These buy little: the largest local type is 1.4% of scoring.

Closure roots for the venue family: the family's `CalculateScore` override plus each subclass's `Test`; plus the meta base path (`MetaInteractionDefinitionUtilities.SpecialCaseTests`, `CheckAvailability` with a lot target). The callees above (lot hours, gig schedule, `CountObjects`, callbox search, localized tooltip) mean most of the meta set will be class P and needs per-lot snapshot fields beyond docs/59 §3.4's per-object rows: hours of operation, scheduled gig, Sims going to or at the lot, desired Sims per meta object, callbox presence [inferred].

## 5. What it means for the docs/59 §3.3 break-even

f is the share of the worker-eligible scoring time spent in allow-listed pairs. A pair's base-path time moves with the pair, so f counts the base path of allow-listed pairs too.

| Stage C scope | Max f (manager-task C2 = 841 samples) | Against f_min | Tag |
|---|---|---|---|
| docs/59 §3.2 as written: local only, meta and socials on the sim thread | 164 / 841 = **0.20** with every local definition allow-listed | below the best case 0.26; with the measured T ≈ 39 ms/s instead of 44, f_min = (1.5 + 3 + 7) / 39 = 0.29 best, 0.97 worst | measured shares, inferred costs |
| Meta phase (B13-B14) on the worker, local on the sim thread | 670 / 841 = **0.80** with every meta definition allow-listed | above the best case, near the worst case | measured shares, inferred costs |
| Meta pilot of ~15-20 types (§4 items 1-4) | **~0.75-0.8**: the 13 named types hold 292 of the 312 meta samples that resolve to a type (94%), and the 358 meta base-path samples belong to pairs of the same small meta set | as above | inferred: base-path time is assumed to follow the pairs, and pairs per type were not counted |

Consequences [inferred]:
- **The worker should score meta, not local.** The docs/59 §3.2 rule "meta stays on the sim thread in C" removes 80% of the eligible time. The meta search already yields between meta objects (docs/59 §1.3 Y2) and the snapshot design already has a per-meta-search scope (docs/59 §2.5), so the change is to the stage C scope, not to the snapshot idea. The yield and its abort checks (search interrupted, actor destroyed) would become apply-time result rules (docs/59 §3.5). docs/59 is left unchanged here; the scope change is for 12e to confirm when it closes the meta set.
- **A meta-only allow-list is small.** ~20 types (plus the meta base path) instead of the ~50 per batch 12f planned, and 12f widening into local definitions has a poor return: 53 resolvable types cover only 34% of scoring, and the local tail is flat.
- **Stage A competes for the same time:** `CheckAvailability` → `IsAllowedInRoom` on lot targets and the venue score (`HowMuchItWantsSimToCome`, `GetHoursOfOperation`) are about 15-25% of scoring between them. Any in-place cache there lowers T and raises f_min (docs/59 §3.3 last bullet).

## 6. Gaps and follow-ups

- **Pairs per type are not measured.** The sampler sees code, not the definition object, so the base-path 56% and the unresolved 10% cannot be split by type. A Verify counter at C2 entry keyed by the definition's runtime type (pairs tested, ticks spent) would replace every inferred number in §5; it needs a counter slot in `docs/registry.md` and a profile run. Proposed for 12e, not reserved here.
- **Worlds and households:** all runs are the BP, IP and SV test saves. The meta share depends on how many community lots and rabbit holes a world has and on how many NPCs run meta searches; a world with more venues would raise it [inferred].
- **Mods:** ts3fixes hooks the `Run` of `VisitCommunityLot`, `VisitLot`, `GoToCommunityLot`, `GoToLot` and `GoHome` (lot arrival fixes), not their `Test` or score, so it is off the scoring path [verified in `mods/ts3fixes/src/Fixes.cs` and its README]. Per docs/59 §3.6 construction step 5, our hook bodies still belong to a closure if they are reached. NRaas-style replacements change runtime types and drop them from any allow-list (docs/59 §3.6), so the ranking holds for an unmodded script set only [inferred].
- Ledger: the claims "meta = 80% of manager-task scoring" and "local f ≤ 0.20" rest on the 841-sample count above and should be rechecked with the per-type counter before 12e commits to the meta-first scope.
