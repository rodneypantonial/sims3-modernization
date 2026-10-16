# 06. Test harness: prior art and building blocks

Written 2026-10-11 against the Steam build of TS3W.exe (1.67.2.024037, PE timestamp 2014-01-21) and the decompiled script assemblies in `game-copy/decompiled/`. The goal is an automated harness that launches the game into a given save without clicks, controls it from outside, reads its internals and runs repeatable benchmark scenarios.

This document does not repeat `03-prior-art-re.md` (S3SS patches, Smooth Patch, Mono/Boehm facts, NRaas stuck-sim logic) or `04-re-findings.md` (routing chain, simulate loop, GC budget).

Confidence tags:
- **[verified]** I read it in source code, the decompiled C#, Ghidra output or the exe bytes.
- **[community]** a third party says so; not checked.
- **[inferred]** reasoned from code; needs a live run to confirm.

Source code was cloned (read-only) into the session scratchpad: `LazyDuchess/MonoPatcher` (2fa43bf), `Chain-Reaction/NRaas` (2f57186, sparse), `Critical-Reynolds/Sims3-MCP` (7599b42), `sims3fiend/Sims3MonoModder` (63788a9), `simtactics/ts3-template` (ab77f9e), `LazyDuchess/TS3-Interaction-Injector`. No binary was run.

---

## 0. Two corrections to earlier documents (read this first)

### 0.1 `analysis/internal-calls-map.csv` is off by one entry [verified]

The `.data` table that binds script `DllImport` names to native functions is laid out as **`{void* fn, char* name}`**, not `{name, fn}`. The map, and therefore every `IC_*` function name in the Ghidra project, gives each name the function pointer of the **next** entry. Three independent checks:

| Map says | What the function at that address actually does | Real owner (fn before the name) |
|---|---|---|
| `Automation_GetNextScript` = `0x73C410` | Takes one string argument and logs under `"Scripting/Automation"`: this is `TakeSnapShot(string)` | `GetNextScript` = `0x73C500` |
| `GameUtils_GetCommandLineSwitch` = `0xEC0E40` | Writes a value through its argument and reads the `"Config"` section: this is `GetConfigValue` | `GetCommandLineSwitch` = `0xEC2730` (uses `"Scripting/GameHost/pSwitch"`) |
| `Command_ExecuteCommandStringImpl` = `0x73FD80` | Takes no argument and builds `"Scripting/Command/CommandList"`: this is `GetCommandListImpl` | `ExecuteCommandString` = `0x73EA80` |

The table also ends each class block with a `{0, 0}` pair, which the `{fn, name}` reading explains and the `{name, fn}` reading does not (for example `Random_NextDoubleImpl` would get a null function). The map also dropped the last entry of each block (`Random_NextDoubleImpl` = `0x74A780`, `Automation_SetFailureExitCode` = `0x73C0F0`, `Command_GetCommandHelpImpl`).

Consequences for `04-re-findings.md`:
- `Route_Plan` is `0x765160`. The function called `IC_Route_Plan` (`0x765240`) is really `Route_PlanFromPoint`. F1's chain still ends in the same A* search under lock `0x1021CF0`, because the neighbouring entries are all routing calls, but every `IC_*` label in F1 must be shifted by one.
- `GameUtils_IsSingleThreadedMode` is `0xEC1500`. The `xor al,al; ret` at `0x620D60` belongs to `GameUtils_IsCallstackProfilingEnabled`. The "proof" sentence in 04 needs to be redone.
- Fix: regenerate the CSV with `fn = dword[name_ref - 4]`, then rerun `tools/ghidra/ApplyInternalCallNames.java`.

### 0.2 MonoPatcher and S3MM signatures match the Steam build [verified]

`03-prior-art-re.md` says MonoPatcher's addresses are for 1.69 only. Its addresses are, but it finds everything **by byte signature**, and all 8 signatures hit exactly once in our Steam TS3W.exe (the ScriptHost singleton pattern hits three times, all pointing to the same global `0x011EE514`). All 61 byte patterns in Sims3MonoModder (S3MM) also hit, and every S3MM hard-coded fallback address equals our Steam address. S3MM was clearly developed on the Steam 1.67 exe. Scans: `scratchpad/scripts/sigscan.js` and `s3mmscan.js` (node, read-only).

---

## 1. LazyDuchess MonoPatcher and other LazyDuchess TS3 tooling

Repo: https://github.com/LazyDuchess/MonoPatcher (0.3.0, last push 2025-04-04, **no license file**). Wiki: https://github.com/LazyDuchess/MonoPatcher/wiki

### Native side (`MonoPatcher.CPP`, built as `MonoPatcher.asi`, MinHook) [verified]

| Signature target | Steam 1.67 address | Use |
|---|---|---|
| ScriptHost singleton (`mov ecx,[g_ScriptHost]`) | global `0x011EE514` | `ScriptHost::GetInstance()` |
| `ScriptHost::CreateMonoClass(ns, class)` (thiscall) | `0xD7DB80` | Instantiates a managed class from native code |
| `InitializeScriptHost` (thiscall) | `0xD7E560` | Hooked: load plugins, then run the original, then register icalls |
| `mono_add_internal_call(name, fn)` | `0xE82550` | Registers `Namespace.Class::Method` icalls |
| Mono `GenerateCode(MonoMethod*, ...)` | `0xE5CA90` | Hooked: swaps `header->ilBegin/codeSize` for patched methods |
| JIT cache check 1 / 2 | `0xE5637D` / `0xE648DC` | Patched to `90 90` / `EB` to force recompilation (`ForceJIT`) |
| `"ret: more values on stack"` print | `0xE59218` | Patched to `90 E9` to silence the spam |

Exact byte patterns are in `MonoPatcher.CPP/src/Addresses.cpp`. Two things matter for us:

1. **Native plugin loader.** At `InitializeScriptHost` it `LoadLibraryW`s every `*.dll` under `Game\Bin\MonoPatcher\plugins\`, calls the export `InitMonoPatcherPlugin(PluginInitContext*)`, and later calls the export **`ICallSetup(mono_add_internal_call)`**. A plugin can therefore register its own internal calls that C# mods call through `[MethodImpl(MethodImplOptions.InternalCall)]` or a `[DllImport]` stub. This is a ready-made bridge between our native code and our C# agent.
2. **Native-to-managed entry.** After init it calls `ScriptHost->CreateMonoClass("MonoPatcherLib", "DLLEntryPoint")`, which runs that class's constructor. This is how native code starts managed code without `mono_runtime_invoke`.

### Managed side (`MonoPatcher.dll`, .NET 2.0, `NoStdLib`) [verified]

- `[ReplaceMethod]`, `[ReplaceProperty]` and `[TypePatch]` attributes, applied by `MonoPatcher.PatchAll()`.
- `ReplaceMethod` is pure managed: it copies 40 bytes of the replacement `MonoMethod` struct over the original (keeping the name pointer). This works even without the ASI.
- `ReplaceIL(method, byte[])` needs the ASI: it hands the IL to the native `GenerateCode` hook.
- Fallback start without the ASI: the package overrides the game's `GameplayInitializers` XML (`S3_0333406C_..._GameplayInitializers`) and adds `MonoPatcherLib.XMLEntryPoint.Init` as the first action. This overrides an EA resource, so it conflicts with any other mod that does the same.

### Other LazyDuchess TS3 repos

| Repo | Relevance to the harness |
|---|---|
| `TS3-Interaction-Injector` | Pure script mod that adds interactions to object types (`InteractionInjector.RegisterInteraction<Sim>(Sim.Chat.Singleton)`). Useful pattern for an in-game "harness" menu, not needed for remote control. [verified] |
| `TS3ThreadMonitor`, `TS3FrameratePatch`, `TS3PatchLauncher`, `LotPopulation`, `TS3BurglarHate` | Covered in 03, or gameplay mods. `TS3PatchLauncher` is an Origin launcher that loads ASIs. |

---

## 2. NRaas: how the big mods automate the game

Source: https://github.com/Chain-Reaction/NRaas (README: "unprotected EA DLL files ... most if not all fields and methods are made public"; **no license file**).

| Technique | Where | Detail |
|---|---|---|
| Compile against "unprotected" DLLs | `Sims3/Compiler/*.dll`, every `.csproj` (`TargetFrameworkVersion v2.0`, `NoStdLib`) | Mono 1.2 does not enforce member access at run time, so code compiled against de-privatised copies of the game DLLs can touch private fields such as `Commands.sGameCommands.mCommands` or `World.sOnWorldLoadFinishedEventHandler`. [verified source; runtime behaviour community] |
| Boot | `NRaas/Common.cs:84-88` | `World.sOnStartupAppEventHandler`, `sOnWorldLoadFinishedEventHandler`, `sOnWorldQuitEventHandler`, then fan out to `IPreLoad`, `IWorldLoadFinished`, `IWorldQuit` implementations found by reflection. [verified] |
| Console commands | `NRaasDebugEnabler/.../RegisterCommands.cs` | Adds entries straight into `Commands.sGameCommands.mCommands` and calls `CommandSystem.RegisterCommand(name, desc, handler, false)`. Handlers are `int Handler(object[] parameters)`. [verified] |
| Select a Sim | `MasterController/.../Sims/Select.cs`, `NRaas/Helpers/DreamCatcher.cs` | `PlumbBob.ForceSelectActor(sim)`, with household switching. [verified] |
| Push an interaction | `NRaas/Helpers/SimEx.cs:248` | `Definition.Singleton.CreateInstance(WithCallbacks)(target, actor, new InteractionPriority(InteractionPriorityLevel.UserDirected), false, true, ...)` then `actor.InteractionQueue.Add(instance)`. [verified] |
| Spawn Sims | `NRaas/Helpers/AddSims.cs`, `Instantiation.cs` | `SimUtils.SimCreationSpec` / `Sim.MakeRandomSim`, then `Instantiation.Perform`. [verified file list] |
| Background work | `NRaas/Tasks/RepeatingTask.cs`, `Common.cs` | `Simulator.AddObject(new OneShotFunctionTask(...))`, alarms via `AlarmManager.Global.AddAlarm*`, events via `EventTracker.AddListener`. [verified] |
| Logging to disk | `NRaas/Common.cs:1166-1230` | `Simulator.CreateScriptErrorFile(ref handle)` + `CustomXmlWriter(handle)`: the only file write NRaas uses. Files land as `ScriptError_*.xml` in the user folder. [verified] |
| Timing | `NRaas/Common.cs` | `TestSpan` / `ExternalTimeSpanLogger` wrap load phases (pre-load, world-load-finished, quit). Useful model for our load-time metrics. [verified] |

NRaas has no remote-control or scripted-scenario feature. Its value for us is the proven API recipes above. ErrorTrap's exception catching and Overwatch's alarms (03 §5.3) are the closest thing to built-in diagnostics.

---

## 3. Built-in EA commands and debug hooks

### 3.1 How commands are registered [verified]

`Sims3.Gameplay.Core.Commands.RegisterGameCommands` (`Commands.cs:210-354`) registers about 140 commands through a `CommandRegistry`. In `CommandRegistry.Register` (same file), **`CommandType.Debug` commands are never registered** in this build; `Cheat` commands are registered only while `testingCheatsEnabled` is on; `General` commands are always on. The handler methods of the Debug commands still exist and are callable from our own mod, either directly (they are `static int X(object[])`) or by re-registering them the way NRaas does.

The native side `CommandSystem` (`ScriptCore.CommandSystem::Command_*`) offers `RegisterCommand`, `UnregisterCommand`, `ExecuteCommandString(string)`, `GetCommandList()` and `GetCommandHelp(string)`. `ExecuteCommandString` runs any console line, native or scripted, which is how Sims3-MCP implements its `cheat` tool.

### 3.2 Useful script commands (file `Sims3GameplaySystems/Sims3.Gameplay.Core/Commands.cs`)

| Command | Type | Handler | Why it helps the harness |
|---|---|---|---|
| `testingCheatsEnabled` | General, hidden | `OnTestingCheats` | Unlocks Cheat commands |
| `speed <0-4>` | Cheat | `OnSpeedCommand` | 0 pause, 1-3 speeds, 4 = "Skip" through the active Sim's current action |
| `quit` | General | `OnQuitGame` → `GameStates.TransitionToGameStateQuit()` | Clean exit |
| `resetSim <first> <last>` | General | `ResetSim` | Recover a stuck Sim |
| `dialogs [on/off]` | Debug | `OnDialogs` | "Enables/disables dialogs, for soak testing" |
| `Select <first> <last>` | Debug | `OnSelectSim` (`PlumbBob.ForceSelectActor`) | Select a Sim |
| `spawn <n>` / `exterminate <n>` | Debug | `OnSpawn` / `OnExterminate` | Create or kill random Sims (uses `RandomUtil`) |
| `resetEveryone` | Debug | `OnResetEveryone` | Reset all Sims |
| `logobjects [lot]` | Debug | `OnLogObjectsOnLot` | Object inventory per lot |
| `generateObjectStatisticsOnLots` | Debug | `OnGenerateObjectStatisticsOnLots` | Object counts per lot |
| `showSimStats` | Debug | `ShowSimStats` | Instantiated-Sim histogram every 15 sim-minutes |
| `interactioninfo` / `situationinfo` / `loginteractions` | Debug | `OnInteractionInfo` / ... | Interaction-queue diagnostics |
| `traffic [on/off]` | Debug | `OnTraffic` | Turn off the car simulation (a benchmark variable) |
| `storyprogression` | Debug | (same file, line 277) | Toggle story progression |
| `routedebuglog` | Debug | `OnRouteDebugLog` | Only flips `Route.sRouteLogEnabled`; nothing in the release scripts reads it, so it does nothing [verified] |
| `computerSpecs` | Debug | `OnGetComputerSpecs` | Hardware info |
| `zoomToSim <id>` | Debug | `OnCameraZoomToSim` | Camera control |

### 3.3 Native console commands (strings in TS3W.exe) [verified names; behaviour as described by their own usage strings]

`fps [on|off]`, `slowMotionViz <0-8>` (visual only), `suspendrendering` (no usage text beyond its name; likely stops scene drawing, which is what we want for "sim only" runs [inferred]), `playSounds [on|off]`, `fullscreen [on|off]`, `wireframe`, `fadeObjects`, `moveObjects`, `constrainFloorElevation`, `recordVideo`, `RepairObjects`, `TryRefreshPartitions`, `DumpBuildBuyInfo`, `deleteAllEffects`. `GameUtils.EnableSceneDraw(bool)` is also callable from script (`InWorldState.cs`).

### 3.4 A dormant EA automation channel [verified code, protocol unknown]

`ScriptCore.AutomationUtils` exposes `Automation_EnableNetworkListening()` (`0x73C170`), `Automation_GetNextScript()` → string (`0x73C500`), `Automation_TakeSnapShot(path)` (`0x73C410`) and `Automation_SetFailureExitCode(int)` (`0x73C0F0`), wrapped by `Sims3.SimIFace.Automation`. `EnableNetworkListening` creates a `"Debug/Automation"` object, picks a non-loopback IPv4 interface (`WSAIoctl SIO_GET_INTERFACE_LIST`) and waits up to 10 sleeps for a peer. **No release script calls any of these.** `Resource.cfg` still references `Automation/AutomationData.package`. This was EA's internal automated-test hook. Our mod could call it, but the wire protocol and port are unknown and it binds a LAN interface, so a pipe of our own is simpler and safer.

### 3.5 Other script-visible diagnostics

- `ProfilerUtils_BeginEvent/EndEvent/Marker/Counter` and `GameUtils_ResetProfilerScopeAverages` resolve to the same function (`0xC0FD60`), almost certainly an empty stub in release [inferred from the shared address].
- `GameUtils_DumpProfilerReport` / `AddOrConfigureProfiler` share one address too [inferred stub].
- `EATrace_LogImpl` / `EATrace_ShouldLogImpl` exist (native log channel; not checked whether release builds keep any sink).
- The exe has a heap-exception string `ScriptHeap: %.2fMB/%.2fMB (%d); %.2fMB` (crash reporter), so script-heap usage is tracked natively.

---

## 4. Launching TS3W.exe directly and auto-loading a save

### 4.1 Direct launch

Since patch 1.22, `Game\Bin\TS3W.exe` launches the game with all packs, bypassing `Sims3LauncherW.exe` [community: SimsWiki "TS3 Bypassing the Launcher", http://simswiki.info/wiki.php?title=Game_Help%3ATS3_Bypassing_the_Launcher]. Whether the Steam build needs the Steam client running is an open question.

### 4.2 Command-line switches the scripts read [verified]

Scripts read switches with `Sims3.SimIFace.CommandLine.FindSwitch(name)` → native `GameUtils_GetCommandLineSwitch` (`0xEC2730`). The EA launchers pass switches as **`-name:value`** (`Sims3LauncherW.exe` strings: `-file:"%1"`, `-telemetry:`, `-doublebuffered:`, `-patchUnlock:`; `TS3.exe`: `-uninstall:`), and `MainMenuState` strips surrounding quotes from the value, so `-worldFile:"My Save.sims3"` is the expected form [inferred, to test]. Switches found in the decompiled code:

| Switch | Read in | Effect |
|---|---|---|
| **`worldFile`** | `Sims3.Gameplay/MainMenuState.cs:26` | **On the first main-menu entry, loads this file directly.** If it ends in `.sims3` it is treated as a save: `GetSaveFileDetails` then `GameStates.TransitionToGameStateToInWorld(file, info, LiveMode, force: true)`. Otherwise it is treated as a `.world` file. This is the built-in auto-load. |
| `speed` | `InWorldState.cs:272`, `PlayFlowModel.cs:403`, `Gameflow.cs:239` | Initial game speed (int or enum name) after load, and locks out speed changes from Gameplay context |
| `nodialogs` | `Gameflow.cs:400` | `ModalDialog.EnableModalDialogs = false` (unattended runs) |
| `ScriptErrorAction` | `Gameflow.cs:393` | Default action of the script-error window |
| `soaknsave[:seconds]` | `Core/AutoSave.cs:35` | Adds an `AutoSave` task that saves every N real seconds (default 3600) |
| `disablestoryprogression` | `StoryProgressionService.cs:217` | Turns story progression off (a benchmark variable) |
| `autonomy`, `gameflow` | `AutonomyManager.cs:226-229`, `Autonomy.cs:639` | Autonomy settings for tests |
| `campos`, `camtarget` | `Core/Camera.cs:1346`, `LiveModeState.cs:78` | Fixed camera position and target on entering live mode (repeatable framing) |
| `NoPlayFlow`, `NoGameEntry` | `PlayFlowModel.cs:155,169` | Skip the new-game play flow |
| `hideTNS` | `UI/NotificationManager.cs:217` | Hide notifications |
| `season`, `enableLifeEvents*`, `soaktest`, `editInGameMode`, `defaultWorld`, `cas`, `ccinstall`, `ccuninstall`, `ccenable`, `tutorial`, `DebugLoadScreen` | various | Less relevant |

`GameStates.IsPerfTestRunning` exists (`GameStates.cs:452`) but nothing sets it in release, so EA's perf-test mode is gone [verified].

Native strings show `Commandline: %ls` (logged at start), `Services/UTFFoundation/CommandLine` and a `NoInactiveIdle` key next to the `[Script] Multithreaded` key. `NoInactiveIdle` probably stops the game idling when the window loses focus, which matters for unattended runs [inferred, to test].

### 4.3 Skipping the intro

The well-known "No Intro" mod is a plain resource override: it replaces every `_VID` resource (type `0xB1CC1AF6`) of the logo and intro movies with a blank black video [community: https://modthesims.info/d/497903, by ellacharmed]. `Sims3.ini [IntroMovies]` only holds gain values. A native alternative would be to skip `Misc/LoadState/Intro`, but the package override is free and already standard.

---

## 5. How script mods are packaged and the minimum skeleton

### 5.1 Packaging [verified unless marked]

- A script mod is a `.package` (DBPF) containing an **S3SA** resource (the .NET 2.0 DLL) and an **`_XML` tuning resource (`0x0333406C`)** whose instance id is `FNV64("<Namespace>.<Class>")` of the instantiator class.
- The game only tunes assemblies marked **`[assembly: Tunable]`**; tuning sets the `[Tunable] static bool kInstantiator` field, which runs the class's static constructor. [Sims3-MCP README; template `AssemblyInfo.cs`]
- S3SA bodies are XOR-chained; a zero table means plaintext (s3pe writes zeros). Sims3-MCP's `tools/dbpf.py` (MIT) writes packages and S3SA without s3pe. [verified source]
- The `Mods\Resource.cfg` must exist (standard "framework"); delete `Documents\Electronic Arts\The Sims 3\scriptCache.package` after changing a script mod, or the game may ignore it [community, Sims3-MCP README].
- Pure script mods avoid replacing EA assemblies; core mods replace `Sims3GameplaySystems` and conflict with each other (S3MM now detects such conflicts).

### 5.2 Skeleton (pure script mod)

<!-- docs-code: ok -->
```csharp
using System;
using Sims3.SimIFace;
[assembly: Tunable]

namespace TestAgent
{
    public class Instantiator
    {
        [Tunable] protected static bool kInstantiator = false;
        static Instantiator()
        {
            World.OnWorldLoadFinishedEventHandler += OnWorldLoadFinished;  // world ready
            World.OnWorldQuitEventHandler += OnWorldQuit;
        }
        static void OnWorldLoadFinished(object s, EventArgs e) { Simulator.AddObject(new AgentTask()); }
        static void OnWorldQuit(object s, EventArgs e) { /* stop task */ }
    }
    public class AgentTask : Task
    {
        public override void Simulate()
        {
            while (true) { /* poll commands, run them on the sim thread */ Simulator.Sleep(0); }
        }
    }
}
```

Pattern source: MTS "Sims 3 Pure Scripting Modding" (https://modthesims.info/wiki.php?title=Tutorial:Sims_3_Pure_Scripting_Modding) and Sims3-MCP `mod/src/Instantiator.cs`. To also run code at the main menu (for auto-load), hook `World.sOnStartupAppEventHandler` like NRaas and MonoPatcher do.

### 5.3 Compiling today

| Option | Details | License |
|---|---|---|
| **Sims3-MCP `tools/build_mod.py`** | Calls `C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe /nostdlib+ /noconfig /langversion:5` against DLLs extracted from the user's own install, then packs S3SA + XML with `dbpf.py`. No EA files in the repo. | MIT |
| `simtactics/ts3-template` | SDK-style csproj, `TargetFramework net20`, `LangVersion latest`, `HintPath` to EA DLLs in `TS3Core/`. It does not set `NoStdLib`/`DisableImplicitFrameworkReferences`, so the reference to the game's `mscorlib` may be shadowed [inferred]. It **commits EA DLLs**, which we must not copy. | Unlicense |
| NRaas csproj | Old-style `v2.0`, `NoStdLib`, unprotected DLLs | none stated |
| S3MM | Compiles `.cs` mods in-game with the system `csc.exe` against dumped assemblies, with hot reload | GPL-3.0 |

Our repo already extracts the assemblies (`tools/extract-assemblies.ts`). Recommended build: a small Node script that calls `csc.exe` with `/nostdlib+ /noconfig /langversion:5` and the DLLs in `game-copy/assemblies/`, then writes the package. C# 5 syntax is safe on Mono 1.2; generics work, LINQ does not (no `System.Core`).

---

## 6. IPC from inside the game

### 6.1 What the script sandbox allows [verified from decompiled assemblies]

- The game's `mscorlib` `System.IO` has only `Stream`, `MemoryStream`, `BinaryReader/Writer`, `StringReader/Writer`, `TextReader/Writer`. There is **no `File`, `FileStream`, `Directory`, `NamedPipe*`**. `System.Threading` has only `Monitor` (no `Thread`). The game's `System.dll` has no `System.Net` or `System.Diagnostics.Process`.
- The only file APIs are engine icalls: `Simulator.CreateScriptErrorFile` / `AppendToScriptErrorFile` (NRaas logging), `Simulator.CreateExportFile(ref handle, prefix)`, the CSV reader `GameUtils.CreateCSVFile(path)` (path semantics unchecked), and the save/export APIs.
- `Simulator.DidSecuritySystemFail()` reads one global byte (`0x011D677C`) and no script calls it. `Default.ini` has `[Config] Security = 0`. I found no evidence of a code-access-security sandbox; the "sandbox" is the trimmed class library plus the fact that `DllImport` names are resolved through the internal-call table [inferred: MonoPatcher and S3MM both register icalls by name instead of exporting functions, which they would not need to do if normal P/Invoke to arbitrary DLLs worked].

### 6.2 Existing working IPC: Sims3-MCP's memory mailbox [verified source]

Repo: https://github.com/Critical-Reynolds/Sims3-MCP (MIT, pushed 2026-10-07, supports 1.67 and 1.69 per README). It is very close to the harness we want:

- The mod allocates a 1 MB `byte[]` (`Mailbox.cs`). Boehm never moves objects, so the address is stable. The header holds a magic string built at run time (`"S3MCP-MAILBOX-01"`, so the literal in the DLL never matches a scan), a nonce, request/response sequence numbers and lengths, a heartbeat and a "world loaded" flag.
- A `Task` polls the mailbox every simulator tick (`Simulator.Sleep(0)` loop) and runs JSON requests on the game thread through a `Dispatcher`.
- The Python server finds the buffer with `VirtualQueryEx` + `ReadProcessMemory` (it notes Mono's heap pages are `PAGE_EXECUTE_READWRITE`) and writes requests with `WriteProcessMemory`.
- 49 tools: status, list households/Sims, needs, interactions (real pie menu, `InteractionQueue.Add`), `go_here`, `set_speed` (`Gameflow.SetGameSpeed`), `wait`, `age_up` (`AgingManager.AgeTransitionWithoutCake`), `cheat` (`CommandSystem.ExecuteCommandString`), `save_game` (`OptionsModel.SaveGame`), dialogs, build/buy, and a `reflect` escape hatch to read, set or call any member by name.
- Lesson recorded in its README: a modal dialog puts the task that opened it to sleep, so anything that can open one runs on its own task, not on the polling task.

Weak points for a test harness: the mailbox only opens after a world loads (no main-menu control), polling is tied to the sim task (it stalls during long blocking calls or loads), scanning a 4 GB process takes time, and `WriteProcessMemory` from another process is the kind of access anti-cheat or AV tools flag (not an issue offline).

### 6.3 Native bridge (better transport, more work)

Use the MonoPatcher plugin interface (or our own ASI calling `mono_add_internal_call` at `0xE82550`) to register icalls such as `TestAgent.Native::Poll() → string` and `Send(string)`. The native side owns a named pipe or a local TCP socket on a worker thread; the C# side only calls the icall from its task. This removes memory scanning and works at the main menu (icalls are registered before scripts start). S3MM shows the same pattern (`[MethodImpl(InternalCall)]` stubs in `S3MM.Helper`) [verified].

---

## 7. Native side: overlays, consoles and calling into Mono

| Tool | What it can read or do | License |
|---|---|---|
| **S3SS** (Insert key ImGui UI) | Patches and settings, **Memory Monitor** (warns near the 4 GB limit, live page counts, protection flags, free/committed), expanded `xcpt` crash logs, `S3SS_LOG.txt`. No script access. [verified README] | none stated |
| **Sims3MonoModder (S3MM)**, https://github.com/sims3fiend/Sims3MonoModder | ImGui overlay with class/field/singleton inspector and **live object explorer**, **heap browser**, **profiler** (per-method call counts, self and total time, allocations, boxing, icall table; CSV and Chrome-trace export), crash capture, chain hooks and hot-reloaded C# mods. Uses Detours. [verified README and source] | **GPL-3.0** |
| TS3ThreadMonitor | Console of thread creation and priorities | none stated |

**Calling into Mono from native code** [verified source]:
- S3MM resolves the embedding API by signature (all hit in our exe): `mono_class_from_name` `0xE6FA90`, `mono_class_get_method_from_name_flags` `0xE70330`, `mono_domain_get` `0xE75260`, `mono_assembly_open_full` `0xE68590`, `mono_image_open_from_data_full` `0xE84780`, `mono_field_get/set_value` `0xEA1350`/`0xEA1290`, `mono_string_new` `0xEA40B0`, `mono_array_new` `0xEA30F0`, `mono_add_internal_call` `0xE82550`, `mono_lookup_internal_call` `0xE82680`, `mono_gc_malloc` `0xE4BF90`, `GC_apply_to_each_object` `0xE4A3F0`, and the invoke entry **`interp_mono_invoke_with_state` `0xE59FD0`** (S3MM's equivalent of `mono_runtime_invoke`). Patterns are in `src/core/addresses.cpp`.
- S3MM's names (`interp_exec_state` `0xE53EB0`, `MINT_CALL` dispatch, `mono_interp_create_runtime_method`, `interp_transform_method`, a "mint verifier") say that **TS3's Mono runs script code in the Mono interpreter ("mint"), not a JIT** [verified naming in S3MM source; I have not confirmed it in Ghidra]. That matters for performance work, and it means a hook on the interpreter call path sees every managed call, which is how S3MM's profiler works.
- Game-side helpers S3MM also maps: `MonoScriptHost_ProcessTasks` `0xD7FC60` (task scheduler), `ScriptHost_FindClass` `0xD79CA0`, `ScriptHost_LoadImage` `0xD78760`, `GameUtils_StartupComplete` `0xEC0EF0`, `RefPack_Decompress` `0x4EC010`, `IniScalarRead` `0x4EF2B0`.
- MonoPatcher's simpler route: `ScriptHost::CreateMonoClass(ns, cls)` at `0xD7DB80` runs a managed constructor from native code.

---

## 8. Deterministic benchmarking

### 8.1 Randomness [verified]

- `RandomUtil` (`Sims3.Gameplay.Core`) draws from `Sims3.SimIFace.RandomGen.NextDouble()`, which caches an `IRandom` from `AppDomain.GetData("RandomGen")` in the static `gRandomGen`. The default implementation is `ScriptCore.Random` → native `Random_NextDoubleImpl` (`0x74A780`), which calls a native RNG object (app vtable `+0x17C`, then `+0x28`).
- **Seed control from script:** set `RandomGen.gRandomGen` (private static) to our own seeded `IRandom` by reflection, or replace `AppDomain` data before first use [inferred, simple to test]. Natively: hook `0x74A780` or find the RNG object's seeding.
- But 35 call sites use unseeded `new System.Random()` (21 can run in sim play, all one-off events; full list and the native seed `[Config] SimulatorRandomSeed` in docs/27), seeded from `Environment.TickCount` (`mscorlib/System/Random.cs`), and native systems (animation, routing jitter, threads) add timing noise. **Bit-exact replay is not realistic.** The harness should fix what it can (seeded `RandomGen`, fixed save, fixed settings, no story progression, fixed camera) and treat results statistically (N runs, median and spread).

### 8.2 Speed and unattended running [verified unless marked]

- Speeds are tunables in `Gameflow`: `kNormalTimeRate = 1`, `kDoubleTimeRate = 3`, `kTripleTimeRate = 10`, `kSkipTimeRate = 20` (`[Tunable] private static float`). `Gameflow.SetGameSpeed` ends in `GameUtils.SetGameTimeScale(float)` (icall). A harness can call `GameUtils.SetGameTimeScale(x)` directly or override the tunables with an `_XML` resource. "Skip" only applies while the active Sim has a head interaction; otherwise it falls back to Normal.
- The real ceiling is the simulate loop: at most 10 catch-up steps per frame, backlog reset above 19 (04 F2), plus the 20 ms `SleepEx` pacing that Smooth Patch and S3SS change (03 §3). Very high time scales therefore stop being linear [inferred].
- Rendering off: native `suspendrendering`, or `GameUtils.EnableSceneDraw(false)` [inferred effect; to measure].
- Unattended: `-nodialogs` or `dialogs off`, `hideTNS`, `soaknsave` for periodic saves, `NoInactiveIdle` (to test).
- Aging and fast-forward: `AgingManager.Singleton.AgeTransitionWithoutCake(sim)` (Sims3-MCP), `SimClock` and alarms for scheduled steps.

### 8.3 Existing benchmark tools [community]

I found no Sims 3 benchmark save or automated FPS test. Community practice is manual: `fps on` in the cheat console, a clean user folder, mods added back in batches (EA forums: https://forums.ea.com/discussions/the-sims-3-technical-issues-en/framerate-isssue-in-live-mode/8300148). External capture tools (PresentMon, CapFrameX, DXVK HUD) remain the right frame-time source.

---

## Recommended harness architecture

### Components

1. **Runner** (`tools/harness/run.ts`, Node, no dependencies, per the repo's script rule).
   - Prepares a test user folder (copy of `Documents\Electronic Arts\The Sims 3\` with the benchmark save, our agent mod, No Intro, `Mods\Resource.cfg`; deletes `scriptCache.package`), and backs up the real one first.
   - Sets environment for the run (`GC_PRINT_STATS=1` when measuring GC, see 04 F3).
   - Starts `TS3W.exe -worldFile:"<save>.sims3" -speed:0 -nodialogs -disablestoryprogression -campos:… -camtarget:…` with a hard timeout, and starts PresentMon on the PID.
   - Talks to the agent, runs the scenario script, collects results into `results/<date>/<scenario>.json`, kills the process on timeout.
2. **In-game agent** (pure script mod, C# 5, .NET 2.0, MIT-compatible):
   - Start from **Sims3-MCP's design and code (MIT)**: instantiator, task-based poller, dispatcher, JSON, handlers for speed, select, interactions, go-here, age-up, cheat, save, and `reflect`. Keep the attribution.
   - Add harness commands: `wait_until(sim-time | condition)`, `spawn(n, seed)`, `seed_rng(seed)`, `quit`, `snapshot` (Sim count, object counts per lot, interaction queues, `Route` failure counters, script heap via `GC.GetTotalMemory`), and a `metrics` stream.
   - Hook `World.sOnStartupAppEventHandler` so the agent is alive at the main menu (needed if `-worldFile` fails and we have to call `GameStates.TransitionToGameStateToInWorld` ourselves).
   - Call the EA Debug command handlers (`OnSpawn`, `OnLogObjectsOnLot`, `ShowSimStats`, `OnResetEveryone`) directly instead of re-implementing them.
3. **Transport**: phase 1 uses the Sims3-MCP memory mailbox (no native code, works today). Phase 2 swaps in a native icall bridge with a named pipe (our own ASI, or a MonoPatcher plugin via `ICallSetup`) so control also works at the main menu and during loads.
4. **Native probe ASI** (ours, phase 2): timers around `MonoScriptHost::Simulate` (`0xD81840`), `GC_try_to_collect`, the route search (correct `Route_*` addresses from §0.1), catch-up step counts, and `Present`; writes a ring buffer the runner reads. This is the "Frida script" from 04 made permanent.
5. **Scenario files** (JSON): save, seed, settings, steps (`set_speed 3`, `wait 2 sim-days`, `spawn 20`, `save`, `quit`) and pass/fail thresholds against `docs/05-baseline-results.md`.

### Reuse versus build

| Need | Reuse | Build |
|---|---|---|
| Auto-load save | `-worldFile` switch (built in) | Fallback call to `GameStates.TransitionToGameStateToInWorld` |
| Skip intro | No Intro package (MTS 497903) | nothing |
| Remote control | Sims3-MCP mod + mailbox (MIT) | Harness commands, later a pipe transport |
| Commands | EA `CommandSystem.ExecuteCommandString`, Debug handlers in `Commands.cs` | thin wrappers |
| Interaction push, select, spawn | NRaas and Sims3-MCP recipes | nothing new |
| Managed profiling | S3MM profiler and inspector (GPL-3.0: use as a separate tool, do not copy its code into our non-GPL files) | nothing, for exploration |
| Native timing | 04 measurement plan | probe ASI |
| Build | `csc.exe /nostdlib+` recipe, our `extract-assemblies.ts` | Node build-and-pack script (port `dbpf.py`'s writer) |

### Risks

- **Licenses:** MonoPatcher, NRaas and S3SS have no license file, so their code is "all rights reserved" by default; learn from them, do not copy. S3MM is GPL-3.0. Sims3-MCP is MIT (safe to fork). `ts3-template` is Unlicense but contains EA DLLs, which we must not commit.
- **Mod interference:** S3MM, MonoPatcher's XML entry point and S3SS all hook the script host or Mono; running several at once in the harness can change the very timings we measure. Each benchmark run must record exactly which ASIs and mods were loaded.
- **Observer effect:** a per-tick poller and native timers cost main-thread time. Measure the harness's own overhead (agent loaded but idle versus not loaded).
- **Determinism:** see 8.1. Plan for statistical comparisons, not exact replays.
- **Dialogs and blocking calls:** a modal dialog or a long blocking call stalls the agent task; the runner needs a watchdog and a hard kill.
- **Save safety:** run only on copies of the user folder; never point the harness at the real `Documents\Electronic Arts\The Sims 3\`.
- **Wrong labels in Ghidra:** fix the off-by-one icall map (§0.1) before writing any native probe that targets an `IC_*` name.

---

## Open questions

1. Does `-worldFile:"Name.sims3"` (colon syntax, save name relative to `Saves\`) load a save on the Steam build? Does the Steam `TS3W.exe` need the Steam client running?
2. What exactly does `NoInactiveIdle` do, and is it a command-line switch or an ini key? Does the game throttle or pause when unfocused?
3. Does `suspendrendering` keep the simulation running at full speed, and can it be turned back on?
4. Is TS3's Mono really interpreter-only (S3MM naming), and what does that mean for per-call costs in our profiles?
5. Does replacing `RandomGen.gRandomGen` with a seeded `IRandom` take effect for all `RandomUtil` users, and how much run-to-run variance remains after seeding? Priority parallel lane since 2026-10-15, steps in PLAN.md "Resume here".
6. What protocol does EA's dormant automation listener (`Automation_EnableNetworkListening` / `GetNextScript`) speak, and on which port? Is `Automation/AutomationData.package` present in the Steam install?
7. Does `GameUtils.CreateCSVFile(path)` read arbitrary files from disk? If so, it is a file-based command channel that needs no native code.
8. How far can `GameUtils.SetGameTimeScale` go before the 10-step catch-up cap makes sim time non-linear, with and without S3SS's tick limiter changes?
9. Does Sims3-MCP's mailbox poller keep running while the game is paused and during save operations?
10. After regenerating the internal-call map, does F1 in `04-re-findings.md` still hold with the correct `Route_Plan` entry (`0x765160`)?

## Sources

- MonoPatcher: https://github.com/LazyDuchess/MonoPatcher and wiki https://github.com/LazyDuchess/MonoPatcher/wiki
- LazyDuchess repos: https://github.com/LazyDuchess/TS3-Interaction-Injector, https://github.com/LazyDuchess?tab=repositories
- NRaas: https://github.com/Chain-Reaction/NRaas (`NRaas/Common.cs`, `NRaas/Helpers/SimEx.cs`, `NRaasDebugEnabler/DebugEnablerSpace/Interactions/RegisterCommands.cs`, `NRaasMasterController/MasterControllerSpace/Sims/Select.cs`)
- Sims3-MCP: https://github.com/Critical-Reynolds/Sims3-MCP (`mod/src/Mailbox.cs`, `Instantiator.cs`, `Handlers/ControlHandlers.cs`, `tools/build_mod.py`, `tools/dbpf.py`)
- Sims3MonoModder: https://github.com/sims3fiend/Sims3MonoModder (`src/core/addresses.cpp`, `src/core/mono_profiler.*`, `S3MM.Helper/S3MM.cs`)
- S3SS: https://github.com/sims3fiend/Sims3SettingsSetter (README)
- ts3-template: https://github.com/simtactics/ts3-template
- MTS Pure Scripting tutorial: https://modthesims.info/wiki.php?title=Tutorial:Sims_3_Pure_Scripting_Modding
- MTS Object Modding tutorial (DLL extraction, VS setup): https://modthesims.info/wiki.php?title=Tutorial:Sims_3_Object_Modding
- SimsWiki, bypassing the launcher: http://simswiki.info/wiki.php?title=Game_Help%3ATS3_Bypassing_the_Launcher
- No Intro with Maxis logo: https://modthesims.info/d/497903
- EA forum, `fps on` and clean-folder testing: https://forums.ea.com/discussions/the-sims-3-technical-issues-en/framerate-isssue-in-live-mode/8300148
- Local, read-only: `game-copy/decompiled/**` (`Commands.cs`, `MainMenuState.cs`, `InWorldState.cs`, `Gameflow.cs`, `AutoSave.cs`, `RandomUtil.cs`, `RandomGen.cs`, `ScriptCore/AutomationUtils.cs`, `ScriptCore/CommandSystem.cs`), `game-copy/bin/TS3W.exe` strings and icall table, `Sims3LauncherW.exe` and `TS3.exe` strings, `Default.ini`, `Sims3.ini`, `Resource.cfg`, Ghidra decompiles of `0x73C170`, `0x73C410`, `0x73C500`, `0xEC0E40`, `0xEC2730`, `0x73EA80`, `0x73FD80`, `0x76CD80`, `0x74A780`.
