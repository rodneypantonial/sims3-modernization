# Queue spring-saves 2026-10-17

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/spring-saves-2026-10-17.jsonl`.

## 2026-10-17T04:25 UTC, build 6ccfe2c, jobs make-spring-bp, make-spring-ip, make-spring-sv

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| make-spring-bp | 04:25 (12:25) | 7.5 | **FAIL** | - | 1 | 2233.5/1835.8/1527.6 -> 2318.1/2043.4/1321 | - | Make Baseline-BP-Spring (spring, new moon, warm, no snow) |

## 2026-10-17T04:39 UTC, build 794f63f, jobs make-spring-bp, make-spring-ip, make-spring-sv

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| make-spring-bp | 04:39 (12:39) | 6.6 | **FAIL** | FAIL: step failed | 1 | 2154.3/1832.2/1540.9 -> 2257.5/2064.8/1349.1 | - | Make Baseline-BP-Spring (spring, new moon, warm, no snow) |

## 2026-10-17T04:47 UTC, build 7f8f027, jobs make-spring-bp, make-spring-ip, make-spring-sv

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| make-spring-bp | 04:47 (12:47) | 6.5 | **FAIL** | FAIL: step failed | 1 | 2241.8/1836.8/1508.9 -> 2321.5/2058.1/1301.1 | - | Make Baseline-BP-Spring (spring, new moon, warm, no snow) |

## 2026-10-17T04:58 UTC, build bc014f6, jobs make-spring-bp, make-spring-ip, make-spring-sv

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| make-spring-bp | 04:58 (12:58) | 6.5 | **FAIL** | FAIL: step failed | 1 | 2246/1849.3/1523.3 -> 2291.3/2064/1300.3 | - | Make Baseline-BP-Spring (spring, new moon, warm, no snow) |

## 2026-10-17T05:05 UTC, build d82b561, jobs make-spring-bp, make-spring-ip, make-spring-sv

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| make-spring-bp | 05:05 (13:05) | 7.2 | **FAIL** | FAIL: set_speed until [{"path":"temperature","atLeast":35},{"path":"snow_level","equals":"None"},{"path":"weather","equals":"Sunny"}] not met in 300 s: {"changed":[],"temperature":40.0877457,"weather":"Sunny","days_to_full_moon":-1,"moon_phase":2,"season":"Spring"} | 1 | 2220.4/1830.8/1527.4 -> 2338.8/2037.3/1321 | - | Make Baseline-BP-Spring (spring, new moon, warm, no snow) |
| make-spring-ip | 05:13 (13:13) | 4.5 | PASS | make-spring-ip: PASS: steps done (no watch) (expected: undefined) | 0 | 2266.7/1867.4/1503.3 -> 2217.9/1870.4/1503.3 | - | Make Baseline-IP-Spring (spring, new moon, warm, no snow) |
| make-spring-sv | 05:17 (13:17) | 3.1 | PASS | make-spring-sv: PASS: steps done (no watch) (expected: undefined) | 0 | 2210.8/1808.5/1564.9 -> 2200.4/1831.1/1556.9 | - | Make Baseline-SV-Spring (spring, new moon, warm, no snow) |

## 2026-10-17T05:23 UTC, build 217b543, jobs make-spring-bp

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| make-spring-bp | 05:23 (13:23) | 3.2 | PASS | make-spring-bp: PASS: steps done (no watch) (expected: undefined) | 0 | 2233.4/1839.3/1532.8 -> 2179.3/1840.7/1516.8 | - | Make Baseline-BP-Spring (spring, new moon, warm, no snow) |
