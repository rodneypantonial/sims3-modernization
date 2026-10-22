# 166. Per-task attribution of script-batch hitches without samplers

Most >= 300 ms hitches are led by a scheduler stage (TaskObjects, ThreadedObjects, OneShotObjects, ScriptTick; docs/161, docs/162), but measurement runs use `--no-samplers` (docs/161 §6: the samplers pause the sim thread), so the "script" cause had no name. This adds per-run timing inside the four seams to `tools/frida/stutter-profile.js`: every hitch line names the slowest script runs of that iteration, with no thread suspended. Built and tested on a dummy; **not run in game**.

Tags: [verified] = disassembly, decompile or live data already on disk; [dummy] = shown by `tools/probe/stutter-tasks-test.py`; [inferred] = to confirm in game.

## 1. Where it hooks

| Hook | What | Per call | Tag |
|---|---|---|---|
| `0xD7F000` | resume one TaskObjects / ThreadedObjects task: thiscall(host, &entry, bool, bool); entry `+8` = task state (`[0]` MonoMethod, `[1]` task object, `+0x140` saved depth or -1, frame cells at `+0x3C + 4 * depth`, cell `+4` = MonoMethod) | the run's object, class, entry method and resume frames (all memory the game reads next), QPC | call shape and entry layout verified (decompile `sched-seams.c`, docs/04 F17, docs/130 §1.6); object/class/method layout verified live by the probe's `/taskstall` (249 records, 2026-10-13..19: `ScriptCore.ScriptProxy.Simulate`, `FunctionTask.Simulate`) |
| `0xEA1090` | `mono_runtime_invoke` (`jmp [0x1195B60]`); ScriptTick and OneShotObjects call it once per object | method and object pointers only, QPC | verified (decompile: OneShot `0xD74800` and ScriptTick `0xD80BC0` call sites) |
| `0xE53620` | frame save of a yielding task (cdecl(state, records, method)) | at its return: the top 3 saved frames (the yield point) | verified static (docs/130 §1.6) |
| seam entries (existing JS hooks) | write the stage id for the C hooks; 0 on return | one 4-byte write | - |

All three are a Frida CModule (`TASK_C` in the profiler), not JS: no JS lock per task. Gating: sim thread only, inside a seam only, outermost run only (a nested runtime invoke from an IC inside a task belongs to that task). A run shorter than `taskMinMs` (default 2 ms) costs the gate, two QPC reads and a counter; a longer one goes into a top-5 table. None of the three addresses is detoured by ts3native, S3PP or S3SS (docs/68: `[Verify]` owns `0xE59FD0`/`0xE5A600`, `[RouteWorker]` swaps the slot `0x1195B60`, which the relocated `jmp [slot]` still follows). Wrong prologue bytes or a CModule failure turn only the attribution off (`[ts3prof] task attribution off: ...`), never the profiler.

## 2. How names are read

At the iteration boundary (`GC_try_to_collect` entry, the sim thread, between game calls) and only for hitch lines (iteration >= `hitchMs`), JS names the kept runs; names are cached by pointer (class and method metadata live for the run):

- class: MonoClass `+0x34` name, `+0x38` namespace; method: MonoMethod `+8` class, `+0x18` name [verified live, `/taskstall`].
- wrappers: `mono_class_get_field_from_name 0xE6F720` (cdecl, walks parents; called on the sim thread, cached per class) finds `mTarget` (ScriptProxy -> the logic object's class, e.g. `ScriptProxy>Sims3.Gameplay.Actors.Sim`) or `mFunction` (EA `OneShotFunctionTask`, NRaas `FunctionTask` -> the delegate). A delegate is a class with a `method_info` field; its MonoReflectionMethod `+4` is the MonoMethod (`FunctionTask>NRaas.OverwatchSpace.Alarms.StuckCheck.PrivatePerformAction`). Field layout (`MonoClassField` type `+0`, attrs `type+4` with `0x10` = static, offset `+8`; objects without a sync word) verified static (I-123-28); `method_info` filled by the interpreter's delegate constructor and NRaas' field name `mFunction` are [inferred] (fallback: the wrapper's own class name).
- every read is in JS `try/catch`; the task object's class is re-read and compared with the class the C hook took while the task ran (a reused object keeps the class name only). Invoke runs take the class at the boundary (the object is still in its seam's map).

Hitch line fields: `tasks: [{stage, name, ms, from?, to?}]` slowest first (`from` = resume frames, `to` = yield frames, innermost first, 3 deep; absent for a fresh start or a run that ended), `taskRuns: {stage: runs}` and `taskDropped` (slow runs pushed out of the top 5). Invoke runs add `: <method>` (e.g. `ScriptProxy>...AlarmManager: Simulate`); a static invoke is the method name.

## 3. Overhead [dummy]

`stutter-tasks-test.py` (32-bit ping.exe, i7-11800H, noisy machine): 200,000 one-loop tasks in one TaskObjects call, attribution off vs on: **~300 ns per task run** (1.25 -> 62 ms). A runtime invoke outside any seam (gate only): **~215 ns per call**. Both include Frida's trampoline. Game cost = runs per second x that [inferred]: a few hundred task runs per iteration at ~30 it/s is ~2-3 ms/s; the invoke count outside the seams (window procs, event callbacks) is not known and is the term to read first in game (compare a `tasks:false` run's it/s, or count with the `taskRuns` totals).

## 4. Dummy checks (RESULT PASS, 2026-10-22)

Names and stages of all five kinds, slowest first, resume and yield frames, no `from`/`to` for a fresh run, run counts per stage with the nested invoke and a second thread running the same task left out, the fast task under `taskMinMs` not kept, kept ms within the seam ms, refusal on wrong bytes (hitch lines stay, log names the hook), dummy alive after unload, and three unloads (`frida_tools.repl -t 1`, as `profile-world.ts` runs it) while the sim thread is inside a 2 s task run: dummy alive, loop still running (Gum skips a detached listener's leave callback).

## 5. Reports

- `worst-hitches.ts cause()` returns `tasks` (top 3 as `name ms (yield frame)`) and `topTask`; the worst table has a "slowest script runs" column.
- `freeze-report.ts`: causes table "slowest script run" (most common top task per class), worst table "slowest script runs (ms)".
- `hitch-ab.ts --causes`: a script-led hitch is `script: <top task name>` when the line has `tasks`, before the sampled managed roots.
Self-checks with fixtures in all three.

## 6. Limits and next step

- Runs inside a seam that are not task resumes or runtime invokes (native work of the seam itself, map walks) stay in the seam's ms only.
- A task run's name says which object ran and where it resumed and yielded, not which method used the time in between; for that, a sampler run (or `/taskstall`) is still the tool.
- First game run: any `freeze-measure` job (the profiler is always loaded); check the log line `task attribution: runs >= 2 ms`, then that TaskObjects-led hitches carry `tasks` whose ms add up to most of `taskObjects` (ledger I-166-1..3).
