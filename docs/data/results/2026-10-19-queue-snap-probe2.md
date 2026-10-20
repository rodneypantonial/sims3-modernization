# Queue snap-probe2 2026-10-19

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-probe2-2026-10-19.jsonl`.

## 2026-10-19T00:55 UTC, build dab4d23, jobs bp-snap2-upstairs

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap2-upstairs | 00:55 (08:55) | 6.9 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2239.4/1851.3/1500.9 -> 2202.6/1918.6/1453.1 | asyncStarted +6837, asyncRefused +22, asyncCollected +6834, asyncDropped +2, asyncUs +34450165, asyncMaxUs +145613, asyncPumpUs +21040473, asyncPumpWaits +4089, asyncRefusedM1Pending +21, asyncSkipped +1, asyncBackoffs +5, asyncQueueFull +1, asyncQueueMax +4 | DRAFT (docs/109 §6): plans and a walk to an upstairs object, then Route.Replan on the used route (repro bp-upstairs-route) for the stair/level sites and Route_UnlinkPaths, Route_PlanCommon 0x760F0A |
