# Queue apex-standdown 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/apex-standdown-2026-10-22.jsonl`.

## 2026-10-22T07:42 UTC, build dabf569e, jobs apex-first-0325, apex-last-0325, apex-save-matrix-0325

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| apex-first-0325 | 07:42 (15:42) | 4.8 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2344.9/2051.1/1213.3 -> 2398.9/2305.1/900.7 | asyncStarted +4765, asyncRefused +47, asyncCollected +4763, asyncDropped +1, asyncUs +25436738, asyncMaxUs +668647, asyncPumpUs +10736343, asyncPumpWaits +1514, asyncRefusedM1Pending +7, asyncBackoffs +7, asyncQueueMax +5 | noise: high (other apps 40.4% of game CPUs, NOT comparable (load or clock)); others on game CPUs 40.4%; clock avg/min 3997/3540 MHz; Apex stand-down live check (2026-10-22) on asi.0325 (stand-down + CE-1). Read ts3native.log for the Apex detection line and any 'compat: stood down for Apex' lines; with release defaults most modules are off, so the main live change is [SaveWrite] leaving 0xAB12AF to |
| apex-last-0325 | 07:47 (15:47) | 5.1 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2270.3/2060.1/1273.4 -> 2447.3/2312.9/1005 | asyncStarted +4294, asyncRefused +34, asyncCollected +4289, asyncDropped +3, asyncUs +20587862, asyncMaxUs +278548, asyncPumpUs +9170078, asyncPumpWaits +1201, asyncRefusedM1Pending +18, asyncBackoffs +3, asyncQueueMax +4 | noise: high (other apps 40.1% of game CPUs); others on game CPUs 40.1%; clock avg/min 3814/3396 MHz; Apex stand-down live check (2026-10-22) on asi.0325 (stand-down + CE-1). Read ts3native.log for the Apex detection line and any 'compat: stood down for Apex' lines; with release defaults most modules are off, so the main live change is [SaveWrite] leaving 0xAB12AF to Room to Save. docs/148 §7 smoke |
| apex-save-matrix-0325 | 07:52 (15:52) | 2.8 | PASS | control: PASS<br>identical: PASS<br>summary: 2 cases, 2 passed, 0 failed<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2356.2/2071.7/1179.4 -> 2331.6/1981.4/1163.4 | - | noise: high (other apps 43.1% of game CPUs); others on game CPUs 43.1%; clock avg/min 3883/3458 MHz; Apex stand-down live check (2026-10-22) on asi.0325 (stand-down + CE-1). Read ts3native.log for the Apex detection line and any 'compat: stood down for Apex' lines; with release defaults most modules are off, so the main live change is [SaveWrite] leaving 0xAB12AF to Room to Save. docs/148 C1: save |

### apex-first-0325: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### apex-last-0325: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### apex-save-matrix-0325: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
