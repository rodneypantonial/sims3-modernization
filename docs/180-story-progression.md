# 180. Story progression for story runs: EA's built-in vs NRaas StoryProgression (and MasterController)

Written 2026-10-23. Static only: decompiled EA scripts in `game-copy/decompiled` (read only), the NRaas source (the local sparse
clone has only `NRaas/`, `NRaasOverwatch/`, `NRaasEconomizer/`; StoryProgression was read in memory from the clone's own origin,
a fork of the public NRaas repo at `master`, `VersionStamp.sVersion` 269), the installed Mods folder listed read only, community
pages. No game run, no mod installed. Tags as AGENTS.md: **[verified]** (read in code or listed), **[community]**, **[inferred]**.
NRaas and EA code are described in our own words; nothing is quoted.

Rodney: let other households live off camera the way the game does; study NRaas StoryProgression before building our own (build
less), but make sure it is not buggy or hitchy like Overwatch (docs/145) and ErrorTrap (docs/146); drive more Sims only in
player-realistic ways. Coordinator addition: study NRaas MasterController's "Allow More Than Eight" for 12-16 and 20 Sim households.

## 1. Verdict

**Use EA's built-in story progression** (as patched by simler90's core mod, which replaces `Sims3GameplaySystems` on this install,
docs/128). Story queues now run with it on; timing queues keep `-disablestoryprogression`.

| Option | Installed here | Hitch pattern (static) | Verdict |
|---|---|---|---|
| EA built-in | yes (part of the game; simler90's copy is the one loaded [verified MVIDs, docs/128]) | one alarm a day at 1:00; 4-10 actions with `Sleep(0)` between them; each action's demographic collect is one tick over all lots and households (§2.3) | **use**; measure the 1 AM ticks with the new attribution (§4) |
| NRaas StoryProgression | **no** [verified: 163 package files, only `NRaas_ErrorTrap`, `NRaas_Overwatch` and simler90's core] | repeating 10 sim-minute alarm into its own task, work sliced at 100 ms (longer than a frame) all day; one world-wide object scan without a yield; 117 catch-alls (§3) | **no**: not installed (we never install mods), more steady sim-thread work than EA's once-a-day run, and simler90's guide calls it redundant [community, docs/07 §2.1] |
| NRaas MasterController | **no** [verified, same listing] | source not in the clone; the shared NRaas framework it is built on has load hooks, timers and 265 swallowed-exception sites (§3.3) | **no**: not installed; "Allow More Than Eight" lifts the cap only for MC's own commands [inferred + community]; the 12-16 and 20 Sim tiers are not built |

## 2. EA's built-in story progression [verified unless marked]

### 2.1 Schedule and toggles
- `StoryProgressionService` adds one global, never-persisted alarm for the next 1:00 AM (`ScheduleInfrequent`): one run per sim
  day. Accelerated mode (`storyprogression Accelerated` cheat) fires every sim minute. One `StoryProgressionTask` at a time.
- A run does `kActionsMin`..`kActionsMax` = 4-10 action attempts, `Simulator.Sleep(0)` between attempts and per candidate while
  scoring.
- On only if the option `EnableStoryProgression` is on, the `-disablestoryprogression` switch is absent and there is no edit-in-game
  switch; the Options toggle and the cheat call `StoryProgressionService.SetMode` (Disabled / Enabled / Accelerated).
- Exception: `FixUpAfterTravel` (world load after travel) runs up to 100 attempts (`kTravellingMaxActions`) with no yield and does not
  check the mode. Story runs keep travel off (docs/171 §13), so this path is out of scope.

### 2.2 What it does
- Picks badly-off demographics (10 weighted draws), up to 5 candidate actions each, scores every candidate by cloning the demographics
  and re-measuring the error, drops those under 20% of the best, runs one.
- Actions: births (`AddSim`: a pregnancy or a new baby in a household), deaths (`KillSim`, `KillElderSim`, roommates, pets), move in
  / move out / bigger or smaller house / emigrate / create household / merge / move in with partner / split, relationship changes
  (befriend, antagonize, romance, marriage only within one household), jobs (get, quit, retire, promote, demote), occults, pets,
  academics, fitness, celebrity, social groups. `ThrowHouseParty` only resets a counter.
- Never the active household, special households or the travel household; major actions skip protected Sims.
- Events: `kNewBaby`, `kSimDied` (`Urnstone.FinalizeSimDeath`), `kFamilyMovedToTown` / `AwayFromTown` / `InToNewHouse` /
  `OutOfHouse`, merge and move-in events, `kEngaged`, `kMarriage`, `kCareerNewJob` / raise / demotion / fired / quit / retired. The
  game shows notifications only for Sims with a significant relationship to the active household.

### 2.3 Cost on the sim thread
- Each attempt starts with `Demographics.Collect`: all lots, every household and every SimDescription for each of ~20-30
  demographics, **no yield inside**; the chosen action's `Execute` is one tick too. So 4-10 single-tick collects per sim day at 1:00
  [verified code]; their length on our saves is not measured [inferred: small next to Overwatch's 22-cleanup tick, a few hundred
  SimDescriptions].
- No catch-all in any action file; the task catches only `ResetException`, and its `finally` re-schedules.
- docs/04a §5 already ranked it as an alarm burst candidate; docs/161 saw no story progression cause because every timing run so far
  had it off.

## 3. NRaas StoryProgression (not installed) [verified unless marked]

### 3.1 Architecture
- A repeating alarm (first after 1 s, then every 10 sim minutes) starts the work as its own task (the shared `AlarmTask` hands it to
  a function task, so it may yield); a re-entry guard; every manager updates when its own speed counter passes (in-game "Speed"
  option per manager).
- The scenario manager runs up to 10 passes (`PassesPerCycle`) over every queued scenario frame per timer, no count cap. After each
  frame it yields only if 100 ms have passed since the last yield (`SleepWatch`). So a busy town gets ~100 ms slices plus the
  slowest single frame, again and again while the queue drains [inferred: 3-6 dropped frames per slice at 60 fps].
- When on, it removes EA's story progression alarm and EA household creation and stops pet adoptions; turning it off reschedules EA's.

### 3.2 Hitch and bug risks
- `RepairScenario`'s first gather walks every game object in the world and each one's household members with no yield; the result
  is cached per scenario instance, so each new instance scans again. Same shape as Overwatch's per-object cleanup (docs/145).
- Other world-wide queries: situation, career and lot managers (rabbit holes, administration centres, all Sims).
- 117 `catch (Exception)` in 51 files, 60 `Common.Exception` calls (logged, ErrorTrap-style script error files), 9 empty catches.
- Its own known issues list: the LynchMob scenario is "extremely slow", a promotion loop between two inactive Sims, missing death
  notices.
- Community: higher speeds overload the game, players are told to use "Snail" or remove it; freezes at speed 3 that stopped with it
  off [community: forums.ea.com threads 4973528 and 8323479, forums.thesims.com comment 17742001, modthesims.info/t/525714].

### 3.3 MasterController (coordinator addition) (not installed)
- Source not in the local clone and not read; findings come from the shared NRaas helpers MC calls [verified] and community posts.
- The household-add helper bypasses the game's cap by calling `Household.Add` directly; the merge helper skips the game's merge
  checks behind a flag; a cap check takes the maximum as an argument. Nothing patches `Household.CanAddSpeciesToHousehold`,
  pregnancy, adoption or Ask to Move In [verified helpers; inferred for MC itself]. Community: the setting applies only to MC
  commands (Add Sim, Instant Baby, Pollinate) [community: steamcommunity discussions 133260492066622608, forums.ea.com 8267435].
- MasterController Integration's replaced-class list (`NRaas/VersionStamp.cs`) covers CAS and makeover screens only, no household
  cap class [verified]; one forum post says it removes the cap [community, contradicts the list].
- Over 8 needs the Portrait Panel mod for the portrait strip and Traveler for travel [community]; each extra Sim costs performance
  and players report instability near 3.5 GB [community: steamcommunity 1638668751268611657].
- Installed-but-unused cost: the NRaas framework's world-load hooks, timers, a one-shot task and event-listener wrappers run in
  every NRaas mod [verified framework]; MC's own alarms unknown (source not read).
- Verdict: not used. Households above 8 would be a `stress` setup that needs three mods we do not ship; the docs/179 rule R-HH-1
  stays the cap for natural runs.

## 4. Built (story runs only; self-checks, agent build; no game run)

| Piece | What |
|---|---|
| queues | `story-days.json` and `story-smoke.json` drop `-disablestoryprogression` (EA's story progression on); timing queues unchanged. `story-days` sets `days.rotate: 1` |
| agent `town_snapshot` | every SimDescription of every household but the service and tourist ones: id, name, age, household id/name, home lot, partner + married, job + level, dead; clock; EA's mode (`StoryProgressionService.sService.mMode` by reflection) |
| `story-day.ts` | one snapshot per calendar day, `townDiff` -> `town` line: births, arrivals, deaths, gone, moves, jobs, promotions, marriages, partners of Sims outside the active household (EA's kill and emigrate both dispose the description, so death and leaving town both show as `gone`); a `clock` line per sim hour; `days.town` (default on) |
| `story-report.ts` | `### Off-camera life` (per day, totals, EA mode, rotation list) and "Hitches attributed to story progression": kept events whose sim time (from `clock` lines) is 1:00-1:30 or whose script task names StoryProgression; counts >= 300 / >= 1000 ms and max |
| rotation | `days.rotate: N` (off by default): every N calendar days, at the first tick of the day, no new story starts, running ones end on their own (180 s cap, else the next day; never cut), then agent `switch_household` under the mode lock; the driven set becomes the new household, the rest partners; `rotate` lines, `rotations` in the summary |
| agent `switch_household` | Edit Town's Change Active Household (`EditTownPuck` -> PlayFlow -> `PlayFlowModel.ActivateSimScreenMaskedFuncFromHousehold`): `LotManager.LockActiveLot`, `Household.PrepareToBecomeActiveHousehold` (internal, reflection), `BinCommon.ActivateSim` (internal class, reflection), camera to the lot. Refused on vacation, at University, in the future (the player's button is off there) and wherever `InWorldSubState.IsEditTownValid` refuses (move, reaper, social worker, age-up, fire, burglar, ...). Shortcut: the PlayFlow state change itself (map view, `ForceReplanOfAllSimRoutes`) is skipped. CSV row `natural`, gate live |
| templates | `social-phone-invite-over` (Phone > Invite Over, first known Sim in the phone's picker), `social-phone-outing` (invite a known Sim to a community lot, lot picker by `auto_dialog`), `social-phone-party` (Phone > Throw Party); all `natural`; the guest is never ordered (R-HH-4). Already present: `social-house-party`, `social-visit-community`, `social-phone-call` |

Our spreads: `overwatchspread`, `easpreadlots`, `easpreadpets`, `easpreaddecay` cover none of story progression (Overwatch's 3 AM
cleanups, EA's 3 AM venue cleanup, pet pool, midnight relationship decay). EA's 1 AM run would need its own spread only if the
story-days attribution shows 1 AM hitches >= 300 ms; the candidate then is one demographic collect per run instead of one per action
attempt (design not started). NRaas StoryProgression would need a spread for `RepairScenario` and a shorter `SleepWatch` slice; not
pursued (not installed).

## 5. Unverified (ledger §300)
- Phone labels "Invite Over", "Invite Out", "Throw" and the picker row text (story-report's "label never matched" tells).
- `switch_household` in Live mode without the PlayFlow state: Sims of the old household keep routing normally, no fault.
- The 1:00-1:30 window covers EA's run at story-days speed (ultra + Skip at night: the run may land inside a Skip stretch).
- `town_snapshot` cost (one pass, once per sim day) and whether `Household.sHouseholdList` holds every off-camera Sim.
- simler90's changes to story progression (its source is not in this repo).
