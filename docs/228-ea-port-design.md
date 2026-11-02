# 228. EA app 1.69.47 port of ts3native: design (2026-11-02 PHT)

Question: ship EA app 1.69.47 support with the least new machinery and no risk to the Steam build. Inputs: docs/226
(coverage of the first EA image), docs/68 §7-§7.5 (tools), docs/227 (mod-version popup), `out/port/ts3-ea169-file-fec39efa/`
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
  (sigmap: 24 features fully mapped, docs/226). These are the first-smoke pool.
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
- 186 `ai=yes`: 44 by-value IC rows -> `gen` takes patch-bytes `ported (by-value)` as table rows (port-run already has them).
  142 left -> `0` in the table + their 42 files OFF on EA. Resolve only the ones a wanted feature needs, in Ghidra on the main
  laptop (`probe-out/ts3-ea169-file.exe` + `ghidra-names.csv`), recorded in `docs/data/ea169-overrides.csv`
  (steam,ea,label,evidence; addresses only) that `gen` reads. The 6 ts3native.cpp rows (0x676340.., 0x74CFC0, 0xE4E168) and
  3 `ts3native.cpp` data rows come first: they sit outside any feature switch. [inferred: check at task 5]
- 5 mismatches, 9 ambiguous, 3 missing, 17 near: `near` passes `--allow-inferred` and is protected by `Matches`; the others
  stay 0 -> their files (eafix, watchpt, crashfx, gcroots, routephases, allocsites, steerro, p4clone, avoidro, memexp, rtoro)
  OFF until hand-verified.
- 120 `ported` byte expectations: expected bytes embed Steam rel32/pointers, so `Matches` SKIPs on EA (safe, feature inert).
  Not in this milestone; later a per-build byte table from the same `gen`. First smoke uses only files whose expectations are
  all `ok`.
- 407 slot-shifted vtables, 5 offset-changed sites: slot numbers and struct displacements are not addresses, the codemod does
  not touch them. steerro, watchpt, rtoro, memexp, lotfd (vt_Portal) OFF on EA; re-derive by hand later (docs/226 reading).
- Gating rule: EA ini = Steam ini with every section `Enabled=0` except an allowlist in build.yml (same awk style as the
  release check); CI fails if an allowlisted section's module file appears in the header's `// unresolved` list.
- Steam identity proof: the `--apply` commit is a separate PR; CI artifact of its parent (main) vs the PR artifact: sha256 of
  the `.text`/`.rdata` sections (parsePe from tools/exe-addr-map.ts, `node -e`) and `ts3native.map` minus the timestamp line
  must be equal. Build.cmd already writes the .map.
- Table review/regeneration: `node tools/port-run.ts --exe probe-out/ts3-ea169-file.exe --allow-inferred` writes
  `native/ts3native/ts3addr-ea169.h` (sorted pairs + `// unresolved: 0x... file:line` comments, no EA bytes); review = git diff.
  Rerun after any merge that adds literals.
- Managed lane (separate, after native smoke): extract the 1.69 assemblies on the EA laptop (`tools/extract-assemblies.ts`,
  into a gitignored dir), `port-report` managed stage -> `.managed.csv` (ts3fixes methods changed), rebuild ts3fixes with
  `REF_DIR` pointed at 1.69 -> `ts3fixes-ea169.package`. Version-1 S3SA header, so no mod-version popup from ours (docs/227);
  the popup itself comes from 1.67 core mods and is answered by the harness guard in docs/227 §2.

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
| 4 | `--apply` on the branch, identity proof vs main artifact (`.text`/`.rdata` sha256, .map diff) | native/ts3native/*.cpp (79 files), docs/68 §7.1 note | sonnet | 30 min | 0 | both hashes equal; second scan 0 `wrap` rows |
| 5 | resolve the 9 ts3native.cpp rows + any zero row in the smoke pool in Ghidra (main laptop), `docs/data/ea169-overrides.csv` | docs/data/ea169-overrides.csv, tools/addr-codemod.ts (read overrides) | reasoning | 2 h | 0 | port-run: those rows leave the unresolved list with evidence text |
| 6 | first smoke (§6) | ts3native-ea169.ini on the EA laptop | reasoning | 1 h | 1 (+1 rerun) | log: host line, no `SKIP all`, every on-feature logs its install line, no `FAIL`, reach the main menu and a lot |
| 7 | widen: features with only `ok` expectations and no zero rows, one per run | allowlist in build.yml | sonnet | 30 min each | 1 each | same as 6 plus the feature's own first log line |
| 8 | managed lane (§3 last bullet) | tools/setup/build-fixes.ts (REF_DIR arg), docs/227 | reasoning | 3 h | 1 | `.managed.csv` reviewed; game starts with ts3fixes-ea169 and no popup |
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
features can go on. - steerro/watchpt struct layouts: rewrite, not port (docs/226).
- Not checked by the design: the ini-section-to-source-file map the task 3 allowlist assert needs (by hand), and whether
  selftest.exe compiles under `/DTS3A_TABLE` (task 3 skips it). Risk: a feature left ON whose data read hits a zero table
  row crashes at load; the ini allowlist + CI assert is the only guard.
