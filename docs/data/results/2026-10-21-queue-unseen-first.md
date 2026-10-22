# Queue unseen-first 2026-10-21

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/unseen-first-2026-10-21.jsonl`.

## 2026-10-21T01:01 UTC, build 624ecc0, jobs bp-unseen-play, bp-unseen-big-crowd, ip-unseen-play, bp-ut-play, bp-ut-play-onfirst, bp-ut-crowd, bp-ut-crowd-onfirst, bp-ut-stuck-off, bp-ut-stuck-on

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-unseen-play | 01:01 (09:01) | 11.8 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166.9/1855.9/1500.9 -> 2302.8/2088.5/1261.5 | - | noise: high (other apps 21.8% of game CPUs); others on game CPUs 21.8%; clock avg/min 4162/4129 MHz; docs/143 step 2: how much sim-thread locomotion work belongs to agents the player cannot see. Release ini defaults ([RouteAsync], [RouteGate] Sites+Enforce, [Discourage]) plus [UnseenProbe] Enabled=1 (counters v79, observe only, nothing skipped; [UnseenThrottle] stays off). Correctness/data job, da |

Paused after bp-unseen-play (AI usage 5h 1% (reset 14:00 PHT) | 7d 80% (reset 14:00 PHT) | read 5 min ago); resume with `--from bp-unseen-big-crowd`.

### bp-unseen-play: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-21T01:27 UTC, build 58e1412, jobs bp-unseen-big-crowd, ip-unseen-play, bp-ut-play, bp-ut-play-onfirst, bp-ut-crowd, bp-ut-crowd-onfirst, bp-ut-stuck-off, bp-ut-stuck-on

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-unseen-big-crowd | 01:27 (09:27) | 9.8 | PASS | PASS: profile 8 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2167.8/1827.2/1548.9 -> 2315.2/2075.7/1277.5 | - | noise: high (other apps 21.1% of game CPUs); others on game CPUs 21.1%; clock avg/min 4168/4133 MHz; docs/143: the same with the rare-big-crowd repro (many walking Sims at once, BP): the steering / avoidance share by class when crowds form. Pass as bp-unseen-play. |
| ip-unseen-play | 01:37 (09:37) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2173/1854.5/1500.9 -> 2180.9/1896.8/1500.9 | - | noise: high (other apps 22.7% of game CPUs); others on game CPUs 22.7%; clock avg/min 4165/4144 MHz; docs/143: the same on Isla Paradiso (boats, water, long walks), 10 min seeded. Pass as bp-unseen-play. |
| bp-ut-play | 01:49 (09:49) | 2.8 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 01:51:34 (agent not answering for 60 s (heartbeat frozen))<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2165.7/1838.9/1548.9 -> 2141.7/1843.4/1548.9 | - | noise: high (other apps 23.5% of game CPUs); others on game CPUs 23.5%; clock avg/min 4196/4147 MHz; [UnseenThrottle] phase 1 A/B, off-first (docs/143 §6.4 G-U2, counters v79 ut*). ts3native 0.36.13 (game-copy/ts3native.asi.0313) with ts3fixes built from the same commit installed (it pushes the active-household positions; with an older ts3fixes NeedViewers=1 keeps every call running and utWhy_7 'n |
| bp-ut-play-onfirst | 01:52 (09:52) | 2.8 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 01:54:37 (agent not answering for 60 s (heartbeat frozen))<br>mono ledger INCOMPLETE: per-item numbers below the table | 1 | 2159.7/1837.1/1532.9 -> 2135.2/1841.7/1532.9 | - | noise: high (other apps 25.3% of game CPUs); others on game CPUs 25.3%; clock avg/min 4182/4168 MHz; [UnseenThrottle] A/B, on-first: the same job as bp-ut-play with the slice order swapped (warm-up on, then on,off,off,on), so the heavy first counted slot (docs/136 §2) lands on the on side here and the off side there. Pass as bp-ut-play; read the two together (mean of the per-key on - off of both j |

### bp-unseen-big-crowd: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 8 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-unseen-play: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

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

Stopped: two jobs in a row crashed.
