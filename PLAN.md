# Plan

Approved 2026-10-10. Rewritten 2026-10-12 with everything found so far (read this first after a context reset).

Goal: The Sims 3 (Steam 1.67, all EPs/SPs) with **less in-game stutter** first (stuck Sims, routing, sim-thread work; load times are fine), then fewer crashes (Error 12 / out of address space) and room for heavy CC. Rodney wants every avenue tried. Workflow: online prior art → reverse engineering → rank big wins → build, one change at a time, measured with the harness.

## Resume here (updated 2026-10-12, afternoon)

**State:** working tree committed. Install = all 22 community fixes + `graphicsrules-vram` + Options.ini re-detected (quality 3, windowed). CC off. Saves: `Baseline-SV`, `Baseline-BP`, `Baseline-IP` (Isla Paradiso, Dregg household on the "Atlantis" houseboat; all aged 14 days). The game window reopens where Rodney last put it (`ts3probe.py`, `probe-out/window.json`).

**Result so far: all fixes together (docs/04 F13), 10 min x3 per world against this morning's baselines:**

| | Bridgeport | Sunset Valley | Isla Paradiso |
|---|---|---|---|
| sim iterations/s | 31 -> **50** (+61%) | 63 -> **117** (+85%) | 32 -> **74** (+133%) |
| hitches/s (>=50 ms) | 4.0 -> **2.5** (-39%) | 1.9 -> **1.0** (-47%) | 3.5 -> **1.7** (-51%) |
| worst iteration | 1,014 -> 468 ms | 2,210 -> 2,885 ms | 1,170 -> 676 ms |
| stuck Sims (10 min) | 0 | 6 (open) | 5 (open) |

Route plans per real second rise only because the sim runs faster (same plans per iteration, cheaper per plan).

**Fixes in place** (all in `mods/ts3agent/src/plugin/Handlers/PatchHandlers.cs`, switchable at runtime with the `patches` agent command, e.g. `patches {gohome:false}`):
- **Patch host** (docs/04 F8): copies a replacement's `MonoMethod` header over the game method's at startup (mid-game swaps miss callers the interpreter already compiled); an instance stub keeps the original callable.
- **Trip shortcuts** (docs/04 F9, F10): an unseen townie on an unseen trip starts at a safe spot at the destination, then the game's own method does the last metres and the arrival, so what the Sim does next is unchanged. Types: go-home, trips into buildings (work, school, shops), community and home visits, go-to-lot, docked houseboats. Spots must be on the destination lot and pass `Sim.IsPointInLotSafelyRoutable` (cached per lot); a 10 sim-minute guard stops loops. **Dropped:** traffic cars (stranded Sims in Bridgeport for ~1% gain).
- **Walk-style comparer** (F11): array instead of enum-keyed dictionary; `Enum.Equals` 5.1% -> 1.0% of script time.
- **Reaction broadcasters** (F12): off-screen ones pulse 1 in 4; `Lot.UpdateReactions` 8.6% -> 5.5%. Camera/household positions cached per sim tick; all hooks cost 0.9%.
- Houseboat finding: boats moving cost 0.3%; Sims boarding through gangways cost 5-8%, hence the docked-houseboat shortcut instead of moving boats.

**Next, in order:**
1. **Stuck Sims in SV and IP** (in progress): four 5 min checks (SV and IP, shortcuts on and off) with the new full moved-Sim record (`patches` -> `moved`). The IP ones are houseboat households (Arias, Scott), so the docked-houseboat shortcut is the first suspect. Fix or drop whatever strands Sims, then re-profile.
2. **Move the fixes into a player mod**: the patch host and fixes live in the test agent; split them into their own package (agent stays test-only), with a settings file for the switches.
3. **Expansion travel shortcuts** (Rodney approved all): Late Night elevators/subway, Into the Future tubes, University bikes, Pets horses; each verified with `verify-shortcut.ts`. If a type does not happen often enough, add a harness `force_trip` command (random unseen townies, staggered) rather than waiting.
4. **Bridgeport's pre-existing stuck Go Home townies** (same Sims in every run of `Baseline-BP`, with or without shortcuts). Analysis: `docs/12-bridgeport-stuck-gohome.md` (agent, code-only): likely not `GoHome` but `GoToVirtualHomeInternal` (homeless/virtual-apartment townies, same "Go Home" label; `Sim.MakeSimGoHome` uses `GoHome` only when `LotHome != null`, verified), stalled on blocked routes (stand-and-wait up to 50 sim-minutes) plus the retry loop. Confirm live first (interaction type and route action of a stuck Sim), then: shortcut on `GoToVirtualHomeInternal.Run` ending the trip the game's own way (fade out), a watchdog using `SetToResetAndSendHome()` for unseen Sims stuck 60+ sim-minutes, and a backoff in `GoHomeHook`.
5. **(d) Route search** (agreed with Rodney 2026-10-12; route search is native C++ A* `0x81b2a0` under lock `0x1021cf0` on the sim thread, still 10-13% of script time after the shortcuts; failed searches cost the most because they exhaust the search). In order of gain per effort:
   1. Fewer plans: the trip shortcuts (done).
   2. **Cheaper failures (next routing fix):** a cache of routes that just failed (same Sim or start area to the same destination: skip re-planning for a while), a per-Sim backoff after a failure, and a search budget cap for unseen townies' routes. Measure first how much route time is failures (stutter profiler `routeFails`, `byEntry`).
   3. Plan off the sim thread: searches on a spare core with the Sim waiting a frame; needs RE of the native route lock and data first.
   4. Two-level planning for long trips: precomputed lot-to-lot graph plus a short local search; only after 2 and 3, touches native data.
   - Not doing: our own replacement of the native planner (the routing world, i.e. footprints, slots, portals, water, cars, ladders, gangways, elevators, lives in native data; months of work and a high risk of stuck Sims), or a learned/AI route model (paths must be exactly valid; a model cannot guarantee that and would cost more than A* in this 32-bit game). Simple prediction ("this route failed recently") is the failure cache in 2.
   Also: GC tuning (forced collections); underwater lag check in Isla Paradiso (Rodney's report: CPU vs GPU with the stutter profiler while diving).
6. **Long-running saves (task 8)**: research done (`docs/09-save-bloat-research.md`, agent, 2026-10-12): keep Overwatch's default cleanups and turn on Cleanse The Dead for multi-generation saves; memories off or cleared; Regul Save Cleaner on a backup; Save As with rotating names. Own cleanup gaps, ranked: weak NPC-NPC relationships (`Relationship.sAllRelationships`, NPC decay at 0.001x), MiniSims from other worlds with no family links (`MiniSimDescription.sMiniSims`), memory snapshots per NPC, histories of dead/non-resident Sims. First measure bloat per checkpoint (save bytes per resource type, counts via `reflect_get`).
7. Later: tests B-E, store-content test (task 6).

**How to test (tools):** `cycle.ts --restart` then `ts3probe.py serve --launch` then `cycle.ts --save X`; `profile-world.ts --tag t` (10 min profile incl. stuck Sims) and `profile-compare.mjs a b`; `verify-shortcut.ts --name a,b [--tries N] [--off x,y] [--max-stuck N]` (quick check, max ~5 min).

**Rules from Rodney:** test fixes as one bundle per world; compare or isolate fixes with the runtime switches inside one load (`profile-world.ts --ab a,b --slice 2`), not one restart and 10-minute run per fix; stop a test on an obvious error and fix first; stop a run as soon as the code it measures is outdated and rerun on the new build; teleports must not break what the Sim does next and must not leave Sims stuck; each new shortcut type must be seen working (`verify-shortcut.ts`, other world if it does not fire), no A/B needed; harness-only helpers stay in the test agent; every crash is triaged against the change ledger; never enter credentials; local-only repo, never push.

**Earlier findings** (details in docs): scripts run in the Mono 1.2 interpreter, no JIT (docs/04 F6); per-world profiles (F5, F7, F7b); offload phase 0 says the 64-bit cache helper is not worth building (docs/08); graphics VRAM fix (docs/05); references SV ~0.40, BP ~0.33 sim days per real minute.

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
3. **Patch host (step 6)** and save cleanup research (memories, NPC inventories, photos, dead Sims; verify against NRaas). Research agent approved by Rodney 2026-10-12 and launched; output `docs/09-save-bloat-research.md`.
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

## Archive: plan notes as of 2026-10-12 noon

1. ~~Profile other worlds~~ **done 2026-10-12** (docs/04 F7b): Isla Paradiso spends 246 ms/s in route search (2.6× Bridgeport, 12% of plans fail), go-home 11%; Sunset Valley has slow, failing plans; Bridgeport's extra cost is crowd script work. `tools/harness/profile-world.ts` + `tools/probe/profile-compare.mjs` repeat it for any world. Optional: underwater/scuba 5 min vs land (Rodney's lag report).
2. ~~Patch host (step 6)~~ **works 2026-10-12** (docs/04 F8): startup-only method swaps with an instance stub to call the original; verified on `GoHome.Run` in Isla Paradiso. Hooks live in the test agent for now; before shipping, move the patch host and fixes into a separate player mod (the agent stays test-only). **Teleport rule (Rodney):** a shortcut must not break what the Sim does next. Shortcuts replace only the route: place the Sim at the route's destination and return success from the route call, so the interaction continues exactly as after a walk (enter the building, socialize, next queued action). Each shortcut gets a check that the Sim's interaction queue and the following action still run. **No stuck Sims (Rodney):** every shortcut places a Sim only on a spot that passes the game's `Sim.IsPointInLotSafelyRoutable` (a short plan from the spot to the lot's edges, so the Sim can walk on later), and every measured run counts stuck Sims (`profile-world.ts` writes `<tag>-stuck.json`, marking Sims recently teleported); go-home was proven with a full profile (docs/04 F9: 0 stuck Sims). **Further shortcut types (Rodney):** verify each with `tools/harness/verify-shortcut.ts --name <type>` (x3, at most ~5 min, enough tries, no stuck teleported Sim), no A/B; if it does not fire enough on one save, try other worlds or lots until it has been seen working. **Status 2026-10-12 (docs/04 F9, F10):** go-home, trips into buildings, community/home visits, go-to-lot and docked-houseboat shortcuts verified (0 stuck Sims); traffic-car shortcut dropped (stranded Sims in Bridgeport). Open: expansion travel (Late Night elevators/subway, Into the Future tubes, University bikes, Pets horses), and Bridgeport's pre-existing stuck Go Home townies. Rodney approved building all of fixes b, b2, b3 (go-home, houseboats, trips to buildings, lot visits, cars, expansion travel), one at a time and measured.
3. **Fixes, one at a time, each measured** (managed sampler + stutter profiler, same save, same duration): (a) ~~walk-style rules / `Enum.Equals`~~ **done** (docs/04 F11: walk-style 6.0% -> 1.7%, `Enum.Equals` 5.1% -> 1.0% in Bridgeport), (b) **off-screen go-home** (Rodney's idea, 2026-10-12): a prefix on `GoHome.Run` (`Sims3.Gameplay.Core.GoHome`, up to 3 route plans: into the lot, fallback, then water planning) that, for a non-selectable Sim far from the camera and from every active-household Sim and not on a lot with one, skips routing and does what `GoToVirtualHomeInternal` already does on failure: `FadeOut(destroyObject: true)` (de-instantiates; the game re-spawns townies through its normal situations). Townies near the camera still walk, so streets stay alive; walk-by population is a separate system and unchanged. Guards: never the active household, Sims in a situation/party/carpool, Sims mid-interaction with someone visible. Measure GoHome % and route fails before/after; fall back to a per-Sim retry backoff if fades look wrong. No existing mod does this (Overwatch only resets stuck Sims home), (b2) **houseboats** (Rodney, 2026-10-12): a houseboat is a boat object (`Objects.Island.Houseboat`) with its lot parented to it (`World.ParentLotToHouseboat`); it moves only through the helm's Cruise Here / Move to Port interactions, which route the whole boat over water. **Measured: NPC households do cruise** (3 of 6 occupied houseboats moved in a 10 min Isla Paradiso profile, docs/04 F7b; the `houseboats` agent command snapshots positions). Only if NPC boats move: for ones far from the camera, place the boat at its destination instead of driving it (still pays the lot/route rebuild at the destination, skips the water route and per-frame movement), (b3) **off-screen trip shortcut, general form of b** (Rodney, 2026-10-12): share of samples planning routes, by trip (docs/04 F7b data): Isla Paradiso 30% (go-home 8.0, rabbit-hole trips 6.4, lot visits 7.3, group walks/rides 2.4, stand-and-wait replans 1.5), Bridgeport 15% (go-home + virtual home 4.6, group walks/rides 1.5, lot visits 1.2, `LiteDriver` cars 1.0), Sunset Valley 13% (go-home 3.7, lot visits 4.2, rabbit holes 1.4). Rule: skip the middle of a trip, never its ends. Origin and destination both unseen (far from the camera and every active-household Sim, not on a lot with one): place the Sim at the destination (rabbit hole: straight inside; lot: at its edge). Destination seen: place at an unseen point near it and route only the last leg. Origin seen: route normally until out of view. Candidates in order of measured share: go-home, rabbit-hole trips (work/school/shops; the Sim vanishes into the building anyway), lot visits, cars (`LiteDriver`, `GoHereWith*` car/group rides: arrive instead of driving), houseboats (b2). Expansions to check with profile-world.ts per world: Late Night elevators/subway (community: off-screen Sims stuck rerouting), Into the Future travel tubes/hover vehicles (Oasis Landing), University bikes, Pets horses, Island Paradise water taxis and swimming. Never: the active household or anyone near the camera, Sims in a group with a seen Sim, service NPCs arriving at the active lot, walk-bys/joggers (their walk is the content). Build one candidate at a time, each only if its share is over ~2% in some world, measured before/after, (c) ~~reaction broadcaster throttle~~ **done** (docs/04 F12: `Lot.UpdateReactions` 8.6% -> 5.5% in Bridgeport), (d) then route search off the sim thread or fewer plans.
4. Later: GC tuning (Boehm env vars: initial heap, free-space divisor) to cut forced collections; tests B–E; long-running saves (task 8); store-content test (task 6).
