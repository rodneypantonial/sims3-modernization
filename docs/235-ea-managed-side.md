# 235. EA app 1.69.47: the managed side (assembly diff, hook targets, compile check)

Static work on the EA laptop, 2026-11-02 14:10-14:55 PHT. docs/232 §3 last bullet, §5 task 8. No game run, nothing written
under `C:\Program Files`, Mods untouched. Companion to docs/230 (edition), docs/231 (popup), docs/233 (Mods set).

## 1. Extraction [verified]
- 1.69 keeps the script assemblies where Steam does: `<EA>\Game\Bin\scripts.package` (3 S3SA), `gameplay.package` (4),
  `simcore.package` (3). `GameData\Shared\Packages\*` holds no S3SA; the EPnn folders have thumbnails only.
- `node tools/extract-assemblies.ts game-copy/assemblies-ea169 <those three>` (read-only; the extractor already takes any
  package paths, so no option was added). Output dir is under the ignored `game-copy/` (`git check-ignore` ok).
- S3SA headers (`tools/s3sa-stamp.py`): scripts/gameplay resources are header v2 with stamp `1.0.0.18` (Steam 1.67: v2,
  `0.0.0.32`); simcore (mscorlib, System, System.Xml) is header v1, no stamp, on both editions.

| assembly | bytes 1.67 / 1.69 | MVID 1.67 -> 1.69 | assembly version | methods | verdict |
|---|---|---|---|---|---|
| ScriptCore.dll | 430080 / same | ee57f83a... -> 5db130b8... | 1.0.5134.21446 -> 1.0.9048.31157 | 4625 | recompiled, 0 methods differ |
| SimIFace.dll | 618496 / same | c5300e3d... -> 1f69b4ba... | 1.0.0.50 | 5990 -> 5991 | +1 method: `Sims3Launcher.BusinessLogic+S3L_EntitledContentItem::.ctor()` |
| Sims3Metadata.dll | 143360 / same | 2eec397f... -> 1ae992d8... | 1.0.0.0 | 959 | recompiled, 0 differ |
| Sims3GameplayObjects.dll | 7626752 / same | b2cc44b0... -> ee64b579... | 1.0.0.0 | 55580 | 1 differs (`ChallengeDelegateFunctions::.cctor`) |
| Sims3GameplaySystems.dll | 13873152 / 13881344 | 36818893... -> 51ac3449... | 1.0.0.0 | 75877 | 8 differ (§2) |
| Sims3StoreObjects.dll | 1236992 / same | bd729220... -> dd745123... | 1.0.0.0 | 6732 | recompiled, 0 differ |
| UI.dll | 3014656 / same | 618382f8... -> c973f95b... | 1.0.0.0 | 14926 | 5 differ (§2) |
| mscorlib / System / System.Xml | same | same MVID, same sha256 | 2.0.0.0 | 8256 / 1340 / 5177 | byte-identical |

Full MVIDs and sha256 prefixes: the header lines of the `out/port/managed-ea169.csv` run (`tools/port-fingerprints.ts`
`assemblies()`). ScriptCore's build number 9048 = days after 2000-01-01 = 2024-10, the 1.69 recompile [inferred].

## 2. Method diff
Two tools, two answers; the token-normalized one is authoritative:
- `port-report`'s managed stage (`out/port/managed-ea169.csv`, 145715 rows): hash-changed 134144, size-changed 11458,
  removed 56, added 57, module-missing 0. Its hash covers raw IL with metadata tokens and its key is short type name +
  method + arity + order, so a recompile with new tokens and reordered overloads flags nearly everything; removed/added are
  all compiler names (`<>c__DisplayClassN`, `b__N`). Not usable for a 1.69 verdict [verified by the second tool].
- `tools/mod-method-diff.ts --asm game-copy/assemblies --pkg <EA scripts.package>=ea169 --pkg <EA gameplay.package>=ea169`
  (token-normalized IL, compiler names folded, docs/218): `out/port/managed-ea169-il.csv`, 14 methods differ + 1 added,
  164690 same. Differing: `ActiveCareer` `.cctor`/`.ctor()`/`.ctor(OccupationNames)`, `Sim::.ctor()`, `CASLogic::.cctor`,
  `Career::ShowUploadResumeTns` (+ its lambda, renamed), `TravelUtil::.cctor`, `ChallengeDelegateFunctions::.cctor`,
  `BuildController::.ctor(uint)`, `CASCharacter::.cctor`/`.ctor(uint)`, `OptionsDialog::.ctor()`, `SceneMgrWindow::.ctor(uint)`;
  every one "calls and stores unchanged: branches, constants or locals", same IL line count = constants or field order,
  not new logic [verified shape diff; the constants themselves not read].
- The 1.69 managed layer is 1.67 recompiled with constant changes in 14 constructors plus the launcher entitlement type.

## 3. Our hook targets [verified]
- ts3fixes: 156 `Patch(...)` calls, 126 distinct `typeof(T), "Method"` targets (the rest name NRaas/Overwatch types from
  variables, not game assemblies) + 64 `GetMethod("...")` reflection reads; ts3agent: reflection by name in its handlers.
- Intersection with the 14 differing / 1 added methods: none. Every targeted method has identical normalized IL in 1.69,
  none is missing. `mods/ts3fixes/src/AutShadowIlHashes.cs` is the exception: `AutShadowCore.IlHash` is not normalized,
  `node tools/autshadow-il-hashes.ts --set ea169=<7 DLLs> --check` reports 961 of 3570 G-M6 hashes new and 1 method
  missing (`InteractionInstance+<>c__DisplayClass1::.ctor()`, renamed), so on 1.69 `[AutShadow]` turns itself off every
  session until the file gets an `ea169` set.

## 4. Compile check [verified]
`TS3_REF_DIR=game-copy/assemblies-ea169 TS3_BUILD_DIR=build/ea169 node tools/setup/build-fixes.ts` and
`... node tools/harness/build-agent.ts` (new env overrides, default unchanged): both compile with csc without an error or
warning; `build/ea169/ts3fixes.package` (1284854 B) and `ts3agent.package` (454426 B), `build/` untouched, nothing installed.

## 5. What a 1.69 build needs
1. Nothing in the C# sources. The same source compiles against 1.67 and 1.69; the API surface we use is unchanged.
2. `AutShadowIlHashes.cs`: regenerate with `--set ea169=...` (adds 961 hashes) or `[AutShadow]` stays off on 1.69.
3. Release/CI: a second package pair built with `TS3_REF_DIR` = 1.69 is not needed for correctness (csc binds by name, not
   MVID); the 1.67-built packages should load on 1.69 as they are [inferred; a load on the EA laptop is the test].
4. Stamp: our packages are header v1 without a stamp (`tools/dbpf.ts` `s3saEncode` writes `head[0] = 1`), so they never
   trigger the docs/231 popup on either edition; no v2 stamp is needed. Open: whether 1.69's content scan treats a v1 core
   replacement differently from a v2 one stamped `1.0.0.18` (simler90 EAapp) [not tested].

## 6. Next steps
- EA laptop load with the current `build/ts3fixes.package` + `ts3agent.package` (verify run, docs/233 pass criteria), read
  the `ts3fixes:` world lines; then decide whether an `ea169` AutShadow hash set ships in v0.2.5 or after.
- Drop the `.managed.csv` stage from the 1.69 port checklist (docs/232 task 8): it over-reports on a recompile; use
  `mod-method-diff.ts --pkg` instead, or give port-fingerprints the ilShape hash (docs/23 idea, not planned).

## 7. AutShadow ea169 hash set (2026-11-02, branch ea169-port)
- `AutShadowIlHashes.Known` is already one sorted union over all checked builds (EA 1.67, simler90, ErrorTrap), so no
  per-build selection was added: `tools/autshadow-il-hashes.ts` now also hashes `game-copy/assemblies-ea169` when present
  ("EA app 1.69.47") and the file gains 960 hashes (to 5437 hashes in total). Steam hashes are untouched;
  a Steam load looks up the same values as before. On 1.69 the modules are not on the content accept list, so ClosureSweep
  checks each closure/hierarchy method against `Known` (the per-definition path of docs/218 §5).
- 960 of 3570 closure methods hash differently from Steam, with identical token names: the hash mixes the raw token of
  generic-instance references (TypeSpec/MethodSpec), which a recompile renumbers [verified: the name traces of all 3570 keys
  are equal between the builds except the one below].
- Guarded off: `InteractionInstance::CreateTooltipCallback(string)`. 1.69 renamed its closure class `<>c__DisplayClass1` to
  `<>c__DisplayClassb` (members `<...>b__0` -> `b__a`); the key `DisplayClass1::.ctor()` does not exist in 1.69 and the renamed
  class is not in `AutShadowLists.Methods`, so its body was never closure-checked. `GUARDED_OFF` in the tool withholds that
  one ea169 hash: the owners whose closure reaches it skip on 1.69 (counter 606), the rest run. Lift it after reading the
  renamed class bodies.
- Build: Steam default and `TS3_REF_DIR=game-copy/assemblies-ea169 TS3_BUILD_DIR=build/ea169` both compile (1293174 B
  `ts3fixes.package`, identical size). `tools/selftest/autshadow` PASS (incl. G-M6 e2e).
- EA laptop managed load test must check: world lines show no `G-M6 GUARD FAULT` (counter 605 = 0), `G-M6` miss lines only
  for `CreateTooltipCallback` (`autshadow_gm6_mismatch` = 1, one `G-M6 skips <owner>` group), `game modules not on the
  content accept list` appears (expected), `[AutShadow]` stays on, and `c[602]` hashed count ~3568 with load cost in c[604].

## 8. Live load test (EA laptop, 2026-11-02 17:21 PHT)
Managed side only: `ts3fixes.package` `a82ad09c0461` and `ts3agent.package` `dd7a7cd67f52` (both built with `TS3_REF_DIR=game-copy/assemblies-ea169 TS3_BUILD_DIR=build/ea169`, head d2879d3; replaced `ac2c3f8799af` / `1921c00c4c14`), script cache cleared, Bin ts3native pair at baseline (`c035abe5a3a6` / `8aa37522782a`, unchanged), `NRaas_ErrorTrap` stays in `Mods\_disabled-steam-1.67\`. Driver as run 7i (TS3_LAUNCH=ea TS3_EXE=Sims3Launcher.exe TS3_MACHINE=ea-laptop, 2 s memory sampler, 2600 MB guard, `cycle.ts --save Baseline-SV-Spring.sims3`, autopilot normal, window never focused). Logs: `probe-out/ea-235-0211.*` (main repo, ignored by git); counters read with the agent action `patches` (`.patches1.json`, `.patches2.json` at +11 min).
- **Result: loads and runs clean, but the G-M6 check was not exercised, so the §7 criteria for it are open.**
- [verified] ts3agent connects after about 90 s, "Mod Scripts Found" accepted, world loaded at 17:22:35 (`LiveModeState`, 37 households, Keaton). All harness actions answered: `autopilot`, `set_speed`, `save_game`, `game_state`, `patches`, `quit_game`.
- [verified] 10.7 min in the world, one `save_game {"name":"ea235-scratch"}` at 17:27:44 (done within 25 s, 39 households, `save_in_progress` false), then `quit_game` at 17:33:15, process gone in 5 s. No unrequested mode exit: the state stayed `LiveModeState` through all 20 samples (30 s apart) and at the end; the baseline native log is inert on EA (`SKIP all: host is not TS3W.exe`), so there are no `mode` lines to cite. No crash, no new `xcpt` file, `ScriptError*.xml` 1 before and after.
- [verified] Peak private memory 1850 MB (guard series, at 17:27:56, during the save); WS 1873-1944 MB, 146-147 threads; the 2600 MB guard never triggered.
- [verified] Counters: c[602] (`autshadow_gm6_methods`) = 0, c[603] = 0, c[605] (GUARD FAULT) = 0, c[606] (`autshadow_gm6_owners_skipped`) = 0, c[2022] content modules = 0. So no `G-M6 GUARD FAULT`, but also no `CreateTooltipCallback` skip group and no "not on the content accept list" line: **AutShadow did not run.** The `autshadow` switch reads `false` (`AutShadowWorldLoad` returns before `GuardSweep` when `ShortcutOn("autshadow")` is off), and even switched on it needs the ts3native internal calls (`Version`, `ImageSha`, `Config`) that the baseline native pair does not register on EA. The hashed count ~3568 and the miss group therefore need a ts3native build with AutShadow on EA (MonoThread) plus the switch on; not testable with the baseline pair.
- ts3fixes load log (`ts3fixes.applied`, 164 lines; the `ts3fixes:` world lines go to `ts3native.log`, which is empty here, so this is the only log). Every Patch line says `ok` or `already compiled` except these, the full WARN/SKIP list:
  - `AutShadow: LocalizeString compiled before ts3fixes loaded (another mod or the game localized first)`
  - `AutShadow: not patched: Localization.LocalizeString(String,Object[]), Localization.LocalizeString(Boolean,String,Object[]); Modes 1/2 off, Mode 0 (count only) still runs`
  - `CachedReflection: not patched, off`
  - `TooltipManager.Simulate at patch time: not started`
  - `tuned: tuning-gated patches at app startup`
  - `errortrapspread: ErrorTrap not loaded, off` (expected: ErrorTrap disabled)
- Restored: Mods listing identical to the pre-run sha listing (174 files), `scriptCache.package` back to `db69b1b6a862`, `ea235-scratch.sims3` deleted, robocopy `/MIR /L` against `game-copy/saves-pre-7f`: 0 differences, Bin pair `c035abe5a3a6` / `8aa37522782a`. The temporary worktree junction `game-copy/assemblies-ea169` was removed (`rmdir`); the main repo copy is intact.
- Open: (1) the "LocalizeString compiled before" line also appears if another mod localizes first; whether it appears on Steam with the same Mods was not checked; (2) §5.3 stays inferred only for load: the 1.69-built packages load and run, the 1.67-built ones were not compared in this run.
- **Run 7j (docs/232, 2026-11-02 17:40 PHT): G-M6 still not exercised; the combined run crashed at game start.** Native `05c1f1ecbf2c` with the 7i ini plus `[RouteSnap] Count=1`, `[MonoThread] Enabled=1`, `[AutShadow] Enabled=1` (ini `cb9c88e481fa`) and the managed packages of this section. MonoThread and AutShadow init lines were `OK` (`mono thread ... hooked (Mode=0 ...)`, `autonomy shadow: ... 44 of 45 descriptor ICs ... change tracking from routesnap's World_FireEvent detour`), then about 20 s after the launch, before the agent answered and so before the `autshadow` switch could be set and before any world load: `exception C0000005 at 0x00E824B2 (info 00000000 00000000)`, a null read in the game's Mono code. c[602], c[605], c[606] were not read, so the §7 criteria are still open. Both changes were on at once, so the cause is not separated.
- **Run 7j2 (docs/232, 2026-11-02 17:52 PHT): G-M6 still not exercised; the 7j startup crash is fixed, the world run crashed.** AutShadow fix 15697ea (16-byte alignment, the bogus 0x10FF6BC icall-desc hit is gone, init line says 1775 IC slots guarded) removed the 7j crash at 0x00E824B2: MonoThread and AutShadow init OK, the agent answered, `autshadow` switch verified, world loaded. About 90 s in the world the sim thread crashed at 0x00AD04EC (null read at +0x24, stack refs inside the asi). c[602]/c[605]/c[606] were not read, so the G-M6 criteria in §7 stay open.
