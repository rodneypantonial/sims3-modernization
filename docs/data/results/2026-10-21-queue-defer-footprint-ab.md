# Queue defer-footprint-ab 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/defer-footprint-ab-2026-10-21.jsonl`.

## 2026-10-21T10:25 UTC, build 02b4324, jobs bp-wr-deferfp, bp-wr-deferfp-onfirst, ip-wr-deferfp

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-wr-deferfp | 10:25 (18:25) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2246/1854.7/1484.9 -> 2270.3/2079.5/1269.1 | asyncStarted +14124, asyncRefused +248, asyncCollected +14118, asyncDropped +3, asyncUs +70457228, asyncMaxUs +167127, asyncPumpUs +42027384, asyncPumpWaits +8943, asyncRefusedM1Pending +33, asyncSkipped +1, asyncBackoffs +10, asyncQueueFull +4, asyncQueueMax +4 | noise: high (other apps 10.4% of game CPUs); others on game CPUs 10.4%; clock avg/min 4202/4151 MHz; docs/89 design U A/B (counters v74, [Discourage] DeferFootprint=1, built 2026-10-21 on the 0.36.9 base, offline only so far). Base = release v0.1.4 ini defaults ([RouteAsync] Enabled/Mode 1 Kinds 0, [RouteGate] Enabled/Sites/Enforce 1, [Discourage] Enabled 1; RouteSnap off, P5c FreeLock off: P5c is |
| bp-wr-deferfp-onfirst | 10:37 (18:37) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2215.1/1846.9/1484.9 -> 2268.8/2107.4/1277.1 | asyncStarted +14589, asyncRefused +261, asyncCollected +14579, asyncDropped +5, asyncUs +71323348, asyncMaxUs +133822, asyncPumpUs +42845389, asyncPumpWaits +9029, asyncRefusedM1Pending +52, asyncSkipped +4, asyncBackoffs +27, asyncQueueFull +5, asyncQueueMax +6 | noise: medium (other apps 6.8% of game CPUs); others on game CPUs 6.8%; clock avg/min 4216/4152 MHz; docs/89 design U A/B (counters v74, [Discourage] DeferFootprint=1, built 2026-10-21 on the 0.36.9 base, offline only so far). Base = release v0.1.4 ini defaults ([RouteAsync] Enabled/Mode 1 Kinds 0, [RouteGate] Enabled/Sites/Enforce 1, [Discourage] Enabled 1; RouteSnap off, P5c FreeLock off: P5c is |
| ip-wr-deferfp | 10:49 (18:49) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2238.5/1855.5/1460.9 -> 2175.1/1869.5/1476.9 | asyncStarted +6306, asyncRefused +151, asyncCollected +6294, asyncDropped +11, asyncUs +65160357, asyncMaxUs +362043, asyncPumpUs +46348879, asyncPumpWaits +4941, asyncRefusedM1Pending +41, asyncBackoffs +7, asyncQueueFull +8, asyncQueueMax +4 | noise: medium (other apps 6.7% of game CPUs); others on game CPUs 6.7%; clock avg/min 4208/4173 MHz; docs/89 design U A/B (counters v74, [Discourage] DeferFootprint=1, built 2026-10-21 on the 0.36.9 base, offline only so far). Base = release v0.1.4 ini defaults ([RouteAsync] Enabled/Mode 1 Kinds 0, [RouteGate] Enabled/Sites/Enforce 1, [Discourage] Enabled 1; RouteSnap off, P5c FreeLock off: P5c is |

### bp-wr-deferfp: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-wr-deferfp-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-wr-deferfp: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
