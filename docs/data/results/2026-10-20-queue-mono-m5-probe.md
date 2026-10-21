# Queue mono-m5-probe 2026-10-20

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/mono-m5-probe-2026-10-20.jsonl`.

## 2026-10-20T04:35 UTC, build 73f0c5c, jobs bp-mt-p1, bp-mt-gates-off, bp-mt-idle-worker

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-mt-p1 | 04:35 (12:35) | 6.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2160.9/1849.1/1532.9 -> 2221.5/1922.1/1484.9 | - | noise: high (other apps 26.6% of game CPUs); others on game CPUs 26.6%; clock avg/min 4134/4062 MHz; Mono M5 probe (docs/30 §14, docs/120). Correctness/smoke only, timing noisy: 5 min profile, no A/B, no perf verdict. Answers I-120-1 (which thread is registered with Boehm today): [MonoThread] Enabled=1 Mode=0 (P1/P2 only, no GcLock, no FrameTls, no worker). Read the v18 counters in -native.json (m |

## 2026-10-20T07:21 UTC, build a164854, jobs bp-mt-gates-off, bp-mt-idle-worker, ip-mt-arena-rate

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-mt-gates-off | 07:21 (15:21) | 6.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2199.6/1891.8/1500.9 -> 2277.8/2111.5/1335.6 | - | noise: high (other apps 15.6% of game CPUs); others on game CPUs 15.6%; clock avg/min 4185/4096 MHz; Mono M5 probe (docs/30 §14, docs/120 §9). Correctness/smoke only, timing noisy: 5 min profile, no A/B, no perf verdict. asi autshadow2 = master 0.35.22 (mtprobe v2: ArenaFix + N2mPrivate). Patches installed, gates off: [MonoThread] Enabled=1 GcLock=1 FrameTls=1 Mode=0 ArenaFix=1 N2mPrivate=1 (P3 19 |
| bp-mt-idle-worker | 07:28 (15:28) | 6.6 | PASS | PASS: profile 5 min, seed 12345 (rng-compare.ts) | 0 | 2165.5/1854.8/1532.9 -> 2256.3/2085/1357.1 | - | noise: high (other apps 11.5% of game CPUs); others on game CPUs 11.5%; clock avg/min 4236/4142 MHz; Correctness/smoke only, timing noisy. Same as bp-mt-gates-off with Mode=1: one idle worker registered with GC_new_thread under the lock, gate on while it runs. No managed jobs: the only MtSubmit caller is [AutShadow] (Enabled=0 here, ts3fixes switch autshadow off), so the worker stays idle. Pass =  |

## 2026-10-20T21:17 UTC, build 5f6a2b6, jobs ip-mt-arena-rate

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| ip-mt-arena-rate | 21:17 (05:17) | 16.7 | PASS | PASS: profile 15 min, seed 12345 (rng-compare.ts) | 0 | 2178.7/1850.4/1516.9 -> 2189.6/1869.1/1516.9 | - | noise: high (other apps 10.4% of game CPUs); others on game CPUs 10.4%; clock avg/min 4190/4131 MHz; I-120-22 (docs/120 §9.1, §9.5 item 5): how often game script tasks end with frames on their arena chain in normal single-thread play. Correctness only, noisy OK. [MonoThread] Enabled=1 GcLock=1 ArenaFix=1 Mode=0 (no worker, no FrameTls needed), Isla Paradiso for the most script tasks, 15 min. Read  |
