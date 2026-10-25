# Queue hotfix-016-gate 2026-10-25

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/hotfix-016-gate-2026-10-25.jsonl`.

## 2026-10-25T00:10 UTC, build d2c0ad42, jobs bp-hf-verify-userdiag, bp-hf-matrix, ip-hf-matrix, bp-hf-travel-france, bp-hf-saver-real, ip-day13-hf-save, ip-hf-smoke

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-hf-verify-userdiag | 00:10 (08:10) | 6.9 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired<br>PASS: no forbidden log lines | 0 | 2157.5/1835.3/1516.9 -> 2345.1/2104.8/1211.8 | - | noise: high (other apps 21.3% of game CPUs); others on game CPUs 21.3%; clock avg/min 4190/4171 MHz; Gate (a) userdiag verify (docs/188): one Bridgeport load, release ini, one agent save to UserDiag-Scratch (repro userdiag-save). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first line "ts3native 0.36.37a", no FAIL line, no unscrubbed \Users\<name> path, no save that ends with a non |
| bp-hf-matrix | 00:17 (08:17) | 8.8 | PASS | control: PASS<br>overwrite: PASS<br>back-to-back: PASS<br>unpaused: PASS<br>== reload: paused: save all off, then all on; load each (two restarts): households match, 60 s at speed 3, game alive; FAIL only on ScriptErrors with ON that OFF does not have<br>reload: PENDING (reload commands above)<br>summary: 4 cases, 4 passed, 0 failed, pending: reload<br>reload on: reload: PENDING (reload commands a | 0 | 2234.3/1874.1/1500.9 -> 2173.6/1899.1/1484.9 | - | noise: high (other apps 20.4% of game CPUs); others on game CPUs 20.4%; clock avg/min 4217/4171 MHz; Gate (b) core save matrix in release mode (save-matrix.ts --fixes shipped --cases control,overwrite,back-to-back,unpaused,reload: save-as to new names, manual save over the same name with backup, two saves back to back, save while the game runs, save then reload). The save path changed (new [UserDi |
| ip-hf-matrix | 00:25 (08:25) | 8.5 | **FAIL** | control: PASS<br>overwrite: PASS<br>back-to-back: PASS<br>unpaused: PASS<br>== reload: paused: save all off, then all on; load each (two restarts): households match, 60 s at speed 3, game alive; FAIL only on ScriptErrors with ON that OFF does not have<br>reload: PENDING (reload commands above)<br>summary: 4 cases, 4 passed, 0 failed, pending: reload<br>reload on: reload: PENDING (reload commands a | 1 | 2158.2/1833.4/1516.9 -> 2146.6/1818.4/1516.9 | - | noise: high (other apps 27.5% of game CPUs); others on game CPUs 27.5%; clock avg/min 4198/4172 MHz; Gate (b) core save matrix in release mode (save-matrix.ts --fixes shipped --cases control,overwrite,back-to-back,unpaused,reload: save-as to new names, manual save over the same name with backup, two saves back to back, save while the game runs, save then reload). The save path changed (new [UserDi |
| bp-hf-travel-france | 00:34 (08:34) | 4.7 | PASS | travel: PASS<br>summary: 1 cases, 1 passed, 0 failed<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: no forbidden log lines | 0 | 2188.3/1835.9/1532.9 -> 2217.9/1876.7/1500.9 | - | noise: high (other apps 26.2% of game CPUs); others on game CPUs 26.2%; clock avg/min 4203/4155 MHz; Gate (b) travel save: Bridgeport -> france -> home with saves at start, away and home (as release-validate bp-rel-travel-france). Pass: travel case PASS (left, arrived, saved, back home, save loads). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first line "ts3native 0.36.37a", no FA |
| bp-hf-saver-real | 00:39 (08:39) | 15.5 | **FAIL** | PASS: profile stopped at 8 events after 12.2 min (off 0, on 8; 8 firings, 8 cycles; rule 8 events), cap 25 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: log /^save: end #8 result 0 / 0 lines (want >= 1)<br>PASS: no forbidden log lines | 0 | 2168.2/1847.2/1500.9 -> 2385.1/2176.9/1163.8 | - | noise: high (other apps 25.3% of game CPUs); others on game CPUs 25.3%; clock avg/min 4193/4154 MHz; Gate (b) NRaas Saver autosave: the real NRaas_Saver.package V22 installed for this job only (fixes.install nraas-saver), 2 real-minute timer, 30+ Sims routing, until 8 Saver saves (repro saver-real-autosave-crowd). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first line "ts3native 0 |
| ip-day13-hf-save | 00:54 (08:54) | 1.9 | PASS | unpaused: PASS<br>summary: 1 cases, 1 passed, 0 failed<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: no forbidden log lines | 0 | 2184.5/1855.5/1484.9 -> 2207.9/1891.9/1484.9 | - | noise: high (other apps 23.5% of game CPUs); others on game CPUs 23.5%; clock avg/min 4208/4163 MHz; Gate (b) large world, one save: save-matrix.ts --fixes shipped --cases unpaused (one save while the game runs) on Isla Paradiso 13 sim days in. The save path changed (new [UserDiag] markers in the save hooks, call-site hooks 0xAAC2F3 / 0xAAC34A): save-change test matrix rule, every case PASS; CHANG |
| ip-hf-smoke | 00:56 (08:56) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired<br>PASS: no forbidden log lines | 0 | 2190.3/1840.1/1500.9 -> 2251/2028.7/1312.6 | asyncStarted +2629, asyncRefused +42, asyncCollected +2629, asyncUs +34484828, asyncMaxUs +259218, asyncPumpUs +24065230, asyncPumpWaits +5485, asyncRefusedM1Pending +8, asyncBackoffs +1, asyncQueueMax +4 | noise: high (other apps 16.8% of game CPUs); others on game CPUs 16.8%; clock avg/min 4210/4148 MHz; Gate (c) smoke: 6 min play with camera pans, release ini + measurement logging (as v0.1.5 ip-rc-smoke-v015pkg). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first line "ts3native 0.36.37a", no FAIL line, no unscrubbed \Users\<name> path, no save that ends with a non-zero result, no  |

### bp-hf-verify-userdiag: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-hf-matrix: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-hf-matrix: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-hf-travel-france: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-hf-saver-real: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-day13-hf-save: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-hf-smoke: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
