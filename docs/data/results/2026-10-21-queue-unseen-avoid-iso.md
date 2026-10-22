# Queue unseen-avoid-iso 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/unseen-avoid-iso-2026-10-21.jsonl`.

## 2026-10-21T17:32 UTC, build e1729d1, jobs ip-ut2-avoid-iso-1, ip-ut2-avoid-iso-2, bp-ut2-avoid-iso-crowd

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-ut2-avoid-iso-1 | 17:32 (01:32) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2217.3/1895.2/1492.9 -> 2197.8/1908.1/1492.9 | - | noise: medium (other apps 6.0% of game CPUs); others on game CPUs 6.0%; clock avg/min 4229/4179 MHz; Avoidance isolation (docs/143 §8.14 follow-up): every unseen part on in BOTH halves via the ini (cars off: ts3fixes tuning), only [UnseenThrottle] Avoid switches (--ab unseenavoid). Question: is the throttled-Sim Replan cost (utRpUs_0_0 ON 16-31 ms/s vs off 7-16 in unseen-diag-ab) caused by the avo |
| ip-ut2-avoid-iso-2 | 17:39 (01:39) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2180.3/1853/1532.9 -> 2147.9/1860.1/1532.9 | - | noise: medium (other apps 5.4% of game CPUs); others on game CPUs 5.4%; clock avg/min 4255/4185 MHz; Avoidance isolation (docs/143 §8.14 follow-up): every unseen part on in BOTH halves via the ini (cars off: ts3fixes tuning), only [UnseenThrottle] Avoid switches (--ab unseenavoid). Question: is the throttled-Sim Replan cost (utRpUs_0_0 ON 16-31 ms/s vs off 7-16 in unseen-diag-ab) caused by the avo |
| bp-ut2-avoid-iso-crowd | 17:47 (01:47) | 8.2 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2169/1859.3/1532.9 -> 2337/2104.7/1269.5 | - | noise: medium (other apps 5.3% of game CPUs); others on game CPUs 5.3%; clock avg/min 4239/4174 MHz; Avoidance isolation (docs/143 §8.14 follow-up): every unseen part on in BOTH halves via the ini (cars off: ts3fixes tuning), only [UnseenThrottle] Avoid switches (--ab unseenavoid). Question: is the throttled-Sim Replan cost (utRpUs_0_0 ON 16-31 ms/s vs off 7-16 in unseen-diag-ab) caused by the avo |

### ip-ut2-avoid-iso-1: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-ut2-avoid-iso-2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-avoid-iso-crowd: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
