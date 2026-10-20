# Queue load-profile 2026-10-19

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/load-profile-2026-10-19.jsonl`.

## 2026-10-19T02:10 UTC, build ac57821, jobs bp-load-1, bp-load-2, ip-load-1, ip-load-2, sv-load-1, sv-load-2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-load-1 | 02:10 (10:10) | 2.7 | PASS | PASS: soak 1 min, watchdog quiet | 0 | 2217.9/1852.7/1484.9 -> 2237.9/1848.9/1500.9 | - | docs/39 §10: [LoadProf] alone (0.32.0, counters v59), fresh launch, load Baseline-BP-Spring, 1 min at normal speed, quit. First of two: the second shows the warm file cache. After the queue: node tools/harness/load-profile-report.ts probe-out/queue/load-profile-<date>.jsonl |
| bp-load-2 | 02:13 (10:13) | 2.6 | PASS | PASS: soak 1 min, watchdog quiet | 0 | 2212.5/1849.8/1484.9 -> 2249.1/1865.8/1484.9 | - | repeat of bp-load-1 (warm cache) |
| ip-load-1 | 02:16 (10:16) | 2.6 | PASS | PASS: soak 1 min, watchdog quiet | 0 | 2238/1857.7/1484.9 -> 2205.5/1853.2/1500.9 | - | Isla Paradiso (water, houseboats: the largest G / water grid) |
| ip-load-2 | 02:18 (10:18) | 2.6 | PASS | PASS: soak 1 min, watchdog quiet | 0 | 2235.8/1853.2/1484.9 -> 2203.5/1848.7/1500.9 | - | repeat of ip-load-1 (warm cache) |
| sv-load-1 | 02:21 (10:21) | 2.6 | PASS | PASS: soak 1 min, watchdog quiet | 0 | 2194.5/1800.2/1532.9 -> 2178/1810/1516.9 | - | Sunset Valley |
| sv-load-2 | 02:24 (10:24) | 2.7 | PASS | PASS: soak 1 min, watchdog quiet | 0 | 2186.9/1799.9/1532.9 -> 2194.4/1816.2/1516.9 | - | repeat of sv-load-1 (warm cache) |
