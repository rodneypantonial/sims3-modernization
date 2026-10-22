# Queue unseen-houseboat-control 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/unseen-houseboat-control-2026-10-22.jsonl`.

## 2026-10-22T06:43 UTC, build 20510c59, jobs ip-houseboat-move-off, ip-dock-edge-off, ip-houseboat-move-on2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-houseboat-move-off | 06:43 (14:43) | 8.7 | **FAIL** | FAIL: profile 9 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step houseboat_move failed: houseboat_move check: 2 Sim(s) off the deck or in water<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2232.2/1839.2/1476.9 -> 2158.8/1845.4/1492.9 | - | noise: high (other apps 40.7% of game CPUs, NOT comparable (load or clock)); others on game CPUs 40.7%; clock avg/min 3854/3614 MHz; CONTROL (2026-10-22): same repro as ip-houseboat-move with [UnseenThrottle] OFF (all parts). If Sims also end up off the deck/dock or in water here, the game causes it, not the throttle. Compare the houseboat_move check count with ip-houseboat-move (4 and 5 Sims on 2 |
| ip-dock-edge-off | 06:51 (14:51) | 10 | **FAIL** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step houseboat_move failed: houseboat_move check: 5 Sim(s) off the dock or in water<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2242.8/1856.1/1468.9 -> 2138.6/1855/1484.9 | - | noise: high (other apps 34.7% of game CPUs, NOT comparable (load or clock)); others on game CPUs 34.7%; clock avg/min 3971/3652 MHz; CONTROL (2026-10-22): same repro as ip-dock-edge with [UnseenThrottle] OFF (all parts). If Sims also end up off the deck/dock or in water here, the game causes it, not the throttle. Compare the houseboat_move check count with ip-dock-edge (4 and 5 Sims on 2026-10-22) |
| ip-houseboat-move-on2 | 07:01 (15:01) | 8.5 | **FAIL** | FAIL: profile 9 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step houseboat_move failed: houseboat_move check: 3 Sim(s) off the deck or in water<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2232.5/1847.5/1500.6 -> 2149.5/1840.5/1516.6 | - | noise: high (other apps 34.1% of game CPUs); others on game CPUs 34.1%; clock avg/min 3982/3507 MHz; Repeat of ip-houseboat-move with the throttle on, to see whether the count repeats. |

### ip-houseboat-move-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 9 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-dock-edge-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-houseboat-move-on2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 9 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
