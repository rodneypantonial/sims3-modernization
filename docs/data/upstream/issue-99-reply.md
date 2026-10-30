Thanks, the registers and the crash-file stack were enough to place this. Write-up: docs/67 §27.

**Does ts3native change when large blocks go back to the OS?** No, not with the release defaults (v0.2.0 to v0.2.4, ts3native 0.36.105):

- `[Alloc]`, our thread cache in front of EA's Malloc/Free, is `Enabled=0`. Even when on, it only caches chunks up to 1 KB, never big blocks.
- `[MemDiag] AllocSites=0`, so nothing hooks EA's heap or `NtFreeVirtualMemory`. `Light=1` only reads (a VirtualQuery walk plus heap counters).
- `[CrashForensics]`, `[ResourceCache] TrimAfterCleanup`, `[SaveCompress]`, `[ThreadStack]` and `[D3DMultithreaded]` are all off. No release feature hooks the thumbnail manager, the pose code, rigs, outfits or the resource cache.
- `[Decompress]` decodes into the game's own buffer. `[GC]` only sizes Boehm's managed heap, which never unmaps. It does change when GCs run, and so when finalizers release native handles, but that is timing only.

EA's own allocator gives big blocks back immediately: its Free of a direct (large) block calls `VirtualFree(MEM_RELEASE)` at `0x4E5306`. So a stale read of a big EA block faults like this in vanilla too. It would only return old data for a small chunk. (Apex Radiance hooks that call, so the timing differs if you run Apex.)

**Where the pointer comes from.** `0xD59C10`, the Sim thumbnail render called from `ThumbnailManager_Update`, builds your `ebx` struct on its own stack at `esp0+0x410` (`esp0 = 0x1AB350`). In that struct, `+0x40` is the rig, `+0x44` the `SceneModelFur`, and `+0x4C` the source pose. It fills `+0x4C` from a raw `+0xC` field of another object and keeps no reference. In one branch (`0xD5B745`-`0xD5B77A`) it even releases its own RefPtr to that object before the pose is evaluated. The pose sits at page offset 0x700, so it was inside a larger block that was released as a whole.

**`ts3native+0x1C992`** is not on the call path. In the v0.2.3 asi it is the return after `__security_check_cookie` in our crash-forensics hook. That hook runs only from our vectored exception handler, on an access violation or illegal instruction, and returns at once when forensics is off. So it's a stale word from an **earlier** first-chance exception on the main thread that the game survived. Your `ts3native.log` should list it on its `crash log:` lines; could you send those?

Reads that would name the owner of the free:

1. Frame check: `[0x1AB3CC]` should be `0x7CFCCB80` and `[0x1AB3F8]` should be `0x7E850E80`.
2. `[0x1AB378]` (the RefPtr local) and, if it's non-zero, 0x20 bytes of that object. Is its `+0xC` `0xA078E700`? Also `[0x1AB450]` and the vector at its `+0x20C` / `+0x210`.
3. VirtualQuery around `0xA078E000`: the free region's base and size, plus the regions just below and above (allocation base, size, state, type).
4. `[0x1AB3D4]` (the destination pose object).
5. The `crash log:` and `diag: mem` lines from `ts3native.log`, and your core's `SimOutfit.Uncache` lines from the seconds before. Did you have S3SS, S3PP or Apex installed?
6. If you can, one session with EA's stock core DLLs and the same autopilot. Your `IsValid` outfit drop is the only outfit change in the run that isn't EA's.
