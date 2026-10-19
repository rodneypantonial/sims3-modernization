# Queue snap-probe3 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-probe3-2026-10-18.jsonl`.

## 2026-10-18T21:32 UTC, build 9a6f8d1, jobs bp-snap-probe3-worker

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-probe3-worker | 21:32 (05:32) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2219.7/1834.8/1500.9 -> 2252/2063.1/1277.1 | asyncStarted +14106, asyncRefused +265, asyncCollected +14101, asyncDropped +3, asyncUs +72912241, asyncMaxUs +173883, asyncPumpUs +43493975, asyncPumpWaits +9061, asyncRefusedM1Pending +40, asyncSkipped +1, asyncBackoffs +18, asyncQueueFull +4, asyncQueueMax +6 | DRAFT, not run (docs/109 §8.8, G2 gap 2): [ProbeSnap] pass 3, the 4 shared-helper sites 0x8A65EB, 0x5BF1FB, 0x8CE574, 0x9158AB (vcallsites3.h, build with /DTS3N_VCSITES3) with Threads=1: hits split job/worker/sim/other; settings as snap-probe2.json bp-snap-probe2; after: node tools/harness/snap-report.ts <row.nativeLog> --all |
