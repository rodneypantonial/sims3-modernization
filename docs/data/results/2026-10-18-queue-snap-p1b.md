# Queue snap-p1b 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p1b-2026-10-18.jsonl`.

## 2026-10-18T13:57 UTC, build 09ea8d2, jobs bp-snap-p1b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p1b | 13:57 (21:57) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2220.8/1859.7/1500.9 -> 2400.4/2080.2/1190.4 | asyncStarted +12584, asyncRefused +33, asyncCollected +12578, asyncDropped +4, asyncUs +75092154, asyncMaxUs +607515, asyncPumpUs +44790887, asyncPumpWaits +7736, asyncRefusedM1Pending +27, asyncBackoffs +6, asyncQueueFull +6, asyncQueueMax +6 | docs/94 §15 P1b: [RouteSnap] Count=1 change counters with the P1b frames and design B records (0.27.0, counters v48), counts only, daytime OK; env-on settings (async + gate + D), BP Spring, seeded; after: node tools/harness/snap-counters-report.ts <row.nativeDump>; pass = no crash, snReady 15, snSkipped 0, 39 IC slots, then the §11.5 G1 verdict |

## 2026-10-18T14:09 UTC, build dfb8ee8, jobs ip-snap-p1b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-snap-p1b | 14:09 (22:09) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2241.5/1852.9/1484.9 -> 2180.4/1877.2/1484.9 | asyncStarted +4916, asyncRefused +37, asyncCollected +4902, asyncDropped +14, asyncUs +66766889, asyncMaxUs +434956, asyncPumpUs +46676255, asyncPumpWaits +4103, asyncRefusedM1Pending +37, asyncBackoffs +3, asyncQueueMax +5 | the same on Isla Paradiso (houseboats, water, EP portals) |
