---
name: ts3-run-verdict
description: Results of a finished game test queue without reading logs: PASS/FAIL/VOID per job with reasons and hitch numbers, optional baseline comparison. Use for "results of queue X", "did it pass", "verdict", "compare with baseline".
---
# Queue verdict

```
node tools/harness/run-verdict.ts <queue name> [--date YYYY-MM-DD] [--baseline <tag>]
```

Outputs a markdown table (job | verdict | reason | >=300 / >=1000 per 10 min | max | p99 | it/s). VOID = the run does not count (hook FAIL line, async off, a fix that is on never fired, noisy machine, wrong build). Only open logs for jobs marked FAIL/VOID whose reason needs explaining. Per-tag detail: `node tools/harness/hitch-ab.ts <tag> --causes`.
