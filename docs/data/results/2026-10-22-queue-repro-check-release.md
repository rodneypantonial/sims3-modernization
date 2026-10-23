# Queue repro-check-release 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/repro-check-release-2026-10-22.jsonl`.

## 2026-10-22T16:13 UTC, build 57dd5fd4, jobs bp-rcr-route-gate-crowd, bp-rcr-save-write, bp-rcr-alloc-churn, bp-rcr-unseen-crowd, ip-rcr-route-legs, bp-rcr-winter-pond, bp-rcr-pet-guard, bp-rcr-object-churn, bp-rcr-diner-crowd, bp-rcr-candidate-loops, bp-rcr-replan-buildbuy

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rcr-route-gate-crowd | 16:13 (00:13) | 6.7 | PASS | PASS: profile loop window end reached: 3 events after 4.1 min, rule not met (off 1, on 2; 4 firings, 0 cycles; rule 4 events per arm), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2167.1/1853.9/1524.9 -> 2347.4/2103.5/1263.6 | asyncStarted +6172, asyncRefused +94, asyncCollected +6152, asyncUs +59543240, asyncMaxUs +447243, asyncPumpUs +37172888, asyncPumpWaits +6380, asyncRefusedM1Pending +11, asyncSkipped +19, asyncBackoffs +10, asyncQueueMax +24 | noise: medium (other apps 9.8% of game CPUs); others on game CPUs 9.8%; clock avg/min 4223/4195 MHz; route-gate-crowd-loop (routegate, tailunlock, h2, optsinit): 30+ async Go Here plans per firing with object add/destroy, 6 route-mode plans + Replan, a camera pan and a build/buy enter/exit under running jobs. Pass: no crash, asyncStarted >= 20, rgEntries >= 1, tuWins >= 1, h2Released >= 1, tuTrip  |
| bp-rcr-save-write | 16:20 (00:20) | 2.4 | **FAIL** | FAIL: profile 6 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step cas_close failed: no reply to 'cas_close' (game paused, loading or blocked?)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: saveCount=0 (want >=3) | 1 | 2172.5/1852.8/1524.9 -> 2253.8/1899.9/1459.5 | asyncStarted +44, asyncCollected +44, asyncUs +356971, asyncMaxUs +57686, asyncPumpUs +173477, asyncPumpWaits +38, asyncQueueMax +2 | noise: medium (other apps 8.6% of game CPUs); others on game CPUs 8.6%; clock avg/min 4285/4267 MHz; save-write-loop (savewrite, decompress): CAS open/close, build/buy open/close and a real save to RC-SaveLoop per firing (the same name: worker backup from the second save). Pass: no crash, saveCount >= 3, decompressCalls >= 1, decompressFails 0, decompressMismatches 0; the RC-SaveLoop save loads (c |
| bp-rcr-alloc-churn | 16:22 (00:22) | 5.3 | PASS | PASS: profile stopped at 6 events after 3.1 min (off 0, on 6; 6 firings, 6 cycles; rule 6 events), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2168.6/1835.2/1532.9 -> 2163.6/1859.1/1451.4 | asyncStarted +556, asyncRefused +17, asyncCollected +551, asyncDropped +4, asyncUs +9305310, asyncMaxUs +805562, asyncPumpUs +2982533, asyncPumpWaits +418, asyncRefusedM1Pending +3, asyncSkipped +1, asyncQueueMax +3 | noise: medium (other apps 8.5% of game CPUs); others on game CPUs 8.5%; clock avg/min 4260/4225 MHz; alloc-churn-loop (gc): Sim, object and CAS create/destroy churn per firing. Pass: no crash, gcForeignAllocs 0; read commit MB before -> after. |
| bp-rcr-unseen-crowd | 16:27 (00:27) | 6.8 | PASS | PASS: profile loop window end reached: 5 events after 4.4 min, rule not met (off 3, on 2; 5 firings, 1 cycles; rule 4 events per arm), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2159.8/1850.1/1532.9 -> 2291.9/2088.3/1283.8 | asyncStarted +7397, asyncRefused +96, asyncCollected +7397, asyncUs +55120981, asyncMaxUs +158751, asyncPumpUs +31384139, asyncPumpWaits +6586, asyncRefusedM1Pending +39, asyncBackoffs +11, asyncQueueMax +18 | noise: medium (other apps 7.2% of game CPUs); others on game CPUs 7.2%; clock avg/min 4245/4179 MHz; unseen-crowd-loop (unseenthrottle, unseenenv, unseenmotives, unseenbroadcast): 20 Sims walk out of view and back, ABBA on [UnseenThrottle] + the three ts3fixes switches. No cameraPan (it would make the walkers seen). Pass: no crash, utSkipped >= 1, unseen_env_skipped + motive + broadcast skips >= 1 |
| ip-rcr-route-legs | 16:34 (00:34) | 7.6 | **FAIL** | FAIL: profile loop window end reached: 3 events after 5.2 min, rule not met (off 1, on 2; 4 firings, 0 cycles; rule 4 events per arm; 1 steps capped), cap 6 min, seed 12345 (rng-compare.ts)<br>FAIL: fixes counters: legreplanskip_skipped=0 (want >=1)<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2165.2/1834.1/1516.9 -> 2212.1/1997.6/1355.4 | asyncRefused +66, asyncRefusedM1Pending +13 | noise: medium (other apps 7.2% of game CPUs); others on game CPUs 7.2%; clock avg/min 4253/4182 MHz; ip-route-legs-loop (legreplanskip, idlememo): IP crowd across the water and back, ABBA on the two ts3fixes switches. Pass: no crash, legreplanskip_skipped >= 1, idlememo_skipped >= 1. |
| bp-rcr-winter-pond | 16:42 (00:42) | 6.7 | PASS | PASS: profile loop window end reached: 5 events after 4.6 min, rule not met (off 3, on 2; 5 firings, 1 cycles; rule 4 events per arm), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2193.9/1833.2/1532.9 -> 2339.2/2077.8/1283.8 | asyncStarted +6246, asyncRefused +72, asyncCollected +6240, asyncDropped +3, asyncUs +48539655, asyncMaxUs +146385, asyncPumpUs +26826570, asyncPumpWaits +7026, asyncRefusedM1Pending +23, asyncSkipped +2, asyncBackoffs +4, asyncQueueMax +5 | noise: medium (other apps 8.3% of game CPUs); others on game CPUs 8.3%; clock avg/min 4241/4207 MHz; winter-pond-loop (waterjps): frozen pond crossing per firing, ABBA on waterjps. Needs noEnv. Pass: no crash, waterJpsMismatches 0. |
| bp-rcr-pet-guard | 16:49 (00:49) | 6.6 | **FAIL** | PASS: profile loop window end reached: 5 events after 4.6 min, rule not met (off 3, on 2; 6 firings, 1 cycles; rule 4 events per arm), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: discDeadSweep=0 (want >=1) | 0 | 2172.5/1837.2/1548.9 -> 2289.3/2054.4/1339.4 | asyncStarted +6672, asyncRefused +81, asyncCollected +6668, asyncDropped +1, asyncUs +46651851, asyncMaxUs +199495, asyncPumpUs +26238586, asyncPumpWaits +5586, asyncRefusedM1Pending +13, asyncBackoffs +6, asyncQueueMax +5 | noise: medium (other apps 7.1% of game CPUs); others on game CPUs 7.1%; clock avg/min 4254/4200 MHz; pet-guard-loop (discourage, deferfootprint): guard/cancel + kill one routing Sim per firing, ABBA on deferfootprint. Pass: no crash, discApplied >= 1, discDeadSweep >= 1 (the killed Sims), discIdMismatch 0. |
| bp-rcr-object-churn | 16:55 (00:55) | 5.6 | PASS | PASS: profile stopped at 8 events after 2.9 min (off 4, on 4; 8 firings, 2 cycles; rule 4 events per arm; 1 steps capped), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2166.1/1849/1532.9 -> 2258.7/2067.3/1291.4 | asyncStarted +3503, asyncRefused +41, asyncCollected +3502, asyncUs +24279861, asyncMaxUs +877036, asyncPumpUs +11744376, asyncPumpWaits +2541, asyncRefusedM1Pending +15, asyncBackoffs +3, asyncQueueMax +3 | noise: high (other apps 16.2% of game CPUs); others on game CPUs 16.2%; clock avg/min 4210/4165 MHz; object-churn-loop (destroypark): 10 objects placed and destroyed under running jobs per firing, ABBA on destroypark. Pass: no crash, dpParked >= 1. |
| bp-rcr-diner-crowd | 17:01 (01:01) | 6.7 | **FAIL** | PASS: profile loop window end reached: 4 events after 3.9 min, rule not met (off 2, on 2; 5 firings, 1 cycles; rule 4 events per arm), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: avTripAv=44144 (want <=0) | 0 | 2171/1839.1/1532.9 -> 2336.8/2057.9/1283.8 | asyncStarted +6363, asyncRefused +94, asyncCollected +6362, asyncDropped +1, asyncUs +49813895, asyncMaxUs +300865, asyncPumpUs +28605508, asyncPumpWaits +6951, asyncRefusedM1Pending +34, asyncBackoffs +2, asyncQueueMax +9 | noise: high (other apps 12.9% of game CPUs); others on game CPUs 12.9%; clock avg/min 4223/4122 MHz; diner-crowd-loop (slotenabledro, avoidro): the crowd arrives at Hogan's diner per firing, ABBA on both. Pass: no crash, slTaken >= 1, slShadowMismatch 0, avTripAv 0, avSimWriteInRo 0. |
| bp-rcr-candidate-loops | 17:08 (01:08) | 3.1 | PASS | PASS: profile stopped at 20 events after 1.2 min (off 10, on 10; 20 firings, 5 cycles; rule 10 events per arm), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2163.8/1854.3/1524.9 -> 2189.1/1886.8/1507.2 | asyncStarted +2134, asyncRefused +31, asyncCollected +2133, asyncUs +13445446, asyncMaxUs +143282, asyncPumpUs +6867406, asyncPumpWaits +1926, asyncRefusedM1Pending +20, asyncSkipped +1, asyncBackoffs +3, asyncQueueMax +3 | noise: high (other apps 13.7% of game CPUs); others on game CPUs 13.7%; clock avg/min 4234/4211 MHz; candidate-loops-loop (farthestsorted, indoorcap, safespotcap): the probe per firing, 20 firings ABBA (~1 min). Pass: no crash, the stop line, farthest_calls >= 1, indoorcap_calls >= 1; farthest lot id equal in every answer. |
| bp-rcr-replan-buildbuy | 17:11 (01:11) | 6.7 | PASS | PASS: profile loop window end reached: 5 events after 4.3 min, rule not met (off 3, on 2; 6 firings, 1 cycles; rule 4 events per arm), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2169.3/1855.9/1524.9 -> 2253/2067.8/1299.7 | asyncStarted +4302, asyncRefused +61, asyncCollected +4298, asyncDropped +1, asyncUs +44721846, asyncMaxUs +596438, asyncPumpUs +27010428, asyncPumpWaits +4400, asyncRefusedM1Pending +13, asyncBackoffs +7, asyncQueueMax +19 | noise: high (other apps 13.9% of game CPUs); others on game CPUs 13.9%; clock avg/min 4216/4179 MHz; replan-buildbuy-loop (replandefer, replanspread): two waves with a build/buy enter/exit each per firing, ABBA. Pass: no crash, replanspread_exceptions 0, replanspread_held >= 1. |

### bp-rcr-route-gate-crowd: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rcr-save-write: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rcr-alloc-churn: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rcr-unseen-crowd: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-rcr-route-legs: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rcr-winter-pond: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rcr-pet-guard: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rcr-object-churn: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rcr-diner-crowd: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rcr-candidate-loops: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rcr-replan-buildbuy: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T17:54 UTC, build b7b6eae9, jobs bp-rcr-save-write, ip-rcr-route-legs, bp-rcr-pet-guard, sv-rcr-diner-crowd

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rcr-save-write | 17:54 (01:54) | 4.4 | PASS | PASS: profile stopped at 4 events after 2.4 min (off 0, on 4; 4 firings, 4 cycles; rule 4 events), cap 14 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2170.2/1856.7/1524.9 -> 2246.8/1890.9/1443.5 | asyncStarted +213, asyncRefused +4, asyncCollected +212, asyncUs +3553201, asyncMaxUs +76070, asyncPumpUs +1494956, asyncPumpWaits +209, asyncQueueMax +2 | noise: medium (other apps 7.3% of game CPUs); others on game CPUs 7.3%; clock avg/min 4253/4228 MHz; save-write-loop (savewrite, decompress): CAS open/close, build/buy open/close and a real save to RC-SaveLoop per firing (the same name: worker backup from the second save). Pass: no crash, saveCount >= 3, decompressCalls >= 1, decompressFails 0, decompressMismatches 0; the RC-SaveLoop save loads (c |
| ip-rcr-route-legs | 17:58 (01:58) | 10.1 | PASS | PASS: profile stopped at 4 events after 6.9 min (off 2, on 2; 4 firings, 1 cycles; rule 2 events per arm), cap 16 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2160.2/1851.5/1532.9 -> 2226.1/2047.7/1371.4 | asyncRefused +67, asyncRefusedM1Pending +13 | noise: medium (other apps 7.7% of game CPUs); others on game CPUs 7.7%; clock avg/min 4243/4170 MHz; ip-route-legs-loop (legreplanskip, idlememo): IP crowd across the water and back, ABBA on the two ts3fixes switches. Pass: no crash, legreplanskip_skipped >= 1, idlememo_skipped >= 1. 2026-10-23: cap 16 min (4 firings per arm at 30-90 s need it; 6 min ran out after 3); legreplanskip never acted in ; **VOID: route worker hook failed (Frida prologue), async off** |
| bp-rcr-pet-guard | 18:08 (02:08) | 7.1 | PASS | PASS: profile stopped at 4 events after 4.0 min (off 2, on 2; 4 firings, 1 cycles; rule 2 events per arm), cap 14 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2173.1/1855.5/1524.9 -> 2318.7/2119.7/1279.3 | asyncStarted +7943, asyncRefused +110, asyncCollected +7939, asyncDropped +2, asyncUs +54208847, asyncMaxUs +199640, asyncPumpUs +30650377, asyncPumpWaits +7375, asyncRefusedM1Pending +23, asyncBackoffs +6, asyncQueueMax +12 | noise: medium (other apps 7.4% of game CPUs); others on game CPUs 7.4%; clock avg/min 4247/4209 MHz; pet-guard-loop (discourage, deferfootprint): guard/cancel per firing under running route jobs with the destroy gate off around the cancel (dead-id sweep each firing), plus one plain guard/cancel with the gate on first; ABBA on deferfootprint, one cycle. Pass: no crash, discApplied >= 1, discDeadSwe |
| sv-rcr-diner-crowd | 18:15 (02:15) | 7 | PASS | PASS: profile stopped at 4 events after 3.4 min (off 2, on 2; 4 firings, 1 cycles; rule 2 events per arm), cap 14 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2154.7/1789.6/1572.9 -> 2175.4/1975.7/1379.4 | asyncStarted +4586, asyncRefused +108, asyncCollected +4586, asyncUs +46563557, asyncMaxUs +342551, asyncPumpUs +26969178, asyncPumpWaits +5310, asyncRefusedM1Pending +13, asyncBackoffs +3, asyncQueueMax +18 | noise: medium (other apps 7.4% of game CPUs); others on game CPUs 7.4%; clock avg/min 4251/4209 MHz; diner-crowd-loop (slotenabledro, avoidro): the crowd arrives at Hogan's diner per firing, ABBA on both. Pass: no crash, slTaken >= 1, slShadowMismatch 0, avTripAv 0, avSimWriteInRo 0. 2026-10-23: Hogan diner and its Sims are Sunset Valley (repro ran on the BP save); avTripAv dropped from the rule:  |

### bp-rcr-save-write: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 14 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-rcr-route-legs: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 16 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rcr-pet-guard: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 14 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-rcr-diner-crowd: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 14 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
