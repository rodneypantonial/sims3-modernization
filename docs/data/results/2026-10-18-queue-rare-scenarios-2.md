# Queue rare-scenarios-2 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/rare-scenarios-2-2026-10-18.jsonl`.

## 2026-10-18T19:21 UTC, build 4199206, jobs uni-rare-world-walk

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| uni-rare-world-walk | 19:21 (03:21) | 9.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2150.4/1838.9/1532.9 -> 2259.4/2039.2/1307.3 | asyncStarted +3254, asyncRefused +40, asyncCollected +3253, asyncDropped +1, asyncUs +26801940, asyncMaxUs +164623, asyncPumpUs +11522908, asyncPumpWaits +4207, asyncRefusedM1Pending +40, asyncQueueMax +4 | UNVERIFIED repro, never run (docs/117): rare-world-walk on Baseline-BP after a moving-van trip to University (profile.travel), [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T19:31 UTC, build 4199206, jobs fr-rare-world-walk

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| fr-rare-world-walk | 19:31 (03:31) | 2.8 | **FAIL** | FAIL: profile 6 min, seed 12345 (rng-compare.ts) | 3221226505 | 2155.1/1854/1532.9 -> 2150.9/1859.5/1532.9 | asyncStarted +3, asyncCollected +3, asyncUs +487969, asyncMaxUs +453328, asyncPumpUs +7279, asyncPumpWaits +4, asyncQueueMax +1 | UNVERIFIED repro, never run (docs/117): rare-world-walk on Baseline-BP after a vacation trip to France (profile.travel), [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T19:34 UTC, build 4199206, jobs bp-rare-owned-vehicles

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-owned-vehicles | 19:34 (03:34) | 8.7 | PASS | PASS: profile 7 min, seed 12345 (rng-compare.ts) | 0 | 2137.9/1852.3/1532.9 -> 2247.9/2077.4/1307.4 | asyncStarted +8012, asyncRefused +16, asyncCollected +8011, asyncDropped +1, asyncUs +47472235, asyncMaxUs +145441, asyncPumpUs +29524713, asyncPumpWaits +5036, asyncRefusedM1Pending +15, asyncBackoffs +8, asyncQueueFull +1, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-owned-vehicles on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T19:43 UTC, build e519e07, jobs bp-rare-stroller

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-stroller | 19:43 (03:43) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2128.2/1835.8/1548.8 -> 2348.5/2048.3/1252.5 | asyncStarted +8035, asyncRefused +17, asyncCollected +8034, asyncDropped +1, asyncUs +43473034, asyncMaxUs +150582, asyncPumpUs +23430493, asyncPumpWaits +5324, asyncRefusedM1Pending +12, asyncBackoffs +8, asyncQueueFull +5, asyncQueueMax +4 | UNVERIFIED repro, never run (docs/117): rare-stroller on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T19:51 UTC, build e519e07, jobs bp-rare-horse-ride

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-horse-ride | 19:51 (03:51) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2133.5/1854.4/1516.9 -> 2369.5/2059.7/1236.7 | asyncStarted +6929, asyncRefused +22, asyncCollected +6926, asyncDropped +2, asyncUs +33081670, asyncMaxUs +210697, asyncPumpUs +18577259, asyncPumpWaits +3998, asyncRefusedM1Pending +21, asyncSkipped +1, asyncBackoffs +17, asyncQueueFull +1, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-horse-ride on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T19:59 UTC, build e519e07, jobs bp-rare-zombies

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-zombies | 19:59 (03:59) | 8.8 | PASS | PASS: profile 7 min, seed 12345 (rng-compare.ts) | 0 | 2128.1/1839.1/1532.9 -> 2333.7/2048.6/1236.7 | asyncStarted +5735, asyncRefused +14, asyncCollected +5734, asyncDropped +1, asyncUs +35484928, asyncMaxUs +187725, asyncPumpUs +19011670, asyncPumpWaits +4379, asyncRefusedM1Pending +9, asyncBackoffs +4, asyncQueueFull +5, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-zombies on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T20:08 UTC, build e519e07, jobs bp-rare-flight-broom

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-flight-broom | 20:08 (04:08) | 8.7 | PASS | PASS: profile 7 min, seed 12345 (rng-compare.ts) | 0 | 2129.9/1853.2/1532.9 -> 2225.3/2059.9/1339.3 | asyncStarted +7519, asyncRefused +18, asyncCollected +7518, asyncDropped +1, asyncUs +41363631, asyncMaxUs +170025, asyncPumpUs +24204402, asyncPumpWaits +4866, asyncRefusedM1Pending +13, asyncBackoffs +6, asyncQueueFull +5, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-flight-broom on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T20:17 UTC, build e519e07, jobs sv-rare-horse-ride

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-rare-horse-ride | 20:17 (04:17) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2064.7/1794.9/1580.9 -> 2170.3/1975.8/1403.4 | asyncStarted +2696, asyncRefused +11, asyncCollected +2696, asyncUs +28107132, asyncMaxUs +315041, asyncPumpUs +16881281, asyncPumpWaits +2867, asyncRefusedM1Pending +11, asyncBackoffs +3, asyncQueueMax +6 | UNVERIFIED repro, never run (docs/117): rare-horse-ride on Baseline-SV-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |
