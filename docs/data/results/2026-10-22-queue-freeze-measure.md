# Queue freeze-measure 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/freeze-measure-2026-10-22.jsonl`.

## 2026-10-22T11:38 UTC, build 2d3f870e, jobs bp-freeze-pan

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-freeze-pan | 11:38 (19:38) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2167.5/1839.9/1516.9 -> 2319/2064.8/1275.8 | asyncStarted +7510, asyncRefused +120, asyncCollected +7509, asyncUs +61085300, asyncMaxUs +206981, asyncPumpUs +30009840, asyncPumpWaits +12116, asyncRefusedM1Pending +14, asyncBackoffs +4, asyncQueueMax +4 | noise: high (other apps 19.2% of game CPUs); others on game CPUs 19.2%; clock avg/min 4203/4163 MHz; Visible freezes + camera stutter (docs/162), Bridgeport, same all-on set and asi as sim-budget.json; slow scripted camera pan (--camera-pan: 8 lots, 120 s per pass); frames.csv (frame-times.js, default on), freezes.jsonl and nativelog.jsonl. 10 min seeded profile with both samplers. Read: node tool |

### bp-freeze-pan: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T12:04 UTC, build be1e91d0, jobs bp-freeze-pan

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-freeze-pan | 12:04 (20:04) | 11.7 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 12:15:54 (TS3W.exe process gone) | 1 | 2165.5/1839.5/1516.9 -> ? | - | noise: high (other apps 14.2% of game CPUs); others on game CPUs 14.2%; clock avg/min 4226/4189 MHz; Visible freezes + camera stutter (docs/162), Bridgeport, same all-on set and asi as sim-budget.json; slow scripted camera pan (--camera-pan: 8 lots, 120 s per pass); frames.csv (frame-times.js, default on), freezes.jsonl and nativelog.jsonl. 10 min seeded profile with both samplers. Read: node tool |

## 2026-10-22T12:31 UTC, build 79ec7db8, jobs bp-freeze-pan

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-freeze-pan | 12:31 (20:31) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2193.1/1848.1/1524.9 -> 2314.7/2093.6/1278.2 | asyncStarted +9308, asyncRefused +100, asyncCollected +9305, asyncDropped +1, asyncUs +72541266, asyncMaxUs +261941, asyncPumpUs +36210547, asyncPumpWaits +19709, asyncRefusedM1Pending +26, asyncBackoffs +5, asyncQueueMax +7 | noise: medium (other apps 9.0% of game CPUs); others on game CPUs 9.0%; clock avg/min 4261/4159 MHz; Visible freezes + camera stutter (docs/162), Bridgeport, same all-on set and asi as sim-budget.json; slow scripted camera pan (--camera-pan: 8 lots, 120 s per pass); frames.csv (frame-times.js, default on), freezes.jsonl and nativelog.jsonl. 10 min seeded profile with both samplers. Read: node tool |

### bp-freeze-pan: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T12:44 UTC, build 6e97d4d4, jobs bp-freeze-pan-mainstack

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-freeze-pan-mainstack | 12:44 (20:44) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2163.4/1848.7/1532.9 -> 2321.7/2096.1/1267.6 | asyncStarted +9310, asyncRefused +113, asyncCollected +9309, asyncUs +82506718, asyncMaxUs +271547, asyncPumpUs +41276752, asyncPumpWaits +18518, asyncRefusedM1Pending +35, asyncSkipped +1, asyncBackoffs +2, asyncQueueMax +4 | noise: high (other apps 10.5% of game CPUs); others on game CPUs 10.5%; clock avg/min 4256/4175 MHz; Cause run for slow render frames: same as bp-freeze-pan plus main-thread stacks every 200 ms (main thread suspended per sample: read causes only, not FPS). |

### bp-freeze-pan-mainstack: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T13:06 UTC, build 29ee5176, jobs bp-freeze-pan

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-22T13:07 UTC, build 29ee5176, jobs bp-freeze-pan

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-freeze-pan | 13:07 (21:07) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2172.3/1847.7/1532.9 -> 2443.9/2135.3/1256.9 | asyncStarted +9350, asyncRefused +134, asyncCollected +9346, asyncDropped +3, asyncUs +73955691, asyncMaxUs +176700, asyncPumpUs +41712816, asyncPumpWaits +17790, asyncRefusedM1Pending +21, asyncSkipped +1, asyncBackoffs +7, asyncQueueMax +4 | noise: high (other apps 11.7% of game CPUs); others on game CPUs 11.7%; clock avg/min 4281/4215 MHz; Visible freezes + camera stutter (docs/162), Bridgeport, same all-on set and asi as sim-budget.json; slow scripted camera pan (--camera-pan: 8 lots, 120 s per pass); frames.csv (frame-times.js, default on), freezes.jsonl and nativelog.jsonl. 10 min seeded profile with both samplers. Read: node tool |

### bp-freeze-pan: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T13:27 UTC, build 13cdd2e1, jobs bp-freeze-pan

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-freeze-pan | 13:27 (21:27) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2176/1859/1532.9 -> 2348.3/2097.1/1249.1 | asyncStarted +8900, asyncRefused +117, asyncCollected +8899, asyncDropped +1, asyncUs +70641893, asyncMaxUs +261237, asyncPumpUs +38662670, asyncPumpWaits +16339, asyncRefusedM1Pending +19, asyncBackoffs +4, asyncQueueMax +5 | noise: high (other apps 13.0% of game CPUs); others on game CPUs 13.0%; clock avg/min 4287/4175 MHz; Visible freezes + camera stutter (docs/162), Bridgeport, same all-on set and asi as sim-budget.json; slow scripted camera pan (--camera-pan: 8 lots, 120 s per pass); frames.csv (frame-times.js, default on), freezes.jsonl and nativelog.jsonl. 10 min seeded profile with both samplers. Read: node tool |

### bp-freeze-pan: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T13:40 UTC, build 7b66bdad, jobs bp-freeze-pan

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-freeze-pan | 13:40 (21:40) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2167.2/1856.2/1532.9 -> 2371.7/2127/1239 | asyncStarted +9784, asyncRefused +127, asyncCollected +9783, asyncDropped +1, asyncUs +74985422, asyncMaxUs +185307, asyncPumpUs +42132885, asyncPumpWaits +16242, asyncRefusedM1Pending +41, asyncBackoffs +3, asyncQueueMax +4 | noise: medium (other apps 9.7% of game CPUs); others on game CPUs 9.7%; clock avg/min 4287/4220 MHz; Visible freezes + camera stutter (docs/162), Bridgeport, same all-on set and asi as sim-budget.json; slow scripted camera pan (--camera-pan: 8 lots, 120 s per pass); frames.csv (frame-times.js, default on), freezes.jsonl and nativelog.jsonl. 10 min seeded profile with both samplers. Read: node tool |

### bp-freeze-pan: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
