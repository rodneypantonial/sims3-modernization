# 12. Bridgeport: townies stuck in "Go Home" on community lots

Observation (measured, docs/04 F10, docs/05): in `Baseline-BP` the same townies (Ti Bellino, Wogan Hemlock, Mars Champion, Regina Choi, Brandy Collins, Daniel Jigger, JC Ernst, Travis Fugate, Ryan Atkins, Christian Dunning, ...) show interaction "Go Home", `routing=true`, queue > 0, same spot for minutes, on Eugi's, Waylon's Haunt, East Tradewinds Shipping Co., the cemetery, the art gallery, Aquarius, The Grind and The Brightmore. This happens with and without our shortcuts.

Code paths below are relative to `game-copy/decompiled/Sims3GameplaySystems/` unless stated otherwise. Tags: [verified in code] = read in the decompiled source. [inferred] = not yet confirmed live.

## 1. What the agent's flags mean

- `routing` in `sims_activity` (`mods/ts3agent/src/plugin/Handlers/HarnessHandlers.cs` `SimsActivity`) is `SimRoutingComponent.IsRouting`, which is `RoutingComponent.IsRouting => mRouteActions.Count > 0` (`Sims3.Gameplay.ObjectComponents/RoutingComponent.cs`). It is true whenever a route action is on the Sim's stack, even when the Sim is standing still. [verified in code]
- A route action can stand still while `IsRouting` stays true in these places [verified in code]:
  - `FollowRouteAction.PerformRoute` (`Sims3.Gameplay.Routing/FollowRouteAction.cs`) calls `Simulator.Sleep(uint.MaxValue)` until the native steering wakes it. Script code has no timeout here. If the native path follower stalls, the Sim stays in this state with no limit on the script side. Whether the native side has its own timeout is not known yet. [inferred]
  - `ObstacleEncounteredRouteInterruptAction` and `DestinationObstructedRouteInterruptAction` play the StandAndWait animation through `StandAndWaitController.Run` for up to `SimRoutingComponent.kDefaultStandAndWaitDuration = 50` sim-minutes, and replan every cycle. In `CollectObstacles`, a routing Sim that is itself in a non-exiting StandAndWait does not count as pushable. So two Sims blocking each other in a doorway each wait out the full 50 minutes, then `Terminate`.
  - `PortalComponent` lane wait (`Sims3.Gameplay.ObjectComponents/PortalComponent.cs`, around line 150): `LoopIdle` plus `WaitForExitReason(0.25)` for up to 20 sim-minutes, or `ElevatorInterior.ElevatorGiveUpTime` for elevator doors (the line is `ElevatorDoors.Line`, at most 12 Sims). `ElevatorInterior` resets a Sim after a 30-minute failsafe.
  - Subway: `Subway.SubwayPortalComponent.OnPortalStop` sleeps for the travel time. The Sim is hidden in the rabbit hole at the station meanwhile.
- Not a wait: cars and taxis. `GetInVehicleRouteAction.PerformAction` gets the car from `Sim.GetVehicle`, which for an NPC is `GetNpcVehicle` and otherwise `Vehicle.CreateTaxi`. It places the car at once and fades the Sim in, so there is no "wait for taxi" loop. If there is no car, it returns `Terminate`. [verified in code]
- `CarpoolManager.WaitForCarpool` (`Sims3.Gameplay.Objects.Vehicles/CarpoolManager.cs`) only loops when `RoutingParent is CarpoolWithDriver { CreatedFromCarpoolManager }`, meaning the Sim is in a carpool car, for up to `kMaxCarpoolRouteWaitInMinutes = 360`. The Sim's `RoutingParent` is then the car, and the Sim does not stand on a community lot. [verified in code; ruled out as the cause, inferred]

Time scale: at about 0.33 sim days per real minute (docs/05), 50 sim-minutes is about 6 real seconds. "Minutes without moving" is therefore either an action with no limit (the `FollowRouteAction` sleep) or many bounded waits in a row inside a retry loop. [inferred]

## 2. Ranked causes

### 1 (most likely). The "Go Home" shown is often not `GoHome`; our hook never sees it [verified in code for the mechanism; townie data inferred]

- `Sim.MakeSimGoHome` (`Sims3.Gameplay.Actors/Sim.cs`) only queues `GoHome` (through `MakeSimGoHomeInCarOrBoat`) when `sim.LotHome != null`. Otherwise it queues `Sim.GoToVirtualHome`, which pushes `Sim.GoToVirtualHome.GoToVirtualHomeInternal` as a continuation. Autonomy can also pick `Sim.GoToVirtualHomeMeta`.
- All three of those classes name themselves `Localization.LocalizeString(..., "Gameplay/Core/Lot/GoHome:InteractionName")`, so the agent shows "Go Home" for them too. That this is the same English text as `GoHome`'s own name is inferred.
- Townies in TS3 are mostly homeless (`LotHome == null`), or virtual residents of apartment buildings (`SimDescription.VirtualLotHome`, lots with `HasVirtualResidentialSlots`: a `PublicRoomMarker` plus NPC doors or elevators, `Lot.UpdateVirtualResidentialSlots`). Bridgeport has many of these. [inferred for these Sims]
- `GoHomeHook` (`mods/ts3fixes/src/Fixes.cs`) only replaces `GoHome.Run`, and `Unseen` additionally requires `sim.LotHome == home`. This explains "with or without our shortcuts".
- `GoToVirtualHomeInternal.Run`:
  - If the virtual home has `BuildableShells`, it calls `buildableShells[0].RouteInsideShell(Actor)` (`ShellWithDoor.RouteInsideShell` leads to `MainEntranceDoor.RouteThroughDoor`). Otherwise it calls `Actor.RouteToLot(virtualLotHome.LotId)`.
  - If either fails, it calls `FadeOut(destroyObject: true)` and `Sleep(uint.MaxValue)`.
  - On success it runs an autonomous action on the home lot (`kAutonomuousReactionChanceOnGoingToVirtualHome = 1`, `OnAutonomousDone` calls `MakeSimGoHome` again). It then routes to the NPC door (`GetAssociatedNPCDoor`) or to the nearest `ElevatorDoors`, and fades out.
  - For a homeless townie (`VirtualLotHome == null`), `GoToVirtualHome.Run` picks a random lot, and the internal run calls `PlanToLotEx` + `DoRoute` before fading out.
  - None of these routes has a time limit beyond the route actions in section 1.

### 2. Native follower stall or blocked exit, repeated by the retry loop [mechanism verified in code; Bridgeport geometry inferred]

- The listed venues are night spots (bars, clubs, the art gallery) or odd geometry (the dock at East Tradewinds, the cemetery). They are crowded at night, with narrow doors, stairs and multi-level builds.
- A blocked exit leads to StandAndWait (50 min). It ends in `Terminate`, so the route fails.
- `GoHome.Run` then tries `Target.PlanToLot`, then a water-planning route. That is up to three routes, and each can wait 50 minutes.
- On failure it increments `NumFail`, and `Sim.CanRunAutonomyImmediately` re-queues the NPC at once (docs/04a section 3). The next autonomy pass picks go-home again from the same spot, so to a 3-minute watchdog the Sim looks permanently stuck.
- Two Sims stuck in StandAndWait facing each other make this worse (see `CollectObstacles` in section 1).
- A `FollowRouteAction` that never gets woken by the native side (section 1) has the same symptom with no retries at all.

### 3. Doors that are forbidden or locked on the way out [verified in code; low]

- `SimRoutingComponent.AboutToPlanRouteCallback` forbids all `CommonDoor`s of the current lot when the Sim is outside (or in a public residential room) and the lot is a closed venue (`!IsResidentialLot && !IsOpenVenue()`). `GoToVirtualHome.Definition` is `IAllowedOnClosedVenues`, which lifts this, but `GoHome` is not.
- This only affects Sims that start outdoors, and only makes planning fail fast (route-fail balloon). It does not cause a long stall.

### 4. Ruled out or unlikely [verified in code]

- Carpool wait: see section 1.
- Taxis and NPC cars: no wait (section 1). The traffic-car shortcut is already dropped (F10).
- Subway (`CheckAndUpdateRouteForStations`, NPC chance `kNPCSubwayUseChance = 0.75`): while waiting, the Sim is hidden in the station rabbit hole, not standing on a venue.
- `TryPlanToPostBox`: only applies to user-directed, non-NPC Sims on vacation.
- Elevators: bounded by `ElevatorGiveUpTime` and the 30-minute failsafe reset. They could still be part of cause 2 on the multi-level venues.
- Built-in recovery: `LotManager.RunStuckCheckHelper` only resets Sims at exactly the same position, once a day after 3 AM (docs/04a section 4). A frozen Sim is cleared then; a Sim stuck in the retry loop of cause 2 is not.

## 3. Confirm in the live game (agent, read-only)

For each flagged Sim `s`, read these with `reflect_get` / `reflect_call` or `sim_details`, twice about 30 s apart:

| What | Path | Tells us |
|---|---|---|
| Interaction type | `s.CurrentInteraction.GetType().FullName`; each `s.InteractionQueue.InteractionList[i].GetType().FullName` | `GoHome` vs `Sim+GoToVirtualHome(+GoToVirtualHomeInternal)` / `GoToVirtualHomeMeta` (cause 1) |
| Target | `s.CurrentInteraction.Target` (a Lot: `Name`, `LotId`); `GoHome.SimFollowers.Count`; `GoToVirtualHome.SuccessCallbackForSequence != null` | where it is going; group or callback cases |
| Homes | `s.LotHome`, `s.SimDescription.VirtualLotHome`, `s.SimDescription.Household` (`IsTouristHousehold`, `IsAlienHousehold`, `== Household.ActiveHousehold`) | homeless, virtual, or real home |
| Apartment | `home.BuildableShells.Length`, `home.HasVirtualResidentialSlots`, `home.GetAssociatedNPCDoor(s.SimDescription)`, `home.GetObjects<ElevatorDoors>().Length` | which branch of `GoToVirtualHomeInternal` runs |
| Route action | `s.SimRoutingComponent.GetCurrentRouteAction().GetType().FullName`, `GetPreviousRouteAction()` | `FollowSubPathRouteAction` (native stall) vs `ObstacleEncounteredRouteInterruptAction` / `DestinationObstructedRouteInterruptAction` (StandAndWait; read `.Exiting` / `.IsExiting`) vs `Portal*Action` (read `.PortalObject` type: `ElevatorDoors` with `.Line`, `Door`, `Subway`) |
| Route | `s.SimRoutingComponent.GetCurrentRoute()`: `PlanResult.mType`, `GetDistanceRemaining()`, `GetDestPoint()`, `GetNumPaths()`, `GetPathData(i).PathType`, `GetRouteMetaType()`, `DoRouteFail` | plan state, distance left, vehicle, portal or subway legs |
| Routing state | `s.RoutingComponent.RoutingParent` (null, or the `CarpoolWithDriver` type), `ShouldIgnoreAllObstacles`, private `mRouteActions.Count`, `mbPushRequested` | carpool ruled out; push or obstacle state |
| Posture and exit | `s.Posture.GetType().Name`, `s.Parent`, `s.ExitReason` | in a car or rabbit hole; a pending cancel |
| Role | `s.SimDescription.AssignedRole`, `s.Service` | bartender or other role Sims |
| Nearby Sims | other flagged Sims within 1 m (`x`/`z` from `sims_activity`) | mutual StandAndWait |
| Loop | `GoHome.NumSuccess`/`NumFail` (static) and `ts3fixes.counters[0..3]` deltas; whether `CurrentInteraction` is the same object at both reads (`GetHashCode()`) | same instance = stall (native follower or StandAndWait); new instance = retry loop (cause 2) |

The agent can't find the interaction class from the name alone. The cheapest improvement is to add `itype` (interaction type) and `raction` (current route action type) to `SimsActivity`: two `GetType().Name` reads.

## 4. Fixes (once the cause is confirmed; all are startup method replacements, unseen townies only, game's own end states)

1. **Virtual-home shortcut** (cause 1).
   - Hook `Sim.GoToVirtualHome.GoToVirtualHomeInternal.Run`.
   - Apply only to an unseen townie (`TownieUnseen`, `FarFromViewers(Actor.Position)` and the target), with `Actor.LotCurrent != Target`, a household that is not active, tourist or alien, and no sequence callbacks.
   - It does what the game's own route-fail branch does: `Actor.FadeOut(true, true); Simulator.Sleep(uint.MaxValue); return true;`. This is the same end state the game reaches after every successful virtual-home trip (the Sim is despawned and the SimDescription stays), so what the Sim does next is unchanged.
   - Off-screen it only skips an invisible walk and the arrival reaction.
   - Add the per-Sim loop guard used by `GoHomeHook`. [design]
2. **Stall watchdog** (cause 2, both classes).
   - Use a repeating global alarm (for example every 10 sim-minutes) or the existing per-tick `RefreshViewers` hook.
   - A Sim qualifies when its `CurrentInteraction` is `GoHome` or `GoToVirtualHome*`, `IsRouting` is true, its position has changed by less than 0.5 m since the last check, and that has lasted at least 60 sim-minutes (more than one StandAndWait).
   - It must also be an unseen townie with `RoutingParent == null` and `Posture` neither in a rabbit hole nor in a vehicle, and not in the active household.
   - Then call the game's own escape, `Sim.SetToResetAndSendHome()`. The game itself uses this in `RoutingComponent.DoRoute` after more than 10 water entries. Its `DoReset` path runs `AttemptToPutInSafeLocation(sendHome: true)`, which places the Sim near the front door of `LotHome ?? VirtualLotHome`. Reset clears the queue, and autonomy carries on.
   - Log each use, and count it in a new counter slot.
3. **Failure backoff in `GoHomeHook`** (cause 2, real homes including apartments).
   - When `RunOrig()` returns false for an unseen townie twice within 60 sim-minutes, place the Sim using `Actor.AttemptToFindSafeLocation(true, out pos, out fwd)`. This is the game's own `FindPositionNearFrontDoor(LotHome)`, so it also works for homes with `BuildableShells`, where `SafeSpot` is not used today.
   - Then run the same success bookkeeping as the shortcut.
   - This also covers apartments without writing our own `RouteInsideShell` or elevator code. [design]
4. Do not lengthen or shorten `kDefaultStandAndWaitDuration` or the elevator timeouts. They are shared by every route; a targeted watchdog is safer.

Order: confirm first (section 3). If most stuck Sims run `GoToVirtualHome*`, fix 1 alone should clear them. Otherwise use fix 2, then fix 3. A/B each one with `profile-world --ab` on `Baseline-BP` and count stuck Sims.
