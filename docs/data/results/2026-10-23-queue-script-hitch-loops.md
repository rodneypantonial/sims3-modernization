# Queue script-hitch-loops 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/script-hitch-loops-2026-10-23.jsonl`.

## 2026-10-23T01:03 UTC, build 7d043ee6, jobs bp-stuckcheck-loop

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-stuckcheck-loop | 01:03 (09:03) | 6.6 | PASS | PASS: profile stopped at 240 events after 3.2 min (off 120, on 120; 20 firings, 5 cycles; rule 120 events per arm, >= 10 firings per arm), cap 12 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2190.3/1835.7/1532.9 -> 2176.9/1880.8/1467.3 | asyncStarted +6207, asyncRefused +35, asyncCollected +6206, asyncDropped +1, asyncUs +50943169, asyncMaxUs +215394, asyncPumpUs +23933618, asyncPumpWaits +3086, asyncRefusedM1Pending +14, asyncQueueMax +3 | noise: high (other apps 10.8% of game CPUs); others on game CPUs 10.8%; clock avg/min 4212/4156 MHz; docs/169: hitch-stuckcheck-loop, 10 firings per arm in one load. Read: hitch-ab.ts --scenario <tag>; clean night only. |

### bp-stuckcheck-loop: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 12 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
