# Queue plan-repeat 2026-10-19

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/plan-repeat-2026-10-19.jsonl`.

## 2026-10-19T03:22 UTC, build c8fd528, jobs bp-plan-repeat, ip-plan-repeat

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-plan-repeat | 03:22 (11:22) | 12.3 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2219.6/1848.2/1500.9 -> 2353.9/2071.3/1222.4 | - | docs/23 §18: [PlanRepeat] plan cache probe (0.32.10, counters v60), counts only, daytime OK; only [PlanRepeat] on (it turns on the Route_PlanLocked detour and the [RouteSnap] Count=1 change counters as its world version; no async, gate, P4). After: node tools/harness/plan-repeat-report.ts <row.nativeDump>; pass = no crash, prReady 3; verdict = build the cache only if sim exact repeats save >= 5 ms |
| ip-plan-repeat | 03:34 (11:34) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2231.5/1851.2/1484.9 -> 2164.9/1886/1484.9 | - | the same on Isla Paradiso (houseboats, water, EP portals) |
