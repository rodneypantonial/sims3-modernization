# Queue pet-crash-confirm 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/pet-crash-confirm-2026-10-23.jsonl`.

## 2026-10-23T03:39 UTC, build c7075d20, jobs bp-pet-toddler-crash, bp-pet-in-build-mode

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-pet-toddler-crash | 03:39 (11:39) | 1.9 | **CRASH** | FAIL: profile 3 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 03:41:08 (TS3W.exe process gone) | 1 | 2131.9/1850.1/1468.9 -> ? | - | noise: high (other apps 18.7% of game CPUs); others on game CPUs 18.7%; clock avg/min 4189/4171 MHz; Expect CRASH at 0x575651 (the game's own Sim.MakePet with a Toddler Dog). No crash = cause refuted.; 2026-10-23T03:41:02.000Z  ACCESS_VIOLATION reading from address 0x00000030 (Unmappable address)  at 0x00575651 in TS3W.exe  stack: TS3W.exe |
| bp-pet-in-build-mode | 03:41 (11:41) | 4.7 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2136.6/1839.4/1468.9 -> 2327.6/2036.5/1254.2 | asyncStarted +3277, asyncRefused +42, asyncCollected +3275, asyncUs +22481390, asyncMaxUs +247783, asyncPumpUs +11228295, asyncPumpWaits +2643, asyncRefusedM1Pending +9, asyncBackoffs +1, asyncQueueMax +3 | noise: high (other apps 13.8% of game CPUs); others on game CPUs 13.8%; clock avg/min 4155/4112 MHz; Second candidate: pet made while in or leaving build mode. Expected: no crash. |

### bp-pet-in-build-mode: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
