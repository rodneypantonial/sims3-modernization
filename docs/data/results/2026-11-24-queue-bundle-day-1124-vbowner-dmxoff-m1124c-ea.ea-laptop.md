# Queue bundle-day-1124-vbowner-dmxoff-m1124c-ea 2026-11-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/bundle-day-1124-vbowner-dmxoff-m1124c-ea-2026-11-24.jsonl`.

Machine: ea-laptop

## 2026-11-24T13:35 UTC, build 00b0922b, jobs bp-vbowner-correct-1, bp-vbowner-correct-2, bp-vbowner-correct-3, bp-vbowner-correct-4, bp-vbowner-correct-5, bp-vbowner-correct-6

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note | machine |
|---|---|---|---|---|---|---|---|---|---|
| bp-vbowner-correct-1 | 13:35 (21:35) | 7.1 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: deadrefsweep_p6_items 0->504, watchdog_second 0->8, deadrefcadence_snapshot_objects 0->11070, deadrefsweep_max_slice_us 0->3766, replanspread_replayed 0->367, watchdog_idle_verdicts 0->177, deadrefsweep_p10_ms 0->427, deadrefsweep_p2_ms 0->1, deadrefsweep_objasked 0->14, watchdogasync_refused 0->2 [cut: full row in the jsonl] | 0 | 2048.7/1711.7/1788.4 -> 2072.8/1765.5/1724.5 | - | timing: invalid (noise medium; correctness-only tool ProbeSnap.CallSites); build: asi 7d6a0509369a ts3native-ea169.asi.eam1124c ts3native 0.36.303-eam1124b2 (log ok); ts3fixes tuning b7018ac0bf65 ts3fixes.package.m1124b/ts3fixes.package.eam1124b/ts3fixes.package.m1124c/ts3fixes.package.eam1124c (expected game-copy/eam1124c/ts3fixes.package); fresh load [cut: full row in the jsonl] | ea-laptop |
