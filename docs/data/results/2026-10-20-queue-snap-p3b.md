# Queue snap-p3b 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p3b-2026-10-20.jsonl`.

## 2026-10-20T00:38 UTC, build 58aef41, jobs ip-snap-p3b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-snap-p3b | 00:38 (08:38) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2236/2017.5/1412.7 -> 2232.9/2056.2/1372.6 | asyncStarted +5068, asyncRefused +85, asyncCollected +5059, asyncDropped +9, asyncUs +55572368, asyncMaxUs +350002, asyncPumpUs +42653425, asyncPumpWaits +4266, asyncRefusedM1Pending +22, asyncBackoffs +82, asyncQueueFull +4, asyncQueueMax +7 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn) |

## 2026-10-20T01:31 UTC, build 1039f45, jobs bp-snap-p3b, ip-snap-p3b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p3b | 01:31 (09:31) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2232.3/2019.3/1397.3 -> 2403.6/2273.5/1109.5 | asyncStarted +10725, asyncRefused +150, asyncCollected +10723, asyncDropped +1, asyncUs +51237434, asyncMaxUs +136919, asyncPumpUs +38794928, asyncPumpWaits +6239, asyncRefusedM1Pending +39, asyncSkipped +1, asyncBackoffs +158, asyncQueueFull +2, asyncQueueMax +5 | docs/94 §16 P3-B: [RouteSnap] Enabled=1 Lots=replay, design B replay clone of the lot footprints / quadtrees + lot audit only, no search (0.28.0, counters v49), Prebuild=1 (every lot graph built at load, Rodney 2026-10-18); env-on settings (async + gate + D), BP Spring, seeded; after: node tools/harness/snap-audit-report.ts <row.nativeDump> --log <row ts3native.log>; pass = no crash, rpReady bits  |
| ip-snap-p3b | 01:43 (09:43) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2252.8/2019.5/1381.3 -> 2250.9/2059.5/1362.7 | asyncStarted +4170, asyncRefused +71, asyncCollected +4165, asyncDropped +5, asyncUs +52024481, asyncMaxUs +324634, asyncPumpUs +45521722, asyncPumpWaits +3626, asyncRefusedM1Pending +23, asyncBackoffs +143, asyncQueueMax +7 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn) |
