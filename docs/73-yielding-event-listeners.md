# 73. Event listeners that yield in their handlers (gap 6)

2026-10-16. Static analysis of the decompiled script assemblies plus the managed sampler data already on disk. No game run. Closes PLAN.md gap (6) and the docs/61 §8 open item "which event listeners yield inside their handler". Tags as in AGENTS.md: [verified] = read in the decompiled source or measured, [measured] = from sampler data, [inferred] = scanner result or reasoning not yet checked by hand or live.

## 1. Summary

- **Yielding listeners exist in vanilla play, but none sits on a high-frequency event.** Of 237 distinct listener methods, 8 paths were confirmed by reading the source (§3.2); about 90 more have an unconfirmed path through shared helpers (§3.3), several of which are scanner false positives. Every confirmed path needs a rare condition (igloo melting, a store fryer event, a cheating couple meeting, a time-traveler reward, the memory-cap warning, a maternity outfit not yet built) [verified paths, conditions read in source].
- **The events that fire most have no yielding listener.** Event dispatch is 1.07% of non-empty managed samples (162 of 15,154, 25 runs) [measured]. 80% of those samples come from senders of `kSimHeartbeat`/`kDnPSynchronizeEvent` (`Autonomy.Update`), `kUsedObect` (`Interaction.DoLoop`), the environment events (`SimUpdate`), `kGotBuff`, dish, room and object-state events. None of these ids has a statically registered listener with a confirmed yield; their consumers are data-bound wish, opportunity, challenge and life-event counters. The 2,233 reflection-bound wish functions have no typed path to a yield (0 tight, 110 through one rare relationship-creation path to a modal dialog) [inferred, scanner].
- **Yields need a script task.** `Simulator.Sleep` throws "non-yielding context" when there is no current task, and `EventTracker` silently removes a listener whose handler throws [verified, managed source]. A dispatcher that runs listeners outside a script task (a worker, or a native hook with no task) would delete yield-capable listeners without a crash.
- **Offload verdict.** Do not raise events off the sim thread (docs/61 §5.3 stands, with this extra reason). Deferring or batching on the sim thread is safe only for the class D events of §5.2, and only when the drain runs inside a script task. The gain is small: deferral moves cost, it does not remove it; coalescing the per-event wish drain is worth about 0.2% of managed samples [inferred from the sampler split]. Not worth a fix now. For autonomy (docs/59 §3.6), the two `Update` events (B3) are class D: they may be recorded and raised later at the sim-thread apply point; the worker still raises nothing.
- **docs/61 §5.1 risk is real but rare.** Because confirmed yielding listeners exist, two tasks can interleave pushes and pops on `EventTracker`'s shared active stack in vanilla play; every confirmed case is rare. Live check drafted as I-73-1.
- **docs/62 §7 correction candidate.** The managed side never asks `SACS_RequestStateImpl` to yield: `StateMachineClient.RequestState` always passes "no yield" to the IC and does its own `Simulator.Sleep(0)` wait loop when the caller wants to yield [verified, managed source]. docs/62 calls the IC itself yielding [inferred from the native scan]. Ledger I-73-4.

## 2. Method

### 2.1 Call graph

A scratch scanner (kept in the session scratch folder, not committed, like the docs/61 §7 helper) parsed the decompiled `ScriptCore`, `SimIFace`, `Sims3GameplaySystems`, `Sims3GameplayObjects`, `Sims3StoreObjects` and `UI` assemblies from `game-copy/` into 122,349 methods, with overloads split by parameter count. For every call site it resolved the target and graded the edge:

| Grade | Edge | Meaning |
|---|---|---|
| tight | static, `this`/`base`, class-qualified or constructor call, or a method name owned by one class; `SimIFace` interface calls into `ScriptCore` | the target is known |
| mid | instance call on a name owned by 2-6 classes | one of a few targets |
| loose | name owned by 7-200 classes | noise; reported for completeness only |

Rules that remove known false edges:

- Block lambdas are treated as deferred (not part of the caller) unless handed to a synchronous consumer (`ForEach`, `Find`, `Exists`, `Sort` and similar). This keeps `OneShotFunctionTask` and alarm callbacks out.
- A call that passes literal `false` to a yield-control parameter (`yield`, `allowYield`, `waitForLerpToComplete` and alike), or omits one whose default is `false`, gets no edge to that target. Example: `FadeOut` with yield off, `RequestState` with yield off, `AddObject` without its yield flag.
- Two guards were checked by hand and cut [verified]: `Autonomy.YieldIfPossible` sleeps only while the search type is `Autonomy`, which only `RunAutonomy` sets (so `FindBestActionForCommodity` from buff auto-solve never yields); `IntroTutorial.StateComplete` does nothing outside the tutorial (its step table is null).

### 2.2 Yield points (seeds)

From docs/62 §4 and §7, plus the managed wrappers: `Simulator_SleepImpl` (every `Simulator.Sleep`, including `ModalDialog`'s sleep until closed), `SACS_AcquireDriverImpl` (only on the `allowYield` path; the no-yield path calls `SACS_AcquireDriverNoYieldImpl`), `GameUtils_WaitFor*`, `GameUtils_StartupYield`, `OnlineFeaturesHost_WaitForDownload`. `SACS_RequestStateImpl` is not a seed (§1, I-73-4); `StateMachineClient.RequestState`'s own sleep loop is reached through `Simulator_SleepImpl` instead.

### 2.3 Listeners

| Source | Count |
|---|---|
| `EventTracker.AddListener` and `DelegateListener` (and its 11 subclasses) constructed with a handler | 314 registrations |
| `CompletionDelegate` assignments (run inside dispatch when a listener completes) | 3 |
| `ProcessEvent` / `DoesEventApply` overrides in the 20 `EventListener` subclasses | 19 |
| Work every dispatch does: `EventTracker.ProcessEvent` itself, `LifeEventManager.SendLifeEvent`, `DynamicChallengeManager.RunTestHarnessFunctions`, the wish drain `DreamsAndPromisesManager.Update` | 4 |
| Distinct handler methods | 237 |
| Event ids named in source / registrations whose id comes from data (wishes, opportunities, challenges, telemetry) | 138 / 54 |

Wish (Dreams and Promises) callbacks are bound by reflection from `Sims3.Gameplay.Objects.DreamsAndPromises.DreamsAndPromisesDelegateFunctions`, so all 2,233 methods of that class were graded separately; `ActiveDreamNode.RunFeedbackFunction` is the listener that calls them.

### 2.4 Frequency

All 25 `probe-out/profile/*-managed.jsonl` files (BP, IP, SV, worker, mainstack, S3PP cache and seeded-RNG runs; 20,861 samples, 15,154 with a managed stack). For each sample with `EventTracker.SendEvent` on the stack, the sender is the frame just outside the outermost `SendEvent`, the handler the frame just inside `EventTracker.ProcessEvent`. The event id per sender comes from the sender's source.

### 2.5 Limits

Virtual and interface dispatch is resolved by name only; argument values other than literal `false` are not tracked; delegate fields invoked by name map to same-named methods (two known false-positive families in §3.3); mods are not scanned. A "no path" verdict is therefore stronger than a "mid path" verdict.

## 3. Results

### 3.1 Counts

| Grade of the best path from the handler to a yield | Handlers |
|---|---|
| tight (typed edges only) | 4 delegate handlers [inferred], all 4 confirmed by hand in §3.2 |
| mid | 87 delegate handlers + 3 dispatch steps (`EventTracker.ProcessEvent`, `LifeEventManager.SendLifeEvent`, wish drain) [inferred] |
| loose only | 89 delegate + 5 overrides (noise) |
| none | 32 delegate + 14 overrides + 2 completion + 1 dispatch step (`DynamicChallengeManager`) |

### 3.2 Confirmed yielding paths (read in source)

| # | Listener | Event | What yields | Condition | Tag |
|---|---|---|---|---|---|
| Y1 | `Igloo.OnWorldFreezingOrMeltingListener` | `kWorldFreezingOrMelting` | `GameObject.FadeOut` with yield on: `Simulator.Sleep(30)` loop for the fade time | an igloo melts down to the puddle stage (Seasons) | verified |
| Y2 | `DeepFryerVegas.OnDeepFry` (Store) | three store event ids (`287454016`/`17`/`19`) | `RouteAndCheckInUse` routes the Sim to the fryer; route failure acquires a SACS driver | every matching event; sent from the actor's own interaction | verified |
| Y3 | `RomanceVisibilityState.CheckIfCoupleNeedsToAccuseEachOther` | `kEventSimMovedToLotOfType` | betrayal, breakup, then an immediate reaction interaction run inline (`Sim.PlayReaction` runs it only when the current task is the reacting Sim's own task) | a cheating couple ends up on the same lot | verified (path and own-task guard) |
| Y4 | `RewardsManager.OccupationRewardInfo.ProcessEvent` | opportunity / occupation completion ids | `SocialComponent.FindSimOfRelationshipType`: `Sleep(1)` loop, up to 60 sim minutes, waiting for a time traveler to spawn | time-traveler reward type only | verified |
| Y5 | `LifeEventManager.SendLifeEvent` (runs inside every dispatch) | all | one-time "too many memories" warning: `SimpleMessageDialog`, whose `ModalDialog` sleeps the calling task until closed | once per Sim, when the visible memories reach the cap | verified |
| Y6 | 22 handlers that show a Sim notification (`ShowTNSIfSelectable`, e.g. career, celebrity, time-statue records, `DreamsAndPromisesManager.ObjectDestroyedDelegate`) | various | the notification thumbnail asks for the everyday outfit; for a pregnant Sim with no maternity outfit yet it builds one with yield allowed (`OutfitUtils.SetOutfit`) | pregnant selectable Sim, first maternity outfit of that category | verified path; which of the 22 can hit it [inferred] |
| Y7 | relationship creation from a listener (wish checks, `VisitSituation.RelationshipChanged`, `AutographSessionSituation.OnConversationLeft` and others) | various | a new `Relationship` initialises its short-term context; an amorous context for a played Sim in a non-romantic group asks the player with `AcceptCancelDialog` (modal) | played Sims in a group situation, romance not yet accepted | verified path; reachable from a new relationship [inferred] |
| Y8 | `HudModel.OnSelectedSimChanged` | `kEventSimSelected` | `PortableMp3Player.StopMusic`: `Sleep(1)` loop until the audio callback | portable music playing when the selected Sim changes | verified `StopMusic`; call on this path [inferred] |

Y2 and Y3 run the actor's own work from inside its own event: the event is sent by the actor's interaction, the listener routes or animates the same Sim on the same task. That pattern is what breaks if the dispatch moves to another task (§5.2).

### 3.3 Unconfirmed paths (mid)

| Family | Handlers | Status |
|---|---|---|
| `Situation.Exit` then a situation's exit callback | 31 | false positive for the path the scanner found (the callback is a delegate field, matched by name to `PlayPretend.OnExit`); other exit paths not checked [inferred] |
| modal dialogs (`ModalDialog.StartModal` / `Pause`) | 19 | real mechanism (Y5, Y7); per handler unconfirmed |
| maternity outfit build (Y6) | 22 | real mechanism; per handler unconfirmed |
| Sim instantiation (`AgingState.PreInstantiateSim` waits for the Sim builder) | 11, all `StoryProgression.Notifications` | false positive: the call is `ArticleTuning.Instantiate`, not `SimDescription.Instantiate` [verified] |
| other | 7 | e.g. `Opportunity.SimCreated` (CAS compositor), `DynamicChallengeManager.OnHouseholdChanged` (save) [inferred] |

The interaction-queue family that dominated the first pass (add, cancel lower-priority interactions, `Gameflow`, tutorial, plumb-bob reselection) is gone once the tutorial guard and the skip-speed guard are read: `Gameflow.NotifyOfInteractionRemoval` changes speed only in skip mode for the skipped interaction, and the tutorial step table is null outside the tutorial [verified]. Adding an interaction from a listener does not yield.

## 4. Events ranked by how often they fire

Sender share of the 162 managed samples that have `EventTracker.SendEvent` on the stack [measured, all 25 runs pooled; per run 0.2-2.7% of samples, median ~1.1%]. "Listeners" counts statically registered handlers for the id; data-bound wish, opportunity and challenge listeners come on top and are graded in §2.3.

| # | Sender (samples) | Event ids | Listeners (graded) | Class |
|---|---|---|---|---|
| 1 | `Autonomy.Update` (49) | `kSimHeartbeat`, `kDnPSynchronizeEvent` (2 per Sim per update) | none static; data-bound only | D |
| 2 | `Interaction.DoLoop` (40) | `kUsedObect` (per Sim per loop notify interval) | none static | D |
| 3 | `SimUpdate.SendSimEnvironmentUpdateEvents` / `SendObjectEnvironmentUpdateEvents` (11) | `kSawSim` (two per pair), `kSawDirtyObject`, `kSawBrokenObject`, `kSawAGhost`, `kChangedInsideOutsideStatus`, `kHasNegativeEnvironmentScore`, `kVampireInSun` | 1 (`HealthManager.OnSimInsideOutside`, mid via a buff add, false-positive family) | D |
| 4 | `BuffManager.AddBuff` (9) | `kGotBuff` | 1 (none) | D |
| 5 | `GameObject.Dispose` (8) | `kEventObjectDisposed` | 12 (4 mid: notification family, TV off) | S |
| 6 | `ServingContainer.SendEventIfNeeded` (6) | `kSawEmptyOrDirtyDishes` | none static | D |
| 7 | `Sim.InternalOnSetLotCurrent` (6) | `kEventSimMovedToLot`, `kEventSimMovedToLotOfType`, `kSimEnterSpecificLot` | 15 (Y3; 11 mid: situations, punishment, burglar, dating) | S |
| 8 | `Sim.RoomChanged` (5) | `kRoomChanged` | 3 (1 mid, deep-snow effect) | D [inferred] |
| 9 | `GameObject.ObjectStateChanged` (5) | `kObjectStateChanged` | 1 (loose) | D |
| 10 | `DreamsAndPromisesManager.RetestNodesAlarmCallback` (4) | `kDnPRetestVisibleNodes` | data-bound | D |
| 11 | `SocialInteractionA.Run` (3) | `kSocialInteraction` and social outcome ids | 5 (3 mid: imminent romance/nemesis buffs, trait listener) | S |
| - | 13 more senders with 1-2 samples each (posture idle, band play, sleep flag, funds, rabbit hole exit) | various | - | per id |

Where the time goes inside dispatch (handler frame): wish feedback `ActiveDreamNode.RunFeedbackFunction` 48 samples (30%), the wish drain `DreamsAndPromisesManager.Update` that runs after every event 33 (20%), `ProcessEvent`'s own lookups, list walks and applicability tests ~50, `LifeEventManager.SendLifeEvent` 9; delegate listeners together ~20 [measured]. docs/28 S11 (0.9%) and docs/re/managed-methods (4.6% inclusive on one BP run) agree on the order of magnitude.

Rare events with confirmed or likely yielding listeners: `kWorldFreezingOrMelting`, store fryer ids, `kEventSimSelected`, `kSimAgeTransition`, `kSimDied`, `kSeasonTransition`, `kMoonRise`, `kLeftConversation`, `kSimWokeUp` / `kSimFellAsleep`, `kAteMeal`, opportunity and occupation completion. None appears as a sender in the samples.

## 5. What this means for offloading

### 5.1 Raising events on another thread: no

docs/61 §5.3 already rules it out (plain collections, shared active stack, silent listener removal, one-stack GC). New reason: a listener that yields needs a script task. With no current task `Simulator.Sleep` throws "non-yielding context" (`CheckYieldingContext`), and `EventTracker` removes any listener whose handler throws [verified]. On a worker, Y1-Y8 would not stall: they would vanish (igloo never melts, fryer never routes, reward never given, wish or opportunity listener gone) with no crash to notice.

### 5.2 Deferring or batching on the sim thread

Class **D** (deferrable): `kSimHeartbeat`, `kDnPSynchronizeEvent`, `kUsedObect`, the environment ids, `kSawEmptyOrDirtyDishes`, `kGotBuff`, `kObjectStateChanged`, `kDnPRetestVisibleNodes`, `kRoomChanged` [inferred]. No statically registered listener has a confirmed yield, and the data-bound consumers only count, compare and update wish/opportunity/challenge state. Conditions for a deferral:

1. Drain inside a script task (a `OneShotFunctionTask` or a long-lived `Threaded` object), so the rare modal-dialog path (Y5, Y7) still has a task to sleep, and the dialog still appears.
2. Keep per-Sim order; `kSimHeartbeat` before `kDnPSynchronizeEvent` as `Autonomy.Update` sends them.
3. Senders must not read the returned `Event` (none do, docs/61 §5.1) and must not depend on the wish drain having run before they continue.

Class **S** (stay synchronous on the sender's task): `kEventObjectDisposed` (listeners read the object being disposed), the lot-change ids, `kSocialInteraction` and social outcomes, `kLeftConversation`, `kEventSimSelected`, `kWorldFreezingOrMelting`, the store fryer ids, opportunity / occupation completion, age, death, season, moon, sleep and meal events. Reasons: confirmed yields (Y1-Y4, Y8); a yield on a drain task suspends the drain, not the actor; `Sim.PlayReaction`'s own-task guard takes the other branch on a foreign task (no inline reaction); routing or animating a Sim from a task that is not its own is the stuck-Sim pattern.

Gain: deferral moves work to a later point of the same thread. Only coalescing saves time: running the wish drain once per batch instead of after every event (20% of dispatch samples, ~0.2% of all managed samples) and dropping duplicate `kSimHeartbeat`/`kDnPSynchronizeEvent` within one tick. Ceiling ~0.2-0.4% of managed sim samples [inferred]. Below the noise of one run; parked.

### 5.3 Autonomy (docs/59 §3.6)

The rule stands: the worker raises no `EventTracker` event and invokes no delegate. The two `SendEvent` calls of the low-LOD `Update` (B3) are class D, so an implementation may move them from "before the snapshot" to the sim-thread apply step, raised there inside the autonomy task (which already is a script task). Nothing in `Test` bodies sends events (docs/59 §2.3). This removes the "listener code is arbitrary script" caveat of docs/59 B3 for those two ids: their listeners are data-bound counters with no confirmed yield [inferred].

## 6. Ledger rows (docs/51 §9)

I-73-1 to I-73-6, drafted, not run. Probe sketch for I-73-1 to I-73-3: a `[Verify]` hook on `Simulator_SleepImpl 0x76CB00` and `SACS_AcquireDriverImpl 0x768410` that, on the sim thread and at most a few times per second, takes the managed stack the way the `managedstack` sampler does and counts stacks with `EventTracker.ProcessEvent` on them, keyed by the frame inside it; plus a per-event-id `SendEvent` counter in ts3fixes (counter slot to reserve in `docs/registry.md`). Repro actions to add per AGENTS.md: melt an igloo (Seasons, force `kWorldFreezingOrMelting`), switch the selected Sim while portable music plays, push a fryer interaction (Store), fill a Sim's memories to the cap.

## 7. Open items

- Per-handler confirmation of the 19 modal-dialog and 22 maternity-outfit handlers (§3.3); only matters if a class S event is ever reconsidered.
- Mods: NRaas and other script mods add listeners (one NRaas handler shows in the samples); a deferral switch must treat unknown listeners as class S.
- Data-bound listener ids: which wish primitives listen to `kSimHeartbeat` and `kDnPSynchronizeEvent` was not enumerated (ids come from the wish XML).
