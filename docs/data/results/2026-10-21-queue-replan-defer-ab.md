# Queue replan-defer-ab 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/replan-defer-ab-2026-10-21.jsonl`.

## 2026-10-21T11:56 UTC, build 02b4324, jobs bp-rdf-crowd-off, bp-rdf-crowd-on

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rdf-crowd-off | 11:57 (19:57) | 10.1 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2137.4/1854.6/1548.8 -> 2354.6/2064.5/1189.8 | asyncStarted +10302, asyncRefused +200, asyncCollected +10300, asyncUs +53194959, asyncMaxUs +284236, asyncPumpUs +32501809, asyncPumpWaits +7249, asyncRefusedM1Pending +33, asyncSkipped +2, asyncBackoffs +6, asyncQueueFull +22, asyncQueueMax +6 | noise: high (other apps 18.5% of game CPUs); others on game CPUs 18.5%; clock avg/min 4154/4102 MHz; docs/141 §6 A/B, arm OFF: repro replan-buildbuy-crowd (20 spawned Sims + household routing onto the home lot and to a far lot, 4 build/buy enter/exit cycles while they route) with ts3fixes replandefer and replanspread off. ts3native 0.36.13 (game-copy/ts3native.asi.0313, as freeze-fix-ab) with ts3f |
| bp-rdf-crowd-on | 12:07 (20:07) | 10 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2220.7/1838.6/1446.2 -> 2390.7/2117.1/1144.7 | asyncStarted +10312, asyncRefused +233, asyncCollected +10309, asyncUs +58817326, asyncMaxUs +272560, asyncPumpUs +37508087, asyncPumpWaits +7186, asyncRefusedM1Pending +20, asyncSkipped +3, asyncBackoffs +6, asyncQueueFull +48, asyncQueueMax +5 | noise: high (other apps 16.3% of game CPUs); others on game CPUs 16.3%; clock avg/min 4160/4137 MHz; docs/141 §6 A/B, arm ON: as bp-rdf-crowd-off with replandefer (door/elevator replans go to the Sim's own ReplanRouteInterruptAction when the route is being followed) and replanspread (OnBuildBuyExit / OnRoutingDataChanged replayed 4 routing Sims a sim tick) on. Expect: replanspread_held > 0 at ever |

### bp-rdf-crowd-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rdf-crowd-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
