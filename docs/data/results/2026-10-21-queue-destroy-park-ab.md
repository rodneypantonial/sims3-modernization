# Queue destroy-park-ab 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/destroy-park-ab-2026-10-21.jsonl`.

## 2026-10-21T09:48 UTC, build 02b4324, jobs bp-wr-destroypark, bp-wr-destroypark-onfirst, ip-wr-destroypark

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-wr-destroypark | 09:48 (17:48) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2219.7/1852.1/1446.2 -> 2264.4/2082.4/1294.2 | asyncStarted +14260, asyncRefused +236, asyncCollected +14255, asyncDropped +4, asyncUs +74189139, asyncMaxUs +195362, asyncPumpUs +46528755, asyncPumpWaits +9001, asyncRefusedM1Pending +49, asyncSkipped +1, asyncBackoffs +13, asyncQueueFull +4, asyncQueueMax +6 | noise: high (other apps 10.3% of game CPUs); others on game CPUs 10.3%; clock avg/min 4184/4142 MHz; docs/88 design G standalone A/B (counters v76, [Discourage] DestroyPark=1, built 2026-10-21 on the 0.36.9 base, offline only so far; revived from the unmerged 0.30.0 branch 14a25ef onto P5c's graveyard). Base = release v0.1.4 ini defaults ([RouteAsync] Enabled/Mode 1 Kinds 0, [RouteGate] Enabled/Si |
| bp-wr-destroypark-onfirst | 10:01 (18:01) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2225.4/1856.2/1484.9 -> 2254.8/2091.2/1309.1 | asyncStarted +14822, asyncRefused +246, asyncCollected +14818, asyncDropped +3, asyncUs +73896188, asyncMaxUs +198716, asyncPumpUs +46198524, asyncPumpWaits +9768, asyncRefusedM1Pending +51, asyncSkipped +1, asyncBackoffs +28, asyncQueueFull +7, asyncQueueMax +5 | noise: medium (other apps 7.5% of game CPUs); others on game CPUs 7.5%; clock avg/min 4222/4149 MHz; docs/88 design G standalone A/B (counters v76, [Discourage] DestroyPark=1, built 2026-10-21 on the 0.36.9 base, offline only so far; revived from the unmerged 0.30.0 branch 14a25ef onto P5c's graveyard). Base = release v0.1.4 ini defaults ([RouteAsync] Enabled/Mode 1 Kinds 0, [RouteGate] Enabled/Si |
| ip-wr-destroypark | 10:13 (18:13) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2236.7/1848.2/1468.9 -> 2157.3/1865.4/1468.9 | asyncStarted +5348, asyncRefused +131, asyncCollected +5342, asyncDropped +6, asyncUs +61605518, asyncMaxUs +466256, asyncPumpUs +45006392, asyncPumpWaits +4570, asyncRefusedM1Pending +36, asyncBackoffs +1, asyncQueueMax +6 | noise: high (other apps 10.4% of game CPUs); others on game CPUs 10.4%; clock avg/min 4195/4150 MHz; docs/88 design G standalone A/B (counters v76, [Discourage] DestroyPark=1, built 2026-10-21 on the 0.36.9 base, offline only so far; revived from the unmerged 0.30.0 branch 14a25ef onto P5c's graveyard). Base = release v0.1.4 ini defaults ([RouteAsync] Enabled/Mode 1 Kinds 0, [RouteGate] Enabled/Si |

### bp-wr-destroypark: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-wr-destroypark-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-wr-destroypark: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
