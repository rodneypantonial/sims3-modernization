# Queue tool-check 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/tool-check-2026-10-22.jsonl`.

## 2026-10-22T14:05 UTC, build 1588c502, jobs bp-tool-check

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-tool-check | 14:05 (22:05) | 6.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2170.5/1840.3/1532.9 -> 2317.7/2079.3/1267.8 | asyncStarted +5472, asyncRefused +63, asyncCollected +5471, asyncDropped +1, asyncUs +39193838, asyncMaxUs +130875, asyncPumpUs +21474493, asyncPumpWaits +8871, asyncRefusedM1Pending +19, asyncBackoffs +4, asyncQueueMax +4 | noise: medium (other apps 8.8% of game CPUs); others on game CPUs 8.8%; clock avg/min 4253/4225 MHz; Correctness check of the measurement tool (5 min): files written, every column filled, causes named, no crash. Not a timing run. |

### bp-tool-check: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T14:21 UTC, build b1c0ea57, jobs bp-tool-check

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-tool-check | 14:21 (22:21) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166.6/1849.7/1524.9 -> 2310.9/2112.5/1267.7 | asyncStarted +5657, asyncRefused +54, asyncCollected +5657, asyncUs +39691679, asyncMaxUs +164533, asyncPumpUs +22285168, asyncPumpWaits +8675, asyncRefusedM1Pending +20, asyncBackoffs +9, asyncQueueMax +5 | noise: high (other apps 13.4% of game CPUs); others on game CPUs 13.4%; clock avg/min 4254/4191 MHz; Correctness check of the measurement tool (5 min): files written, every column filled, causes named, no crash. Not a timing run. |

### bp-tool-check: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
