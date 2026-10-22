# Queue profiler-cost 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/profiler-cost-2026-10-22.jsonl`.

## 2026-10-22T21:31 UTC, build 50872648, jobs bp-profcost-off-1, bp-profcost-on-1, bp-profcost-on-2, bp-profcost-off-2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-profcost-off-1 | 21:31 (05:31) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2232.2/1886.6/1516.8 -> 2328.9/2133.2/1288.3 | asyncStarted +13565, asyncRefused +192, asyncCollected +13564, asyncUs +86413073, asyncMaxUs +161874, asyncPumpUs +45613044, asyncPumpWaits +10607, asyncRefusedM1Pending +32, asyncBackoffs +9, asyncQueueMax +5 | noise: medium (other apps 8.0% of game CPUs); others on game CPUs 8.0%; clock avg/min 4249/4204 MHz; Lean measurement: stutter-profile.js -P {"stages":false} (boundary, catch-up steps, publish gaps; no stage/GC/route/task hooks), [HitchAttr] off, RouteJobPhases off; frames.csv as in full. Pair partner of the matching -on job. |
| bp-profcost-on-1 | 21:43 (05:43) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2168.1/1840.2/1548.8 -> 2279/2065.7/1315.3 | asyncStarted +13416, asyncRefused +218, asyncCollected +13413, asyncDropped +1, asyncUs +95859574, asyncMaxUs +185015, asyncPumpUs +51821537, asyncPumpWaits +11408, asyncRefusedM1Pending +47, asyncSkipped +1, asyncBackoffs +6, asyncQueueMax +6 | noise: medium (other apps 7.5% of game CPUs); others on game CPUs 7.5%; clock avg/min 4251/4192 MHz; Full measurement (freeze-measure.json bp-freeze-still as is): stage, GC, route and task hooks, [HitchAttr], RouteJobPhases + SyncPlans. Pair partner of the matching -off job (same save, seed 12345, camera untouched). |
| bp-profcost-on-2 | 21:55 (05:55) | 9 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: exit 1 with no verdict line (child stopped from outside or died before its summary); last output: at process.processTicksAndRejections (node:internal/process/task_queues:104:5) | 1 | 2166.3/1843.1/1548.9 -> ? | - | noise: medium (other apps 7.7% of game CPUs); others on game CPUs 7.7%; clock avg/min 4245/4190 MHz; Full measurement (freeze-measure.json bp-freeze-still as is): stage, GC, route and task hooks, [HitchAttr], RouteJobPhases + SyncPlans. Pair partner of the matching -off job (same save, seed 12345, camera untouched).; 2026-10-22T22:03:59.000Z  ACCESS_VIOLATION writing to address 0x00fc1a7c; State:  |
| bp-profcost-off-2 | 22:04 (06:04) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2169.1/1854.1/1532.9 -> 2309.3/2122.8/1274 | asyncStarted +13509, asyncRefused +227, asyncCollected +13504, asyncDropped +3, asyncUs +84866632, asyncMaxUs +181027, asyncPumpUs +44710367, asyncPumpWaits +9616, asyncRefusedM1Pending +46, asyncSkipped +1, asyncBackoffs +5, asyncQueueMax +4 | noise: high (other apps 10.1% of game CPUs); others on game CPUs 10.1%; clock avg/min 4239/4167 MHz; Lean measurement: stutter-profile.js -P {"stages":false} (boundary, catch-up steps, publish gaps; no stage/GC/route/task hooks), [HitchAttr] off, RouteJobPhases off; frames.csv as in full. Pair partner of the matching -on job. |

### bp-profcost-off-1: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-profcost-on-1: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-profcost-off-2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
