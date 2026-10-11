# 04a. Script layer: where in-game stutter comes from

Scope: the C# gameplay scripts that TS3W.exe runs on its embedded Mono runtime, decompiled with ILSpy into `game-copy/decompiled/<Assembly>/` (git-ignored). The priority is in-game stutter, not load times. All paths below are relative to `game-copy/decompiled/`. Only type and method names and a few short quotes are reproduced here; the EA code itself stays out of the repo.

Tags used:
- **[verified-in-code]**: read directly in the decompiled source at the path given.
- **[inferred]**: a conclusion from the code that still has to be confirmed in Ghidra or by measurement (x32dbg/Frida, tests B to D in `02-baseline-install-guide.md`).
- **[community-reported]**: known from mods or forums, not checked here.

Time units used throughout: the script clock runs at 37.5 simulator ticks per sim minute (`SimClock.kSimulatorTicksPerSimMinute`, `Sims3GameplaySystems/Sims3.Gameplay.Utilities/SimClock.cs`), and `Sims3.SimIFace.Simulator.kRealSecondsPerSimulatorTick = 1/30` (`SimIFace/Sims3.SimIFace/Simulator.cs`). At speed 1 one sim minute is therefore about 1.25 real seconds [verified-in-code for the constants; the real-time mapping at speeds 2 and 3 is inferred].

---

## 1. Simulation scheduling

### What a script task is
- Every scripted thing the simulator runs is an `ITask` (`SimIFace/Sims3.SimIFace/ITask.cs`): `Simulate()`, `Stop()`, `ObjectId`, and an `ExecuteType`. The enum `ScriptExecuteType` (`SimIFace/Sims3.SimIFace/ScriptExecuteType.cs`) has `InitFailed, None, OneShot, Threaded, Task` [verified-in-code].
- Game objects are not tasks themselves. `ScriptCore.Simulator.AddObject` (`ScriptCore/ScriptCore/Simulator.cs`) wraps any `IScriptLogic` in a `ScriptCore.ScriptProxy` and hands it to native code through `Simulator_AddObjectImpl`; a raw `ITask` is handed over directly [verified-in-code].
- `ScriptProxy.Simulate()` (`ScriptCore/ScriptCore/ScriptProxy.cs`) calls the object's own `Simulate()` and catches `ResetException` (calls `OnReset`). Any other exception goes to `ScriptErrorWindow.DisplayScriptError`, which in retail simply returns `false` (`SimIFace/Sims3.SimIFace/ScriptErrorWindow.cs`), so the proxy runs `OnReset` and rethrows into native code. There is no tight script-side retry loop on exceptions [verified-in-code].
- Long-lived objects return `ScriptExecuteType.Threaded` from `Init` and then sit in an endless `while (true) { ...; Simulator.Sleep(n); }` loop. The ones in the gameplay assemblies are `Sim`, `Lot`, `AutonomyManager`, `Services`, `SeasonsManager`, `ScriptTask` (base of `SimUpdate` and others), `OneShotFunction`, `MinorPet`, `MarineLifeBase`, `BirdCage`, `HoloDiscManager` [verified-in-code, grep for `return ScriptExecuteType.Threaded`].

### How tasks yield
- `Sims3.SimIFace.Simulator.Sleep(uint ticks)` calls `CheckYieldingContext()` (throws "Attempting to yield in a non-yielding context" when there is no current task) and then `ScriptCore.Simulator.Sleep`, which is a straight call to the native `Simulator_SleepImpl(simTicks)` [verified-in-code].
- `Sleep(0)` means "give up the rest of this tick and resume next tick"; `Sleep(uint.MaxValue)` parks a task until `Simulator.Wake` (`Simulator_WakeImpl`) [verified-in-code for usage; semantics inferred from usage such as `FollowRouteAction`, `ScriptProxy.RestoreSavedTaskContext`].
- The script side never switches stacks itself. A sleeping task resumes in the middle of its C# method, and save games can persist a task's paused state (`ScriptCore/ScriptCore/TaskControl.cs`: `TaskControl_GetTaskContext`, `TaskControl_SaveNativeYieldObject`, `TaskControl_CanSaveYieldingCall`). So the native side keeps one saved Mono execution context per sleeping task: cooperative microthreads (fibers or copied stacks) inside one OS thread [inferred: the context save/restore API proves it is cooperative; whether it is Win32 fibers or stack copying must be found in Ghidra under `Simulator_SleepImpl`].
- `Simulator.SetYieldingDisabled` / `Simulator_SetYieldingDisabledImpl` and `Simulator.EnableYieldAnalysis` exist for debugging which methods yield [verified-in-code].

### Is scripting single-threaded?
Yes, as far as the scripts can tell. No script assembly (ScriptCore, SimIFace, Sims3GameplaySystems, Sims3GameplayObjects, Sims3StoreObjects, UI, Sims3Metadata) contains `System.Threading`, `lock`, `Monitor`, `ThreadPool` or `new Thread` [verified-in-code: ripgrep for `Threading` returns nothing]. Every task runs on the one simulator thread and only gives up control at `Simulator.Sleep` or at native calls that yield (route following, animation `RequestState`) [inferred for the latter two].

### Per-tick loop and time budget
- The tick loop itself is native; scripts never see it. The tick counter `ScriptCore.Simulator.sSimTickCnt` and `SimClock.sSimTickCnt` are only ever reset to 0 in script, so native code writes them directly [verified-in-code that script never increments them; native write is inferred].
- There is **no global script time budget visible on the script side**. Each task decides its own pacing via the tick count it passes to `Sleep`. Script code that never sleeps runs to completion inside the current tick, however long it takes [verified-in-code].
- Native strings in `game-copy/bin/TS3W.exe` include "Budget violations detected", "noBudget" and "RemoveMostFrequentlyUnusedTimeSliceUSec". These are leads for a native budget around the script VM, not proof [inferred].
- A few systems throttle themselves in script:
  - `AutonomyManager.Simulate` (`Sims3GameplaySystems/Sims3.Gameplay.Autonomy/AutonomyManager.cs`) is one global task that runs **one** autonomy search per tick (`kNumSimsCanRunAutonomy = 1`) and one queued reaction, then `Sleep(0)`. It alternates between a high-LOD queue (on-screen / active lot) and a low-LOD queue [verified-in-code].
  - `SimUpdate.Simulate` (`Sims3GameplaySystems/Sims3.Gameplay.Actors/SimUpdate.cs`) lets at most 2 high-LOD sims do their full update per tick (`kMaxSimUpdates = 2`, `sUpdateLimit`) and stretches the sleep (`kSimUpdateSleepTicks = 5`) as the high-LOD count grows [verified-in-code].
  - `Services.Simulate` (`Sims3GameplaySystems/Sims3.Gameplay.Services/Services.cs`) round-robins one service every `kSimulatorSleepCount = 30` ticks [verified-in-code].
  - Meta autonomy yields every 5 venues if more than 1 ms passed (`Autonomy.ScoreInteractionsForMetaAutonomy`, `sUpdateTimer`, section 4) [verified-in-code].

### Alarms
- `AlarmManager` (`Sims3GameplaySystems/Sims3.Gameplay.Utilities/AlarmManager.cs`) keeps a binary heap (`Sims3.Collections.PriorityQueue`, `System/Sims3.Collections/PriorityQueue.cs`) keyed by tick, plus `Dictionary<AlarmHandle, List<Timer>>` and an owner index [verified-in-code].
- There is one alarm manager per lot; `AlarmManager.Global` is the world lot's. They are serviced from `Lot.Simulate` (`Sims3GameplaySystems/Sims3.Gameplay.Core/Lot.cs`), which calls `mSavedData.mAlarmManager.Simulate()` at the top of every loop iteration [verified-in-code].
- `AlarmManager.Simulate()` drains **every** alarm whose time has passed in one call, with no count or time limit. Non-yielding callbacks run inline; yielding ones become a new `AlarmOneShot` task via `Simulator.AddObject`. Removed alarms are only flagged (`Timer.Cleared`) and skipped when they reach the head of the heap [verified-in-code]. At speed 3, or when many alarms share a time (top of the hour, 1 AM, 3 AM), all of them fire in the same tick [inferred].

### How often things wake up
| Task | Sleep pattern | Source |
|---|---|---|
| `Sim.Simulate` → `Sim.DoInteraction` | After an interaction: `Sleep(0)`. Idle: `Sleep(2)` if not queued for autonomy, `Sleep(10)` while queued, `Sleep(20)` with autonomy off (`kSimLoopSleepTicksWhen*`) | `Sims3.Gameplay.Actors/Sim.cs` [verified-in-code] |
| `Sim.WaitForExitReason` (used by `Interaction.DoLoop`) | `Sleep(1)` in a loop; checks exit reasons every tick for the whole interaction | `Sims3.Gameplay.Actors/Sim.cs`, `Sims3.Gameplay.Interactions/Interaction.cs` [verified-in-code] |
| `SimUpdate.Simulate` (one per sim) | High-LOD under the per-tick limit: 5+ ticks. **Low-LOD sims and high-LOD sims over the limit: `num3` stays 0, so `Sleep(0)`, every tick** | `Sims3.Gameplay.Actors/SimUpdate.cs` [verified-in-code] |
| `Lot.Simulate` (one per lot) | Every tick if `ShouldSimulate` (active lot, world lot, the active sim's lot, **every commercial lot**) or if broadcasters with sims exist; otherwise `Sleep(30)` + `Sleep(0)` (`kInactiveLotSleepTicks = 30`) | `Sims3.Gameplay.Core/Lot.cs` [verified-in-code] |
| `SeasonsManager` | Every tick (`Sleep(0)`) | `Sims3.Gameplay.Seasons/SeasonsManager.cs` [verified-in-code] |
| `AutonomyManager` | Every tick | see above |

So the number of task resumes per tick grows roughly with (sims in world × 2) + (simulating lots). Each resume is a native context switch into Mono [inferred cost; measure in Ghidra/Frida around `Simulator_SleepImpl`].

---

## 2. Native boundary

**Correction to the brief:** the gameplay scripts do not use `[MethodImpl(MethodImplOptions.InternalCall)]`. Only `ScriptCore.Reflection` does (`Reflection_GetTypeClassification`, `Reflection_IsTypeAssignableFrom`). Everything else, about 1,930 methods, is declared as `[DllImport("Sims3Common.dll")] static extern` (or `"Sims3Common"`, plus 148 `"UTFConnector.dll"` test hooks) in `ScriptCore/ScriptCore/*.cs` [verified-in-code]. No `Sims3Common.dll` exists in `game-copy/bin/`, while TS3W.exe contains the full string `ScriptCore.Simulator::Simulator_SleepImpl` [verified for this string]. So EA's Mono resolves these P/Invokes against a name table inside TS3W.exe keyed `ScriptCore.<Class>::<Method>` [inferred mechanism]. The full list of 1,777 names already extracted is in `game-copy/analysis/internal-calls.txt`; all names below were checked against it.

### Simulation, tasks and time (`ScriptCore/ScriptCore/Simulator.cs`, `TaskControl.cs`, `ScriptProxy.cs`, `StopWatch.cs`, `ProfilerUtils.cs`, `GameUtils.cs`)
- `ScriptCore.Simulator::Simulator_SleepImpl`: the yield point. Highest priority in Ghidra; the scheduler is behind it.
- `ScriptCore.Simulator::Simulator_WakeImpl`, `Simulator_AddObjectImpl`, `Simulator_DestroyObjectImpl`, `Simulator_DisableScriptImpl`, `Simulator_CreateObjectImpl`
- `ScriptCore.Simulator::Simulator_GetCurrentTaskImpl`, `Simulator_GetTaskImpl` (used by every `ObjectGuid.ObjectFromId<T>` via `GetProxy`: one native call per lookup), `Simulator_IsInitDoneImpl`
- `ScriptCore.Simulator::Simulator_GetTickCountImpl`, `Simulator_SetYieldingDisabledImpl`, `Simulator_EnableYieldAnalysis`, `Simulator_DisableYieldAnalysis`, `Simulator_DoCPPProfile`
- `ScriptCore.Simulator::Simulator_GetUsedBytesImpl` (managed heap size; useful for GC work)
- `ScriptCore.TaskControl::TaskControl_GetTaskContext`, `TaskControl_SetTaskContext`, `TaskControl_SaveNativeYieldObject`, `TaskControl_LoadNativeYieldObject`, `TaskControl_ReleaseYieldingCall`, `TaskControl_CanSaveYieldingCall`
- `ScriptCore.ScriptProxy::ScriptProxy_SetResetState` (how `SetObjectToReset` reaches native code)
- `ScriptCore.StopWatch::StopWatch_CreateStopwatch`, `StopWatch_RestartStopwatch`, `StopWatch_GetElapsedTimeFloatStopwatch`
- `ScriptCore.ProfilerUtils::ProfilerUtils_BeginEventImpl`, `ProfilerUtils_EndEventImpl`, `ProfilerUtils_MarkerImpl` (EA's own profiler markers; may name zones in native code)
- `ScriptCore.GameUtils::GameUtils_GetGameTimeScaleImpl`, `GameUtils_SetGameTimeScaleImpl`, `GameUtils_GetGameTimeSpeedLevel`, `GameUtils_SetGameTimeSpeedLevel`, `GameUtils_GetCurrentMemoryUsage`, `GameUtils_DumpMemoryReport`

### Routing (`ScriptCore/ScriptCore/Route.cs`, `RouteManager.cs`, `RouteEvent.cs`)
- Planning: `ScriptCore.Route::Route_Plan`, `Route_PlanFromPoint`, `Route_Replan`, `Route_ReplanFromPosition`, `Route_CreateRoute`, `Route_CreateRouteWithSameGoal`, `Route_Clone`, `Route_DestroyRoute`
- Destinations and filters: `Route_AddPointDestination`, `Route_AddLotDestination`, `Route_AddRadialRangeDestination`, `Route_AddSlotArrayDestination`, `Route_SetValidRooms`, `Route_AddObjectToForbiddenPortalList`, `Route_AddObjectToIgnoreForRoute`, `Route_SetOption`
- Obstruction checks used in retry loops: `Route_GetDestinationObstruction`, `Route_GetObstructionAtPoint`, `Route_GetObstructionAtWorldOrLotPoint`, `Route_FindObstaclesForSimAlongRoute`, `Route_IsPointRoutable`, `Route_IsPointObstructedBySim`
- Following: `Route_BeginFollowingRouteAnimationSpec`, `Route_BeginFollowingRouteAnimationSpecMult`, `Route_StopFollowingRoute`, `Route_IsActive`, `Route_SetRouteActionResult`
- Manager: `ScriptCore.RouteManager::RouteManager_IsRouteToObjectUnobstructed`, `RouteManager_IsSlotUnobstructed`, `RouteManager_RoutingBlockerCheck2d`, `RouteManager_IsObjectRouting`, `RouteManager_IsOkToIdleAtCurrentLocation`, `RouteManager_MarkRoomForUpdate`, `RouteManager_BeginPortalRouting`, `RouteManager_AddPortal`, `RouteManager_RemovePortals`, `RouteManager_SetPortalLockStatus`, `RouteManager_OnExitBuildBuy`
- Events: `ScriptCore.RouteEvent::RouteEvent_CreateRouteEvent`, `RouteEvent_GetRouteEventType`

### Object lookup and queries (`ScriptCore/ScriptCore/Queries.cs`, `Objects.cs`, `World.cs`)
- `ScriptCore.Queries::Query_GetObjects` (world-wide by type; backs `Sims3.Gameplay.Queries.GetObjects<T>()`, used about 207 times in Sims3GameplaySystems), `Query_GetObjectsOnLot`, `Query_GetObjectsInRange`, `Query_GetGlobalObjects`, `Query_CountObjects`, `Query_CountObjectsOnLot`, `Query_CountObjectsInRange`, `Query_GetSimsOverlappingFootprint`, `Query_GetObjectsOverlappingObject`. Each returns a freshly allocated managed array [verified-in-code for the wrapper `Sims3GameplaySystems/Sims3.Gameplay/Queries.cs`; allocation in native code inferred].
- `ScriptCore.Objects::Objects_IsValid`, `Objects_GetPosition`, `Objects_GetLotId`, `Objects_GetRoomId`, `Objects_GetLevel`
- `ScriptCore.World::World_GetLotLocationImpl`, `World_LotSetDetailPriority` (called by every simulating lot every tick, see section 5)

### Reflection / type checks
- `ScriptCore.Reflection::Reflection_GetTypeClassification`, `Reflection_IsTypeAssignableFrom` (the only real `InternalCall`s).

---

## 3. Routing

### Layers
- `Sims3.SimIFace.Route` (`SimIFace/Sims3.SimIFace/Route.cs`) is the abstract API; `ScriptCore.Route` (`ScriptCore/ScriptCore/Route.cs`) implements it. `Route.Plan()` calls `Route.AboutToPlanCallback`, then the native `Route_Plan`, then `Route.PostPlanCallback(this, "Plan", mRoutePlanResult.ToString())`. The `ToString()` allocates a string on every plan [verified-in-code].
- `SimRoutingComponent.SimStartup` (`Sims3GameplaySystems/Sims3.Gameplay.Actors/SimRoutingComponent.cs`) hooks those callbacks:
  - `AboutToPlanRouteCallback` can call `lot.GetObjects<CommonDoor>()` on the source and destination lots and add each door to the forbidden-portal list (for not-greeted / closed-venue rules), and `GetObjects<Fire>()` on lots [verified-in-code].
  - `PostPlanRouteCallback` may call the world-wide `Queries.GetObjects<IStation>()` to consider subways for cross-lot routes [verified-in-code].
  So every plan costs the native search plus extra native queries and allocations.
- `RoutingComponent.DoRoute(Route)` (`Sims3GameplaySystems/Sims3.Gameplay.ObjectComponents/RoutingComponent.cs`) turns a planned route into `RouteAction`s and runs them. If the initial plan failed it logs "Initial Path Plan Failure (WILL NOT play route fail)" and returns false. Mid-route it can `ReplanFromPoint` (`ContinueAndPopPathAndReplan` / `...AndRestart`). Safety caps: more than 30 queued route actions aborts the route; more than 10 boat entries in one route triggers `SetToResetAndSendHome` [verified-in-code].
- `RoutingComponent.LogFailure` only increments `mnNumDoRouteFailures` unless `mbLogFailures` is set [verified-in-code].
- Entry points used by interactions: `RouteToSlot`, `RouteToPoint`, `RouteToObjectRadius`, `RouteToObjectRadialRange` (plays `Sim.PlayRouteFailure` thought balloon if `route.DoRouteFail`), `RouteToLot`, `RouteToDynamicObjectRadius*` [verified-in-code].

### Retry loops inside a single route
- `DestinationObstructedRouteInterruptAction.PerformAction` (`Sims3GameplaySystems/Sims3.Gameplay.Routing/DestinationObstructedRouteInterruptAction.cs`) runs a `StandAndWaitController` for up to `SimRoutingComponent.kDefaultStandAndWaitDuration = 50` sim minutes. Each cycle calls `GetObstructionAtPoint`, `PushSims(destPt, 0.6f)` (a range query), and every 2 to 3 cycles (`kAvoidanceReplanCheckFrequencyMin/Max = 2/3`) a full `Route.Replan()` [verified-in-code].
- `ObstacleEncounteredRouteInterruptAction` (same folder) does the same: `CollectObstacles` (native `Route_FindObstaclesForSimAlongRoute`) and periodic `Replan()` [verified-in-code].
- `StandAndWaitController.Run` (`Sims3.Gameplay.Routing/StandAndWaitController.cs`) alternates `wait_ping` / `wait_pong` animation states between cycles; the cycle length is the animation length [inferred: `RequestState` yields until the state is reached].
- In a crowded doorway or on a badly built lot, several sims can each sit in this loop and replan every couple of seconds for up to 50 sim minutes (about a real minute at speed 1) [inferred].

### Retry loops across interactions
- `InteractionQueue.ProcessOneInteraction` (`Sims3GameplaySystems/Sims3.Gameplay.ActorSystems/InteractionQueue.cs`) tries posture transitions (each usually a route) with counters `num = 4` (reset to 4 after each failed transition) and `num2 = 10`, so up to 10 transition attempts per interaction [verified-in-code].
- On failure `OnValidInteractionFailed` calls `Target.UpdateBlockTime()` if the actor has an exit reason in `ExitReason.CausesBlocking` (`0x10687` = TimedOut, FailedToStart, **RouteFailed**, ObjectInUse, CanceledByScript, FailedToRemoveFromInventory, NullValueFound; `Sims3.Gameplay.Actors/ExitReason.cs`) [verified-in-code].
- The block is weak (`Sims3.Gameplay.Abstracts/GameObject.cs`):
  - `IsBlocked` is true for only `kBlockingTimeToTestOut = 1` sim minute (about 1.25 s at speed 1). Only autonomous tests check it (`InteractionDefinitionUtilities.CommonTests` returns `Common_Blocked`) [verified-in-code].
  - After that, `GetBlockingScore()` scales the autonomy score linearly from 0 back to 1 over `kBlockingTimeToLowerScore = 30` sim minutes (`Autonomy.CalculateScoreForObjectInteraction` multiplies by it) [verified-in-code].
  - The block lives on the object, not on the sim/object pair [verified-in-code].
- Rabbit holes: `RabbitHole.RouteNearEntranceAndEnterRabbitHole` tries `kRouteAttemptsToEnterRabbitHole = 3` times and sleeps 1 to 5 sim minutes between tries (`kMin/MaxSimMinutesToSleepOnFailedRouteAttempt`) (`Sims3.Gameplay.Abstracts/RabbitHole.cs`) [verified-in-code].
- `GoHome.Run` (`Sims3.Gameplay.Core/GoHome.cs`) plans up to three world-scale routes in a row: the original route, then `Target.PlanToLot` again, then once more with `EnableWaterPlanning`. Only the third one plays the route-fail balloon. On failure it just increments the global debug counter `GoHome.NumFail` and returns false. There is no per-sim fallback such as teleporting home or resetting [verified-in-code].

### Where a sim can re-plan routes over and over
1. **NPC with nowhere to go.** `Autonomy.RunAutonomy(CommodityKind, bool)` (`Sims3.Gameplay.Autonomy/Autonomy.cs`) calls `Sim.MakeSimGoHome` when it finds no action for a non-selectable NPC on a lot with no service role. If `GoHome` fails (3 world plans), the queue empties and `Sim.DoInteraction` re-queues autonomy. `Sim.CanRunAutonomyImmediately` (`Sims3.Gameplay.Actors/Sim.cs`) returns true right away for a non-selectable sim whose last autonomous interaction failed (`mLastInteractionWasAutonomous && !mLastInteractionSucceeded`). Otherwise the delay is `kAutonomyDelayNormal = 5` sim minutes. On a world where the home lot cannot be reached, this is a loop of "autonomy search, 3 world route plans, fail" every few seconds of real time per stuck NPC, with no exit until the daily stuck check (section 4) [verified-in-code for each step; loop frequency inferred].
2. **Autonomy re-picking the same broken object.** Because the hard block lasts 1 sim minute and the score penalty fades over 30, a sim with few alternatives (for example the only toilet is unreachable) picks it again soon after, routes, fails, and plays a route-fail balloon [verified-in-code mechanism; frequency inferred].
3. **Obstructed destinations** (stand-and-wait replan loop above).

---

## 4. Stuck sims: interaction queue, autonomy and resets

### Interaction queue
- The sim's own task is `Sim.Simulate` → `while (true) DoInteraction()` (`Sims3.Gameplay.Actors/Sim.cs`). `DoInteraction` either runs the head interaction through `InteractionQueue.ProcessOneInteraction()` and `Sleep(0)`, or idles (`LoopIdle`), puts the sim in the `AutonomyManager` queue and sleeps 2/10/20 ticks [verified-in-code].
- `ProcessOneInteraction` drops the head interaction if its target is null, runs posture transitions (section 3), `RunTest`, then `RunInteractionWithoutCleanup`. If the current task is no longer a valid yielding context afterwards it throws `ResetException` [verified-in-code].

### Autonomy
- `AutonomyManager` (`Sims3.Gameplay.Autonomy/AutonomyManager.cs`) has two `LinkedList<Autonomy>` queues (high/low LOD) and a `Queue` of reactions, and runs one `Autonomy.TryFindBestAction()` per tick. `DequeueActorFromQueue` and `AddWhileInteracting` (`LinkedList.Contains`) are linear scans [verified-in-code]. With many sims the queue gets long and each sim waits longer between decisions, so sims stand idle and look "stuck" even when nothing is broken [inferred].
- `Autonomy.FindBestAction` / `RunAutonomy` → `ScoreInteractionsOnObjects`:
  - **Local autonomy** `ScoreInteractionsForLocalAutonomy` scores `lot.Map` (every interaction on every object on the lot), the sim's own map, **every other sim on the lot** (`lot.GetObjects<Sim>()`), the inventory, situation interactions and solo interactions. **It never yields**: `YieldIfPossible` is only called from the meta-autonomy loop [verified-in-code: the only four callers are in `ScoreInteractionsForMetaAutonomy`]. So a full local search on a big, object-heavy lot runs inside one tick.
  - **Meta autonomy** `ScoreInteractionsForMetaAutonomy` walks `sPublicMetaObjects` plus `mLotsKnownAbout` (copied into a new `List` each time) and yields only when `num % 5 == 0 && sUpdateTimer.GetElapsedTimeFloat() > 1f` (StopWatch in milliseconds). So each slice runs at least about 1 ms and always at least 5 venues [verified-in-code].
  - It also calls `Sim.CalculateNumSimsAtHomeOrGoingHome()` (a loop over `LotManager.Actors`) on every search [verified-in-code].
  - `Autonomy.IsSufficientlyOnScreenForHighLODSimulation` re-evaluates LOD every 60 ticks [verified-in-code].
- `Autonomy.Update` (motives, buffs, mood, skills, careers, relationships, opportunities, two `EventTracker.SendEvent` calls) is called from `SimUpdate.Simulate` for high-LOD sims, and from `RunAutonomy` for low-LOD sims [verified-in-code].
- Scoring functions: `Autonomy.CalculateScoreForObjectInteraction`, `ComputeCheckScore`, `CalculateScoreForSocial`; the tuning hook is `iop.Tuning.ScoringFunction` [verified-in-code; per-object cost not measured].

### What makes a sim loop
- A failed autonomous interaction leads to immediate autonomy for NPCs (`CanRunAutonomyImmediately`), which often picks the same object again (weak block, section 3) [verified-in-code mechanism].
- No action found leads to `MakeSimGoHome`, which fails on unreachable homes and repeats (section 3) [verified-in-code].
- Interactions that wait for an exit reason poll every tick (`WaitForExitReason` → `Sleep(1)`). A sim parked in a looping interaction whose exit condition never comes keeps a task waking every tick indefinitely [verified-in-code mechanism].

### Built-in reset and stuck detection
- `LotManager.RunStuckCheckHelper` (`Sims3.Gameplay.Core/LotManager.cs`) is armed for **every actor once a day** from `LotManager.CleanUpLotsAndInventories`, a daily alarm at `kVenueCleanupTime = 3` (3 AM) [verified-in-code].
  - It schedules a global alarm 20 to 60 sim minutes later.
  - It treats a sim as stuck only if the sim is **at exactly the same position** as when armed, is not on the active household's lot and is not in a rabbit hole.
  - It then tries up to `Sim.kNumberOfPlanAttemptsForStuckSim = 10` route plans to random lot points (on the world lot: 4 short plans, selectable sims only). If all fail it re-arms, up to `kMaxAttempts = 3`, and finally calls `sim.SetObjectToReset()`.
  - The plans run synchronously inside an alarm callback (no yield) [verified-in-code].
  - So a sim that keeps moving while looping (route fail, turn, retry) is **never** caught. A truly frozen sim is reset at the earliest about 1 to 3 sim hours after 3 AM [verified-in-code thresholds; the conclusion is inferred].
- Other built-in resets: `RoutingComponent.DoRoute` water-entry cap → `SetToResetAndSendHome` (section 3); `Sim.SetObjectToReset` → `ScriptProxy.SetObjectToReset` → native `ScriptProxy_SetResetState(kResetRequested)`; `GameObject.SetObjectToReset` / `DoRootReset` for non-sim objects. There are about 103 `SetObjectToReset()` call sites in Sims3GameplaySystems [verified-in-code].
- Debug: `Sim.DEBUG_CheckStuckSim` (testingcheats) runs the same helper on demand [verified-in-code].
- Community mods (NRaas Overwatch, ErrorTrap) add stronger stuck-sim resets and error trapping [community-reported; not checked here].

---

## 5. Expensive per-tick and periodic script work (ranked by likely stutter impact)

1. **Route planning in failure loops** (sections 3 and 4). Each `Route_Plan` / `Route_Replan` is a native pathfinding search; world-scale plans (`GoHome`, `PlanToLot`, cars, subways) are the expensive ones. This is multiplied by the per-plan script callbacks (`GetObjects<CommonDoor>` per lot, world-wide `GetObjects<IStation>`, `ToString` allocation). On worlds with bad pathing (unroutable lots, broken portals) failures are frequent [verified-in-code mechanism; cost inferred, measure with Frida on `Route_Plan`].
2. **Local autonomy in one tick.** `Autonomy.ScoreInteractionsForLocalAutonomy` scores every interaction on the lot plus every sim on it with no yield. Cost grows with objects × interactions on the lot, and with mods that add interactions to every object [verified-in-code; size of spike inferred].
3. **Per-tick wake-ups that scale with population.** Every sim's `SimUpdate` resumes every tick when low-LOD or over the 2-per-tick limit, every sim in a looping interaction resumes every tick (`WaitForExitReason`), every commercial lot's `Lot.Simulate` runs every tick (`ShouldSimulate` returns true for `LotType.Commercial`), plus `SeasonsManager`. Per lot per tick this includes `AlarmManager.Simulate`, `UpdateDetailPriority` (loops over the active household's actors and calls native `World.LotSetDetailPriority`), and `UpdateReactions` (which allocates `list.ToArray()`). This is a steady drag, not a spike, and grows with town size [verified-in-code; cost inferred].
4. **Alarm bursts.** `AlarmManager.Simulate` drains all due alarms at once. Known heavy daily alarms:
   - 3 AM `LotManager.CleanUpLotsAndInventories`: every lot runs `MagicallyCleanUp` or `MagicallyChangeSeason`, plus per-lot `GetObjects<IVelvetRopes>`, per-NPC inventory seeding, and arming the stuck check.
   - 1 AM `StoryProgressionService`: 4 to 10 actions, each calling `Demographics.Collect` over all lots and households with no yield inside `Collect`, though there are `Sleep(0)` between actions.
   - Global repeating alarms such as `MaintenanceComponent.DecayCallback` every 10 minutes over every maintenance component (each firing an `EventTracker` event), and `Responder` / `HudModel` once-a-minute ticks.

   At speed 3 these line up into one-tick spikes [verified-in-code; impact inferred].
5. **Reaction broadcasters.** `ReactionBroadcaster.Simulate` (`Sims3.Gameplay.Autonomy/ReactionBroadcaster.cs`) pulses once per sim minute per broadcaster, and each pulse does a native range query (`Queries.GetObjects<Sim>(pos, radius)`) plus a new `List` and `Dictionary`. These are driven by `Lot.UpdateReactions` on every simulating lot [verified-in-code].
6. **Meta autonomy.** At least about 1 ms of venue scoring per tick while a search is in progress, plus an O(sims) count per search [verified-in-code].
7. **`AutonomyManager` serialization.** Only one search per tick means latency, not CPU, but with many sims it makes idle "stuck-looking" sims more common [verified-in-code; perception effect inferred].

Lists that grow over time (candidates, not proven leaks):
- `AlarmManager.mTimerQueue` keeps cleared timers until they reach the heap head [verified-in-code].
- `Lot.mSavedData.mReactions` / `mBroadcastersWithSims` are pruned only when a broadcaster's end condition fires [verified-in-code].
- `Household.sHouseholdList` / `EveryHumanSimDescription()` grow with story progression [inferred]. Ghost and orphaned SimDescriptions were not traced in this pass.

No obvious O(n²) loop runs every tick. The closest are `AutonomyManager.AddWhileInteracting` (`LinkedList.Contains` per add) and the per-search O(sims) count, which together are O(sims²) per autonomy cycle across the population [verified-in-code; impact small].

---

## 6. GC pressure

Mono 2.0-era runtimes use the non-generational Boehm collector, so every collection is a full stop-the-world mark of the whole heap. Allocation rate therefore shows up directly as periodic hitches whose length grows with heap size [inferred for TS3W's exact Mono build; confirm in Ghidra via `mono_gc_collect` / `GC_gcollect` and measure with `Simulator_GetUsedBytesImpl`].

Hot allocation sites found:
- Every `Queries.GetObjects<T>(...)` / `lot.GetObjects<T>()` returns a new array (native allocation into the managed heap) [verified-in-code wrapper]. These appear in per-plan callbacks, local autonomy (`lot.GetObjects<Sim>()`), `Lot.UpdateDoorPrivacy`, and `ReactionBroadcaster.GetInRadiusSims`.
- `Lot.UpdateReactions`: `list.ToArray()` every tick on every simulating lot that has reactions [verified-in-code].
- `ReactionBroadcaster.GetInRadiusSims`: `new List<CASAgeGenderFlags>` + `new Dictionary<ObjectGuid, Sim>` per pulse [verified-in-code].
- `Route.Plan` / `PlanFromPoint` / `Replan`: `RoutePlanResult.ToString()` per plan for `PostPlanCallback` [verified-in-code].
- `Autonomy.ScoreInteractionsForMetaAutonomy` and `FindClosestLotForLocalAutonomy`: `new List<IMetaObject>(sPublicMetaObjects)` per search [verified-in-code]; `FindActions` builds new lists of candidates [verified-in-code].
- `AlarmManager.InvokeYieldingAlarm`: a new `AlarmOneShot` task per yielding alarm; `Autonomy.PerformInteraction`: `new ImmediateInteractionTask` per immediate autonomous action [verified-in-code].
- `RunStuckCheckHelper.CreateAlarm`: an interpolated string (`$"RunStuckCheck(#{attempt}:{sim.FullName}"`) per sim per day [verified-in-code].
- `Household.EveryHumanSimDescription()`: a new list of every human SimDescription per call; `Demographics` calls it at least 7 times [verified-in-code].
- No LINQ: no file in Sims3GameplaySystems or Sims3GameplayObjects imports `System.Linq` [verified-in-code].

Explicit `GC.Collect()` calls exist only in rare paths: `DynamicChallengeManager.DestroyChallenges`, the `ReferenceWatchdog.ValidateReferences` debug tool, and a cheat command in `Commands.cs` [verified-in-code]. So the stutter-relevant collections are the runtime's own, triggered by allocation [inferred].

---

## 7. Threading

- Scripts are entirely single-threaded. There is no `System.Threading` use in any game script assembly, and every task runs cooperatively on the simulator thread (section 1) [verified-in-code].
- `ScriptExecuteType.Threaded` does not mean an OS thread. It marks a long-running microthread task that loops and sleeps, as `Sim`, `Lot` and `AutonomyManager` do [verified-in-code usage; native meaning inferred].
- Anything parallel happens in native code: pathfinding (unknown whether planning runs off-thread; `Route_Plan` returns a result synchronously to script, so from the script's view it blocks), animation, rendering, streaming and audio [inferred].
- Consequence: script-side gains come from doing less per tick (fewer wake-ups, fewer plans, yielding inside long searches, fewer allocations). Native gains come from cheaper context switches, cheaper `Route_Plan`, a better GC, or moving pathfinding off the main thread if the native design allows it [inferred].

---

## Top stutter suspects (ranked)

| # | Suspect | Mechanism | Evidence path | Fix idea | Fix type | Est. impact |
|---|---|---|---|---|---|---|
| 1 | NPC "go home" and route-fail loops | Autonomy finds nothing → `MakeSimGoHome` → `GoHome` does up to 3 world-scale plans → fails → `CanRunAutonomyImmediately` re-queues at once; no per-sim give-up | `Sims3.Gameplay.Autonomy/Autonomy.cs` `RunAutonomy`; `Sims3.Gameplay.Core/GoHome.cs` `Run`; `Sims3.Gameplay.Actors/Sim.cs` `DoInteraction`, `CanRunAutonomyImmediately` | Count consecutive route failures per sim; after N, fade out and teleport home or reset. Add a cool-down before re-queueing autonomy after a failure. Natively: profile and cache `Route_Plan` failures per (start lot, dest lot) for a few sim minutes | both | High on bad-pathing worlds and big towns |
| 2 | Local autonomy runs in one tick | `ScoreInteractionsForLocalAutonomy` scores the whole lot map and every sim on the lot with no yield | `Sims3.Gameplay.Autonomy/Autonomy.cs` (only meta autonomy calls `YieldIfPossible`) | Add a time-sliced yield (like the meta loop) inside `ScoreInteractions`; trim interactions per object | script-mod | High on big or modded lots, during autonomy-heavy play |
| 3 | Weak object blocking after route failure | `IsBlocked` lasts 1 sim minute and the penalty fades over 30; the block is per object, not per sim, so autonomy re-picks the unreachable object | `Sims3.Gameplay.Abstracts/GameObject.cs` `IsBlocked`, `GetBlockingScore`; `Sims3.Gameplay.ActorSystems/InteractionQueue.cs` `OnValidInteractionFailed` | Per-sim, per-object block that lasts longer for `RouteFailed` | script-mod | Medium-high (drives #1 and route-fail spam) |
| 4 | Stand-and-wait replan loops | Obstructed destination: replan every 2 to 3 cycles for up to 50 sim minutes, plus `PushSims` queries | `Sims3.Gameplay.Routing/DestinationObstructedRouteInterruptAction.cs`, `ObstacleEncounteredRouteInterruptAction.cs`, `StandAndWaitController.cs` | Shorter `kDefaultStandAndWaitDuration`, back off the replan frequency (tunables via XML) | script-mod (tuning) | Medium-high in crowded venues |
| 5 | Per-tick task wake-ups scale with sims and lots | Low-LOD `SimUpdate` sleeps 0, `WaitForExitReason` sleeps 1, every commercial lot ticks every frame | `Sims3.Gameplay.Actors/SimUpdate.cs`, `Sim.cs` `WaitForExitReason`, `Sims3.Gameplay.Core/Lot.cs` `Simulate`, `ShouldSimulate` | Script: sleep longer for low-LOD sims and inactive commercial lots. Native: make `Simulator_SleepImpl` context switches cheaper or skip tasks that would immediately sleep | both | Medium (constant main-thread drag) |
| 6 | GC pauses from steady allocation | Query arrays, `ToArray` per lot per tick, `ToString` per plan, per-search lists; non-generational GC | sections 5 and 6 | Script: reuse buffers, drop `ToArray`/`ToString`. Native: tune or replace the GC (S3SS already throttles GC; avoid duplicating it) | both | Medium (periodic hitches that grow with save age) |
| 7 | Alarm bursts | `AlarmManager.Simulate` drains all due alarms in one tick; 3 AM cleanup and stuck checks over all lots and actors; 1 AM story progression | `Sims3.Gameplay.Utilities/AlarmManager.cs` `Simulate`; `Sims3.Gameplay.Core/LotManager.cs` `CleanUpLotsAndInventories`; `Sims3.Gameplay.StoryProgression/StoryProgressionService.cs` | Cap alarms per tick (defer the rest); spread daily work across ticks | script-mod | Medium (spikes at specific times, worse at speed 3) |
| 8 | Built-in stuck check misses looping sims | Only checks once a day, only exact-position matches; 10 synchronous plans per check; reset after up to 3 tries | `Sims3.Gameplay.Core/LotManager.cs` `RunStuckCheckHelper` | Detect loops by counting route failures or repeated identical interactions, and reset after a threshold | script-mod | Medium (removes the cause of #1 and #4 sooner) |
| 9 | Per-plan script callbacks | Door forbidding (`GetObjects<CommonDoor>`), world-wide `GetObjects<IStation>`, allocations on every plan | `Sims3.Gameplay.Actors/SimRoutingComponent.cs` `AboutToPlanRouteCallback`, `PostPlanRouteCallback` | Cache door and station lists per lot or world and invalidate on build/buy | script-mod | Low-medium (multiplies #1) |
| 10 | Meta autonomy slices | At least about 1 ms per slice, O(sims) count per search, list copy | `Sims3.Gameplay.Autonomy/Autonomy.cs` `ScoreInteractionsForMetaAutonomy` | Lower the slice to about 0.3 ms; cache `CalculateNumSimsAtHomeOrGoingHome` per tick | script-mod | Low-medium |

Next steps:
- Confirm #1, #2 and #5 by measurement before building fixes: Frida hooks on `Route_Plan`, `Route_Replan`, `Simulator_SleepImpl` and `Query_GetObjects` (count calls and time per tick).
- Check what S3SS already patches (tick limiting, GC throttling) so we do not duplicate it.
- Check what NRaas mods already do for stuck sims [community-reported] before writing a script mod.

## Native entry points to find in Ghidra

TS3W.exe contains these names as `ScriptCore.<Class>::<Method>` strings, verified for `ScriptCore.Simulator::Simulator_SleepImpl`. Each one should lead to a registration table that maps the name to its native function.

Scheduler and tasks:
- `ScriptCore.Simulator::Simulator_SleepImpl`
- `ScriptCore.Simulator::Simulator_WakeImpl`
- `ScriptCore.Simulator::Simulator_AddObjectImpl`
- `ScriptCore.Simulator::Simulator_DestroyObjectImpl`
- `ScriptCore.Simulator::Simulator_GetCurrentTaskImpl`
- `ScriptCore.Simulator::Simulator_GetTaskImpl`
- `ScriptCore.Simulator::Simulator_GetTickCountImpl`
- `ScriptCore.Simulator::Simulator_SetYieldingDisabledImpl`
- `ScriptCore.Simulator::Simulator_GetUsedBytesImpl`
- `ScriptCore.Simulator::Simulator_DoCPPProfile`
- `ScriptCore.TaskControl::TaskControl_GetTaskContext`
- `ScriptCore.TaskControl::TaskControl_SaveNativeYieldObject`
- `ScriptCore.TaskControl::TaskControl_LoadNativeYieldObject`
- `ScriptCore.ScriptProxy::ScriptProxy_SetResetState`

Routing:
- `ScriptCore.Route::Route_Plan`
- `ScriptCore.Route::Route_PlanFromPoint`
- `ScriptCore.Route::Route_Replan`
- `ScriptCore.Route::Route_ReplanFromPosition`
- `ScriptCore.Route::Route_GetObstructionAtPoint`
- `ScriptCore.Route::Route_GetDestinationObstruction`
- `ScriptCore.Route::Route_FindObstaclesForSimAlongRoute`
- `ScriptCore.Route::Route_IsPointRoutable`
- `ScriptCore.Route::Route_BeginFollowingRouteAnimationSpec`
- `ScriptCore.RouteManager::RouteManager_IsRouteToObjectUnobstructed`
- `ScriptCore.RouteManager::RouteManager_RoutingBlockerCheck2d`
- `ScriptCore.RouteManager::RouteManager_MarkRoomForUpdate`
- `ScriptCore.RouteManager::RouteManager_BeginPortalRouting`

Queries and objects:
- `ScriptCore.Queries::Query_GetObjects`
- `ScriptCore.Queries::Query_GetObjectsOnLot`
- `ScriptCore.Queries::Query_GetObjectsInRange`
- `ScriptCore.Queries::Query_GetGlobalObjects`
- `ScriptCore.Queries::Query_CountObjects`
- `ScriptCore.Objects::Objects_IsValid`
- `ScriptCore.Objects::Objects_GetPosition`
- `ScriptCore.World::World_LotSetDetailPriority`
- `ScriptCore.World::World_GetLotLocationImpl`

Time and profiling:
- `ScriptCore.GameUtils::GameUtils_GetGameTimeScaleImpl`
- `ScriptCore.GameUtils::GameUtils_SetGameTimeSpeedLevel`
- `ScriptCore.GameUtils::GameUtils_GetCurrentMemoryUsage`
- `ScriptCore.StopWatch::StopWatch_GetElapsedTimeFloatStopwatch`
- `ScriptCore.ProfilerUtils::ProfilerUtils_BeginEventImpl`
- `ScriptCore.ProfilerUtils::ProfilerUtils_EndEventImpl`

Real `InternalCall`s:
- `ScriptCore.Reflection::Reflection_GetTypeClassification`
- `ScriptCore.Reflection::Reflection_IsTypeAssignableFrom`

Other leads, string search only: "Budget violations detected", "noBudget", "RemoveMostFrequentlyUnusedTimeSliceUSec", "YieldAnalysisTaskMap", "CheckYieldingContext".
