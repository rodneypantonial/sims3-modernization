# Queue earlyout-ab 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/earlyout-ab-2026-10-22.jsonl`.

## 2026-10-22T05:25 UTC, build 48c803c, jobs bp-earlyout-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-earlyout-verify | 05:25 (13:25) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2218.8/1849.6/1508.9 -> 2332.9/2045.6/1230.3 | asyncStarted +8070, asyncRefused +78, asyncCollected +8069, asyncDropped +1, asyncUs +40820074, asyncMaxUs +4094372, asyncPumpUs +24776236, asyncPumpWaits +4487, asyncRefusedM1Pending +17, asyncBackoffs +3, asyncQueueMax +7 | noise: high (other apps 10.1% of game CPUs); others on game CPUs 10.1%; clock avg/min 4126/4083 MHz; Day job, 5 min, noisy machine OK (mechanism only). BP rare-route-bits: its forced car-only plan to the farthest community lot (step 51, FailedBecauseNoPathToDestination) floods the street graph once per goal-lot entry candidate (docs/142 §1, 4,060 ms in freeze-data 2026-10-20); every mode-2 call th |

### bp-earlyout-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T08:26 UTC, build 314296f6, jobs bp-earlyout-routebits-offfirst, bp-earlyout-routebits-onfirst, sv-earlyout-crowd-offfirst, sv-earlyout-crowd-onfirst, ip-earlyout-play-offfirst, ip-earlyout-play-onfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-earlyout-routebits-offfirst | 08:26 (16:26) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2168.3/1853/1516.9 -> 2280.6/2050.1/1293.5 | asyncStarted +8577, asyncRefused +74, asyncCollected +8572, asyncDropped +1, asyncUs +43580516, asyncMaxUs +2037575, asyncPumpUs +23514438, asyncPumpWaits +4296, asyncRefusedM1Pending +24, asyncSkipped +4, asyncBackoffs +3, asyncQueueMax +5 | noise: high (other apps 22.4% of game CPUs); others on game CPUs 22.4%; clock avg/min 4210/4156 MHz; Clean night. BP rare-route-bits, --ab earlyout off-first (warm-up on, then off,on,on,off). Read with bp-earlyout-routebits-onfirst. |
| bp-earlyout-routebits-onfirst | 08:37 (16:37) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2158.9/1846.7/1516.9 -> 2262.1/2058.4/1285.5 | asyncStarted +9203, asyncRefused +73, asyncCollected +9200, asyncDropped +2, asyncUs +44274638, asyncMaxUs +1927108, asyncPumpUs +24149411, asyncPumpWaits +5043, asyncRefusedM1Pending +13, asyncSkipped +1, asyncBackoffs +10, asyncQueueMax +6 | noise: high (other apps 13.0% of game CPUs); others on game CPUs 13.0%; clock avg/min 4228/4138 MHz; Clean night. BP rare-route-bits, --ab earlyout on-first (warm-up on, then on,off,off,on). Read with bp-earlyout-routebits-offfirst. |
| sv-earlyout-crowd-offfirst | 08:49 (16:49) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2110.4/1793.3/1580.9 -> 2209.5/2018.7/1330.5 | asyncStarted +5341, asyncRefused +137, asyncCollected +5340, asyncDropped +1, asyncUs +51695621, asyncMaxUs +2710085, asyncPumpUs +31899315, asyncPumpWaits +6006, asyncRefusedM1Pending +10, asyncBackoffs +6, asyncQueueMax +22 | noise: high (other apps 16.1% of game CPUs); others on game CPUs 16.1%; clock avg/min 4217/4129 MHz; Clean night. SV rare-big-crowd (freeze-data 2026-10-20: 7,361 + 7,350 ms NoStart plans, unattributed), --ab earlyout off-first. Memo proofs matter here (pedestrian candidates cut off by walls the colours do not see). Read with sv-earlyout-crowd-onfirst. |
| sv-earlyout-crowd-onfirst | 09:01 (17:01) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2149.5/1787.8/1580.9 -> 2126.4/1965.4/1429.1 | asyncStarted +5805, asyncRefused +152, asyncCollected +5800, asyncDropped +5, asyncUs +44157010, asyncMaxUs +339734, asyncPumpUs +24823124, asyncPumpWaits +5537, asyncRefusedM1Pending +43, asyncBackoffs +1, asyncQueueMax +12 | noise: high (other apps 13.4% of game CPUs); others on game CPUs 13.4%; clock avg/min 4217/4073 MHz; Clean night. SV rare-big-crowd, --ab earlyout on-first. Read with sv-earlyout-crowd-offfirst. |
| ip-earlyout-play-offfirst | 09:12 (17:12) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2179/1853.3/1516.9 -> 2129.8/1869.7/1516.9 | asyncStarted +7446, asyncRefused +101, asyncCollected +7434, asyncDropped +12, asyncUs +70586186, asyncMaxUs +331741, asyncPumpUs +48560915, asyncPumpWaits +6203, asyncRefusedM1Pending +31, asyncBackoffs +8, asyncQueueMax +7 | noise: medium (other apps 7.6% of game CPUs); others on game CPUs 7.6%; clock avg/min 4235/4191 MHz; Clean night. IP play (no repro: ordinary routing load, boats and water links), --ab earlyout off-first. Read with ip-earlyout-play-onfirst. |
| ip-earlyout-play-onfirst | 09:24 (17:24) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2182/1838.2/1516.9 -> 2156.2/1853.7/1500.9 | asyncStarted +6881, asyncRefused +81, asyncCollected +6870, asyncDropped +11, asyncUs +69363465, asyncMaxUs +311081, asyncPumpUs +48326071, asyncPumpWaits +5401, asyncRefusedM1Pending +57, asyncBackoffs +3, asyncQueueMax +7 | noise: high (other apps 11.1% of game CPUs); others on game CPUs 11.1%; clock avg/min 4228/4156 MHz; Clean night. IP play, --ab earlyout on-first. Read with ip-earlyout-play-offfirst. |

### bp-earlyout-routebits-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-earlyout-routebits-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-earlyout-crowd-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-earlyout-crowd-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-earlyout-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-earlyout-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
