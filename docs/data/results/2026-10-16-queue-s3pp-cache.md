# Queue s3pp-cache 2026-10-16

Tool: `tools/harness/queue.ts` (docs/TESTING.md). Times UTC (PHT in brackets). Rows: `probe-out/queue/s3pp-cache-2026-10-16.jsonl`.

## 2026-10-16T01:14 UTC, build 99f46ec, jobs bp-s3pp-cache-on, bp-s3pp-cache-off

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-s3pp-cache-on | 01:14 (09:14) | 9.5 | **FAIL** | FAIL: profile 10 min, unseeded (rng-compare.ts) | 1 | 2172.9/1836.1/1548.9 -> 2316.6/2106.1/1293.1 | - | S3PP Mono caches on (installed default): stutter profile with simstack sampling, compare with -off |
| bp-s3pp-cache-off | 01:23 (09:23) | 9.1 | **FAIL** | FAIL: profile 10 min, unseeded (rng-compare.ts) | 1 | 2174.2/1853.1/1548.9 -> ? | - | S3PP MonoTypeEHashProbe + MonoIsinstCache off: does mono_metadata_type_equal come back (docs/29 §5)? |

## 2026-10-16T01:43 UTC, build 2b45b85, jobs bp-s3pp-cache-on, bp-s3pp-cache-off

| job | start | min | result | verdict | exit | commit / private / largest free MB (before -> after) | counters changed | note |
|---|---|---|---|---|---|---|---|---|
| bp-s3pp-cache-on | 01:43 (09:43) | 12.1 | PASS | PASS: profile 10 min, unseeded (rng-compare.ts) | 0 | 2185.8/1834.3/1548.9 -> 2280.7/2083.5/1325.1 | - | S3PP Mono caches on (installed default): stutter profile with simstack sampling, compare with -off |
| bp-s3pp-cache-off | 01:55 (09:55) | 12 | PASS | PASS: profile 10 min, unseeded (rng-compare.ts) | 0 | 2179.3/1838.6/1548.9 -> 2266.4/2090.8/1309.1 | - | S3PP MonoTypeEHashProbe + MonoIsinstCache off: does mono_metadata_type_equal come back (docs/29 §5)? |

## Analysis (PASS run 01:43 UTC, build 2b45b85)

Inputs: `probe-out/profile/s3pp-cache-2026-10-16-bp-s3pp-cache-{on,off}-*` (`-600-seconds.csv`, `-simstack.jsonl`, `-managed.txt`, `-rng.json`, `-upkeep.log`) and the memory rows in the queue jsonl. Tools: `tools/harness/sample-split.ts` (native split of the simstack samples) plus a throwaway awk/node summary of the per-second CSV (first 60 s dropped). The FAIL run at 01:14 (sampler bug) is ignored. One run per side, unseeded RNG: the two sims diverge, so whole-run rates differ by more than the effect under test.

**Verdict: the S3PP caches help a little and cost nothing in memory. Keep them on.** The profile shows the lookup they remove. One unseeded run per side is too noisy to show any change in sim rate.

| metric | cache on | cache off | note |
|---|---|---|---|
| `0xEA8720` (`mono_metadata_type_equal`) self | 0 / 877 samples | 15 / 882 (1.70%) | top-2 self function with the cache off [measured] |
| `0xEBBDE0` (type-hash lookup) inclusive | 0.46% (4) | 3.97% (35) | docs/69 expectation "<= 2% with caches on" met [measured] |
| `0xEA3820` (`mono_object_isinst`) inclusive | not in top 400 | 1.13% (10) | with the cache on the detour runs in S3PP's module, which the TS3W-only split does not see [measured / inferred] |
| Mono runtime family, self | 6.84% | 9.75% | +2.9 pp with the cache off [measured] |
| managed self `Object.GetType` + `RuntimeTypeHandle.get_Value` | 0.7% (GetType not in top list) | 3.3% + 1.6% | C# callers of the type table [measured] |
| sim iterations/s, mean (median) | 33.3 (32) | 44.1 (40) | off is faster, against the expected direction: noise, see below [measured] |
| sim minutes advanced in 10 min | 5,028 | 6,034 | same save, same start minute 20,350.9 [measured] |
| iterations/s per minute block | 29.9-42.2 | 31.8-59.6 | off swings by 2x between minutes [measured] |
| ThreadedObjects / TaskObjects / route ms/s | 629 / 73 / 110 | 631 / 71 / 114 | stage time per second is flat [measured] |
| hitch iterations/s; seconds with max iteration >= 100 ms (>= 250 ms) | 3.51; 160 (9) | 2.91; 122 (6) | off looks smoother, also noise [measured] |
| max iteration p50 / p95 / p99 / max ms | 81 / 174 / 292 / 749 | 76 / 152 / 294 / 526 | [measured] |
| private MB before -> after | 1834.3 -> 2083.5 (+249.2) | 1838.6 -> 2090.8 (+252.2) | equal within 3 MB [measured] |
| largest free MB after | 1325.1 | 1309.1 | 16 MB apart, run-to-run noise [measured] |

Reading:
- The cache works as documented [measured]. With `MonoTypeEHashProbe` off, `mono_metadata_type_equal` comes back as the #2 self function, and the type-hash lookup `0xEBBDE0` reaches about 4% of sim-thread samples. With the cache on, both nearly vanish. About 3.5 pp of sim-thread samples move away from that lookup, which is roughly 30-35 ms/s if the sim thread is busy all the time [inferred]. That fits the 20-30 ms/s estimate in docs/29 §5. `MonoIsinstCache` saves about 1 pp more [measured off-side, inferred on-side].
- The rates point the other way (off has 33% more iterations/s and 20% more sim minutes), and that is not a cache effect [inferred]. Total stage time per second is the same in both runs, while the off run's per-minute iteration rate swings between 32 and 60. The unseeded worlds diverged: the off run also planned more routes (41.6/s vs 34.2/s) and ran more GC marks (3.2/s vs 1.8/s). A real loss of about 3.5% of sim-thread time cannot show through that spread. Measuring the rate effect needs seeded runs (`seeded-rng` queue, docs/27) or several runs per side.
- Memory: no measurable cost [measured]. Private memory growth and largest free block match within run noise.

**What this means for our fixes (docs/69 rows #1 and #3):** we have no overlapping fix for either cache. docs/69 lists both as "Keep S3PP (we have nothing)", so the "prefer our fixes" rule does not apply. Keep `MonoTypeEHashProbe=1` and `MonoIsinstCache=1` (the installed default) and do not write our own type-table or isinst cache [inferred from the measured profile plus docs/69]. Our own Mono-side target stays the `loader_mutex` lock under the IC transition (`0xE85030` from `0xDA5970`, 0.68% self with the cache on; PLAN `[IcLockCheck]`). The docs/69 hook-conflict rows for these two detours stay as they are.
