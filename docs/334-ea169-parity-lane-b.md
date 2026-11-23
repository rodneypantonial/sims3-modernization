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

## 5. [GoalValidRO] Hits / HitsShadow (gvhits.cpp, 7 rows)

`gvhits.cpp` had the only 0 rows of `[GoalValidRO]`, so ea-ini zeroed the whole section on EA (Steam ships
Enabled=1 Mode=1, Hits=0). GoalPoint_Validate 0x759B20 -> 0x75AAE0 (row mapped before) is byte-equal at **+0xFC0** over
0x759B20..0x75AF80 (5216 bytes) modulo 143 pointer / rel32 operands; every address operand inside it maps through the
table (24 table-consistent, 0 inconsistent), so the rows below are read off the block:

| Steam | EA | what |
|---|---|---|
| 0x759B26 | 0x75AAE6 | `sub esp, 0xF4` (frame size check) |
| 0x75A950 | 0x75B910 | the loop end's release call (HitsShadow observer) |
| 0x75AAA1, 0x75AAC4 | 0x75BA61, 0x75BA84 | LEAVE label, the exit's second step |
| 0x75AF4C | 0x75BF0C | the type switch: 4 targets each +0xFC0, the 0x1C index bytes after it equal |
| 0xAB88A0 | 0xAB7510 | Lot point transform the loop calls (40 / 40 callers, sigmap unique) |
| 0x1010960 | 0x107287C | the cost float the loop copies (read at 0x75A64C +0x1B5 / +0x1FF / +0x21C and the same offsets from 0x75B60C; 225 / 225 refs, 220 contexts equal) |

Code: `kGvhCodeFnv` (FNV-1a over the four code ranges `GvhCodeOk` hashes) becomes `TS3A_PICK(0xB6EAEA97, 0xA3548D56,
0xB6EAEA97)`: the Steam value recomputed from the exe equals the constant, the EA one is the same ranges on the EA image
(the ranges hold rel32 / pointer operands, so the hash differs); retail keeps the Steam value (its rows are still 0, its
own stand-down unchanged). `GvhMayInstall` loses its `ts3a_noaddr` stand-down. 2 bytes rows. EA rc ini: `[GoalValidRO]`
comes back as Steam ships it (Enabled=1, Mode=1, Hits=0, HitsShadow=0). Live check: `OK   goal validation RO`; Hits stays
off on EA as on Steam (PLAN 2026-11-21: an ON candidate for Rodney after its shadow queue).

## 6. [Binder] TypeHandleDirect (2 rows)

| Steam | EA | what | evidence |
|---|---|---|---|
| 0xDB0940 | 0xDB09C0 | Type.GetTypeFromHandle native body (86 bytes checked) | each build has two byte-identical bodies (0xDB0940 / 0xDA51B0 -> 0xDB09C0 / 0xDA5230, both at +0x80); this one has 41 direct callers in both builds, the other 1; 86 bytes equal modulo 4 pointer / rel32 operands, its call 0xD8A8E0 -> 0xD8AA40 (table row) |
| 0xDB095E | 0xDB09DE | patch window, body +0x1E | same layout |

sigmap finds neither (two equal bodies, the window is not unique): override rows (PR #598, evidence restated).
Code: the `static_assert` on the window (body +0x1E) now holds on every build; the `ts3a_noaddr` stand-down of
`ApplyBinderTypeHandle` goes; 1 bytes row (86 bytes, `ported`). EA rc ini: `[Binder]` TypeHandleDirect=1 as Steam ships it.
Live check: `OK   binder type handle direct`.

## 7. [GC] IdleGate / SubTimers / MarkHelpers (gcgate.cpp 28 rows, gcmark.cpp 35 rows)

The Boehm collector in the exe is one block: Steam 0xE49980..0xE51300 is byte-equal to EA at **+0x50** over 31104 bytes
modulo 1153 pointer / rel32 operands (0 other differing bytes). Every address operand inside it maps one to one (574
distinct targets, 74 already in the table, 0 inconsistent), so all code rows in the block are +0x50 and the data rows
are read off the instructions that use them:

| Steam | EA | what |
|---|---|---|
| 0xE49980..0xE51290 (47 code rows) | +0x50 | GC_stopped_mark, GC_finish_collection sites, GC_collect_a_little_inner, GC_try_to_collect(_inner) sites, GC_finalize, GC_notify_or_invoke_finalizers, GC_clear_marks, GC_mark_some, GC_mark_from, GC_signal_mark_stack_overflow, the 8 mark-bit sites, push / set-bit sites, start / stop world |
| 0x11F2FA8..0x11F30B4 (9 rows) | +0x5A090 (0x124D038..0x124D144) | n_attempts, mark state, mark stack too small, start hook flag / pointer, print stats, ... (as 0x11F2F90 / 0x11F2F94 / 0x11F3054 / 0x11F30D8, mapped before) |
| 0x1234338..0x1234358 (5 rows) | +0x5A380 (0x128E6B8..0x128E6D8) | mark stack base / top / limit, black lists (as 0x123435C, mapped before) |
| 0x5BFB90 | 0x8A7CE0 | GC_never_stop_func (`xor eax,eax; ret`, folded; 85 / 85 callers); the block pushes it at the same places |
| 0xD70800 | 0xD70800 | forced-collection thunk `jmp GC 0xE4A320` (EA: `jmp 0xE4A370`), the only such jmp in each build |

Over the whole image the data rows' reference contexts agree for 50% to 100% of the references per row (the rest are references outside the block whose next
instructions differ). Byte checks: the 4 IdleGate state checks (0xE4C060 12, 0xE4A022 10, 0xE49AD9 6, 0xE49E6D 6),
the push / set-bit literals and 0xE49DF2..0xE4A0A6 call sites have `ok` / `ported` bytes rows (22 new rows; `ported`
operands are the mapped data rows). The run-time built expectations were compared on both images: the 8 mark-bit site
literals (14 bytes, no addresses) are equal, the overflow routine's head (with this build's data addresses) and tail,
and the P4 store `A3 <finish pending>`.

Code: the `ts3a_noaddr` stand-downs of `ApplyGcIdleGate` and `ApplyGcMarkHelpers` go. These keys ship 0 on Steam
(`IdleGate=0`, `SubTimers=0`, `MarkHelpers=0`), so the EA rc ini keeps them 0 as well: this only removes the EA SKIP,
nothing turns on. A test of them on EA needs its own queue (not written here).
