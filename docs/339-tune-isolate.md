# 339. Tune isolate: one broken script mod no longer stops tuning for every mod after it

Written 2026-11-23 (branch `bugfix/tune-assemblies-isolate-1123`, build `tuneiso1`). Static analysis + build + selftests;
no game run. Queue `tools/harness/queues/tuneisolate-correct-1124.json` (NOT RUN). Ledger §606.

## Summary

EA bug [verified, decompiled Sims3GameplaySystems `Sims3.Gameplay.Tuning.TuningControl.TuneAssemblies` and SimIFace
`Sims3.SimIFace.PersistStatic.GameplayInitialize`, IL read with `tools/ilpatch-convert.ts --dump`]: both walk every loaded
script assembly that carries `[Tunable]` / `[PersistableStatic]`, call `GetTypes()`, and when it throws (a mod assembly that
references one that is not loaded, e.g. an add-on installed without its base mod) they throw `ApplicationException` from the
catch block. That ends the loop. `Initializers.Action.DoInit` swallows the exception [verified], so the game starts normally,
but every script mod later in the assembly list gets no tuning XML read and, since tuning is what touches each mod's
`Instantiator.kInstantiator`, no static constructor: the mod never starts. TuningControl also skips its tuning-cache save and
its `Simulator.OnAssembliesLoaded` subscription.

Repro (compat-overlap-1122-rerun5, job `ip-overlap-bisect-e-icarus`, 2026-11-23) [verified from the job's logs]:
`icarusallsorts.CookWithAnyIngredient_WoodFireOven.package` (assembly `icarusallsorts.WOBakeWithAnyIngredient`) without
`icarusallsorts.CookWithAnyIngredient.package`. ts3agent never started (cycle.ts: "agent answering: not reached within 300 s").

Fix: `ts3native [IlPatch] ilp_tuneisolate` (default 0, needs `Enabled=1`): two docs/506 IL specs replace the `newobj
ApplicationException; throw` pair in each catch with `pop; leave <loop increment>`, so a failing assembly is skipped and the
loop goes on. Everything else in both methods is EA's own code.

## 1. Why the fix is native, and why it is early enough

- ts3fixes cannot fix it itself: its own code starts from inside `TuneAssemblies` (the tuning touch runs our static
  constructor), so it is either already past the bad assembly or never reached; and a method that is running is compiled, so
  `Fixes.SwapMethod` would refuse it.
- The assembly order is the load order of the S3SA resources, not the folder order (in rerun5 ts3fixes came before the add-on,
  ts3agent after it [inferred from the logs]). Another install can put ts3fixes after it.
- ts3native's `[IlPatch]` detours Mono's IL transform (`0xE643B0`, docs/526). The asi is loaded before Mono starts; both
  methods are transformed the first time `StartupState` runs the `GameplayInitializers`, so the patched body is the one that
  runs, whatever the assembly order [inferred: same path as the b89 `Fixup` spec, verified live in docs/526 §5].
- Exact-hash rule as every IL spec: EA 1.67 and simler90 UPDATE242 bodies are known (`TuneAssemblies` 321 -> 345 bytes,
  `GameplayInitialize` 559 -> 607 bytes); any other body is left alone and logged. EA 1.69 (EA app edition) rows added
  2026-11-23 (§7).
- The relocator refused every branch that leaves its edit's exception region (docs/506 I-506-3). A `leave` may exit regions
  by ECMA-335, never enter one, so `ilpatch.cpp` now allows a `leave` whose target lies in no region the edit is not in
  (`IlpLeaveOut`; selftest: leave out of a try allowed, leave into a try refused). Other branches keep the old rule.
- The 1-argument `TuneAssemblies(Assembly[])` overload shares the method name (the native match is by name): it has another
  body and is logged "left alone" if it ever runs (`OnAssembliesLoaded`); it calls the patched 2-argument overload.

## 2. Logging and the hardening (ts3fixes `TuneIsolate.cs`, ts3agent loader)

- At app startup ts3fixes calls `GetTypes()` on every non-EA assembly that carries either attribute and logs one line per
  failure: `ts3fixes: tuning: broken script mod <assembly>: GetTypes failed, missing assembly <names>` (the references no
  loaded assembly provides; else the first loader exception), with `; skipped (ilp_tuneisolate), ...` or `; EA's tuning stops
  here ...`. Log only, always on (it needs ts3fixes to start: with the fix on it always does).
- Late init (backstop for a core build the IL patch does not know), only with `ilp_tuneisolate` armed (UserDiag op 49,
  `docs/registry.md`): at app startup ts3fixes runs `Ts3Agent.Instantiator`'s static constructor when its `ts3agent.started`
  marker is missing, and ts3agent runs `Ts3Fixes.Instantiator`'s when `ts3fixes.applied` is missing, then ts3fixes'
  startup scan; each logs `tuning: late init of ... (tuning loop aborted?)`. A late ts3fixes has C# default settings
  (kInstantiator false: tuning-gated patches go in at the first world load, as before).
- Not done: re-running EA's tuning for the mods after the broken one when the patch is not applied (would need
  `TuningControl`'s private methods by reflection; the IL patch covers the known builds).

## 3. Why ts3fixes logged only 9 lines in the repro

ts3fixes was reached (its `Fixes.Apply` patched the UI task hooks, whose `play: ui tasks` lines are the 9), so it was tuned
in full: `TuneAssemblies` tunes one assembly at a time, and the loop stopped after ts3fixes, at the add-on [inferred from the
log; the earlier "partly un-tuned" guess is withdrawn]. The other ~318 normal lines are written at the first world load
(`DiagWorldLoad`: version, settings, world size, script assemblies, and the world-load hooks). No world loaded because the
harness loads the save through ts3agent, which EA's loop never reached.

## 4. compat-scan

`tools/compat-scan.ts` reads each mod assembly's AssemblyRef table (`managed-callgraph.ts assemblyRefs`) and adds a note when
a reference is provided by no scanned package and no game assembly (mscorlib, System, System.Xml, ts3fixes and ts3agent count
as present): `missing assembly: <asm> references <names>, which no installed package provides (base mod not installed?)`.
On the e-icarus set it names `icarusallsorts.CookWithAnyIngredient` [verified, run 2026-11-23].

## 5. Test mod sets (game-copy, not in git)

`compat-overlap-1122` and its bisect split `compat-overlap-1122-a-other8` now carry the icarus base package
(`icarusallsorts.CookWithAnyIngredient.package`, sha256 7adf0a1607bd, from game-copy/compat-mods) next to the add-on; the
set notes say so. `compat-overlap-1122-e-icarus` keeps the add-on alone on purpose: it is the repro set.

## 6. Queue `tuneisolate-correct-1124` (NOT RUN, Bin state apex)

| Job | Set | Switch | Expect |
|---|---|---|---|
| `ip-tuneiso-off` | e-icarus | off | EXPECTED FAIL: agent silent (the bug) |
| `ip-tuneiso-on` | e-icarus | on | both specs applied, broken-mod line naming the missing base, world-load `script assemblies` line, no late init, 0 script errors |
| `bp-tuneiso-normal` | normal harness mods, BP 10 min | on | both specs applied, no broken-mod or late-init line, 0 script errors |

Release: EA-bug / rare-fix rule: `Enabled=1` + `ilp_tuneisolate=1` in the release ini once `ip-tuneiso-on` and
`bp-tuneiso-normal` pass (Rodney's call; `Enabled=1` arms only the switches that are 1).

## 7. EA app 1.69 rows (2026-11-23, branch `feature/ea169-tuneisolate-1123`)

`tools/ilpatch-convert.ts` now loads a third build `ea-1.69` from `game-copy/assemblies-ea169` when present, and `--dump`
takes `--build <label>`. `--fill` on both specs added the EA rows; `--emit-native` regenerated `ilpatch-specs.h` (3 builds each).

| Spec | EA 1.69 body | sha256 | patched sha256 | Edit |
|---|---|---|---|---|
| tune-isolate-tuning | 321 bytes | `9fd23f1be5f6fb70…` | `f37e9ffc7491284e…` | 0x56 `newobj; throw` -> `pop; leave @0x12F` |
| tune-isolate-persist | 559 bytes | `fbeca10178d68295…` | `a4ade19eaf105d71…` | 0x132 `newobj; throw` -> `pop; leave @0x19E` |

- [verified, `--dump --build ea-1.67` vs `--build ea-1.69`, decoded text identical] Both methods decode to the same
  instructions, offsets, member names and exception clauses on 1.67 and 1.69 (TuneAssemblies: catch 0x34+40 with the
  `newobj ApplicationException; throw` at 0x56/0x5B as its last instructions, finally 0xD4+11; GameplayInitialize: catch
  0x120+24, `newobj; throw` at 0x132/0x137). The raw bytes differ (metadata tokens), hence the new hashes. The patch site
  has the same stack (the message string on it, popped) and the `leave` targets the same loop-increment offsets, outside
  every clause, so `pop; leave` is valid on EA exactly as on Steam. The spec uses no tokens, so no per-build token rows.
- [verified] `ea-ini.ts`: `[IlPatch]` is kept on EA (no unresolved address row); `Enabled=0` / `ilp_tuneisolate=0` there as on Steam.
- Selftests: `ilpatch-convert --self-check` now re-applies every spec to each build whose assemblies are local and compares
  the body hash and patched hash (57 spec/build bodies; skipped without game-copy, as in CI); the native `TestIlPatch`
  asserts both tune-isolate specs carry an `ea-1.69` row.
- Queue `tuneisolate-correct-1124`: the applied-line regexes now accept `ea-1.69` (`ea-1\.6[79]`). Running it on EA needs an
  EA build of the pair (`/DTS3A_TABLE` asi, EA ini), the e-icarus mod set and the EA Baseline saves on the EA laptop (§5 set is
  not there yet). NOT RUN on EA.

