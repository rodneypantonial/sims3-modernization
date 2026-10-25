# Queue sv-cause-run 2026-10-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/sv-cause-run-2026-10-24.jsonl`.

## 2026-10-24T07:54 UTC, build d7455e8c, jobs sv-cause-run

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-cause-run | 07:54 (15:54) | 47.1 | PASS | PASS: profile 45 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2116.8/1807.3/1548.9 -> 2251.9/2002.3/1371.3 | asyncStarted +21221, asyncRefused +792, asyncCollected +21193, asyncDropped +22, asyncUs +212760291, asyncMaxUs +486349, asyncPumpUs +133809322, asyncPumpWaits +17697, asyncRefusedM1Pending +96, asyncSkipped +6, asyncBackoffs +10, asyncQueueFull +2, asyncQueueMax +4 | noise: medium (other apps 24.7% of game CPUs); others on game CPUs 24.7%; clock avg/min 4159/4106 MHz; v0.1.6 cause run (docs/183 §, PLAN post-release item 4): SV 45 min seeded natural play on the release defaults (master build 0.36.39) with script stacks on every sync plan >= 20 ms (LogFallbackStack=16), own-job sim wait lines (OwnJobWait=1) and HitchAttr holders, to name the callers of the Forei |

### sv-cause-run: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 45 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
