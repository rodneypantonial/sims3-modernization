# Queue route-hitch-loops 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/route-hitch-loops-2026-10-22.jsonl`.

## 2026-10-22T20:28 UTC, build 94bee156, jobs bp-route-stuckcheck-reset-ab, bp-route-queuefull-replan-ab, bp-route-nopath-crowd-ab, sv-route-seacheck-freeze-ab

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-route-stuckcheck-reset-ab | 20:28 (04:28) | 4.1 | PASS | PASS: profile stopped at 121 events after 1.3 min (off 61, on 60; 8 firings, 2 cycles; rule 40 events per arm), cap 15 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2166.1/1841.7/1532.9 -> 2274.3/2065.9/1315.4 | asyncStarted +4296, asyncRefused +42, asyncCollected +4285, asyncDropped +10, asyncUs +30444974, asyncMaxUs +530019, asyncPumpUs +15517338, asyncPumpWaits +4821, asyncRefusedM1Pending +9, asyncBackoffs +2, asyncQueueMax +5 | noise: medium (other apps 7.9% of game CPUs); others on game CPUs 7.9%; clock avg/min 4269/4227 MHz; route-stuckcheck-reset-loop: the game's stuck-check alarm body and reset safe-location search on 20 stranded Sims, loop form, one ABBA cycle until 40 stuck checks armed per arm (4 firings; stuckchecknear, failrepeat, safespotcap). Pass: >= 20 stuck checks ran, >= 20 failed sim-thread plans. |
| bp-route-queuefull-replan-ab | 20:33 (04:33) | 5.4 | PASS | PASS: profile stopped at 4 events after 2.4 min (off 2, on 2; 4 firings, 1 cycles; rule 2 events per arm), cap 15 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2166.3/1837.2/1532.9 -> 2277.1/2080/1243.8 | asyncStarted +3498, asyncRefused +88, asyncCollected +3495, asyncDropped +2, asyncUs +29780292, asyncMaxUs +279554, asyncPumpUs +17270304, asyncPumpWaits +4489, asyncRefusedM1Pending +9, asyncSkipped +1, asyncBackoffs +5, asyncQueueFull +64, asyncQueueMax +28 | noise: medium (other apps 8.1% of game CPUs); others on game CPUs 8.1%; clock avg/min 4260/4216 MHz; route-queuefull-replan-loop: 30+ Go Here starts in one tick and build/buy exit replan-all, loop form, one ABBA cycle (perArm 2 firings) on native fallbackasync. Pass: >= 20 refused STARTs, QueueFull and NoStart sync plan lines. NoStart plans run async with FallbackAsync on (fbForeign), no sync NoSt |
| bp-route-nopath-crowd-ab | 20:38 (04:38) | 4.2 | PASS | PASS: profile stopped at 132 events after 1.4 min (off 66, on 66; 4 firings, 1 cycles; rule 50 events per arm), cap 15 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2165.8/1833.5/1548.8 -> 2272.4/2053.9/1323.3 | asyncStarted +3457, asyncRefused +39, asyncCollected +3456, asyncDropped +1, asyncUs +23691986, asyncMaxUs +180934, asyncPumpUs +12157538, asyncPumpWaits +5371, asyncRefusedM1Pending +9, asyncBackoffs +1, asyncQueueMax +27 | noise: high (other apps 10.1% of game CPUs); others on game CPUs 10.1%; clock avg/min 4262/4203 MHz; route-nopath-crowd-loop: 30+ Go Here plans to one enclosed spot per loop, loop form, one ABBA cycle until 50 no-path plans sent per arm (4 firings; earlyout, failrepeat). Pass: >= 20 failed world A* searches (counted in the on arms only). Failed plans mostly end before world A* (2026-10-23: 94 fail |
| sv-route-seacheck-freeze-ab | 20:42 (04:42) | 2.6 | **FAIL** | FAIL: profile 15 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step crowd_sea_lot failed: no lot with sea inside its area in this world (community or residential)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: eoAsFail=0 (want >=20) | 1 | 2167.5/1789.4/1564.9 -> 2118/1801.2/1547.3 | asyncRefused +7, asyncRefusedM1Pending +2 | noise: high (other apps 10.6% of game CPUs); others on game CPUs 10.6%; clock avg/min 4246/4219 MHz; route-sv-seacheck-freeze: the game's stuck check on crowd Sims standing on the SV lot with the most sea, 4 loops ABBA (SyncCapMs + FbJobCapMs + stuckchecknear + failrepeat). Pass: >= 20 stuck checks ran, >= 20 failed world A* searches. Read: failed plans >= 1 s and their 16-frame stacks (the 7 s ca; **VOID: route worker hook failed (Frida prologue), async off** |

### bp-route-stuckcheck-reset-ab: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-route-queuefull-replan-ab: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-route-nopath-crowd-ab: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-route-seacheck-freeze-ab: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
