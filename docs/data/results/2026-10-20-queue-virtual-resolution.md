# Queue virtual-resolution 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/virtual-resolution-2026-10-20.jsonl`.

## 2026-10-20T19:16 UTC, build 3643ebe, jobs bp-vres

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-vres | 19:16 (03:16) | 4.3 | **CRASH** | FAIL: profile 10 min, seed 12345 (rng-compare.ts)<br>FAIL: game died at 19:19:25 (agent not answering for 61 s (TimeoutError: The operation was aborted due to timeout)) | 1 | 2163.7/1857.2/1516.9 -> ? | - | noise: high (other apps 12.7% of game CPUs); others on game CPUs 12.7%; clock avg/min 4219/4141 MHz; docs/133 / docs/51 §186. Release defaults (repo ts3native.ini, no experiment switched on) plus two read-only probes for one 10 min seeded profile: (1) managed: the probe agent snapshots every mint callvirt inline cache it can reach (/vcache, profile.vres) every 60 s and once at the end into <tag>-v |
