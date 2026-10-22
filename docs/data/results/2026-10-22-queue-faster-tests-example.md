# Queue faster-tests-example 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/faster-tests-example-2026-10-22.jsonl`.

## 2026-10-22T15:47 UTC, build a213b69d, jobs bp-scen-multi, bp-natural-1, bp-natural-2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-scen-multi | 15:47 (23:47) | 2.8 | **FAIL** | FAIL: scen-lotswitch: profile 10 min, first window after load<br>FAIL: repro step sim_visit_lot failed: no vehicle: own_vehicle first<br>STOP: scen-lotswitch failed: 2 repros not run (daytime rule; TS3_NIGHT=1 or reuseSession.onFail "restart" reloads and goes on)<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2173.3/1856.7/1524.9 -> 2165.6/1880.3/1524.9 | asyncStarted +1818, asyncRefused +31, asyncCollected +1816, asyncUs +11691230, asyncMaxUs +88906, asyncPumpUs +6306717, asyncPumpWaits +1370, asyncRefusedM1Pending +21, asyncSkipped +1, asyncBackoffs +1, asyncQueueMax +3 | noise: medium (other apps 6.7% of game CPUs); others on game CPUs 6.7%; clock avg/min 4272/4227 MHz; scenario repros lotswitch, buildbuy, cas back to back in one load; each its own tag <job tag>-<repro id>; read: hitch-ab.ts --scenario <job tag> |

### bp-scen-multi: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

Stopped: repro scen-lotswitch of bp-scen-multi failed (daytime rule).

## 2026-10-22T18:36 UTC, build f22871a3, jobs bp-scen-multi, bp-natural-1, bp-natural-2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-scen-multi | 18:36 (02:36) | 33 | PASS | PASS: scen-lotswitch: profile 10 min, first window after load<br>PASS: scen-buildbuy: profile 10 min, first window after load<br>PASS: scen-cas: profile 8 min, first window after load<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2171.8/1859.1/1524.9 -> 2305.1/2056.4/1243.6 | asyncStarted +8393, asyncRefused +127, asyncCollected +8392, asyncDropped +1, asyncUs +57847084, asyncMaxUs +144750, asyncPumpUs +31929971, asyncPumpWaits +8347, asyncRefusedM1Pending +29, asyncBackoffs +4, asyncQueueMax +5 | fresh load; drift: Sims 91 -> 106 (limit +-8); sim time +60.4 h since load (limit 48 h); 1 stuck Sims (Sims 106 (+15), members 3, lot objects 114 (+4), commit +171 MB, largest free 1243.6 MB, sim +60.4 h, stuck 1, window 1 since load); fresh start for bp-natural-1; noise: medium (other apps 7.7% of game CPUs); others on game CPUs 7.7%; clock avg/min 4262/4178 MHz; scenario repros lotswitch, buildb |
| bp-natural-1 | 19:09 (03:09) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166.2/1844.6/1532.9 -> 2336.3/2092.3/1283.4 | asyncStarted +12868, asyncRefused +185, asyncCollected +12863, asyncDropped +2, asyncUs +91481331, asyncMaxUs +204852, asyncPumpUs +50634919, asyncPumpWaits +10747, asyncRefusedM1Pending +56, asyncBackoffs +4, asyncQueueMax +5 | fresh load; drift: lot objects 110 -> 116 (limit +-5); sim time +96.1 h since load (limit 48 h) (Sims 86 (-5), members 3, lot objects 116 (+6), commit +170.29999999999973 MB, largest free 1283.4 MB, sim +96.1 h, stuck 0, window 1 since load); fresh start for bp-natural-2; noise: medium (other apps 7.5% of game CPUs); others on game CPUs 7.5%; clock avg/min 4256/4172 MHz; natural play 10 min in the |
| bp-natural-2 | 19:21 (03:21) | 11.2 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 19:31:10 (agent not answering for 60 s (heartbeat frozen))<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2168.6/1855.7/1532.9 -> 2337.7/2113/1280.7 | asyncStarted +11473, asyncRefused +164, asyncCollected +11471, asyncDropped +2, asyncUs +78587489, asyncMaxUs +208850, asyncPumpUs +42233332, asyncPumpWaits +10277, asyncRefusedM1Pending +39, asyncBackoffs +7, asyncQueueMax +4 | noise: medium (other apps 6.9% of game CPUs); others on game CPUs 6.9%; clock avg/min 4259/4223 MHz; the same with legreplanskip off (a runtime switch: no restart); window 5 of the load = maxJobs, so the job after it would start fresh; 2026-10-22T12:15:54.230Z  ?  at ?  stack: - |

### bp-scen-multi: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-natural-1: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-natural-2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
