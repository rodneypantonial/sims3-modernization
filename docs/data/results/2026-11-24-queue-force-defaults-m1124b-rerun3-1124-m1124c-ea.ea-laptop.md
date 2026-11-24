# Queue force-defaults-m1124b-rerun3-1124-m1124c-ea 2026-11-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/force-defaults-m1124b-rerun3-1124-m1124c-ea-2026-11-24.jsonl`.

Machine: ea-laptop

## 2026-11-24T12:40 UTC, build 5f133f92, jobs lineswitch-force

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note | machine |
|---|---|---|---|---|---|---|---|---|---|
| lineswitch-force | 12:40 (20:40) | 4.7 | **FAIL** | FAIL: profile 5 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step do_interaction: patches until [{"path":"lineswitch_plans","atLeast":1}] not met in 90 s: {"lineswitch_plans":0,"lineswitch_kept":0,"lineswitch_switched":0,"lineswitch_would":0,"lineswitch_errors":0}<br>ts3fixes activity: ts3fixes overwatchchain: not reached (sim 09:48 +785 min, no 03:00) [cut: full row in the jsonl] | 1 | 1990.3/1660.6/1842.6 -> 2027.5/1700/1796.1 | - | timing: invalid (job FAIL: FAIL: profile 5 min, seed 12345 (rng-compare.ts); FAIL: repro step do_interaction: patches until [{"path":"lineswitch_pl; noise medium; correctness-only tool ProbeSnap.CallSites); build: asi 7d6a0509369a ts3native-ea169.asi.eam1124c ts3native 0.36.303-eam1124b2 (log ok) [cut: full row in the jsonl] | ea-laptop |
