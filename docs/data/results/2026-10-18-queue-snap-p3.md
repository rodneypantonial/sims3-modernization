# Queue snap-p3 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p3-2026-10-18.jsonl`.

## 2026-10-18T12:57 UTC, build fad2ad8, jobs bp-snap-p3

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p3 | 12:57 (20:57) | 13.3 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2242.1/1880.8/1500.9 -> 2486.2/2176.5/1078.1 | asyncStarted +14057, asyncRefused +52, asyncCollected +14053, asyncDropped +1, asyncUs +83225655, asyncMaxUs +233447, asyncPumpUs +50208379, asyncPumpWaits +8852, asyncRefusedM1Pending +46, asyncSkipped +1, asyncBackoffs +7, asyncQueueFull +6, asyncQueueMax +5 | docs/94 §5 P3: [RouteSnap] Enabled=1 Mode=0 snapshot images + audit only, no search (0.26.0, counters v47); env-on settings (async + gate + D), BP Spring, seeded; after: node tools/harness/snap-audit-report.ts <row.nativeDump> --log <row ts3native.log>; pass = no crash, spReady bit 0, every input audited, spMis all 0 (G3), cut cost <= 2 ms/s, image memory <= 64 MB |

## 2026-10-18T13:10 UTC, build f39ffbb, jobs ip-snap-p3

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-snap-p3 | 13:10 (21:10) | 13.2 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2261.4/1877.6/1484.9 -> 2280.3/1966.6/1356.6 | asyncStarted +6063, asyncRefused +24, asyncCollected +6050, asyncDropped +13, asyncUs +71967178, asyncMaxUs +449856, asyncPumpUs +50839953, asyncPumpWaits +4954, asyncRefusedM1Pending +24, asyncBackoffs +1, asyncQueueMax +6 | the same on Isla Paradiso (houseboats, water, high-cost maps, EP portals) |
