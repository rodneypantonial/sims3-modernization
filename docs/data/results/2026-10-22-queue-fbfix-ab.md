# Queue fbfix-ab 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/fbfix-ab-2026-10-22.jsonl`.

## 2026-10-22T05:00 UTC, build 48c803c, jobs fbfix-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| fbfix-verify | 05:00 (13:00) | 6.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2219.3/1853.4/1492.9 -> 2214.5/2028.1/1348.9 | asyncStarted +7848, asyncRefused +84, asyncCollected +7846, asyncDropped +2, asyncUs +40257431, asyncMaxUs +1982481, asyncPumpUs +22122572, asyncPumpWaits +4239, asyncRefusedM1Pending +20, asyncBackoffs +5, asyncQueueMax +6 | noise: high (other apps 15.2% of game CPUs); others on game CPUs 15.2%; clock avg/min 4125/4073 MHz; Day job, 5 min, noisy machine OK (mechanism only). BP rare-route-bits: fbjobcap-ab 2026-10-22 had a 1,948 ms foreign job here capped only after 1,942 ms (32 world A* nodes). Cap on for the whole run; [RouteEarlyOut] Mode=1 (observe only) adds the world A* share of each slow plan (LogMs 200). PASS ( |

### fbfix-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T09:36 UTC, build 9dfaed44, jobs sv-fbfix-play-offfirst, sv-fbfix-play-onfirst, sv-fbfix-crowd-offfirst, sv-fbfix-crowd-onfirst, bp-fbfix-routebits-offfirst, bp-fbfix-routebits-onfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-fbfix-play-offfirst | 09:36 (17:36) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2178.1/1840.6/1540.9 -> 2221.2/2058.5/1370.3 | asyncStarted +8054, asyncRefused +226, asyncCollected +8046, asyncDropped +7, asyncUs +61656161, asyncMaxUs +361752, asyncPumpUs +37254212, asyncPumpWaits +7160, asyncRefusedM1Pending +33, asyncSkipped +1, asyncBackoffs +6, asyncQueueMax +6 | noise: medium (other apps 7.3% of game CPUs); others on game CPUs 7.3%; clock avg/min 4223/4135 MHz; Clean night. SV play, --ab fbjobcap off-first (warm-up on, then off,on,on,off). fbjobcap-ab 2026-10-22: 1,432 / 7,755 / 7,874 ms foreign jobs, all in cap-off slices. Read with sv-fbfix-play-onfirst. |
| sv-fbfix-play-onfirst | 09:47 (17:47) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2143.6/1804.3/1564.9 -> 2187.2/1998.8/1397.1 | asyncStarted +8162, asyncRefused +213, asyncCollected +8152, asyncDropped +10, asyncUs +63661772, asyncMaxUs +1008010, asyncPumpUs +39166443, asyncPumpWaits +6255, asyncRefusedM1Pending +29, asyncBackoffs +6, asyncQueueMax +8 | noise: medium (other apps 8.5% of game CPUs); others on game CPUs 8.5%; clock avg/min 4202/4108 MHz; Clean night. SV play, --ab fbjobcap on-first (warm-up on, then on,off,off,on). Read with sv-fbfix-play-offfirst. |
| sv-fbfix-crowd-offfirst | 09:59 (17:59) | 31.6 | **FAIL** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: stutter profiler still running 90 s past -t 600; killed (fail-fast guard)<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2137.3/1801.7/1564.9 -> 2095.8/1826.6/1544.9 | asyncStarted +277, asyncRefused +12, asyncCollected +277, asyncUs +1710336, asyncMaxUs +44212, asyncPumpUs +909091, asyncPumpWaits +223, asyncQueueMax +2 | noise: medium (other apps 5.0% of game CPUs); others on game CPUs 5.0%; clock avg/min 4194/4164 MHz; Clean night. SV rare-big-crowd (freeze-data: 7,361 + 7,350 ms NoStart plans; with FallbackAsync on they become foreign jobs), --ab fbjobcap off-first. Read with sv-fbfix-crowd-onfirst. |
| sv-fbfix-crowd-onfirst | 10:31 (18:31) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2121.1/1786.4/1580.9 -> 2131.4/1954.2/1395.4 | asyncStarted +5540, asyncRefused +147, asyncCollected +5539, asyncDropped +1, asyncUs +42217454, asyncMaxUs +199140, asyncPumpUs +24363906, asyncPumpWaits +5979, asyncRefusedM1Pending +23, asyncBackoffs +6, asyncQueueMax +11 | noise: high (other apps 13.4% of game CPUs); others on game CPUs 13.4%; clock avg/min 4204/4148 MHz; Clean night. SV rare-big-crowd, --ab fbjobcap on-first. Read with sv-fbfix-crowd-offfirst. |
| bp-fbfix-routebits-offfirst | 10:43 (18:43) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2183.7/1851.8/1524.9 -> 2221.9/2080.2/1301.1 | asyncStarted +8580, asyncRefused +81, asyncCollected +8578, asyncDropped +2, asyncUs +40358248, asyncMaxUs +1864671, asyncPumpUs +21635930, asyncPumpWaits +3839, asyncRefusedM1Pending +27, asyncBackoffs +8, asyncQueueMax +5 | noise: medium (other apps 8.1% of game CPUs); others on game CPUs 8.1%; clock avg/min 4233/4173 MHz; Clean night. BP rare-route-bits, --ab fbjobcap off-first. Read with bp-fbfix-routebits-onfirst. |
| bp-fbfix-routebits-onfirst | 10:54 (18:54) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2156.4/1836.6/1540.9 -> 2207.7/2046.6/1349.1 | asyncStarted +8945, asyncRefused +97, asyncCollected +8943, asyncDropped +1, asyncUs +48108001, asyncMaxUs +3877739, asyncPumpUs +26247297, asyncPumpWaits +5081, asyncRefusedM1Pending +21, asyncSkipped +1, asyncBackoffs +7, asyncQueueMax +5 | noise: high (other apps 19.0% of game CPUs); others on game CPUs 19.0%; clock avg/min 4184/4097 MHz; Clean night. BP rare-route-bits, --ab fbjobcap on-first. Read with bp-fbfix-routebits-offfirst. |

### sv-fbfix-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-fbfix-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-fbfix-crowd-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-fbfix-crowd-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-fbfix-routebits-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-fbfix-routebits-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T11:30 UTC, build a9787072, jobs sv-fbfix-crowd-offfirst, sv-fbfix-crowd-onfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-22T11:51 UTC, build 2d3f870e, jobs sv-fbfix-crowd-offfirst, sv-fbfix-crowd-onfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
