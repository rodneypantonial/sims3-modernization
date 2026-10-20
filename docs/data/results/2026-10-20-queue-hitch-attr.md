# Queue hitch-attr 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/hitch-attr-2026-10-20.jsonl`.

## 2026-10-20T00:50 UTC, build 881158c, jobs ip

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip | 00:50 (08:50) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2212.3/1890.3/1484.9 -> 2218.7/1910.8/1484.9 | asyncStarted +6100, asyncRefused +131, asyncCollected +6096, asyncDropped +4, asyncUs +65041818, asyncMaxUs +307755, asyncPumpUs +43682607, asyncPumpWaits +4966, asyncRefusedM1Pending +23, asyncBackoffs +5, asyncQueueMax +4 | the same on Isla Paradiso (houseboats, water, EP portals; most route-search hitches, docs/96 §4.5) |
