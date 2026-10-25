# Queue story-days-gen 2026-10-23

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/story-days-gen-2026-10-23.jsonl`.

## 2026-10-23T05:58 UTC, build 9179d067, jobs bp-story-days-fullhouse

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-days-fullhouse | 05:58 (13:58) | 27 | **FAIL** | PASS: profile 25 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: story: Lala Angelista missed work/school on day 17; pursuits per Sim-day: free days 0.75 not > work/school days 0.90 | 0 | 2149.1/1833.4/1548.9 -> 2460.4/2112.1/1148.3 | asyncStarted +18210, asyncRefused +258, asyncCollected +18209, asyncDropped +1, asyncUs +147177373, asyncMaxUs +310429, asyncPumpUs +77953554, asyncPumpWaits +15427, asyncRefusedM1Pending +50, asyncBackoffs +15, asyncQueueMax +5 | noise: high (other apps 11.8% of game CPUs); others on game CPUs 11.8%; clock avg/min 4143/4071 MHz; First live run of a generated household (docs/181): archetype full-house (8 Sims, traits with up- and downsides) made in CAS and moved in with make_household, then the day planner; rotation off so the generated household is played all run. Correctness, not timing. One 25-min story run with the day  |

### bp-story-days-fullhouse: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-23T06:50 UTC, build 30af2f3e, jobs bp-story-days-fullhouse

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-days-fullhouse | 06:50 (14:50) | 1.7 | **FAIL** | FAIL: profile 25 min, seed 12345 (rng-compare.ts)<br>FAIL: household creation failed: full-house: NullReferenceException: GlobalFunctions.CreateObject: Object was created but unable to get proxy.  Possibly the medator instance is specifiying a script that does not exist.  Resource key for created object was 319e4f1d:38000000:000000000098a509, guid was 0x0000000000000000<br>mono ledger INCOMPLETE:  | 1 | 2147/1852.6/1548.9 -> 2179/1884.1/1513.6 | asyncStarted +55, asyncRefused +1, asyncCollected +55, asyncUs +639579, asyncMaxUs +82706, asyncPumpUs +383170, asyncPumpWaits +114, asyncRefusedM1Pending +1, asyncQueueMax +4 | noise: high (other apps 13.2% of game CPUs); others on game CPUs 13.2%; clock avg/min 4163/4136 MHz; First live run of a generated household (docs/181): archetype full-house (8 Sims, traits with up- and downsides) made in CAS and moved in with make_household, then the day planner; rotation off so the generated household is played all run. Correctness, not timing. One 25-min story run with the day  |

### bp-story-days-fullhouse: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-23T06:59 UTC, build 75536bbb, jobs bp-story-days-fullhouse

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-days-fullhouse | 06:59 (14:59) | 1.8 | **FAIL** | FAIL: profile 25 min, seed 12345 (rng-compare.ts)<br>FAIL: household creation failed: full-house: funds: 26000 simoleons buy no vacant house with the essentials for 8 Sims (cheapest: Rhapsody in Blue - 1br, 1ba, furnished 15860 + essentials (no crib in the catalog; 2 catalog products not creatable: The Twinbrook Roller 319E4F1D:38000000:000000000098A509: NullReferenceException (ObjectManagerFail<b | 1 | 2118.7/1837.5/1588.9 -> 2153.8/1872.7/1571.2 | asyncStarted +53, asyncRefused +1, asyncCollected +51, asyncUs +576085, asyncMaxUs +77094, asyncPumpUs +357731, asyncPumpWaits +139, asyncRefusedM1Pending +1, asyncQueueMax +3 | noise: high (other apps 15.9% of game CPUs); others on game CPUs 15.9%; clock avg/min 4196/4189 MHz; First live run of a generated household (docs/181): archetype full-house (8 Sims, traits with up- and downsides) made in CAS and moved in with make_household, then the day planner; rotation off so the generated household is played all run. Correctness, not timing. One 25-min story run with the day  |

### bp-story-days-fullhouse: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-23T07:02 UTC, build 62334237, jobs bp-story-days-overachievers

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-days-overachievers | 07:02 (15:02) | 1.8 | **FAIL** | FAIL: profile 25 min, seed 12345 (rng-compare.ts)<br>FAIL: household creation failed: buy bed_double (B.R.A.S.S Double Bed, 450: no free spot on the home lot for B.R.A.S.S Double Bed; Suite Dreams Bed, 560: no free spot on the home lot for Suite Dreams Bed; The Four Post Bed from McKraken Industries, 650: no free spot on the home lot for The Four Post Bed from McKraken Industries)<br>mono ledger I | 1 | 2226.4/1853.6/1452.9 -> 2273.2/1892.9/1366.2 | asyncStarted +47, asyncRefused +1, asyncCollected +47, asyncUs +890748, asyncMaxUs +424784, asyncPumpUs +293064, asyncPumpWaits +72, asyncRefusedM1Pending +3, asyncBackoffs +1, asyncQueueMax +2 | noise: high (other apps 18.0% of game CPUs); others on game CPUs 18.0%; clock avg/min 4195/4178 MHz; First live run of a generated household (docs/181): archetype full-house (8 Sims, traits with up- and downsides) made in CAS and moved in with make_household, then the day planner; rotation off so the generated household is played all run. Correctness, not timing. One 25-min story run with the day  |

### bp-story-days-overachievers: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-23T07:12 UTC, build 330799fa, jobs bp-story-days-fullhouse

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-days-fullhouse | 07:12 (15:12) | 2 | **FAIL** | FAIL: profile 25 min, seed 12345 (rng-compare.ts)<br>FAIL: household creation failed: buy bed_double (B.R.A.S.S Double Bed, 450: no free spot in the house on the home lot for B.R.A.S.S Double Bed; Suite Dreams Bed, 560: no free spot in the house on the home lot for Suite Dreams Bed; The Four Post Bed from McKraken Industries, 650: no free spot in the house on the home lot for The Four Post Bed fro | 1 | 2137.7/1847.6/1548.9 -> 2268.7/1875.9/1426.8 | asyncStarted +58, asyncRefused +1, asyncCollected +58, asyncUs +565975, asyncMaxUs +74792, asyncPumpUs +293520, asyncPumpWaits +197, asyncQueueMax +3 | noise: high (other apps 11.5% of game CPUs); others on game CPUs 11.5%; clock avg/min 4155/4144 MHz; First live run of a generated household (docs/181): archetype full-house (8 Sims, traits with up- and downsides) made in CAS and moved in with make_household, then the day planner; rotation off so the generated household is played all run. Correctness, not timing. One 25-min story run with the day  |

### bp-story-days-fullhouse: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
