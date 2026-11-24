# Queue master-smoke-m1124d2-ea 2026-11-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/master-smoke-m1124d2-ea-2026-11-24.jsonl`.

Machine: ea-laptop

## 2026-11-24T13:57 UTC, build 3874e731, jobs bp-m1124d2-smoke

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note | machine |
|---|---|---|---|---|---|---|---|---|---|
| bp-m1124d2-smoke | 13:57 (21:57) | 7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->1, owspread_types_run 0->20, stuckcheckasync_queued 0->81, stuckcheckasync_run 0->81, stuckcheck_noplan 0->61, stuckcheck_notstuck 0->5, stuckcheck_rearmed 0->11, stuckcheck_reset 0->4; ts3fixes errortrapspread: stood down (errortrapspread: ErrorTrap not loaded, off)<br>PASS: repro events fired [cut: full row in the jsonl] | 0 | 2041.8/1728.7/1771.3 -> 2102.1/1770.2/1719.1 | - | timing: invalid (noise medium; correctness-only tool ProbeSnap.CallSites); build: asi ef19c1b510a1 ts3native-ea169.asi.eam1124d2 ts3native 0.36.305-eam1124d2 (log ok); ts3fixes tuning b7018ac0bf65 ts3fixes.package.m1124b/ts3fixes.package.eam1124b/ts3fixes.package.m1124c/ts3fixes.package.m1124d/ts3fixes.package.eam1124d2 (expected game-copy/eam1124d2/ts3fixes.package) [cut: full row in the jsonl] | ea-laptop |
