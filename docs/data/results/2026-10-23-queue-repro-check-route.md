# Queue repro-check-route 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/repro-check-route-2026-10-23.jsonl`.

## 2026-10-23T23:04 UTC, build cab77de5, jobs sv-route-seacheck-freeze

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| sv-route-seacheck-freeze | 23:04 (07:04) | 2.5 | **FAIL** | FAIL: profile 6 min, seed 12345 (rng-compare.ts)<br>FAIL: repro step crowd_sea_lot failed: no lot with sea inside its area in this world (community or residential)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: eoAsFail=0 (want >=20) | 1 | 2142.9/1805.9/1548.9 -> 2150.8/1817.9/1531.3 | asyncStarted +802, asyncRefused +5, asyncCollected +801, asyncUs +7145392, asyncMaxUs +738327, asyncPumpUs +4179841, asyncPumpWaits +1221, asyncRefusedM1Pending +1, asyncQueueMax +2 | noise: medium (other apps 4.9% of game CPUs); others on game CPUs 4.9%; clock avg/min 4196/4171 MHz; route-sv-seacheck-freeze: the game's stuck check on crowd Sims standing on the SV lot with the most sea, 4 loops ABBA (SyncCapMs + FbJobCapMs + stuckchecknear + failrepeat). Pass: >= 20 stuck checks ran, >= 20 failed world A* searches. Read: failed plans >= 1 s and their 16-frame stacks (the 7 s ca |

### sv-route-seacheck-freeze: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
