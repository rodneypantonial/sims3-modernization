# Queue hotfix-016-rerun 2026-10-25

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/hotfix-016-rerun-2026-10-25.jsonl`.

## 2026-10-25T01:17 UTC, build 6303d236, jobs ip-hf-matrix-rerun

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-hf-matrix-rerun | 01:17 (09:17) | 8.2 | PASS | control: PASS<br>overwrite: PASS<br>back-to-back: PASS<br>unpaused: PASS<br>== reload: paused: save all off, then all on; load each (two restarts): households match, 60 s at speed 3, game alive; FAIL only on ScriptErrors with ON that OFF does not have<br>reload: PENDING (reload commands above)<br>summary: 4 cases, 4 passed, 0 failed, pending: reload<br>reload on: reload: PENDING (reload commands a | 0 | 2186.1/1855.2/1500.9 -> 2174.1/1855.4/1516.9 | - | noise: high (other apps 15.9% of game CPUs); others on game CPUs 15.9%; clock avg/min 4216/4164 MHz; Gate (b) core save matrix in release mode (save-matrix.ts --fixes shipped --cases control,overwrite,back-to-back,unpaused,reload: save-as to new names, manual save over the same name with backup, two saves back to back, save while the game runs, save then reload). The save path changed (new [UserDi |

### ip-hf-matrix-rerun: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
