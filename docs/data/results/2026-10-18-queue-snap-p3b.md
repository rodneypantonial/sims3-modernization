# Queue snap-p3b 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p3b-2026-10-18.jsonl`.

## 2026-10-18T14:50 UTC, build 95dc843, jobs bp-snap-p3b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p3b | 14:50 (22:50) | 12.1 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2243.7/1878.6/1446.2 -> 2361.5/2131.2/1209.3 | asyncStarted +11480, asyncRefused +55, asyncCollected +11478, asyncDropped +1, asyncUs +79083949, asyncMaxUs +248465, asyncPumpUs +48483277, asyncPumpWaits +7632, asyncRefusedM1Pending +49, asyncBackoffs +2, asyncQueueFull +6, asyncQueueMax +5 | docs/94 §16 P3-B: [RouteSnap] Enabled=1 Lots=replay, design B replay clone of the lot footprints / quadtrees + lot audit only, no search (0.28.0, counters v49), Prebuild=1 (every lot graph built at load, Rodney 2026-10-18); env-on settings (async + gate + D), BP Spring, seeded; after: node tools/harness/snap-audit-report.ts <row.nativeDump> --log <row ts3native.log>; pass = no crash, rpReady bits  |

## 2026-10-18T20:44 UTC, build 6614c57, jobs bp-snap-p3b, ip-snap-p3b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p3b | 20:44 (04:44) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2269.2/1991.6/1405.3 -> 2383.2/2227.7/1096.8 | asyncStarted +13426, asyncRefused +247, asyncCollected +13426, asyncUs +70740976, asyncMaxUs +203054, asyncPumpUs +42327044, asyncPumpWaits +8668, asyncRefusedM1Pending +32, asyncBackoffs +11, asyncQueueFull +5, asyncQueueMax +6 | docs/94 §16 P3-B: [RouteSnap] Enabled=1 Lots=replay, design B replay clone of the lot footprints / quadtrees + lot audit only, no search (0.28.0, counters v49), Prebuild=1 (every lot graph built at load, Rodney 2026-10-18); env-on settings (async + gate + D), BP Spring, seeded; after: node tools/harness/snap-audit-report.ts <row.nativeDump> --log <row ts3native.log>; pass = no crash, rpReady bits  |
| ip-snap-p3b | 20:56 (04:56) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2267.8/2006.3/1387.1 -> 2224.9/2041.5/1381.8 | asyncStarted +5426, asyncRefused +79, asyncCollected +5413, asyncDropped +13, asyncUs +62813199, asyncMaxUs +323463, asyncPumpUs +44421689, asyncPumpWaits +4582, asyncRefusedM1Pending +22, asyncQueueMax +3 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn) |

## 2026-10-18T21:44 UTC, build 9a6f8d1, jobs bp-snap-p3b, ip-snap-p3b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p3b | 21:44 (05:44) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2235.5/1984.8/1404.4 -> 2261.3/2191.6/1180.6 | asyncStarted +12996, asyncRefused +188, asyncCollected +12990, asyncDropped +2, asyncUs +70975808, asyncMaxUs +240217, asyncPumpUs +43218888, asyncPumpWaits +8539, asyncRefusedM1Pending +37, asyncSkipped +2, asyncBackoffs +18, asyncQueueFull +1, asyncQueueMax +5 | docs/94 §16 P3-B: [RouteSnap] Enabled=1 Lots=replay, design B replay clone of the lot footprints / quadtrees + lot audit only, no search (0.28.0, counters v49), Prebuild=1 (every lot graph built at load, Rodney 2026-10-18); env-on settings (async + gate + D), BP Spring, seeded; after: node tools/harness/snap-audit-report.ts <row.nativeDump> --log <row ts3native.log>; pass = no crash, rpReady bits  |
| ip-snap-p3b | 21:56 (05:56) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2278.9/2006.7/1391.8 -> 2244.6/2064/1359.8 | asyncStarted +4983, asyncRefused +114, asyncCollected +4969, asyncDropped +13, asyncUs +61516705, asyncMaxUs +304061, asyncPumpUs +44000813, asyncPumpWaits +3920, asyncRefusedM1Pending +24, asyncBackoffs +3, asyncQueueFull +4, asyncQueueMax +4 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn) |

## 2026-10-18T22:09 UTC, build 2982894, jobs bp-snap-p3b, ip-snap-p3b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p3b | 22:09 (06:09) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2242.4/2005.3/1375.9 -> 2284/2232.5/1168.1 | asyncStarted +13286, asyncRefused +199, asyncCollected +13284, asyncDropped +1, asyncUs +69932188, asyncMaxUs +252104, asyncPumpUs +41265165, asyncPumpWaits +8669, asyncRefusedM1Pending +39, asyncSkipped +1, asyncBackoffs +24, asyncQueueFull +6, asyncQueueMax +5 | docs/94 §16 P3-B: [RouteSnap] Enabled=1 Lots=replay, design B replay clone of the lot footprints / quadtrees + lot audit only, no search (0.28.0, counters v49), Prebuild=1 (every lot graph built at load, Rodney 2026-10-18); env-on settings (async + gate + D), BP Spring, seeded; after: node tools/harness/snap-audit-report.ts <row.nativeDump> --log <row ts3native.log>; pass = no crash, rpReady bits  |
| ip-snap-p3b | 22:21 (06:21) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2283.5/2009.7/1386.1 -> 2268.4/2051.2/1374.4 | asyncStarted +5356, asyncRefused +110, asyncCollected +5349, asyncDropped +7, asyncUs +64402864, asyncMaxUs +329223, asyncPumpUs +45536257, asyncPumpWaits +4429, asyncRefusedM1Pending +31, asyncBackoffs +9, asyncQueueFull +3, asyncQueueMax +6 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn) |

## 2026-10-18T22:34 UTC, build e444612, jobs bp-snap-p3b, ip-snap-p3b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p3b | 22:35 (06:35) | 12.3 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2283.2/2024.2/1372.1 -> 2450/2286.4/1021.8 | asyncStarted +5041, asyncRefused +83, asyncCollected +5040, asyncUs +27420960, asyncMaxUs +119511, asyncPumpUs +20370398, asyncPumpWaits +2963, asyncRefusedM1Pending +16, asyncBackoffs +60, asyncQueueMax +4 | docs/94 §16 P3-B: [RouteSnap] Enabled=1 Lots=replay, design B replay clone of the lot footprints / quadtrees + lot audit only, no search (0.28.0, counters v49), Prebuild=1 (every lot graph built at load, Rodney 2026-10-18); env-on settings (async + gate + D), BP Spring, seeded; after: node tools/harness/snap-audit-report.ts <row.nativeDump> --log <row ts3native.log>; pass = no crash, rpReady bits  |
| ip-snap-p3b | 22:47 (06:47) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2293.4/2023.5/1372 -> 2254.4/2079.6/1326.9 | asyncStarted +2471, asyncRefused +45, asyncCollected +2468, asyncDropped +3, asyncUs +32611205, asyncMaxUs +394246, asyncPumpUs +45980103, asyncPumpWaits +2058, asyncRefusedM1Pending +8, asyncBackoffs +75, asyncQueueMax +3 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn) |
