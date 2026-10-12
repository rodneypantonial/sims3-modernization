# Expansion travel and off-screen trip shortcuts

Research note (2026-10-12): which game methods move Sims during expansion travel (Late Night, Into the Future, University, Pets, Island Paradise), whether the existing trip shortcuts (docs/04 F8 to F10, `mods/ts3fixes/src/Fixes.cs`) already cover them, and where a new hook would pay. Code paths are relative to `game-copy/decompiled/`. Markers: **[verified in code]** = read in the decompiled source; **[inferred]** = reasoning, still to confirm by measurement or the agent.

Shortcut rules used throughout (from F9/F10): unseen townie (not active household, alone, standing, off screen, far from camera and active Sims), destination unseen, spot passes `Sim.IsPointInLotSafelyRoutable`, then the game's original method runs the last metres. Never break what the Sim does next, never strand a Sim, skip the middle of a trip, never its ends.

## Key finding: expansion transport is mostly *inside* the route plan

`SimRoutingComponent.SetRouteOptions` (`Sims3GameplaySystems/Sims3.Gameplay.Actors/SimRoutingComponent.cs`) switches the transport modes on for every Sim route: `EnableSubwayPlanning` if EP3 is installed, `EnableHoverTrainPlanning` in the EP11 future world, `EnablePlanningAsBoat`/`EnableWaterPlanning` and `RouteAsMermaid` with EP10, `RouteWhileMounted`/`RouteAsLargeAnimal` for horses, `EnablePlanningAsCar` (cars *and* bikes) for child-or-above. **[verified in code]** So subway, hover-train, boat, bike and mermaid legs are parts of the same trip that `GoHome.Run`, `VisitCommunityLot.Run`, `VisitLot.Run`, `GoToLot.Run`, `GoToCommunityLot.Run` and `RabbitHole.RouteNearEntranceAndEnterRabbitHole` plan. Our shortcuts place the Sim *before* that plan, so when the guards pass, every one of these legs is already skipped. **[verified in code]** What is left are (a) trips that do not go through those six entry points, and (b) per-plan overhead that runs for Sims we do not shortcut.

## Summary table (ranked by expected gain vs risk)

| # | Item | Entry method | Covered today? | Expected gain | Risk | Recommendation |
|---|---|---|---|---|---|---|
| 1 | Townies with no home lot "going home" (walk to a random lot or their apartment building, then vanish) | `Sim.GoToVirtualHome.GoToVirtualHomeInternal.Run` (`Sim.cs`) | No | **High**: 4.4% of sim-thread samples in Bridgeport, ~1.1% elsewhere (docs/04 F7b) | Low (game's own end state is `FadeOut(destroyObject: true)`) | **Build** (fix V1) |
| 2 | Subway / hover-train station check after every successful cross-lot plan | `SimRoutingComponent.CheckAndUpdateRouteForStations` (private static, from `PostPlanRouteCallback`) | No (runs for every Sim we do not shortcut) | Medium, unmeasured: up to 3 extra plans + `Queries.GetObjects<IStation>` per plan | Low-medium; static target is untested in the patch host | **Measure first**, then hook or tune (fix V2) |
| 3 | Apartment residents (`VirtualLotHome` with a `BuildableShell`) walking across town to the shell door | `ShellWithDoor.RouteInsideShell` (called from #1) | No | Medium in Bridgeport | Medium (elevators on arrival stay) | Build after V1 (fix V3) |
| 4 | Home-lot apartments / penthouses (`LotHome` has `BuildableShells`) | `GoHome.Run` | No (guard `home.BuildableShells.Length > 0`) | Low-medium | Medium (elevator contention) | Later, only after elevator stuck cause is known |
| 5 | GoHome shortcut can leave an NPC's horse behind | `GoHome.Run` adds the horse after our hook decides | Gap in existing hook | Correctness | Low | **Add guard** (fix V4) |
| 6 | Elevators (Late Night) | `ElevatorDoors.ElevatorDoorsPortalComponent`, `ElevatorInterior.ElevatorPortalComponent`, `PortalComponent.AcquireLaneRouteAction` | Partially (trips into shells skipped by guards) | – | High | **Do not hook**; investigate stuck Sims with the agent |
| 7 | Island Paradiso boats / water taxis | `GetInBoatRouteAction.PerformAction` (inside trips) | Yes when trip is shortcut | Residual only | High | Do not hook; route-failure backoff instead |
| 8 | Hover train as an activity, travel tubes | `Sim.NPCUseHoverTrain.Run`, `HoverTrainStation.TravelTo.Run`, `TubeElevator` | n/a | Low | Changes an activity | Do not touch |
| 9 | Bicycles, hover scooters, hoverboards, jetpacks, skateboards | car-mode routing / postures | Yes (same trip methods) | – | – | Nothing to do |
| 10 | Horse riding, mermaids, ocean swimming | `Terrain.RideHere`, `SetRouteOptions` | Excluded by `TownieUnseen` posture checks, or covered | – | High | Do not touch |

---

## 1. Late Night (EP3)

### 1a. Townies "going home" without a home lot (`GoToVirtualHome`) — biggest win

Path **[verified in code]**:
- `Sim.MakeSimGoHome(Sim, bool, InteractionPriority, WalkStyle, ...)` (`Sim.cs` ~48125): a Sim with `LotHome != null` gets `GoHome`; every other Sim (townies from the pool, apartment-building NPCs, service Sims) gets `Sim.GoToVirtualHome`. `GoToVirtualHomeMeta.Run` (~4300) pushes the same interaction from meta autonomy.
- `GoToVirtualHome.Run` (~4561): target is `SimDescription.VirtualLotHome`; if null, **a random lot** (`LotManager.AllLotsWithoutCommonExceptions`, not current, not active, not the selected Sim's home, not in sea). Resort guests are pushed into a resort tower instead. Then pushes `GoToVirtualHomeInternal`.
- `GoToVirtualHomeInternal.Run` (~4385):
  - not on target yet: apartment with shells → `buildableShells[0].RouteInsideShell(Actor)`; apartment without shells → `Actor.RouteToLot(...)`; no virtual home → `Target.PlanToLotEx(route)` + `Actor.DoRoute(route)` (a full cross-town plan to a random lot).
  - active household → return; tourists → `SendTouristHome`; aliens → HUD update + `FadeOut`.
  - virtual-home Sims arriving from elsewhere: with `kAutonomuousReactionChanceOnGoingToVirtualHome = 1f` they always try one autonomous action on the apartment lot first (`FindBestActionForCommodityOnLot`), callback `OnAutonomousDone` sends them home again.
  - otherwise route to their NPC apartment door (`GetAssociatedNPCDoor`, `RouteToSlotListAndCheckInUse`) or **plan a route to every `ElevatorDoors` on the lot** with a closer level and walk to the best one, then `FadeOut(yield: true, destroyObject: true)` + `Simulator.Sleep(uint.MaxValue)`.
  - **no virtual home: after the walk, `FadeOut(destroyObject: true)`.** The walk result is not even checked.
- The interaction's display name is "Go Home" (`GetInteractionName` uses `Gameplay/Core/Lot/GoHome:InteractionName`), so in the UI and the agent it looks like `GoHome`. **[verified in code]**

So a homeless townie plans and walks to a random lot only to be destroyed there. F7b measured `GoToVirtualHomeInternal` at 4.4% (Bridgeport), 1.2% (Sunset Valley), 1.1% (Isla Paradiso). **[measured, docs/04 F7b]**

**Hook design V1** (target `Sim.GoToVirtualHome.GoToVirtualHomeInternal.Run`, instance, public nested class, so the usual `Hook : GoToVirtualHomeInternal` + instance stub pattern works):
- Guards: `TownieUnseen(Actor, null)`; `Actor.LotCurrent != Target`; `Actor.SimDescription.VirtualLotHome == null`; household not active, not tourist (`IsTouristHousehold`), not alien (`IsAlienHousehold`); not a service Sim mid-job (`Actor.Service == null`) **[inferred guard, cheap]**.
- Action: exactly the original's final statements for this case: `Actor.FadeOut(true, true); Simulator.Sleep(uint.MaxValue); return true;`. No placement, no teleport: the Sim vanishes where it stands, off screen, with the same end state as the original after its walk. Success/failure callbacks behave as in the original, which never returns on this path either. **[verified in code]**
- Counters: runs / vanished early. Check in the harness: Sims-in-world count over time, and whether ambient spawners refill faster (fewer Sims "at home or going home" feeds `GoHomeToFillQuota` scoring in `GoToVirtualHomeMeta.Definition.CalculateScore`) **[inferred risk: may offset part of the gain]**.
- Why it is safe vs the dropped traffic shortcut: nothing is placed and nothing routes afterwards; the Sim object is destroyed exactly as the game does a few sim-minutes later.

### 1b. Apartment residents (`VirtualLotHome` set) — fix V3

`GoToVirtualHomeInternal.Run` computes `flag = Actor.LotCurrent == Target` *before* calling `RouteInsideShell`; if the Sim is already on the lot, the arrival reaction is skipped. So placing the Sim in a `GoToVirtualHomeInternal.Run` hook would change "what it does next". The right place is **`ShellWithDoor.RouteInsideShell(Sim)`** (`Sims3.Gameplay.Abstracts/ShellWithDoor.cs`; `BuildableShell` does not override it): it is called after `flag` is set, and its body is `MainEntranceDoor.RouteThroughDoor(actor)`. **[verified in code]**
- Callers: only `GoToVirtualHomeInternal.Run` (`Sim.cs` ~4401) and `Shell.cs` ~49 (a shell interaction). Guard `Actor.CurrentInteraction is GoToVirtualHomeInternal` to stay out of the second.
- Hook: if unseen townie and the shell lot is far and unseen, `SafeSpot(sim, LotCurrent, MainEntranceDoor position, ...)` (needs a variant without the `BuildableShells.Length > 0` exclusion), `PlaceSim`, then the original `RouteThroughDoor` (a few metres). The arrival reaction, the NPC door / elevator walk and the fade-out stay the game's own.
- Risk: the Sim then still uses the building's elevators (1c) for the last part. Gain is the cross-town walk only.

### 1c. Elevators: why off-screen Sims get stuck at Bridgeport elevators

Methods that move Sims **[verified in code]**:
- `ElevatorDoors.ElevatorDoorsPortalComponent` (`Sims3.Gameplay.Objects.Elevator/ElevatorDoors.cs`): `AddPortals` (route-through portal into the car), `GetStartSlotsForApproachingSim` (returns **no slots** while the car is in use, full, or going the other way; then only calls `RequestElevator`), `OnApproachingPortalObject`, `OnPortalStart` (in-use car: `PlayRouteFailure` + `Route.Stop(kStop_Hard)`; else `RegisterSim`, maybe teleport + `ForceReplan`), `TryTeleportSkip`.
- `ElevatorInterior.ElevatorPortalComponent` (`ElevatorInterior.cs`): one-way portals between every pair of connected floors (`UpdateAllElevatorPortalsOnLot`, cost `kElevatorToElevatorPortalCost = 10`). `OnPortalStart` sleeps until every assigned Sim is "sleeping" in the car and the hold timer fired (`IsElevatorReadyForLaunch`, `kMinutesToHoldElevator = 5`), with a **30 sim-minute failsafe that calls `s.SetObjectToReset()`**; then `SetPosition` to the next floor. `OnPortalStop` re-registers the Sim on the next floor's car. `ElevatorOnRouteActionsFinished` **resets** a Sim whose route ends while still registered.
- `PortalComponent.AcquireLaneRouteAction.WaitInLine/AcquireLane` (`Sims3.Gameplay.ObjectComponents/PortalComponent.cs` ~109-210): Sims queue in `ElevatorDoors.Line` (`SimQueue`, `OrderByProximity`), try `TryTeleportSkip` when the car has ≥2 riders or after `kElevatorSimQueueWaitBeforeTeleportSkipTime = 15` min, and give up after `ElevatorGiveUpTime` (`kElevatorGiveupTime = 30` min) → `ActionResult.Terminate` → the trip fails and the calling interaction re-plans.
- Every floor hop and every teleport skip ends in `ForceReplan` / `ContinueAndPopPathAndReplan` → `Route.ReplanFromPoint` in `RoutingComponent.DoRoute` (`RoutingComponent.cs` ~540-585). **[verified in code]**

Why townies get stuck rerouting **[inferred, to confirm]**:
1. One car serves both directions; a Sim waiting to go the other way gets no lane (`GetStartSlotsForApproachingSim` empty) and waits up to 15 + 30 sim-minutes, then fails its route, and its go-home / visit interaction re-plans the whole trip (another full `Route_Plan` + replans), and comes back to the same elevator.
2. Launch needs *all* assigned Sims asleep in the car. A Sim registered on a car (`mAssignedSims`, persisted) that never arrives (cancelled, reset, re-planned elsewhere) blocks launch until the 30-minute failsafe **resets the waiting Sims**; reset Sims are sent home again and repeat.
3. Destinations inside shells (`Lot.PlanRouteIntoLot` with `HasVirtualResidentialSlots` picks an upper-level point, ~9006) make many Bridgeport go-home and visit routes cross elevators, and our shortcuts skip shell lots on purpose (`ShortcutToLot`, `GoHomeHook.Unseen`), so these trips run in full.
4. The named stuck townies in `Baseline-BP` show "Go Home": that may be `GoToVirtualHomeInternal` (same display name), not `GoHome`.

Recommendation: **do not shortcut through elevators** (teleporting a registered Sim leaves `mAssignedSims` state behind, exactly the stranding pattern of the dropped traffic shortcut). Next step: agent query on the stuck Sims for interaction type, current route action (`AcquireLaneRouteAction` on `ElevatorDoors`?), `ElevatorPortalComponent.IsSimInElevator`, and the car's `mAssignedSims`. Only then consider a narrow fix (e.g. unregister Sims whose task is gone).

### 1d. Subway — fix V2

- The subway is a route portal (`Subway.SubwayPortalComponent`, `Sims3GameplayObjects/Sims3.Gameplay.Objects.RabbitHoles/Subway.cs`): `OnPortalStart` animates into the rabbit hole; `OnPortalStop` sleeps `TimeToDestination`, `SetPosition` to the destination station's exit slot, then `PortalAction.ForceReplan`. **[verified in code]**
- NPCs get a subway leg from `SimRoutingComponent.PostPlanRouteCallback` → `CheckAndUpdateRouteForStations` (`SimRoutingComponent.cs` ~2184-2340), which runs after **every successful plan with subway or hover-train planning on** (including replans): skips Sims with a usable car; for NPCs continues with `kNPCSubwayUseChance = 0.75`; scans all `IStation` objects (`Queries.GetObjects<IStation>()`), plans a second route to the start station (`PlanToSlot`), replans the main route from the exit station (`ReplanFromPoint`), and on failure or too little saving **replans again from the original start** (`kDistanceMustSaveInOrderToUseSubway = 10`). **[verified in code]** That is up to 3 extra native plans per accepted plan for every carless townie in Bridgeport (and Oasis Landing).
- Player-directed subway rides (`Subway` travel interaction, ~785-805) use `RouteNearEntranceAndEnterRabbitHole`, already hooked (`RabbitHoleHook`). **[verified in code]**
- Design V2: (1) first add a counter (sampler frame share of `CheckAndUpdateRouteForStations` in BP). (2) If it pays: replace `CheckAndUpdateRouteForStations` with a version that returns immediately for unseen townies (`r.Follower.Target is Sim s && TownieUnseen(s, null)`) and calls the original otherwise. It is **private static**: the patch host only verified instance targets (docs/04 F8: static stubs break), so this needs a patch-host test on a static target first. Fallback without a code hook: set the private tunable `kNPCSubwayUseChance` lower by reflection at startup; global, so visible NPCs would also ride less (A/B only).
- Risk: an unseen townie walks instead of riding; arrival is later but identical. Low.

---

## 2. Into the Future (EP11)

- **Hover train**: same station mechanism as the subway. `CheckAndUpdateRouteForStations` inserts an `ElevatedTrainPath` sub-path for `IHoverTrainStation` pairs (~2310-2330); boarding is `GetInHoverTrainRouteAction`, the station portal is `HoverTrainStation.HoverTrainStationPortalComponent` (`HoverTrainStation.cs` ~27-210). Covered by our trip shortcuts when they fire; V2 covers the post-plan cost. **[verified in code]**
- `Sim.NPCUseHoverTrain` (`Sim.cs` ~24530, autonomous solo interaction): picks a random station and pushes `HoverTrainStation.TravelTo` (~456: `RouteOutside` of the destination, station use forced through `IHoverTrainStationInteraction`). This is a ride *as an activity*; shortcutting it deletes the activity. **Do not touch.** **[verified in code]**
- **Travel tubes** (`Sims3.Gameplay.Objects/TubeElevator.cs`): a buildable lift portal between floors with a line (`kMaximumNumberOfSimsInLine = 3`, `kTimeToWaitInLine = 15`) and skill-based failure; used inside lots, not for town travel. Do not touch. **[verified in code]**
- **Hoverboards / jetpacks**: postures pushed and popped around ordinary pedestrian routes (`RoutingComponent.DoRoute` `PushJetpackPostureIfNecessary` ~435, `PushHoverboardPostureIfNecessary` ~491; portal actions pop them). No own planning. Covered. **[verified in code]**
- Hover scooter taxis (`HoverScooterTaxi`) are car-mode vehicles, like bikes below. **[verified: class names; inferred: same path]**

## 3. University (EP9)

- **Bicycles**: `Bicycle : Vehicle` (`Sims3.Gameplay.Objects.Vehicles/Bicycle.cs`); chosen by `Sim.GetOwnedAndUsableVehicle(..., bAllowBikes: true, ...)` (Rebellious / Environmentally Conscious preferences ~51246-51254); in the University world `Vehicle.CreateTaxi` returns an `AdultTaxiBicycle` (`Vehicle.cs` `CreateTaxiForSpecialWorld`). The route is planned with `EnablePlanningAsCar`; `GetInVehicleRouteAction.PerformAction` places the bike and seats the Sim; `VehicleFollowSubPathRouteAction` follows. So a bike trip *is* a car trip of `GoHome` / `VisitCommunityLot` / rabbit-hole trips: **already covered** by the existing hooks. **[verified in code]**
- **Skateboards**: no skateboard vehicle class exists (only `TableEndSkateboard`, decor); skating is a walk style (`SimWalkStyleRules.IsSkating`). Nothing to hook. **[verified in code]**

## 4. Pets (EP5)

- **NPC riding**: `HorseManager.NpcShouldRideHorse` (`Sims3.Gameplay.ActorSystems/HorseManager.cs` ~723) is called from `GoHome.Run` (~314), `VisitCommunityLot.Run` (~271), `VisitLot.Run` (~150) and `InviteToLotSituation`; it adds a household horse on the *current* lot as a follower. A Sim already mounted goes through `Terrain.RideHere` (`MountedTerrainInteraction`), routing with `RouteWhileMounted` / `RouteAsLargeAnimal`. **[verified in code]**
- Existing coverage: `TownieUnseen` rejects riding and being-ridden postures; `ShortcutToLot` rejects households with pets; `VisitCommunityLotHook` presets an empty follower list, which also skips the horse pick (it sits inside the `mFollowers == null` branch). **[verified in code]**
- **Gap (fix V4)**: `GoHomeHook.Unseen` has no pet guard, and `GoHome.Run` adds the horse to `SimFollowers` *after* our hook has decided. A townie that rode its horse to a lot and dismounted can be placed at home while its horse stays on the away lot. **[verified in code; consequence inferred]** Fix: in `GoHomeHook.Unseen`, return false when `sim.Household.Pets.Count > 0` (same rule as `ShortcutToLot`), or narrower, when `sim.LotCurrent.GetAnimalsOfType(HorseAdult | HorseElder)` contains a horse of the same household.
- **Pets following owners**: followers come from `VisitCommunityLot.GetFollowers` and `Sim.GoForWalkWithDog`; trips with followers are never shortcut (`TownieUnseen` follower check). A pet's own trip home is a normal `GoHome` and may be shortcut (posture Standing, not selectable). Fine. **[verified in code]**
- Recommendation: no new hook; add V4.

## 5. Island Paradise (EP10)

- **Planner**: `SetRouteOptions` sets `EnablePlanningAsBoat` for most humans and `EnableWaterPlanning` for child-or-above humans; `RouteAsMermaid` when the whole group is teen+ mermaids (`OccultMermaid.IsEveryOneGroupedWithMeATeenOrAboveMermaid`). Wild animals, strays, unicorns, wild horses get `DestinationMustBeOnLand`. **[verified in code]**
- **Water taxi / own boat**: inside `RoutingComponent.DoRoute`, a boat sub-path pushes `GetInBoatRouteAction.PerformAction` (`Sims3.Gameplay.Routing/GetInBoatRouteAction.cs`): `Sim.GetBoatForGetInBoatRouteAction` takes an owned boat or, for NPCs, `Vehicle.CreateTaxiBoat()` (`BoatTaxiBoat`, `DestroyOnRelease`); a `StandAndWaitController` waits while the water entry point is blocked and **re-plans the whole route** every few cycles (`mOriginalRoute.Replan()`); after boarding, `ContinueAndPopPathAndReplan` → `ReplanFromPoint`. More than 10 water entries in one route → `Sim.SetToResetAndSendHome`. **[verified in code]** This matches F7b's Isla Paradiso profile (`Route_ReplanFromPosition` 10.9%, `StandAndWaitController` 1.5%). **[inferred link]**
- `Boat.RouteToAndGetIn`, `Boat.RouteBoatToLandOnWayToPosition`, `Boat.GetOutOfBoat`, `ReturnToShore`, `GoBoating`, `BoatHere` serve Sims who are boating (already seated, not Standing), mostly player-directed or recreation. Not townie trips; `TownieUnseen` excludes them. **[verified in code]**
- **Swimming** (`SwimHere`, `SwimAroundInOcean`, `Ocean`, `PondManager`): the swimmer is in `SwimmingInPool` posture, so excluded. Mermaid townies standing on land qualify and the shortcut skips their swim leg. **[verified in code]**
- Coverage: cross-water go-home and lot trips are already shortcut when the guards pass (F9: 329/404 go-homes in IP; F10 b2 houseboats). What remains is trips whose origin or destination is visible, households with pets, and houseboat lots that are moving. **Do not hook the boat route actions** (a boat mid-route is a second routing object; placing the Sim strands either the boat or the Sim, the traffic-car lesson). Better lever: the route-failure cache / per-Sim backoff already planned (docs/04 F7b, PLAN), which also cuts the `StandAndWait` replans.

---

## Lessons from the dropped traffic shortcut (F10) applied here

- The traffic hook acted *inside* a route (`Route.PlanToPoint` for a car), on an object that other systems still tracked. Every rejected item above (elevators, boats, subway portals, hover-train action) has the same shape: per-object state (`mAssignedSims`, boat driver/passengers, station use lists, `DestroyOnRelease` taxis) that a teleport would leave behind.
- V1 and V3 act at interaction entry, before any route or portal state exists, like the six working hooks. V2 removes a planning *option* without moving anyone.

## Proposed order

1. V1 `GoToVirtualHomeInternal` early fade (with counters; verify-shortcut on Bridgeport, 0 stuck, Sims-in-world trend).
2. V4 horse/pet guard in `GoHomeHook` (one line).
3. Counter for `CheckAndUpdateRouteForStations`; patch-host test on a static target; then V2.
4. Agent investigation of the Bridgeport elevator-stuck townies (1c); only then V3 and any shell-lot go-home shortcut.
