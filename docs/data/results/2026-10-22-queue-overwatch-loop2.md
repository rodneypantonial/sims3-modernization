# Queue overwatch-loop2 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/overwatch-loop2-2026-10-22.jsonl`.

## 2026-10-22T19:32 UTC, build f22871a3, jobs bp-overwatch-ab-loop2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-overwatch-ab-loop2 | 19:32 (03:32) | 3.3 | PASS | PASS: profile stopped at 20 events after 1.5 min (off 10, on 10; 20 firings, 5 cycles; rule 10 events per arm), cap 22 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2167.9/1858.8/1540.7 -> 2156.5/1849.6/1523.1 | asyncStarted +346, asyncRefused +4, asyncCollected +346, asyncUs +5978839, asyncMaxUs +101716, asyncPumpUs +2554011, asyncPumpWaits +276, asyncRefusedM1Pending +5, asyncQueueMax +3 | noise: high (other apps 10.3% of game CPUs); others on game CPUs 10.3%; clock avg/min 4257/4185 MHz; Overwatch nightly cleanup A/B in one load, repro bp-overwatch-ab-loop2 (loop: arm + fire, ABBA, until 10 firings per arm, loop cap 20 min; the window ends there). Row keeps "stopped at N events after M min". Read: node tools/harness/hitch-ab.ts --scenario <tag> (rows reflect_call off / reflect_call |

### bp-overwatch-ab-loop2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 22 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
