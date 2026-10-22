# Queue unseen-diag-ab 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/unseen-diag-ab-2026-10-21.jsonl`.

## 2026-10-21T17:08 UTC, build e1729d1, jobs ip-ut2-diag, bp-ut2-diag-crowd

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-ut2-diag | 17:08 (01:08) | 1.7 | **CRASH** | FAIL: profile 6 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 17:10:01 (TS3W.exe process gone) | 1 | 2178/1855.1/1532.9 -> ? | - | noise: high (other apps 12.3% of game CPUs); others on game CPUs 12.3%; clock avg/min 4205/4115 MHz; Isla Paradiso, every unseen part on vs off (as ip-ut2-all), 6 min. Pass: no crash or freeze, utDgReady 3, utRpN_0_* + utRpN_1_* > 0, class 0 (follower throttled in the last 8 passes) > 0 in on slices and ~0 in off slices after their first 8 passes. Read per docs/143 §8.14 table: class 0 vs class 1  |
| bp-ut2-diag-crowd | 17:10 (01:10) | 1.6 | **CRASH** | FAIL: profile 6 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step spawn_sims failed: InvalidOperationError: script has been destroyed | 1 | 2159.9/1847.7/1516.9 -> undefined/undefined/undefined | - | noise: low; others on game CPUs 2.0%; clock avg/min 4242/4205 MHz; Bridgeport rare-big-crowd (many walking Sims, doors), every unseen part on vs off (as bp-ut2-crowd), 6 min. Pass and read as ip-ut2-diag; the crowd waves start with the window, so the warm-up and the first off slice carry most of the crowd: compare class 0 vs class 1 inside the on slices first, the on/off totals second.; 2026-10-21 |

Stopped: two jobs in a row crashed.

## 2026-10-21T17:13 UTC, build e1729d1, jobs ip-ut2-diag, bp-ut2-diag-crowd

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-ut2-diag | 17:13 (01:13) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2180.1/1852.5/1516.9 -> 2140.9/1856.3/1516.9 | - | noise: medium (other apps 6.6% of game CPUs); others on game CPUs 6.6%; clock avg/min 4232/4150 MHz; Isla Paradiso, every unseen part on vs off (as ip-ut2-all), 6 min. Pass: no crash or freeze, utDgReady 3, utRpN_0_* + utRpN_1_* > 0, class 0 (follower throttled in the last 8 passes) > 0 in on slices and ~0 in off slices after their first 8 passes. Read per docs/143 §8.14 table: class 0 vs class 1  |
| bp-ut2-diag-crowd | 17:21 (01:21) | 8.2 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2165.6/1848.4/1516.9 -> 2281/2091.4/1301.1 | - | noise: medium (other apps 4.9% of game CPUs); others on game CPUs 4.9%; clock avg/min 4235/4150 MHz; Bridgeport rare-big-crowd (many walking Sims, doors), every unseen part on vs off (as bp-ut2-crowd), 6 min. Pass and read as ip-ut2-diag; the crowd waves start with the window, so the warm-up and the first off slice carry most of the crowd: compare class 0 vs class 1 inside the on slices first, the |

### ip-ut2-diag: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-diag-crowd: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
