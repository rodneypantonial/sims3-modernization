# Queue snap-p5 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p5-2026-10-20.jsonl`.

## 2026-10-20T05:13 UTC, build c1bb4bd, jobs bp-snap-p5, ip-snap-p5

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p5 | 05:13 (13:13) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2218.8/2004/1421.1 -> 2409.3/2279.5/1122.9 | asyncStarted +12423, asyncRefused +182, asyncCollected +12308, asyncDropped +4, asyncUs +107691519, asyncMaxUs +204104, asyncPumpUs +80766265, asyncPumpWaits +13574, asyncRefusedM1Pending +68, asyncSkipped +109, asyncBackoffs +82, asyncQueueFull +3, asyncQueueMax +5 | noise: high (other apps 26.5% of game CPUs); others on game CPUs 26.5%; clock avg/min 4147/4093 MHz; docs/94 §22 / §22.22 P5 step 2 (0.35.19, counters v72): [RouteSnap] Mode=2, the game TAKES the clone run's answer for async ReplanFromPosition jobs (RouteAsync.Kinds 4, RouteSnap.Kinds 8); 1 job in 10 runs the game's plan too and keeps it (VerifyEvery=10, "m2":1 records). No lock lending (ReleaseLo |
| ip-snap-p5 | 05:25 (13:25) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2246.2/2005.4/1425.3 -> 2279.4/2078/1333.4 | asyncStarted +4999, asyncRefused +114, asyncCollected +4867, asyncDropped +6, asyncUs +89122527, asyncMaxUs +339309, asyncPumpUs +76409563, asyncPumpWaits +6535, asyncRefusedM1Pending +39, asyncSkipped +124, asyncBackoffs +54, asyncQueueFull +2, asyncQueueMax +8 | noise: high (other apps 25.7% of game CPUs); others on game CPUs 25.7%; clock avg/min 4149/4121 MHz; the same on Isla Paradiso (houseboats, water, EP portals, more door asks per plan: docs/108 8.78 vs 4.30, so more retakes and a smaller lent share); ShadowWorld IP |
