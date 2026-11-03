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
| 7e | rc smoke of the routing sections (RouteAsync, RouteGate, Discourage, LotRemoveGuard, UnseenFallback) with the regenerated rc ini of 2207dbd, load `Baseline-SV-Spring.sims3`, 10 min at normal speed, quit | tools/ea-ini.ts | sonnet | 30 min | 1 (done 2026-11-02, **FAIL**, see "Run 7e") | init OK for the 5 sections, nonzero routing counters, no crash, no new `ScriptError*.xml`, clean quit |
| 7e2 | rerun of 7e with the callee fix df3f2e5 (rtsites.cpp takes the displacement from the trampoline copy), same procedure, 10 min at normal speed | rtsites.cpp | sonnet | 30 min | 1 (done 2026-11-02, **PASS**, see "Run 7e2") | as 7e |
| 7f | first live run of the range-guard ports WaterJps, FootprintIndex, LotSinCos (rc ini copy with `[FootprintIndex]`/`[LotSinCos]` Enabled=1 Verify=1, `[WaterJps]` Verify=1), 10 min at normal speed, one save, quit | rtsites.cpp range hashes | sonnet | 30 min | 1 (2026-11-02, **FAIL: startup crash**, see "Run 7f"; rerun = 7f2) | three expected OK init lines, no SKIP for them, Verify 0 mismatches, `footprint:` line after the save, routing counters active, no crash |
| 7f2 | rerun of 7f: same asi (76d2558) and the exact 7f ini plus `[MemDiag]` Light=1 + AllocSites=1 (virtual hooks only), 2 s memory sampler, external VM-region guard (kill above 2600 MB private), world load, 5 min, quit without saving | none | sonnet | 30 min | 1 (done 2026-11-02, **no growth repeated**, see "Run 7f2") | private memory stays under 2600 MB, three OK init lines, world loads, no crash |
| 7g | 7f ini again, no MemDiag, real routing (autopilot speed normal), one save via `save_game`, 10 min in the world, 2 s sampler + 2600 MB guard | none | sonnet | 30 min | 1 (done 2026-11-02, **PASS**; the coordinator's stop arrived after the run had ended, see "Run 7g") | three OK init lines, routing counters nonzero, `footprint:` line with verify ok, peak private under 2600 MB, no crash |
| 7h | 7g on head 7f6f7f8 (CI run 41415700157, asi `05c1f1ecbf2c`), same ini bytes (`e00614eb615a`) | none | sonnet | 30 min | 1 (2026-11-02, **FAIL: crash in the world after about 1.5 min**, see "Run 7h"; no fix tried) | as 7g |
| 7i | identical rerun of 7h (asi `05c1f1ecbf2c`, ini `e00614eb615a`, no change) | none | sonnet | 30 min | 1 (2026-11-02, **PASS**, see "Run 7i") | as 7h |
| 7j | 7i plus the managed 1.69 mods (ts3fixes `a82ad09c0461`, ts3agent `dd7a7cd67f52`) and `[RouteSnap] Count=1`, `[MonoThread] Enabled=1`, `[AutShadow] Enabled=1` (ini `cb9c88e481fa`), managed switch `autshadow` to be set before the world load, G-M6 guard check (docs/235 §7) | none | sonnet | 30 min | 1 (2026-11-02, **FAIL: crash at game start, 20 s after launch, before the agent answered**, see "Run 7j"; no fix tried) | MonoThread and AutShadow init OK, c[605]=0, c[606] one skip group (`CreateTooltipCallback`), `not on the content accept list` line, c[602] about 3568, routing counters as 7i, footprint verify ok, no crash |
| 7j2 | rerun of 7j with the AutShadow alignment fix 15697ea (asi `44b413fa7c35`), same ini `cb9c88e481fa`, same managed packages | native/ts3native/autshadow.cpp | sonnet | 30 min | 1 (2026-11-02, **FAIL: world loaded, then crash at 0x00AD04EC after about 90 s in the world**, see "Run 7j2"; no second fix tried) | as 7j |
| 7j3 | rerun of 7j2 with ts3agent 04a5609 (`9abf3308fc89`): kDialogRules declines the long-distance move dialogs, autopilot clicks logged at once; same asi `44b413fa7c35`, ini `cb9c88e481fa`, ts3fixes `a82ad09c0461` | mods/ts3agent | sonnet | 30 min | 1 (2026-11-02, **no crash, no unrequested mode exit; G-M6 counters differ from the doc 235 expectation**, see "Run 7j3") | as 7j |
| 7k | 7k stopped early (plan change: harness baseline first) | - | - | - | - | 0 | - |
| 7m | LotSinCos register fix 3949b38 + selftest order d91856d (CI run 41486682141, asi `119f5d400f2a`), 7i ini `e00614eb615a` (`[LotSinCos] Enabled=1 Mode=1 Verify=1`), normal ts3fixes `ac2c3f8799af`, ts3agent b58ab59 (`995699294ac4`); the vh-move flow (`vanilla-steps.ts accept`, accept_all 180 s, 10 min after the move) with the mod | native/ts3native/lotsincos.cpp | reasoning | 30 min | 1 (2026-11-03, **PASS**, see "Run 7m") | `edxClobber` 0, household moved, no crash 10 min after the move, LotSinCos init OK, no Verify mismatch line |
| 7n | Same set-up as 7m (asi `119f5d400f2a`, 7i ini `e00614eb615a`, normal ts3fixes `ac2c3f8799af`, ts3agent `995699294ac4`), second clean run; the vh-move flow with the mod | native/ts3native/lotsincos.cpp | reasoning | 30 min | 1 (2026-11-03, **PASS**, see "Run 7n") | `edxClobber` 0, household moved, no crash 10 min after the move, LotSinCos init OK, no Verify mismatch line |
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
- Verdict: PASS for the criteria, with the `mintcall` SKIP to decide on (§7: keep as a guard SKIP or zero `[MonoFast]` in the rc ini). **Decision 2026-11-02: keep `[MonoFast]` as is in the rc ini; its guard SKIP writes nothing, so no zeroing is needed.** Logs: `probe-out/ea-7d-1102.*`.

Run 7e (rc smoke of the routing sections) 15:38 PHT: **FAIL, crash during the world load.** CI run 41407209440 (head 2207dbd): asi `0e2ce2aec305`, `ts3native-ea169-rc.ini` `3618cda81a58`, installed as the Bin pair (`.pre-7e` backup of `c035abe5a3a6` / `8aa37522782a`, restored and sha-checked), `ts3native.log` deleted first, `TS3_LAUNCH=ea TS3_EXE=Sims3Launcher.exe TS3_MACHINE=ea-laptop`, driver as 7d. Agent answered after 17 s, "Mod Scripts Found" accepted, `Handlers.LoadSave` started; 15:39:12 the game died. [verified]
- Fault: `PRIV_INSTRUCTION` (C0000096) at `Sims3Launcher.exe` 0x00769111 (section 0001:0x00368111), thread 22728, edx = 0x00EC0FE0 (the `IC_GameUtils_TransitionToLoadWorldImpl` route-gate detour site), caller 0x00E5495B (inside the `mintcall` region that logged SKIP), then 0x00E5A4DE, 0x00D7F42D. Not analysed further (no fix tried). Dump and report: `probe-out/xcpt ALRPANTONIAL1 26-11-02 15.39.12.mdmp` / `.txt`.
- Init lines before the crash: RouteAsync `OK detour Route_PlanLocked at 0x0082B3B0`, `OK detour IC_Route_Plan at 0x00766120`, `OK kind deadlines: ready 1 ... mode 1`, `OK route deadline: PlanDeadlineMs 300 ... ready 0x7`; RouteGate `OK route gate sites: 25 of 25 docs/60 2.2/2.5 sites, ready 0x1FFFFFF`, `OK route gate closures on (Enforce=1)`; Discourage `OK discourage deferral (Mode=1 DestroyApply=1 DeadSweep=1)`, `OK footprint deferral (design U, docs/89)`; LotRemoveGuard `OK detour LotRouting_RemoveObjectFootprints (lot-0 guard) at 0x00636CF0` (no separate "LotRemoveGuard" summary line); UnseenFallback `OK unseen fallback: deadline 300 ms, ready 0xF ... mode 1`. No routing counters were logged (the crash came before any route ran), so counter evidence is missing.
- SKIP lines (verbatim): `SKIP water jps: code at 0x00620880..0x00621063 differs from Steam 1.67, nothing written` and `SKIP mintcall: EA code at 0x00E5498E (173 bytes) is not the stock Steam 1.67 bytes (another hook in the call path)`. The 7d `door memo x` SKIP did not appear.
- No soak (game gone at the load), no new `ScriptError*.xml`. Next: find which of the 5 sections puts 0x00769111 in the path (bisect by zeroing sections in the rc ini, or disassemble the ts3addr-ea169.h rows for 0x00766120 / 0x00EC0FE0); until then the five routing sections stay off in the EA ini. Logs: `probe-out/ea-7e-1102.*`.

Run 7e2 (rerun of 7e) 16:00 PHT: **PASS.** Cause of 7e: `rtsites.cpp` rebuilt the callee of a `call rel32` site from the Steam bytes baked into `kRtSites[i].pro`; site 18 (`0xEC0FE0`, `E8 4B 92 8A FF`, callee `0x76A230` on EA) became `0x769110`, mid-instruction, hence `PRIV_INSTRUCTION` at `0x769111`. Fix df3f2e5: the displacement now comes from `tr`, the trampoline's copy of the live site bytes (Steam unchanged: same bytes there). No other fixup decodes a rel from a baked array (`decompress.cpp` and `loadprof.cpp` build expected bytes and compare them with the live image first). CI run 41408362293 (native, native-ea169, node green): asi `8266a76191fc`, rc ini `3618cda81a58`, Bin pair backed up as `.pre-7e2` (`c035abe5a3a6` / `8aa37522782a`, restored, sha-checked). Driver as 7e (TS3_LAUNCH=ea TS3_EXE=Sims3Launcher.exe TS3_MACHINE=ea-laptop, `ts3probe.py serve --launch`, `cycle.ts --save Baseline-SV-Spring.sims3`, autopilot speed normal, window never focused). `selectedflags already 001fefff`; agent after 22 s, world loaded 25 s, `LiveModeState`, households 37, Keaton. Soak 10 min, sampled every 30 s on `Sims3Launcher.exe`: WS 2090 -> 2136 MB (peak 2139), threads 144-146, cpu 127 -> 626 s. No crash, no new `ScriptError*.xml`, `quit_game` exited cleanly. Init OK for all 5 sections (RouteGate `25 of 25` + closures on; RouteAsync detours + `route deadline ... ready 0x7`; Discourage deferral; LotRemoveGuard `OK detour LotRouting_RemoveObjectFootprints`; UnseenFallback `ready 0xF`). Final counters [verified]: `route async: mode 1 started 1077 refused 37 collected 1076 dropped 0`, `route plans: off 0 (fails 0) on 1485 (fails 15) hostCalls 13413`, `footprint defer: mode 1 calls 1516 inJob 3 deferred 3 applied 2 collapsed 1`, `unseen fallback: ... marks 994 (refused 0), sync 32 cut 0`. SKIP lines as 7e: `SKIP water jps: ...`, `SKIP mintcall: EA code at 0x00E5498E (173 bytes) ...`. New vs 7d: three handled first-chance `exception C0000005 at 0x04297CC2` (thread 40988, menu phase; not analysed). watchdog.ts printed the stale 13:07 xcpt, not this run. Logs: `probe-out/ea-7e2-1102.*`.

Run 7f (range-guard ports, first live run) 16:21 PHT: **FAIL, the game crashed during startup, before the agent answered; no soak, no save.** CI run 41411835802 (head 76d2558): asi `651adf4c01d3`, rc ini `3618cda81a58`; the installed ini is a copy of it (`e00614eb615a`) with `[FootprintIndex]` Enabled=1 Verify=1, `[LotSinCos]` Enabled=1 Verify=1, `[WaterJps]` Verify=1 (the repo ini is unchanged). Bin pair backed up as `.pre-7f` (`c035abe5a3a6` / `8aa37522782a`, restored, sha-checked); Saves backed up to `game-copy/saves-pre-7f` (robocopy, finished 16:21:00, no save written). Driver as 7e2. [verified] Init lines, all three OK: `OK   water jps: call 0x0062186B -> WaterJumpHook, mode 1 verify 1, callback 0x0066F0B0 inlined`; `OK   footprint index: 0x006678A8, 0x0082B610, 0x00AAAF20 (Mode=1 Verify=1)`; `OK   lot sin/cos cache: 0x00AB71B0 (Mode=1 Verify=1, 256 angles)` (with `OK   detour lot world-to-local (sin/cos cache) at 0x00AB71B0`). SKIP lines: only `SKIP mintcall: EA code at 0x00E5498E (173 bytes) is not the stock Steam 1.67 bytes (another hook in the call path)`; none for the three. Crash: `ACCESS_VIOLATION writing address 0x00000000` at `MSVCR80.dll` 0x5DE273D0 (the log's `exception C0000005 at 0x5DE273D0`), caller 0x5DE27476, then `Sims3Launcher.exe` 0x004DF17F, 0x00D70E17, 0x0076E23E; thread 29076, ecx 0x8FB, edi 0. The dump header says `Free virtual memory: 36 Mb` of 4095 Mb, so this looks like address-space exhaustion during the mod scan (diag: 162 packages, 5181 MB, Saves 17822 MB), not one of the three features [inferred, not analysed; 7e2 loaded the same Mods]. The probe logged `process-terminated` at 16:21:32; `cycle.ts` failed after 60 s with `game start: Sims3Launcher.exe process gone for 60 s`. No fix tried, no retry. Logs: `probe-out/ea-7f-1102.*`, `probe-out/xcpt ALRPANTONIAL1 26-11-02 16.21.24.txt` (the `.mdmp` is 0 bytes).

Run 7f2 (rerun of 7f, instrumented) 16:31 PHT: **the address-space growth did not repeat; the world loaded, no crash.** Same asi `651adf4c01d3` (76d2558) and the exact 7f ini (`e00614eb615a`) with only these `[MemDiag]` keys changed (installed ini `a497181570f0`): `Light=1`, `AllocSites=1`, `AllocSitesSample=64`, `AllocSitesTop=12`, `AllocSitesVirtual=1`; `AllocSitesHeap=0` on purpose (allocsites.cpp hooks the EA heap entries at raw Steam addresses 0x004E69D0/0x004E6A20/0x004E6A70/0x004E5920 and accepts a leading `E9`, so on 1.69 it could hook the wrong code; with the hooks left off the ntdll hooks still see every heap arena). memdiag.cpp and allocsites.cpp have no rows in `ts3addr-ea169.h` / `ts3bytes-ea169.h` (one `unresolved` text match is the header comment, `0 zero`); `MemDiag` is in `ALWAYS_OFF` in tools/ea-ini.ts only by policy ("memexp.cpp struct layouts"), not because of a table row. It works on EA: `mem diag light`, 7 `OK   alloc sites Nt* detour` lines (ntdll), `alloc sites on ... EA heap hooks 0/4, ntdll hooks 7`, `diag: mem` and `allocsites:` lines at the world load and every 5 min. Bin pair backed up as `.pre-7f2`, restored and sha-checked (`c035abe5a3a6` / `8aa37522782a`). Driver as 7e2 (TS3_LAUNCH=ea TS3_EXE=Sims3Launcher.exe TS3_MACHINE=ea-laptop, `ts3probe.py serve --launch --memory-interval 2`, `cycle.ts --save Baseline-SV-Spring.sims3`), plus a scratch guard that read private bytes every 0.5 s, wrote a VirtualQueryEx region summary every +500 MB and was set to capture and kill above 2600 MB (never triggered). Window never focused, no save made (Saves untouched, `saves-pre-7f` not needed). [verified] Private memory: 504 MB at 16:31:35, 1122 MB at :38, 1642 MB at :40 (the same startup ramp, 5 s), then flat at 1700-2030 MB; peak **2032 MB** at 16:33:23 (7f: 3287 MB at about 22 s, 7e2 plateau 1818 MB). Agent after 8 s, "Mod Scripts Found" accepted, world loaded 28 s, `LiveModeState`, households 37, Keaton; 5 min in the world (16:33-16:38), then `quit_game`, process gone within 15 s, no EA relaunch seen. Init lines, all OK as in 7f: `OK   water jps: call 0x0062186B -> WaterJumpHook, mode 1 verify 1, callback 0x0066F0B0 inlined`; `OK   footprint index: 0x006678A8, 0x0082B610, 0x00AAAF20 (Mode=1 Verify=1)`; `OK   lot sin/cos cache: 0x00AB71B0 (Mode=1 Verify=1, 256 angles)` (+ `OK   detour lot world-to-local (sin/cos cache) at 0x00AB71B0`). Not covered: no `footprint:` line (no save), routing counters all 0 (`route async: ... started 0`; the sims were not made to route, autopilot speed was not set), the three features print no Verify counters at exit, so "0 mismatches" is only that the log has no mismatch or `FAIL` line. Baseline owners at the world load (`allocsites: #1 load`, virtual 1740 MB in 474 blocks, mapped 130 MB): 425 MB virtual with no recorded caller (`at=-`, 222 blocks: reserved before the hooks), 371 MB at 0x00D70916 (Boehm `GC_win32_get_mem`, the GC heap, 76 blocks), five 80/80/64/48/32 MB blocks at the EA allocator 0x004E50E7, d3d9.dll (DXVK) 82 MB of file views; `diag: mem #1` priv 1966 MB, `#2` (5 min) 2009 MB, free 1406 MB, largest free 1304 MB. Verdict: no owner named, because there was no extra memory to attribute. The 7f spike (+1.5 GB in about 14 s, in the Mods scan before the agent answered) is either nondeterministic (a race between the launcher's own startup and the mod scan) or depends on something this run changed (MemDiag Light and the ntdll hooks load before the scan, the sampler runs at 2 s); one clean run cannot separate these. Open: if 7f recurs, the guard in the scratchpad (private > 2600 MB, region summary, kill) is the capture; MemDiag samples only at load, save, exit and every 5 min, so it cannot see a startup ramp. Logs: `probe-out/ea-7f2-1102.*` (`.vm.txt` region summaries, `.vm.txt.csv` 0.5 s private series, `.ts3native.log`, `.drive.log`), samples `probe-out/20261102-163118/memory.csv`.

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

## Range-guard ports (2026-11-02, static only)

Method for each guarded range: decode the Steam 1.67 range and its table-mapped 1.69.47 range (`probe-out/ts3-ea169-file.exe`)
with iced-x86, function by function, jump tables compared as data. Every instruction must have the same opcode and length;
a byte difference is accepted only where it is a branch/call target that maps through the range, an absolute data pointer
whose target holds the same bytes (strings, float constants), or a call out of the range whose callee has the same
instruction shape on both builds. No game run.

### WaterJps on 1.69 [verified, static]

Verdict: **relocation only.** The Steam range `0x61F750..0x61FF93` holds the jump's seven functions plus one heap sift-up
helper at `0x61F830` (0x60 bytes) that is not on the jump's path (nothing in the range calls it); EA links that helper
elsewhere, so the mapped EA range `0x620880..0x621063` is 0x60 shorter and holds only the seven. Those seven are the same
code: 0 bad rows; the only differing bytes are the two `jmp [eax*4+table]` operands and their 8-entry jump tables, which
map entry for entry. The near-callback `0x66DC50` -> `0x66F0B0` (0x60 bytes) is byte-identical (FNV `0x585FD97D` on both).
The caller (search `0x620560` -> `0x6215D0`) has the same shape; its differences are data pointers (profiler names, floats
1.0/2.0, same bytes) and calls out. The patched site `0x6207FB` -> `0x62186B` is `E8` to `0x620EE0` (= mapped `0x61FE10`);
`PatchCalls` checks that at run time and writes only the rel32. Change: under `TS3A_TABLE` the expected range hash is the
EA one, `0x201847A4` (Steam stays `0x5A2C1E2B`); the SKIP line now prints the measured hash. `[WaterJps]` added to the
minimal EA ini's `ALLOW` list (it is `Enabled=1` in the Steam ini). Next smoke must show
`OK   water jps: call 0x0062186B -> WaterJumpHook, mode 1 verify 0, callback 0x0066F0B0 inlined`.

### FootprintIndex on 1.69 [verified, static]

Verdict: **relocation only.** `LotLevel_SaveFootprints` `0x6663C0..0x666740` -> `0x6677E0..0x667B60`, same length 0x380:
0 bad rows; the differing bytes are 4 data pointers (profiler name `Routing/PathPlanner/FootprintMapEntrySav...` and floats,
same bytes on both) and 26 calls out to 9 callees, each with the same instruction shape on both builds (incl. the save-index
call `0x62B1E0` -> `0x62C5A0`). The map layout the stub relies on ([L+0x20]+0x24, node +8/+0x10) is in those identical
instructions. Patched sites: `0x666488` -> `0x6678A8` holds `8B 53 20 8B 4A 24` (ts3bytes row ok); `0x82C940` -> `0x82B610`
is `E8` to `0x63DC80` (= mapped `0x63C690`, same shape), `0xAAC320` -> `0xAAAF20` is `E8` to `0xC6C7E0` (= mapped
`0xC6D460`, same shape), same surrounding instructions. Change: under `TS3A_TABLE` the expected hash is `0xCA5029BC` (Steam
`0xF2CDC1C1`); the SKIP line prints the measured hash. Already in `ALLOW_ON`. Next smoke must show
`OK   footprint index: 0x006678A8, 0x0082B610, 0x00AAAF20 (Mode=1 Verify=0)` and, after a save, the per-save `footprint:` line.

### LotSinCos on 1.69 [verified, static]

Verdict: **relocation only.** `Lot_WorldToLocal2D` `0xAB8540` -> `0xAB71B0`, whole body to `ret 10h` (0x11C bytes): one
differing instruction, the `movss xmm2,[-0.0 constant]` operand (`0xF988F0` -> `0xFE29F0`, both `00 00 00 80`). The fields our
replacement reads (lot `+0x190`, `+0x198`, `+0x1A0`), the x87 fsin/fcos order and the packed SSE operand order are the same
bytes, so the bit-exact claim carries over. No range hash in this feature; the 6-byte prologue `55 8B EC 83 E4 F0` is a
ts3bytes `ok` row. No code change; `[LotSinCos]` added to `ALLOW_ON` (minimal EA ini, `Enabled=1` forced). Next smoke must
show `OK   lot sin/cos cache: 0x00AB71B0 (Mode=1 Verify=0, 256 angles)`; a run with `Verify=1` would confirm bit-exactness live
(`lotsincos MISMATCH` lines = stop).

### AllocSites heap hooks on 1.69 [verified, static]

The four heap entries were already `TS3A` (since 4f097f5; the Run 7f2 note "raw Steam addresses" is stale): 0x4E69D0 /
0x4E6A20 / 0x4E6A70 / 0x4E5920 -> 0x4E6C30 / 0x4E6C80 / 0x4E6CD0 / 0x4E5B80, all four with the same 10-byte prologue on
1.69. The raw literal was the `.text` range (0x401000..0xF95000, -> ..0xFDF000), and the guard took any leading `E9`. Now
an entry must match the prologue (`Matches`) or be [Alloc]'s jmp into this module. Other raw literals the scanner had
marked `unsure` (page-aligned or a size-like name) are wrapped with overrides in `docs/data/ea169-overrides.csv`.
`AllocSitesHeap=1` is static-safe on EA; not yet run.

Run 7g (7f ini, routing, save) 16:41 PHT: **PASS** [verified]. The "stop 7g" message arrived at 16:53, after the run had already finished (20 samples, ended at 16:53:53), so this is the full result, not a partial one. Asi `651adf4c01d3` (76d2558), ini `e00614eb615a` (the 7f ini, `[MemDiag]` off), Bin pair backed up as `.pre-7g`, restored and sha-checked (`c035abe5a3a6` / `8aa37522782a`). Driver as 7e2 plus `autopilot {enabled:true,speed:"normal"}`; the first `set_speed` was refused (a dialog was open), the later one answered `Normal`. `ts3probe.py serve --launch --memory-interval 2` and the scratch VM guard (private > 2600 MB: region summary, kill; never triggered). World loaded 16:43:12, 10.7 min in the world, one `save_game {"name":"ea7g-scratch"}` at 16:48:22 (autopilot off, speed pause; finished in under 30 s, `save_in_progress` false, 39 households), then autopilot and speed normal again. Private memory (guard series): 1620 MB at 16:41:58, 2010 MB at the world load, then 1979-2048 MB flat; peak **2054 MB** at 16:48:36 (about 14 s after the save); the 7f growth (3287 MB) did not repeat, as in 7f2. No crash, ScriptError xml 1 before and after, clean `quit_game` (process gone in 5 s). Init lines as 7f2: `OK   water jps: call 0x0062186B -> WaterJumpHook, mode 1 verify 1, callback 0x0066F0B0 inlined`; `OK   footprint index: 0x006678A8, 0x0082B610, 0x00AAAF20 (Mode=1 Verify=1)`; `OK   lot sin/cos cache: 0x00AB71B0 (Mode=1 Verify=1, 256 angles)`. Counters at exit, verbatim: `route async: mode 1 started 698 refused 26 collected 697 dropped 0 job 11639 ms max 142076 us pump 3597 ms (324 waits)`; `route plans: off 0 (fails 0) on 1010 (fails 6) hostCalls 10016`; `footprint defer: mode 1 calls 1042 inJob 3 deferred 3 applied 2 collapsed 1 dup 0 ageMax 31 ms applyInJob 0`; `unseen fallback: mode 1 deadline 300 ms, cut at 220 ms sync / 220 ms job (...); marks 662 (refused 0), sync 22 cut 0 (...), jobs 584 cut 0, cut answers 0, longest 142 ms, >= setting 0`. Save line (FootprintIndex save path): `footprint: levels 299 entries 102379 matched 102375 calls 92 ms 586 verify ok (mode 1 fallbacks 0 worldSave ms 10488)`: verify ok, but 4 of 102379 entries are not "matched" (not analysed; no mismatch word in the log). Verify results for the other two: WaterJps and LotSinCos print no Verify counters (init lines only), so "0 mismatches" for them is only the absence of a mismatch or `FAIL` line. SKIP lines: only `SKIP mintcall: EA code at 0x00E5498E (173 bytes) is not the stock Steam 1.67 bytes (another hook in the call path)`. Saves restored: `ea7g-scratch.sims3` deleted, a robocopy `/MIR /L` against `game-copy/saves-pre-7f` lists 0 differences. Logs: `probe-out/ea-7g-1102.*` (`.vm.txt.csv` 0.5 s private series, `.ts3native.log`, `.soak.log`).

Run 7h (head 7f6f7f8) 16:55 PHT: **FAIL, the game crashed in the world about 1.5 min after the load; stopped, no fix tried.** CI run 41415700157 (green) artifact in `game-copy/ci-ea169-7f6f7f8/`: asi `05c1f1ecbf2c` (sha256 `05c1f1ecbf2c1fa1fcd1f9d2a488e989c336a722a2f1a32ae930f799a2f0eafe`); the artifact's `ts3native-ea169-rc.ini` is byte-identical to the 76d2558 one (`3618cda81a58`), so the 7f edits (`[FootprintIndex]`/`[LotSinCos]` Enabled=1 Verify=1, `[WaterJps]` Verify=1) give the same ini as 7f, sha `e00614eb615a`, `[MemDiag]` off. Procedure as 7g (2 s sampler, 2600 MB guard, autopilot normal). World loaded 16:56:31; private 2049 / 2023 / 2051 MB at +30/+60/+90 s (peak 2057 MB, no growth); the process was gone at the 16:58:33 sample (so between 16:58:03 and 16:58:33), before the save step (planned at +5 min). Log tail [verified]: `exception C0000005 at 0x00AD04EC (info 00000000 00000024) thread 44324 eip 00AD04EC esp 4035EBD4; stack code refs: 00AB7CAC 00AB7E1F 5D48C468 5D32443E 00C67CB5 00C69FEB 0095C8BC 0095DF7B`, eax 0, ecx 0: a read at 0x24 through a null pointer. The asi wrote its exit-style counters afterwards (`route async: mode 1 started 79 ... collected 78`, `route plans: off 0 (fails 0) on 212 (fails 5)`); no new `xcpt` txt or mdmp appeared in `probe-out` (newest is the 7f one). The two lowest stack refs 0x00AB7CAC / 0x00AB7E1F lie 0xAFC / 0xC6F above the LotSinCos detour site 0x00AB71B0 (`lot world-to-local`) [inferred from addresses only, nothing analysed: neighbouring code could just as well be a caller]; 7g with the 76d2558 asi and the same ini did not crash in 10.7 min, so the cause is either the 7f6f7f8 changes (15 wrapped raw addresses, allocsites guard) or a rare race; one crash and one pass cannot separate them. Init lines compared with 7g: **none changed** (diff of all `OK`/`SKIP`/`FAIL`/`disabled` lines is empty): the new mappings ([Alloc], unseen, rtoro, monothread, gcroots) belong to sections that are `Enabled=0` in the rc ini, so they log nothing; nothing in this run tests them. No `footprint:` line, no save (crash first), Verify counters not reached, SKIP lines: only the `mintcall` one as in 7g. Bin pair restored from `.pre-7h` (`c035abe5a3a6` / `8aa37522782a`, sha-checked), Saves untouched (0 robocopy differences, no save made). Logs: `probe-out/ea-7h-1102.*` (`.ts3native.log`, `.vm.txt.csv`, `.soak.log`).
/^Run 7i (identical/i
Run 7h trigger analysis (read-only, after the fact) [verified from logs unless marked]: the harness sent nothing between the world load (16:56:31) and the crash (xcpt `16.58.13`): `drive.log` ends at 16:56:31, `soak.log` only holds the 30 s samples, no `ScriptError*.xml` (count 1 before and after; newest is 12:11), no dialog or travel line in `ts3native.log`, and the autopilot reply (cleared the "new towns" dialog) is the same as in 7g. The `mode LiveMode -> None, family Keaton -> none at +97 s` line (16:58:08) is the game leaving the world on its own, before the exception: the 2 s memory series drops at 16:58:06 (mapped files 164 -> 118 MB, free +48 MB) and then repeats the same values until the crash at 16:58:13 (the game stopped answering for about 7 s). The access violation (`[lot+0x290]` null, read at 0x24) is therefore a lookup on a lot that the world teardown had already cleared [inferred]. What made the game leave LiveMode is not in any log; the unseen lines before it are plain (`unseen fallback ... marks 96, sync 5 cut 0, jobs 67 cut 0, longest 64 ms`), and 7g at the same point stayed in LiveMode until the harness quit at +647 s. Correction to the 7h paragraph: an `xcpt ALRPANTONIAL1 26-11-02 16.58.13.txt/.mdmp` was written (the earlier "no new xcpt" was wrong).
Run 7i (identical rerun of 7h) 17:05 PHT: **PASS**, no crash, no unrequested mode exit [verified]. Same asi `05c1f1ecbf2c` and ini `e00614eb615a` (no build or config change), Bin pair backed up as `.pre-7i`, driver `run7h.sh` with tag 7i (2 s sampler, 2600 MB guard, 7e2 routing, autopilot normal, window never focused). World loaded 17:06:43, 10.7 min in the world, one harness `save_game {"name":"ea7i-scratch"}` at 17:11:51 (done by 17:12:17, 39 households), then clean `quit_game` (process gone in 5 s). The only `mode` lines: `None -> LiveMode at +2 s` and `LiveMode -> None at +645 s` (the quit); nothing at +97 s, so the 7h exit did not repeat (one crash, one pass on the same build and ini: a rare race, not a deterministic fault of 7f6f7f8; cause not found). Unseen fallback at exit: `marks 588 (refused 0), sync 11 cut 0, jobs 501 cut 0, cut answers 0, longest 129 ms, >= setting 0` (the classification with 0x959710 now sees seen and unseen Sims). Routing counters at the last dump: `route async: mode 1 started 656 refused 12 collected 655 dropped 0`, `route plans: off 0 (fails 0) on 972 (fails 8) hostCalls 8364`, `footprint defer: mode 1 calls 1028 inJob 1 deferred 1 applied 1 collapsed 0`. Save line: `footprint: levels 299 entries 102413 matched 102410 calls 92 ms 645 verify ok (mode 1 fallbacks 0 worldSave ms 10274)` (3 of 102413 not matched, as 7g's 4 of 102379). Peak private **2092 MB** (guard series; 7h 2057, 7g 2054; never near the 2600 MB guard). ScriptError xml 1 before and after. Bin pair restored (`c035abe5a3a6` / `8aa37522782a`, sha-checked), `ea7i-scratch.sims3` deleted, robocopy `/MIR /L` against `game-copy/saves-pre-7f`: 0 differences. Logs: `probe-out/ea-7i-1102.*`.
Run 7j (7i plus managed 1.69 mods, MonoThread, AutShadow) 17:40 PHT: **FAIL, the game crashed at start, about 20 s after the launch, before the agent answered and before any world load; stopped, no fix tried** [verified from the logs]. Setup: asi `05c1f1ecbf2c` (7f6f7f8), ini `cb9c88e481fa` = the 7i ini (`e00614eb615a`) with exactly three keys changed: `[RouteSnap] Count=0 -> 1` (AutShadow's change tracking; without it the init line says PART), `[MonoThread] Enabled=0 -> 1` (Mode=0, GcLock=0, FrameTls=0 as shipped; AutShadow's cut rides its Simulate slot stub), `[AutShadow] Enabled=0 -> 1` (Mode=0, count only). Managed: ts3fixes `a82ad09c0461` and ts3agent `dd7a7cd67f52` (docs/235 §8 packages, `build/ea169`), script cache removed. The managed `autshadow` switch is not an ini key: it is set with the agent command `patches {"autshadow":true}` (tuning `kAutShadow` is off), planned from a watcher as soon as the agent answers, before `load_save`; the crash came first, so it was never sent. Native log, init lines [verified]: `OK   mono thread: [0x011EA8AC] 0x00E4EFE0 -> MtPushOtherRoots, Simulate slot 0x010D60A8 hooked (Mode=0, GcLock=0, FrameTls=0)`; `OK   autonomy shadow: Guard=all, 1776 IC slots guarded (39 in front of routesnap's stubs, 0 left), 44 of 45 descriptor ICs, F2 on, change tracking from routesnap's World_FireEvent detour; Mode=0 (count only), Sample=4, WarmIn=20, Ledger=0`; SKIP lines: only `SKIP mintcall: EA code at 0x00E5498E (173 bytes) is not the stock Steam 1.67 bytes (another hook in the call path)`. Crash [verified]: `exception C0000005 at 0x00E824B2 (info 00000000 00000000) thread 3516 eip 00E824B2 esp 3EF0FE74; stack code refs: 00E5A790 00E534C0 00E52590 00E53790 00E51FF0 00E53940 00E5A650 00E530C0`, registers eax 010DE554 ebx 00000729 ecx 0 edx 00030553 esi 0 edi 000001BB ebp 010DE554: a read through a null pointer, in the game's Mono code (the game's xcpt `17.40.30` call stack: 0x00E824B2, 0x00E5A790, 0x00D7E9D1; thread 3516 is not the thread that printed the earlier `thread 44832 (main)` decompress line) [inferred: the address lies just below `mono_add_internal_call` 0x00E82550 used by AutShadow's IC registration; nothing analysed, it could equally be Mono work on the new script packages]. The last native log lines before it are the `diag:` package scan (162 packages, 9 with scripts, `ts3agent.package`, `ts3fixes.package` among them) and `diag: header done in 365 ms`; the crash is therefore during startup script loading, with both changes (the three native keys and the new managed packages) in effect at once, so this run cannot say which one causes it. The new managed mods with the baseline native pair (docs/235 §8) and the 7i native with the old managed mods (7i) both started clean; MonoThread and AutShadow have never run on EA before, so the three native keys alone are untested. Not reached: world load, G-M6 sweep (c[602], c[605], c[606] not read), routing counters, footprint save, the 10 min in the world. Memory: peak private **1118 MB** (2 s sampler, the process was gone at 17:40:37, never near the 2600 MB guard). ScriptError xml 1 before and after. Bin pair restored from `.pre-7j` (`c035abe5a3a6` / `8aa37522782a`, sha-checked), `ts3fixes.package` `ac2c3f8799af`, `ts3agent.package` `1921c00c4c14`, `scriptCache.package` `db69b1b6a862` restored, Mods listing identical (174 files), Saves robocopy `/MIR /L` against `game-copy/saves-pre-7f`: 0 differences (no save made). Logs: `probe-out/ea-7j-1102.*` (`.ts3native.log`, `.xcpt.txt`, `.xcpt.mdmp`, `.drive.log`, `.vm.txt.csv`, `.soak.log`). Next (not done): rerun with the new managed mods and the 7i ini, then with the 7i native keys changed one at a time, to see which change faults at start.
Run 7j2 (7j rerun with the AutShadow alignment fix) 17:52 PHT: **FAIL, the game crashed in the world about 90 s after the load, no second fix tried** [verified from the logs]. Fix 15697ea (`fn & 15` check in `AsInstallGuards`): the bogus 0x10FF6BC pair is gone, the startup crash of 7j (0x00E824B2) did not repeat. CI run 41423801892 (native, native-ea169, node green), asi `44b413fa7c35`, ini `cb9c88e481fa` (the 7j ini), same ts3fixes `a82ad09c0461` / ts3agent `dd7a7cd67f52`. Init lines [verified]: `OK   mono thread: [0x011EA8AC] 0x00E4EFE0 -> MtPushOtherRoots, Simulate slot 0x010D60A8 hooked (Mode=0, GcLock=0, FrameTls=0)`; `OK   autonomy shadow: Guard=all, 1775 IC slots guarded (39 in front of routesnap's stubs, 0 left), 44 of 45 descriptor ICs, F2 on, ...`; SKIP lines: only the `mintcall` one of 7j. The agent answered at 17:52:58, `patches {"autshadow":true}` verified, `cycle.ts` loaded Baseline-SV-Spring (rc 0, 17:54:02), autopilot on. Crash [verified]: `exception C0000005 at 0x00AD04EC (info 00000000 00000024) thread 37912 (sim) esp 321DEBD4; stack code refs: 00AB7CAC 00AB7E1F 5D48C468 5D32443E 00C67CB5 00C69FEB 0095C8BC 0095DF7B`, registers eax 0 ecx 0 (read of [0+0x24]), the xcpt call stack 0x00AD04EC, 0x00C69FEB, 0x0095C8BC; two refs (5D48C468, 5D32443E) lie inside the asi (module 0x5D320000-0x5DA98000, offsets 0x16C468 and 0x4443E) [inferred: a hooked path is in the sim thread's stack, not analysed]. Last log lines before it: `ts3fixes: action: mode LiveMode -> None, family Keaton -> none at +88 s`, route plans on 190 (fails 1). Not reached: c[602]/c[605]/c[606] read, routing counter read, footprint save (sample 10), 10 min in the world; the footprint save never ran, so no verify line. Memory: peak private **2083 MB** (samples 2058, 2083, 2074 MB; guard 2600 MB not hit). ScriptError xml 1 before and after. Bin pair, ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, scriptCache `db69b1b6a862` restored and sha-checked; Mods 174 files; Saves robocopy `/MIR /L` against `game-copy/saves-pre-7f`: 0 differences. Logs and dump: `probe-out/ea-7j2-1102.*` (`.ts3native.log`, `.xcpt.txt`, `.xcpt.mdmp`, `.soak.log`, `.drive.log`). Next (not done): analyse 0x00AD04EC and the two asi return addresses; then isolate by turning [MonoThread] / [AutShadow] off one at a time.
Run 7j3 (7j2 rerun with the move dialogs declined) 18:10 PHT: **no crash and no unrequested mode exit in 10 min in the world; the G-M6 numbers (c[602], c[606]) do not match the doc 235 §7 expectation** [verified from the logs]. Change: ts3agent 04a5609 (`9abf3308fc89`; CI run 41426373659 green) adds a `kDialogRules` rule for "new towns available" / "long-distance move" / "preparing to move" (declining words: not now, cancel, later, decline, no) and logs every autopilot `title -> clicked` pair through a DiagLine at once. Everything else as 7j2: asi `44b413fa7c35` (15697ea), ini `cb9c88e481fa`, ts3fixes `a82ad09c0461`, script cache cleared, switch `autshadow` verified before the load, `cycle.ts --save Baseline-SV-Spring.sims3` (rc 0, 18:12:59), autopilot normal, 20 samples of 30 s, one `save_game ea7j3-scratch` at 18:18:13 (autopilot off and paused around it; done within 27 s, 39 households), `quit_game` 18:23:51, process gone after 10 s. Routing was not driven by commands, as in 7e2 (natural play at normal speed). Click log, the only line in the run [verified]: `ts3fixes: ap click: There are new towns available for your Sims to live in! You can always perform a -> #3 no no` (ts3native.log line 454, right after `action: mode None -> LiveMode at +4 s`). "Your Sim family is preparing to move" did not appear. The only mode exit is the requested one: `action: mode LiveMode -> None ... at +661 s` after `quit_game`. Init [verified]: `OK   mono thread: ... Simulate slot 0x010D60A8 hooked (Mode=0, GcLock=0, FrameTls=0)`; `OK   autonomy shadow: Guard=all, 1775 IC slots guarded (39 in front of routesnap's stubs, 0 left), 44 of 45 descriptor ICs, F2 on, ...`. SKIP/WARN lines of the native log: only `SKIP mintcall: EA code at 0x00E5498E (173 bytes) is not the stock Steam 1.67 bytes (another hook in the call path)`. ts3fixes applied log (172 lines), WARN/accept lines verbatim: `AutShadow: LocalizeString compiled before ts3fixes loaded (another mod or the game localized first)`; `AutShadow: not patched: Localization.LocalizeString(String,Object[]), Localization.LocalizeString(Boolean,String,Object[]); Modes 1/2 off, Mode 0 (count only) still runs`; `AutShadow: game modules not on the content accept list: ScriptCore b0b126a630762ef5, SimIFace 0c30697e7c8693f2, Sims3Metadata c7208a6e6a2469bb, Sims3StoreObjects afea6512f05f5684, UI c78716f1eb0191f3: G-M6 checks their closure and hierarchy methods per definition (docs/218 §5)` (the line is present); also `AutShadow: G-M4 sweep: 683 of 688 closure methods found`, `G-M5 skips Sims3.Gameplay.Core.VisitCommunityLot+Definition (derived: ...VisitCommunityLotLabLecture+Definition)`, `H11 shelter answer off (rain pairs abort)`. Counters (agent `patches`, end of run): c[602] `autshadow_gm6_methods` = **683** (expected about 3568), c[605] `autshadow_gm6_faults` = 0, c[606] `autshadow_gm6_owners_skipped` = **0** (expected one skip group, CreateTooltipCallback), `autshadow_gm6_mismatch` = 0, `autshadow_gm6_cha_methods` = 376, `autshadow_content_modules` = 7, `autshadow_content_unknown` = 5, `autshadow_gm4_methods` = 683, `autshadow_requests` = 497, `autshadow_exceptions` = 0. Routing counters [verified]: `route async: mode 1 started 771 refused 24 collected 771 dropped 0`; `route plans: off 0 (fails 0) on 1133 (fails 21) hostCalls 10873`; `footprint defer: mode 1 calls 1193 inJob 5 deferred 6 applied 5`; `unseen fallback: mode 1 ... marks 735 (refused 0), sync 21 cut 0`; footprint save `footprint: levels 299 entries 102349 matched 102345 calls 92 ms 867 verify ok (mode 1 fallbacks 0 worldSave ms 19059)`. Memory: peak private **2096 MB** (guard series; sampler 2032-2092 MB, WS up to 2191 MB, 148 threads; guard 2600 MB not hit). ScriptError xml 1 before and after. Bin pair `c035abe5a3a6` / `8aa37522782a`, ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, scriptCache `db69b1b6a862` restored and sha-checked; Mods 174 files; Saves mirrored from `game-copy/saves-pre-7f` (the run's `ea7j3-scratch` save removed), robocopy `/MIR /L`: 0 differences. Logs: `probe-out/ea-7j3-1102.*`. **Reading of 7h and 7j2:** both ended with an exit `LiveMode -> None` that nobody asked for, and the harness autopilot was accepting EA's long-distance move dialogs (no `kDialogRules` rule, so `Score()` took the accept button) [inferred: the accept click is what left LiveMode; the 7j2 log has the first dialog but no click log, because the log line did not exist yet]. With the dialog declined, the same build runs 10 min clean. **Open question:** whether vanilla 1.69 (no ts3native, no ts3fixes) also crashes at 0x00AD04EC when a move is accepted in the world, i.e. whether the crash is EA's or ours. Settling it needs a control run with the asi and mods off and the move accepted; Rodney must approve it first. The G-M6 deviation (c[602] 683, no skip group, no CreateTooltipCallback miss) is not analysed: it looks like the §6 hash set differs from the loaded modules (5 of 7 content modules unknown), see doc 235 §7.
Run 7m (LotSinCos register fix, vh-move with the mod) 01:01 PHT 2026-11-03: **PASS: the household moved, no crash in the 10 min after the move, `edxClobber 0`, no Verify mismatch** [verified from the logs]. Cause (static triage, [verified asm]): Lot_WorldToLocal2D (Steam 0xAB8540, 1.69 0xAB71B0; byte-identical apart from the -0.0 constant address, `ret 10h`) writes only eax, ecx, xmm0-xmm3, the x87 stack (balanced) and EFLAGS, so it keeps edx, ebx, esi, edi, ebp and xmm4-xmm7. Its callers (1.69 0xAB7C80 with return 0xAB7CAC; Steam 0xAB9037, 0xAC0C0F, 0xAC586B, 0xAC996D) keep the lot in edx across the call. The detour went straight to the C++ `LscHook`, which on a cache miss returned with edx = `&g_lsc[slot]` (Verify=0: hits too). That is the 0x00AD04EC crash after the long-distance move of 7h and 7j2 (7j2 dump: lot = `&g_lsc[201]`). Fix 3949b38: the detour enters the naked `LscStub`, which saves edx and xmm4-xmm7 around `LscHook` and returns with `ret 10h`. An always-on exit check compares edx with a second copy of the entry value (`lotsincos EDX CLOBBER` lines, first 10, with the caller return address and hit/miss), and the 60 s counter dump prints `lot sin/cos: ... edxClobber N`. The count is a file static, not a Ts3nCounters slot, because this branch is behind main's v147-v158. Selftest: the stub keeps edx, xmm4 and xmm7 on a miss and on a hit, with and without Verify. Set-up: the 7i ini, the normal installed ts3fixes, ts3agent b58ab59, script cache cleared, real python first on PATH, session preflight `session 2 Active, console held by our session`, Saves backed up and mirrored back, window never focused. Driven by a scratch driver (as 7h-7j3) that runs `cycle.ts --save Baseline-SV-Spring.sims3`, then the vanilla-matrix vh-move step `vanilla-steps.ts accept --title "new towns available for your sims to live in" --minutes 6 --all-seconds 180 --after 10`. queue.ts was not used: in mod mode it writes the Steam ini plus overrides and checks the Steam build DB, so it cannot run the EA ini. Result: init `OK   lot sin/cos cache: 0x00AB71B0 (Mode=1 Verify=1, 256 angles)`; offer shown 01:03:47, accept_all clicks `#2 yes`, `#2 accept`, `#2 yes`, `#2 save`, `#3 accept`; moved (out of the world) at 01:04:28, Isla Paradiso loaded (world load #2), the game then sat in FamilyPicker (the placement pick is not clicked, as on vanilla) until the quit; `PASS: moved (out of the world), game up 10 min after the move, no crash`. Last counter line `lot sin/cos: mode 1 callsK 2237 hitsK 2236 mismatches 0 edxClobber 0`; 0 `EDX CLOBBER` lines, 0 `lotsincos MISMATCH` lines; no new xcpt; ScriptError xml 1 before and 1 after. Restore verified: Bin pair c035abe5a3a6 / 8aa37522782a, ts3fixes ac2c3f8799af, ts3agent 1921c00c4c14, scriptCache db69b1b6a862, Options.ini 50cb641832f8, Saves identical to the backup (2602 files), no game process left. One clean run; a second clean run is needed to call the crash fixed (7h's crash also did not repeat in 7i).

Run 7n (LotSinCos register fix, second clean vh-move run with the mod) 01:17 PHT 2026-11-03: **PASS: the household moved, no crash in the 10 min after the move, `edxClobber 0`, no Verify mismatch** [verified from the logs]. Same driver, installed files, ini and flow as 7m (only the run label and output paths changed); session preflight `session 2 Active, console held by our session`, Saves backed up first, window never focused. Init `OK   lot sin/cos cache: 0x00AB71B0 (Mode=1 Verify=1, 256 angles)`; offer shown 01:20:25, moved (out of the world) at 01:21:35 (70 s after the offer; 7m: 41 s); `PASS: moved (out of the world), game up 10 min after the move, no crash`. Last counter line `lot sin/cos: mode 1 callsK 471 hitsK 470 mismatches 0 edxClobber 0`; 0 `EDX CLOBBER` lines, 0 `lotsincos MISMATCH` lines; no new xcpt; ScriptError xml 1 before and 1 after. Restore verified: Bin pair c035abe5a3a6 / 8aa37522782a, ts3fixes ac2c3f8799af, ts3agent 1921c00c4c14, scriptCache db69b1b6a862, Options.ini 50cb641832f8; the game had rewritten Baseline-SV-Spring.sims3 (Meta.data, .nhd), so Saves were mirrored back from the backup and are identical to it; no game process left. With 7m this is the second clean run: the project rule for calling the LotSinCos move crash fixed is met.

## Run 8: ea-mod-matrix through queue.ts (2026-11-03)

This is the first EA run with our mod driven by `queue.ts` itself, with no scratch driver. The harness change is draft PR #226 (branch `ea-mod-mode`, 19b93b6): the edition is read from the exe (PE stamp `0x6707155C` + the 1.69.47 sha256), asi `"tested"` = `TS3_NATIVE_DIR/ts3native-ea169.asi` and the base ini = `ts3native-ea169-rc.ini`, and the log has to carry `host build: EA app 1.69.47`.

Set-up:
- Queue `tools/harness/queues/ea-mod-matrix.json`: the vanilla-matrix jobs except hf-kill, in mod mode. Each runs asi `"tested"` with `releaseDefaults` and `callSites: false`, so the shipped ini runs as is.
- Native build: `game-copy/ci-ea169-d91856d`, asi `119f5d400f2a` (ts3native 0.36.152, includes the LotSinCos fix 3949b38).
- ini `ts3native-ea169-rc.ini`. Features on: GC, RouteAsync, SaveWrite, Decompress, WaterJps, UserDiag, RouteGate, Discourage, LotRemoveGuard, OptsInit, UnseenFallback. LotSinCos is off, as shipped.
- Managed packages: `cycle.ts --restart` installed the ea-mod-mode tree's `build/`: ts3fixes `ed863ae4e462`, ts3agent `859b4e337910`.
- Env: `TS3_LAUNCH=ea TS3_EXE=Sims3Launcher.exe TS3_MACHINE=ea-laptop`, real python first on PATH, session preflight before each job, each job run with `--only` in the background. The window was never focused. Saves were backed up first and mirrored back after each job.

| job | result | notes |
|---|---|---|
| vh-move (1st try) | not run | `cycle.ts --restart` exit 1: the fresh worktree had no `build/ts3-nointro.package`. Environment slip, game never started, not counted. The package was copied from the main clone (`2d4394daca11`, same as installed). |
| vh-move | **FAIL** (13.4 min) | The flow passed: `PASS: moved (out of the world), game up 10 min after the move, no crash`, 0 new script errors. The build check passed: asi 119f5d400f2a, ts3native 0.36.152, log version and EA host line ok, ts3fixes tuning e4fe8e1b7b0f matches the build. The one FAIL is the save guard: `baseline save changed: Baseline-SV-Spring.sims3 9ded84456f8254e2 -> 4b1931cc528e9d0f`. |
| vh1-soak, vh-cycle-a, vh-cycle-b, vh-travel-matrix, vh-travel-trip, vh-newsave, vh-matrix, vh-repro, hf-statecmd | not run | The matrix stopped at the first FAIL (rule: stop, triage, no second attempt). |

Triage (static, from the row and logs) [verified for the clicks and hashes; cause inferred]:
- The accept_all flow clicked `#2 save save (accept requested)` in the move dialogs, and the game saved over the loaded Baseline save. 7m and 7n clicked the same `save` button and rewrote Baseline-SV-Spring too; that is why their Saves were mirrored back. Those drivers had no save guard. The vanilla Round 8 PASS clicked `#3 don't save` at that point and its hashes stayed equal.
- No crash, no xcpt. The native log has 83 `OK` lines, 0 `FAIL` lines, and only the known `SKIP mintcall` line.
- Suspect: the vh-move accept_all flow (it accepts the save offer), not a mod feature [inferred].
- Not done: a fix (for example, have the move flow answer the save offer with "don't save", or save to a scratch copy) and the remaining nine jobs.

Logs: `probe-out/queue/ea-mod-matrix-2026-11-02*` in the ea-mod-mode worktree (jsonl row, job log, `-vh-move-2240-ts3native.log`) and `probe-out/ea-eamod-1103.*` (driver log, pre/post state).

Restore verified:
- Bin pair `c035abe5a3a6` / `8aa37522782a` (queue.ts bin-before read-back).
- ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, ts3-nointro `2d4394daca11`, scriptCache `db69b1b6a862`, Options.ini `50cb641832f8`, LoadOption.ini `e2dccbd46afd`.
- Mods listing identical (174 files).
- Saves robocopy `/L` against the backup: 0 differences (2602 files).

### Run 8b: rerun after the save-offer fix (2026-11-03)

Why the save was accepted: mod mode and vanilla used the same dialog rule (`dialog_expect accept_all`, rules off). In AutoDialog, "save" and "don't save" both score 1, so the button listed first got the click. Vanilla Round 8 happened to get "don't save"; Run 8 got "save". Fix on PR #226 (45f2284): accept_all passes `decline_save`, and any dialog with an enabled "don't save" button gets that button (logged `save declined`). All other move-flow dialogs are accepted as before. The pick is in `DialogPick.cs`, with an offline self-check in `tools/selftest/dialogpick`. selftest-all: 191 PASS, 0 FAIL. CI is green.

Same set-up as Run 8, with the fixed ts3agent built by `cycle.ts --restart` from the ea-mod-mode tree. Saves were backed up again first.

| job | result | notes |
|---|---|---|
| vh-move | **PASS** (12.6 min) | `PASS: moved (out of the world), game up 10 min after the move, no crash`. Clicks: `#2 yes`, `#2 accept`, `#2 yes`, `#3 don't save (save declined)`. Save guard ok: Baseline-SV-Spring `9ded84456f8254e2` before and after. 0 new script errors. |
| vh1-soak | **FAIL** (9.7 min) | No crash and 0 native `FAIL` lines; ts3fixes was active (stuckcheck 153 runs, owspread 40). The watchdog stopped on `autopilot: dialog with no safe button, nothing clicked: 'Marty was talked into running an obstacle course at practice against some teammates. Should Marty compete or just watch the competition?' buttons: #2 'compete', #3 'watch'`. |
| vh-cycle-a, vh-cycle-b, vh-travel-matrix, vh-travel-trip, vh-newsave, vh-matrix, vh-repro, hf-statecmd | not run | Stopped at the first FAIL. |

Triage of vh1-soak (static) [verified for the dialog; cause inferred]: a random school-practice event offered a choice that no ts3agent rule covers, and neither button is a safe word. The harness refuses such dialogs on purpose and does not default-accept them (docs/236). This is a harness rule gap, not a mod fault. A rule for this event (for example, answer "watch") is needed before the soak can pass; not done.

Restore verified:
- Bin pair `c035abe5a3a6` / `8aa37522782a`.
- ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, ts3-nointro `2d4394daca11`, scriptCache `db69b1b6a862`, Options.ini `50cb641832f8`, LoadOption.ini `e2dccbd46afd`.
- Mods listing identical (174 files).
- Saves: 0 differences against the backup (2602 files).
- No game process left.

### Run 8c: competition-event rule, then the remaining jobs (2026-11-03)

PR #226 76b873c adds a named ts3agent rule. It matches the dialog text "compete or just watch", never the buttons alone, and clicks "watch" as a rule click. It lives in `DialogPick.kEventRules`, and `RuleFor` checks it first. The unknown-dialog policy is unchanged. Self-check cases are in `tools/selftest/dialogpick`; selftest-all: 191 PASS, 0 FAIL; CI is green. Same set-up as 8b; Saves were backed up again first.

| job | result | notes |
|---|---|---|
| vh1-soak | **PASS** (12.1 min) | `soak 10 min, watchdog quiet`; ts3fixes active (stuckcheck 220 runs, owspread 60); 0 new script errors. The competition event did not appear in this run, so the new rule was not exercised live. |
| vh-cycle-a | **CRASH** (4.3 min) | `cycle.ts --save` exit 1. The game crashed at the main menu, before the world load. |
| vh-cycle-b, vh-travel-matrix, vh-travel-trip, vh-newsave, vh-matrix, vh-repro, hf-statecmd | not run | Stopped at the crash. |

Crash, static triage [verified for the addresses and log lines; cause inferred]:
- EA xcpt `26-11-03 07.37.18`: `ACCESS_VIOLATION writing address 0x00000000 at 0x5de273d0` = msvcr80.dll+0x173D0.
- Call stack: msvcr80 0x5DE273D0, 0x5DE27476, then Sims3Launcher.exe 0x00412DEE (+0x12DEE), 0x00412F0C (+0x12F0C), 0x00445F0C (+0x45F0C).
- Our crash log line: `exception C0000005 at 0x5DE273D0 (msvcr80.dll+0x173D0) (info 00000001 00000000) thread 24776 ... stack code refs: 007FE05C 004E41F5 007FE05C 00412DEE 00800000 00412F0C 00445F0C`. Registers: eax 0, edi 0, edx 0, ecx 3F. This is a CRT copy or fill into a null destination.
- The xcpt system block says `Free virtual memory: 34 Mb` of 4095 MB.
- The last ts3native lines before the crash are the start-up `diag:` block and `ts3fixes: play: ui tasks ... ToMainMenuState.OnEnterNotInWorld`. Four handled first-chance AVs at 0x04157CC2 in another module came earlier.
- None of the crash or caller addresses is in `ts3addr-ea169.h`, and the change ledger and docs have no match.
- Reading [inferred]: the 32-bit address space was used up at the main menu, so an allocation returned null and the CRT wrote through it. Our build counts as the cause until shown otherwise. The likeliest suspect is `[GC]` (InitialHeapMB=352, GrowthStepMB=32: the larger Mono heap reserve; `OK GC initial heap ... imm 0x2800 -> 0x16000`) together with the 162 packages / 5181 MB of CC on this machine. vh-move and vh1-soak started and loaded the same set-up cleanly, so it is not deterministic.
- No memory sample: the row's memory is empty because the crash came before the first sample.
- No bisect and no fix attempt.

Kept: `probe-out/ea-eamod-1103.vh-cycle-a.xcpt.txt`, `.xcpt.mdmp`, and the job's native log `probe-out/queue/ea-mod-matrix-2026-11-02-vh-cycle-a-2336-ts3native.log` (ea-mod-mode worktree).

Restore verified:
- Bin pair `c035abe5a3a6` / `8aa37522782a`.
- ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, ts3-nointro `2d4394daca11`, scriptCache `db69b1b6a862`, Options.ini `50cb641832f8`, LoadOption.ini `e2dccbd46afd`.
- Mods listing identical (174 files).
- Saves: 0 differences against the backup.
- No game process left.

### Run 8d (2026-11-03): triage of the 8c crash, dump fix, memory logging, reruns, CC parked

Static triage of the 8c vh-cycle-a crash (no bisect, no GC-off run):
- Start-up lines identical in vh-move, vh1-soak and the crashed vh-cycle-a [verified]: `GC InitialHeapMB=352 GrowthStepMB=32`, `OK GC initial heap: 0x00E4E1B8 imm 0x2800 -> 0x16000`, `diag: large address aware: yes`.
- Crashed run's probe samples (`probe-out/20261103-073613/memory.csv`, ea-mod-mode worktree) [verified]: 07:36:49 at the main menu private 1745 MB, free 1912 MB, largest free 1877 MB (same as the passing runs); 07:36:59 private 3627 MB, commit 3792 MB, free 30.9 MB, largest free 0.9 MB: **+1.88 GB in 10 s at the menu**; crash at 07:37:18. No passing run has such a jump.
- Round 8 vanilla vh-cycle-a has memory rows only after the load (free 1774 MB, largest 1729 MB), no menu sample [verified].
- [GC] on 1.69 [verified]: patch sites carry the same immediates as Steam (default 0x2800 = 40 MB; 0x00E4E1B8 vs Steam 0x00E4E168; growth sites 0x00E4A228/24F/256), `ts3bytes-ea169.h` "ok" for all; the reserve is 352 MB as on Steam. The 8c note naming [GC] as likeliest suspect is withdrawn: it reserves no more than on Steam.
- The 1.69 exe is LAA-flagged (characteristics 0x123) [verified].
- The failing allocation (decrypted image `probe-out/ts3-ea169.bin`) [verified code; size inferred]: Sims3Launcher.exe+0x12DEE (0x412DE8) is `call [0xFDF704]` (MSVCR80 memcpy) after the EASTL allocator 0x4E41E0 (`allocator.h` string 0xFE2A80) returned null; 0x412CC0 is an `eastl::vector<uint8_t>` grow/insert (new capacity = max(2*cap, size+n)) called from resize 0x412EE0; its caller 0x445E80 is an image decode that reads the image info from a decoder vtable and resizes the buffer to `((bpp+7)/8) * [esi+14] * [esi+10]` (bytes per pixel x width x height). So a ~1.9 GB raster buffer was requested at the menu: a huge or corrupt image header [inferred]; the 1.88 GB jump is that buffer or its first doubling. Image source unknown; Decompress (RefpackHook 0x004EC1A5, verify 0) feeds package reads and is one candidate [inferred, not checked]. Run 7f (11-02 16:21) had the same signature (msvcr80 0x5DE273D0, 36 MB free, 0-byte mdmp, at start-up).
- DXVK was active in every EA mod-mode job, the crashed one included [verified]: `diag: Bin add-ons: d3d9.dll 7672 KB v10.0.17763.1 (DXVK)` in all job logs; `Bin/Sims3Launcher_d3d9.log` (rewritten per launch): DXVK v3.1.1 x86, Intel(R) Graphics driver 101.8826.0, heap 0 17.96 GiB DEVICE_LOCAL, every memory type HOST_VISIBLE (UMA). The crashed run's DXVK log ends `err: D3D9: InitTexture: map failed`: a victim of the exhaustion [inferred]. The docs/236:375 device-creation hang did not lead to DXVK being removed; the laptop does not run plain D3D9.
- DXVK and 32-bit address space (docs/08, verified from source): managed textures in MemoryFilePool 16 MiB file-mapping chunks, mapped views capped by `d3d9.textureMemory` (default 100 MiB); Vulkan mapped chunks 16 MiB (64 MiB for host-visible VRAM); one larger resource gets a dedicated allocation; no maxChunkSize option. On this UMA iGPU every device-local type is host-visible, so DXVK may map buffers it keeps there [inferred]. Laptop `dxvk.conf`: customVendorId 10de, customDeviceId 1080, customDeviceDesc "Geforce GTX 580", maxFrameRate 60, `dxvk.hud=fps,memory`, samplerAnisotropy 16; `d3d9.textureMemory` not set.

Harness (PR #226, 0b57700, CI green):
- 0-byte dump [inferred]: the game ended itself (probe `process-terminated` 07:37:28) before the harness acted, and EA's in-process handler cannot write a minidump with ~34 MB of free address space. The probe now takes an out-of-process mini dump (`game-alive.ts --dump <pid> --mini`) on the first sample whose largest free block is under 200 MB, and writes `regions.jsonl` every 60 s and at that sample (free, largest free, reserve-only, biggest reserve-only blocks, allocations >= 64 MB).
- `memory.csv` split: `driverImgMB` (d3d9/dxgi/vulkan-1/ig*/nv*/amd* images), `anonMappedMB` (pagefile-backed sections: DXVK MemoryFilePool views, driver mappings; owner inferred), `ccPkgMB`/`gamePkgMB` (mapped `.package` files), `otherMappedMB`, `imageOtherMB`, `privateOtherMB` (private minus GC heap). GC counters use 0x127C920/0x127C934/0x124D020 when the exe has the EA PE stamp.
- ea-mod-matrix jobs add `[MemDiag]` Light=1, AllocSites=1 (Virtual=1, Heap=0: the EA heap hooks are at Steam addresses); each job keeps its DXVK log (`row.dxvkLog`).

Memory split, EA laptop vs Steam (Steam: `diag: mem #1 load` of the 11-01/11-02 Steam release-ini logs; EA: both vh-cycle-a reruns). **Open finding:**

| bucket (MB) | EA menu (probe +10 s) | EA at load (diag: mem #1) | Steam at load | EA minus Steam |
|---|---|---|---|---|
| committed | 1886-1893 | 2294 / 2448 | 2005-2051 | **+245 to +400** |
| private (walk) | 1723-1731 | 1782 / 1945 | 1485-1498 | **+290 to +455** |
| images (modules) | 354 (driver DLLs 204) | 354 | 391 | -37 |
| mapped | 84 (anon 71) | 183 / 173 (anon ~150-165) | 153-187 | ~0 |
| free / largest free | 1927-1935 / 1892-1899 | 1548 / 1507, 1383 / 1341 | 1723-1804 / 1597-1693 | largest -150 to -350 |

Per owner inside private at load (EA / Steam): unattributed `rest` 702-707 / 307-309 (+395); mimalloc `miRes` 0 / 192 (S3SS is not installed on the EA laptop, so the CRT heap stays in `rest`; Bin add-ons verified, attribution inferred); thread stacks `stkRes` 184-185 / 98-99 (+86; S3SS stack trim absent, inferred); GC heap 385 and 544 / 352 (+33 to +192; run 2 grew before the load); resource cache `rc` 222 / 170-284; EA heaps `eaCom`/`eaRes` 646-650 / 617-652 (~0). Net: **private is the larger bucket, +290 to +455 MB**: ~+200 CRT heap and unattributed private without S3SS, +86 stacks, +33 to +192 GC growth. Driver DLL images are 204 of the 354 MB images (Steam's per-DLL split is not logged: open). Whether the Intel driver's vkMapMemory views count as private (in `rest`) or mapped is not determined. None of our reservations differs in size on the EA build ([GC] 352 MB, same immediates; EA heap reserve 660 vs 631-664; ThreadStack, Alloc, RouteSnap pool, FootprintIndex, SaveCompress, Compaction off on both); the differences come from S3SS (mimalloc, stack trim) being absent on EA.

CC parked (Rodney 11-03: the EA laptop is GPU-limited and checks mod correctness and compatibility on 1.69 only). Details in docs/233 "CC parked". 88 files / 5145.4 MB moved (never deleted) to `Documents\Electronic Arts\The Sims 3\Mods-CC-parked`; 86 files / 53.8 MB kept, incl. a 15-item CC sample. Moved between jobs: the driver loop was stopped after vh-cycle-b ended, game closed.

Environment slip: 8 leftover `python -` processes from 11-02 agent heredocs (11-18 h CPU each) held ~58% of the CPU during every EA job since 11-02 13:46; killed 11-03 08:20. vh-travel-trip run 1 failed under that load (`trip dropped: no travel interaction in the household queue for ~1 min`, no cancel, exception or crash line in its logs); rerun once on the quiet machine.

| job | set-up | verdict |
|---|---|---|
| vh-cycle-a | full CC + logging | PASS 4.2 min, PASS 4.2 min (2 reruns) |
| vh-cycle-b | full CC + logging | PASS 4.2 min |
| vh-travel-matrix | CC parked | PASS 6.8 min (1/1 case) |
| vh-travel-trip | CC parked | FAIL 3.1 min under 58% stray load; rerun PASS 4.8 min |
| vh-newsave | CC parked | PASS 1.4 min (EaMod-SV-fresh created) |
| vh-matrix | CC parked | PASS 3.2 min (5/5 cases) |
| vh-repro | CC parked | **CRASH** 3.8 min, at start-up (4 s after launch); stopped |
| hf-statecmd | - | not run (stopped at the crash) |

vh-repro crash, static triage [code verified; cause inferred]: the game ended 4 s after the probe attached (00:29:45 to 00:29:49 UTC, `detached: process-terminated`), before D3D init (no new DXVK log) and with no xcpt file. ts3native.log: right after the seven `OK   alloc sites Nt* detour` lines, `exception C0000005 at 0x00000000 ... thread 24284 eip 00000000 ...; stack code refs: 5D33F358` (0x5D33F358 is in d3d9.dll, DXVK, 0x5D320000-0x5DA98000). This is a call through a null pointer, and `allocsites.cpp` has that window: `AsDetour()` writes the `jmp` into ntdll (`AsWrite5`) and only then returns the trampoline, which the caller stores in `g_asNt*` (`g_asNtFree = AsDetour(...)` and the rest in `AsInstallVirtual`). Another thread (here a DXVK thread) that calls NtAllocateVirtualMemory / NtMapViewOfSection / ... in between enters `AsNt*Hook`, which calls `g_asNt*` = NULL: eip 0. The same window exists for the EA heap hooks. So the crash comes from the 8d logging (`[MemDiag] AllocSites`, a test-run tool), not from the shipped ini [inferred, one occurrence]. On EA the asi loads while DXVK threads already run, which makes the window reachable [inferred]; whether Steam can hit it is not checked. Suggested fix, not made (no second fix attempt): store the trampoline in the function pointer before the jmp goes live (AsDetour takes the slot and writes it first), then rerun vh-repro and hf-statecmd. No mini dump: the process never reached a low-address-space sample.

No low-address-space sample, no dump, no out-of-memory crash in any 8d job; lowest largest-free block per job 1432-1548 MB after the CC park (vh-cycle-a with full CC: 1287 / 1423 MB). Not rerun on the parked set-up: vh-move, vh1-soak, vh-cycle-a/b (passed before the park).

Restore verified (08:34): Bin pair `c035abe5a3a6` / `8aa37522782a`, ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, ts3-nointro `2d4394daca11`, scriptCache `db69b1b6a862` (built with the 174-file set; the harness clears it on the next run), Options.ini `50cb641832f8`, LoadOption.ini `e2dccbd46afd`, Mods listing identical to the new 86-file baseline, Saves 0 differences against the backup (EaMod-SV-fresh removed), no game process.

### Run 8e (2026-11-03): detour publish-order fix, vh-repro rerun

Fix for the 8d vh-repro start-up crash: `InstallDetour` and `AsDetour` take the caller's trampoline slot and store it (full barrier) before one atomic 5-byte jmp write (`Write5`, shared); a copied leading `call rel32` is re-targeted inside `InstallDetour` before the jmp goes live (rtsites.cpp did it after). All InstallDetour sites (about 90) and the 11 AsDetour sites pass their slot; routesnap lot-0 guard and the unseen replan split now pass the global instead of a local. Hand-rolled patches (PatchCalls, savewrite, eafix, monothread, autshadow, values, loadprof, ...) audited: they already store first. selftest `TestDetourPublish` (200 installs under a spinning caller) passes in CI. Steam: PR #233 (5ebc17e + 6c55e49, CI green). Port: b762bd9 + c84a872, CI run 41554751599 green (native, native-ea169, node); asi `ac66bc0efc8f`, rc ini unchanged.

Rerun with the 8d set-up (driver as 8d, CC parked, 86 Mods files, `[MemDiag]` Light=1 AllocSites=1 Virtual=1 Heap=0, Saves backed up to `game-copy/saves-pre-eamod8e`), machine quiet (no stray python/node, CPU idle):

| job | verdict |
|---|---|
| vh-repro | **CRASH** 4.8 min, during `Handlers.LoadSave`; stopped |
| hf-statecmd | not run (stopped at the crash) |

- The 8d start-up crash is gone [verified]: all seven `OK   alloc sites Nt* detour` lines, then the game ran ~53 s to the save load; no null-call line.
- New crash [verified]: `exception C0000096 at 0x00769111`, thread 32296, edx 0x00EC0FE0, caller 0x00E5495B (EA report `xcpt ALRPANTONIAL1 26-11-03 08.46.57.txt`, `PRIV_INSTRUCTION`). Same signature as Run 7e.
- Cause [code verified]: the port's `DetourCore` re-targets the copied `call rel32` from `prologue + 1`, the baked Steam bytes; on 1.69 `Matches` compares through `ExpectedAt`, so the live rel32 differs. Route gate site 18 (`0xEC0FE0`, callee `0x76A230` on EA) gets callee `0x769110`, mid-instruction. This is the 7e bug that df3f2e5 fixed in rtsites.cpp (displacement from `tr`), brought back when the fixup moved into the shared helper. Fix (not made, no second attempt): take the displacement from the trampoline copy, `*(const LONG*)(tramp + 1)`, in `DetourCore` on both branches (Steam unchanged: same bytes).

Restore verified (08:51): Bin pair `c035abe5a3a6` / `8aa37522782a`, ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, ts3-nointro `2d4394daca11`, scriptCache `db69b1b6a862`, Options.ini `50cb641832f8`, LoadOption.ini `e2dccbd46afd`, state identical to the pre-state, Mods listing identical (86 files, `6d65db6779b1`), Saves 0 differences against the backup, no game process. Logs: `probe-out/ea-eamod-1103e.*`.

### [ImageGuard] (2026-11-03): the 8c image decode, static triage and guard

Static only, no game run. Ledger docs/51 §425.
- Decoder [verified code]: 1.69 `0x445E80` (Steam `0x445FB0`) is the generic decode of EA's image codec layer: a list of codecs (JPEG 2, PNG 4, BMP 5, TGA 6, GIF 7, DDS 0x1F; nodes `{next, id, desc, probe, open, getinfo, decode, close}`, registered at `0x446140`). Its only caller is the wrapper `0x445FE0` (Steam `0x446110`), called from 7 places with (stream, format 0x32 = probe every codec, sink, 0xA). The decode asks the chosen codec for the info and resizes an `eastl::vector<uint8_t>` (`this+0xC`) at `0x445F07` to `(bpp / 8) * [info+0x14] * [info+0x10]` (32-bit imul, no check).
- Where width/height come from [verified for PNG and DDS, inferred for the others]: not from caller arguments. The info is the codec's GetInfo answer for the stream, built fresh per decode (PNG `0x4175F0` allocates and zeroes it, DDS `0x4153A0` reads the 0x70-byte header at offset 0 into a zeroed local). The TGA probe caps 4096 x 2048. With the file scans clean (CC 26,185 images max 2048 x 2048; caches and save DBs 2,777 images max 512 x 512), the open readings are a stream whose bytes are not a file image (game-built or a non-image resource), or the header read racing with another user of the same stream (probe and GetInfo each seek to 0 and read) [inferred, open].
- Key / package [verified at `0x7F3088`, inferred at the other two]: at the resize the decode has the info (esi), the decoder (ebp; stream at +4, codec node at +8) and its frame; the wrapper's caller's ebx is on the stack. Three of the 7 callers (`0x7E235B`, `0x7E78A8`, `0x7F30FA`) keep the resource in ebx; its vtable slot 4 returns the key {instance, type, group} (type compared with 0x2F7D0002/4/6). The package path is not reachable from this frame.
- Failure path [verified]: `0x445FA7` (Steam `0x4460D7`) releases the info, closes the decoder and returns `al = bl`; with bl = 0 the decode returns false, the path of a codec that fails on the rows. All 7 callers test al and free their objects. eax/ecx/edx are written before they are read there; `tools/reg-liveness.py --edition both --addr 0x446037`: 0 risky triples.
- Hook: `[ImageGuard]` (imgguard.cpp, counters v160 at `0x5AE4`, the same offset as main: v147-v159 are `igMainPad` here). Call-site retarget of `0x446037` (`E8` target, the 30 size bytes and the 11 failure-path bytes checked first; no trampoline, the target is a constant). Pass: `pushfd/pushad`, check, restore everything, tail jump to the resize. >= LogMB (64): an `IMAGEGUARD` line (raw w, h, bpp, format request, codec, bytes and the game's 32-bit size, key when the caller has one, stream and resource vtables, caller plus 3 stack code refs) and `igBig`. > MaxMB (512) or a negative/wrapping size: `IMAGEGUARD REJECT` and the failure path. First 64 lines. Table rows in docs/data/ea169-overrides.csv (9 rows); port-run's own map agrees on `0x446037 -> 0x445F07` (masked) and the resize (ref).
- Ini: `[ImageGuard] Enabled=0 LogMB=64 MaxMB=512` in ts3native.ini; the EA minimal ini forces Enabled=1 (build.yml `ALLOW_ON`); the EA rc ini follows the shipped value (0) until a run proves it. Proposal for the Steam rc ini (Steam session's call): Enabled=1 with the defaults; it only acts above 512 MB, which no shipped or CC image reaches.
- Repro: `tools/harness/repros.json` `imgguard-oversized-png` + `node tools/imgguard-repro.ts` (a package with a PNG whose header claims 30000 x 30000); not run.

### Run 8f (2026-11-03): call rel32 fix, vh-repro and hf-statecmd rerun

Fix for the 8e crash: `DetourCore` takes the displacement of a copied leading `call rel32` from the trampoline's copy of the live bytes (`*(const LONG*)(tramp + 1)`), as df3f2e5 did in rtsites.cpp. selftest `TestDetourCallRel`: a detour over `call rel32` must reach the live callee; on the port a test row (`TS3N_TEST_EXPECTED`, a one-row `ExpectedAt`) makes the literal point at another callee, as Steam bytes do on 1.69 (the 8e code returns 9, the fix 7). Port 18df64a (it also carried the ImageGuard session's uncommitted ts3native.cpp / selftest.cpp edits, completed by 3292c74 + 0771db2); CI run 41556473868 at 0771db2 green (native incl. `detour call rel32: ... (7, want 7)`, native-ea169, node); asi `9b4afdad3751`; rc ini differs from 8e only by `[ImageGuard]` Enabled=0. Steam: PR #233 3a9d384, CI green.

Same set-up as 8e (driver, CC parked, 86 Mods files, `[MemDiag]` AllocSites, Saves backed up to `game-copy/saves-pre-eamod8f`), machine quiet:

| job | verdict |
|---|---|
| vh-repro | **PASS** 6.3 min (store-cas-browse) |
| hf-statecmd | **FAIL** 2.2 min: `state-cmd: buy_close: previous CAS/Buy action is still being timed; wait and retry` |

- Both jobs [verified]: 7 `OK   alloc sites Nt* detour` lines, `OK   route gate sites: 25 of 25`, no null call, no PRIV_INSTRUCTION, no crash. hf-statecmd logged two first-chance AVs at 0x04877CC2 (no module name, handled; game kept running).
- hf-statecmd cause [code verified, harness]: `vanilla-steps.ts state-cmd` counts `buy_open` as reached once `cas_state` has `buy_catalog` (50 s here), then sends `buy_close` at once; ts3agent `CasHandlers.cs` still measures the open (`sUiMeasuring`, until the list settles) and refuses. The step must also wait for the timing to settle (or retry on that answer). Not a mod or native fault; not fixed here.

Restore verified (09:10): Bin pair `c035abe5a3a6` / `8aa37522782a`, ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, ts3-nointro `2d4394daca11`, scriptCache `db69b1b6a862`, Options.ini `50cb641832f8`, LoadOption.ini `e2dccbd46afd`, state identical to the pre-state, Mods listing identical (86 files, `6d65db6779b1`), Saves 0 differences, no game process. Logs: `probe-out/ea-eamod-1103f.*`.

### Run 8g (2026-11-03): harness fix for hf-statecmd, reruns on the mod and on vanilla

Harness fix [code verified]: ts3agent refuses every CAS/Buy action while `cas_state.measuring` is true (`CasHandlers.cs` Measure, until the list settles, own 180 s cap), and that outlasts the mode switch. New `game-alive.ts untilUiDone`: poll `cas_state` every 2 s until `measuring` is false and the step's mode holds; every 30 s stallGuard (process, probe, heartbeat) plus `uiStep` (a miss = no answer, or the timing ended without the mode); 2 misses = FAIL with the reason and the last `cas_state`. Used by `vanilla-steps.ts state-cmd` (buy_open, buy_close) and `save-reload.ts` visit (every CAS/Buy step, in place of the fixed waitS); `repros.json` store-cas-browse `cas_state` untils also need `measuring` false. Self-check cases in `game-alive.ts --self-check`. story-fuzzer.ts not changed (story tests stopped). Commits: #226 78fbddb (CI 41558571170 green), main draft #235 a48f81a (CI 41558618595 green).

Same set-up as 8f (asi `9b4afdad3751`, rc ini plus `[MemDiag]` Light + AllocSites, CC parked, 86 Mods files, Saves backed up to `game-copy/saves-pre-eamod8g`), machine load clean (other CPU 4.3%):

| job | verdict |
|---|---|
| hf-statecmd (mod), try 1 | **CRASH** 3.8 min at the main menu, before any state step: `ACCESS_VIOLATION writing address 0x00000000` at msvcr80.dll+0x173D0, stack refs `007FE05C 004E41F5 007FE05C 00412DEE 00800000 00412F0C 00445F0C`, the 8c crash exactly (image decode; `[ImageGuard]` is off in the 0771db2 rc ini). Not the harness change: the game had not reached the world. |
| hf-statecmd (mod), try 2 | **PASS** 3.8 min: `buy_open: mode reached and timing done after 48 s` (`previous` shows buy_open settled, 3 items, 45859 ms), buy_close done after 2 s, enter_live_mode, mode guard quiet; 0 first-chance AVs |
| hf-statecmd (vanilla, `TS3_VANILLA=1`, `TS3_AGENT_PACKAGE` = the worktree ts3agent build) | **PASS** 3.7 min: buy_open timing done after 52 s, buy_close after 2 s; vanilla.ts restored 7 files (sha read back) |

AV triage (8f hf-statecmd, two handled first-chance AVs at 0x04877CC2) [verified from logs; owner inferred]: the address lies in a 23.75 MB pagefile-backed mapped section with `prot=0x40` (PAGE_EXECUTE_READWRITE) and alloc protection 0x80 (EXECUTE_WRITECOPY), base 0x04810000 (`allocsites: #1 load untr r=11 type=map base=0x04810000 MB=23.75 ... owner=pagefile-section`). It is not a module: not our asi, not DXVK `d3d9.dll`, not Mono JIT code (JIT buffers are private VirtualAlloc memory, and Mono's null-check AVs read near 0; these read the page-aligned addresses `0BFB4000` / `7F7D1000` through esi). The same fault repeats in EA runs with a different section base and the same offset +0x67CC2 (vh-cycle-a 0x043A7CC2, vh-cycle-b 0x04607CC2, vh-travel-trip 0x04557CC2 / 0x047B7CC2, 8i 0x044D7CC2, 8i2 0x04607CC2), always on one thread, always handled: code in a manually mapped RWX blob that walks memory and catches the fault at the end of a region. Owner [inferred]: an EA app / launcher component (activation layer), not ours. Verdict: benign, no action. 8i and 8i2 hit the crash log's 16-exception cap with it.

### Run 8h (2026-11-03): batched verify of the off-by-default native features, one load

Asi `9b4afdad3751` (0771db2), its rc ini plus `[LotSinCos]`, `[FootprintIndex]`, `[HeightCache]`, `[IdCache]` Enabled=1 Verify=1, `[WaterJps]` Verify=1, `[TerrainGrave]` Enabled=1, `[ImageGuard]` Enabled=1 LogMB=64 MaxMB=512, `[MemDiag]` Light + AllocSites (ntdll hooks). WorldImage stays off (25 unmapped addresses). Not included, with reason: probes and diagnostics (Probe*, LoadProf, HitchAttr, UnseenProbe, CrashForensics, GcThreadCheck, GcRootCheck, ArenaGuard, IcLockCheck); routing modes that replace the shipped async path (RouteWorker, MonoThread, D3DMultithreaded, JobQueue, LotStream, RouteSnap, AutShadow, PlanRepeat); save-path features that need the full save matrix (SaveCompress, SaveFlush, Compaction); the allocator cache `[Alloc]` (Apex area); sections zeroed for EA (tested in 8i). Flow: queue.ts job running `age-save.ts --days 3 --checkpoint 0 --save EaVerify-8h` (ultra, Keatons on Baseline-SV-Spring, then a save under a scratch name). Run 8h: PASS 9.4 min, but its log has no `footprint:` line and the row kept only the water/height counters. Run 8h2, same load with `minCounters: 24` (full `/native` dump), `logForbid` MISMATCH / IMAGEGUARD REJECT, and a side poller reading `ts3native_counters` +0x5AE4..+0x5AF4 through `/nativeexport` (agent.js reads counters only up to v158): PASS 9.6 min, no crash, 0 first-chance AVs.

| feature | init | counters (8h2, after the load to after the save) | Verify | verdict |
|---|---|---|---|---|
| [LotSinCos] | `OK   lot sin/cos cache: 0x00AB71B0 (Mode=1 Verify=1, 256 angles)` | lscCallsK 774 -> 29181, hitsK 29154, edxClobber 0 | lscMismatches 0 | PASS |
| [FootprintIndex] | `OK   footprint index: 0x006678A8, 0x0082B610, 0x00AAAF20 (Mode=1 Verify=1)` | `footprint: levels 299 entries 104077 matched 104073 calls 92 ms 440 verify ok (mode 1 fallbacks 0 worldSave ms 7926)` | verify ok | PASS (4 entries not matched, as in 7g; not analysed) |
| [HeightCache] | `OK   height cache: 0x00C28AC0, 0x00C28D90 (Mode=1 Verify=1 ...)` | heightCalls 665524 -> 2564901, hits 1502965 | heightMismatches 0 | PASS |
| [IdCache] | `OK   object id cache: 0x009394C0, 0x009390C0 (Mode=1 Verify=1 ...)` | idcLookupsK 758 -> 25213, hitsK 18485, fillsK 213 | idcMismatches 0 | PASS |
| [ImageGuard] | `OK   image guard: call 0x00445F07 (decode 0x00445E80) -> IgStub, LogMB 64 MaxMB 512 ...` | igCalls 714, igBig 0, igRejected 0, igOverflow 0 | n/a | PASS (no image >= 64 MB in this load) |
| [MemDiag] AllocSites | 7 `OK   alloc sites Nt* detour` lines | `allocsites:` lines at each `diag: mem` sample, vCalls 126581 (8h) | n/a | PASS |
| [WaterJps] Verify | `OK   water jps: call 0x0062186B -> WaterJumpHook, mode 1 verify 1, ...` | waterJpsCallsK 0, cellsK 0 | 0 mismatches, 0 fallbacks | N/A: not triggered (no water route in Sunset Valley; needs an Isla Paradiso water route) |
| [TerrainGrave] | `OK   terrain graveyard: 0x00683BA0 (...), Mode=1` | vfRtGraveParked/Freed/Races 0; heightRebuilds 64 -> 64 (no cell rebuild after the load) | n/a | N/A: not triggered (needs a terrain cell rebuild: Edit Town or terrain edits) |

No feature failed, so no isolation rerun. Harness gap [verified, not fixed]: queue.ts `featureIdle` should have voided the row for `[WaterJps] Verify` with 0 calls (and marked `[LotSinCos]` UNKNOWN when its counters were missing in 8h); both rows show PASS without that line. Logs: `sims-3-ea-mod-mode/probe-out/queue/ea-feat-verify-2026-11-03-*` and `ea-feat-verify2-2026-11-03-*` (the native.json has the full dump).

### Run 8i (2026-11-03): the five EA-off sections (ea169-structs 2de1253)

Asi `1b5be3a15d62` (CI 41562133685), its rc ini `505e95f68f2c` plus `ea-5sections-overlay.ini` (SlotEnabledRO Mode=1, SteerRO Mode=1 Shadow=1, FpRebuildLock Mode=0, RouteToObjRO Mode=1 Shadow=1, Watchpoint Lot=0, MemDiag Light + SelfTest + TypeGrowth + AllocSites with the EA heap hooks). Queue job: 10 min soak at normal speed (autopilot, Keatons, Baseline-SV-Spring), full `/native` dump, `logForbid` MISMATCH; one save `EaVerify-8i` from a side driver (autopilot off, pause, `save_game`, `probe-out/saving` set for the watchdog; done in 3 s). Init lines as in the criteria (`OK   steer RO: 0x85FD10 + 12 worker writer detours + LotGraph_Build gate + 11 tripwire stubs (Mode=1 Shadow=1)`, `OK   footprint rebuild lock C-A (Mode=0: count only)`, `OK   route-to-object RO: sites 0x831568 / 0x831633 / 0x8316FF (ready 15, Mode=1 Shadow=1; SH on)`, `watchpoint: handler installed ...`, both setter detours at 0x008595C0 / 0x00621DA0, `mem experiments: ... ready=0x3`, `alloc sites on ...: EA heap hooks 4/4, ntdll hooks 7`); no SKIP or FAIL line for the five (only the known mintcall SKIP); the expected `SKIP arena tail watch` line did not appear (`arena tail pad` on, watch arms 0). Queue row PASS 11.4 min, no crash, no new ScriptError. MemDiag failed its self-test, so Run 8i2 repeated the load with `[MemDiag]` off (isolation for the other four, not a bisect): PASS 11.4 min, save `EaVerify-8i2` in 3 s.

| section | counters (8i / 8i2) | must stay 0 | verdict |
|---|---|---|---|
| [SteerRO] | steerReady 1, steerTwStubs 11; calls 181203 / 203935, taken 1543 / 1116, shadowRuns 179660 / 202819 | shadowMismatch, shadowQueryMismatch, simWriteInRo, seqSkew all 0; no `steer RO:` difference lines | PASS |
| [RouteToObjRO] | rtoReady 15; calls 2 / 11, taken 0 / 6, shadowRuns 1004 / 946, rtoSlot 0 / 0 (rtoShadowFb 705 / 674 and rtoFb[8] 707 / 679 are allowed fallbacks) | shadowMismatch, shadowQueryMismatch, simWriteInRo 0; no difference lines | verify counters PASS; the slot path (rtoSlot) was not exercised in either load |
| [FpRebuildLock] | caReady 1, caMode 0; caSimInJob 0/0/0/0 in both | caQlHeld 0 | N/A: no sim rebuild of an invalid level during a job in 2 x 10 min; the fixed 0xFEB970 row is not proven yet |
| [Watchpoint] (Lot=0) | wrZ[0] 299773 / 300918, wrZ[1] 6164 / 5081; wrP30[0] 0 / 0; wpArms 0, wpHits 0 | - | child z setter PASS; the parent +0x30 setter was never called (0 in both loads), so its detour is unproven |
| [MemDiag] | mxReady 3, tyMode 1, tyPasses 125, tyClasses 9239, sqGcs 0 | tyFaults 0 | **FAIL**: `diag: mem selftest EA heap direct block 8 MB eaDir +6.00, back -8.00 (want +8.00 +-0.10) FAIL, try 3`, `... eaDirN +0.00, back -1.00 (want +1.00 +-0.00) FAIL, try 3`, `diag: mem selftest leak 8 x 1 MB eaDir +10.70 (want +8.00 +-0.20) FAIL`; every other self-test check ok (commit, arena, stack, mapping, routesnap, memsus, cross-check) |

sqGcs 0 is not a fault: it counts only when `SqrtHeapLimit` is on (memexp.cpp `MxOnGc` calls `MxSqrtOnGc` only under `sh = k.sqMode != 0`) [verified code]; that criterion did not fit this overlay.

MemDiag static analysis. Log to code: the failing fields are `eaDir` / `eaDirN`, read by `MdEaDirect` (memdiag.cpp) from the MemoryMan heap object `[TS3A(0x011CB864)]` (1.69: 0x012258E4, ts3addr-ea169.h) at +0x488 (count) and +0x48C (bytes). The self-test step `MdActEa` allocates 8 MB through EA Malloc with flag 8 (direct VirtualAlloc block) and frees it, up to 3 tries. (1) The address and offsets are right on 1.69 [verified from the log]: the free side moves exactly -8.00 MB and -1 block, private moves +6.02 / -8.02, and the field reads sane values at every sample (eaDir 154 MB, 100 blocks at load). (2) The miss is on the alloc side only: +6.00 MB and +0 blocks = our +8 MB / +1 block plus one other 2 MB direct block the game freed between the two samples; the leak check's +10.70 is the same effect in the other direction [inferred: concurrent EA direct-block churn during the sample window; the 0.1 MB / 0-block tolerance cannot absorb one foreign block]. On Steam, S3SS mimalloc takes most large allocations off the EA heap; S3SS is not installed on the EA laptop (Run 8d memory notes), so direct-block churn is likely higher here [inferred, not measured]. Not caused by the struct port, and no 1.69 address differs. Per Rodney's rule MemDiag did not pass, so `[MemDiag] Light` stays 0 in the EA rc ini (`RC_ZERO_KEYS` unchanged on ea169-structs). Options for later: run the EA-heap step with the sim paused, or allow one foreign block when the free side matches exactly.

Restore verified (10:21) from the 8g backup: Bin pair `c035abe5a3a6` / `8aa37522782a`, ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, ts3-nointro `2d4394daca11`, scriptCache `db69b1b6a862`, Options.ini `50cb641832f8`, LoadOption.ini `e2dccbd46afd`, state identical to the pre-state, Mods listing identical (86 files), Saves 0 differences (the scratch saves are gone), no game process, no stray python or node. Logs: `probe-out/ea-eamod-1103g.*`, `probe-out/ea-eamod-1103i.*`; queue files in `sims-3-ea-mod-mode/probe-out/queue/` (`ea-mod-matrix-2026-11-03-hf-statecmd*`, `vanilla-matrix-2026-11-03-hf-statecmd*`, `ea-feat-verify*`, `ea-8i-*`, `ea-8i2-*`).

### Run 8j (2026-11-03): [ImageGuard] catch attempt for the 8c / 8g main-menu crash

Goal: catch the ~1.9 GB raster request of image decode `0x445E80` (8c, 8g hf-statecmd try 1: msvcr80+0x173D0 null write) with the guard on, and map who asks. Asi `90c9f67dc981` (CI 41564463782 at 705be99, newest green ea169-port build; `OK   image guard: call 0x00445F07 (decode 0x00445E80) -> IgStub, LogMB 64 MaxMB 512` in every cycle), its rc ini plus the hf-statecmd job ini (`[MemDiag]` Light=1, AllocSites=1 Virtual=1 Heap=0, no SelfTest) and `[ImageGuard]` Enabled=1 LogMB=64 MaxMB=512. CC parked (86 Mods files), Saves backed up to `game-copy/saves-pre-eamod8j`. Flow: scratch queue job `ig-cycle` (vh-cycle-a style: launch through the EA app, main menu, load Baseline-SV-Spring, 2 min normal soak, quit), one `queue.ts --only ig-cycle` launch per cycle, Saves mirrored back after each. Stop rule: two IMAGEGUARD lines >= 512 MB (or REJECT) or a crash.

| cycles | verdict | IMAGEGUARD lines | lowest largest free block (probe, per cycle) |
|---|---|---|---|
| 8 of 8 | PASS 3.3-3.6 min each, no crash | **0** (no decode >= 64 MB in any cycle) | 1515-1546 MB; no menu jump (the only large 10 s step in each cycle is the normal start-up reserve, +1.06-1.26 GB private in the first ~20 s, same as 8i2) |

- No catch, so no caller chain from a live sample. The largest decode in 8 cycles is under 64 MB (LogMB); the guard counters (v160 `ig*`) are not in the queue row (agent.js reads to v158), so the exact maximum is not known.
- First-chance AVs: 2-16 per cycle (16 = the crash log cap), all at +0x67CC2 in the RWX pagefile section (bases 0x03CC0000-0x048B0000), the benign EA-side blob of Run 8g [verified from logs]. No msvcr80 fault.
- Machine load: other CPU 2.1-4.0% in 7 cycles; cycle 2 had 10.9% (a `node` process of the harness-editing session, 12.6%). Correctness run, not timing, so kept.
- Static pre-map of the decode's callers [verified code on `probe-out/ts3-ea169.bin`; subsystem of the crash still open]: wrapper `0x445FE0` has 7 call sites, in functions `0x5A2A50`, `0x77AB10`, `0x7E22F0`, `0x7E7810`, `0x7F3080`, `0x9E3EE0`, `0xD63E00`. `0x9E3EE0` references `Renderer/RadTextureFactory/MemoryStream` and `RadTexture` (texture factory); `0x5A2A50` is called from `0x9A7A60` (`Scratch/DownloadContent/ExportWorld/DBPackedFile`) and from world init `0xC61660` (`World/InitCommon-WeatherMan`, `Terrain/WorldTerrain`, `Misc/Init-Sky`); `0xD63E00` from `0xD63EA0`; the other four have no direct call (virtual methods). None of these is one of our hook sites (`ts3addr-ea169.h`; our only hook on this path is the ImageGuard call-site retarget at 0x446037 itself, off in 8c and 8g) [verified for the table; whether another hook feeds the stream, e.g. RefpackHook Decompress, is not excluded: inferred, open].
- Reading [inferred]: the crash is rare (2 in ~25 EA mod launches with this menu path, 0 in 8 here) and not tied to a cycle count; a race on a shared stream (probe and GetInfo both seek to 0) fits the rarity better than a fixed bad resource. The guard costs nothing visible (8 PASS, no line) and fails such a decode safely.
- Recommendation: ship `[ImageGuard]` Enabled=1 (LogMB 64, MaxMB 512) in the EA rc ini, and propose the same for Steam; keep it on in all EA runs so the next occurrence leaves an IMAGEGUARD REJECT line with key, codec, caller and return addresses instead of a crash.

Restore verified (10:55): Bin pair `c035abe5a3a6` / `8aa37522782a`, ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, ts3-nointro `2d4394daca11`, scriptCache `db69b1b6a862`, Options.ini `50cb641832f8`, LoadOption.ini `e2dccbd46afd`, state identical to the pre-state, Mods listing identical (86 files), Saves 0 differences, no game process. Logs: `probe-out/ea-eamod-1103j.*` (per-cycle native log and memory.csv); queue files `sims-3-ea-mod-mode/probe-out/queue/ea-8j-2026-11-03*`.

### Run 8k (2026-11-03): [ImageGuard] crafted repro; the 8c crash is address-space exhaustion, not a huge image

Asi `90c9f67dc981` (705be99) with its rc ini plus the 8j job ini (`[MemDiag]` Light=1, AllocSites=1 Virtual=1 Heap=0); CC parked (86 Mods files, listing `6d65db6779b1`), Saves backed up to `game-copy/saves-pre-eamod8k`. One `queue.ts --only <job>` launch per run (start, main menu, load Baseline-SV-Spring, 1 min soak); the crafted package went in as a queue `ccSet` (`game-copy/cc-synth/imgguard-16k`, moved into `Mods/Packages/cc-synth` for the job and back after it).

| run | ini | package | verdict | IMAGEGUARD lines | at the crash |
|---|---|---|---|---|---|
| scan (step 1) | Enabled=1 LogMB=1 MaxMB=2047 | none | **CRASH** at the main menu, 3.7 min | 4, all PNG `0x2F7D0004` group 0 via loader `0x7E78AD` (key ok): `0x240E56DDD114DC22` and `...DC23` 1024 x 1024, `0x5CF1F035F4DCC34A` 512 x 1024, `0xD4C492FE84FF892E` 2048 x 1024 (8 MB) | ACCESS_VIOLATION writing 0 at msvcr80 `0x5DE273D0` (+0x173D0), stack `0x412DEE` / `0x412F0C` / `0x445F0C` (inside decode `0x445E80`, after the resize); stream on the stack `0x2A7A0CE8` = the 8 MB `0xD4C492FE84FF892E` decode; "Free virtual memory: 34 Mb" |
| A (guard on) | Enabled=1 LogMB=64 MaxMB=512 | 16000 x 16000 PNG over `0x2F7D0004:0x0:0x240E56DDD114DC22` | guard **PASS**, run **CRASH** at the main menu, 3.5 min (other site) | 27 x `IMAGEGUARD REJECT over MaxMB: 16000 x 16000, 32 bpp, codec 4 PNG, 1024000000 bytes (game's 32-bit size 1024000000), decode returns failure; key ok type 0x2F7D0004 group 0x00000000 instance 0x240E56DDD114DC22 ... caller 0x007E78AD` | ACCESS_VIOLATION writing 0 at `0x00E48BA5` (game exe, no stack); "Free virtual memory: 28 Mb" |
| B (guard off), C (30000 x 30000) | - | - | not run: the address-space crash hits every launch now, so a guard-off crash would prove nothing | - | - |

- [verified from logs] The guard works live: the crafted key overrides the game's PNG from Mods, the decode asks for 1,024,000,000 bytes, the guard logs the REJECT with the crafted size and key and takes the failure path; the game goes on (27 retries of the same image, all rejected) until the separate crash below.
- [verified from logs] The 8c crash is address-space exhaustion, not a huge image header. Both 8k runs and the original 8c report (`probe-out/ea-eamod-1103.vh-cycle-a.xcpt.txt`) say "Free virtual memory: 34 Mb" (8k A: 28 Mb). memory.csv: the Boehm GC heap jumps from 352 MB to 2,316 MB (scan, `gcFreeMB` 2,147) and 2,284 MB (A, free 2,208) in one 10 s sample at the main-menu entry; the largest free block falls to 2.8 MB / 0.7 MB. The next normal 8 MB image decode then gets a null buffer and msvcr80 memcpy writes through it (the 8c signature). The "~1.9 GB" of 8c is this GC heap growth, not a raster request: the scan run logged every decode >= 1 MB and none was over 8 MB. In 8j (8 PASS) the GC heap stayed at 352 MB.
- [open] Who grows the GC heap by ~1.9 GB at the main menu (a managed allocation of ~1.9 GB, or Boehm heap expansion after one): 2 of 2 launches today vs 0 of 8 in 8j with the same build, ini and Mods; triage per the crash-cause rule (our mod first: ts3fixes / ts3agent at `ToMainMenuState`, `[MemDiag]` AllocSites; then the script mods in Mods).
- [inferred] Static, Decompress: if the image is a RefPack-compressed package entry, the bytes the decode reads are a buffer our `[Decompress]` hook filled (`OpenRecord` 0x4A96CD.. allocates RecordData, 0x4A72F0 calls the codec slot 0x4EC150 whose call at 0x4EC1A5 we hook; the 3 loaders 0x7E22F0 / 0x7E7810 / 0x7F3080 pass `record->vf18()` to the wrapper) [routing verified in code]. Neither our core nor EA's checks the written length, but the header is written first and RefpackFast matches EA's checks and return values (verify mode replaces any mismatch). Not the 8c cause now: the crash needs no bad header.
- Consequence: `[ImageGuard]` stays a valid guard against oversized image headers (proven here), but it does not fix the 8c / 8g main-menu crash. Default-on in the EA rc ini is held for Rodney's call; agent.js reads v159/v160 since ea-mod-mode `4c6d3b6`.

Restore verified (11:09): Bin pair `c035abe5a3a6` / `8aa37522782a`, ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, ts3-nointro `2d4394daca11`, scriptCache `db69b1b6a862`, Options.ini `50cb641832f8`, LoadOption.ini `e2dccbd46afd`, state identical to the pre-state, Mods listing identical (86 files, `6d65db6779b1`, no cc-synth, no ImageGuardRepro), Saves 0 differences, no game process. Logs: `probe-out/ea-eamod-1103k-scan.*`, `probe-out/ea-eamod-1103k-a.*` (native log, memory.csv, xcpt); queue files `sims-3-ea-mod-mode/probe-out/queue/ea-8k-*`.

### Run 8l (2026-11-03): GC heap growth at the main menu, static triage and `[GcGrowLog]`; rerun blocked

Goal: name who grows the Boehm heap from 352 MB to ~2.3 GB at the main menu (Run 8k scan and A, 8g hf-statecmd try 1). Our build crashed, so ours until proven otherwise; no bisect of the mod config.

8j (8 PASS) vs 8k (2 CRASH), from the logs [verified]:

| item | 8j | 8k scan / A |
|---|---|---|
| asi, rc ini | `90c9f67dc981` (705be99), ini dump identical | same; only `[ImageGuard]` LogMB=1 MaxMB=2047 (scan) / 64, 512 (A) |
| ts3fixes / ts3agent / nointro | rebuilt by cycle.ts every launch (hash differs per cycle by design), nointro `2d4394daca11` | same |
| Mods | 86 files, 74 packages (listing hash `b24e033aa5b3` of `mods-before.txt`) | same listing; A adds the crafted package (75) and a cache-clear step |
| save, launch, harness steps | Baseline-SV-Spring, EA app `origin2://` launch, cycle.ts restart/save, watchdog | same steps; crash before the menu dialog and load_save |
| agent first answer | 22-34 s after launch | 22 s (scan), 22 s (A) |
| machine load | other CPU 2.1-4.0% (one 10.9%) | 4.2% / 3.5% |
| free disk | 127 GB | 109 GB (one more 17.8 GB Saves backup); not a cause candidate (no paging limit near) [inferred] |

Decisive pattern: in all three crashing launches the GC heap is 352 MB at the first sample after the agent first answered and 2.3 GB at the next (8g try 1: answer 09:18:01, 352 MB at :02, 2,323 MB at :12; scan: 10:59:40, :42, :52; A: 11:04:39, 352 MB at :31, 2,284 MB at :41), before the "Mod Scripts Found" dialog, auto_dialog handling or load_save. In 8j the heap stays 352-354 MB through the same window in every cycle [verified from memory.csv and the queue logs]. No configuration difference explains 3 of 3 vs 0 of 8; the trigger looks timing-dependent [inferred].

Static GC findings:
- `[GC]` on 1.69: initial heap 0x2800 -> 0x16000 at 0xE4E1B8 and the three growth-step immediates 0x800 -> 0x2000 at 0xE4A228 / 0xE4A24F / 0xE4A256, all `OK` lines (bytes checked) [verified log]. No hook of ours on collect/expand was active: SqrtHeapLimit=0, TypeGrowth=0, GcThreadCheck, GcRootCheck, MonoThread off (`mem experiments off` etc.) [verified log]. Our only live memory hooks at the menu were `[MemDiag] AllocSites` ntdll detours (also on in 8j).
- 1.69 `GC_collect_or_expand` 0xE4A170 decoded from `probe-out/ts3-ea169.bin` [verified bytes]: in incremental mode it expands only when `GC_words_allocd` [0x127C944] is 0 (right after a forced collection), by heap/5 + needed when that is <= GrowthStep, else max(needed + slop, GrowthStep) = 32 MB; `GC_expand_hp_inner` 0xE49CD0 has only 3 callers (0xE4A25C, 0xE4A269, GC_init 0xE4E3BE). The globals the probe reads (heap 0x127C920, large free 0x127C934, gc_no 0x124D020) give sane values in every sample [verified].
- So 1.96 GB in <= 10 s is ~60 expansions of <= 64 MB each (allocations64 lists no block >= 64 MB besides the 352 MB initial section and igc32.dll) with `GC_large_free_bytes` 2,147 / 2,208 MB afterwards: the space was added and not used. That fits one allocation that keeps failing after each forced GC + expansion until VirtualAlloc fails (largest free block 0.7-2.8 MB at the end), not many live allocations [inferred]. Candidates: a large-object request whose new blocks are black-listed, or a needed-blocks value the step never satisfies [inferred, open].
- Collections during the growth: not observable from these runs (memory.csv has no gcCount column; `diag: mem` lines start only in play) [verified: none logged].
- Managed code at that moment: ts3agent's server answers its first `ping`, then `auto_dialog`; ts3fixes logs `ToMainMenuState.OnEnterNotInWorld` (scan) or nothing yet (A); EA `MainMenu.Init` / `PopulateWorldFileList` not reached in either crash (8j logs them next) [verified log]. Who allocates is not known.

Logging `[GcGrowLog]` (branch `ea169-gcgrow-log`, draft PR #238; 2f0f429, 9996c16, 4fde5c8; CI 41573191015 green incl. native-ea169; asi `c0eed2686392`): both calls of `GC_expand_hp_inner` in `GC_collect_or_expand` (Steam 0xE4A20C / 0xE4A219, 1.69 0xE4A25C / 0xE4A269) go to `GwThunk`; one `gcgrow:` line per expansion (blocks asked, needed blocks from esi, ok/FAILED, heap before/after, gc_no, bytes allocated since the last GC, large free, thread, return address + 3 stack code refs, script frames), first 50 then >= 64 MB, 500 max; growth outside these calls counts as `gwUnseenKB`. Off by default (`Enabled=0`), runtime switch `gwMode` (v161 0x5B04, `--ab gcgrowlog`). Checks: addr-codemod, port-run, patch-bytes-check, hook-sites --check, ea-ini, native-switches, reg-liveness both editions at both sites (0 risky), selftest-all 180 PASS.

Rerun: not run. Saves backed up to `game-copy/saves-pre-eamod8l` (11:39), job = 8k scan ini + `GcGrowLog.Enabled=1` (`ea-8l-scan.json`, asi `c0eed2686392`). The session preflight refused cycle 1 at 11:39:33: `console session state: session 2 Disconnected, console held by another user` (the laptop console is another user's session), so the game cannot run unattended now. Nothing was installed (queue.ts never started); Bin pair, packages, Options/LoadOption, Mods listing and Saves unchanged (state read back below). To resume once our session holds the console: `QUEUE=<scratch>/ea-8l-scan.json JOB=gw-scan bash eamod8l.sh run 3`, stop at the first launch whose memory.csv shows gcHeapMB > 600; the `gcgrow:` lines then name the asking thread, size and script frames.

Restore verified (11:40): Bin pair `c035abe5a3a6` / `8aa37522782a`, ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, ts3-nointro `2d4394daca11`, scriptCache `db69b1b6a862`, Options.ini `50cb641832f8`, LoadOption.ini `e2dccbd46afd`, state identical to the pre-state, Mods listing identical to 8k (86 files), Saves 0 differences, no game process. Logs: `probe-out/ea-eamod-1103l.*`.

#### Run 8l resumed (12:54-13:02, console back in our session)

Same job and asi `c0eed2686392`; Saves backup from 11:39 (unchanged since, restore mirrored it back). Preflight `session 2 Active, console held by our session`, no game process, machine load 2.3-2.8% other CPU.

| launch | verdict | gcgrow lines | max gcHeapMB |
|---|---|---|---|
| 1 | PASS 2.6 min | 3 | 450 |
| 2 | PASS 2.3 min | 0 | 353 |
| 3 | PASS 2.3 min | 0 | 354 |

No runaway in 3 launches (crash runs: 3 of 3 in 8g/8k, 0 of 8 in 8j, 0 of 3 here), so the stop rule never fired.

Launch 1 still shows the mechanism in small, in the same start-up window (right after `diag: header done`, ~20-30 s after launch, the window where the crash runs grew) [verified log]:
- `gcgrow: #1..#3 site 1 (step) ask 8192 blocks (32 MB) needed 969 blocks -> ok; heap 360920 -> 393688 -> 426456 -> 459224 KB; gc_no 52 (no collection between the three), allocated since the last GC 0 KB, large free 331148 -> 363916 -> 396684 KB; thread 38888 (not main)`.
- Native chain: return 0x00E4A261 (GC_collect_or_expand step call) < 0x00E4BB36 in `GC_alloc_large` 0xE4BAE0 < 0x00E4BD98 in `GC_generic_malloc` 0xE4BD30 < 0x00E4BE71 in `GC_malloc_atomic` 0xE4BE10 (Steam 0xE4BA90 / 0xE4BCE0 / 0xE4BDC0, table rows) [verified]: a pointer-free large object of 969 blocks (~3.8 MB), i.e. a managed string.
- Script frames: `ScriptCore.Simulator.Simulator_LoadXmlAsString` (icall) < `Simulator_LoadXmlAsString` < `ScriptCore.Simulator.LoadXmlString` < `ScriptCore.Simulator.ReadXml` < `Sims3.SimIFace.Simulator.ReadXml` (x2) < `Sims3.Gameplay.Utilities.XmlDbData.ReadData` (x2); outer frames past 8 not logged [verified].
- The request did not fit although 331 MB of large blocks were free, and the first two fresh 32 MB sections did not take it either (large free rose by exactly 32 MB each time); the third did. A ~3.8 MB atomic block refused by 331 MB of free space and by two empty sections is the same pattern as the crash runs' ~60 unused expansions [inferred: same request, there never fitting until the address space ran out; why it is refused (black-listed blocks or free-list fragmentation in EA's allocator) is open].

Who: EA's tuning loader `XmlDbData.ReadData` -> `Simulator.ReadXml` -> the `LoadXmlAsString` icall allocating the whole XML resource as one string, on a non-main thread at start-up [verified for launch 1; for the crash runs inferred by timing]. Ours on that path: no frame of ts3agent (ping handler) or ts3fixes (`ToMainMenuState`) among the 8 script frames, and no native hook of ours between the icall and `GC_expand_hp_inner` except the logging thunk itself [verified for the frames logged]; which mod or EA module calls `XmlDbData.ReadData` (outer frames) is not known [open]. Next: raise the script frame count to 16 and log the `GC_malloc_atomic` byte size and the XML resource key at the icall; rerun until a runaway is caught.

Restore verified (13:02): Bin pair `c035abe5a3a6` / `8aa37522782a`, ts3fixes `ac2c3f8799af`, ts3agent `1921c00c4c14`, ts3-nointro `2d4394daca11`, scriptCache `db69b1b6a862`, Options.ini `50cb641832f8`, LoadOption.ini `e2dccbd46afd`, state identical to the pre-state, Mods listing identical (86 files), Saves 0 differences, no game process. Logs: `probe-out/ea-eamod-1103l.*` (c1-c3 native log and memory.csv).

### Run 8m (2026-11-03): `[GcGrowLog]` round 3 (7c38ddb), static checks of the refusal path, rerun

Logging added (7c38ddb, CI 41582513401 green incl. native-ea169, selftest `GcGrowLog ... ok`): a `gcgrow why:` line per expansion with the `GC_allochblk_nth` refusal inputs read just before it, 16 script frames, a 9-byte detour of `IC_Simulator_LoadXmlAsString` (Steam 0x76D290, EA 0x76E1E0, prologue checked) that records the resource key per thread and the string size (`gcgrow xml:` for 1 MB or more), and the 20 expansions up to the heap crossing 600 MB always written. EA 1.69 addresses of the gate globals checked in the disassembly of `GC_allochblk_nth` 0xE49370 and `GC_promote_black_lists` [verified].

Static (Steam 1.67 disassembly; the same code at +0x50 on 1.69) [verified unless marked]:
- Thread 38888 is the game's simulator thread: the route-gate lines of the same launch print `(main 46456, sim 38888, worker 22160)`, sim = the game's own sim-thread id global, worker 22160 = our route worker. EA creates it; ours does not create, rename or wrap it: `[ThreadStack]` (exe IAT CreateThread / _beginthreadex) logged nothing (off), `[MonoThread]` off, and no ts3native code calls SetThreadDescription. So the tuning XML at start-up is read on the sim thread (script host), not on a loader thread of ours.
- `GC_requested_heapsize` 0x12225A8 has one code reference in the exe, the read in `GC_allochblk_nth`; nothing writes it (EA dropped `GC_expand_hp` / the `GC_init` add), so it stays 0 and "used >= requested" is always true. `InitialHeapMB` does not change it: `GC_init` 0xE4E160 only passes ebp (our patched 0x2800 -> 352 MB in blocks at 0xE4E168) to `GC_expand_hp_inner`.
- `GC_use_entire_heap` 0x11F2D98: one reference (the read in `GC_allochblk_nth`), no writer, 0. `GC_dont_gc` 0x11F30A8: set only by the `GC_DONT_GC` env var in `GC_init`.
- `GC_all_interior_pointers` 0x11949B4: .data initial 1, written only from the `GC_ALL_INTERIOR_POINTERS` env var in `GC_init`; ours never writes it (monothread.cpp only reads it, and is off).
- `GC_black_list_spacing` 0x119482C: .data initial 0x40000 (256 KB); `GC_promote_black_lists` (0xE4A800 region) sets it after each collection to 4 KB x heap / stack-blacklisted blocks, clamped to 12 KB..8 MB (0x800000 at 0xE4A85D, not one of the `[GC]` patch sites). `GC_collect_or_expand` reads it for the slop (2 x spacing / 4 KB blocks, capped at needed) on the non-ignore-off-page path.
- The four `[GC]` patches (initial heap 0xE4E168, growth step 0xE4A1D8 / 0xE4A1FF / 0xE4A206, BudgetCeiling .data 0x1192324 unchanged at 0) touch none of the blacklist code, spacing clamps or the `GC_allochblk_nth` gate. Indirect effect [inferred, the new log line measures it]: the spacing scales with heap size, so a 352 MB heap makes it larger (toward the 8 MB cap) than the stock 40 MB heap; a ~3.8 MB atomic request below the spacing is then refused on black-listed blocks instead of being taken with a warning (the punt applies only above the spacing).

Rerun (13:15-13:22, asi from 7c38ddb, same 8k scan job with `GcGrowLog.Enabled=1`, `run 10` stopping at the first heap over 600 MB). Preflight: console held by our session, no game process, other CPU 2.1% mean.

| launch | verdict | gcgrow lines | max gcHeapMB |
|---|---|---|---|
| 1 | PASS 2.3 min | 0 | 354 |
| 2 | CRASH 3.6 min (AV writing 0x0 in msvcr80.dll+0x173D0, thread 27656) | 58 + 8 at the crossing | 2259 |

Launch 2 is the runaway [verified log, `probe-out/ea-eamod-1103m.c2.ts3native.log`]:
- 57 expansions, all `site 1 (step) ask 8192 blocks (32 MB) needed 969 blocks (~3.8 MB) -> ok`, heap 360664 KB -> ~2.2 GB, `gc_no 51` the whole time (no collection), allocated since the last GC 0 KB, large free rising by exactly 32 MB per expansion (295 MB -> 1.9 GB free), all on thread 27656, all with the same XML key: type 0x0333406C (XML tuning) group 0 instance 0x14C166293CD50B89.
- Script frames (16): `Simulator_LoadXmlAsString` < `LoadXmlString` < `ReadXml` < `Simulator.ReadXml` (x2) < `XmlDbData.ReadData` (x2) < `Sims3.Gameplay.Autonomy.MetaAutonomyManager.LoadMetaAutonomy` < `InitializerType.Invoke` < `Action.DoInit` < `Initializers.Initialize` < `StartupState.RunInitializers` < `StartupState.Startup` < `StateMachineStatus.StartupCurState` < `SetState`. EA start-up initializer code only; no ts3agent / ts3fixes frame and no native hook of ours on the path except the log thunk and the LoadXmlAsString detour (both log only) [verified].
- `gcgrow why:` identical on all 50 written lines: gate off (`should_collect 0`: adj 20 vs min 3551604 words; `GC_requested_heapsize` 0), enough large left yes (87792 + growing list bytes vs max 96116 KB), finalizer freed 0, `GC_black_list_spacing` 4868 KB with the 3876 KB request below it, large-alloc warn counter 0 (never punted).

Cause [refusal reason inferred by elimination; inputs verified]: in `GC_allochblk_nth` the only refusal left for a free block big enough is the black-list check (the gate is off and the finalizer rule does not fire). Because the request (3.8 MB) is below the spacing (4.8 MB), a black-listed page inside the candidate range makes the allocator skip it, and every fresh 32 MB section is skipped too. `GC_collect_or_expand` then expands again instead of collecting (`should_collect` 0: nothing allocated since the last GC), so the black lists are never promoted or cleared, and the loop runs until the 32-bit address space is gone. Why every new section has a black-listed page every <3.8 MB is open; candidates [inferred, not checked]: the hashed black-list bitmap (page-address hash, fixed size) is dense after a collection with `GC_all_interior_pointers` 1 and a large heap, so almost any new page hashes onto a set bit; or false pointers in the roots covering the new address range. Our only influence found is indirect: `InitialHeapMB=352` makes the spacing (heap-scaled, 12 KB..8 MB) larger than with 40 MB, putting a 3.8 MB request under it [inferred; launch 1 of 8l also hit 3 expansions for the same 969-block request].

Logging bug found: the `gcgrow xml:` size is wrong. It reads +8 of the returned string, which holds the first two UTF-16 characters `<?` (0x003F003C = 4128828), not the length, so every line shows "4128828 chars"; the real string is ~2 M characters (969 blocks). The key is right. Not fixed in this round.

Restore verified (13:22): state identical to the pre-state (Bin pair `c035abe5a3a6` / `8aa37522782a`, ts3fixes, ts3agent, ts3-nointro, scriptCache, Options.ini, LoadOption.ini), Mods listing identical (86 files), Saves mirrored back (2602 files, 0 differences), no game process. Logs: `probe-out/ea-eamod-1103m.*`.

### Run 8n (2026-11-03): static check of the black-list refusal, `[GC] BlackListCapKB` fix, 8 launches

Static (1.69 image `probe-out/ts3-ea169.bin`, Steam `game-copy/bin/TS3W.exe`; the GC code is byte-identical at +0x50 on 1.69, data at +0x55EE0) [verified unless marked]:
- `GC_allochblk_nth` (Steam 0xE49320 / EA 0xE49370): for a free block big enough, kinds other than UNCOLLECTABLE and PTRFREE <= 8 KB walk `GC_is_black_listed` (0xE4A560 / 0xE4A5B0) over the span. If no clean sub-span of the needed size is left, the block is taken anyway ("punt", the warn call at 0xE49540 / 0xE49590) only when `needed > GC_black_list_spacing && block - needed > spacing` (0xE4950F / 0xE4955F); else it is skipped.
- `GC_is_black_listed`: the page hash is `(addr >> 12) & 0xFFFF`, 65,536 bits (0x2000-byte bitmaps), so the hash wraps every 256 MB. With `GC_all_interior_pointers` = 1 only the two stack black lists (old + incomplete) are checked, page by page. A new 32 MB section is checked against the same bits as the old heap pages that share its hash.
- `GC_black_list_spacing` (0x119482C / 0x11EA70C) is written only by `GC_promote_black_lists` (0xE4A7C0 / 0xE4A810): `heap bytes / (stack-black-listed heap blocks x 4 KB) x 4 KB`, i.e. heap / N, clamped to 12 KB..8 MB (`cmp eax,0x800000` 0xE4A856, `mov [spacing],0x800000` 0xE4A85D). It scales linearly with the heap for a given N. Promote has one caller, 0xE4A025 in the collection 0xE4A000, so only a collection refreshes the lists and the spacing. `GC_expand_hp_inner` (0xE49C80 / 0xE49CD0) adds the section and sets `GC_collect_at_heapsize` without touching them.
- `GC_collect_or_expand` (0xE4A120 / 0xE4A170): incremental (`GC_incremental` 0x124D02C on 1.69, set only by `GC_enable_incremental`): it collects (call 0xD70800 on 1.69) only when `!GC_dont_gc && GC_words_allocd > 0`. Non-incremental: it collects when `(GC_dont_expand && words_allocd > 0) || GC_should_collect()`, with an EA extra gate on `GC_gc_no` vs the last forced collection + 500. Otherwise it expands by `max(heap / (4 KB x divisor) + needed (+ slop of 2 x spacing, at most needed), GrowthStep)`. With nothing allocated since the last GC (Run 8m: 0 KB, `should_collect` 0) both modes expand. `GC_alloc_large` (0xE4BA90 / 0xE4BAE0) then retries `GC_allochblk` in a loop while collect_or_expand returns 1. Which of the two modes runs was not logged [open]; both give the same expand-only loop.
- Steam: same code and the same `[GC]` patches (InitialHeapMB=352 shipped), so the same latent loop [inferred]. S3SS patches no Boehm allocation or black-list code (its GC patches are in `MonoScriptHost::Simulate` 0xD819AA / 0xD81A1D / 0xD81A37 and `GC_stop_world` 0xE511F5, docs/03 §1.1). Why Steam has not shown it is open. The trigger is N (stack-black-listed pages after the last collection before the XML load), which varies per launch (8n: spacing 268 KB to over 1 MB at the same request). Address layout (S3SS mimalloc / stack fix, different DLL set) changes N [inferred, not measured on Steam].

Fix `[GC] BlackListCapKB` (gcgrow.cpp, 60d6668 + 55c696a, counters v162; shipped 0 = unchanged, EA rc ini 1024 via `tools/ea-ini.ts` `EA_RC_SET`). It lowers the 8 MB clamp to the cap (both immediates, bytes checked), so any request above the cap takes a black-listed block through Boehm's own punt instead of skipping it. The cap does not depend on K and adds no detour in the allocator. Counters: the punt's warn call goes to `GcBlackOk` (gcBlackOk / gcBlackOkMaxKB; interval set to 1, warning forwarded every 5th as before), and `gcExpandStreak` / `gcExpandStreakMax` count expansions with the same `GC_gc_no` in `GwThunk` (installed when the cap or `[GcGrowLog]` is on; "gc expand streak" line per new maximum >= 2). Why not the alternatives: (a) a forced collection promotes the lists, but the same false pointers (the sim thread is blocked in the same allocation) re-black-list the same hash bits, and the spacing grows with the heap, so it may not end the loop and costs a full GC; (b) is the same punt with a counter and needs a detour in the middle of `GC_allochblk_nth`; (d) a smaller initial heap lowers the spacing only until the heap grows back in play, and gives up the measured forced-GC gain. Also fixed: the `gcgrow xml:` size reads EA's MonoString length at +4 (8m read the first two chars). Checks: tool self-checks, port-run (4 new rows, 0 unresolved), hook-sites --check, ea-ini, reg-liveness (0xE4A20C / 0xE4A219 0 risky; 0xE49540 is an indirect call, checked by hand: eax/ecx/edx are written before any read after it), selftest-all 180 PASS. CI 41585365279 green including native-ea169 (asi `3ff42bd9049a`).

Live (13:44-14:03, 8k scan job + `GcGrowLog.Enabled=1` + `GC.BlackListCapKB=1024`, `ea-8n-scan.json`; console held by our session, other CPU 2.0-2.8%):

| launch | verdict | spacing when the 3.8 MB XML string came | expansions (max streak) | punts logged (first 20) | max gcHeapMB |
|---|---|---|---|---|---|
| 1 | PASS 2.2 min | 284 KB | 0 | 5 | 353 |
| 2 | PASS 2.3 min | 280 KB | 0 | 5 | 353 |
| 3 | PASS 2.3 min | **1024 KB (at the cap)** | 1 (1) | 20 | 386 |
| 4 | PASS 2.3 min | 284 KB | 0 | 15 | 353 |
| 5 | PASS 2.3 min | **1024 KB (at the cap)** | 0 | 9 | 353 |
| 6 | PASS 2.3 min | **1024 KB (at the cap)** | 0 | 4 | 353 |
| 7 | PASS 2.3 min | 268 KB | 0 | 11 | 354 |
| 8 | PASS 2.3 min | 324 KB | 0 | 6 | 353 |

Every launch logs `OK GC black-list spacing cap` at 0xE4A8A6 / 0xE4A8AD and the punt count hook at 0xE49590. The 3876 KB string of key 0x0333406C:0:0x14C166293CD50B89 is taken by a punt in all 8 launches (`gc black-list ok #n: 3876 KB`). In 3 of the 8 the spacing sat at the cap, so without it the request would have stayed below the spacing whenever the uncapped value exceeded 3.8 MB (8m: 4868 KB). No expand streak of 2 or more, no runaway, no crash. The one expansion (launch 3) was the ordinary "large bytes left" gate for a 1.3 MB Opportunities XML, streak 1. Verdict: PASS on the mechanism (8 of 8); a runaway rate of 4 in 24 needs more launches to show a rate change, so the claim rests on the mechanism. Restore verified (14:03): state identical to the pre-state, Mods listing identical (86 files), Saves 0 differences. Logs: `probe-out/ea-eamod-1103n.*`.

### Run 8o (2026-11-03): vanilla native config + `[GcGrowLog]` only, is the refusal loop an EA bug?

Setup: asi from 5b2c561 (`a94a70ce4056`: 55c696a plus spacing, heap, gc_no and streak on the `gcgrow xml:` line; log only). Base ini = the minimal EA ini with `ALLOW_ON=GcGrowLog` (`tools/ea-ini.ts`: every integer key 0, only `[GcGrowLog] Enabled=1`), placed as the rc ini in `game-copy/ci-ea169-55c696a-vanlog`; job `ea-8o-scan.json` (8k scan flow, `releaseDefaults`, no other ini keys). Init lines, every launch [verified]: `GC patches disabled (Enabled=0)` (EA default 40 MB heap, first expansion from 40,988 KB, 8 MB steps), `gc black-list cap off`, gc grow log thunks + LoadXmlAsString detour. Deviations: two non-GC stubs still install, `Route_GetOption` (installed unconditionally to answer ts3fixes' NOYIELD query) and the hostcalls door stub 0x650AB3 (the `[RouteSnap] DoorMemoX=0` key being present counts as "stub in"); neither is on an allocation path. ts3fixes stayed installed: queue.ts mod mode always installs it (cycle.ts --restart), and vanilla mode refuses an asi. ts3agent as in the vanilla matrix.

| launch | verdict | spacing when the 3.8 MB XML string came (heap) | expansions | max expand streak | max gcHeapMB |
|---|---|---|---|---|---|
| 1 | PASS 2.3 min | 40 KB (65,628 KB) | 10 | 1 | 124 |
| 2 | PASS 2.3 min | 52 KB (65,628 KB) | 9 | 1 | 116 |
| 3 | PASS 2.3 min | 60 KB (65,628 KB) | 10 | 1 | 124 |
| 4 | PASS 2.3 min | 56 KB (82,392 KB) | 9 | **4** (1028 KB request) | 116 |
| 5 | PASS 2.3 min | 48 KB (65,752 KB) | 10 | 1 | 124 |
| 6 | PASS 2.3 min | 80 KB (65,628 KB) | 9 | 1 | 116 |
| 7 | PASS 2.3 min | 48 KB (65,628 KB) | 9 | 1 | 116 |
| 8 | PASS 2.3 min | 24 KB (65,628 KB) | 9 | 1 | 116 |

- The 3.8 MB LoadMetaAutonomy string never streaked in vanilla: the spacing at that moment was 24-80 KB, far below 3.8 MB, so a black-listed span is punted at once [verified, 8 of 8]. With `InitialHeapMB=352` it was 268 KB to over 1 MB (8n, capped) and 4,868 KB in the 8m runaway: our big initial heap is what puts this request under the spacing (heap / N with a similar N) [verified for the values; the per-launch N is not logged].
- But the refusal loop itself is EA/Boehm behaviour: launch 4 expanded 4 times (8 MB each, gc_no 59 throughout, allocated since the last GC 0 KB) for one 257-block (1028 KB) pointer-free request from `Ts3Agent.Mailbox.Open` (`Instantiator..cctor`, our harness package), with the gate off, enough large bytes left from the 2nd try, finalizer rule not firing, and spacing 1080 KB just above the request: the same black-list refusal as 8m [inferred by the same elimination; inputs verified]. It ended after 4 because the small heap's new sections reached a clean slice of the 256 MB hash ring.
- Verdict: latent EA bug (an expand-only loop for a refused large request below the spacing), present at the stock heap for requests near the spacing; our 352 MB heap raises the spacing about 9x and turns a rare short streak into a 3.8 MB request that can run away (4 of 24). The cap covers both cases above it (1028 KB > 1024 KB; 8n: 0 streaks). Requests at or below the cap can still streak where the spacing exceeds them [inferred]. Turning the cap on for players is Rodney's call.

Restore verified (14:22): state identical to the pre-state (Bin pair `c035abe5a3a6` / `8aa37522782a`, ts3fixes, ts3agent, ts3-nointro, scriptCache, Options.ini, LoadOption.ini), Mods listing identical (86 files), Saves 0 differences (2602 files), no game process, 92 GB free. Logs: `probe-out/ea-eamod-1103o.*`.
