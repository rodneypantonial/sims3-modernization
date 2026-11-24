# Queue master-smoke-m1124c-ea 2026-11-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/master-smoke-m1124c-ea-2026-11-24.jsonl`.

Machine: ea-laptop

## 2026-11-24T11:10 UTC, build 9663650c, jobs bp-m1124c-smoke

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note | machine |
|---|---|---|---|---|---|---|---|---|---|
| bp-m1124c-smoke | 11:11 (19:11) | 6.9 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->1, owspread_types_run 0->20, stuckcheckasync_queued 0->76, stuckcheckasync_run 0->76, stuckcheck_noplan 0->60, stuckcheck_notstuck 0->8, stuckcheck_rearmed 0->6, stuckcheck_reset 0->2; ts3fixes errortrapspread: stood down (errortrapspread: ErrorTrap not loaded, off)<br>PASS: repro events fired [cut: full row in the jsonl] | 0 | 2045/1728.5/1565.9 -> 2102.3/1788.5/1565.9 | - | timing: invalid (noise medium; correctness-only tool ProbeSnap.CallSites); build: asi 7d6a0509369a ts3native-ea169.asi.eam1124c ts3native 0.36.303-eam1124b2 (log ok); ts3fixes tuning b7018ac0bf65 ts3fixes.package.m1124b/ts3fixes.package.eam1124b/ts3fixes.package.m1124c/ts3fixes.package.eam1124c (expected game-copy/eam1124c/ts3fixes.package); noise: medium (other apps 7.1% of game CPUs) [cut: full row in the jsonl] | ea-laptop |
