# Queue unseen-onfirst-twins 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/unseen-onfirst-twins-2026-10-21.jsonl`.

## 2026-10-21T18:23 UTC, build e1729d1, jobs ip-ut2-noav-1-onfirst, bp-ut2-noav-crowd-onfirst, ip-ut2-avoid-iso-1-onfirst, bp-ut2-avoid-iso-crowd-onfirst, ip-ut2-noav-2-onfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-ut2-noav-1-onfirst | 18:23 (02:23) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2174.8/1851.3/1516.9 -> 2176.9/1864.8/1516.9 | - | noise: medium (other apps 5.3% of game CPUs); others on game CPUs 5.3%; clock avg/min 4238/4154 MHz; Fix verification (docs/143 §8.14): [UnseenThrottle] List+Footprint+Rate on vs all off, Avoid=0 (avoid-iso 2026-10-22: the avoidance skip costs +13-22 ms/s locomotion, +14-26 ms/s lock wait, -5..-21 it/s). Cars off. Pass: no crash; gain = it/s, locomotion, wait-ab total, route on vs off. On-first tw |
| bp-ut2-noav-crowd-onfirst | 18:31 (02:31) | 8.3 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2184.5/1852.1/1532.9 -> 2305.3/1958.1/1423.9 | - | noise: medium (other apps 5.6% of game CPUs); others on game CPUs 5.6%; clock avg/min 4242/4202 MHz; Fix verification (docs/143 §8.14): [UnseenThrottle] List+Footprint+Rate on vs all off, Avoid=0 (avoid-iso 2026-10-22: the avoidance skip costs +13-22 ms/s locomotion, +14-26 ms/s lock wait, -5..-21 it/s). Cars off. Pass: no crash; gain = it/s, locomotion, wait-ab total, route on vs off. On-first tw |
| ip-ut2-avoid-iso-1-onfirst | 18:39 (02:39) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2176.5/1851.9/1516.9 -> 2140.4/1858.1/1516.9 | - | noise: medium (other apps 4.8% of game CPUs); others on game CPUs 4.8%; clock avg/min 4236/4165 MHz; Avoidance isolation (docs/143 §8.14 follow-up): every unseen part on in BOTH halves via the ini (cars off: ts3fixes tuning), only [UnseenThrottle] Avoid switches (--ab unseenavoid). Question: is the throttled-Sim Replan cost (utRpUs_0_0 ON 16-31 ms/s vs off 7-16 in unseen-diag-ab) caused by the avo |
| bp-ut2-avoid-iso-crowd-onfirst | 18:47 (02:47) | 8.2 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2187.9/1841/1532.9 -> 2198.7/1910/1484.9 | - | noise: medium (other apps 5.9% of game CPUs); others on game CPUs 5.9%; clock avg/min 4248/4174 MHz; Avoidance isolation (docs/143 §8.14 follow-up): every unseen part on in BOTH halves via the ini (cars off: ts3fixes tuning), only [UnseenThrottle] Avoid switches (--ab unseenavoid). Question: is the throttled-Sim Replan cost (utRpUs_0_0 ON 16-31 ms/s vs off 7-16 in unseen-diag-ab) caused by the avo |
| ip-ut2-noav-2-onfirst | 18:56 (02:56) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2178.5/1849.6/1516.9 -> 2158.5/1855.3/1516.9 | - | noise: medium (other apps 5.6% of game CPUs); others on game CPUs 5.6%; clock avg/min 4241/4187 MHz; Fix verification (docs/143 §8.14): [UnseenThrottle] List+Footprint+Rate on vs all off, Avoid=0 (avoid-iso 2026-10-22: the avoidance skip costs +13-22 ms/s locomotion, +14-26 ms/s lock wait, -5..-21 it/s). Cars off. Pass: no crash; gain = it/s, locomotion, wait-ab total, route on vs off. On-first tw |

### ip-ut2-noav-1-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-noav-crowd-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-ut2-avoid-iso-1-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-avoid-iso-crowd-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-ut2-noav-2-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
