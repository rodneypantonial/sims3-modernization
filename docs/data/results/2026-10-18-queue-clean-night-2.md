# Queue clean-night-2 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/clean-night-2-2026-10-18.jsonl`.

## 2026-10-18T03:01 UTC, build 3b87d16, jobs bp-p10, ip-p10

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-p10 | 03:01 (11:01) | 13.3 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2223.1/1853.9/1500.9 -> 2408.2/2176.2/1116.8 | asyncStarted +16861, asyncRefused +42, asyncCollected +16855, asyncDropped +5, asyncUs +87988421, asyncMaxUs +199529, asyncPumpUs +52357015, asyncPumpWaits +11192, asyncRefusedM1Pending +42, asyncSkipped +1, asyncBackoffs +16, asyncQueueMax +6 | Probe P10 observe (0.20.0, counters v38, docs/87 §6): sync plans by route IC. Pass = game alive, 0 watchdog lines, clock guard ok, p10Ready 1, p10Entries* > 0. Read with node tools/harness/p10-report.ts: per IC entries, Route_PlanLocked ms/s, early returns, CanYield share; gate-10 waits by IC (P1b row 6); decides A10's order and size (docs/87 §5) |

Paused after bp-p10 (AI usage 5h 54% (reset 13:10 PHT) | 7d 81% (reset 14:00 PHT) | read 2 min ago); resume with `--from ip-p10`.

## 2026-10-18T03:16 UTC, build 3b87d16, jobs ip-p10

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-p10 | 03:16 (11:16) | 13.1 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2241.5/1834.2/1484.9 -> 2318.9/1990.1/1228.6 | asyncStarted +6895, asyncRefused +39, asyncCollected +6886, asyncDropped +9, asyncUs +73038798, asyncMaxUs +473168, asyncPumpUs +50799321, asyncPumpWaits +5056, asyncRefusedM1Pending +35, asyncBackoffs +3, asyncQueueFull +4, asyncQueueMax +7 | bp-p10 in Isla Paradiso (0.20.0): same pass rules; IP sync plans were 125 ms/s |

## 2026-10-18T03:29 UTC, build e861de8, jobs bp-s2cov-shadow

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-s2cov-shadow | 03:29 (11:29) | 13.1 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2220.9/1848/1500.9 -> 2362/2157.7/1180.8 | asyncStarted +15993, asyncRefused +51, asyncCollected +15989, asyncDropped +4, asyncUs +88850798, asyncMaxUs +224598, asyncPumpUs +52665712, asyncPumpWaits +10509, asyncRefusedM1Pending +46, asyncBackoffs +15, asyncQueueFull +5, asyncQueueMax +5 | S2 coverage (0.21.0, counters v39): same ini as bp-s2-shadow; SR_FB_SHAPE split per site (s2Shape*) and buffer demand (s2MaxObjs/Local/Nodes/Ids/Depth) after the limits doubled (kSrCap 1024, kSrLocal 128, kSrNodes 32, kSrIds 512); pass = steerShadowMismatch 0, steerShadowQueryMismatch 0, steerShadowRuns > 1000, fallback share (sum of steerFb* / steerCalls) well under 10% (bp-s2-shadow: 28% shape), |

## 2026-10-18T03:43 UTC, build e861de8, jobs bp-g0w-shadow

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-g0w-shadow | 03:43 (11:43) | 13.2 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2217.8/1847.3/1500.9 -> 2393.3/2214.6/1036.8 | asyncStarted +16119, asyncRefused +50, asyncCollected +16114, asyncDropped +4, asyncUs +82062900, asyncMaxUs +190972, asyncPumpUs +49077491, asyncPumpWaits +9642, asyncRefusedM1Pending +41, asyncSkipped +1, asyncBackoffs +11, asyncQueueFull +9, asyncQueueMax +7 | Design H2 shadow (0.19.0, counters v37, docs/86 §6): [LocoWindow] Mode 0 = count only. Pass = game alive, 0 watchdog lines, clock guard ok, g0wDropJobWait = g0wDropSyncPlan = 0, g0wWatchdog = 0, g0wPasses about = sim iterations, and g0wJobStartInPass > 0 (else H2 has nothing to remove). Also read g0wRunningAtEntry, g0wPassUs/g0wPasses, g0wGate0Waits |

## 2026-10-18T03:56 UTC, build 98338a4, jobs bp-p1b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-p1b | 03:56 (11:56) | 13.3 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2204.2/1819.3/1500.9 -> 2362.2/2140.1/1148.8 | asyncStarted +14734, asyncRefused +60, asyncCollected +14729, asyncDropped +4, asyncUs +82998001, asyncMaxUs +212186, asyncPumpUs +49896858, asyncPumpWaits +9575, asyncRefusedM1Pending +54, asyncBackoffs +6, asyncQueueFull +6, asyncQueueMax +5 | Probe P1b observe (0.14.0, counters v32): gate-12/gate-0 waits by second frame, T-L3 portalRef, T-L5 caSimInJob; pass = node tools/harness/p1b-report.ts PASS on both checks, ltQlHeldBlock 0, 0 watchdog lines, game alive |

## 2026-10-18T05:21 UTC, build dc58451, jobs bp-ar-shadow

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ar-shadow | 05:21 (13:21) | 13.1 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2216/1833.7/1500.9 -> 2354/2201/1020.8 | asyncStarted +16928, asyncRefused +52, asyncCollected +16925, asyncDropped +3, asyncUs +83640106, asyncMaxUs +191653, asyncPumpUs +51602179, asyncPumpWaits +9542, asyncRefusedM1Pending +47, asyncBackoffs +15, asyncQueueFull +5, asyncQueueMax +5 | Design A10 shadow (0.22.0, counters v40, docs/87 §5-§6): Kinds=7, KindsMode=0: the three Replan-family ICs run every START check, count would-be jobs (arShadow*) and plan synchronously. Pass = game alive, 0 watchdog lines, arReady 7, arMismatch = 0, arOrphan = 0, arStarted* = 0. Read with node tools/harness/p10-report.ts: arShadow vs arRefused/arActive per IC (refusals by reason in the log), arSyn |

## 2026-10-18T05:35 UTC, build dc58451, jobs bp-g0ro-shadow

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-g0ro-shadow | 05:35 (13:35) | 13.1 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2237.1/1849.1/1500.9 -> 2360.6/2153.6/1148.8 | asyncStarted +16586, asyncRefused +60, asyncCollected +16578, asyncDropped +5, asyncUs +88350452, asyncMaxUs +234711, asyncPumpUs +53560987, asyncPumpWaits +10277, asyncRefusedM1Pending +51, asyncBackoffs +24, asyncQueueFull +9, asyncQueueMax +5 | Gate-0 fix shadow (0.21.1, counters v41, docs/86 §10): [ObstructedRO] Mode=0 Shadow=1, async + queue + D on. Pass = game alive, 0 watchdog lines, clock guard ok, orReady 1, orShadowRuns > 100, orShadowMismatch 0, ltQlHeldBlock 0, steerT4Bare 0, steerS7Foreign 0; report orFb* (state, QL busy, lot, cache, shape, tripwire) against orCalls |

## 2026-10-18T05:54 UTC, build 6adde4d, jobs bp-slotro-shadow

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-slotro-shadow | 05:54 (13:54) | 13.1 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2219/1835.2/1484.9 -> 2363.3/2116.5/1164.8 | asyncStarted +17036, asyncRefused +44, asyncCollected +17031, asyncDropped +5, asyncUs +84749614, asyncMaxUs +186008, asyncPumpUs +50660091, asyncPumpWaits +11106, asyncRefusedM1Pending +38, asyncBackoffs +22, asyncQueueFull +6, asyncQueueMax +4 | Design SH shadow (0.22.2, counters v42, docs/90): [SlotEnabledRO] Mode 0 Shadow 1 = with no job the game's call then a dry hold, compared. Pass = game alive, 0 watchdog lines, clock guard ok, slShadowMismatch 0 with slShadowRuns > 100, slShadowDrop small, ltQlHeldBlock 0. Also read slCalls, slDrop* split. |

## 2026-10-18T06:11 UTC, build 1d823c3, jobs bp-l1-shadow

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-l1-shadow | 06:11 (14:11) | 13.1 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2209.2/1833.9/1500.9 -> 2444.1/2116.1/1094.1 | asyncStarted +16364, asyncRefused +48, asyncCollected +16358, asyncDropped +3, asyncUs +87201445, asyncMaxUs +194083, asyncPumpUs +53369428, asyncPumpWaits +10175, asyncRefusedM1Pending +47, asyncSkipped +3, asyncBackoffs +26, asyncQueueFull +1, asyncQueueMax +5 | Design L1 shadow (0.19.0, l1b: single InstallLotFd): clone vs game outside jobs; pass = ltShadowMismatch 0, ltShadowRuns > 100, ltQlHeldBlock 0, 0 watchdog lines (P1b on: 0x631814 second frames show 0x788373) |

## 2026-10-18T06:25 UTC, build 41052ac, jobs ip-ar-ab

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-ar-ab | 06:25 (14:25) | 21.1 | PASS | PASS: profile 18 min, seed 12345 (rng-compare.ts) | 0 | 2239.8/1835.1/1484.8 -> 2438.2/2114.4/972.3 | asyncStarted +12458, asyncRefused +62, asyncCollected +12450, asyncDropped +8, asyncUs +150178652, asyncMaxUs +397947, asyncPumpUs +106433637, asyncPumpWaits +11090, asyncRefusedM1Pending +59, asyncBackoffs +11, asyncQueueFull +3, asyncQueueMax +8 | bp-ar-ab in Isla Paradiso (0.22.0): same pass rules; P10 had ReplanFromPosition at 77 ms/s of sim plan time in IP (~38 ms per call), the main target. Needs the A10 ts3fixes (RouteBackoff.cs Replan/PlanFromPoint/ReplanFromPoint hooks) installed in Mods before the run: node tools/setup/install-fixes.ts install --only stutter-fixes from the merged checkout (the queue does not pick a ts3fixes build pe |

## 2026-10-18T06:46 UTC, build 41052ac, jobs bp-ar-ab

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ar-ab | 06:46 (14:46) | 2.7 | **FAIL** | cycle.ts --save exit 1 | - | ? -> ? | - | Design A10 A/B (0.22.0): in-load --ab asyncreplan (arMode 1/0 every 2 min, ABBA after a warm-up slice; ignore the first). Mode 1 halves: arCollected* > 0, arMismatch = 0, arOrphan = 0, gate-10 waits (blockKey 0x760BFF) near 0, arSyncUs* and g11PlanUs down; compare total blockUs and iterations/s with the Mode 0 halves; 0 stuck Sims (log only). Re-run P0 after (gains do not add). Needs the A10 ts3fi |

## 2026-10-18T06:49 UTC, build 4a140d2, jobs bp-slotro-ab

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-slotro-ab | 06:49 (14:49) | 21.2 | PASS | PASS: profile 18 min, seed 12345 (rng-compare.ts) | 0 | 2217.3/1840.3/1452.9 -> 2538.9/2205.8/981.8 | asyncStarted +26738, asyncRefused +96, asyncCollected +26726, asyncDropped +8, asyncUs +146454918, asyncMaxUs +205552, asyncPumpUs +87718952, asyncPumpWaits +16270, asyncRefusedM1Pending +85, asyncSkipped +4, asyncBackoffs +32, asyncQueueFull +11, asyncQueueMax +5 | Design SH A/B (0.22.2): in-load --ab slotenabledro (slMode 1/0 every 2 min), shadow on; ignore the first slice (warm-up). Mode 1 halves: slTaken > 0, gate-12 waits (blockKey 0x62F782, second frame 0x76F15D) and slPass12/slPass0 rise, slShadowMismatch 0, slDrop Lock/Managed small; compare total blockUs and iterations/s with the Mode 0 halves; 0 stuck Sims (log only). Re-run P0 after (gains do not a |

## 2026-10-18T07:10 UTC, build bfaed94, jobs ip-slotro-ab

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-slotro-ab | 07:10 (15:10) | 21.1 | PASS | PASS: profile 18 min, seed 12345 (rng-compare.ts) | 0 | 2236.7/1848.3/1484.9 -> 2458/2140.3/972.5 | asyncStarted +11495, asyncRefused +51, asyncCollected +11474, asyncDropped +18, asyncUs +117596249, asyncMaxUs +277666, asyncPumpUs +82329302, asyncPumpWaits +9521, asyncRefusedM1Pending +51, asyncSkipped +1, asyncBackoffs +21, asyncQueueMax +7 | Design SH A/B on an IP save (0.22.2): --ab slotenabledro, 18 min, 2-min slices, shadow on. Same pass criteria as bp-slotro-ab. |

## 2026-10-18T07:32 UTC, build 90eeebc, jobs ip-ab-routeasync-rlog

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-ab-routeasync-rlog | 07:32 (15:32) | 21.1 | PASS | PASS: profile 18 min, seed 12345 (rng-compare.ts) | 0 | 2232.7/1857.5/1484.9 -> 2448.3/2138.4/972.5 | asyncMode -1, asyncStarted +6073, asyncRefused +18, asyncCollected +6065, asyncDropped +8, asyncUs +59210435, asyncMaxUs +251929, asyncPumpUs +40695602, asyncPumpWaits +4639, asyncRefusedM1Pending +18, asyncBackoffs +6, asyncQueueMax +4 | ip-ab-routeasync on 0.17.0 (counters v35, docs/60 §2.3): log why long route plans fall back to sync. In-load A/B async Mode 1 on/off (warm-up slice, then ABBA). Read: rfbN*/rfbMs* by group and rfbMaxMs/rfbMaxWhy in the on slices, and the "route async: sync plan" lines in ts3native.log (reason per plan >= 200 ms) |

## 2026-10-18T07:54 UTC, build 283c834, jobs ip-p1b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-p1b | 07:54 (15:54) | 13.1 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2229.9/1837.8/1484.9 -> 2325.2/1992.2/1228.6 | asyncStarted +6598, asyncRefused +43, asyncCollected +6584, asyncDropped +14, asyncUs +73778584, asyncMaxUs +296012, asyncPumpUs +51105126, asyncPumpWaits +5724, asyncRefusedM1Pending +41, asyncBackoffs +2, asyncQueueFull +2, asyncQueueMax +6 | Probe P1b observe on Isla Paradiso (0.19.0, l1b: single InstallLotFd): as bp-p1b |
