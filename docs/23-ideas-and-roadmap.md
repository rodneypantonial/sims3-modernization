# 23. Ideas and roadmap beyond the current plan

`PLAN.md` holds the work in progress ("Next, in order"). This doc collects what comes after, or what needs more hands than one machine: infrastructure for many contributors, and larger research bets. Each idea lists value, effort and risk (low / medium / high) and the issue it should become. Claim an idea by opening that issue with the matching template and commenting on it (see `CONTRIBUTING.md`).

Claims here are `[inferred]` unless marked otherwise; most are proposals.

## Summary

| # | Idea | Value | Effort | Risk | Issue template |
|---|---|---|---|---|---|
| 1 | Community baseline saves recipe | high | low | low | fix proposal (harness) |
| 2 | CI on GitHub Actions | high | medium | low | fix proposal (harness) |
| 3 | One-click player installer and releases | high | medium | medium | fix proposal (installer) |
| 4 | Crash-report aggregator | high | medium | low | fix proposal (harness) |
| 5 | Multi-laptop test farm and job queue | high | high | medium | research, then fix proposal |
| 6 | Public results dashboard | medium | low | low | fix proposal (harness) |
| 7 | Telemetry-free benchmark mode | high | medium | low | fix proposal (harness) |
| 8 | Mod-conflict scanner for players | medium | low | low | fix proposal (installer) |
| 9 | Shareable symbol file of verified EA addresses | high | low | medium (legal) | research |
| 10 | Decision log (ADRs) | medium | low | low | docs |
| 11 | Allocator: mimalloc vs our cache | medium | medium | high | research |
| 12 | Texture streaming and DXVK tuning for Error 12 | high | medium | low | research |
| 13 | World-level own route planner | high | high | high | research |
| 14 | Animation LOD | medium | medium | medium | research |
| 15 | Mono interpreter speedups | high | high | high | research |
| 16 | 64-bit research notes | low (clarity) | low | low | research |
| 17 | Sim-thread offload architecture (long-term goal) | high | high | high | research |

Suggested order: 1, 2, 10, 9 first (they make every later contribution cheaper and comparable), then 7, 4, 3, 6, then the research bets as contributors with RE experience join.

## Contributor infrastructure

### 1. Community baseline saves recipe

Results from different machines only compare when everyone measures the same save. Saves cannot be shared (EA content, personal data), but they can be rebuilt deterministically.

- `tools/harness/new-save.ts` already picks the household by name and member count, repeatably. `age-save.ts` ages with autopilot, `keep_alive` and declines, but the game's own randomness (story progression, autonomy) differs per run.
- Proposal: a `docs/data/baseline-saves.json` recipe per world (world file, household, days, checkpoints, settings such as story progression and aging, the exact installer set used while aging) and one command `node tools/harness/baseline-save.ts --world bp` that runs both scripts with it. Check equivalence with `save-stats.ts` / `save-sources.ts` (Sim, household, object counts within a tolerance) and publish those counts so a contributor can check that their save is "the" Baseline-BP.
- Open question: can the game's RNG be seeded (a native patch on the seed source) so aging is bit-identical? Research first.
- Value high, effort low, risk low. Issue: `[fix] Deterministic community baseline saves (new-save + age-save recipe)`.

### 2. CI on GitHub Actions

- Native: `windows-latest` has MSVC; run `native/ts3native/build.cmd` (path to `vcvars32.bat` made configurable) including `selftest.exe` and `decompress-test.exe`. `waterjps-test.exe` needs code from the user's `TS3W.exe`, so it stays local; CI builds it only.
- Mods: `build-fixes.ts` and `build-agent.ts` compile against the game's own DLLs, which must never be in the repo. Option: generate **reference stubs** (public type and member signatures only, empty bodies, no EA code or resources) locally with a script, compile against them in CI. Even signatures may be a legal grey zone; decide in an ADR (idea 10) before committing any stub. Fallback: CI only runs a C# syntax check (`csc /t:library` with `/nostdlib` won't resolve types, so this is weak).
- Harness: run every tool's `--self-check` (`milestone.ts`, `tests-be.ts`, `game-alive.ts`, `content-health.ts --self-test`), `node --check` on `.ts`/`.mjs`, YAML lint on the issue templates, a grep gate for personal paths (`C:\Users\`, `/c/Users/`) and EA file types in the diff.
- Value high, effort medium, risk low. Issue: `[fix] CI: native build + self-tests, harness self-checks, forbidden-file gate`.

### 3. One-click player installer and release packaging

- GitHub Releases with `ts3fixes.package`, `ts3native.asi` + `ts3native.ini` (safe defaults: only measured, default-on sections) and a small installer that wraps `install-fixes.ts` (downloads community fixes from their official sources, never re-hosts, verifies SHA-256, backs up, can uninstall). No EA files.
- Needs: a signed/hashed release checklist, a player README in plain words, a version check in `ts3native.log` and the mod.
- Risk medium: players without the harness hit untested combinations (other core mods, non-Steam builds). The ASI must refuse to patch when bytes at a patch site differ (it already logs and skips) and the installer must refuse non-1.67 exes.
- Issue: `[fix] Release packaging: ts3fixes.package + ts3native.asi + installer on GitHub Releases`.

### 4. Crash-report aggregator

- Contributors upload `xcpt` summaries (exception code, faulting module + RVA, top frames as module+offset, active change-set key from the ledger), never dumps.
- A script (`tools/harness/crash-cluster.ts`) reads `docs/data/crashes/*.txt` plus submitted summaries and clusters by module+RVA and change set, extending `crash-ledger.ts triage`. Output: a table "address -> count, builds, switches on", which points straight at the patch to suspect.
- Issue: `[fix] Crash summaries: submission format + crash-ledger clustering by address`.

### 5. Multi-laptop test farm and job queue

- Game tests are serial per machine; contributors' laptops are idle most of the day. A coordinator (one Claude Code session, or a small script) writes jobs as files (`jobs/<id>.json`: build hash, ini, installer set, save recipe, command) to a branch; each farm machine's agent pulls, runs `cycle.ts` + the command, and pushes `docs/data/results/...` back. Git is the queue and the results store; no server.
- Needs: idea 1 (same saves), a machine profile file (hardware line), a lock per job (a claim commit), a kill switch, and strict rules: farm agents run only commands from the job allow-list, never arbitrary text from a job file.
- Risk medium: a bad build crashing many machines; job files as an injection vector for agents. Start with two machines and read-only jobs (profiles of merged builds).
- Issue: `[research] Test farm: git-based job queue for harness runs across contributor machines`.

### 6. Public results dashboard

- GitHub Pages built from `docs/data/results/*.md` (parse the hardware line, build line and the table): per fix, on vs off across machines, iterations/s and hitches/s, with the hardware shown next to each point. A static page from a build script, no backend.
- Issue: `[fix] Results dashboard on GitHub Pages from docs/data/results`.

### 7. Telemetry-free benchmark mode

- Today's numbers are sim-thread metrics (iterations/s, hitches) from the probe; FPS needs test B. Players and reviewers want FPS from a fixed scenario.
- Proposal: an agent action that pins the camera to a fixed route (a few waypoints over the busiest lots) at fixed speed on the baseline save, runs for N minutes, and reports frame-time percentiles (`frame-times.js`) and sim metrics. "Telemetry-free": nothing leaves the machine; the output is a results file the player chooses to submit.
- Issue: `[fix] Benchmark mode: fixed camera route + fixed save, frame-time percentiles`.

### 8. Mod-conflict scanner for players' Mods folders

- `tools/setup/check-conflicts.ts` already finds CLASH/WARN/INFO between packages and core mods; `content-health.ts` checks Resource.cfg depth and caches. Extend for players: known-bad or outdated mods list (by resource or name, maintained in a JSON), duplicate copies (sync folders), merged packages over the size limit, and overlap with what ts3fixes patches (so a player knows which fix is shadowed).
- Issue: `[fix] Player Mods folder scanner (extends check-conflicts.ts)`.

### 9. Knowledge base of verified EA function addresses

- Our docs name hundreds of addresses (`0x82C6E0` Route_PlanLocked, `0x4E5E40` allocator core, ...). A shareable symbol file (CSV or JSON: address, name, signature guess, confidence, doc link; addresses and names only, no code or bytes) lets every contributor start Ghidra with our names applied (`tools/ghidra/ApplyInternalCallNames.java` pattern; `tools/ghidra/anchors.csv` is a start).
- Legal check first (names and addresses we derived, not EA's text). Record the decision as an ADR.
- Issue: `[research] Shareable symbol file: verified addresses and names for TS3W.exe 1.67`.

### 10. Decision log (ADRs)

- `docs/adr/NNNN-title.md`: context, decision, consequences, status. Back-fill the big ones: pausing native async routing (three crashes, the unlocked per-lot search state, docs/18), dropping the 64-bit resource cache helper (docs/08 phase 0), stuck Sims treated as solved (log only), default-off until measured, no re-hosting of community fixes, behaviour specs instead of porting unlicensed mod code.
- Issue: `[research] Decision log: ADRs for major choices (async routing pause, offload helper drop, ...)`.

## Research bets

### 11. Allocator: mimalloc vs our cache

- S3SS offers a mimalloc option; our ts3native 0.8.0 cache sits in front of EA's dlmalloc derivative (docs/15 Phase 8) and gave +19% iterations/s in one A/B (PLAN, 2026-10-13). Open: does S3SS's mimalloc replace the same heap, do they stack, and which wastes less 32-bit address space (Error 12)?
- Measure: both, each alone, on the same save; address space after 30 min; crash-free hours.
- Risk high (heap corruption shows up late). Issue: `[research] Allocator: S3SS mimalloc vs ts3native alloc cache, address space and speed`.

### 12. Texture streaming and DXVK tuning for Error 12

- DXVK moves textures out of the process (docs/05: vanilla cannot even load the save). Remaining Error 12 risk is address space from caches and CC (docs/08, docs/17). Research: DXVK options for the 32-bit game (`d3d9.textureMemory`, memory budget behaviour on laptops with 4-8 GB VRAM), the engine's own texture LOD and streaming budgets in `graphicsrules`, and whether `GetAvailableTextureMem` is read elsewhere (PLAN next task 1).
- Issue: `[research] Error 12: texture streaming budgets and DXVK settings on 4-8 GB laptop GPUs`.

### 13. World-level own route planner

- docs/18: running the game's planner off-thread hit unlocked shared lot state; making searches cheaper is the current path. A bolder option: our own world-level planner (hierarchical: precomputed lot-to-lot graph + local search) answering the common long trips, falling back to the game's planner. High value for townie traffic, high effort and risk (route semantics: portals, doors, ownership rules, footprints).
- Start with research: what fraction of plan time is world-level vs lot-level per world (docs/04a §8 numbers), and which result fields callers read.
- Issue: `[research] Own world-level route planner: share of plan time and result contract`.

### 14. Animation LOD

- docs/22: parallel animation update is unsafe; §5 lists serial alternatives. Animation LOD (update off-screen or distant actors at a lower rate) is the cheapest. Measure first (docs/22 §6), then a native switch with in-load A/B.
- Issue: `[research] Animation LOD for off-screen Sims (docs/22 §5)`.

### 15. Mono interpreter speedups

- docs/04 F6/F7: scripts run in Mono 1.2's interpreter, the largest sim-thread cost. Ideas: a method lookup cache, inline caches for virtual/interface calls, faster field access paths, cheaper `isinst`/`castclass`. Each is a native patch inside the interpreter loop, with correctness risk across all game scripts.
- Research first: which interpreter opcodes and lookups dominate (sample the interpreter with the probe), and what upstream Mono later changed in those paths (as design ideas, not code).
- Issue: `[research] Mono interpreter hot paths: method/virtual call caches`.

### 16. 64-bit research notes

- Why it is not realistic: a 32-bit exe with ~40k functions, embedded Mono 1.2 (32-bit, interpreter), 32-bit pointer assumptions in data files and saves, third-party 32-bit plugins (S3SS, DXVK x32, ASI loader). What it would take: per-function rewrites verified live, or an out-of-process 64-bit helper (docs/08 tried a resource cache helper and dropped it). Write it down once so the question stops costing time.
- Issue: `[research] 64-bit: why not, and what would be needed (notes only)`.

### 17. Sim-thread offload architecture (long-term goal)

- Goal (Rodney, 2026-10-14): move everything that can safely leave the sim thread to other cores, so the game scales with longer saves, more objects and more Sims instead of hitting one core's limit. Value high, effort high, risk high. Needs a redesign of how work reaches the sim thread, built step by step.
- Acceptance rule: an offload step is accepted with no speed gain today, or up to ~2% fewer sim iterations/s, if it is stable (no crashes, no new stuck Sims, same results) and moves measurable work off the sim thread (route ms/s, stage ms/s, sampler share). Over ~2% needs a plan to win it back. See `PLAN.md` (Goal).
- Known candidates and blockers: route search on a worker (`[RouteWorker]` works, synchronous today; async blocked by unlocked per-lot routing state, docs/18), save write and backup copy (`[SaveWrite]`), resource decoding (`[Decompress]`), CAS tile building (docs/21: mostly UI layout, which must stay on its thread), GC marking, autonomy scoring and story progression (Mono, research).
- First step: a map of sim-thread work by owner (native vs Mono, per stage) from the existing profiles, ranked by what could run elsewhere and what shared state blocks it.
- Issue: `[research] Sim-thread offload architecture: map, blockers, step plan`.
