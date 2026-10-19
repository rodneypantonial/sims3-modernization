# Queue snap-p4 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p4-2026-10-18.jsonl`.

## 2026-10-18T21:08 UTC, build ec863fc, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 21:08 (05:08) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2270/1987.8/1405 -> 2475.8/2253.4/1094.4 | asyncStarted +12279, asyncRefused +274, asyncCollected +11943, asyncUs +64618942, asyncMaxUs +274997, asyncPumpUs +37885950, asyncPumpWaits +7574, asyncRefusedM1Pending +124, asyncSkipped +335, asyncBackoffs +7, asyncQueueFull +4, asyncQueueMax +5 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 21:19 (05:19) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2257.1/2003.1/1393.6 -> 2225.9/2042.8/1393.6 | asyncStarted +5425, asyncRefused +163, asyncCollected +5172, asyncDropped +13, asyncUs +58494058, asyncMaxUs +693144, asyncPumpUs +41204266, asyncPumpWaits +4195, asyncRefusedM1Pending +67, asyncSkipped +240, asyncBackoffs +3, asyncQueueFull +9, asyncQueueMax +4 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP |

## 2026-10-18T22:59 UTC, build 50cf8a9, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 22:59 (06:59) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2302.7/2023.5/1367.3 -> 2427.5/2148.9/1168.9 | asyncStarted +3614, asyncRefused +52, asyncCollected +3611, asyncDropped +2, asyncUs +20699555, asyncMaxUs +155782, asyncPumpUs +24097235, asyncPumpWaits +2315, asyncRefusedM1Pending +1, asyncSkipped +1, asyncBackoffs +49, asyncQueueMax +5 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 23:11 (07:11) | 12.2 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2210.7/2027.5/1437.2 -> 2350.2/2072.7/1256 | asyncStarted +2566, asyncRefused +52, asyncCollected +2566, asyncUs +32163378, asyncMaxUs +273037, asyncPumpUs +26908111, asyncPumpWaits +1901, asyncRefusedM1Pending +9, asyncBackoffs +55, asyncQueueFull +4, asyncQueueMax +8 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP |

## 2026-10-18T23:44 UTC, build 3b98d84, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 23:44 (07:44) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2281.9/2005.1/1385 -> 2417.8/2278.7/1050.6 | asyncStarted +8973, asyncRefused +137, asyncCollected +8969, asyncDropped +1, asyncUs +46535377, asyncMaxUs +206529, asyncPumpUs +41214090, asyncPumpWaits +5666, asyncRefusedM1Pending +10, asyncSkipped +1, asyncBackoffs +88, asyncQueueMax +5 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 23:56 (07:56) | 2.6 | **FAIL** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: exit 3221225794 with no verdict line (child stopped from outside or died before its summary); last output: (no output) | 3221225794 | 2207/2012.4/1378.3 -> 2207/2012.4/1378.3 | - | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP |
