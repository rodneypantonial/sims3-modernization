# Queue clean-night 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/clean-night-2026-10-18.jsonl`.

## 2026-10-18T00:21 UTC, build 52d01d8, jobs ip-ab-routeasync

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-ab-routeasync | 00:21 (08:21) | 21.4 | PASS | PASS: profile 18 min, seed 12345 (rng-compare.ts) | 0 | 2179.6/1842.9/1532.9 -> 2470.8/2143/1020.5 | asyncMode -1, asyncStarted +4866, asyncRefused +28, asyncCollected +4854, asyncDropped +12, asyncUs +57138499, asyncMaxUs +277669, asyncPumpUs +40077253, asyncPumpWaits +4443, asyncRefusedM1Pending +24, asyncBackoffs +3, asyncQueueFull +4, asyncQueueMax +7 | In-load A/B: async Mode 1 on/off every 2 min, one load (rules out load-to-load noise) |
