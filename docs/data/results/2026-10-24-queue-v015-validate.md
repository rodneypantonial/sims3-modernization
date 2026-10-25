# Queue v015-validate 2026-10-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/v015-validate-2026-10-24.jsonl`.

## 2026-10-24T08:45 UTC, build 85cf1978, jobs bp-rc-smoke-v015pkg, ip-rc-smoke-v015pkg, sv-rc-smoke-v015pkg, bp-rel-matrix-v015pkg, bp-rel-travel-france-v015pkg, bp-rc-soak-v015pkg

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-24T08:55 UTC, build 85cf1978, jobs bp-rc-smoke-v015pkg, ip-rc-smoke-v015pkg, sv-rc-smoke-v015pkg, bp-rel-matrix-v015pkg, bp-rel-travel-france-v015pkg, bp-rc-soak-v015pkg

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rc-smoke-v015pkg | 08:55 (16:55) | 0 | **FAIL** | cycle.ts --restart exit 1 | - | ? -> ? | - | Release check part a (smoke): 6 min play, release ini + measurement logging. Pass: 0 crashes, 0 watchdog lines, ts3native version as docs/RELEASE.md, asyncStarted > 0, every tripwire of docs/RELEASE.md 0, route fails per minute not above v0.1.4 (2026-10-20 release-validate *-rel-play) by > 10%. Read: freeze-report.ts and hitch-ab.ts <tag>. |
| ip-rc-smoke-v015pkg | 08:55 (16:55) | 0 | **FAIL** | cycle.ts --restart exit 1 | - | ? -> ? | - | Release check part a (smoke): 6 min play, release ini + measurement logging. Pass: 0 crashes, 0 watchdog lines, ts3native version as docs/RELEASE.md, asyncStarted > 0, every tripwire of docs/RELEASE.md 0, route fails per minute not above v0.1.4 (2026-10-20 release-validate *-rel-play) by > 10%. Read: freeze-report.ts and hitch-ab.ts <tag>. |
| sv-rc-smoke-v015pkg | 08:55 (16:55) | 0 | **FAIL** | cycle.ts --restart exit 1 | - | ? -> ? | - | Release check part a (smoke): 6 min play, release ini + measurement logging. Pass: 0 crashes, 0 watchdog lines, ts3native version as docs/RELEASE.md, asyncStarted > 0, every tripwire of docs/RELEASE.md 0, route fails per minute not above v0.1.4 (2026-10-20 release-validate *-rel-play) by > 10%. Read: freeze-report.ts and hitch-ab.ts <tag>. |
| bp-rel-matrix-v015pkg | 08:55 (16:55) | 0 | **FAIL** | cycle.ts --restart exit 1 | - | ? -> ? | - | Release v0.2.0 validation (ts3native release build parked as game-copy/ts3native.asi.rel020, sha256 in docs/RELEASE.md v0.2.0; release ini = the repo ini after node tools/release-features.ts <chosen> --write; no job ini overrides). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first line "ts3native <version in docs/RELEASE.md v0.2.0>", stuck Sims within docs/TESTING.md (no more than |
| bp-rel-travel-france-v015pkg | 08:55 (16:55) | 0 | **FAIL** | cycle.ts --restart exit 1 | - | ? -> ? | - | Release v0.2.0 validation (ts3native release build parked as game-copy/ts3native.asi.rel020, sha256 in docs/RELEASE.md v0.2.0; release ini = the repo ini after node tools/release-features.ts <chosen> --write; no job ini overrides). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first line "ts3native <version in docs/RELEASE.md v0.2.0>", stuck Sims within docs/TESTING.md (no more than |

## 2026-10-24T08:55 UTC, build 85cf1978, jobs bp-rc-smoke-v015pkg, ip-rc-smoke-v015pkg, sv-rc-smoke-v015pkg, bp-rel-matrix-v015pkg, bp-rel-travel-france-v015pkg, bp-rc-soak-v015pkg

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-rc-smoke-v015pkg | 08:55 (16:55) | 7.8 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2178.6/1850.3/1500.9 -> 2396.7/2159.4/1195.8 | asyncStarted +6074, asyncRefused +77, asyncCollected +6074, asyncUs +33126837, asyncMaxUs +168253, asyncPumpUs +19519834, asyncPumpWaits +6982, asyncRefusedM1Pending +13, asyncBackoffs +7, asyncQueueFull +1, asyncQueueMax +4 | noise: high (other apps 24.9% of game CPUs); others on game CPUs 24.9%; clock avg/min 4170/4137 MHz; Release check part a (smoke): 6 min play, release ini + measurement logging. Pass: 0 crashes, 0 watchdog lines, ts3native version as docs/RELEASE.md, asyncStarted > 0, every tripwire of docs/RELEASE.md 0, route fails per minute not above v0.1.4 (2026-10-20 release-validate *-rel-play) by > 10%. Rea |
| ip-rc-smoke-v015pkg | 09:03 (17:03) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2222.4/1876.3/1500.9 -> 2264.3/2056.7/1323.3 | asyncStarted +2539, asyncRefused +52, asyncCollected +2536, asyncDropped +3, asyncUs +38511788, asyncMaxUs +465491, asyncPumpUs +27849377, asyncPumpWaits +5224, asyncRefusedM1Pending +13, asyncBackoffs +2, asyncQueueMax +4 | noise: high (other apps 27.5% of game CPUs); others on game CPUs 27.5%; clock avg/min 4154/4072 MHz; Release check part a (smoke): 6 min play, release ini + measurement logging. Pass: 0 crashes, 0 watchdog lines, ts3native version as docs/RELEASE.md, asyncStarted > 0, every tripwire of docs/RELEASE.md 0, route fails per minute not above v0.1.4 (2026-10-20 release-validate *-rel-play) by > 10%. Rea |
| sv-rc-smoke-v015pkg | 09:11 (17:11) | 7.7 | PASS | PASS: profile 6 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2144.6/1803.5/1548.9 -> 2160.7/1986.3/1353.8 | asyncStarted +1953, asyncRefused +92, asyncCollected +1953, asyncUs +27606526, asyncMaxUs +947452, asyncPumpUs +16406432, asyncPumpWaits +4594, asyncRefusedM1Pending +3, asyncBackoffs +2, asyncQueueMax +3 | noise: high (other apps 25.5% of game CPUs); others on game CPUs 25.5%; clock avg/min 4173/4085 MHz; Release check part a (smoke): 6 min play, release ini + measurement logging. Pass: 0 crashes, 0 watchdog lines, ts3native version as docs/RELEASE.md, asyncStarted > 0, every tripwire of docs/RELEASE.md 0, route fails per minute not above v0.1.4 (2026-10-20 release-validate *-rel-play) by > 10%. Rea |
| bp-rel-matrix-v015pkg | 09:19 (17:19) | 10.4 | PASS | overwrite: PASS<br>back-to-back: PASS<br>unpaused: PASS<br>build-mode: PASS<br>verify-counters: PASS<br>== reload: paused: save all off, then all on; load each (two restarts): households match, 60 s at speed 3, game alive; FAIL only on ScriptErrors with ON that OFF does not have<br>reload: PENDING (reload commands above)<br>summary: 8 cases, 8 passed, 0 failed, pending: reload<br>reload on: reload | 0 | 2160.7/1851.2/1500.9 -> 2132.9/1861.5/1500.9 | - | noise: high (other apps 20.6% of game CPUs); others on game CPUs 20.6%; clock avg/min 4194/4016 MHz; Release v0.2.0 validation (ts3native release build parked as game-copy/ts3native.asi.rel020, sha256 in docs/RELEASE.md v0.2.0; release ini = the repo ini after node tools/release-features.ts <chosen> --write; no job ini overrides). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first  |
| bp-rel-travel-france-v015pkg | 09:29 (17:29) | 4.3 | PASS | travel: PASS<br>summary: 1 cases, 1 passed, 0 failed<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2158.7/1834.4/1500.9 -> 2201.9/1864.1/1484.9 | - | noise: medium (other apps 7.0% of game CPUs); others on game CPUs 7.0%; clock avg/min 4230/4200 MHz; Release v0.2.0 validation (ts3native release build parked as game-copy/ts3native.asi.rel020, sha256 in docs/RELEASE.md v0.2.0; release ini = the repo ini after node tools/release-features.ts <chosen> --write; no job ini overrides). Pass (every job): 0 crashes, 0 watchdog lines, ts3native.log first  |
| bp-rc-soak-v015pkg | 09:33 (17:33) | 46.9 | PASS | PASS: profile 45 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table | 0 | 2166.3/1851.8/1500.9 -> 2381.3/2155.5/1211.4 | asyncStarted +57949, asyncRefused +882, asyncCollected +57924, asyncDropped +24, asyncUs +355015541, asyncMaxUs +256821, asyncPumpUs +209525926, asyncPumpWaits +37557, asyncRefusedM1Pending +134, asyncSkipped +1, asyncBackoffs +56, asyncQueueFull +25, asyncQueueMax +5 | noise: high (other apps 21.4% of game CPUs); others on game CPUs 21.4%; clock avg/min 4187/3919 MHz; Release check part f (soak): 45 min natural play, no repro. Pass: no crash, 0 watchdog lines, stuck Sims within docs/TESTING.md. Read: hitch-ab.ts <tag> (docs/161 real-hitch counts, p99, max), freeze-report.ts <tag>, VAS growth = the row's commit/private/largest free MB before -> after (v0.1.4 bp-r |

### bp-rc-smoke-v015pkg: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### ip-rc-smoke-v015pkg: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### sv-rc-smoke-v015pkg: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 6 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rel-matrix-v015pkg: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rel-travel-france-v015pkg: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 10 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

### bp-rc-soak-v015pkg: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 45 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
