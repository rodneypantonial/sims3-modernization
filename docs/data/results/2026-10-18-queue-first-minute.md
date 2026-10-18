# Queue first-minute 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/first-minute-2026-10-18.jsonl`.

## 2026-10-18T10:26 UTC, build 9a205a6, jobs bp-firstmin-taskstall, ip-firstmin-taskstall

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-firstmin-taskstall | 10:26 (18:26) | 7.2 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2198.6/1915.4/1468.9 -> 2411/2012.8/1318.4 | - | docs/96 cause C (first minute after a load, 16.5% of >=300 ms hitch time, never sampled): fresh load, 5 min seeded, --taskstall 250 records the task and C# stack of every stage stall >= 250 ms from the window start; S3PP SimulatorStageProfiler off (taskstall needs it off). Read the probe-out/<session>-taskstall.jsonl first-minute rows. |
| ip-firstmin-taskstall | 10:33 (18:33) | 7.1 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2235/1845/1436.9 -> 2131.7/1850.6/1452.9 | - | bp-firstmin-taskstall in Isla Paradiso (docs/96 cause C and the unsampled class D cases) |
