# Queue routing-mods-compat 2026-10-22

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/routing-mods-compat-2026-10-22.jsonl`.

## 2026-10-22T06:03 UTC, build 1ff0a60, jobs ip-mods-on, ip-houseboatfix-off, ip-oceanplay-off, bp-mods-on, bp-footprints-off

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-mods-on | 06:03 (14:03) | 6.9 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2232.5/1856.8/1492.9 -> 2132/1862.7/1492.9 | - | noise: high (other apps 41.0% of game CPUs, NOT comparable (load or clock)); others on game CPUs 41.0%; clock avg/min 3894/3340 MHz; Reference arm for the ip-* jobs: every routing mod installed (as the installer default). Read: routesrc_buckets (plans/us/fails per source; [2] houseboat = S2), routesrc_houseboat_legs, total route plans and fails, watchdog resets (counters 46-49), stuck-Sim lines. P |
| ip-houseboatfix-off | 06:10 (14:10) | 7.2 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2143/1850.9/1516.8 -> 2165.5/1862.7/1516.8 | - | noise: high (other apps 53.3% of game CPUs, NOT comparable (load or clock)); others on game CPUs 53.3%; clock avg/min 3677/3228 MHz; docs/153 §3.1: House Boat Fix out, simler90 UPDATE242 in (it carries the same gangway condition). Expected if redundant: S2 houseboat plans, fails and legs within noise of ip-mods-on; stack frames name SimRoutingComponent.EnterHouseboatThroughGangway instead of fp.ho |
| ip-oceanplay-off | 06:17 (14:17) | 7.4 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2154.6/1855.5/1484.9 -> 2145.4/1860.7/1484.9 | - | noise: high (other apps 51.2% of game CPUs); others on game CPUs 51.2%; clock avg/min 3568/3219 MHz; docs/153 §3.5: Autonomous Play In The Ocean Fix out. Expected if the mod cuts ocean trips: more plans and plan us in routesrc 'other' and more water routes (Sims walking to the ocean from inland lots) than ip-mods-on. Read the same counters as ip-mods-on plus the interaction log for GoAndPlayInOcea |
| bp-mods-on | 06:25 (14:25) | 8.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2222.6/1853/1484.9 -> 2268.3/1950.4/1421.1 | - | noise: high (other apps 38.7% of game CPUs); others on game CPUs 38.7%; clock avg/min 3811/3304 MHz; Reference arm for bp-footprints-off: BP crowd (rare-big-crowd: 30 Sims, home lot, farthest community lot, home again), every routing mod installed. Read: routesrc_buckets (plans/us/fails per source), total route plans and fails, RouteBackoff counters, watchdog resets (counters 46-49), stuck-Sim lin |
| bp-footprints-off | 06:33 (14:33) | 8.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2220.8/1847.2/1462.2 -> 2331.1/2080.3/1247.4 | - | noise: high (other apps 42.5% of game CPUs, NOT comparable (load or clock)); others on game CPUs 42.5%; clock avg/min 3818/3255 MHz; docs/153 §3.3/3.4: OhRudi Sims/Pets need less Space (jig footprint polygons halved) and Nikel23 sloped terrain (jig OBJD slope bit) out. Expected if the mods help: more route fails and RouteBackoff entries, same or fewer plans than bp-mods-on (failed jig placements d |

### ip-mods-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-houseboatfix-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-oceanplay-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-mods-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-footprints-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
