# Queue unseen-noavoid-ab 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/unseen-noavoid-ab-2026-10-21.jsonl`.

## 2026-10-21T17:57 UTC, build e1729d1, jobs ip-ut2-noav-1, ip-ut2-noav-2, bp-ut2-noav-crowd

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-ut2-noav-1 | 17:57 (01:57) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2177.7/1856.5/1516.9 -> 2133.8/1861.3/1516.9 | - | noise: medium (other apps 5.3% of game CPUs); others on game CPUs 5.3%; clock avg/min 4231/4172 MHz; Fix verification (docs/143 §8.14): [UnseenThrottle] List+Footprint+Rate on vs all off, Avoid=0 (avoid-iso 2026-10-22: the avoidance skip costs +13-22 ms/s locomotion, +14-26 ms/s lock wait, -5..-21 it/s). Cars off. Pass: no crash; gain = it/s, locomotion, wait-ab total, route on vs off. |
| ip-ut2-noav-2 | 18:05 (02:05) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2174/1850.8/1516.9 -> 2153.6/1857.7/1516.9 | - | noise: medium (other apps 5.6% of game CPUs); others on game CPUs 5.6%; clock avg/min 4249/4186 MHz; Fix verification (docs/143 §8.14): [UnseenThrottle] List+Footprint+Rate on vs all off, Avoid=0 (avoid-iso 2026-10-22: the avoidance skip costs +13-22 ms/s locomotion, +14-26 ms/s lock wait, -5..-21 it/s). Cars off. Pass: no crash; gain = it/s, locomotion, wait-ab total, route on vs off. |
| bp-ut2-noav-crowd | 18:13 (02:13) | 8.2 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2164.2/1835.4/1532.9 -> 2360.7/2095.5/1277.5 | - | noise: medium (other apps 4.5% of game CPUs); others on game CPUs 4.5%; clock avg/min 4230/4164 MHz; Fix verification (docs/143 §8.14): [UnseenThrottle] List+Footprint+Rate on vs all off, Avoid=0 (avoid-iso 2026-10-22: the avoidance skip costs +13-22 ms/s locomotion, +14-26 ms/s lock wait, -5..-21 it/s). Cars off. Pass: no crash; gain = it/s, locomotion, wait-ab total, route on vs off. |

### ip-ut2-noav-1: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-ut2-noav-2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-noav-crowd: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
