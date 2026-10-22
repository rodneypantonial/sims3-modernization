# Queue fbjobcap-ab 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/fbjobcap-ab-2026-10-22.jsonl`.

## 2026-10-22T00:18 UTC, build 9d6392e, jobs sv-fjc-play-offfirst, sv-fjc-play-onfirst, bp-fjc-route-bits

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-fjc-play-offfirst | 00:18 (08:18) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2136.2/1773.9/1580.9 -> 2204.7/1977.2/1421.1 | asyncStarted +7788, asyncRefused +235, asyncCollected +7780, asyncDropped +7, asyncUs +60914497, asyncMaxUs +1432692, asyncPumpUs +37649907, asyncPumpWaits +6717, asyncRefusedM1Pending +42, asyncSkipped +1, asyncBackoffs +1, asyncQueueMax +6 | noise: medium (other apps 5.8% of game CPUs); others on game CPUs 5.8%; clock avg/min 4255/4203 MHz |
| sv-fjc-play-onfirst | 00:30 (08:30) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2138.4/1808.3/1564.9 -> 2147/1989.9/1397.1 | asyncStarted +7629, asyncRefused +209, asyncCollected +7616, asyncDropped +12, asyncUs +72631421, asyncMaxUs +7874581, asyncPumpUs +49261592, asyncPumpWaits +6404, asyncRefusedM1Pending +31, asyncSkipped +1, asyncBackoffs +1, asyncQueueMax +6 | noise: high (other apps 11.5% of game CPUs); others on game CPUs 11.5%; clock avg/min 4186/4112 MHz |
| bp-fjc-route-bits | 00:42 (08:42) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2192/1847.4/1548.9 -> 2247.5/1898.4/1508.9 | asyncStarted +8559, asyncRefused +92, asyncCollected +8557, asyncDropped +2, asyncUs +37293482, asyncMaxUs +1948295, asyncPumpUs +21071343, asyncPumpWaits +4670, asyncRefusedM1Pending +26, asyncBackoffs +5, asyncQueueMax +7 | noise: medium (other apps 8.9% of game CPUs); others on game CPUs 8.9%; clock avg/min 4198/4117 MHz; freeze-fix-ab ON had a 4,638 ms job here (4,664 ms hitch, all locomotion). Correctness + long-job lines; 6 min, cap on for the whole run. |

### sv-fjc-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-fjc-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-fjc-route-bits: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
