# 334 EA app 1.69 parity, lane B: ThumbStall, UnseenAnim, EqualsFast, GoalValidRO, Binder, GC

2026-11-23, static only (no game run). Branch `feature/ea169-parity-b` from main 56acf732. Ledger §589.

**Goal.** Sections that ship ON in the Steam 1.67 ini but were zeroed in the EA app 1.69.47 rc ini because their
`ts3addr-ea169.h` rows were 0 (docs/232 §8) get EA rows and run on EA. Lane A (`feature/ea169-parity-a`) owns the
eafix / wastar / QuitStop / BoundCall / AutShadow Races rows; this lane does not touch them. Draft PR #598 (2026-11-20)
mapped most of these rows on an older base; its values were re-derived here with the checks below and agree row for row.

## 1. Method

- Images: Steam `game-copy/bin/TS3W.exe` and the decrypted EA memory image `probe-out/ts3-ea169.bin` (+ `.bin.json`),
  read-only; `ts3-ea169-file.exe` (the same dump in file layout) for `port-run.ts`.
- Candidates: `node tools/port-run.ts --exe <EA file exe> --ref <Steam exe> --allow-inferred --zero-unresolved --table
  out/... --overrides docs/data/ea169-overrides.csv` into `out/` (not committed); only this lane's rows were copied into
  the committed tables (a full regen changes rows of other lanes). `tools/sigmap.ts gen --sites` / `match` on the same
  rows as a second, independent map.
- Code rows [verified] when all hold: (a) 10 instructions at both addresses have the same shape (mnemonic, registers,
  immediates and displacements; image addresses masked; iced-x86); (b) every address operand of those instructions maps
  through the table to the EA operand; (c) the same number of direct callers (`E8`) and absolute references; (d) the
  match is unique: a unique sigmap hit, or the enclosing block is identical to the EA block at one constant delta
  modulo pointer and rel32 operands (whole-block byte compare), or the call graph pins it (the only callers / callee).
- Data rows [verified] when the Steam instructions that reference the Steam address and the EA instructions that
  reference the EA address have the same shape and count (context votes), or the slot holds the mapped function.
- Bytes: rows for the new sites come from the same `port-run` bytes table (kind `ok` = same bytes, `ported` = differs
  only in pointer / rel32 operands); each `ported` row's operands map through the table. Expectations built at run time
  (gcmark's mark-bit sites, its overflow head, the P4 store) were compared on both images by hand.
- Hook-site safety: `tools/reg-liveness.py` (branch `feature/ea-reg-liveness-cc-image-scan`, PR #661) on every row of
  this lane, both editions, table = the new rows: per site the same kind, preserved set and RISK result on Steam and
  EA (0 differences). The only RISK rows are `0x44C470` and `0x5BFB90`, which this lane calls or compares, never hooks.

## 2. [ThumbStall] / [ThumbCallers] (17 rows)

| Steam | EA | what | evidence |
|---|---|---|---|
| 0xD5A9C8 / D2 / DD / 0xD5AA25 | 0xD5A9E8 / F2 / FD / 0xD5AA45 | state 0x11 site, keep, go, abort | block 0xD5A900..0xD5AA80 equal at +0x20 modulo 7 pointer operands; sigmap unique for 3 of 4 |
| 0xD5C830, 0xD614E0, 0xD61670 | 0xD5C850, 0xD61500, 0xD61690 | UI request, household request, manager Update | sigmap unique; callers 4/4, 2/2; 0xD61690 is slot 7 of the EA vtable |
| 0x5A80A0, 0x5A80A9 | 0x5A8750, 0x5A8759 | CASUtils_SimBuilderBuildModelImpl entry, return after its call | internal-call table row {fn, name} with the same name string in both builds; sigmap unique (0x5A80A9) |
| 0x44C470 | 0x44C5D0 | getter `mov eax,[g]; ret` called by 0x5A80A0 | 386 / 386 callers; 0x5A8754 calls it. sigmap picked a look-alike getter (0x60F340, 194 callers) |
| 0x5A9E90 | 0x5AA510 | CASUtils_SetOutfitInGameObjectDelayedImpl | IC table entry, same name; sigmap unique |
| 0x5EDB30, 0x5F12A3 | 0x5EE670, 0x5F1D73 | ready-job pick, its call site | sigmap unique; 1 / 1 caller |
| 0xFE514C | 0x10306B4 | thumbnail service vtable | both constructors store it with the same two secondary vtables at -8 / -0x2C |
| 0x1088F48 | 0x10D3938 | manager vtable slot 7 | holds 0xD61690 = TS3A(0xD61670), unique dword (override row, from PR #598) |
| 0x11CEB88, 0x11CEB8C | 0x1228C38, 0x1228C3C | two globals the log reads | 7 / 7 and 8 / 8 reference contexts equal |

Code: the five inline-asm literals get EA values (`TS_ASM_*`, `TC_ASM_*`: 0xD5A9F2 0xD5AA45 0xD5A9FD 0x44C5D0
0x5A8759, `static_assert` to `TS3A`), the two `ts3a_noaddr` stand-downs go. 9 bytes rows. EA rc ini: `[ThumbStall]`
Enabled=1 as Steam ships it, `[ThumbCallers]` stays 0 (Steam 0). Live check: `OK   thumb stall (EXPERIMENT, [ThumbStall])`.

## 3. [UnseenAnim] / [AnimPhase] (19 rows)

| Steam | EA | what | evidence |
|---|---|---|---|
| 0x940569 0x94059E 0x940742 0x94118B | 0x940B09 0x940B3E 0x940CE2 0x94172B | AnimPhase call sites (advance, notify, evaluate, publish) | block 0x940500..0x941200 equal at +0x5A0 (846 instructions, in-block branches keep the delta); sigmap unique for 3 of 4 |
| 0x942CC6 0x942CF1 | 0x943266 0x943291 | pose call sites | sigmap unique, operands map |
| 0x583D00 0x5833C0 0x959670 | 0x5832E0 0x5829A0 0x959D80 | advance, evaluate, publish targets | sigmap unique; 1/1, 2/2, 4/4 callers; 0x583D00..0x583E00 equal at -0xA20 |
| 0x568710 | 0x5682B0 | clip event dispatch (vtable slot +0x10) | sigmap unique; its 3 Steam callers 0x568A6C 0x568B28 0x56FA98 = the 3 EA callers 0x56860C 0x5686C8 0x56F488 |
| 0x568A6C 0x568B28 0x56FA98 | 0x56860C 0x5686C8 0x56F488 | clip event call sites 1-3 | caller sets one to one (above); block 0x568700..0x568C00 equal at -0x460; sigmap near/ambiguous (site 3: 0x56F3C8 calls the slot +8 clone 0x568260). Override rows (PR #598) |
| 0x571A50 0x571E0F 0x571E1A | 0x571380 0x57173F 0x57174A | drain / event sites | block 0x571A50..0x571F00 equal at -0x6D0 |
| 0x577060 0x742710 0x801E51 | 0x576A70 0x743590 0x8010E1 | pose / root-motion sites | sigmap unique, 2/2 and 1/1 callers |

Code: the `ts3a_noaddr` stand-down of `ApplyAnimPhase` goes. 11 bytes rows (incl. the locomotion site 0xD81C5C ->
0xD8201C, whose row was mapped before but had no bytes row: Simulate 0xD81840..0xD82100 equal at +0x3C0). EA rc ini:
`[UnseenAnim]` Enabled=1 as Steam ships it (Objects=0), `[AnimPhase]` stays 0. Live check: `OK   unseen anim: ready`.

## 4. [EqualsFast] (2 rows)

| Steam | EA | what | evidence |
|---|---|---|---|
| 0xE7A590 | 0xE7A5E0 | ValueType.InternalEquals internal call (detour, prologue `55 8B EC 83 EC 10`) | sigmap unique; 0xE7A590..0xE7A700 equal at +0x50 modulo 5 pointer operands; same slot of the Mono internal-call function table (neighbours 0xE81DB0 / 0xE7A410 map to 0xE81DD0 / 0xE7A460) |
| 0xE70180 | 0xE701E0 | the class field walk it calls | 38 / 38 callers (paired deltas +0x20..+0x3C0, the Mono code shifts), the call at InternalEquals +0x4A in both, 0xE70180..0xE70260 equal at +0x60 modulo pointers; sigmap none (window) |

Code: the `ts3a_noaddr` stand-down of `ApplyEqualsFast` goes; 1 bytes row (the 6 prologue bytes, `ok`). EA rc ini:
`[EqualsFast]` Enabled=1 as Steam ships it (SmallEnums=0). Live check: `OK   equals fast (docs/258 option C)`.
