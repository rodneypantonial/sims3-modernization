# Queue live-reads 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/live-reads-2026-10-18.jsonl`.

## 2026-10-18T11:35 UTC, build 08c1ecf, jobs bp-inroom-reads

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-inroom-reads | 11:35 (19:35) | 8.5 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2134.3/1855.2/1532.9 -> 2371.9/2101.7/1154.8 | - | docs/111 / docs/51 §47: in-room routing live reads, read-only Frida (tools/frida/inroom-reads.js), no native change. Tree root-border dumps at the window start and ~25 s before the end (I-98-15), SetBounds lot rectangle and c, planner +0xB8 and static 0x11CF02C (I-98-21, I-101-4), X+0x50 / X+0x4C per in-room caller (U6, U7). RouteWorker and RouteAsync off so every plan, and so every dump, runs on  |

## 2026-10-18T11:44 UTC, build b65c870, jobs ip-inroom-reads

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-inroom-reads | 11:44 (19:44) | 8.4 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2154.4/1855.1/1564.9 -> 2170/1898.3/1483.1 | - | bp-inroom-reads on Isla Paradiso (houseboat lots, water, EP objects; more lot shapes for the root-border and SetBounds reads). Queue total ~16 min. |
