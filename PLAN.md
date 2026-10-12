# Plan

Approved 2026-10-10. Rewritten 2026-10-12 with everything found so far (read this first after a context reset).

Goal: The Sims 3 (Steam 1.67, all EPs/SPs) with **less in-game stutter** first (stuck Sims, routing, sim-thread work; load times are fine), then fewer crashes (Error 12 / out of address space) and room for heavy CC. Rodney wants every avenue tried. Workflow: online prior art → reverse engineering → rank big wins → build, one change at a time, measured with the harness.

## Resume here (end of session 2026-10-12)

**State:** no runs active; working tree committed. Install = all 22 community fixes + `graphicsrules-vram` (DXVK VRAM fallback) + Options.ini re-detected (quality 3, windowed). CC off. Saves: `Baseline-SV`, `Baseline-BP` (aged 14 days) and their `Validation-*`/`Ref-*` copies; no Isla Paradiso save yet.

**Key findings so far (details in docs):**
- **Scripts run in the Mono 1.2 *interpreter*** (no JIT in TS3W.exe); the interpreter loop `0xe53eb0` is ~30% of sim-thread samples. All C# (game and mods) is interpreted (docs/04 F6).
- **Managed profile, Bridgeport x3** (docs/04 F7): `Sim.Simulate` interactions 34.8% of script time; routing 16.6% (native search ~10% self); walk-style rules ~6% with `System.Enum.Equals` 5.1% self; `Lot.UpdateReactions` → `ReactionBroadcaster.PulseSims`/`GetInRadiusSims` ~7%; NPC `GoHome` + `GoToVirtualHomeInternal` 7–11%; `Queries.GetObjects` 6%; `EventTracker.SendEvent` 4.6%.
- **Native stutter profile, 10 min Bridgeport** (docs/04 F5): 4 hitches/s (>50 ms iterations, worst 1 s); route search 95 ms/s, 26.5 plans/s, only 1.6% fail (failures cluster); GC 33 ms/s with forced collections up to 149 ms. A route-*failure* cache alone is weak; cutting how many routes are planned (and their walk-style overhead) is the lever.
- **Offload phase 0** (docs/08 end): main resource cache ~400 MB at budget 200/50/0 MB, with or without Rodney's 143 CC packages; only 10–20 MB evictable. The 64-bit cache helper is **not worth building**. The budget is set in `GraphicsRules.sgr` (`setProp $ResourceGroup CacheBudget`), not `Default.ini`. Open: Store repacks and store items in use.
- **References with the autopilot** (docs/05): SV ~0.40, BP ~0.33 sim days per real minute, no crashes; memory ~1.9–2.1 GB committed, ≥1.5 GB largest free.
- Graphics: under DXVK the game read 0 MB VRAM and picked low presets; fixed (docs/05).

**Next, in order:**
1. **Profile other worlds** with the same three tools (stutter profiler, `/simstack` sampler, managed sampler), 10 min each at x3 with upkeep: Sunset Valley (`Baseline-SV`) and **Isla Paradiso** (new: `new-save.ts` → household → `age-save.ts --days 14` → `Baseline-IP`; boats, houseboats, islands). Compare top methods per world; record in docs/04 F7.
2. **Patch host (step 6):** runtime C# method patching from our hot-reloadable plugin (study MonoPatcher 0.3.0, already installed, under the Mono 1.2 interpreter: patching = swapping the method's interpreter info at `MonoMethod+0x14` or its IL). Licensing rule applies (spec, then reimplement).
3. **Fixes, one at a time, each measured** (managed sampler + stutter profiler, same save, same duration): (a) walk-style rules / `Enum.Equals`, (b) NPC go-home backoff or off-screen send-home, (c) reaction broadcaster throttle, (d) then route search off the sim thread or fewer plans.
4. Later: GC tuning (Boehm env vars: initial heap, free-space divisor) to cut forced collections; tests B–E; long-running saves (task 8); store-content test (task 6).

**Rules from Rodney:** stop a test on an obvious error and fix first; harness-only helpers (repairs, autopilot) stay in the test agent; every crash is triaged against the change ledger; never enter credentials; local-only repo, never push.

## Where things stand

**Install:** Steam `C:\Program Files (x86)\Steam\steamapps\common\The Sims 3` (`TS3W.exe` SHA-256 `10b2423e…0609`, LAA). Fixes via `tools/setup/install-fixes.ts` (DXVK 3.1.1, UAL `wininet.dll`, S3SS 1.6.3, MonoPatcher 0.3.0, NRaas ErrorTrap/Overwatch, simler90 core, Catalogue Lag Fix, House Boat Fix, Smooth Patch, 5 routing fixes, Resource.cfg, no-intro, Options.ini, `graphicsrules-vram`). Experiments (off by default): `user-cc` (143 CC packages, Store repacks excluded), `cache-budget --cache-budget-mb N`. `check-conflicts.ts`: 0 clash.

**Backups (D:\Sims3-backup):** `2026-10-11\` original user folder incl. CC; `pre-fixes-2026-10-11\` pristine Game\Bin + user folder; `options-redetect\` Options.ini/DeviceConfig before re-detection; `test-saves\`. Installer undo: `install-fixes.ts uninstall [--only id]` (backups in `%LOCALAPPDATA%\ts3-fixes`).

**Crashes:** 17:54 vanilla OOM; 19:14 wild pointer `0xeeacf3` (did not recur); 20:59 our probe (fixed). Since 2026-10-11 every run is in `docs/data/change-ledger.jsonl`; `crash-ledger.ts triage` maps crashes to change sets.

## Tools (all in the repo, Windows)

| Tool | What |
|---|---|
| `tools/probe/ts3probe.py serve [--launch]` | Frida probe on 127.0.0.1:47891: `status`, `memory`, `simstack` (sim thread native stack), **`managedstack`** (C# stack via interpreter frames), `interp`, **`rescache`** (resource caches), `read`, `string`, `agent?cmd=…`, `agentstatus` (incl. `worldLoaded`), `reload`, `detach` (always stop this way). After changing `ts3probe.py`, detach and re-run `serve` (attaches without relaunch). |
| `tools/probe/managed-sample.mjs [secs] [hz] [out]` | C# sampler: ranks self, task and inclusive methods. |
| `tools/frida/stutter-profile.js` | Native per-iteration profiler (routes, GC, finalizers, hitches): `python -m frida_tools.repl -n TS3W.exe -l tools/frida/stutter-profile.js -P '{"out":"<path>","hitchMs":50}' -q -t 600`. |
| `mods/ts3agent` | In-game agent: loader (mailbox kept across loads) + hot-reloadable plugin. Harness commands incl. game_state, load_save, save_game, auto_dialog, **autopilot** (holds speed, clears popups within a frame in any in-world state; speed `none` leaves the speed alone), keep_alive (+ repairs broken home objects), decline_requests, quiet_ui, catalog_search/buy_object/place_object. Build `build-agent.ts`, hot reload `reload-agent.ts`. |
| `tools/harness/cycle.ts --restart` / `--save X` | Restart (rebuilds the agent; clears the script cache only when agent source or Mods changed); load a save (waits on the world-loaded flag), end in live mode, ledger snapshot. Launch ~55 s, menu→ready ~37 s. |
| `tools/harness/new-save.ts`, `age-save.ts`, `watchdog.ts`, `crash-ledger.ts`, `offload-phase0.ts` | New save; aging with autopilot/keep_alive/declines; 1-min watchdog (stuck Sims, frozen clock, low memory; triage on crash); crash ledger; offload phase 0 variants. |
| `tools/setup/install-fixes.ts`, `check-conflicts.ts`, `fixes.json` | Fixes installer and conflict check. |
| `tools/ghidra/*` (`run-headless.sh DecompileTo.java <out> 0 <addrs>`), `internal-calls-map.ts`, `extract-assemblies.ts`, `dbpf.ts` | RE tools. Decompiled C# in `game-copy/decompiled`, native decompiles in `game-copy/analysis/decomp`. |

## Next tasks (backlog, by area)

1. **Fixes layer (step 2c):** done except tests B–E (frame times via `0xEC9F00`; travel hitches; memory after 30 min; save time). Open: S3SS CPUOptimization/Mimalloc/TimerOptimization/AdaptiveWait show no install line (find S3SS's log); whether the engine reads VRAM elsewhere (`GetAvailableTextureMem`).
2. **Stutter (step 4):** see "Next, in order". Native facts: `Route_PlanLocked 0x82c6e0` runs A* `0x81b2a0` under lock `0x1021cf0` on the sim thread (F1); GC every frame with ~1 ms budget, finalizer drain, 10-step catch-up (F2); `GC_PRINT_STATS` available (F3). Interpreter facts: TLS index at `[0x11f4160]`, ThreadContext +8 current frame, frame[0] parent, frame[1] = info, info+8 = MonoMethod (klass +8, info back pointer +0x14, name +0x18), MonoClass name/namespace +0x34/+0x38; current task `[0x11f4170]` (+0 base MonoMethod, +0x140 depth).
3. **Patch host (step 6)** and save cleanup research (memories, NPC inventories, photos, dead Sims; verify against NRaas). Ask Rodney before launching a research agent (one was permission-denied).
4. **Offload (step 5):** cache helper dropped (phase 0). Remaining tests: `cc-full` (Store repacks) at load, and store items placed/worn in the world (MB per item used; the agent can buy and place). Only if in-use data grows a lot: a different design (not a cache).
5. **Isla Paradiso (task 7):** House Boat Fix and Ocean Play Fix installed; community world fix (MTS 514909) only on a copy. Profile first ("Next, in order" 1).
6. **Long-running saves (task 8):** measure growth per checkpoint (save size, Sim/object/memory counts, errors, memory, `/rescache`, pace), clean only what grows, periodic world reset; Overwatch already does nightly cleanup.

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

### 3. Reverse engineering: first pass done; interpreter and managed sampler added (F6, F7)
`docs/04-re-findings.md` (route planning synchronous under a global lock; GC every frame with adaptive ~1 ms budget; finalizer drain; catch-up steps; Boehm env vars), `docs/04a-script-layer-stutter.md` (NPC retry loops, weak route-fail blocking, unyielding local autonomy, weak EA stuck check). REA (MCP) is registered locally; restart the session to use its tools.
- [ ] Re-run Ghidra analysis with more heap (two analyzers hit OutOfMemory at 16 GB; some functions missing, e.g. `0xD81840` had to be created by hand).

### 4. Our own fixes
Re-ranked by measurement (docs/04 F5–F7): walk-style rules, NPC go-home, reaction broadcasters, then route search; a route-failure cache alone targets only ~2% of plans. All need the patch host (step 6). One change at a time, measured.

### 5. 64-bit resource offload helper: measured, cache helper dropped (phase 0, docs/08)
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
