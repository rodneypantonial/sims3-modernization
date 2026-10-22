# Queue script-hitch-loops 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/script-hitch-loops-2026-10-22.jsonl`.

## 2026-10-22T19:35 UTC, build f22871a3, jobs bp-stuckcheck-loop, bp-easpread-lots-loop, bp-easpread-pets-loop, bp-easpread-decay-loop, bp-errortrap-loop

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-stuckcheck-loop | 19:35 (03:35) | 6.6 | PASS | PASS: profile stopped at 240 events after 3.4 min (off 120, on 120; 20 firings, 5 cycles; rule 120 events per arm), cap 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2159.5/1833.7/1548.8 -> 2152.5/1881.6/1499.3 | asyncStarted +6186, asyncRefused +336, asyncCollected +6185, asyncDropped +1, asyncUs +47483732, asyncMaxUs +152906, asyncPumpUs +22177775, asyncPumpWaits +3170, asyncRefusedM1Pending +9, asyncBackoffs +2, asyncQueueFull +298, asyncQueueMax +23 | noise: high (other apps 10.0% of game CPUs); others on game CPUs 10.0%; clock avg/min 4246/4201 MHz; docs/169: hitch-stuckcheck-loop, 10 firings per arm in one load. Read: hitch-ab.ts --scenario <tag>; clean night only. |
| bp-easpread-lots-loop | 19:42 (03:42) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2161.6/1836/1548.8 -> 2215.6/1874.7/1443.6 | asyncStarted +4800, asyncRefused +44, asyncCollected +4799, asyncDropped +1, asyncUs +47907500, asyncMaxUs +310136, asyncPumpUs +21711558, asyncPumpWaits +3158, asyncRefusedM1Pending +9, asyncBackoffs +1, asyncQueueMax +3 | noise: medium (other apps 9.8% of game CPUs); others on game CPUs 9.8%; clock avg/min 4222/4179 MHz; docs/169: hitch-easpread-lots-loop, 10 firings per arm in one load. Read: hitch-ab.ts --scenario <tag>; clean night only. |
| bp-easpread-pets-loop | 19:53 (03:53) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2168.7/1838.5/1532.9 -> 2203.4/1864.1/1451.6 | asyncStarted +2905, asyncRefused +36, asyncCollected +2905, asyncUs +22565482, asyncMaxUs +178581, asyncPumpUs +9478064, asyncPumpWaits +1823, asyncRefusedM1Pending +8, asyncBackoffs +2, asyncQueueMax +4 | noise: medium (other apps 8.1% of game CPUs); others on game CPUs 8.1%; clock avg/min 4232/4014 MHz; docs/169: hitch-easpread-pets-loop, 10 firings per arm in one load. Read: hitch-ab.ts --scenario <tag>; clean night only. |
| bp-easpread-decay-loop | 20:05 (04:05) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2200.6/1854.3/1524.9 -> 2219.2/1873.3/1475.5 | asyncStarted +2904, asyncRefused +36, asyncCollected +2904, asyncUs +20625378, asyncMaxUs +138284, asyncPumpUs +9164311, asyncPumpWaits +1972, asyncRefusedM1Pending +8, asyncBackoffs +1, asyncQueueMax +4 | noise: medium (other apps 7.6% of game CPUs); others on game CPUs 7.6%; clock avg/min 4243/4179 MHz; docs/169: hitch-easpread-decay-loop, 10 firings per arm in one load. Read: hitch-ab.ts --scenario <tag>; clean night only. |
| bp-errortrap-loop | 20:17 (04:17) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166.4/1853.4/1532.9 -> 2255.6/1885.1/1467.6 | asyncStarted +2917, asyncRefused +39, asyncCollected +2917, asyncUs +22263570, asyncMaxUs +240684, asyncPumpUs +9625298, asyncPumpWaits +1809, asyncRefusedM1Pending +21, asyncBackoffs +2, asyncQueueMax +4 | noise: medium (other apps 7.6% of game CPUs); others on game CPUs 7.6%; clock avg/min 4266/4228 MHz; docs/169: hitch-errortrap-loop, 10 firings per arm in one load. Read: hitch-ab.ts --scenario <tag>; clean night only. |

### bp-stuckcheck-loop: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-easpread-lots-loop: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-easpread-pets-loop: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-easpread-decay-loop: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-errortrap-loop: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
