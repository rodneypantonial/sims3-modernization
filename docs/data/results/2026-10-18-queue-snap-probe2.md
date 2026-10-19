# Queue snap-probe2 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-probe2-2026-10-18.jsonl`.

## 2026-10-18T11:53 UTC, build de44bff, jobs bp-snap-probe2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-probe2 | 11:53 (19:53) | 2.3 | **FAIL** | FAIL: profile 10 min, seed 12345 (rng-compare.ts) | 1 | 2132.3/1840.6/1548.9 -> 2132.3/1840.6/1548.9 | - | DRAFT, not run (docs/109 §5): [ProbeSnap] pass 2, site table native/ts3native/vcallsites2.h (pass 1 + 70 extra roots, 1534 sites); same settings as snap-probe.json bp-snap-probe; asi built by the docs/109 §4 steps; after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T12:03 UTC, build 602b7f7, jobs bp-snap2-build

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap2-build | 12:03 (20:03) | 8.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2216.7/1837.7/1484.9 -> 2486.4/2156.2/991.3 | asyncStarted +6868, asyncRefused +13, asyncCollected +6866, asyncDropped +1, asyncUs +38911708, asyncMaxUs +175655, asyncPumpUs +22043625, asyncPumpWaits +4617, asyncRefusedM1Pending +12, asyncSkipped +1, asyncBackoffs +6, asyncQueueFull +1, asyncQueueMax +5 | DRAFT (docs/109 §6 group B): build mode, floor up/down, a wall run and floor tiles on levels 0 and 1, a stair try, build exit (repro bp-build-levels) for LotRouting_QuadTreeBuild, LotRouting_FootprintRoomIds, Footprint_EnsureLevelPolygons |

## 2026-10-18T12:12 UTC, build f07bb9a, jobs bp-snap2-upstairs

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap2-upstairs | 12:12 (20:12) | 8.4 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2221.4/1838.6/1500.9 -> 2441.5/2131.7/1110.1 | asyncStarted +7443, asyncRefused +28, asyncCollected +7441, asyncDropped +2, asyncUs +48798928, asyncMaxUs +214115, asyncPumpUs +28643067, asyncPumpWaits +5567, asyncRefusedM1Pending +26, asyncBackoffs +9, asyncQueueFull +2, asyncQueueMax +6 | DRAFT (docs/109 §6): plans and a walk to an upstairs object, then Route.Replan on the used route (repro bp-upstairs-route) for the stair/level sites and Route_UnlinkPaths, Route_PlanCommon 0x760F0A |

## 2026-10-18T12:20 UTC, build e2bec05, jobs bp-snap2-door

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap2-door | 12:20 (20:20) | 8.5 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2215.1/1830.5/1500.9 -> 2406.3/2093/1174.1 | asyncStarted +7479, asyncRefused +21, asyncCollected +7477, asyncDropped +1, asyncUs +46028566, asyncMaxUs +191550, asyncPumpUs +28196776, asyncPumpWaits +4801, asyncRefusedM1Pending +19, asyncSkipped +1, asyncBackoffs +4, asyncQueueFull +2, asyncQueueMax +5 | DRAFT (docs/109 §6 group B): home door locked for non-household, household Sim and visitors plan through it (repro bp-door-lock) for the deferred LotQuadTree_Insert 0x666D88/0x666E04 |

## 2026-10-18T13:24 UTC, build dfc6677, jobs bp-snap-probe2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-probe2 | 13:24 (21:24) | 13.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2218/1841.3/1500.9 -> 2413.1/2091.3/1142.1 | asyncStarted +13321, asyncRefused +37, asyncCollected +13318, asyncDropped +3, asyncUs +76993290, asyncMaxUs +228624, asyncPumpUs +46235486, asyncPumpWaits +8189, asyncRefusedM1Pending +35, asyncBackoffs +8, asyncQueueFull +2, asyncQueueMax +5 | DRAFT, not run (docs/109 §5): [ProbeSnap] pass 2, site table native/ts3native/vcallsites2.h (pass 1 + 70 extra roots, 1534 sites); same settings as snap-probe.json bp-snap-probe; asi built by the docs/109 §4 steps; after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |

## 2026-10-18T14:37 UTC, build 9c4a89a, jobs bp-snap2-objects

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap2-objects | 14:37 (22:37) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2216.7/1835.5/1484.9 -> 2370.5/2077.2/1135.6 | asyncStarted +7826, asyncRefused +17, asyncCollected +7825, asyncDropped +1, asyncUs +50525331, asyncMaxUs +218841, asyncPumpUs +28120334, asyncPumpWaits +5899, asyncRefusedM1Pending +17, asyncBackoffs +7, asyncQueueMax +4 | DRAFT (docs/109 §6 group B): object churn + buy mode (repro rt-s2b-sweep: place/remove 10 objects twice, buy_open/close) for LotQuadTree_Insert/ApplyInsert, LotRouting_FootprintRoomIds, Footprint_EnsureLevelPolygons sites |

## 2026-10-18T15:03 UTC, build 82f7176, jobs bp-snap2-cancel

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap2-cancel | 15:03 (23:03) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2222.6/1855.6/1452.9 -> 2335.1/2064.7/1222.2 | asyncStarted +10359, asyncRefused +17, asyncCollected +10355, asyncUs +60309441, asyncMaxUs +239746, asyncPumpUs +35478262, asyncPumpWaits +6848, asyncRefusedM1Pending +14, asyncSkipped +4, asyncBackoffs +5, asyncQueueFull +3, asyncQueueMax +6 | DRAFT (docs/109 §6 group C): cancels landing during route jobs (repro pet-guard-cancel, 8 guard/cancel cycles at ultra speed) for Job_Cancel 0x59A603 |

## 2026-10-18T15:15 UTC, build 406d23d, jobs bp-snap2-travel

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap2-travel | 15:15 (23:15) | 4.6 | PASS | travel: PASS<br>summary: 1 cases, 1 passed, 0 failed | 0 | 2240.5/1837.3/1500.9 -> 2231.9/1879.6/1484.9 | - | DRAFT (docs/109 §6 group A): travel save case to France and back (save-matrix travel case, as travel-all.json) for the lot-table miss path, LotGraph_dtor, the lot-index rebuild 0xC66700 and lot unload; arming starts DelaySec after start-up, so the arrival load is covered |

## 2026-10-18T15:44 UTC, build db1d7c6, jobs ip-snap2-houseboat

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-snap2-houseboat | 15:44 (23:44) | 7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2248.4/1839.5/1484.9 -> 2158.5/1856.6/1468.9 | asyncStarted +3205, asyncRefused +21, asyncCollected +3202, asyncDropped +2, asyncUs +34691505, asyncMaxUs +499733, asyncPumpUs +21345913, asyncPumpWaits +2738, asyncRefusedM1Pending +21, asyncBackoffs +1, asyncQueueMax +6 | DRAFT (docs/109 §6 groups A, D): houseboat lot created and deleted twice at a port gangway (repro ip-houseboat-create) for the lot-table miss, LotGraph_dtor and the lot index rebuild 0xC66700 |

## 2026-10-18T20:25 UTC, build 675507f, jobs ip-snap-probe2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-snap-probe2 | 20:25 (04:25) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2131.2/1849.7/1532.9 -> 2161.4/1881.9/1532.9 | asyncStarted +5855, asyncRefused +30, asyncCollected +5842, asyncDropped +11, asyncUs +68594051, asyncMaxUs +492357, asyncPumpUs +49079065, asyncPumpWaits +4420, asyncRefusedM1Pending +28, asyncQueueFull +2, asyncQueueMax +6 | IP copy of bp-snap-probe2 (G2 gap 3, docs/109 §8: pass 2 had only the houseboat job in IP). DRAFT, not run (docs/109 §5): [ProbeSnap] pass 2, site table native/ts3native/vcallsites2.h (pass 1 + 70 extra roots, 1534 sites); same settings as snap-probe.json bp-snap-probe; asi built by the docs |
