# 300. EA app 1.69 tester diagnostic kit: frame timing, GPU selection, PC check, mod timing (2026-11-20)

Branch `test/ea169-eli-diag-1120`, from the v0.2.12 EA tree (public #391 head `8363ececc`, `release/rc-1111g-ea169`,
asi 0.36.247). A player-report build for one Discord tester (EA app 1.69, TS3.exe, Apex 4.0.4 + RTX Remix bridge, S3PP,
Turbo, about 18 fps on an RTX 4080). Never a release. The goal: the kit names the bottleneck by itself while she plays
normally for 15-20 minutes; she is not asked to toggle effects, change settings or remove mods.

## 1. What is in the build (asi 0.36.247-elidiag1)

Same code and ini as the published v0.2.12 EA files, plus three read-only sections. All three are off in the shipped
`ts3native.ini` and in the code defaults; only the kit's diagnostic ini turns them on.

| Section | Source | What it measures | Writes into the game |
|---|---|---|---|
| `[PcCheck] Enabled` (code default changed to 0 here) | `pccheck.cpp` (docs/299, from `feature/modtime-diag-1120`) | CPU name / cores / RAM, power plan and overlay, AC or battery, affinity, priority, efficiency mode, per-core MHz vs limit every 10 s, other programs' CPU | nothing |
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
  world" with >= 60 sim iterations and frames.
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
- README-diag.txt: install with D, play normally 15-20 min, quit, Diagnose, send the zip. No settings or mod changes.

## 5. Open

- One EA smoke (Rodney's EA laptop, A alone + E Apex bridge): log lines `OK   framediag`, `framediag: Present timed`,
  `OK   modtime`, `OK   pccheck`, a `framediag:` line per minute, `fps-summary.txt` written, scriptErrors 0.
- Whether the Remix bridge client blocks inside Present or in other d3d9 calls: the main-thread classes answer it.
- ModTime package names: the v0.2.12 package has no ModTime.cs; the native package scan still names the packages.
