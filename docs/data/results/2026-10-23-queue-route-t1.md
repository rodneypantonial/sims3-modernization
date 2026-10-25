# Queue route-t1 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/route-t1-2026-10-23.jsonl`.

## 2026-10-23T04:30 UTC, build 5d9758f0, jobs sv-crowd-t1-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-crowd-t1-verify | 04:30 (12:30) | 11.9 | **FAIL** | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: fbjdReady=0 (want >=7); gsReady=0 (want >=255); owReady=0 (want >=1) | 0 | 2081.2/1804.8/1612.9 -> 2212.2/1998.7/1407 | asyncStarted +5548, asyncRefused +131, asyncCollected +5548, asyncUs +50407728, asyncMaxUs +367573, asyncPumpUs +29839701, asyncPumpWaits +6045, asyncRefusedM1Pending +26, asyncBackoffs +4, asyncQueueMax +10 | noise: high (other apps 14.9% of game CPUs); others on game CPUs 14.9%; clock avg/min 4186/4146 MHz; Day (correctness, not timing): sv-crowd-cause settings (rare-big-crowd, 10 min) + T1 on, P-G + P-GS, OwnJobWait. Log must show OK route worker: runtime_invoke. Pass: no crash, the two OK lines, fbjdReady 7, gsReady 255, g0wWatchdog 0. Read: every 'route async: long job ... foreign' line capped with |

### sv-crowd-t1-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-23T05:45 UTC, build 551dfa85, jobs sv-crowd-t1-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-crowd-t1-verify | 05:45 (13:45) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2128/1806.3/1468.9 -> 2260.2/2000.6/1254.2 | asyncStarted +5532, asyncRefused +145, asyncCollected +5532, asyncUs +52757181, asyncMaxUs +379552, asyncPumpUs +31234695, asyncPumpWaits +5992, asyncRefusedM1Pending +4, asyncBackoffs +6, asyncQueueMax +12 | noise: high (other apps 15.8% of game CPUs); others on game CPUs 15.8%; clock avg/min 4156/4117 MHz; Day (correctness, not timing): sv-crowd-cause settings (rare-big-crowd, 10 min) + T1 on, P-G + P-GS, OwnJobWait. Log must show OK route worker: runtime_invoke. Pass: no crash, the two OK lines, fbjdReady 7, gsReady 255, g0wWatchdog 0. Read: every 'route async: long job ... foreign' line capped with |

### sv-crowd-t1-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-23T06:38 UTC, build 5f74322f, jobs sv-crowd-t1-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-crowd-t1-verify | 06:38 (14:38) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2126.9/1807.3/1468.9 -> 2263.2/1976/1270.2 | asyncStarted +5776, asyncRefused +146, asyncCollected +5776, asyncUs +50787731, asyncMaxUs +365316, asyncPumpUs +30372035, asyncPumpWaits +5813, asyncRefusedM1Pending +15, asyncBackoffs +4, asyncQueueMax +10 | noise: high (other apps 10.8% of game CPUs); others on game CPUs 10.8%; clock avg/min 4131/4091 MHz; Day (correctness, not timing): sv-crowd-cause settings (rare-big-crowd, 10 min) + T1 on, P-G + P-GS, OwnJobWait. Log must show OK route worker: runtime_invoke. Pass: no crash, the two OK lines, fbjdReady 7, gsReady 255, g0wWatchdog 0. Read: every 'route async: long job ... foreign' line capped with |

### sv-crowd-t1-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
