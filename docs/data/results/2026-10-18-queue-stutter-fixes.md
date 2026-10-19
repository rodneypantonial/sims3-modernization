# Queue stutter-fixes 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/stutter-fixes-2026-10-18.jsonl`.

## 2026-10-18T16:43 UTC, build 18af736, jobs ip-safespot-ab

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-safespot-ab | 16:43 (00:43) | 2.6 | **FAIL** | FAIL: profile 10 min, seed 12345 (rng-compare.ts) | 3221225794 | 2148.2/1837.7/1468.9 -> 2148.2/1837.7/1468.9 | - | docs/95 5.1: SafeSpot reuses the FindGoodLocation result stored with a verified spot (switch safespotreuse, off by default); 10 min seeded Isla Paradiso, A/B in one load (2-min slices), verify on (1 in 8 also searches); pass = safespotreuse_hits > 0, searches lower on the on halves, mismatches small, no script errors, 0 stuck Sims |
