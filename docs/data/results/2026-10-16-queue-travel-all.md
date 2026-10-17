# Queue travel-all 2026-10-16

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/travel-all-2026-10-16.jsonl`.

## 2026-10-16T12:29 UTC, build be4267a, jobs bp-travel-university, bp-travel-future, bp-travel-china, bp-travel-egypt, bp-travel-france

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-travel-university | 12:29 (20:29) | 22.1 | **FAIL** | travel: FAIL<br>summary: 1 cases, 0 passed, 1 failed (travel) | 1 | 2295.8/1923.3/1460.9 -> 2189.7/1915.2/1476.9 | flushDeferred +3, flushJobs +2, flushLastMs +3, compressCommits +34, compressDirectCalls +19431, compressDirectMs +2251, compressDirectMB +279 | travel save case to University, direct trip (no van walk), actions ended + needs max first |
| bp-travel-future | 12:51 (20:51) | 8.5 | **FAIL** | - | 1 | 2239.8/1852.7/1492.9 -> 2226.1/1871.5/1492.9 | flushDeferred +2, flushJobs +1, flushLastMs +93, compressCommits +2, compressDirectCalls +3694, compressDirectMs +1383, compressDirectMB +181 | travel save case to the future, direct portal trip (portal_prepare), actions ended + needs max first |
| bp-travel-china | 13:00 (21:00) | 4.5 | **FAIL** | travel: FAIL<br>summary: 1 cases, 0 passed, 1 failed (travel) | 1 | 2227/1852.9/1492.9 -> 2280/2028.6/1344.8 | flushDeferred +8, flushJobs +5, flushLastMs +163, compressCommits +71, compressDirectCalls +29530, compressDirectMs +5282, compressDirectMB +566 | travel save case to Shang Simla (vacation) |
| bp-travel-egypt | 13:05 (21:05) | 4.6 | **FAIL** | travel: FAIL<br>summary: 1 cases, 0 passed, 1 failed (travel) | 1 | 2230.3/1851/1500.9 -> 2251.4/2039.2/1340.9 | flushDeferred +8, flushJobs +5, flushLastMs +81, compressCommits +71, compressDirectCalls +29073, compressDirectMs +5029, compressDirectMB +565 | travel save case to Al Simhara (vacation) |
| bp-travel-france | 13:09 (21:09) | 4.6 | **FAIL** | travel: FAIL<br>summary: 1 cases, 0 passed, 1 failed (travel) | 1 | 2227.8/1851.7/1508.9 -> 2269.8/1912.2/1457 | flushDeferred +8, flushJobs +5, flushLastMs +47, compressCommits +73, compressDirectCalls +31593, compressDirectMs +4996, compressDirectMB +595 | travel save case to Champs Les Sims from Bridgeport (IP -> France passed 2026-10-15) |
