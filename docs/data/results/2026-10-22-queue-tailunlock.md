# Queue tailunlock 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/tailunlock-2026-10-22.jsonl`.

## 2026-10-22T07:14 UTC, build 5ce8c377, jobs bp-tailunlock-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-tailunlock-verify | 07:14 (15:14) | 4.8 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2223.1/1850.7/1508.7 -> 2325.3/1899.8/1389.9 | asyncStarted +3201, asyncRefused +42, asyncCollected +3201, asyncUs +18281192, asyncMaxUs +273037, asyncPumpUs +10489028, asyncPumpWaits +2034, asyncRefusedM1Pending +8, asyncBackoffs +1, asyncQueueMax +4 | noise: high (other apps 36.3% of game CPUs, NOT comparable (load or clock)); others on game CPUs 36.3%; clock avg/min 3999/3579 MHz; Daytime correctness, noisy machine OK (no timing read). ts3native 0.36.23 + counters v89 (game-copy/ts3native.asi.tailunlock; sha256 in PLAN.md) run from a checkout whose native/ts3native/ts3native.ini is master (release defaults): the job ini adds only [RouteAsync]  |

### bp-tailunlock-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
