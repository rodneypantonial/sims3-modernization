# Baseline results

Measured with the harness (`tools/probe`, `tools/harness`), not by hand. Raw data in `docs/data/`.

## Setup on 2026-10-11

- Steam build, patch 1.67 (`TS3W.exe` SHA-256 `10b2423e…0609`), all expansions and stuff packs, fresh reinstall.
- Alienware m15 R6: i7-11800H, 32 GB RAM, RTX 3070 Laptop GPU (driver 32.0.16.1692), Windows 11.
- User folder: no CC and no third-party mods. Only our harness packages are installed: `ts3agent.package` (control agent) and `ts3-nointro.package` (skips the intro videos).
- Test save `Baseline-SV-fresh`: new Sunset Valley game with the Keaton household (2 Sims), saved right after selecting the household. Backup in `D:\Sims3-backup\test-saves\`.

## Layer 1, vanilla renderer (D3D9): runs out of address space just loading the save

| Measurement | Value |
|---|---|
| GPU detection (`DeviceConfig.log`) | RTX 3070 Laptop matched, but `Texture memory: 32MB <<OVERRIDE>>` |
| Graphics options chosen by the game | `texturequality = 3`, `simquality = 4`, `treequality = 4`, `lightingquality = 3` |
| Main menu | 1,455 MB committed, 2,029 MB largest free block |
| After loading the save (live mode) | **3,490 MB committed, 176 MB largest free block, 264 MB free in total** |
| Load time | ~4 to 7 minutes |
| Crash | One run crashed at 17:54: `ACCESS_VIOLATION reading 0x00000014` at `0x005ebe31` in `FUN_005ebc50` (SimService), with **60 MB of virtual memory free** (game's `xcpt` report). That is the out-of-address-space crash (Error 12 family). |

Timeline: `docs/data/2026-10-11-vanilla-load-memory.csv` (one sample every 10 s; the jump happens while the world loads).

**Conclusion [measured]:** on this machine the unmodified game cannot reliably even load a fresh save. The vanilla layer is therefore measured as "out of address space on load", and the remaining layers start from DXVK.

## Layer 3, DXVK 3.1.1 (x32 `d3d9.dll`, installed by `tools/setup/install-fixes.ts --only dxvk-conf`)

| Measurement | Value |
|---|---|
| GPU detection | `Geforce GTX 580 [Found: 1, Matched: 1]` (DXVK's built-in TS3W profile); texture memory still reports `32MB <<OVERRIDE>>` (to check) |
| After loading the same save (live mode) | **1,742 MB committed, 1,853 MB largest free block, 1,973 MB free** |
| Load time | 52 s |

**DXVK alone frees about 1.75 GB of address space** on this save [measured].

## Still to measure

- Done: aged saves `Baseline-SV` and `Baseline-BP` (14 sim days each under DXVK); the "all known fixes" layer installed and validated on Bridgeport (below).
- Tests B to E from `02-baseline-install-guide.md` (idle FPS, travel hitches, memory after 30 minutes, save time), automated with the harness, on both aged saves, DXVK-only vs all fixes.

## Layer: all known fixes (22 default fixes, installed 2026-10-11 by `tools/setup/install-fixes.ts`)

Validation run on `Baseline-BP` (Bridgeport, aged 14 sim days), 2 sim days at speed 3 with keep_alive, declines and the 1-minute watchdog.

| Measurement | DXVK only (Baseline-BP aging) | All fixes |
|---|---|---|
| Crashes | 1 (our probe, fixed) | none |
| Sim pace | ~0.21 sim days per real minute | ~0.28 (2 sim days in 7.1 min; 0.35 in the first minute) |
| Memory in live mode | ~2.0 GB committed, ~1.58 GB largest free | 1.94–2.07 GB committed, 1.56–1.82 GB largest free |
| Launch to agent ready | 25–48 s | 105 s (S3SS, MonoPatcher, script cache rebuild; to investigate) |
| Script errors | none | 1 ErrorTrap correction log on load (references to destroyed objects repaired) |

S3SS patches confirmed installed in `S3SS_LOG.txt`: CreateFileRandomAccess, ExpandedCrashLogs, GCFinalizeThrottle, OversizedThreadStackFix, RefPackDecompressor, SmoothPatchPrecise, StartupWarningDialogFix, LotStreamingOptimizations, StoreFeaturedItemsDisable. Listed but without an install line: CPUOptimization, Mimalloc, TimerOptimization, AdaptiveWait (open). `DeviceConfig.log` still shows `Texture memory: 32MB <<OVERRIDE>>` (open).

Caveat: the DXVK-only pace comes from the aging run (with checkpoints), not from an identical 2-day window; the like-for-like comparison is part of tests B–E.

### Sunset Valley validation (all fixes, low presets)

`Baseline-SV` (Keatons, aged 14 days), same procedure: 2 sim days in 5.5 min (~0.36 sim days per minute), no crash, watchdog clean, memory 1.76–1.94 GB committed with 1.72–2.0 GB largest free. Load 82 s after the "Mod Scripts Found" dialog. The new keep_alive upkeep found 4 broken plumbing objects in the Keaton house (bathtub, toilet, 2 sinks) and repaired them.

### Graphics presets were low under DXVK [verified 2026-10-11]

Under DXVK the engine reads texture memory as 0, `GraphicsRules.sgr` falls back to 32 MB (`Texture memory: 32MB <<OVERRIDE>>`), GPU memory level 1, and first-run detection wrote texture, sim and lighting quality 1 (vanilla D3D9 picked 3–4). So the DXVK-only and all-fixes numbers above ran on low presets, and vanilla on high ones. `textureMemory` only selects the preset tier (thresholds 112/240/368 MB; it is not a VRAM budget). Fix `graphicsrules-vram` sets the fallback to 1024 MB; after re-detection (Options.ini regenerated) the game picks texture/sim/lighting quality 3, GPU memory level 4, `maxactivelots` 2. Every later run uses these presets; the low-preset runs stay as a separate row.

## Reference runs: all fixes, detected presets (quality 3), in-game autopilot [measured 2026-10-11]

From here on every run uses the in-game autopilot (holds speed 3, clears popups within a frame; `AutopilotHandlers.cs`), keep_alive with home-object repair, and the redetected presets. Earlier pace numbers are not comparable: the harness only re-forced speed 3 every 30 s, so the game ran at normal speed in between (the same SV run went from ~0.30 to ~0.40 sim days per minute with the autopilot).

| Save | Pace (2 sim days) | Memory committed | Largest free (min) | Crashes | Notes |
|---|---|---|---|---|---|
| `Baseline-SV` (Ref-SV-fixes) | 5.0 min, ~0.40 sim days/min | avg 1.88 GB, max 2.02 GB | 1.65 GB | none | 3 speed restores, 1 popup, 5 repairs, 0 stuck Sims |
| `Baseline-BP` (Ref-BP-fixes) | 6.0 min, ~0.33 sim days/min | avg 1.97 GB, max 2.12 GB | 1.52 GB | none | 5 speed restores, 1 popup; 28 stuck-Sim flags, 14 of them "Go Home" (the NPC retry loop, docs/04a) |

Resource cache on Bridgeport after the run (`/rescache`): main cache budget 200 MB, +0x94 = 405 MB, 113k entries; animation cache 5.6 of 15 MB.

For reference, without the autopilot: SV low presets 5.5 min; SV quality 3 6.6 min.
