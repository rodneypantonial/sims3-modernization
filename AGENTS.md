# Instructions for AI agents

For AI coding agents (Claude Code, Codex, Copilot and others) working on this repo for a contributor. Humans: this is also the short version of the project rules. `CLAUDE.md` has the project background and hard constraints; read it too.

## Read first

1. `PLAN.md`: "Resume here" (state, results, open items) and "Next, in order". It is the source of truth; docs can be older.
2. `CLAUDE.md`: goal, hard constraints (32-bit exe, single-threaded sim), tooling.
3. The doc for the area you touch (`docs/04-re-findings.md` for findings, `docs/15-native-patches.md` for ts3native, `mods/ts3fixes/README.md` for the player mod, `docs/TESTING.md` before any game run, `docs/BUILD.md` before building).

## Rules

- **No EA files in git, ever:** no game binaries or packages, no extracted DLLs, no decompiled C# or native decompiles, no saves, dumps, traces or captures. Keep them in `game-copy/` or `probe-out/` (git-ignored). Quote at most an address, a method name or a few instructions needed to explain a finding, in your own words.
- **No personal data:** no user names in paths (`C:\Users\<name>`), machine names or emails in commits, docs or logs. Use placeholders: `<SteamLibrary>`, `<Documents>`, `<repo>`.
- **Verified vs inferred:** mark every claim `[verified]` (disassembly, decompiled code, measurement), `[community]` or `[inferred]`. Confirm an inferred claim before code depends on it. Do not treat earlier AI-written notes as verified; check the source they cite.
- **One change at a time**, measured against the baseline (docs/TESTING.md). Prefer in-load A/B with runtime switches over separate runs.
- **Repro per fix:** every fix and every bug gets a trigger action in the agent and an entry in `tools/harness/repros.json`. Do not wait for a problem to happen in play.
- **Stuck Sims are log-only:** record possible cases in `docs/data/stuck-log.md`; no research or fix unless a case repeats often or one of our fixes caused it.
- **Stop on obvious errors:** stop a run when it shows an error or the code under test changed; fix, then rerun. Triage every crash against the change ledger (`crash-ledger.ts triage`).
- **Every new switch defaults off** until measured in game; anything that changes save data stays opt-in.
- **Community code:** mods without a license (NRaas, simler90, MonoPatcher, S3SS) are read for behaviour only. Write a behaviour spec in your own words, implement from the spec, credit the author. Never port their code line by line.
- **Times:** use UTC in shared logs, tables and commit messages. Older entries in `PLAN.md` and `docs/` use PHT (UTC+8), the maintainer's local time; do not convert them, but do not add new local times.
- **Game runs are serial;** research and code can run in parallel sub-agents, but never edit `mods/` or `tools/setup/` while a `cycle.ts --restart` or installer step may run (it rebuilds the mods from source).
- Update `PLAN.md` ("Resume here") in the same commit as each fix or result.
- Scripts are TypeScript run with `node script.ts` (Node 22.18+, no npm dependencies) unless they need Windows-only features.

## Picking work

1. Look at the GitHub issues (labels such as `good first issue`, `needs-measurement`, `research`) and at `PLAN.md` "Next, in order".
2. Comment on the issue that you (or your agent) take it, so two people do not run the same experiment.
3. Good first tasks without deep RE: run the milestone or tests B-E on your machine and report (docs/TESTING.md §8); run an untested opt-in switch on your saves; improve a harness script.

## Branches, commits, pull requests

- Branch from the default branch (`master`): `feature/<issue#>-kebab-summary` (`bugfix/`, `docs/` alike), e.g. `feature/42-water-jps-verify`.
- Commit message: `<gitmoji> <summary>`, a blank line, the detail (what changed, why, how it was measured), then `Closes #<issue>`. One logical change per commit.
- Gitmojis: ✨ feature, 🐛 fix, ⚡️ performance, ♻️ refactor, ✅ tests, 📝 docs, 🔧 config, 🔥 remove, 🥅 error handling, 🔊 logs, 👷 CI.
- PR title = commit summary. The PR body has the measurement table (docs/TESTING.md §8): build hash, settings, machine, baseline vs change.
- Before committing: `git status` shows no EA files, saves, `probe-out/` or `game-copy/` content, and no personal paths in the diff.
