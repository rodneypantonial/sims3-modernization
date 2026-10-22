# Queue repro-check-route 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/repro-check-route-2026-10-22.jsonl`.

## 2026-10-22T15:27 UTC, build cce4f1ea, jobs bp-route-stuckcheck-reset, bp-route-queuefull-replan, bp-route-nopath-crowd, sv-route-seacheck-freeze

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-route-stuckcheck-reset | 15:27 (23:27) | 6.7 | **FAIL** | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: sypPlans.1 missing | 0 | 2210.2/1879/1532.8 -> 2401.2/2123.6/1275.6 | asyncStarted +6595, asyncRefused +95, asyncCollected +6583, asyncDropped +11, asyncUs +44972199, asyncMaxUs +136136, asyncPumpUs +25563810, asyncPumpWaits +9803, asyncRefusedM1Pending +18, asyncBackoffs +5, asyncQueueMax +5 | noise: high (other apps 12.1% of game CPUs); others on game CPUs 12.1%; clock avg/min 4273/4229 MHz; route-stuckcheck-reset-loop: the game's stuck-check alarm body and reset safe-location search on 20 stranded Sims, 8 loops ABBA (stuckchecknear, failrepeat, safespotcap). Pass: >= 20 stuck checks ran, >= 20 failed sim-thread plans. |

## 2026-10-22T15:50 UTC, build a213b69d, jobs bp-route-stuckcheck-reset, bp-route-queuefull-replan, bp-route-nopath-crowd, sv-route-seacheck-freeze

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-route-stuckcheck-reset | 15:50 (23:50) | 6.7 | **FAIL** | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: sypPlans.1 missing | 0 | 2170.4/1837.2/1532.9 -> 2339.1/2088.4/1251.8 | asyncStarted +6622, asyncRefused +82, asyncCollected +6617, asyncDropped +4, asyncUs +48330212, asyncMaxUs +112943, asyncPumpUs +25233006, asyncPumpWaits +13032, asyncRefusedM1Pending +30, asyncBackoffs +5, asyncQueueMax +8 | noise: medium (other apps 8.2% of game CPUs); others on game CPUs 8.2%; clock avg/min 4279/4231 MHz; route-stuckcheck-reset-loop: the game's stuck-check alarm body and reset safe-location search on 20 stranded Sims, 8 loops ABBA (stuckchecknear, failrepeat, safespotcap). Pass: >= 20 stuck checks ran, >= 20 failed sim-thread plans. |
| bp-route-queuefull-replan | 15:57 (23:57) | 6.7 | **FAIL** | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: log /reason NoStart/ 0 lines (want >= 5) | 0 | 2191/1840/1516.9 -> 2364.8/2066.6/1271.7 | asyncStarted +4802, asyncRefused +215, asyncCollected +4802, asyncUs +45719902, asyncMaxUs +279319, asyncPumpUs +29311194, asyncPumpWaits +6028, asyncRefusedM1Pending +28, asyncBackoffs +3, asyncQueueFull +151, asyncQueueMax +27 | noise: medium (other apps 8.6% of game CPUs); others on game CPUs 8.6%; clock avg/min 4265/4199 MHz; route-queuefull-replan-loop: 30+ Go Here starts in one tick and build/buy exit replan-all, 6 loops ABBA on native fallbackasync. Pass: >= 20 refused STARTs, QueueFull and NoStart sync plan lines. |
| bp-route-nopath-crowd | 16:04 (00:04) | 6.7 | **FAIL** | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: eoAsFail=8 (want >=20) | 0 | 2194/1855.3/1524.9 -> 2319.2/2113.9/1291.4 | asyncStarted +6548, asyncRefused +88, asyncCollected +6546, asyncUs +44746019, asyncMaxUs +166615, asyncPumpUs +23820868, asyncPumpWaits +10439, asyncRefusedM1Pending +9, asyncSkipped +1, asyncBackoffs +8, asyncQueueFull +1, asyncQueueMax +32 | noise: medium (other apps 7.7% of game CPUs); others on game CPUs 7.7%; clock avg/min 4276/4200 MHz; route-nopath-crowd-loop: 30+ Go Here plans to one enclosed spot per loop, 7 loops ABBA (earlyout, failrepeat). Pass: >= 20 failed world A* searches (counted in the on arms only). |
| sv-route-seacheck-freeze | 16:10 (00:10) | 2.6 | **FAIL** | FAIL: profile 6 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step crowd_sea_lot failed: no community lot with sea inside its area in this world<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: eoAsFail=0 (want >=20) | 1 | 2122.3/1790.4/1564.9 -> 2179.4/1971.8/1360.5 | asyncRefused +3, asyncRefusedM1Pending +1 | noise: medium (other apps 8.3% of game CPUs); others on game CPUs 8.3%; clock avg/min 4256/4214 MHz; route-sv-seacheck-freeze: the game's stuck check on crowd Sims standing on the SV lot with the most sea, 4 loops ABBA (SyncCapMs + FbJobCapMs + stuckchecknear + failrepeat). Pass: >= 20 stuck checks ran, >= 20 failed world A* searches. Read: failed plans >= 1 s and their 16-frame stacks (the 7 s ca |

### bp-route-stuckcheck-reset: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-route-queuefull-replan: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-route-nopath-crowd: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-route-seacheck-freeze: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T17:38 UTC, build 7639f0ef, jobs bp-route-stuckcheck-reset, bp-route-queuefull-replan, bp-route-nopath-crowd, sv-route-seacheck-freeze

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-route-stuckcheck-reset | 17:38 (01:38) | 4.1 | PASS | PASS: profile stopped at 134 events after 1.4 min (off 72, on 62; 8 firings, 2 cycles; rule 40 events per arm), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2167.5/1851.8/1532.9 -> 2252.4/1920.9/1467.3 | asyncStarted +4387, asyncRefused +40, asyncCollected +4380, asyncDropped +7, asyncUs +25537561, asyncMaxUs +173861, asyncPumpUs +12526866, asyncPumpWaits +6057, asyncRefusedM1Pending +15, asyncBackoffs +2, asyncQueueMax +9 | noise: medium (other apps 7.1% of game CPUs); others on game CPUs 7.1%; clock avg/min 4256/4180 MHz; route-stuckcheck-reset-loop: the game's stuck-check alarm body and reset safe-location search on 20 stranded Sims, loop form, one ABBA cycle until 40 stuck checks armed per arm (4 firings; stuckchecknear, failrepeat, safespotcap). Pass: >= 20 stuck checks ran, >= 20 failed sim-thread plans. |
| bp-route-queuefull-replan | 17:42 (01:42) | 5.1 | PASS | PASS: profile stopped at 4 events after 2.3 min (off 2, on 2; 4 firings, 1 cycles; rule 2 events per arm), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2163.7/1833/1532.9 -> 2360.3/2074.8/1267.8 | asyncStarted +3198, asyncRefused +117, asyncCollected +3196, asyncDropped +2, asyncUs +29035636, asyncMaxUs +457864, asyncPumpUs +17177919, asyncPumpWaits +4591, asyncRefusedM1Pending +9, asyncBackoffs +6, asyncQueueFull +77, asyncQueueMax +27 | noise: medium (other apps 7.0% of game CPUs); others on game CPUs 7.0%; clock avg/min 4275/4228 MHz; route-queuefull-replan-loop: 30+ Go Here starts in one tick and build/buy exit replan-all, loop form, one ABBA cycle (perArm 2 firings) on native fallbackasync. Pass: >= 20 refused STARTs, QueueFull and NoStart sync plan lines. NoStart plans run async with FallbackAsync on (fbForeign), no sync NoSt |
| bp-route-nopath-crowd | 17:47 (01:47) | 4.2 | PASS | PASS: profile stopped at 132 events after 1.5 min (off 66, on 66; 4 firings, 1 cycles; rule 50 events per arm), cap 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2167/1850.6/1532.9 -> 2236.5/2077.2/1323.4 | asyncStarted +3610, asyncRefused +33, asyncCollected +3609, asyncDropped +1, asyncUs +22854761, asyncMaxUs +113134, asyncPumpUs +11497960, asyncPumpWaits +5048, asyncRefusedM1Pending +27, asyncBackoffs +3, asyncQueueMax +31 | noise: medium (other apps 8.0% of game CPUs); others on game CPUs 8.0%; clock avg/min 4257/4201 MHz; route-nopath-crowd-loop: 30+ Go Here plans to one enclosed spot per loop, loop form, one ABBA cycle until 50 no-path plans sent per arm (4 firings; earlyout, failrepeat). Pass: >= 20 failed world A* searches (counted in the on arms only). Failed plans mostly end before world A* (2026-10-23: 94 fail |
| sv-route-seacheck-freeze | 17:51 (01:51) | 2.6 | **FAIL** | FAIL: profile 6 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step crowd_sea_lot failed: no lot with sea inside its area in this world (community or residential)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: eoAsFail=0 (want >=20) | 1 | 2103.8/1800.5/1556.9 -> 2174.2/1866.7/1507.4 | asyncRefused +9 | noise: medium (other apps 7.3% of game CPUs); others on game CPUs 7.3%; clock avg/min 4270/4246 MHz; route-sv-seacheck-freeze: the game's stuck check on crowd Sims standing on the SV lot with the most sea, 4 loops ABBA (SyncCapMs + FbJobCapMs + stuckchecknear + failrepeat). Pass: >= 20 stuck checks ran, >= 20 failed world A* searches. Read: failed plans >= 1 s and their 16-frame stacks (the 7 s ca |

### bp-route-stuckcheck-reset: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-route-queuefull-replan: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-route-nopath-crowd: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-route-seacheck-freeze: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
