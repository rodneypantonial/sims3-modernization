# 160. Sim-thread budget: one table per world

Written 2026-10-22. **Queue and report built, not run in game.** Goal: pick weekend targets by measured ms/s of the sim thread, not by guess (memory rule "measure before build"). It completes docs/28 §4 item 4 (per-stage split of `MonoScriptHost::Simulate`, SV missing, ~290 ms/s untimed).

## How to run and read

- Queue: `tools/harness/queues/sim-budget.json`, 3 jobs (`bp-`, `ip-`, `sv-sim-budget`), ~12 min each. Release defaults on `game-copy/ts3native.asi.0325`, plus `[HitchAttr] Enabled=1` (observe only, docs/96 §9). 10 min seeded profile (`profile-world.ts --minutes 10 --seed 12345`): stutter profiler, managed sampler (minutes 1-4), native sim-thread sampler (minutes 5-8). No `--ab`, no repro. Timing counts only from a clean night.
- Report: `node tools/harness/sim-budget-report.ts` (newest `sim-budget-*` run per world; or pass tags). From a worktree, set `TS3_REPO=<main checkout>`. `--self-check` runs a fixture, then checks every bucket on the newest real run that has managed samples.
- Inputs per tag: `probe-out/profile/<tag>-<secs>-seconds.csv` and `-hitches.jsonl` (stutter-profile.js), `<tag>-managed.jsonl`, `<tag>-simstack.jsonl`, `probe-out/queue/<tag>-native.json` (`/native` before and after: `blockKey/N/Us`).
- Rows: the first 60 s (load work, as in hitch-attr-report.ts) and paused seconds are dropped. ms/s = ms per wall second, so the buckets add up to 1000.

## Buckets

| Bucket | What it measures | Top 5 contributors from | Tag |
|---|---|---|---|
| Mono script | Scheduler stages (tick `0xD80BC0`, TaskObjects, OneShot, ThreadedObjects) minus route, the waits taken from script, and forced GC; multiplied by the share of managed samples that are not inside an internal call | owner category of the C# stack (`stall-split.ts category`: autonomy, interaction step, SimUpdate, NRaas, alarms...) | timers measured, split inferred |
| ICs (native under script) | The same remainder times the share of managed samples whose leaf is an IC from `docs/data/ic-safety.csv` (non-plan Route ICs included) | leaf IC name | split inferred |
| route sync plans | stutter-profile `route` (`Route_PlanLocked 0x82C6E0` on the sim thread) minus `rtPlanWait` | plan IC (`Route_Plan`, `PlanFromPoint`, `Replan`, `ReplanFromPosition`) and its first script caller | measured |
| +0x918 / job waits | `rtLock + rtOtherLock + rtJob + rtPlanWait` ([HitchAttr]) | `/native` `blockKey` deltas named with hitch-attr-report.ts `keyName` (docs/84-90 keys) | measured; key split covers the whole `/native` window |
| GC + finalizers | `gcMark` (step and forced) + `fin` | step mark, forced (allocation), finalizers | measured |
| anim + locomotion + publish | `animSacs + animPose + locomotion + publish` minus the steering-key share of the lock waits | the four stages | measured; wait split inferred |
| other (untimed, sync, idle) | row wall minus all of the above: catch-up overhead, the remote-call pump `0xD81FC3` (shared JobManager jobs, incl. render jobs, docs/41 S4), frame waits, untimed native work | leaf frame of simstack samples with no timed-stage or Mono-runtime frame | remainder measured, split approximate |

**Frames >= 300 ms it leads:** for every hitch record >= 300 ms (after the first 60 s), the same split on that frame's own fields; the bucket with the most ms leads. Mono and ICs are one bucket here (no per-frame managed samples).

## Known blind spots

1. **Mono vs IC split is a sample ratio** (5 Hz, minutes 1-4 only, ~1,000 samples per world), applied to the whole window. A wait inside an IC (e.g. `IsSlotUnobstructed`) is subtracted as wait but its samples still count as IC, so the IC share is a little high [inferred].
2. **Waits by key** come from the `/native` before/after pair, which spans the whole profile including the first 60 s. Only waits while a route job runs or is queued are keyed (`WaitNote`), and only the first 32 keys get a slot (`blockOther` counts the rest without time).
3. **Steering waits** are moved from the script stages to anim/locomotion by run-level key share (per second) or by `rtMaxKey` (per hitch). A wrong key list moves time between the two buckets, not out of the table.
4. **Route plans called outside the scheduler stages** (locomotion callbacks, docs/141 class c) are still subtracted from the script stages; the script bucket clamps at 0 per row.
5. **Samplers suspend the sim thread** (5 Hz) during minutes 1-8, which adds to whichever stage was running. This is the same for all three worlds, so worlds compare.
6. **Render/UI on the sim thread:** render runs on the main thread (docs/41). What reaches the sim thread is the publish sync (`publish`, waits for the main-thread consume), render jobs the unbudgeted pump picks up (in "other", not split by job), and UI ICs (in "ICs"). No main-thread sampler runs in this queue.
7. **Idle/sleep:** the sim loop has no idle state while unpaused; paused seconds are dropped. Waits in `ntdll`/`KERNELBASE` outside the timed stages show as an `ntdll.dll` contributor of "other" (no export names).
8. `publish` may nest inside another stage (stutter-profile.js note); the overlap is not removed.
