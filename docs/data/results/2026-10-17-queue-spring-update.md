# Queue spring-update 2026-10-17

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/spring-update-2026-10-17.jsonl`.

## 2026-10-17T10:52 UTC, build 3a0681b, jobs update-spring-bp, update-spring-ip, update-spring-sv

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| update-spring-bp | 10:52 (18:52) | 3.1 | **FAIL** | FAIL: step failed | 1 | 2146.2/1837/1575.6 -> ? | - | Add a pet to Baseline-BP-Spring, clean, check, save over it |
| update-spring-ip | 10:56 (18:56) | 1.6 | **FAIL** | cycle.ts --save exit 1; probe server exited (0) for ~60 s (fail-fast guard) | - | ? -> ? | - | Add a pet to Baseline-IP-Spring, clean, check, save over it |
| update-spring-sv | 10:57 (18:57) | 1.1 | **FAIL** | cycle.ts --save exit 1; probe server exited (0) for ~60 s (fail-fast guard) | - | ? -> ? | - | Add a pet to Baseline-SV-Spring, clean, check, save over it |

## 2026-10-17T11:00 UTC, build 3a0681b, jobs update-spring-bp, update-spring-ip, update-spring-sv

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| update-spring-bp | 11:00 (19:00) | 2.9 | **FAIL** | FAIL: test_env until [{"path":"season","equals":"Spring"},{"path":"temperature","atLeast":35},{"path":"snow_level","equals":"None"},{"path":"weather","equals":"Sunny"},{"path":"moon_phase","equals":4}] not met in 6 s: {"changed":[],"temperature":50.1379738,"weather":"Sunny","snow_level":"None","days_to_full_moon":-1,"moon_phase":1,"season":"Spring"} | 1 | 2229.5/1838.8/1532.9 -> 2234.6/1830.7/1532.9 | - | Add a pet to Baseline-BP-Spring, clean, check, save over it |
| update-spring-ip | 11:03 (19:03) | 2.6 | **FAIL** | FAIL: test_env until [{"path":"season","equals":"Spring"},{"path":"temperature","atLeast":35},{"path":"snow_level","equals":"None"},{"path":"weather","equals":"Sunny"},{"path":"moon_phase","equals":4}] not met in 6 s: {"changed":[],"temperature":42.3088379,"weather":"Sunny","snow_level":"None","days_to_full_moon":0,"moon_phase":0,"season":"Spring"} | 1 | 2226.3/1837/1516.9 -> 2224.3/1815.4/1548.9 | - | Add a pet to Baseline-IP-Spring, clean, check, save over it |
| update-spring-sv | 11:06 (19:06) | 2.7 | **FAIL** | FAIL: test_env until [{"path":"season","equals":"Spring"},{"path":"temperature","atLeast":35},{"path":"snow_level","equals":"None"},{"path":"weather","equals":"Sunny"},{"path":"moon_phase","equals":4}] not met in 6 s: {"changed":[],"temperature":48.794857,"weather":"Sunny","snow_level":"None","days_to_full_moon":-1,"moon_phase":1,"season":"Spring"} | 1 | 2182.1/1800.6/1564.9 -> 2087.2/1942.9/1436.9 | - | Add a pet to Baseline-SV-Spring, clean, check, save over it |

## 2026-10-17T11:09 UTC, build 3a0681b, jobs update-spring-bp, update-spring-ip, update-spring-sv

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| update-spring-bp | 11:09 (19:09) | 2.9 | PASS | update-spring-bp: PASS: steps done (no watch) (expected: undefined) | 0 | 2243/1839.9/1500.9 -> 2195.8/1858.5/1500.9 | - | Add a pet to Baseline-BP-Spring, clean, check, save over it |
| update-spring-ip | 11:12 (19:12) | 3 | PASS | update-spring-ip: PASS: steps done (no watch) (expected: undefined) | 0 | 2241/1858.9/1512.8 -> 2259.2/1863.8/1504.8 | - | Add a pet to Baseline-IP-Spring, clean, check, save over it |
| update-spring-sv | 11:15 (19:15) | 3.2 | PASS | update-spring-sv: PASS: steps done (no watch) (expected: undefined) | 0 | 2204.5/1790.4/1574.2 -> 2210.3/1786.7/1574.2 | - | Add a pet to Baseline-SV-Spring, clean, check, save over it |
