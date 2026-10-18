# Queue value-probe 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/value-probe-2026-10-18.jsonl`.

## 2026-10-18T10:53 UTC, build 8b30fc9, jobs bp-value-probe, ip-value-probe

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-value-probe | 10:53 (18:53) | 13.5 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2225.1/1850.2/1500.9 -> 2385.2/2169.6/1180.8 | asyncStarted +12090, asyncRefused +42, asyncCollected +12082, asyncDropped +5, asyncUs +86512730, asyncMaxUs +408607, asyncPumpUs +51459912, asyncPumpWaits +8035, asyncRefusedM1Pending +37, asyncSkipped +1, asyncBackoffs +11, asyncQueueFull +5, asyncQueueMax +5 | docs/108: [ProbeValues] routing value probe (0.24.0, counters v44), env-on settings as bp-snap-probe (async + gate + D) plus ProbeValues, BP Spring, seeded; after: node tools/harness/values-report.ts <row.nativeDump> --log <row.nativeLog>; pass = pvReady 3, pvSkipped 0, pvWorlds >= 1, pvFrameFull 0, no crash |
