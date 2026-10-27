# Queue typegrowth2 2026-10-26

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/typegrowth2-2026-10-26.jsonl`.

## 2026-10-26T02:45 UTC, build 10d5877f, jobs bp-cc-typegrowth2

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-cc-typegrowth2 | 02:45 (10:45) | 20.3 | PASS | PASS: profile 15 min, seed 12345 (rng-compare.ts)<br>ts3fixes activity: owchain_alarms 0->2, owspread_types_run 0->40, stuckcheckasync_queued 0->359, stuckcheckasync_run 0->359, etspread_passes 0->1, stuckcheckcap_marked 0->0, stuckcheck_noplan 0->76, stuckcheck_notstuck 0->164, stuckcheck_rearmed 0->111, stuckcheck_reset 0->8<br>mono ledger INCOMPLETE: per-item numbers below the table<br>PASS: re | 0 | 2232.9/1920.4/1452.9 -> 2387.5/2185.5/1179.8 | - | timing: invalid (noise medium); build: asi 05551d7f15ff not in DB ts3native 0.36.83 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 11.7% of game CPUs); others on game CPUs 11.7%; clock avg/min 4230/4175 MHz; live-cc-cycle 15 min, shipped ini + [MemDiag] TypeGrowth=1 (every completed GC, top 8, graph every 10) + HitchAttr. |

### bp-cc-typegrowth2: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
