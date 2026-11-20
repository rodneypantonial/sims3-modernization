# 300. EA app 1.69 tester diagnostic kit: frame timing, GPU selection, PC check, mod timing (2026-11-20)

Branch `test/ea169-eli-diag-1120`, from the v0.2.12 EA tree (public #391 head `8363ececc`, `release/rc-1111g-ea169`,
asi 0.36.247). A player-report build for one Discord tester (EA app 1.69, TS3.exe, Apex 4.0.4 + RTX Remix bridge, S3PP,
Turbo, about 18 fps on an RTX 4080). Never a release. The goal: the kit names the bottleneck by itself while she plays
normally for 15-20 minutes; she is not asked to toggle effects, change settings or remove mods.

## 1. What is in the build (asi 0.36.247-elidiag1; elidiag2 = the EA smoke 1 fixes, §6)

Same code and ini as the published v0.2.12 EA files, plus three read-only sections. All three are off in the shipped
`ts3native.ini` and in the code defaults; only the kit's diagnostic ini turns them on.

| Section | Source | What it measures | Writes into the game |
|---|---|---|---|
| `[PcCheck] Enabled` (code default changed to 0 here) | `pccheck.cpp` (docs/299, from `feature/modtime-diag-1120`, plus the PDH fix of `feature/pccheck-1120` 9461fac2: clock from `% Processor Performance` x `Processor Frequency`, limits from `Performance Limit Flags`, PowerReadFriendlyName sized; Windows 11 reports CurrentMhz = base and MhzLimit 0) | CPU name / cores / RAM, power plan and overlay, AC or battery, affinity, priority, efficiency mode, effective clock and limit flags every 10 s, other programs' CPU | nothing |
| `[Diag] ModTime` | `modtime.cpp` (docs/298) | sim-thread script time per mod (50 Hz stack sampler), hitch stacks | one simloop.cpp callback (code patch at the Simulate loop-back, existing site) |
| `[FrameDiag] Enabled` (new) | `framediag.cpp` | fps, frame p50/p99, Present wait, main / sim thread CPU, sim iterations, main-thread module samples, file I/O, GPU selection; `fps-summary.txt` | Present timing wrapper (vtable), one simloop.cpp callback |

ts3fixes stays `rel0211b` (570f8845, the v0.2.12 package): no ModTime.cs / PcCheck.cs in it, so no in-game notice and
no per-sim-hour windows; ModTime and PcCheck write every 10 real minutes (and a real hour), FrameDiag every minute.
UserDiag ops 61 (ModTime) and 62 (PcCheck) are wired as on `feature/modtime-diag-1120` (its docs/registry.md rows); this
tree's registry has no op table.

## 2. [FrameDiag]

- **Present.** At load the exe's IAT slot for `d3d9.dll!Direct3DCreate9` (found by name in the import name table, so a
  wrapper another plugin put there is chained) goes to a wrapper; on the returned `IDirect3D9`, vtable slot 16
  (`CreateDevice`) is wrapped; on the device, slot 17 (`Present`, the call `Device_Present 0x611680` makes, docs/538 §4.2)
  is timed: QPC before and after, every argument and the result passed through. Frame time = Present-to-Present
  interval; present wait = time inside Present. With the bridge, that is the bridge client's round trip plus anything
  it blocks on (frames in flight, the server, the GPU). No game address is used, so the EA port needs no table row.
- **Main thread.** A 50 Hz sampler suspends the main thread (the DllMain thread, `g_mainTid`), reads eip and 16 stack
  words, resumes it, then classifies: TS3, d3d9 (the bridge client), "wait under d3d9" (eip in ntdll / win32u /
  KernelBase with a d3d9 return address in those words), system/wait, Apex, S3PP, ts3native, other.
- **Sim thread.** One simloop.cpp callback: iterations/s and iteration ms (histogram); the sim thread id for its CPU.
- **Per minute:** `framediag:` log line, `framediag.csv` row, `fps-summary.txt` rewritten. A minute counts as "in
  world" with >= 60 sim iterations and frames (elidiag2: by the userdiag world count, §6).
- **Verdict (FdVerdict, selftest fixtures):** fps >= 50 = none; Present >= 350 ms/s or >= 50% of main samples in d3d9
  = BRIDGE / GPU side; main CPU >= 750 ms/s = main (render) thread; sim CPU >= 800 ms/s = sim thread (top script mods
  from ModTime follow); main waits >= 400 ms/s outside Present = WAITS; else mixed. Thresholds [inferred] docs/51 §545.
- **GPU selection (once):** DXGI `EnumAdapters1` (name, VRAM, LUID, outputs, software flag); integrated = < 1 GB
  dedicated or Intel < 2 GB [inferred]; `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` values for this exe and
  `Bin\.trex\NvRemixBridge.exe` (and any other NvRemixBridge.exe path). Lines: "bridge renders on <GPU>" / "bridge may
  run on the integrated GPU: set High performance for NvRemixBridge.exe", the hybrid (Optimus-style) note, and "the
  display is connected to <iGPU>" when the discrete GPU has no output. Which adapter the bridge server really opened
  is not visible from TS3; the kit collects the bridge logs (`.trex`, `.trex\logs`) for that.
- **Disk / OneDrive:** process read / write MB and ops per minute (`GetProcessIoCounters`; all files, no wait time),
  whether the user folder is under OneDrive, OneDrive.exe CPU from [PcCheck]. File wait time per minute is not
  measured (it needs file API hooks).

Cost: one QPC pair per frame and per sim iteration, a critical section per frame, a few microseconds of main-thread
stop per 20 ms sample; not measured in game yet.

## 3. EA port

- `framediag.cpp`, `pccheck.cpp`: no game addresses.
- `modtime.cpp`: three `TS3A()` sites, all with EA rows (the /DTS3A_TABLE build fails on a missing row):
  `0x011F4160 -> 0x0124E1F0` (Mono TLS slot; also read by monothread-m5.cpp and autledger.cpp, both on in the EA release
  ini, so used live on EA), GC range `0x00E4A050 -> 0x00E4A0A0` (bytes row "ported") and `0x00E50380 -> 0x00E503D0`
  ([inferred] range on both editions, only used to label samples). The retail tree's `ts3a_noaddr` guard does not exist
  on this tree and was dropped. Simloop site `0x00D81FF6 -> 0x00D823B6`, bytes row "ok".
- simloop.cpp holds 4 callbacks; on the EA release ini only MonoThread uses one, ModTime and FrameDiag add two.
- `tools/addr-codemod.ts scan`: modtime.cpp 3 wrapped, 0 unwrapped. `tools/ea-ini.ts`: the three sections kept as
  shipped (0), 0 zeroed.
- **Not smoked on EA.** No EA test machine is reachable from this host (the EA laptop is Rodney's, correctness only).
  Static checks + host selftests only.

## 4. Kit 1120.e1 (from TS3Kit 1119.2)

- Target switched to the EA app exe: `$KitEdition = 'EA app 1.69'`, `$KitExe = 'TS3.exe'`, `$KitAsi =
  'ts3native-ea169.asi'`; normal payload = the published v0.2.12 EA files (asi 3965e9a8, ini 164edf62, package 570f8845),
  `payload\diag` = the diagnostic asi + ini (v0.2.12 EA ini + the three sections on) + the same package.
- The game section reads the edition from TS3W.exe or TS3.exe (a known edition wins); 1119.2 said "no TS3W.exe" on EA.
- Collector adds: `dxdiag /t` (written into the staging folder, copied masked, raw removed), Bin `fps-summary.txt`,
  `pccheck-summary.txt`, `modtime-summary.txt`, `framediag.csv`, `pccheck.csv`, `modtime.csv`, `.trex\logs`, the user
  folder's `ApexRadiance*.txt`, and NvRemixBridge / global entries of the GPU preference key. Every copied text file
  goes through the kit's Mask.
- `test-kit.ps1`: main fake game is an EA TS3.exe; new checks for the diag outputs, the Apex log and masking. 0 FAIL
  under Windows PowerShell 5.1.
- Kit inis: the maintainer's first name in the shipped ini comments is replaced by "maintainer" (values unchanged), so the
  normal payload ini is 6a99a849 (pinned in TS3Kit.ps1), not the published 164edf62. Kit folder `TS3Kit-ea169-diag-1120`.
  Personal-info scan of every zip entry: no hits except the public repo URL (and the kit's fake mask fixtures).
- README-diag.txt: install with D, play normally 15-20 min, quit, Diagnose, send the zip. No settings or mod changes.

## 5. Open

- One EA smoke (Rodney's EA laptop, A alone + E Apex bridge): log lines `OK   framediag`, `framediag: Present timed`,
  `OK   modtime`, `OK   pccheck`, a `framediag:` line per minute, `fps-summary.txt` written, scriptErrors 0.
- Whether the Remix bridge client blocks inside Present or in other d3d9 calls: the main-thread classes answer it.
- ModTime package names: the v0.2.12 package has no ModTime.cs; the native package scan still names the packages.

## EA smoke 1 (2026-11-20, Lenovo, DXVK, no bridge): FAIL, causes
- [verified, log + code] ModTime 0 samples: the sampler only ran while ts3fixes op 61 had a sim hour open; the v0.2.12 EA
  ts3fixes has no ModTime.cs, so op 61 never came (the EA rows for TLS 0x11F4160 / GC range / sim loop exist and the sim
  loop ran at 30 it/s). Fixed: a live world is also userdiag.cpp's `world:` count above its `world quit` count; summary
  every real minute and at each world quit (it was every 10 min, so the 10:25 write was the last before the 10:31 exit).
  S3SA XOR decrypt + byte-step MVID scan ported from feature/modtime-diag-1120 1360853cc (0 MVIDs from 20 resources).
- [verified, static] FrameDiag Present not timed: EA TS3.exe's import directory is the OOA stub (`Core/Activation.dll`
  only); the real import table sits plain in .rdata (d3d9 descriptor RVA 0xD96A94, IAT slot RVA 0xBDFB00) and is filled
  at run time. Fix left to the EA laptop session: find the slot by value (d3d9!Direct3DCreate9) in non-code sections,
  retried until filled, vtable target checks.
- FrameDiag "(not in world)": follows from frames = 0 (`simIters >= 60 && frames > 0`); use the same userdiag world count.
- "d3d9 (bridge client)" is a fixed label; DXVK, the bridge client and system d3d9 differ by strings in the file
  (`NvRemixBridge` / `bridge.conf` only in the bridge client, `dxvk.conf` in DXVK and the bridge client, neither in system).
- GPU preference looked up `Sims3Launcher.exe` because the laptop harness runs TS3.exe swapped over that name (docs/230):
  look up TS3.exe / TS3W.exe in Bin too.

## 6. elidiag2 / kit 1120.e2: EA smoke 1 fixes (2026-11-20, branch `eli-diag-fix`)

asi 0.36.247-elidiag2, built for EA (/DTS3A_TABLE) with the laptop's portable MSVC, native selftest ok. NOT RUN in game.
- **Present** (framediag.cpp): the device vtable is now found from the game's own device, `[GraphicsDevice 0x11CECC8]
  +0x0C`, the pointer `Device_Present` calls through (docs/37 §1). EA 1.69 `Device_Present 0x612760` has the same bytes
  as Steam `0x611680`, and EA `GraphicsDevice_Get 0x60F340` returns `[0x01228D78]` = the existing TS3A row [verified,
  decrypted EA image]. The minute thread polls it every 20 ms until a device exists (a device made before the asi starts
  is covered). The d3d9 import path stays: Steam's import table slot, else the slot found by value (d3d9!Direct3DCreate9
  in an initialized non-code section followed by 0 or another d3d9 address; EA expects exe+0xBDFB00), once a second
  for 5 min, then `CreateDevice` is wrapped for later devices. Before any write: the vtable must sit inside a loaded
  module and slots 0-2 and 17 (16 for IDirect3D9) must point at executable code. Stand-down lines: `framediag: Present
  NOT timed, device vtable ... refused: <why>`, `... slot 17 not writable`, `framediag: another device vtable ... not
  timed`, and after 120 s without a timed Present `framediag: Present still NOT timed after 120 s: game device ...,
  Direct3DCreate9 import ...`. Only the vtable slot is used, so stock d3d9, DXVK and the Remix bridge client are alike.
- **In world:** with `[UserDiag]` on, a minute is in world when the ts3fixes `world:` lines outnumber `world quit`
  (userdiag.cpp, the count ModTime uses since 298da6c1); without it, the old test.
- **d3d9 label:** the loaded d3d9.dll's file is read once: `NvRemixBridge` / `bridge.conf` = bridge client, `dxvk.conf`
  / `DXVK_` = DXVK, else system (System32 / SysWOW64 folder) or other. Log `framediag: d3d9.dll from <folder>, N KB:
  d3d9 (DXVK)`; the main-thread class uses the same name.
- **GPU preference:** looked up for this exe, Bin\TS3W.exe and Bin\TS3.exe (when present) and the bridge server; the
  verdict names the first one with a preference set.
- **ModTime:** sampling without op 61 and the per-minute summary came with 298da6c1. New: after 30 s of live-world
  samples one line `modtime: stack walk ok: N of M live-world samples with a script frame` or `modtime: stack walk FAIL:
  ... TLS index i, x without a Mono context, y unreadable: <reason>`; the summary's first line counts samples with a frame.
- Kit 1120.e2 = 1120.e1 with the elidiag2 asi in `payload\diag`; ini, package and normal payload unchanged.
- To confirm on the EA laptop: `OK   framediag`, `framediag: Present timed via the game device` (or `via
  IDirect3D9::CreateDevice`), `framediag: d3d9.dll from outside the system folder, ... KB: d3d9 (DXVK)`, per-minute
  `framediag:` lines with fps > 0 and no `(not in world)` while in live mode, `framediag: d3d9!Direct3DCreate9 slot found
  by value at exe+0xBDFB00`, `modtime: stack walk ok`, `Windows graphics preference: Sims3Launcher.exe ..., TS3.exe ...`.

## 7. elidiag3 / kit 1120.e3: ModTime frame walk without first-chance faults (2026-11-20)

The EA live smoke of e2 passed every check, but `MdtGrab` raised 14 first-chance AVs inside its `__try` (no crash).
They took 14 of the crash log's 16 first-chance slots and would hide a real crash. elidiag3 checks every pointer of the
Mono frame walk before reading it: the TEB, the TLS expansion array, the context and each method info must be non-null,
dword-aligned and in 0x10000..0x7FFF0000 (`MdtPtrOk`); each frame must lie dword-aligned on the sim thread's stack with
both of its words inside, and strictly above the frame before it (`MdtFrameOk`). The stack range comes from
`NtQueryInformationThread(ThreadBasicInformation)` -> TEB: DeallocationStack (+0xE0C) .. StackBase (+4), read once per
sim thread id (log `modtime: sim thread <tid> stack 0x...-0x...`). A pointer that fails stops the walk without a read and
counts as an unreadable stack (frames read before it are kept). The `__try` stays as the last guard. Selftest fixtures
for both checks. [inferred] interpreter parent frames sit above their child (docs/04 F6a frame chain); if not, walks cut
after the first frame and the summary's unreadable count rises: check it in the next EA smoke.
To confirm: `modtime: sim thread ... stack`, `modtime: stack walk ok`, and no first-chance AV at modtime RVAs in the
crash log's first-chance list.

## 8. elidiag4 / kit 1120.e4: name reads checked, frame walk relaxed, unreadable reasons (2026-11-20)

The e3 EA smoke passed (stack line `modtime: sim thread 33888 stack 0x32C20000-0x32CA0000`, FrameDiag about 59.5 fps), with
two leftovers: 3 first-chance AVs, all in the name reads after the resume (2 in `MdtStr`, source 0x309 / 0xA3D5569E; 1 in
the `memcpy` of `MdtRead`, source 0x2A), and 3958 of 29491 samples unreadable (one-time line: 836 of 1500 with a script
frame, 642 without a Mono context, 360 unreadable; e2 had 18).
- **Reads:** every pointer the sampler thread reads goes through `MdtRegionEnd`: user range (0x10000..0x7FFF0000), then
  a committed, readable, non-guard region (VirtualQuery; 32 regions cached, cleared once a minute, sampler thread only).
  Struct pointers (MonoMethod, MonoClass, MonoImage, context, method info, frames) must also be dword-aligned; strings may
  be unaligned and are read up to the end of their region at most. `MdtRead` is a checked byte copy (no memcpy of an
  unchecked source); a 4-byte field read needs an aligned source. The `__try` blocks stay as the last guard.
- **Frame walk:** stock Mono 1.2's interpreter keeps each `MonoInvocation` as a local of its caller's C frame, so on one
  native stack the chain would run upward [inferred from the Mono 1.2 interpreter's structure, not reread in the EA
  binary]. e3's check (on the sim thread's TEB stack, strictly upward) still rejected about 24% of samples, so some frames
  live elsewhere or in another order [inferred: script tasks on their own stacks]. elidiag4 accepts any aligned, readable
  frame other than the one just read, caps the walk at 48 frames, and only counts off-stack and downward frames.
- **Reasons:** the one-time line and the summary add `unreadable: N bad pointer, N bad frame pointer, N frame loop,
  N fault; frames: N off the sim stack, N downward, N walks at the 48 cap`.
- To confirm on EA: 0 first-chance AVs at modtime RVAs; `modtime: stack walk ok` with unreadable back near e2 (18 per
  1500); the off-stack / downward counts tell where the frames live.
