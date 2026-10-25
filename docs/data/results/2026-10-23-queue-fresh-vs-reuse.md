# Queue fresh-vs-reuse 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/fresh-vs-reuse-2026-10-23.jsonl`.

## 2026-10-23T00:17 UTC, build 7abc5d29, jobs bp-fvr-fresh-1, bp-fvr-reused-1, bp-fvr-reused-2, bp-fvr-fresh-2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-fvr-fresh-1 | 00:17 (08:17) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2165.8/1835.7/1532.9 -> 2293.8/2056.2/1315.4 | asyncStarted +12916, asyncRefused +176, asyncCollected +12912, asyncDropped +3, asyncUs +92543639, asyncMaxUs +177964, asyncPumpUs +49560377, asyncPumpWaits +9693, asyncRefusedM1Pending +45, asyncSkipped +1, asyncBackoffs +6, asyncQueueMax +5 | fresh load; drift ok (Sims 85 (-6), members 3, lot objects 114 (+4), commit +128.5 MB, largest free 1315.4 MB, sim +94.7 h, stuck 0, window 1 since load); kept for bp-fvr-reused-1; noise: medium (other apps 9.0% of game CPUs); others on game CPUs 9.0%; clock avg/min 4223/4168 MHz; fresh vs reused ABBA (docs/170 §2, I-170-14): same save/seed/settings, fresh game start; compare real-hitch rates, FPS |
| bp-fvr-reused-1 | 00:29 (08:29) | 10.1 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2293.6/2054.6/1315.4 -> 2278.3/2090/1265.8 | asyncStarted +12613, asyncRefused +144, asyncCollected +12612, asyncDropped +1, asyncUs +88660791, asyncPumpUs +47106419, asyncPumpWaits +8184, asyncRefusedM1Pending +90, asyncBackoffs +6 | reused session (#1 since load); noise: medium (other apps 6.3% of game CPUs); others on game CPUs 6.3%; clock avg/min 4239/4179 MHz; fresh vs reused ABBA (docs/170 §2, I-170-14): same save/seed/settings, reused game session; compare real-hitch rates, FPS, it/s across arms |
| bp-fvr-reused-2 | 00:39 (08:39) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2164.3/1854.4/1524.9 -> 2320.2/2089.6/1251.7 | asyncStarted +12269, asyncRefused +185, asyncCollected +12267, asyncDropped +2, asyncUs +89445111, asyncMaxUs +160936, asyncPumpUs +46867794, asyncPumpWaits +10039, asyncRefusedM1Pending +39, asyncBackoffs +11, asyncQueueMax +4 | noise: high (other apps 17.7% of game CPUs); others on game CPUs 17.7%; clock avg/min 4194/4125 MHz; fresh vs reused ABBA (docs/170 §2, I-170-14): same save/seed/settings, reused game session; compare real-hitch rates, FPS, it/s across arms |
| bp-fvr-fresh-2 | 00:51 (08:51) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2149.6/1812.6/1564.9 -> 2329.4/2074.2/1283.4 | asyncStarted +12591, asyncRefused +179, asyncCollected +12587, asyncDropped +4, asyncUs +90142170, asyncMaxUs +169324, asyncPumpUs +47814601, asyncPumpWaits +9631, asyncRefusedM1Pending +29, asyncBackoffs +4, asyncQueueMax +6 | noise: high (other apps 12.2% of game CPUs); others on game CPUs 12.2%; clock avg/min 4223/4171 MHz; fresh vs reused ABBA (docs/170 §2, I-170-14): same save/seed/settings, fresh game start; compare real-hitch rates, FPS, it/s across arms |

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
