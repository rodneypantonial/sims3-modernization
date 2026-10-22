# Queue cellmemo-probe 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/cellmemo-probe-2026-10-22.jsonl`.

## 2026-10-22T07:19 UTC, build b013bec5, jobs bp-cellmemo-verify, ip-cellmemo-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-cellmemo-verify | 07:20 (15:20) | 2.1 | **CRASH** | FAIL: profile 3 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 07:22:05 (TS3W.exe process gone) | 1 | 2136/1834.8/1580.9 -> ? | - | noise: high (other apps 31.8% of game CPUs, NOT comparable (load or clock)); others on game CPUs 31.8%; clock avg/min 4125/4066 MHz; Bridgeport, release defaults + [RouteJobPhases] + [CellMemoProbe]. Read would-hit ms/s (decision: >= 5 builds M), repeat share per wrapper, per-plan max and histogram.; 2026-10-22T01:11:05.000Z  ACCESS_VIOLATION reading from address 0x00000010 (Unmappable address)  a |
| ip-cellmemo-verify | 07:22 (15:22) | 2.2 | **CRASH** | FAIL: profile 3 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 07:24:17 (TS3W.exe process gone) | 1 | 2233.6/1854.6/1476.9 -> ? | - | noise: high (other apps 45.9% of game CPUs, NOT comparable (load or clock)); others on game CPUs 45.9%; clock avg/min 3887/3780 MHz; Isla Paradiso, release defaults + [RouteJobPhases] + [CellMemoProbe]. Read as BP (IP plans are water-bound: fewer in-room legs expected).; 2026-10-22T01:11:05.000Z  ACCESS_VIOLATION reading from address 0x00000010 (Unmappable address)  at 0x0084eac3 in TS3W.exe  stac |

Stopped: two jobs in a row crashed.

## 2026-10-22T08:06 UTC, build 314296f6, jobs bp-cellmemo-verify, ip-cellmemo-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-cellmemo-verify | 08:06 (16:06) | 4.8 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2143.9/1842/1564.9 -> 2239.1/1908.5/1491.4 | - | noise: high (other apps 22.5% of game CPUs); others on game CPUs 22.5%; clock avg/min 4208/4136 MHz; Bridgeport, release defaults + [RouteJobPhases] + [CellMemoProbe]. Read would-hit ms/s (decision: >= 5 builds M), repeat share per wrapper, per-plan max and histogram. |
| ip-cellmemo-verify | 08:11 (16:11) | 4.6 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2179.3/1840.2/1516.9 -> 2133.9/1848.6/1516.9 | - | noise: high (other apps 19.1% of game CPUs); others on game CPUs 19.1%; clock avg/min 4233/4184 MHz; Isla Paradiso, release defaults + [RouteJobPhases] + [CellMemoProbe]. Read as BP (IP plans are water-bound: fewer in-room legs expected). |

### bp-cellmemo-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-cellmemo-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
