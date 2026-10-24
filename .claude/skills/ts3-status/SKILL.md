---
name: ts3-status
description: Heartbeat for game test runs: what queue/job runs now, PASS/FAIL so far, ETA in PHT. Use for "heartbeat", "ETA", "how is the run", "what is running".
---
# Test run status

Run, from the repo root (Node >= 22.18):

```
node tools/harness/status.ts                         # the running queue (process list, main or worktree), else the newest run
node tools/harness/status.ts --queue <name>          # one queue (tools/harness/queues/<name>.json)
node tools/harness/status.ts --root .claude/worktrees/<wt> --queue <name>   # a queue run from a worktree
node tools/harness/status.ts --chain probe-out/queue/<chain>.log            # a chain of queues (worktree job logs too)
node tools/harness/status.ts --all                    # every queue run today, main checkout + .claude/worktrees/*
```

Report the table as is: running job, finished jobs with PASS/FAIL/VOID, ETA ("done ~HH:MM PHT"; harness logs are UTC, PHT = UTC+8), machine-load verdict. Do not read raw logs unless the tool reports something it cannot explain.
