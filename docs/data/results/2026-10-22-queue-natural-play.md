# Queue natural-play 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/natural-play-2026-10-22.jsonl`.

## 2026-10-22T22:15 UTC, build a6f1bae6, jobs bp-natural-30, ip-natural-30, sv-natural-30

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-natural-30 | 22:15 (06:15) | 31.7 | PASS | PASS: profile 30 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166.8/1847.5/1524.9 -> 2406.9/2160.5/1195.7 | asyncStarted +38848, asyncRefused +483, asyncCollected +38836, asyncDropped +10, asyncUs +294387823, asyncMaxUs +282360, asyncPumpUs +159788742, asyncPumpWaits +28876, asyncRefusedM1Pending +149, asyncSkipped +1, asyncBackoffs +21, asyncQueueMax +5 | noise: medium (other apps 7.9% of game CPUs); others on game CPUs 7.9%; clock avg/min 4249/4201 MHz; Bridgeport, 30 min natural play (autopilot, camera untouched), no repro, measurement settings of freeze-measure.json (asi.0329 all-on set, [Harness] ForegroundRender=1, [HitchAttr], RouteJobPhases + SyncPlans, LogFallbackMs 20 / stack 16, profile.noSamplers, frames.csv kept loaded, per-task attribu |
| ip-natural-30 | 22:47 (06:47) | 31.7 | PASS | PASS: profile 30 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2168.9/1855.6/1508.9 -> 2206.5/1907.4/1475.2 | asyncStarted +19945, asyncRefused +211, asyncCollected +19929, asyncDropped +16, asyncUs +213351966, asyncMaxUs +750709, asyncPumpUs +130078313, asyncPumpWaits +16456, asyncRefusedM1Pending +98, asyncBackoffs +8, asyncQueueMax +8 | noise: medium (other apps 8.2% of game CPUs); others on game CPUs 8.2%; clock avg/min 4247/4100 MHz; Isla Paradiso, 30 min natural play (autopilot, camera untouched), no repro, measurement settings of freeze-measure.json (asi.0329 all-on set, [Harness] ForegroundRender=1, [HitchAttr], RouteJobPhases + SyncPlans, LogFallbackMs 20 / stack 16, profile.noSamplers, frames.csv kept loaded, per-task attr |
| sv-natural-30 | 23:19 (07:19) | 31.8 | PASS | PASS: profile 30 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2104.9/1791.4/1580.9 -> 2230.5/2009/1379.4 | asyncStarted +19447, asyncRefused +373, asyncCollected +19431, asyncDropped +13, asyncUs +193442352, asyncMaxUs +2211016, asyncPumpUs +117123380, asyncPumpWaits +15826, asyncRefusedM1Pending +77, asyncSkipped +3, asyncBackoffs +4, asyncQueueMax +5 | noise: high (other apps 10.9% of game CPUs); others on game CPUs 10.9%; clock avg/min 4214/4138 MHz; Sunset Valley, 30 min natural play (autopilot, camera untouched), no repro, measurement settings of freeze-measure.json (asi.0329 all-on set, [Harness] ForegroundRender=1, [HitchAttr], RouteJobPhases + SyncPlans, LogFallbackMs 20 / stack 16, profile.noSamplers, frames.csv kept loaded, per-task attr |

### bp-natural-30: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 30 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-natural-30: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 30 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-natural-30: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 30 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
