# Queue candidate-loop-ab 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/candidate-loop-ab-2026-10-21.jsonl`.

## 2026-10-21T12:17 UTC, build 02b4324, jobs bp-cl-off, bp-cl-on, bp-cl-onrc, sv-cl-off, sv-cl-on, sv-cl-onrc

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-cl-off | 12:17 (20:17) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2245.8/1856.2/1484.9 -> 2354.3/2080.9/1174.4 | asyncStarted +18763, asyncRefused +189, asyncCollected +18760, asyncDropped +3, asyncUs +86668162, asyncMaxUs +214420, asyncPumpUs +47291093, asyncPumpWaits +11007, asyncRefusedM1Pending +39, asyncBackoffs +8, asyncQueueMax +5 | noise: high (other apps 15.2% of game CPUs); others on game CPUs 15.2%; clock avg/min 4161/4120 MHz; docs/141 §7 candidate-loop A/B, arm OFF: the game's candidate loops (all three switches at their tuning default, off). ts3native 0.36.13 (game-copy/ts3native.asi.0313, as freeze-fix-ab) with ts3fixes built from the commit that adds CandidateLoops.cs installed. All arms: FallbackAsync=1, SyncCapMs=0 |
| bp-cl-on | 12:29 (20:29) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2222.3/1842.9/1476.9 -> 2304.4/2075/1269.1 | asyncStarted +19590, asyncRefused +176, asyncCollected +19586, asyncDropped +2, asyncUs +93977317, asyncMaxUs +201672, asyncPumpUs +51491077, asyncPumpWaits +11255, asyncRefusedM1Pending +47, asyncSkipped +2, asyncBackoffs +14, asyncQueueMax +6 | noise: high (other apps 14.2% of game CPUs); others on game CPUs 14.2%; clock avg/min 4166/4136 MHz; docs/141 §7 candidate-loop A/B, arm ON: safespotcap + farthestsorted + indoorcap on for the whole run (profile.fixesOn). ts3native 0.36.13 (game-copy/ts3native.asi.0313, as freeze-fix-ab) with ts3fixes built from the commit that adds CandidateLoops.cs installed. All arms: FallbackAsync=1, SyncCapMs |
| bp-cl-onrc | 12:41 (20:41) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2128.6/1849.2/1492.9 -> 2358.6/2080.6/1180.6 | asyncStarted +18969, asyncRefused +197, asyncCollected +18964, asyncDropped +3, asyncUs +85573749, asyncMaxUs +212869, asyncPumpUs +47179999, asyncPumpWaits +10852, asyncRefusedM1Pending +51, asyncSkipped +1, asyncBackoffs +11, asyncQueueMax +7 | noise: high (other apps 13.6% of game CPUs); others on game CPUs 13.6%; clock avg/min 4170/4130 MHz; docs/141 §7 candidate-loop A/B, arm ON+routablecache: as ON plus the existing routablecache (shared 0.5 m verdict cache for IsPointInLotSafelyRoutable, opt-in since 2026-10-13 with no exercised A/B), to see its hits on reset paths. ts3native 0.36.13 (game-copy/ts3native.asi.0313, as freeze-fix-ab)  |
| sv-cl-off | 12:54 (20:54) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2161.6/1808.2/1532.9 -> 2171.5/1991.6/1372.9 | asyncStarted +6976, asyncRefused +202, asyncCollected +6973, asyncDropped +2, asyncUs +61628587, asyncMaxUs +1036751, asyncPumpUs +38485796, asyncPumpWaits +6452, asyncRefusedM1Pending +20, asyncBackoffs +3, asyncQueueMax +8 | noise: medium (other apps 9.7% of game CPUs); others on game CPUs 9.7%; clock avg/min 4177/4132 MHz; docs/141 §7 candidate-loop A/B, arm OFF: the game's candidate loops (all three switches at their tuning default, off). ts3native 0.36.13 (game-copy/ts3native.asi.0313, as freeze-fix-ab) with ts3fixes built from the commit that adds CandidateLoops.cs installed. All arms: FallbackAsync=1, SyncCapMs=0 |
| sv-cl-on | 13:06 (21:06) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2203.9/1809.8/1532.9 -> 2160/1982.1/1364.9 | asyncStarted +7533, asyncRefused +198, asyncCollected +7531, asyncDropped +1, asyncUs +69091603, asyncMaxUs +7959816, asyncPumpUs +45076342, asyncPumpWaits +6685, asyncRefusedM1Pending +13, asyncBackoffs +7, asyncQueueMax +9 | noise: medium (other apps 9.7% of game CPUs); others on game CPUs 9.7%; clock avg/min 4166/4066 MHz; docs/141 §7 candidate-loop A/B, arm ON: safespotcap + farthestsorted + indoorcap on for the whole run (profile.fixesOn). ts3native 0.36.13 (game-copy/ts3native.asi.0313, as freeze-fix-ab) with ts3fixes built from the commit that adds CandidateLoops.cs installed. All arms: FallbackAsync=1, SyncCapMs |
| sv-cl-onrc | 13:18 (21:18) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2197/1801.6/1494.2 -> 2163.8/1983.7/1382.2 | asyncStarted +7180, asyncRefused +194, asyncCollected +7179, asyncUs +96228231, asyncMaxUs +8414408, asyncPumpUs +73369689, asyncPumpWaits +6290, asyncRefusedM1Pending +35, asyncSkipped +1, asyncBackoffs +4, asyncQueueMax +8 | noise: high (other apps 12.7% of game CPUs); others on game CPUs 12.7%; clock avg/min 4170/4057 MHz; docs/141 §7 candidate-loop A/B, arm ON+routablecache: as ON plus the existing routablecache (shared 0.5 m verdict cache for IsPointInLotSafelyRoutable, opt-in since 2026-10-13 with no exercised A/B), to see its hits on reset paths. ts3native 0.36.13 (game-copy/ts3native.asi.0313, as freeze-fix-ab)  |

### bp-cl-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-cl-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-cl-onrc: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-cl-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-cl-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-cl-onrc: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
