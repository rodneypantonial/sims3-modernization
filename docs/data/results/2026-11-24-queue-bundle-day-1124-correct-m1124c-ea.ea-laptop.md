# Queue bundle-day-1124-correct-m1124c-ea 2026-11-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/bundle-day-1124-correct-m1124c-ea-2026-11-24.jsonl`.

Machine: ea-laptop

## 2026-11-24T12:45 UTC, build 5f133f92, jobs bp-vbowner-correct-1, bp-vbowner-correct-2, bp-vbowner-correct-3, bp-vbowner-correct-4, bp-vbowner-correct-5, bp-vbowner-correct-6, sv-pausegate-unseen-hold-pause, bp-spring-lanea-gate-travel-reload

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note | machine |
|---|---|---|---|---|---|---|---|---|---|
| bp-vbowner-correct-1 | 12:45 (20:45) | 5.8 | **KILLED** | FAIL: profile 5 min, seed 12345 (rng-compare.ts)<br>FAIL: self check: ts3native hook failed, run void (FAIL door memo x: a memo answer differed from the live ask in mode 1: DoorMemoX off for this session) for ~60 s (fail-fast guard); child stopped<br>ts3fixes activity: deadrefsweep_p6_items 0->288, watchdog_second 0->2, deadrefcadence_snapshot_objects 0->11085, deadrefsweep_max_slice_us 0->3578 [cut: full row in the jsonl] | 1 | 2069.4/1731.9/1771.3 -> 2026.1/1728.6/1744.5 | - | timing: invalid (job VOID: FAIL: self check: ts3native hook failed, run void (FAIL door memo x: a memo answer differed from the live ask in mode 1:; noise medium; correctness-only tool ProbeSnap.CallSites); build: asi 7d6a0509369a ts3native-ea169.asi.eam1124c ts3native 0.36.303-eam1124b2 (log ok) [cut: full row in the jsonl] | ea-laptop |

Stopped: bp-vbowner-correct-1 failed in a reuseSession queue (daytime rule; TS3_NIGHT=1 or reuseSession.onFail "restart" goes on with a fresh start).

## 2026-11-24T12:52 UTC, build 5f133f92, jobs sv-pausegate-unseen-hold-pause, bp-spring-lanea-gate-travel-reload

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note | machine |
|---|---|---|---|---|---|---|---|---|---|
| sv-pausegate-unseen-hold-pause | 12:52 (20:52) | 7.1 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: unseenfbfinish_claims 0->174, unseenfbsites_ids 0->10023, unseenfbfinish_rj_end 0->48, unseenfbfinish_max_hold_min 0->120, unseenfbfinish_shortcuts 0->155, unseenfbfinish_cancelled 0->18, unseenfbsites_lists 0->476, unseenfbfinish_no_spot 0->1, unseenfbfinish_rj_route 0->2132, unseenfbfinish_flagged 0->1796 [cut: full row in the jsonl] | 0 | 1996.3/1675.5/1826.8 -> 2050.8/1709.3/1786.4 | - | timing: invalid (noise medium; correctness-only tool ProbeSnap.CallSites); build: asi 7d6a0509369a ts3native-ea169.asi.eam1124c ts3native 0.36.303-eam1124b2 (log ok); ts3fixes tuning b7018ac0bf65 ts3fixes.package.m1124b/ts3fixes.package.eam1124b/ts3fixes.package.m1124c/ts3fixes.package.eam1124c (expected game-copy/eam1124c/ts3fixes.package); noise: medium (other apps 10.6% of game CPUs) [cut: full row in the jsonl] | ea-laptop |
| bp-spring-lanea-gate-travel-reload | 12:59 (20:59) | 35.3 | PASS | PASS: profile 33 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->8, owspread_types_run 0->160, stuckcheckasync_queued 0->509, stuckcheckasync_run 0->508, stuckcheck_noplan 0->102, stuckcheck_notstuck 0->259, stuckcheck_rearmed 0->131, stuckcheck_reset 0->13; ts3fixes errortrapspread: stood down (errortrapspread: ErrorTrap not loaded, off)<br>PASS: repro events fired [cut: full row in the jsonl] | 0 | 2003.1/1695.4/1800.6 -> 2183.4/1839.6/1635.4 | - | timing: invalid (noise medium; correctness-only tool ProbeSnap.CallSites); build: asi 7d6a0509369a ts3native-ea169.asi.eam1124c ts3native 0.36.303-eam1124b2 (log ok); ts3fixes tuning b7018ac0bf65 ts3fixes.package.m1124b/ts3fixes.package.eam1124b/ts3fixes.package.m1124c/ts3fixes.package.eam1124c (expected game-copy/eam1124c/ts3fixes.package); noise: medium (other apps 15.0% of game CPUs) [cut: full row in the jsonl] | ea-laptop |
