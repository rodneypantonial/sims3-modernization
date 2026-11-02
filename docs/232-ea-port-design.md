# 232. EA app 1.69.47 port of ts3native: design (2026-11-02 PHT)

Question: ship EA app 1.69.47 support with the least new machinery and no risk to the Steam build. Inputs: docs/230
(coverage of the first EA image), docs/68 §7-§7.5 (tools), docs/231 (mod-version popup), `out/port/ts3-ea169-file-fec39efa/`
(EA-derived, never committed). [verified] = read in the CSVs/sources this session, [inferred] = reading of them.

## 1. What the map says [verified]
- `map.csv` 1,123 rows: exact 573, masked 268, ref 248, near 17, ambiguous 9, offset-changed 5, missing 3. `problems.csv`
  284 rows, 186 `ai=yes`: by section .text 19, .rdata 50, .data 117; spread over 42 source files, worst routesnap.cpp 40,
  replay.cpp 14, crashfx.cpp 9, memdiag.cpp 9, locowin.cpp 8, memexp.cpp 8. 44 of the 186 are `ported (by-value)` IC tables
  (`kSnIcDef[0..38]`, `kHxIc[0..6]`): patch-bytes-check already holds their 1.69 address, `gen` just has no rule for them.
  `ghidra-names.csv` places 0 of the 186 (code names only). Patch bytes: 244 ok, 120 ported, 5 mismatch (4 addresses,
  eafix/watchpt/crashfx/gcroots), 4 unmapped. Vtables: 27 named by our sources, 17 unshifted (vt_Portal 0xFED048 ->
  0x1038590, 3 votes), 10 shifted or resized (0xFEB848 221 -> 73 slots). `map.offsets.csv` 9 groups, 5 sites (steerro, watchpt,
  rtoro, memexp).
- Not in any source file with `ai=yes`: decompress.cpp, optsinit.cpp, waterjps*.cpp, alloc.cpp, cellmemo.cpp, movedwait.cpp
  (sigmap: 24 features fully mapped, docs/230). These are the first-smoke pool.
- `Matches` = `Readable(addr, n) && memcmp` (ts3native.cpp:47); `InstallDetour`/`PatchImm32` go through it. Plain data reads
  (`*(DWORD*)0x0116...`) have no check. Host gate today: name `TS3W.exe` + base 0x400000, else `SKIP all` (ts3native.cpp:4688). <!-- docs-code: ok -->

## 2. Options
**A. Codemod + compile-time table, second asi (`ts3native-ea169.asi`).** `addr-codemod --apply` wraps 1,393 literals as
`TS3A(0x...)`; default `TS3A(x)` = `(x)` (preprocessor: Steam object code cannot change). `/DTS3A_TABLE` build takes the pairs
from a generated header; constexpr lookup, so static initializers (130 `static const DWORD ... = 0x00...` lines [verified
grep]) stay constants, and 12 case labels (apex.cpp) + 5 inline asm (mintcall.cpp) stay literal `manual` rows [verified
docs/68 §7.1]. Unresolved rows: `gen` writes `0` (new flag `--zero-unresolved`), so a code patch at 0 fails `Readable` and
logs SKIP; a data read at 0 would crash, so every file with a zero row is OFF in the EA ini and CI asserts it. Players
install the asi for their edition; the host check selects by PE timestamp + SizeOfImage (6707155C / 0xEB5000 vs 52DEC247 /
0xE5A000). New code: ~40 lines gen, ~10 lines host check, ~15 lines build.yml. [inferred: counts from the §7.1 scan]
**B. One asi, runtime table.** `TS3A` as a runtime lookup breaks every constant context: the 130 static-initializer lines
(several hundred literals), 12 case labels, 5 inline asm, template/constexpr uses, array initializers (`kSnIcDef` 39 dwords,
`kHxIc` 7), and the 5 generated `vcallsites*.h` headers (5,917 rows, probe-only). Each becomes lazy init or an accessor: a
non-mechanical refactor of ~74 files that changes Steam code paths and adds a lookup to hot reads. Most machinery, and it
removes the Steam byte-identity guarantee. Rejected. [inferred]
**C. Apex-style runtime signature scan (`sigmap-ea169.json`).** 740 of 875 code sites unique, but sigmap has no data
addresses at all (code sites only, tools/sigmap.ts header) and no ref-following; the 248 `ref` + 167 unmapped data rows
would need exe-addr-map's ref logic rewritten in C++ inside the asi, plus signature storage and ambiguity handling at run
time with no offline review. Also [inferred] the EA `.text` is decrypted after the DRM wrapper runs; a scan in DllMain may
see ciphertext. Largest new machinery; its one advantage (future EA patches without a rebuild) is not asked for. Rejected.
**Recommendation: A.** It reuses port-run/gen/patch-bytes-check as built, the table is addresses only (committable like
`docs/data/sigmap-ea169.json`), and Steam stays provably byte-identical.

## 3. Handling the problem classes under A
- 2026-11-02 `--xrefs` pass (docs/68 §7): 109 of the 140 data/`.rdata` rows resolved by Steam xref windows (kind `xref`, inferred); 140 -> 38 zero rows, `AI needed` 186 -> 82. The leftovers are code rows and data nobody references directly.
- 186 `ai=yes`: 44 by-value IC rows -> `gen` takes patch-bytes `ported (by-value)` as table rows (port-run already has them).
  142 left -> `0` in the table + their 42 files OFF on EA. Resolve only the ones a wanted feature needs, in Ghidra on the main
  laptop (`probe-out/ts3-ea169-file.exe` + `ghidra-names.csv`), recorded in `docs/data/ea169-overrides.csv`
  (steam,ea,label,evidence; addresses only) that `gen` reads. The 6 ts3native.cpp rows (0x676340.., 0x74CFC0, 0xE4E168) and
  3 `ts3native.cpp` data rows come first: they sit outside any feature switch. [inferred: check at task 5]
- 5 mismatches, 9 ambiguous, 3 missing, 17 near: `near` passes `--allow-inferred` and is protected by `Matches`; the others
  stay 0 -> their files (eafix, watchpt, crashfx, gcroots, routephases, allocsites, steerro, p4clone, avoidro, memexp, rtoro)
  OFF until hand-verified.
- 120 `ported` byte expectations: expected bytes embed Steam rel32/pointers, so `Matches` SKIPs on EA (safe, feature inert).
  Done on branch ea169-bytes: `native/ts3native/ts3bytes-ea169.h`, written by `patch-bytes-check.ts --table` / `port-run.ts` (244 ok + 120 ported = 364 rows: EA address, length, the game's own bytes), used by `Matches`/`InstallDetour` under `TS3A_TABLE` (docs/68 7.3). The 5 mismatches and 4 unmapped get no row and still SKIP.
- 407 slot-shifted vtables, 5 offset-changed sites: slot numbers and struct displacements are not addresses, the codemod does
  not touch them. steerro, watchpt, rtoro, memexp, lotfd (vt_Portal) OFF on EA; re-derive by hand later (docs/230 reading).
- Gating rule: EA ini = Steam ini with every section `Enabled=0` except an allowlist in build.yml (same awk style as the
  release check); CI fails if an allowlisted section's module file appears in the header's `// unresolved` list. `ALLOW_ON` (build.yml, since e09801d) lists sections that get the same assert, keep their shipped values and also get `Enabled=1` forced (for sections the Steam ini ships off); `[FootprintIndex]` is the first.
- Steam identity proof: the `--apply` commit is a separate PR; CI artifact of its parent (main) vs the PR artifact: sha256 of
  the `.text`/`.rdata` sections (parsePe from tools/exe-addr-map.ts, `node -e`) and `ts3native.map` minus the timestamp line
  must be equal. Build.cmd already writes the .map.
- Table review/regeneration: `node tools/port-run.ts --exe probe-out/ts3-ea169-file.exe --allow-inferred` writes
  `native/ts3native/ts3addr-ea169.h` (sorted pairs + `// unresolved: 0x... file:line` comments, no EA bytes); review = git diff.
  Rerun after any merge that adds literals.
- Managed lane (separate, after native smoke): extract the 1.69 assemblies on the EA laptop (`tools/extract-assemblies.ts`,
  into a gitignored dir), `port-report` managed stage -> `.managed.csv` (ts3fixes methods changed), rebuild ts3fixes with
  `REF_DIR` pointed at 1.69 -> `ts3fixes-ea169.package`. Version-1 S3SA header, so no mod-version popup from ours (docs/231);
  the popup itself comes from 1.67 core mods and is answered by the harness guard in docs/231 §2.

## 4. Branch and merge plan around v0.2.5 (PHT)
- Mon 11-02 .. Thu 11-05: branch `ea169-port` from main. Only tooling + ts3addr.h + host check + build.yml there; no
  `--apply` (79 files rewritten would conflict with every main commit until the release). Freeze Tue 11-03, ship Thu 11-05 from main.
- Fri 11-06: merge main into the branch, run `--apply` as one commit (idempotent; a second scan must show 0 `wrap` rows),
  identity proof (task 4), PR to main. After that every merge from main is `--apply` again, never a conflict resolution.
- First EA smoke can run before 11-06 from a branch CI artifact (PR builds run `native-ea169`), with `--apply` done on the
  branch only and dropped before the merge.

## 5. Tasks (effort per CLAUDE.md: minutes/hours wall-clock; game runs on the EA laptop, correctness only)
| # | task | files | agent | time | EA runs | verify |
|---|---|---|---|---|---|---|
| 1 | `gen --zero-unresolved`, by-value rows, committed output `native/ts3native/ts3addr-ea169.h` with `TS3A_HOST_PE/SIZE` defines and `// unresolved` list; ts3addr.h includes it | tools/addr-codemod.ts, tools/port-run.ts, native/ts3native/ts3addr.h | sonnet | 45 min | 0 | port-run prints gen 1,031 addresses, 142 zero rows; `node tools/selftest-all.ts --ci` green |
| 2 | host check: under `TS3A_TABLE` accept any host name whose PE timestamp+SizeOfImage match the table; Steam build unchanged | native/ts3native/ts3native.cpp | sonnet | 20 min | 0 | EA log `host Sims3Launcher.exe ... pe 6707155C size 0xEB5000`, no `SKIP all` |
| 3 | build.yml job `native-ea169`: one `cl ... /DTS3A_TABLE` line (no selftest), EA ini from the Steam ini via awk allowlist, assert allowlist vs unresolved list, artifact `ts3native-ea169` | .github/workflows/build.yml | sonnet | 40 min | 0 | PR artifact has asi + ini; a deliberately wrong allowlist entry fails the job |
| 4 | **done 2026-11-02**: `--apply` on the branch (80 files, 1,414 literals), identity proof vs the pre-codemod branch build (run 41387248277): `.text`/`.data` equal, `.rdata` differs only in the debug-dir timestamp (2 bytes); no .map in the Steam artifact | native/ts3native/*.cpp (79 files), docs/68 §7.1 note | sonnet | 30 min | 0 | both hashes equal; second scan 0 `wrap` rows |
| 5 | **done 2026-11-02: 30 of 31 rows + 8 bytes-bad sites via `docs/data/ea169-overrides.csv` (port-run `--overrides`)**; resolve the 9 ts3native.cpp rows + any zero row in the smoke pool in Ghidra (main laptop), `docs/data/ea169-overrides.csv` | docs/data/ea169-overrides.csv, tools/addr-codemod.ts (read overrides) | reasoning | 2 h | 0 | port-run: those rows leave the unresolved list with evidence text |
| 6 | first smoke (§6) **done 2026-11-02: PASS on the second run (ini fix 80b215b, Verify=1; lot reached, 5 min soak, see end)** | ts3native-ea169.ini on the EA laptop | reasoning | 1 h | 1 (+1 rerun) | log: host line, no `SKIP all`, every on-feature logs its install line, no `FAIL`, reach the main menu and a lot |
| 7 | widen: features with only `ok` expectations and no zero rows, one per run | allowlist in build.yml | sonnet | 30 min each | 1 each | same as 6 plus the feature's own first log line. **Run 7c done 2026-11-02 (build e09801d, `[FootprintIndex]` forced on): `SKIP footprint index: 0x006677E0..0x00667B60 differs from Steam 1.67`, FNV range guard, nothing written; game stable with all packs mounted. Next standalone candidate: `[LotSinCos]` (not built).** **Run 7a done 2026-11-02 (build 30603bd, `[WaterJps]`): `SKIP water jps`, code hash differs on 1.69.47; game stable, see "Task 7 runs" below.** 7b not run: every other candidate section is `Enabled=0` in the Steam ini (the allowlist keeps the value, so nothing installs) and `[SlotEnabledRO]` / `[CellMemoProbe]` also SKIP by dependency (`[RouteGate] Enforce=1` + async, `[RouteJobPhases]`), both off on EA |
| 7d | rc smoke: `ts3native-ea169-rc.ini` (section "EA release-candidate ini" below) as `ts3native.ini` on the EA laptop, one load of `Baseline-SV-Spring.sims3`, a save, quit | tools/ea-ini.ts | sonnet | 30 min | 1 (done 2026-11-02, see "Run 7d") | game stable with all packs mounted; log has no `FAIL`/exception from a kept section; every kept section with Enabled=1 logs its install or a SKIP line; a crash names its section, which goes to `ALWAYS_OFF` |
| 8 | managed lane (§3 last bullet) | tools/setup/build-fixes.ts (REF_DIR arg), docs/231 | reasoning | 3 h | 1 | `.managed.csv` reviewed; game starts with ts3fixes-ea169 and no popup |
| 9 | post-release: `--apply` on main, PR, docs/68 §7.1 "applied" | as task 4 | sonnet | 30 min | 0 | identity proof repeated on main |

## 6. First smoke on the EA laptop
ON: `[Decompress]`, `[OptsInit]` only (both sigmap fully mapped, shipped on Steam, every site byte-checked, their files have no
`ai=yes` row [verified]). Add `[WaterJps]` on the rerun if its files show no zero row. OFF: everything else, in particular
`[GC]`/gcroots (mismatch kMxNotifySite), `[UnseenFallback]` (unseen.cpp 5 rows), `[RouteAsync]`/`[RouteGate]`/`[Discourage]`
(routing modules with offset-changed and ambiguous sites), `[SaveWrite]` (savecompress.cpp 2 rows), `[LotRemoveGuard]`
(lotfd vt_Portal), `[UserDiag]`, `[CrashReport]`, `[MemDiag]`, `[Verify]`, steerro, watchpt, mintcall, apex. If every patch
logs `bytes differ`, suspect `.text` still encrypted at DllMain [inferred]: then defer installs to the first hooked frame.

## 7. Open
- Decryption timing at asi load (first smoke answers it). - 120 `ported` byte expectations need per-build bytes before most
features can go on. - steerro/watchpt struct layouts: rewrite, not port (docs/230).
- Not checked by the design: the ini-section-to-source-file map the task 3 allowlist assert needs (by hand), and whether
  selftest.exe compiles under `/DTS3A_TABLE` (task 3 skips it). Risk: a feature left ON whose data read hits a zero table
  row crashes at load; the ini allowlist + CI assert is the only guard.
- WaterJps (run 7a): the guard hashes `0x61F750..0x61FF93` (table: `0x00620880..0x00621063`) against one Steam 1.67 FNV constant and logs no measured
  value, so the log cannot say whether only the hash or the code differs. Fix: a per-build expected hash column in the port table (same for `[FootprintIndex]`,
  whose guard has the same shape), plus a way to set `Enabled=1` in the EA ini for sections that are off on Steam; `[FootprintIndex]` confirmed it in run 7c (same SKIP shape, no measured value). ALLOW_ON (e09801d) now forces `Enabled=1` on EA for listed sections; the remaining standalone candidate without a range hash is `[LotSinCos]` (lotsincos.cpp: one mapped address `0xAB8540`, 6-byte prologue check, `Verify=1` bit-compares against the game's code, no routing dependency, no unresolved or problems.csv row).
Fix 2026-11-02 (after the first smoke): the generated EA ini zeroed only `Enabled`, so `[ArenaGuard] TailPad=1` and `InitobjFix=1` (independent of `Enabled`) ran on EA and TailPad read an unresolved row (arenatail.cpp:233, caught C0000005). The `native-ea169` step now zeroes every integer-valued key outside the allowlist and prints the zeroed keys per section.

## First smoke 2026-11-02 (task 6)
Run 1 12:27 PHT: CI artifact `ts3native-ea169.asi` (run 41387870205, 1,135,104 B, sha256 `bfef6925...cf040f`) as `Bin\ts3native.asi` with the
EA ini (Decompress and OptsInit `Enabled=1`, 64 sections `Enabled=0`), launched through `ts3probe.py serve --launch`, Bin pair backed
up as `.pre-ea169`. Log head [verified]: `ts3native 0.36.152`, `host Sims3Launcher.exe base 0x00400000 pe 6707155C size 0xEB5000`,
`host build: EA app 1.69.47`, no `SKIP all`. Installed: `OK   decompress: call 0x004EC1A5 -> RefpackHook, mode 1 verify 0 (0x004EB4F0
starts 83 EC 10 8B 4C = game code at load time)`, `OK   detour RoutePlanOptions_ctor (I-94-322 hub refs) at 0x00654240`, `OK   OptsInit:
search options hub references +4 / +0x34 start at 0 (I-94-322)`. No `FAIL`, no `bytes differ`: `.text` is decrypted when the asi loads
(§7 open point closed; the one `SKIP detour Route_GetOption at 0x0074DF80: prologue differs` is the unconditional NOYIELD detour in
ts3native.cpp, nothing written). `GC patches disabled (Enabled=0)` (not `SKIP GC`: the ini gate runs first). Decompress worked through
the menu and the save load: `calls 21135 fails 0 mismatches 0 in 54 MB out 131 MB time 162 ms` (Verify=0, so output bytes unchecked).
- Not in the §6 ON list but installed: `OK   arena tail pad: 6 sites` and `OK   initobj size: transform call 0xE63427` ([ArenaGuard]
  `TailPad=1` / `InitobjFix=1` are independent of `Enabled`, the awk allowlist keeps them). TailPad also read the zero row 0x011EE508
  (`arenatail.cpp:233`, `// unresolved`), seen as `exception C0000005 at 0x5D40CC02` inside the asi on the main thread, caught by its own
  `__except` [inferred from the log order and the unresolved list]. Task 7 rerun: set both to 0 or resolve the row first.
- Run stable: no. `load_save Baseline-SV-Spring` ended in `A serious error has occurred while loading 'Baseline-SV-Spring'` after 10 s (no
  new `ScriptError*.xml`, no crash, game alive at the menu). Rerun 12:43 with the `.pre-ea169` pair (`SKIP all`, c035abe5a3a6) and the
  same Mods and save: identical failure at 9 s. So the load failure is not the ea169 asi. Cause (isolated 13:04, docs/233): the smoke
  driver passed `file: "Baseline-SV-Spring"` without `.sims3`; cycle.ts passes `Baseline-SV-Spring.sims3` and the same save then loads
  in ~12 s. Not the cause: ts3agent/ts3fixes (sha256 `e9b98bf37d90`/`b067efaa9047`, identical to the 12:09 change set `344a2752d415`),
  the 260 B `scriptCache.package` (the game writes it at every start). The restamped ErrorTrap P169 is a separate failure: it crashes
  the load (`0xc0000005`), so it is disabled (docs/233). Task 7 reruns must load `Baseline-SV-Spring.sims3`.
- Verdict: host check PASS, decryption timing PASS, Decompress + OptsInit installed PASS, lot reached FAIL (environment). CPU/WS: only
  at the menu (300 MB WS at attach), no soak. Both runs' logs: `probe-out/ea-smoke6-1102*.log`, `probe-out/ea-smoke6-1102.ts3native.log`.
- Task 7 candidates (files with 0 unresolved rows and 0 problems.csv rows, own ini gate): `[WaterJps]` (waterjps.cpp + wastar.cpp, §6
  names it), `[SlotEnabledRO]` (slotro.cpp), `[CellMemoProbe]` (cellmemo.cpp, probe only). replandedupe/plandeadline/movedwait are clean
  too but need `[RouteAsync]` (unseen.cpp 5 rows, routing sites offset-changed).

### Second run 2026-11-02 (ini fix 80b215b, Verify=1)
13:22 PHT, CI artifact of 80b215b (asi sha256 `ff4f6ef2...f87366`, ini `730b6afa...944f0` with `[ArenaGuard]` `TailPad=0` / `InitobjFix=0`),
installed as the Bin pair with the 12:43 pair backed up as `.pre-ea169b` (`c035abe5a3a6` / `8aa37522782a`, restored after the run, sha-checked);
`[Decompress] Verify=1` set in the installed ini for this run only. Driver: `ts3probe.py serve --launch` + `cycle.ts --save Baseline-SV-Spring.sims3`
(agent after 14 s, "Mod Scripts Found" accepted, world loaded 14 s, `LiveModeState`, households 32, Keaton). **Verdict: PASS** [verified]:
- Log: `host Sims3Launcher.exe base 0x00400000 pe 6707155C size 0xEB5000`, `host build: EA app 1.69.47`, no `SKIP all`, no `FAIL`, no `bytes differ`.
  `OK   decompress: call 0x004EC1A5 -> RefpackHook, mode 1 verify 1`; `OK   detour RoutePlanOptions_ctor (I-94-322 hub refs) at 0x00654240`;
  `OK   OptsInit: search options hub references +4 / +0x34 start at 0 (I-94-322)`. ArenaGuard off as intended: `arena guard disabled (Enabled=0)`,
  `arena tail pad disabled (TailPad=0)`, `initobj size fix disabled (InitobjFix=0)`; no exception inside the asi (run 1's `0x5D40CC02` is gone).
- Decompress with verification over menu, load, 5 min live, one in-world reload and a clean quit (`quit_game`, process exit writes the
  end line): `decompress: mode 1 verify 1 calls 45625 fails 0 mismatches 0 in 215 MB out 619 MB time 450 ms max 14502 us`: the RefpackFast
  output equals the game decoder's on 1.69.47 too.
- Soak 13:24-13:29 (every 30 s): WS 1580 -> 1541 MB flat (1542 at the end), commit 1415 MB flat, game CPU 3.4-3.7 % of 16 logical CPUs
  (about half a core), 67 threads; no new `ScriptError*.xml` (only the 12:11 one from before the run). Logs: `probe-out/ea-smoke6b-1102.log`,
  `probe-out/ea-smoke6b-1102.drive.log`, `probe-out/ea-smoke6b-1102.ts3native.log`, events `probe-out/20261102-132245`.
- Not ours, logged by the asi's first-chance exception logger: 3x `exception C0000005 at 0x04697CC2` on thread 27868 (page-aligned read faults at
  `0x42841000`, `0x38DB1000`, `0x41485000`, eip in a JIT/heap region, outside the asi module 0x5D400000-0x5DB74000, ebp 0, same esp each time),
  during the lot load; the game did not notice. Steam logs show no such lines [inferred: Mono/GC first-chance faults on 1.69; check against a
  `SKIP all` run before reading anything into it].
- Also in this run: the ts3agent `LoadSave` fix (bare name gets `.sims3`, package `3eb7bb6fc994`) checked live: `load_save {"file":"Baseline-SV-Spring","reload":true}`
  answered `{"loading":"Baseline-SV-Spring.sims3"}` and reached `LiveModeState` again in ~40 s. No EA app relaunch followed the clean quit (150 s watch).

## Task 7 runs 2026-11-02
Run 7a 13:56 PHT: CI artifact of 30603bd (run 41396273029; asi sha256 `bc71151ee14d`, ini `208ffe1715a1`, on: `[Decompress]` `[OptsInit]` `[WaterJps]`),
Bin pair backed up as `.pre-7a` (`c035abe5a3a6` / `8aa37522782a`, restored after the run, sha-checked), `Bin	s3native.log` deleted first. Driver as 6b:
`ts3probe.py serve --launch` + `cycle.ts --save Baseline-SV-Spring.sims3` (agent after 24 s, "Mod Scripts Found" accepted, world loaded 16 s, `LiveModeState`,
households 32, Keaton), 5 min soak, `quit_game`, 150 s relaunch watch. Install lines [verified]:
- `SKIP water jps: code at 0x00620880..0x00621063 differs from Steam 1.67, nothing written` (the guard prints no measured hash; §7)
- `OK   decompress: call 0x004EC1A5 -> RefpackHook, mode 1 verify 0 (0x004EB4F0 starts 83 EC 10 8B 4C = game code at load time)`
- `OK   detour RoutePlanOptions_ctor (I-94-322 hub refs) at 0x00654240`, `OK   OptsInit: search options hub references +4 / +0x34 start at 0 (I-94-322)`
- host line unchanged (`pe 6707155C size 0xEB5000`, `EA app 1.69.47`), no `SKIP all`, no `FAIL`, no `bytes differ`, no exception line inside the asi
  (6b's 3x `C0000005 at 0x04697CC2` first-chance faults did not recur: so they are not reproducible per load either).
- Decompress: `calls 45668 fails 0 mismatches 0 in 222 MB out 646 MB time 583 ms max 25284 us` (Verify=0 this run).
- Soak 13:58-14:03 (10 samples, 30 s): game CPU 3 % of 16 logical CPUs every sample, WS 1577 -> 1569 MB, commit 1451 -> 1452 MB, 67 threads;
  no new `ScriptError*.xml` (1 before, 1 after). Clean quit: process gone 5 s after `quit_game`, no EA relaunch in 150 s.
- Verdict: WaterJps SKIP (safe, nothing written), the 6b feature set still PASS. Logs: `probe-out/ea-7a-1102.ts3native.log`, `.soak.log`, `.drive.log`, `.log`.
Run 7b: not run (see §5 row 7 and §7). Candidates with clean files (no unresolved and no problems.csv rows): GoalGen, CellMemoProbe, Compaction, FootprintIndex,
HeightCache, TerrainGrave, PlacementRO, GoalValidRO, MovedWaits, SlotEnabledRO, Gate0RO, ProbeP10, LotSinCos, ProbeSnap, HitchAttr, UnseenFallback, UnseenGhost,
WorldImage, CrashReport, P6Waits; all `Enabled=0` on Steam, most need a routing feature that is off on EA. Best standalone next: `[FootprintIndex]` (FNV range
guard + 3 byte-checked sites), once the EA ini can turn it on.
Run 7c 15:04 PHT: CI artifact of e09801d (run 41398218278; asi sha256 `f4d01234cea7...780ff`, ini `8df4066c764c...9e80`, on: `[Decompress]` `[OptsInit]`
`[FootprintIndex]` via ALLOW_ON, `Enabled=1` read back in the installed ini), Bin pair backed up as `.pre-7c` (`c035abe5a3a6` / `8aa37522782a`, restored after the run,
sha-checked), `Bin	s3native.log` deleted first. Driver as 7a (`ts3probe.py serve --launch --port 47891` + `cycle.ts --save Baseline-SV-Spring.sims3`); the probe
logged `LoadOption.ini selectedflags already 001fefff`, so every installed pack mounted: agent after 15 s, "Mod Scripts Found" accepted, world loaded 39 s (16 s in 7a),
`LiveModeState`, households 37 (32 in 7a), Keaton. Install lines [verified]:
- `SKIP footprint index: 0x006677E0..0x00667B60 differs from Steam 1.67, nothing written` (the FNV range guard over the ported LotLevel_SaveFootprints body; the
  three byte-checked sites were never reached, the guard prints no measured hash, same shape as WaterJps in §7)
- `OK   decompress: call 0x004EC1A5 -> RefpackHook, mode 1 verify 0 (0x004EB4F0 starts 83 EC 10 8B 4C = game code at load time)`
- `OK   detour RoutePlanOptions_ctor (I-94-322 hub refs) at 0x00654240`, `OK   OptsInit: search options hub references +4 / +0x34 start at 0 (I-94-322)`
- host line unchanged (`pe 6707155C size 0xEB5000`, `EA app 1.69.47`), no `SKIP all`, no `FAIL`, no `bytes differ`, no exception line inside the asi (module
  `0x5D320000-0x5DA96000`); `arena guard disabled (Enabled=0)`, `arena tail pad disabled (TailPad=0)`, `initobj size fix disabled (InitobjFix=0)`, `water jps off (Enabled=0)`.
- Decompress at exit: `calls 67063 fails 0 mismatches 0 in 380 MB out 1229 MB time 1139 ms max 19292 us` (Verify=0; about twice 7a's volume, all packs).
- Soak 15:06-15:11 (10 samples, 30 s): game CPU 3-4 % of 16 logical CPUs, WS 1810-1842 MB (7a: 1569-1578 with fewer packs), commit 1709-1741 MB, 142 threads
  (7a: 67); no new `ScriptError*.xml` (1 before, 1 after). Clean quit: process gone 5 s after `quit_game`, no EA relaunch in 150 s.
- Not checked: the window title ("The Sims™ 3 Into the Future" expected with all packs); the probe does not log it and the game had exited before anyone looked.
- Verdict: FootprintIndex SKIP (safe, nothing written), the 6b feature set still PASS with all packs mounted. Logs: `probe-out/ea-7c-1102.ts3native.log`, `.soak.log`,
  `.drive.log`, `.log`, events `probe-out/20261102-150447`. Next standalone candidate (§7): `[LotSinCos]`, not built yet.

Run 7d (rc smoke) 15:24 PHT: CI artifact of 9839785, asi sha256 `633cbc7e9c96`, `ts3native-ea169-rc.ini` (`ff9c42a4be72`) installed as the Bin pair (`.pre-7d` backup of
`c035abe5a3a6` / `8aa37522782a`, restored and sha-checked), `ts3native.log` deleted first. Driver as 7c (`ts3probe.py serve --launch` + `cycle.ts --save Baseline-SV-Spring.sims3`,
TS3_LAUNCH=ea). `LoadOption.ini selectedflags already 001fefff` (all packs mounted); agent after 25 s, "Mod Scripts Found" accepted, world loaded 22 s, `LiveModeState`, households 37,
Keaton. Enabled=1 in the rc ini: `[GC]` `[SaveWrite]` `[Decompress]` `[WaterJps]` `[UserDiag]` `[OptsInit]`. Log lines [verified]:
- `OK   GC initial heap` + 3 `GC growth step`, `OK   save write: 6 call sites`, `OK   user diag save phases: 2 call sites`, `OK   decompress: call 0x004EC1A5 -> RefpackHook, mode 1 verify 0`,
  `OK   detour RoutePlanOptions_ctor ... at 0x00654240`, `OK   OptsInit: search options hub references +4 / +0x34 start at 0 (I-94-322)`.
- Expected SKIP: `SKIP water jps: code at 0x00620880..0x00621063 differs from Steam 1.67, nothing written`.
- Two further SKIPs, not from a section turned on in the rc ini, both safe (nothing written):
  `SKIP door memo x: DoorMemoX=0 needs the route snapshot's World_FireEvent hook and IC slots (snReady 0x0)` and
  `SKIP mintcall: EA code at 0x00E5498E (173 bytes) is not the stock Steam 1.67 bytes (another hook in the call path)` (`[MonoFast]` is default-on in the Steam ini).
- No `FAIL`, no `bytes differ`, no `SKIP all`, no asi exception line, no crash file; decompress at exit `calls 66523 fails 0 mismatches 0`.
- Soak 15:29-15:31 (5 samples, 28 s apart, taken after about 4 min in the world): WS 2065-2073 MB, 144 threads, game CPU flat; no new `ScriptError*.xml`. `quit_game`: process gone
  within 5 s, no EA relaunch in 60 s; exit line `1 world loads`. No save made (the row's save step was not run, as in 7c).
- Verdict: PASS for the criteria, with the `mintcall` SKIP to decide on (§7: keep as a guard SKIP or zero `[MonoFast]` in the rc ini). Logs: `probe-out/ea-7d-1102.*`.

## EA release-candidate ini (2026-11-02)

`ts3native-ea169-rc.ini` (artifact `native-ea169`, written by `tools/ea-ini.ts`; the minimal `ts3native-ea169.ini` stays as before). Start = the shipped Steam ini; a section is zeroed (every integer key 0) when (a) a source file of it (tools/ini-sections.json, plus cpp files including a flagged .h) has an `// unresolved` row in ts3addr-ea169.h or a `// bytes-bad` row (mismatch/unmapped; written by port-run into ts3bytes-ea169.h), (b) it is on the always-off list (EaDiag, Verify, MemDiag, SteerRO, FpRebuildLock, Watchpoint, RouteToObjRO: struct layouts, §3), or (c) a dependency is zeroed (`DEPENDS` in the script: the RouteGate/RouteAsync pair, RouteJobPhases, SaveWrite, UserDiag, MonoThread, RouteSnap). [GC] ignores memexp.cpp (only its `SqrtHeapLimit`, shipped 0). Table regenerated with `--overrides docs/data/ea169-overrides.csv` (task 5: 34 hand-verified rows, each with two independent matches, in the CSV evidence column): 1 unresolved row left (allocsites.cpp 0x4E3D00, a range bound), 0 bytes-bad sites; sections whose only evidence is "files clean" are not proven to work on 1.69, only free of known address/bytes gaps (WaterJps still logs SKIP by its hash guard).

| section | shipped Enabled | rc state | reason |
|---|---|---|---|
| [GC] | 1 | kept | files clean |
| [Compat] | - | kept | files clean |
| [ThreadStack] | 0 | kept | files clean |
| [Watchpoint] | 0 | zeroed | always off: watchpt.cpp struct layouts |
| [CrashForensics] | 0 | kept | files clean |
| [RouteCache] | 0 | kept | files clean |
| [RouteWorker] | 0 | kept | files clean |
| [RouteAsync] | 1 | kept | files clean |
| [RouteBudget] | 0 | kept | files clean |
| [RouteJobPhases] | 0 | kept | files clean |
| [GoalGen] | - | kept | files clean |
| [CellMemoProbe] | 0 | kept | files clean |
| [RouteEarlyOut] | 0 | kept | files clean |
| [SaveWrite] | 1 | kept | files clean |
| [Decompress] | 1 | kept | files clean |
| [Alloc] | 0 | kept | files clean |
| [WaterJps] | 1 | kept | files clean |
| [SaveFlush] | 0 | kept | files clean |
| [UserDiag] | 1 | kept | files clean |
| [MemDiag] | 0 | zeroed | always off: memexp.cpp struct layouts |
| [Compaction] | 0 | kept | files clean |
| [SaveCompress] | 0 | kept | files clean |
| [FootprintIndex] | 0 | kept | files clean |
| [HeightCache] | 0 | kept | files clean |
| [TerrainGrave] | 0 | kept | files clean |
| [GcThreadCheck] | 0 | kept | files clean |
| [GcRootCheck] | 0 | kept | files clean |
| [ArenaGuard] | 0 | kept | files clean |
| [IcLockCheck] | 0 | kept | files clean |
| [RouteGate] | 1 | kept | files clean |
| [Discourage] | 1 | kept | files clean |
| [LotRemoveGuard] | 1 | kept | files clean |
| [SteerRO] | 0 | zeroed | always off: steerro.cpp struct layouts |
| [FpRebuildLock] | 0 | zeroed | always off: steerro.cpp struct layouts |
| [LotFrontDoorRO] | 0 | kept | files clean |
| [ObstructedRO] | 0 | kept | files clean |
| [AvoidRO] | 0 | kept | files clean |
| [RouteToObjRO] | 0 | zeroed | always off: rtoro.cpp struct layouts |
| [PlacementRO] | 0 | kept | files clean |
| [GoalValidRO] | - | kept | files clean |
| [IdleCheckRO] | 0 | kept | files clean |
| [MovedWaits] | 0 | kept | files clean |
| [SlotEnabledRO] | 0 | kept | files clean |
| [Gate0RO] | 0 | kept | files clean |
| [ProbeP1b] | 0 | kept | files clean |
| [LocoWindow] | 0 | kept | files clean |
| [ProbeP10] | 0 | kept | files clean |
| [MonoThread] | 0 | kept | files clean |
| [LotStream] | 0 | kept | files clean |
| [Verify] | 0 | zeroed | always off: bit-compares against Steam code |
| [JobQueue] | 0 | kept | files clean |
| [D3DMultithreaded] | 0 | kept | files clean |
| [LotSinCos] | 0 | kept | files clean |
| [IdCache] | 0 | kept | files clean |
| [ProbeSnap] | 0 | kept | files clean |
| [ProbeValues] | 0 | kept | files clean |
| [LoadProf] | 0 | kept | files clean |
| [RouteSnap] | 0 | kept | files clean |
| [OptsInit] | 1 | kept | files clean |
| [HitchAttr] | 0 | kept | files clean |
| [UnseenProbe] | 0 | kept | files clean |
| [UnseenThrottle] | 0 | kept | files clean |
| [UnseenFallback] | 1 | kept | files clean |
| [UnseenGhost] | 0 | kept | files clean |
| [PlanRepeat] | 0 | kept | files clean |
| [MonoFast] | - | kept | files clean |
| [AutShadow] | 0 | kept | files clean |
| [WorldImage] | 0 | kept | files clean |
| [CrashReport] | 0 | kept | files clean |
| [P6Waits] | 0 | kept | files clean |
| [ResourceCache] | - | kept | files clean |
| [EaCrashFixes] | - | kept | files clean |
| [EaDiag] | - | zeroed | always off: EA-only diagnostics, log only, minimal ini only |
