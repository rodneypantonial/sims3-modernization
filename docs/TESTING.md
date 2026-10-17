# Testing

How we measure a change, from a clean game start to a result table. Every number in the docs comes from these tools, not from a stopwatch. Build first (docs/BUILD.md); the probe needs Python with `frida` 17.

All commands run from the repo root. Paths: `<Documents>` = your Documents folder; saves live in `<Documents>\Electronic Arts\The Sims 3\Saves`. Harness output goes to `probe-out/` (git-ignored).

## 1. The pieces

| Piece | Command | What it does |
|---|---|---|
| Restart | `node tools/harness/cycle.ts --restart` | Detaches the probe, closes the game, rebuilds and installs our packages (`ts3agent`, `ts3fixes`), clears the script cache only when they changed. |
| Probe server | `python tools/probe/ts3probe.py serve --launch` | Starts the game under Frida and serves JSON on `127.0.0.1:47891`: `status`, `memory`, `simstack`, `managedstack`, `rescache`, `native` (ts3native counters and switches), `net`, `taskstall`, `agent?cmd=...`, `agentstatus`, `detach`. Start it separately: it must outlive the scripts. Without `--launch` it attaches to a running game. |
| Load a save | `node tools/harness/cycle.ts --save Baseline-BP.sims3` | Waits for the agent, loads the save, waits for the world-loaded flag, ends in live mode, focuses the game window and records a change-ledger snapshot. |
| Agent command | `node tools/harness/ag.mjs <cmd> '<json>'` | One command to the in-game agent (`mods/ts3agent`), full JSON reply. |
| Stop | `python tools/probe/ts3probe.py get detach` | Always stop the probe this way, never by killing Python. |

Typical start: `cycle.ts --restart`, then `ts3probe.py serve --launch` (in its own terminal or in the background), then `cycle.ts --save <save>`.

**Agent commands** (handlers in `mods/ts3agent/src/plugin/Handlers/`): harness control (`game_state`, `load_save`, `save_game`, `autopilot` holds speed 3 and clears popups, `keep_alive`, `decline_requests`, `quiet_ui`), repro triggers (`place_sim`, `sim_go_home`, `sim_visit_lot`, `sim_route_to`, `sim_strand`, `sim_portal_trip`), fix switches and counters (`patches`, e.g. `patches '{"gohome":false}'`), content (`catalog_search`, `buy_object`, `place_objects`, `cas_open`, `cas_category`, `label_check`), travel (`travel_*`), clock state (`clock_state`, read-only: GameClock pause bits, game dt, speed, managed speed, game minutes since the previous call; the probe adds `window.iconic` and `window.foreground`) and reflection helpers. Probe-side commands on the same `/agent` endpoint (the game's Mono has no user32): `window_minimize` and `window_restore` (`ShowWindowAsync` `SW_SHOWMINNOACTIVE` / `SW_SHOWNOACTIVATE`, never focus or move; approved only for the `clock-minimize-pause` repro, docs/85 §6). `node tools/harness/build-agent.ts --hot` plus `reload-agent.ts` swaps the plugin without a restart.

## 2. Repro first

Every fix and every bug gets a repro: a saved situation plus agent steps that trigger it, and a check.

- Entries: `tools/harness/repros.json` (`{ id, bug, fix, save, steps: [{cmd, args, waitS?, until?}], watch, expect, cleanup? }`). A step's `until` polls a command until every condition holds and FAILs after `maxS`; `cleanup` steps run before any FAIL exit (e.g. `window_restore`).
- `watch.kind "stuck"`: PASS when the watched Sims (or all) are not stuck for `watch.minutes`. `watch.kind "counter"`: PASS when `patches.<counter>` rose by at least `watch.min`.
- Run: `node tools/harness/repro.ts run <id> [--set a=true,b=false]`. It reloads the save unless `--no-load` (use `--no-load` for a second pass in the same load). `run-many id,id` runs several repros in one load when their areas are 150 m apart and they share no Sims.
- Off/on: `--set <switch>=false` runs the same repro with the fix off. A fixed bug's repro stays as a regression test.
- `node tools/harness/stuck-scan.ts` runs alongside and drafts a repro entry for every other stuck Sim it sees (`probe-out/repro-candidates.json`).
- A run that ends with stuck Sims saves `Repro-<tag>-<HHMM>` (`repro-save.ts`). Saves keep positions, not interactions, so problems tied to a place come back on load.
- No waiting for a problem to happen: add a trigger action to the agent instead.

Quick checks of a trip shortcut: `node tools/harness/verify-shortcut.ts --name rabbithole,visit [--tries 20] [--minutes 5]`.

## 3. Profiling and A/B

`node tools/harness/profile-world.ts --tag <tag> [--minutes 10]` on a loaded world: autopilot at speed 3, `keep_alive` and `decline_requests` every 30 s, the native stutter profiler (`tools/frida/stutter-profile.js`: sim iterations, hitches >= 50 ms, route search, GC, finalizers) and, in turn, the managed C# sampler (minutes 1-4) and the native sim-thread stack sampler (5-8). It counts stuck Sims at the end. Output: `probe-out/profile/<tag>-*`. `--mainstack main|all|<id>` (off by default) also samples the main thread (or every non-sim thread) beside the sim sampler into `<tag>-mainstack.jsonl` (docs/20 §7).

**Machine load (2026-10-17):** every profile run writes `<tag>-machine.jsonl` every 30 s (`tools/harness/machine-load.ts`: CPU of all processes except TS3W in % of the machine, top 3, TS3W's own share, `% Processor Performance` clock) and ends with a `machine load:` line; mean other CPU >= 10% or the clock below 80% of its first sample marks the run NOT comparable. Self-check: `node tools/harness/machine-load.ts`. First catch: Maven tests from another session at 22-32% of the machine during `ip-env-off`.

**Run length (decided 2026-10-17):** performance A/B between loads keeps `--minutes 10`. Smoke, crash and "does the switch work" runs, and route-cost checks, may use 5 minutes or less. Why: the town's load builds over the run; in seeded BP runs async on and off were equal in minutes 0-5 (37.5 vs 37.5 iterations/s) and only split in minutes 5-10 (37 vs 30.5, hitches 187 vs 251/min). Repeats of one build agree within ~1% (iterations/s) and ~4% (hitches) even at 5 minutes; route ms/s splits early (45 vs 105); p99 is noisy at any length. Load and setup add 3-5 min per job, so 5-minute runs save about a third, not half. Open idea: a warm base save taken after ~5 min of seeded play, valid for 5-minute A/B only if a 5-minute run on it matches minutes 5-10 of a normal run.

- **In-load A/B** (preferred for single fixes): `--ab a,b --slice 2` switches the named fixes on and off every 2 minutes in one load, starting on. Names are `patches` switches of `ts3fixes`, or `routeworker`, `routebudget`, `routeasync`, `alloc`, `waterjps` for ts3native. Samplers are skipped in this mode. One load removes load-to-load noise; separate restarts per fix are only for things that cannot switch at runtime.
- **`--taskstall MS`**: also times every stage of the sim loop and logs stalls over `MS` ms with their managed stack (`probe-out/<session>-taskstall.jsonl`). The managed sampler is skipped then (two samplers pausing the sim thread at once deadlock Frida's agent). S3PP's `SimulatorStageProfiler` must be off.
- Compare: `node tools/probe/profile-compare.mjs <tag> <tag>...` (A/B tags also give `<tag>:on` and `<tag>:off`; the first 15 s after each switch are dropped).
- Gate: `node tools/harness/run-check.mjs <tag> <reference> [--max-stuck 3] [--min-ratio 0.9]` exits 1 on an obvious regression.

**Tests B-E** (docs/02 §1): `node tools/harness/tests-be.ts --tag <world>-<layer> [--only B,C,D,E]`.

| Test | Measures |
|---|---|
| B | frame times for 60 s: FPS, p50/p95/p99/p99.9/max, long frames per minute, and which long frames overlap a sim-thread hitch (frame hook on the per-frame function `0xEC9F00`, docs/04 F19) |
| C | travel hitch: the active Sim visits a community lot and goes home; trip time and worst frames around arrival |
| D | memory over `--minutes` (committed, largest free block, private bytes) |
| E | save time and size (`TestE-<tag>`, never overwrites the loaded save) |

Run B on a throwaway save first. `tests-be.ts --self-check` tests parsing without the game.

**Save safety matrix** (before releasing a save-path fix): with a world loaded, `node tools/harness/save-matrix.ts [--cases a,b] [--travel]` (`--list` for the cases, `--self-check` without the game). In one paused load it switches the save fixes at runtime (ts3fixes `fastreftable`, ts3native footprint index, save compress, save flush) and compares saves resource by resource: `control` (off/off: which resource types change on every save), `identical` (all off vs all on), `per-fix` (each fix alone), `overwrite` (`.backup` equals the previous save), `back-to-back`, `unpaused`, `build-mode`, `verify-counters`, `reload` (saves all off, then all on at the same paused moment and prints the restart and load commands; `--reload-check` after loading the ON save, `--reload-check --off` after loading the OFF save; the later check compares both and fails only on ScriptErrors with ON that OFF does not have) and, opt-in, `travel` (`--to france|china|egypt|university|future`; real queued trip after the clean-repro steps in docs/19, `--direct` skips the University/future interaction; fixes the game does not have, e.g. `cachedreflection` when its startup probe refused it, are listed as info there, not failed; the start log names why a fix is not available). Every save is a new `Matrix-<world>-*` (Baseline-* refused; about 20 saves, several GB, delete them after). Results: `probe-out/save-matrix/<date>.jsonl` and `docs/data/results/<date>-save-matrix.md`.

**Milestone:** `node tools/harness/milestone.ts [--worlds sv,bp,ip] [--runs 1] [--minutes 10] [--label X]` runs a restart, then per world: load, test B, profile; it writes one table against the no-mod baselines to `docs/data/milestone-<label>-<date>.md`. `--report` rebuilds the table from existing data. Full per-world runs are for milestones only; single fixes use repros and in-load A/B.

**Queue (unattended):** `node tools/harness/queue.ts tools/harness/queues/<name>.json [--from <job>] [--only a,b] [--dry-run]` runs jobs back to back (example: `queues/example.json`). Per job: `cycle.ts --restart`, the job's `asi` (`tested` or a path) into Bin, Bin `ts3native.ini` = repo ini + the job's `"Section.Key": value` (fresh each job), optional `install-fixes.ts` changes (undone after the job), its own `ts3probe.py serve --launch`, `cycle.ts --save` (or `"save": "menu"`), then one run (`repro`, `matrix` args for `save-matrix.ts`, `soak` minutes with `watchdog.ts`, `menuMemory`), `/memory` and the matching `/native` counters before and after, game closed. A crash is triaged (`crash-ledger.ts`) and the queue goes on; it stops when the same job crashes twice (today's rows count) or two jobs crash in a row, and pauses at >= 80% AI usage (`ai-usage.ts G <job>`) with a `--from` resume line. Rows: `probe-out/queue/<queue>-<date>.jsonl`, table: `docs/data/results/<date>-queue-<queue>.md`; the tested build and the start ini go back into Bin at the end. `--self-check` without the game.
Per-job `"content"`: `as-is` (default), `no-cc` (moves the `user-cc` + `store-repacks-test` sets of `tools/setup/fixes.json`, the ones `offload-phase0.ts` switches, out of the live `Mods`), `no-store` (Store repacks only). With the game closed, a manifest is written to `<Documents>\Electronic Arts\The Sims 3 content-stash\manifest.json`, then the packages move there; after the job they move back and caches are cleared (`install-fixes.ts install --only cache-clear`, also right after the move; the next `cycle.ts --restart` clears scriptCache because Mods changed). `ts3agent`/`ts3fixes`/`ts3-nointro` and files other fixes installed are never moved. A leftover manifest (interrupted run) blocks the queue: `node tools/harness/queue.ts --restore-content` puts everything back and never overwrites a file in Mods. Example pair (M9 catalog cost at the menu): `menu-cc-as-is` / `menu-cc-none` in `queues/example.json`.
**Queue (unattended):** `node tools/harness/queue.ts tools/harness/queues/<name>.json [--from <job>] [--only a,b] [--dry-run]` runs jobs back to back (example: `queues/example.json`). Per job: `cycle.ts --restart`, the job's `asi` (`tested` or a path) into Bin, Bin `ts3native.ini` = repo ini + the job's `"Section.Key": value` (fresh each job), optional `install-fixes.ts` changes (undone after the job), its own `ts3probe.py serve --launch`, `cycle.ts --save` (or `"save": "menu"`), then one run (`repro`, `matrix` args for `save-matrix.ts`, `soak` minutes with `watchdog.ts`, `menuMemory`), `/memory` and the matching `/native` counters before and after, game closed. A crash is triaged (`crash-ledger.ts`) and the queue goes on; it stops when the same job crashes twice (today's rows count) or two jobs crash in a row, and pauses at >= 80% AI usage (`ai-usage.ts G <job>`) with a `--from` resume line. Rows: `probe-out/queue/<queue>-<date>.jsonl`, table: `docs/data/results/<date>-queue-<queue>.md`; the tested build and the start ini go back into Bin at the end. `--self-check` without the game. Also `profile` runs (`{minutes, seed?}`: `profile-world.ts`, `--seed` calls `seed_rng` at the window start), `gameArgs` for TS3W.exe (e.g. `-disablestoryprogression`) and `defaultIni` (`"Config.Key": value` merged into Bin `Default.ini` for the job, then put back).

**Seeded RNG (docs/27, PLAN.md lane step 4):** `node tools/harness/queue.ts tools/harness/queues/seeded-rng.json` (~2 h: 3 seeded + 3 unseeded 10-min Baseline-BP profiles, alternating, story progression off, ~15 min each; then 3 optional menu jobs for the `[Config] SimulatorRandomSeed` Default.ini check, ~5 min each), then `node tools/harness/rng-compare.ts` prints per metric (sim minutes, it/s, route plans, route fail %, hitches, autonomy picks, pick overlap) the mean and sd per group and the seeded/unseeded variance ratio, plus the Default.ini verdict. Remaining variance: record it here once run. `--self-check` without the game.

**Save growth, overnight (PLAN.md lane G 10, task 8):** `node tools/harness/queue.ts tools/harness/queues/overnight-age.json` (~1-1.5 h: Baseline-BP aged 14 sim days at speed 3, checkpoint every 2). Run kind `{age: {days, checkpoint, keepAlive?, save?}}` runs `age-save.ts` on the loaded save under a new name (`save`, default `<job id>-<date>`; refused when it starts with `Baseline-` or a save of that name or `<name>-dayN` exists). For the loaded save and each checkpoint it records folder MB, bytes/count per resource type (`save-stats.ts`), the `save: result ... total N ms` line of `ts3native.log`, `/memory` and the sim day in `row.checkpoints` and `probe-out/queue/<queue>-<date>-<job>-age.csv`. The md row and `node tools/harness/age-report.ts <csv>` give growth MB/day, save s/day, largest-free trend and the top growing types. `--self-check` without the game.

Other tools: `travel-trip.ts` (travel legs), `save-stats.ts` / `save-sources.ts` (save growth by resource type and by script class), `offload-phase0.ts` (load memory with and without CC), `tools/probe/gc-read.mjs` (GC counters), `tools/probe/managed-sample.mjs` (C# sampler on its own).

## 4. Safety nets

- **Game alive:** `profile-world.ts`, `tests-be.ts` and `repro.ts` watch the game (`game-alive.ts`): TS3W.exe gone, agent heartbeat frozen or probe unreachable for 60 s prints `FAIL: game died ...` with the last crash triage and exits 1. Without it a crashed game reads as "no stuck Sims".
- **Watchdog** for long runs: `node tools/harness/watchdog.ts [--every 1]` checks game, probe, sim clock, dialogs and address space, recovers simple cases and exits non-zero with a diagnosis.
- **Focus:** `node tools/harness/focus-game.ts` brings the game to the front (`cycle.ts --save` and `profile-world.ts` call it). Unfocused, the game drops below 60 FPS and runs do not compare.
- **Change ledger:** every `cycle.ts --save` appends the active change set (git hash, dirty flag, installed fixes, note) to `docs/data/change-ledger.jsonl` (`crash-ledger.ts snapshot --note "why"` by hand).
- **Fail-fast guards:** see below; no wait sits for long when nothing is happening.
- **Clock guard** (`profile-world.ts`): game minutes unchanged at 2 checks 30 s apart while not paused = `FAIL`; on the trip it logs `clock_state` (pause bits: 2 = window minimized, 4 = device; `window.iconic`) to the run log (docs/85). The probe also logs `game window minimized` / `restored` events with UTC to `events.jsonl`, and the stutter profiler writes `simPaused` / `stepAcc` per second.
- **Crash triage:** `node tools/harness/crash-ledger.ts triage` maps each game crash report (`xcpt*.txt`) to the change set active then and what changed just before. Triage every crash before the next run. Keep notable crash reports in `docs/data/crashes/` (strip personal paths).

### Fail-fast guards

Rule (Rodney 2026-10-16): a wait checks every 30 s (or faster) that the thing it waits for is still alive; two misses in a row (~1 min) end it with a specific reason in the log or verdict. The old long timeouts stay as a last resort for slow work that is still moving. A guard never turns a FAIL into a PASS or changes what PASS checks; it only fails a dead wait sooner. `until()` in these scripts swallows errors thrown inside its check, so guards run outside the check (the `guard` argument) or set a flag the caller throws after.

Generic guard (`game-alive.ts` `stallGuard`): samples at most every 30 s; a miss is TS3W.exe gone, the probe not answering, or none of agent heartbeat, world-loaded flag, committed MB (`/memory`, whole MB) and `ts3native.log` size changed since the last sample. Loading screens pass it as long as something moves; they are never judged on wall time alone. `liveOnly` (game start) counts only "TS3W.exe gone" and samples first after 30 s. Worst case: ~60 s after the last sample that saw progress, so 60-90 s after the game stops moving, plus up to 30 s of request timeouts. The heartbeat moves while the clock is paused, so in a loaded world the generic guard only catches a dead or hung game; in-world waits add their own signal (save in progress, trip in the queue, sim clock).

| wait | file:line | old timeout | new guard | signal used | worst case to fail now |
|---|---|---|---|---|---|
| agent answering (game start) | `cycle.ts:86` | 300 s | generic, `liveOnly` | TS3W.exe running | ~60-90 s after the exe is gone |
| Mod Scripts dialog wait at the menu | `cycle.ts:90` | 150 s loop | generic | process, probe, heartbeat (moves at the menu) | ~60-90 s |
| `load_save` reply retries | `cycle.ts:108` | 24 x 5 s (+ replies) | generic (shared with the load) | process, probe, heartbeat / flag / commit / log | ~60-90 s |
| world loaded | `cycle.ts:123` (`loadGuard`) | 600 s | generic | process, probe, heartbeat / world flag / commit / log | ~60-90 s of no movement |
| live mode after Edit Town | `cycle.ts:152` | 120 s | generic | process, probe, heartbeat | ~60-90 s (dead game only) |
| Mod Scripts dialog wait at the menu | `new-save.ts:47` | 150 s loop | generic | process, probe, heartbeat | ~60-90 s |
| world loaded / household active / live mode | `new-save.ts:29` (`until` default guard) | 900 / 180 / 180 s | generic per wait | process, probe, heartbeat / flag / commit / log | ~60-90 s |
| saved | `new-save.ts:76-83` | 240 s | generic + save idle | agent answers, `save_in_progress` false and no `async_log` line at 2 checks 30 s apart | ~60 s |
| save (case) | `save-matrix.ts:258-264` | 600 s | save idle | agent answers, `save_in_progress` false and no `save:` line in ts3native.log at 2 checks 30 s apart | ~60 s |
| left the start world | `save-matrix.ts:476` | 900 s | trip in queue (df4dcaa) while not `in_travel`; no-progress rule (`tripStall`) once `in_travel` | travel / moving van / portal interaction in the household queue; "left" = start world unloaded or another world answers (`in_travel` alone no longer ends the wait) | ~60 s; no progress: 90 s |
| arrived in live mode | `save-matrix.ts:495` | 1200 s | trip aborted (df4dcaa) + no-progress rule + generic | back in the start world, not travelling; loading screen: process, probe, counters | ~60-90 s; no progress: 90 s |

No-progress rule for trips (`tripStall`, Rodney 2026-10-16): world loaded, `in_travel` true, no save running (`save_in_progress`), and still in the start world or in another world with no active household, for over 90 s (`STALL_S`) = FAIL `no progress: ...`. The clock is shared by both waits, restarts when the world unloads or a new `save: ... travel 1/1` line appears. It catches the two 2026-10-16 hangs: University arrived with no household for 20 min, and the future trip set `in_travel` but never left Bridgeport.

Both trip waits also read the agent's `async_log` every ~5 s: a new `travel_*` or `uni_move_in` entry ending in `-> error` (a game-task exception, logged with its inner chain and stack) fails the leg at once with that entry. University: the arrival wait answers the play-flow lot picker with `uni_move_in` (docs/19), and the fly-through (300 s) and the move-in mini load (180 s) do not count as a stall. Arrival now also needs an active household.
| build mode / live mode | `save-matrix.ts` build-mode case | 120 s | none (short); `watchGame` covers a dead game | heartbeat | 70 s (watchGame) |
| saved (travel leg) | `travel-trip.ts:66-72` | 300 s | generic + save idle | as new-save | ~60 s |
| left the home world (boarding) | `travel-trip.ts:102-105` | 600 s | trip in queue + generic | travel / moving van / portal / university / future interaction in the household queue | ~60 s |
| arrived in live mode | `travel-trip.ts:114-119` | 900 s | trip aborted + generic | as save-matrix | ~60-90 s |
| live hours at the destination | `travel-trip.ts:141` | none (loop until the clock reached N hours) | clock moving | `time` answers and `minutes_total` changes at speed 3 | 60-75 s |
| checkpoint / final save | `age-save.ts:73-83` | 120 x 2 s, then printed "saved" anyway | generic + save idle; prints `save X FAILED: why`, no `saved` line (queue.ts: no checkpoint, no final save = FAIL) | as new-save | ~60-90 s |
| main loop, agent not answering | `age-save.ts:109-130` | none (retried forever) | generic, reset on every answer | process, probe, heartbeat / commit / log | ~2-3 min (each silent pass waits out ~5 agent timeouts) |
| main loop, sim clock | `age-save.ts:137` | logged only after 6 checks (3 min) | clock moving; one live-mode recovery try at the first miss | `minutes_total` unchanged at 2 checks 30 s apart while the autopilot holds speed 3 | ~60-90 s |
| out of a world (menu, loading, agent silent) | `watchdog.ts:115` | none (logged "not in a world yet" forever) | generic (`stallStep`) per check | process, probe, heartbeat / flag / commit / log | 2 checks after the first out-of-world one: ~2-3 min at `--every 1` |
| trip leg (test C) | `tests-be.ts:212` | 300 s per leg | Sim still on the trip | the Sim has an interaction at 2 checks 30 s apart | ~60 s |
| save (test E) | `tests-be.ts:271` | 600 s | save never started | agent answers, not in progress, no file written after t0, at 2 checks 30 s apart | ~60 s |
| stutter profiler exit (test B) | `tests-be.ts:173` | none (`await profDone`) | profiler killed 60 s past its `-t` (+30 s attach) | process still running | B + 95 s |
| stutter profiler exit | `profile-world.ts:69` | none (`await profDone`) | profiler killed 90 s past `-t`, `FAIL:` line | process still running | run + 90 s |
| any child after the game is launched | `queue.ts:522-527` (`run`) | job deadline (default 60 min) | probe server child alive + TS3W.exe running, checked every 30 s; child killed, `FAIL: ... child stopped`, reason in `row.error` | probe child exit / signal, tasklist | ~60 s |
| main menu (menu jobs) | `queue.ts:611-621` | 300 s | same as `run` | probe child, TS3W.exe | ~60 s |

Not changed, already bounded or covered: `cycle.ts` game closed (60 s), settle (30 s), world state (60 s); `save-matrix.ts` save accepted (30 s), save flush (60 s), reload check (60 s sleep under `watchGame`); `repro.ts`, `stuck-scan.ts`, `verify-shortcut.ts`, `profile-world.ts` (in-world, `watchGame`: 60 s heartbeat / probe / process, checked every 10 s); `offload-phase0.ts` (load via `cycle.ts`; sampling loop fails after 6 misses of 10 s); `reload-agent.ts` (each call times out in 15 s); `milestone.ts` (child timeouts, loads via `cycle.ts`); `queue.ts` `closeGame` (60 s).

## 5. Stuck Sims

- Rule (`watchdog.ts`, `stuck-scan.ts`, `repro.ts`): same spot and interaction for 6 checks of 30 s, or routing without moving for 3 checks. `tools/harness/stuck-rule.ts` lists interactions that loop by design (e.g. Late Night bouncers) and are not stuck.
- Pass for a 10-minute run: stuck Sims no more than the same world without our mods, and at most one Sim stuck after one of our shortcuts moved it.
- **Log-only policy:** stuck Sims are treated as solved. Record possible cases in `docs/data/stuck-log.md` (date, run, Sims, interaction, lot, how it happened); research or fix only when a case repeats often or one of our fixes caused it.

## 6. Rules and gotchas

- Change one thing at a time. Stop a run on an obvious error or when the code it measures changed, fix, then rerun.
- Do not edit `mods/` or `tools/setup/` while a `cycle.ts --restart` or installer step can run: the restart rebuilds the mods from source. Keep `mods/ts3fixes/src` unchanged during a run that compares builds.
- The installed `ts3native.asi` is locked while the game runs: close the game before `install --only native-patches`.
- **Two Frida agents:** each Python host injects its own frida-agent. The one loaded last must detach first; detaching in load order kills the game. `tests-be.ts` loads the frame hook first and lets the profiler detach first.
- Probe: `get reload` swaps `agent.js` but can leave a dead script handle; prefer `detach` and a fresh `serve`. After changing `ts3probe.py`, detach and serve again (it re-attaches without relaunching). Frida 17 has no static `Module.findExportByName`; use `Process.findModuleByName(x).findExportByName(y)`.
- Slow agent replies while the native stack sampler runs (WinError 10053 in the serve log) are client timeouts, not a hang.
- New native code: test it on a dummy process that hits the same path before injecting into the game, and keep exception handlers opt-in.
- Never enter credentials for the game or EA services during tests.

## 7. Baseline saves

EA saves contain EA data and the maintainer's saves cannot be shared, so every contributor makes their own baselines. Your numbers then compare only with your own baselines, which is why reports always include both.

1. Install the community layer only (`install-fixes.ts install --skip stutter-fixes,native-patches`), plus the test agent (`cycle.ts --restart`).
2. New save from a premade household: `node tools/harness/new-save.ts --world "Bridgeport.world" --save Baseline-BP-fresh [--household Name] [--members 2]` (without `--household` it picks the first household with that many members, sorted by name, so it repeats). Our worlds: Sunset Valley (Keaton household), Bridgeport (Angelista household), Isla Paradiso (Dregg household on a houseboat).
3. Age it: `node tools/harness/age-save.ts --days 14 --save Baseline-BP --checkpoint 2` (speed 3, popups dismissed, active household kept alive, a checkpoint save every 2 sim days).
4. Profile the baseline: `cycle.ts --save Baseline-BP.sims3`, then `profile-world.ts --tag bp` (3 runs if you can). Then install our layer and profile again with a new tag.
5. Keep your baseline saves read-only; tests that save write new names.

## 8. Reporting results

Post results (in a PR or issue) as a table against your own baseline:

- what changed (fix, switch, commit), build hash (`git rev-parse --short HEAD`, plus `+dirty`), the `ts3native.ini` sections you changed, `ts3fixes` switches that differ from default, and the installed fix set (`install-fixes.ts status` or the change-ledger line);
- machine (CPU, RAM, GPU, Windows build), world and save, minutes, number of runs, in-load A/B or separate loads;
- sim iterations/s, hitches/s, worst iteration, route ms/s, GC ms/s, stuck Sims; for FPS work test B numbers; for memory test D;
- dates and times in UTC;
- mark each conclusion `[measured]`, `[verified]` or `[inferred]`.

Use the tables in `docs/04-re-findings.md` (F13, F15, F16) as the pattern. Never attach saves, game files or crash dumps with personal paths.

## Test world conditions (2026-10-17)

Goal: no skew in the data, while the world stays real. `profile-world.ts` calls the agent action `test_env` before seeding and again every 30 s: the save's own season, weather and moon play out, except the cases that skew results: **winter or extreme cold** (Sims freeze solid and stand still) switches to **spring**, and a **full moon** less than 3 days away (1 day during the run) moves the moon to **first quarter** (no zombies, no werewolf changes). Every correction is logged (`test_env guard: ...`). Only the running session changes; no game option is written. `test_env {mode: "fixed"}` gives identical conditions (summer, sunny 48 h, first quarter) when an A/B needs them; `--no-env` turns it off. Runs from before 2026-10-17 had winter and any moon, so take new baselines.

Fresh base saves: the agent action `fresh_world` maxes motives and drops temporary negative moodlets for every Sim in the world, repairs broken objects, cleans dirty objects, removes puddles, trash and ash piles, dirty dishes and spoiled food, empties trash cans, puts out fires and drops unpaid bills (no money charged; no Sim reset, relationships, skills or careers untouched). Only the `make-spring-*` repros that write the `-Spring` base saves use it, not every run: once at the start, then after the warm-up while paused, again after 20 s of normal speed, and a final `fresh_world {check: true, fail_if_dirty: true}` while paused fails the repro if anything is still not fresh before the save.

Stuck Sims: every check (`profile-world`, `watchdog`, `repro`, `stuck-scan`, `verify-shortcut`) uses `mayBeStuck` in `tools/harness/stuck-rule.ts`. Loop-by-design jobs (`Watch Door`) and still-by-design states (`Freeze Solid`, `Thaw`, `Pass Out`, `Faint`, `Sleep`, `Nap`) do not count unless the Sim is routing without moving.
