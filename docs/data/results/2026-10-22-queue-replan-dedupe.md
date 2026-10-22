# Queue replan-dedupe 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/replan-dedupe-2026-10-22.jsonl`.

## 2026-10-22T07:24 UTC, build 91ad3f6f, jobs bp-dedupe-verify, ip-dedupe-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-dedupe-verify | 07:24 (15:24) | 5.1 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2215.8/1851.5/1494 -> 2307.6/1925.3/1423.3 | asyncStarted +3685, asyncRefused +77, asyncCollected +3683, asyncUs +21114758, asyncMaxUs +95310, asyncPumpUs +12616826, asyncPumpWaits +2597, asyncRefusedM1Pending +15, asyncBackoffs +1, asyncQueueFull +2, asyncQueueMax +3 | noise: high (other apps 34.4% of game CPUs, NOT comparable (load or clock)); others on game CPUs 34.4%; clock avg/min 4012/3738 MHz; Daytime correctness, noisy machine OK (no timing read). ts3native 0.36.23 + counters v90 (game-copy/ts3native.asi.0324 (combined integration build, sha256 3F93D74994045DEDE812A9B3BA79A7475BC70CB6B148E22B65053468D6A8FB0D); sha256 in PLAN.md) from a checkout whose nati |
| ip-dedupe-verify | 07:29 (15:29) | 4.8 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2151.9/1856.2/1532.9 -> 2148.4/1860/1532.9 | asyncStarted +1262, asyncRefused +29, asyncCollected +1258, asyncDropped +1, asyncUs +17343655, asyncMaxUs +233644, asyncPumpUs +12554729, asyncPumpWaits +1026, asyncRefusedM1Pending +7, asyncSkipped +1, asyncQueueMax +4 | noise: high (other apps 25.9% of game CPUs); others on game CPUs 25.9%; clock avg/min 4033/3821 MHz; IP play (water and boat replans), daytime, noisy OK. Everything else as bp-dedupe-verify (build, ini, install check, slices, pass and reads). |

### bp-dedupe-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-dedupe-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
