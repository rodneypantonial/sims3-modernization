# Queue atlas-cawstitched-render4-wn2-1128 2026-11-28

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/atlas-cawstitched-render4-wn2-1128-2026-11-28.jsonl`.

## 2026-11-28T04:27 UTC, build 579e5d2aa, jobs atlas-cawstitched-render4-wn2-1128

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| atlas-cawstitched-render4-wn2-1128 | 04:27 (12:27) | 0 | **FAIL** | FAIL: UNKNOWN: no ts3native.log copy, log rules not checked<br>FAIL: events not fired: log /^ts3native 0\.36\.306-m1126b / 0 lines (want >= 1); log /^OK   ea fix: PortalFlagRefs / 0 lines (want >= 1)<br>PASS: 0 new script errors (max 0)<br>FAIL: UNKNOWN: no Bin/atlas.log, extraAsi log rules not checked (module not loaded?) | - | ? -> ? | - | timing: invalid (job FAIL: FAIL: UNKNOWN: no ts3native.log copy, log rules not checked; FAIL: events not fired: log /^ts3native 0\.36\.306-m1126b /; noise unknown; correctness-only tool ProbeSnap.CallSites); [est 8 min] render check: camera shots over and beyond 2048 m. [est 35 min] [est 20 min] Atlas 4096 m RC world (G-008) with its 461 whole lots (Atlas docs/063 §12): source lot ids, buildings [cut: full row in the jsonl] |

Stopped: atlas-cawstitched-render4-wn2-1128 failed (stopQueueOnFail): ENOENT: no such file or directory, copyfile 'C:\Users\<user>\IdeaProjects\sims-3-modernization\.claude\worktrees\verify-wn2-1128\game-copy\ts3agent.package.lot-target-1128i' -> 'C:\Users\<user>\IdeaProjects\sims-3-modernization\.claude\worktrees\verify-wn2-1128\build\ts3agent.package'.

## 2026-11-28T04:30 UTC, build 579e5d2aa, jobs atlas-cawstitched-render4-wn2-1128

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| atlas-cawstitched-render4-wn2-1128 | 04:30 (12:30) | 6 | PASS | atlas-cawstitched-render4-wn2-1128: PASS: steps done (no watch) (expected: diagnostic: in world, move-in past 2048 m, then five logged plan results; the job passes unless a non-optional step fails or the game dies)<br>PASS: repro events fired<br>PASS: no forbidden log lines<br>PASS: 0 new script errors (max 0)<br>PASS: atlas.log log rules (required present [cut: full row in the jsonl] | 0 | 1384.4/1229.9/1893.3 -> 2381.6/2338.1/1229.8 | - | timing: invalid (noise high; correctness-only tool ProbeSnap.CallSites); build: asi a2ee78e8685b ts3native.asi.m1126b ts3native 0.36.306-m1126b (log ok); ts3fixes tuning 5f21313c91ab ts3fixes.package.m1126b (expected game-copy/ts3fixes.package.m1126b); noise: high (other apps 38.3% of game CPUs, NOT comparable (load or clock)); others on game CPUs 38.3%; clock avg/min 4023/3905 MHz [cut: full row in the jsonl] |
