# Queue mono-night 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/mono-night-2026-10-20.jsonl`.

## 2026-10-20T18:24 UTC, build 9ec0c07, jobs bp-mt-p4-tls

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-mt-p4-tls | 18:24 (02:24) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2165.6/1855.6/1532.8 -> 2214.8/2078.7/1340.9 | - | noise: high (other apps 12.5% of game CPUs); others on game CPUs 12.5%; clock avg/min 4203/4146 MHz; 1/6 Mono night (quiet overnight run, clean-night rules: read the machine load line; other CPU >= 10% or a clock drop = timing not valid, correctness still counts). asi mono0366 = ts3native 0.36.6 (Mono integration: ledger probe, docs/125 ICs, G-M1..G-M5, H11, P3 OOM fixes I-120-8 + GC_oom_fn wrappe |

### bp-mt-p4-tls: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-20T18:31 UTC, build 9ec0c07, jobs bp-mt-idle-worker

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-mt-idle-worker | 18:31 (02:31) | 6.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2167.6/1856.2/1500.9 -> 2261.8/2050.4/1341.1 | - | noise: high (other apps 11.2% of game CPUs); others on game CPUs 11.2%; clock avg/min 4217/4178 MHz; 2/6 Run only after bp-mt-p4-tls passed. Idle worker, 5 min BP: [MonoThread] Enabled=1 GcLock=1 FrameTls=1 Mode=1 (one worker registered with GC_new_thread under the lock, gate on while it runs; no managed jobs: [AutShadow] off). PASS = 0 crashes, no freeze, 0 watchdog lines; ts3native.log worker st |

### bp-mt-idle-worker: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-20T18:38 UTC, build 9ec0c07, jobs bp-mono-ledger

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-mono-ledger | 18:38 (02:38) | 12 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2160.5/1837.9/1532.9 -> 2254.7/2027.5/1309.5 | - | noise: high (other apps 11.2% of game CPUs); others on game CPUs 11.2%; clock avg/min 4189/4139 MHz; 3/6 Mono ledger probe on 0.36.6 (closes docs/51 I-121-5, I-121-7, I-121-9, I-121-12, I-120-4, I-119-44; settings as tools/harness/queues/mono-ledger-probe.json). Measurement only, 10 min BP: [MonoThread] Enabled=1 Mode=0 (no GcLock/FrameTls: the frame walk reads the vanilla interpreter context), [A |

### bp-mono-ledger: mono ledger probe (docs/51)

    mono ledger: on 0x3 (ledger on, ticks yes, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 25; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 1290; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 44951; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 138869; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-20T18:50 UTC, build 9ec0c07, jobs bp-autshadow-count

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-autshadow-count | 18:50 (02:50) | 6.8 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2180.1/1869.4/1516.9 -> 2247.4/1890.3/1485.3 | - | noise: high (other apps 12.9% of game CPUs); others on game CPUs 12.9%; clock avg/min 4191/4130 MHz; 4/6 [AutShadow] G-A3 count, Mode 0 = cut and count only, no worker, 5 min BP (settings as tools/harness/queues/autshadow-first.json). fixesOn autshadow switches the driver on for this job only. With simler90's core mod installed, G-M1 must accept it: ts3fixes.applied shows 'AutShadow: game assembli |

### bp-autshadow-count: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-20T18:57 UTC, build 5f1eb28, jobs ip-autshadow-count

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-autshadow-count | 18:57 (02:57) | 6.7 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2177.1/1869.9/1500.9 -> 2172.6/1884/1500.9 | - | noise: high (other apps 12.9% of game CPUs); others on game CPUs 12.9%; clock avg/min 4185/4122 MHz; 5/6 Same as bp-autshadow-count on Isla Paradiso (more lots and Sims: the largest snapshot). Same asi, switch, G-M1 log check ('accepted set 1') and pass rule. STOP before Mode R on a crash or requests == 0. |

### ip-autshadow-count: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 5 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-20T19:04 UTC, build 9b49f8c, jobs bp-autshadow-r

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-autshadow-r | 19:04 (03:04) | 12.2 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2180.2/1872.9/1500.9 -> 2209.4/2069.6/1293.5 | - | noise: high (other apps 11.4% of game CPUs); others on game CPUs 11.4%; clock avg/min 4199/4130 MHz; 6/6 Run only after jobs 2, 4 and 5 passed. [AutShadow] Mode 1 = R, gate G-A4, 10 min BP (settings as autshadow-first.json bp-autshadow-r): sim thread re-scores each sampled request at the cut (RNG saved/restored), the M5 worker scores it from the snapshot, ts3fixes compares bit for bit; [MonoThread |

### bp-autshadow-r: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
