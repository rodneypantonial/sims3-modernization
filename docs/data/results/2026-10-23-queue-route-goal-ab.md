# Queue route-goal-ab 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/route-goal-ab-2026-10-23.jsonl`.

## 2026-10-23T03:04 UTC, build a0a4c59b, jobs ip-goal-split-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-goal-split-verify | 03:04 (11:04) | 9.7 | **FAIL** | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: pgReady=0 (want >=15); ggReady=0 (want >=1) | 0 | 2155.6/1853.9/1516.9 -> 2231.5/2023.8/1337.6 | asyncStarted +4771, asyncRefused +55, asyncCollected +4768, asyncDropped +3, asyncUs +57101608, asyncMaxUs +493163, asyncPumpUs +35747690, asyncPumpWaits +6226, asyncRefusedM1Pending +13, asyncQueueMax +4 | noise: high (other apps 13.8% of game CPUs); others on game CPUs 13.8%; clock avg/min 4165/4119 MHz; Day (correctness, not timing), 8 min natural IP play: P-G on (GoalSplit), D1 in shadow (Mode 2: both rings run, arguments and validation answers compared), H2Ic off. Pass: no crash, both OK lines, pgReady 15, validations counted (pgN_3 > 0), duplicates seen (ggDup > 0) with ggArgMismatch 0; read gg |

### ip-goal-split-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
