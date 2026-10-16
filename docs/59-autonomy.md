# 59. Autonomy: call graph, yield points, internal calls, caches, read and write sets (T-AUT-a, T-AUT-b)

Static read of the decompiled game scripts (Sims3GameplaySystems, Sims3GameplayObjects, Sims3StoreObjects, SimIFace, ScriptCore). No game run. It extends docs/04a §4 (autonomy) and docs/28 S2 (autonomy scoring: 11.1% of managed samples, 11 / 13 / 10 over three captures; the `AutonomyManager` task itself 5.5%). It is input for the internal-call work (T-IC-c, §1.4) and for any second-thread plan (docs/30 §6, §1.6 here).

Tags: [verified] = read in the decompiled code (file and method named); [inferred] = reasoning or name mapping not checked further. Class names are short: `Autonomy` = `Sims3.Gameplay.Autonomy.Autonomy`, `Sim` = `Sims3.Gameplay.Actors.Sim`.

## 1. Call graph of one autonomy search

### 1.1 Per-tick structure [verified]

- Exactly one task runs autonomy searches: `AutonomyManager`, a `Threaded` script object added once by `AutonomyManager.Startup`. Its `Simulate` loop pops **one** `Autonomy` per iteration (`kNumSimsCanRunAutonomy = 1`, a constant), runs its whole search, then calls `Simulator.Sleep(0)`. So at most one Sim's search starts per tick, for the whole world.
- Two FIFO queues (`LinkedList<Autonomy>`): high LOD and low LOD. `GetNextAutonomyQueue` alternates between them each iteration and falls back to the other when one is empty. Which queue a Sim joins is decided at enqueue time by `Autonomy.IsRunningLocalAutonomy`, a flag left behind as a side effect of the last `ShouldRunLocalAutonomy` read.
- The only producer in normal play is the Sim's own task: `Sim.DoInteraction` with an empty queue calls `LoopIdle`, then `AutonomyManager.Add` if the Sim is not already queued and either `CanRunAutonomyImmediately` (NPC whose last autonomous interaction failed, among others) or the idle delay has passed (`AutonomyDelayNormal`, `...DuringSocializing`, `...WhileMounted`, `...AfterUserCancellation`), or the Sim is a service Sim or has an active role. The Sim task then sleeps 2 ticks (not queued), 10 ticks (already queued) or 20 ticks (autonomy disabled).
- `AutonomyManager.AddWhileInteracting` is called only from three object types (picnic basket and picnic situation in Sims3GameplayObjects, baby dragon in Sims3StoreObjects); it sets `OverrideCurrentInteraction` so the search runs even though the Sim has a head interaction, but with meta autonomy off. It does two linear `LinkedList.Contains` scans.
- `AutonomyManager.AddReaction` has **no caller** in any decompiled assembly, so the reaction branch of `Simulate` never runs in the base game (mods could call it). Reactions instead reach `Autonomy.RespondToEvent` through `ReactionBroadcaster` / `Lot.AddReaction` in the caller's own task.
- Throughput consequence [inferred]: with N Sims waiting, a Sim waits about N ticks (more when a meta search spans ticks) between decisions; the per-tick cost is one Sim's full local search plus at least the first slice of its meta search.

### 1.2 Call graph table

Y = can yield (`Simulator.Sleep`), N = never yields itself. "ICs" names the native internal calls (`ScriptCore.<Class>::<Method>`, the DllImport table, docs/04a §2) reached directly or through one or two SimIFace/ScriptCore wrappers; the full list is in §1.4.

**A. Scheduling (Sim task and manager task)**

| # | Method (class) | Role | Yield | ICs | Tag |
|---|---|---|---|---|---|
| A1 | `Sim.DoInteraction` | Sim task loop body. Runs the head interaction, or idles and enqueues autonomy | Y: `Sleep(0)` after an interaction; `Sleep(2/10/20)` in the idle branch | `Objects_IsValid` (after an interaction), `Simulator_SleepImpl` | verified |
| A2 | `Sim.CanRunAutonomyImmediately` | Skips the idle delay (e.g. NPC after a failed autonomous interaction) | N | none seen | verified |
| A3 | `AutonomyManager.Add` / `Enqueue` | Sets `InAutonomyManagerQueue`, appends to the high or low LOD list | N | none | verified |
| A4 | `AutonomyManager.AddWhileInteracting` | Enqueue with `OverrideCurrentInteraction` (3 object callers) | N | none | verified |
| A5 | `AutonomyManager.OnAutonomyLODChanged` | Moves a queued Autonomy between lists (`LinkedList.Remove`, linear) when `SetIsHighAutonomyLOD` flips | N | none | verified |
| A6 | `AutonomyManager.Simulate` | Manager task: alternate queue, pop first, skip destroyed actor, `TryFindBestAction`, clear queued flag in `finally`, dead reaction branch, `Sleep(0)` | Y: one `Sleep(0)` per search, plus any yield inside the search | `Simulator_GetTaskImpl` (`HasBeenDestroyed`), `Simulator_GetCurrentTaskImpl`, `Simulator_SleepImpl` | verified |
| A7 | `AutonomyManager.GetNextAutonomyQueue` / `GetFirstNonEmpty` | High/low alternation with fallback | N | none | verified |
| A8 | `AutonomyManager.DequeueActor` | Removal on Sim destroy: linear scans of both lists, rebuilds the reaction `Queue` | N | none | verified |

**B. Search driver (`Autonomy`)**

| # | Method | Role | Yield | ICs | Tag |
|---|---|---|---|---|---|
| B1 | `TryFindBestAction` | `RunAutonomy(addToQueue: true)` | via B11 | - | verified |
| B2 | `RunAutonomy(bool)` | Sets search type `Autonomy`; for **low LOD** Sims runs `Update()` first (high LOD Sims get it from `SimUpdate.Simulate`) | via B11 | - | verified |
| B3 | `Update` | Per-Sim time step: motives, buffs, mood, skills, careers, relationships, opportunities, thought balloons, map tags, two `EventTracker.SendEvent` (heartbeat, DnP sync), DnP refresh flag. Listener code is arbitrary script | N [inferred for listeners] | not mapped (separate subsystem, docs/28 S4) | verified (calls) |
| B4 | `RunAutonomy(CommodityKind, bool)` | Bails if the Sim has a head interaction (unless override, which also turns meta off); `ClearScoring`; allocates the check table; insane-trait shortcut; conversation socials; exit-conversation balloon; `ScoreInteractionsOnObjects`; aborts if that returns non-Continue; chooses; pushes; NPC with no pick and not selectable, on a lot, no service → `Sim.MakeSimGoHome` | via B11 | - | verified |
| B5 | `TestInsaneAutonomy` | Collects all lot and inventory interaction pairs into a new list, tests random ones (`InteractionDefinition.Test` directly), pushes the first that passes | N | per-Test ICs (§1.4 group B) | verified |
| B6 | `SocialComponent.ScoreSocialsOnSims()` / `ChooseSimToSocializeWith` / `ScoreSocialsOnSims(Sim)` | In a conversation and on our turn: weighted pick of a member, then scores every social pair from `GetAllInteractionsForSim` through C1 | N | `Random_NextDoubleImpl` (weighted pick) [inferred via `RandomUtil`] | verified |
| B7 | `ScoreInteractionsOnObjects` | Turns on the per-search cache (C0), runs local scoring if `ShouldRunLocalAutonomy`, meta scoring if allowed | via B13 | - | verified |
| B8 | `ShouldRunLocalAutonomy` → `IsSufficientlyOnScreenForHighLODSimulation` | Local autonomy only for "high LOD" Sims: active household, active lot, same lot as the active Sim, hosted guest, daycare, resort worker, or outdoors and on screen. Re-evaluated at most every 60 ticks | N | `World_IsObjectOnScreenImpl`, `World_IsPositionOutsideImpl` + `Objects_GetPosition` (`IsOutside`), `Slots_GetParentIdImpl` (`LotCurrent`) | verified |
| B9 | `ScoreInteractionsForLocalAutonomy` | Picks the lot (override lot, current lot, or `FindClosestLotForLocalAutonomy` on the world lot); scores `lot.Map`, the Sim's own map, **every other Sim's map on the lot** (not on the world lot), the inventory map (not for NPCs on the world lot), situation interactions (generic search only), solo interactions | **N** (no yield call anywhere in it) | `Query_GetObjectsOnLot` (`lot.GetObjects<Sim>()`, fresh array each search) | verified |
| B10 | `FindClosestLotForLocalAutonomy` | Copies public meta objects + known lots into a new list; nearest lot under a distance threshold | N | `Objects_GetPosition` x2 per lot | verified |
| B11 | `ScoreInteractions(map, c)` / `ScoreInteractions(map)` | Generic search: for every commodity with use value > 0 and every desire below its max (plus energy at bedtime) → B12. Commodity search: B12 for that commodity only | N | - | verified |
| B12 | `ScoreInteractionsForCommodity` | Walks the map's list for one commodity, C1 per pair. A non-reset exception removes the pair from the map (self-healing) | N | - | verified |
| B13 | `ScoreInteractionsForMetaAutonomy` | Gate `CanRunAnyMetaAutonomy`; `Sim.CalculateNumSimsAtHomeOrGoingHome`; restarts the static timer; full meta: copies public meta objects + known lots into a new list and scores each (B14); toddlers/pets inside; current lot, home lot, career location as meta objects; go home / stay home / go out to yard pairs | **Y**: `YieldIfPossible` at 4 sites (§1.3) | `StopWatch_RestartStopwatch`, `StopWatch_GetElapsedTimeFloatStopwatch`, `Simulator_GetTaskImpl` (`HasBeenDestroyed` per meta object) | verified |
| B14 | `ScoreInteractionsForMetaObject` | Walks the object's `MetaAds`; commodity filter via `Tradeoff.SatisfiesCommodity` or `MetaAdSatisfiesCommodity`; C1 per ad | N | - | verified |
| B15 | `Sim.CalculateNumSimsAtHomeOrGoingHome` | Loop over `Sim.VisibleActors`, counts at home or running `GoHome`, into a static | N | `Slots_GetParentIdImpl` (`LotCurrent` inside `IsAtHome`) [inferred] | verified (loop) |
| B16 | `ConsiderBringingToddlersInside` / `ConsiderBringingPetsInside` | Only when meta runs and local does not: synthesises pick-up pairs for outdoor toddlers/pets, C1 each | N | `World_IsPositionOutsideImpl` + `Objects_GetPosition` (`IsOutside`) | verified |
| B17 | `YieldIfPossible` | See §1.3 | Y | `Simulator_GetCurrentTaskImpl` (x2), `Simulator_SleepImpl`, `Simulator_GetTaskImpl` | verified |

**C. Per interaction pair: dedupe, test, score**

| # | Method | Role | Yield | ICs | Tag |
|---|---|---|---|---|---|
| C0 | `CacheValuesToOptimizeTestFunctions` | Once per search: on fire, baby coming, neat, party guest, grouping situation, scuba, fire near on lot, tomb room, selected household members | N | `Slots_GetParentIdImpl` (`LotCurrent`), tomb room checks [inferred] | verified |
| C1 | `CalculateScoreAndAddToCandidates` | `InteractionCheckTable.TryAdd` (skip pairs already scored in this search), then C2, then `BestScores.Add` | N | - | verified |
| C1a | `InteractionCheckTable` (`Allocate`, `TryAdd`, `Clear`, `Free`) | 109-bucket hash set keyed by pair hash (definition ^ target); tables and nodes come from **static** pools | N | - | verified |
| C2 | `CalculateScore` → `InteractionObjectPair.TestAndCalculateScore` | Builds the parameters struct, runs C3; on pass, `IOverridesCalculateScore.CalculateScore` (a few definitions override it) or C8 | N | - | verified |
| C3 | `InteractionDefinition<TActor,TTarget,TInteraction>.Test` (base) | Null checks; walks the target's parent chain looking for the hand-tool drag object; tuning flags (disallow autonomous, fun exit, player Sim); C4; `UpdateCacheIfNeeded`; C5; special-case tests (C6 or the meta variant); **the definition's own `Test(actor, target, autonomous, ref tooltip)`**, which is arbitrary script, mods included; C7; tooltip special cases | N for the base; definition bodies not audited [inferred N] | `Slots_GetParentIdImpl` (one per parent level, at least one per Test), `Simulator_GetTaskImpl` (per real parent) | verified |
| C4 | `Autonomy.CheckAvailability` → `CheckAvailabilityOnLot` | Age/species, world restriction, motive threshold, traits, skill, career, buffs, owner-lot rules (greeted, community, public room), inventory, tomb room, mood, occult. Uses `Sim.IsAllowedInRoom` | N | `GameUtils_GetWorldType` / `GameUtils_GetWorldName` (only with a world restriction), `World_IsPositionOutsideImpl` + `Objects_GetPosition` (community lot and "outside" flags), `Slots_GetParentIdImpl` (`LotCurrent`) | verified |
| C4a | `Sim.IsAllowedInRoom(lotId, roomId)` | Lookup in a per-lot, versioned allowed-rooms cache; rebuilds it when the version moves (room flood over portals) | N | rebuild: `Query_GetObjectsOnLot` [inferred from the portal scan in `Lot`] | verified (cache) |
| C5 | `InteractionDefinitionUtilities.CommonTests` / `ScubaLotTests` | Autonomous: blocked, charred, not in world; allowed-in-room for the target's room; dead active Sim hidden check; baby/toddler level and standing tests; birth, fire, fire on lot, fogged tomb room, performance show, zombie; scuba lots (EP10) | N | `World_GetLotLocationImpl` (target with no lot, terrain), `World_ObjectGetHiddenFlags` (rare), `Slots_GetParentIdImpl` (`LotCurrent`) | verified |
| C6 | `InteractionDefinitionUtilities.SpecialCaseTests` | Target Sim age tests, horse in box stall, `InteractionsAreDisabled`, harvestable, imaginary friend, punishment ban (EP4), art pedestal parent walk, treasure pedestal, shut-down bot (EP11), **target on roof** | N | `World_IsOnRoofImpl`, `Slots_GetParentIdImpl` (pedestal walks) | verified |
| C7 | `Autonomy.CheckAvailabilityTooltip`, `SpecialCaseTooltipTests`, `ResortTests` | Snow level and heavy snow (Seasons), stress exit, pregnant, resort check-in (EP10), neat Sim and dirty object. On failure it builds a localized tooltip string **eagerly**, even for autonomous tests whose tooltip nobody shows | N | `World_HasAnyRoofOrFloorAbovePosImpl`, `World_GetLotLocationImpl`, `World_GetSeaLevelImpl` (shelter checks, Seasons only); `LocalizedStringService_*` on failing tooltips [inferred mapping] | verified |
| C8 | `Autonomy.CalculateScoreForObjectInteraction` | Tuning scoring function (`ScoringFunctionDelegate`, by name); meta ad score; C9; multipliers: outdoors traits, loyal dog, loner (iterates all actors on the target's lot), party host room, group, teen BFF, babysitter/butler, blocking score, public room, posture chain, vampire in daylight, box stall, snub targets (`SimDescription.Find` + two positions per target), allowed-in-room flag, **rain/hail on outdoor targets (creates a full `InteractionInstance` per pair just to test shelter)**, inventory in rain; then C11, interaction modifier delegate, in-use multiplier | N | `World_IsPositionOutsideImpl` + `Objects_GetPosition` (target `IsOutside`, every pair with a tradeoff), `Objects_GetPosition` (snub), `World_GetSunsetTime` / `World_GetSunriseTime` (vampire), Seasons shelter ICs (C7 list), `Slots_GetParentIdImpl` (`InUse`, `LotCurrent`) | verified |
| C9 | `Tradeoff.GetScore` → `InteractionScorer.ScoreList` + `TimeMultiplier` → `InteractionScorer.TimeToRoute` | Motive value of the outputs times a walk-time factor. Walk time is straight-line distance plus a per-level penalty, **no route plan** | N | `Objects_GetPosition` x2 (distance), `Slots_GetParentIdImpl` (`LotCurrent` x2) | verified (`TimeToRoute`); `ScoreList` internals [inferred managed math] |
| C10 | `MetaInteractionDefinition.MetaAdScore` | For lot meta ads: walks the lot's bubble-up commodity map and values each satisfied commodity against the Sim's motive | N | none seen | verified (first part) |
| C11 | `Autonomy.ComputeCheckScore` | Situation checks (per situation and child, hashtable per actor) and lot checks; `Check.Test` per matching check | N | not mapped (`Check.Test` bodies) | verified |
| C12 | `Autonomy.CalculateScoreForSocial` | Social scoring (reached through the social definitions' score override) | N [inferred] | not mapped | inferred (route) |

**D. Choose and push**

| # | Method | Role | Yield | ICs | Tag |
|---|---|---|---|---|---|
| D1 | `BestScores.Add` → `PriorityQueue.Add` / `AddInteractionOnlyIfBest` | Keeps the **top 10** scores in a preallocated min-heap; `AlwaysChooseBest` tunings kept one per definition in a side list | N | - | verified |
| D2 | `AssignProbabilitiesAndChooseInteraction` → `BestScores.ChooseInteraction` | Merges the side list; randomness 0 (distance-prioritised searches): random pick among scores within a fraction of the top; otherwise exponential weights and one random draw | N | `Random_NextDoubleImpl` | verified |
| D3 | `GetCreationParameters` + `InteractionCandidate.CreateInstance` | Builds the chosen `InteractionInstance` (allocates; `ConfigureInteraction`) | N | not mapped | verified |
| D4 | `ConsiderPerformingAutonomousAction` | Empty queue: push. Else replaces an autonomous head interaction of lower priority only if the score is considerably higher, with a time-based random interrupt chance | N | `Random_NextDoubleImpl` | verified |
| D5 | `PerformInteraction` → `UseThisInteraction` | `IImmediateInteraction`: new `ImmediateInteractionTask` via `Simulator.AddObject`; reaction: `Sim.InterruptWithNewInteraction`; normal: `InteractionQueue.Add`. The Sim's own task then runs it on its next `DoInteraction` pass (A1) | N | `Simulator_AddObjectImpl` (immediate only) | verified |
| D6 | `ClearScoring` | Clears the heap, returns the check table to the pool | N | - | verified |
| D7 | `Sim.MakeSimGoHome` | Fallback when an NPC search found nothing (docs/04a §3 covers its retry behaviour) | not mapped here | not mapped | verified (call) |

**E. Other entry points into the same scoring (run inside the caller's task)**

| Entry | Callers in decompiled code | Meta | Yields |
|---|---|---|---|
| `FindBestActionForCommodityOnLot` | 23 call sites | no | N |
| `FindBestActionForCommodity` | 6 | no | N |
| `FindBestSocial` | 5 | no (socials on one target) | N |
| `FindBestAction` (generic) | 4 | no | N |
| `FindActionsForCommodity` | 2 | no | N |
| `AutoSolveCommodity` (buff auto-solve) | 1 | yes when in motive distress | N: search type is `BuffAutoSolve`, so `YieldIfPossible` returns without sleeping |
| `RespondToEvent` (reactions) | `ReactionBroadcaster` | no (single pair, C2 only) | N |
| `ScoreAllInteractionsForInspectionWindow` | debug UI | yes | N (inspection flag skips the yield checks) |

All [verified]. The commodity searches temporarily overwrite the Sim's use value for that commodity in `InteractionScorer` and restore it after. Any of them started while a manager search for the same Sim is parked in a yield sets `mAutonomySearchWasInterrupted`, and the parked search aborts on resume with nothing pushed [verified in `SetSearchType` / `YieldIfPossible`].

Path size: 8 scheduling methods, 17 search-driver methods, 13 per-pair method groups (about 20 methods), 7 choose/push methods, 8 side entry points: about 55 methods on the main path, plus the per-definition `Test` and scoring-function bodies, which are open-ended (every interaction in the game and in mods).

### 1.3 Yield points [verified]

| # | Where | Condition | Effect |
|---|---|---|---|
| Y1 | `AutonomyManager.Simulate`, end of each iteration | always | `Sleep(0)`: next search no earlier than the next time the scheduler runs this task [inferred: next tick, docs/04a §1] |
| Y2 | `ScoreInteractionsForMetaAutonomy`, after each scored meta object (full meta loop, current lot, home lot, career location: 4 sites) | not the inspection window, count of meta objects a multiple of 5, static `sUpdateTimer` (millisecond stopwatch) above 1 ms, search type is `Autonomy`, and `CheckYieldingContext` passes | `Sleep(0)`; on resume, abort if another search type interrupted (`SearchInterrupted`) or the actor was destroyed (`ActorDestroyed`), else restart the timer and go on |
| Y3 | `Sim.DoInteraction` | idle branch | `Sleep(2/10/20)` on the Sim's task, not the search |

What never yields:
- The whole local search (B9 to B12 and all of C), every side entry point in table E, the low-LOD `Update` (B3), the choice and push (D).
- The timer restarts at the **start of the meta phase**, after the local phase, so local time is not charged to the 1 ms budget. A manager search therefore always runs its full local search plus at least 5 meta objects and at least 1 ms of meta scoring in one slice.
- The interaction definitions' own `Test` bodies and tuning scoring functions are expected not to yield [inferred; not audited across all definitions]. A yield there inside a side entry point would run on the caller's interaction task.

### 1.4 Internal calls on the path (input for T-IC-c)

Names checked against the extracted IC name table (docs/04a §2). The wrapper chain is SimIFace static → interface instance → ScriptCore DllImport [verified for each row's wrapper unless tagged].

| IC | Reached from | Frequency |
|---|---|---|
| **Group A: per search** | | |
| `ScriptCore.Simulator::Simulator_GetTaskImpl` | `HasBeenDestroyed` (A6, B13 per meta object, situations), `GameObject.GetObject` for every real parent | per search + per meta object |
| `ScriptCore.Queries::Query_GetObjectsOnLot` | B9 `lot.GetObjects<Sim>()` (allocates an array) | 1 per local search on a non-world lot |
| `ScriptCore.StopWatch::StopWatch_RestartStopwatch` | B13 | 1 per meta search + 1 per yield |
| `ScriptCore.StopWatch::StopWatch_GetElapsedTimeFloatStopwatch` | B13 | every 5th meta object |
| `ScriptCore.Simulator::Simulator_GetCurrentTaskImpl` | `CheckYieldingContext` (twice per yield: once directly, once inside `Simulator.Sleep`) | per yield |
| `ScriptCore.Simulator::Simulator_SleepImpl` | Y1, Y2, Y3 | per search + per yield |
| `ScriptCore.World::World_IsObjectOnScreenImpl` | B8 LOD check | at most 1 per Sim per 60 ticks, only outdoors / room 0 off the active lot |
| `ScriptCore.Random::Random_NextDoubleImpl` | D2, D4, B6 | 1-2 per search |
| `ScriptCore.Simulator::Simulator_AddObjectImpl` | D5 immediate interactions | rare |
| **Group B: per tested or scored pair (the hot set)** | | |
| `ScriptCore.Slots::Slots_GetParentIdImpl` | C3 drag-object parent walk (every Test), every `LotCurrent` and `InUse` read (C4, C5, C8, C9), pedestal walks (C6) | several per pair |
| `ScriptCore.Objects::Objects_GetPosition` | `IsOutside` (C8 for every pair with a tradeoff, C4 for some), C9 distance (x2), snub check (x2 per snub target), B10 | 3+ per scored pair |
| `ScriptCore.World::World_IsPositionOutsideImpl` | `IsOutside` (C8, C4, B8, B16) | 1+ per scored pair |
| `ScriptCore.World::World_IsOnRoofImpl` | C6 | 1 per Test that reaches the special-case tests |
| **Group C: conditional** | | |
| `ScriptCore.GameUtils::GameUtils_GetWorldType`, `GameUtils_GetWorldName` | C4 world restriction | per pair whose tuning restricts worlds |
| `ScriptCore.World::World_GetLotLocationImpl` | C5 target without a lot, `LotManager.GetLotAtPoint` in shelter and fog checks | Seasons rain/hail: per outdoor pair |
| `ScriptCore.World::World_HasAnyRoofOrFloorAbovePosImpl` | Seasons shelter check (C7, C8) | rain/hail/deep snow: per outdoor pair |
| `ScriptCore.World::World_GetSeaLevelImpl` | shelter check on diving lots | rare |
| `ScriptCore.World::World_GetSunsetTime`, `World_GetSunriseTime` | `SimClock.IsNightTime` in C8 | vampire actor: per outdoor pair |
| `ScriptCore.World::World_ObjectGetHiddenFlags` | C5 dead active non-selectable actor | rare |
| `ScriptCore.LocalizedStringService::LocalizedStringService_GetLocalizedStringBy*` | failing tooltip tests (C7) | per failing pair [inferred wrapper mapping] |
| `ScriptCore.Objects::Objects_IsValid` | A1 (Sim task, after an interaction) | not on the search itself |

Size: **21 ICs** (9 per search, 4 per pair, 8 conditional), plus `Objects_IsValid` on the Sim side. Not mapped (left for T-IC-c): the low-LOD `Update` subtree (B3), `InteractionQueue.Add`, `CreateInstance` / `ConfigureInteraction`, `Check.Test`, posture multipliers, and every definition-specific `Test` and scoring function.

Observations for T-IC-c [inferred, from the frequencies above]:
- The per-pair set is small and repetitive: parent id, position and is-outside are read again and again for the same actor and target inside one search. A per-search memo of actor position, actor `LotCurrent` and target outside/position would remove most of group B without touching game logic.
- Rain and hail multiply the per-pair cost: one `InteractionInstance` allocation plus `GetLotLocation` and a roof query per outdoor pair (C8).

### 1.5 Existing caches [verified]

| Cache | Where | Scope | What it saves |
|---|---|---|---|
| Commodity → pair lists | `CommodityInteractionMap` (`lot.Map`, each Sim's map, inventory map; `Dictionary<ulong, List<InteractionObjectPair>>`) | maintained on object add/remove and tuning reload | no per-search object enumeration; local search walks prebuilt lists per commodity |
| Tuning lookup | `InteractionObjectPair.AddTuning`: `sRequiresTuningCache` (per definition type), `sTuningCache` (definition type, target type) | process | reflection and tuning lookup once per type pair |
| Actor facts | `CacheValuesToOptimizeTestFunctions` + `mUseCachedValues` (C0) | one search | 9 actor-level checks done once, not per Test (`UpdateCacheIfNeeded` recomputes per Test when no search is active) |
| Scored-pair set | `InteractionCheckTable` (C1a), pooled | one search | each pair is tested and scored once per search even if it serves several commodities |
| Top candidates | `BestScores` / `PriorityQueue(10)`, preallocated | per Autonomy object, cleared per search | no sort of all candidates |
| LOD | `IsSufficientlyOnScreenForHighLODSimulation` (`mLODCheckTick`, 60 ticks) | per Sim | LOD walk and on-screen IC at most every 60 ticks |
| Allowed rooms | `Sim.IsAllowedInRoom` versioned per-lot dictionary | per Sim, per lot version | room flood only after the lot changes |
| Fogged tomb rooms | `TombRoomManager.IsObjectInAFoggedRoom(target, useCachedValues: true)` in C5 | not traced | inferred: per-object fog result |
| Object fields | `GameObject.RoomId`, `Level`, owner lot, `InWorld`/`InInventory`/`Charred` flags, blocking time | managed fields | no IC for these reads |
| Meta home pairs | `mMetaInteractionGoHome`, `...StayHome`, `...GoOutToYard` | rebuilt only when the home lot changes | no allocation per search |

Not cached (each search redoes it) [verified]: Test results and scores across searches (even when nothing on the lot changed); the Sim array on the lot (`Query_GetObjectsOnLot`); the copy of public meta objects + known lots (B10, B13); `CalculateNumSimsAtHomeOrGoingHome`; motive/desire use values feeding B11.

### 1.6 Notes for a second managed thread (docs/30 §6) [verified unless tagged]

Static or shared state the search writes, beyond the live world reads docs/28 S2 already lists:
- `Autonomy.sUpdateTimer` (one static stopwatch for all Sims), `InteractionCheckTable` static table and node pools, `Sim.sNumSimsAtHomeOrGoingHome`, `LiveDragHelperModel` cached drag object (cleared after `FindBestAction`), `InteractionObjectPair` static tuning caches (add-only).
- Per-Sim state: `InteractionScorer` use values (temporarily overwritten by commodity searches), `mBestScores`, `mCurrentSearchType` / `mAutonomySearchWasInterrupted`, `IsRunningLocalAutonomy` (read by the manager's enqueue).
- Side effects inside scoring: removal of throwing pairs from shared maps (B12, B5), `InteractionInstance` allocation in the rain path (C8), event sends in the low-LOD `Update` (B3).
- So a worker could at most run C2-C11 on a snapshot; the pick and push (D) and `Update` (B3) must stay on the sim thread [inferred].

## 2. Read set, write set and interaction `Test()` sample (T-AUT-b)

Static read of the same decompiled scripts as §1, no game run. Scope: the manager search (B, C, D), the side entry points (E) and the per-definition `Test` bodies. It answers docs/62 §4: which state a worker must be handed so that the scoring path does not touch live game state or call an IC. IC verdicts are from `docs/data/ic-safety.csv` (docs/62). Tags as in §1. Sizes are estimates [inferred] unless a measurement is cited.

### 2.1 Method

- **Core path [verified]:** member-access inventory of the §1.2 methods in `Autonomy`, `InteractionDefinitionUtilities`, `InteractionScorer`, `Tradeoff`, `MetaInteractionDefinition` and `InteractionObjectPair` (about 1,540 lines, 483 distinct receiver.member chains), then read by hand where a chain hides an IC.
- **`Test()` population [inferred, mechanical]:** a scan pulled every `Test(actor, target, isAutonomous, ref tooltip)` override from all assemblies: **4,932 bodies, 4,342 without DEBUG/cheat definitions**. Each body was expanded one level into same-file helpers and tagged by name patterns (wrapper names that reach an IC, writes, yields, random draws). Not followed: helpers in other files, virtual calls, tuning scoring functions, `ActionData` procedural social tests. The scan undercounts ICs and overcounts writes (a local list `Add` or a lambda looks like a write).
- **Sample [verified by reading]:** 30 common definitions (§2.4) plus 30 drawn with a seeded RNG (mulberry32, seed 59) from the 4,342; each body and its helpers, including cross-file helpers where the class depended on them, was read by hand.
- Also counted [verified]: **225** definitions override the parameter-level `Test(ref InteractionInstanceParameters, ...)` (the socials among them), **202** bodies call `base.Test`, and **22** classes implement `IOverridesCalculateScore`. These run on the same path and are open-ended like the bodies.

### 2.2 Read set

Grouped by owner. "IC" = reached through a SimIFace wrapper; the verdict column is from `ic-safety.csv`. Everything else is a managed field or property read.

**A. Actor (`Sim`, `SimDescription`, managers): per actor, once per search** [verified unless tagged]

| Group | What the path reads | Where | IC |
|---|---|---|---|
| Identity | age, species, gender, `CASAGSAvailabilityFlags`, occult types and current occult, robot/bot/ghost/vampire/mummy/zombie/plant flags, celebrity level, `IsVisuallyPregnant`, death style | C4, C5, C6, most Tests | none |
| Placement | `LotCurrent`, `LotHome`, `RoomId`, `Level`, `IsOutside`, `IsAtHome`, `Position`, hidden flags, `InWorld`, `Bed` (lot, room) | C4, C5, C8, C9, Tests | `Slots_GetParentIdImpl` (LotCurrent walk), `World_IsPositionOutsideImpl` + `Objects_GetPosition`, `World_ObjectGetHiddenFlags`: PROXY |
| Motives | `Motives.GetValue` per commodity, motive tuning (insatiable), distress and stress tests, `MoodManager.MoodValue`, mood thresholds | C4, C9, C10, Tests (bath, shower, go home) | none |
| Scorer | `InteractionScorer` use values, desire curves (`GetDesireYFromX`, `MaxY`), interaction modifier, commodity-desire map | B11, C9, Tests | none |
| Buffs, traits, skills | `BuffManager.HasElement`/`HasAnyElement`/buff list, `TraitManager.HasElement`, `SkillManager.GetSkillLevel`, `IsAutosolving` | C4, C8 (outdoors, loner, loyal dog traits), Tests | none |
| Career, service | `Occupation` (guid, highest level), `CareerManager`, `CareerLocation`, `Service` type and motives, performance-career show flag | C4, C5, B13 | none |
| Social | `Conversation`, `SocialComponent` (only-allowed social), `SnubManager` targets, greeted lots (`IsGreetedOnLot` → household greeted set), `Relationship.Get` (LTR, bits, liking) to targets on the lot | B6, C8, C12, socials | none |
| Activity | `Posture` (container, chain, `Satisfies`), `InteractionQueue` head and priority, `CurrentInteraction`, `IsSleeping`, situations of the actor (`SituationComponent`, party, grouping, daycare, fire) | C5, C8, C11, most Sim-target Tests | none |
| Household, control | `Household` (roommates, real estate, greeted set), `IsSelectable`, `IsNPC`, `IsActiveSim`, `AutonomyRestrictions.GetLevel` | C4, Tests (go home, visit) | none |
| Inventory | `Inventory.Contains`, `Find`/`FindAll`, inventory commodity map | B9, C4, C5, Tests | none |
| Caches | `IsAllowedInRoom` (per-lot room cache, version), the 9 C0 cached flags, `mLODCheckTick`, times (`WhenArrivedOnLot`, `LastAutonomousVisitDate`, last wake-up, last sip) | C4a, C0, Tests | rebuild: `Query_GetObjectsOnLot` [inferred] |

**B. Other Sims on the lot (as targets of socials and Sim interactions): per Sim** [verified]: identity flags as above, `ShowSocialsOnSim`, `CanBeSocializedWith`, `InWorld`, posture (swimming, kissing booth, box stall, carried), queue head (sleeping, rabbit hole, eating/peeing, user-directed priority), `IsSleeping`, `IsLeavingLot`, `LotCurrent`/`LotHome`/`IsOutside` (both sides), greeted state, celebrity level, object in right hand (`Slots_GetContainedObject`, PROXY), `SimRoutingComponent.IsInCarSequence`, active role, relationship to the actor. Loner and babysitter multipliers read **every Sim on the target's lot or in the household** (room, sleeping, age).

**C. Objects (targets): per object** [verified]

| Group | Reads | IC |
|---|---|---|
| Hierarchy | `Parent` chain (drag-object walk C3, pedestal walks C6, `InUse`, `LotCurrent`) | `Slots_GetParentIdImpl` PROXY (hot rank 1) |
| Place | `Position`, `IsOutside`, `IsOnRoof`, `RoomId`, `Level`, owner lot, `IsInPublicResidentialRoom`, on diving floor, fogged tomb room | `Objects_GetPosition`, `World_IsPositionOutsideImpl`, `World_IsOnRoofImpl` PROXY (ranks 2-4); `RoomId`/`Level` are managed fields |
| Life cycle | `InWorld`, `InInventory`, `Charred`, `IsBlocked`, `HasBeenDestroyed`, `InteractionsAreDisabled` | `Simulator_GetTaskImpl` PROXY (rank 5) for destroyed |
| Use | `InUse`, `ActorsUsingMe`, `IsActorUsingMe`, blocking score, in-use multiplier | parent walk |
| Object state (Tests) | `Repairable.Broken`, turned on, stocked/empty, fridge inventory, food recipe, trash units, mailbox flag, `CanBeUsedAsBed`, viewable, harvestable, cooldowns, owner fields | none (managed) |
| Item | `ItemComp.InventoryParent`, inventory owner lot | none |

**D. Lot: per lot scored (current, home, career, every meta lot)** [verified]: `LotId`, type flags (community, residential, world lot, open venue, diving, resort check-in), `Household`, `Map` (commodity → pair lists), `Checks`, `FireManager` (fire on lot, near source), public room list, per-type object caches (`GetObjectsInRoom` hits a managed cache; a miss and every `GetObjects<T>`/`CountObjects<T>` go to `Query_GetObjects`/`Query_CountObjects`, PROXY), bubble-up commodity map (meta), `MetaAds` per meta object, `GetMetaAutonomyType`.

**E. World and globals** [verified]: `SimClock` ticks (managed statics), `SimClock.IsNightTime` (`World_GetSunsetTime`/`GetSunriseTime`, PROXY), `SeasonsManager` (enabled, weather, snow level, shelter test: `World_GetLotLocationImpl`, `World_HasAnyRoofOrFloorAbovePosImpl`, `World_GetSeaLevelImpl`, PROXY), world type and name (`GameUtils_GetWorldType`/`GetWorldName`, PROXY, constant per load), product flags (`GameUtils.IsInstalled` → `GameUtils_GetProductFlags`, **SAFE**), `PlumbBob.SelectedActor`, active household and its lot, `GameStates.IsBuildBuyLikeState`, `LiveDragHelperModel` dragged object, `Sim.VisibleActors` (B15), `SimDescription.Find` (snub), meta-autonomy public objects + known lots, `LotManager.GetWorldLot`/`GetLot`/`GetLotAtPoint`, situation registries (`Situation.GetSituations<T>`), `TombRoomManager`, `Party`, `Punishment` bans.

**F. Tuning (process-wide, read-only after load)** [verified reads; read-only inferred]: per-pair `InteractionTuning` (`Availability` flags and thresholds, world restrictions, scoring-function name, posture preconditions, `Checks`, commodity outputs), `TraitTuning`, `SocialComponent`/`Babysitter`/`Butler`/`Party` multipliers, `k*` constants in Tests (550 bodies), `ActionData` social tuning, motive and desire curves. Written only at load and tuning reload [inferred], so a worker can read it in place.

**Size of the read set.** About 100 distinct members on the core path: ~40 actor, ~25 object, ~15 lot, ~20 world/global [verified count of distinct chains, grouped by hand]. Test bodies add mostly managed reads of the same groups (actor flags 607 bodies, lot or parent 1,183, object state 901, queue/posture 575, motives/buffs 405, traits 257, situations 145) [inferred, mechanical].

### 2.3 Write set

Everything the scoring path writes outside its own locals [verified unless tagged]:

| Owner | Write | When | Worker consequence [inferred] |
|---|---|---|---|
| `Autonomy` (per Sim) | `mBestScores` heap and side list, `mHasBeenScored` check table, `mUseCachedValues` + 9 cached flags (C0), `mCurrentSearchType`, `mAutonomySearchWasInterrupted`, `IsRunningLocalAutonomy`, `mLODCheckTick` | every search | worker-private copies; only the result goes back |
| `InteractionScorer` (per Sim) | use value of one commodity overwritten and restored | commodity searches (E) | worker gets its own use-value table |
| `Sim` | `IsAllowedInRoom` per-lot cache rebuild (C4a); exit reasons + `OnExitReasonsAdded` event (one sampled Test, §2.4) | room cache: after a lot change; exit reason: Test-specific | precompute room access in the snapshot; definitions that write the actor stay on the sim thread |
| Statics | `Autonomy.sUpdateTimer`, `InteractionCheckTable` table and node pools, `Sim.sNumSimsAtHomeOrGoingHome`, `LiveDragHelperModel` cached drag object, `InteractionObjectPair` tuning caches (add-only), meta home pairs `mMetaInteractionGoHome`/`StayHome`/`GoOutToYard` | per search / per yield / on home-lot change | per-worker pool and counter; tuning cache warmed on the sim thread |
| Random state | script RNG (`RandomGen`, `Random_NextDoubleImpl` PROXY): D2, D4, B6, plus any Test that draws (PokerTable chair pick, 12 bodies by scan) | per search, per such Test | worker-owned RNG seeded by the sim thread (docs/27, docs/62 rank 10); definitions that draw in `Test` are not deterministic against the live game anyway |
| Shared maps | `CommodityInteractionMap.RemoveInteractionForObject` on a throwing pair (B12, B5) | rare | report the pair; the sim thread removes it |
| Definition singletons | instance fields written inside `Test` (BotChargingStation `Recharge` caches its `Owner`; scan: 72 bodies assign a non-local name, part of them lambdas and discards) | per Test | shared across Sims; such definitions stay on the sim thread |
| Game objects | `PlantObjectHere` creates, adds, ghosts and destroys a soil object inside `Test`; `Jetpack.FlyAround` creates a jig and runs a placement search (both via `CreateObjectOutOfWorld`, `Simulator_CreateObjectImpl` **UNSAFE**) | per Test | never on a worker |
| UI | thought balloon on conversation exit (B4), not inside scoring | per search | stays on the sim thread |
| Heap | query result arrays, `new List` in 265 Test bodies, closures and tooltip callbacks, eager localized tooltip strings (494 bodies build `CreateTooltipCallback(LocalizeString(...))` on a failing branch: `LocalizedStringService_*` PROXY), one `InteractionInstance` per outdoor pair in rain or hail (C8) | per pair | allocation is legal on a worker with docs/30 P3; the localized string IC and the rain instance are not, see §2.5 |

Events: no `EventTracker.SendEvent` in any sampled or scanned `Test` body (0 of 4,342 by name) [inferred, one-level scan]; the only event raise found is `OnExitReasonsAdded` above. Yields: **none** in any `Test` body (0 of 4,342 call `Simulator.Sleep`, `Yield`, `YieldIfPossible` or a wait) [inferred, one-level scan], which supports the §1.3 assumption.

### 2.4 `Test()` sample

Classes: **M** pure-read, managed state only; **H** pure-read plus hot-set ICs only (parent walk, position, outside, roof, destroyed: docs/62 ranks 1-5); **P** pure-read plus another PROXY IC (query, slots, pool/terrain); **U** reads an UNSAFE IC; **W** writes outside its locals (includes random draws); **Y** yields. A definition in M, H or P is pure-read. All rows [verified] by reading the body and the helpers named.

**Common set (30).** Picked as the definitions every Sim or the usual household objects offer (every Sim to every other Sim, chairs, fridge, bathroom, bed, TV, computer, bookshelf, stereo, decor, mailbox, trash can, visits), since there is no static per-household count.

| # | Definition | Reads (main) | ICs (verdict) | Class |
|---|---|---|---|---|
| 1 | socials (`SocialInteractionA`, all action keys; parameter-level Test + `CanSocializeWith`) | ages, posture, queue head, sleeping, relationship, greeted state, celebrity, drink in hand, both Sims' lot and outside flags | parent walk, `Objects_GetPosition` + `World_IsPositionOutsideImpl` ×2, `Slots_GetContainedObject` (PROXY); `ActionData` procedural tests not audited | P |
| 2 | `Chat` (toddler) | target age, learn-to-talk skill | none | M |
| 3 | `Tickle` | target age | none | M |
| 4 | `PickUpChild` | ages, postures, carry posture | none | M |
| 5 | `Sim.WakeUpSim` | species, sleeping, posture container, cooldown time | none | M |
| 6 | `Sim.GiveGift` | postures, greeted state (household, situation, relationship, lots) | parent walk | H |
| 7 | `Sim.SocializeWith` | conversation, postures, then `CanSocializeWith` as #1 | as #1 | P |
| 8 | `Sit` | sittable cast; autonomous returns before the posture checks | none | M |
| 9 | `ViewObjects` (paintings, sculptures, decor) | viewable, parent, drag model, beach lot | `World_IsInPoolImpl`, position; beach: terrain height, sea level (PROXY) | P |
| 10 | `Fridge_Prepare` (party food) | party host situation, `base.Test` | `Query_CountObjects` (PROXY) | P |
| 11 | `Fridge_EatLeftovers` | robot, hunger buffs, home lot, butler, fridge inventory (`FindAll` with in-use check, allocates) | parent walk | H |
| 12 | `Fridge_GetRandomSnack` (have snack) | none | none | M |
| 13 | `Eat` (served food) | in use, empty, recipe, vampire food, parent (bake sale), ghost grave lookup | parent walk | H |
| 14 | `CleanUp` (dishes) | slob trait, serviceable (active household, real estate, room, service job), in use, parent | parent walk | H |
| 15 | `Toilet.UseToilet` | none | none | M |
| 16 | `Shower.TakeShower` | hygiene motive, robot, broken; lazy tooltip | none | M |
| 17 | `Sink.WashHands` | trait, broken | none | M |
| 18 | `Sink.BrushTeeth` | broken | none | M |
| 19 | `Bathtub.TakeBath` | hygiene motive and desire curve, broken, cat in tub (`ActorsUsingMe`) | parent walk | H |
| 20 | `BedSleep` | mummy, bed usable, electronics in the room (`GetObjects<IDisturbingElectronicObject>`, allocates a list and a dictionary per Test) | `Query_GetObjects` (PROXY) | P |
| 21 | `BedRelax` | bed usable | none | M |
| 22 | `TV.WatchTVAutonomously` | trait, broken, workout, `ActorsUsingMe`, Sims asleep in the room (lot room cache) | parent walk; `Query_GetObjects` only on a cache miss | H |
| 23 | `Computer.PlayComputerGames` | laptop/greeted/venue, broken, chair at the desk (surface slots, in-front test) | slots and position (PROXY) | P |
| 24 | `Computer.ChatRandom` | same helper as #23 | as #23 | P |
| 25 | `Bookshelf_ReadSomething` | shelf inventory | none | M |
| 26 | `Stereo.DanceSolo` | bonehilda, community stereo on, broken, dancer situations, installed packs | parent walk; `GameUtils_GetProductFlags` (SAFE) | H |
| 27 | `Mailbox.GetMail` | home lot vs mailbox lot field, flag, virtual unit | none | M |
| 28 | `TrashcanIndoor.EmptyTrash` | serviceable, broken, neat trait, fill level | parent walk | H |
| 29 | `GoHome` (meta) | home lot, resort, autonomy restriction level, buffs, lot hunger commodity, situations, curfew | parent walk | H |
| 30 | `VisitLot` (meta) | greeted, restriction level, last visit time, houseboat, lot type, owner, callbox (`GetObjects<Callbox>`) | `Query_GetObjects` (PROXY) | P |

**Random set (30)** (seed 59; by class):

| Class | Definitions |
|---|---|
| M (17) | `CityHall.TriggerCelebration`, `Sim.CatDogInvestigate` (true), `BrainEnhancingMachineCollege.UpgradeToLevelThree`, `Phone.CallSellVacationHomeLot` (world name: constant), `OccultFairy.PickNewWings`, `DrillHole.EscapeFromDrillHole` (true), `TreasureChestEP10.SetRequiredSkillLevel` (true), `Treehouse.WooHoo` (true), `Stand`, `FindJobNewspaper`, `Sim.SaluteFlag`, `Daycare.HelpDaycareChildWithHomework`, `MagicGnomeBase.GrabAngryGnome`, `Sim.BecomePlantSim` (true), `Sim.TurnToFaceAndAskToLeave` (true), `MoveHouseholdIn`, `Sim.CallOverToDance` |
| H (5) | `ResortFrontDesk.BaseFrontDeskSocialA`, `TimePortal.RemoveRainbowDew`, `ShoppingRegister.Browse`, `DiveWell.SetAsWarpWell`, `RabbitHole.RenameRabbitHole` |
| P (5) | `TV.PlayVideoGameSystemOnTVOnline` (closest object, radius), `LightGameObject.FrugalTurnOff` (`World.LightGetDimmer`), `FrontDeskProxyInteractions` (`GetObjects`), `DanceOnCounterOrTable` (`World.GetObjects` at a location, space above), `ClothingPileWet.DryClothesOnClothesline` (`Queries.CountObjects`, closest clothesline) |
| W (3) | `PokerTable.PlayPoker` (random chair pick via `RandomUtil.GetInt`, eager localized tooltip), `BotChargingStation.Recharge` (writes the definition's `Owner` field), `FVShoppingRegister.Buy` (adds an exit reason to the actor, which raises `OnExitReasonsAdded`) |
| U, Y | none |

**Class counts**

| Class | Common 30 | Random 30 | Sample 60 | Population scan, 4,342 [inferred] |
|---|---|---|---|---|
| M pure-read, managed | 13 | 17 | 30 (50%) | 2,972 (68%), of which 1,036 are `return true` |
| H pure-read + hot ICs | 9 | 5 | 14 (23%) | 849 (20%) |
| P pure-read + other PROXY | 8 | 5 | 13 (22%) | 277 (6%) |
| W writes | 0 | 3 | 3 (5%) | 241 (6%; overcounts, includes 5 random-only) |
| U reads UNSAFE IC | 0 | 0 | 0 | 2 (`PlantObjectHere`, `Jetpack.FlyAround`: create objects; found by IC name search, already counted in the rows above) |
| Y yields | 0 | 0 | 0 | 0 |
| **pure-read (M+H+P)** | **30** | **27** | **57 (95%)** | ~4,100 (94%) |

Reading the split [inferred]:
- The scan's P share is lower than the sample's because most query and slot calls sit in cross-file helpers (`IsComputerUsable`, `IsRoomBeingDisturbedByElectronics`, `CommonVisitTest`) that the one-level scan does not follow. The sample is the better estimate for P: about one definition in five.
- The common set is heavier in P and H than the random set: the household definitions Sims actually score are the ones that look at the room, the lot and other Sims. A per-search estimate should weight by pairs scored, which favours socials (P) and object definitions (H) over the many `return true` definitions.
- No sampled or scanned `Test` yields or sends an event. The unsafe share is small but real and cannot be found by sampling alone: a worker needs an allow-list (docs/62 §4), not a deny-list.

### 2.5 What a snapshot must contain to cover the pure-read share

Two kinds of state, with different rules [inferred from §2.2-2.4 and docs/62 §3-4]:

1. **IC-backed values: copy them.** Every PROXY IC the pure-read definitions reach becomes a field, filled on the sim thread before the worker starts (a per-call sync proxy costs up to a sim pass, docs/62 §3).
2. **Managed state: copy what is enumerated, revalidate what is scalar.** `Test` bodies read live managed objects directly (`target.Repairable.Broken`, `a.BuffManager`), so a full copy is not possible without rewriting each definition. Scalar reads that race with the sim thread can only give a stale answer. Collections the bodies enumerate (buff lists, `ActorsUsingMe`, inventories, situation lists, relationship tables, lot object caches, commodity map lists) can throw or misread while the sim thread changes them (Mono 1.2 `List`/`Dictionary` have no concurrent-reader guarantee). So: copy the enumerated collections or hold them still, and have the sim thread re-run `Test` (C3) on the picked candidate, and the next ones if it fails, before D2-D5. Staleness then changes only the ranking, never what runs.

Snapshot content (per search; one actor, the lot it scores, the meta lots):

| Scope | Fields | Covers | Size [inferred] |
|---|---|---|---|
| Per object on the scored lot (every target in `lot.Map`, the actor's inventory map and every Sim map on the lot) | id; position (12 B); parent id and root parent; lot id; room id; level; flag word: in world, in inventory, charred, blocked, interactions disabled, destroyed, **outside**, **on roof**, **in pool**, hidden, public room, fogged room, broken, in use; `ActorsUsingMe` ids; blocking score; owner lot | ranks 1-5, `World_IsInPoolImpl`, hidden flags; H class and most of C3-C8 | ~64 B + 8 B per user; 300-800 objects on a furnished home lot → 20-60 KB |
| Per lot scored | id, type flags, household id, fire state, public room list, room table (per room: allowed for this actor, Sims in room, sleeping Sims), **per-type query results** used by allow-listed Tests (counts and id lists: callbox, prepared food, disturbing electronics, surfaces/chairs, clotheslines, front desks), a frozen copy of the pair lists the search walks | `Query_*`, `IsAllowedInRoom`, `GetObjectsInRoom`, B9/B11 walk; P class queries | ~1-4 KB + 4 B per pair (pair counts not measured; 1-5k pairs → 4-20 KB) |
| Per actor | motives and desire values, use values (own table), mood, buff and trait id lists, skill levels, career, occult, identity flags, household, lot home, posture container and kind, queue head (definition type, target, priority), conversation, greeted lots, snub list, inventory item types, situations, times; the 9 C0 flags | C4, C8-C11, actor-side Test reads | ~2-4 KB |
| Per other Sim on the lot (social targets) | identity flags, sleeping, posture kind, queue head kind and priority, social flags, greeted state, lot and outside flags, celebrity level, right-hand object kind, in-car flag, relationship row to the actor | socials (P), C8 loner/babysitter/butler multipliers, Sim-target Tests | ~0.3 KB per Sim |
| Per meta search | public meta objects and known lots (ids, positions), meta ads per object, bubble-up commodity maps (frozen), home/career meta pairs, `sNumSimsAtHomeOrGoingHome` computed on the sim thread | B10, B13-B16 | ~10-50 B per meta object, a few hundred meta objects → under 20 KB |
| World, once per search | sim clock ticks, sunrise/sunset, weather and snow level, **per outdoor target shelter flag only while it rains, hails or snows**, world type and name, active household and selected Sim, autonomy restriction levels, build-buy state, dragged object, RNG seed | §2.2 E; docs/62 ranks 10-15 | under 1 KB + 1 bit per outdoor target |
| Tuning | none: shared read-only | §2.2 F | 0 |

Total: about **30-100 KB per local search** on a busy home lot, plus up to 20 KB with meta [inferred]. The sim-thread cost is about 4-5 native calls per object (docs/62 §3) plus the per-type queries, once per search instead of several calls per pair.

Not covered, so these definitions stay on the sim thread (the allow-list rule):
- W and U: definitions that write the actor, a definition field, the RNG, or create objects (3 of 60 sampled; `PlantObjectHere` and `Jetpack.FlyAround` by scan).
- Bodies whose cross-file helpers were not read: the allow-list must be built from a full managed call graph per definition (docs/62 §6), not from this sample. The sample says the payoff is large: about 95% of definitions are pure-read, and with the snapshot above the H share (~23%) and the M share (~50%) need nothing else.
- Failing tooltips: 494 bodies build a localized tooltip string on a failing branch, some on autonomous calls (`LocalizedStringService_*`, PROXY, low confidence). Options: a per-definition check that the branch is non-autonomous, or a worker-side stub that returns an empty tooltip. Autonomous tooltips are never shown (§1.2 C7), so the stub would not change behaviour.
- Rain and hail: C8 builds an `InteractionInstance` per outdoor pair; the snapshot shelter flag replaces it, which means a worker variant of C8 rather than the game's method.
- Tuning scoring functions, `Check.Test` (C11), `ActionData` procedural social tests and `IOverridesCalculateScore` (22 classes) are not sampled here; they need the same audit before socials can leave the sim thread.

## 3. Snapshot design and go/no-go for scoring off the sim thread (T-AUT-c)

Static design from §1-2, docs/28, docs/30, docs/56-58, docs/61 and docs/62. No game run, no Ghidra. Every number below that is not tagged [measured] is an estimate to be replaced by the counters named in §3.10. docs/63 (`tools/emu/`) and docs/64 are not in the tree at the time of writing, so the confirmation plan uses live probes, Verify counters and repros.

### 3.1 Verdict

| Stage | What | Verdict | Expected effect | Gate to the next stage |
|---|---|---|---|---|
| A | In-place caches, no second thread (§3.7) | **Go** | 5-20 ms/s of sim-thread time across all scoring callers [inferred] | in-load `--ab` per switch shows a lower S2 share and no behaviour change |
| B | Snapshot built and scoring re-run against it on the sim thread (shadow mode, no threads) | **Go with conditions**: Verify-only, sampled, default off; needs the native snapshot builder, IC shims and allow-list v0 | none (costs time while on); it produces the real break-even inputs | 0 mismatches and 0 snapshot misses over ≥10k shadow searches on BP, SV and IP; measured snapshot cost and allow-listed share pass the §3.3 inequality |
| C | Worker scores a small allow-list behind a switch | **Go with conditions**, not before `[MonoThread]` steps 1-5 pass in game and B passes. Today: **no-go** | -25 to +2 ms/s; break-even at best [inferred]. Acceptable under the offload goal only if stable and within ~2% | 0 crashes, 0 Mono Verify hits, apply drop rate < 5%, pick distribution unchanged, net sim-thread cost within ~2% |
| D | Widen the allow-list (socials, meta, P-class queries, `Check.Test`, scoring overrides) | **No-go for now**; revisit with C data | 0 to +26 ms/s at f ≈ 0.85 [inferred] | per batch, as C |

Overall [inferred]: scoring off the sim thread is feasible only as **snapshot -> pure scoring of audited definitions -> live re-test and re-score on the sim thread**. The worker-eligible prize is the manager task's scoring (~44 ms/s), not the whole 110 ms/s of S2, because the side entry points (§1.2 E) need their answer inside the caller's task. Fixed costs (`[MonoThread]` overhead, apply step, snapshot) eat most of a small allow-list's gain, so C is an offload step for stability and later reuse, not a speed win. Stage A is the only stage with a clear speed gain.

### 3.2 Worker flow

One search in flight, pipelined through the manager task, so throughput stays one search per tick (§1.1) [design, inferred]:

| Step | Thread | Work | Notes |
|---|---|---|---|
| 1 | sim | `AutonomyManager.Simulate` pops Sim k. Low-LOD `Update` (B3, sends 2 events), B4 prologue (`ClearScoring`, insane-trait shortcut, conversation socials, exit balloon), C0 cached flags, B8 LOD decision | unchanged game code; events stay here (§3.6) |
| 2 | sim | Snapshot: freeze the pair lists B9 would walk (lot map, own map, inventory map; other Sims' maps are not frozen in C), rows for every target object and Sim (§3.4), world constants, generation = tick number | native builder (§3.3: a managed builder never breaks even) |
| 3 | sim | Hand-off at the end of the script work of the iteration: post the job; `InAutonomyManagerQueue` stays true until apply | the worker runs during the native tail of the iteration (animation, locomotion, publish, waits), when no script runs on the sim thread [inferred, §3.5] |
| 4 | worker | Our own walk (ts3fixes `AutWorker`, a copy of the B11/B12/C1 loop): for every commodity with use value > 0 and every pair: allow-listed definition type → game `TestAndCalculateScore` (C2-C11) with IC shims answering from the snapshot; else record the pair index as a "sim-thread pair". Keeps a private top-10 (pair index, score). Never yields, never calls `Simulator.*`, never pushes | rain, hail and deep-snow searches are not sent to the worker in C (the C8 shelter path builds an `InteractionInstance`) |
| 5 | sim | Next iteration, before popping Sim k+1: apply. Drop the result on any §3.5 rule. Else: live `Test` + score of every sim-thread pair into the real `BestScores`; live re-test (C3) and re-score (C2) of the worker's top-10 into the same heap; then the meta phase (B13, yields as today) and D2-D5 as today | every candidate D2 sees is tested and scored on live state; staleness can only change which allow-listed pairs reached the top-10 |
| 6 | sim | Fallback: no result by the apply deadline, worker exception, snapshot miss, shim refusal → run the whole search on the sim thread as today (§3.8) | counted |

Socials (B6, other Sims' maps), meta (B13-B16), `IOverridesCalculateScore` definitions and every non-allow-listed definition stay on the sim thread in C. The side entry points (§1.2 E) are not touched.

### 3.3 Snapshot cost against the scoring it removes (break-even)

Inputs:

| Input | Value | Source |
|---|---|---|
| S2 autonomy scoring, all callers | 11.1% of managed samples ≈ 110 ms/s | docs/28 S2 [measured share; 1% ≈ 10 ms/s is inferred from the 5 Hz sampler] |
| Manager task (`AutonomyManager.Simulate`) | 5.5% ≈ 55 ms/s | docs/28 S2 [measured share] |
| Sim iterations | 30.4 (BP) / 35.3 (IP) it/s | docs/28 G12 [measured] |
| Manager searches | ≤ 1 per iteration (`kNumSimsCanRunAutonomy = 1`) | §1.1 [verified] |
| Manager search | ≥ ~1.6-1.8 ms each if every iteration searches; more per search if not | [inferred] |
| Scoring share of the manager task (B9-B14, C; the rest is B3, D, A) | ~80% → **T = ~44 ms/s**, ~1.3-1.5 ms per search | [inferred] |
| Objects per scored lot | 300-800 | §2.5 [inferred] |
| Object id lookup | 8-15 hash entries walked per lookup | docs/62 §5 [measured] |
| Snapshot row, native builder (5 lookups: position, parent, outside, roof, destroyed/hidden) | ~1-1.5 µs | [inferred] |
| Snapshot row, managed builder (5 SimIFace wrapper chains, interpreted) | ~5-15 µs | [inferred] |
| Apply (re-test and re-score 10 pairs live) | 0.1-0.3 ms per search → 3-10 ms/s | [inferred; pairs per search not measured] |
| `[MonoThread]` overhead while installed and the worker runs (P3 gate + lock 0.6-2.0%, P4 0.1-0.5%) | ~7-25 ms/s | docs/30 §8 [inferred] |

Snapshot cost per variant (30 searches/s):

| Variant | Per search | ms/s |
|---|---|---|
| Managed builder, all objects | 1.5-12 ms | 45-360: **never breaks even** (costs more than the search) |
| Native builder, all objects each search | 0.3-1.2 ms | 9-36 |
| Native builder, incremental: Sims + dirty objects (20-60 rows) + pair-list freeze (4-20 KB copy) | 0.05-0.1 ms | 1.5-3 |

Break-even: the worker pays off when **f × T > C_snap + C_apply + C_mt**, where f is the share of scoring time spent in allow-listed pairs (the sim thread still tests and scores the other 1 - f at apply). So **f_min = (C_snap + C_apply + C_mt) / T**:

| Snapshot | Best case (low costs) | Worst case (high costs) |
|---|---|---|
| Native incremental | (1.5 + 3 + 7) / 44 = **0.26** | (3 + 10 + 25) / 44 = **0.86** |
| Native full | (9 + 3 + 7) / 44 = 0.43 | (36 + 10 + 25) / 44 = 1.6 (never) |

Reading [inferred]:
- Only an incremental native snapshot can pay off. Dirty-row tracking needs a hook on the native transform and parent setters, which are not mapped yet (Ghidra item). Until then: full native rebuild for B (correctness), and round-robin refresh of static objects in C (a stale position only changes ranking; apply re-tests).
- C at f ≈ 0.3 (M and H definitions only, no socials) gives ~13 ms/s against 11.5-38 ms/s of cost: -25 to +2 ms/s. D at f ≈ 0.85 gives ~37 ms/s: about 0 to +26 ms/s.
- The largest fixed cost is the `[MonoThread]` overhead, which any other worker load shares (S11 story progression, S4 motive math, docs/28). It amortises only if more than one load uses the worker.
- Stage A lowers T. If A removes 20% of scoring, T drops to ~35 ms/s and f_min rises by a quarter. A and C compete for the same time; A goes first because it is cheaper and needs no thread.

### 3.4 Snapshot content and IC shims

Because the worker runs while no script runs on the sim thread (§3.5), it reads managed state (motives, buffs, tuning, queues, lists) in place. The snapshot then needs only the values behind PROXY ICs (§2.5 kind 1); the managed copies of §2.5 kind 2 are not needed [inferred]. If the quiescent window does not hold (§3.5 measurement), kind 2 copies are needed and the cost table grows.

**IC shims** [design]: replace the native implementation pointer of each IC below in the `{fn, name}` binding table (`tools/internal-calls-map.ts`) before warm-up, so every caller reaches the shim. The shim compares the calling thread with the worker id (~1 ns on the sim thread) and on the worker answers from the snapshot; anything not in the snapshot sets the search's abort flag and returns a neutral value. This lets the game's own C2-C11 run unchanged on the worker.

| IC (docs/62 rank) | Worker answer |
|---|---|
| `Slots_GetParentIdImpl` (1), `Objects_GetPosition` (2), `World_IsPositionOutsideImpl` (3), `World_IsOnRoofImpl` (4), `Simulator_GetTaskImpl` for `HasBeenDestroyed` (5), `World_ObjectGetHiddenFlags` (16) | per-object row; `IsPositionOutside` for a position that is not an object's position → abort |
| `Query_GetObjectsOnLot` (6) | the frozen Sim id list of the lot; other types → abort in C |
| `GameUtils_GetWorldType` / `GetWorldName` (11), `World_GetSeaLevelImpl` (15) | constants per load |
| `World_GetSunsetTime` / `GetSunriseTime` (14) | per-search values |
| `LocalizedStringService_GetLocalizedStringBy*` (17) | a preallocated, rooted empty string (autonomous tooltips are never shown, C7) |
| `World_GetLotLocationImpl`, `World_HasAnyRoofOrFloorAbovePosImpl` (12, 13) | not shimmed: rain, hail and deep snow keep the search on the sim thread in C |
| `StopWatch_*` (7, 8), `World_IsObjectOnScreenImpl` (9), `Random_NextDoubleImpl` (10), `Simulator_SleepImpl`, `GetCurrentTaskImpl`, `AddObjectImpl` | not reached by the worker walk (no yield; LOD and D stay on the sim thread); any call → abort |
| any other IC | abort (Verify: count by IC name) |

Row: id, position, parent id and root parent, lot id, room id, level, flag word (in world, inventory, charred, blocked, interactions disabled, destroyed, outside, on roof, in pool, hidden), as in §2.5. Kept in native memory, not on the GC heap; the shim indexes it with its own open-addressing table, not the overloaded game hash.

### 3.5 Staleness rules and the quiescent window

**When the worker runs** [inferred, to measure]: between the end of the script work of iteration i (script tick, GC step, finalizers) and the start of the script tick of i+1, the sim thread runs native animation, locomotion, render publish and frame waits (part of the ~290 ms/s no hook times today, docs/28 G12). Managed state does not change there, so allow-listed `Test` bodies see a consistent world. Exceptions that need a guard: native-to-managed calls on the sim thread inside that window (remote-call pump `0xD81FC3` jobs, animation clip events that reach script, `mono_runtime_invoke` from host code). Guard: a detour on the sim thread's managed entry points sets the worker's abort flag and continues without waiting (with P4, both threads may interpret). The worker stops at the next pair boundary and its partial result is dropped. A race can then affect at most one `Test` call whose result is dropped anyway; an exception from it is caught (docs/30 §5 step 7).

**Result-level rules** (apply drops the whole result and runs the sim-thread search instead):
- age above 2 iterations (generation vs current tick);
- actor destroyed, out of world, or `HasBeenDestroyed`;
- actor `LotCurrent` differs from the snapshot lot, or the LOD flag flipped;
- actor now has a head interaction and the search was not an override (B4 bail rule);
- a side search ran for the same Sim since the snapshot (the `mAutonomySearchWasInterrupted` case, §1.2 E);
- game state changed: build/buy, CAS, edit town, travel, save or load started (the worker is also paused for these; a save walks the managed heap, docs/28 E4);
- tuning reload or a mod's tuning edit since the snapshot (generation counter bumped by a hook on the tuning loader and setters; Retuner-style edits, §3.6).

**Candidate-level rule:** every candidate that reaches D2 was tested and scored live at apply (§3.2 step 5). Snapshot staleness can only change which allow-listed pairs made the worker's top-10. Not covered: a pair the worker rejected on stale data is missing from this search; the Sim's next search sees it again.

### 3.6 Allow-list, mods and the event rule

**Construction** (tooling needed; the §2 name scan is not enough, §2.5):
1. Roots per definition type: `Test(actor, target, autonomous, ref tooltip)`, the parameter-level `Test`, the `base.Test` chain, the tuning scoring function (resolved by name from the tuning), `IOverridesCalculateScore.CalculateScore`; and once for all definitions, the base path C2-C11 including every `Check.Test` override reachable from lot and situation checks.
2. Transitive closure over `call`, `callvirt`, `newobj`, `ldftn`; virtual and interface calls resolved to every override in all loaded assemblies (class hierarchy analysis, conservative).
3. Reject the definition if the closure reaches: an IC with no shim (§3.4); `Simulator.*`; `EventTracker.*` or any delegate or event invoke; a `stfld`/`stsfld` on an object not allocated inside the closure (W class), definition singleton fields included; `RandomUtil`/`RandomGen`; reflection, `Type.GetType`, assembly loading; a `[ThreadStatic]` field; `GCHandle`, `WeakReference`, or `newobj` of a class with a finalizer; a static constructor of a class not on the warm-up list; a loop over a live collection with no bound the tool can see (flag for review).
4. Output: `docs/data/autonomy-allow.csv` (definition type, assembly MVID, closure size, ICs reached, verdict, first reason) and a warm-up list (every method in the accepted closures, every class with a static constructor, one call per IC signature).
5. Include ts3fixes' own managed hooks (docs/re/managed-methods.md): a hook body replaces the game body it patches, so it belongs to the closure.

Tool: try REA's static .NET graph (`project_managed_application_graph`) on the script DLLs first; if its call resolution is too coarse, a TypeScript ECMA-335 metadata and IL reader in `tools/` (no npm dependency) [effort inferred: 1-2 weeks]. Name scans are only a pre-filter.

**Mods** [community-reported behaviour, not checked in their code; NRaas is read for behaviour only, AGENTS.md]:
- NRaas and other mods add their own interaction definitions and replace some game definitions' singletons with derived types (Woohooer is the known case). The allow-list is keyed by the **runtime type and assembly MVID** of the definition object, so mod types and replaced definitions are not on it and stay on the sim thread. No mod code runs on the worker.
- Retuner-style mods edit `InteractionTuning` at run time from the sim thread. The worker reads tuning in place during the quiescent window; an edit bumps the tuning generation and drops in-flight results (§3.5).
- StoryProgression-style mods push interactions and run searches from their own tasks: those are side entry points and stay on the sim thread. A mod calling `AutonomyManager.AddReaction` (no caller in the game, §1.1) is unaffected.
- Core mods that replace game script assemblies, and run-time method patchers (MonoPatcher-style), change closures without changing type names. At enable time the native side compares the MVIDs of the game script assemblies with the allow-list file and looks for known patcher assemblies; any mismatch turns C and D off for the session and logs the reason.
- A definition that throws on the worker: drop, sim fallback; after 3 throws it leaves the allow-list for the session.

**Event rule** (docs/61 §5.3): the worker raises no `EventTracker` event and invokes no delegate (allow-list rule 3). The two `SendEvent` calls of the low-LOD `Update` (B3) run on the sim thread before the snapshot; the push (D5) and the conversation-exit balloon run on the sim thread. The only event-like write in the `Test` sample, `OnExitReasonsAdded` (§2.4, W class), is not allow-listable. Nothing in the autonomy worker needs the event request queue today; if a later stage does, it records requests in its own queue and the sim thread sends them at one fixed point.

### 3.7 Stage A: in-place savings, no second thread

ts3fixes managed hooks (the existing hook/`Orig` pattern), each behind its own switch, default off, measured one at a time with in-load `--ab`. Lesson from `kSpatialCache` (docs/20, A/B -5%): no array copy on a hit and no clearing on every object destroy.

| # | Cache (from §1.5 "not cached") | Rule | Expected gain [inferred] |
|---|---|---|---|
| A1 | Per-search IC memo: actor position and `LotCurrent`; per target: position, parent id, outside, on roof (§1.4 observation, docs/62 §3) | valid for one search; no behaviour change (the local phase never yields) | the largest item: the hot set is several ICs per pair; 10-25% of local scoring → ~5-15 ms/s over all callers |
| A2 | Negative `Test` cache: an (actor, pair) that failed `Test` in the last K ticks is skipped | only drops candidates, never runs an untested pair. Behaviour change: a pair that just became valid waits up to K ticks | depends on the failing share and the per-Sim search interval [to measure]; 0-10 ms/s |
| A3 | Lot Sim array per tick (`lot.GetObjects<Sim>()` in B9) | the same array goes to B9 only (it enumerates, never writes); dropped at the next tick | one IC and one allocation per search: < 1 ms/s |
| A4 | Meta list copy (public meta objects + known lots, B10/B13) | reuse while both source counts and a lot-add generation are unchanged | one list of a few hundred refs per meta search: < 1 ms/s, less GC |
| A5 | `CalculateNumSimsAtHomeOrGoingHome` once per tick (docs/04a #10) | compute once per tick into the same static | O(Sims) with a `LotCurrent` IC each, per meta search: ~1-3 ms/s in big towns |
| A6 | Optional: skip the eager tooltip string in the base C7 path for autonomous tests | autonomous tooltips are never shown | small; base path only, not the 494 bodies |

Total: **5-20 ms/s (0.5-2% of sim time) [inferred]**. A1 also applies to the side entry points, so it reaches the whole 110 ms/s, not only the manager share.

### 3.8 Failure modes and fallbacks

| Failure | Detection | Fallback |
|---|---|---|
| Worker misses the apply deadline (2 iterations) | generation check at apply | sim-thread search for that Sim; `autWorkerTimeouts` |
| Managed exception in a `Test` on the worker | `exc` from `0xE5A600` | drop, sim search; per-definition throw count → session deny |
| Shim refusal or snapshot miss | abort flag | drop, sim search; log the IC name or object id (Verify) |
| Native-to-managed call on the sim thread in the window | §3.5 guard | worker aborts at the next pair; result dropped |
| Worker stuck (no pair boundary within 50 ms) | watchdog on the sim thread | C and D off for the session, all searches on the sim thread. The thread is left parked, not terminated (it may hold the P3 lock). Residual risk: a hang while holding the P3 lock blocks the sim thread at its next allocation. Mitigation: allow-list rule 3 rejects unbounded loops over live collections |
| Mono warm-up gap (class init, vtable, loader, thunk, GCHandle on the worker) | docs/58 §4 Verify counters on the worker thread | Verify mode only; any non-zero count → fix the warm-up list before a stable build |
| EA IC nesting counter `[0x11F2D84]` touched from the worker | Verify snapshot of the counter around worker runs (docs/56) | open item: check whether the IC transition writes it; shims answer without calling the EA bodies |
| Worker raises an event or writes game state (allow-list error) | static only (no runtime guard for managed writes) | B shadow mode compares results, not writes; residual risk carried |
| Save, load, travel, build/buy, CAS during a search | game state hook | worker paused; in-flight result dropped |

### 3.9 Mono prerequisites by stage

| Stage | `[MonoThread]` (docs/30 §11) | Other |
|---|---|---|
| A | none | none |
| B | none (no second thread) | native snapshot builder, IC shims (sim-thread branch only), allow-list v0 from the tool, ts3fixes shadow mode |
| C | step 1 (P1 registration + P2 multi-stack root scan; built, selftest ok, not run in game) → **step 2**: P3 gated recursive lock, **17 detours** (the 12 of docs/30 + `GC_free` `0xE4BEB0`, frame arena `0xD71900`/`0xD71A20`/`0xD71B70`, `GC_add_roots` `0xE4D9F0`; docs/57 §7) plus the `0xE4B3F0` replacement and the `0xE50F90` descriptor detour (docs/56, docs/58) → **step 3**: P4 per-thread `[0x11F4170]`/`[0x11F416C]` and the `0xE534E0` replacement → step 4: worker entry (domain TLS, direct `0xE5A600` invoke, no thread attach) → step 5: Mode=0/1/2 Verify runs pass on three worlds | warm-up: B's shadow runs execute each allow-listed closure on the sim thread first, plus the tool's warm-up list (static constructors, forced method transform, one call per IC signature so `RuntimeMethod+0x10` and the thunk hash `0x11F4128` are filled on the sim thread) |
| D | as C | wider shims (per-type query results for P-class definitions, weather shelter flags); audit of socials (`ActionData`), meta, `Check.Test`, scoring overrides |

Worker rules for C and D (docs/30 §5, docs/56, docs/57, docs/58): no game IC except through a shim; no `Simulator.*`; no entry through `0xD8A8E0`; no host invoke wrappers `0xD7EED0`/`0xD7F000`/`0xD7F210`; no `GCHandle`, weak reference or finalizer class; no `[ThreadStatic]`; no reflection or class loading; no event raise; every method and IC signature warmed on the sim thread first; stop the worker before shutdown `0xD80750` and before saves.

### 3.10 Confirmation per stage

| Stage | Confirms | How |
|---|---|---|
| A | gain, no behaviour change | per-switch in-load `--ab`, one bundle run per world; counters `autSearches`, `autSearchUs`, `autPairsTested` (reserve the range in docs/registry.md first); S2 share from the managed sampler; a `tools/harness/repros.json` entry with an agent action that queues many idle Sims on one lot |
| B | snapshot completeness, shim correctness, real cost inputs | Verify counters `vfAutShadow`, `vfAutMismatch` (top-10 or score differs from the live search in the same tick), `vfAutMiss` (shim refusals by IC), `vfAutSnapUs`, `vfAutAllowShare` (f, by time). Pass: 0 mismatches and 0 misses for allow-listed pairs in ≥10k shadow searches per world, then recompute §3.3 with measured T, C_snap and C_apply. Add inference-ledger rows (docs/51) for the §3.4 and §3.5 claims |
| C | stability and net cost | in-load `--ab` on switch `autworker`: 0 crashes, docs/58 Verify counters 0, `mtMismatches` 0, apply drop rate < 5%, per-definition pick histogram worker vs sim within sampling noise, net sim-thread ms/s within ~2% (offload goal); one save per run; possible stuck Sims logged only |
| D | as C, per batch | as C |
| quiescent window | native-only time after the script work, per iteration | `stutter-profile.js` columns `animSacs`, `animPose`, `locomotion`, `publish` (built, not yet run, docs/28 G12) plus a guard-trip counter |
| native snapshot lookups | the row builder matches the IC bodies | offline differential test of the lookup functions on recorded inputs (PLAN rule (b)): under an x86 emulator if `tools/emu/` lands, else in-process against the real ICs |

Tags in this section: numbers taken from docs/28 and docs/62 §5 are [measured] where marked; all designs, costs and gains are [inferred]; the §1-2 facts it builds on are [verified].
