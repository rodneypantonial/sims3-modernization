# Queue snap-p5b 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p5b-2026-10-20.jsonl`.

## 2026-10-20T05:37 UTC, build 97e5b1e, jobs bp-snap-p5b, ip-snap-p5b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p5b | 05:37 (13:37) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2229.2/2033.2/1386.4 -> 2421.4/2306/1072.1 | asyncStarted +13119, asyncRefused +272, asyncCollected +13003, asyncDropped +4, asyncUs +85259414, asyncMaxUs +271148, asyncPumpUs +62447733, asyncPumpWaits +11126, asyncRefusedM1Pending +70, asyncSkipped +112, asyncBackoffs +120, asyncQueueFull +3, asyncQueueMax +6 | noise: high (other apps 24.5% of game CPUs); others on game CPUs 24.5%; clock avg/min 4161/4120 MHz; docs/94 §22.9-§22.10 / §22.22 P5b step 3 (0.35.19, counters v72): snap-p5.json plus [RouteSnap] ReleaseLock=1 ReleaseGates=1: while a taken job's clone run runs, +0x918 is lent to the sim's steering and its read-only gate leaves (gate-12 F-B/F-C/F-G, gate-0 hits); taken back before the answer, each |
| ip-snap-p5b | 05:49 (13:49) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2241.3/2020/1409.3 -> 2313.9/2063.5/1327.2 | asyncStarted +5571, asyncRefused +160, asyncCollected +5468, asyncDropped +10, asyncUs +75534038, asyncMaxUs +553727, asyncPumpUs +60547006, asyncPumpWaits +5662, asyncRefusedM1Pending +39, asyncSkipped +93, asyncBackoffs +74, asyncQueueFull +9, asyncQueueMax +7 | noise: high (other apps 24.8% of game CPUs); others on game CPUs 24.8%; clock avg/min 4150/4115 MHz; the same on Isla Paradiso (houseboats, water, EP portals, more door asks per plan: docs/108 8.78 vs 4.30, so more retakes and a smaller lent share); ShadowWorld IP |
