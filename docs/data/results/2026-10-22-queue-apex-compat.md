# Queue apex-compat 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/apex-compat-2026-10-22.jsonl`.

## 2026-10-22T03:16 UTC, build 2cfaf1d, jobs apex-base, apex-first, apex-last, apex-save-matrix, apex-ab-saveprep

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| apex-base | 03:16 (11:16) | 7.3 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2248.5/1857.8/1476.9 -> 2209.7/2042.4/1317.1 | asyncStarted +6600, asyncRefused +80, asyncCollected +6593, asyncUs +41700160, asyncMaxUs +174357, asyncPumpUs +22928794, asyncPumpWaits +3525, asyncRefusedM1Pending +14, asyncSkipped +4, asyncBackoffs +4, asyncQueueMax +8 | noise: high (other apps 49.6% of game CPUs); others on game CPUs 49.6%; clock avg/min 3701/3296 MHz; docs/148 §7 baseline: tested build + release-candidate ini, no Apex (5 min). Reference for the two Apex jobs |
| apex-first | 03:24 (11:24) | 7.3 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2412.8/2213.5/1066.2 -> 2583.8/2408.2/880.3 | asyncStarted +7013, asyncRefused +75, asyncCollected +7010, asyncDropped +1, asyncUs +43807570, asyncMaxUs +705817, asyncPumpUs +21588541, asyncPumpWaits +2452, asyncRefusedM1Pending +19, asyncSkipped +2, asyncBackoffs +4, asyncQueueMax +5 | noise: high (other apps 50.7% of game CPUs); others on game CPUs 50.7%; clock avg/min 3553/3149 MHz; docs/148 §7 smoke, Apex loads BEFORE ts3native (file-name order: ApexRadiance.asi < ts3native.asi), Apex default config (no ApexRadiance.toml = every default). Pass: no crash; ts3native.log shows the same SKIP-free install as apex-base; ApexRadiance_LOG.txt (read by hand, see docs/148 §7: the queue |
| apex-last | 03:32 (11:32) | 7.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2378.5/2077.1/1183.1 -> 2546.5/2395/856.6 | asyncStarted +7203, asyncRefused +47, asyncCollected +7200, asyncDropped +1, asyncUs +41149009, asyncMaxUs +395588, asyncPumpUs +21040265, asyncPumpWaits +2305, asyncRefusedM1Pending +9, asyncSkipped +2, asyncBackoffs +4, asyncQueueMax +7 | noise: high (other apps 52.6% of game CPUs); others on game CPUs 52.6%; clock avg/min 3683/3333 MHz; docs/148 §7 smoke, Apex loads AFTER ts3native (installed as zzApexRadiance.asi). Expected identical to apex-first: Apex installs its game-code features after the first Present + 1 s in both orders. Only the DllMain parts differ (Direct3DCreate9 detour order, instance mutex) |
| apex-save-matrix | 03:40 (11:40) | 3 | **FAIL** | control: PASS<br>identical: FAIL<br>summary: 2 cases, 1 passed, 1 failed (identical)<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2252.4/2052.3/1210.9 -> 2349.1/2105.4/1051.1 | - | noise: high (other apps 47.8% of game CPUs); others on game CPUs 47.8%; clock avg/min 3935/3712 MHz; docs/148 C1: save path with Apex on (Room to Save) and [SaveWrite] on: control + identical cases (docs/TESTING.md). Checks the saves are byte-identical to the Apex-off matrix and that no Error 12 / hang appears when both wrap the 0xAB12AF dispatch |
| apex-ab-saveprep | 03:43 (11:43) | 3.9 | **FAIL** | control: FAIL<br>identical: FAIL<br>summary: 2 cases, 0 passed, 2 failed (control, identical)<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2359/2040.5/1233 -> 2311.5/2045.9/1105 | - | noise: high (other apps 47.4% of game CPUs); others on game CPUs 47.4%; clock avg/min 3887/3334 MHz; docs/148 C1 proposal (needs the ts3native change in §8 first): same as apex-first with [SaveWrite] Enabled=0 (the call keeps its rel32; the RouteGate Sites=1 count-only jmp at 0xAAC110 stays, Apex accepts it) so the call at 0xAB12AF keeps its original rel32 and Apex's early save preparation can sta |

### apex-base: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### apex-first: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### apex-last: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### apex-save-matrix: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### apex-ab-saveprep: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T07:38 UTC, build 91ad3f6f, jobs apex-ab-saveprep

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| apex-ab-saveprep | 07:38 (15:38) | 2.4 | **FAIL** | control: PASS<br>identical: FAIL<br>summary: 2 cases, 1 passed, 1 failed (identical)<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2352.8/2034.7/1258.3 -> 2331.4/2078.2/1114.3 | - | noise: high (other apps 19.8% of game CPUs); others on game CPUs 19.8%; clock avg/min 4115/4028 MHz; docs/148 C1 proposal (needs the ts3native change in §8 first): same as apex-first with [SaveWrite] Enabled=0 (the call keeps its rel32; the RouteGate Sites=1 count-only jmp at 0xAAC110 stays, Apex accepts it) so the call at 0xAB12AF keeps its original rel32 and Apex's early save preparation can sta |

### apex-ab-saveprep: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
