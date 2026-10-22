# Queue unseen-throttle-p2-ab 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/unseen-throttle-p2-ab-2026-10-21.jsonl`.

## 2026-10-21T06:30 UTC, build 6fbe611, jobs bp-ut2-list, bp-ut2-snapback, ip-ut2-boats, bp-ut2-horses, bp-ut2-stuck-off, bp-ut2-stuck-on

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ut2-list | 06:30 (14:30) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2284.3/1917.5/1452.8 -> 2299.1/2126.2/1269 | - | noise: medium (other apps 7.2% of game CPUs); others on game CPUs 7.2%; clock avg/min 4196/4139 MHz; [UnseenThrottle] phase 2 A/B (docs/143 §8.6), part list alone (the other parts not installed), --ab unseenthrottle, off-first (warm-up on, then off,on,on,off; pair with -onfirst), BP Spring 10 min, 2-min slices, seed 12345. Build: ts3native 0.36.15 from this branch (copy native/ts3native/out/ts3nat |
| bp-ut2-snapback | 06:42 (14:42) | 9.7 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2197.5/1832.5/1500.9 -> 2425.1/2152.7/1158.4 | - | noise: medium (other apps 4.7% of game CPUs); others on game CPUs 4.7%; clock avg/min 4229/4177 MHz; [UnseenThrottle] in-game snap-back check (docs/143 §8.9; Rodney: proof that Sims return to full-rate routing when seen). Every part on for the whole run (List, Avoid, Footprint, Rate; modes 1, no --ab), repro unseen-snapback: 6 unseen walking Sims mid-route, the agent moves the in-game camera onto  |
| ip-ut2-boats | 06:52 (14:52) | 10.1 | **CRASH** | FAIL: profile 12 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 07:02:30 (TS3W.exe process gone) | 1 | 2192.5/1848.6/1484.9 -> ? | - | noise: high (other apps 10.0% of game CPUs); others on game CPUs 10.0%; clock avg/min 4179/4139 MHz; [UnseenThrottle] boats / water taxi / swimming check (docs/143 §8.10; Rodney: never a Sim left in open water off a vehicle). Isla Paradiso, every part on for the whole run (List, Avoid, Footprint, Rate; no --ab), repro unseen-boats (10 spawned Sims cross water to the farthest lot, swim into the sea |
| bp-ut2-horses | 07:02 (15:02) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2222.8/1857.2/1484.9 -> 2337/2060.4/1206.2 | - | noise: medium (other apps 9.1% of game CPUs); others on game CPUs 9.1%; clock avg/min 4197/4165 MHz; [UnseenThrottle] ridden-horse check (docs/143 §8.10; Rodney: ridden horses at full rate). BP, every part on (List, Avoid, Footprint, Rate; no --ab), repro unseen-horses (2 Sims mount new horses and ride unseen to the farthest lot and back). 10 min, needs Pets. Correctness, daytime OK. Build as the  |
| bp-ut2-stuck-off | 07:14 (15:14) | 16.8 | PASS | PASS: profile 15 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2224.8/1854.5/1484.9 -> 2339.4/2066.9/1206.4 | - | noise: high (other apps 10.8% of game CPUs); others on game CPUs 10.8%; clock avg/min 4202/4116 MHz; [UnseenThrottle] phase 2 stuck-Sim check, reference arm: every part installed but its switch off for the whole run (utMode / utAvMode / utFpMode / utRateMode 0 is not reachable from the ini, so this arm sets UnseenThrottle.Enabled=0: only the probe), unseencars off (tuning default), BP 15 min with  |
| bp-ut2-stuck-on | 07:31 (15:31) | 17 | PASS | PASS: profile 15 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2220.3/1848.9/1484.9 -> 2432.6/2143.4/1126.4 | - | noise: medium (other apps 5.7% of game CPUs); others on game CPUs 5.7%; clock avg/min 4229/4176 MHz; [UnseenThrottle] phase 2 stuck-Sim check, every part on for the whole run (List, Avoid, Footprint, Rate installed, modes 1 from the start, no --ab; unseencars on for the whole run through profile.fixesOn), BP 15 min, otherwise as bp-ut2-stuck-off. Build: ts3native 0.36.15 from this branch (copy nat |

### bp-ut2-list: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-snapback: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
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

### bp-ut2-stuck-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-stuck-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-21T07:49 UTC, build 8493a79, jobs bp-ut2-snapback, ip-ut2-boats

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ut2-snapback | 07:49 (15:49) | 13.8 | PASS | PASS: profile 12 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2288.5/1917.3/1452.8 -> 2462.8/2181.2/1118.1 | - | noise: medium (other apps 8.7% of game CPUs); others on game CPUs 8.7%; clock avg/min 4196/4111 MHz; [UnseenThrottle] in-game snap-back check (docs/143 §8.9; Rodney: proof that Sims return to full-rate routing when seen). Every part on for the whole run (List, Avoid, Footprint, Rate; modes 1, no --ab), repro unseen-snapback: 6 unseen walking Sims mid-route, the agent moves the in-game camera onto  |
| ip-ut2-boats | 08:03 (16:03) | 13.7 | PASS | PASS: profile 12 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2240.4/1851.2/1468.9 -> 2231/1844.7/1398.2 | - | noise: medium (other apps 4.9% of game CPUs); others on game CPUs 4.9%; clock avg/min 4208/4150 MHz; [UnseenThrottle] boats / water taxi / swimming check (docs/143 §8.10; Rodney: never a Sim left in open water off a vehicle). Isla Paradiso, every part on for the whole run (List, Avoid, Footprint, Rate; no --ab), repro unseen-boats (10 spawned Sims cross water to the farthest lot, swim into the sea |

### bp-ut2-snapback: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 12 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-ut2-boats: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 12 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-21T08:17 UTC, build 02b4324, jobs ip-ut2-boats

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-ut2-boats | 08:17 (16:17) | 13.8 | PASS | PASS: profile 12 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2235.1/1853.6/1452.9 -> 2173/1868/1437.1 | - | noise: medium (other apps 9.0% of game CPUs); others on game CPUs 9.0%; clock avg/min 4209/4162 MHz; [UnseenThrottle] boats / water taxi / swimming check (docs/143 §8.10; Rodney: never a Sim left in open water off a vehicle). Isla Paradiso, every part on for the whole run (List, Avoid, Footprint, Rate; no --ab), repro unseen-boats (10 spawned Sims cross water to the farthest lot, swim into the sea |

### ip-ut2-boats: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 12 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
