# Queue freeze-crashrep.tmp 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/freeze-crashrep.tmp-2026-10-22.jsonl`.

## 2026-10-22T12:17 UTC, build be1e91d0, jobs bp-freeze-pan-crashrep

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-freeze-pan-crashrep | 12:17 (20:17) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2167.8/1852.2/1524.9 -> 2301.1/2099.5/1283.7 | asyncStarted +10868, asyncRefused +118, asyncCollected +10864, asyncDropped +1, asyncUs +79909537, asyncMaxUs +248891, asyncPumpUs +37259786, asyncPumpWaits +20131, asyncRefusedM1Pending +27, asyncSkipped +2, asyncBackoffs +7, asyncQueueMax +5 | noise: medium (other apps 8.0% of game CPUs); others on game CPUs 8.0%; clock avg/min 4263/4154 MHz; Visible freezes + camera stutter (docs/162), Bridgeport, same all-on set and asi as sim-budget.json; slow scripted camera pan (--camera-pan: 8 lots, 120 s per pass); frames.csv (frame-times.js, default on), freezes.jsonl and nativelog.jsonl. 10 min seeded profile with both samplers. Read: node tool |

### bp-freeze-pan-crashrep: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
