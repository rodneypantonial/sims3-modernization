# 15. ts3native.asi: our native patches

Our own ASI plugin for patches the C# mod cannot do. Source: `native/ts3native/`. Build: `native\ts3native\build.cmd` (VS 2022 Professional, `vcvars32.bat`, `cl /LD /O2 /MT`, 32-bit, static CRT). It also builds and runs `selftest.exe`, which checks the patch and detour helpers on local memory. Output: `native/ts3native/out/ts3native.asi` + `ts3native.ini` (git-ignored).

Tags as in docs/13: **[code]** = checked in the exe bytes or decompile, **[inferred]** = to confirm by measurement.

Target: TS3W.exe Steam 1.67, SHA-256 `10b2423e2368e161774462b0ef1960231ad0af298d564a7bb6a6e78c5e880609`. PE header of `game-copy/bin/TS3W.exe`: ImageBase `0x400000`, DllCharacteristics `0x0` (no DYNAMIC_BASE, so no ASLR), Characteristics `0x123` (relocations stripped). Sections: `.text` 0x401000, `.rdata` 0xF95000, `.data` 0x1146000. **[code]**

## How it works

- `DllMain(DLL_PROCESS_ATTACH)` runs all patches once. It does nothing unless the host is `TS3W.exe` loaded at `0x400000`.
- Each patch lists its sites with the full original instruction bytes. All sites of a patch are compared first; if any byte differs, that patch is skipped and the found bytes are logged. Only then are the immediates written, with `VirtualProtect` around each write and `FlushInstructionCache`. Memory is checked with `VirtualQuery` before it is read, so a wrong exe cannot crash the plugin.
- Settings: `ts3native.ini` next to the `.asi`. Log: `ts3native.log` next to the `.asi`, rewritten each launch (version, host, settings, each patch `OK`/`SKIP`/`FAIL` with old and new values).

## Phase 1: GC patches (docs/13)

| Patch | Site | Original bytes | Instruction | New (defaults) | ini |
|---|---|---|---|---|---|
| Initial heap | `0xE4E168` (imm `0xE4E169`) in `GC_init` `0xE4E160` | `BD 00 28 00 00` | `mov ebp,0x2800` (40 MB in 4 KB blocks) | `BD 00 00 01 00` = 0x10000 blocks = 256 MB | `InitialHeapMB` (40-1024, default 256) |
| Growth step 1/3 | `0xE4A1D8` (imm `0xE4A1D9`) in `GC_collect_or_expand` `0xE4A120` | `3D 00 08 00 00 76 2C` | `cmp eax,0x800 ; jbe` | `3D 00 20 00 00` = 0x2000 = 32 MB | `GrowthStepMB` (8-256, default 32) |
| Growth step 2/3 | `0xE4A1FF` (imm `0xE4A200`) | `3D 00 08 00 00 77 05` | `cmp eax,0x800 ; ja` | `3D 00 20 00 00` | same |
| Growth step 3/3 | `0xE4A206` (imm `0xE4A207`) | `B8 00 08 00 00 50` | `mov eax,0x800 ; push eax` | `B8 00 20 00 00` | same |
| Budget ceiling (off by default) | `0x1192324` (`.data` dword) | `DC 05 00 00` | 1500, max of the per-frame GC budget `0x11922A4` | e.g. `C4 09 00 00` = 2500 | `BudgetCeiling` (0 = unchanged, else 500-20000) |

Only the trailing opcode bytes (`76 2C`, `77 05`, `50`) are in the comparison, not written. The three growth sites are patched together or not at all. Values equal to the game default (40 MB, 8 MB) write nothing. `GC_INITIAL_HEAP_SIZE`, if set in the environment, still overrides our default because `GC_init` only uses `ebp` when the env var is absent (decompile `gc13/core.c`); the plugin logs a note when it sees the variable. **[code]**

### Is DllMain early enough? Yes.

- `GC_init` (`0xE4E160`) begins `cmp dword [0x11F30D0],0 ; push ebp ; mov ebp,0x2800 ; jnz <return>`: it reads the immediate on its single real run, guarded by `GC_is_initialized` (`0x11F30D0`), which it sets to 1 at the end. **[code]**
- It has no static or CRT-initializer caller. The only direct calls come from lazy GC entry points: `0xE4A06D` and `0xE4A0CD` (`GC_try_to_collect` / `GC_gcollect`), `0xE4BAB0` and `0xE4BC07` (large-object alloc), `0xE4E47E` (`GC_enable_incremental`, called from MonoScriptHost init at `0xD7E602`). The only `jmp`s to it come from the public wrapper `0xE4E4C0` (init the allocation lock, then `jmp GC_init`), which is called from `0xE66242` and `0xE75C22`; walking callers up, both lead to `0xE5A680` (Mono runtime init), called only from `0xD7E60C` inside MonoScriptHost init (`0xD7E560`). No absolute pointer to either function exists in the file. So the first GC init happens when MonoScriptHost starts Mono, well after `WinMain`. **[code: E8/E9 and pointer scan of the whole file]**
- Ultimate ASI Loader, as `wininet.dll`, loads plugins during process start-up, before the exe's entry point (`0xF7F371`) runs. **[community: UAL design; S3SS relies on the same timing]**
- Runtime proof per launch: the plugin reads `0x11F30D0` before patching. If it is already 1, the initial-heap patch is skipped and the log says "plugin loaded too late". The growth-step and ceiling patches still apply, as both are read on every use.

## Phase 2 (not implemented): route negative cache

`InstallDetour(name, target, prologue, len, hook)` in `ts3native.cpp` writes a 5-byte `jmp` plus NOP padding and returns a trampoline (copied prologue + `jmp` back). It is covered by the self-test, but nothing uses it yet. The prologue must end on an instruction boundary and contain no relative operands.

For `Route_PlanLocked` `0x82C6E0` (docs/11): original bytes `81 EC D8 00 00 00 | 53 | 56 | 8B F1 | ...` = `sub esp,0xD8 ; push ebx ; push esi ; mov esi,ecx`. Use `len = 6` (`sub esp,0xD8` alone; no relative operands). `ecx` = this (thiscall), so the hook must be `__fastcall`-style or naked asm that keeps `ecx`. **[code]**

## Proposed install entry for tools/setup/fixes.json (not applied)

```json
{
  "id": "ts3native",
  "name": "ts3native.asi (our native patches)",
  "author": "this project",
  "category": "engine",
  "version": "0.1.0",
  "source": { "kind": "local-build", "path": "native/ts3native/out", "build": "native\\ts3native\\build.cmd" },
  "install": { "target": "bin", "files": ["ts3native.asi", "ts3native.ini"] },
  "requires": ["asi-loader"],
  "default": false,
  "notes": "Phase 1: GC initial heap 256 MB, growth step 32 MB (docs/13, docs/15). Check Game\\Bin\\ts3native.log after the first start. Remove both files to roll back."
}
```

The `local-build` source kind does not exist in the installer yet; it needs a small branch that copies from `native/ts3native/out` instead of downloading.

## Risks

- **Address space.** The 256 MB initial heap is one contiguous reservation at Mono start, and Boehm never gives heap back. That is up to 216 MB less for DXVK, textures and the world cache (Error 12). Measure committed memory and the largest free block against the baseline. If `GC_expand_hp_inner` cannot get the block, the game exits at start-up with "Can't start up: not enough memory" (no save touched). **[code]**
- **32 MB growth steps** add up to 24 MB extra peak per expansion. **[inferred]**
- **Other plugins** patching the same bytes: whichever runs second sees different bytes and skips (ours logs it). S3SS 1.6.3 does not patch these sites as far as docs/03 §3 lists. **[inferred]**
- The budget unit is probably microseconds (docs/13 Q3). Raising the ceiling costs sim-loop time each frame. **[inferred]**
