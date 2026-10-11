# Sims 3 modernization

Make The Sims 3 run well on modern hardware: fewer crashes (Error 12, crash on save), less stutter, faster loads, and room for heavy mod setups. First collate what existing community fixes already do, then reverse engineer the game and build our own fixes (binary patches and DLL hooks) where those fall short.

## Setup this work targets
- The Sims 3, Steam edition, all expansions and stuff packs
- Windows 11
- Alienware m15 R6: Intel i7-11800H (8 cores / 16 threads, 11th gen, so no Alder Lake fix needed), 32 GB RAM, RTX 30-series laptop GPU
- Game exe: `<Steam library>\steamapps\common\The Sims 3\Game\Bin\TS3W.exe`

## Hard constraints
- **TS3W.exe is 32-bit.** With Large Address Aware it gets 4 GB of address space at most, no matter how much RAM is installed. We cannot raise that without a 64-bit rebuild; we can only waste less of it (smaller stacks, leaner caches and allocators, textures moved out of process via DXVK). Extra RAM still helps through the Windows file cache.
- The simulation (Mono script VM, interaction scheduler, pathfinding) is mostly single-threaded. Gains come from making the main thread cheaper per tick and moving side work to other cores, not from parallelizing the sim outright.
- Always keep `TS3W.exe.orig` and back up `Documents\Electronic Arts\The Sims 3\` before testing a patch.

## Tooling decisions
- **Static analysis:** REA, Reverse Engineer Anything (https://github.com/morluto/rea), an MCP server and CLI that wraps Ghidra/IDA plus static .NET analysis. Use it with Ghidra on copies of TS3W.exe and the Mono script DLLs. It has no Windows runtime tracing.
- **Live profiling and debugging:** x64dbg (x32dbg for this 32-bit exe) or Frida.
- **DLL hook reference:** Sims 3 Settings Setter (https://github.com/sims3fiend/Sims3SettingsSetter), an ASI plugin with assembly patches for stacks, WorldCache, mimalloc, GC throttling, tick limiting and lot streaming. Study it before writing our own hooks and avoid duplicating what it already does.
- Baseline stack: DXVK x32 `d3d9.dll` (has a built-in TS3W profile) + S3SS, optionally the Sims 3 Performance Patch (Nexus mod 235).

## Status
See `PLAN.md` ("Where things stand" and "Next tasks"): it is the source of truth after a context reset. Priority: in-game stutter.

## Repository
- This is a **local-only git repo**. Do not add a GitHub remote or push: game binaries, Ghidra projects and captures make it too big, and EA's files must not be published.
- First CLI step if it isn't a repo yet: `git init`, then `git add -A` and `git commit -m "Project setup: plan, notes and docs"`.
- `.gitignore` keeps EA game files (exe, dll, package), Ghidra/REA projects, dumps, traces and build output out of git. Keep game copies for analysis in `game-copy/`, which is ignored. Commit notes, findings, scripts and our own patch source only.

## Docs
- `docs/01-existing-fixes-comparison.md`: every known fix compared, with what each actually changes
- `docs/02-baseline-install-guide.md`: layer-by-layer install and the measurement table (tests A to E)
- `docs/03-prior-art-re.md`: prior reverse-engineering work, S3SS anchors on the Steam exe, routing and GC background
- `docs/04-re-findings.md`: our verified findings (routing, sim loop, GC) and the ranked big wins
- `docs/04a-script-layer-stutter.md`: script (C#) side of stutter: scheduler, autonomy and routing retry loops
- `docs/05-baseline-results.md` (measurements), `docs/06` (harness prior art), `docs/07` (known fixes), `docs/08-resource-offload-design.md` (offload design)
- `tools/`: `extract-assemblies.ts` (script DLLs out of .package files) and Ghidra headless scripts (`tools/ghidra/run-headless.sh`)

## Working style
- Rodney wants every avenue tried, including binary patches and DLL hooks.
- Mark claims as verified, community-reported, or inferred, and confirm inferred ones in the disassembly or by measurement.
- Change one thing at a time and compare against the baseline table.
