# Queue route-hitch-loops 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/route-hitch-loops-2026-10-23.jsonl`.

## 2026-10-23T01:09 UTC, build 7d043ee6, jobs bp-route-stuckcheck-reset-ab, bp-route-queuefull-replan-ab, bp-route-nopath-crowd-ab

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-route-stuckcheck-reset-ab | 01:09 (09:09) | 2.7 | **FAIL** | FAIL: profile 15 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step strand_crowd failed: no unroutable spot on Community Lot (try another lot or more tries)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 1 | 2163.4/1834.4/1548.9 -> 2217.4/1899/1491.3 | asyncStarted +1262, asyncRefused +7, asyncCollected +1261, asyncUs +9752646, asyncMaxUs +90078, asyncPumpUs +5730705, asyncPumpWaits +1906, asyncRefusedM1Pending +8, asyncSkipped +1, asyncQueueMax +3 | noise: high (other apps 13.9% of game CPUs); others on game CPUs 13.9%; clock avg/min 4218/4173 MHz; route-stuckcheck-reset-loop: the game's stuck-check alarm body and reset safe-location search on 20 stranded Sims, loop form, ABBA until 40 stuck checks armed and >= 10 firings per arm (loopRule, docs/168 §7; ~20 firings x <= 34 s; stuckchecknear, failrepeat, safespotcap). Pass: >= 20 stuck checks  |
| bp-route-queuefull-replan-ab | 01:12 (09:12) | 15.5 | PASS | PASS: profile stopped at 20 events after 12.5 min (off 10, on 10; 20 firings, 5 cycles; rule 10 events per arm, >= 10 firings per arm; 2 steps capped), cap 19 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2165/1848.9/1532.9 -> 2353.4/2137.8/1211.6 | asyncStarted +10957, asyncRefused +608, asyncCollected +10952, asyncDropped +2, asyncUs +118078652, asyncMaxUs +619465, asyncPumpUs +72474919, asyncPumpWaits +12944, asyncRefusedM1Pending +43, asyncSkipped +1, asyncBackoffs +11, asyncQueueFull +457, asyncQueueMax +17 | noise: medium (other apps 8.3% of game CPUs); others on game CPUs 8.3%; clock avg/min 4235/4172 MHz; route-queuefull-replan-loop: 30+ Go Here starts in one tick and build/buy exit replan-all, loop form, 10 firings per arm (loopRule, docs/168 §7; the repro's own perArm 2 stays for repro-check-route) on native fallbackasync. Pass: >= 20 refused STARTs, QueueFull and NoStart sync plan lines. NoStart  |
| bp-route-nopath-crowd-ab | 01:27 (09:27) | 11.2 | PASS | PASS: profile stopped at 660 events after 8.2 min (off 330, on 330; 20 firings, 5 cycles; rule 50 events per arm, >= 10 firings per arm; 3 steps capped), cap 15 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2160.7/1851/1532.9 -> 2283.8/2145.7/1275.4 | asyncStarted +12479, asyncRefused +136, asyncCollected +12466, asyncDropped +1, asyncUs +89138265, asyncMaxUs +258861, asyncPumpUs +46918366, asyncPumpWaits +20633, asyncRefusedM1Pending +73, asyncSkipped +10, asyncBackoffs +10, asyncQueueMax +31 | noise: medium (other apps 7.9% of game CPUs); others on game CPUs 7.9%; clock avg/min 4253/4195 MHz; route-nopath-crowd-loop: 30+ Go Here plans to one enclosed spot per loop, loop form, ABBA until 50 no-path plans sent and >= 10 firings per arm (loopRule, docs/168 §7; earlyout, failrepeat). Pass: >= 20 failed world A* searches (counted in the on arms only). Failed plans mostly end before world A*  |

### bp-route-stuckcheck-reset-ab: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-route-queuefull-replan-ab: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 19 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
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

## 2026-10-23T23:06 UTC, build cab77de5, jobs sv-route-seacheck-freeze-ab

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-route-seacheck-freeze-ab | 23:06 (07:06) | 2.5 | **FAIL** | FAIL: profile 15 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step crowd_sea_lot failed: no lot with sea inside its area in this world (community or residential)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: eoAsFail=0 (want >=20) | 1 | 2138.9/1803.2/1548.9 -> 2145.9/1840.7/1531.3 | asyncStarted +690, asyncRefused +5, asyncCollected +690, asyncUs +6514316, asyncMaxUs +437262, asyncPumpUs +3776617, asyncPumpWaits +1418, asyncQueueMax +3 | noise: medium (other apps 5.6% of game CPUs); others on game CPUs 5.6%; clock avg/min 4207/4176 MHz; route-sv-seacheck-freeze: the game's stuck check on crowd Sims standing on the SV lot with the most sea, 4 loops ABBA (SyncCapMs + FbJobCapMs + stuckchecknear + failrepeat). Pass: >= 20 stuck checks ran, >= 20 failed world A* searches. Read: failed plans >= 1 s and their 16-frame stacks (the 7 s ca |

### sv-route-seacheck-freeze-ab: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
