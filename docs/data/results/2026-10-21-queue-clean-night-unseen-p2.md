# Queue clean-night-unseen-p2 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/clean-night-unseen-p2-2026-10-21.jsonl`.

## 2026-10-21T14:09 UTC, build 817cd2e, jobs bp-ut2-crowd-offfirst, bp-ut2-crowd-onfirst, bp-ut2-rate-offfirst, bp-ut2-rate-onfirst, ip-ut2-all-offfirst, ip-ut2-all-onfirst, bp-ut2-footprint-offfirst, bp-ut2-footprint-onfirst, bp-ut2-cars-offfirst, bp-ut2-cars-onfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ut2-crowd-offfirst | 14:09 (22:09) | 13.2 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2145.2/1832.1/1564.9 -> 2274/2050.3/1291.8 | - | noise: medium (other apps 7.6% of game CPUs); others on game CPUs 7.6%; clock avg/min 4199/4060 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |
| bp-ut2-crowd-onfirst | 14:22 (22:22) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2163.3/1838/1548.9 -> 2313.9/2113.9/1269.1 | - | noise: medium (other apps 7.6% of game CPUs); others on game CPUs 7.6%; clock avg/min 4236/4138 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |
| bp-ut2-rate-offfirst | 14:34 (22:34) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2164.2/1854.9/1532.9 -> 2304.7/2074.9/1301.5 | - | noise: unknown (no samples); Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-copy/ts3native.asi.0315, copy of native/ts3native/out/ts3native.asi from th |
| bp-ut2-rate-onfirst | 14:46 (22:46) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2163.4/1854.8/1548.9 -> 2318.3/2124.9/1285.6 | - | noise: medium (other apps 3.9% of game CPUs); others on game CPUs 3.9%; clock avg/min 4231/4184 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |
| ip-ut2-all-offfirst | 14:58 (22:58) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2154/1856.6/1516.9 -> 2201.5/1887.9/1516.9 | - | noise: medium (other apps 4.7% of game CPUs); others on game CPUs 4.7%; clock avg/min 4229/4151 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |
| ip-ut2-all-onfirst | 15:10 (23:10) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2193.5/1860/1532.9 -> 2163.2/1888.9/1532.9 | - | noise: medium (other apps 7.1% of game CPUs); others on game CPUs 7.1%; clock avg/min 4242/4129 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |
| bp-ut2-footprint-offfirst | 15:22 (23:22) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2190.9/1857.2/1532.9 -> 2306.3/2049.1/1317.5 | - | noise: medium (other apps 6.5% of game CPUs); others on game CPUs 6.5%; clock avg/min 4243/4154 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |

## 2026-10-21T15:40 UTC, build 817cd2e, jobs bp-ut2-footprint-onfirst, bp-ut2-cars-offfirst, bp-ut2-cars-onfirst, bp-ut2-list-offfirst, bp-ut2-list-onfirst, bp-ut2-avoid-offfirst, bp-ut2-avoid-onfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ut2-footprint-onfirst | 15:40 (23:40) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2163.4/1852/1532.9 -> 2306.8/2097.9/1253.5 | - | noise: high (other apps 10.8% of game CPUs); others on game CPUs 10.8%; clock avg/min 4220/4148 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |
| bp-ut2-cars-offfirst | 15:52 (23:52) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166.1/1852.1/1532.9 -> 2216.6/1897/1484.9 | - | noise: high (other apps 11.3% of game CPUs); others on game CPUs 11.3%; clock avg/min 4202/4107 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |
| bp-ut2-cars-onfirst | 16:04 (00:04) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2160.1/1835/1548.9 -> 2272.9/1892.6/1429.5 | - | noise: medium (other apps 7.3% of game CPUs); others on game CPUs 7.3%; clock avg/min 4231/4162 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |
| bp-ut2-list-offfirst | 16:16 (00:16) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2161.5/1851/1548.9 -> 2211.1/2062.2/1333.1 | - | noise: medium (other apps 5.2% of game CPUs); others on game CPUs 5.2%; clock avg/min 4248/4203 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |
| bp-ut2-list-onfirst | 16:28 (00:28) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2196.1/1891.2/1516.9 -> 2329.2/2121/1301.1 | - | noise: medium (other apps 6.1% of game CPUs); others on game CPUs 6.1%; clock avg/min 4248/4186 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |
| bp-ut2-avoid-offfirst | 16:40 (00:40) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2184.9/1850/1532.9 -> 2250.2/2067.1/1324.9 | - | noise: medium (other apps 5.3% of game CPUs); others on game CPUs 5.3%; clock avg/min 4243/4179 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |
| bp-ut2-avoid-onfirst | 16:52 (00:52) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2161.5/1853.8/1548.9 -> 2273.4/2079.3/1327.8 | - | noise: medium (other apps 5.0% of game CPUs); others on game CPUs 5.0%; clock avg/min 4225/4095 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). [UnseenThrottle] phase 2 timing (docs/143 §8.6; timing subset of unseen-throttle-p2-ab.json, whose snap-back and stuck-Sim jobs run in the daytime). ts3native 0.36.15 (game-cop |

### bp-ut2-footprint-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-cars-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-cars-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-list-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-list-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-avoid-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut2-avoid-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
