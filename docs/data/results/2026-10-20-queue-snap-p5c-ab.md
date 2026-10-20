# Queue snap-p5c-ab 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p5c-ab-2026-10-20.jsonl`.

## 2026-10-20T08:01 UTC, build 78bcb36, jobs bp-snap-p5c-ab, ip-snap-p5c-ab

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p5c-ab | 08:01 (16:01) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2222.7/2022.9/1410.5 -> 2389.9/2277.1/1147.4 | asyncStarted +15223, asyncRefused +237, asyncCollected +15159, asyncDropped +1, asyncUs +95723721, asyncMaxUs +183599, asyncPumpUs +58767278, asyncPumpWaits +12800, asyncRefusedM1Pending +51, asyncSkipped +63, asyncBackoffs +37, asyncQueueFull +8, asyncQueueMax +7 | noise: high (other apps 10.7% of game CPUs); others on game CPUs 10.7%; clock avg/min 4231/4126 MHz; 0.35.25 (docs/94 §22.29): same settings as snap-p5c.json (InsFreeInPlace=1 LotsUnlocked=1 ReplaySigMs=250 LazyKeep=1) on game-copy/ts3native.asi.snapp5-03525; run only after snap-p5c.json passed on 0.35.25. docs/94 §22.22 G5 A/B (CLEAN NIGHT ONLY, 0.35.19, counters v72): snap-p5c.json's settings, - |
| ip-snap-p5c-ab | 08:13 (16:13) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2230.4/2006/1421.2 -> 2317.5/2097.5/1318.4 | asyncStarted +6481, asyncRefused +130, asyncCollected +6467, asyncDropped +10, asyncUs +77951662, asyncMaxUs +436333, asyncPumpUs +54017098, asyncPumpWaits +5913, asyncRefusedM1Pending +24, asyncSkipped +3, asyncBackoffs +6, asyncQueueFull +2, asyncQueueMax +6 | noise: high (other apps 18.5% of game CPUs); others on game CPUs 18.5%; clock avg/min 4173/4109 MHz; the same on Isla Paradiso (houseboats, water, EP portals, more door asks per plan: docs/108 8.78 vs 4.30, so more retakes and a smaller lent share); ShadowWorld IP |

## 2026-10-20T08:25 UTC, build fb9c945, jobs bp-snap-p5c-ab

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p5c-ab | 08:25 (16:25) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2227.6/2018.5/1412.1 -> 2355.1/2278.6/1158.1 | asyncStarted +14931, asyncRefused +264, asyncCollected +14857, asyncDropped +2, asyncUs +91163468, asyncMaxUs +241706, asyncPumpUs +55998774, asyncPumpWaits +12687, asyncRefusedM1Pending +54, asyncSkipped +72, asyncBackoffs +22, asyncQueueFull +7, asyncQueueMax +6 | noise: high (other apps 16.9% of game CPUs); others on game CPUs 16.9%; clock avg/min 4214/4139 MHz; 0.35.25 (docs/94 §22.29): same settings as snap-p5c.json (InsFreeInPlace=1 LotsUnlocked=1 ReplaySigMs=250 LazyKeep=1) on game-copy/ts3native.asi.snapp5-03525; run only after snap-p5c.json passed on 0.35.25. docs/94 §22.22 G5 A/B (CLEAN NIGHT ONLY, 0.35.19, counters v72): snap-p5c.json's settings, - |
