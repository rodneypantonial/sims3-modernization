# Queue seeded-rng 2026-10-16

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/seeded-rng-2026-10-16.jsonl`.

## 2026-10-16T11:12 UTC, build c1a6b08, jobs bp-seeded-1, bp-unseeded-1, bp-seeded-2, bp-unseeded-2, bp-seeded-3, bp-unseeded-3, ini-seed-menu-1, ini-seed-menu-2, ini-seed-menu-off

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-seeded-1 | 11:12 (19:12) | 12.5 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2238.9/1852.9/1468.9 -> 2311.8/2137.6/1261.1 | - | seeded RNG step 4: profile 10 min, seed_rng 12345 at window start, story progression off |
