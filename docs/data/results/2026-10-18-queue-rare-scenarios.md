# Queue rare-scenarios 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/rare-scenarios-2026-10-18.jsonl`.

## 2026-10-18T16:59 UTC, build 18af736, jobs bp-rare-winter-pond

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-winter-pond | 16:59 (00:59) | 7.9 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2132.6/1834.1/1532.9 -> 2369.9/2045.6/1188.7 | asyncStarted +6330, asyncRefused +15, asyncCollected +6328, asyncDropped +2, asyncUs +32321142, asyncMaxUs +204470, asyncPumpUs +17870729, asyncPumpWaits +4397, asyncRefusedM1Pending +10, asyncBackoffs +4, asyncQueueFull +5, asyncQueueMax +4 | UNVERIFIED repro, never run (docs/117): rare-winter-pond on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T17:07 UTC, build d611276, jobs bp-rare-thunderstorm

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-thunderstorm | 17:07 (01:07) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2132.8/1858.7/1532.9 -> 2354.4/2070.8/1236.7 | asyncStarted +6479, asyncRefused +100, asyncCollected +6471, asyncDropped +8, asyncUs +35546629, asyncMaxUs +203146, asyncPumpUs +20845078, asyncPumpWaits +3900, asyncRefusedM1Pending +98, asyncBackoffs +5, asyncQueueFull +2, asyncQueueMax +4 | UNVERIFIED repro, never run (docs/117): rare-thunderstorm on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T17:15 UTC, build d611276, jobs bp-rare-fullmoon-werewolf

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-fullmoon-werewolf | 17:15 (01:15) | 9.8 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts) | 0 | 2126/1840.1/1548.9 -> 2362.5/2033.4/1252.5 | asyncStarted +6856, asyncRefused +50, asyncCollected +6852, asyncDropped +4, asyncUs +48782196, asyncMaxUs +128684, asyncPumpUs +27773639, asyncPumpWaits +5481, asyncRefusedM1Pending +50, asyncBackoffs +4, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-fullmoon-werewolf on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T17:24 UTC, build d611276, jobs bp-rare-route-bits

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-route-bits | 17:24 (01:24) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2135.2/1856.6/1532.9 -> 2232.7/2056.8/1339.4 | asyncStarted +6034, asyncRefused +15, asyncCollected +6033, asyncUs +31680447, asyncMaxUs +176112, asyncPumpUs +17615294, asyncPumpWaits +3661, asyncRefusedM1Pending +13, asyncBackoffs +8, asyncQueueFull +2, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-route-bits on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T17:32 UTC, build d611276, jobs bp-rare-ghost-walk

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-ghost-walk | 17:32 (01:32) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2128.7/1851/1548.9 -> 2315.2/2057.1/1268.7 | asyncStarted +6140, asyncRefused +22, asyncCollected +6140, asyncUs +33482764, asyncMaxUs +207763, asyncPumpUs +18405552, asyncPumpWaits +3686, asyncRefusedM1Pending +21, asyncBackoffs +10, asyncQueueFull +1, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-ghost-walk on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T17:40 UTC, build d611276, jobs bp-rare-occult-walks

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-occult-walks | 17:40 (01:40) | 9.7 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts) | 0 | 2131.8/1857.5/1516.9 -> 2427.9/2116.6/1188.7 | asyncStarted +8804, asyncRefused +18, asyncCollected +8799, asyncDropped +2, asyncUs +52572679, asyncMaxUs +236996, asyncPumpUs +31038529, asyncPumpWaits +6571, asyncRefusedM1Pending +15, asyncSkipped +3, asyncBackoffs +9, asyncQueueFull +3, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-occult-walks on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T17:50 UTC, build d611276, jobs bp-rare-death-walk

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-death-walk | 17:50 (01:50) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2130.8/1839.4/1548.9 -> 2312.4/1908.3/1380.7 | asyncStarted +4833, asyncRefused +14, asyncCollected +4833, asyncUs +28309553, asyncMaxUs +113295, asyncPumpUs +15699698, asyncPumpWaits +3183, asyncRefusedM1Pending +11, asyncBackoffs +3, asyncQueueFull +3, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-death-walk on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T17:58 UTC, build d611276, jobs bp-rare-big-crowd

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-big-crowd | 17:58 (01:58) | 9.9 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts) | 0 | 2126.2/1831.3/1564.9 -> 2318.1/2024.7/1268.7 | asyncStarted +9545, asyncRefused +108, asyncCollected +9542, asyncUs +52088969, asyncMaxUs +180041, asyncPumpUs +31686374, asyncPumpWaits +5705, asyncRefusedM1Pending +17, asyncSkipped +3, asyncBackoffs +8, asyncQueueFull +91, asyncQueueMax +6 | UNVERIFIED repro, never run (docs/117): rare-big-crowd on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T18:08 UTC, build d611276, jobs ip-rare-ip-water

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-rare-ip-water | 18:08 (02:08) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2100.9/1834.4/1532.9 -> 2173.8/1969/1355.4 | asyncStarted +2538, asyncRefused +14, asyncCollected +2536, asyncDropped +2, asyncUs +29010583, asyncMaxUs +320140, asyncPumpUs +20368688, asyncPumpWaits +1882, asyncRefusedM1Pending +14, asyncQueueMax +3 | UNVERIFIED repro, never run (docs/117): rare-ip-water on Baseline-IP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T18:16 UTC, build d611276, jobs sv-rare-route-bits

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-rare-route-bits | 18:16 (02:16) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2068/1790.3/1580.8 -> 2218.4/1957.7/1316.5 | asyncStarted +2075, asyncRefused +13, asyncCollected +2075, asyncUs +18811758, asyncMaxUs +169385, asyncPumpUs +10775949, asyncPumpWaits +1927, asyncRefusedM1Pending +13, asyncBackoffs +2, asyncQueueMax +4 | UNVERIFIED repro, never run (docs/117): rare-route-bits on Baseline-SV-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T18:24 UTC, build d611276, jobs sv-rare-winter-pond

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-rare-winter-pond | 18:24 (02:24) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2102.1/1788.6/1564.9 -> 2294.6/1968.5/1300.5 | asyncStarted +2353, asyncRefused +10, asyncCollected +2353, asyncUs +25349225, asyncMaxUs +323654, asyncPumpUs +15284607, asyncPumpWaits +2528, asyncRefusedM1Pending +10, asyncBackoffs +1, asyncQueueMax +3 | UNVERIFIED repro, never run (docs/117): rare-winter-pond on Baseline-SV-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T18:32 UTC, build d611276, jobs sv-rare-death-walk

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-rare-death-walk | 18:32 (02:32) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2093.1/1800.5/1564.9 -> 2176.8/1824.2/1428.7 | asyncStarted +2924, asyncRefused +2, asyncCollected +2924, asyncUs +25012315, asyncMaxUs +339124, asyncPumpUs +14415990, asyncPumpWaits +2795, asyncRefusedM1Pending +2, asyncQueueMax +6 | UNVERIFIED repro, never run (docs/117): rare-death-walk on Baseline-SV-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T18:40 UTC, build d611276, jobs sv-rare-big-crowd

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-rare-big-crowd | 18:40 (02:40) | 9.7 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts) | 0 | 2109.6/1789.8/1580.9 -> 2181.8/1958.2/1419.3 | asyncStarted +4609, asyncRefused +117, asyncCollected +4607, asyncDropped +1, asyncUs +41909781, asyncMaxUs +285809, asyncPumpUs +23685331, asyncPumpWaits +4485, asyncRefusedM1Pending +19, asyncSkipped +1, asyncBackoffs +4, asyncQueueFull +98, asyncQueueMax +7 | UNVERIFIED repro, never run (docs/117): rare-big-crowd on Baseline-SV-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T18:50 UTC, build d611276, jobs bp-rare-winter-pond-p1b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-winter-pond-p1b | 18:50 (02:50) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2140.7/1834/1532.9 -> 2371.7/2040.3/1204.7 | asyncStarted +6964, asyncRefused +6, asyncCollected +6964, asyncUs +33124752, asyncMaxUs +169746, asyncPumpUs +18106979, asyncPumpWaits +4257, asyncRefusedM1Pending +6, asyncBackoffs +10, asyncQueueMax +4 | UNVERIFIED repro, never run (docs/117): rare-winter-pond on Baseline-BP-Spring, [RouteSnap] Count=1 writer counters; after: node tools/harness/snap-counters-report.ts <row.nativeDump> |

## 2026-10-18T18:58 UTC, build d611276, jobs bp-rare-death-walk-p1b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-death-walk-p1b | 18:58 (02:58) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2123.9/1835.5/1532.9 -> 2211.5/1881/1499.4 | asyncStarted +4842, asyncRefused +14, asyncCollected +4839, asyncDropped +2, asyncUs +27595194, asyncMaxUs +122273, asyncPumpUs +15406966, asyncPumpWaits +2700, asyncRefusedM1Pending +11, asyncSkipped +1, asyncBackoffs +9, asyncQueueFull +3, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-death-walk on Baseline-BP-Spring, [RouteSnap] Count=1 writer counters; after: node tools/harness/snap-counters-report.ts <row.nativeDump> |

## 2026-10-18T19:06 UTC, build d611276, jobs bp-rare-big-crowd-p1b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-big-crowd-p1b | 19:06 (03:06) | 9.8 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts) | 0 | 2126/1851/1532.9 -> 2238.5/2080.4/1307.4 | asyncStarted +9164, asyncRefused +70, asyncCollected +9162, asyncDropped +1, asyncUs +53928987, asyncMaxUs +194193, asyncPumpUs +32966329, asyncPumpWaits +6584, asyncRefusedM1Pending +14, asyncSkipped +1, asyncBackoffs +10, asyncQueueFull +56, asyncQueueMax +6 | UNVERIFIED repro, never run (docs/117): rare-big-crowd on Baseline-BP-Spring, [RouteSnap] Count=1 writer counters; after: node tools/harness/snap-counters-report.ts <row.nativeDump> |

## 2026-10-18T19:15 UTC, build d611276, jobs bp-rare-travel-university

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-travel-university | 19:16 (03:16) | 4.9 | PASS | travel: PASS<br>summary: 1 cases, 1 passed, 0 failed | 0 | 2140.1/1837.6/1532.9 -> 2185.8/2045.7/1325.1 | - | UNVERIFIED (docs/117 §2.10): travel save case to University (moving van) with [ProbeSnap] pass 2 armed, as snap-probe2 bp-snap2-travel; covers the campus load and the first minutes there; walking round campus after arrival is not driven (save-matrix runs only its own case steps); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --si |
