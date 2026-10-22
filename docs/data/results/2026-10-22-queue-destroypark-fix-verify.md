# Queue destroypark-fix-verify 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/destroypark-fix-verify-2026-10-22.jsonl`.

## 2026-10-22T02:06 UTC, build aa4fe21, jobs bp-destroypark-fix-offfirst, bp-destroypark-fix-onfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-destroypark-fix-offfirst | 02:06 (10:06) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2169.2/1848.6/1532.9 -> 2259/2095.9/1325.1 | - | noise: medium (other apps 6.7% of game CPUs); others on game CPUs 6.7%; clock avg/min 4247/4139 MHz; Crash I verify (docs/67 §11): Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.19 (game-copy/ts3native.asi.destroypark-fix, sha256 6b1a0cdd3751...) with ts3fixes built and installed from the same commit (node t |
| bp-destroypark-fix-onfirst | 02:18 (10:18) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2180.6/1836.4/1548.9 -> 2280.7/2062.2/1317.1 | - | noise: high (other apps 17.2% of game CPUs); others on game CPUs 17.2%; clock avg/min 4174/4100 MHz; Crash I verify (docs/67 §11): Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.19 (game-copy/ts3native.asi.destroypark-fix, sha256 6b1a0cdd3751...) with ts3fixes built and installed from the same commit (node t |

### bp-destroypark-fix-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-destroypark-fix-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
