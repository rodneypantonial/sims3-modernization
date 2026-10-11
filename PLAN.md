# Plan

Approved 2026-10-10.

## 1. Compare existing fixes: done
Prior-art RE research (S3SS signatures, Mono/Boehm, routing, NRaas, world fixes): `docs/03-prior-art-re.md`.
Output: `docs/01-existing-fixes-comparison.md`. Core picks: DXVK x32 (spoofs a GTX 580 for TS3W, keeps texture memory out of the 32-bit address space) and Sims 3 Settings Setter. Sims 3 Performance Patch is optional and overlaps S3SS's stack fix.

## 2. Baseline setup and measurements: guide done, results pending
Output: `docs/02-baseline-install-guide.md`.
- [ ] Rodney runs layers 1 to 6 and fills in the results table (load time, idle FPS, streaming hitches, commit size after 30 min, save time, crashes).
- [ ] Record the results in `docs/05-baseline-results.md`. These are the numbers steps 3 and 4 have to beat.

## 2b. Test harness: next (decided 2026-10-11)
Manual test runs are slow and noisy, so we automate them before measuring. Prior art and design: `docs/06-test-harness-prior-art.md`.
- [~] Runner (`tools/harness/run.ts`; launch, load and aging work via `tools/probe/ts3probe.py serve --launch` and `tools/harness/age-save.ts`): prepares a copy of the user folder, launches `TS3W.exe -worldFile:"<save>.sims3"` with a timeout, records PresentMon, collects results.
- [x] In-game agent mod, forked from Sims3-MCP (MIT): speed, camera, select, push interactions, spawn, wait for sim time, save/quit, snapshots (sims, queues, route failures, heap).
- [~] Live probe (`tools/probe`, Frida): stacks, address-space timeline, crash/script-error capture, agent bridge, hot reload of probe and agent plugin. A permanent ASI comes later.
- [x] Watchdog (`tools/harness/watchdog.ts`): 1-minute stuck detection, hangs, blocked ticks, sim lag, stuck Sims.
- [~] Scenario files: create and age the test saves automatically, then run tests A to E unattended. Done: `Baseline-SV` (Sunset Valley, Keatons, aged 14 sim days under DXVK, in `D:\Sims3-backup\test-saves`). Next: Bridgeport, then tests A to E.

## 2c. All-known-fixes installer: next after the vanilla baseline (decided 2026-10-11)
One tool applies every established community fix, so we only hunt problems that remain with all of them installed. Catalog: `docs/07-known-fixes-catalog.md`, manifest `tools/setup/fixes.json`.
- [ ] Installer `tools/setup/install-fixes.ts`: downloads from official sources (manual step for login-only sites), verifies SHA-256, backs up, installs, can uninstall. Never bundles other authors' files.
- [ ] Measure the "all known fixes" layer with the harness; this becomes the baseline our own fixes must beat.

## 3. Reverse engineer TS3W.exe and the Mono layer: in progress (first pass done)
Focus decided 2026-10-11: in-game stutter (stuck sims, threading, bad world pathing); load times are fine. Findings and ranked big wins: `docs/04-re-findings.md`, script side: `docs/04a-script-layer-stutter.md`.
- [x] Copy TS3W.exe and the script DLLs (Game\Bin, Mono assemblies) into `game-copy/` (git-ignored); never analyse the live install.
- [ ] Run REA with Ghidra: map the main sim loop, Mono GC calls, allocator use, thread creation, resource streaming and RefPack decompression.
- [x] Line up what S3SS already patches with those functions, so we only target what is left (`docs/03-prior-art-re.md`, anchors table).
- [ ] Profile live with x32dbg or Frida during tests B to D to find where the main thread spends time and where address space goes.
- [x] Write findings to `docs/04-re-findings.md`, ranked by expected gain (first pass; re-rank after the measurements listed there).

## 4. Build and test our own fixes
- [ ] Build an ASI/DLL hook (S3SS as the reference) for the top findings from step 3.
- [ ] Candidates, to be confirmed by step 3: offloading work from the main thread, trimming address-space waste further, cheaper GC behaviour, faster streaming.
- [ ] Measure each patch against the baseline table, one change at a time, and keep the ones that win.

## 5. 64-bit resource offload helper: design started 2026-10-11 (in parallel with the baselines)
Heavy custom content (`Mods\Packages` via `Resource.cfg`, `.sims3pack` in `DCCache`) fills the 32-bit address space: package indexes, decompressed resources and CAS caches live inside TS3W.exe. A 64-bit helper process holds them instead, and the game maps in only what it needs. Routing data and WorldCache are out of scope (static).
- [ ] Map the resource system and estimate what grows with CC: `docs/08-resource-offload-design.md`.
- [ ] Phase 0: measure address space by category with and without the user's CC set (probe, when the game is free).
- [ ] Phase 1: package index offload. Phase 2: decompressed-resource cache. Phase 3: CAS caches.

Ideas, only if the offload helper works (not scheduled yet):
- Use all cores in the helper: parallel index parsing, a RefPack decompression pool, prefetch of lot objects and CAS parts.
- Preload the whole world's resources (offscreen lots included) into the helper's RAM, so the game's streaming hits a warm cache.
- Raise the game's own resource cache (`Sims3.ini` `[ResourceSystem] MemoryUsageLimit = 20000000`, ~20 MB) and extend `[Preloader]`, once their meaning is verified.
- Warm the Windows file cache before launch (`tools/harness/warm-cache.ts`, written but not used yet).
- NVIDIA/GPU: textures already sit in VRAM through DXVK; DirectStorage only as an optional fast reader in the helper; RefPack decompression stays on CPU cores; GPUDirect Storage is not available on GeForce/Windows.
