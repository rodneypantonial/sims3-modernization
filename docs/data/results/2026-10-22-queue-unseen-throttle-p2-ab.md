# Queue unseen-throttle-p2-ab 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/unseen-throttle-p2-ab-2026-10-22.jsonl`.

## 2026-10-22T02:33 UTC, build 7c9dc6c, jobs bp-ut2-snapback, ip-ut2-boats, bp-ut2-horses

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ut2-snapback | 02:33 (10:33) | 18.4 | PASS | PASS: profile 16 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2131.2/1849/1516.8 -> 2205.5/1859.8/1430 | - | noise: high (other apps 36.7% of game CPUs); others on game CPUs 36.7%; clock avg/min 4075/3599 MHz; [UnseenThrottle] in-game snap-back check (docs/143 §8.9; Rodney: proof that Sims return to full-rate routing when seen). Every part on for the whole run (List, Avoid, Footprint, Rate; modes 1, no --ab), repro unseen-snapback: 6 unseen walking Sims mid-route, the agent moves the in-game camera onto  |
| ip-ut2-boats | 02:52 (10:52) | 10.4 | **FAIL** | FAIL: profile 14 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step unseen_water_watch failed: unseen_water_watch: 50 stranded sample(s)<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2209.6/1853.1/1468.9 -> 2165.9/1864.4/1468.9 | - | noise: high (other apps 39.0% of game CPUs); others on game CPUs 39.0%; clock avg/min 3938/3268 MHz; [UnseenThrottle] boats / water taxi / swimming check (docs/143 §8.10; Rodney: never a Sim left in open water off a vehicle). Isla Paradiso, every part on for the whole run (List, Avoid, Footprint, Rate; no --ab), repro unseen-boats (10 spawned Sims cross water to the farthest lot, swim into the sea |
| bp-ut2-horses | 03:03 (11:03) | 12.4 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2221/1854.4/1468.9 -> 2306.2/1925.6/1334.4 | - | noise: high (other apps 44.6% of game CPUs, NOT comparable (load or clock)); others on game CPUs 44.6%; clock avg/min 3770/3337 MHz; [UnseenThrottle] ridden-horse check (docs/143 §8.10; Rodney: ridden horses at full rate). BP, every part on (List, Avoid, Footprint, Rate; no --ab), repro unseen-horses (2 Sims mount new horses and ride unseen to the farthest lot and back). 10 min, needs Pets. Correc |

### bp-ut2-snapback: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 16 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-ut2-boats: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 14 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-horses: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
