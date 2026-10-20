# Queue release-reload 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/release-reload-2026-10-20.jsonl`.

## 2026-10-20T13:26 UTC, build 3725da6, jobs bp-reload-run-2, sv-reload-run-4, ip-reload-run-3

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-reload-run-2 | 13:26 (21:26) | 4.8 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts) | 0 | 2192.2/1868.6/1500.9 -> 2157/1902.4/1484.9 | - | noise: high (other apps 30.5% of game CPUs); others on game CPUs 30.5%; clock avg/min 4153/4079 MHz; Release v0.1.4 reload check (Rodney 2026-10-20): load Matrix-NewDowntownWorld-run-2, a save the release build (ts3native 0.36.0, release ini) wrote today with background routing on (saved unpaused), then 3 min seeded play. Pass: the save loads (world loaded, in live mode, households > 0), 0 crashes |
| sv-reload-run-4 | 13:31 (21:31) | 4.7 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts) | 0 | 2149/1785.8/1596.8 -> 2105.4/1813.5/1580.9 | - | noise: high (other apps 28.1% of game CPUs); others on game CPUs 28.1%; clock avg/min 4132/4023 MHz; Release v0.1.4 reload check (Rodney 2026-10-20): load Matrix-SunsetValley-run-4, a save the release build (ts3native 0.36.0, release ini) wrote today with background routing on (saved unpaused), then 3 min seeded play. Pass: the save loads (world loaded, in live mode, households > 0), 0 crashes, 0  |
| ip-reload-run-3 | 13:36 (21:36) | 4.6 | PASS | PASS: profile 3 min, seed 12345 (rng-compare.ts) | 0 | 2189/1862.9/1516.9 -> 2152/1864.7/1516.9 | - | noise: high (other apps 26.1% of game CPUs); others on game CPUs 26.1%; clock avg/min 4176/4148 MHz; Release v0.1.4 reload check (Rodney 2026-10-20): load Matrix-IslaParadiso-run-3, a save the release build (ts3native 0.36.0, release ini) wrote today with background routing on (saved unpaused), then 3 min seeded play. Pass: the save loads (world loaded, in live mode, households > 0), 0 crashes, 0  |
