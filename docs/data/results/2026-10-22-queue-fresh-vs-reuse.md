# Queue fresh-vs-reuse 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/fresh-vs-reuse-2026-10-22.jsonl`.

## 2026-10-22T20:45 UTC, build 94bee156, jobs bp-fvr-fresh-1, bp-fvr-reused-1, bp-fvr-reused-2, bp-fvr-fresh-2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-fvr-fresh-1 | 20:45 (04:45) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2162.6/1847.9/1524.9 -> 2283.6/2070.9/1331.4 | asyncStarted +12930, asyncRefused +191, asyncCollected +12924, asyncDropped +6, asyncUs +93754487, asyncMaxUs +143099, asyncPumpUs +50327321, asyncPumpWaits +9886, asyncRefusedM1Pending +59, asyncBackoffs +10, asyncQueueMax +5 | noise: high (other apps 11.5% of game CPUs); others on game CPUs 11.5%; clock avg/min 4215/4174 MHz; fresh vs reused ABBA (docs/170 §2, I-170-14): same save/seed/settings, fresh game start; compare real-hitch rates, FPS, it/s across arms |
| bp-fvr-reused-1 | 20:56 (04:56) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2165.8/1837/1548.9 -> 2304.5/2086.1/1278.1 | asyncStarted +12312, asyncRefused +165, asyncCollected +12307, asyncDropped +4, asyncUs +89103157, asyncMaxUs +178958, asyncPumpUs +47962817, asyncPumpWaits +10206, asyncRefusedM1Pending +52, asyncSkipped +1, asyncBackoffs +8, asyncQueueMax +5 | fresh load; drift: sim time +94.0 h since load (limit 48 h) (Sims 84 (-7), members 3, lot objects 111 (+1), commit +140.5 MB, largest free 1278.1 MB, sim +94.0 h, stuck 0, window 1 since load); fresh start for bp-fvr-reused-2; noise: high (other apps 11.0% of game CPUs); others on game CPUs 11.0%; clock avg/min 4216/4175 MHz; fresh vs reused ABBA (docs/170 §2, I-170-14): same save/seed/settings, r |
| bp-fvr-reused-2 | 21:08 (05:08) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2160.4/1835.3/1532.9 -> 2274.9/2060.5/1299.4 | asyncStarted +13025, asyncRefused +217, asyncCollected +13018, asyncDropped +4, asyncUs +92977587, asyncMaxUs +203573, asyncPumpUs +50967719, asyncPumpWaits +11258, asyncRefusedM1Pending +33, asyncBackoffs +11, asyncQueueMax +5 | noise: medium (other apps 7.8% of game CPUs); others on game CPUs 7.8%; clock avg/min 4248/4198 MHz; fresh vs reused ABBA (docs/170 §2, I-170-14): same save/seed/settings, reused game session; compare real-hitch rates, FPS, it/s across arms |
| bp-fvr-fresh-2 | 21:20 (05:20) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2161.7/1838.3/1532.9 -> 2285.5/2069.2/1291.4 | asyncStarted +13385, asyncRefused +179, asyncCollected +13382, asyncDropped +2, asyncUs +97677002, asyncMaxUs +188296, asyncPumpUs +52160341, asyncPumpWaits +11419, asyncRefusedM1Pending +42, asyncBackoffs +9, asyncQueueMax +5 | noise: medium (other apps 7.7% of game CPUs); others on game CPUs 7.7%; clock avg/min 4246/4203 MHz; fresh vs reused ABBA (docs/170 §2, I-170-14): same save/seed/settings, fresh game start; compare real-hitch rates, FPS, it/s across arms |

### bp-fvr-fresh-1: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-fvr-reused-1: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-fvr-reused-2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-fvr-fresh-2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
