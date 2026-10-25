---
name: ts3-apex-run
description: Run Apex Radiance's in-game runner (crash triage, live checks, with/alone loads) with our ts3native pair in a known state, crash triage per load, Bin pair always restored. Use for "apex run", "run the Apex job", "Apex crash rerun", "Apex alone", "restore Bin after an Apex run".
---
# Apex runner wrapper

From the repo root (Node >= 22.18, Windows; one game run at a time):

```
node tools/harness/apex-run.ts --apex-repo <Apex checkout> --job tools/ingame_runner/jobs/<job>.json --apex-asi <ApexRadiance.asi> \
     [--variant off|on] [--ab] [--profiler Frames] [--release | --ts3native <asi> --ini <ini> | --no-ts3native] \
     [--loads 3] [--arms with,alone] [--stop-on-crash] [--tag <t>] [--dry-run]
node tools/harness/apex-run.ts --restore     # a killed run left probe-out/apex/restore-pending.json: put the Bin pair back
node tools/harness/status.ts --apex          # progress, min left per load
```

- Our pair: `--release` (newest release asi + ini, docs/data/builds.json), a given pair, or Bin's pair as found. Bin's start pair always comes back at the end (also on Ctrl+C); a WARN line says when that is not a release build.
- `--arms with,alone` is a bisect: refused unless Rodney said "ok bisect" (then `--ok-bisect`). AGENTS.md crash rule: logging rounds first.
- Output: `probe-out/apex/<tag>/summary.txt` (per load: exit codes, crash, crash-ledger triage, `DRAW BAD|HEAP|crash forensics: DUMP` lines), `runs.jsonl`, per-load runner log and `ts3native.log` copy. Read the summary, not the raw logs, unless it cannot explain a crash.
- Stops on Apex `restore-pending.json` (run its `run.ps1 -Restore <run dir>`), on `--stop-on-crash`, or 15 min without runner output.
