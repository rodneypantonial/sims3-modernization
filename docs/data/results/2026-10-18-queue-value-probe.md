# Queue value-probe 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/value-probe-2026-10-18.jsonl`.

## 2026-10-18T10:53 UTC, build 8b30fc9, jobs bp-value-probe, ip-value-probe

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-value-probe | 10:53 (18:53) | 13.5 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2225.1/1850.2/1500.9 -> 2385.2/2169.6/1180.8 | asyncStarted +12090, asyncRefused +42, asyncCollected +12082, asyncDropped +5, asyncUs +86512730, asyncMaxUs +408607, asyncPumpUs +51459912, asyncPumpWaits +8035, asyncRefusedM1Pending +37, asyncSkipped +1, asyncBackoffs +11, asyncQueueFull +5, asyncQueueMax +5 | docs/108: [ProbeValues] routing value probe (0.24.0, counters v44), env-on settings as bp-snap-probe (async + gate + D) plus ProbeValues, BP Spring, seeded; after: node tools/harness/values-report.ts <row.nativeDump> --log <row.nativeLog>; pass = pvReady 3, pvSkipped 0, pvWorlds >= 1, pvFrameFull 0, no crash |
| ip-value-probe | 11:06 (19:06) | 13.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2249.4/1859.8/1436.9 -> 2257.1/1938.3/1324.6 | asyncStarted +5025, asyncRefused +25, asyncCollected +5019, asyncDropped +5, asyncUs +71956338, asyncMaxUs +437720, asyncPumpUs +49022149, asyncPumpWaits +4490, asyncRefusedM1Pending +25, asyncBackoffs +1, asyncQueueMax +6 | the same on Isla Paradiso (water grid, houseboats, sea height, water waypoints) |

## 2026-10-18T15:19 UTC, build d9fade2, jobs bp-value-probe

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-value-probe | 15:19 (23:19) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2216.6/1834/1500.9 -> 2395.6/2118.5/1158.4 | asyncStarted +11720, asyncRefused +35, asyncCollected +11711, asyncDropped +7, asyncUs +80852810, asyncMaxUs +223928, asyncPumpUs +49656260, asyncPumpWaits +7684, asyncRefusedM1Pending +31, asyncSkipped +1, asyncBackoffs +5, asyncQueueFull +4, asyncQueueMax +5 | docs/108: [ProbeValues] routing value probe (0.24.1, counters v45: docs/112 fixes, docs/108 §8), env-on settings as bp-snap-probe (async + gate + D) plus ProbeValues, BP Spring, seeded; after: node tools/harness/values-report.ts <row.nativeDump> --log <row.nativeLog>; pass = pvReady 3, pvSkipped 0, pvWorlds >= 1, pvFrameFull 0, no crash |

## 2026-10-18T15:32 UTC, build db1d7c6, jobs ip-value-probe

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-value-probe | 15:32 (23:32) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2240.5/1857.8/1484.9 -> 2132.7/1871.1/1484.9 | asyncStarted +4518, asyncRefused +29, asyncCollected +4508, asyncDropped +10, asyncUs +68066292, asyncMaxUs +364622, asyncPumpUs +49643607, asyncPumpWaits +3882, asyncRefusedM1Pending +26, asyncBackoffs +3, asyncQueueFull +3, asyncQueueMax +4 | the same on Isla Paradiso (water grid, houseboats, sea height, water waypoints) |
