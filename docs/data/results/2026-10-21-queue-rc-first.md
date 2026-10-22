# Queue rc-first 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/rc-first-2026-10-21.jsonl`.

## 2026-10-21T02:30 UTC, build eae00d6, jobs bp-ut-play, bp-ut-play-onfirst, bp-ut-crowd, bp-ut-crowd-onfirst, bp-ut-stuck-off, bp-ut-stuck-on, bp-easpread-lots-repro, bp-easpread-pets-repro, bp-easpread-decay-repro, bp-ab-easpread, bp-errortrap-spread-repro, bp-errortrap-spread-yield, bp-errortrap-load-off, bp-errortrap-load-on

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ut-play | 02:30 (10:30) | 11.9 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2225.9/1853.8/1468.9 -> 2390.9/2085.7/1190.2 | - | noise: high (other apps 19.4% of game CPUs); others on game CPUs 19.4%; clock avg/min 4146/4102 MHz; [UnseenThrottle] phase 1 A/B, off-first (docs/143 §6.4 G-U2, counters v79 ut*). ts3native 0.36.13 (game-copy/ts3native.asi.0314) with ts3fixes built from the same commit installed (it pushes the active-household positions; with an older ts3fixes NeedViewers=1 keeps every call running and utWhy_7 'n |
| bp-ut-play-onfirst | 02:42 (10:42) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2134.9/1860.1/1532.9 -> 2416/2117/1204.7 | - | noise: high (other apps 17.6% of game CPUs); others on game CPUs 17.6%; clock avg/min 4144/4101 MHz; [UnseenThrottle] A/B, on-first: the same job as bp-ut-play with the slice order swapped (warm-up on, then on,off,off,on), so the heavy first counted slot (docs/136 §2) lands on the on side here and the off side there. Pass as bp-ut-play; read the two together (mean of the per-key on - off of both j |
| bp-ut-crowd | 02:54 (10:54) | 9.9 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2138.8/1857.3/1484.9 -> 2248.7/2082.2/1277.1 | - | noise: high (other apps 16.9% of game CPUs); others on game CPUs 16.9%; clock avg/min 4148/4121 MHz; [UnseenThrottle] A/B, off-first, door-heavy crowd (rare-big-crowd repro: many walking Sims at once): as bp-ut-play, 8 min. Pass as bp-ut-play, plus utWhy_11 (portal) > 0 (the sub-path rule fires on doors) and no Sim stuck at a door at the end. |
| bp-ut-crowd-onfirst | 03:04 (11:04) | 9.9 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2220.8/1857.9/1484.9 -> 2353.6/2070.3/1214.2 | - | noise: high (other apps 16.1% of game CPUs); others on game CPUs 16.1%; clock avg/min 4142/4112 MHz; [UnseenThrottle] A/B, on-first, rare-big-crowd: bp-ut-crowd with the slice order swapped. Pass as bp-ut-crowd; read the two together. |
| bp-ut-stuck-off | 03:15 (11:15) | 16.9 | PASS | PASS: profile 15 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2136.4/1856.2/1500.8 -> 2299/2100.7/1243.3 | - | noise: high (other apps 11.8% of game CPUs); others on game CPUs 11.8%; clock avg/min 4154/4093 MHz; [UnseenThrottle] stuck-Sim check, reference arm: release defaults with [UnseenThrottle] off (Enabled=0) and [UnseenProbe] on, BP 15 min, seed 12345, no --ab. Correctness only, daytime OK. Read the end-of-run stuck Sims (docs/TESTING.md rule) as the reference for bp-ut-stuck-on. |
| bp-ut-stuck-on | 03:32 (11:32) | 16.8 | PASS | PASS: profile 15 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2248.3/1852.1/1484.9 -> 2344.2/2115.8/1269.1 | - | noise: medium (other apps 7.8% of game CPUs); others on game CPUs 7.8%; clock avg/min 4201/4172 MHz; [UnseenThrottle] stuck-Sim check, throttle on for the whole run (Enabled=1, utMode 1 from the start, no --ab), otherwise as bp-ut-stuck-off. Pass: no crash, 0 watchdog lines, utSkipped > 0, stuck Sims at the end no more than bp-ut-stuck-off (docs/TESTING.md: at most one more, and none standing at a |
| bp-easpread-lots-repro | 03:49 (11:49) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2230.1/1849.8/1484.9 -> 2276.7/2076.2/1293.1 | - | noise: medium (other apps 9.4% of game CPUs); others on game CPUs 9.4%; clock avg/min 4182/4094 MHz; docs/147 §2 correctness first (5 min): repro bp-easpread-lots under /taskstall. Pass: both easpread_log lines show the same lots per kind and Sim count, ours > 1 tick with easpread_lots_max_tick_us < ~5000 and verify 'after' equal to the game's; the game's pass shows one >= 250 ms stall under LotMa |
| bp-easpread-pets-repro | 03:56 (11:56) | 6.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2230.6/1855.3/1484.9 -> 2315.1/2096.8/1277.1 | - | noise: high (other apps 19.9% of game CPUs); others on game CPUs 19.9%; clock avg/min 4160/4132 MHz; docs/147 §3 (5 min): repro bp-easpread-pets under /taskstall. Pass: test pet 1 tick on the game's path, > 1 tick on ours (easpread_pets_max_ticks > 1), no 'failed' line, no >= 250 ms stall under PetPoolManager/GeneticsPet on our side; the real alarm's line follows |
| bp-easpread-decay-repro | 04:03 (12:03) | 6.9 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2229.9/1853.8/1468.9 -> 2213/2045.4/1293.1 | - | noise: high (other apps 16.6% of game CPUs); others on game CPUs 16.6%; clock avg/min 4152/4111 MHz; docs/147 §4 (5 min): repro bp-easpread-decay under /taskstall. Pass: ours > 1 tick, easpread_decay_max_tick_us < ~5000, easpread_decay_threw 0, both verify lines (relationships after <= before); the game's DecayAll is one tick (a stall only on old saves) |
| bp-ab-easpread | 04:10 (12:10) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2237.7/1856.3/1500.8 -> 2278.2/2078.5/1292.9 | - | noise: high (other apps 18.1% of game CPUs); others on game CPUs 18.1%; clock avg/min 4143/4109 MHz; docs/147 in-load A/B, seeded BP, 10 min, 2-min slices, easpreadlots + easpreadpets + easpreaddecay alternating together (each decided when its alarm fires; a pass started in an on slice may finish in the next). Natural fires only: 3 AM venue cleanup and midnight decay once per sim-day, pet alarm ev |
| bp-errortrap-spread-repro | 04:22 (12:22) | 6.9 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2249.8/1859.5/1468.9 -> 2227.5/2060.9/1293.1 | - | noise: high (other apps 22.9% of game CPUs); others on game CPUs 22.9%; clock avg/min 4135/4030 MHz; docs/146 correctness first (5 min): repro bp-errortrap-spread-inventory under /taskstall, errortrapspread on for the whole run (set while paused, before the load-up scan's 1 sim-minute alarm fires). Pass: the load-up check ran ours (etspread_passes >= 1 before the repro steps, applied log 'Inventor |
| bp-errortrap-spread-yield | 04:29 (12:29) | 6.9 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2145.4/1853.6/1446.2 -> 2265.1/2081.7/1318.2 | - | noise: high (other apps 20.6% of game CPUs); others on game CPUs 20.6%; clock avg/min 4150/4116 MHz; docs/146 §3 marker contract on the installed ErrorTrap (5 min): repro bp-errortrap-spread-upstream-yield. Pass: etspread_upstream -1, etspread_theirs >= 1, the 'upstream spreading found (faked), ours off' line, no crash |
| bp-errortrap-load-off | 04:36 (12:36) | 6.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2143.3/1856.5/1478 -> 2273.1/2072.1/1302.2 | - | noise: high (other apps 20.8% of game CPUs); others on game CPUs 20.8%; clock avg/min 4173/4070 MHz; docs/146 load-time hitch, fresh load, switch off (5 min, /taskstall 250, no repro): the load-up scan cannot be repeated in one load, so this is the off side of a two-load A/B with bp-errortrap-load-on (same save, seed, build). Pass: one taskstall row of 0.5-0.9 s from ErrorTrap's load-up task about |
| bp-errortrap-load-on | 04:43 (12:43) | 6.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2141.7/1849.3/1500.8 -> 2222.6/2078.4/1275.3 | - | noise: high (other apps 16.5% of game CPUs); others on game CPUs 16.5%; clock avg/min 4192/4141 MHz; docs/146 load-time hitch, fresh load, switch on (5 min, /taskstall 250, no repro): the on side of the two-load A/B. Pass: etspread_passes 1, etspread_failed 0, no taskstall row with Inventories.GetName / CheckInventories samples; ErrorTrap's remaining first-tick row (controllers' PreProcess pass, e |

### bp-ut-play: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut-play-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut-crowd: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut-crowd-onfirst: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut-stuck-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ut-stuck-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 15 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-easpread-lots-repro: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-easpread-pets-repro: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-easpread-decay-repro: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-ab-easpread: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-errortrap-spread-repro: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-errortrap-spread-yield: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-errortrap-load-off: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-errortrap-load-on: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-21T08:31 UTC, build 02b4324, jobs bp-easpread-lots-repro, bp-easpread-pets-repro, bp-easpread-decay-repro, bp-errortrap-spread-repro

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-easpread-lots-repro | 08:31 (16:31) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2236.4/1854.2/1468.9 -> 2227.2/2045.2/1309.1 | - | noise: high (other apps 12.7% of game CPUs); others on game CPUs 12.7%; clock avg/min 4190/4122 MHz; docs/147 §2 correctness first (5 min): repro bp-easpread-lots under /taskstall. Pass: both easpread_log lines show the same lots per kind and Sim count, ours > 1 tick with easpread_lots_max_tick_us < ~5000 and verify 'after' equal to the game's; the game's pass shows one >= 250 ms stall under LotMa |
| bp-easpread-pets-repro | 08:38 (16:38) | 7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2234.4/1854.7/1446.2 -> 2234.5/2056.9/1302.2 | - | noise: high (other apps 17.5% of game CPUs); others on game CPUs 17.5%; clock avg/min 4138/4099 MHz; docs/147 §3 (5 min): repro bp-easpread-pets under /taskstall. Pass: test pet 1 tick on the game's path, > 1 tick on ours (easpread_pets_max_ticks > 1), no 'failed' line, no >= 250 ms stall under PetPoolManager/GeneticsPet on our side; the real alarm's line follows |
| bp-easpread-decay-repro | 08:45 (16:45) | 6.9 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2140.4/1857/1532.9 -> 2287.5/2063.2/1315.4 | - | noise: high (other apps 14.3% of game CPUs); others on game CPUs 14.3%; clock avg/min 4146/4099 MHz; docs/147 §4 (5 min): repro bp-easpread-decay under /taskstall. Pass: ours > 1 tick, easpread_decay_max_tick_us < ~5000, easpread_decay_threw 0, both verify lines (relationships after <= before); the game's DecayAll is one tick (a stall only on old saves) |
| bp-errortrap-spread-repro | 08:52 (16:52) | 6.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2235.9/1852.3/1484.9 -> 2249.3/2107.3/1277.1 | - | noise: high (other apps 13.4% of game CPUs); others on game CPUs 13.4%; clock avg/min 4148/4110 MHz; docs/146 correctness first (5 min): repro bp-errortrap-spread-inventory under /taskstall, errortrapspread on for the whole run (set while paused, before the load-up scan's 1 sim-minute alarm fires). Pass: the load-up check ran ours (etspread_passes >= 1 before the repro steps, applied log 'Inventor |

### bp-easpread-lots-repro: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-easpread-pets-repro: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-easpread-decay-repro: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-errortrap-spread-repro: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
