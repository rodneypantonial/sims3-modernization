# Queue route-phases 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/route-phases-2026-10-22.jsonl`.

## 2026-10-22T07:10 UTC, build 5ce8c377, jobs bp-route-phases-verify, ip-route-phases-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-route-phases-verify | 07:10 (15:10) | 2 | **CRASH** | FAIL: profile 3 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 07:12:29 (TS3W.exe process gone) | 1 | 2226.4/1835.5/1500.9 -> ? | - | noise: high (other apps 28.3% of game CPUs); others on game CPUs 28.3%; clock avg/min 4145/4125 MHz; Bridgeport, release defaults + [RouteJobPhases]. Read: own START phase split (docs/150 §2 sampler estimate: cell A* ~30%, post pass ~19%, PathNodes_Insert ~13%, world A* ~10%), allocator and host-wait shares, foreign jobs if any.; 2026-10-22T01:11:05.000Z  ACCESS_VIOLATION reading from address 0x00 |
| ip-route-phases-verify | 07:12 (15:12) | 1.8 | **CRASH** | FAIL: profile 3 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 07:14:20 (TS3W.exe process gone) | 1 | 2237.2/1843.5/1484.9 -> ? | - | noise: high (other apps 26.6% of game CPUs); others on game CPUs 26.6%; clock avg/min 4114/4024 MHz; Isla Paradiso, release defaults + [RouteJobPhases]. Read: water paths and world A* share (IP long plans are water-bound, docs/150 §2), foreign jobs and their host-call waits.; 2026-10-22T01:11:05.000Z  ACCESS_VIOLATION reading from address 0x00000010 (Unmappable address)  at 0x0084eac3 in TS3W.exe  |

Stopped: two jobs in a row crashed.

## 2026-10-22T07:57 UTC, build 314296f6, jobs bp-route-phases-verify, ip-route-phases-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-route-phases-verify | 07:57 (15:57) | 4.9 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2221.9/1836.5/1500.9 -> 2221.6/1914.4/1453.1 | - | noise: high (other apps 29.3% of game CPUs); others on game CPUs 29.3%; clock avg/min 4150/4100 MHz; Bridgeport, release defaults + [RouteJobPhases]. Read: own START phase split (docs/150 §2 sampler estimate: cell A* ~30%, post pass ~19%, PathNodes_Insert ~13%, world A* ~10%), allocator and host-wait shares, foreign jobs if any. |
| ip-route-phases-verify | 08:02 (16:02) | 4.8 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2147.8/1837/1516.9 -> 2151.2/1845.8/1483.3 | - | noise: high (other apps 18.5% of game CPUs); others on game CPUs 18.5%; clock avg/min 4122/4002 MHz; Isla Paradiso, release defaults + [RouteJobPhases]. Read: water paths and world A* share (IP long plans are water-bound, docs/150 §2), foreign jobs and their host-call waits. |

### bp-route-phases-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-route-phases-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 3 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
