# Queue mem-suspects-verify 2026-10-25

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/mem-suspects-verify-2026-10-25.jsonl`.

## 2026-10-25T14:12 UTC, build a3d11921, jobs bp-memsus-catalogue, bp-memsus-reload, bp-memsus-place

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-memsus-catalogue | 14:12 (22:12) | 11.7 | **FAIL** | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->4, owspread_types_run 0->80, stuckcheckasync_queued 0->468, stuckcheckasync_run 0->468, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->114, stuckcheck_notstuck 0->213, stuckcheck_rearmed 0->127, stuckcheck_reset 0->15<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL:  | 0 | 2180/1863.3/1516.8 -> 2358.9/2091.7/1275.6 | - | timing: invalid (job FAIL: FAIL: events not fired: log /^diag: memsus #\d+ load / 0 lines (want >= 1); log /^diag: memsus #\d+ mark ev=cat-r1 cc=\d; noise medium); build: asi ed34629edc60 not in DB ts3native 0.36.74 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 10.6% of game CPUs); others on game CPUs 10.6%; clock avg/min 4185/4138 MH |
| bp-memsus-reload | 14:24 (22:24) | 2.7 | **FAIL** | FAIL: profile 15 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step time failed: no reply to 'time' (game paused, loading or blocked?)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: log /^diag: memsus #\d+ load .* etRef=\d+/ 1 lines (want >= 3); log /^diag: memsus #\d+ mark ev=load-2 / 0 lines (want >= 1); log /^diag: memsus #\d+ mark ev=load-3 / 0 lines ( | 1 | 2173.4/1837/1500.9 -> 2185.6/1872.2/1451.6 | - | timing: invalid (job VOID: runInvalid: profiler cut short (47 of 900 s); noisy machine (others 10.4% of game CPUs); crash or abort (no "done; game ; noise medium); build: asi ed34629edc60 not in DB ts3native 0.36.74 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 9.7% of game CPUs); others on game CPUs 9.7%; clock avg/min 4182/4164 MHz; |
| bp-memsus-place | 14:27 (22:27) | 11.6 | **FAIL** | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->4, owspread_types_run 0->80, stuckcheckasync_queued 0->463, stuckcheckasync_run 0->461, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->111, stuckcheck_notstuck 0->206, stuckcheck_rearmed 0->124, stuckcheck_reset 0->21<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS:  | 0 | 2164.8/1853.8/1500.9 -> 2296.3/2097.1/1275.4 | - | timing: invalid (job FAIL: FAIL: forbidden log lines: log /^diag: mem(sus)? .*=[-\d.]+! / 9 lines (want <= 0); noise medium); build: asi ed34629edc60 not in DB ts3native 0.36.74 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 5.6% of game CPUs); others on game CPUs 5.6%; clock avg/min 4233/4169 MHz; docs/191 module 2 / docs/192 #2: 20 b |

### bp-memsus-catalogue: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-memsus-reload: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-memsus-place: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-25T16:05 UTC, build 61245feb, jobs bp-memsus-catalogue, bp-memsus-reload, bp-memsus-place

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-memsus-catalogue | 16:05 (00:05) | 3.6 | **FAIL** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: sim clock stuck at day 15 18:20 for ~60 s (speed Pause; fail-fast guard)<br>ts3fixes activity: owchain_alarms 0->0, owspread_types_run 0->0, stuckcheckasync_queued 0->0, stuckcheckasync_run 0->0, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->0, stuckcheck_notstuck 0->0, stuckcheck_rearmed 0->0, stuckcheck_reset 0->0 | 1 | 2179.7/1844.3/1516.9 -> 2194.1/1866.4/1467.6 | - | timing: invalid (job VOID: runInvalid: profiler cut short (119 of 600 s); crash or abort (no "done; game alive"); noise medium); build: asi b6b7ecaf4b76 not in DB ts3native 0.36.76 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 10.2% of game CPUs); others on game CPUs 10.2%; clock avg/min 4195/4151 MHz; docs/191 module 2 verify (correc |
| bp-memsus-reload | 16:09 (00:09) | 16.9 | PASS | PASS: profile 15 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->5, owspread_types_run 0->100, stuckcheckasync_queued 0->588, stuckcheckasync_run 0->588, etspread_passes 0->3, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->153, stuckcheck_notstuck 0->256, stuckcheck_rearmed 0->162, stuckcheck_reset 0->18<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: | 0 | 2170.8/1855.7/1484.9 -> 2443.6/2178.1/1147.8 | - | timing: invalid (noise medium); build: asi b6b7ecaf4b76 not in DB ts3native 0.36.76 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 12.3% of game CPUs); others on game CPUs 12.3%; clock avg/min 4192/4149 MHz; docs/191 module 2 / docs/192 #4 #5: the same save loaded 3 times in one session (repro leak-overwatch-errortrap-reload) with NRaa |
| bp-memsus-place | 16:25 (00:25) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->4, owspread_types_run 0->80, stuckcheckasync_queued 0->464, stuckcheckasync_run 0->464, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->100, stuckcheck_notstuck 0->218, stuckcheck_rearmed 0->130, stuckcheck_reset 0->16<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS:  | 0 | 2178.1/1862.6/1516.9 -> 2315.1/2084.6/1307.4 | - | timing: invalid (noise medium); build: asi b6b7ecaf4b76 not in DB ts3native 0.36.76 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 10.1% of game CPUs); others on game CPUs 10.1%; clock avg/min 4206/4154 MHz; docs/191 module 2 / docs/192 #2: 20 buy + sell rounds of a store object (repro leak-place-object-loop), marks every 5 rounds, for |

### bp-memsus-catalogue: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-memsus-reload: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-memsus-place: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
