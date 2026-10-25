# Queue refcount-guard 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/refcount-guard-2026-10-23.jsonl`.

## 2026-10-23T06:27 UTC, build 5f74322f, jobs sv-refcount-guard-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-refcount-guard-verify | 06:27 (14:27) | 10.4 | PASS | PASS: profile stopped at 8 events after 6.9 min (off 4, on 4; 8 firings, 2 cycles; rule 3 events per arm, >= 3 firings per arm), cap 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired | 0 | 2217.1/1806.8/1462.2 -> 2181.9/2008.7/1323.5 | asyncStarted +7074, asyncRefused +142, asyncCollected +7060, asyncDropped +4, asyncUs +98684067, asyncMaxUs +9263014, asyncPumpUs +28074513, asyncPumpWaits +10285, asyncRefusedM1Pending +40, asyncSkipped +9, asyncBackoffs +2, asyncQueueMax +19 | noise: high (other apps 11.9% of game CPUs); others on game CPUs 11.9%; clock avg/min 4154/4091 MHz; Day (correctness, not timing): diner-crowd-loop until 3 firings (cap 8 min) on SV with the ip-locktaker-ab settings plus RefcountGuard=1 Mode 1. Pass: no crash, the OK lines, rcReady 1, kept levels on the steering callers (rcDeferred > 0, rcByFn_8 RouteFollow_A or rcByFn_11 Locomotion_ObstacleCheck |

### sv-refcount-guard-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
