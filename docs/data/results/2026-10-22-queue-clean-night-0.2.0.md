# Queue clean-night-0.2.0 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/clean-night-0.2.0-2026-10-22.jsonl`.

## 2026-10-22T00:50 UTC, build c9a3e70, jobs bp-fb-play-offfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-fb-play-offfirst | 00:50 (08:50) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2167.6/1853.9/1532.9 -> 2263.7/2088.1/1328.8 | asyncStarted +18227, asyncRefused +227, asyncCollected +18223, asyncDropped +2, asyncUs +82876558, asyncMaxUs +216851, asyncPumpUs +47697247, asyncPumpWaits +11012, asyncRefusedM1Pending +63, asyncBackoffs +13, asyncQueueFull +2, asyncQueueMax +7 | noise: medium (other apps 5.3% of game CPUs); others on game CPUs 5.3%; clock avg/min 4232/4160 MHz; Clean night only (docs/TESTING.md clean-night rules: nothing else running, machine-load line < 10% other CPU, else rerun). ts3native 0.36.14 (game-copy/ts3native.asi.0314) with ts3fixes built and installed from the same commit (node tools/setup/build-fixes.ts; tuning as master, every v0.2.0 candida |

### bp-fb-play-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
