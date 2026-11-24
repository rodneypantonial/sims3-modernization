# Queue force-defaults-m1124b-rerun2-1124-m1124c-ea 2026-11-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/force-defaults-m1124b-rerun2-1124-m1124c-ea-2026-11-24.jsonl`.

Machine: ea-laptop

## 2026-11-24T11:47 UTC, build daf28333, jobs lineswitch-force, reactoverlay-force

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note | machine |
|---|---|---|---|---|---|---|---|---|---|
| lineswitch-force | 11:47 (19:47) | 4.8 | **FAIL** | FAIL: profile 5 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step reflect_call answer does not match /switched 0; .*kept [1-9]\d*; .*errors 0/: lineswitch: switched 0; off: would 0; kept 1 (household without lineswitchhh 1); plans 3; errors 0; members found=True; last: kept, household without lineswitchhh: household Judy Bunch, UseToilet ToiletCheap, waited 5.5 sim-min [cut: full row in the jsonl] | 1 | 1984.8/1660.7/1842.6 -> 1992.4/1694.5/1805 | - | timing: invalid (job FAIL: FAIL: profile 5 min, seed 12345 (rng-compare.ts); FAIL: repro step reflect_call answer does not match /switched 0; .*kep; noise medium; correctness-only tool ProbeSnap.CallSites); build: asi 7d6a0509369a ts3native-ea169.asi.eam1124c ts3native 0.36.303-eam1124b2 (log ok) [cut: full row in the jsonl] | ea-laptop |
| reactoverlay-force | 11:51 (19:51) | 6.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: ts3fixes overwatchchain: not reached (sim 09:48 +819 min, no 03:00); ts3fixes stuckcheckasync: not reached (easpread_lots_passes rose 0: the 3 AM venue cleanup had not armed the stuck checks yet); ts3fixes errortrapspread: stood down (errortrapspread: ErrorTrap not loaded, off)<br>PASS: repro events fired [cut: full row in the jsonl] | 0 | 2004.1/1667.9/1830.8 -> 2002.2/1687.3/1795.8 | - | timing: invalid (noise medium; correctness-only tool ProbeSnap.CallSites); build: asi 7d6a0509369a ts3native-ea169.asi.eam1124c ts3native 0.36.303-eam1124b2 (log ok); ts3fixes tuning b7018ac0bf65 ts3fixes.package.m1124b/ts3fixes.package.eam1124b/ts3fixes.package.m1124c/ts3fixes.package.eam1124c (expected game-copy/eam1124c/ts3fixes.package); noise: medium (other apps 7.5% of game CPUs) [cut: full row in the jsonl] | ea-laptop |
