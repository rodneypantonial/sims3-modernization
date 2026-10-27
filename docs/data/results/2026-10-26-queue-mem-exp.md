# Queue mem-exp 2026-10-26

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/mem-exp-2026-10-26.jsonl`.

## 2026-10-26T00:02 UTC, build 42b05486, jobs bp-cc-typegrowth, bp-cc-sqrtheap-observe, bp-cc-sqrtheap-on, bp-cc-smallheap

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-cc-typegrowth | 00:02 (08:02) | 9.9 | **FAIL** | FAIL: profile 15 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step buy_object failed: Tiara Angelista's inventory refused 4 of 25 pool products (added 21): CCGen Easy Machine from Fred's PC Hut #2080, CCGen Easy Machine from Fred's PC Hut #2096, CCGen Easy Machine from Fred's PC Hut #2016<br>ts3fixes activity: owchain_alarms 0->0, owspread_types_run 0->0, stuckcheckasync_queued 0->0, stuckcheck | 1 | 2224.5/1908.1/1452.9 -> 2283.7/1976.6/1403.3 | - | timing: invalid (job VOID: runInvalid: profiler cut short (195 of 900 s); crash or abort (no "done; game alive"); noise medium); build: asi da4bd7ca90e2 not in DB ts3native 0.36.81 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 13.7% of game CPUs); others on game CPUs 13.7%; clock avg/min 4238/4178 MHz; live-cc-cycle 15 min, shipped in |
| bp-cc-sqrtheap-observe | 00:12 (08:12) | 9.2 | **FAIL** | FAIL: profile 15 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step buy_object failed: Tiara Angelista's inventory refused 4 of 25 pool products (added 21): CCGen Easy Machine from Fred's PC Hut #2080, CCGen Easy Machine from Fred's PC Hut #2096, CCGen Easy Machine from Fred's PC Hut #2016<br>ts3fixes activity: owchain_alarms 0->0, owspread_types_run 0->0, stuckcheckasync_queued 0->0, stuckcheck | 1 | 2247.1/1922.2/1452.9 -> 2286.7/1976.9/1419.3 | - | timing: invalid (job VOID: runInvalid: profiler cut short (194 of 900 s); crash or abort (no "done; game alive"); noise medium); build: asi da4bd7ca90e2 not in DB ts3native 0.36.81 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 16.6% of game CPUs); others on game CPUs 16.6%; clock avg/min 4228/4155 MHz; Baseline arm: [GC] SqrtHeapLimit |
| bp-cc-sqrtheap-on | 00:21 (08:21) | 20.5 | PASS | PASS: profile 15 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->3, owspread_types_run 0->60, stuckcheckasync_queued 0->364, stuckcheckasync_run 0->364, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->57, stuckcheck_notstuck 0->176, stuckcheck_rearmed 0->114, stuckcheck_reset 0->18<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: r | 0 | 2230.2/1902.2/1468.9 -> 2407.8/2165.6/1179.8 | - | timing: invalid (noise medium); build: asi da4bd7ca90e2 not in DB ts3native 0.36.81 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 12.7% of game CPUs); others on game CPUs 12.7%; clock avg/min 4234/4180 MHz; Act arm: [GC] SqrtHeapLimit=1 (SqrtC 0.0005/MB, ceiling 1024 MB), otherwise bp-cc-sqrtheap-observe. Pass: switch read back (SqrtH |
| bp-cc-smallheap | 00:41 (08:41) | 20.4 | **FAIL** | PASS: profile 15 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->2, owspread_types_run 0->40, stuckcheckasync_queued 0->249, stuckcheckasync_run 0->249, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->46, stuckcheck_notstuck 0->122, stuckcheck_rearmed 0->73, stuckcheck_reset 0->8<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: eve | 0 | 2064.8/1752.3/1684.7 -> 2251.5/2002.6/1376.5 | - | timing: invalid (job FAIL: FAIL: events not fired: log /^gcsize: gc=\d+ mode=1 .* grewMB=[1-9]/ 0 lines (want >= 1); noise medium); build: asi da4bd7ca90e2 not in DB ts3native 0.36.81 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 16.0% of game CPUs); others on game CPUs 16.0%; clock avg/min 4236/4172 MHz; Error 12 arm: [GC] InitialHea |

### bp-cc-typegrowth: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-cc-sqrtheap-observe: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-cc-sqrtheap-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-cc-smallheap: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
