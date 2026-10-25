# Queue release-check 2026-10-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/release-check-2026-10-24.jsonl`.

## 2026-10-24T00:47 UTC, build 85cf1978, jobs bp-rc-smoke, ip-rc-smoke, sv-rc-smoke, bp-rel-matrix, bp-rel-travel-france, rc-on-savewrite, rc-on-routeasync, rc-on-routegate, rc-on-pet-guard-loop, rc-on-refcountguard, rc-on-stuckcheckasync, rc-on-overwatchchain, rc-on-errortrapspread, rc-on-ts3fixes-v014, rc-ab-ip-refcount-ring-loop, rc-ab-pet-guard-loop, rc-ab-hitch-stuckcheck-loop, rc-ab-bp-overwatch-ab-loop2, rc-ab-hitch-errortrap-loop, bp-rel-rare-winter-pond, ip-rel-rare-ip-water, bp-rc-soak

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rc-smoke | 00:47 (08:47) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2190.3/1847.1/1484.9 -> 2382.9/2159.7/1163.8 | asyncStarted +6232, asyncRefused +87, asyncCollected +6228, asyncDropped +1, asyncUs +34307295, asyncMaxUs +246672, asyncPumpUs +20552305, asyncPumpWaits +9235, asyncRefusedM1Pending +8, asyncSkipped +3, asyncBackoffs +17, asyncQueueMax +6 | noise: medium (other apps 5.1% of game CPUs); others on game CPUs 5.1%; clock avg/min 4245/4159 MHz; Release check part a (smoke): 6 min play, release ini + measurement logging. Pass: 0 crashes, 0 watchdog lines, ts3native version as docs/RELEASE.md, asyncStarted > 0, every tripwire of docs/RELEASE.md 0, route fails per minute not above v0.1.4 (2026-10-20 release-validate *-rel-play) by > 10%. Rea |
| ip-rc-smoke | 00:54 (08:54) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2186.9/1851.2/1468.9 -> 2224.5/2045.8/1291.3 | asyncStarted +2729, asyncRefused +55, asyncCollected +2728, asyncDropped +1, asyncUs +37624326, asyncMaxUs +308533, asyncPumpUs +27511218, asyncPumpWaits +5848, asyncRefusedM1Pending +10, asyncBackoffs +2, asyncQueueMax +6 | noise: medium (other apps 4.8% of game CPUs); others on game CPUs 4.8%; clock avg/min 4239/4184 MHz; Release check part a (smoke): 6 min play, release ini + measurement logging. Pass: 0 crashes, 0 watchdog lines, ts3native version as docs/RELEASE.md, asyncStarted > 0, every tripwire of docs/RELEASE.md 0, route fails per minute not above v0.1.4 (2026-10-20 release-validate *-rel-play) by > 10%. Rea |
| sv-rc-smoke | 01:02 (09:02) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2152.4/1796.8/1548.9 -> 2267.5/1982.3/1339.8 | asyncStarted +2322, asyncRefused +91, asyncCollected +2322, asyncUs +23746183, asyncMaxUs +184772, asyncPumpUs +15568147, asyncPumpWaits +4889, asyncRefusedM1Pending +4, asyncQueueMax +5 | noise: medium (other apps 5.0% of game CPUs); others on game CPUs 5.0%; clock avg/min 4245/4156 MHz; Release check part a (smoke): 6 min play, release ini + measurement logging. Pass: 0 crashes, 0 watchdog lines, ts3native version as docs/RELEASE.md, asyncStarted > 0, every tripwire of docs/RELEASE.md 0, route fails per minute not above v0.1.4 (2026-10-20 release-validate *-rel-play) by > 10%. Rea |
| bp-rel-matrix | 01:10 (09:10) | 10 | PASS | overwrite: PASS<br>back-to-back: PASS<br>unpaused: PASS<br>build-mode: PASS<br>verify-counters: PASS<br>== reload: paused: save all off, then all on; load each (two restarts): households match, 60 s at speed 3, game alive; FAIL only on ScriptErrors with ON that OFF does not have<br>reload: PENDING (reload commands above)<br>summary: 8 cases, 8 passed, 0 failed, pending: reload<br>reload on: reload | 0 | 2165.5/1848.6/1500.9 -> 2137.4/1877.5/1500.9 | - | noise: medium (other apps 6.7% of game CPUs); others on game CPUs 6.7%; clock avg/min 4201/4133 MHz; Release v0.2.0 validation (ts3native release build parked as game-copy/ts3native.asi.rel020, sha256 in docs/RELEASE.md v0.2.0; release ini = the repo ini after node tools/release-features.ts <chosen> --write; no job ini overrides). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first  |
| bp-rel-travel-france | 01:20 (09:20) | 4.3 | PASS | travel: PASS<br>summary: 1 cases, 1 passed, 0 failed<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2161.4/1850.3/1500.9 -> 2199.3/1878.3/1500.9 | - | noise: medium (other apps 4.6% of game CPUs); others on game CPUs 4.6%; clock avg/min 4191/4163 MHz; Release v0.2.0 validation (ts3native release build parked as game-copy/ts3native.asi.rel020, sha256 in docs/RELEASE.md v0.2.0; release ini = the repo ini after node tools/release-features.ts <chosen> --write; no job ini overrides). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first  |
| rc-on-savewrite | 01:24 (09:24) | 5.6 | PASS | PASS: profile stopped at 4 events after 2.5 min (off 0, on 4; 4 firings, 4 cycles; rule 4 events), cap 18 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2197.5/1852.7/1516.8 -> 2188.3/1889.3/1467.4 | asyncStarted +371, asyncRefused +16, asyncCollected +371, asyncUs +5979801, asyncMaxUs +98532, asyncPumpUs +2089756, asyncPumpWaits +205, asyncRefusedM1Pending +3, asyncQueueMax +2 | noise: medium (other apps 6.1% of game CPUs); others on game CPUs 6.1%; clock avg/min 4200/4138 MHz; Release check part c: savewrite ([SaveWrite] save write path (v0.1.4)) on only, forced repro save-write-loop until 4 firings. Pass: no crash, the stop line, counters rose, saveCount>=4. Read: hitch lines in the window (hitch-ab.ts --scenario <tag>) should name the fix's path when they occur, not th |
| rc-on-routeasync | 01:29 (09:29) | 5.4 | PASS | PASS: profile stopped at 4 events after 2.4 min (off 0, on 4; 4 firings, 4 cycles; rule 4 events), cap 7 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2161.9/1853.3/1484.9 -> 2222.2/1923.3/1403.6 | asyncStarted +4238, asyncRefused +190, asyncCollected +4238, asyncUs +27696110, asyncMaxUs +138376, asyncPumpUs +17457696, asyncPumpWaits +2951, asyncRefusedM1Pending +9, asyncBackoffs +5, asyncQueueFull +136, asyncQueueMax +5 | noise: medium (other apps 6.1% of game CPUs); others on game CPUs 6.1%; clock avg/min 4199/4155 MHz; Release check part c: routeasync ([RouteAsync] background route plans (v0.1.4)) on only, forced repro route-queuefull-replan-loop until 4 firings. Pass: no crash, the stop line, counters rose, asyncStarted>=20. Read: hitch lines in the window (hitch-ab.ts --scenario <tag>) should name the fix's pat |
| rc-on-routegate | 01:35 (09:35) | 8.2 | PASS | PASS: profile stopped at 4 events after 5.2 min (off 0, on 4; 4 firings, 4 cycles; rule 4 events), cap 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2193.7/1853.9/1500.9 -> 2331.3/2099/1243.8 | asyncStarted +8260, asyncRefused +383, asyncCollected +8259, asyncUs +52959543, asyncMaxUs +852461, asyncPumpUs +32432015, asyncPumpWaits +6488, asyncRefusedM1Pending +18, asyncSkipped +1, asyncBackoffs +7, asyncQueueFull +219, asyncQueueMax +8 | noise: medium (other apps 5.3% of game CPUs); others on game CPUs 5.3%; clock avg/min 4206/4155 MHz; Release check part c: routegate ([RouteGate] gates while an async job runs (v0.1.4)) on only, forced repro route-gate-crowd-loop until 4 firings. Pass: no crash, the stop line, counters rose, asyncStarted>=20,rgEntries>=1. Read: hitch lines in the window (hitch-ab.ts --scenario <tag>) should name t |
| rc-on-pet-guard-loop | 01:43 (09:43) | 7.2 | PASS | PASS: profile stopped at 4 events after 4.0 min (off 0, on 4; 4 firings, 4 cycles; rule 4 events), cap 9 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2162.5/1831.4/1532.9 -> 2327.6/1959.7/1417.8 | asyncStarted +8019, asyncRefused +181, asyncCollected +8015, asyncDropped +2, asyncUs +48170842, asyncMaxUs +203660, asyncPumpUs +29089249, asyncPumpWaits +5855, asyncRefusedM1Pending +24, asyncBackoffs +6, asyncQueueFull +32, asyncQueueMax +7 | noise: medium (other apps 4.9% of game CPUs); others on game CPUs 4.9%; clock avg/min 4203/4142 MHz; Release check part c: discourage ([Discourage] footprints/destroy apply/dead sweep (v0.1.4)), deferfootprint ([Discourage] DeferFootprint (docs/89 design U)) on only, forced repro pet-guard-loop until 4 firings. Pass: no crash, the stop line, counters rose, discApplied>=1. Read: hitch lines in the  |
| rc-on-refcountguard | 01:50 (09:50) | 7.6 | PASS | PASS: profile stopped at 4 events after 4.7 min (off 0, on 4; 4 firings, 4 cycles; rule 4 events; 2 steps capped), cap 9 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2191.2/1855.9/1500.9 -> 2231/1907.6/1467.3 | asyncStarted +3766, asyncRefused +139, asyncCollected +3764, asyncDropped +2, asyncUs +36162829, asyncMaxUs +358939, asyncPumpUs +23855152, asyncPumpWaits +3356, asyncRefusedM1Pending +14, asyncBackoffs +6, asyncQueueFull +65, asyncQueueMax +8 | noise: medium (other apps 6.2% of game CPUs); others on game CPUs 6.2%; clock avg/min 4187/4152 MHz; Release check part c: refcountguard ([RouteAsync] RefcountGuard, footprint refcount guard (docs/177 §7)) on only, forced repro ip-refcount-ring-loop until 4 firings. Pass: no crash, the stop line, counters rose, rcDeferred>=1,rcPass_4<=0,rcDropped_1<=0. Read: hitch lines in the window (hitch-ab.ts  |
| rc-on-stuckcheckasync | 01:58 (09:58) | 3.2 | PASS | PASS: profile stopped at 12 events after 0.2 min (off 0, on 12; 1 firings, 1 cycles; rule 4 events), cap 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2160.5/1834.2/1532.9 -> 2165.3/1879.6/1483.3 | asyncStarted +2546, asyncRefused +36, asyncCollected +2546, asyncUs +11207948, asyncMaxUs +104442, asyncPumpUs +6010803, asyncPumpWaits +1319, asyncRefusedM1Pending +11, asyncBackoffs +5, asyncQueueFull +1, asyncQueueMax +3 | noise: medium (other apps 6.1% of game CPUs); others on game CPUs 6.1%; clock avg/min 4155/4078 MHz; Release check part c: stuckcheckasync (ts3fixes 3 AM stuck check in a task (docs/134 §8)) on only, forced repro hitch-stuckcheck-loop until 4 firings. Pass: no crash, the stop line, counters rose (stuckcheckasync_run>=4). Read: hitch lines in the window (hitch-ab.ts --scenario <tag>) should name St |
| rc-on-overwatchchain | 02:01 (10:01) | 3.2 | PASS | PASS: profile stopped at 4 events after 0.3 min (off 0, on 4; 4 firings, 4 cycles; rule 4 events), cap 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2190.2/1853.8/1500.9 -> 2181.3/1865.8/1483.3 | asyncStarted +119, asyncRefused +7, asyncCollected +119, asyncUs +1599791, asyncMaxUs +81550, asyncPumpUs +621720, asyncPumpWaits +46, asyncRefusedM1Pending +7, asyncQueueMax +1 | noise: medium (other apps 6.5% of game CPUs); others on game CPUs 6.5%; clock avg/min 4210/4165 MHz; Release check part c: overwatchchain (ts3fixes NRaas Overwatch v2: cleanup types one tick apart in its alarm task (docs/145 §10; overwatchspread held off)) on only, forced repro bp-overwatch-ab-loop2 until 4 firings. Pass: no crash, the stop line, counters rose (owchain_alarms>=4). Read: hitch line |
| rc-on-errortrapspread | 02:04 (10:04) | 3.5 | PASS | PASS: profile stopped at 4 events after 0.2 min (off 0, on 4; 4 firings, 4 cycles; rule 4 events), cap 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2165.4/1837/1532.9 -> 2173.2/1867.4/1499.3 | asyncStarted +2466, asyncRefused +36, asyncCollected +2465, asyncUs +10902569, asyncMaxUs +87782, asyncPumpUs +6048129, asyncPumpWaits +1313, asyncRefusedM1Pending +13, asyncSkipped +1, asyncBackoffs +3, asyncQueueMax +4 | noise: medium (other apps 5.3% of game CPUs); others on game CPUs 5.3%; clock avg/min 4207/4183 MHz; Release check part c: errortrapspread (ts3fixes NRaas ErrorTrap load scan spread (docs/146)) on only, forced repro hitch-errortrap-loop until 4 firings. Pass: no crash, the stop line, counters rose (etspread_passes>=4,etspread_failed<=0). Read: hitch lines in the window (hitch-ab.ts --scenario <tag |
| rc-on-ts3fixes-v014 | 02:08 (10:08) | 3.2 | PASS | PASS: profile stopped at 33 events after 0.3 min (off 0, on 33; 1 firings, 1 cycles; rule 4 events), cap 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2164.4/1850.3/1500.9 -> 2179.8/1921.3/1451.3 | asyncStarted +3103, asyncRefused +104, asyncCollected +3100, asyncUs +15549436, asyncMaxUs +565362, asyncPumpUs +8043371, asyncPumpWaits +2198, asyncRefusedM1Pending +15, asyncBackoffs +3, asyncQueueFull +51, asyncQueueMax +5 | noise: medium (other apps 5.4% of game CPUs); others on game CPUs 5.4%; clock avg/min 4190/4130 MHz; Release check part c: ts3fixes-v014 (ts3fixes fixes shipped on since v0.1.3/v0.1.4 (route failure backoff, stuck watchdog, shortcuts, UI caches, fast ref table, portal leak)) on only, forced repro route-nopath-crowd-loop until 4 firings. Pass: no crash, the stop line, counters rose (backoff_plans>= |
| rc-ab-ip-refcount-ring-loop | 02:11 (10:11) | 27.3 | PASS | PASS: profile stopped at 20 events after 24.4 min (off 10, on 10; 20 firings, 5 cycles; rule 10 events per arm; 13 steps capped), cap 36 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2192.1/1857.8/1500.9 -> 2257.8/2077.9/1329 | asyncStarted +15773, asyncRefused +632, asyncCollected +15756, asyncDropped +14, asyncUs +151963081, asyncMaxUs +378716, asyncPumpUs +103584223, asyncPumpWaits +16181, asyncRefusedM1Pending +102, asyncSkipped +2, asyncBackoffs +19, asyncQueueFull +316, asyncQueueMax +8 | noise: medium (other apps 5.4% of game CPUs); others on game CPUs 5.4%; clock avg/min 4191/4127 MHz; Release check part d: bundle refcountguard,deferfootprint,stuckcheckasync,overwatchchain,errortrapspread off vs on together (off = the previous release for all of them), ABBA on ip-refcount-ring-loop until 10 firings per arm. Pass: no crash, no tripwire, the stop line. Read: hitch-ab.ts --scenario  |
| rc-ab-pet-guard-loop | 02:38 (10:38) | 24.1 | PASS | PASS: profile stopped at 20 events after 20.9 min (off 10, on 10; 20 firings, 5 cycles; rule 10 events per arm), cap 34 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2160.9/1849.7/1500.9 -> 2319.6/2104.4/1259.4 | asyncStarted +29283, asyncRefused +1104, asyncCollected +29273, asyncDropped +8, asyncUs +182696531, asyncMaxUs +193496, asyncPumpUs +112284558, asyncPumpWaits +19743, asyncRefusedM1Pending +104, asyncSkipped +1, asyncBackoffs +52, asyncQueueFull +599, asyncQueueMax +8 | noise: medium (other apps 5.7% of game CPUs); others on game CPUs 5.7%; clock avg/min 4201/4164 MHz; Release check part d: bundle refcountguard,deferfootprint,stuckcheckasync,overwatchchain,errortrapspread off vs on together (off = the previous release for all of them), ABBA on pet-guard-loop until 10 firings per arm. Pass: no crash, no tripwire, the stop line. Read: hitch-ab.ts --scenario <tag> ( |
| rc-ab-hitch-stuckcheck-loop | 03:03 (11:03) | 4.1 | PASS | PASS: profile stopped at 48 events after 0.9 min (off 24, on 24; 4 firings, 1 cycles; rule 10 events per arm), cap 16 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2168.3/1853.7/1500.9 -> 2185.2/1887.6/1483.3 | asyncStarted +2449, asyncRefused +37, asyncCollected +2448, asyncDropped +1, asyncUs +12622007, asyncMaxUs +146754, asyncPumpUs +6634976, asyncPumpWaits +1514, asyncRefusedM1Pending +8, asyncBackoffs +6, asyncQueueFull +1, asyncQueueMax +5 | noise: medium (other apps 6.8% of game CPUs); others on game CPUs 6.8%; clock avg/min 4226/4165 MHz; Release check part d: bundle refcountguard,deferfootprint,stuckcheckasync,overwatchchain,errortrapspread off vs on together (off = the previous release for all of them), ABBA on hitch-stuckcheck-loop until 10 firings per arm. Pass: no crash, no tripwire, the stop line. Read: hitch-ab.ts --scenario  |
| rc-ab-bp-overwatch-ab-loop2 | 03:07 (11:07) | 5.1 | PASS | PASS: profile stopped at 20 events after 2.1 min (off 10, on 10; 20 firings, 5 cycles; rule 10 events per arm), cap 26 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2178.7/1852.7/1500.9 -> 2123.5/1842.4/1483.3 | asyncStarted +178, asyncRefused +8, asyncCollected +178, asyncUs +5566551, asyncMaxUs +757161, asyncPumpUs +765002, asyncPumpWaits +92, asyncRefusedM1Pending +8, asyncBackoffs +1, asyncQueueMax +1 | noise: medium (other apps 5.5% of game CPUs); others on game CPUs 5.5%; clock avg/min 4218/4174 MHz; Release check part d: bundle refcountguard,deferfootprint,stuckcheckasync,overwatchchain,errortrapspread off vs on together (off = the previous release for all of them), ABBA on bp-overwatch-ab-loop2 until 10 firings per arm. Pass: no crash, no tripwire, the stop line. Read: hitch-ab.ts --scenario  |
| rc-ab-hitch-errortrap-loop | 03:12 (11:12) | 5.1 | PASS | PASS: profile stopped at 20 events after 1.8 min (off 10, on 10; 20 firings, 5 cycles; rule 10 events per arm), cap 16 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2163.1/1835/1532.9 -> 2199.4/1849.3/1467.6 | asyncStarted +2809, asyncRefused +55, asyncCollected +2808, asyncDropped +1, asyncUs +13517022, asyncMaxUs +137150, asyncPumpUs +6706765, asyncPumpWaits +1644, asyncRefusedM1Pending +10, asyncBackoffs +5, asyncQueueFull +2, asyncQueueMax +3 | noise: medium (other apps 5.9% of game CPUs); others on game CPUs 5.9%; clock avg/min 4213/4188 MHz; Release check part d: bundle refcountguard,deferfootprint,stuckcheckasync,overwatchchain,errortrapspread off vs on together (off = the previous release for all of them), ABBA on hitch-errortrap-loop until 10 firings per arm. Pass: no crash, no tripwire, the stop line. Read: hitch-ab.ts --scenario < |
| bp-rel-rare-winter-pond | 03:17 (11:17) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2162.2/1851.2/1500.9 -> 2269.5/2075.7/1275.3 | - | noise: medium (other apps 5.2% of game CPUs); others on game CPUs 5.2%; clock avg/min 4213/4154 MHz; Release v0.2.0 validation (ts3native release build parked as game-copy/ts3native.asi.rel020, sha256 in docs/RELEASE.md v0.2.0; release ini = the repo ini after node tools/release-features.ts <chosen> --write; no job ini overrides). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first  |
| ip-rel-rare-ip-water | 03:24 (11:24) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2182.9/1855.5/1500.9 -> 2172.9/1875.8/1483.3 | - | noise: medium (other apps 6.0% of game CPUs); others on game CPUs 6.0%; clock avg/min 4207/4161 MHz; Release v0.2.0 validation (ts3native release build parked as game-copy/ts3native.asi.rel020, sha256 in docs/RELEASE.md v0.2.0; release ini = the repo ini after node tools/release-features.ts <chosen> --write; no job ini overrides). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first  |
| bp-rc-soak | 03:32 (11:32) | 46.9 | PASS | PASS: profile 45 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2169/1838.1/1532.9 -> 2371.8/2135/1211.4 | asyncStarted +59104, asyncRefused +921, asyncCollected +59087, asyncDropped +11, asyncUs +334714959, asyncMaxUs +173702, asyncPumpUs +200664293, asyncPumpWaits +35971, asyncRefusedM1Pending +202, asyncSkipped +6, asyncBackoffs +106, asyncQueueFull +20, asyncQueueMax +5 | noise: medium (other apps 6.7% of game CPUs); others on game CPUs 6.7%; clock avg/min 4192/4136 MHz; Release check part f (soak): 45 min natural play, no repro. Pass: no crash, 0 watchdog lines, stuck Sims within docs/TESTING.md. Read: hitch-ab.ts <tag> (docs/161 real-hitch counts, p99, max), freeze-report.ts <tag>, VAS growth = the row's commit/private/largest free MB before -> after (v0.1.4 bp-r |

### bp-rc-smoke: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-rc-smoke: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-rc-smoke: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rel-matrix: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rel-travel-france: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-on-savewrite: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 18 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-on-routeasync: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 7 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-on-routegate: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-on-pet-guard-loop: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 9 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-on-refcountguard: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 9 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-on-stuckcheckasync: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-on-overwatchchain: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-on-errortrapspread: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-on-ts3fixes-v014: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-ab-ip-refcount-ring-loop: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 36 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-ab-pet-guard-loop: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 34 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-ab-hitch-stuckcheck-loop: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 16 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-ab-bp-overwatch-ab-loop2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 26 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### rc-ab-hitch-errortrap-loop: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 16 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rel-rare-winter-pond: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-rel-rare-ip-water: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rc-soak: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 45 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
