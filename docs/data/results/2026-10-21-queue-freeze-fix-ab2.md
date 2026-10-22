# Queue freeze-fix-ab2 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/freeze-fix-ab2-2026-10-21.jsonl`.

## 2026-10-21T01:13 UTC, build 8381f5f, jobs bp-ffx2-play-off, bp-ffx2-play-on, bp-ffx2-big-crowd-on, bp-ffx2-big-crowd-off

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ffx2-play-off | 01:13 (09:13) | 13.8 | PASS | PASS: profile 12 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2160.5/1848.9/1516.9 -> 2250.7/2062.8/1323.1 | asyncStarted +22160, asyncRefused +217, asyncCollected +22157, asyncDropped +2, asyncUs +103117281, asyncMaxUs +207908, asyncPumpUs +58043696, asyncPumpWaits +12282, asyncRefusedM1Pending +50, asyncBackoffs +12, asyncQueueMax +6 | noise: high (other apps 20.5% of game CPUs); others on game CPUs 20.5%; clock avg/min 4158/4115 MHz; docs/134 §2.6 M1Retry A/B (ts3native 0.36.13 merge build, game-copy/ts3native.asi.0313; ts3fixes from the same commit). Both arms: FallbackAsync=1, SyncCapMs=0 (no cap, so M1Pending sync plans show their full length), LogFallbackMs=20, LogFallbackStack=16 (docs/141: 8 frames showed only ~2 game fra |

Paused after bp-ffx2-play-off (AI usage 5h 2% (reset 14:00 PHT) | 7d 80% (reset 14:00 PHT) | read 0 min ago); resume with `--from bp-ffx2-play-on`.

### bp-ffx2-play-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 12 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-21T01:56 UTC, build 9c1e26e, jobs bp-ffx2-play-on, bp-ffx2-big-crowd-on, bp-ffx2-big-crowd-off

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ffx2-play-on | 01:56 (09:56) | 13.7 | PASS | PASS: profile 12 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166.8/1852.3/1516.9 -> 2223.1/1961.8/1421.1 | asyncStarted +23026, asyncRefused +251, asyncCollected +23017, asyncDropped +5, asyncUs +107027418, asyncMaxUs +1481261, asyncPumpUs +57689872, asyncPumpWaits +12324, asyncRefusedM1Pending +74, asyncSkipped +3, asyncBackoffs +14, asyncQueueMax +9 | noise: high (other apps 23.4% of game CPUs); others on game CPUs 23.4%; clock avg/min 4152/4120 MHz; docs/134 §2.6 M1Retry A/B (ts3native 0.36.13 merge build, game-copy/ts3native.asi.0313; ts3fixes from the same commit). Both arms: FallbackAsync=1, SyncCapMs=0 (no cap, so M1Pending sync plans show their full length), LogFallbackMs=20, LogFallbackStack=16 (docs/141: 8 frames showed only ~2 game fra |
| bp-ffx2-big-crowd-on | 02:09 (10:09) | 9.7 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166/1841.7/1516.9 -> 2262.6/2092.2/1261.5 | asyncStarted +12863, asyncRefused +135, asyncCollected +12856, asyncDropped +2, asyncUs +63341410, asyncMaxUs +285206, asyncPumpUs +34712995, asyncPumpWaits +7784, asyncRefusedM1Pending +35, asyncSkipped +5, asyncBackoffs +10, asyncQueueMax +23 | noise: high (other apps 19.1% of game CPUs); others on game CPUs 19.1%; clock avg/min 4158/4120 MHz; docs/134 §2.6 M1Retry A/B (ts3native 0.36.13 merge build, game-copy/ts3native.asi.0313; ts3fixes from the same commit). Both arms: FallbackAsync=1, SyncCapMs=0 (no cap, so M1Pending sync plans show their full length), LogFallbackMs=20, LogFallbackStack=16 (docs/141: 8 frames showed only ~2 game fra |
| bp-ffx2-big-crowd-off | 02:19 (10:19) | 9.8 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2159.1/1834.9/1500.9 -> 2370.3/2073.5/1206.4 | asyncStarted +12391, asyncRefused +152, asyncCollected +12388, asyncDropped +2, asyncUs +63759493, asyncMaxUs +384789, asyncPumpUs +34928933, asyncPumpWaits +7332, asyncRefusedM1Pending +17, asyncSkipped +1, asyncBackoffs +10, asyncQueueMax +18 | noise: high (other apps 24.1% of game CPUs); others on game CPUs 24.1%; clock avg/min 4159/4114 MHz; docs/134 §2.6 M1Retry A/B (ts3native 0.36.13 merge build, game-copy/ts3native.asi.0313; ts3fixes from the same commit). Both arms: FallbackAsync=1, SyncCapMs=0 (no cap, so M1Pending sync plans show their full length), LogFallbackMs=20, LogFallbackStack=16 (docs/141: 8 frames showed only ~2 game fra |

### bp-ffx2-play-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 12 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ffx2-big-crowd-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ffx2-big-crowd-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
