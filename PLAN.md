# Plan

Approved 2026-10-10.

## 1. Compare existing fixes: done
Prior-art RE research (S3SS signatures, Mono/Boehm, routing, NRaas, world fixes): `docs/03-prior-art-re.md`.
Output: `docs/01-existing-fixes-comparison.md`. Core picks: DXVK x32 (spoofs a GTX 580 for TS3W, keeps texture memory out of the 32-bit address space) and Sims 3 Settings Setter. Sims 3 Performance Patch is optional and overlaps S3SS's stack fix.

## 2. Baseline setup and measurements: guide done, results pending
Output: `docs/02-baseline-install-guide.md`.
- [ ] Rodney runs layers 1 to 6 and fills in the results table (load time, idle FPS, streaming hitches, commit size after 30 min, save time, crashes).
- [ ] Record the results in `docs/05-baseline-results.md`. These are the numbers steps 3 and 4 have to beat.

## 3. Reverse engineer TS3W.exe and the Mono layer: in progress (first pass done)
Focus decided 2026-10-11: in-game stutter (stuck sims, threading, bad world pathing); load times are fine. Findings and ranked big wins: `docs/04-re-findings.md`, script side: `docs/04a-script-layer-stutter.md`.
- [x] Copy TS3W.exe and the script DLLs (Game\Bin, Mono assemblies) into `game-copy/` (git-ignored); never analyse the live install.
- [ ] Run REA with Ghidra: map the main sim loop, Mono GC calls, allocator use, thread creation, resource streaming and RefPack decompression.
- [x] Line up what S3SS already patches with those functions, so we only target what is left (`docs/03-prior-art-re.md`, anchors table).
- [ ] Profile live with x32dbg or Frida during tests B to D to find where the main thread spends time and where address space goes.
- [x] Write findings to `docs/04-re-findings.md`, ranked by expected gain (first pass; re-rank after the measurements listed there).

## 4. Build and test our own fixes
- [ ] Build an ASI/DLL hook (S3SS as the reference) for the top findings from step 3.
- [ ] Candidates, to be confirmed by step 3: offloading work from the main thread, trimming address-space waste further, cheaper GC behaviour, faster streaming.
- [ ] Measure each patch against the baseline table, one change at a time, and keep the ones that win.
