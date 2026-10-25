# Queue repro-check-release 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/repro-check-release-2026-10-23.jsonl`.

## 2026-10-23T22:55 UTC, build cab77de5, jobs ip-rcr-route-legs

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-rcr-route-legs | 22:55 (06:55) | 9.3 | PASS | PASS: profile stopped at 4 events after 6.3 min (off 2, on 2; 4 firings, 1 cycles; rule 2 events per arm; 1 steps capped), cap 16 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2196.8/1858.2/1500.9 -> 2260.8/1929.2/1451.4 | asyncStarted +6251, asyncRefused +65, asyncCollected +6227, asyncDropped +2, asyncUs +61810380, asyncMaxUs +263225, asyncPumpUs +39782882, asyncPumpWaits +6103, asyncRefusedM1Pending +26, asyncSkipped +20, asyncBackoffs +14, asyncQueueMax +31 | noise: medium (other apps 5.9% of game CPUs); others on game CPUs 5.9%; clock avg/min 4186/4061 MHz; ip-route-legs-loop (legreplanskip, idlememo): IP crowd across the water and back, ABBA on the two ts3fixes switches. Pass: no crash, legreplanskip_skipped >= 1, idlememo_skipped >= 1. 2026-10-23: cap 16 min (4 firings per arm at 30-90 s need it; 6 min ran out after 3); legreplanskip never acted in  |

### ip-rcr-route-legs: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 16 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
