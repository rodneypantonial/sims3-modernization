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

**Agent commands** (handlers in `mods/ts3agent/src/plugin/Handlers/`): harness control (`game_state`, `load_save`, `save_game`, `autopilot` holds speed 3 and clears popups, `keep_alive`, `decline_requests`, `quiet_ui`), repro triggers (`place_sim`, `sim_go_home`, `sim_visit_lot`, `sim_route_to`, `sim_strand`, `sim_portal_trip`), fix switches and counters (`patches`, e.g. `patches '{"gohome":false}'`), content (`catalog_search`, `buy_object`, `place_objects`, `cas_open`, `cas_category`, `label_check`), travel (`travel_*`) and reflection helpers. `node tools/harness/build-agent.ts --hot` plus `reload-agent.ts` swaps the plugin without a restart.

## 2. Repro first

Every fix and every bug gets a repro: a saved situation plus agent steps that trigger it, and a check.

- Entries: `tools/harness/repros.json` (`{ id, bug, fix, save, steps: [{cmd, args, waitS?}], watch, expect }`).
- `watch.kind "stuck"`: PASS when the watched Sims (or all) are not stuck for `watch.minutes`. `watch.kind "counter"`: PASS when `patches.<counter>` rose by at least `watch.min`.
- Run: `node tools/harness/repro.ts run <id> [--set a=true,b=false]`. It reloads the save unless `--no-load` (use `--no-load` for a second pass in the same load). `run-many id,id` runs several repros in one load when their areas are 150 m apart and they share no Sims.
- Off/on: `--set <switch>=false` runs the same repro with the fix off. A fixed bug's repro stays as a regression test.
- `node tools/harness/stuck-scan.ts` runs alongside and drafts a repro entry for every other stuck Sim it sees (`probe-out/repro-candidates.json`).
- A run that ends with stuck Sims saves `Repro-<tag>-<HHMM>` (`repro-save.ts`). Saves keep positions, not interactions, so problems tied to a place come back on load.
- No waiting for a problem to happen: add a trigger action to the agent instead.

Quick checks of a trip shortcut: `node tools/harness/verify-shortcut.ts --name rabbithole,visit [--tries 20] [--minutes 5]`.

## 3. Profiling and A/B

`node tools/harness/profile-world.ts --tag <tag> [--minutes 10]` on a loaded world: autopilot at speed 3, `keep_alive` and `decline_requests` every 30 s, the native stutter profiler (`tools/frida/stutter-profile.js`: sim iterations, hitches >= 50 ms, route search, GC, finalizers) and, in turn, the managed C# sampler (minutes 1-4) and the native sim-thread stack sampler (5-8). It counts stuck Sims at the end. Output: `probe-out/profile/<tag>-*`.

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

**Milestone:** `node tools/harness/milestone.ts [--worlds sv,bp,ip] [--runs 1] [--minutes 10] [--label X]` runs a restart, then per world: load, test B, profile; it writes one table against the no-mod baselines to `docs/data/milestone-<label>-<date>.md`. `--report` rebuilds the table from existing data. Full per-world runs are for milestones only; single fixes use repros and in-load A/B.

Other tools: `travel-trip.ts` (travel legs), `save-stats.ts` / `save-sources.ts` (save growth by resource type and by script class), `offload-phase0.ts` (load memory with and without CC), `tools/probe/gc-read.mjs` (GC counters), `tools/probe/managed-sample.mjs` (C# sampler on its own).

## 4. Safety nets

- **Game alive:** `profile-world.ts`, `tests-be.ts` and `repro.ts` watch the game (`game-alive.ts`): TS3W.exe gone, agent heartbeat frozen or probe unreachable for 60 s prints `FAIL: game died ...` with the last crash triage and exits 1. Without it a crashed game reads as "no stuck Sims".
- **Watchdog** for long runs: `node tools/harness/watchdog.ts [--every 1]` checks game, probe, sim clock, dialogs and address space, recovers simple cases and exits non-zero with a diagnosis.
- **Focus:** `node tools/harness/focus-game.ts` brings the game to the front (`cycle.ts --save` and `profile-world.ts` call it). Unfocused, the game drops below 60 FPS and runs do not compare.
- **Change ledger:** every `cycle.ts --save` appends the active change set (git hash, dirty flag, installed fixes, note) to `docs/data/change-ledger.jsonl` (`crash-ledger.ts snapshot --note "why"` by hand).
- **Crash triage:** `node tools/harness/crash-ledger.ts triage` maps each game crash report (`xcpt*.txt`) to the change set active then and what changed just before. Triage every crash before the next run. Keep notable crash reports in `docs/data/crashes/` (strip personal paths).

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
