# Queue snap-p5-shadow 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p5-shadow-2026-10-20.jsonl`.

## 2026-10-20T04:03 UTC, build af0396d, jobs bp-snap-p5-shadow, ip-snap-p5-shadow

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p5-shadow | 04:03 (12:03) | 3.1 | **FAIL** | cycle.ts --save exit 1 | - | ? -> ? | - | noise: high (other apps 35.9% of game CPUs); others on game CPUs 35.9%; clock avg/min 4062/3993 MHz; docs/94 §22.22 P5 step 1 (0.35.19, counters v72): [RouteSnap] Mode=2 with VerifyEvery=1, so EVERY armed ReplanFromPosition job runs the P5 path (clone run with door asks proxied to the sim, then the game's own plan) and KEEPS THE GAME'S ANSWER: nothing is taken, P5b/P5c windows never open (verify j |
| ip-snap-p5-shadow | 04:06 (12:06) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2241.2/2006.2/1422.5 -> 2290/2088.7/1342.1 | asyncStarted +3962, asyncRefused +85, asyncCollected +3857, asyncDropped +4, asyncUs +94391060, asyncMaxUs +580823, asyncPumpUs +82717864, asyncPumpWaits +5390, asyncRefusedM1Pending +36, asyncSkipped +101, asyncBackoffs +53, asyncQueueMax +8 | noise: high (other apps 40.8% of game CPUs); others on game CPUs 40.8%; clock avg/min 4055/3572 MHz; the same on Isla Paradiso (houseboats, water, EP portals, more door asks per plan: docs/108 8.78 vs 4.30); ShadowWorld IP |

## 2026-10-20T04:23 UTC, build 893cadf, jobs bp-snap-p5-shadow

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p5-shadow | 04:23 (12:23) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2221.2/2020.7/1412.5 -> 2421/2301.9/1122.5 | asyncStarted +11195, asyncRefused +130, asyncCollected +11126, asyncDropped +1, asyncUs +111041252, asyncMaxUs +191580, asyncPumpUs +81867790, asyncPumpWaits +11260, asyncRefusedM1Pending +42, asyncSkipped +65, asyncBackoffs +53, asyncQueueFull +4, asyncQueueMax +5 | noise: high (other apps 32.0% of game CPUs); others on game CPUs 32.0%; clock avg/min 4065/3783 MHz; docs/94 §22.22 P5 step 1 (0.35.19, counters v72): [RouteSnap] Mode=2 with VerifyEvery=1, so EVERY armed ReplanFromPosition job runs the P5 path (clone run with door asks proxied to the sim, then the game's own plan) and KEEPS THE GAME'S ANSWER: nothing is taken, P5b/P5c windows never open (verify j |
