# Queue story-days 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/story-days-2026-10-23.jsonl`.

## 2026-10-23T04:01 UTC, build b821c52d, jobs bp-story-days

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-days | 04:01 (12:01) | 27 | **FAIL** | PASS: profile 25 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: story: Morrigan Hemlock missed work/school on day 17 | 0 | 2139.3/1853.3/1516.9 -> 2505.1/2166.5/1100.3 | asyncStarted +21805, asyncRefused +330, asyncCollected +21797, asyncDropped +8, asyncUs +175830087, asyncMaxUs +1303036, asyncPumpUs +87424026, asyncPumpWaits +17385, asyncRefusedM1Pending +87, asyncBackoffs +15, asyncQueueMax +5 | noise: medium (other apps 9.8% of game CPUs); others on game CPUs 9.8%; clock avg/min 4141/4090 MHz; One 25-min story run with the day planner, seed 1, on the Story-* scratch copy; read with story-report.ts <tag> (Day plan section). Not a timing run. |

### bp-story-days: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-23T04:47 UTC, build d0dafc17, jobs bp-story-days

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-days | 04:47 (12:47) | 26.9 | **FAIL** | PASS: profile 25 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: story: pursuits per Sim-day: free days 0.13 not > work/school days 0.30 | 0 | 2139.1/1856/1548.9 -> 2475.9/2149.3/1153.6 | asyncStarted +19864, asyncRefused +297, asyncCollected +19856, asyncDropped +6, asyncUs +164883779, asyncMaxUs +277078, asyncPumpUs +87908311, asyncPumpWaits +15960, asyncRefusedM1Pending +73, asyncSkipped +2, asyncBackoffs +17, asyncQueueMax +6 | noise: high (other apps 16.8% of game CPUs); others on game CPUs 16.8%; clock avg/min 4164/4109 MHz; One 25-min story run with the day planner, seed 1, on the Story-* scratch copy; read with story-report.ts <tag> (Day plan section). Not a timing run. |

### bp-story-days: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-23T05:16 UTC, build 7992a9c3, jobs bp-story-days

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-days | 05:16 (13:16) | 28 | **FAIL** | PASS: profile 25 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: story: 17416828182964297824 missed work/school on day 18; pursuits per Sim-day: free days 0.17 not > work/school days 0.63 | 0 | 2220.5/1852.6/1430.2 -> 2506.7/2173/1104.8 | asyncStarted +24296, asyncRefused +323, asyncCollected +24283, asyncDropped +7, asyncUs +210959258, asyncMaxUs +413861, asyncPumpUs +110280136, asyncPumpWaits +21664, asyncRefusedM1Pending +112, asyncSkipped +4, asyncBackoffs +17, asyncQueueMax +5 | noise: high (other apps 15.5% of game CPUs); others on game CPUs 15.5%; clock avg/min 4154/4109 MHz; One 25-min story run with the day planner, seed 1, on the Story-* scratch copy; read with story-report.ts <tag> (Day plan section). Not a timing run. |

### bp-story-days: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-23T08:14 UTC, build 97a26ce2, jobs bp-story-days

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-days | 08:14 (16:14) | 23 | **CRASH** | FAIL: profile 25 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 08:37:25 (TS3W.exe process gone)<br>FAIL: story: no days summary (day planner did not run or the run stopped early) | 1 | 2137.8/1855.2/1516.9 -> ? | - | noise: high (other apps 12.3% of game CPUs); others on game CPUs 12.3%; clock avg/min 4238/4177 MHz; One 25-min story run with the day planner, seed 1, on the Story-* scratch copy; read with story-report.ts <tag> (Day plan section). Not a timing run.; 2026-10-23T03:41:02.000Z  ACCESS_VIOLATION reading from address 0x00000030 (Unmappable address)  at 0x00575651 in TS3W.exe  stack: TS3W.exe |
