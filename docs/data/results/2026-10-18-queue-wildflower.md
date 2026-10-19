# Queue wildflower 2026-10-18

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/wildflower-2026-10-18.jsonl`.

## 2026-10-18T11:21 UTC, build 788150b, jobs bp-firstmin-wildflowerprobe

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-firstmin-wildflowerprobe | 11:21 (19:21) | 7.1 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2240.2/1841.1/1516.9 -> 2343.8/2065.6/1254.4 | - | docs/110 §7.5 verify run: bp-firstmin-taskstall with ts3fixes wildflowerprobe on (repro wildflower-probe sets it at the window start; game behaviour unchanged). Read the patches step: wildflower_* counters and wildflower_log (tries, max/sum ms per fire: 20 failing searches or one long one), and the taskstall rows against probe-out/20261018-182629-taskstall.jsonl |

## 2026-10-18T11:28 UTC, build 0d726ae, jobs ip-firstmin-wildflowerprobe

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-firstmin-wildflowerprobe | 11:28 (19:28) | 7.3 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2304.5/1911.6/1484.8 -> 2218.4/1921.1/1484.8 | - | bp-firstmin-wildflowerprobe in Isla Paradiso; the wildflower_log simmin column explains the IP fire spacing (docs/110 §2) |

## 2026-10-18T11:55 UTC, build de44bff, jobs bp-firstmin-wildflowerprobe

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-firstmin-wildflowerprobe | 11:55 (19:55) | 7.3 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2214.7/1852.8/1452.9 -> 2250.4/1929.2/1452.9 | - | docs/110 §7.5 verify run: bp-firstmin-taskstall with ts3fixes wildflowerprobe on (repro wildflower-probe sets it at the window start; game behaviour unchanged). Read the patches step: wildflower_* counters and wildflower_log (tries, max/sum ms per fire: 20 failing searches or one long one), and the taskstall rows against probe-out/20261018-182629-taskstall.jsonl |

## 2026-10-18T16:46 UTC, build 18af736, jobs bp-ab-wildflowertask

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-ab-wildflowertask | 16:46 (00:46) | 11.7 | PASS | PASS: profile 10 min, seed 12345 (rng-compare.ts) | 0 | 2132.5/1852.5/1516.9 -> 2322.7/2057.5/1220.7 | - | docs/110 W1 in-load A/B: repro wildflower-drain (probe on all load, drain refilled with 150 community lots, a fire per 30 sim-minutes) with wildflowertask alternating (warm-up, then ABBA, 2 min slices). Pass: no /taskstall row >= 250 ms from OnGrowWildflowersOnLot in the on slices, placed/fires ratio the same in both halves, no script errors. Only meaningful if the probe jobs showed 20 failing tri |
