# Queue story-mansion 2026-10-24

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/story-mansion-2026-10-24.jsonl`.

## 2026-10-24T06:52 UTC, build 2d24870f, jobs bp-story-mansion-check

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|

## 2026-10-24T06:56 UTC, build cb00eb98, jobs bp-story-mansion-check

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-mansion-check | 06:56 (14:56) | 1.6 | **FAIL** | FAIL: profile 25 min, seed 12345 (rng-compare.ts)<br>FAIL: household creation failed: full-house: fit: Luxurious European Mansion has no room in the house for crib (Kinder Kontainer)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: story: no story step ran; no days summary (day planner did not run or the run stopped early) | 1 | 2177.7/1842/1516.9 -> 2198.4/1874.5/1499.3 | - | noise: medium (other apps 6.0% of game CPUs); others on game CPUs 6.0%; clock avg/min 4245/4237 MHz; Story check on Story-BP-Mansion (Rodney 2026-10-24: Bridgeport + MTS "Luxurious European Mansion", base game, no CC, 40x30, built for 8, placed empty). The harness generates the 8-Sim full-house household and moves it in the player's way (the move-in targets the mansion by lot id (story.householdLo |

### bp-story-mansion-check: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)

## 2026-10-24T06:59 UTC, build 8a3b27a3, jobs bp-story-mansion-check

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-story-mansion-check | 06:59 (14:59) | 28 | **FAIL** | PASS: profile 25 min, seed 12345 (rng-compare.ts)<br>mono ledger INCOMPLETE: per-item numbers below the table<br>FAIL: story: Freya Northcott missed work/school on day 16, 18; Rosa Northcott missed work/school on day 16; Kira Northcott missed work/school on day 16; Nora Northcott missed work/school on day 16 | 0 | 2160.4/1823.6/1548.9 -> 2394.7/2098/1243.8 | - | noise: medium (other apps 12.6% of game CPUs); others on game CPUs 12.6%; clock avg/min 4212/4096 MHz; Story check on Story-BP-Mansion (Rodney 2026-10-24: Bridgeport + MTS "Luxurious European Mansion", base game, no CC, 40x30, built for 8, placed empty). The harness generates the 8-Sim mansion-eight household (full-house with the toddler as a child: the mansion has no crib and no room for one, 202 |

### bp-story-mansion-check: mono ledger probe (docs/51)

    mono ledger: on 0x0 (ledger OFF, ticks NO, scope NO, frames no, task no); 0 meta scopes over 25 min, 0 ms inside (wall), full/unmatched 0, records dropped 0
    I-121-5 RNG: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; caller chains 0, outside the two expected paths 0 (0 calls)
    I-121-7 Query_GetObjects: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -
    I-121-12 Query_GetObjectsInRange: in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 classes: -; not Sim: 0
    I-121-9 Slots_GetPositionOfSlotImpl (slot helper 0x76F0A0): in meta scoring 0 (0.0/min, -/scope), elsewhere 0; 0 distinct objects, 0 caller chains
    I-120-4 [0x11DC18C] at each Simulate: 0 ticks, 0 non-zero -> no data
    I-119-44 Sleep(0) resume ticks 0:0 1:0 2:0 3+:0; Sleep(1) 0:0 1:0 2:0 3+:0 -> no data (probe task did not run)
