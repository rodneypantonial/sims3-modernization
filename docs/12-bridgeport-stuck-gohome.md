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

Status 2026-10-13: fixes 1 (`VirtualHomeHook`) and 2 (`StuckWatchdog`, 60 sim-min going home / 120 routing, once per Sim per sim-day) are built (`mods/ts3fixes/src/RouteFixes.cs`); fix 3 is not.

## 5. Apartments, penthouses and elevators (code review 2026-10-13)

Question: do Bridgeport townies who live in apartments or penthouses stand stuck in "Go Home" because they cannot route into their building? Tags: [verified] = read in the decompiled source, [community] = with link, [measured] = from repo logs, [inferred] = not confirmed.

### 5.1 How each kind of resident goes home

| Resident | Interaction | Route target | Elevator involved |
|---|---|---|---|
| Real household, home lot is an apartment building with NPC units (`LotHome.HasVirtualResidentialSlots`: residential lot with a `PublicRoomMarker`, `Lot.cs` ~5303) | `GoHome` | `Lot.PlanRouteIntoLot` (`Lot.cs` ~9006): the first time per lot it picks a portal (door, not elevator) between a public and a private room on the **lowest** non-negative level (`IsBetterLocation`), caches it in `mBestRouteIntoLotLocation` (until `BestRouteIntoLotLocationValidated` is cleared, ~8733), and plans to within 1-3 m of it | Only if no such door exists: then the best `ElevatorDoors` whose slot is in a private room off the lobby level (`PrivateBetterElevatorDoorTestCheck`), so the route rides the elevator [verified] |
| Real household in a penthouse or shell building (`LotHome.BuildableShells.Length > 0`, no virtual slots) | `GoHome` | `PlanRouteIntoLot` falls through to `FindFrontDoor()`, the routing slot of the front door that is not outdoors; if that door is on an upper floor the route goes through the shell's `MainEntranceDoor` and an elevator. Falls back to the mailbox, then `PlanToLotEx` [verified] | If the front door is upstairs [inferred per lot] |
| NPC virtual resident (`VirtualLotHome`, `LotHome == null`) | `GoToVirtualHome(Internal)` | `BuildableShells[0].RouteInsideShell` (`MainEntranceDoor.RouteThroughDoor`) or `RouteToLot`; failure = fade out. On arrival one autonomous action on the home lot, then the NPC unit door (`GetAssociatedNPCDoor`) or a plan to every `ElevatorDoors` slot, then fade out [verified] | Only walks *to* the elevator slot, never rides. A non-service NPC may use an elevator only through doors in a public room (`ElevatorDoors.CanTravelInElevator` returns `IsInPublicResidentialRoom`); a real resident always may (`IsOnHomeLot`) [verified] |

Bounds on the elevator part (section 1): lane wait in `PortalComponent` up to `ElevatorInterior.ElevatorGiveUpTime`, at most 12 Sims in `ElevatorDoors.Line`, a 30-minute failsafe reset. A failed route ends `GoHome.Run` with `NumFail++`. Nothing in the apartment path waits without a limit [verified].

Our hooks on these paths [verified in `mods/ts3fixes/src`]:
- `GoHomeHook.Unseen` returns false when `home.BuildableShells.Length > 0`, so **shell apartments and penthouses are the only unseen townie homes that still get the full walk** (and every elevator ride on the way).
- `VirtualHomeHook` fades every unseen non-tourist, non-alien virtual resident whose home is unseen, so the `GoToVirtualHome*` path should no longer stall unseen Sims.
- `StuckWatchdog` calls `SetToResetAndSendHome()`. With the send-home flag, reset runs `AttemptToFindSafeLocation(forceHome: true)`, which calls `FindPositionNearFrontDoor(LotHome ?? VirtualLotHome)`: a safely routable point inside the home, near `FindFrontDoor()`, so a penthouse resident lands inside the penthouse. If that fails, `AttemptToPutInSafeLocation` puts the Sim at `lot.Position` of `LotHome ?? VirtualLotHome ?? LotCurrent`, which is the lot's corner (`Sim.cs` ~54465-54480). For a homeless townie that is the **corner of the lot it is stuck on**, which can itself be unroutable [verified; consequence inferred].

### 5.2 Does the apartment theory fit the data? Mostly not

- Every recorded stuck "Go Home" Sim stands on the lot it is leaving, not on its home lot [measured: `probe-out/stuck-sims.jsonl`, `probe-out/profile/bp-mod-stuck.json`]. An elevator or entry stall at an apartment would show the Sim on the apartment lot (the elevator lane, lobby or shell door). The only residential-lot entries are Jason Westfall on "Rhapsody in Blue - 1br, 1ba" and Mars Champion on "Bachelor Tower" (not going home). [inferred]
- They do not move for many sim-hours [measured: `probe-out/watchdog.log`]. On 2026-10-11 the watchdog started at 15:19 (day 15 12:31), and 13 Sims were flagged on the third check at 15:22 (day 16 05:19). So they were stuck from the first minute after load, about 17 sim-hours.
  - That is longer than any bounded wait in section 1.
  - It points to a saved state that never recovers: a Sim in an unroutable spot, a native follower that is never woken, or a retry loop that starts again at the same spot. It is not a 20-minute elevator wait. [inferred]
- They line up along streets [measured]: x≈298 (The Grind z 1252-1262, Hogan's Deep-Fried Diner lot z 1489-1502), x≈629-632 (Eugi's, The Brightmore, Bridgeport Acres, the art gallery) and z≈1592-1595 (The Brightmore). That looks like sidewalk or lot-edge geometry on those streets (a lot entry point, or a sidewalk segment cut off from the network), not anything inside a building. [inferred]
- Bridgeport elevators are a known stuck source for unseen Sims, and NPC-only units on the ground floor trap residents coming out of hibernation: NRaas staff on Steam, https://steamcommunity.com/app/47890/discussions/0/3133918821790545121 [community]. In code, the ground-floor case matches `PlanRouteIntoLot` preferring the lowest-level door: if that door belongs to an NPC-only unit, every resident plans to it [inferred].
- Which stuck Sims live in apartments is not known: no repo capture holds household -> home lot with shell or virtual-slot flags. Section 5.4 reads it live.

Conclusion:
- Apartments and penthouses are a plausible minor source: shell homes still get the full walk plus elevator rides, and there is the NRaas ground-floor case. Confidence that they are the main cause: low.
- The recorded stuck Sims are stuck at their **origin**, along two streets, from load onward. Confidence that the main cause is on the origin side (where they stand, a saved stuck state, possibly the route-into-lot entry point): medium.
- Confirm with section 3 plus 5.4.

### 5.3 Fix design, safest first

1. **Watchdog tweaks** (no new teleport kinds; uses the game's own reset):
   - (a) Allow a second reset per Sim per sim-day when the Sim is stuck again at a different spot. Today it is once per day, and Wogan Hemlock was stuck on both days of the bp-mod run.
   - (b) Homeless townies (`LotHome == null && VirtualLotHome == null`) and virtual residents: instead of `SetToResetAndSendHome`, which can drop a homeless Sim on the corner of the current lot, end the trip the game's own way. Either `FadeOut(true, true)` from the Sim's own interaction, or reset it and let `VirtualHomeHook` fade it on the next trip. Off-screen only.
   - (c) Log the home kind on each reset (`BuildableShells.Length`, `HasVirtualResidentialSlots`, `ElevatorDoors` count, front-door level) to `ShortcutLog`, so one run answers 5.2.
   - Risk: low. These are end states the game already uses.
2. **Shell-apartment arrival in `GoHomeHook`** (only if 5.4 shows shell or penthouse residents among the stuck):
   - Lift the `BuildableShells` guard only when `Actor.AttemptToFindSafeLocation(true, out pos, out fwd)` succeeds. That is the game's own `FindRoutablePointInsideNearFrontDoor`: inside the home, near the front door, checked with `IsPointSafelyRoutable`. On a penthouse level it lands inside the penthouse.
   - Also require that the point is on `LotHome` and that the Sim and the home are unseen. Keep the existing pet, follower, active-lot and loop guards.
   - Then run the same lead-in and success bookkeeping as today.
   - Skip homes that also have virtual residential slots (NPC units sharing the lobby), so we never pick a spot in an NPC unit.
   - Risk: low-medium. Placement inside a home is what the game's reset already does.
3. **Arrival through the building's entrance or elevator**: do not build. Our own `RouteInsideShell` or elevator code would need elevator line and portal bookkeeping, which strands Sims when it is skipped (docs/10 rule, same as traffic cars).
4. **Origin-side fix** (if section 3 shows the Sims are frozen in place from load): a one-time pass right after the world loads.
   - For each unseen townie whose position fails `IsInSafeLocation()` or `IsPointInLotSafelyRoutable`, call `AttemptToPutInSafeLocation(false)`, the game's own placement on the current lot.
   - Risk: low. This is the right fix if the save holds Sims in unroutable spots.

### 5.4 Repro design (for `repros.json` once the Sim is named)

Find an affected resident first (agent, read-only, one-off):
1. `list_households`: read `home_lot` per household.
2. For each home lot, go through a member Sim with `reflect_get`: `@obj:<sim id>.LotHome.BuildableShells.Length` and `.HasVirtualResidentialSlots`. Use `reflect_call` `GetObjects` to count `ElevatorDoors`.
3. Pick one adult townie from a shell or penthouse home, and one from a virtual-slot building with elevators.

Entry, using the stuck Waylon's Haunt spot that `bp-waylons-gohome` already uses:

```json
{"id":"bp-apartment-gohome","bug":"Townies living in Bridgeport shell apartments or penthouses stand stuck in Go Home (GoHomeHook skips BuildableShells homes; elevator rides at the home)","fix":"docs/12 §5.3 fix 1 (watchdog home-kind logging and second reset), then fix 2 if confirmed","save":"Baseline-BP","steps":[{"cmd":"place_sim","args":{"sim":"<shell/penthouse resident>","x":411.9,"z":1344}},{"cmd":"sim_go_home","args":{"sim":"<shell/penthouse resident>"},"waitS":2},{"cmd":"place_sim","args":{"sim":"<virtual-slot building resident>","x":412.9,"z":1345},"optional":true},{"cmd":"sim_go_home","args":{"sim":"<virtual-slot building resident>"},"optional":true}],"watch":{"kind":"stuck","sims":["<shell/penthouse resident>","<virtual-slot building resident>"],"minutes":5},"expect":"both reach their home lot (inside the building) or are reset onto it; none stuck on Waylon's Haunt or in an elevator line","area":[411.9,1344]}
```

- Run it once with `patches {gohome:false, watchdog:false}` to see the vanilla behaviour, then with both on.
- Read `ShortcutLog` and `ts3fixes.moved` for where the Sim ended up.
- `sim_go_home` calls `Sim.MakeSimGoHome`, so a real resident gets `GoHome` (the hook path) and a virtual resident gets `GoToVirtualHome`.

### 5.5 Discovery result (2026-10-13, Baseline-BP, read-only `reflect_get` over `Household.sHouseholdList`)

Of 55 households, 30 have a home lot; **22 of those homes are shell apartments or penthouses** (`BuildableShells.Length` 1, `HasVirtualResidentialSlots` true), e.g. Clavier (Bridgeport Arms), Hemlock (The Tidewater), Bumble (The Simerset), Jones (One Leo Center), Talon and Striker (Hieroglyph Condos), Rake (Bachelor Tower), Maloney (Maloney Tower), King (Glenview Manor) [verified].

Matched against the Sims in our stuck repros (bp-hogans-gohome, bp-hogans-pileup, the bp-mod run): **Wogan Hemlock, Wayne Bumble and Johnny Jones live in shell apartments**, the homes `GoHomeHook` skips, so they still walk home through the building; **Suzy Strummer, Lilly-Bo Chique, Marie Desmarais and Paul Hogue have no household home lot** (homeless or virtual residents, the `VirtualHomeHook` and watchdog path) [verified]. So both causes in §5.1-5.3 are real: shell-apartment residents (fix 2: unseen -> game's own safe spot inside the home) and homeless/virtual townies (fix 1: watchdog uses the game's fade-out). The repro `bp-apartment-gohome` can use Wogan Hemlock (The Tidewater) and Wayne Bumble (The Simerset).

**Built (2026-10-13): §5.3 fix 1** in `StuckWatchdog` (`mods/ts3fixes/src/RouteFixes.cs`, switch `watchdog`): (a) a second action the same sim-day only when the Sim is stuck again more than 10 m from the first spot; (b) a townie with no household home lot (homeless or virtual resident, not tourist or alien) is faded out and removed (`FadeOut(false, true)`, the end state of `GoToVirtualHome`) instead of a send-home reset; (c) every action logs the home kind (none/virtual, shell apartment, house, plus the lot name) to `ShortcutLog` and `ts3fixes.moved`. Counters [33] none/virtual, [34] shell apartment, [35] house, [36] second actions (agent `patches`: `watchdog_home_none/shell/house`, `watchdog_second`). Repro `bp-apartment-gohome` (Wogan Hemlock, Wayne Bumble) is in `tools/harness/repros.json`. Not yet measured in game; fading from the watchdog task (not the Sim's own interaction) is [inferred] safe, as the game also destroys Sims from other contexts (`GameStates`).

### 5.6 Fix 1 in game (2026-10-13, Baseline-BP)

`bp-apartment-gohome`: PASS with the watchdog off (both shell residents walked home on their own from Waylon's Haunt, so the apartment case does not reproduce from that spot) and PASS with it on [verified]. In the ~5 min of the second pass the new watchdog acted 8 times world-wide: 5 homeless/virtual townies faded out, 2 shell-apartment residents and 1 house resident reset, 2 second actions at a new spot; no stuck Sims [verified, `patches` counters 31-36]. Fix 2 (shell residents to a safe spot inside the home) stays on hold until a shell resident is seen stuck.
