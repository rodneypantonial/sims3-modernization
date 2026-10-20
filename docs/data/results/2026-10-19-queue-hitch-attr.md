# Queue hitch-attr 2026-10-19

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/hitch-attr-2026-10-19.jsonl`.

## 2026-10-19T08:34 UTC, build fd171d2, jobs bp, ip

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp | 08:34 (16:34) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2222.6/1839.7/1516.9 -> 2401.5/2061.8/1222.4 | asyncStarted +13493, asyncRefused +219, asyncCollected +13489, asyncDropped +1, asyncUs +71436765, asyncMaxUs +170907, asyncPumpUs +42703238, asyncPumpWaits +9342, asyncRefusedM1Pending +40, asyncSkipped +3, asyncBackoffs +20, asyncQueueFull +4, asyncQueueMax +4 | docs/96 §9 (G5 decision): per-frame sim waits for the route job, [HitchAttr] Enabled=1 (0.34.10, counters v66, observe only); env-on settings (async + gate + D), no RouteSnap, no P5; BP Spring, seeded, 10 min. Build: native\ts3native\build.cmd <repo>\game-copy\hitchattr-build, then copy its ts3native.asi to game-copy\ts3native.asi.hitchattr. After: node tools/harness/hitch-attr-report.ts; pass = n |
| ip | 08:46 (16:46) | 3.8 | **FAIL** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: exit 3221225794 with no verdict line (child stopped from outside or died before its summary); last output: (no output) | 3221225794 | 2244.6/1840.3/1484.9 -> ? | - | the same on Isla Paradiso (houseboats, water, EP portals; most route-search hitches, docs/96 §4.5); child profile-world exited (code 3221225794) without a result |
