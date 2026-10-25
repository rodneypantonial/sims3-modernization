# Night plan 2026-10-24 (Sat ~21:00 to Sun ~08:00 PHT)

Timing-grade night (machine quiet, screen locked). One `night-run.ts --queue-list` call runs five queues in order. Durations below are estimates: profile window or loop cap plus ~3-4 min per job for restart, load and close (measured on the 2026-10-23/24 queues). Times assume a 21:00 PHT start.

## Builds

- ts3fixes switches: master `build/ts3fixes.package` (built 2026-10-24 16:54 PHT, tuning `c094be923c4a`; it has stuckcheckown, drivertaskasync, seasonmarkercache, safespotyield, disposespread, overwatchstuckslice and jobyield, all off by default). No `fixesBuild` pin.
- Verify and A/B asi: `game-copy/ts3native.asi.0344` (0.36.38, counters v109, the same sha256 as `asi.jobyield`, has native JobYield).
- P6 asi: `game-copy/ts3native.asi.p6` (0.36.41, counters v112; master source is 0.36.41).
- Mansion soak and tool-cost: unchanged (`asi.rel015`).

## Queue order

| # | queue | jobs | est. | PHT (from 21:00) | what it proves |
|---|---|---|---|---|---|
| 1 | `night-1024-verify.json` | 7 | ~1.6 h (worst case, every job to its timeout: 3 h) | 21:00-22:35 | First verify of today's switches: the fix turns on, its counters rise, no crash, no watchdog line. stuckcheckown first, then jobyield, seasonmarkercache, overwatchstuckslice (these four have A/Bs tonight), then drivertaskasync car + boat and disposespread (verify only). |
| 2 | `night-1024-p6.json` | 3 | ~0.9 h | 22:35-23:30 | G1 BP: Route.Plan shadow compares on a build with the IP fix (Mode=1, Kinds=1, until 10,000 compares). Then P6 correctness BP and IP (docs/94 §24.6 step 1). |
| 3 | `night-1024-ab.json` | 8 | ~5.0 h | 23:30-04:30 | Timing A/Bs: stuckcheckown forced ABBA (BP), SV natural-play pair off then on (45 min each), jobyield ABBA (SV), seasonmarkercache ABBA (IP), overwatchstuckslice ABBA (BP), then the second SV pair (on then off) so the four SV runs are ABBA over runs. |
| 4 | `story-mansion-soak.json` | 1 | ~2.7 h | 04:30-07:10 | 150-min story soak (existing queue). |
| 5 | `tool-cost.json` | 22 | ~4.5 h total, ~0.8 h tonight | 07:10-08:00 | Filler: cheapest tool cost jobs first; resumes next night. |

Total before tool-cost: ~10.1 h. A noisy timing job reruns once at the end of its queue (`<id>-rerun`), which pushes the later queues back.

## Details

### 1. night-1024-verify.json (correctness, `noiseCheck: false`)

| job | repro (save) | rule | pass |
|---|---|---|---|
| `bp-stuckcheckown-verify` | `route-stuckcheck-own-loop` (BP-Spring) | repro's own: 40 armed checks per arm, cap 10 min | `stuckcheckown_sent`, `ownertask_started` >= 1; `watchdogs` 0 |
| `sv-jobyield-verify` | `route-ownjob-yield-loop` (SV-Spring) | job-yield-ab.json job 1, 10 min | `jyBusy` >= 1, `jobyield_steps` >= 1, `jobyield_capped` 0 |
| `ip-seasonmarkercache-verify` | `ip-world-dispose-loop` (IP) | 3 firings per arm, cap 8 min | `seasonmarkercache_seeds`, `_hits` >= 1, `_mismatches` 0 |
| `bp-overwatchstuckslice-verify` | `bp-overwatch-stuckslice-loop` (BP) | 2 firings per arm, cap 10 min | `owstuck_runs`, `owstuck_slices` >= 1; no `overwatchspread: ... failed` |
| `bp-drivertask-car-verify` | `route-traffic-car-loop` (BP-Spring) | repro's own, cap 8 min | `drivertask_car_sent`, `ownertask_started` >= 1 |
| `ip-drivertask-boat-verify` | `ip-boat-houseboat-return-loop` (IP) | repro's own, cap 12 min | `drivertask_rider_sent` >= 1 (0 = the rider test is wrong: read a live stack) |
| `bp-disposespread-verify` | `bp-disposespread-burst-loop` (BP, stress) | 2 firings per arm, cap 8 min | `disposespread_queued`, `_destroyed` >= 1, `_full` 0 |

Left out: **safespotyield**. `ip-school-run-safespot-loop` has `<school child N>` placeholders. No saved `list_sims` output in probe-out records ages or school, so the IP school children cannot be named offline, and the harness cannot fill step arguments at runtime. The repro also needs a weekday school-hours window. To run it: one day job with `list_sims` + `jobs` reads on Baseline-IP, fill the names, then verify.

### 2. night-1024-p6.json

The prerequisites (docs/94 §22.30.4):
- **G0: met.** The 0.36.2 IP clone-run fix passed two clean IP runs (`ip-snap-p4-0362-1/-2`, 2026-10-20).
- **G1 IP: met.** `shadow-compare-report.ts` on those runs' shadow files gave 10,696 Plan compares, M1/M2/M3 0, M4 320, M5 24 (read 2026-10-24).
- **G1 BP: open.** The only BP run (10,075 compares, M2/M3 0) was on 0.35.24, before the fix. `bp-p6-g1-compares` is that second BP run, on the P6 build.
- **G2:** cut sharing (`ShareCuts`) is what P6 builds. Its cost is read from the P6 runs themselves (§24.6 step 2: `snPcCutUs` <= ~5 ms/s BP, dT).

Jobs:
- `bp-p6-g1-compares` (~24 min). Pass: no `P4 FAULT` or `fault applying record`. Afterwards run `node tools/harness/shadow-compare-report.ts probe-out/queue/night-1024-p6-<date>-bp-p6-g1-compares-<HHMM>-shadow.jsonl`, which must show M1+M2+M3 = 0. A G1 FAIL voids the next two jobs.
- `bp-p6-verify` and `ip-p6-verify` (~14 min each). They run the routesnap stack (release-features.ts `routesnap`) plus `RouteSnap.Kinds=9 TakeKinds=9 ShareCuts=1 VerifyEvery=1 VerifyLend=1 P6LogMs=20 OptsInit.Enabled=1` on the forced repro `route-queuefull-replan-loop` for 10 min. Pass (`nativeRequire`): `p6Armed_0`, `p6Taken_0`, `p6Borrowed_0` > 0; `m2VerBad` 0, `p6BorrowVerBad` 0, `m2Off` 0, `m2Trips` <= 2. Pass (`logForbid`): no `P4 FAULT`, no `fault applying record`.
- `RouteSnap.Kinds=9`, not routesnap's 8: TakeKinds is ANDed with Kinds (`p5.cpp` `P5TakeMask() & g_p4Kinds`; the TestP6 selftest checks that TakeKinds 9 with Kinds 8 takes no Route.Plan). With Kinds=8, `p6Armed_0` would stay 0.
- The IP job runs a BP-written repro. If `p6Armed_0` is 0 there, check `asyncStarted` and the repro log before calling it a P6 fault.

### 3. night-1024-ab.json (timing)

| job | est. | design |
|---|---|---|
| `bp-stuckcheckown-ab` | ~25 min (cap 36) | ABBA, 120 armed checks and >= 6 firings per arm, cap 30 min |
| `sv-scown-nat-1-off` / `-1-on` | 2 x ~49 min | sv-cause-run settings (45 min, seed 12345, SV-Spring, LogFallbackStack 16, OwnJobWait, HitchAttr holders), stuckcheckown off / on for the whole run |
| `sv-jobyield-ab` | ~40 min (cap 50) | job-yield-ab.json job 2: ABBA until 50 no-path plans per arm |
| `ip-seasonmarkercache-ab` | ~22 min (cap 31) | ABBA, 20 firings per arm |
| `bp-overwatchstuckslice-ab` | ~18 min (cap 25) | repro's own: 10 firings per arm |
| `sv-scown-nat-2-on` / `-2-off` | 2 x ~49 min | second order, so the four SV runs are off, on, on, off |

Why separate natural runs for SV and not `--ab` in one load: the 3 AM stuck check fires about once per 24 real minutes at normal speed. ABBA slices in one load would put each 3 AM in a single arm. Separate seeded runs from the same save see the same 3 AMs in both arms.

How to read the results:
- Loops: `hitch-ab.ts --scenario <tag>`.
- SV pair: `hitch-ab.ts` over the four rows, plus `--causes` and `nostart-report.ts` (StuckCheckDrain Foreign plans; sv-cause-run had 59, 20.2 s, 11 >= 1 s).

Each A/B counts only if its verify job in queue 1 passed. queue.ts has no per-job dependency, so a failed verify does not stop its A/B: read the verify rows first and mark those A/B rows void.

### 4-5. Existing queues

- `story-mansion-soak.json`: one 150-min job (timeout 200).
- `tool-cost.json`: 22 jobs, about 4.5 h in all (reworked 2026-10-24: 2 in-load ABBA jobs, 10 two-load off/on pairs; docs/168 §9). The dry run prints 2 `WARN pair ... profOpts` lines. They are by design, because those jobs compare profiler options.

## Dry runs (2026-10-24, run from the main checkout's `queue.ts`; game-copy and build exist only there)

| queue | jobs | refusals | notes |
|---|---|---|---|
| night-1024-verify.json | 7 | 0 | asi 0344 sha256 `8baf3af01c14` known (0.36.38); ts3fixes build/ tuning `c094be923c4a` (not in builds.json; installed is `d5e68fbf8941`, cycle.ts reinstalls) |
| night-1024-p6.json | 3 | 0 | asi p6 `521d40d4623a` (0.36.41) |
| night-1024-ab.json | 8 | 0 | asi 0344 |
| story-mansion-soak.json | 1 | 0 | asi rel015 |
| tool-cost.json | 45 | 0 | 8 profOpts pair WARNs (by design) |

## Commands

Start (main checkout, after this branch is merged to master; screen locked, `--wait` optional to wait for lock + idle):

```
node tools/harness/night-run.ts --queue-list night-1024-verify.json,night-1024-p6.json,night-1024-ab.json,story-mansion-soak.json,tool-cost.json --close
```

Resume the same list after a stop (continues the queue that ran last at its first job without a row):

```
node tools/harness/night-run.ts --queue-list night-1024-verify.json,night-1024-p6.json,night-1024-ab.json,story-mansion-soak.json,tool-cost.json --close --resume
```

Next night, tool-cost only:

```
node tools/harness/night-run.ts --queue tool-cost.json --close --resume
```

`--stop-on-fail` is left off on purpose. With it, a failed verify job would also end P6, the soak and tool-cost.
