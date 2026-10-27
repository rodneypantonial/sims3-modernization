# Queue cc-build-items 2026-10-26

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/cc-build-items-2026-10-26.jsonl`.

## 2026-10-26T01:31 UTC, build 59b14bb1, jobs bp-ccb-catalog, bp-ccb-cycle

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-26T04:12 UTC, build c3f135e4, jobs bp-ccb-catalog, bp-ccb-cycle

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ccb-catalog | 04:12 (12:12) | 2.8 | **FAIL** | FAIL: profile 3 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step build_cc_catalog failed: build catalogue misses the set: wall_objects 30 < 40<br>ts3fixes activity: owchain_alarms 0->0, owspread_types_run 0->0, stuckcheckasync_queued 0->0, stuckcheckasync_run 0->0, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->0, stuckcheck_notstuck 0->0, stuckcheck_rearmed 0->0, stuckc | 1 | 2169.7/1855.4/1548.9 -> 2192.1/1883.8/1513.6 | - | timing: invalid (job VOID: runInvalid: profiler cut short (45 of 180 s); crash or abort (no "done; game alive"); noise medium); build: asi e0317f4a3be9 not in DB ts3native 0.36.85 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 11.2% of game CPUs); others on game CPUs 11.2%; clock avg/min 4112/4065 MHz; Load smoke (~8 min with the load) |
| bp-ccb-cycle | 04:15 (12:15) | 2.8 | **FAIL** | FAIL: profile 24 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step build_cc_catalog failed: build catalogue misses the set: wall_objects 30 < 40<br>ts3fixes activity: owchain_alarms 0->0, owspread_types_run 0->0, stuckcheckasync_queued 0->0, stuckcheckasync_run 0->0, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->0, stuckcheck_notstuck 0->0, stuckcheck_rearmed 0->0, stuck | 1 | 2134.4/1855.5/1580.9 -> 2166.3/1897.2/1529.6 | - | timing: invalid (job VOID: runInvalid: profiler cut short (45 of 1440 s); crash or abort (no "done; game alive"); noise medium); build: asi e0317f4a3be9 not in DB ts3native 0.36.85 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 12.6% of game CPUs); others on game CPUs 12.6%; clock avg/min 4114/4088 MHz; Build-mode CC, 3 phases (repro b |
