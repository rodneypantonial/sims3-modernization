# Queue cc-soak 2026-10-25

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/cc-soak-2026-10-25.jsonl`.

## 2026-10-25T23:06 UTC, build 758a4e24, jobs bp-cc-load-smoke, bp-ccsoak-1k, bp-ccsoak-2k

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-25T23:10 UTC, build 8b924b13, jobs bp-cc-load-smoke

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-cc-load-smoke | 23:10 (07:10) | 7.6 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->2, owspread_types_run 0->40, stuckcheckasync_queued 0->259, stuckcheckasync_run 0->258, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->47, stuckcheck_notstuck 0->106, stuckcheck_rearmed 0->93, stuckcheck_reset 0->13<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: no  | 0 | 2109.8/1823.3/1500.9 -> 2318.8/2086.8/1211.8 | - | timing: invalid (noise medium); build: asi b6b7ecaf4b76 not in DB ts3native 0.36.76 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 15.5% of game CPUs); others on game CPUs 15.5%; clock avg/min 4215/4170 MHz; 12-item smoke set (6 objects + 6 CAS parts, game-copy/cc-synth/smoke-12): catalogue shows the CCGen products, CAS lists the parts |

### bp-cc-load-smoke: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
