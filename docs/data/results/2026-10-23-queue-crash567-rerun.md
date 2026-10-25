# Queue crash567-rerun 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/crash567-rerun-2026-10-23.jsonl`.

## 2026-10-23T01:39 UTC, build bae2e784, jobs bp-crash567-exact, bp-crash567-split

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-crash567-exact | 01:39 (09:39) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2165/1852.6/1524.9 -> 2308.2/2116.7/1251.4 | asyncStarted +12940, asyncRefused +174, asyncCollected +12934, asyncDropped +6, asyncUs +89954391, asyncMaxUs +185846, asyncPumpUs +49281538, asyncPumpWaits +9264, asyncRefusedM1Pending +47, asyncBackoffs +6, asyncQueueMax +5 | noise: medium (other apps 7.0% of game CPUs); others on game CPUs 7.0%; clock avg/min 4227/4176 MHz; Exact rerun of bp-profcost-on-2 (crash 0x567121, EA SequenceController pop on empty list, docs/173): does it repeat? |
| bp-crash567-split | 01:51 (09:51) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2160.3/1836.6/1532.9 -> 2279.7/2083.1/1299.4 | asyncStarted +13743, asyncRefused +181, asyncCollected +13741, asyncDropped +1, asyncUs +96320343, asyncMaxUs +201565, asyncPumpUs +54972067, asyncPumpWaits +10672, asyncRefusedM1Pending +37, asyncBackoffs +11, asyncQueueMax +5 | noise: medium (other apps 7.0% of game CPUs); others on game CPUs 7.0%; clock avg/min 4217/4095 MHz; Split rerun: same as bp-profcost-on-2 with ReplanDedupe and UnseenThrottle off (docs/173 ranked causes). |

### bp-crash567-exact: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-crash567-split: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
