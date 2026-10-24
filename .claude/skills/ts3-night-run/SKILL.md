---
name: ts3-night-run
description: Start or resume unattended game test queues (night runs, chains of queues) with clean-machine checks. Use for "start tonight's run", "run these queues", "resume the night run".
---
# Night / chained runs

```
node tools/harness/night-run.ts                                   # report only: lock/idle state, apps, resume plan
node tools/harness/night-run.ts --queue-list a.json,b.json --close [--stop-on-fail]
node tools/harness/night-run.ts --queue-list a.json,b.json --resume  # continue from the first job without a row
```

Rules: timing queues only when the machine is quiet (Rodney says when he uses it); Spotify is stopped automatically; other apps close only with `--close` on Rodney's go (needs lock or idle). Correctness tests may run in the daytime when they unblock work. Check progress with the ts3-status skill.
