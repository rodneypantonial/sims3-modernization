# Queue freeze-fix-ab 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/freeze-fix-ab-2026-10-20.jsonl`.

## 2026-10-20T23:03 UTC, build e46e324, jobs bp-ffx-play-off, bp-ffx-play-on, bp-ffx-rare-route-bits-off, bp-ffx-rare-route-bits-on, bp-ffx-rare-big-crowd-off, bp-ffx-rare-big-crowd-on, bp-ffx-rare-thunderstorm-off, bp-ffx-rare-thunderstorm-on, ip-ffx-rare-ip-water-off, ip-ffx-rare-ip-water-on, sv-ffx-rare-big-crowd-off, sv-ffx-rare-big-crowd-on, sv-ffx-rare-route-bits-off, sv-ffx-rare-route-bits-on

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ffx-play-off | 23:03 (07:03) | 13.9 | PASS | PASS: profile 12 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2182.3/1835.2/1500.9 -> 2322.1/2074.9/1245.5 | asyncStarted +16074, asyncRefused +254, asyncCollected +16066, asyncDropped +2, asyncUs +84150278, asyncMaxUs +215764, asyncPumpUs +50688440, asyncPumpWaits +10642, asyncRefusedM1Pending +48, asyncSkipped +3, asyncBackoffs +21, asyncQueueFull +2, asyncQueueMax +5 | noise: high (other apps 29.4% of game CPUs); others on game CPUs 29.4%; clock avg/min 4180/4155 MHz; docs/134 A/B, arm OFF: bp-frz-play (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START). Both arms: L |
| bp-ffx-play-on | 23:18 (07:18) | 13.8 | PASS | PASS: profile 12 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2171.4/1857.1/1500.9 -> 2266.3/2082.3/1293.1 | asyncStarted +22519, asyncRefused +290, asyncCollected +22517, asyncDropped +1, asyncUs +105943552, asyncMaxUs +206796, asyncPumpUs +57527156, asyncPumpWaits +13115, asyncRefusedM1Pending +61, asyncBackoffs +10, asyncQueueMax +7 | noise: high (other apps 26.1% of game CPUs); others on game CPUs 26.1%; clock avg/min 4166/4126 MHz; docs/134 A/B, arm ON: bp-frz-play (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START). Both arms: Lo |
| bp-ffx-rare-route-bits-off | 23:32 (07:32) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166.8/1852.5/1484.9 -> 2284.2/2038.9/1293.5 | asyncStarted +5766, asyncRefused +84, asyncCollected +5766, asyncUs +32346189, asyncMaxUs +170435, asyncPumpUs +17846142, asyncPumpWaits +3309, asyncRefusedM1Pending +6, asyncBackoffs +3, asyncQueueFull +3, asyncQueueMax +4 | noise: high (other apps 27.2% of game CPUs); others on game CPUs 27.2%; clock avg/min 4167/4127 MHz; docs/134 A/B, arm OFF: bp-frz-rare-route-bits (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START). B |
| bp-ffx-rare-route-bits-on | 23:40 (07:40) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2161.9/1841.2/1500.9 -> 2239.4/1896.1/1421.3 | asyncStarted +7269, asyncRefused +91, asyncCollected +7268, asyncDropped +1, asyncUs +41574696, asyncMaxUs +4637590, asyncPumpUs +25083065, asyncPumpWaits +4292, asyncRefusedM1Pending +21, asyncBackoffs +2, asyncQueueMax +6 | noise: high (other apps 32.3% of game CPUs); others on game CPUs 32.3%; clock avg/min 4174/4153 MHz; docs/134 A/B, arm ON: bp-frz-rare-route-bits (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START). Bo |
| bp-ffx-rare-big-crowd-off | 23:48 (07:48) | 9.8 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2161.7/1852.4/1468.9 -> 2296.8/2074.4/1245.5 | asyncStarted +7635, asyncRefused +214, asyncCollected +7634, asyncUs +49147480, asyncMaxUs +289359, asyncPumpUs +28020379, asyncPumpWaits +5853, asyncRefusedM1Pending +18, asyncBackoffs +13, asyncQueueFull +75, asyncQueueMax +5 | noise: high (other apps 36.3% of game CPUs); others on game CPUs 36.3%; clock avg/min 4183/4166 MHz; docs/134 A/B, arm OFF: bp-frz-rare-big-crowd (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START). Bo |
| bp-ffx-rare-big-crowd-on | 23:58 (07:58) | 10.1 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2160.4/1850.4/1484.9 -> 2296.8/2076.9/1261.5 | asyncStarted +11301, asyncRefused +122, asyncCollected +11297, asyncDropped +4, asyncUs +63610569, asyncMaxUs +308447, asyncPumpUs +35515341, asyncPumpWaits +7911, asyncRefusedM1Pending +21, asyncBackoffs +7, asyncQueueMax +7 | noise: high (other apps 35.0% of game CPUs); others on game CPUs 35.0%; clock avg/min 4159/4079 MHz; docs/134 A/B, arm ON: bp-frz-rare-big-crowd (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START). Bot |
| bp-ffx-rare-thunderstorm-off | 00:08 (08:08) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2163.2/1857.6/1468.9 -> 2294/2056/1261.5 | asyncStarted +5944, asyncRefused +142, asyncCollected +5932, asyncDropped +7, asyncUs +35119675, asyncMaxUs +202793, asyncPumpUs +19885107, asyncPumpWaits +3457, asyncRefusedM1Pending +75, asyncSkipped +5, asyncBackoffs +5, asyncQueueMax +4 | noise: high (other apps 29.0% of game CPUs); others on game CPUs 29.0%; clock avg/min 4168/4149 MHz; docs/134 A/B, arm OFF: bp-frz-rare-thunderstorm (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START). |
| bp-ffx-rare-thunderstorm-on | 00:16 (08:16) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2161/1850.1/1500.9 -> 2276.1/2067.4/1277.5 | asyncStarted +8504, asyncRefused +138, asyncCollected +8499, asyncDropped +5, asyncUs +42444546, asyncMaxUs +278793, asyncPumpUs +23009267, asyncPumpWaits +5190, asyncRefusedM1Pending +95, asyncBackoffs +5, asyncQueueMax +6 | noise: high (other apps 24.6% of game CPUs); others on game CPUs 24.6%; clock avg/min 4164/4142 MHz; docs/134 A/B, arm ON: bp-frz-rare-thunderstorm (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START).  |
| ip-ffx-rare-ip-water-off | 00:24 (08:24) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2167.2/1831.8/1500.9 -> 2154.8/1841/1485.1 | asyncStarted +2150, asyncRefused +38, asyncCollected +2147, asyncDropped +3, asyncUs +30009273, asyncMaxUs +322381, asyncPumpUs +22849175, asyncPumpWaits +1900, asyncRefusedM1Pending +8, asyncQueueMax +4 | noise: high (other apps 23.5% of game CPUs); others on game CPUs 23.5%; clock avg/min 4160/4125 MHz; docs/134 A/B, arm OFF: ip-frz-rare-ip-water (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START). Bot |
| ip-ffx-rare-ip-water-on | 00:32 (08:32) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2182.3/1852.4/1468.9 -> 2153.1/1986.3/1340.9 | asyncStarted +3358, asyncRefused +45, asyncCollected +3357, asyncDropped +1, asyncUs +34888985, asyncMaxUs +508636, asyncPumpUs +23692301, asyncPumpWaits +2774, asyncRefusedM1Pending +8, asyncBackoffs +3, asyncQueueMax +5 | noise: high (other apps 21.9% of game CPUs); others on game CPUs 21.9%; clock avg/min 4148/4114 MHz; docs/134 A/B, arm ON: ip-frz-rare-ip-water (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START). Both |
| sv-ffx-rare-big-crowd-off | 00:40 (08:40) | 9.8 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2104.3/1809.5/1516.9 -> 2093/1954.5/1372.9 | asyncStarted +4741, asyncRefused +206, asyncCollected +4740, asyncDropped +1, asyncUs +37678782, asyncMaxUs +373832, asyncPumpUs +21489514, asyncPumpWaits +4988, asyncRefusedM1Pending +4, asyncQueueFull +99, asyncQueueMax +4 | noise: high (other apps 21.9% of game CPUs); others on game CPUs 21.9%; clock avg/min 4173/4134 MHz; docs/134 A/B, arm OFF: sv-frz-rare-big-crowd (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START). Bo |
| sv-ffx-rare-big-crowd-on | 00:50 (08:50) | 9.8 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2134/1788.1/1548.9 -> 2147.5/1985.9/1373.1 | asyncStarted +5651, asyncRefused +140, asyncCollected +5651, asyncUs +42645736, asyncMaxUs +667997, asyncPumpUs +24619119, asyncPumpWaits +5545, asyncRefusedM1Pending +13, asyncBackoffs +3, asyncQueueMax +9 | noise: high (other apps 22.5% of game CPUs); others on game CPUs 22.5%; clock avg/min 4166/4133 MHz; docs/134 A/B, arm ON: sv-frz-rare-big-crowd (freeze-data.json, same save, seed 12345, same repro) on ts3native 0.36.7 (game-copy/ts3native.asi.0368 = native/ts3native/out/ts3native.asi of the 0.36.7 build, copied there by hand) with ts3fixes built from the same commit installed (foreign START). Bot |

Paused after sv-ffx-rare-big-crowd-on (AI usage 5h 31% (reset 09:00 PHT) | 7d 80% (reset 14:00 PHT) | read 0 min ago); resume with `--from sv-ffx-rare-route-bits-off`.

### bp-ffx-play-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 12 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ffx-play-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 12 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ffx-rare-route-bits-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ffx-rare-route-bits-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ffx-rare-big-crowd-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ffx-rare-big-crowd-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ffx-rare-thunderstorm-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ffx-rare-thunderstorm-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-ffx-rare-ip-water-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-ffx-rare-ip-water-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-ffx-rare-big-crowd-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-ffx-rare-big-crowd-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
