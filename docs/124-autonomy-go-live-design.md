# 124. Autonomy go-live: using the worker's meta scores on the sim thread (design)

Written 2026-10-20. Static design only: no build, no game run, no queue. It answers the open decision of docs/118 §3 ("go-live, scores used from the worker, is a separate decision after G6, with the docs/59 §3.5 staleness rules") and of docs/122 §7 (after G-A6). It assumes the docs/122 shadow pilot (`[AutShadow]`, Modes 0/R/W) has passed G-A3 to G-A6 in game; none of them has run yet (PLAN.md, docs/122 §14.7).

Inputs: docs/118 (plan, G0-G6), docs/122 (shadow pilot as designed and built, §1.2, §4.2, §14), docs/121 (IC closure, RNG §4.1), docs/123 (world image, offline Mode R), docs/59 (call graph §1.2, yields §1.3, worker flow §3.2, break-even §3.3, staleness §3.5, allow-list and mods §3.6, failure modes §3.8), docs/75 (meta = 80% of manager scoring), docs/97 (§1 net estimate), docs/76 and docs/79 (change events), docs/73 §5 (events stay on the sim thread), docs/104 (mod guard), docs/27 (script RNG), docs/30 + docs/120 (second Mono thread).

Tags: **[verified, docs/N]** = verified there, not re-read here; **[inferred]** = reasoning still to confirm (ledger docs/51 §166, I-124-*); **[design]** = a choice made here. Effort in CLAUDE.md units (wall-clock for Claude with agents; game runs and clean nights listed apart).

## Summary

- **Hand-off point [design]:** the meta phase of a manager search. At entry of `Autonomy.ScoreInteractionsForMetaAutonomy` (docs/59 B13), the existing ts3fixes B13 hook (docs/122 §14.5) posts the request and yields once (`Sleep(0)`, the same kind of yield B13 already makes at its Y2 sites). The cut runs at the return of `MonoScriptHost_Simulate` `0xD81840` of that tick [verified, docs/122 §14.1], the worker scores in the native tail, and the result is marked ready at the top of the next Simulate. When the manager task resumes in tick t+1, the hook runs the **game's own B13** (`Orig`). A new hook on **B14 `ScoreInteractionsForMetaObject`** consumes the result per meta object: for an object with a valid worker result it sends only the selected ads through the game's own C1 `CalculateScoreAndAddToCandidates`, so they are tested and scored **live** and enter `BestScores` exactly as in vanilla. Every other object runs the game's B14 unchanged.
- **Selected ads = the worker's top N (default 20) passing pairs** across the whole meta list (none of the 12 definitions has `AlwaysChooseBest` in the shipped tuning, §13.3; a pair whose runtime tuning has it sends its object to the game's B14), plus a margin check that pulls in the next band when the live scores come out lower than the worker predicted. `BestScores` keeps the top 10 (docs/59 D1), so every candidate D2 draws from has been tested and scored on live state. Staleness can only drop a pair the worker rejected or ranked low on a one-tick-old view; the next search sees it again.
- **Staleness:** result level (age > 1 tick, checked again at every B14 call because B13 can yield between objects, §13.1; world epoch, actor destroyed, lot or LOD change, head interaction, interrupted search, an actor field compare instead of an event listener, §13.4; tuning generation, save/travel/build) means the whole result is dropped and the game's B13 runs as today. Object level (object events `6`, `0x0B`/`0x0C`, `0x18`, `0x21`, `0x12` for the object; per-lot generation from object events `4`/`6` and world event `0x11`; lot events `0x0D`-`0x10`, `0x46`) means that object runs the game's B14. Pair level is covered by the live re-test of every selected pair.
- **RNG:** the decision draws (D2 choice, D4 interrupt chance) stay on the sim thread. Pet actors abort on the worker (`rng`) and score live, in B13 order. What changes are the tooltip draws in `Localization.FillInTokens` for pairs whose `Test` failed on the worker: vanilla makes them and throws the text away, go-live skips them. Picks stay **equal in distribution**, not path-equal; seeded runs (docs/27 `seed_rng`) repeat only within one go-live setting.
- **Mods:** everything docs/122 §4.1 and §14.5 guard (assembly MVIDs, per-method L3 integrity, exact runtime types) plus the consumer path itself: 18 methods, 13 of them new (§13.5); a hit there turns go-live off for the load. Side searches from mods (NRaas StoryProgression and others) never reach the hand-off and invalidate an in-flight result, as vanilla aborts a parked search.
- **Gates:** G7 dry apply (predicted top 10 vs the game's own search on the same tick), G8 behaviour in distribution (seeded on/off), G9 cost and stability (clean-night ABBA plus two clean runs per world).
- **Effort:** ~10-18 h on top of the pilot, **9-11 game jobs and 1-2 clean nights** (§10). Net sim-thread effect -18 to +10 ms/s [inferred, §8.3]: an offload step under the 2% rule, not a speed win, as docs/59 §3.3 and docs/97 §1 already said.
- **Top risks:** net cost (M5 fixed cost plus the per-search cut at `Sample=1`), staleness bias in which pairs reach the top 10, and M5 in game (the worker has not interpreted in the game yet; the P4 TLS fix landed in 2f4c595) (§11).

## 1. Starting point

| Have (docs/122, built offline) | Missing for go-live |
|---|---|
| B13 request hook, sampled; request = actor, commodity, search type, flags, meta object list in B13's order | a yield between request and use, and a consumer |
| Cut + window on `[MonoThread]`'s Simulate slot stub; worker via `MtSubmit`; result arrays `sWork` per (meta object, ad): score, code; per object meta score | "result ready" published before the script tick of t+1 (today `finish` runs at a later Simulate return) |
| Epoch drop and overrun at the next Simulate entry (`LastRun` 1/2) | object-level and actor-level invalidation queries for managed code |
| Mode R reference with the MT state at host `+0xC30` saved and restored [verified, docs/122 §14.1] | sampled reference while live, to keep catching bugs |
| Mod guard: MVIDs + L3 over the driver's callees | L3 over the consumer path |

## 2. Hand-off point

### 2.1 Options

| # | Where the result is used | For | Against | Verdict |
|---|---|---|---|---|
| H1 | Precompute the **next** search of the same Sim while it waits in the queue | no extra yield | the result is many ticks old by the time it is used (a Sim waits ~N ticks with N Sims queued, docs/59 §1.1); motives, Sims and lots move | no |
| **H2** | **B13 entry of the same search**: post, yield once, use at resume one tick later | age 1 tick; B13 already yields (Y2), so the manager task is already a legal yielding context there; the game's own B13 runs around the consumer (gates, B15, B16, go-home pairs, Y2, abort checks unchanged) | one tick of extra latency for meta searches that would have finished in one slice | **chosen** |
| H3 | Take the worker's top-1 and push it (skip D2) | cheapest | not vanilla: D2's weighted random pick, `AlwaysChooseBest` and the local-phase scores are lost | no |
| H4 | Whole search (local + meta) on the worker, docs/59 §3.2 | more work moved | local phase is 20% of manager scoring and a long tail of definitions (docs/75 §1); its non-yielding slice cannot wait a tick | later, separate |

### 2.2 Timeline of one search (H2)

| Tick | Thread | Point | Work |
|---|---|---|---|
| t | sim, manager task | B4 prologue, local phase B9-B12 | unchanged; local pairs enter `BestScores` |
| t | sim, manager task | **B13 entry, hook prefix** | if eligible (§2.3): `Begin`, `AddRow`, `Post` as docs/122 §14.1; then `Simulator.Sleep(0)` |
| t | sim, native | return of `0xD81840` | cut (snapshot update, version), optional sampled reference (§7.1), `MtSubmit` |
| t → t+1 | worker | native tail of t (animation, locomotion, publish, waits) | `AutShadow.ScoreMeta` on the snapshot [verified as built, docs/122 §14.5] |
| t+1 | sim, native | top of Simulate | window close: worker done = result **ready** (version, search id, epoch at cut); still inside = stop, overrun, result not ready |
| t+1 | sim, manager task | **resume in the hook prefix** | B17-equivalent checks (search interrupted, actor destroyed) with the same return values; result-level rules (§3.1); then the game's own B13 |
| t+1 | sim, manager task | each B14 call inside the game's B13 | **B14 hook**: object in the request and valid → selected ads through the game's C1; else the game's B14 (§2.4) |
| t+1 (+ Y2 slices) | sim | after B13 | D2 choose, D4/D5 push, as vanilla |

Why the consumer is B14 and not a new B13 [design]: the game's B13 keeps every rule we would otherwise have to copy (`CanRunAnyMetaAutonomy`, `CalculateNumSimsAtHomeOrGoingHome`, toddler/pet inside pairs B16, go home and stay home pairs, current lot, home lot, career location, the stopwatch and Y2 yields, the `SearchInterrupted`/`ActorDestroyed` returns) and re-copies the meta list after the resume. Objects added since the request are simply not in the request and run the game's B14.

### 2.3 Eligibility at B13 entry (else the game's B13, no request)

1. `[AutShadow] Mode=3` and switch `autlive` on; the session and per-load mod guards passed (§6).
2. Search type `Autonomy` (manager search), not inspection, not `BuffAutoSolve`, not an override (`AddWhileInteracting` turns meta off anyway) [verified, docs/59 §1.2-1.3].
3. The yield context check that Y2 uses passes (`CheckYieldingContext`) [verified, docs/59 §1.3]. The hook runs directly in the manager task's B13 frame, not inside a native-to-managed call, so the docs/04 yield guard case (a yield from code running inside a native-to-managed call aborts the game) does not apply [inferred, I-124-1].
4. No rain, hail or deep-snow search (C8 builds an `InteractionInstance`, out of the pilot, docs/122 §1.1).
5. Worker idle and no request in flight (`Begin` refuses when busy, docs/122 §14.1).

### 2.4 The B14 consumer, per meta object

| Case | What the hook does |
|---|---|
| Result not ready, dropped (§3.1) or search id mismatch | game's B14 (whole search falls back) |
| B14 called in a later tick than cut + 1 (B13 yielded at a Y2 site between objects, §13.1) | game's B14 for this and every later object of the search; the margin check still runs |
| A selected or unselected ad's runtime tuning has `AlwaysChooseBest` (mod tuning; none in the shipped tuning, §13.3) | game's B14 |
| Object not in the request (list changed) | game's B14 |
| Object invalidated since the cut (§3.2) | game's B14 |
| Object `skip` or aborted on the worker (A4 reason: `rng`, `row`, `pos`, `memo`, `query`, `sf`, `privacy`, `roomcache`, `simstale`, `exc` ...) | game's B14 |
| Object has an ad of a non-allow-listed definition or a mod type | game's B14 (re-scores the allow-listed ads too; small) |
| Valid, all ads allow-listed | for each **selected** ad of this object, in `MetaAds` order: identity check (same ad reference, same `MetaAds` count as at the cut, else game's B14 for the object), then the game's C1 with the same pair object B14 would pass. Unselected ads: nothing |

C1 does the check-table dedupe (C1a), C2 (live `Test` + score) and `BestScores.Add` [verified, docs/59 C1]. So `BestScores` and `InteractionCheckTable` are filled by the game's code with live values; the hook only chooses which pairs reach C1. The worker already applied B14's commodity filter (`Tradeoff.SatisfiesCommodity` / `MetaAdSatisfiesCommodity`) to the same commodity kind [verified as built, docs/122 §14.5], so every selected ad passed it.

### 2.5 Selection (computed once, at resume, before the game's B13)

1. Collect worker pairs with code 0 (pass) from valid objects, sort by worker score.
2. Select the top `TopN` (default 20, twice the `BestScores` capacity of 10 [verified, docs/59 D1]).
3. ~~For each allow-listed definition whose tuning has `AlwaysChooseBest`: also select its best pair.~~ Dropped after S4 [verified, §13.3]: none of the 12 has it in the shipped tuning, so the per-definition best-pair rule is replaced by the guard row of §2.4 (an object with such an ad runs the game's B14, which keeps `BestScores`' remembered-best handling exactly as vanilla).
4. **Margin check** (after the game's B13 returns, still in the hook): let `s10` = the 10th live score in `BestScores` (or the lowest if fewer than 10) and `w` = the best worker score among unselected pairs. If `w × (1 + Margin) ≥ s10` (default `Margin` = 0.05 [design; G7 sets it]), send the next `TopN` band through C1 (same identity checks), up to `Bands` (default 3); after that, the game's B14 for every remaining valid object. Counted (`alBand2`, `alBand3`, `alBandFull`).

The pairs that never reach C1 are exactly those the worker judged failed or ranked below the selected bands. Their live verdict is not computed; that is the saving and the only source of difference from vanilla (§5).

## 3. Staleness and invalidation

### 3.1 Result level: drop everything, the game's B13 runs (counted by reason)

| Rule | Source | Tag |
|---|---|---|
| Result age > 1 tick (resume tick ≠ cut tick + 1), or `MaxAge` (default 1) | version and tick words | [design] |
| World epoch moved since the cut: world events `2`, `3`, `5`, `7`, `9`, `0x0A`, lot events `0x0D`-`0x10`, build/buy slot swaps `World_LotClusterTreesImpl` `0x11623B0` and `RouteManager_OnExitBuildBuy` `0x1160DA8` | routesnap's `World_FireEvent` `0x78F2B0` detour; already drops in-flight runs (`LastRun` 2) | [verified, docs/76 §2.2; docs/122 §14.1] |
| Actor destroyed, out of world, `HasBeenDestroyed` | B17-equivalent check at resume; `ActorDestroyed` return as vanilla | [verified, docs/59 §1.3] |
| Another search type ran for the actor (`mAutonomySearchWasInterrupted`, side entry points incl. mod story progression) | same check; `SearchInterrupted` as vanilla (nothing pushed) | [verified, docs/59 §1.2 E] |
| Actor `LotCurrent` differs from the request, or the LOD flag flipped | request fields vs live | [design, docs/59 §3.5] |
| Actor got a head interaction and the search is not an override | B4 bail rule | [verified, docs/59 B4] |
| Actor fields differ from the request: `SimDescription` age and species, `LotHome`, `Household`, `IsSelectable` (`IsNPC` is its negation, not compared twice), `AssignedRole`, `Occupation` (reference) plus its `JobCount` and `CurrentJob` (`HasJobAtLot` reads the job list, which work-day alarms change under the same `Occupation`, §13.6), `CareerLocation`, the bot trait `CapacityToLoveChip` (the two date definitions only) | the request stores the values at B13 entry; the resume compares them (about ten reads, no listener). Replaces the planned `EventTracker` actor-generation listener: most of the actor state the 12 tests read has no event (§13.4). Writers and same-identity changes: §13.6 | [verified fields, §13.4, §13.6; design] |
| Tuning generation moved (Retuner-style edits) | hook on the tuning loader and setters (docs/59 §3.5) | [design] |
| Save, load, travel, CAS, Edit Town, build/buy started | game state hook; the worker is paused for these | [design, docs/59 §3.8] |
| Overrun, worker exception for the whole run, submit refusal | `LastRun` 1/4/5 | [verified as built, docs/122 §14.1] |

### 3.2 Object level: that object runs the game's B14

| Change since the cut | Event source | Tag |
|---|---|---|
| Object destroyed, removed from or returned to the scene | object events `6`, `0x0B`/`0x0C` | [verified, docs/76 §1] |
| Visibility flag | object event `0x18` | [verified, docs/76 §1] |
| Parent changed, child notification | object events `0x21`, `0x12` | [verified, docs/79 §1-2] |
| Transform published (non-Sim objects only; Sims are always refreshed) | object event `0x0E` | [verified, docs/76 §1]; rate is high from `RigPose_Update`, so the meta object row ignores `0x0E` for animated props unless the meta object itself moved [design] |
| Lot membership (objects added to or removed from a scored lot) | per-lot generation from object events `4`, `6` and world event `0x11` `OnObjectLotUpdate` (`0xC8A290`) | [verified, docs/122 §2.2] |
| Lot changed | lot events `0x0D`-`0x10`, `0x46` | [verified, docs/76 §2.2] |

ts3native already keeps these as row dirty bits and lot generations (docs/122 §13). New for go-live: one query IC `AutShadowNative.ObjStale(index)` that answers "row or its lot changed since version v" from the same bits [design].

### 3.3 Pair level

No rule needed: every selected pair is re-tested and re-scored live by C1/C2. A pair that fails live drops out as in vanilla. Not covered (accepted, as docs/59 §3.5 already accepts): a pair that the worker failed or ranked low on the one-tick-old view and that would now rank in the top 10. G7 measures how often it happens.

### 3.4 Managed state in the window

The worker reads managed state in place in the quiescent window (docs/59 §3.5, docs/122 §1.2). That premise still needs G-A5. If a native-to-managed call on the sim thread enters the window, ts3native sets stop and the run is an overrun; in go-live that means a fallback to the game's B13 at t+1, never a wrong score.

## 4. Fallback to the sim-thread search

The fallback is always the game's own code on the sim thread: the game's B13 (result-level drop) or the game's B14 (object level). Cost of a fallback: the vanilla meta phase plus one tick of latency and the cut. No retry, no second request for the same search. Session-level off switches (counted, one log line each): mod guard failure (§6), watchdog (worker inside > 50 ms, docs/59 §3.8), more than 3 worker exceptions for one definition (that definition leaves the allow-list for the session, docs/59 §3.6), `asUnguarded` or `asWorkerClassInit` non-zero, a sampled reference mismatch A2/A3 (§7.1).

## 5. RNG and equivalence with vanilla

### 5.1 Draws in vanilla's meta phase

| Draw | Where | Go-live |
|---|---|---|
| Pet branch of the base meta score (`RandomUtil.GetRandomObjectFromList`) | C2 of pet actors [verified, docs/121 §4.1] | the worker aborts `rng` on that object; it runs the game's B14 on the sim thread, so the draw happens there in B13 order |
| `Localization.FillInTokens` (`RandomUtil.GetInt`) | tooltip text built on failing `Test` branches, even for autonomous tests [verified, docs/121 §2.6, docs/78] | for pairs the worker failed, the draw is skipped (the worker's `LocalizeString` shim returns the rooted empty string, docs/122 §3.5); for selected pairs, C2 runs live and draws as vanilla |
| D2 choice, D4 interrupt chance | sim thread after B13 [verified, docs/59 D2, D4] | unchanged |

### 5.2 Equal in distribution, not path-equal

- The decision input D2 sees is the top 10 of live scores. When the selection covers vanilla's top 10 (§2.5), it is the same set with the same scores.
- The skipped tooltip draws only pick text that autonomy discards [inferred, I-121-4, I-124-6]. With the MT treated as a stream of independent uniform values, skipping k values that no decision reads leaves the D2 draw uniform and independent of the scores, so the pick distribution is unchanged; only the position in the stream moves.
- Two more path changes, both already timing-dependent in vanilla (docs/27 §3: bit-exact replay is out of scope): the meta phase usually spans fewer ticks (fewer Y2 slices), and other tasks' draws interleave differently with the one-tick wait.
- Consequence for testing: a seeded A/B (`seed_rng`) of go-live on vs off compares distributions, not paths (G8). Seeded runs that compare anything else must use the same `autlive` setting on both halves (docs/121 §4.1 note).
- No parity mode [design]: advancing the MT by the "skipped" count would need the number of draws `FillInTokens` would have made, which depends on the text it would have built; computing it means running the text code the worker must not run (EAText globals, docs/121 §2.6).
- Rejected alternatives (docs/121 §4.1): a per-worker MT seeded from the sim MT, and a draw-ahead buffer. Both reorder the sim's own decision draws and gain nothing over abort-on-draw for the meta allow-list.

## 6. Mod compatibility

| Mod behaviour [community, read for behaviour only] | What go-live does | Tag |
|---|---|---|
| A mod replaces EA script assemblies (core mods) | session guard: MVID mismatch, go-live off for the session | [verified as built, docs/122 §14.5] |
| A run-time patcher (MonoPatcher-style, ts3fixes' own patch host) swaps a method in the driver's closure or **in the consumer path** (the 18 methods of §13.5: B13's caller, B13, B17, B14, C1, C2 and its pair getters, `BestScores` add and choose, D2) | L3 check (`MonoMethod+0x08` = declaring class) over the driver's callees, extended by the consumer path; hit in the driver closure = that definition `skip`, hit in the consumer path = go-live off for the load. Our own known swaps (AM-1 `IsAllowedInRoom` memo, `HoursMemoHook`) are accepted by target and hook class, as docs/104 §1 does for routing | [verified, docs/104 §5.4; design for the extension] |
| NRaas mods that replace definition singletons with derived types (Woohooer is the known case, docs/59 §3.6) or add their own definitions and meta objects | exact runtime type + MVID: not on the allow-list, so their ads and objects run the game's B14 live | [verified as built, docs/122 §4.1] |
| NRaas StoryProgression and other mods that push interactions or run `FindBestAction*` from their own tasks | side entry points never reach the hand-off; one for the queued actor sets `mAutonomySearchWasInterrupted`, so the parked search returns `SearchInterrupted` as vanilla | [verified, docs/59 §1.2 E] |
| Mods that change autonomy through tuning (Retuner, autonomy on/off settings) | tuning generation drops in-flight results; the worker reads tuning in place | [design, docs/59 §3.6] |
| Overwatch / ErrorTrap resets a Sim mid-search | actor destroyed or reset: result-level drop | [design] |
| A mod overrides `Autonomy.RunAutonomy`, B13 or `AutonomyManager.Simulate` | caught by L3 on the consumer path; if the mod calls B13 from another context, rule 2/3 of §2.3 sends it to the game's B13 | [inferred, I-124-7] |
| Mod event listeners | the worker raises no event (docs/73 §5.3); every C1/C2 and push runs on the sim thread, so listeners see the same calls in the same task | [verified, docs/73 §5] |

## 7. Switches, counters, logs

### 7.1 Switches

- Ini `[AutShadow]`: `Mode=3` (live; 0/1/2 as docs/122 §4.3). New keys, all ignored unless `Mode=3`: `TopN=20`, `Margin=0.05`, `Bands=3`, `MaxAge=1`, `VerifyEvery=50` (every 50th live search also runs the Mode R reference at the cut with the MT saved and restored; an A2/A3 there turns live off for the session), `Dry=0` (G7: compute and compare, do not use; §8.1). `Sample` is honoured (1 = every eligible search; larger = staged roll-out).
- ts3fixes switch `autlive` (on/off for `--ab`), off by default; tuning `kAutLive` false. AGENTS.md rule: every new switch defaults off; nothing here changes save data.

### 7.2 Counters (reserve in `docs/registry.md` at build time, not here)

- ts3native `ts3native_autshadow` v3: `alReady`, `alNotReady` (by `LastRun`), `alVerify`, `alVerifyMis`, `alObjStaleQ`.
- ts3fixes: `alEligible`, `alPosted`, `alApplied`, `alDrop[12]` (age, epoch, destroyed, interrupted, lot, lod, head, actor, tuning, gamestate, overrun, other), `alObjLive` by reason (stale, notinreq, abort, skip, mixed, late, acb), `alPairsWorker`, `alPairsC1` (selected), `alBand2`, `alBand3`, `alBandFull`, `alApplyUs`, `alLatencyTicks` (histogram: request to push), pick histogram per definition type (live vs `Dry` reference), `alTop10Same`, `alTop1Same` (Dry only).
- Log: `autshadow.jsonl` gets `live-drop` (first 20 per reason per load) and `dry-diff` records (pair, worker score, live score, invalidation events since the cut).

## 8. Gates

Numbering continues docs/118 (G0-G6). Prerequisites: G-A3 to G-A6 passed (docs/122 §7), the P4 TLS fix confirmed in game (no `FrameTls SKIPPED`), worker overrun < 5% (G-A5).

| Gate | After | Pass when | Fail means |
|---|---|---|---|
| **G7** dry apply | `Mode=3 Dry=1`, BP, IP, SV, 10 min each + `bp-autlive-edit` (build/buy, Edit Town) | over ≥ 2,000 eligible searches per world: predicted top 10 (selected bands, scored live with C2 under MT save/restore) equals the game's own top 10 of the same search in ≥ 99% and top 1 in ≥ 99.5%; every difference record has a pair and its cause (stale reject, below band); live scores of the shared pairs bit-equal; `alVerifyMis` 0; 0 crashes in two clean runs (crash rule) | widen `TopN`/`Margin`, add the missing invalidation event, or stop: the one-tick view is too stale for meta |
| **G8** behaviour | `Mode=3` live vs off, same `seed_rng` seed, BP and IP, 2 x 30 min per world (correctness runs: daytime allowed) | per-definition pick shares: chi-square p ≥ 0.01 over definitions with ≥ 50 picks; autonomous pushes per Sim-hour within ±5%; `MakeSimGoHome` fallbacks within ±10%; median request-to-push latency not up by more than 1 tick; fallback share < 10% with every reason sized; 0 crashes; possible stuck Sims logged only | find the biased definition (dry-diff records by definition), exclude it from the allow-list, rerun |
| **G9** cost and stability | clean night ABBA `--ab autlive` BP and IP (18 min); then 2 clean runs per world (BP, IP, SV) with one save + reload during a meta burst | sim it/s not down > ~2% and ThreadedObjects ms/s not up > ~2% (offload rule, CLAUDE.md); manager-task meta ms/s on the sim thread down (work moved off, measured, docs/75 §5 method); 0 crashes in 6 runs; `asUnguarded` 0, `asWorkerClassInit` 0; save during an in-flight search drops the result and the save loads | keep `autlive` off; write the measured break-even (§8.3 inputs) into docs/59 §3.3 |

Shipping after G9 is Rodney's decision (release defaults, docs/RELEASE.md); this design keeps everything off.

### 8.1 Dry mode

`Dry=1` posts and yields exactly as live (so the staleness is the real one-tick staleness), then at resume scores the selected bands with the game's C2 directly (no `BestScores`, no check table), with the MT at host `+0xC30` saved before and restored after, and runs the game's B13 normally. The Mode W C1 record hook (docs/122 §14.5) gives the game's own scores for the same search; the compare runs after B13. Side effects of the extra C2 calls are the idempotent lazy fills docs/122 §12 S1 lists [verified, docs/122 §12].

### 8.2 Repros (tools/harness/repros.json, AGENTS.md rule)

- `aut-meta-burst` (exists in the docs/122 plan: idle adults, needs max, unpause) drives G7-G9.
- New: `aut-live-pet` (pet household: `rng` aborts go live), `aut-live-interrupt` (start a commodity search for a queued Sim during the one-tick wait: `SearchInterrupted`), `aut-live-venue-hour` (advance time across a venue opening hour during a burst: a store-hours `Test` flips between cut and apply), `aut-live-save` (save during a burst).

### 8.3 Cost model [inferred, replaced by G9]

| Item | ms/s | Source |
|---|---|---|
| Meta scoring in the manager task | ~31 | docs/75 §1: 80% of ~39 ms/s C2 |
| Allow-listed share f | ~0.75 → ~23 eligible | docs/75 §1 (13 types), docs/122 §1.1 |
| Re-scored at apply (top 20 + bands + live objects), r ≈ 10-25% of eligible | -2 to -6 | [inferred; pairs per search not measured] |
| Cut at `Sample=1` (10-30 meta searches/s x ≤ 0.3 ms) | -3 to -9 | docs/122 §2.3 budget at `Sample=4` was ≤ 2 |
| Request build (AddRow per meta object) | -1 to -3 | [inferred] |
| `[MonoThread]` fixed (P3 gate, P4) | -7 to -25 | docs/59 §3.3, docs/30 §8 |
| Guard thunks on all ICs | ≤ -1 | docs/122 §7 G-A3 limit |
| **Net on the sim thread** | **-18 to +10** | consistent with docs/97 §1 (-16 to +7) |

The M5 fixed cost dominates; it amortises only if another worker load shares it (docs/59 §3.3). That is why G9 is judged by the offload rule, with ms moved off the sim thread reported next to it/s.

## 9. Failure modes

| Failure | Detection | Effect / fallback |
|---|---|---|
| Worker not done at the top of t+1 | `LastRun` 1 | game's B13 at t+1 (one tick of latency) |
| Worker exception on one object | per-object `try/catch` (A4 `exc`) | that object live; 3 per definition = definition off for the session |
| Worker hangs | watchdog, 50 ms | live off for the session; worker parked, not killed (it may hold the P3 lock, docs/59 §3.8) |
| Extra yield refused at B13 entry (no context) | rule 3 of §2.3 | no request, game's B13 |
| Hook resumes in the wrong search (id mismatch after an abort and requeue) | search id + actor id in the result | drop |
| Stale reject: a pair the worker failed now passes and would rank top 10 | Dry diff records (G7); sampled in live via `VerifyEvery` only for bugs, not staleness | accepted; next search sees it; counted in G7 |
| Live scores lower than predicted across the board (motive change since the cut) | margin check | bands 2-3, then the game's B14 for the rest |
| A selected pair object changed identity | `MetaAds` reference and count check | that object live |
| Snapshot or shim bug (wrong worker score) | sampled reference A2/A3 (`VerifyEvery`) | live off for the session; candidates were still live-scored, so a bug changes ranking at most, never a pushed score |
| Mod patches the consumer path at run time after the load check | L3 rolling slice per search (docs/104 §6) | live off for the load |
| Save, travel, build/buy during a search | game state hook | worker paused, result dropped |

## 10. Effort (CLAUDE.md units)

| Step | Work | Estimate |
|---|---|---|
| S4 static checks | B14 call sites and signature (I-124-2), `HowMuchItWantsSimToCome` call site (I-124-3), `AlwaysChooseBest` in the 12 definitions' tuning (I-124-4), actor state read by the tests (I-124-8): **done 2026-10-20, §13.1-13.4**; L3 list for the consumer path, writers of the compared actor fields, request object list: **done 2026-10-20, §13.5-13.7** | 1-2 h (done ~2 h) |
| ts3native | `Mode=3`: result ready at window close, `ObjStale` IC, `VerifyEvery`, `Dry` flag passed through, counters v3, selftest cases (ready/not ready, stale object, verify mismatch off) | 2-3 h |
| ts3fixes | B13 prefix yield + resume checks, selection and bands, B14 hook (with the per-call tick check), actor field compare, tuning generation, Dry compare, counters, pick histogram, `autlive` switch | 3-5 h |
| Offline | selection/margin/identity logic selftest (`tools/selftest/autshadow`), monohost: result-ready timing with a synthetic driver | 1-2 h |
| Game rounds | G7-G9 build and fix rounds | 3-6 h |
| **Total** | on top of the docs/122 pilot (~17-29 h + M5) | **~10-18 h** |
| Game runs | G7: 3 dry + 1 edit repro, plus reruns to two clean; G8: 4 seeded (2 per world, 30 min); G9: 2 ABBA + 6 stability (shared with G7 reruns where possible) | **9-11 jobs** (~8-30 min each) |
| Clean nights | G9 ABBA (one night, BP + IP); a second if the first is noisy | **1-2** |

## 11. Risks

| # | Risk | Effect | Mitigation |
|---|---|---|---|
| 1 | **Net cost**: M5 fixed cost plus a cut per search at `Sample=1` eat the eligible ~23 ms/s | G9 fails the 2% rule although correct | measure f, r and the cut in G7 before G9; `Sample` > 1 halves cut cost but also the moved work; share M5 with another worker load |
| 2 | **Staleness bias**: one-tick-old rejects are not random (venue hours, buffs, a Sim arriving), so some definitions lose picks systematically | behaviour drifts from vanilla in distribution | object and actor invalidation, bands, G7 diff records by cause, G8 per-definition chi-square, exclude a biased definition |
| 3 | **M5 in game**: the worker has never interpreted in the game (P4 TLS was skipped in game until 2f4c595; shadow Mode R jobs were dropped from the queue) and the quiescent-window premise is unmeasured | crash, hang or many overruns | G-A3 to G-A6 first; crash rule; overruns fall back, never use a partial result |
| 4 | The extra yield at B13 entry | a new yield point in the manager task; a mod or our own hook calling B13 from a non-task context | eligibility rules 2-3; L3 on B13; `aut-live-interrupt` repro |
| 5 | C1/C2 side effects skipped for unselected pairs | a lazy fill or cache the game relies on later is not made | docs/122 §12 S1 found only idempotent fills and hooked writes [verified there]; I-124-9 confirms C1 has no other consumer |

## 12. Ledger

Open claims: docs/51 §166 (I-124-1 to I-124-9). S4 results (I-124-2, -3, -4 verified; I-124-8 refuted as worded, design changed) and the new rows I-124-10 and I-124-11: docs/51 §169. S4 remainder (L3 list, field writers, request objects; I-124-12 to I-124-15): docs/51 §176.

## 13. Static step S4 results (2026-10-20)

Sources: `tools/managed-callgraph.ts callers` over the call-graph index of the Steam 1.67 script assemblies (`game-copy/analysis/callgraph`, built by `managed-callgraph.ts build` from `tools/extract-assemblies.ts` output), the decompiled class library in `game-copy/decompiled` (read locally, not quoted), and the interaction tuning (ITUN, type `0x03B33DDF`) of `Game/Bin/Gameplay/GameplayData.package` read in place from the Steam install (4,128 rows; docs/121 §8.4 found no ITUN in any other installed package). No build, no game run.

### 13.1 B14 call sites (I-124-2) [verified]

- `Autonomy.ScoreInteractionsForMetaObject` (private, non-virtual, instance) has exactly one caller in the index: B13 `ScoreInteractionsForMetaAutonomy`. No delegate creation (`ldftn`) of it. B13 itself has one caller, `ScoreInteractionsOnObjects` (the manager search).
- B13 calls B14 from 4 sites: the loop over a copy of the public meta objects plus `mLotsKnownAbout` (skips objects with `HasBeenDestroyed`; only when `CanRunFullMetaAutonomy`), then, only when `CanRunLocalAutonomy`, the actor's `LotCurrent`, `LotHome` (if different from `LotCurrent`) and `CareerLocation`. The request (docs/122 §14.1) must list the last three too, or they simply run the game's B14 (the "not in the request" row).
- A lot can reach B14 twice in one search (in `mLotsKnownAbout` and as `LotCurrent`/`LotHome`). Vanilla's second pass is a no-op through the C1 check table (`InteractionCheckTable.TryAdd` per pair). The consumer behaves the same: selected pairs are already in the table, unselected ones are skipped again. No extra rule.
- C1 `CalculateScoreAndAddToCandidates` is also called directly by B13 for the three go-home pairs (`GoHome`/`GoToVirtualHomeMeta`, `StayHome`, `GoOutToYard`) and by B16 (`ConsiderBringingToddlersInside`, `ConsiderBringingPetsInside`, between the public loop and the lot sites). These never pass through B14, so they stay live, as §2.2 assumed.
- **Design change:** B13 can yield between objects (every 5th object, when its update stopwatch is past its limit, `YieldIfPossible`). The rest of the loop then runs one or more ticks later, older than the one-tick view §3.1 allows. The §2.4 table now has a per-call tick check: from the first B14 call after a yield, every remaining object runs the game's B14. Counted under `alObjLive` (new reason `late`).

### 13.2 `HowMuchItWantsSimToCome` (I-124-3) [verified]

- Implementors: `Lot` (plus its private per-venue-type overload, called only from its own public one), `RabbitHole`, `HomeMetaObject`, `WorkMetaObject`, `GardeningMetaObject`, `WorldPondHelper`, `BakeSaleTable`. No direct call to any implementation; the only call site is the interface call `IMetaObject.HowMuchItWantsSimToCome` in `MetaInteractionScoredByVenue.CalculateScore`.
- That `CalculateScore` is reached only through `IOverridesCalculateScore.CalculateScore`, whose only caller is `InteractionObjectPair.TestAndCalculateScore` (C2), after the definition's `Test` returned `Pass`, and only when the search is not in its local phase. B13 itself (including `GetDesiredNumSimsForMetaObject` and `CalculateNumSimsAtHomeOrGoingHome`) never calls it.
- So an unselected pair skips the call entirely (the §8.3 saving holds), and a pair that fails `Test` never made it in vanilla either.

### 13.3 `AlwaysChooseBest` (I-124-4) [verified for the shipped tuning]

- The flag is `InteractionTuning.AlwaysChooseBest`, set only by the tuning parser from the `alwaysChooseBest` attribute of a tradeoff's `ScoringFunction` element. No other store to the field in any script assembly. It is read only in `BestScores.Add` (true = remembered-best list instead of the priority queue).
- Tuning lookup (`AutonomyTuning.GetTuning`) tries the target type, then its base types up to `GameObject`, then the definition's generic base; every one of the 12 allow-listed definitions (and the excluded `VisitCommunityLotWith`) has exactly one ITUN row, on `Lot`, `GardeningMetaObject`, `Restaurant`, `CityHall`, `Hospital`, `RabbitHole` (Stadium), `ShoppingRabbitHole` or `ShowVenue`, and all 13 say `alwaysChooseBest="False"`.
- 61 of the 4,128 rows say `True`; all are object interactions of the local phase (toilets, cleaning, trash, hay, sand play, booby traps and similar), none a meta definition.
- **Design change:** §2.5 step 3 is dropped. A mod tuning package can still set the flag, so the consumer checks the pair's runtime tuning and sends that object to the game's B14 (§2.4 guard row). One field read per selected object.

### 13.4 Actor state the tests read (I-124-8) [refuted as worded; design changed]

The 12 definitions' `Test` paths (the `InteractionDefinition.Test` wrapper, `CheckAvailability` with the shipped tuning values, `CheckAvailabilityOnLot`, `CommonTests`, the meta `SpecialCaseTests`, the definitions' own tests and `SharedVisitTests`/`SharedVisitVenueTests`/`Lot.CommonVisitTest`) read this actor state:

| Group | Actor state read | Event? |
|---|---|---|
| tuning, all 12 | age and species flags; lot rules: owned residential lot, daycare situation, `IsAllowedInRoom`, `IsStray`, greeted on lot, `Household`, `LotCurrent`, outside / public room; tomb room; `IsSelectable` for `DisallowPlayerSim`; `WorkMotive` | age: yes; the rest mostly no |
| tuning, 2 date definitions | required trait `CapacityToLoveChip` (bots) | no general one |
| tuning, not used by the 12 | motive, mood, skill and career thresholds, buff and occult restrictions, `funExit` (all `None`/`Ignore`/`False` in the 12 rows) | - |
| definition tests | `IsSelectable` + the player's autonomy level option; `LotCurrent`, `LotHome`, `IsNPC`, teen or below, `AssignedRole`, `IsNeighborOfPlayerWhoIsLastOneAtHome`, `IsGreetedOnLot`, `LastAutonomousVisitDate`, `Occupation.HasJobAtLot`, `Household` (vacation lot ownership), celebrity lot access; plus the clock: venue hours, `IsTooLateToVisit`, restaurant `IsOpen` | few; the clock has none |
| meta score (C2, live for every selected pair) | `LotCurrent`, `LotHome`, vampire + night, celebrity flag and level, `IsRich`, traits, `PreferredVenues`, relationships to Sims on the lot | ranking only |

So a "short list of `EventTracker` ids" cannot cover what flips a `Test` in one tick: greeted state, room permissions, lot rent, the player's autonomy level and above all the clock (venue opening and closing, too late to visit) change without any actor event, and the buff/occult/outfit events the design listed are read by none of the 12. **Design change [design]:** no event listener. The request stores the cheap identity fields (§3.1 row: age and species, `LotHome`, `Household`, `IsSelectable`, `IsNPC`, `AssignedRole`, `Occupation`, `CareerLocation`, the chip trait for the two date definitions) and the resume compares them; any difference drops the whole result. Everything else is the accepted one-tick staleness of §3.3: a pair the worker rejected that now passes is missed for this one search (the next search sees it); a pair the worker passed that now fails is re-tested live and drops out as in vanilla. The clock flips are the expected main cause; `aut-live-venue-hour` and the G7 diff records (cause `clock`) size them (I-124-10).

Effect on effort: the ts3fixes row loses the listener and gains the compare and the per-call tick check; the total stays ~10-18 h.

### 13.5 L3 list for the consumer path [verified callees; design for the verdicts]

Source: direct callees (edge kinds `call`, `cv`, `virt`, `iface`) of B13, B14, C1, C2, `BestScores.Add`/`ChooseInteraction`, B17 and D2/D4 in the call-graph index (`managed-callgraph.ts query --depth 1` and `callers`). Owner of all 18 methods: `Sims3GameplaySystems`, MVID `3681889397f4aa44b399a2926b2454fc` (the allow-list MVID, `docs/data/autonomy-allow.csv`), so the session MVID guard already covers a replaced assembly; L3 covers run-time swaps. Token = MethodDef row in that image (from the index's row order). The checker resolves each method by type and exact signature at load, as docs/104 §5.4 does, not by name: `AutShadow.AllL3Ok` matches by name, which would put both `CalculateScore` overloads under our own record hook's whitelist. A hit on any row turns go-live off for the load (one log line; every later search counts `alDrop` reason `other`); the pilot's own sets keep their per-definition `skip` verdict for shadow modes.

| # | Method | Token | In the pilot set? | What a mod swap breaks |
|---|---|---|---|---|
| 1 | `Autonomy.ScoreInteractionsOnObjects(CommodityKind, bool)` (B13's only caller; called by `RunAutonomy`, `FindBestAction`, `FindActions`) | `0x06011E9B` | no | a replacement can call B13 out of vanilla's order (meta before local, twice, or with `BestScores` cleared after it), so the margin check's "local pairs already in `BestScores`" premise and the one-request-per-search rule no longer hold |
| 2 | B13 `Autonomy.ScoreInteractionsForMetaAutonomy` | `0x06011EA5` | no (hooked) | our prefix is itself a swap; a mod swap means one of the two is lost. Whitelist: our hook class only |
| 3 | B17 `Autonomy.YieldIfPossible` | `0x06011EBA` | no | the resume copies B17's `SearchInterrupted` / `ActorDestroyed` returns; a changed B17 makes the copy diverge from the rest of the search |
| 4 | B14 `Autonomy.ScoreInteractionsForMetaObject` | `0x06011EA6` | no (hooked) | as 2 for the go-live hook; a mod body here (own commodity filter, own ad list) would be bypassed for consumed objects |
| 5 | C1 `Autonomy.CalculateScoreAndAddToCandidates` | `0x06011E86` | no | the consumer sends only the selected pairs through it; a mod that counts, logs, filters or collects every meta pair there sees a subset |
| 6 | C2 `Autonomy.CalculateScore(InteractionObjectPair)` | `0x06011E88` | self-check only | a mod that rescales or adds score here changes live scores but not the worker's, so the top-N picks the wrong pairs (the margin check sees it only as lower live scores). Whitelist: the docs/122 Mode W / `Dry` record hook, on this overload only |
| 7 | `InteractionObjectPair.TestAndCalculateScore` | `0x06004D6A` | yes (shared) | worker and live scores differ, as 6 |
| 8 | `InteractionObjectPair.get_Tradeoff` | `0x06004D62` | yes (shared) | B14's commodity filter (applied by the worker) differs from live |
| 9 | `InteractionObjectPair.get_InteractionDefinition` | `0x06004D60` | yes (shared) | allow-list type check and filter read another definition than live |
| 10 | `InteractionObjectPair.get_Target` | `0x06004D61` | yes (shared) | the lot passed to `MetaAdSatisfiesCommodity` differs |
| 11 | `Tradeoff.SatisfiesCommodity` | `0x060050BF` | yes (shared) | as 8 |
| 12 | `InteractionObjectPair.get_Tuning` | `0x06004D64` | no | the §2.4 `AlwaysChooseBest` guard reads it; a swap that returns other tuning hides a mod-set flag, and the remembered-best list misses pairs |
| 13 | `BestScores..ctor` | `0x06006359` | no | sets the heap capacity (10, docs/59 D1) that `TopN` = 20 and `s10` assume; a wider heap shows the unselected pairs' absence in D2's input |
| 14 | D1 `BestScores.Add` | `0x0600634C` | no | a mod that keeps more than the top 10 or ranks by its own rule relies on seeing every pair |
| 15 | `BestScores.AddInteractionOnlyIfBest` | `0x0600634E` | no | as 14, for the remembered-best list |
| 16 | D2 `Autonomy.AssignProbabilitiesAndChooseInteraction()` | `0x06011E85` | no | a mod D2 that reads beyond `BestScores` (the check table, a rescan of the meta objects) sees fewer scored pairs; §5's equal-in-distribution argument covers only vanilla's D2 |
| 17 | `BestScores.ChooseInteraction(float, DecisionSnapshot)` | `0x0600634F` | no | as 16 |
| 18 | `BestScores.ChooseInteractions(int)` (multi-pick, `FindActions`) | `0x0600634D` | no | as 16, for the side entry point that takes several candidates |

Not on the list [design], with the reason:
- `CanRunAnyMetaAutonomy` `0x06011EA4`, `CanRunFullMetaAutonomy` `0x06011EA3`, `CanRunLocalAutonomy` `0x06011E74`: the request (by reflection) and the game's B13 call the same body, so a swap acts on both; a different answer at t+1 only makes objects "not in the request".
- `InteractionCheckTable.Allocate` `0x06011EE9` / `TryAdd` `0x06011EED`, the `PriorityQueue` methods (`.ctor`, `Add`, `InsertElement`, `ReplaceTopElement`, `ReSortAfterReplacingTopElement`, `SmallestChildIndex`, `get_SmallestElement`, `Element`, `get_CurrentNumElements`), `BestScores.ChooseBestInteraction` / `AssignProbabilitiesAndChooseInteraction(float, DecisionSnapshot)` and `InteractionCandidate.CompareTo`: reached only through rows 5 and 13-18, and they act the same on a subset as on the full set.
- `IMetaObject.get_MetaAds` and `IMetaInteractionDefinition.MetaAdSatisfiesCommodity`: already checked per implementor and per definition by the pilot (`kImplCallees`, `kDefCallees`); a hit sends that object or definition to the game's B14, the right fallback here too.
- D4 `ConsiderPerformingAutonomousAction` `0x06011E95` and D5: they see only the chosen instance.
- `Simulator.Sleep` / `CheckYieldingContext` (`SimIFace`): every yield in the game uses them; our extra yield behaves like B13's own Y2 under any swap.
- The actor getters of the §3.1 compare: both sides of the compare go through the same getter.

Cost: 13 more `MonoMethod` checks per sweep (docs/104 §6: nanoseconds each), in the pilot's load / world load / `ForceJIT`-change sweep.

### 13.6 Writers of the compared actor fields (item 2) [verified writers; inferred timing]

Backing state from the getters' field reads in the index, writers from its field-write tags, their callers from `callers`. "Window" = from the B13 post in tick t to the resume in t+1: the rest of tick t's script slice and the start of t+1, where every other task (the actor's own interaction task, alarms, `RoleManagerTask`, situations, UI commands) can run on the sim thread.

| Field (getter path) | Backing state | Writers (callers) | In the window? | Same identity, changed meaning? |
|---|---|---|---|---|
| age, species (`SimDescription.Age`, `.Species`) | `SimDescription.mSimFlags` | `set_Age` (`AgeUp`/`AgeDown.RunNonPersistable`, `AgingState.PreInstantiateSim`, `UpdateFromCasData`, `OccultRobot.MakeRobot`, `GeneticsPet.MakePet`), `set_Species`, `set_AgeGenderSpecies`, `Set`, `Init`, `UpdateFromOutfit`, `ImportContent`, `MergeTravelInformation` | yes: a birthday runs in the actor's own interaction task while its search is parked | no: compared by value (age and species bits only, so gender and handedness writes to the same word do not drop) |
| `Household` | `SimDescriptionCore.mHousehold` | `SimDescription.OnHouseholdChanged` (from `Household.Add`/`Remove`/`AddTemporary`/`RemoveTemporary`/`Cleanup`/`ImportContent`, `PetPool.AddPet`, `NPCMermaidManager.RefreshMermaidHousehold`, `AlienUtils.AlienRefreshCallBack`, travel import, moving model), `Dispose` | yes (adoption, move-in, marriage, mermaid or alien refresh) | members and funds change under the same reference; the 12 tests read the household for lot ownership (covered by `LotHome`) and `IsServiceNpcHousehold` (fixed per household). Vacation and venue ownership is read from `Household.ActiveHousehold.RealEstateManager`, not the actor's household: a purchase in the window is accepted staleness (§3.3), G7 cause `actor` |
| `LotHome` | `Household.mLotHome` via `SimDescription.LotHome` | `Household.SetLot` (`Lot.MoveIn`/`MoveOut`, `Household.Init`, `Sim.OnLoad`, `EditTownModel.EndMoveRotateLotContents`), `Household.Cleanup` | moving UI and Edit Town (game state rule drops those); `MoveIn`/`MoveOut` from interactions possible | no: compared by reference (the `Lot`); lot contents are object level (§3.2) |
| `IsSelectable` (`IsNPC` = not selectable) | `PlumbBob.mSelectedActor` (`Sim.ActiveActor`), the actor's `Household`, `SimDescription.mFlags` bit `IsNeverSelectable` | `PlumbBob.DoSelectActor`, `PlumbBob.CheckForChangeInActiveHousehold`, `SimDescription.SetFlags`, the household writers above | yes (the player selects another Sim or household) | no: compared by value |
| `AssignedRole` | public field `SimDescription.AssignedRole` | `Role..ctor`, `Role.RemoveSimFromRole` (from `RoleManager.UpdateAndGetRolesThatNeedPeople`, `Pregnancy.HourlyCallback`, `Sim.OnBecameSelectable`, tourist / explorer / time-traveler `KillSim`, register and barista `Dispose`, `HouseParty.PushComeToParty`, others), `RoleManagerTask.FillInWorldRole`, `RoleManager.DestroyAllRoleBotsBeforeSwitchingWorlds` | yes (`RoleManagerTask`, hourly callbacks) | the tests read null-ness and `Role.Type`; `Role.mType`'s only writer is the `Role` constructor, so the reference compare is complete. `Role.IsActive` changes with role hours under the same reference, but no caller of `SimDescription.HasActiveRole` is a meta definition test [inferred, I-124-14] |
| `Occupation` | `CareerManager.mJob` via `SimDescription.Occupation` | `CareerManager.AssignJob` (from `Occupation.AssignOccupation`), `ImportContent`, `OnLoadFixup` | yes (join or quit from interactions and the phone) | **yes:** `HasJobAtLot` reads `Occupation.mJobs`, written by `AddJob` (`TryCreateJob`, `PerformanceCareer.CreateGigFromSpec`), `RemoveJob` (`Job.ShutDown`, show stage `Tend`, others) and `RemoveAllJobs` (`RegularWorkDayStartAlarmHandler`, firefighter work-day end, `AcademicCareer.CreateCoursesForToday`, `LeaveJobNow`, others). Hence `JobCount` and `CurrentJob` in the compare; a same-count swap of other jobs in one tick stays accepted staleness (it happens at work-day alarms, so G7 sees it as cause `clock`) |
| `CareerLocation` | `Career.mCareerLoc.Owner` (`Sim.CareerLocation` returns the rabbit hole) | `Career.set_CareerLoc` (virtual, from `TransferBetweenCareerLocations`, `SetAttributesForNewJob`, `LeaveJobNow`, `School.LeaveJobNow`, `CareerLocation.Dispose`, `OnLoadFixup`); `CareerLocation.Owner` is written only by its constructor | yes (transfer, new job, quit) | no: compared by reference; `Dispose` comes from build/buy (game state rule) |
| `CapacityToLoveChip` (2 date definitions) | the actor's `TraitManager` trait set | `TraitManager.AddElement` / `AddHiddenElement` / `RemoveElement` (bot chip install and removal) | yes, rare | no: compared by value (`HasElement`) |

So the only same-identity change of actor state that moves a `Test` verdict in the window is the job list behind `Occupation`, closed with two more reads; the vacation-property case is household-of-the-player state, not actor state. Every other write changes the compared reference or value, or runs only in a game state the existing rule drops.

### 13.7 Request objects `LotCurrent`, `LotHome`, `CareerLocation` (item 3) [verified in the source]

The docs/122 request already lists them: `AutShadow.TryRequest` (ts3fixes) adds, after the public meta objects and `mLotsKnownAbout` (only when `CanRunFullMetaAutonomy`), the actor's `LotCurrent`, `LotHome` when it differs from `LotCurrent`, and `CareerLocation`, all only when `CanRunLocalAutonomy`: the same 4 sites and order as B13 (§13.1). `AddObj` also rows each object's lot. No change to the object list. Two small differences, both safe: `AddObj` skips destroyed objects on every site (B13 checks only in the public loop) and stops at `kMaxObjs`; such an object is "not in the request" and runs the game's B14.

The go-live change to the request is the §3.1 compare, not objects: `Request` gains the age and species bits, `LotCurrent` (already read for the list; stored for the §3.1 lot rule), `LotHome`, `Household`, `IsSelectable`, `AssignedRole`, `Occupation`, `JobCount`, `CurrentJob`, `CareerLocation` and the chip flag, filled in `TryRequest` before `Post`.
