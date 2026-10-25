# Queue p5-drift-check 2026-10-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/p5-drift-check-2026-10-24.jsonl`.

## 2026-10-24T07:28 UTC, build fcc73f1f, jobs ip-p5-drift-watch

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-p5-drift-watch | 07:28 (15:28) | 26.7 | **FAIL** | PASS: profile 25 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: wpArms=0 (want >=1) | 0 | 2240.9/2000.9/1405.3 -> 2303.3/2086.7/1327.5 | asyncStarted +17512, asyncRefused +198, asyncCollected +17262, asyncDropped +11, asyncUs +229784499, asyncMaxUs +397335, asyncPumpUs +133434510, asyncPumpWaits +19481, asyncRefusedM1Pending +106, asyncSkipped +239, asyncBackoffs +17, asyncQueueMax +7 | noise: medium (other apps 17.3% of game CPUs); others on game CPUs 17.3%; clock avg/min 4195/4121 MHz; PASS: no crash; the start-up lines for ReplayAuditEvery=1 and the Lot= watch; rpAudits > 0 (audits ran); wpArms >= 1 (a child on the lot was armed). Read after the run: 'watchpoint DRn hit' lines (eip, thread, route worker, job 0/1, stack) and whether that lot then logs 'snap replay audit MISMATC |

### ip-p5-drift-watch: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-24T08:41 UTC, build 9d258d81, jobs ip-p5-drift-watch

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-24T08:45 UTC, build 61f4be0f, jobs ip-p5-drift-watch

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-24T10:20 UTC, build 01a26080, jobs ip-p5-drift-watch

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-p5-drift-watch | 10:20 (18:20) | 8.1 | **CRASH** | FAIL: profile 25 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 10:28:55 (TS3W.exe process gone)<br>FAIL: events not fired: rpAudits missing; wpArms missing; wpHits missing; wrZ_0 missing; wrZ_1 missing; wrZ_2 missing; wrP30_0 missing; wrP30_1 missing; wrP30_2 missing; rpMis missing; wpCtor missing; wpFollow_0 missing; wpFollow_1 missing; rpHold_0 missing; rpHold_1 missing; rpHold_2 missin | 1 | 2226.2/2014.9/1431.8 -> ? | - | build: asi 1489dbef569d ts3native.asi.p5hold ts3native 0.36.40 (log ok); ts3fixes tuning c094be923c4a not in DB (expected build/ts3fixes.package); noise: medium (other apps 8.0% of game CPUs); others on game CPUs 8.0%; clock avg/min 4228/4142 MHz; Rerun (docs/157 §10): the arm and hold checks are log lines, not counter deltas (the 2026-10-24 run armed all 4 before the queue's c0 snapshot: wpArms d |
