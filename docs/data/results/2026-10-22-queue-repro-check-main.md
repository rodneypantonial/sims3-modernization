# Queue repro-check-main 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/repro-check-main-2026-10-22.jsonl`.

## 2026-10-22T17:17 UTC, build d5a86a52, jobs bp-main-cas, bp-main-lots, bp-main-swarm

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-main-cas | 17:17 (01:17) | 6.6 | **FAIL** | FAIL: profile 5 min, seed 12345 (rng-compare.ts)<br>FAIL: frame lead: CAS/SimService led 12 frames > 50 ms (need 20)<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2131.9/1849.1/1548.9 -> 2266.7/2073.4/1317.6 | asyncStarted +7129, asyncRefused +108, asyncCollected +7124, asyncDropped +1, asyncUs +47896983, asyncMaxUs +237774, asyncPumpUs +24221104, asyncPumpWaits +6973, lotAdds +426, lotAddUs +5397, lotAddMaxUs -105, lotAddObjs +41902, lotRemoves +13, lotRemoveUs +749, lotRemoveMaxUs +115, pumpCalls +9993, pumpUs +683457, pumpMaxUs -44589, pumpLotPasses +419, lotScoreCalls +17703, lotScoreUs +244758, lot | noise: high (other apps 13.8% of game CPUs); others on game CPUs 13.8%; clock avg/min 4247/4174 MHz; Main-thread hitch repro correctness check (5 min, docs/169 main thread): CAS/SimService: camera jumps to populated lots, Sims spawned in view, forced outfit changes. Repro main-cas-builds (repros.json) in a profile-world --repro window with the measurement tool settings of tool-check.json (asi.0330 |
| bp-main-lots | 17:24 (01:24) | 6.7 | **FAIL** | FAIL: profile 5 min, seed 12345 (rng-compare.ts)<br>FAIL: frame lead: World/Service led 17 frames > 50 ms (need 20)<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2195.4/1859.1/1524.9 -> 2332.3/2078.5/1299.7 | asyncStarted +6925, asyncRefused +110, asyncCollected +6923, asyncDropped +2, asyncUs +47807623, asyncMaxUs +205241, asyncPumpUs +25694856, asyncPumpWaits +5407, lotAdds +198, lotAddUs +2624, lotAddMaxUs -99, lotAddObjs +22120, lotRemoves +4, lotRemoveUs +405, lotRemoveMaxUs +125, pumpCalls +12687, pumpUs +494131, pumpMaxUs -65658, pumpLotPasses +197, lotScoreCalls +17855, lotScoreUs +222906, lotS | noise: medium (other apps 9.7% of game CPUs); others on game CPUs 9.7%; clock avg/min 4246/4207 MHz; Main-thread hitch repro correctness check (5 min, docs/169 main thread): World/Service: camera jumps across far lots (lot streaming). Repro main-lot-stream (repros.json) in a profile-world --repro window with the measurement tool settings of tool-check.json (asi.0330, frames.csv with the services s |
| bp-main-swarm | 17:31 (01:31) | 6.6 | **FAIL** | FAIL: profile 5 min, seed 12345 (rng-compare.ts)<br>FAIL: frame lead: SwarmSystem led 2 frames > 50 ms (need 10)<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2168.2/1850/1532.9 -> 2305.6/2094.2/1279.6 | asyncStarted +5983, asyncRefused +74, asyncCollected +5972, asyncDropped +8, asyncUs +45367643, asyncMaxUs +154361, asyncPumpUs +24048504, asyncPumpWaits +4373, lotAdds +481, lotAddUs +7659, lotAddMaxUs -191, lotAddObjs +55095, lotRemoves +13, lotRemoveUs +2078, lotRemoveMaxUs +1014, pumpCalls +10638, pumpUs +530633, pumpMaxUs -39917, pumpLotPasses +473, lotScoreCalls +17441, lotScoreUs +235222, l | noise: medium (other apps 7.5% of game CPUs); others on game CPUs 7.5%; clock avg/min 4256/4210 MHz; Main-thread hitch repro correctness check (5 min, docs/169 main thread): SwarmSystem: thunderstorm + fire/magic effects re-created in view. Repro main-swarm-fx (repros.json) in a profile-world --repro window with the measurement tool settings of tool-check.json (asi.0330, frames.csv with the servic |

### bp-main-cas: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-main-lots: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-main-swarm: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T18:23 UTC, build f22871a3, jobs bp-main-cas, bp-main-lots

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-main-cas | 18:23 (02:23) | 6.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166.4/1848/1540.7 -> 2333.6/2103.9/1274.2 | asyncStarted +6982, asyncRefused +76, asyncCollected +6978, asyncDropped +3, asyncUs +45245794, asyncMaxUs +160529, asyncPumpUs +24105595, asyncPumpWaits +5641, lotAdds +170, lotAddUs +2199, lotAddMaxUs -163, lotAddObjs +19591, lotRemoves +3, lotRemoveUs +278, lotRemoveMaxUs +122, pumpCalls +11192, pumpUs +659188, pumpMaxUs -42820, pumpLotPasses +169, lotScoreCalls +17888, lotScoreUs +217804, lotS | noise: medium (other apps 7.0% of game CPUs); others on game CPUs 7.0%; clock avg/min 4251/4215 MHz; Main-thread hitch repro correctness check (5 min, docs/169 main thread): CAS/SimService: camera jumps to populated lots, Sims spawned in view, forced outfit changes. Repro main-cas-builds (repros.json) in a profile-world --repro window with the measurement tool settings of tool-check.json (asi.0330 |
| bp-main-lots | 18:29 (02:29) | 6.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2159.6/1849/1524.9 -> 2226.7/2064.8/1331.4 | asyncStarted +6642, asyncRefused +109, asyncCollected +6638, asyncUs +47382574, asyncMaxUs +186310, asyncPumpUs +25141462, asyncPumpWaits +5600, lotAdds +186, lotAddUs +2564, lotAddMaxUs -79, lotAddObjs +20080, lotRemoves +5, lotRemoveUs +451, lotRemoveMaxUs +248, pumpCalls +12407, pumpUs +469505, pumpMaxUs -44754, pumpLotPasses +182, lotScoreCalls +18075, lotScoreUs +211163, lotScoreChanged +5, l | noise: medium (other apps 7.5% of game CPUs); others on game CPUs 7.5%; clock avg/min 4242/4214 MHz; Main-thread hitch repro correctness check (5 min, docs/169 main thread): World/Service: camera jumps across far lots (lot streaming). Repro main-lot-stream (repros.json) in a profile-world --repro window with the measurement tool settings of tool-check.json (asi.0330, frames.csv with the services s |

### bp-main-cas: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-main-lots: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
