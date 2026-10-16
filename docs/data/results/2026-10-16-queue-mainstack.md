# Queue mainstack 2026-10-16

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/mainstack-2026-10-16.jsonl`.

## 2026-10-16T01:05 UTC, build 6f99996, jobs bp-mainstack

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-mainstack | 01:05 (09:05) | 8.3 | PASS | PASS: profile 6 min, unseeded (rng-compare.ts) | 0 | 2184.3/1823.2/1548.9 -> 2283/1940.1/1453.6 | - | M-TOOL / docs/20 §7: first main-thread stack sample (frame loop, D3D, UI, SpeedTree cost) beside the sim sampler, installed build |
