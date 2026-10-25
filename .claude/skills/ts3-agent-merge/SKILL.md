---
name: ts3-agent-merge
description: Parallel agent hygiene for sims3-modernization: reserve counter versions/slots before building, merge agent branches with known-conflict auto-resolution, clean up worktrees safely. Use for "merge the agent branch", "reserve counters", "clean worktrees", agent briefs that add counters.
---
# Agent branches: reserve, merge, clean

Before an agent adds counters (tell agents to do this in their brief):
```
node tools/registry.ts reserve native <n-int32> --by <who> --what <text>   # ts3native counters version + offsets
node tools/registry.ts reserve fixes <n-slots> --by <who> --what <text>    # ts3fixes counter slots
node tools/registry.ts reserve ledger --for "<topic>"                     # next docs/51 `## N.` section, before writing one
```

Merge an agent branch into master:
```
git merge-tree --write-tree --quiet HEAD <branch> >/dev/null && echo clean
git merge --no-edit <branch>        # on conflict:
node tools/agent-merge.cjs          # resolves counters arrays, switch lists, tuning lines, registry rows, ledger § (incoming renumbered past reserved §, refs fixed)
```
Rebuild what changed (native: build.cmd + selftest; ts3fixes: node tools/setup/build-fixes.ts; agent: node tools/harness/build-agent.ts) before committing the merge. Never merge tools/harness/*.ts while a queue job is starting.

Remove a worktree (deletes a game-copy junction first, never its target):
```
node tools/worktree-clean.ts <path>            # dry run
node tools/worktree-clean.ts <path> --yes
```
Do NOT use `--all-merged --yes` while agents or worktree-based queues run (a fresh worktree looks merged).
