# Queue stuckcheck-async 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/stuckcheck-async-2026-10-21.jsonl`.

## 2026-10-21T11:00 UTC, build 02b4324, jobs bp-sca-off, bp-sca-on

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-sca-off | 11:00 (19:00) | 9.8 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2220/1858/1484.9 -> 2283/2113/1245.1 | asyncStarted +17105, asyncRefused +204, asyncCollected +17103, asyncDropped +1, asyncUs +70237768, asyncMaxUs +169634, asyncPumpUs +39528964, asyncPumpWaits +9402, asyncRefusedM1Pending +21, asyncBackoffs +11, asyncQueueMax +8 | noise: medium (other apps 7.6% of game CPUs); others on game CPUs 7.6%; clock avg/min 4189/4085 MHz; docs/134 §8 A/B, arm OFF: the 3 AM stuck check in the world alarm, as the game does it. ts3native 0.36.13 (game-copy/ts3native.asi.0313) with ts3fixes built from the commit that adds stuckcheckasync installed (the switch defaults off). Both arms: FallbackAsync=1 (foreign-task plans async), LogFallb |
| bp-sca-on | 11:11 (19:11) | 9.7 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2243/1851.5/1484.9 -> 2252.9/2061.7/1293.1 | asyncStarted +16930, asyncRefused +186, asyncCollected +16924, asyncDropped +3, asyncUs +70957784, asyncMaxUs +129836, asyncPumpUs +41619279, asyncPumpWaits +9978, asyncRefusedM1Pending +24, asyncSkipped +1, asyncBackoffs +12, asyncQueueMax +6 | noise: medium (other apps 5.9% of game CPUs); others on game CPUs 5.9%; clock avg/min 4218/4174 MHz; docs/134 §8 A/B, arm ON: as bp-sca-off with ts3fixes stuckcheckasync on for this job (profile.fixesOn, verified through the agent, restored at the end): the alarm only queues the check, Ts3Fixes.Fixes.StuckCheckDrain runs the game's RunStuckCheck one Sim per tick in our task. Pass = no crash, game  |

### bp-sca-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-sca-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
