# Queue rare-scenarios 2026-10-19

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/rare-scenarios-2026-10-19.jsonl`.

## 2026-10-19T00:31 UTC, build bd794f7, jobs bp-rare-route-bits

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-route-bits | 00:31 (08:31) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2219.2/1855.9/1500.9 -> 2330.6/1932.2/1366.4 | asyncStarted +6007, asyncRefused +20, asyncCollected +6004, asyncDropped +3, asyncUs +33031512, asyncMaxUs +148711, asyncPumpUs +18422887, asyncPumpWaits +3733, asyncRefusedM1Pending +16, asyncBackoffs +3, asyncQueueFull +4, asyncQueueMax +4 | UNVERIFIED repro, never run (docs/117): rare-route-bits on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-19T00:39 UTC, build aa5ec16, jobs sv-rare-route-bits

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-rare-route-bits | 00:39 (08:39) | 7.9 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2179.2/1803.3/1532.9 -> 2145.5/1968.4/1373.1 | asyncStarted +2163, asyncRefused +5, asyncCollected +2163, asyncUs +20430502, asyncMaxUs +334763, asyncPumpUs +11683937, asyncPumpWaits +1870, asyncRefusedM1Pending +5, asyncBackoffs +2, asyncQueueMax +3 | UNVERIFIED repro, never run (docs/117): rare-route-bits on Baseline-SV-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-19T00:47 UTC, build aa5ec16, jobs ip-rare-ip-water

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-rare-ip-water | 00:47 (08:47) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2235.4/1859.5/1484.9 -> 2144.4/1879.6/1469.1 | asyncStarted +2309, asyncRefused +19, asyncCollected +2307, asyncDropped +2, asyncUs +31647493, asyncMaxUs +631905, asyncPumpUs +22124229, asyncPumpWaits +1811, asyncRefusedM1Pending +19, asyncBackoffs +1, asyncQueueMax +4 | UNVERIFIED repro, never run (docs/117): rare-ip-water on Baseline-IP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-19T01:02 UTC, build dab4d23, jobs bp-rare-winter-pond

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-winter-pond | 01:02 (09:02) | 1.8 | **CRASH** | FAIL: profile 6 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step test_env failed: TypeError: fetch failed | 1 | 2237.7/1841.4/1516.9 -> ? | - | UNVERIFIED repro, never run (docs/117): rare-winter-pond on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h; 2026-10-19T01:04:33.000Z  ACCESS_VIOLATION reading from address 0x00000000 (NULL)  at ?  stack: frida-agent.d |

## 2026-10-19T01:24 UTC, build 637a222, jobs bp-rare-winter-pond

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-winter-pond | 01:24 (09:24) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2216.5/1852.7/1500.9 -> 2357.3/2081.6/1285.1 | asyncStarted +6740, asyncRefused +14, asyncCollected +6738, asyncDropped +2, asyncUs +34284602, asyncMaxUs +166719, asyncPumpUs +20184755, asyncPumpWaits +4024, asyncRefusedM1Pending +12, asyncBackoffs +11, asyncQueueFull +2, asyncQueueMax +4 | UNVERIFIED repro, never run (docs/117): rare-winter-pond on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-19T01:32 UTC, build 919713e, jobs bp-rare-winter-pond

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rare-winter-pond | 01:32 (09:32) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2223.9/1856.5/1500.9 -> 2303.6/2044.2/1308.2 | asyncStarted +6959, asyncRefused +9, asyncCollected +6957, asyncDropped +2, asyncUs +34980722, asyncMaxUs +180970, asyncPumpUs +18663634, asyncPumpWaits +4508, asyncRefusedM1Pending +9, asyncBackoffs +3, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-winter-pond on Baseline-BP-Spring, [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |
