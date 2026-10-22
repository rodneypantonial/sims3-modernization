# Queue clean-night-0.2.0 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/clean-night-0.2.0-2026-10-21.jsonl`.

## 2026-10-21T04:50 UTC, build 3b2b5b2, jobs bp-fb-play-offfirst, bp-fb-play-onfirst, sv-fb-play-offfirst, sv-fb-play-onfirst, ip-fb-play-offfirst, ip-fb-play-onfirst, sv-fb-crowd-off, sv-fb-crowd-on, bp-fb-routebits-on, bp-fb-routebits-off

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-fb-play-offfirst | 04:50 (12:50) | 3.2 | **FAIL** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: sim clock stuck at day 15 13:41 for ~60 s (speed Triple; fail-fast guard)<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2134.6/1853.8/1532.9 -> 2141.4/1844/1548.9 | asyncStarted +424, asyncRefused +10, asyncCollected +423, asyncUs +2417501, asyncMaxUs +62490, asyncPumpUs +1449653, asyncPumpWaits +427, asyncRefusedM1Pending +8, asyncQueueMax +3 | noise: high (other apps 19.4% of game CPUs); others on game CPUs 19.4%; clock avg/min 4207/4190 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-fb-play-onfirst | 04:54 (12:54) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2131.2/1852/1462.2 -> 2270/2057.4/1302.2 | asyncStarted +16782, asyncRefused +204, asyncCollected +16776, asyncDropped +6, asyncUs +84231558, asyncMaxUs +210810, asyncPumpUs +50069055, asyncPumpWaits +10790, asyncRefusedM1Pending +59, asyncBackoffs +15, asyncQueueFull +5, asyncQueueMax +5 | noise: high (other apps 12.9% of game CPUs); others on game CPUs 12.9%; clock avg/min 4143/4092 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| sv-fb-play-offfirst | 05:06 (13:06) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2068.1/1791.5/1580.9 -> 2188.6/1984.1/1419.4 | asyncStarted +7805, asyncRefused +222, asyncCollected +7793, asyncDropped +11, asyncUs +62928308, asyncMaxUs +1016822, asyncPumpUs +36308582, asyncPumpWaits +7021, asyncRefusedM1Pending +43, asyncSkipped +1, asyncBackoffs +3, asyncQueueFull +2, asyncQueueMax +7 | noise: medium (other apps 5.2% of game CPUs); others on game CPUs 5.2%; clock avg/min 4198/4136 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| sv-fb-play-onfirst | 05:18 (13:18) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2208.3/1806.8/1500.9 -> 2213.7/2011/1356.9 | asyncStarted +7421, asyncRefused +232, asyncCollected +7415, asyncDropped +6, asyncUs +57797653, asyncMaxUs +521598, asyncPumpUs +35961195, asyncPumpWaits +6569, asyncRefusedM1Pending +42, asyncBackoffs +4, asyncQueueMax +5 | noise: medium (other apps 5.0% of game CPUs); others on game CPUs 5.0%; clock avg/min 4222/4156 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| ip-fb-play-offfirst | 05:30 (13:30) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2217.4/1853.1/1468.9 -> 2151.1/1871.9/1468.9 | asyncStarted +6297, asyncRefused +113, asyncCollected +6283, asyncDropped +14, asyncUs +72197267, asyncMaxUs +347754, asyncPumpUs +50620251, asyncPumpWaits +5190, asyncRefusedM1Pending +38, asyncBackoffs +4, asyncQueueMax +12 | noise: medium (other apps 6.5% of game CPUs); others on game CPUs 6.5%; clock avg/min 4202/4160 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| ip-fb-play-onfirst | 05:42 (13:42) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2232.2/1849.2/1452.9 -> 2173.5/1864.7/1452.9 | asyncStarted +6484, asyncRefused +92, asyncCollected +6477, asyncDropped +7, asyncUs +68941614, asyncMaxUs +480938, asyncPumpUs +46389783, asyncPumpWaits +5081, asyncRefusedM1Pending +46, asyncBackoffs +3, asyncQueueMax +11 | noise: high (other apps 11.5% of game CPUs); others on game CPUs 11.5%; clock avg/min 4182/4122 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| sv-fb-crowd-off | 05:53 (13:53) | 9.7 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2190.1/1803.4/1500.9 -> 2133.8/1980.6/1341.1 | asyncStarted +4490, asyncRefused +281, asyncCollected +4490, asyncUs +33810683, asyncMaxUs +176031, asyncPumpUs +19418734, asyncPumpWaits +4315, asyncRefusedM1Pending +28, asyncBackoffs +3, asyncQueueFull +101, asyncQueueMax +3 | noise: medium (other apps 8.4% of game CPUs); others on game CPUs 8.4%; clock avg/min 4179/4155 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| sv-fb-crowd-on | 06:03 (14:03) | 9.7 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2204.4/1803.8/1516.9 -> 2150.1/2005/1357.1 | asyncStarted +5901, asyncRefused +141, asyncCollected +5901, asyncUs +46799196, asyncMaxUs +351155, asyncPumpUs +28783491, asyncPumpWaits +5449, asyncRefusedM1Pending +6, asyncBackoffs +3, asyncQueueMax +16 | noise: medium (other apps 7.0% of game CPUs); others on game CPUs 7.0%; clock avg/min 4213/4154 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-fb-routebits-on | 06:13 (14:13) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2226.2/1850.8/1468.9 -> 2241.9/2080.8/1245.1 | asyncStarted +8649, asyncRefused +65, asyncCollected +8647, asyncDropped +2, asyncUs +37294493, asyncMaxUs +1836635, asyncPumpUs +20322839, asyncPumpWaits +4198, asyncRefusedM1Pending +22, asyncBackoffs +4, asyncQueueMax +6 | noise: medium (other apps 4.9% of game CPUs); others on game CPUs 4.9%; clock avg/min 4227/4159 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-fb-routebits-off | 06:21 (14:21) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2220.6/1857.4/1468.9 -> 2308.1/2173.8/1078.4 | asyncStarted +7443, asyncRefused +99, asyncCollected +7442, asyncUs +36693136, asyncMaxUs +151135, asyncPumpUs +17553803, asyncPumpWaits +4735, asyncRefusedM1Pending +16, asyncBackoffs +2, asyncQueueMax +5 | noise: medium (other apps 4.9% of game CPUs); others on game CPUs 4.9%; clock avg/min 4230/4178 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |

### bp-fb-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-fb-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-fb-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-fb-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-fb-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-fb-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-fb-crowd-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-fb-crowd-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-fb-routebits-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-fb-routebits-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-21T17:11 UTC, build e1729d1, jobs ip-slotro-offfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-21T17:29 UTC, build e1729d1, jobs ip-slotro-offfirst, ip-slotro-onfirst, sv-slotro-offfirst, sv-slotro-onfirst, bp-slotro-offfirst, bp-slotro-onfirst, bp-ut-play-offfirst, bp-ut-play-onfirst, ip-ut-play-offfirst, ip-ut-play-onfirst, bp-ut-crowd-offfirst, bp-ut-crowd-onfirst, bp-unseen-play-offfirst, bp-unseen-play-onfirst, sv-unseen-play-offfirst, sv-unseen-play-onfirst, ip-unseen-play-offfirst, ip-unseen-play-onfirst, bp-unseen-crowd-offfirst, bp-unseen-crowd-onfirst, bp-spreads-offfirst, bp-spreads-onfirst, sv-spreads-offfirst, sv-spreads-onfirst, bp-et-load-off, bp-et-load-on, bp-et-load-on-2, bp-et-load-off-2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-21T17:56 UTC, build e1729d1, jobs ip-slotro-offfirst, ip-slotro-onfirst, sv-slotro-offfirst, sv-slotro-onfirst, bp-slotro-offfirst, bp-slotro-onfirst, bp-ut-play-offfirst, bp-ut-play-onfirst, ip-ut-play-offfirst, ip-ut-play-onfirst, bp-ut-crowd-offfirst, bp-ut-crowd-onfirst, bp-unseen-play-offfirst, bp-unseen-play-onfirst, sv-unseen-play-offfirst, sv-unseen-play-onfirst, ip-unseen-play-offfirst, ip-unseen-play-onfirst, bp-unseen-crowd-offfirst, bp-unseen-crowd-onfirst, bp-spreads-offfirst, bp-spreads-onfirst, sv-spreads-offfirst, sv-spreads-onfirst, bp-et-load-off, bp-et-load-on, bp-et-load-on-2, bp-et-load-off-2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-21T18:21 UTC, build e1729d1, jobs ip-slotro-offfirst, ip-slotro-onfirst, sv-slotro-offfirst, sv-slotro-onfirst, bp-slotro-offfirst, bp-slotro-onfirst, bp-ut-play-offfirst, bp-ut-play-onfirst, ip-ut-play-offfirst, ip-ut-play-onfirst, bp-ut-crowd-offfirst, bp-ut-crowd-onfirst, bp-unseen-play-offfirst, bp-unseen-play-onfirst, sv-unseen-play-offfirst, sv-unseen-play-onfirst, ip-unseen-play-offfirst, ip-unseen-play-onfirst, bp-unseen-crowd-offfirst, bp-unseen-crowd-onfirst, bp-spreads-offfirst, bp-spreads-onfirst, sv-spreads-offfirst, sv-spreads-onfirst, bp-et-load-off, bp-et-load-on, bp-et-load-on-2, bp-et-load-off-2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-21T19:03 UTC, build e1729d1, jobs ip-slotro-offfirst, ip-slotro-onfirst, sv-slotro-offfirst, sv-slotro-onfirst, bp-slotro-offfirst, bp-slotro-onfirst, bp-ut-play-offfirst, bp-ut-play-onfirst, ip-ut-play-offfirst, ip-ut-play-onfirst, bp-ut-crowd-offfirst, bp-ut-crowd-onfirst, bp-unseen-play-offfirst, bp-unseen-play-onfirst, sv-unseen-play-offfirst, sv-unseen-play-onfirst, ip-unseen-play-offfirst, ip-unseen-play-onfirst, bp-unseen-crowd-offfirst, bp-unseen-crowd-onfirst, bp-spreads-offfirst, bp-spreads-onfirst, sv-spreads-offfirst, sv-spreads-onfirst, bp-et-load-off, bp-et-load-on, bp-et-load-on-2, bp-et-load-off-2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-slotro-offfirst | 19:03 (03:03) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2182.4/1857.7/1516.9 -> 2153.3/1874.6/1508.9 | - | noise: medium (other apps 6.8% of game CPUs); others on game CPUs 6.8%; clock avg/min 4241/4104 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| ip-slotro-onfirst | 19:15 (03:15) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2179.2/1850.1/1532.8 -> 2162.3/1866.4/1532.8 | - | noise: medium (other apps 4.6% of game CPUs); others on game CPUs 4.6%; clock avg/min 4244/4194 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| sv-slotro-offfirst | 19:27 (03:27) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2097/1790.6/1596.9 -> 2181.5/1988.2/1421.1 | - | noise: medium (other apps 5.7% of game CPUs); others on game CPUs 5.7%; clock avg/min 4252/4179 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| sv-slotro-onfirst | 19:39 (03:39) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2171.9/1843.2/1548.9 -> 2207/2044.6/1379.8 | - | noise: medium (other apps 6.7% of game CPUs); others on game CPUs 6.7%; clock avg/min 4249/4181 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-slotro-offfirst | 19:51 (03:51) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2232.3/1888.2/1516.9 -> 2387/2181.5/1239.4 | - | noise: medium (other apps 5.1% of game CPUs); others on game CPUs 5.1%; clock avg/min 4206/4051 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-slotro-onfirst | 20:03 (04:03) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2158/1850.9/1532.9 -> 2264.7/2079.7/1325.1 | - | noise: medium (other apps 4.5% of game CPUs); others on game CPUs 4.5%; clock avg/min 4219/4012 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-ut-play-offfirst | 20:15 (04:15) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2159.3/1834.8/1532.9 -> 2228/2052.8/1317.1 | - | noise: medium (other apps 4.9% of game CPUs); others on game CPUs 4.9%; clock avg/min 4254/4196 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-ut-play-onfirst | 20:27 (04:27) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2188.9/1851/1532.9 -> 2242/2089.9/1317.1 | - | noise: medium (other apps 4.9% of game CPUs); others on game CPUs 4.9%; clock avg/min 4238/4145 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| ip-ut-play-offfirst | 20:39 (04:39) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2164.2/1840.9/1548.9 -> 2150.3/1854.6/1516.9 | - | noise: medium (other apps 5.4% of game CPUs); others on game CPUs 5.4%; clock avg/min 4226/4136 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| ip-ut-play-onfirst | 20:51 (04:51) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2184.3/1839.1/1516.9 -> 2184.3/1871/1516.9 | - | noise: medium (other apps 7.0% of game CPUs); others on game CPUs 7.0%; clock avg/min 4248/4183 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-ut-crowd-offfirst | 21:03 (05:03) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2164.4/1857/1532.9 -> 2248.5/2047.4/1301.5 | - | noise: medium (other apps 5.1% of game CPUs); others on game CPUs 5.1%; clock avg/min 4243/4195 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-ut-crowd-onfirst | 21:15 (05:15) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2162.3/1849.4/1532.9 -> 2272.9/2073.9/1309.5 | - | noise: medium (other apps 6.0% of game CPUs); others on game CPUs 6.0%; clock avg/min 4252/4199 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-unseen-play-offfirst | 21:27 (05:27) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2191.1/1851.6/1532.9 -> 2241.8/2093.9/1311.8 | - | noise: medium (other apps 5.8% of game CPUs); others on game CPUs 5.8%; clock avg/min 4255/4184 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-unseen-play-onfirst | 21:39 (05:39) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2163.1/1835.2/1532.9 -> 2292.1/2073/1285.1 | - | noise: medium (other apps 5.2% of game CPUs); others on game CPUs 5.2%; clock avg/min 4251/4200 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| sv-unseen-play-offfirst | 21:51 (05:51) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2092.8/1786/1596.9 -> 2210.3/2004.2/1421.1 | - | noise: medium (other apps 4.2% of game CPUs); others on game CPUs 4.2%; clock avg/min 4243/4188 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| sv-unseen-play-onfirst | 22:03 (06:03) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2143.3/1789.7/1564.9 -> 2177.3/1999.2/1413.1 | - | noise: medium (other apps 5.6% of game CPUs); others on game CPUs 5.6%; clock avg/min 4248/4205 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| ip-unseen-play-offfirst | 22:15 (06:15) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2155.2/1838.5/1548.9 -> 2177.2/1845.4/1506.2 | - | noise: medium (other apps 4.9% of game CPUs); others on game CPUs 4.9%; clock avg/min 4232/4193 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| ip-unseen-play-onfirst | 22:27 (06:27) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2179.1/1835.5/1532.9 -> 2223.5/2012.1/1373.1 | - | noise: medium (other apps 6.6% of game CPUs); others on game CPUs 6.6%; clock avg/min 4238/4152 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-unseen-crowd-offfirst | 22:39 (06:39) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2178.7/1848.5/1532.9 -> 2261.1/2075.5/1285.5 | - | noise: medium (other apps 6.7% of game CPUs); others on game CPUs 6.7%; clock avg/min 4241/4133 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-unseen-crowd-onfirst | 22:51 (06:51) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2184.5/1851.3/1532.9 -> 2219.9/2064.7/1333.1 | - | noise: medium (other apps 5.2% of game CPUs); others on game CPUs 5.2%; clock avg/min 4225/4115 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-spreads-offfirst | 23:03 (07:03) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2162.8/1851.2/1532.9 -> 2253.2/2082.6/1325.1 | - | noise: medium (other apps 5.8% of game CPUs); others on game CPUs 5.8%; clock avg/min 4214/3978 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-spreads-onfirst | 23:15 (07:15) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2161.7/1854.4/1532.9 -> 2249.2/2063.2/1317.1 | - | noise: medium (other apps 5.5% of game CPUs); others on game CPUs 5.5%; clock avg/min 4231/4129 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| sv-spreads-offfirst | 23:27 (07:27) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2103.5/1807.2/1564.9 -> 2195.9/1995.6/1405.1 | - | noise: medium (other apps 5.6% of game CPUs); others on game CPUs 5.6%; clock avg/min 4233/4132 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| sv-spreads-onfirst | 23:39 (07:39) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2134.8/1786.1/1596.9 -> 2155.7/1961.4/1421.1 | - | noise: medium (other apps 6.0% of game CPUs); others on game CPUs 6.0%; clock avg/min 4251/4157 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-et-load-off | 23:50 (07:50) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2183/1849.4/1532.9 -> 2227.8/2042.9/1357.1 | - | noise: medium (other apps 5.4% of game CPUs); others on game CPUs 5.4%; clock avg/min 4243/4136 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-et-load-on | 23:57 (07:57) | 6.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2158.4/1828.5/1548.9 -> 2249.7/2039.2/1364.9 | - | noise: medium (other apps 5.1% of game CPUs); others on game CPUs 5.1%; clock avg/min 4240/4175 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-et-load-on-2 | 00:04 (08:04) | 6.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166.2/1851.9/1532.9 -> 2213.5/2057.3/1349.1 | - | noise: medium (other apps 4.8% of game CPUs); others on game CPUs 4.8%; clock avg/min 4226/4156 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |
| bp-et-load-off-2 | 00:11 (08:11) | 6.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2162.9/1833.9/1548.9 -> 2216.1/2038.1/1365.1 | - | noise: medium (other apps 6.1% of game CPUs); others on game CPUs 6.1%; clock avg/min 4248/4189 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |

### ip-slotro-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-slotro-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-slotro-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-slotro-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-slotro-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-slotro-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-ut-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-ut-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut-crowd-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut-crowd-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-unseen-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-unseen-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-unseen-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-unseen-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-unseen-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-unseen-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-unseen-crowd-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-unseen-crowd-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-spreads-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-spreads-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-spreads-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-spreads-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-et-load-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-et-load-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-et-load-on-2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-et-load-off-2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
