# Queue snap-p4-optsinit 2026-10-19

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/snap-p4-optsinit-2026-10-19.jsonl`.

## 2026-10-19T07:31 UTC, build 186c2e9, jobs bp-snap-p4-optsinit

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-snap-p4-optsinit | 07:31 (15:31) | 12.3 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2271.8/2008.4/1381.5 -> 2480.9/2297.6/1022.9 | asyncStarted +6364, asyncRefused +132, asyncCollected +6278, asyncDropped +1, asyncUs +43186934, asyncMaxUs +304254, asyncPumpUs +50202939, asyncPumpWaits +6055, asyncRefusedM1Pending +46, asyncSkipped +81, asyncBackoffs +865, asyncQueueFull +4, asyncQueueMax +6 | snap-p4 BP with [OptsInit] Enabled=1 (I-94-322 check: the recurring door-pair M2 should vanish). docs/94 §21 P4 part B, shadow only (0.29.0, counters v51). DO NOT RUN before the private pools build (docs/94 §16.6, pools.cpp by a separate agent) is merged and this asi rebuilt with it: without it the replay consumer and the clone run share the game's unlocked pools. Then: env-on async + [RouteSnap]  |
