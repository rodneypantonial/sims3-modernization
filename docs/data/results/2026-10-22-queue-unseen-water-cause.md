# Queue unseen-water-cause 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/unseen-water-cause-2026-10-22.jsonl`.

## 2026-10-22T05:32 UTC, build 48c803c, jobs ip-water-cause-ab, ip-houseboat-move, ip-dock-edge

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-water-cause-ab | 05:32 (13:32) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2239.6/1836.3/1492.9 -> 2150.3/1863.3/1477.1 | - | noise: high (other apps 21.9% of game CPUs); others on game CPUs 21.9%; clock avg/min 4142/4100 MHz; Isla Paradiso, repro unseen-water-cause (12 spawned Sims cross the sea, swim, return, swim; normal speed), all four throttle parts switched together by --ab (warm-up on, then off,on,on,off; 2-min slices), 10 min. Pass: unseen_water_cause {result, require, ab} ok = native answering, >= 3 entries, no |
| ip-houseboat-move | 05:44 (13:44) | 8.4 | **FAIL** | FAIL: profile 9 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step houseboat_move failed: houseboat_move check: 4 Sim(s) off the deck or in water<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2153.8/1853.4/1494.2 -> 2151.8/1858.7/1494.2 | - | noise: high (other apps 21.4% of game CPUs); others on game CPUs 21.4%; clock avg/min 4136/4108 MHz; Isla Paradiso, repro unseen-houseboat-move (6 spawned Sims walking on a houseboat deck, camera away, the boat autopilots 80 m out and back to port), every part on, no --ab, 9 min. Pass: houseboat_move {check, require} ok (every Sim on the houseboat lot, none in water, boat moved >= 20 m) and unseen |
| ip-dock-edge | 05:53 (13:53) | 9.8 | **FAIL** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step houseboat_move failed: houseboat_move check: 5 Sim(s) off the dock or in water<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2236.4/1858.5/1476.9 -> 2160.1/1878.4/1476.9 | - | noise: high (other apps 17.1% of game CPUs); others on game CPUs 17.1%; clock avg/min 4132/4079 MHz; Isla Paradiso, repro unseen-dock-edge (6 spawned Sims walking along a port gangway's dock while the docked houseboat leaves, returns and leaves again; camera away), every part on, no --ab, 10 min. Pass: houseboat_move {check, require} ok (no dock Sim in water, boat moved >= 20 m) and unseen_water_c |

### ip-water-cause-ab: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-houseboat-move: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 9 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-dock-edge: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
