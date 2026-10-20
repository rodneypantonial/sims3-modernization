# Queue snap-p4-long 2026-10-19

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p4-long-2026-10-19.jsonl`.

## 2026-10-19T12:07 UTC, build 4f0bc4e, jobs bp-snap-p4-long, ip-snap-p4-long

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-19T12:18 UTC, build 4f0bc4e, jobs bp-snap-p4-long, ip-snap-p4-long

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4-long | 12:18 (20:18) | 22.2 | PASS | PASS: profile 20 min, seed 12345 (rng-compare.ts) | 0 | 2230.9/2025.4/1402.4 -> 2459.5/2339.9/1056.5 | asyncStarted +20975, asyncRefused +312, asyncCollected +20818, asyncDropped +8, asyncUs +149963707, asyncMaxUs +223588, asyncPumpUs +119805302, asyncPumpWaits +18807, asyncRefusedM1Pending +125, asyncSkipped +148, asyncBackoffs +1233, asyncQueueFull +7, asyncQueueMax +6 | G4 sign-off: snap-p4 with a 20 min profile so one run per world reaches >= 10k compares (BP ~570/min, IP ~250/min: IP may need 2 runs). docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked p |
| ip-snap-p4-long | 12:40 (20:40) | 21.7 | PASS | PASS: profile 20 min, seed 12345 (rng-compare.ts) | 0 | 2211.1/2017.1/1446.5 -> 2286.2/2103.8/1307.2 | asyncStarted +7495, asyncRefused +182, asyncCollected +7154, asyncDropped +19, asyncUs +114391648, asyncMaxUs +331029, asyncPumpUs +108931071, asyncPumpWaits +9642, asyncRefusedM1Pending +69, asyncSkipped +322, asyncBackoffs +1473, asyncQueueMax +6 | G4 sign-off: snap-p4 with a 20 min profile so one run per world reaches >= 10k compares (BP ~570/min, IP ~250/min: IP may need 2 runs). the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP. trip shortcuts off so every trip is routed (more and longer plans for P4 coverage). |

## 2026-10-19T18:40 UTC, build 9a28257, jobs bp-snap-p4-long, ip-snap-p4-long

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4-long | 18:40 (02:40) | 16.2 | PASS | PASS: profile 20 min, seed 12345 (rng-compare.ts) | 0 | 2224.2/2029.8/1405.1 -> 2432.6/2347.6/1025.2 | asyncStarted +13904, asyncRefused +306, asyncCollected +13540, asyncDropped +7, asyncUs +88492817, asyncMaxUs +185532, asyncPumpUs +78457921, asyncPumpWaits +14330, asyncRefusedM1Pending +182, asyncSkipped +357, asyncBackoffs +964, asyncQueueFull +5, asyncQueueMax +6 | G4 sign-off: snap-p4 with a 20 min profile so one run per world reaches >= 10k compares (BP ~570/min, IP ~250/min: IP may need 2 runs). docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked p |
| ip-snap-p4-long | 18:56 (02:56) | 21.7 | PASS | PASS: profile 20 min, seed 12345 (rng-compare.ts) | 0 | 2241.3/2005.5/1421.1 -> 2338/2134.9/1301.3 | asyncStarted +7960, asyncRefused +286, asyncCollected +7478, asyncDropped +10, asyncUs +107709592, asyncMaxUs +462346, asyncPumpUs +110434699, asyncPumpWaits +10153, asyncRefusedM1Pending +129, asyncSkipped +472, asyncBackoffs +1492, asyncQueueFull +8, asyncQueueMax +7 | G4 sign-off: snap-p4 with a 20 min profile so one run per world reaches >= 10k compares (BP ~570/min, IP ~250/min: IP may need 2 runs). the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP. trip shortcuts off so every trip is routed (more and longer plans for P4 coverage). |
