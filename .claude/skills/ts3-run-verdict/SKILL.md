---
name: ts3-run-verdict
description: Results of a finished game test queue without reading logs: PASS/FAIL/VOID per job with reasons and hitch numbers, optional baseline comparison. Use for "results of queue X", "did it pass", "verdict", "compare with baseline".
---
# Queue verdict

```
node tools/harness/run-verdict.ts <queue name> [--date YYYY-MM-DD] [--baseline <tag>]
```

Outputs a markdown table (job | verdict | timing | reason | >=300 / >=1000 per 10 min | max | p99 | it/s). Timing (docs/168 §10): `valid` = in-load A/B usable as timing evidence (PASS, noise low, light tools only, >= 4 iterations per arm), `valid-half` = 10-min ABBA (2 slices per arm), valid once pooled with the opposite-order run of the same job and build, `valid-single` = one side of a cross-run comparison (same job, asi, ts3fixes tuning), else `invalid (<reason>)`. Reuse past runs: `node tools/harness/hitch-ab.ts --reuse <job or repro id> [--since YYYY-MM-DD]`. VOID = the run does not count (hook FAIL line, async off, a fix that is on never fired, noisy machine, wrong build). Only open logs for jobs marked FAIL/VOID whose reason needs explaining. Per-tag detail: `node tools/harness/hitch-ab.ts <tag> --causes`.

Main session: run this and decide only; hand log reads, triage and fixes to a sub-agent (AGENTS.md, "Main agent orchestrates").
