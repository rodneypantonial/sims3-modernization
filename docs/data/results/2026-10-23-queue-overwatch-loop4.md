# Queue overwatch-loop4 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/overwatch-loop4-2026-10-23.jsonl`.

## 2026-10-23T02:58 UTC, build f25b5cba, jobs bp-overwatch-ab-loop4

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-overwatch-ab-loop4 | 02:58 (10:58) | 4.7 | PASS | PASS: profile stopped at 20 events after 1.5 min (off 10, on 10; 20 firings, 5 cycles; rule 10 events per arm, >= 10 firings per arm), cap 22 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2135.7/1857.4/1548.9 -> 2264.3/1864.1/1426.8 | asyncStarted +529, asyncRefused +15, asyncCollected +529, asyncUs +8214923, asyncMaxUs +93021, asyncPumpUs +3017127, asyncPumpWaits +368, asyncRefusedM1Pending +5, asyncQueueMax +2 | noise: medium (other apps 9.3% of game CPUs); others on game CPUs 9.3%; clock avg/min 4182/4145 MHz; Rerun of repro bp-overwatch-ab-loop2 (arm + fire, ABBA, until 10 firings per arm, loop cap 20 min) with ts3fixes overwatchchain v2. Pass: every on-arm firing ran as a chain and no iteration holds a >= 100 ms run of the chain task (hitchTasks cap rule; the off arm runs in the trigger's own task, not |

### bp-overwatch-ab-loop4: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 22 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
