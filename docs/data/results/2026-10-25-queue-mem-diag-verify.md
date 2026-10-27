# Queue mem-diag-verify 2026-10-25

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/mem-diag-verify-2026-10-25.jsonl`.

## 2026-10-25T14:01 UTC, build a3d11921, jobs bp-memdiag-selftest-build-save, bp-memdiag-travel

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-memdiag-selftest-build-save | 14:01 (22:01) | 6.8 | **FAIL** | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->2, owspread_types_run 0->40, stuckcheckasync_queued 0->240, stuckcheckasync_run 0->240, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->53, stuckcheck_notstuck 0->105, stuckcheck_rearmed 0->71, stuckcheck_reset 0->11<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: eve | 0 | 2195/1851.2/1516.9 -> 2303.1/2093.6/1275.8 | - | timing: invalid (job FAIL: FAIL: events not fired: log /^diag: mem selftest PASS/ 0 lines (want >= 1); log /^diag: mem fields \(after the self-test; noise medium); build: asi 8e7a43d6db4d not in DB ts3native 0.36.69 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 7.6% of game CPUs); others on game CPUs 7.6%; clock avg/min 4203/4145 MHz; |
| bp-memdiag-travel | 14:08 (22:08) | 4.3 | PASS | travel: PASS<br>summary: 1 cases, 1 passed, 0 failed<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repro events fired<br>PASS: no forbidden log lines | 0 | 2184.8/1856.8/1500.9 -> 2171.4/1899.9/1468.9 | - | timing: invalid (noise medium; correctness-only tool ProbeSnap.CallSites); build: asi 8e7a43d6db4d not in DB ts3native 0.36.69 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 5.1% of game CPUs); others on game CPUs 5.1%; clock avg/min 4227/4201 MHz; docs/191 module 1: travel round trip Bridgeport -> France -> Bridgeport (save-matrix tra |

### bp-memdiag-selftest-build-save: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-memdiag-travel: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-25T15:58 UTC, build 29a771a0, jobs bp-memdiag-selftest-build-save

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-memdiag-selftest-build-save | 15:58 (23:58) | 6.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->2, owspread_types_run 0->40, stuckcheckasync_queued 0->243, stuckcheckasync_run 0->243, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->63, stuckcheck_notstuck 0->102, stuckcheck_rearmed 0->69, stuckcheck_reset 0->9<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: repr | 0 | 2197.4/1836.4/1500.9 -> 2355/2078.5/1243.8 | - | timing: invalid (noise medium); build: asi 24d975c77d2a not in DB ts3native 0.36.75 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 13.2% of game CPUs); others on game CPUs 13.2%; clock avg/min 4196/4177 MHz; docs/191 module 1 / docs/190 phase 1 [MemDiag] verify (correctness only, never timing: SelfTest leaks 8 MB on purpose). One Bridg |

### bp-memdiag-selftest-build-save: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
