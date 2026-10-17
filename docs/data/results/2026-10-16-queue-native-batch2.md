# Queue native-batch2 2026-10-16

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/native-batch2-2026-10-16.jsonl`.

## 2026-10-16T13:14 UTC, build be4267a, jobs bp-native-batch2, ip-native-batch2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-native-batch2 | 13:14 (21:14) | 14 | PASS | PASS: profile 12 min, unseeded (rng-compare.ts) | 0 | 2221.1/1851.4/1508.9 -> 2347.4/2112.9/1198.3 | allocHitsK +38409, allocMissesK +58448, allocCachedK +38410, allocPassedK +58832, allocFlushed +83 | PLAN Next 2, native fix batch 2 (0.10.9, counters v23), one bundle load in BP: #13 SimSkipShared alternates on/off every 3 min (--ab simskipshared); start-up fixes on for the whole load: #14 Workers=2, #4 D3DMultithreaded, [Alloc] with Dedupe (docs/67; allocDupFrees > 0 = the game double frees). Pass: no crash, 0 stuck Sims, jqSimSharedJobs/jqSimRenderJobs flat in on slices, jqWorkerTids = 2, d3dM |
| ip-native-batch2 | 13:28 (21:28) | 14.1 | PASS | PASS: profile 12 min, unseeded (rng-compare.ts) | 0 | 2245.5/1853.9/1492.9 -> 2170.3/1880.5/1476.9 | allocHitsK +43356, allocMissesK +40407, allocCachedK +43357, allocPassedK +40910, allocThreads -1, allocFlushed +1764 | the same bundle in IP (second world: houseboats, water, other lot density) |
