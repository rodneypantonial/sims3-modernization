# Queue routealloc 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/routealloc-2026-10-22.jsonl`.

## 2026-10-22T07:34 UTC, build 91ad3f6f, jobs bp-routealloc-probe, ip-routealloc-probe

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-routealloc-probe | 07:34 (15:34) | 1.9 | **CRASH** | FAIL: profile 3 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 07:36:29 (TS3W.exe process gone) | 1 | 2231.4/1854.9/1462.2 -> ? | - | noise: high (other apps 20.3% of game CPUs); others on game CPUs 20.3%; clock avg/min 4163/4149 MHz; Bridgeport, release defaults + [RouteJobPhases]. Read: allocator ms/s per kind, top rows by self time (site, parent phase call, calls/job, mean bytes), PathNodes_Insert grows by capacity (how many vectors pass 16 / 64 nodes), jobs by allocator calls.; 2026-10-22T01:11:05.000Z  ACCESS_VIOLATION read |
| ip-routealloc-probe | 07:36 (15:36) | 1.8 | **CRASH** | FAIL: profile 3 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 07:38:19 (TS3W.exe process gone) | 1 | 2231.9/1834.1/1484.9 -> ? | - | noise: high (other apps 20.5% of game CPUs); others on game CPUs 20.5%; clock avg/min 4139/4116 MHz; Isla Paradiso, release defaults + [RouteJobPhases]. Read: as BP, plus the water planner's 16-byte site 0x6DFE2D (via 0x620560, K8's top site on water legs) and foreign jobs.; 2026-10-22T01:11:05.000Z  ACCESS_VIOLATION reading from address 0x00000010 (Unmappable address)  at 0x0084eac3 in TS3W.exe   |

Stopped: two jobs in a row crashed.

## 2026-10-22T08:16 UTC, build 314296f6, jobs bp-routealloc-probe, ip-routealloc-probe

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-routealloc-probe | 08:16 (16:16) | 4.7 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2163.7/1829.5/1548.9 -> 2273.5/1912.4/1453.5 | - | noise: high (other apps 30.2% of game CPUs); others on game CPUs 30.2%; clock avg/min 4181/4147 MHz; Bridgeport, release defaults + [RouteJobPhases]. Read: allocator ms/s per kind, top rows by self time (site, parent phase call, calls/job, mean bytes), PathNodes_Insert grows by capacity (how many vectors pass 16 / 64 nodes), jobs by allocator calls. |
| ip-routealloc-probe | 08:21 (16:21) | 4.6 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2146.9/1833.1/1548.9 -> 2148/1840.3/1515.3 | - | noise: high (other apps 23.2% of game CPUs); others on game CPUs 23.2%; clock avg/min 4190/4133 MHz; Isla Paradiso, release defaults + [RouteJobPhases]. Read: as BP, plus the water planner's 16-byte site 0x6DFE2D (via 0x620560, K8's top site on water legs) and foreign jobs. |

### bp-routealloc-probe: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-routealloc-probe: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
