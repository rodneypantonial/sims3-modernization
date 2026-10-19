# Queue rare-scenarios-2 2026-10-19

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/rare-scenarios-2-2026-10-19.jsonl`.

## 2026-10-19T00:22 UTC, build f90918f, jobs fr-rare-world-walk

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| fr-rare-world-walk | 00:22 (08:22) | 9.1 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts) | 0 | 2223.2/1841.8/1516.8 -> 2308.9/2012.3/1238.2 | asyncStarted +1211, asyncRefused +21, asyncCollected +1208, asyncUs +13088492, asyncMaxUs +283513, asyncPumpUs +5307172, asyncPumpWaits +1032, asyncRefusedM1Pending +21, asyncSkipped +3, asyncBackoffs +1, asyncQueueMax +5 | UNVERIFIED repro, never run (docs/117): rare-world-walk on Baseline-BP after a vacation trip to France (profile.travel), [ProbeSnap] pass 2 indirect-call sampler (2,110 sites); after: node tools/harness/snap-report.ts <row.nativeLog> and node tools/indirect-live-vs-static.ts <log> --unfired --sites native/ts3native/vcallsites2.h |
