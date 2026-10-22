# Queue worldimage 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/worldimage-2026-10-20.jsonl`.

## 2026-10-20T21:08 UTC, build 5700532, jobs bp-worldimage, ip-worldimage

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-worldimage | 21:08 (05:08) | 4.1 | **FAIL** | FAIL: worldimage 0/2 captures; capture 1: request not taken (ts3native sees it only while the sim ticks) for ~60 s (fail-fast guard) | 1 | 2168.3/1841.9/1532.9 -> 2179.5/1839.7/1516.9 | - | noise: high (other apps 13.0% of game CPUs); others on game CPUs 13.0%; clock avg/min 4201/4164 MHz; Mono M3 first real world images (docs/123 §8.6). CORRECTNESS ONLY, never a timing run: each capture stalls the game a few seconds. [WorldImage] Enabled=1, MaxCaptures=2; queue.ts fills OutDir=<repo>\game-copy\worldimage (git-ignored: images hold the game's whole memory, personal data included) and  |
| ip-worldimage | 21:12 (05:12) | 4.1 | **FAIL** | FAIL: worldimage 0/2 captures; capture 1: request not taken (ts3native sees it only while the sim ticks) for ~60 s (fail-fast guard) | 1 | 2178.7/1855.6/1516.9 -> 2159.2/1850/1532.9 | - | noise: high (other apps 13.0% of game CPUs); others on game CPUs 13.0%; clock avg/min 4190/4124 MHz; Same as bp-worldimage on Isla Paradiso (more lots, boats, water). CORRECTNESS ONLY, never a timing run. ~4 GB disk for 2 captures. |
