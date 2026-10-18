# Queue snap-probe 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-probe-2026-10-18.jsonl`.

## 2026-10-18T09:27 UTC, build 0521901, jobs bp-snap-probe, ip-snap-probe

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-probe | 09:27 (17:27) | 5 | **FAIL** | cycle.ts --save exit 1 | - | ? -> ? | - | docs/94 §7: [ProbeSnap] indirect call/jmp targets of the routing closure (0.23.0, counters v43), env-on settings (async + job queue + D), BP Spring, seeded; after: node tools/harness/snap-report.ts <row.nativeLog>; pass = vcReady 1, vcFaults 0, no crash, vcHits / 600 s within the overhead budget |
| ip-snap-probe | 09:32 (17:32) | 13.4 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2310.7/1918.3/1444.9 -> 2336.8/2148.7/1172.8 | asyncStarted +5955, asyncRefused +28, asyncCollected +5942, asyncDropped +13, asyncUs +73668992, asyncMaxUs +407856, asyncPumpUs +51301030, asyncPumpWaits +4904, asyncRefusedM1Pending +26, asyncBackoffs +4, asyncQueueFull +2, asyncQueueMax +5 | the same on Isla Paradiso (houseboats, water, EP objects) |

## 2026-10-18T09:47 UTC, build 7be3d69, jobs bp-snap-probe

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-probe | 09:47 (17:47) | 13.4 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2225/1850.3/1516.9 -> 2500.8/2185.1/1078.1 | asyncStarted +14475, asyncRefused +50, asyncCollected +14473, asyncUs +82482141, asyncMaxUs +196387, asyncPumpUs +48051889, asyncPumpWaits +9138, asyncRefusedM1Pending +49, asyncBackoffs +12, asyncQueueFull +1, asyncQueueMax +5 | docs/94 §7: [ProbeSnap] indirect call/jmp targets of the routing closure (0.23.0, counters v43), env-on settings (async + job queue + D), BP Spring, seeded; after: node tools/harness/snap-report.ts <row.nativeLog>; pass = vcReady 1, vcFaults 0, no crash, vcHits / 600 s within the overhead budget |
