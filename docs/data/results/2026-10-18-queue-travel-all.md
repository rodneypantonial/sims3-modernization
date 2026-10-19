# Queue travel-all 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/travel-all-2026-10-18.jsonl`.

## 2026-10-18T15:52 UTC, build 9c154c8, jobs bp-travel-university, bp-travel-future, bp-travel-future-direct, bp-travel-china, bp-travel-egypt, bp-travel-france

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-travel-university | 15:52 (23:52) | 12.4 | **CRASH** | FAIL: game died at 16:04:25 (TS3W.exe process gone)<br>run: FAIL<br>summary: 1 cases, 0 passed, 1 failed (run) | 1 | 2239/1846.3/1484.9 -> ? | - | travel save case to University, queued moving van (actions ended, needs max, autonomy off for the student); 2026-10-18T14:36:52.434Z  ?  at ?  stack: - |
| bp-travel-future | 16:04 (00:04) | 3.6 | **FAIL** | travel: FAIL<br>summary: 1 cases, 0 passed, 1 failed (travel) | 1 | 2243.8/1853.5/1500.8 -> 2279.8/2022.7/1356.9 | flushDeferred +5, flushJobs +3, flushLastMs +45, compressCommits +42, compressDirectCalls +21962, compressDirectMs +2946, compressDirectMB +399 | travel save case to the future, queued portal Travel (portal_prepare; actions ended, needs max, autonomy off for the traveller) |
| bp-travel-future-direct | 16:08 (00:08) | 3.2 | **CRASH** | FAIL: game died at 16:11:31 (TS3W.exe process gone)<br>run: FAIL<br>summary: 1 cases, 0 passed, 1 failed (run) | 1 | 2151/1845.7/1500.8 -> ? | - | travel save case to the future, direct switch (docs/77 order; verifies docs/51 I-77-2 from the unwrapped async_log exception); 2026-10-18T16:11:26.000Z  ACCESS_VIOLATION reading from address 0x44973010; State: MEM_FREE  at 0x006a81d6 in TS3W.exe  stack: TS3W.exe |
| bp-travel-china | 16:11 (00:11) | 4.2 | PASS | travel: PASS<br>summary: 1 cases, 1 passed, 0 failed | 0 | 2226.3/1845.3/1484.9 -> 2223.4/1889/1452.9 | flushDeferred +8, flushJobs +5, flushLastMs +47, compressCommits +71, compressDirectCalls +29134, compressDirectMs +4954, compressDirectMB +553 | travel save case to Shang Simla (vacation) |
| bp-travel-egypt | 16:16 (00:16) | 4.4 | PASS | travel: PASS<br>summary: 1 cases, 1 passed, 0 failed | 0 | 2229.9/1842.9/1484.9 -> 2230.5/1890/1382.2 | flushDeferred +8, flushJobs +5, flushLastMs +147, compressCommits +71, compressDirectCalls +29760, compressDirectMs +5111, compressDirectMB +565 | travel save case to Al Simhara (vacation) |
| bp-travel-france | 16:20 (00:20) | 4.4 | PASS | travel: PASS<br>summary: 1 cases, 1 passed, 0 failed | 0 | 2224.7/1850.1/1484.9 -> 2238.8/1913.3/1436.9 | flushDeferred +8, flushJobs +5, flushLastMs +43, compressCommits +73, compressDirectCalls +31302, compressDirectMs +4845, compressDirectMB +593 | travel save case to Champs Les Sims from Bridgeport (IP -> France passed 2026-10-15) |

## 2026-10-18T20:38 UTC, build 675507f, jobs bp-travel-university

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-travel-university | 20:38 (04:38) | 5 | PASS | travel: PASS<br>summary: 1 cases, 1 passed, 0 failed | 0 | 2149.7/1833.5/1548.9 -> 2174/2042/1254.4 | flushDeferred +8, flushJobs +5, flushLastMs +177, compressCommits +71, compressDirectCalls +36067, compressDirectMs +4454, compressDirectMB +602 | travel save case to University, queued moving van (actions ended, needs max, autonomy off for the student) |
