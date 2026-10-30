# 207. Compile candidates: never-yielding managed time

Written 2026-10-30. Static, from existing runs: no build, no game run, no queue. Question (Rodney): how much sim-thread interpreter time is spent in managed methods that can never pause a script task, directly or through any callee? That is the ceiling for an ahead-of-time or at-load compiler of non-yielding methods (docs/29 option d). Tool: `tools/compile-candidates.ts` (`--self-check`). Data: `docs/data/compile-candidates.csv`.

Tags: **[measured]** = counted from existing managed samples; **[verified]** = from the call-graph index of the Steam 1.67 script assemblies (docs/78); **[inferred]** = reasoning, not yet measured.

## 1. Method

- **Samples.** Managed sampler stacks (`probe-out/profile/<tag>-managed.jsonl`, leaf first) of the v0.2.x release builds, two 10-minute runs per world pooled: `sim-budget-v024-2026-10-29-<w>-simbudget-v024` (docs/160 queue) and `release-validate-2026-10-28-<w>-rel-play`. BP 2,050 samples, IP 1,772, SV 1,771.
- **Interpreter self time.** A sample counts when its leaf is a managed method. Leaves that are internal calls (native, the docs/160 "ICs" bucket) and `ScriptCore.Route.*` leaves (route sync plans) are left out, as in docs/160. Counted: BP 780, IP 606, SV 668. The counted share of all samples (BP 38%) matches the docs/160 split of the BP sim-budget run (329 Mono of 886, 37%) [measured].
- **Yield closure.** Every method of the docs/78 index gets one class [verified]:
  - **Y** (can yield): the method yields itself (`Simulator.Sleep`/`Yield*`, `SACS_AcquireDriverImpl`, `StateMachineClient.RequestState`, the docs/78 `YIELD` rules), or any callee can. Virtual, interface and constrained calls count every game override (class hierarchy, as docs/127 `virtualTargets`).
  - **D**: no yield except possibly through a delegate `Invoke`, whose target is unknown.
  - **N**: never yields.
  - **IC**: native binding.
  - **?**: the frame is not in the index (mods, our own ts3fixes hooks).
  A sample frame name has no parameter list, so it maps to every overload, and the worst class wins.
- **ms/s.** Shares are applied to docs/160's v0.2.4 "Mono script" bucket: BP 334.3, IP 335.7, SV 358.0 ms/s (`probe-out/queue/simbudget-v024-report.txt`). At speed-up s, the gain is share × Mono × (1 − 1/s) [inferred].
- **Region root.** For each N or D leaf, the tool walks outward while the frames stay N or D. The outermost such frame is where a compiled region would be entered (CSV rows of kind `root`).

## 2. Result per world

| World | Managed-leaf samples | N | D | Y | ? (mods) | N+D ms/s | Gain at 2x | Gain at 3x |
|---|---|---|---|---|---|---|---|---|
| BP | 780 | 71.4% | 2.4% | 20.0% | 6.2% | 246.9 | 123 | 165 |
| IP | 606 | 72.3% | 3.3% | 20.3% | 4.1% | 253.7 | 127 | 169 |
| SV | 668 | 71.9% | 3.3% | 19.2% | 5.7% | 269.0 | 134 | 179 |

The shares are [measured]. The ms/s values are shares times the docs/160 bucket. The gains are [inferred] upper bounds (see §5). Counting N only, without the D share, the gain is BP 119 / 159, IP 121 / 162 and SV 129 / 172 ms/s at 2x / 3x.

About **72% of interpreter self time is in methods that can never yield**, the same in all three worlds [measured]. The remaining ~20% is self time inside yielding methods: the simulate loops and interaction bodies. Its share is small because those methods mostly call out and spend little time in their own bodies.

## 3. Top never-yielding methods (mean ms/s over the three worlds, self time)

| # | Method | Assembly | IL bytes | ms/s | IL features |
|---|---|---|---|---|---|
| 1 | `Dictionary`2.GetPrev` | mscorlib | 106 | 12.9 | callvirt, newobj, box, throw, generic |
| 2 | `Enum.Equals` | mscorlib | 8 | 9.8 | call (reflection-backed, see §5) |
| 3 | `RuntimeTypeHandle.get_Value` | mscorlib | 7 | 7.4 | none |
| 4 | `Enumerator.MoveNext` (all collection enumerators) | mscorlib + game | 1,603 total | 5.7 | generic, box, switch |
| 5 | `Enumerator.get_Current` | mscorlib + game | 937 total | 5.5 | generic |
| 6 | `Array.Resize` | mscorlib | 76 | 4.8 | newarr, generic |
| 7 | `Dictionary`2.TryGetValue` | mscorlib | 46 | 4.6 | generic |
| 8 | `GameObject.GetComponent` | Sims3GameplaySystems | 88 | 4.5 | callvirt, unbox, cast, generic |
| 9 | `InteractionObjectPair.Equals` | Sims3GameplaySystems | 48 | 3.7 | callvirt, cast |
| 10 | `Dictionary`2.GetSlot` | mscorlib | 29 | 3.5 | generic |
| 11 | `Array.Copy` | mscorlib | 755 (overloads) | 3.1 | try, throw |
| 12 | `CASUtils.CASAGSAvailabilityFlagsFromCASAgeGenderFlags` | SimIFace | 977 | 3.0 | none (bit tests) |
| 13 | `GameObject.GetObject` | Sims3GameplaySystems | 54 | 3.0 | unbox, cast, generic |
| 14 | `Type.GetTypeFromHandle` | mscorlib | 13 | 2.8 | call |
| 15 | `DefaultComparer.Equals` | mscorlib | 38 | 2.8 | box, generic |
| 16 | `SimClock.ElapsedTimeInMinutes` | Sims3GameplaySystems | 17 | 2.7 | call |
| 17 | `List`1.GetEnumerator` | mscorlib | 7 | 2.3 | newobj, generic |
| 18 | `GameObject.get_Parent` | Sims3GameplaySystems | 19 | 2.2 | call |
| 19 | `IEquatableOfTEqualityComparer`1.GetHashCode` | mscorlib | 14 | 2.0 | generic |
| 20 | `List`1.Add` | mscorlib | 51 | 1.8 | generic |
| 21 | `List`1.get_Item` | mscorlib | 33 | 1.8 | throw, generic |
| 22 | `ScriptObject.get_ObjectId` | Sims3GameplaySystems | 23 | 1.7 | callvirt, ldsfld |
| 23 | `List`1.GrowIfNeeded` | mscorlib | 47 | 1.6 | generic |
| 24 | `UInt64.GetHashCode` | mscorlib | 14 | 1.5 | none |
| 25 | `ObjectGuid.op_Inequality` | SimIFace | 20 | 1.5 | none |
| 26 | `GameObject.get_LotCurrent` | Sims3GameplaySystems | 55 | 1.5 | callvirt, cast |
| 27 | `Enum.GetHashCode` | mscorlib | 14 | 1.4 | callvirt |
| 28 | `Members.GetActorsOfType` | Sims3GameplaySystems | 85 | 1.3 | try, newobj |
| 29 | `Array.IndexOf` | mscorlib | 320 (overloads) | 1.3 | generic, throw |
| 30 | `Slots.GetParentId` | SimIFace | 12 | 1.2 | callvirt, ldsfld |

IL sizes and features come from the IL bodies [verified]. The ms/s values carry ±20-40% Poisson error at 10-80 hits per row [measured]. Call counts are not available, because the sampler records stacks, not calls. Mint superinstructions (docs/130, `0x1AB`-`0x1C0`) are an interpreter-time fusion and do not show in IL.

**Profile of the candidates.** The top 30 are almost all tiny (median ~40 IL bytes). Two thirds are BCL generics: collections, comparers and enum/type helpers. The rest are game accessors. The **region roots** are small too: `Household.get_AllActors`, `Dictionary.TryGetValue`, `GameObject.get_LotCurrent`, `RuntimeTypeHandle.get_Value`, and the native event delegate `OnNativeEventDelegate.Invoke`. Each region is one accessor plus the BCL calls under it, entered straight from yielding interpreted code.

## 4. What stays interpreted (top Y, self time, mean ms/s)

| Method | IL bytes | ms/s | First yield path |
|---|---|---|---|
| `SimUpdate.Simulate` | 1,817 | 9.2 | `Simulator.Sleep`, 1 hop |
| `Lot.Simulate` | 214 | 5.3 | `Sleep`, 1 hop |
| `Sim.DoInteraction` | 737 | 2.9 | `Sleep`, 1 hop |
| `Motive.UpdateMotiveWithDecay` | 5,132 | 2.1 | `RequestState`, 8 hops |
| `Motive.UpdateMotiveBuffs` | 1,566 | 2.0 | `Sleep`, 6 hops |
| `InteractionDefinitionUtilities.CommonTests` | 926 | 1.8 | through a game override |
| `Sim.WaitForExitReason` | 69 | 1.6 | `Sleep`, 1 hop |
| `Lot.UpdateReactions` | 209 | 1.6 | `RequestState`, 10 hops |
| `InteractionDefinition`3.Test` | 518 | 1.4 | `RequestState` |
| `IdleManager.DistressIdle` | 246 | 1.4 | `RequestState`, 3 hops |

Several of these are Y only through deep or virtual paths: the motive updates, `CommonTests` and `Test`. With per-call-site receiver typing (docs/127), or a guard that falls back to the interpreter when a yield is reached, some of them could join the compilable set [inferred]. That is worth up to the ~20% Y share, mostly spread thin.

## 5. Why the gain is a ceiling, and blind spots

- **Small methods.** The compiled set is dominated by 7-100 byte methods. docs/130 puts ~43% of interpreter self time into call/return. A compiler that compiles only leaves, while callers stay interpreted, keeps one interpreted-to-compiled transition per call, so 2-3x on these bodies is optimistic. A realistic first estimate is the 2x row or below, unless whole regions, inlining or a compiled-to-compiled calling convention cut the transitions [inferred].
- **Runtime helpers inside self time.** `Enum.Equals`, `RuntimeTypeHandle.get_Value`, `Type.GetTypeFromHandle`, `box` and `newobj` call into the Mono runtime (type lookups, boxing, allocation). Compiled code still calls those helpers, so part of their self time does not shrink [inferred]. `Enum.Equals` plus the type-handle helpers alone are ~20 ms/s, and a cheaper option for them is a fast path in the runtime (docs/139) or a script change that avoids boxing an enum.
- **Over-approximation.** Every game override of a virtual counts as a possible yield (CHA), so N is a lower bound for "truly never yields". Delegate invokes are put in D (2-3%). BCL virtual targets are not expanded (docs/127). A game type that overrides a BCL virtual and yields, such as an iterator `MoveNext`, would be missed. None is known [inferred].
- **Name matching.** Frames have no parameter list and print nested types without their outer type, so `Enumerator.MoveNext` merges every collection enumerator. The worst class across overloads wins.
- **Mods.** 4-6% of self time is in mod or ts3fixes frames that are not in the index (NRaas ErrorTrap, our hooks): class `?`, left out of N. The index is the game-only build of 2026-10-16. Mod overrides of game virtuals are not in the yield closure (docs/128).
- **Sampler.** About 1,800-2,000 samples per world, from two runs of one release family. The sampler sees the managed stack only, so the leaf is the innermost managed frame and time in runtime helpers below it is booked to that frame. No call counts.

## 6. Answer

About **72% of sim-thread interpreter self time (~245-270 ms/s of the ~335-358 ms/s Mono bucket)** is in methods that can never yield [measured share, docs/160 scale]. Compiling all of it would save **~120-135 ms/s at 2x and ~165-180 ms/s at 3x** [inferred upper bound]. The time is spread over many tiny BCL generics and accessors, so the reachable part depends on removing per-call transitions, not just on speeding up method bodies. Next step if pursued: measure the interpreted-to-compiled transition cost with a monohost prototype that compiles the top 30 (docs/139 bench), before any in-game build.

## 7. P0 prototype: a method compiler in monohost (2026-10-30)

Rodney approved P0: a monohost-only compiler for the hottest never-yielding methods, to measure the real speed-up and the cost of switching between the interpreter and compiled code before anything goes near the game. Everything below ran offline in monohost (docs/119, docs/139). No game run, no queue, nothing in the game folder. Code: `native/monohost/monohost-jit.cpp` (included by `monohost.cpp`), workload `HostTest.JitBench` in `native/monohost/HostTest.cs`.

Tags as above, plus **[measured offline]** = monohost run on this machine (machine in normal use, pinned core, best of N).

### 7.1 How it works [verified: host runs]

| Piece | What it does |
|---|---|
| Hook | transform `0xE643B0(RuntimeMethod*, ThreadContext*)` is wrapped (prologue `55 8B EC 83 EC 78` checked, trampoline). After EA's transform and peephole, a method whose name ends in a listed `Class.Method` is compiled. Generic instantiations are separate `RuntimeMethod`s, so each one is compiled on its own. |
| Entry | Every instruction start of a compiled method gets its opcode word replaced by `0x1A9`, an opcode EA never emits (its jump-table entry was the "Unimplemented opcode" handler). The table entry for `0x1A9` points to `JitEnter`. The original words are kept in a shadow array. Operands, ip values, branch offsets and clause ranges are not touched. |
| `JitEnter` | Looks up `edi` (the ip) in an open-addressing table (65,536 x 16-byte entries, ~10 instructions on a hit), loads `ebx` = argument area (`MonoInvocation+0x10`) and `edi` = locals (`[ebp-0x14]`), and jumps to that instruction's native code. So entry works from a call, from a return into the middle of a compiled method, and after any exit. |
| Native code | Runs inside the interpreter's own C frame. `esi` stays the eval-stack pointer and every argument, local and stack slot stays in the interpreter's frame memory, written with the same widths as EA's handlers (a 4-byte load writes 4 bytes of the 8-byte slot, as EA does). The conservative GC sees exactly the memory it sees today (docs/57): no managed reference lives only in a register across anything that can collect. |
| Templates | 150 base opcodes have templates: argument, local and field loads and stores of every integer width, `ldthis`, address-of, constants, `dup`/`pop`, all short and long integer branches, `ldlen`, integer `ldelem`/`ldind`/`stind`, integer ALU, compares and widening conversions. EA's 22 superinstructions are compiled as their base sequences (the inner opcode words are still in the code, docs/139 §4.1). |
| Exits | Everything else (calls, `callvirt`, `newobj`, `ret`, boxing, casts, statics, strings, float, value-type moves, switch, throw) leaves through an exit: `edi` = ip, `[ebp-4]` = ip, `eax` = `edx` = the base opcode, then `jmp` to EA's own handler. The handler ends in the dispatch head, which reads the next word: `0x1A9` again, so control comes back to native code. Every check that can throw (null receiver, array bounds) is tested first and, on failure, exits to EA's handler before anything changed; EA then throws with the same ip, frame and stack (P0 bails to the interpreter on every throw path). |
| Switches | `MONOHOST_JIT=off\|dump\|on\|verify`, `MONOHOST_JITLIST=<names>` (per-method enable list; default 38 names: the §3 methods that run offline, their BCL callees and three bench methods), `MONOHOST_JITEXITALL=1` (every instruction an exit: switch-cost measurement), `MONOHOST_JITCOUNT=1` (re-entries and exits counted), `MONOHOST_JITVERIFY=bench` (verify only the bench workloads), `MONOHOST_JITREPS=<n>`. The bench toggles compiled code on and off in one process by restoring or re-patching the opcode words, so "interpreted" is EA's untouched code. |
### 7.2 Verify mode

`MONOHOST_JIT=verify` runs every verifiable call twice from the same state [verified: host runs]:

1. At a fresh call (`JitEnter` at word 0) it snapshots the frame's arena (arguments, locals, eval stack), the free arena space after it (where callee frames go, so dead bytes that `ret` copies are the same in both passes), the stack arguments, and the **whole object graph reachable from the receiver and the arguments** (Boehm blocks via `GC_base` `0xE4DC30` and the block header, plus 32 bytes behind a byref that points into a caller frame of the same activation). Limit 4,096 objects / 1 MB; a larger graph runs compiled only and is counted.
2. Pass 1 runs the compiled code. Compiled callees run compiled.
3. At the compiled `ret` it records the return value and all snapshot bytes, keeps copies of objects the pass allocated, restores the snapshot and the interpreter frame words `[ebp-8]..[ebp-0x1C]`, and restarts at word 0 in EA's own handlers.
4. Pass 2 runs EA's interpreter for the method **and every compiled callee** (one verification in flight per activation), so it is a pure-interpreter reference for the whole call tree.
5. At pass 2's `ret` it compares the return value and every snapshot byte. Two words that differ only because each pass allocated its own object are equal when the objects are equal word by word (recursively, 3 levels). Dead memory is restored but not compared: eval-stack slots at or above the `ret`'s stack pointer (EA's superinstructions skip temporary stores their base sequences make) and the callee-frame space. A difference gets one retry from the same entry state (a one-time effect of the first pass, such as a class initialiser or a lazily filled cache outside the graph, is then in place for both passes); a real compiler difference repeats.
6. Then the bench cases and `MintBench` run again with EA's untouched code (opcode words restored) and their checksums must match.

Two harness details were needed to get there, both recorded because they are traps for any later verifier: a shallow snapshot is not enough (Mono 1.2.3's `Dictionary` relinks its `Slot` objects on resize, three references away from the dictionary, so restoring only the first level corrupted the dictionary for pass 2), and `GC_malloc_atomic` blocks keep old bytes after the value, so verify mode redirects `GC_malloc_atomic` `0xE4BDC0` to `GC_malloc` `0xE4BE30` while no M5 worker runs (the redirect is off while M5 installs its P3 detours on the same entry points).

**Result [verified: host run 2026-10-30]** over the whole selftest (M1.1-M1.5 including the 9,840-type `.cctor` sweep, M2, M5 up to the [AutShadow] install check) plus the bench (21 cases at n = 1,024) and `MintBench(300)`:

| Item | Count |
|---|---|
| Methods compiled | 236 instantiations of 38 listed names |
| Verified calls (outermost compiled frame, both passes compared) | **1,992,404** |
| Compiled calls inside a verified caller (compared as part of the caller's tree) | 6,859,040 |
| **Mismatches** | **0** |
| Retried after a first-attempt difference | 1 (then equal: a one-time effect in the `.cctor` sweep) |
| Ran compiled only, graph over the snapshot limit | 2 |
| Abandoned by an exception / methods with clauses | 0 / 0 |
| Bench and `MintBench` checksums vs EA's untouched code | 21 of 21 equal, `MintBench` equal |
| Selftest checks | 32 passed, the same set as with the compiler off; the one failure (`AS-install`: [AutShadow] contract version) fails identically with `MONOHOST_JIT=off` and belongs to the [AutShadow] lane |

Not covered by verify [inferred]: writes to static fields of classes not reachable from the arguments (the graph walk does not visit static storage unless an object points at it), the M5 worker thread (compiled code runs there unverified; M5's own result checks pass), and the game's `Sims3Gameplay*` accessors, which need world objects and were not compiled offline.
### 7.3 Speed per method [measured offline]

`MONOHOST_JIT=on`, `HostTest.JitBench`: each case calls one candidate 200,000 times from an interpreted loop; best of 9 runs per side, compiled and interpreted interleaved in one process (opcode words patched and restored between runs), one pinned core, high priority, machine in normal use (other load ~9% of the CPU during the runs). The empty loop (10.8 ns per iteration) is subtracted. Times are per call and include the interpreted caller's call into the method, which compiled code does not change. Checksums equal in every case.

| # | Case (compiled methods on the path) | Interpreted | Compiled | Speed-up | Switches per call |
|---|---|---|---|---|---|
| 0 | `Dictionary<int,Shape>.TryGetValue` (TryGetValue, GetSlot, GetPrev, comparer) | 197.7 ns | 173.6 ns | 1.14x | 12.8 |
| 1 | same on `Dictionary<ulong,int>` with a 256-entry collision chain (GetPrev loop, `UInt64.Equals`) | 11,245 ns | 9,635 ns | 1.17x | 973 |
| 2 | `Dictionary<ObjectGuid,int>.TryGetValue` (`DefaultComparer`, boxing) | 302.0 ns | 316.0 ns | 0.96x | 30.5 |
| 3 | `List<T>` foreach, per element (`MoveNext`, `get_Current`) | 59.8 ns | 42.2 ns | 1.42x | 2.0 |
| 4 | `List<T>.get_Item` + `Count` | 43.5 ns | 40.2 ns | 1.08x | 2.0 |
| 5 | `List<int>.Add` with `GrowIfNeeded`, `Array.Resize` | 101.8 ns | 94.8 ns | 1.07x | 5.6 |
| 6 | boxed `Enum.Equals` + `Enum.GetHashCode` | 679.0 ns | 691.9 ns | 0.98x | 6.0 |
| 7 | `GetType() == typeof(...)` (`Type.GetTypeFromHandle`) | 198.4 ns | 170.4 ns | 1.16x | 4.0 |
| 8 | `Array.Resize<int>` | 493.1 ns | 485.7 ns | 1.02x | 6.9 |
| 9 | `Array.IndexOf<int>` over 64 elements | 1,956 ns | 1,662 ns | 1.18x | 136 |
| 10 | `ObjectGuid !=` | 53.8 ns | 60.9 ns | 0.88x | 4.0 |
| 11 | `CASUtils.CASAGSAvailabilityFlagsFromCASAgeGenderFlags` (977 IL bytes of bit tests) | 89.1 ns | 52.5 ns | **1.70x** | 3.1 |
| 12 | `Dictionary` foreach, per element | 220.7 ns | 234.8 ns | 0.94x | 8.1 |
| 13 | `ContainsKey` + `set_Item` | 437.9 ns | 358.4 ns | 1.22x | 27 |
| 14 | `JitId(x)`: trivial, compiled (`stinarg`, `ldarg`, `ret`) | 13.9 ns | 13.0 ns | 1.06x | 1.0 |
| 18 | `JitSum`: 64-element loop, no calls, per call | 1,056 ns | 307 ns | **3.4x** | 0.02 |
| - | `MintBench(3000)` (docs/139 workload: List/Dictionary generics, enumerators, boxed `Enum.Equals`, `GetType`) | 349.7 ms | 286.6 ms | **-18.1%** | - |

"Switches per call" = interpreter-to-compiled re-entries (`MONOHOST_JITCOUNT=1`); every one follows an exit (call, `callvirt`, `ret`, `newobj`, box, unsupported opcode). Twin cases with the same bodies left interpreted (`JitPlain`, `JitOutPlain`, `JitSumPlain`) stayed within ±4%, which is the noise of this setup.

What the numbers say:

- **Straight-line and loop bodies compile well**: 3.4x on a call-free loop, 1.7x on the bit-test method, 1.4x on the `List` enumerator. A template compiler without register allocation already removes most of the ~1.9 ns per mint instruction (docs/139 §1) where the code stays in compiled code.
- **The candidates are call-bound**: the hot BCL methods are a few instructions between calls (`Enum.Equals` is 4 mint instructions: argument move, `this`, `call`, `ret`). Every call, `callvirt` and `ret` still takes EA's call path (~190 instructions per call, docs/139 §5) plus one switch, so 1.0-1.2x is all these methods get; the ones that are almost only calls and boxing (`ObjectGuid !=`, boxed enums, `DefaultComparer`, `Dictionary` enumeration) get 0.88-0.98x and must stay interpreted.
- `MintBench` gains 18% because its hot loop is dominated by the enumerator and dictionary bodies with few calls per instruction; it is the optimistic end, not the game's mix.

### 7.4 Switch cost [measured offline]

- **Interpreter to compiled (entry)**: the interpreted call path is unchanged; `JitEnter` replaces the first instruction's dispatch with a hash lookup (~10 instructions) plus two frame loads. `JitId` (trivial, compiled) took 13.0 ns per call against 13.9 ns interpreted: the entry costs nothing measurable beyond the dispatch it replaces.
- **Compiled to interpreter and back (exit + re-entry)**: measured with `MONOHOST_JITEXITALL=1` (every instruction an exit) against the interpreter, divided by the counted switches: **~1.0-1.5 ns per round trip** in most cases (`JitId` 1.0, `JitSum` 1.42, `Array.IndexOf` 1.43, `CASUtils` 1.45, `List` foreach 1.39), up to ~3 ns in the call-heaviest cases (`TryGetValue` 3.0, `ContainsKey + set_Item` 3.4). That is ~0.5-1.5x the cost of one mint dispatch on top of the instruction itself. With every instruction an exit, `MintBench` runs 92% slower.
- So switching does not eat the body speed-up (methods with 2-6 switches per call still win where they have straight-line work), but it sets the floor: a method that is mostly calls cannot win while calls go through the interpreter.

### 7.5 Projection to the game [inferred]

Each top-30 method's self time (§3, mean of the three worlds) times (1 - 1/speed-up), with the speed-up of the bench case that runs it (the game accessors `GameObject.GetComponent`, `InteractionObjectPair.Equals`, `get_Parent`, `get_ObjectId`, `get_LotCurrent`, `GetObject`, `SimClock.ElapsedTimeInMinutes`, `GetActorsOfType`, `Slots.GetParentId` could not run offline and get the `List.get_Item` accessor ratio, 1.08x); a method that gets slower stays interpreted (0); `RuntimeTypeHandle.get_Value` and `Array.Copy` were not compiled (native body / clauses):

| Scope | Self time | Gain |
|---|---|---|
| Top 30 never-yielding methods (§3) | 103 ms/s | **~11 ms/s** (10.9%) |
| All never-yielding self time at the same ratio (§2: ~250 ms/s) | ~250 ms/s | **~27 ms/s** |
| Upper end: the whole never-yielding set behaving like `MintBench` (-18%) | ~250 ms/s | ~45 ms/s |

**Low ~11 ms/s, realistic ~20-27 ms/s, optimistic ~45 ms/s.** The realistic figure scales the top-30 ratio to a long tail of even smaller accessors, so it is more likely high than low. Not counted: the extra instructions a sampling profile books to a caller (a compiled callee's `ret` exit is booked to the callee here), GC or cache effects of 150+ KB more code, and the sim's own data-cache misses (the `callvirt` receiver stall, docs/139 §6), which no compiler removes.

### 7.6 Verdict: stop the body compiler; the call path is the lever

The P0 stop rule is met: the realistic projection (~20-27 ms/s) is under ~30 ms/s. The switch cost is not the problem (~1-1.5 ns per round trip, entry free); the problem is that the never-yielding time sits in tiny methods whose cost is EA's call and return path, which a compiler that keeps EA's frames (the GC and exception model need them, docs/57) still runs for every call. Recommendation:

1. **Do not take the compiler to the game as it is.** Keep `monohost-jit.cpp` as the measuring tool (verify mode included).
2. **Build docs/139 §5 (call/return fast path) next** if script stutter is worked on: it cuts the ~190-instruction call for interpreted and compiled code alike (~15-30 ms/s inferred there), and it is the part this prototype shows is missing.
3. **Revisit the compiler only on top of that**, with compiled-to-compiled calls (a compiled call site that builds the callee's frame directly, no exit and no `JitEnter`) and inlining of tiny callees with no exception path (`get_Item`, `GetSlot`, `List` enumerator members). Re-run this bench then: the same harness gives the answer in one session.
### 7.7 Address space and W^X

- **Code size** [measured offline]: the default list compiled 234 instantiations (37 listed names; every generic instantiation the selftest and bench created is its own method) into **~150 KB** of x86 (4,917 template instructions, 1,090 exits; ~650 bytes per method, ~24 bytes per mint instruction, exits ~20 bytes each). The ip table is 1 MB as built (65,536 x 16-byte entries; 6,007 used), the shadow of original words 12 KB.
- **In game** [inferred]: the game has more instantiations of the same names (every `Dictionary<K,V>` the scripts use), so a top-30 list would compile a few hundred to ~1,000 methods: ~0.2-0.7 MB of code. Sizing the ip table to the compiled set (8,192 entries = 128 KB) keeps the total under ~1 MB of the 32-bit address space, allocated once at load (no growth in play). That is small next to the docs/66 budget, but it is not free: it should come out of a reserved region made at startup, not from fragmenting the heap mid-session.
- **W^X**: the prototype allocates one `PAGE_EXECUTE_READWRITE` pool (4 MB reserved). In game: emit into `PAGE_READWRITE` pages, `VirtualProtect` to `PAGE_EXECUTE_READ` and `FlushInstructionCache` before publishing the ip-table entries and patching the opcode words (compile happens once per method right after transform, so the protect calls are a load-time cost). The mint code itself is in ordinary heap memory (written by EA's transform), so patching opcode words needs no protection change. The jump-table entry for `0x1A9` lives in the exe's `.rdata`-like table and is written once with `VirtualProtect`, as docs/139 does for its table writes.
### 7.8 The [AutShadow] write log (docs/206 option 2b) and compiled code

Another lane builds docs/206 option 2b: while an [AutShadow] worker job runs, sim-thread managed stores log (address, old value) on the first write, and worker-thread loads read the logged old value. It works by swapping mint's store and load handlers in the jump table. Compiled code does not go through those handlers, so a compiler past P0 must cooperate [inferred, design]:

- **Stores on the sim thread.** Every compiled store to managed memory (`stfld.*`, `stind.*`, `stelem.*` once templated, and any store through a byref) gets the barrier inline: `cmp byte [job_active], 0` / `jne slow`. The slow path calls the same log routine the swapped handler uses (first write only), then does the store. Stores to the frame (arguments, locals, eval stack) need nothing, since the worker never reads another thread's frame. Cost while no job runs: one compare and one not-taken branch per heap store, about 0.3-0.5 ns [inferred]; P0's hot methods do 0-3 heap stores per call, against 30-700 ns per call (§7.5).
- **Loads on the worker.** Simplest and safe: compiled code never runs on a worker thread. `JitEnter` already sees the thread (TLS or a per-thread flag); on a worker it dispatches the original opcode (the "interpreted" path of verify mode), so the worker runs EA's handlers, which the other lane swapped. A later step could emit worker-side load templates that check the log, but the worker gets the speed-up only then.
- **Switch flips.** The barrier is compiled in unconditionally (the flag test), so turning [AutShadow] on or off needs no recompile. If the write-log handlers are installed by swapping table entries, compiled exits still jump to whatever handler the table had at compile time (P0 caches `g_opHandler`); a P1 must read the live table entry at exit (one extra load) or re-link exits when the other lane swaps.
- **Effect on the offload's numbers.** Compiling the sim thread's never-yielding methods makes the sim's own scoring and lookups cheaper, so the share of sim time the worker moves off the thread shrinks by the same factor for compiled methods (the worker runs interpreted code). On the other side, the consume re-score at the cut (docs/124: top-N ads re-tested on the sim thread) gets cheaper too, and if the worker later runs compiled code, its jobs get shorter, which shortens the window in which the write log is active (fewer logged first writes). Net: the two add, but each one's measured gain is smaller with the other on; A/Bs must state which of the two was on.
