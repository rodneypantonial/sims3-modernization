# Reverse-engineering knowledge base

What we know about the inside of The Sims 3 (`TS3W.exe` and its C# scripts), collected from the research docs (`docs/03`-`docs/22`), `PLAN.md` and our own code (`native/ts3native`, `mods/ts3fixes`, `tools/probe`, `tools/frida`). Start here if you want to help.

No EA code is in this repository: no decompiled C#, no pseudo-C, no disassembly beyond the few bytes our patches check before they write. Everything below is names, addresses, offsets, signatures and behaviour described in our own words. Reproduce the analysis on your own copy of the game (below) to read the code itself.

## Target build

| | |
|---|---|
| Game | The Sims 3, Steam, patch 1.67 (1.67.2.024037), all expansions and stuff packs |
| Exe | `Game\Bin\TS3W.exe`, 32-bit, Large Address Aware, image base `0x400000`, no ASLR, PE timestamp 2014-01-21 |
| SHA-256 | `10b2423e2368e161774462b0ef1960231ad0af298d564a7bb6a6e78c5e880609` |
| Script runtime | Embedded Mono 1.2.3.1, **interpreter only** (no JIT), Boehm GC 6.x/7.1-ish with EA edits |

Every native address in these docs is for that exact file. The disc/EA App builds differ; S3SS-style byte patterns are the way to port an address.

## Files

| File | What it holds |
|---|---|
| [native-symbols.md](native-symbols.md) | Every native function, call site and global we named (504), grouped by subsystem: sim loop, scheduler, Mono interpreter, GC, allocator, routing (plan chain, A*, lot graph, water JPS, locks), resources/DBPF, save, frame loop, animation, strings, online. Signature, thread, notes, source doc, and whether the bytes were checked against the exe |
| [structs.md](structs.md) | Known object layouts and globals with confidence marks: script host, interpreter state and frames, MonoMethod/MonoClass, Boehm globals, route objects, routing world, lot planner and A* nodes, EA heap, resource caches, `ts3native_counters` |
| [managed-methods.md](managed-methods.md) | The C# classes and methods that matter for stutter, stuck Sims and save growth: what they do, where the time goes, which fix touches them, and the full ts3fixes replacement table |
| [patch-points.md](patch-points.md) | Every patch, detour and hook we use or tested (ts3native, ts3fixes, probe/profiler), with expected bytes, status and conflicts with S3SS, S3PP, MonoPatcher, NRaas and Smooth Patch |
| [dead-ends.md](dead-ends.md) | Approaches we tried or analysed and dropped, with the evidence, so nobody repeats them blind |
| [`tools/ghidra/symbols/ts3w-1.67.symbols.csv`](../../tools/ghidra/symbols/ts3w-1.67.symbols.csv) | Importable symbol file: 504 of our names plus 1,706 functions named after their script internal calls (2,210 rows) |
| [`tools/ghidra/ImportSymbols.java`](../../tools/ghidra/ImportSymbols.java) | Ghidra script (GUI or headless) that applies the CSV |

**Confidence marks** used throughout: **[verified]** read in a decompile, the disassembly or the exe bytes; **[measured]** seen in a live run (probe, profiler, logs); **[inferred]** reasoned from code, not yet confirmed; **[community]** from another project's source or notes. The `Check` column in native-symbols.md and the `[verified: ...]` / `[unverified: ...]` tag in the CSV say how each address was checked when the files were generated.

## Reproduce the analysis on your own copy

You need your own legal copy of the game. Keep everything you copy or produce in `game-copy/` (git-ignored) and never commit it.

1. **Check the exe.** Copy `Game\Bin\TS3W.exe` to `game-copy/bin/` and compare its hash with the one above:
   ```
   sha256sum game-copy/bin/TS3W.exe                        # Git Bash / Linux / macOS
   Get-FileHash game-copy\bin\TS3W.exe -Algorithm SHA256   # PowerShell
   ```
   A different hash means a different build: our addresses will not match.
2. **Extract the script assemblies.** The C# scripts live as encrypted S3SA resources (type `0x073FAA07`) inside `gameplay.package`, `scripts.package` and `simcore.package` under the game's `Game\Bin` tree. Copy those packages to `game-copy/packages/`, then:
   ```
   node tools/extract-assemblies.ts game-copy/assemblies game-copy/packages/*.package
   ```
   It reads the DBPF index, RefPack-decompresses and decrypts each S3SA and writes the DLLs (10 on Steam 1.67: `ScriptCore`, `SimIFace`, `Sims3GameplaySystems`, `Sims3GameplayObjects`, `Sims3StoreObjects`, `Sims3Metadata`, `UI`, `mscorlib`, `System`, `System.Xml`). Node 22.18 or newer runs the `.ts` file directly.
3. **Decompile the C#** with ILSpy's command-line tool (the project files keep namespaces as folders):
   ```
   dotnet tool install -g ilspycmd
   ilspycmd -p -o game-copy/decompiled/Sims3GameplaySystems game-copy/assemblies/Sims3GameplaySystems.dll
   ```
   (repeat per DLL). The same tool decompiles mod assemblies extracted the same way (NRaas, Smooth Patch) when you need to know what they hook; read them, do not copy them.
4. **Import the exe into Ghidra** (we use Ghidra 12.1.4 with JDK 21). Use a project at `game-copy/ghidra`, project name `TS3`. Give it a lot of memory: with 16 GB two analyzers (X86Analyzer, StackVariableAnalyzer) still ran out of memory, so some functions are missing (for example `MonoScriptHost_Simulate 0xD81840`; the import script below creates the ones it needs).
   ```
   GHIDRA_MAXMEM=16G analyzeHeadless game-copy/ghidra TS3 -import game-copy/bin/TS3W.exe
   ```
5. **Import our symbols** (also names every internal call, so step 6 is optional). Headless writes the project, so no `-readOnly`:
   ```
   analyzeHeadless game-copy/ghidra TS3 -process TS3W.exe -noanalysis -scriptPath tools/ghidra \
     -postScript ImportSymbols.java tools/ghidra/symbols/ts3w-1.67.symbols.csv
   ```
   In the GUI: Script Manager, category `Sims3`, `ImportSymbols`, pick the CSV. Options after the CSV path: `keep` (do not rename symbols you already named yourself), `force` (apply to a different hash; not recommended). On a fresh Steam 1.67 project it names about 2,090 functions, creates the ~17 the analysis missed and sets 122 labels; one internal call (`IC_UIManager_GetWorldMetadataImpl 0x77FE80`) cannot become a function in our database.
6. **Regenerate the internal-call names yourself** (optional, as a check): `node tools/internal-calls-map.ts game-copy/bin/TS3W.exe > game-copy/analysis/internal-calls-map.csv` reads the `{fn, name}` binding tables in `.data` (1,758 bindings; it asserts `Route_Plan = 0x765160`), and `tools/ghidra/ApplyInternalCallNames.java` applies them as `IC_<Method>`. `tools/ghidra/anchors.csv` holds our first hand-named anchors (`ApplyInternalCallNames.java <csv> anchors`).
7. **Read code from the command line.** `tools/ghidra/run-headless.sh <Script.java> <args>` runs a script read-only against `game-copy/ghidra` (edit the Ghidra and JDK paths at the top for your machine). Scripts: `DecompileTo.java <out.c> <depth> <name-or-0xaddr>...` (decompile functions and callees), `StringXrefs.java` (who uses a string), `CallPaths.java` (call paths between functions), `ExportFunctions.java <out.csv>` (every function with its range, for symbolizing stack samples).
8. **Watch it live** (Windows, game running): `tools/probe/ts3probe.py serve --launch` (Frida probe: sim-thread native and managed stacks, memory, resource caches, GC counters, ts3native counters) and `tools/frida/stutter-profile.js` (per-iteration profiler of the sim loop stages). Their hook addresses are in [patch-points.md §3](patch-points.md). Both check the bytes at every hook and refuse to attach elsewhere.

## Where to start

- Stutter is mostly the sim thread: the interpreter (~29%), route planning (~11%, more in Isla Paradiso), the EA allocator (~7%), GC (~8-17%), animation, spatial queries. See the shares in [native-symbols.md](native-symbols.md) and [docs/20](../20-offload-candidates.md).
- Before you try to move work off the sim thread, read [dead-ends.md](dead-ends.md) (async route planning crashed three times on unlocked shared state).
- Open questions are marked [inferred] in these files and in the source docs; confirming one in the disassembly or by a measurement is a good first contribution.

## Contributing to this knowledge base

- Add a new address to both `native-symbols.md` (readable table) and the CSV (importable), with a `[verified: ...]` or `[unverified: ...]` tag and the doc or measurement that backs it. Keep names in `Subsystem_Action` form and say where the name came from if it is not ours.
- Describe behaviour in your own words. Byte signatures, offsets and short instruction mnemonics that identify a patch point are fine; function bodies and longer listings are not.
- Keep personal paths and machine details out of docs and code.
