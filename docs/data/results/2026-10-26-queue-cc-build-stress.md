# Queue cc-build-stress 2026-10-26

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/cc-build-stress-2026-10-26.jsonl`.

## 2026-10-26T01:10 UTC, build 426151f6, jobs bp-cc-live-trimoff

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-cc-live-trimoff | 01:10 (09:10) | 20.4 | PASS | PASS: profile 15 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->2, owspread_types_run 0->40, stuckcheckasync_queued 0->355, stuckcheckasync_run 0->355, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->63, stuckcheck_notstuck 0->182, stuckcheck_rearmed 0->102, stuckcheck_reset 0->9<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: re | 0 | 2223.7/1904.4/1468.9 -> 2393/2185.2/1163.8 | - | timing: invalid (noise medium); build: asi 501714b2a78d not in DB ts3native 0.36.80 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 11.8% of game CPUs); others on game CPUs 11.8%; clock avg/min 4248/4144 MHz; Live saturation, TrimAfterCleanup off. Pass: no crash, repro steps ok (wear_parts sim 75-149 optional), 3 x live-full / live-empt |

### bp-cc-live-trimoff: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-26T01:42 UTC, build ffec56ed, jobs bp-cc-edittown

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-cc-edittown | 01:43 (09:43) | 12.7 | **FAIL** | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->0, owspread_types_run 0->0, stuckcheckasync_queued 0->0, stuckcheckasync_run 0->0, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->0, stuckcheck_notstuck 0->0, stuckcheck_rearmed 0->0, stuckcheck_reset 0->0<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fi | 0 | 2225.8/1884.7/1468.9 -> 2285.8/1906.5/1403.5 | - | timing: invalid (job FAIL: FAIL: events not fired: log /^diag: mem #\d+ mark ev=et-empty / 0 lines (want >= 5); noise medium); build: asi 00b85823af76 not in DB ts3native 0.36.84 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 10.0% of game CPUs); others on game CPUs 10.0%; clock avg/min 4212/4131 MHz; Edit Town lot cycles (repro et-lot |

### bp-cc-edittown: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-26T04:28 UTC, build c3f135e4, jobs bp-cc-edittown

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-cc-edittown | 04:28 (12:28) | 13.5 | **FAIL** | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->0, owspread_types_run 0->0, stuckcheckasync_queued 0->0, stuckcheckasync_run 0->0, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->0, stuckcheck_notstuck 0->0, stuckcheck_rearmed 0->0, stuckcheck_reset 0->0<br>FAIL: events not fired: log /^diag: mem #\d+ mark ev=et-empty / 0 lines (want > | 0 | 2211.7/1905.9/1500.9 -> 2356.8/1960.3/1346.7 | - | timing: invalid (job FAIL: FAIL: events not fired: log /^diag: mem #\d+ mark ev=et-empty / 0 lines (want >= 5); noise medium); build: asi e0317f4a3be9 not in DB ts3native 0.36.85 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 25.2% of game CPUs); others on game CPUs 25.2%; clock avg/min 4134/4086 MHz; Edit Town lot cycles (repro et-lot |
