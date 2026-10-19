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

Read 2026-10-18 for the `travel-all` future job (build be4267a): its ErrorTrap file (`ScriptError_..._10-16-2026__20-54-54`, 46 s after `travel_future`, kept in the probe's `events.jsonl`) holds only ErrorTrap's correction log (`Destroyed Object Found` for dishes, mirrors, a bathtub and a `UniversityWelcomeKit`) and no exception [verified]. So that run gives no evidence for the §2.2 throw; the trip stayed in Bridgeport with `in_travel` set while the Sims went on with autonomy (the harness since turns autonomy off for the traveller and fails after 90 s without progress).

### 2.3 University: how a real trip runs, and why our trip home hung (2026-10-19)

**What the game expects (player view).** Sources: the reviews and player reports listed at the end of this section [community-reported]; the code points below are [verified, EA code] in `game-copy/decompiled/`.

1. **Enrolment.** From the phone or a computer ("Enroll for University Term"), a young adult or adult picks one of six degrees (Business, Technology, Science, Communications, Fine Arts, Physical Education), the course load (credit hours) and the term length: 1 term = one Sim week, 2 terms = two Sim weeks. Tuition is per credit; scholarships and a need-based grant come off it. A degree needs 48 credits. More than one household Sim can enrol; the others stay home and see the students off.
2. **The trip.** The students walk to a moving van and the world switches to University (our `travel_university` does the enrolment's accept path and queues that same van, see `TravelHandlers.cs`).
3. **Arrival and housing.** The game opens the "choose a lot" map (§2.1): dorms, empty student houses and the Greek houses. Dorm and apartment Sims move in with NPC roommates, who join the active household as roommates (`RoommateManager`); a house is rented. Our `uni_move_in` confirms the first dorm the way the map's confirm does (`PlayFlowModel.PlayWithTravelHousehold`). The 2026-10-18 run shows it works: `uni_move_in: Angelista -> Henry Hall`, live mode with the household, a clean save in University.
4. **Staying.** Students attend classes (the academic career); grades come from attendance, homework and class performance. On the last day of the term the report card arrives (Saturday morning), the household gets "one day / half a day / one hour left" notices, and each student's term is completed.
5. **Leaving.** Two legitimate ways home:
   - **Term end:** the household's term-end alarm starts the return flow: every non-roommate household Sim travels; the NPC roommates stay and see them off. The students' academic careers already ended with the term.
   - **Early:** the phone or computer "Leave University" call (a yes/no "drop out" dialog). For each chosen student it **drops out first** (scholarship money paid back, the term completed with grade 0, the academic career ends), then sends the non-roommates home and makes the roommates (and any student who stays enrolled) see them off. Dropping out loses the term; earlier credits stay [inferred, not checked].

**Why our trip home hung (2026-10-18 `bp-travel-university`, 23:56-00:04 PHT).** The move-in had worked. `travel_home` then called the game's "travel home from University" step with **every** household actor as a traveller and nobody staying: Tiara plus the five NPC dorm roommates (the log's `Holly:Meet Up, Kaz:Meet Up, ...`), and **without dropping Tiara out**. The moving van's run checks whether any non-roommate household Sim still has an academic career. Tiara still had hers, so the van took the **early single departure** path, which exists for one student going home while housemates keep studying: the student is removed from the household, faded out and parked as an early departure, with **no world switch**. The roommates did the same. Result: University loaded, `in_travel` false, no active household, nothing else ever happens: the 8 min of `queue no active household` and the `set_motive household: no active household` errors. The NullReferenceException ErrorTrap logged at 23:56:23 PHT (`SimDescription.IncrementLifetimeHappiness` from the mood update) fits Sims ticking after they left their household [inferred]. The 2026-10-16 hang was the other step: the arrival map, before `uni_move_in` existed (§2.1).

**Fix (agent, `travel_home` in University).** Do what "Leave University" does after its dialog, on a game task: `AcademicDegreeManager.DropOut` for each household Sim still in an academic career, the HUD world-change note, then the travel-home step with travellers = the household minus NPC roommates, staying = the NPC roommates. The game then shows its "term over" message (the harness's dialog handling answers it) and returns home. It fails on its game task (in `async_log`) if a Sim is still enrolled after the drop-out. A term-end return (waiting out the term) is the other legitimate route; it needs a full term of game time, so the test uses the early one.

**Harness.** Two no-progress rules now cover this state (`tools/harness/save-matrix.ts`): `tripStall` also fires for "world loaded, no switch running, no active household" (90 s), and `staleClock` fires when the trip status line (travel_state plus queues) stays the same for 120 s while the world is loaded (reset by loads, travel saves and the University arrival grace). A failure ends with the full `travel_state`; the queue then closes the game and moves on.

Common player reports of "arrived with no Sim / no household" or "student came home alone" [community-reported]: travel with a broken or overloaded save, mods interfering with travel, and a missing housing pick. NRaas suggests Traveler plus Overwatch and ErrorTrap, which does not automate the housing pick (§2.1).

Sources:
- State Hornet review (2013): degrees, 48 credits, housing choices: https://statehornet.com/2013/03/the-sims-finally-bring-the-college-experience-with-new-expansion-university-life/
- GameChronicles review (enrolment: major, units, term length 1 or 2 weeks): https://gamechronicles.com/the-sims-3-university-life-limited-edition-review-pc
- Steam discussion (report card Saturday morning, term = one or two Sim weeks, graduation at 48 credits): https://steamcommunity.com/app/47890/discussions/0/2269194081611956196
- EA forums, Sims missing on University arrival / UI with no Sim on return: https://forums.ea.com/discussions/the-sims-3-technical-issues-en/sims-3-university--sims-wont-load/8295423 and https://forums.ea.com/discussions/the-sims-3-en/sim-disappearing-upon-returning-from-university/11945131
- EA forums, cannot leave University / term never ends: https://forums.ea.com/discussions/the-sims-3-technical-issues-en/cannot-leave-university/8320136 and https://forums.ea.com/discussions/the-sims-3-technical-issues-en/re-sims-3-uni-term-never-ends-sims-stuck-for-weeks-in-college/8290677/replies/8290687
- EA product sheet (University Life press release, housing and roommates): https://s204.q4cdn.com/701424631/files/doc_news/2013/03/1/744808.pdf


**Fix verified live (2026-10-19 03:16 PHT, `bp-rare-travel-university`, PASS in 4.9 min):** move-in to Henry Hall after the fly-through wait, away save OK (8.0 s), then `travel_home` via `LeaveUniversity` (DropOut + TriggerTravelHomeFromUniversity) queued Travel Home for the student and See Student Off for a roommate, the travel save ran (7.1 s) and the home save loaded back in NewDowntownWorld (10.0 s). No stall. Second clean run 2026-10-19 04:38 PHT (`bp-travel-university`, travel-all queue, PASS in 5.0 min): **fixed** (2 clean runs).

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
