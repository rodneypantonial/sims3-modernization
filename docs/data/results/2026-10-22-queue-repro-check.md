# Queue repro-check 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/repro-check-2026-10-22.jsonl`.

## 2026-10-22T14:37 UTC, build 8e709d77, jobs bp-stuckcheck-check, bp-easpread-lots-check, bp-easpread-pets-check, bp-easpread-decay-check, bp-errortrap-check

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-stuckcheck-check | 14:37 (22:37) | 6.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2162.8/1851/1532.9 -> 2133.5/1850/1515.3 | asyncStarted +2367, asyncRefused +19, asyncCollected +2366, asyncDropped +1, asyncUs +24252688, asyncMaxUs +114198, asyncPumpUs +8929003, asyncPumpWaits +1207, asyncRefusedM1Pending +10, asyncBackoffs +3, asyncQueueMax +3 | noise: high (other apps 11.1% of game CPUs); others on game CPUs 11.1%; clock avg/min 4258/4212 MHz; docs/169: hitch-stuckcheck-check correctness (5 min): the firing counted, hitch lines carry the expected task name. Not a timing run. |

## 2026-10-22T14:49 UTC, build 2947aa79, jobs bp-stuckcheck-check, bp-easpread-lots-check, bp-easpread-pets-check, bp-easpread-decay-check, bp-errortrap-check

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-stuckcheck-check | 14:49 (22:49) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2141.6/1854.8/1548.9 -> 2147.5/1865.1/1513.6 | asyncStarted +2338, asyncRefused +11, asyncCollected +2338, asyncUs +23668101, asyncMaxUs +187946, asyncPumpUs +8765766, asyncPumpWaits +1016, asyncRefusedM1Pending +11, asyncBackoffs +1, asyncQueueMax +3 | noise: high (other apps 14.9% of game CPUs); others on game CPUs 14.9%; clock avg/min 4231/4154 MHz; docs/169: hitch-stuckcheck-check correctness (5 min): the firing counted, hitch lines carry the expected task name. Not a timing run. |
| bp-easpread-lots-check | 14:56 (22:56) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2169.3/1850.2/1524.9 -> 2160.6/1860.9/1507.2 | asyncStarted +1595, asyncRefused +19, asyncCollected +1595, asyncUs +21060006, asyncMaxUs +135334, asyncPumpUs +8476667, asyncPumpWaits +838, asyncRefusedM1Pending +8, asyncQueueMax +3 | noise: high (other apps 15.6% of game CPUs); others on game CPUs 15.6%; clock avg/min 4234/4187 MHz; docs/169: hitch-easpread-lots-check correctness (5 min): the firing counted, hitch lines carry the expected task name. Not a timing run. |
| bp-easpread-pets-check | 15:02 (23:02) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2136/1852.2/1564.8 -> 2204.1/1861.8/1497.7 | asyncStarted +982, asyncRefused +23, asyncCollected +982, asyncUs +11470736, asyncMaxUs +92942, asyncPumpUs +3945874, asyncPumpWaits +469, asyncRefusedM1Pending +11, asyncBackoffs +1, asyncQueueMax +2 | noise: high (other apps 19.2% of game CPUs); others on game CPUs 19.2%; clock avg/min 4228/4183 MHz; docs/169: hitch-easpread-pets-check correctness (5 min): the firing counted, hitch lines carry the expected task name. Not a timing run. |
| bp-easpread-decay-check | 15:09 (23:09) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2197.7/1851.9/1532.8 -> 2213.6/1867.4/1483.4 | asyncStarted +1040, asyncRefused +13, asyncCollected +1040, asyncUs +11527515, asyncMaxUs +99328, asyncPumpUs +3941152, asyncPumpWaits +493, asyncRefusedM1Pending +7, asyncQueueMax +2 | noise: high (other apps 16.6% of game CPUs); others on game CPUs 16.6%; clock avg/min 4227/4189 MHz; docs/169: hitch-easpread-decay-check correctness (5 min): the firing counted, hitch lines carry the expected task name. Not a timing run. |
| bp-errortrap-check | 15:16 (23:16) | 6.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2159.3/1833.2/1532.9 -> 2201.6/1851.2/1483.6 | asyncStarted +1002, asyncRefused +10, asyncCollected +1002, asyncUs +11092312, asyncMaxUs +90513, asyncPumpUs +4408399, asyncPumpWaits +456, asyncRefusedM1Pending +5, asyncQueueMax +3 | noise: high (other apps 12.1% of game CPUs); others on game CPUs 12.1%; clock avg/min 4247/4183 MHz; docs/169: hitch-errortrap-check correctness (5 min): the firing counted, hitch lines carry the expected task name. Not a timing run. |

### bp-stuckcheck-check: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-easpread-lots-check: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-easpread-pets-check: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-easpread-decay-check: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-errortrap-check: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
