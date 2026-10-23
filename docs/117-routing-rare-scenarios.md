# 117. Routing rare scenarios: repros for probe coverage

Status 2026-10-19: built and statically checked only (§2.11-2.16 and queue 2 added 2026-10-19). **None of these repros has run in the game; every one is UNVERIFIED.** The goal is coverage, not a fix: drive routing through situations a plain profile run on our saves never reaches (weather, moon, occults, vehicles, water, deaths, crowds, other worlds), with the probes logging what they hit, so the unfired sites of docs/109 §6 and the writer counters of docs/94 §15 get data for later study.

## 1. How to run

```
node tools/harness/queue.ts tools/harness/queues/rare-scenarios.json --dry-run
node tools/harness/queue.ts tools/harness/queues/rare-scenarios.json
node tools/harness/queue.ts tools/harness/queues/rare-scenarios-2.json --dry-run
node tools/harness/queue.ts tools/harness/queues/rare-scenarios-2.json
```

Before the run: install the agent built from this branch (`node tools/harness/build-agent.ts`, then the usual install; the commands below are new), and have both probe builds in `game-copy/`: `ts3native.asi.snapprobe2` ([ProbeSnap] pass 2, indirect-call sampler, 2,110 sites, docs/109 §5) and `ts3native.asi.snapp1b` ([RouteSnap] Count=1 writer counters, counters v48, docs/94 §15). Night run (memory rule: clean night testing), but these are coverage runs, not timing runs, so external load only matters for crashes.

After each job: `node tools/harness/snap-report.ts <row.nativeLog>` and `node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h` (snapprobe2), or `node tools/harness/snap-counters-report.ts <row.nativeDump>` (snapp1b). Compare the site list with `bp-snap-probe2` (plain 10-min profile) to find what each scenario adds.

## 2. Scenarios

Each scenario is a `tools/harness/repros.json` entry (id below) run by `profile-world.ts --repro` at the window start. Sources are the script DLLs (decompiled locally, not quoted here) [verified in the scripts, not live].

| § | Scenario | Repro | How it is driven | What to look for |
|---|---|---|---|---|
| 2.1 | Snow on the ground, frozen pond walk | `rare-winter-pond` | `test_env {mode: fixed, season: winter, weather: snow, intensity: Heavy, temperature: -15, snow_level: Deep, freeze_ponds: true}`, then `go_here {to: pond}`, `sim_route_to {to: pond}` with and without `options: [EnablePondPlanning]`, `@route.Replan`, 6 spawned Sims `crowd_go {to: pond}` | FrozenPondPath subpaths (docs/99 §11 node flag `0x200`, docs/100 §1 item 6); `pond_state` FrozenSolid in the test_env read; the docs/94 E26 gap (no writer of the frozen state found) |
| 2.2 | Thunderstorm | `rare-thunderstorm` | `test_env {weather: rain, intensity: Heavy}` (lightning strikes need Heavy), 8 spawned Sims and the active Sim walk across town (`crowd_go`, `go_here {to: far}`) | Sims running in rain, shelter reroutes, lightning reactions during walks |
| 2.3 | Full moon night, werewolf, zombies | `rare-fullmoon-werewolf` | `test_env {season: fall, weather: fog, moon_phase: 0, hour: 22}`, active Sim made a werewolf (`reflect_call_async @sim.OccultManager.AddOccultType ["Werewolf", true, false, false]`), crowd walks | Night routing, werewolf run; zombies only if the game spawns them on its own (not driven) |
| 2.4 | Planner bits no save exercises | `rare-route-bits` | `sim_route_to {to: far \| target: @upstairs \| home, options: [...]}` plans for: ghost (`RouteAsGhost`, `PassThroughWalls`, `PassThroughObjects`), flier, Sim leading a horse, mounted large animal, walking a dog, stroller, car (mode 3), car only (mode 2), UFO, subway, direct path / no avoidance; each plan replanned once; then a real long walk | Follower agent classes `0x800/0x900/0xb00/0x4400` (docs/11 table), car-only edges (docs/51 I-99-9), stroller planning |
| 2.5 | Playable ghost walking | `rare-ghost-walk` | `reflect_call_async Sims3.Gameplay.Objects.Urnstone.SimToPlayableGhost ["@sim"]`, then walks upstairs, across town and home | `RouteAsGhost` from the ghost buff (the game's own option, not ours) |
| 2.6 | Vampire, fairy, witch | `rare-occult-walks` | `AddOccultType` Vampire, Fairy, Witch in turn on the active Sim, a far walk after each, a `RouteAsFlier` plan | Vampire running; fairy/witch walking (broom and fairy run: §2.16) |
| 2.7 | Mermaid, boat, shark, lifeguard (IP) | `rare-ip-water` | Mermaid occult, `go_here {to: ocean}`; plans to the sea with `options2` `RouteAsMermaid`, `EnablePlanningAsBoat` (+ `EnableWaterPlanning`), boat to deep water, shark, lifeguard; `sim_portal_trip {via: houseboat}` | Water grid and boat legs (docs/100), mermaid follower, houseboat portal |
| 2.8 | Death during a walk | `rare-death-walk` | 8 spawned Sims walk (`crowd_go`); `kill_sim {pick: routing}` four times (Electrocution, last Drown) | Route cancel and object destroy while jobs run (docs/88 destroy waits, `Job_Cancel` 0x59A603 in docs/109 §6 group C) |
| 2.9 | Big crowd to one spot | `rare-big-crowd` | 30 spawned Sims + household, three `crowd_go` waves (home lot, farthest community lot, home) | Steering among many agents (extends `steer-crowd`), footprint churn; watch commitMB |
| 2.10 | University | queue job `bp-rare-travel-university` | save-matrix travel case to University with snapprobe2 armed (as `bp-snap2-travel`) | Campus load, lot-table misses; for walking round campus see §2.11 |
| 2.11 | Walking in another world | `rare-world-walk`, run by `profile.travel` (queue 2) | `profile-world.ts --travel university\|france` runs the save-matrix travel case with `--stay` (its trip code and fail-fast checks, the Matrix-* save-as first; no save away, no trip back, save fixes left as the ini sets them), then the profile window and the repro in the destination world: two `go_here {to: far}` (the second crosses back), `sim_visit_lot {lot: far}`, a plan + replan, 6 spawned Sims `crowd_go` (optional: needs a non-active household with a home in that world) | Campus / vacation-world lot tables, terrain and routing in a world no save of ours starts in |
| 2.12 | Owned car and bike | `rare-owned-vehicles` | `own_vehicle {kind: car}` (cheapest entry of the parking-space purchase list `ParkingSpace.sPurchasableVehicleInfo`, owner lot home, into the Sim's own inventory, else placed; preferred vehicle), `sim_visit_lot {lot: far, vehicle: true}` (reserves it, as `CarOwnable.VisitCommunityLot` does, then the game's visit), home the same way; then `own_vehicle {kind: bike}` and the same two trips | Vehicle path legs (`GetInVehicleRouteAction`), parking, car-only edges (docs/51 I-99-9) with a real car |
| 2.13 | Stroller with a toddler | `rare-stroller` | `make_sim {age: Toddler}` (`Sim.MakeRandomSim` at the active Sim: joins the household of that lot), `own_vehicle {kind: stroller}` (Generations `strollerSimple` by EP4 product key, then `strollerDeluxe`, then catalog products named *stroller*), `stroll` (`Stroller.GoOnStrollSocial` with that stroller: a walk to a lot 0..360 m away, ~2 Sim hours); a baby and a second stroll optional | Stroller path type and follower (docs/11 `0xb00` class) on a real walk |
| 2.14 | Horse riding outside Appaloosa Plains | `rare-horse-ride` (BP and SV) | `make_sim {species: Horse, age: Adult}` (`Sim.MakePet`, liking 50 with the active Sim), `ride` (queues `Sim.Mount`), `ride {to: far}` and `ride {target: home}` (`Terrain.RideHere`, the tests-skipping definition; plain Go Here is refused while mounted) | Mounted routing (car planning off), large-animal follower `0x4400` with a real horse |
| 2.15 | Full-moon zombies | `rare-zombies` (noEnv) | full moon at 22:00 (`test_env`), the game's own wave `LunarCycleManager.TriggerLunarEvents` (`reflect_call_async`, private static), then `zombies {spawn: 4, target: home}` (townies passing the game's `IsZombifiable`, instantiated on the active lot, `Sim.BeReanimatedSolo`, then Go Here) and `zombies {spawned: true, to: far}` (Zombie buff, `Origin.FromFullMoon`, on 6 spawned Sims) | Zombie walk style, grave-rise placement near the active Sim, slow crowd |
| 2.16 | Fairy flight, witch broom | `rare-flight-broom` | Fairy occult, FairyMagic 4, `do_interaction {interaction: fly}` (ICanFly, optional), far walk (FairyRun); Witch occult, `own_vehicle {kind: broom}` (`Sim.GrantWitchTraitBroom(true)`, the hidden inventory broom; reserved), `sim_visit_lot {lot: far \| home, vehicle: true}` | Broom vehicle legs (brooms count as bikes in `Sim.GetVehicle`), fairy run style |

## 3. Agent changes (ts3agent, built, not run)

| Command | Change |
|---|---|
| `test_env` (mode fixed) | New `weather` (Weather enum name, default sunny as before), `intensity` (Light/Moderate/Heavy for rain or snow: `ForceRequestRainWithIntensity` / `ForceRequestSnowWithIntensity`), `temperature` (debug temperature override flag + value; without it the flag is cleared), `snow_level` (`Debug_SetSnowLevel`), `freeze_ponds` (`Debug_SetPondFreezeLevel(100)` plus the FrozenSolid pond state by reflection: the end state of the game's own freezing steps, which fire no event either). Any mode but read: `hour` jumps the clock with `SimClock.AddOneHour` (at most 24 steps; alarms in between fire at once). Read-back adds `pond_state` and `hour` |
| `sim_route_to` | `options` / `options2`: `Route.RouteOption` / `RouteOption2` names set on top of the Sim's own options before the plan; `to: pond \| ocean \| far` as the goal. Result lists the options set |
| `go_here` | `to: pond \| ocean \| far` |
| `crowd_go` (new) | Every `spawn_sims` Sim (and the household with `household: true`) gets a user-directed Go Here to one point (`target`, `to`, or `x, z`); `max` caps the count |
| `kill_sim` (new) | `Sim.Kill(DeathType)` (the game queues `Urnstone.KillSim` at MaxDeath priority) on a named Sim or a spawned one; `pick: routing` (default) prefers one that is routing now; the result says whether it was |
| queue `profile.noEnv` | Passes `--no-env` to profile-world.ts: no test_env guard, which would otherwise turn winter into spring and move a full moon away mid-run |
| queue `profile.travel` (2026-10-19) | `university \| future \| france \| china \| egypt`: passes `--travel` to profile-world.ts, which first runs `save-matrix.ts --cases travel --to <world> --stay` and fails the run when the trip fails; `timeoutMin` must cover the trip (60 in queue 2) |
| `save-matrix.ts --stay` (2026-10-19) | Travel case ends on arrival: no save away, no trip back, the save fixes not switched (the job's ini decides); the trip-out travel save checks still count |
| `sim_visit_lot` (2026-10-19) | `lot: far` (farthest community lot from the active Sim); `vehicle: true` reserves the `own_vehicle` object (`Sim.SetReservedVehicle`) before the visit |
| `own_vehicle` (new) | `{kind: car \| bike \| stroller \| broom, sim?, name?}`: car / bike from the purchase list sorted by cost (bike: an `AdultBicycle` that is not a motorcycle or scooter, else `BicycleAdultStreet`), stroller = EP4 `strollerSimple` / `strollerDeluxe` (product key with ProductVersion.EP4; `StrollerCheap` is the script class, not an instance name) then the catalog, broom by `GrantWitchTraitBroom(true)`; owner lot = home (`Sim.CanUseVehicleRightNow` needs it), the Sim's own inventory (the family inventory does not count) else placed, preferred vehicle; root `@vehicle`. Result: type, where, can_drive |
| `make_sim` (new) | `{age?: Toddler (pets: Adult; pets only Child/Adult/Elder), species?: Human, gender?: Female}`, live mode only: `Sim.MakeRandomSim` / `Sim.MakePet` at the active Sim's position (joins that lot's household); a horse gets liking 50 with the active Sim; root `@made` |
| `stroll` (new) | `{child?: @made, sim?}`: `Stroller.GoOnStrollSocial` with the `@vehicle` stroller |
| `ride` (new) | `{horse?: @made, target \| to \| x,z}`: not mounted -> queues `Sim.Mount`; mounted -> `Terrain.RideHere` to the goal |
| `zombies` (new) | `{spawn?: n, spawned?: bool, target \| to \| x,z?}`: risen townies (`IsZombifiable`, `BeReanimatedSolo` at CriticalNPCBehavior, as `PossiblySpawnZombie` without its 20% roll) and / or the Zombie buff on the spawn_sims Sims, then a Go Here for each. Ours are not in the game's sunrise list: they stay zombies until the game closes |

Point search for `to`: `pond` = representative point of the nearest world pond (lot ponds not searched); `ocean` = 10 m into the sea found on rings of 10..400 m round the Sim (the `sim_portal_trip` search, now shared); `far` = centre of the community lot farthest from the Sim.

## 4. Not driven, and why

Status 2026-10-19, after §2.11-2.16. Script sources read, nothing run [verified in the scripts, not live].

| Scenario | Now | Why / limit |
|---|---|---|
| Zombies | Driven (§2.15) | Spawn path found: `LunarCycleManager` sunset alarm `TriggerLunarEvents` (full moon only: 2 townies get the Zombie buff, then an hourly 20% `PossiblySpawnZombie`). No "zombies on" option is read in that code; the lunar options only set cycle and phase |
| Fairy flight | Driven as far as it exists (§2.16) | There is no fairy flight route mode: a fairy's run uses the `FairyRun` walk style (SimWalkStyleRules); `ICanFly` is an animation in place. `RouteAsFlier` stays plan-only (`rare-route-bits`): no game caller sets it |
| Witch broom | Driven (§2.16) | Brooms are owned vehicles (`MagicBroom`); the joy and stunt rides are private pie-menu interactions with no destination and are not driven |
| Car, bike | Driven (§2.12) | Whether a leg is driven is still the planner's choice (vehicle path type); teens need a licence (Generations); bikes are off in worlds with special car rules |
| Stroller | Driven (§2.13) | Toddler, not a newborn: a baby made by `MakeRandomSim` lies on the floor (the game's own birth has the mother hold it); the private `GoOnStroll` needs a Sim picker, so the social variant is used |
| Horse riding | Driven in BP and SV (§2.14) | No world check for horses in the scripts (EP5 installed is enough; apartments refuse pet adoption). Not checked: rider age limits of `Mount`, an untrained horse refusing (the made horse is random) |
| Boat sailing | Not driven | Plan bits only (`rare-ip-water`); an owned boat needs a dock or water spawn, not looked at |
| Walking round the University campus, vacation worlds | Driven (§2.11) | One trip per job (~30 min of the 60 min timeout). The future (Oasis Landing) works with `travel: future` but has no job: its portal needs Into the Future set up in the save (docs/77) |
| Other home worlds | Not driven | Only BP, IP and SV saves exist; queue 2 adds SV for the horse. Appaloosa Plains itself needs a save |

Risks to read in the logs: `SimToPlayableGhost` on a living Sim (the CAS-ghost path) and several occult types on one Sim were not tried before; `@upstairs` and `to: pond` fail in a world without them (those steps are optional); a plan with a follower type the Sim is not may fail, which is logged, not an error.

## 5. Queue `tools/harness/queues/rare-scenarios.json`

Ini for the profile jobs: env-on (`RouteAsync`, `RouteGate` Sites+Enforce, `Discourage`), seed 12345, `-disablestoryprogression`; snapprobe2 jobs add `ProbeSnap.Enabled=1, DelaySec=60` (min counters v43), snapp1b jobs add `RouteSnap.Count=1` (v48).

| Job | Save | Probe | Min |
|---|---|---|---|
| `bp-rare-winter-pond` | BP Spring | snapprobe2 | 6 (noEnv) |
| `bp-rare-thunderstorm` | BP Spring | snapprobe2 | 6 (noEnv) |
| `bp-rare-fullmoon-werewolf` | BP Spring | snapprobe2 | 8 (noEnv) |
| `bp-rare-route-bits` | BP Spring | snapprobe2 | 6 |
| `bp-rare-ghost-walk` | BP Spring | snapprobe2 | 6 |
| `bp-rare-occult-walks` | BP Spring | snapprobe2 | 8 |
| `bp-rare-death-walk` | BP Spring | snapprobe2 | 6 |
| `bp-rare-big-crowd` | BP Spring | snapprobe2 | 8 |
| `ip-rare-ip-water` | IP Spring | snapprobe2 | 6 |
| `sv-rare-route-bits` | SV Spring | snapprobe2 | 6 |
| `sv-rare-winter-pond` | SV Spring | snapprobe2 | 6 (noEnv) |
| `sv-rare-death-walk` | SV Spring | snapprobe2 | 6 |
| `sv-rare-big-crowd` | SV Spring | snapprobe2 | 8 |
| `bp-rare-winter-pond-p1b` | BP Spring | snapp1b | 6 (noEnv) |
| `bp-rare-death-walk-p1b` | BP Spring | snapp1b | 6 |
| `bp-rare-big-crowd-p1b` | BP Spring | snapp1b | 8 |
| `bp-rare-travel-university` | BP | snapprobe2 | travel case (~30) |

Profile time 108 min plus the travel case; with restarts and loads (~4 min a job) about 3.5 hours.

Queue 2 `tools/harness/queues/rare-scenarios-2.json` (2026-10-19), same ini and probe (snapprobe2, v43):

| Job | Save | Repro | Min |
|---|---|---|---|
| `uni-rare-world-walk` | BP, then a trip to University (`profile.travel`) | `rare-world-walk` | 6 (+ trip, timeout 60) |
| `fr-rare-world-walk` | BP, then a vacation trip to France | `rare-world-walk` | 6 (+ trip, timeout 60) |
| `bp-rare-owned-vehicles` | BP Spring | `rare-owned-vehicles` | 7 |
| `bp-rare-stroller` | BP Spring | `rare-stroller` | 6 |
| `bp-rare-horse-ride` | BP Spring | `rare-horse-ride` | 6 |
| `bp-rare-zombies` | BP Spring | `rare-zombies` | 7 (noEnv) |
| `bp-rare-flight-broom` | BP Spring | `rare-flight-broom` | 7 |
| `sv-rare-horse-ride` | SV Spring | `rare-horse-ride` | 6 |

Profile time 51 min plus two trips (~30 min each); with loads about 2.5 hours.

## 6. Results

| Job | Date (UTC) | Build | Ran | Steps failed | New sites vs bp-snap-probe2 | Writer counters (p1b) | Notes |
|---|---|---|---|---|---|---|---|
| bp-rare-winter-pond | 2026-10-18 17:00 | tested | PASS 7.9 min | 2/15 (@route.Replan TargetException) | 5 pairs (0 inside) | - |  |
| bp-rare-thunderstorm | 17:07 | tested | PASS 7.8 min | 0/11 | 3 (1 inside) | - |  |
| bp-rare-fullmoon-werewolf | 17:15 | tested | PASS 9.8 min | 0/13 | 6 (4 inside) | - |  |
| bp-rare-route-bits | 17:25 | tested | PASS 7.7 min | 12/29 (all Replan calls) | 4 (3 inside) | - | **void**: every replan failed, the option bits were never exercised |
| bp-rare-ghost-walk | 17:33 | tested | PASS 7.7 min | 1/9 (Replan) | 21 (0 inside; 19 in route following) | - |  |
| bp-rare-occult-walks | 17:41 | tested | PASS 9.7 min | 1/14 (Replan) | 0 | - |  |
| bp-rare-death-walk | 17:51 | tested | PASS 7.8 min | 0/10 | 2 (1 inside) | - |  |
| bp-rare-big-crowd | 17:59 | tested | PASS 9.9 min | 0/9 | 2 (0 inside) | - |  |
| ip-rare-ip-water | 18:09 | tested | PASS 7.7 min | 5/17 (Replan) | 4 (4 inside) | - |  |
| sv-rare-route-bits | 18:16 | tested | PASS 7.8 min | 12/29 (all Replan calls) | 10 (6 inside) | - | **void**, as bp |
| sv-rare-winter-pond | 18:24 | tested | PASS 7.7 min | 6/15 | 16 (7 inside) | - | **void**: no pond point near the Sim in SV (go_here, sim_route_to, crowd_go) |
| sv-rare-death-walk | 18:32 | tested | PASS 7.8 min | 0/10 | 13 (5 inside) | - |  |
| sv-rare-big-crowd | 18:40 | tested | PASS 9.7 min | 0/9 | 21 (12 inside) | - |  |
| bp-rare-winter-pond-p1b | 18:50 | snapp1b | PASS 7.8 min | 2/15 (Replan) | no vcall probe in this build | no new family (G1 PASS); ponds `FrozenSolid`, `EnablePondPlanning` plan Succeeded |  |
| bp-rare-death-walk-p1b | 18:58 | snapp1b | PASS 7.8 min | 0/10 | no vcall probe in this build | no new family (G1 PASS) | 23 possible stuck Sims listed (log only) |
| bp-rare-big-crowd-p1b | 19:06 | snapp1b | PASS 9.8 min | 0/9 | no vcall probe in this build | no new family (G1 PASS) |  |
| bp-rare-travel-university | 19:16 | tested | PASS 4.9 min | - | 13 (3 inside) | - | trip home fix works live (docs/77 §2.3) |
| uni-rare-world-walk | 19:21 | tested | PASS 9.7 min | 1/12 (Replan) | 13 (2 inside) | - |  |
| fr-rare-world-walk | 19:31 | tested | FAIL 2.8 min | - | 1 (0 inside) | - | trip to France PASS, then agent `test_env` NullReferenceException (no SeasonsManager in vacation worlds); fixed in e519e07, rerun after an agent build |
| bp-rare-owned-vehicles | 19:34 | tested | PASS 8.7 min | 1/11 (Replan) | 4 (0 inside) | - |  |
| bp-rare-stroller | 19:43 | tested | PASS 7.7 min | 4/9 | 7 (4 inside) | - | **void**: no stroller or baby could be made (expansion content missing or household full) |
| bp-rare-horse-ride | 19:51 | tested | PASS 7.8 min | 1/10 (Replan) | 6 (1 inside) | - |  |
| bp-rare-zombies | 19:59 | tested | PASS 8.8 min | 0/11 | 12 (2 inside) | - |  |
| bp-rare-flight-broom | 20:08 | tested | PASS 8.7 min | 0/12 | 0 | - |  |
| sv-rare-horse-ride | 20:17 | tested | PASS 7.8 min | 1/10 (Replan) | 20 (10 inside) | - |  |

Night 2026-10-18/19 (chains 24-25, `tested` = the pinned 0.10.1 build, `snapp1b` = ts3native 0.27.0). A PASS here only means the job ran to the end: the queue did not yet fail a job on a failed repro step (fix held on branch, b0d062f). `@route.Replan` fails with TargetException in every job that calls it (cause under investigation), so replan-driven parts of each job are missing. Next: reruns of the void jobs.

"New sites" = (site, target) pairs in the job's `ts3native.log` that none of the ten `snap-probe2-2026-10-18-*` logs has (MSVCR80 targets compared by module and low 16 bits, as docs/109 §8.7); "inside" = inside the route search by the docs/109 §8.2 rule. A pair counts in every job that saw it, so the column does not add up to the §6.1 total. The `tested` jobs' logs report ts3native 0.24.0 with the pass-2 site table (`probe snap: 2110 indirect call/jmp sites`), the same probe as bp-snap-probe2; the void jobs' pairs are valid code observations, only their scenario is not.

### 6.1 Findings (2026-10-19, logs only)

**Input.** 22 `ts3native.log` files of the `tested` jobs (both chains), joined as docs/109 §7: `node tools/indirect-live-vs-static.ts <22 logs> --pairs --unfired --sites native/ts3native/vcallsites2.h` against the CSV as it is after docs/109 §8, and a set difference against the ten snap-probe2 logs. Inside/outside by the §8.2 reachability rule (direct closure of `Route_PlanLocked`, extended by live pairs of the probe2 and rare logs), bodies read with capstone 5 on the Steam 1.67 exe. Scratch scripts in the session scratchpad (`rare/`).

**Totals.** 1,232 sites and 1,960 pairs over the 22 logs. Against the CSV: match 370, resolves 372, family 1,167, hint 5, **new 24 (18 sites), no-row 22 (22 sites)**, so the docs/109 §8.6 "0 new / 0 no-row" no longer holds over these logs. Against the probe2 logs: **86 pairs (72 sites) never seen before**; 40 of them already have a CSV row (static prediction), 46 are the new and no-row pairs. The live rare edges add 29 functions to the live-extended closure (2,994 to 3,023); none of them has a global write.

| Class | Pairs | Sites | What runs | Jobs |
|---|---|---|---|---|
| inside, read only | 10 | 10 | `Stub_Return1` from `FootprintMember_FwdSlot0`, `Footprint_GetOwnerIdAlias` from `RouteObstructionAtPoint`, `LotQuadTree_QueryCbD` (callback of `QuadTreeCellPos_A`), the folded `return 1` stub `0x9C1B00` from `LotQuadTree_ApplyInsert` slot `+0x20`, a scene-shape float getter `0xB71880` (vtable `0x106EEB0`, slot `+0x54`) from `0xB73C70`, switch cases in `0x643E60`, `QuadTreeCellPos_C`, `0xB48480`; world cost mode 4 `RouteCost_Mode4 0x675800` from `WorldAStar_Search` `0x676678`/`0x6766AD` (mode known, docs/99, first fired at these two sites; its callees are reads) | SV jobs, zombies, uni, ip-water |
| inside, references and frees | 12 | 10 | refcount release `0xEC4FC0` (count at `+4`, deleting dtor at slot `+8` on zero; slot `+0xF0` of `vt_FootprintMemberIface`) from 5 sites in `PathPlanner_PlanHierarchical`; quadtree AddRef `0x662DF0` from `0x9EC420`; `AStarGoalNode_DeletingDtor` from `0x61C3C0` and `InRoomPlanToPoint`; deleting dtor `0x64E600` of a plan-built class (`0xFECF6C`, made by `0x64E5E0`) from `PathPlan_WithRetries`; footprint deleting dtors behind `RefCounted_ReleaseAt8 0x9158AB`: `0x624F20`, `0x80EF70` (seen in pass 3, I-109-33) and **`0x620E10`** (thunk to `0x620E20`, class `0xFEADE0` made by the footprint factory `0x62CB20`, `0x6236D0` and `LotTable_InitLotEntry`) | SV jobs, werewolf, stroller, route-bits, death-walk, thunderstorm, travel |
| outside, shared helper (docs/109 §8.4) | 18 | 11 | `Job_Cancel 0x59A603` paths (`0x4466F2`, `0x5933A1`, `0x593C9E`, `0x593D32`, `0xB033F6`), `Job_Destroy 0x59A688` -> `RefCounted_AtomicRelease` slot `+8`, `0x8A65EB` -> `0x731D40` fan-out (4 targets), `0x8CE540` (lazy W3/W4 builds only, I-109-34), `0x4CF583` -> `0xC53F30`, `0x9158AB` -> **`0x907850`** (class `0x103F9E0`: no constructor in the closure) | many |
| outside, Mono host behind event 59 | 1 | 1 | `0xD7D2F0` switch case | fr-world-walk |
| outside, route following and steering | 23 | 23 | `RouteFollow_A`, jig checks `0x849D40`, `RouteFollow_EndStep`/`RouteFollow_Update` helpers `0x84B0C0`, `0x84B590`, `0x852700`, `0x8504D0`, `ObstacleAvoidance_B`, `RoutingWorld_BlockerCheck2d`; 4 sites read `CameraGlue_GetActiveController` | ghost-walk (19), horse-ride (BP and SV), stroller, zombies, sv-big-crowd |
| outside, plan group and route objects (sim thread) | 13 | 9 | `Route_PlanCommon`, `Route_GoalStep`, `Route_MapStatusToResult`, `Route_UnlinkPaths`, destination generation `0x830340`, route-object helper `0x55CFA0` (44 callers: route ICs, route following) and its target `0x56FFD0`, portal building `0x6387B0` (from `RoutingWorld_AddObjectPortals`, 4 switch cases) | many |
| outside, object creation | 2 | 2 | `Object_Allocate` -> `FootprintComponent_ctor`, `0x95A360` -> component `0x951370` | SV jobs, uni |
| outside, timer and Mono runtime | 7 | 7 | `Timer_Get` switch, `0xE458E0`, `0xE459F0`, `0xE9C3A0`, `GC_collect_a_little_inner` -> `IC_EATrace_AssertImpl` | zombies, uni, travel, sv-winter-pond |

**Snapshot verdict (G2, docs/109 §8.6).** No new routing state: every inside target is a read, a stub, a count change on a footprint member or quadtree, or a free of an object the plan or the lot graph owns, the families of docs/109 §8.3 and I-109-38. Nothing new for the P4 cut or the D1-D6 guards. 6 inside pairs come only from SV jobs (three of the hierarchical planner's releases, the `FootprintMember_FwdSlot0` stub, the `QuadTreeCellPos_A` callback, the `0xFEADE0` footprint dtor): world coverage, not scenario coverage, since probe2 had no Sunset Valley job. Cost mode 4 fired in SV and in BP zombies.

**Writer counters (P1b).** `node tools/harness/snap-counters-report.ts` on the three `-p1b` native dumps against `snap-p1b` BP and IP: every writer leaf, sim source, P1b source, IC slot, world event id and object event id that fired in a rare job also fired in a baseline; the only counters non-zero in a rare job and zero in both baselines are `asyncSkipped` (1 each in death-walk and big-crowd; a queued job the world outran, not a writer), a record-rate bucket and the unattributed-frame tables. New unattributed callers: 3 single hits in winter-pond (`RoomGraph_FirstUse` with no next frame; `RemoveFootprintLocked` -> `LotRouting_RemoveObjectFootprints` with a heuristic next frame inside `App_Run`), no new family. G1 PASS in all three. Frozen ponds with `EnablePondPlanning` plans fired no writer, as docs/100 §7 predicts (pond state is script-side, the option arrives per request). **No missed change signal for the snapshot.**

**Follow-ups.**
1. Append CSV rows for the 46 new and no-row pairs, so the §8.6 join is "0 new / 0 no-row" again (~0.5 h, static). **Done 2026-10-19 (docs/51 §102, I-117-5):** 40 rows appended to `docs/data/routing-indirect-sites.csv` (18 sites with new targets get a replacement row with the old plus the live targets; 22 no-row sites get a first row, kind and slot read with capstone), note tag `[live rare scenarios]` with the class from the table above. The join over the 10 probe2 + 22 rare logs: 2,064 pairs, match 392, resolves 400, family 1,267, hint 5, **new 0, no-row 0**.
2. `0x9158AB` -> `0x620E10`: footprint class `0xFEADE0` as a fourth footprint dtor on the worker (I-117-2): check its body against I-109-38's write list and, if a probe pass 3 runs again, add an SV job to see its thread split (~0.5 h static; the run only with another pass-3 job). **Static part done 2026-10-19 (docs/51 §102, I-117-6):** `0xFEADE0` is the secondary vtable of the **box footprint `0xFEAE08`** (the release at `0x9158AB` goes through the `+4` interface, hence the this-adjust thunk). The dtor stores the box vtables, runs the shared base dtor `0x807AA0` (child list `+0x24`, plain decrement of the lock-key set count at `[fp+0x20]+4`, delete at 0) and EA-frees the block: a subset of I-109-38's list (no pool #5/#6 work, the box class has no level cache). A P4 clone run cannot reach it on a clone footprint: the box class is in the replay class table (copied flat, 0x4C bytes), every copy gets `RP_BIAS` in the count the release decrements (`fp+8`), its `fp+0x20` is the private set copy, and the cut runs the pending updates on the sim first. Nothing new to cover; the thread split stays open for a future pass-3 job.
3. Rerun the void jobs after the `@route.Replan` fix; the route-bits option paths are still unobserved.

## 7. Effort

| Task | Hours |
|---|---|
| Agent steps, repros, queue, this doc (done) | 3 |
| Queue 2: travel option, vehicle / stroller / horse / zombie commands, 6 repros (done) | 3 |
| Night run of queue 2 (unattended) | 2.5 |
| Night run of the queue (unattended) | 3.5 |
| Reports per job and the new-site list into docs/109 / docs/51 (done 2026-10-19: §6.1, docs/51 §96) | 1.5 |
| CSV rows for the 46 new / no-row pairs and the `0x620E10` body (§6.1 follow-ups 1-2, done 2026-10-19) | 0.5 |
| Fix failed steps after the first run, rerun those jobs | 2 |
