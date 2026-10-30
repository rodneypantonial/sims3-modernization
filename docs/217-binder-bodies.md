# 217. What EA's binder native bodies write

Written 2026-10-30. Static only: no game run, no build. Contributor research round 2, task 2.

Question: EA replaced 1,085 managed methods with native bodies, bound through the binder (`0xD9A7B0`, docs/130 D17). Satb's mint barrier does not see their stores, and docs/212 classified them only by the IL of the managed method they replace (a proxy). This doc classifies every body from its own code: pure reader, writes only its out / ref / array arguments, writes other managed memory, writes native state only, or unknown, and marks the bodies the `[AutShadow]` worker can reach.

Output: `docs/data/binder-bodies.csv`, 1,085 rows, one per registration. Tools: `tools/binder-bodies.py` (registrations, store analysis, classes) and `tools/binder-bodies.ts` (managed facts: bound method, parameter kinds, field layout, IL field writes, worker reach).

## 1. Results

| Class | Rows | Worker-reachable (any label) | Direct (`v0` or `tier2`) |
|---|---|---|---|
| reader | 660 | 120 | 44 |
| writes-args | 24 | 11 | 3 |
| writes-other | 325 | 48 | 17 |
| native-only | 32 | 15 | 6 |
| unknown | 44 | 24 | 10 |
| **total** | **1,085** | **218** | **80** |

Confidence: high 502, medium 539, low 44 (the unknown rows). Medium means the row's class may be too low: 516 rows pass a managed pointer to a call the analysis cannot follow (`opaque_handoffs`: mostly indirect calls, 40 truncated GC or allocator callees), and 23 more have a native store whose base pointer was lost (`unresolved_native`).

Reach labels are the ones of docs/215 (PR #112): `v0` = the docs/122 pilot closure, `tier2` = a docs/126 ready or new-rows candidate, `-cha` = through overrides up to 5 hops by class hierarchy, `tier2-blocked` = only through blocked candidates, `no` = not reachable (867 rows).

What the classes contain [verified, from the CSV]:

- **writes-other (325).** 262 rows write only fields of `this`: `Mono.Xml2.XmlTextReader` 79, `System.Text.StringBuilder` 57, `System.Xml.XmlTextReader` 33, `Sims3.Gameplay.Utilities.Curve` 18, `System.Xml.XmlParserContext` 17, `ScriptCore.PersistReader` 16, `Sims3.Collections.PriorityQueue` 8. 43 rows write objects reached from arguments, 8 both. 12 rows write static fields: 7 `.cctor`s plus `SimClock.set_TicksAdvanced`, `set_StartTime`, `SetStartTime`, `AddOneHour`, `AddOneDay`. None of the 12 static writers is reachable from the worker.
- **writes-args (24).** Sizes are in the column: `this:4` (struct `this`, 9 rows, e.g. `DateAndTime..ctor(long)`), `arg1:4` 6, `arg0:4` 5 (e.g. `SimClock.ElapsedTimeInMinutes(ref float)`), `arg2:4` 2, `arg0:1` 1, and `XmlTextReader.ParseAttributeFromString`'s three byref arguments 1.
- **native-only (32).** 27 rows write only the IC nesting counter `[0x11F2D84]` (§4.2). 5 are `String.memcpy`/`memcpy1`/`memcpy2`/`memcpy4`, which write through their `byte*` destination (`arg0 target`); the destination is managed memory whenever the caller passes a string's characters.
- **reader (660).** No store outside the interpreter frame, the return slot, the thread context and runtime bookkeeping. 254 readers have an opaque handoff, so their confidence is medium.

Direct worker-reachable writers (80 direct rows, 36 that store):

| Group | Rows | What they write |
|---|---|---|
| `StringBuilder` constructors, `Append`, `Insert`, `Remove`, `InternalEnsureCapacity` | 13 | `this._str`, `_length`, `_cached_str`, `_maxCapacity` |
| `String.Format(IFormatProvider, string, object[])`, `FormatHelper`, `Array.Sort` | 3 | objects reached from the `object[]` / `Array` argument (offset unknown, medium), plus the IC counter |
| `String.InternalSetChar` | 1 | the string's characters in place |
| `DateAndTime..ctor(long)`, `SimClock.ElapsedTimeInMinutes(ref float)`, `String.ParseDecimal(string, ref int)` | 3 | struct `this` or the byref argument |
| `String.memcpy*`, `String.Format(string, object, object)`, `Enum.GetName` | 6 | native only: the copy destination or the IC counter |
| unknown: `SimClock` day helpers, `DateAndTime.get_DayOfWeek`, `Substring`, `Concat`, `Replace`, `ParseFormatSpecifier` | 10 | 9 copy into a string built on the same path (§3) |

Most of these write objects the caller just made (a `StringBuilder` for a tooltip or a key, a result string). The CSV cannot tell a fresh receiver from a shared one; that is a property of the call site (§4.3).

## 2. Method

| Step | How | Tag |
|---|---|---|
| Registrations | the table builder `0xE3E570` calls `0xD9A860(hash, adapter, body, init)` 1,085 times; `index` = call order (the hashes ascend) | [verified] |
| Bound method | hash -> method by `managed-callgraph.ts` `bindKeys` (the binder's own key string); 1,085 of 1,085 bound | [verified] |
| Entry and roots | the dispatcher calls `adapter(MonoInvocation frame, ThreadContext)` through `RuntimeMethod+0x24`; frame `+0x0C` = `this`, `+0x14` = arguments in 8-byte slots, `+0x08` = return slot, `+0x20` = pending exception (docs/130 call path) | [verified] |
| Stores | abstract store analysis over symbolic inputs (stack arguments, entry registers): each function is analysed once, its summary (stores, final registers, stack pop) is substituted at every call site; pointer values are tracked 4 loads deep | [verified] method; per-row results as tagged |
| Stops | Mono allocation entries return a fresh object; managed re-entry (`ScriptHost_NativeToManaged 0xD8A8E0`, interpreter and `runtime_invoke` entries, class init `0xEA3370`) is recorded, not followed; the exception builders `0xE77000`-`0xE775FF` and `mono_raise_exception 0xEA18F0` end the path (docs/122 I-122-26); the EA static-field helpers `0xD89CD0` / `0xD89BF0` / `0xD89C60` are modelled | [verified] by reading each |
| Not counted | stores to the interpreter frame, the return slot and `ThreadContext` (per script thread, docs/57); runtime bookkeeping (stores in Mono and the GC at `>= 0xE40000`, the EA heap `0x4E0000`-`0x4EFFFF`, EA runtime support such as the binder `0xD9A7B0`, and records reached through the binder's init globals `0x11EE000`-`0x11F3000`); stores to objects the body allocated | [verified] ranges |
| Field names | Mono 1.2 `mono_class_layout_fields` rules with this build's 4-byte object header (docs/130 D18): parent fields first, reference fields first for auto-layout classes outside mscorlib, other fields sorted by alignment, structs in declaration order, explicit layout from the FieldLayout table | [verified] check below |

**Layout check [verified].** Of the 859 native stores through `this` that the analysis resolves, 851 land on a field start with the field's size under the model above. Declaration order matches 714, gc-aware without the alignment sort 758. The 8 misses: 7 are the high half of a `long` field in a struct `this` (`DateAndTime`, `PriorityQueue+QueueItem`; the CSV shows them as `this:4`), and 1 is `String.InternalSetLength` at `+4` (strings are not laid out by the model; their stores show as characters in place). The `--self-check` of `binder-bodies.ts` pins three known layouts (`PriorityQueue`, `Curve.m_x` at `0x14`, `Vector3`).

## 3. The 44 unknown rows

| Group | Rows | Why unresolved | Likely target |
|---|---|---|---|
| copy into a string the method allocates (`Substring`, `Concat`, `Replace`, `Insert`, `Remove`, `PadLeft`, ...) | 12 | the destination comes from an icall pointer that the body's init stores in a binder global (for `Concat(string, string)`: `call [0x11EF4A0]`), so its result is unknown to the analysis | the fresh result string [inferred]: the IL calls `String.InternalAllocateStr` |
| path runs into such a body | 19 | same store, reached through a callee body (`String.Concat` 14, `String.Copy` 4, `String.Remove` 1) | a fresh string [inferred]; for the `SimClock` day helpers it is the message of an `IndexOutOfRangeException` (`IntToDay` builds it with `Concat`, [verified] in the IL) |
| `GetCustomAttributes` (`MonoCustomAttrs`, `MonoType`, `MonoMethod`) | 6 | store at `0xD9CCA4` through a lost pointer | unknown |
| `ToUpper` / `ToLower` (with and without culture) | 4 | char-by-char store into a buffer from the culture's `TextInfo` path | likely the result string [inferred] |
| `ParseFormatSpecifier`, `PersistReader.ReadObjectTable`, `XmlTextReader.SkipTextDeclaration` | 3 | merged paths lose the base (`ParseFormatSpecifier` also writes its byref ints and the IC counter) | unknown |

The init passes a callee index to `0xD9A890` (`0x39`, `0xBA`, `0x9D`, ... for `Concat`) and `0xD89F00` turns the record into the icall pointer. These indexes are not MethodDef rows of mscorlib (tested: rows `0x49` and `0x56` are `Exception.set_Source` and a `SystemException` constructor). Resolving them would turn the first 31 rows into stores on fresh strings. The `unknown_hint` column carries the inferred target.

## 4. Findings for the worker

1. **Re-entry [verified].** 862 of 1,085 bodies can call back into managed code through `ScriptHost_NativeToManaged 0xD8A8E0` (162 also through `mono_runtime_invoke`). That code runs in the interpreter, so Satb's mint barrier and the managed closures cover its stores; the native part of the body does not. docs/122 found the same for its 78-body closure (59 enter the interpreter).
2. **IC nesting counter `[0x11F2D84]` [verified].** 131 bodies reach a write of it, 29 of them reachable from the worker (`v0` 4, `tier2` 2, `v0-cha` 9, `tier2-cha` 14). A byte scan of the exe finds 121 write instructions (18 `add`, 103 `sub`) in the glue range `0xD9B18D`-`0xE37F5B`, all non-atomic. docs/122 §12 lists only `ParseFormatSpecifier 0xDA9790` as a writer: that holds inside its 78-body closure, not for the binder as a whole. A worker that runs any of these bodies races the sim thread's counter (docs/122 I-56-1). Fix shapes [design]: add these bodies to the F2 abort list, or give the worker its own counter (the binder table can be patched at install, docs/212 §fix shape).
3. **Fresh or shared [inferred].** The direct worker-reachable writers mostly write a receiver the caller made in the same scoring call (`StringBuilder`, result strings). A receiver that outlives the call is a real race: `Curve.Fx` writes `this.m_x` on a tuning curve shared by every Sim (reach `v0-cha`, 4 `Curve` rows reachable), and `PriorityQueue` bodies write the alarm queue (docs/212). The call sites decide; the next step is a fresh-receiver check per call site in the managed closures (`managed-callgraph.ts` already has `newobj` edges).
4. **Satb coverage.** The CSV is the list docs/212 §fix shape needs: 660 readers need nothing; the 349 writes-args and writes-other rows need sim-side logging or a worker stop; the 32 native-only rows write EA globals the worker must not touch; the 44 unknown rows need the init-index resolution (§3) or a stop.

## 5. Reproduce and limits

```
python tools/binder-bodies.py --hashes regs.txt
node tools/binder-bodies.ts --asm game-copy/assemblies --index game-copy/analysis/callgraph/index.json --hashes regs.txt --out facts.json
python tools/binder-bodies.py --facts facts.json
python tools/binder-bodies.py --self-check
node tools/binder-bodies.ts --self-check --asm game-copy/assemblies
```

The Python tool needs capstone 5 and a copy of the Steam `TS3W.exe` (`game-copy/bin/TS3W.exe`, or `--exe`, or `TS3W_EXE`); the full run takes about a minute. Its self-check runs a classifier fixture always and, when the exe is there, checks 4 known bodies (`ElapsedTimeInMinutes`, `Curve.Fx`, `PriorityQueue.Add`, `SimClock.get_CurrentTicks`).

Limits:

- Static, one build (1.67 Steam). No body was run.
- Calls through unresolved pointers are opaque; the row lists them as handoffs and drops to medium confidence. A medium reader may write through such a call.
- The analysis budget is 200,000 instructions per glue function and a quarter of that elsewhere; 40 rows hit it in a GC or allocator callee (`trunc:` handoffs). Those callees are runtime code.
- Merged paths can put an argument atom on a pointer it never was; such stores show as unknown (`arg0 (scalar) used as a pointer`), never as a lower class.
- Only the first path to each store is kept for `unknown_hint`.
- The worker reach labels inherit docs/215's limits (static closures, overrides by class hierarchy unless `--typed` is given).
