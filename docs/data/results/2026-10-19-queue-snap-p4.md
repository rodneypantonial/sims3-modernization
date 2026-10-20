# Queue snap-p4 2026-10-19

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p4-2026-10-19.jsonl`.

## 2026-10-19T00:15 UTC, build f90918f, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 00:16 (08:16) | 5.1 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 00:21:06 (TS3W.exe process gone) | 1 | 2208.5/1998.9/1445.3 -> ? | - | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |

## 2026-10-19T01:05 UTC, build dab4d23, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 01:05 (09:05) | 2 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 01:07:18 (TS3W.exe process gone) | 1 | 2274.8/1998.5/1394.4 -> ? | - | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |

Stopped: same job crashed twice.

## 2026-10-19T02:08 UTC, build 2156fb3, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 02:08 (10:08) | 2 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 02:10:10 (TS3W.exe process gone) | 1 | 2300.9/2002/1379.3 -> ? | - | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |

Stopped: same job crashed twice.

## 2026-10-19T02:52 UTC, build f4d226c, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 02:52 (10:52) | 4.1 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 02:55:30 (agent not answering for 60 s (heartbeat frozen)) | 1 | 2275.7/2007/1372.3 -> 2199.5/1992.1/1388.3 | asyncStarted +6, asyncRefused +1, asyncCollected +4, asyncUs +51304, asyncMaxUs +30248, asyncPumpUs +28542, asyncPumpWaits +13, asyncRefusedM1Pending +1, asyncSkipped +1, asyncBackoffs +1, asyncQueueMax +1 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |

Stopped: same job crashed twice.

## 2026-10-19T03:47 UTC, build b1b068f, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 03:47 (11:47) | 12.1 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2291.8/2014.5/1366.8 -> 2459.4/2283.6/1048.4 | asyncStarted +7098, asyncRefused +150, asyncCollected +7026, asyncUs +33382283, asyncMaxUs +146177, asyncPumpUs +66162666, asyncPumpWaits +6913, asyncRefusedM1Pending +41, asyncSkipped +71, asyncBackoffs +716, asyncQueueFull +2, asyncQueueMax +7 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 03:59 (11:59) | 3.1 | **FAIL** | cycle.ts --save exit 1 | - | ? -> ? | - | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP |

## 2026-10-19T04:02 UTC, build 79bc267, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 04:02 (12:02) | 3.7 | **FAIL** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: exit 3221225794 with no verdict line (child stopped from outside or died before its summary); last output: (no output) | 3221225794 | 2196.9/2002.1/1385.2 -> ? | - | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 04:06 (12:06) | 4.1 | **FAIL** | Steam is not running and did not start in 180 s (the Steam game quits without it) | - | ? -> ? | - | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP |

## 2026-10-19T04:56 UTC, build ee6a27c, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 04:56 (12:56) | 4.6 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 05:00:33 (agent not answering for 60 s (heartbeat frozen)) | 1 | 2290.1/2028.9/1362.9 -> 2252.9/2057.7/1362.9 | asyncStarted +143, asyncRefused +6, asyncCollected +141, asyncUs +1348810, asyncMaxUs +76002, asyncPumpUs +11144371, asyncPumpWaits +208, asyncRefusedM1Pending +1, asyncSkipped +1, asyncBackoffs +30, asyncQueueMax +3 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |

Stopped: same job crashed twice.

## 2026-10-19T06:08 UTC, build 1557069, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 06:08 (14:08) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2273/1999.3/1384 -> 2392.9/2090.4/1217.4 | asyncStarted +3090, asyncRefused +50, asyncCollected +3051, asyncDropped +1, asyncUs +12955520, asyncMaxUs +120475, asyncPumpUs +91290005, asyncPumpWaits +1941, asyncRefusedM1Pending +15, asyncSkipped +36, asyncBackoffs +141, asyncQueueFull +1, asyncQueueMax +4 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 06:20 (14:20) | 9.7 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 06:29:11 (agent not answering for 60 s (heartbeat frozen)) | 1 | 2214.7/2002.3/1477.1 -> 2298/2069.2/1360.6 | asyncStarted +1101, asyncRefused +14, asyncCollected +1075, asyncUs +16008395, asyncMaxUs +1107438, asyncPumpUs +63288495, asyncPumpWaits +1192, asyncRefusedM1Pending +5, asyncSkipped +25, asyncBackoffs +163, asyncQueueMax +7 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP; 2026-10-19T02:10:03.000Z  ACCESS_VIOLATION reading from address 0xbf5d9b01; State: MEM_FREE  at 0x009a32c0 in TS3W.exe  stack: TS3W.exe |

## 2026-10-19T07:06 UTC, build eefdde9, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 07:06 (15:06) | 12.5 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2279.1/1997.3/1373.1 -> 2513.9/2185.3/1158.8 | asyncStarted +7116, asyncRefused +173, asyncCollected +7022, asyncDropped +1, asyncUs +45587225, asyncMaxUs +179111, asyncPumpUs +54953578, asyncPumpWaits +6633, asyncRefusedM1Pending +50, asyncSkipped +91, asyncBackoffs +968, asyncQueueFull +4, asyncQueueMax +5 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 07:18 (15:18) | 12.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2292.7/2006.2/1386.3 -> 2298.6/2117.9/1302.8 | asyncStarted +3012, asyncRefused +83, asyncCollected +2917, asyncUs +45109151, asyncMaxUs +372740, asyncPumpUs +55861469, asyncPumpWaits +4208, asyncRefusedM1Pending +29, asyncSkipped +95, asyncBackoffs +982, asyncQueueMax +6 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP |

## 2026-10-19T07:44 UTC, build 186c2e9, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 07:44 (15:44) | 12.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2191.4/2000.8/1462.3 -> 2514.2/2176/1190.8 | asyncStarted +7009, asyncRefused +130, asyncCollected +6942, asyncUs +43634998, asyncMaxUs +243614, asyncPumpUs +50228587, asyncPumpWaits +6611, asyncRefusedM1Pending +36, asyncSkipped +67, asyncBackoffs +897, asyncQueueFull +4, asyncQueueMax +7 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 07:57 (15:57) | 12.4 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2219.4/2023.1/1421.3 -> 2309.3/2109.7/1313.1 | asyncStarted +3427, asyncRefused +100, asyncCollected +3322, asyncDropped +3, asyncUs +46434265, asyncMaxUs +655882, asyncPumpUs +56081680, asyncPumpWaits +4779, asyncRefusedM1Pending +33, asyncSkipped +101, asyncBackoffs +1122, asyncQueueFull +2, asyncQueueMax +4 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP |

## 2026-10-19T09:40 UTC, build da9c237, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 09:40 (17:40) | 12.1 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2303.5/2012.9/1380.6 -> 2484.9/2325.7/1020.3 | asyncStarted +8728, asyncRefused +163, asyncCollected +8620, asyncUs +48014038, asyncMaxUs +240167, asyncPumpUs +55502683, asyncPumpWaits +7907, asyncRefusedM1Pending +45, asyncSkipped +103, asyncBackoffs +1274, asyncQueueFull +5, asyncQueueMax +5 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 09:52 (17:52) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2295.7/1992.2/1379.9 -> 2317/2118.9/1300.3 | asyncStarted +3454, asyncRefused +65, asyncCollected +3364, asyncDropped +5, asyncUs +45435653, asyncMaxUs +296114, asyncPumpUs +51459850, asyncPumpWaits +4639, asyncRefusedM1Pending +15, asyncSkipped +85, asyncBackoffs +961, asyncQueueMax +6 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP |

## 2026-10-19T10:05 UTC, build 7fe7939, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 10:05 (18:05) | 12.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2194.2/2002/1445.3 -> 2520/2219.9/1172 | asyncStarted +6655, asyncRefused +146, asyncCollected +6565, asyncUs +45684461, asyncMaxUs +263556, asyncPumpUs +53371225, asyncPumpWaits +5951, asyncRefusedM1Pending +36, asyncSkipped +90, asyncBackoffs +857, asyncQueueFull +6, asyncQueueMax +5 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 10:18 (18:18) | 12.2 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2293.2/1996.2/1357.3 -> 2272.1/2059.3/1357.3 | asyncStarted +3447, asyncRefused +76, asyncCollected +3345, asyncDropped +2, asyncUs +43875562, asyncMaxUs +357035, asyncPumpUs +53777554, asyncPumpWaits +4774, asyncRefusedM1Pending +21, asyncSkipped +100, asyncBackoffs +1086, asyncQueueFull +2, asyncQueueMax +6 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP |

## 2026-10-19T10:56 UTC, build 6f0b810, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 10:56 (18:56) | 12.2 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2275.7/1998.6/1395.4 -> 2493.5/2160.7/1158.4 | asyncStarted +8361, asyncRefused +178, asyncCollected +8250, asyncUs +46681408, asyncMaxUs +208034, asyncPumpUs +53991490, asyncPumpWaits +8203, asyncRefusedM1Pending +47, asyncSkipped +110, asyncBackoffs +1196, asyncQueueFull +5, asyncQueueMax +6 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 11:08 (19:08) | 2.3 | **FAIL** | cycle.ts --save exit 1 | - | ? -> ? | - | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP |

## 2026-10-19T11:11 UTC, build 4ab651f, jobs bp-snap-p4, ip-snap-p4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4 | 11:11 (19:11) | 12.2 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2281.9/2005.7/1385.2 -> 2458.9/2281/1034.6 | asyncStarted +8363, asyncRefused +185, asyncCollected +8241, asyncUs +50961790, asyncMaxUs +308148, asyncPumpUs +50757888, asyncPumpWaits +8371, asyncRefusedM1Pending +58, asyncSkipped +122, asyncBackoffs +1108, asyncQueueFull +4, asyncQueueMax +5 | docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap] Enabled=1 Lots=replay Prebuild=1 Mode=1 (every async plan is planned again on the replay clone th |
| ip-snap-p4 | 11:23 (19:23) | 12.2 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2301.5/2020.6/1372 -> 2339.9/2131.1/1324.3 | asyncStarted +3721, asyncRefused +102, asyncCollected +3591, asyncDropped +4, asyncUs +43162767, asyncMaxUs +273188, asyncPumpUs +45849329, asyncPumpWaits +4765, asyncRefusedM1Pending +27, asyncSkipped +126, asyncBackoffs +1046, asyncQueueFull +8, asyncQueueMax +6 | the same on Isla Paradiso (houseboats, water, EP portals, more footprint churn); ShadowWorld IP |
