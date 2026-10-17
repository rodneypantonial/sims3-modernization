# Queue ts3fixes-ui 2026-10-16

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/ts3fixes-ui-2026-10-16.jsonl`.

## 2026-10-16T12:01 UTC, build df4dcaa, jobs bp-ts3fixes-ui, ip-ts3fixes-ui

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ts3fixes-ui | 12:01 (20:01) | 14.1 | PASS | PASS: profile 12 min, unseeded (rng-compare.ts) | 0 | 2223.9/1831.6/1500.9 -> 2296.1/2138.5/1245.1 | - | v0.1.2 candidates: ts3fixes UI caches #25 motivebar, #26 buffgrid, #30 namecache (docs/74); A/B in one load, 3-min slices, starts on |
| ip-ts3fixes-ui | 12:15 (20:15) | 14.1 | PASS | PASS: profile 12 min, unseeded (rng-compare.ts) | 0 | 2266.5/1869.8/1476.9 -> 2269.9/1903.8/1390.1 | - | same on Isla Paradiso |
