# Queue story-smoke 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/story-smoke-2026-10-23.jsonl`.

## 2026-10-23T02:47 UTC, build 072bcbc1, jobs bp-story-smoke

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-smoke | 02:47 (10:47) | 10.4 | **FAIL** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: sim clock stuck at day 1 08:00 for ~60 s (speed Normal; fail-fast guard)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: story: 53 steps logged, join ok (story-report.ts story-smoke-2026-10-23-bp-story-smoke) | 1 | 2130.8/1835.4/1516.9 -> 2233.3/2042.6/1187 | asyncStarted +7524, asyncRefused +114, asyncCollected +7524, asyncUs +54184263, asyncMaxUs +173227, asyncPumpUs +29134022, asyncPumpWaits +5182, asyncRefusedM1Pending +33, asyncBackoffs +6, asyncQueueMax +5 | noise: medium (other apps 9.4% of game CPUs); others on game CPUs 9.4%; clock avg/min 4154/4114 MHz; One 10-min story run, seed 1, on the Story-* scratch copy; read with story-report.ts <tag>. Not a timing run. |

### bp-story-smoke: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-23T03:15 UTC, build 1b8fe133, jobs bp-story-smoke

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-smoke | 03:15 (11:15) | 5 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 03:20:29 (TS3W.exe process gone)<br>PASS: story: 26 steps logged, join ok (story-report.ts story-smoke-2026-10-23-bp-story-smoke) | 1 | 2136.9/1850/1516.6 -> ? | - | noise: high (other apps 13.5% of game CPUs); others on game CPUs 13.5%; clock avg/min 4169/4131 MHz; One 10-min story run, seed 1, on the Story-* scratch copy; read with story-report.ts <tag>. Not a timing run.; 2026-10-23T03:20:26.000Z  ACCESS_VIOLATION reading from address 0x00000030 (Unmappable address)  at 0x00575651 in TS3W.exe  stack: TS3W.exe |

## 2026-10-23T03:45 UTC, build b821c52d, jobs bp-story-smoke

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-smoke | 03:46 (11:46) | 15.4 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: story: 56 steps logged, join ok (story-report.ts story-smoke-2026-10-23-bp-story-smoke) | 0 | 2229.3/1855.3/1468.9 -> 2332.9/2126.8/1209.3 | asyncStarted +10858, asyncRefused +157, asyncCollected +10852, asyncDropped +6, asyncUs +88973829, asyncMaxUs +518011, asyncPumpUs +43657494, asyncPumpWaits +8564, asyncRefusedM1Pending +26, asyncBackoffs +4, asyncQueueMax +5 | noise: high (other apps 12.0% of game CPUs); others on game CPUs 12.0%; clock avg/min 4159/4112 MHz; One 10-min story run, seed 1, on the Story-* scratch copy; read with story-report.ts <tag>. Not a timing run. |

### bp-story-smoke: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
