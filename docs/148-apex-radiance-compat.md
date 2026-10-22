# Apex Radiance compatibility (static)

Written 2026-10-22. Static only: no game run, nothing installed, nothing executed from the download. Question: can Apex Radiance (loinyx, MIT) run next to our `ts3native.asi` and `ts3fixes`, and who owns each shared site. Live test: `tools/harness/queues/apex-compat.json` (drafted, not run, §9).

Marks: **[verified]** = read in their source/binary or in ours; **[claim]** = their documentation, not checked; **[inferred]** = our conclusion, not yet seen in a log.

## 1. What was read

| | |
|---|---|
| Repo | `loinyx/Sims3-ApexRadiance`, MIT, shallow clone at tag `v2.15.0` = commit `4f7d710c` (the tag and the default branch head are the same commit) [verified `gh api`] |
| Release | `v2.15.0`, published 2026-10-21 13:37 UTC; one asset, `ApexRadiance.asi`, 10,340,352 bytes, SHA-256 `87f0a8b1d536372c7a954b088282934927c7537264cc537a64b1966ef9b2be03` [verified] |
| Handling | Downloaded to a scratch folder outside the repo; only read (text search, byte search, PE header parse). Never run. |
| Our side | `native/ts3native` (776 code-address literals, 527 distinct, `node tools/hook-sites.ts`), docs/68, release defaults in `native/ts3native/ts3native.ini` and `tools/release-features.ts` |
| Data | `docs/data/apex-sites.csv`: every code-range literal in their `.cpp`/`.h` (346 distinct; comments excluded, so it is a literal scan and not a patch list), with a class (C0..C3) where we also name the address |

## 2. How Apex Radiance works

**Entry and init order** [verified `apex_main.cpp`]. `DllMain(ATTACH)`: process check (`TS3W.exe`/`TS3.exe`), instance mutexes `Local\ApexRadiance.<pid>` and `Local\S3SSApex.<pid>` (a second copy, or an old `S3SSApex.asi`, idles), a Detours detour of the `Direct3DCreate9` export of the already loaded `d3d9.dll` (DXVK here), then an init thread. Init thread: log, game version, background HLSL precompile, S3SS detection (module strings), feature objects, config load; then **waits for the first Present plus 1 s** (20 s timeout) so official S3SS has installed its patches; then resolves game addresses (fixed table on Steam 1.67.2, masked signature scan elsewhere), installs the enabled features, installs its crash filter, and becomes a 10 ms pump (feature `Update()`, autosave, once a second `CrashReport::Refresh`). Consequence for us: ASI loader order is file-name order, so `ApexRadiance.asi` loads before `ts3native.asi`, but its game-code writes always come after our `DllMain` writes. It never sees "vanilla" bytes where we patched.

**Config** [verified `apex_paths.cpp`]: `<Documents>\Electronic Arts\The Sims 3\Apex Radiance\ApexRadiance.toml`, one `[patches.<Name>]` table per switch, a missing key reads as the default (on), an explicit `enabled = false` stays off; `apex_radiance_imgui.ini`, `ApexRadiance_LOG.txt`, `ApexRadiance_Crash.txt/.dmp`, `Diagnostics\`. It reads `S3SS\S3SS.toml` read-only (migration, S3SS intent).

**Hook mechanism** [verified]. Microsoft Detours is statically linked (`.detourc`/`.detourd` sections in the binary). Game-code sites use three layers: (1) `MemPatch::WriteBytes/WriteDWORD` with an `expected` byte array (call rel32 retargets, immediates); (2) `entry_chain` (5-byte `jmp` over a function entry, layered between Apex features; fails with "the entry bytes ... changed (hooked by another module?)" and never overwrites foreign bytes); (3) `slot_chain` (vtable slot swap; fails with "the vtable slot ... holds X, expected Y (replaced by another module?)"). A mismatch makes **that one feature** stay off with a status line and a log line; it does not abort Apex or the game. Write errors on game code run under `WriteCodeSuspended` (other threads stopped). D3D9: 15 `IDirect3DDevice9` methods detoured at the first EndScene; their doc says Detours chains compose in either order.

**Threads** [verified by `CreateThread`/`std::thread` search]: init/pump, log writer, HLSL cache worker, Room to Save watcher (64 KB stack), allocator big-block release worker, codec pools (DXT, CAS, RefPack), two lighting workers (`lot_light_bridge`, `lightmap_smooth`); developer mode adds the address-space observer and the Frame Profiler sampler/writer.

**Build flavour** [verified `build_flavor.h`, binary]: `game_alloc_categories` (hooks `0x4E3F70/90/B0`) is development-build only and its addresses are not in the release binary. Frame Profiler code is in the release binary but only runs in developer mode (off by default).

**Imports of note** [verified PE parse]: `SetUnhandledExceptionFilter`, `AddVectoredExceptionHandler`, `GetAsyncKeyState` (hotkeys), `Get/SetWindowLongW` (window subclass), `VirtualProtect`, `LoadLibrary*`, `ShellExecuteW`. No network imports (no WinInet/WinHTTP/sockets). No exports.

## 3. Features

All 15 performance switches default **on**; the lot streaming group and some dev switches default off [claim, `docs/features/performance/README.md`, confirmed in `memory_guard.cpp`/`performance_patches.cpp` where read]. "Guard" = their rule that the first calls also run the game's function and compare (16 for encoders, 64 for object lookups, 256 for the script type cache) and a difference turns that switch off for the session [claim; seen in `faster-scripts.md`].

| Switch (`[patches.X]`) | Game function / how | Guard | Their gain claim |
|---|---|---|---|
| ResourceLookupCache (+ ResourceLookupMisses) | provider lookups behind 5 vtable slots (`0x4AFFC0` family): per-key provider cache re-checked every 60 s; remembers missing keys (~36 % of lookups) | read-only class check | removes 8-16 ms frames [claim] |
| FileListCache | `0x4B1AE0` key lists of read-only packages cached | base loop and predicate bytes checked | share of CAS hitches [claim] |
| ObjectLookupIndex (+ ObjectIdMap) | id index mirroring the object service map (`0x939100/0x939170/0x938D00` entry detours, SRW lock) and a lot lookup cache (`0xC60D30` walk) | first 64 answers compared | not measured in frames [claim] |
| FastTextureCompression | DXT encoders `DxtEncode1/5`, multi-core | first 16 textures vs the game's | dominant 50 ms+ hitch counter [claim] |
| FastCacheCompression (+ FastRecordCrc) | RefPack compressor behind vtable slot `0xFB901C`, CRC-32C | decoded by the game's own decoder; first 16 | 5.5 MB write ~330 ms -> far less [claim] |
| FastCasSort | `0x5C9DE0` triangle sort | identical output | 90 ms part -> few ms [claim] |
| FastMemory | allocator CS spin 10 -> 2000, `VirtualFree` of big blocks (`0x4E5306`) moved to a worker outside the lock; trim `0x4E4F90` every 60 s + top-down flag `+0x498` (Steam, always on) | n/a | trim/top-down not measured in game [claim] |
| RoomLightQueue | room solve scheduling (`0x6xxxxx` lot priority, `0x4 ms` budget) | part off if bytes differ | hitch reduction [claim] |
| LotLightingMotion | lot lighting budget 15 ms -> 3 ms while the camera moves | camera ids parsed | [claim] |
| WallShadingWhileMoving | wall AO pass limited to one per frame | body bytes compared | 10-108 ms passes [claim] |
| SceneNodeBudget | scene drain copy with a node/ms cap while moving | whole drain compared | 25 ms+ hitches [claim] |
| MemoryGuard (Room to Save) | reserve of address space freed at save; redirects `0xAAC320`, `0xAB12AF`, `0xC6D529/537/544` calls | call targets + context bytes | Error 12 [claim], not measured |
| WindowRepaint | short jump over `InvalidateRect` in the pump `0x410890` | bytes | "not measured in frames" [claim] |
| ScriptMath | 25 float compare handlers in the Mono interpreter (`0xE54D1B..0xE58304`) inline NaN test; `mono_type_get_object` `0xEA8A00` cache, `mono_domain_free` `0xE75340` generation bump | first 256 answers | "not measured in frames" [claim] |
| NightTerrainRelight (Night Lights) and ~25 lighting parts | lamp/room/terrain light, shaders, picture filters (AO, depth blur, SMAA); hundreds of sites in `0x69xxxx-0x6Cxxxx`, `0xC2xxxx`, `0xC7xxxx`, `0xADxxxx` | each site's original bytes | visual, no FPS claim |
| Lot streaming (`LotLodStreaming`, `LotObjectThrottle`, `LotVisibilityOverride`, `LotActiveThreshold`, `LotDetailRange`) | `0xC6C290`, `0xAC1130`, `0xABFAC0`, `0xAC20E0`, remote call scope `0x7D2DB0` | | off by default, experimental |

## 4. Release binary vs the tagged source [verified by byte search, no execution]

- The tag commit equals the clone head; the PE timestamp is 2026-10-21 13:27:59 UTC, ten minutes before the release was published.
- The string `2.15.0` appears 6 times; `ApexRadiance`/`Sims3SettingsSetter` strings match the source's detection needles; Detours sections present; the PE is 32-bit with sections `.text .rdata .data .detourc .detourd .fptable .rsrc .reloc`.
- **329 of 346** code-address literals from the source are present in the binary as little-endian dwords. The 17 absent are explained by (by source file; the reason for each is inferred, not traced): 3 `game_alloc_categories` (development build only), 3 Frame Profiler targets, 5 night-lighting patch sites, 1 split-level site and 5 in `performance_patches.cpp` (`0x410890`, `0x7D8110`, `0xC6BA15`, `0xE54D1B`, `0xE58304`), probably built as base plus offset or kept as a different integer type. Nothing in the binary names a code address that is not in the source tables (not exhaustively proven: the check is source-to-binary, not binary-to-source).
- Not in the source (build-only): the author's build path in the debug info (left out here on purpose), the pre-compiled shader blob (`resources/ApexRadiance_ShaderCache.bin` is in the repo).
- Verdict: consistent with a build of the tag. Not a reproducible-build proof.

## 5. GitHub state (read-only `gh api`, 2026-10-22)

- 5 issues (2 open): #7 AO shader does not compile (Linux/Proton, open, reporter on 2.12), #3 F10 hides effects (fixed announced), #6 memory use up (answered: Room to Save protects the save), #5 Sim occlusion and F8 screenshot (reported fixed in 2.11.0, reporter says not), #1 lot light flicker (closed). Discussions disabled. 23 stars.
- No issue mentions S3SS, S3PP, NRaas, MonoPatcher, other ASIs or crashes of the game; Linux/Proton/DXVK is the only environment issue (#7). Their release 2.14.0 says Room to Save "also works with sims3-modernization installed"; 2.15.0 adds Proton/Deck/DXVK recognition and pre-compiled shaders.
- EA app 1.69.47: signature tables for it exist, with a section "what is uncertain on EA 1.69.47" and later in-game checks for some ids; Steam 1.67.2 is the studied build. We support Steam only, so only the Steam rows matter.
- Roadmap: no public roadmap file; docs mention night-lighting and "standalone split" plans in history pages only. Their `ConflictGuard` (defer, refuse S3SS pairs, watch every second) is an interface with no body ([verified `conflict_guard.h`]; the settle wait is the only implemented stage). Their architecture doc claims no overlap with S3SS/S3PP/MonoPatcher except the Split-Level `GetLotID` (Apex stands down when S3SS's fix is on).

## 6. Conflict table

Classes in `docs/data/apex-sites.csv`: C0 none, C1 default-on both sides (release), C2 ours off by default / theirs on, C3 developer-only on their side. Severity words: crash / feature silently off / double effect / none.

| # | Site | Ours (release state) | Theirs (default) | Severity | Fix |
|---|---|---|---|---|---|
| 1 | call `0xAB12AF` (target `0xAAC110`); entry `0xAAC110` | [SaveWrite] retargets the call's rel32 (on); [RouteGate] `Sites=1` count-only `jmp` at `0xAAC110` (on) | Room to Save early save preparation needs the original rel32 (`CallsTo(0xAB12AF, 0xAAC110)`) and accepts only a `jmp` at the entry that lands in `ts3native.asi` | **feature silently off** (early preparation only; the older Room to Save path stays; log: "Early save sites differ; existing world-save preparation retained") [inferred from both sources; their 2.14.0 note covers the entry jump, not our call retarget] | Ask loinyx to accept a call whose rel32 lands inside `ts3native.asi` (same test they already use for the entry), or `[SaveWrite] Enabled=0` while testing (queue job `apex-ab-saveprep`). Do not turn ours off by default: the backup offload is ours. |
| 2 | call `0xAAC320` (to `World_Save 0xC6D460`) | [FootprintIndex] retargets it (off) | Room to Save redirects it (on) | feature silently off for Apex Room to Save if ours is on (ours is installed first) | Keep [FootprintIndex] off with Apex; ts3native should refuse when Apex is present (§8) |
| 3 | vtable slot `0xFB901C` (`RefPackCompress 0x4EC200`) | [SaveCompress] swaps it (off) | FastCacheCompression swaps it (on) | feature silently off for Apex (slot check "holds X, expected Y") when ours is on | Keep [SaveCompress] off; for tests set `[patches.FastCacheCompression] enabled = false` |
| 4 | entries `0x939100`, `0x939170`, `0x938D00` | [IdCache] (off), [Verify] (off) | ObjectIdMap, same three entries (on) | feature silently off for Apex's index if ours is on | Theirs owns it (same design, first 64 answers compared, default on). [IdCache] stays off when Apex is present |
| 5 | `0xABFAC0`, `0xAC1130`, `0xAC20E0`, `0xC6C290`, `0x7D2DB0` | [LotStream] (off), [Verify] (off) | Lot streaming / LotObjectThrottle (off); S3SS LotStreamingOptimizations is on in our `S3SS.toml` (docs/68 §1.1) | none today; three owners if all were switched on | One owner: keep S3SS's. Apex lot switches and [LotStream] stay off |
| 6 | `0x4AFFC0`, `0x4B1AE0`, `0x4B2D00`, `0xC60D30` | [Verify] probe module (off) | ResourceLookupCache, FileListCache, ObjectLookupIndex (on) | none in release | Do not enable [Verify] with these probes beside Apex |
| 7 | entry `0x4E16F0` (Lock_Acquire) and other entries Apex's Frame Profiler wants (`0x599720`, `0x59A220`, `0xD819A0`, `0xE4A050`, ...) | [RouteAsync]/[RouteGate] detour `0x4E16F0` (on) | Frame Profiler, developer mode only (off) | none for players (profiler skips a hook whose bytes differ: counters missing) | Do not run Apex developer mode when measuring with ts3native |
| 8 | IAT `0xF9517C` and the process exception filter | [CrashReport] swaps the exe IAT slot and sets the kernel32 filter (off, opt-in) | Does **not** touch the exe IAT [verified: it calls through its own IAT]. Sets the kernel32 filter after start and **again every second** (`Refresh`), keeps the previous one and calls it after its own report; also a vectored handler (records only), `set_terminate` per thread | **double effect**: chain is Apex report (text + minidump) -> ours -> EA's. Ours still runs unless Apex's filter faults. S3SS `ExpandedCrashLogs` (on in our config) is a third writer [inferred order] | No change needed. Detect Apex and log "Apex crash report also active" so crash triage (docs/135) expects `ApexRadiance_Crash.txt` and a second report |
| 9 | allocator `[0x11CB864]`: CS `+0x4E4`, calls `0x4E4E81`, `0x4E504C`, `0x4E5306`, trim `0x4E4F90` | [Alloc] entries `0x4E69D0/6A20/5920/5CB0` (off); p4 reads the CS | spin count 10 -> 2000, big-block release worker, trim every 60 s, top-down flag (always on, Steam) | none while [Alloc] is off. With [Alloc] on: double effect on lock cost (our cache already avoids the lock for chunks <= 1 KB), trim only touches free core tops so cached chunks (still "in use") are safe [inferred] | A/B before shipping [Alloc] with Apex; top-down placement changes the address-space layout docs/66 measured without it |
| 10 | Mono interpreter loop `ves_exec_method_with_context 0xE53EB0`: Apex 25 handlers `0xE54D1B..0xE58304` | MonoThread P4 sites `0xE53EBD..0xE5986C` (planned/off); MonoPatcher `0xE5637D`, `0xE59218` | ScriptMath (on) | none today; **future crash risk** if P4 writes a handler Apex rewrites | Before building P4/M-fast paths, add the 25 handler addresses (Apex finds them by signature) to docs/68 and make P4 refuse when a site's bytes differ (its rule already) |
| 11 | `mono_type_get_object 0xEA8A00`, `mono_domain_free 0xE75340`; S3PP `0xEA3820`, `0xEA8720`, `0xEBBDE0` | ts3native `0xE75260/70` (M5, off) | ScriptMath type cache (on) | none (different entries, 0x80+ bytes apart) | A second managed thread (docs/30) must keep Apex's per-thread generation-checked cache valid: re-check at P4 |
| 12 | game window procedure, hotkeys, ImGui overlay | none in ts3native [verified grep: no `SetWindowLong`, `GetAsyncKeyState`, hotkeys] | subclass at first Present (outer over S3SS), F8 screenshot, F10 compare, own ImGui context | none for ts3native; harness input injection must not hit Apex hotkeys | Keep the "never focus the game window" rule |
| 13 | `d3d9.dll` (DXVK): `Direct3DCreate9` export + 15 device methods | [D3DMultithreaded] patches game code at `0x611080` (off), no d3d9 hooks | Detours on DXVK; recognises DXVK; AO shader issue on Proton (#7, open) | none | Detours chains compose with S3SS in either order [claim]; test overlay draw once |
| 14 | load order | `ts3native.asi` sorts after `ApexRadiance.asi` and after `Sims3SettingsSetter.asi` | Apex installs game code after first Present + 1 s | none: in both orders Apex sees our bytes; neither of us verifies bytes the other changed earlier except as in #1-#4 | Rule: ours writes in `DllMain`, Apex later. Test the rename `zzApexRadiance.asi` once (queue `apex-last`) |
| 15 | address space (32-bit) | route worker, P3 world images, caches | Night Lights adds RAM (their issue #6), Room to Save reserves a block, Vulkan driver guard | none direct; budget interaction | Re-measure docs/66 with Apex on; `32 GB` RAM does not help address space |

Totals: C1 (release defaults on both sides) = 1 finding that matters (#1, feature silently off) plus #8 (double effect) and #14 (order, none); C2 = 5 (#2-#6); C3/dev = 1 (#7); future = 2 (#10, #11); none = rest. No crash found statically.

## 7. Who owns what, and how ts3native detects Apex

| Area | Owner when both are present | Why |
|---|---|---|
| Object id index (#4), file/resource lookups, DXT/RefPack/CAS speed, window repaint, scene node budget, room light queue, lighting | **Apex** | done, default on, self-verifying; ours are off/probes or do not exist. We drop [IdCache] in favour of ObjectLookupIndex |
| Save pipeline: backup copy offload, flush, footprint | **ours** ([SaveWrite]); Apex keeps address-space reserve and trim | different goals; #1 is the only collision |
| Allocator | **Apex** by default (spin + deferred release); ours [Alloc] stays experimental | ours is a crash suspect and off |
| Lot streaming | **S3SS** (on in our config) | one owner; Apex and ours stay off |
| Crash report | both, in order Apex -> ours -> EA | no stand-down possible: filters chain |
| Routing, route gates, discourage, GC step, WaterJps, OptsInit, unseen throttle | **ours** | Apex names no routing function [verified: their literals in `0x61C020-0x83B3A0` are lighting, scene and resource functions from `0x688920`; none lies within 0x400 bytes of one of our routing sites] |
| Mono interpreter handlers | **Apex** (ScriptMath), ours must avoid them | #10 |

**Runtime detection in ts3native** (proposal, not built): (a) `GetModuleHandleW(L"ApexRadiance.asi")` in `DllMain` (present because of file-name order; also try `zzApexRadiance.asi`); (b) `OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\ApexRadiance.<pid>")`, taken by the owner copy in its `DllMain` [verified `s3ss_detect.h`]; (c) fall back to a module scan (Toolhelp) at the first plan or first Present, because the module list can change after our `DllMain`; (d) read `ApexRadiance.toml` `[patches.<Name>] enabled` (missing = on) for `FastCacheCompression`, `ObjectLookupIndex`, `MemoryGuard`, `Lot*`. Then: [IdCache], [SaveCompress], [FootprintIndex], [LotStream] log "stood down: Apex Radiance owns this site" and do not install; [SaveWrite] stays and logs "Apex early save preparation will stay off". The same data lets the installer (`tools/setup/install-fixes.ts`) refuse those ini keys when `apex-radiance` is installed.

## 8. Recommended settings for using both (Steam 1.67.2)

- `ts3native.ini`: release defaults as they are (`[SaveCompress]`, `[FootprintIndex]`, `[IdCache]`, `[LotStream]`, `[Alloc]`, `[Verify]`, `[MonoThread]` all 0).
- `ApexRadiance.toml`: defaults; keep `LotLodStreaming`, `LotObjectThrottle`, `LotVisibilityOverride` off (their default); developer mode off; `FastCacheCompression` off only while `[SaveCompress]` is under test.
- S3SS and S3PP unchanged (their docs and our scan find no overlap beyond those listed).
- Suggested follow-ups (not done): 1. upstream request for #1; 2. ts3native stand-down code from §7; 3. docs/68 rows (added) kept current by `node tools/hook-sites.ts --check` (now also checks the Apex data file).

## 9. Live test (drafted, not run)

`tools/harness/queues/apex-compat.json`: baseline without Apex; Apex default config loaded before ts3native (`apex-radiance`) and after it (`apex-radiance-late`, the same file installed as `zzApexRadiance.asi`); a save matrix with Apex on; a deferred A/B of #1. The harness **can** install a third-party ASI: `tools/setup/install-fixes.ts` catalog entries with `"fixes": {"install": [...]}` in a job; two catalog entries were added to `tools/setup/fixes.json` (`default: false`, pinned SHA-256). Gaps (minimal changes, not made): (1) the queue copies only `ts3native.log`; `ApexRadiance_LOG.txt` (under `<Documents>\...\Apex Radiance\`) has to be read by hand, a `job.extraLogs` copy step would fix it; (2) there is no per-job `ApexRadiance.toml` key, so "Apex features off" jobs are expressed as "Apex not installed"; a `job.apexToml` merge like `s3ssToml` would allow per-switch A/B. Pass criteria are in each job's note: no crash, no new `SKIP` in `ts3native.log`, in the Apex log only the expected #1 line, saves identical, ts3native counters unchanged versus `apex-base`.

## 10. Not checked

Binary-to-source completeness, the 17 absent literals one by one, runtime behaviour of any Apex feature, Apex's `ApexRadiance.toml` migration paths, whether Apex tolerates a renamed `.asi` (inferred from the source: it checks only the `S3SSApex.asi` name), EA app builds.
