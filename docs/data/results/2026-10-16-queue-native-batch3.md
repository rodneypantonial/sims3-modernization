# Queue native-batch3 2026-10-16

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/native-batch3-2026-10-16.jsonl`.

## 2026-10-16T13:42 UTC, build be4267a, jobs bp-native-batch3, ip-native-batch3

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-native-batch3 | 13:42 (21:42) | 14.5 | PASS | PASS: profile 12 min, unseeded (rng-compare.ts) | 0 | 2230/1839.9/1508.7 -> 2450.1/2218.9/1070.1 | - | PLAN Next 3, native fix batch 3 (0.10.10, counters v24), one bundle load in BP: #11 [LotSinCos] and #22 [IdCache] alternate on/off together every 3 min (--ab lotsincos,idcache), both with Verify=1 (first live run: every hit is checked against the game's code, so the on slices pay for a second computation). [Alloc] stays off until batch 2 passes. Pass: no crash, 0 stuck Sims, lscMismatches = 0, idc |

Paused after bp-native-batch3 (AI usage 5h 90% (reset 23:00 PHT) | 7d 48% (reset 14:00 PHT) | read 0 min ago); resume with `--from ip-native-batch3`.
