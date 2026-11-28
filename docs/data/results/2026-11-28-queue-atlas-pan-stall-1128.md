# Queue atlas-pan-stall-1128 2026-11-28

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/atlas-pan-stall-1128-2026-11-28.jsonl`.

## 2026-11-28T05:26 UTC, build 32c540701, jobs pan-stitched-stall

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| pan-stitched-stall | 05:26 (13:26) | 6.4 | **FAIL** | PASS: profile 4 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: etspread_passes 0->1; ts3fixes stuckcheckasync: not reached (easpread_lots_passes rose 0: the 3 AM venue cleanup had not armed the stuck checks yet)<br>FAIL: self check: ts3fixes overwatchchain on but owchain_alarms+owspread_types_run rose 0 although sim time 00:00+1611 min covered 03:00: fix idle, run void [cut: full row in the jsonl] | 0 | 1459.6/1312.9/1543.4 -> 2204.2/2118.6/1375 | lotAdds +16895, lotAddUs +35275, lotAddObjs +3784586, lotAddMaxObjs +575, pumpCalls +15184, pumpUs +88070, pumpMaxUs +1065, pumpLotPasses +74, pumpMaxLots +3, lotScoreCalls +2917, lotScoreUs +334875, lotScoreMaxUs +2310 | timing: invalid (job VOID: FAIL: self check: ts3fixes overwatchchain on but owchain_alarms+owspread_types_run rose 0 although sim time 00:00+1611 m; noise high; heavy tool taskstall); build: asi 01f6a20d5ab5 ts3native.asi.m1126c ts3native 0.36.307-m1126c (log ok); ts3fixes tuning b6584d9dc058 ts3fixes.package.m1126c (expected game-copy/ts3fixes.package.m1126c) [cut: full row in the jsonl]<br>sim ms/s 374.1; p99 195 ms, p99.9 4755, max 4877, >=300 15, >=1000 15 (0.3 min) |

Stopped: pan-stitched-stall failed (stopQueueOnFail): FAIL: self check: ts3fixes overwatchchain on but owchain_alarms+owspread_types_run rose 0 although sim time 00:00+1611 min covered 03:00: fix idle, run void.

## 2026-11-28T05:40 UTC, build b5be9b66c, jobs pan-stitched-stall

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| pan-stitched-stall | 05:40 (13:40) | 0 | **FAIL** | FAIL: UNKNOWN: no ts3native.log copy, log rules not checked<br>FAIL: events not fired: log /^ts3native 0\.36\.307-m1126c / 0 lines (want >= 1)<br>PASS: 0 new script errors (max 0)<br>FAIL: UNKNOWN: no Bin/atlas.log, extraAsi log rules not checked (module not loaded?) | - | ? -> ? | - | timing: invalid (job FAIL: FAIL: UNKNOWN: no ts3native.log copy, log rules not checked; FAIL: events not fired: log /^ts3native 0\.36\.307-m1126c /; noise unknown; heavy tool taskstall) [cut: full row in the jsonl] |

Stopped: pan-stitched-stall failed (stopQueueOnFail): ENOENT: no such file or directory, copyfile 'C:\Users\<user>\IdeaProjects\sims-3-modernization\.claude\worktrees\atlas-pan-stall2-1128\game-copy\ts3agent.package.pan1128b' -> 'C:\Users\<user>\IdeaProjects\sims-3-modernization\.claude\worktrees\atlas-pan-stall2-1128\build\ts3agent.package'.

## 2026-11-28T05:40 UTC, build b5be9b66c, jobs pan-stitched-stall

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-11-28T05:49 UTC, build b5be9b66c, jobs pan-stitched-stall

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| pan-stitched-stall | 05:49 (13:49) | 6.4 | **FAIL** | PASS: profile 4 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: etspread_passes 0->1; ts3fixes stuckcheckasync: not reached (easpread_lots_passes rose 0: the 3 AM venue cleanup had not armed the stuck checks yet)<br>FAIL: self check: ts3fixes overwatchchain on but owchain_alarms+owspread_types_run rose 0 although sim time 00:00+1411 min covered 03:00: fix idle, run void [cut: full row in the jsonl] | 0 | 1376.3/1228.3/1891.2 -> 2120.9/2036.7/1566.3 | lotAdds +16895, lotAddUs +34773, lotAddObjs +3784609, lotAddMaxObjs +575, pumpCalls +14323, pumpUs +39862, pumpMaxUs +130, pumpLotPasses +69, pumpMaxLots +4, lotScoreCalls +3063, lotScoreUs +447322, lotScoreMaxUs +4795 | timing: invalid (job VOID: FAIL: self check: ts3fixes overwatchchain on but owchain_alarms+owspread_types_run rose 0 although sim time 00:00+1411 m; noise medium; heavy tool taskstall); build: asi 01f6a20d5ab5 ts3native.asi.m1126c ts3native 0.36.307-m1126c (log ok); ts3fixes tuning b6584d9dc058 ts3fixes.package.m1126c (expected game-copy/ts3fixes.package.m1126c) [cut: full row in the jsonl]<br>sim ms/s 78.7; p99 82 ms, p99.9 5668, max 6059, >=300 10, >=1000 9 (0.2 min) |

Stopped: pan-stitched-stall failed (stopQueueOnFail): FAIL: self check: ts3fixes overwatchchain on but owchain_alarms+owspread_types_run rose 0 although sim time 00:00+1411 min covered 03:00: fix idle, run void.

## 2026-11-28T05:56 UTC, build b5be9b66c, jobs pan-sv-stall, pan-stitched-stall

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-11-28T06:05 UTC, build b5be9b66c, jobs pan-sv-stall, pan-stitched-stall

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| pan-sv-stall | 06:05 (14:05) | 5.7 | PASS | PASS: profile 4 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->2, owspread_types_run 0->40, stuckcheckasync_queued 0->195, stuckcheckasync_run 0->195, etspread_passes 0->1, stuckcheck_noplan 0->12, stuckcheck_notstuck 0->137, stuckcheck_rearmed 0->45, stuckcheck_reset 0->1<br>PASS: repro events fired<br>REPORT: ErrorTrap correction lines (not a FAIL [cut: full row in the jsonl] | 0 | 1373.4/1226.3/1893.3 -> 1649.7/1567.8/1801.3 | lotAdds +4354, lotAddUs +12401, lotAddObjs +987081, lotAddMaxObjs +519, lotRemoves +2, lotRemoveUs +204, lotRemoveMaxUs +179, pumpCalls +43448, pumpUs +769187, pumpMaxUs +7974, pumpLotPasses +247, pumpMaxLots +28, lotScoreCalls +1610, lotScoreUs +402303, lotScoreMaxUs +32180, lotScoreChanged +5, lotSetOn +4, lotSetOff +2 | timing: invalid (job VOID: runInvalid: profiler cut short (1 of 240 s); noisy machine (others 17.8% of game CPUs); noise medium; heavy tool taskstall); build: asi 01f6a20d5ab5 ts3native.asi.m1126c ts3native 0.36.307-m1126c (log ok); ts3fixes tuning b6584d9dc058 ts3fixes.package.m1126c (expected game-copy/ts3fixes.package.m1126c); noise: medium (other apps 17.3% of game CPUs) [cut: full row in the jsonl] |
| pan-stitched-stall | 06:11 (14:11) | 6.3 | PASS | PASS: profile 4 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->1, owspread_types_run 0->20, stuckcheckasync_queued 0->28, stuckcheckasync_run 0->28, etspread_passes 0->1, stuckcheck_noplan 0->12, stuckcheck_notstuck 0->9, stuckcheck_rearmed 0->7<br>PASS: repro events fired<br>PASS: 0 new script errors (max 0)<br>PASS: atlas.log log rules (required present [cut: full row in the jsonl] | 0 | 1460.4/1313.2/1543.4 -> 2223.9/2141.7/1388.1 | lotAdds +16895, lotAddUs +34355, lotAddObjs +3784597, lotAddMaxObjs +575, pumpCalls +23169, pumpUs +286589, pumpMaxUs +9703, pumpLotPasses +73, pumpMaxLots +3, lotScoreCalls +3147, lotScoreUs +442031, lotScoreMaxUs +1938 | timing: invalid (job VOID: runInvalid: profiler cut short (1 of 240 s); noise medium; heavy tool taskstall); build: asi 01f6a20d5ab5 ts3native.asi.m1126c ts3native 0.36.307-m1126c (log ok); ts3fixes tuning b6584d9dc058 ts3fixes.package.m1126c (expected game-copy/ts3fixes.package.m1126c); noise: medium (other apps 17.4% of game CPUs); others on game CPUs 17.4%; clock avg/min 4102/4051 MHz [cut: full row in the jsonl] |
