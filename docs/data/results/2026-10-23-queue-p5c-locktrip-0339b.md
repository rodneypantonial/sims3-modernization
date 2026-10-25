# Queue p5c-locktrip-0339b 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/p5c-locktrip-0339b-2026-10-23.jsonl`.

## 2026-10-23T09:00 UTC, build 58551f08, jobs bp-p5c-recs-locktrip-0339b

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-p5c-recs-locktrip-0339b | 09:00 (17:00) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2204/2021.9/1412.6 -> 2403.8/2263.4/1119 | asyncStarted +9720, asyncRefused +98, asyncCollected +9682, asyncDropped +3, asyncUs +57271995, asyncMaxUs +175336, asyncPumpUs +27407770, asyncPumpWaits +7886, asyncRefusedM1Pending +30, asyncSkipped +30, asyncBackoffs +1, asyncQueueMax +8 | noise: high (other apps 13.1% of game CPUs); others on game CPUs 13.1%; clock avg/min 4255/4198 MHz; Job 2 + FreeLock=1: every plain sim +0x918 acquire is admitted in the window, destroys park one reference (with DestroyPark=1 also outside windows), a recorded write after the cut makes the record time skew (M5, stale true) instead of a refusal. PASS: job 2's, plus p5cJobs > 0, p5cAcq > 0, p5cParke |

### bp-p5c-recs-locktrip-0339b: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
