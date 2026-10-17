# 77. NRaas Traveler study: how it starts trips and handles arrival

Research note (2026-10-16). Question: how does NRaas Traveler start trips to University, to the future world (Oasis Landing) and on vacation, and how does it handle arrival? Our agent's `direct: true` trips (docs/19, "`direct: true` for `travel_university` and `travel_future`") stall, and the queued interactions (portal Travel, moving van) get dropped. What should the agent do instead?

Credit: the approach described here is Twallan's NRaas Traveler, now maintained by the NRaas community. NRaas has no license, so this doc describes its behaviour in our own words and copies no code (AGENTS.md "Community code").

Tags: [verified] = read in the Traveler source or in the extracted EA script DLLs (`game-copy/decompiled/`); [inferred] = our reasoning, not yet checked in game. No game run was made for this doc.

## 1. Sources

- **Traveler source:** public on GitHub, `Chain-Reaction/NRaas`, folder `NRaasTraveler` plus the shared `NRaas/Common.cs`. We read commit `2f57186` (2026-07-29). The files sit in `game-copy/nraas-traveler/src/`, which git ignores; nothing from them is committed.
- **Traveler package:** not downloaded. nraas.net returns HTTP 403 to scripted requests, even with a browser user agent, and we found no direct file link. The repo holds no built `.package`. So we could not check that the source matches the released 1.67 build byte for byte. The repo is the community's current tree, and every point below concerns long-standing travel code, so we treat it as representative [inferred].
- **Our install:** `Mods/Packages` holds NRaas ErrorTrap and Overwatch, not Traveler. During our test runs the game uses EA's own travel and arrival code.

## 2. Two root causes for our stalls

### 2.1 University: arrival waits for the housing pick (the main finding)

[verified, EA code] When the travel switch leaves a world, the game sends the next world to one of two in-world sub-states. If the destination is University and the Sims are not coming back from University, it picks **PlayFlow**, the "choose a lot" map screen. Every other trip goes to **LiveMode**.

In PlayFlow the play-flow model sees a travel household in an EP9 game and starts the "University travel" flow:

1. A camera fly-through runs at normal speed.
2. The map shows tags for dorms, empty houses and, by household gender, fraternity or sorority houses.
3. The game then **waits for the player to click a lot and confirm**.

Confirming calls the play-flow model's "play with travel household" step for that lot:

1. A mini loading screen.
2. Sims are kicked off the lot and the lot is locked as active.
3. For a dorm or apartment, the household moves in as roommates; for a house, it moves in normally.
4. A one-shot task selects a Sim, makes sure the lot objects exist and goes to LiveMode.

Only LiveMode's startup calls `GameStates.OnArrivalAtVacationWorld`, and that call clears `sIsChangingWorlds`.

This is what we see: "University reached, `in_travel` true, no active household for 20 min". **The arrival is not broken. It waits for input the agent never gives.** The van path would stop at the same screen, because the moving van ends in the same `TravelToVacationWorld` call.

[verified] Traveler does not automate this screen. Its PlayFlow override only keeps the "EP venues placed" flags across a travel load, so the "place required venues" wizard does not reappear. Players with Traveler still pick their housing by hand.

[verified] PlayFlow's startup also runs the required-venue placement check. On a world that lacks EP venues this can open a two-button prompt, which `auto_dialog` has to answer.

### 2.2 Future: `sIsChangingWorlds` is set before the step most likely to throw

[verified, EA code] `GameStates.TravelToVacationWorld` runs in this order:

1. Stop the operations that cannot travel.
2. Create `sTravelData` and set `sIsChangingWorlds = true`.
3. Fill in the trip data.
4. For FutureWorld only: `CauseEffectService.PreFutureWorldLoadProcess`, then `FutureDescendantService.BuildDescendantHouseholdSpecs`.
5. Queue the one-shot `SwitchWorlds` task.

EA guards none of these steps.

If step 4 throws, the flag stays set and `SwitchWorlds` is never queued. That is exactly the stall we saw: `in_travel` true and no world switch.

[verified] `TimePortal.TravelToFuture` has no try/catch, so the exception reaches our `RunAsync` delegate. Our handler calls it by reflection, so the exception arrives as a `TargetInvocationException`. `RunAsync` logs only the outer type and message, which is the generic "Exception has been thrown by the target of an invocation", in `async_log`. The real cause is hidden.

[inferred, to confirm] The descendant build is the likeliest thrower.

- Traveler replaces it with a guarded version. That version first drops descendant-household records whose progenitor count is not 4 (they move to the invalid list), then wraps the whole build in a try/catch.
- `portal_prepare` fakes the end state of the time-traveler chain on test saves. It does not create the future-world service data that the real chain and the intro would leave behind.

The ErrorTrap log written 36 s after the 2026-10-16 command (docs/19) fits a caught script exception, but we did not read it.

## 3. Traveler's call sequences

### 3.1 Shared pattern [verified]

Traveler replaces EA's travel interactions with its own subclasses. It injects new interaction definitions over EA's tuning, so the same menu entries run its code. Each trip interaction then follows the same pattern:

1. **Pre-checks.** Traveler runs its own `FinalBoardingCall`: one fail-reason check per traveller, with a configurable filter, and it returns the reason text. EA's private reason check is not used.
2. **Interaction body.** Routing, animations and the van or car run as in EA.
3. **Deferred trigger.** The interaction does **not** start the world switch itself. It creates an NRaas alarm task for **2 sim-minutes later**, then (by default) saves the game. When the alarm fires, a fresh one-shot script task does the boarding work: kill the visitor situations, set the map view, tidy reserved vehicles, then Traveler's own `TravelToVacationWorld`. That task is added with `Simulator.AddObject` and runs on the sim thread outside any interaction.
4. **`PlayerMadeTravelRequest`.** It is set during the interaction and cleared in the interaction's `finally` block. Traveler's `SwitchWorlds` clears it again on entry.
5. **No `Gameflow.DisableSave`.** EA's portal and van interactions disable saving during the trip. Traveler comments those calls out and makes its own save before the trip instead (setting "prompt to save", on by default). The save temporarily clears `PlayerMadeTravelRequest` and waits for the save to finish.

### 3.2 Traveler's `TravelToVacationWorld` and `SwitchWorlds` [verified]

They follow EA's order, with these differences:

- **Order of the future-world step.** Traveler runs the future-world preparation (cause-effect pre-load and its guarded descendant build) *before* it creates `sTravelData` and sets `sIsChangingWorlds`. A failure there cannot leave the flag set. EA's order (§2.2) can.
- **Second hop.** If a trip is already running (a hop from one vacation world to another), Traveler keeps the home world, home time, real-estate manager and "days since last visit" from the old travel data instead of overwriting them. It also skips the future-world preparation on that second hop.
- **Day counter.** Traveler sets the current day of the trip to 1; EA sets it to 0.
- **Cross-world bookkeeping.** Before it queues `SwitchWorlds` it records the travellers for its cross-world retention and storage.
- **Its own `SwitchWorlds`.** It is EA's sequence with each stage in its own try/catch and logged, plus cleanup of broken opportunities and skills and the "store vehicles" option. Traveler's MiniSim and household export replace EA's. A load-file failure turns the loading screen and scene drawing back on and clears the travel flags; EA's version just returns with the loading screen still up. On an exception Traveler also restores the scene and unloads the loading screen. EA swallows the exception and still goes to TravelDeparture.

### 3.3 Per trip

| Trip | Traveler's sequence [verified] | Differences from EA |
|---|---|---|
| **Vacation** (computer, phone or rabbit hole) | Its own trip-planner dialog, then EA's `TravelUtil.TriggerTravelToVacationWorld` queues the car interaction. Traveler's car interaction pays, routes the group to the farthest lot (none when the target is the world lot), then sets the 2-min alarm and saves. The alarm runs Traveler's `FinalBoardingCall` and Traveler's `TravelToVacationWorld`. | EA runs the boarding call and `TravelToVacationWorld` inside the car interaction's `Run`. |
| **University** | Enrolment through Traveler's academic-career version, which uses its traveller filter. EA's `TravelUtil.TriggerTravelToUniversityWorld` queues the moving van. Traveler's van interaction pays the tuition, routes the group to the gathering point, creates the van and the suitcase, walks to the lot, then sets the 2-min alarm and saves. The alarm runs Traveler's `FinalBoardingCall`, the visitor-situation kills, map view and Traveler's `TravelToVacationWorld(University, ids, length, daysSince)`. The trip length is scaled by Traveler's term-length setting (7 days by default, the same as EA). | EA triggers inside the van's `Run`. Arrival is EA's PlayFlow housing pick in both cases (§2.1). |
| **Future** | Portal Travel interaction: tutorial exit, broken check, confirm dialog, `PlayerMadeTravelRequest = true`, opportunity cancels, route into the portal slot, the `kTravelToFuture` event, transform-buff removal, enter animation, Future skill. Then Traveler's `TravelToFuture`: Traveler's `FinalBoardingCall`, then a 2-min alarm task plus the save. The alarm adds the actor to the guids, kills both visitor situations, sets the map view, tidies reserved vehicles, stops the portal FX, runs Traveler's `TravelToVacationWorld(FutureWorld, guids, 0, 0)` and then `SetDepartureTimePortal`. The interaction finishes its exit animation normally. | EA runs all of this inside `Run`, before the exit animation, with EA's unguarded descendant build and save disabled. Traveler also sets the portal Travel tuning to "disallow autonomous" and allows teens. |
| **Return from future** | In the future world, the portal interaction sets a 2-min alarm that runs Traveler's return-from-vacation routine, then saves. | Same deferral pattern. |

### 3.4 Arrival [verified]

Traveler replaces the game states: TravelArrival, TravelDeparture, InWorld, LiveMode, PlayFlow and others. Its LiveMode startup calls its own `OnArrivalAtVacationWorld` instead of EA's. If travel is flagged but `sTravelData` is null, it clears the travel statics instead of crashing. Its arrival handler does the following:

1. Restores the travellers' relationships.
2. Places the Sims with its own home locator. It prefers the base camp, then a residential lot. If it finds no lot, it opens Edit Town. Then it selects a Sim and sets normal speed.
3. For the future world, runs its guarded descendant post-load step and EA's cause-effect post-load step.
4. Sends the "Sim entered vacation world" event and runs the special story progression.
5. Sets the trip state to OnVacation.
6. Clears `sIsChangingWorlds` in a `finally` block, so a failure cannot leave the flag set.

University arrival still goes through EA's PlayFlow housing pick (§2.1). The future descendants are rebuilt by the guarded post-load step. Earlier, during the travelling-household import in its InWorld state, Traveler restores the household's stash, vacation alarms and spawners (its version of EA's vacation-arrival step).

## 4. Recommendation for our agent

All of this runs on the sim thread inside a script task (`Simulator.AddObject` of a one-shot task), never inside an interaction and never off-thread. That is where Traveler's deferred trigger runs too. Do not depend on queued interactions: autonomy and exit reasons drop them, and Traveler gets no help there either (it relies on the player pushing them).

1. **Report the real exception.** In `RunAsync`, unwrap `TargetInvocationException.InnerException` and log its type, message and the first stack lines. Read `async_log` after every direct trip before calling it a stall. This is cheap and settles §2.2.
2. **Future, direct.** Do not call EA's `TravelToVacationWorld` with its unguarded descendant build. Copy Traveler's order instead [inferred, to verify]:
   - (a) Run `FinalBoardingCall` and the boarding side effects as now.
   - (b) In a try/catch, run `CauseEffectService.PreFutureWorldLoadProcess` and the descendant build. If the build throws, record the inner exception and stop the trip, with the flags not yet set. A guarded rebuild like Traveler's is a later option, not a port.
   - (c) Fill `GameStates.sTravelData` yourself the way EA does (§2.2 fields), set `sIsChangingWorlds`, and queue EA's private `GameStates.SwitchWorlds` by reflection as a one-shot task.
   - (d) If (c) throws, clear `sTravelData` and `sIsChangingWorlds` so the game is not left half-switched.

   Simpler first try: keep the current call, but run it from a fresh one-shot task, with the game unpaused and no interaction on the actor, then read the unwrapped exception. If the descendant build is the culprit, (b) is required.
3. **University, direct.** Keep `UniversityDirect` as it is (it already matches the van's trigger step). Add an **arrival step**, either a new `uni_move_in {lot?: "dorm"}` command or an automatic step at the end of `travel_university`:
   - Wait until the game is in the PlayFlow sub-state (the HUD model reports the play-flow state) and the play-flow model is initialized (the fly-through has ended).
   - Answer any venue-placement prompt with `auto_dialog`.
   - Pick a lot: by default the first dorm lot (`Lot.IsDormitoryLot`) in the world, or a given lot id.
   - Call `PlayFlowModel.Singleton.PlayWithTravelHousehold(lotId)` from a one-shot task.
   - Done when `travel_state` reports live mode, an active household and `in_travel` false.

   A dorm avoids the house rent panel. We did not find where rent for a rented house is charged, so houses stay unsupported until checked [inferred]. Extend `travel-trip.ts --to university` to send this step after the world loads.
4. **Vacation.** The current queued car path works in EA's shape. If it gets dropped, add a direct variant built like Traveler's deferred trigger: `FinalBoardingCall`, then `GameStates.TravelToVacationWorld(world, ids, days, daysSince)` from a one-shot task. Arrival goes through LiveMode and needs no step from us.
5. **Saves.** Do not call `Gameflow.DisableSave`. Traveler skips it. Any save during the switch belongs to our harness, which already saves around trips (`--save-stats`).
6. **Repro and ledger.** Add the PlayFlow claim (§2.1) and the descendant-build claim (§2.2) to the docs/51 ledger. Verify them with one University run (`direct: true` plus the move-in step) and one future run with the unwrapped exception logged.

## 5. Open points

- Whether the descendant build (or `PreFutureWorldLoadProcess`) actually throws on our `portal_prepare` test save: one run with the unwrapped exception settles it.
- Whether `PlayWithTravelHousehold` runs safely from a one-shot script task. The UI calls it from its own click handler, also on the sim thread; its mini-load callbacks queue their own one-shot task [inferred safe].
- Where rent for a non-dorm University house is charged (not in the play-flow move-in path we read).
