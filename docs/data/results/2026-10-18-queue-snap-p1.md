# Queue snap-p1 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p1-2026-10-18.jsonl`.

## 2026-10-18T12:29 UTC, build 56a3e5f, jobs bp-snap-p1

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p1 | 12:29 (20:29) | 13.3 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2127.6/1838.7/1468.9 -> 2347/2120.9/1212.6 | asyncStarted +14441, asyncRefused +64, asyncCollected +14436, asyncDropped +3, asyncUs +81714535, asyncMaxUs +189140, asyncPumpUs +50065215, asyncPumpWaits +8913, asyncRefusedM1Pending +52, asyncSkipped +1, asyncBackoffs +17, asyncQueueFull +12, asyncQueueMax +4 | docs/94 §5 P1 / §10: [RouteSnap] Count=1 change counters (0.25.0, counters v46), counts only, daytime OK; env-on settings (async + gate + D), BP Spring, seeded; after: node tools/harness/snap-counters-report.ts <row.nativeDump>; pass = no crash, snReady 15, snSkipped 0, 39 IC slots, then the G1 verdict |

## 2026-10-18T12:43 UTC, build de859e8, jobs ip-snap-p1

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-snap-p1 | 12:43 (20:43) | 13.2 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2240.8/1852.3/1484.9 -> 2212.9/1939.9/1356.6 | asyncStarted +6350, asyncRefused +39, asyncCollected +6345, asyncDropped +4, asyncUs +69378971, asyncMaxUs +349476, asyncPumpUs +48429865, asyncPumpWaits +4943, asyncRefusedM1Pending +33, asyncBackoffs +3, asyncQueueFull +6, asyncQueueMax +7 | the same on Isla Paradiso (houseboats, water, EP portals) |
