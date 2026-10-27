# Queue cc-makeup-presets 2026-10-26

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/cc-makeup-presets-2026-10-26.jsonl`.

## 2026-10-26T02:03 UTC, build c0a14d32, jobs bp-ccp-load, bp-ccm-cycle, bp-ccp-cycle

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ccp-load | 02:03 (10:03) | 5.3 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->1, owspread_types_run 0->20, stuckcheckasync_queued 0->122, stuckcheckasync_run 0->122, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->13, stuckcheck_notstuck 0->63, stuckcheck_rearmed 0->39, stuckcheck_reset 0->7<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro | 0 | 2190.3/1850.6/1532.8 -> 2296.8/1974.2/1387.6 | - | timing: invalid (noise medium); build: asi 00b85823af76 not in DB ts3native 0.36.84 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 15.4% of game CPUs); others on game CPUs 15.4%; clock avg/min 4194/4132 MHz; Load smoke (~8 min with the load). Pass: preset_cc_check ok (>= 100 placeable CCGen products with >= 6 presets), pcc-loaded mark, |
| bp-ccm-cycle | 02:08 (10:08) | 12.2 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->4, owspread_types_run 0->80, stuckcheckasync_queued 0->488, stuckcheckasync_run 0->487, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->90, stuckcheck_notstuck 0->236, stuckcheck_rearmed 0->148, stuckcheck_reset 0->14<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: r | 0 | 2184.1/1831.2/1548.9 -> 2467.7/2195.5/1126.4 | - | timing: invalid (noise medium); build: asi 00b85823af76 not in DB ts3native 0.36.84 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 10.6% of game CPUs); others on game CPUs 10.6%; clock avg/min 4233/4190 MHz; Makeup CC (repro makeup-cc-cycle). Pass: 3 x mkp-full and 3 x mkp-empty marks, every cycle's first wear_parts dressed > 0 (repro- |
| bp-ccp-cycle | 02:21 (10:21) | 16.1 | PASS | PASS: profile 14 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->5, owspread_types_run 0->100, stuckcheckasync_queued 0->610, stuckcheckasync_run 0->610, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->107, stuckcheck_notstuck 0->289, stuckcheck_rearmed 0->189, stuckcheck_reset 0->27<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: | 0 | 2192.4/1857.9/1516.9 -> 2333.4/2095.6/1249 | - | timing: invalid (noise medium); build: asi 00b85823af76 not in DB ts3native 0.36.84 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 9.2% of game CPUs); others on game CPUs 9.2%; clock avg/min 4220/4160 MHz; Object design presets (repro preset-cc-cycle, agent preset_cc_cycle). Pass: cycles_done 3, no error, applied > 0 every cycle (repro |

### bp-ccp-load: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ccm-cycle: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ccp-cycle: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 14 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
