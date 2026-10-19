
### 8.7 Worker reruns on the P1b build (2026-10-19 00:25 and 00:33 PHT)

Both jobs ran `game-copy/ts3native.asi.snapp1b` (ts3native 0.27.0, so the §8.6 stale build is out of the picture), RouteAsync Mode 1 + RouteGate Enforce + Discourage. Report: `node tools/harness/watch-stalk-report.ts --worker probe-out/profile/watch-stalk-2026-10-18-<bp|ip>-watch-stalk-worker-watchstalk.json`.

| Item | BP (PASS, 7.8 min) | IP (PASS, 9.1 min) |
|---|---|---|
| U1 on the worker | **verified**: 48 of 3,705 worker plans followed (tid 14704); 2 written addresses, both known (zero-store sentinel `0x114C900`, #1 A* stamp) | **verified**: 48 of 1,545 (tid 29596); same 2 addresses |
| ms per followed plan | median 95, max 1,071 | median 105, max 2,020 |
| fs/gs writes | 0 | 0 |
| U10 | verified: 40 routes, IC writers only (5 hits), zeroing 0; 33 watches ended at the Route dtor | verified: 40 routes, IC writers only (4 hits); 38 ended at the Route dtor |
| U3 (`0x66AB70`) | not run in 48 plans | not run |
| Probe errors | 0 | 1: the window closed during a followed plan at 285 s (forced unfollow, harmless) |

So the route search writes nothing on the worker beyond what docs/101 §2.2 and §7.2 already list. That is the same write set as the sim-thread runs (§7). No crash on the current build: the §8.6 crash belonged to the stale 0.10.1 build [verified by the rerun, 2 clean runs].
