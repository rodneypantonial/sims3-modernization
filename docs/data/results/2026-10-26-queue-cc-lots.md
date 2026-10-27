# Queue cc-lots 2026-10-26

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/cc-lots-2026-10-26.jsonl`.

## 2026-10-26T02:37 UTC, build 482b7a3b, jobs bp-cc-lots

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-cc-lots | 02:37 (10:37) | 7.4 | **CRASH** | FAIL: profile 32 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 02:43:46 (agent not answering for 60 s (heartbeat frozen))<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: events not fired: log /^diag: mem #\d+ mark ev=etcc-full / 0 lines (want >= 12); log /^diag: mem #\d+ mark ev=etcc-empty / 0 lines (want >= 12)<br>PASS: no forbidden log lines<br>PASS: 0 new script er | 1 | 2251.6/1911.6/1484.9 -> 2389.5/2014.1/1351.9 | - | timing: invalid (job VOID: runInvalid: profiler cut short (46 of 1920 s); crash or abort (no "done; game alive"); noise medium); build: asi 00b85823af76 not in DB ts3native 0.36.84 (log ok); ts3fixes tuning 58cdf1e3a38e not in DB (expected build/ts3fixes.package); noise: medium (other apps 10.9% of game CPUs); others on game CPUs 10.9%; clock avg/min 4249/4165 MHz; Pass: 6 lots x 2 cycles, every c |

### bp-cc-lots: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 32 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
