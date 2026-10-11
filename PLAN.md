# Plan

Approved 2026-10-10. Rewritten 2026-10-11 with everything found so far (read this first after a context reset).

Goal: The Sims 3 (Steam 1.67, all EPs/SPs) with **less in-game stutter** first (stuck Sims, routing, sim-thread work; load times are fine), then fewer crashes (Error 12 / out of address space) and room for heavy CC. Rodney wants every avenue tried. Workflow: online prior art → reverse engineering → rank big wins → build, one change at a time, measured with the harness.

## Where things stand (2026-10-11)

**Install:** fresh Steam reinstall at `C:\Program Files (x86)\Steam\steamapps\common\The Sims 3` (`TS3W.exe` SHA-256 `10b2423e…0609`, LAA set). All 22 default community fixes installed by `tools/setup/install-fixes.ts` (DXVK 3.1.1, Ultimate ASI Loader as `wininet.dll`, S3SS 1.6.3 + `S3SS.toml`, MonoPatcher 0.3.0, NRaas ErrorTrap V100 1.67-Steam, Overwatch V123, simler90 core UPDATE242 Steam, Catalogue Lag Fix (Nexus 242), House Boat Fix, Smooth Patch `.package`, 5 routing fixes incl. 11 per-EP "Sims need less space", Resource.cfg, no-intro, Options.ini tweaks). `check-conflicts.ts`: 0 clash, 0 warn. Our own `ts3agent.package` + `ts3-nointro.package` in `Mods\Packages`.

**Backups (D:\Sims3-backup):** `2026-10-11\` = Rodney's original user folder incl. CC (535 files) and old Bin mods; `pre-fixes-2026-10-11\` = pristine `Game\Bin` + user folder right before the fixes (restore = copy back); `test-saves\` = `Baseline-SV(-fresh)`, `Baseline-BP(-fresh)`, `checkpoints\`. Installer undo: `node tools/setup/install-fixes.ts uninstall [--only id]` (state + per-file backups in `%LOCALAPPDATA%\ts3-fixes`). Manual downloads kept in `~\Downloads\ts3-fixes`.

**Measured (docs/05):** vanilla D3D9 runs out of address space loading a fresh save (3.49 GB committed, 176 MB largest free; crash at 60 MB free in SimService). DXVK: 1.74 GB committed, 1.85 GB largest free, 52 s load. All fixes on Bridgeport: no crash, sim pace ~0.28 sim-days/min vs ~0.21 with DXVK only (first minute 0.35), memory ~1.95–2.07 GB committed / 1.56–1.82 GB largest free (similar to DXVK only), agent ready 105 s after launch (was 25–48 s), ErrorTrap repaired destroyed-object references on load.

**Crashes seen:** 17:54 vanilla OOM (SimService); 19:14 wild pointer at `0xeeacf3` (DXVK, aging day 12, after accepting an age-up dialog; did not recur); 20:59 our own probe (Frida FUZZY backtracer) — fixed. Reports in `docs/data/crashes/`.

## Tools (all in the repo, Windows)

| Tool | What |
|---|---|
| `tools/probe/ts3probe.py serve [--launch]` | Frida probe on 127.0.0.1:47891: `status`, `memory` (4 GB map), `simstack` (sim thread only, safe), `threads` (all, avoid while running), `modules`, `read`, `events`, `scripterrors`, `agent?cmd=…`, `agentstatus`, `reload` (probe JS in place), `detach` (always stop this way). Logs `probe-out/<ts>/events.jsonl` + `memory.csv` (10 s). Safety rules: memory `probe-safety`. |
| `mods/ts3agent` | In-game C# agent (fork of Sims3-MCP, MIT): loader (`src/loader`, mailbox, needs restart) + hot-reloadable plugin (`src/plugin`). Commands incl. game_state, load_world, load_save, world_households, play_household, save_game (real save-as), auto_dialog (rules for known dialogs), keep_alive, decline_requests, quiet_ui, sims_activity, queues, enter_live_mode, reflect_*, plugin_*. Build `tools/harness/build-agent.ts`, hot reload `tools/harness/reload-agent.ts` (~300 ms). |
| `tools/harness/cycle.ts --restart` / `--save X.sims3` | Restart cleanly (installs our packages, clears scriptCache); load a save, clear "Mod Scripts Found", settle dialogs, end in live mode. Start the probe server as its own background task in between. |
| `tools/harness/new-save.ts`, `age-save.ts`, `watchdog.ts` | Create a test save from the menu; age N sim days (keep_alive, declines every 10 s, checkpoints, `--until-day`); watchdog every 1 min (frozen clock in a world, 3 min save grace, hangs, frozen heartbeat, SLOW SIM with sim stack, low memory, stuck Sims → `probe-out/stuck-sims.jsonl`). |
| `tools/setup/install-fixes.ts`, `check-conflicts.ts`, `fixes.json` | Known-fixes installer (plan/install/uninstall/status) and static Mods conflict check. |
| `tools/ghidra/*`, `tools/internal-calls-map.ts`, `tools/extract-assemblies.ts`, `tools/dbpf.ts` | Ghidra headless scripts, the `{fn,name}` script-binding map (1,757), S3SA extract/decrypt, DBPF writer. Anchors: `tools/ghidra/anchors.csv`. |

## Next tasks, in order

1. **Record and finish the fixes layer (step 2c).** Run the same validation on `Baseline-SV`; run tests B–E (frame times via the once-per-frame function `0xEC9F00`, which contains S3SS's post-present limiter `0xEC9FBA`; travel hitches; memory after 30 min; save time) on both aged saves, DXVK-only vs all fixes.
2. **Open questions on the installed fixes:** S3SS lists CPUOptimization, Mimalloc, TimerOptimization, AdaptiveWait as enabled but logs no install line (check S3SS_LOG after a second launch; Mimalloc needs a restart); `Texture memory: 32MB <<OVERRIDE>>` still in `DeviceConfig.log` under DXVK (find the GraphicsRules rule; test whether the game uses it as a texture budget); startup to agent got slower (105 s).
3. **Stutter fix #1: route-failure cache (step 4).** Stuck/unroutable Sims today: EA's 3 AM `RunStuckCheck` (only Sims that never moved), Overwatch (resets after >10 failed plans in 30 sim-min), our watchdog (detects only). All act after the cost: every failed `Route.Plan` runs A* synchronously on the sim thread under a global lock (docs/04 F1). Build a native negative cache + per-Sim backoff on `Route_PlanLocked 0x82c6e0` / `IC_Route_Plan 0x765160`, invalidated on build/buy, portal and footprint changes; plus the NPC "go home" immediate-retry loop (`Sim.CanRunAutonomyImmediately`, docs/04a). Measure with SLOW SIM / stuck-sims data on Bridgeport.
4. **Other stutter candidates (docs/04):** GC pause shape (`GC_PRINT_STATS=1` → gc.log; budget window 500–1500), finalizer drain-all and 10-step catch-up spikes in `MonoScriptHost_Simulate 0xD81840`, per-search time budget.
5. **Single patch host (step 6)** and save cleanup research (memories, NPC inventories, photos, dead Sims; verify against NRaas source). The background research agent for this was blocked by permissions; ask Rodney before re-launching.
6. **Offload (step 5): measure first.** docs/08: indexes are tiny (~17.6 B/resource, CC ≈ 3.5 MB); nothing is memory-mapped; the real budget is `[Resources] CacheBudget` (200 MB + 10 MB) in `Default.ini`, a global lock is held through read+decompress, CC grows CAS data / catalog objects / string tables (~90 MB est.). Phase 0: measure cache fill with and without Rodney's CC; try CacheBudget 200 → 50 → 0. Build the helper (~60–100 h, est. 150–250 MB) only if the cache is a large share. **Tooling ready (2026-10-11), not run:** `node tools/harness/offload-phase0.ts` (variants nocc/cc × 200/50/0 MB; installer entries `user-cc` and `cache-budget --cache-budget-mb N`; read-only probe `/rescache`); run it after the baselines; see docs/08 §7 "Phase 0 tooling".

## Steps (history and status)

### 1. Compare existing fixes: done
`docs/01-existing-fixes-comparison.md`, prior-art RE `docs/03-prior-art-re.md`.

### 2. Baselines
- [x] Fresh reinstall; vanilla and DXVK layers measured (docs/05). Vanilla cannot reliably load a save (address space).
- [x] Aged test saves: `Baseline-SV` (Sunset Valley, Keatons), `Baseline-BP` (Bridgeport, Angelistas), 14 sim days each under DXVK with keep_alive.
- [ ] Tests B–E on both saves (see next tasks).

### 2b. Test harness: working
Probe, agent (hot reload), cycle, new-save, age-save, watchdog, dialog rules, keep_alive, declines, quiet UI. Prior art: `docs/06-test-harness-prior-art.md`.
- [ ] Frame-time capture (hook `0xEC9F00`; test on a dummy first per probe-safety).
- [ ] A permanent probe ASI later (Frida stays for exploration).

### 2c. All-known-fixes layer: installed, first validation passed
Catalog `docs/07-known-fixes-catalog.md`, manifest `tools/setup/fixes.json`, installer and conflict checker.
- [x] Install with backups; conflict check clean; Bridgeport 2-day run without crash.
- [ ] Sunset Valley run, tests B–E, the open questions in next-task 2.

### 3. Reverse engineering: first pass done
`docs/04-re-findings.md` (route planning synchronous under a global lock; GC every frame with adaptive ~1 ms budget; finalizer drain; catch-up steps; Boehm env vars), `docs/04a-script-layer-stutter.md` (NPC retry loops, weak route-fail blocking, unyielding local autonomy, weak EA stuck check). REA (MCP) is registered locally; restart the session to use its tools.
- [ ] Re-run Ghidra analysis with more heap (two analyzers hit OutOfMemory at 16 GB; some functions missing, e.g. `0xD81840` had to be created by hand).

### 4. Our own fixes
Route-failure cache first (next-task 3), then the other docs/04 candidates. One change at a time, measured.

### 5. 64-bit resource offload helper: measure first
`docs/08-resource-offload-design.md`. Ideas only if it works: multi-core prefetch, whole-world preload into the helper, raising the game's cache, `tools/harness/warm-cache.ts` (written, unused), GPU options (textures already in VRAM via DXVK; DirectStorage at most a helper reader; no CUDA RefPack; no GPUDirect on GeForce). Note: `Sims3.ini [ResourceSystem] MemoryUsageLimit` is the async loader's limit, not the cache; `HoldOntoResource` is misspelled there (ignored).

### 6. Single patch host: one tool instead of clashing mods
Core mods clash because each replaces a whole EA assembly. Our agent gets a runtime method-patch host (MonoPatcher approach on the hot-reloadable plugin); each fix is a small switchable patch.
- [x] Pre-flight conflict checker. Old Mods folder had: ErrorTrap twice (sync copy), two ld_RandomSimFixes variants, overlapping Store Updates/Fixes packages (third-party repack, excluded), outdated simler90 UPDATE197.
- [ ] Study MonoPatcher's method patching under Mono 1.2; build our patch host.
- [ ] Reimplement only where mods truly fight or where our tool needs the behaviour (stuck Sims, routing, save cleanup).
- Licensing: NRaas, simler90, MonoPatcher and S3SS have no license file (all rights reserved). Read their decompiled code to understand the behaviour, write a behaviour spec in our own words (`docs/specs/<fix>.md`), implement from the spec, credit the author. Never paste or port their code line by line, in any language. Ask authors for permission where possible. simler90 publishes its source on its MTS page (check its terms).

### 7. Later
- 64-bit: not realistic as a rewrite (40,263 functions + Mono 1.2); grows only from per-function rewrites verified live with the probe.
- Rodney's CC (backup): 172 packages, 209,735 resources, 5.18 GB (15.4 GB decompressed), 98% in three "Store Updates" repack packages; textures 86% (handled by DXVK).
