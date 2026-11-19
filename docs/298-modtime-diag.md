# 298. `[Diag] ModTime`: which script mods use the sim thread (2026-11-20)

Status: built + host tests; in-game smoke see §7. Branch `feature/modtime-diag-1120` on the retail test basis
(`feature/abhourly-diag-1119` = `test/retail167-tester-1119` + docs/294, plus the kit fix `bugfix/kit-userdir-pick-1119`).
Not for merge to master as is (old basis); the PC check part (docs/299) is meant for master. Ledger §544.

## 1. Why

A retail tester (743 packages, 287 script mods incl. the full NRaas set, 4-thread i5) has the sim thread ~95% busy in both
arms of the docs/294 A/B, ~122 script hitches per hour and idle Sims. The AbHourly rows say "script", not which script.
Rodney 2026-11-20: no bisect, build the diagnostic first. This names the mods from inside the game, without the player
removing anything.

## 2. What it does

`[Diag] ModTime=1` (default 0; needs `[UserDiag] Enabled=1` and a ts3fixes with `ModTime.cs`), `ModTimeHz` 10-200 (default 50).

- **Sampler** (`modtime.cpp`, one thread, above-normal priority so a saturated PC does not bias it): every 1/Hz s it suspends
  the sim thread, reads `eip` (`GetThreadContext`, which also makes the suspend take effect) and up to 48 Mono interpreter
  frames, resumes it. While it is suspended: no lock, heap or log; every read under SEH. Frame walk = autledger.cpp
  `AsLgFrames` (TEB TLS slot `[0x11F4160]` -> context, +8 current frame, frame[0] parent, frame[1] info, info +8
  `MonoMethod*`) [verified docs/04 F6a]; the sim thread's TEB is taken by the Simulate-iteration callback on that thread.
- **Names** (after the resume, cached): `MonoMethod` +8 class, +0x18 name; `MonoClass` +0 image, +0x34 / +0x38
  name / namespace [verified docs/104 §4]; `MonoImage` +0x18 assembly name, +0x1C module name [inferred I-544-1],
  +0x50 `#GUID` heap = MVID [verified docs/104 §4].
- **Weight** of a sample = the sim thread's CPU cycles since the previous sample (`QueryThreadCycleTime`). Waits weigh 0,
  so the shares are shares of sim-thread CPU, not of wall time (I-544-2).
- **Owner** of a sample (`MdtPick`): the innermost frame of a mod (not EA, not ours), else the innermost ts3fixes frame,
  else the innermost frame (game). No script frame = "engine" (native game work outside script). EA = the 11 game
  assembly names (`MdtGameName`); ours = `Ts3Fixes`. A mod's cost includes the EA code it calls (that is the cost of using
  the mod); EA code that no mod called stays "game".
- **gc/alloc**: `eip` inside `0xE49A00-0xE51380` (Boehm code around the verified functions 0xE4A050 .. 0xE50380,
  docs/130 §2) adds the sample to the owner's gc share [inferred I-544-3].
- **Hitch stacks**: a Simulate iteration of 250 ms or more keeps its samples' top 3 owner: method, with how many samples were
  on the CPU and in gc. One `modtime: hitch` log line each (cap 2000), worst 20 in the summary.
- **Mod -> package**: one scan on a lowest-priority thread at the first summary: every `.package` under `<user>\Mods`
  (`UserSims3Dir`, then the other `Documents\Electronic Arts\*\Mods` folders, so localized folder names work), S3SA
  resources (type `0x073FAA07`; RefPack decoded with `RefpackRef`), MVID = first `#GUID` entry of the metadata root
  (`MdtMvidOf`), matched to the loaded image's MVID. Package names are relative to Mods (no user paths).
- Only while a world is live (op 61 hour open; after world quit nothing is sampled).

## 3. Files (next to ts3native.log, Game\Bin)

- `modtime-summary.txt`: sample counts, measured overhead, gc share; top 20 owners (cpu %, gc %, samples, kind, package);
  top 10 methods of each of the top 5 mods; top 10 game methods; worst 20 hitches; the docs/299 PC check block.
- `modtime.csv`: per sim hour the top 30 owners (`session,sim_hour,rank,name,kind,package,pct_of_sim_cpu`).
- Written by the sampler thread (never file work on the sim thread): at each sim hour (ts3fixes op 61 hi 1), world quit
  (hi 2) and every 10 real minutes (a game closed without a world quit).
- One in-game notice per game session at the first world load: "Mod timing diagnostic running ...".

## 4. Cost

Per sample: one suspend / context read / <= 48 frame reads / resume. The summary prints the measured stop time per sample
and per real second (`overhead (measured)`). Expected ~10-30 us per sample, so ~0.05-0.15% of one core at 50 Hz [inferred,
replaced by the smoke number in §7]. Name lookups are cached (8192 methods, 512 assemblies).

## 5. Limits

- A core mod that replaces an EA assembly (same assembly name) counts as game; its package shows in the package column.
- Native work our ts3native hooks do under EA frames counts as game / engine.
- Samples of one iteration are assigned by the iteration counter at sample time: a sample right at an iteration end may go
  to the next one.
- Retail and Steam 1.67 only (TS3A rows exist for both; EA 1.69 builds stand down without the table row).

## 6. Kit

Kit 1120.1 (`tools/player/ts3-kit`, kit 1119.2 + PC preflight): `payload\diag` = `ts3native-retail167.asi` (this branch,
retail), `ts3fixes.package` (this branch, experimental tuning kUnseenPark, kBouncerSlotSkip, kTakeARiskGuard as the kit's
normal package), `ts3native.ini` (kit ini + `[Diag] ModTime=1`, `AbHourly=0`, `[EaCrashFixes] KeepOptionsOnGpuChange=0`,
`[PcCheck] Enabled=1 Notify=1`). Answer D installs it. README-diag.txt names the files to send back.

## 7. Tests

- Native selftest `TestModTime` (MVID from a metadata root incl. a truncated one, kinds, the pick order, ordering, hitch list).
- Queue `tools/harness/queues/modtime-diag-smoke-1120.json` (correctness smoke on our Steam install, Bin state apex): the
  summary must name real assemblies (ts3fixes / EA / NRaas when present), overhead line present, 0 script errors.
