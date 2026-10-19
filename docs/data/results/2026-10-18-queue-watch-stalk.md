# Queue watch-stalk 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/watch-stalk-2026-10-18.jsonl`.

## 2026-10-18T13:38 UTC, build b63ea49, jobs bp-watch-stalk

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-watch-stalk | 13:38 (21:38) | 9.3 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2215.2/1847.9/1500.9 -> 2351/1952.8/1349.8 | - | docs/113 / docs/51 §53: docs/101 U10, U1, U3 by read-only Frida (tools/frida/watch-stalk.js), no native change. 3 min watch: up to 4 live routes at a time with a 4-byte rw hardware watchpoint on route+0x1F4 on every game thread (routes taken after the IC_Route_Replan / ReplanFromPosition increments; 40 routes max, 30 s each). Then 3 min stalk: Route_PlanLocked on the sim thread followed by Stalker |

## 2026-10-18T13:47 UTC, build b40b793, jobs ip-watch-stalk

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-watch-stalk | 13:47 (21:47) | 9.3 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2244.1/1853.6/1484.9 -> 2161.5/1873.5/1500.3 | - | bp-watch-stalk on Isla Paradiso (water routes, houseboats, boats: more route kinds for U10 and more plan paths for U1/U3). Queue total ~18 min. |

## 2026-10-18T14:32 UTC, build 1abcbde, jobs bp-watch-stalk-worker

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-watch-stalk-worker | 14:32 (22:32) | 4.7 | **CRASH** | FAIL: profile 6 min, seed 12345 (rng-compare.ts) | 1 | 2213.4/1851.8/1500.9 -> ? | - | docs/113 §8: U1 on the route worker (docs/101 names it for P4). Env-on async settings (snap-p1.json without RouteSnap): RouteAsync Mode 1 + RouteGate Enforce + Discourage, so plans run as worker jobs. Watch phase as bp-watch-stalk (now ended at the Route dtor 0x75D640, clean U10 liveness). Stalk phase follows Route_PlanLocked on the worker (first non-sim caller; plan prologue = ts3native detour ju |

## 2026-10-18T14:37 UTC, build 9c4a89a, jobs ip-watch-stalk-worker

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-18T16:25 UTC, build bd22032, jobs bp-watch-stalk-worker

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-watch-stalk-worker | 16:25 (00:25) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2289.2/1908.7/1460.9 -> 2395.4/2122/1182.3 | asyncStarted +7973, asyncRefused +12, asyncCollected +7971, asyncDropped +1, asyncUs +53718109, asyncMaxUs +1076117, asyncPumpUs +34115731, asyncPumpWaits +4912, asyncRefusedM1Pending +8, asyncBackoffs +7, asyncQueueFull +4, asyncQueueMax +5 | docs/113 §8: U1 on the route worker (docs/101 names it for P4). Env-on async settings (snap-p1.json without RouteSnap): RouteAsync Mode 1 + RouteGate Enforce + Discourage, so plans run as worker jobs. Watch phase as bp-watch-stalk (now ended at the Route dtor 0x75D640, clean U10 liveness). Stalk phase follows Route_PlanLocked on the worker (first non-sim caller; plan prologue = ts3native detour ju |

## 2026-10-18T16:33 UTC, build a2eca4b, jobs ip-watch-stalk-worker

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-watch-stalk-worker | 16:33 (00:33) | 9.1 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2235.8/1849.4/1484.9 -> 2201/1884/1484.3 | asyncStarted +3795, asyncRefused +16, asyncCollected +3795, asyncUs +60566799, asyncMaxUs +2025346, asyncPumpUs +48304552, asyncPumpWaits +2766, asyncRefusedM1Pending +14, asyncBackoffs +2, asyncQueueFull +2, asyncQueueMax +5 | bp-watch-stalk-worker on Isla Paradiso (water routes, houseboats). Queue total with the two sim-thread jobs ~36 min; --only bp-watch-stalk-worker,ip-watch-stalk-worker for these two. (asi snapp1b = ts3native 0.27.0 with the route gate; the tested build is the pinned 0.10.1 without it, docs/113 §8.6) |
