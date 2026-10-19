# Queue snap-p4 2026-10-19

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p4-2026-10-19.jsonl`.

## 2026-10-19T00:15 UTC, build f90918f, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 00:16 (08:16) | 5.1 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 00:21:06 (TS3W.exe process gone) | 1 | 2208.5/1998.9/1445.3 -> ? | - | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
