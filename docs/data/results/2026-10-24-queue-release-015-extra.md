# Queue release-015-extra 2026-10-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/release-015-extra-2026-10-24.jsonl`.

## 2026-10-24T04:19 UTC, build 85cf1978, jobs bp-rvb-rel015, ip-rvb-rel015, sv-rvb-rel015, sv-soak-rel015, ip-rvb-v014

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rvb-rel015 | 04:19 (12:19) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2161.8/1831.9/1532.9 -> 2291.5/2078.2/1334 | - | noise: medium (other apps 5.2% of game CPUs); others on game CPUs 5.2%; clock avg/min 4202/4125 MHz; README comparison, release side (BP): v0.1.5 release build asi.rel015 with the repo (release) ts3native.ini and the built ts3fixes.package as shipped, no switch overrides. 10 min seeded (12345). Read with hitch-ab.ts against bp-rvb-vanilla / bp-rvb-v014. |
| ip-rvb-rel015 | 04:31 (12:31) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2171.7/1834.7/1532.9 -> 2158.5/1861.1/1499.3 | - | noise: medium (other apps 6.8% of game CPUs); others on game CPUs 6.8%; clock avg/min 4196/4125 MHz; README comparison, release side (IP): v0.1.5 release build asi.rel015 with the repo (release) ts3native.ini and the built ts3fixes.package as shipped, no switch overrides. 10 min seeded (12345). Read with hitch-ab.ts against ip-rvb-vanilla / ip-rvb-v014. |
| sv-rvb-rel015 | 04:42 (12:42) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2104.5/1773.6/1580.9 -> 2192.8/1994.5/1403.4 | - | noise: medium (other apps 5.7% of game CPUs); others on game CPUs 5.7%; clock avg/min 4242/4175 MHz; README comparison, release side (SV): v0.1.5 release build asi.rel015 with the repo (release) ts3native.ini and the built ts3fixes.package as shipped, no switch overrides. 10 min seeded (12345). Read with hitch-ab.ts against sv-rvb-vanilla / sv-rvb-v014. |
| sv-soak-rel015 | 04:54 (12:54) | 46.8 | PASS | PASS: profile 45 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2150.3/1803.3/1548.9 -> 2224/1999.2/1339.8 | - | noise: medium (other apps 6.3% of game CPUs); others on game CPUs 6.3%; clock avg/min 4230/4139 MHz; SV soak on the v0.1.5 release build (asi.rel015, release ini, shipped ts3fixes), 45 min seeded natural play; compare with sv-soak-v014 (3 >= 1 s, max 1719 ms, 97-105 it/s). |
| ip-rvb-v014 | 05:41 (13:41) | 11.6 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2184.1/1856.6/1516.9 -> 2196.8/1882.7/1499.3 | asyncStarted +5802, asyncRefused +136, asyncCollected +5789, asyncDropped +13, asyncUs +61095373, asyncMaxUs +274406, asyncPumpUs +44726440, asyncPumpWaits +4753, decompressCalls +89501, decompressInMB +3316, decompressOutMB +8946, decompressMs +7059, decompressMainCalls +8236, decompressMainMs +431, waterJpsCallsK +13238, waterJpsCellsK +1530550, asyncRefusedM1Pending +33, asyncBackoffs +1, async | noise: medium (other apps 5.9% of game CPUs); others on game CPUs 5.9%; clock avg/min 4189/4120 MHz; README comparison, side 2 of 3 (IP): v0.1.4 = asi.rel0360 with the shipped release ini pinned (release-vs-baseline.json rel side), ts3fixes shipped tuning. 10 min seeded (12345). 2026-10-24: v0.1.5 tuning ships kStuckCheckAsync/kOverwatchChain/kErrorTrapSpread True, so this v0.1.4 side switches the |

### bp-rvb-rel015: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-rvb-rel015: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-rvb-rel015: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-soak-rel015: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 45 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
