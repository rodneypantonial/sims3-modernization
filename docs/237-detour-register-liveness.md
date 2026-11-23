# 237. Detour register liveness (2026-11-03, EA lane)

Why: `[LotSinCos]` replaced `Lot_WorldToLocal2D` (Steam `0xAB8540`, EA 1.69 `0xAB71B0`) with a `__fastcall` C++ hook. That hook writes edx, which the x86 ABI calls volatile. But the original never writes edx, and MSVC whole-program optimisation let some callers keep the lot pointer in edx across the call. The result was a crash on EA 1.69 (run 7j2, in `0xAB7C80`) and silently wrong heights on Steam. Any detour that replaces or wraps an EA function with ABI-following code can have the same bug when the original keeps a volatile register (eax, ecx, edx, xmm0-7, flags) and a caller relies on that. This doc describes a static checker for that bug class and its results on both editions. It is a static audit only: no game runs.

## Tool

`python tools/reg-liveness.py [--edition steam|ea169|both] [--addr 0x...] [--all] [--modules] [--csv FILE]`. Self-check: `--self-check` (synthetic bytes, then the validation case below on the local images), wired into `tools/selftest-all.ts` as `tools/selftest/reg-liveness/run.ts` (local only: needs python 3 and iced-x86). A full run on both editions takes about 90 s.

Method, per site:

1. **Sites.** Every TS3W.exe code literal in `native/ts3native` (`node tools/hook-sites.ts`, 875 Steam addresses), mapped to 1.69 through `ts3addr-ea169.h` (port worktree). Steam image: `game-copy/bin/TS3W.exe`. 1.69 image: `probe-out/ts3-ea169.bin`.
2. **Kind.** An `E8` at the site means a patched call site (`PatchCalls`, for example WaterJps or SaveWrite): the replaced function is its target and the only caller is the site. A function with direct `E8` xrefs is an entry detour candidate, and its callers are those xrefs (byte scan of `.text`). Anything else is mid-function or reached only through pointers (vtables, Mono internal-call slots, function tables). That last group is listed as checked but not analysed. Inline stubs own every register anyway, and pointer callers cannot rely on more than the ABI.
3. **Preserved set.** These are the volatile registers the original never writes. The walk follows jumps, tail calls, switch tables and direct calls, transitively. Indirect calls and import thunks count as writing everything. The bounds are 40000 instructions per function and 30 call levels. Hitting a bound can only make the preserved set too big (a false alarm), never too small.
4. **Live across the call.** Starting at the return address, the checker looks at every path for the first access to the register. If it is a read, the register is live. The search also descends into direct calls that read the register on entry (a value passed in a register). Scalar SSE merges (`cvtsi2ss`, `movss xmm,xmm` and similar) count as writes. A search that hits its 4000-instruction bound is printed as `live search bound`.
5. **Hook side** (`HOOKS` in the script, reviewed by hand). This side is conservative. Compiled C++ reached before the caller gets control back is assumed to write eax, ecx, edx, xmm0-7 and flags. A naked stub counts only for the registers it saves around every C++ call. `nohook` marks an address that we only call or read, or that we reach through a pointer-slot swap. An unlisted site with a live caller prints as `UNREVIEWED` and counts as ABI C++.

Limits: the checker does not follow a caller's own callers. A caller that never writes the register passes the reliance up, and the checker does not check that. eax at the caller's `ret` is not counted. Tail-jump (`E9`) xrefs are not counted either. Addresses built at run time (pattern scans, for example the autshadow F2 format hook) are not in the hook-sites list. Those are JIT or Mono-convention code, not WPO callers. The hook side is a review, not a disassembly of our asi.

## Validation

The checker must flag LotSinCos, and it does on both editions. Steam callers `0xAB9037`, `0xAC0C0F`, `0xAC586B` and `0xAC996D` keep edx live (plus `0xAB9437`). The 1.69 call `0xAB7CA7` inside `0xAB7C80` (the 7j2 crash function) keeps edx live, as do 4 more calls. The self-check asserts both.

## Results

18 risky (hook, register, caller) triples. All of them are on `Lot_WorldToLocal2D`. No other hooked function has a caller that relies on a register our hook may write.

| Hook | Edition | Live register | Callers (call site, in function) | Risk | Shipped ini (`native/ts3native/ts3native.ini`) | rc ini (`game-copy/ts3native.ini.rel025b`) |
|---|---|---|---|---|---|---|
| `[LotSinCos]` `0xAB8540` | Steam | edx | `0xAB9037` (in `0xAB9010`), `0xAB9437` (`0xAB93F0`), `0xAC0C0F` (`0xAC0B10`), `0xAC586B` (`0xAC5610`), `0xAC996D` (`0xAC9460`) | **high**: the C++ hook writes edx (wrong heights seen) | Enabled=0 | Enabled=0 |
| `[LotSinCos]` `0xAB8540` | Steam | xmm4, xmm5 | `0xAC0C0F` (splatted constants loaded at `0xAC0BA7`/`0xAC0BAF`, read at `0xAC0C41`/`0xAC0C47`) | **possible**: only if LscHook's code uses xmm4/xmm5 | Enabled=0 | Enabled=0 |
| `[LotSinCos]` `0xAB71B0` | EA 1.69 | edx | `0xAB7CA7` (in `0xAB7C80`), `0xAB80A7` (`0xAB8060`), `0xAC028F` (`0xAC0190`), `0xAC4FBB` (`0xAC4D60`), `0xAC90BD` (`0xAC8BB0`) | **high**: crash 7j2 | Enabled=0 (EA test inis set it) | Enabled=0 |
| `[LotSinCos]` `0xAB71B0` | EA 1.69 | xmm4, xmm5 | `0xAC028F` | **possible** | Enabled=0 | Enabled=0 |
| `[Verify]` worldToLocal probe `0xAB8540` | both | xmm4, xmm5 | `0xAC0C0F` / `0xAC028F` | **low**: the VF_STUB and VfLeaveThunk stubs keep eax, ecx, edx and flags, so edx is safe, but xmm is not saved around VfEnter/VfLeave. The probe is also skipped while `[LotSinCos]` owns the prologue | Enabled=0 | Enabled=0 |

**v0.2.5 Steam release: not affected.** None of the risky hooks is on (Enabled=1) in the shipped ini or the rc ini. The fix for `[LotSinCos]` (in progress on `ea169-port`) must keep edx and xmm4-7 for the caller. One way is a naked stub that saves edx and xmm4-7 around the C++ body. Another is to write the result without touching them.

### Checked, no risky caller

On Steam: 105 patched call sites and 378 entries were analysed (5 hit the write bound, harmless as noted in step 3; 1, the Satb value-type test `0xE52160`, hit the live-search bound and is not hooked), and 392 sites were mid-function or pointer-only. On 1.69: 105 call sites, 373 entries and 386 mid-function or pointer-only sites, plus 11 addresses with no 1.69 row (crashfx 5, fgro, routesnap, satb, savecompress, threadstack, unseenfbc, ts3native: those features stay off on 1.69 per docs/232). By family (Steam; `--modules` prints the full list):

| Family (module) | Sites analysed (call site / entry / mid or pointer-only) | Risky |
|---|---|---|
| RouteGate stubs (`routegate`) | 0 / 15 / 3 | 0 |
| RouteAsync detours, host call, Lock_* (`ts3native`) | 0 / 7 / 37 | 0 |
| LotRemoveGuard and RouteSnap IC stubs (`routesnap`) | 18 / 29 / 34 | 0 |
| FootprintIndex (`footprint`) | 2 / 3 / 2 | 0 |
| WaterJps call patch (`waterjps`) | 1 / 2 / 2 | 0 |
| UnseenFallback (`unseenfbc`, `unseen`) | 3 / 15 / 10 | 0 hooked (`unseen` calls `0x935770`) |
| AutShadow (`autshadow`, `autshadow-run`) | 1 / 8 / 0 | 0 hooked (calls `0x935770`) |
| MonoThread (`monothread`, `monothread-m5`) | 1 / 25 / 12 | 0 |
| Decompress, SaveWrite, SaveCompress | 11 / 16 / 3 | 0 |
| GC (`gcthreads`, `gcroots`, `memexp`) | 1 / 7 / 5 | 0 hooked (memexp swaps the GC notify pointer) |
| Alloc, AllocSites, pools, arenatail | 1 / 19 / 21 | 0 |
| Verify probes (`verify`) | 0 / 61 / 30 | worldToLocal only (above) |
| Satb, values, loadprof, routephases, rtsites, steerro and the rest | see `--modules` | 0 hooked |

IC-slot hooks reached through Mono's internal-call path (P10, A10, replandedupe, unseen IC detours, obstructro) follow the Mono convention. They fall in the pointer-only group or have no live caller.

### Called, never hook with ABI C++

These EA functions are not hooked today. We call them or read them, but callers rely on them keeping a register. Any future detour on one of them needs a register-saving stub (counts are the same on both editions):

| Address (Steam) | Our use | Live registers (callers) |
|---|---|---|
| `0x00580420` | EaCrashFixes GeomNoMaterial getter | ecx (6), edx (1) |
| `0x006C93D0` | crash forensics resource getter | ecx (9) |
| `0x007EAEA0` | SteerRo vtable slot check | ecx (3) |
| `0x0081C880` | SteerRo option test | edx (3) |
| `0x008292D0` | SteerRo world getter | ecx (3), edx (2) |
| `0x00935770` | component getter (autshadow, rtoro, unseen) | ecx (55) |
| `0x00938B50` | Discourage object service getter | ecx (4), edx (6) |
| `0x00953690` | AvoidRo posture getter | ecx (3) |
| `0x00C0FD60` | GC notify (pointer slot swapped; direct callers keep the original) | ecx (28), flags (12), eax (3), edx (1) |
| `0x00E52160` | Satb value-type test | edx (3) |

## Rule for new hooks

Proposed for docs/68 §6: before a detour on an EA function goes in, run `python tools/reg-liveness.py --addr <steam addr>`. If it prints a live caller, the hook needs a naked stub that saves those registers around all C++ code (before and after the original), or a review entry in `HOOKS`.
