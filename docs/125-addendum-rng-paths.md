# 125 addendum. RNG and slot-position paths against the docs/125 unit boundaries

Written 2026-10-20. Static only: no game run, no build. docs/125 (branch `worktree-agent-a7f5770960151a57d`, not merged when this was written) gives every meta score and every pair its own Mersenne Twister stream (`AutShadowNative.RngUnit` in ts3fixes `AutShadow.ScoreMeta`). Its Mode R argument rests on two ledger rows: **I-125-2** (every script RNG draw during meta scoring happens inside a unit) and **I-125-3** (the tooltip `FillInTokens` draw, which only the sim-thread reference makes, is the last thing in its pair unit). It also did not trace the managed path to `Slots_GetPositionOfSlotImpl`. This note lists every static path and checks both rows. Data: `docs/data/rng-unit-paths.csv` (335 rows). Ledger: docs/51 §173.

Tags as docs/125: **[verified]** = read in the IL index, the decompiled class library or ts3fixes source; **[inferred]** = reasoning still to confirm.

## 1. Method

- **Units, from `AutShadow.ScoreMeta` on that branch [verified]:** unit `o << 16` wraps `IMetaObject.HowMuchItWantsSimToCome`; unit `(o << 16) | k` wraps `ScorePair`: the `Tradeoff` getter, `Tradeoff.SatisfiesCommodity`, `MetaAdSatisfiesCommodity`, the `InteractionInstanceParameters` constructor, `InteractionDefinition.Test(ref parameters, ref tooltip)` and the score (`CalculateScore` override or `Autonomy.CalculateScoreForObjectInteraction`). Between units the driver calls only native ICs (`Stopped`, `Aborted`, `ResetAbort`), so no managed game code runs outside a unit.
- **Roots:** the 7 implementors' `HowMuchItWantsSimToCome` (meta units) and, per allow-list v0 definition (12, `docs/data/autonomy-allow.csv`), the pair-unit calls above. `Test(ref, ref)` is the generic base body that `ScorePair` actually calls (availability, `CommonTests`, special-case tests, the definition's own `Test`, tooltip tests); docs/121 used the definition's own `Test` plus `CommonTests` as a separate root.
- **Tool:** `tools/rng-unit-paths.ts` on the `managed-callgraph.ts` index, depth 12, `ldftn` edges followed. Virtual and interface calls expand to every override, except that a call declared on an ancestor of a unit receiver (the definition, its interaction class, the 7 implementors, `Sim`) only reaches that receiver's chain. **Limitation:** that receiver rule assumes such calls go to `this`. Plain class-hierarchy expansion gives 155,459 rows, almost all through `CreateInstance`/`ConfigureInteraction` and `GameObject` virtuals of unrelated classes. One real route the rule drops is `SimDescription.IsRich` -> owned objects' `get_Cost` -> ... -> `UiLocalizationModel.FillInTokens` (not hooked, so the same draw on both threads).
- **Targets:** `RandomGen.NextDouble` / `Random_NextDoubleImpl` (`rng`), `System.Random` (`sysrand`, none reached), `Slots_GetPositionOfSlotImpl` (`slotpos`). One row per (unit root, target, last game frame before the RandomUtil/RandomGen/Slots layer), with the shortest path.
- **Worker hooks** (ts3fixes `ApplyAutShadow`: `LocalizeString` x4, `IsAllowedInRoom`, `NumSimsDoingGardening`, `DidSimsMeetOnline`, the desire function, `GetPunishmentManagerFromSim`): a path through one is run only by the reference (`one_sided` = yes).
- **Guards** (docs/118 §5: `SeasonsManager.LazyAllocateManager`, `Relationship.Get`) are cut, with one row per caller.

## 2. Results

**Meta units:** after the two guards, no path reaches the RNG or the slot IC [verified: tool]. The 9 `Relationship.Get` callers in the closures pass `createIfNone: false` or enumerate, so `CalculateAttractionScore` (the draw) never runs [verified in source for 7; the `Sim`/`SimDescription.GetRelationship` wrappers pass their caller's flag, false from `CanVisitCelebrityLot` per docs/78]. The seasons allocation draws only while `sInstance` is null.

**Pair units**, by last game frame (the same for all 12 definitions unless noted):

| Target | Last game frame (route) | Threads | Review [verified in source unless tagged] | I-125-2/3 |
|---|---|---|---|---|
| rng | `Relationship.GetHighestRelationshipSimForPet` (C2 `CalculateScoreForObjectInteraction` -> `GetBFFOrSimWithHighestLikingForPet`) | both | pet actors; the **only both-sided draw** in the units; at the top of C2, before C2's posture and room calls | inside, fine |
| rng | `VisitCommunityLot.SharedVisitVenueTests` (VCL only) | ref | store-hours / resort-closed tooltip, then `return false` | holds |
| rng | `Autonomy.CheckAvailabilityTooltip`, `Sim.LocalizeString` | ref | snow / pregnant tooltip, then a fail result | holds |
| rng | `Motives.StressInteractionTest` | ref | failure text, then `true` = `Tuning_TooStressed` | holds |
| rng | `InteractionDefinitionUtilities.ResortTests` | ref | check-in tooltip, then `return false` | holds |
| rng | `MoodManager` stress lambda, `CleanableComponent.LocalizeString`, `Lot.CommonTestLocalizeString` (VL) | ref | tooltip lambdas / method group: created, never invoked by `ScorePair` | no draw |
| rng | `IsAllowedInRoom` from `CommonTests`, `CheckAvailabilityOnLot` and C2 | ref | stale room cache: the reference recalculates (`DivingBoard.TestRoutabilitySafety` -> `Sim.FullName` -> `LocalizeString`) and **refreshes the cache**, so the worker that runs after it reads the fresh cache without abort 10. The `CommonTests`/`CheckAvailabilityOnLot` calls run in `Test`, **before** the pet draw | **hole** |
| rng | `Pools.Pool.ReactToSkinnyDippers` / `ShouldReactPositiveToSkinnyDipper` (`CommonTests` -> `Posture.Satisfies` -> `SwimmingInPool` -> `Posture.Interactions`) | ref first | `TryPopulateInteractionList` is a **lazy fill**: the first read builds the list and calls `Pool.AddSkinnyDipper` (world writes, reactions, draws). In Mode R the reference fills it; in Mode W the worker can be first = off-thread world writes | **hole** + Mode W write |
| rng | `SimDescription.IsFemale` (C2 posture multiplier, box stall) | ref | Grim Reaper branch: localized gender string; the worker's empty string makes `IsFemale` itself differ. Needs a Grim Reaper actor in a horse posture [inferred: not reachable] | value, not RNG |
| rng, slotpos | `Punishment.IsInteractionBanned` -> `GetPunishmentManagerFromSim` | ref | manager create path only; the worker aborts 16 there | A4, fine |
| slotpos | `IsAllowedInRoom` recalculation (`Ladder` / `ElevatorDoors.GetRoomConnections`) | ref | the reference gets the live answer; the worker reads the cache or aborts 10 | fine |
| slotpos | `GameObject.GetPositionOfSlot` via `CommonTests` -> `TombRoomManager.IsObjectInAFoggedRoom` -> `DoesObjectConnectOnlyFoggedRooms` | both | the docs/121 "CommonTests branch". It runs only when the target's root parent is a `Stairs` or `RoomConnectionObject` on a lot with tomb or hidden rooms. `IsObjectInAFoggedRoom` returns false at once for a `Lot` [verified]; the other 6 implementors are not stairs or room connections [inferred] | **unreachable** for v0 |

No path reaches `System.Random` [verified: tool]. Static constructors are a residual gap: 44 game `.cctor`s draw within 4 hops [verified: IL index]. A type first touched inside a unit draws once, on whichever thread touches it first (the reference in Mode R) [inferred: the I-122-37 warm-up runs the definitions' and implementors' own `.cctor`s, not every type their closures touch].

## 3. Verdicts

- **I-125-2 holds statically.** Every draw path starts inside a unit, and no managed game code runs between units or before the first `RngUnit` [verified]. Residuals: `.cctor` first touches and other lazy fills (§2), which put a draw on one thread inside a unit, not outside one.
- **I-125-3 holds for every tooltip site** (each direct `LocalizeString` on a tooltip path is followed by a fail result, and the lambda tooltips never run) **but is too narrow as a Mode R argument.** Two other reference-only draw sources sit in `Test`, before the one both-sided draw (the pet branch in C2): the room-cache recalculation in `IsAllowedInRoom` (a draw only for Variation-token name strings) and the swimming posture's lazy interaction list. A pet actor with either one in the same pair gives A2 in Mode R with no snapshot bug behind it. Both cases are rare; neither is ruled out.
- **Slot IC:** the `CommonTests` path is statically unreachable for allow-list v0. Expect 0 learned slot keys and 0 `slotpos` aborts from the worker in G-A3. Any `slotpos` abort means a path this audit missed (a mod, or a dispatch the receiver rule cut). docs/125 §3 stays correct, just idle.

## 4. Proposed changes [design, not built]

1. **Reference-side draw bracket** (closes every hook-caused one-sided draw, whatever the path): in the reference run, a hooked call (`LocalizeString`, `IsAllowedInRoom`) draws from a scratch MT instead of the unit's stream. One IC pair `RngPause(bool)` around the hook's `Orig` call when `!OnWorker()` and in the reference. Cost: two IC calls per hooked call in the reference only.
2. **Room cache pre-warm:** at the request (sim thread, `TryRequest`), call `IsAllowedInRoom(lot, 0)` for the actor on its current lot and the request lots, so the cache is fresh before the reference and the worker run. Then neither thread recalculates in the run.
3. **Posture list guard:** at the request, read `actor.Posture.Interactions` on the sim thread (fills it as the game would). On the worker, abort when the posture's list is still null (new hook on `Posture.get_Interactions`, `OnWorker()` only). This also removes the Mode W off-thread write.
4. Count draws inside the reference that happen while a hook's `Orig` runs (`rngRefHooked`), so G-A3 shows whether 1 is needed before it is built.

## Reproduce

```sh
node tools/managed-callgraph.ts build --asm <repo>/game-copy/assemblies --out <repo>/game-copy/analysis/callgraph   # if the index is missing
node tools/rng-unit-paths.ts --asm <repo>/game-copy/assemblies --index <repo>/game-copy/analysis/callgraph/index.json > docs/data/rng-unit-paths.csv
node tools/rng-unit-paths.ts --cha ...   # plain class-hierarchy expansion: the 155,459-row noise count
```
