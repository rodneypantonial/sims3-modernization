# Queue scriptfix-ab 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/scriptfix-ab-2026-10-22.jsonl`.

## 2026-10-22T04:31 UTC, build 24bccf5, jobs bp-spreads-verify, sv-spreads-verify, sv-unseen-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-spreads-verify | 04:31 (12:31) | 6.9 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2225/1837.6/1500.8 -> 2281.9/2048.1/1324.9 | - | noise: high (other apps 23.8% of game CPUs); others on game CPUs 23.8%; clock avg/min 4056/3546 MHz; CORRECTNESS, daytime OK (noisy machine fine: counters, not timing), 5 min. All four spread switches on for the whole load; repro night-maintenance-clock sets 22:00, so at the measured 11-14 sim-minutes per real second (rng.json of the 2026-10-21 spreads jobs) midnight fires ~10 s and 3 AM ~25 s int |
| sv-spreads-verify | 04:38 (12:38) | 6.9 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2165.5/1803/1500.9 -> 2140.4/1988.5/1340.9 | - | noise: high (other apps 23.1% of game CPUs); others on game CPUs 23.1%; clock avg/min 4124/3974 MHz; CORRECTNESS, daytime OK (noisy machine fine: counters, not timing), 5 min. All four spread switches on for the whole load; repro night-maintenance-clock sets 22:00, so at the measured 11-14 sim-minutes per real second (rng.json of the 2026-10-21 spreads jobs) midnight fires ~10 s and 3 AM ~25 s int |
| sv-unseen-verify | 04:45 (12:45) | 6.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2195.2/1786/1516.9 -> 2144.1/1954.3/1373.1 | - | noise: high (other apps 23.5% of game CPUs); others on game CPUs 23.5%; clock avg/min 4139/4084 MHz; CORRECTNESS, daytime OK, 5 min, SV play (where the >= 300 ms frames rose). unseenenv + unseenmotives + unseenbroadcast on for the whole load. Proves: the throttle acts (env and motive skips > 0) and unseen motive catch-ups stay bounded per sim tick (unseen_catchup_max_tick <= 4 = UnseenMath.kCatchU |

### bp-spreads-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-spreads-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-unseen-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
