# 63. Offline function harness: TS3W.exe functions under an x86 emulator (`tools/emu/`)

Runs single functions of your own copy of the Steam 1.67 `TS3W.exe` (image base `0x400000`, no relocations) under the Unicorn x86-32 emulator, feeds them generated inputs, compares the outputs with a Python (or repo TypeScript) model and measures basic-block coverage. This is route (b) of the PLAN.md rule of 2026-10-16: an offline input/output test with block coverage counts as confirmation for pure or near-pure functions. The game is never started.

Tags: **[verified]** = shown by a harness run or the disassembly; **[inferred]** = reasoning, still to confirm.

## 1. Setup

```
pip install unicorn capstone            # tested with unicorn 2.1.4, capstone 5.0.9, Python 3.15
python tools/emu/run.py --self-check    # the three example specs below + a negative check of the harness
python tools/emu/run.py [--exe PATH] [--cases N] [--seed S] tools/emu/specs/<spec>.py ...
```

- Exe path: `--exe`, else env `TS3W_EXE`, else `<repo>/game-copy/bin/TS3W.exe`. A git worktree has no `game-copy/`; set `TS3W_EXE` to the main checkout's copy.
- Without the exe, or without `unicorn`/`capstone`, `--self-check` prints `SKIP: ...` and exits 0, so CI without game files passes. Exit 1 = a spec failed (mismatch or coverage below target).
- The ASCII cross-checks call `node tools/emu/ts-ref.ts` (Node 22.18+); without node they are skipped with a note.

**Why Python, not TypeScript:** the repo's scripts are TypeScript by default (AGENTS.md), but we know of no maintained Node binding for Unicorn 2 (the `unicorn.js` Emscripten build is Unicorn 1 era) [inferred, not re-checked], while Unicorn and Capstone ship official Python wheels for Windows, Linux and macOS. Python is already used in `tools/probe`. The harness is cross-platform and has no Windows-only parts.

## 2. What it does (`tools/emu/emu.py`)

| Piece | Behaviour |
|---|---|
| Image | PE sections mapped at their virtual addresses (`.text`, `.rdata`, `.data` incl. zeroed `.bss`, `.tls`, `.rsrc`). Globals start as in the file (no static constructors ran) |
| Imports | Every IAT slot points at its own trap address. A call through an import runs a Python stub, or fails with `unstubbed import <dll!name> called from block <addr>` |
| Stack, heap, fs: | 1 MB stack, 64 MB scratch heap (`emu.alloc`, bump allocator, zeroed per case), a TEB page behind `fs:` through a small GDT (`fs:[0]` = -1, `fs:[0x18]` = self), so SEH prologues run [verified with a snippet] |
| Call | `emu.call(target, args, conv, this=, follow=, budget=, timeout_ms=)`; `conv` = `cdecl`, `stdcall`, `thiscall` (`this` in ECX), `fastcall` (first two dwords in ECX/EDX). Args: int = dword, Python `float` = float32 on the stack, `('f64', x)` = double. Returns EAX, EDX, ST(0) (x87 top, decoded from the 80-bit register), blocks run, stub call counts, `.data` addresses read/written |
| Checks after return | Stack balanced for the convention (catches a wrong convention or argument count), EBX/ESI/EDI/EBP restored, return reached within the instruction budget and timeout |
| Strict default | Any basic block outside the target and its `follow` list (and not a stub) stops the run: `unstubbed call or jump to <addr> <name>`. Unmapped access stops it with the address and EIP. A write into `.text`/`.rdata` stops it. Writes and reads of `.data` are recorded, not fatal |
| Stubs | `emu.stub(addr or symbol name or 'dll!name', fn=lambda emu, arg: ..., ret=, pop=)`; `arg(i)` reads the i-th stack argument, `fn` may write memory and returns EAX or `(EAX, EDX)`; `pop` = bytes the callee pops (stdcall/thiscall). Symbol names come from `tools/ghidra/symbols/ts3w-1.67.symbols.csv` |
| Allocator | `emu.stub_allocator()` replaces `GeneralAllocator_Malloc 0x4E69D0` (thiscall(size, flags), ret 8), `GeneralAllocator_Memalign 0x4E6A20` (ret 0x10) and `GeneralAllocator_Free 0x4E5920` (ret 4) with the scratch heap; signatures from the symbols CSV. Wrappers above them (`EA_AllocTagged 0x4E3F90`, `ICoreAllocator_*`) must then be in `follow` and may need the allocator global, which is zero in the image [inferred, not exercised yet] |
| Coverage | Blocks per function by recursive descent from the entry with Capstone (conditional and direct jumps followed, calls not, a `jmp` to another named function is a tail call). Each executed translation block marks the CFG blocks inside it. Indirect jumps (switch tables) are listed: their targets are missing from the total until added |
| x87 / SSE | Emulated by Unicorn (QEMU). FPU control word set to the MSVC default `0x27F`, MXCSR `0x1F80` before each call |

## 3. Writing a spec

A spec is a Python file (see `tools/emu/specs/`):

| Name | Required | Meaning |
|---|---|---|
| `NAME`, `TARGET`, `CONV` | yes | label, address (int or symbol name), calling convention |
| `FOLLOW` | no | callees that run for real (and get coverage lines); everything else must be stubbed |
| `CASES` | no | random cases (default 200; `--cases` overrides) |
| `COVERAGE_TARGET` | no | fraction of the target's blocks that must be reached (default 1.0). Lower it only with a reason in the spec |
| `setup(emu)` | no | stubs |
| `cases(rng, n)` | yes | list of dicts with a `label`: edge cases first, then `n` random ones from `rng` (seeded, `--seed`) |
| `run(emu, case)` | yes | write inputs with `emu.alloc`/`wf32`/`wstr`, `emu.call(...)`, return the output (registers and memory) |
| `reference(case)` or `reference_batch(cases)` | yes | the model's output; `from emu import ts_ref`; `ts_ref([...])` runs the repo's TypeScript code |
| `compare(got, want)` | no | `None` when equal, else a message (default `==`); use it for float tolerances |
| `NOTES` | no | printed under the report: what a stub assumes, emulator quirks |

The report per spec: pass/fail counts, the first 5 mismatches, coverage of the target and each followed function as reached/total with the missed block addresses, the stubbed callees (stubbed code is **not** tested), `.data` reads/writes (zero = no global state touched for these inputs), notes, and `RESULT: PASS|FAIL`.

## 4. Example results (`--self-check`, seed 1, 2026-10-16)

| Spec | Target | Cases | Result | Coverage | Stubbed | Model |
|---|---|---|---|---|---|---|
| `fnv64.py` | `HashStringFNV64_W 0x7CFE40` (cdecl(wchar_t*) -> EDX:EAX), following `ResourceKey_HashNameFNV64 0x4AEB90` and the CRT 64-bit multiply `0xF7EB40` | 312 (12 edge: empty, 1000 chars, non-ASCII, lone surrogate, 0xFFFF; 300 random) | 312 pass | target 3/3, `0x4AEB90` 5/5, `0xF7EB40` 2/3 (missed block = the both-high-dwords-zero fast path of the MSVC `__allmul` shape [inferred]; it cannot occur because the FNV prime has a high dword of 1) | `msvcr80!towlower` ("C" locale: A-Z only) | Python FNV-1 64 over UTF-16 code units, lowercased; plus `tools/dbpf.ts` `fnv64` on the 218 ASCII cases |
| `lot_world_to_local.py` | `Lot_WorldToLocal2D 0xAB8540` (thiscall(lot, x, z, float* outX, float* outZ), ret 0x10) | 438 (38 edge: angles 0, ±π, 2^62, 2^64, ±inf, NaN; coords ±0, subnormal, ±3e38, inf, NaN; 400 random) | 438 pass, all bit-exact (also 3,038 of 3,038 with `--cases 3000`) | 1/1 (straight-line code) | none | `s = f32(sin a)`, `c = f32(cos a)`, `dx = x - ox`, `dz = z - oz`; `outX = (-0.0 - s)*dz + c*dx`, `outZ = s*dx + c*dz`, each step rounded to float32 |
| `refpack.py` | `RefPack_DecompressCore 0x4EB3B0` (cdecl(dst, dstCap, src, srcLen) -> size or 0) | 212 (200 round trips of 0 B to ~48 KB with every opcode form and header variant; 11 error cases; 1 header quirk) | 212 pass | 66/66 | none | encoder in the spec, decode checked against the original data and `refpack()` from `tools/extract-assemblies.ts` |

Findings from these runs:

- `HashStringFNV64_W` is FNV-1 (multiply, then XOR) over each UTF-16 code unit after `towlower`, offset basis `0xCBF29CE484222325`, prime `0x100000001B3`, so it matches `tools/dbpf.ts` `fnv64` for ASCII names [verified]. Lowercasing of non-ASCII depends on the CRT locale; the stub assumes the "C" locale [inferred].
- `Lot_WorldToLocal2D` is a plain 2D rotation by the lot angle around the lot origin, in float32, with sin and cos computed per call; the `-s` comes from a `-0.0` constant at `0xF988F0` [verified bytes and outputs]. It reads no globals [verified, 0 `.data` accesses]. A replacement that caches `sin`/`cos` per lot must round them to float32 the same way to stay bit-exact.
- `RefPack_DecompressCore` bounds-checks everything (source length, `dstCap`, copy offsets before the buffer) and returns 0 on any error; it does not compare the `0xFB` magic byte [verified disassembly, not tested], and it returns the size from the header even when the stream ends early (`header_size_larger`: header 9, one byte decoded, return 9) [verified]. An empty resource also returns 0, so 0 is not a reliable error signal for size-0 data [verified]. It reads no globals and calls nothing, which confirms docs/39 "pure, safe on any thread" for the inputs tested [verified].
- The harness negative check passes: calling `0x7CFE40` without allowing `0x4AEB90` stops with `unstubbed call or jump to 0x4aeb90` [verified].

## 5. What a pass proves, and what it cannot

Proves: for the tested inputs, the exe's own bytes produce the model's outputs, and the listed share of the function's basic blocks ran. With 100% block coverage plus edge cases, a replacement or a model of the function is confirmed for its arithmetic and branches, which is enough for pure functions (hashes, codecs, transforms, parsers, lookups on memory we build).

Does not prove:

- **Threads, locks and timing:** one thread, no other code runs, no races; lock functions must be stubbed. Thread safety of a function stays a claim from the disassembly (or a live check).
- **Stubbed callees:** stubbed code is not tested; the stub is an assumption (listed in the report).
- **Real game state:** globals start as in the file, not as at runtime; a function that reads `.data` sees zeroes or link-time values. Structures we build (a lot, a hash table) are only as right as our layout notes.
- **Edges not taken:** block coverage is not edge or path coverage; a block can be reached while one of its outgoing edges never is. Missed blocks are listed; indirect-jump targets are not counted.
- **The FPU of a real CPU:** x87 transcendental results come from QEMU (host `sin`/`cos` on the 80-bit value), so the last bit can differ from an Intel FPU [inferred]; and QEMU leaves `fsin`/`fcos` of ±inf unchanged where an Intel FPU returns NaN [verified for Unicorn 2.1.4; hardware per the Intel SDM]. Specs with x87 transcendentals carry a 1-ulp tolerance and a note.
- **Speed:** emulated time says nothing about game cost; use the profilers for that.
- **Self-modifying or other-module code:** other DLLs (msvcr80, d3d9, Mono) are not loaded; only stubs stand in for them.

## 6. Files

| File | What |
|---|---|
| `tools/emu/emu.py` | loader, emulator, stubs, CFG and coverage |
| `tools/emu/run.py` | spec runner, `--self-check` |
| `tools/emu/ts-ref.ts` | the repo's TypeScript reference functions (`refpack`, `fnv64`) over a JSON batch on stdin |
| `tools/emu/specs/*.py` | the three example specs |

No EA bytes are stored: specs hold addresses and models only, and the exe is read from your own copy at run time.
