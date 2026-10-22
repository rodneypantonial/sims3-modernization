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
