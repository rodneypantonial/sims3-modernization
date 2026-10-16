# Sims 3 modernization

Make The Sims 3 run well on modern hardware: fewer crashes (Error 12, crash on save), less stutter, faster loads, and room for heavy mod setups. First collate what existing community fixes already do, then reverse engineer the game and build our own fixes (binary patches and DLL hooks) where those fall short.

## Setup this work targets
- The Sims 3, Steam edition, all expansions and stuff packs
- Windows 11
- Alienware m15 R6: Intel i7-11800H (8 cores / 16 threads, 11th gen, so no Alder Lake fix needed), 32 GB RAM, RTX 30-series laptop GPU
- Game exe: `<Steam library>\steamapps\common\The Sims 3\Game\Bin\TS3W.exe`

## Hard constraints
- **TS3W.exe is 32-bit.** With Large Address Aware it gets 4 GB of address space at most, no matter how much RAM is installed. We cannot raise that without a 64-bit rebuild; we can only waste less of it (smaller stacks, leaner caches and allocators, textures moved out of process via DXVK). Extra RAM still helps through the Windows file cache.
- The simulation (Mono script VM, interaction scheduler, pathfinding) is mostly single-threaded. Gains come from making the main thread cheaper per tick and moving side work to other cores, not from parallelizing the sim outright. Long-term goal: move all work that can safely leave the sim thread to other cores; offload changes are accepted with no gain or up to ~2% loss if stable (PLAN.md Goal, docs/23 #17).
- Always keep `TS3W.exe.orig` and back up `Documents\Electronic Arts\The Sims 3\` before testing a patch.

## Tooling decisions
- **Static analysis:** REA, Reverse Engineer Anything (https://github.com/morluto/rea), an MCP server and CLI that wraps Ghidra/IDA plus static .NET analysis. Use it with Ghidra on copies of TS3W.exe and the Mono script DLLs. It has no Windows runtime tracing.
- **Live profiling and debugging:** x64dbg (x32dbg for this 32-bit exe) or Frida.
- **DLL hook reference:** Sims 3 Settings Setter (https://github.com/sims3fiend/Sims3SettingsSetter), an ASI plugin with assembly patches for stacks, WorldCache, mimalloc, GC throttling, tick limiting and lot streaming. Study it before writing our own hooks and avoid duplicating what it already does.
- Baseline stack: DXVK x32 `d3d9.dll` (has a built-in TS3W profile) + S3SS, optionally the Sims 3 Performance Patch (Nexus mod 235).

## Status
See `PLAN.md` ("Where things stand" and "Next tasks"): it is the source of truth after a context reset. Priority: in-game stutter.

## Repository
- **Published on GitHub** for contributors (`README.md`, `AGENTS.md` for agent rules, branch and PR conventions). Never commit EA game files, decompiled code, saves, dumps or captures, and no personal paths, machine names or emails.
- `.gitignore` keeps EA game files (exe, dll, package), Ghidra/REA projects, dumps, traces and build output out of git. Keep game copies for analysis in `game-copy/`, which is ignored. Commit notes, findings, scripts and our own patch source only.

## Docs
- `docs/01-existing-fixes-comparison.md`: every known fix compared, with what each actually changes
- `docs/02-baseline-install-guide.md`: layer-by-layer install and the measurement table (tests A to E)
- `docs/03-prior-art-re.md`: prior reverse-engineering work, S3SS anchors on the Steam exe, routing and GC background
- `docs/04-re-findings.md`: our verified findings (routing, sim loop, GC) and the ranked big wins
- `docs/04a-script-layer-stutter.md`: script (C#) side of stutter: scheduler, autonomy and routing retry loops
- `docs/05-baseline-results.md` (measurements), `docs/06` (harness prior art), `docs/07` (known fixes), `docs/08-resource-offload-design.md` (offload design)
- `docs/28-sim-thread-map.md`: every known chunk of sim-thread work by owner, cost and offload blocker, ranked (docs/23 #17 step 1)
- `docs/29-mono-jit.md`: embedded Mono 1.2.3 (mint interpreter, no JIT), JIT vs hot-method and runtime fast paths, second managed thread verdict (GC scans one stack)
- `docs/30-mono-second-thread.md`: design for `[MonoThread]`: GC thread registration, multi-stack root scan, gated GC lock, per-thread interpreter frame globals, first proof on copied input
- Engine maps (2026-10-15/16, static unless marked): `docs/32` middleware inventory, `33` physics/collision, `34` SpeedTree boundary, `35` audio (already threaded), `36` scheduler/JobManager, `37` D3D9 device, `38` world/terrain/lots, `39` resource system, `40` textures/VRAM, `41` frame loop, `42` CAS/Sim builder, `43` UI call boundary, `44` lighting/shadows/imposters, `45` object model, `46` render jobs, `47` UTFWin window tree, `48` scene graph/culling, `49` layout loader, `50` object lifecycle, `52` UI drawing, `53` materials/shaders, `54` EAText/strings, `55` UI input
- Off-thread prerequisites: `docs/56` Mono interpreter thread state, `57` GC/allocator (P3 needs 17 detours), `58` Mono caches/loader, `59` autonomy (call graph, read/write sets, snapshot go/no-go), `60` routing state + async routing closures, `61` events/remote calls, `62` + `docs/data/ic-safety.csv` C#-to-native call safety
- Investigations: `docs/51` inference ledger (claims + live/emu verdicts), `63` emulator harness (`tools/emu`), `64` ThreadedObjects stalls, `65` lot scoring, `66` address-space budget, `67` crash triage, `68` hook conflict map vs S3SS/S3PP (`tools/hook-sites.ts --check`), `69` S3PP features vs ours, `72` dropped sim frames (by design: newest-2 interpolation, every message drained), `73` yielding event listeners (8 confirmed, all on rare events; frequent events deferrable on the sim thread inside a script task, never off-thread), `74` script hot spots (Stereo, motive panel, tooltips, lot detail priority, cane, venue hours: fix designs); `tools/check-docs-code.ts` keeps decompiled code out of docs
- `CONTRIBUTING.md` (workflow, PR evidence, parallel agents), `docs/registry.md` (ts3fixes counter slots and ts3native counter versions: reserve before adding), `docs/23-ideas-and-roadmap.md` (ideas beyond PLAN.md)
- `docs/27-seeded-rng.md`: where the script RNG is seeded (`[Config] SimulatorRandomSeed`, else RDTSC), `System.Random()` sites in sim play, `seed_rng` switch design
- `tools/`: `extract-assemblies.ts` (script DLLs out of .package files) and Ghidra headless scripts (`tools/ghidra/run-headless.sh`)

## Working style
- Rodney wants every avenue tried, including binary patches and DLL hooks.
- Mark claims as verified, community-reported, or inferred, and confirm inferred ones in the disassembly or by measurement.
- Change one thing at a time and compare against the baseline table.
