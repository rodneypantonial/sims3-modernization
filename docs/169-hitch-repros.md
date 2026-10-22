# 169. Hitch repros

Repros that fire one hitch cause many times per run, so a fix can be measured without waiting for play to cause it. Other agents add the script and routing sections; this file was created with the main-thread section only.

## Main thread

**Built 2026-10-22, static only: agent compiled, queue dry-run ok, never run in game.** Ledger §268.

Camera stutter in a BP camera pan is led by the services stage (docs/167 §7: 85% of the time in frames >= 100 ms). Inside it, three main-slot services hold most of it: CAS/SimService 29%, World/Service 20% (lot streaming), SwarmSystem 16% (particles) [measured, `freeze-measure-2026-10-22-bp-freeze-pan`]. Each repro below sets the triggering state directly and re-creates it in a loop for the whole window. Nothing waits for a natural event.

**Owner:** these are engine services, **Apex Radiance's area** (engine, visuals, lookups, allocator, memory; issue #32). We own routing, script and Mono. The repros, agent commands and pass rule serve both projects: Apex can measure an engine fix with them, and we use them to confirm our offload work leaves the main thread alone.

### Agent commands (`mods/ts3agent/src/plugin/Handlers/MainThreadReproHandlers.cs`)

| Command | What it does | Counters (`{status:true}`) |
|---|---|---|
| `camera_jumps {order: far\|populated, lots, dwellS, height, loops, outfits, spawn}` | Game task. Jumps the camera (no pan) to one lot after another and holds it still `dwellS` wall-clock seconds at street zoom. Lot activation needs a slow camera (docs/28 §5.2), so it waits while holding. `far` = farthest-next order, so every jump crosses the town. `populated` = lots with the most Sims, re-ranked every loop. Per jump, `outfits` Sims on the lot switch outfit (athletic or everyday, by loop parity), and `spawn` new Sims stand within 6 m of the lot centre. Each loop first removes the Sims the previous loop spawned, through `remove_sims`. Shares `camera_pan`'s generation, so either one stops the other. | `jumps`, `loops`, `outfitChanges`, `spawned` |
| `vfx_burst {names, count, radius, seconds, restartS}` | Game task. Every `restartS` it creates `count` effects on a ring around the camera target, starts them, then stops and disposes them and creates them again. Default names come from the game's own scripts: fire (`fireSystemFx`, `fireSystemMidFx`), Supernatural spells and fairies, lightning and sparkles. These are effects only, with no `Fire` object: nothing burns, spreads, kills a Sim or saves. A real fire is left out on purpose (firefighters, deaths, spread). | `rounds`, `started`, `missing` (unknown names) |

Only the in-game camera moves. The game window is never moved or focused.

### Repros (`tools/harness/repros.json`) and jobs (`tools/harness/queues/repro-check-main.json`)

| Job | Repro | Loop | Expected events [inferred] | Pass rule (`profile.frameLead`) |
|---|---|---|---|---|
| `bp-main-cas` | `main-cas-builds` | `camera_jumps populated`, 8 lots, 6 s dwell, spawn 3 and outfits 6 per jump | ~45 jumps, ~135 new Sims built + up to ~270 outfit builds | CAS/SimService leads >= 20 frames > 50 ms |
| `bp-main-lots` | `main-lot-stream` | `camera_jumps far`, 12 lots, 8 s dwell | ~35 jumps, each streaming the lots around it in and the previous ones out | World/Service leads >= 20 frames > 50 ms, and `lotAdds` >= 20 |
| `bp-main-swarm` | `main-swarm-fx` | `test_env` heavy rain (thunderstorm), camera held on the busiest lot, `vfx_burst` with 24 effects re-created every 3 s (`profile.noEnv`) | ~93 rounds, ~2,200 effects | SwarmSystem leads >= 10 frames > 50 ms |

All three jobs run 5 min on Bridgeport `Baseline-BP-Spring` with the measurement-tool settings of `tool-check.json`: `asi.0330`, frames.csv with the services split (docs/167 §7), no samplers and seed 12345. They also turn on `[LotStream] Enabled=1`, which adds lot adds per second to the seconds csv. Each job takes ~8 min, ~25 min for the queue. They are correctness checks, not timing runs (noisy-machine rule). The N values are first guesses and should be set from the first run.

### Counting the events

- **Lot loads:** the `[LotStream]` counters (docs/28 §5.5, counters v19) are already in every build. With `Enabled=1`, `stutter-profile.js` writes `lotAdds`, `lotAddMaxMs`, `lotSetActive` and related columns per second. This is the cheap native counter, and `frame-lead.ts` sums `lotAdds` after the first minute.
- **Sim builds:** there is no cheap counter. `SimService_Update 0x5F0E50` swaps a request list, and `[Verify]` counts only that list's lock (docs/42). The repro counts the builds it asks for (`spawned`, `outfitChanges` in the status step). The services table of `freeze-report.ts` shows the cost: "Services by service" lists CAS/SimService's lead count in frames > 50/100/300 ms and its ms share. Note that `freeze-report.ts` drops frames in repro step windows (+10 s). Here the whole window is one long step, so read `frame-lead.ts` instead.
- **Effects:** `vfx_burst` status gives `started` and `missing`. There is no native swarm counter.

### Pass rule: `tools/harness/frame-lead.ts`

`node tools/harness/frame-lead.ts <tag> '{"service":"World/Service","ms":50,"min":20,"lotAdds":20}'` counts presented frames longer than `ms` whose longest service (`svc1` in frames.csv) is the target. Only the first minute and the profiler edge are dropped (`worst-hitches.ts baseWins`). Repro windows count, because the repro is the cause being measured. `profile-world.ts --frame-lead` (queue `profile.frameLead`) runs it once the window ends and prints `PASS: frame lead ...` or `FAIL: frame lead ...` as the job verdict. If the services split was refused (`na`), the rule fails with that reason. Self-check: `node tools/harness/frame-lead.ts --self-check`.

### Open

- First run: `node tools/harness/queue.ts tools/harness/queues/repro-check-main.json`, then `freeze-report.ts <tag>` per job for the stage and service tables.
- I-169-1..4 (ledger §268): whether a camera jump makes lots activate within 8 s of dwell, whether spawned Sims and forced outfit changes cause SimService work led on the main thread, whether the swarm effects cost > 50 ms frames at all, and whether every default effect name exists (`missing` = 0).

## Script causes

### Repros

| Repro (`-loop` 10 min, `-check` 5 min) | State set up each loop | Fired by | Off arm (game's path) | On arm (switch) |
|---|---|---|---|---|
| `hitch-stuckcheck-*` | `strand_many {count: 8}` (new agent command): 8 townies on unroutable spots of 8 different lots (their current or home lot; never the world lot or the active lot), re-stranded every loop (the previous round reset them) | `Ts3Fixes.Fixes.StuckCheckNow(0, 3)` (new): every actor's `RunStuckCheckHelper` armed due in 1 sim-minute, all in one alarm tick | EA check in the global alarm: stranded Sims fail all 10 plans | `stuckcheckasync` + `stuckchecknear` together (one Sim per tick in `StuckCheckDrain`, nearest point first) |
| `hitch-easpread-lots-*` | the save's lots (object walk); dirt cleaned by the first firing is not re-made [inferred: the walk dominates] | `EaSpreadNow("lots")` (existing): the real callback from a 1-sim-minute global alarm | one-tick venue cleanup | `easpreadlots` |
| `hitch-easpread-pets-*` | a fresh stray cat per firing (built, then disposed; works with full pools) | `EaSpreadNow("petbuild")` | one-tick build in the alarm | `easpreadpets` |
| `hitch-easpread-decay-*` | the save's relationships (one extra decay day per firing; the loaded save is never saved) | `EaSpreadNow("decay")` | one-tick `DecayAll` | `easpreaddecay` |
| `hitch-errortrap-*` | the save's world objects and inventories | `ErrorTrapSpreadNow("hook")` (existing): ErrorTrap's inventory check now, in a fresh task, path picked by the switch | ErrorTrap's own one-tick check | `errortrapspread` |

Loop timing: 75 s lead-in (the load minute is excluded), `set_speed normal`, then 20 firings off,on,on,off x5 about 24-27 s apart (arm 1 s, setup 3 s where present, fire + wait 20-23 s): **10 firings per arm**, ~555-615 s after the window start. The stuck-check loop is the tightest (~595 s with `strand_many` time); its later firings may fall after the profiler's window end [inferred], so read its count from the step log. Check variants: 8 firings, no lead-in, ~2-3.5 min.

### Why not the clock (stuck check)

`test_env {hour}` jumps with `SimClock.AddOneHour` in a loop inside one agent call [verified, `HarnessHandlers.cs`], so a jump to the next day's 2:57 skips 24 h of alarms that then fall due together: midnight decay, pet pool, Overwatch's nightly alarm, hourly alarms [inferred]. The real 3 AM arming also places each Sim's check 20-60 sim-minutes after 3:00, so one loop would be ~40 sim-minutes of scattered checks mixed with every other daily alarm. `StuckCheckNow` builds the same helper the game's private `CreateAlarm` builds (Sim id, position now, attempt) through its private constructor and puts its private `Execute` on the same kind of global alarm [verified, decompiled `LotManager.RunStuckCheckHelper`]; only the delay differs (1 sim-minute, or `1 + random(spread)` with `spreadMinutes > 0`). `Execute` -> `RunStuckCheck` goes through our hook, so the switches pick the path. `attempt 3` is the final round: all points fail -> the game's reset, no re-armed alarm left behind to fire in a later step's window. Overwatch's own nightly Stuck Check is not in this loop: `OverwatchSpreadNow("StuckCheck", 0)` fires it alone and `bp-overwatch-ab-loop` covers the whole nightly alarm.

### ErrorTrap re-trigger limit

`ErrorTrapSpreadNow` re-runs the inventory check, ~3/4 of the load-up tick (12 of 16 stall samples, docs/146 §1). The dereference controllers' `PreProcess` (the other 4 of 16) **cannot be re-run in one load**: their reference lists are filled only by the game's load callbacks and cleared after the scan (docs/146 §3). Step 1 (role/recovery/outfit/lot checks) had 0 of 16 samples and is left out. Both arms run inside our trigger's one-shot task, so the hitch name is `ErrorTrapSpreadNow` on both; the off arm is the long one.

### Isolation

- The venue cleanup arms every Sim's stuck check 20-60 sim-minutes later on both paths; the lots, pets, decay and ErrorTrap jobs keep `stuckcheckasync` on for the whole run, so those checks run as `StuckCheckDrain`, not in the world lot task the off arm is read by.
- Repro steps are labelled by agent command in `hitch-ab.ts --scenario` (`reflect_call`); the arm of each window is the step order (notes in `<tag>-repro.jsonl`).

### Pass rules: `tools/harness/queues/repro-check.json` (5 min each, settings of `tool-check.json`, asi.0330)

- **Firing counted** (`profile.fixesRequire`, on-arm counters for the 4 on firings): stuck `stuckcheckasync_run>=4,stuckchecknear_checks>=4`; lots `easpread_lots_passes>=4,easpread_lots_stopped==0`; pets `easpread_pets_moved>=4`; decay `easpread_decay_passes>=4,easpread_decay_threw==0`; ErrorTrap `etspread_passes>=4,etspread_theirs>=4,etspread_failed==0`.
- **Expected task in hitch lines** (`profile.hitchTasks`, new): `<regex>[@<ms>]>=N` = at least N hitch lines with a docs/166 task run matching the regex and taking >= ms (`tools/harness/hitch-tasks.ts`, self-check PASS; `profile-world.ts --hitch-tasks`, `queue.ts profile.hitchTasks`). Off arm only (the fix's point is that the on arm does not hitch): EA paths `Core\.(World)?Lot$@100>=2` (stuck check `@50`), ErrorTrap `ErrorTrapSpreadNow@100>=2`. The ms floor keeps the world lot task's ordinary slow runs (155 kept runs in the 2026-10-22 captures) from passing the rule.
- [inferred, I-169-1] `AlarmManager.Global` alarms run inside the world lot's task, named `ScriptProxy>Sims3.Gameplay.Core.WorldLot` (docs/134 §8 has the stuck-check alarm in `Lot.Simulate -> AlarmManager.Simulate`); the regex also takes `...Core.Lot`. A FAIL with the counters passing = the name is wrong, read the hitch lines.
- [inferred, I-169-2] With `[RouteAsync] FallbackAsync=1` (tool-check ini) the stuck check's failed plans in the alarm are foreign-task plans: the world lot task may sleep inside the alarm while the worker plans (docs/134 §8), so the off arm can show several shorter world lot runs instead of one long one; hence `@50`.

Measurement queue `tools/harness/queues/script-hitch-loops.json` (10 min each, clean night): the same jobs with `-loop` repros, counters `>=10`, task rule `>=5`. Read `hitch-ab.ts --scenario <tag>` and `--causes`.

### Built

- `mods/ts3fixes/src/HitchTriggers.cs`: `Fixes.StuckCheckNow(int spreadMinutes, int attempt)` (test entry point, no switch). ts3fixes built; selftests easpread, errortrapspread, overwatchspread, nostart, slicequeue PASS (StuckCheckNow is reflection over game types only; first exercised by the check job).
- `mods/ts3agent/src/plugin/Handlers/HitchReproHandlers.cs`: `strand_many {count?, tries?}`; agent built.
- `tools/harness/hitch-tasks.ts`, `--hitch-tasks` / `profile.hitchTasks`; `queue.ts --self-check` ok, both queues dry-run ok.
- Repros `hitch-{stuckcheck,easpread-lots,easpread-pets,easpread-decay,errortrap}-{loop,check}` in `tools/harness/repros.json`.

Game runs: 5 check jobs (~10 min each with load) then 5 loop jobs (~15 min each, clean night). Ledger §266.
## Routing

**Built 2026-10-22, static only: agent compiled, queue self-check and dry-run ok, never run in game.** Ledger §267.

docs/163 found that the multi-second `rare-route-bits` plans were our own `SimRouteTo`, not play. These repros make **the game's own callers** plan, many times per run, in a loop: the agent only places Sims, picks goals and arms the game's own alarms. The state is re-created each loop and the switches flip per loop (ABBA, off-on-on-off), so one load gives both arms.

### Agent commands (`mods/ts3agent/src/plugin/Handlers/RouteFreezeHandlers.cs`)

| Command | What it does |
|---|---|
| `strand_crowd {lot \| x,z, tries, max}` | Stands every `spawn_sims` Sim on a spot of the lot that fails the game's `Sim.IsPointInLotSafelyRoutable` (enclosed room, fenced corner; no pool tiles), interactions cancelled. Spots are sampled once per lot (one plan per sample, agent task) and reused every loop. Refuses the active household's lot (the stuck check skips it). |
| `stuck_check_fire {attempt, delay_min, lot \| x,z}` | Arms the game's own `LotManager.RunStuckCheckHelper` (internal, by reflection) for every spawned Sim off the active lot: the game's alarm on `AlarmManager.Global`, `delay_min` sim-minutes (default 1) instead of 20-60, attempt given. The game's body then plans its 10 random lot points (WorldSea included); at attempt 3 with all failed it calls `SetObjectToReset` (the reset's safe-location search, docs/141 §4 #1). |
| `crowd_unreachable {lot \| x,z, household, max}` | The game's Go Here (as `crowd_go`) for every spawned Sim (and the household) to one unroutable spot of the lot: every plan floods the start's reachable area, then fails (docs/142). |
| `crowd_sea_lot {max}` | Go Here to the community lot with the most sea inside its area (16 `GetRandomPosition(WorldSea)` draws per lot, no plans). |

`profile-world.ts` adds the repro step **`native_set {switch: value}`** (handled by profile-world, not the agent): ts3native runtime switches by the `--ab` names (`fallbackasync`, `earlyout`, `fbjobcap`, ...) plus the per-plan values `synccapus` (`[RouteAsync] SyncCapMs` in us, +0x3164) and `fbjobcapus` (`FbJobCapMs` in us, +0x3550), which the game reads per plan/job. `repro.ts` (standalone runner) does not know it.

### Repros and the SV 7 s trigger

| Repro (world) | Loop (re-created each time) | Callers that plan | Events per loop [inferred] | Switches per loop |
|---|---|---|---|---|
| `route-stuckcheck-reset-loop` (BP) | 20 Sims stranded on Hogan's diner lot (`x 312.4, z 1496.7`, the `bp-idle-unroutable` lot), stuck check attempt 3 in 1 sim-minute; 8 loops x ~31 s | stuck-check alarm body (or ts3fixes' `stuckcheckasync` drain, which the job turns on), then the game's reset -> `AttemptToFindSafeLocation` -> `IsPointInLotSafelyRoutable` (`PlanFromPoint`, sync on the sim thread: `NoStart`) | 200 failed stuck-check plans, 20 resets with >= 3 failed safe-spot plans each | `stuckchecknear`, `failrepeat`, `safespotcap` |
| `route-queuefull-replan-loop` (BP) | 30 spawned Sims + household: Go Here to the far lot, build mode in/out while they route, Go Here home through the doors, build mode in/out; 6 loops x ~40 s | Go Here (own task, 30+ STARTs in one tick), `ExitBuildBuyMode -> OnBuildBuyExit -> ForceRouteReplan` per routing Sim, door actions' sync `ReplanFromPoint` | refused STARTs past the 8 / 32 job slots (`QueueFull`), `IC ReplanFromPosition, reason NoStart` after each exit | native `fallbackasync` (0 = 8 slots, foreign sync) |
| `route-nopath-crowd-loop` (BP) | 30 + household Go Here to one enclosed spot of Hogan's diner lot, then home again; 7 loops x ~32 s | Go Here (own task; plans past the slots run sync as `QueueFull`) | 30+ failed plans, each a full flood | native `earlyout` (eoMode) and `failrepeat` |
| `route-sv-seacheck-freeze` (SV) | clock to 2 AM (the game's 3 AM cleanup also arms its checks), 30 Sims Go Here to the sea lot and stand, stuck check attempt 1 in 1 sim-minute for those on it; 4 loops x ~72 s | stuck-check alarm body: 10 points per standing Sim, sea points fail after flooding the reachable world | sea-goal failures back to back (the 7 s shape) | `synccapus` 0 / 1,000,000, `fbjobcapus` 0 / 300,000, `stuckchecknear`, `failrepeat` |

**The SV 7 s freeze** (docs/161: SV crowd 2.6 plans >= 1 s per 10 min, max 7.4 s; docs/163 §1: 7,361 + 7,350 ms failed `Route_Plan`s, `NoStart`, ThreadedObjects stage, ~0.3 s apart, 30-45 s after the third crowd wave with the clock at ultra across several 3 AMs, no script stack). Reading [inferred]: two equal multi-second failures back to back in a Sim/lot task fit one caller's candidate loop failing twice on the same kind of goal; the stuck check is the only caller in the docs/163 ranking that runs at 3 AM, plans up to 10 points per Sim back to back inside one alarm, and draws sea points (`WorldTerrain | WorldSea | LotTerrain`), each of which floods the whole reachable world before "no path" (docs/142 §2). Crowd Sims left standing on a lot with sea after wave 2 match the precondition. `route-sv-seacheck-freeze` reproduces exactly that, with `LogFallbackStack=16` so a hit names its frames. If its failed plans >= 1 s carry no `RunStuckCheckHelper` frame, or there are none, the hypothesis is wrong (I-169-R3) and the stacks of the next SV crowd run decide.

`failrepeat` (docs/163 §4) answers a Sim's repeat of its own failed plan within 500 ms; whether any repro here makes the game repeat that fast is open (the stuck check's 10 points are different goals): read `failrepeat_answered` per arm.

### Jobs: `tools/harness/queues/repro-check-route.json`

Four 5-min jobs (SV 6 min) with the `tool-check.json` settings (`asi.0330`, release ini + `LogFallbackStack=16`, `RouteJobPhases SyncPlans=1`, seed 12345, no samplers, camera pan, the same `fixesOn`). Pass = no crash **and the events fired**, by two new queue fields (queue.ts, self-checked):

- `nativeRequire: "name>=n,..."` over the job's `/native` after - before (arrays as `name.i`, e.g. `sypPlans.1` = failed sim-thread sync plans), with the `fixes-require.ts` rule syntax (now also camelCase names);
- `logRequire: {"<regex>": n}` = at least n lines in the job's `ts3native.log` copy (sync plan lines are capped at 20 per minute, so keep n low).

| Job | nativeRequire | logRequire | fixesRequire |
|---|---|---|---|
| `bp-route-stuckcheck-reset` | `sypPlans.1>=20` | `route phases SyncPlans` 1 | `stuckcheckasync_run>=20` |
| `bp-route-queuefull-replan` | `asyncQueueFull>=20` | `reason QueueFull` 5, `reason NoStart` 5 | - |
| `bp-route-nopath-crowd` | `eoAsFail>=20` (on arms only count) | `route phases SyncPlans` 1 | - |
| `sv-route-seacheck-freeze` | `eoAsFail>=20` | `route phases SyncPlans` 1 | `stuckcheckasync_run>=20` |

~35 min for the queue, 4 game runs, daytime OK (correctness only). Then: `nostart-report.ts --filter repro-check-route --jobs` (callers by stack), `route-phases-report.ts --sync <log> --min 300` (phase split), `hitch-ab.ts --scenario` for the per-loop windows. For a timing A/B, the same repros at 10 min on a clean night, arms read from the step notes (`arm on #i` / `off #i`).

### Open

- I-169-R1..R5 (ledger §267). The N values are first guesses; set them from the first run.
- Hogan's diner spot: `strand_crowd` / `crowd_unreachable` fail the step (and the job) when no unroutable spot is found in 60 samples; then pick another lot by `x,z`.
