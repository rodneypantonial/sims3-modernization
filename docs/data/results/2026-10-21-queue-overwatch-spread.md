# Queue overwatch-spread 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/overwatch-spread-2026-10-21.jsonl`.

## 2026-10-21T09:22 UTC, build 02b4324, jobs bp-overwatch-spread-repro, bp-overwatch-spread-yield, bp-ab-overwatchspread

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-overwatch-spread-repro | 09:22 (17:22) | 6.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2230.4/1857.8/1484.9 -> 2271.9/2083/1301.1 | - | noise: high (other apps 23.6% of game CPUs); others on game CPUs 23.6%; clock avg/min 4153/4115 MHz; docs/145 correctness first (5 min): repro bp-overwatch-spread-inventory under /taskstall. Pass: our pass in > 1 tick with no tick over ~5 ms, verify 'after 0' for both paths, Overwatch's own pass shows its one-tick stall (>= 250 ms row from NRaas CleanupInventory) and ours none. Stop the queue on a |
| bp-overwatch-spread-yield | 09:29 (17:29) | 6.9 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2144.5/1856.7/1532.9 -> 2298/2087.9/1299.4 | - | noise: high (other apps 16.0% of game CPUs); others on game CPUs 16.0%; clock avg/min 4162/4116 MHz; docs/145 §4 marker contract on the installed V123 (5 min): repro bp-overwatch-spread-upstream-yield. Pass: owspread_upstream -1, owspread_alarms 0, owspread_theirs 1, the 'upstream spreading found (faked), ours off' line, no crash |
| bp-ab-overwatchspread | 09:36 (17:36) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2231.1/1859.8/1476.9 -> 2304.4/2086.1/1269.1 | - | noise: high (other apps 13.6% of game CPUs); others on game CPUs 13.6%; clock avg/min 4139/4100 MHz; docs/145 in-load A/B, seeded BP, 10 min, 2-min slices, overwatchspread alternating (decided at each nightly alarm, so a sim-day's ~17 s of spread slots may run into the next slice). Pass: worst-hitches.ts class 'TaskObjects burst' 3-4 in the off slices -> 0-1 in the on slices, no NRaas CleanupInven |

### bp-overwatch-spread-repro: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-overwatch-spread-yield: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ab-overwatchspread: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
