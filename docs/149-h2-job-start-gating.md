# 149. Design H2 again: no route job starts during the locomotion pass (`[RouteAsync] H2`)

Written 2026-10-22. Offline build only; the game was not run. ts3native **0.36.21**, counters **v85** (docs/registry.md; built as 0.36.20 / v84, renumbered at the merge after the v84 foreign job split). Combined build parked as `game-copy/ts3native.asi.0321` (sha256 in PLAN.md). Queue `tools/harness/queues/h2-ab.json` (not run). Ledger §224 (docs/51).

Tags as in AGENTS.md: **[verified]** = read in the disassembly, source or a run file; **[inferred]** = reasoning, for the queue to confirm.

## 1. Why again

docs/143 §8.14-8.15 measured that the sim thread's summed `+0x918` wait is conserved per acquirer: skipping a taker hands the wait to the next taker in the same tick, so the wait is bounded by the route job's hold, not by the number of takers. The levers left are a shorter or lent hold (P5b/P5c, docs/94 §22) or fewer job starts where the sim takes the lock most often. H2 (docs/86 §5) is the second kind: jobs queued while the sim is inside `Locomotion_Update` start right after it.

H2 was already built in 0.19.0 as `[LocoWindow]` (`locowin.cpp`, docs/86 §9). Its only live reads were `bp-g0w-shadow` (Mode 0, 2026-10-18: 25,334 passes, job running at pass entry 1,280, starts inside a pass 213; read for gate 0 only) and one single-order `--ab locowin` job on a noisy night (docs/136 §3.7, read as a no-op for gate 0). Neither measured what H2 is for in the docs/143 sense: all in-pass `+0x918` waits, both slice orders. So this build reuses the thunk and adds the switch, the counters and the both-order queue; no second copy of the mechanism.

## 2. Pass boundaries

| Boundary | Where | Tag |
|---|---|---|
| Pass = one call | `MonoScriptHost_Simulate 0xD81840` calls `Locomotion_Update 0x83CED0` once per tick at `0xD81C5C` (`fld [esp+58h]; push ecx; fstp [esp]; call`, ecx = `[0x11D9140]`); `thiscall`, one float, one `ret 4` at `0x83D383` | **verified** (capstone, docs/86 §9; the install checks the 13 bytes from `0xD81C54`; steerro-test H patches the mapped site, 2 x 3000 seeds this build) |
| Inside: steering | per walking Sim: `Locomotion_UpdateAgent 0x957210` (`0x83CF52`) -> obstacle list `0x9573B7` -> `0x85FD10` -> `0x82EDB0` (key `0x85FE10`) | **verified** (docs/143 §2.2) |
| Inside: avoidance | `0x839F70` behaviour update -> `ObstacleAvoidance 0x847390` -> `RouteFollow_A 0x846B70` (key `0x846D21`) | call chain **verified** (docs/143 §2.2); share of `0x846D21` that is in-pass vs the two route ICs **inferred** (v85 counts it) |
| Outside: footprint | `UpdateSimFootprint 0x833560` (key `0x833593`) comes from the animation passes (`Animation_UpdateActors 0xD81C73`, the next call after the site), event `0x21` and ICs, not from `Locomotion_Update` | **verified** (docs/89 §1); so H2 does **not** cover `0x833593`, and held jobs start just before it [inferred risk: some of the removed wait reappears there] |
| Where starts happen | `ServeAsync` (worker): claim `J_RUNNING`, then loop `while (g_rgOffSimOpen || G0wHolds(h)) Sleep(0)`, acquire `+0x918`, publish `A_RUNNING`, re-check (Dekker with the thunk), break | **verified** (source) |

## 3. What changed (0.36.21)

- **Switch.** `[RouteAsync] H2=1` (default 0) installs the `[LocoWindow]` thunk in Mode 1 (same code; `[LocoWindow] Enabled=1` still works). Runtime switch `g0wMode` (+0x13E8): `--ab h2` (alias of `locowin` in `profile-world.ts`); 0 = count only, the thunk stays, so off slices still count. Needs `[RouteGate] Enforce=1` and async on: both are release defaults.
- **Exact in-pass start count.** The thunk now raises the window before `g_g0wIn` and drops `g_g0wIn` first at the end; `ServeAsync` samples `g_g0wIn` before its last window check. So a start with `inPass` set and no hold is a real in-pass start (Mode 0, after a `G0wDrop`, or the 250 ms watchdog), never a start that only raced the thunk's entry (that one is "running at entry"). Before v84 `g0wJobStartInPass` could count such a race. [verified: selftest asserts it 0 over 300 threaded passes in Mode 1]
- **Counters v85** (+0x364C): `h2Released` (held starts that began after the pass ended), `h2PassLockN[2]` / `h2PassLockUs[2]` (sim `+0x918` waits inside the pass: [0] a job was running when the wait began, [1] none running: no job, or one queued / held), `h2ReleaseMaxUs` (longest gap from a pass's end to a held job's start). Jobs held and hold time stay v37's `g0wJobsHeld`, `g0wHoldUs`, `g0wHoldMaxUs`. The in-pass waits are timed in `LockAcquireAsync` (the pump path; with no job busy, one try first, then the original blocking call timed), only on the sim thread inside the pass.

## 4. Offline checks (this build)

| Check | Result |
|---|---|
| `build.cmd` (`/W4 /WX`, all targets) | pass |
| selftest x5 (whole suite) | 5/5 pass |
| `TestH2Threaded` (new; real worker, real `ServeAsync`, `LockAcquireHook` / `LockReleaseHook` on the M1 lock, fake pass with 3 `+0x918` takers of ~200 us, jobs queued inside and between passes, each job holds the lock ~2 ms) | Mode 1, 300 passes: an independent check in the job body (start inside a pass that did not see it running at entry) = 0, `g0wJobStartInPass` 0, every held job released after its pass (`h2Released == g0wJobsHeld`), `h2ReleaseMaxUs` < 100 ms, 0 watchdogs, 0 drops, in-pass waits on running jobs counted; Mode 0, 300 passes: the same check sees starts inside (> 0), nothing held; another thread holding the lock with no job: one "none running" wait >= 20 ms; nothing left (`AsyncBusy` false, windows closed); 20 s deadlock guard never hit |
| `TestLocoWindow` (0.19.0 cases: hold, running at entry, drops, watchdog, nesting, Mode 0, SEH) | pass with the new order |
| `steerro-test.exe` seeds 1, 2 x 3000 | ok; section H: 6000 passes through the patched rel32 each, answers equal |
| `node tools/hook-sites.ts --check` | no new site; 4 addresses not in docs/68 are pre-existing (`monothread-m5.cpp`, `threadstack.cpp`) |
| `node tools/check-docs-code.ts` | ok |

Not covered offline: the game's own lock holders outside our hooks, a real locomotion pass (its length, how many takers wait), and the gain.

## 5. Queue `h2-ab.json`

1. **`bp-h2-verify`** (first, daytime, noisy machine OK, 5 min BP, Mode 1 whole run): correctness by counters only: `g0wJobsHeld` > 0; `g0wJobStartInPass` = 0 (or <= drops, each logged); `h2ReleaseMaxUs` < 20 ms, `g0wHoldMaxUs` < 250 ms, `g0wWatchdog` 0; `h2Released + g0wJobStartInPass >= g0wJobsHeld - 1` (no lost job).
2. **Night pairs** (clean night, 10 min, 2-min slices, `--ab h2`, seed 12345, master ini + `RouteAsync.H2=1`): BP play, IP play, BP `rare-big-crowd`, each off-first and on-first. Main metric: `wait-ab.ts` total keyed `+0x918` wait on vs off, pair mean of both orders; also it/s, hitches, jobs held / hold time, `h2PassLockUs_0` (off slices = the cap of what H2 can remove).

Expected [inferred]: the cap is the in-pass wait on running jobs (`h2PassLockUs_0` in off slices); per docs/143 §8.14 part of it can reappear after the pass (`0x833593`, destroys, gates), so 0 to -10 ms/s net. Cost: job start latency grows by up to one pass (~2.2 ms per pass BP in `bp-g0w-shadow`: 93 ms/s over 25,334 passes in 10 min, PLAN.md) [verified run figures, per-pass mean computed].

## 6. Effort

Built in one session (reuse of 0.19.0). Live: `bp-h2-verify` ~8 min by day, then 6 night jobs (~1.3 h) on one clean night.
