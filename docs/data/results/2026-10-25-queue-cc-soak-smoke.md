# Queue cc-soak-smoke 2026-10-25

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/cc-soak-smoke-2026-10-25.jsonl`.

## 2026-10-25T22:54 UTC, build 758a4e24, jobs bp-memlite-check

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-memlite-check | 22:54 (06:54) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->4, owspread_types_run 0->80, stuckcheckasync_queued 0->490, stuckcheckasync_run 0->490, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->88, stuckcheck_notstuck 0->217, stuckcheck_rearmed 0->163, stuckcheck_reset 0->22<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: r | 0 | 2188.6/1852.3/1500.9 -> 2279.9/2056.8/1307.4 | asyncStarted +14088, asyncRefused +184, asyncCollected +14083, asyncDropped +4, asyncUs +78851935, asyncMaxUs +191231, asyncPumpUs +46048314, asyncPumpWaits +9526, asyncRefusedM1Pending +31, asyncSkipped +1, asyncBackoffs +15, asyncQueueFull +6, asyncQueueMax +5 | timing: invalid (noise medium); build: asi 97d1d1bc04ad not in DB ts3native 0.36.79 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 15.8% of game CPUs); others on game CPUs 15.8%; clock avg/min 4186/4144 MHz; Cost and function check of the player memory lines (docs/191 'Player memory lines', ts3native 0.36.79 [MemDiag] Light default on  |

### bp-memlite-check: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-25T23:09 UTC, build 8b924b13, jobs bp-memlite-check

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
