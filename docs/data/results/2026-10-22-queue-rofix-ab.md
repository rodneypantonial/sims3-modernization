# Queue rofix-ab 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/rofix-ab-2026-10-22.jsonl`.

## 2026-10-22T03:48 UTC, build ed2aee8, jobs bp-avoidro-verify, bp-slotro-verify, bp-deferfp-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-avoidro-verify | 03:48 (11:48) | 7.1 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2247.8/1850.4/1484.9 -> 2316.8/1922.9/1334.4 | - | noise: high (other apps 43.9% of game CPUs); others on game CPUs 43.9%; clock avg/min 3850/3328 MHz; Day job (5 min, noisy machine OK: counters and log lines only, no timing). ts3native 0.36.21 combined build incl. rofix (game-copy/ts3native.asi.0321, sha256 4233eec8...96575): wait keys 0x846D21/0x62F782/0x833593/0x83156D and the other A/B keys keep their probe-P0 slots from the start; AvoidRO pas |
| bp-slotro-verify | 03:55 (11:55) | 7.2 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2223.6/1853/1468.9 -> 2343.6/2058.1/1222.2 | - | noise: high (other apps 45.0% of game CPUs, NOT comparable (load or clock)); others on game CPUs 45.0%; clock avg/min 3817/3149 MHz; Day job (5 min, noisy machine OK: counters and log lines only, no timing). ts3native 0.36.21 combined build incl. rofix (game-copy/ts3native.asi.0321, sha256 4233eec8...96575): wait keys 0x846D21/0x62F782/0x833593/0x83156D and the other A/B keys keep their probe-P0 s |
| bp-deferfp-verify | 04:03 (12:03) | 7.2 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2250.4/1854.8/1460.9 -> 2331.2/1922.8/1358.3 | - | noise: high (other apps 33.8% of game CPUs, NOT comparable (load or clock)); others on game CPUs 33.8%; clock avg/min 3949/3104 MHz; Day job (5 min, noisy machine OK: counters and log lines only, no timing). ts3native 0.36.21 combined build incl. rofix (game-copy/ts3native.asi.0321, sha256 4233eec8...96575): wait keys 0x846D21/0x62F782/0x833593/0x83156D and the other A/B keys keep their probe-P0 s |

### bp-avoidro-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-slotro-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-deferfp-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
