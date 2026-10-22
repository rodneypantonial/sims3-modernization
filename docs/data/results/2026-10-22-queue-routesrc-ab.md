# Queue routesrc-ab 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/routesrc-ab-2026-10-22.jsonl`.

## 2026-10-22T05:09 UTC, build 48c803c, jobs bp-routesrc-verify, ip-routesrc-verify

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-routesrc-verify | 05:09 (13:09) | 8.4 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2219.9/1852.5/1484.9 -> 2397.1/2142.2/1134.4 | - | noise: high (other apps 12.4% of game CPUs); others on game CPUs 12.4%; clock avg/min 4127/4074 MHz; CORRECTNESS, daytime and noisy machine OK (counters, not timing), 5 min. docs/151 §5 V-job and first live look at D1/D3/D5: routesrc on for the whole load (observe only), legreplanskip, bouncerskip, idlememo alternating in 1-min slices (--ab; their skip counters move only in ON slices). BP crowd (r |
| ip-routesrc-verify | 05:18 (13:18) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2220/1836.5/1492.9 -> 2137/1842.6/1492.9 | - | noise: high (other apps 10.3% of game CPUs); others on game CPUs 10.3%; clock avg/min 4123/4083 MHz; CORRECTNESS, daytime and noisy machine OK, 5 min. As bp-routesrc-verify in IP play (no repro: the docs/151 §2 IP numbers are plain play with boats, water taxis and houseboats). Pass: no crash, game alive, 0 watchdog lines, fixesRequire. Expected non-zero: S1 leg-end replans (routesrc_buckets[1]) an |

### bp-routesrc-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-routesrc-verify: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-22T11:06 UTC, build 69cf2ebc, jobs bp-bouncerskip-offfirst, bp-bouncerskip-onfirst

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-bouncerskip-offfirst | 11:06 (19:06) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2167.2/1856.6/1548.9 -> 2296.4/2065.3/1317.5 | - | noise: high (other apps 22.0% of game CPUs); others on game CPUs 22.0%; clock avg/min 4181/4152 MHz; CLEAN NIGHT ONLY (docs/TESTING.md: other CPU < 10%, else rerun). docs/151 D3 (bouncer at its slot): bouncerskip alternating in 2-min slices, 10 min; only the pair mean of bp-bouncerskip-offfirst and -onfirst counts (docs/143 §8.15). routesrc off (its stack walk would load both arms). Read: route ms |
| bp-bouncerskip-onfirst | 11:18 (19:18) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2165.7/1850.6/1532.9 -> 2278.5/2067.2/1301.5 | - | noise: high (other apps 24.8% of game CPUs); others on game CPUs 24.8%; clock avg/min 4174/4128 MHz; CLEAN NIGHT ONLY (docs/TESTING.md: other CPU < 10%, else rerun). docs/151 D3 (bouncer at its slot): bouncerskip alternating in 2-min slices, 10 min; only the pair mean of bp-bouncerskip-offfirst and -onfirst counts (docs/143 §8.15). routesrc off (its stack walk would load both arms). Read: route ms |

### bp-bouncerskip-offfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-bouncerskip-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
