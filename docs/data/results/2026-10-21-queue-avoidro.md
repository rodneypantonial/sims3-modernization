# Queue avoidro 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/avoidro-2026-10-21.jsonl`.

## 2026-10-21T11:21 UTC, build 02b4324, jobs bp-avoidro-shadow, bp-wr-steer-avoid, bp-wr-steer-avoid-onfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-avoidro-shadow | 11:21 (19:21) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2313.5/2034.7/1348.3 -> 2506/2326.6/1005.8 | asyncStarted +14354, asyncRefused +268, asyncCollected +14245, asyncDropped +3, asyncUs +84317172, asyncMaxUs +197402, asyncPumpUs +54959189, asyncPumpWaits +11623, asyncRefusedM1Pending +75, asyncSkipped +106, asyncBackoffs +97, asyncQueueFull +8, asyncQueueMax +6 | noise: medium (other apps 7.3% of game CPUs); others on game CPUs 7.3%; clock avg/min 4208/4169 MHz; [AvoidRO] shadow (docs/137 §5, counters v77) with S2's child-footprint whitelist (0.37, docs/84 §3.6): base as wait-reducers-ab bp-wr-steerro (async + gates + D, RouteSnap replay Mode=2 with ReleaseLock/ReleaseGates/FreeLock, HitchAttr), S2 Mode=1 Shadow=1 (answers during jobs, compares with no job |
| bp-wr-steer-avoid | 11:32 (19:32) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2277.6/2012.9/1370.5 -> 2362.3/2308.2/1114.7 | asyncStarted +14312, asyncRefused +250, asyncCollected +14211, asyncDropped +4, asyncUs +86276218, asyncMaxUs +270574, asyncPumpUs +54868756, asyncPumpWaits +12449, asyncRefusedM1Pending +64, asyncSkipped +97, asyncBackoffs +109, asyncQueueFull +2, asyncQueueMax +6 | noise: medium (other apps 6.8% of game CPUs); others on game CPUs 6.8%; clock avg/min 4216/4192 MHz; [AvoidRO] A/B, off-first (docs/137 §5, docs/136 §5): base as bp-avoidro-shadow but S2 Mode=1 Shadow=0 in both halves, AvoidRO Mode=1 Shadow=0 switched by --ab avoidro (2-min slices: warm-up on, then off,on,on,off). Run only after bp-avoidro-shadow passed; clean night (timing). Target key 0x846D21 ' |
| bp-wr-steer-avoid-onfirst | 11:44 (19:44) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2282.2/2002.3/1377.1 -> 2498.5/2273.6/1043.6 | asyncStarted +13070, asyncRefused +241, asyncCollected +12964, asyncUs +83559399, asyncMaxUs +238245, asyncPumpUs +54783143, asyncPumpWaits +10924, asyncRefusedM1Pending +63, asyncSkipped +105, asyncBackoffs +107, asyncQueueFull +1, asyncQueueMax +6 | noise: high (other apps 14.7% of game CPUs); others on game CPUs 14.7%; clock avg/min 4177/4124 MHz; [AvoidRO] A/B, on-first: the same job as bp-wr-steer-avoid with the slice order swapped (--ab-on-first: warm-up on, then on,off,off,on), so the heavy first counted slot (docs/136 §2) lands on the on side here and on the off side there. Pass as bp-wr-steer-avoid; read the two together (mean of the p |

### bp-avoidro-shadow: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-wr-steer-avoid: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-wr-steer-avoid-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
