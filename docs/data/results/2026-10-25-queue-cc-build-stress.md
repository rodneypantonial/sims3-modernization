# Queue cc-build-stress 2026-10-25

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/cc-build-stress-2026-10-25.jsonl`.

## 2026-10-25T23:20 UTC, build 050cadc2, jobs bp-cc-live-trimoff, bp-cc-live-trimon, bp-cc-buildbuy, bp-cc-edittown

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-cc-live-trimoff | 23:21 (07:21) | 8.6 | **FAIL** | FAIL: profile 15 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step buy_object failed: Tiara Angelista's inventory took no CCGen Another Era Dining Table #1724<br>ts3fixes activity: owchain_alarms 0->0, owspread_types_run 0->0, stuckcheckasync_queued 0->103, stuckcheckasync_run 0->103, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->15, stuckcheck_notstuck 0->53, stuckcheck | 1 | 2231.7/1901.8/1420.9 -> 2272.2/1968.9/1387.3 | - | timing: invalid (job VOID: runInvalid: profiler cut short (191 of 900 s); crash or abort (no "done; game alive"); noise medium); build: asi 501714b2a78d not in DB ts3native 0.36.80 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 13.9% of game CPUs); others on game CPUs 13.9%; clock avg/min 4223/4174 MHz; Live saturation, TrimAfterCleanu |
| bp-cc-live-trimon | 23:29 (07:29) | 8.6 | **FAIL** | FAIL: profile 15 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step buy_object failed: Tiara Angelista's inventory took no CCGen Another Era Dining Table #1724<br>ts3fixes activity: owchain_alarms 0->0, owspread_types_run 0->0, stuckcheckasync_queued 0->105, stuckcheckasync_run 0->105, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->11, stuckcheck_notstuck 0->61, stuckcheck | 1 | 2230.7/1918.3/1436.9 -> 2316.4/1996.6/1387.3 | - | timing: invalid (job VOID: runInvalid: profiler cut short (191 of 900 s); crash or abort (no "done; game alive"); noise medium); build: asi 501714b2a78d not in DB ts3native 0.36.80 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 13.3% of game CPUs); others on game CPUs 13.3%; clock avg/min 4214/4124 MHz; bp-cc-live-trimoff with [Resourc |
| bp-cc-buildbuy | 23:38 (07:38) | 16.8 | **FAIL** | FAIL: profile 12 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step bb_cc_cycle failed: bb_cc_cycle still running<br>ts3fixes activity: owchain_alarms 0->0, owspread_types_run 0->0, stuckcheckasync_queued 0->0, stuckcheckasync_run 0->0, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->0, stuckcheck_notstuck 0->0, stuckcheck_rearmed 0->0, stuckcheck_reset 0->0<br>mono ledger  | 1 | 2228.9/1902.8/1468.9 -> 2237.9/1893.6/1419.6 | - | timing: invalid (job VOID: runInvalid: noisy machine (others 16.1% of game CPUs); crash or abort (no "done; game alive"); noise high); build: asi 501714b2a78d not in DB ts3native 0.36.80 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: high (other apps 15.3% of game CPUs); others on game CPUs 15.3%; clock avg/min 4216/4157 MHz; Build/Buy CC cycles (repro b |
| bp-cc-edittown | 23:55 (07:55) | 6.8 | **FAIL** | FAIL: profile 8 min, seed 12345 (rng-compare.ts)<br>FAIL: sim clock stuck at day 15 14:58 for ~60 s (speed Triple) (fail-fast guard)<br>ts3fixes activity: owchain_alarms 0->0, owspread_types_run 0->0, stuckcheckasync_queued 0->0, stuckcheckasync_run 0->0, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->0, stuckcheck_notstuck 0->0, stuckcheck_rearmed 0->0, stuckcheck_reset 0-> | 1 | 2215.8/1870.1/1500.9 -> 2276.5/1898.4/1427.5 | - | timing: invalid (job VOID: runInvalid: profiler cut short (118 of 480 s); crash or abort (no "done; game alive"); noise medium); build: asi 501714b2a78d not in DB ts3native 0.36.80 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 15.3% of game CPUs); others on game CPUs 15.3%; clock avg/min 4218/4164 MHz; Edit Town lot cycles (repro et-l |

### bp-cc-live-trimoff: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-cc-live-trimon: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-cc-buildbuy: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 12 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-cc-edittown: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
