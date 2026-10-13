# 24. Community reverse-engineering projects and how to reuse them

Researched 2026-10-13. Goal: find other people doing reverse engineering, engine research or deep performance and bug fixing of The Sims 3, so we can reuse their names, addresses, parsers and findings instead of rediscovering them, and know who to invite to the crowdsourced repo.

Builds on docs/01 (fix comparison), docs/03 (S3SS anchors, LazyDuchess repos, symbol situation), docs/06 (Sims3MonoModder, MonoPatcher, Sims3-MCP, ts3-template, Interaction-Injector) and docs/07 (installable fixes). Those are not repeated; this file only adds what is new or changes their value.

Tags: **[verified]** I opened the repo, README, file tree, commits or page (GitHub API, 2026-10-13). **[snippet]** from a search-result summary only. **[inferred]** our reasoning.

License note: "none" means the repo has no license file. Then the default is all rights reserved: we may read it and cite it, and re-derive facts (addresses, struct offsets, behaviour) ourselves, but must not copy code. GPL-3.0 code can be copied only into GPL-3.0 code of ours. MIT/zlib/Unlicense can be copied with the notice.

---

## Top 10 by value to us

| # | Project | Why it ranks here |
|---|---|---|
| 1 | **Apex Radiance** (loinyx) | MIT, Steam 1.67.2 addresses, a published TS3W engine reference (main loop, services, threads, Mono GC, lot streaming, timers) and 15 shipped hitch fixes, several of which are the same ideas as our docs/08 offload and lookup-index plans |
| 2 | **Sims3MonoModder** (sims3fiend) | Already used (docs/06, 08). New value: per-method Mono profiler and assembly diff for script-side stutter (docs/04a) |
| 3 | **Sims 3 Settings Setter issue tracker** (sims3fiend) | 80 issues of field reports on exactly our patches: GC patch stutter (#43), memory growth (#60, #63), save lag (#65), access violations (#57) |
| 4 | **TS3-64bit-Patch** (cmedina-dev) | The only other public attempt at the 4 GB limit; GPL-3.0; has a hitch detector and allocation-pattern analytics |
| 5 | **S3LotLoDThing** (sims3fiend) | Two Steam addresses for the "Throttle Lot LoD Transitions" debug variable and the JZ it bypasses; author says it "reduces stutter dramatically" |
| 6 | **sims3-package-interface / s3pi-modern** (marcos4503, Destrospean; original Peter L Jones) | Reference DBPF/resource parsers; we already ported the S3SA decrypt from s3pi-modern |
| 7 | **Sims3HookMods** (ChrisNonyminus) | MIT, Steam-only ASI that hooks Mono setup and adds native internal calls: the pattern for a native-to-script bridge we can copy |
| 8 | **NRaas source** (Chain-Reaction) | Already used for routing logic (docs/03 §5.3); source of truth for Overwatch/ErrorTrap stuck-Sim resets |
| 9 | **sims-3-bevy** (Eamo5) | Clean-room Rust readers for worlds, terrain, meshes and textures; useful to check our own format assumptions |
| 10 | **SimsWiki / MTS wiki TS3 modding reference** | Resource type IDs and DBPF/RCOL docs that `tools/dbpf.ts` should cite |

---

## 1. Apex Radiance (loinyx): highest value

- **URL:** https://github.com/loinyx/Sims3-ApexRadiance [verified]
- **Author:** loinyx; lot-streaming research credited to **idavidveiga** [verified README/docs]
- **Activity:** created 2026-09-28, last push 2026-10-12 (release 2.12.2), 18 stars, issues on [verified]
- **License:** MIT [verified repo metadata]
- **What it is:** an ASI for lighting and visuals that also ships a "Performance" group of 15 hitch fixes and a Frame Profiler. Its docs follow a strict standard: `docs/engine/` is a reverse-engineering reference for `TS3W.exe`, and "Game addresses are for Steam 1.67.2 (image base 0x00400000) unless stated otherwise", which is **our exact build**. [verified]
- **Engine reference pages** [verified, file tree and page heads]: `main-loop-and-services.md`, `lot-loading-and-streaming.md`, `mono-gc.md`, `timers-and-sleeps.md`, `terrain-and-light-bake.md`, `room-light-maps.md`, `light-objects-and-rigs.md`, `shaders.md`, `camera-and-map-view.md`, `game-versions.md` (Steam fixed addresses plus a signature table for EA app 1.69.47).
  Examples from `main-loop-and-services.md`: main loop `0x00ECA960`, called from `0x00ECBD00` ("Services/Sims3App"); simulation thread entry `0x00D81840` ("Simulator Frame") with an explicit GC call at `0x00D819AA`; ServiceManager object at `[0x011CE3F4]`, `ServiceManager::Update 0x00588E00`, `AddService 0x0059F500`, a node layout with per-service main-loop and sim-loop flags, and "58 functions that register services"; inactive-window sleep at `0x00EC9FBA` (the bytes S3SS's limiter replaces); RefPack at `0x004EC010` (matches our docs/08). [verified, page text]
- **Performance switches that overlap our plans** [verified, feature pages]:
  - *Faster Game File Lookups* (`ResourceLookupCache`): remembers which package answered each resource and re-checks it, instead of probing ~290 packages. Same idea as our docs/08 H3 native lookup index.
  - *Faster File Lists* (`FileListCache`): caches per-type key lists of read-only packages (CAS, Sim load). Overlaps docs/21 CAS offload.
  - *Faster Object Lookups* (`ObjectLookupIndex`): index by object ID next to the object service's map, and cached lot lookups instead of walking the world tree.
  - *Faster Memory Handling* (`FastMemory`): spin before sleep on the shared allocator lock, and return big blocks to Windows in the background. Overlaps our alloc cache result (PLAN: +19% it/s).
  - *Faster Scripts* (`ScriptMath`, experimental): removes a library call on every Mono decimal comparison and a locked search on type-info lookups. Directly relevant to the script-thread cost in docs/04a.
  - *Room to Save* (`MemoryGuard`, experimental): reserves address space and frees it right before a save; trims the file cache when memory is tight. Error-12-on-save.
  - Also: room-light queue, lot lighting while moving, wall shading while moving, scene-node budget, fast DXT and RefPack encoders, fast CAS sort, remember missing files, lot streaming switches (off by default).
  - Their rules are worth copying as policy: bit-identical results, the first 16/64 calls also run the game's function and compare, and every hook site is byte-checked against the studied Steam 1.67.2 bytes before writing. [verified]
- **How it speeds us up:**
  1. Import their address names into our Ghidra project (`tools/ghidra`) as a "loinyx/Apex" name source: main loop, services, sim thread, GC sites. Cross-check against docs/04 before trusting. [inferred]
  2. Before building docs/08 H3 or more of docs/20 and docs/21, read `features/resource_cache.cpp` and `features/object_index.cpp`. If they already do it, measure Apex in the baseline table instead of writing our own.
  3. Use `mono-gc.md` against docs/13 (GC tuning). Their open questions (what increments `[0x011F2F98]`, whether `[0x011EE528]` can be 0) are ones our probe can answer.
  4. Their Frame Profiler is a second opinion on our `profile-world` numbers.
- **Reuse:** MIT, so code can be copied with the notice. Note that `sigcheck.pl` and the Ghidra research tools are private (`game-versions.md`), and a PR (#17) mentions a private Ghidra analysis of Steam TS3W. [verified page; PR from snippet]
- **Overlap:** high. It also detours the resource manager and allocator, so it may conflict with our ts3native hooks and S3SS: check overlapping bytes before running both (as docs/08 already says for S3MM).

## 2. Sims3MonoModder (sims3fiend)

- **URL:** https://github.com/sims3fiend/Sims3MonoModder [verified]. GPL-3.0, last push 2026-09-29, one contributor (sims3fiend). [verified]
- **Already in docs/06 and docs/08.** New angles only:
  - The **Profiler** (per-method calls, self and total time, allocation, boxing, icall tables; CSV and Chrome-trace export) answers docs/04a's "which script methods cost the most per tick" without us writing a managed profiler. [verified README]
  - **Assembly Diff** shows exactly which methods a core mod (simler90, NRaas) changed versus vanilla. Useful to check whether simler90 already fixes a routing retry loop before we patch it.
  - A **vectored handler can recover from a known recurring crash**: ask which crash, since it may be one in our ledger.
- **Reuse:** GPL-3.0; reference or GPL-only reuse. Use as a tool at runtime, do not ship it.

## 3. Sims 3 Settings Setter issue tracker (sims3fiend)

- **URL:** https://github.com/sims3fiend/Sims3SettingsSetter/issues [verified, 80 issues listed]. No license file ("none"). Contributors: sims3fiend (91 commits), **just-harry** (25), **swiffyjk** (10), **LazyDuchess** (7). [verified]
- **Issues worth reading before our next patches** [verified titles]: #43 "Experimental GC_try_to_collect() patch causes stuttering" (docs/13), #60 and #63 memory growth since 1.4.0, #65 "Game takes too long to save, super fast mode is very laggy", #57 random `ACCESS_VIOLATION`, #44 crash with CPU Thread Optimization, #46 SpecialK frame pacing, #33 WorldCache size cap, #72 headless patch-only mode for translated D3D9 (DXVK).
- **How it helps:** field reports from many machines about the same patches we run. Triage our crash ledger against them. If #72 shipped, our DXVK setup can use the patch-only mode.
- **Reuse:** reference only (no license); docs/03 already uses its anchors.

## 4. TS3-64bit-Patch / TS3 Memory Server (cmedina-dev)

- **URL:** https://github.com/cmedina-dev/TS3-64bit-Patch [verified]. GPL-3.0; created 2025-10-09, last push 2026-03-30; 19 stars; 4 issues (#1 hook on launch causes arena fault, #3 StackWalker crashes during hitches). [verified]
- **What it does:** a 64-bit server process plus an injected 32-bit DLL. Hooked `HeapAlloc` calls of 4 MB or more are sent over a named pipe to the server, which allocates in its own 64-bit address space and returns a handle; the game gets a 4-byte "proxy" pointer. Also ships `AdvancedHitchDetector`, `AllocationPatternDetector`, an `AsyncCacheWriter` and cache-integrity code. [verified README and file tree]
- **Our read:** a proxy pointer to 4 bytes cannot be read or written by the game as if it were 10 MB, so as described this can only work for allocations the game never touches directly, or through extra mapping not shown in the README. Treat the approach as unproven until we read `src/ts3_patch`. [inferred] The hitch detector and allocation-pattern code are useful ideas for our probe regardless.
- **Overlap:** docs/08 and docs/20 (resource offload out of process) are the same goal with a different, safer mechanism (shared memory views). Compare notes; do not adopt.
- **Reuse:** GPL-3.0; reference, or GPL-only reuse.

## 5. S3LotLoDThing (sims3fiend)

- **URL:** https://github.com/sims3fiend/S3LotLoDThing [verified]. Steam only, no license, last push 2025-02-28.
- **Content:** `0x011ECBC0` = the "Throttle Lot LoD Transitions" bool in the Streaming debug-variable category; `0x00C63015` = a `JZ` after `CALL 0x00AB1860` that the patch turns into a `JMP`. The author guesses frustum culling and says "it reduces stutter dramatically". The full Streaming category is editable in S3SS. [verified README]
- **How it helps:** two named Steam addresses for lot streaming (stutter priority), and a lead to check against Apex's lot-streaming page and our docs/04. Label them in Ghidra.
- **Reuse:** facts only (no license).

## 6. s3pi family: sims3-package-interface, s3pi-modern, Sims3Tools

- https://github.com/marcos4503/sims3-package-interface: GPL-3.0, archived copy of s3pi plus docs and a SimsWiki mirror; last push 2024-09-10. LazyDuchess has a fork. [verified]
- https://github.com/Destrospean/s3pi-modern: s3pi on modern .NET, GPL-3.0, 2026-07. Already the source of our S3SA decrypt port (`tools/extract-assemblies.ts`). [verified]
- https://sourceforge.net/projects/sims3tools/ (Peter L Jones, "pljones"): original s3pi, s3pe, s3oc; development finished. [snippet]
- **How it helps:** the reference for every resource wrapper (RCOL, GEOM, CASP, OBJD, world data). When `tools/dbpf.ts` needs a new resource type, port the parser from s3pi rather than reverse engineer it. Our tools are TypeScript and not GPL: port the format knowledge (layout, field order) and write our own code, and cite the source file, as `extract-assemblies.ts` already does. [inferred]

## 7. Sims3HookMods (ChrisNonyminus)

- **URL:** https://github.com/ChrisNonyminus/Sims3HookMods [verified]. MIT; a single push on 2024-03-02 (inactive).
- **What it does:** Steam-only proof-of-concept ASI that hooks the game's Mono runtime setup and registers custom internal calls (`MonoAddInternalCall("Namespace.Class::Func", fn)`), called from C# via `[DllImport("Sims3Common")]`; example `IO.File::CReadAllText`. It also loads further plugin DLLs that add internal calls. Files: `GameFuncs.cpp`, `Hooking/Hooks.cpp`. [verified README and tree]
- **How it helps:** if our probe or ts3native needs a direct script-to-native call (for example to expose a native routing cache to a script mod), this is the smallest working MIT example on our exact build. Compare its Mono setup hook address with our `tools/internal-calls-map.ts`.
- **Reuse:** MIT, copy with notice.

## 8. NRaas (Chain-Reaction)

- **URL:** https://github.com/Chain-Reaction/NRaas [verified]. No license file; last push 2026-07-29; 60 stars.
- Already used in docs/03 §5.3 and docs/07. Nothing new, except that it is still maintained, so a question to the NRaas community (nraas.net forums) about Overwatch's stuck-Sim reset thresholds can still get an answer.

## 9. sims-3-bevy (Eamo5)

- **URL:** https://github.com/Eamo5/sims-3-bevy [verified]. Created 2026-10-08, very active (push 2026-10-12), no license.
- **What it is:** a standalone Rust/Bevy reimplementation that reads an installed copy of the game. A baker converts all worlds (base, EPs, WA destinations) to a GPU-ready cache: terrain paint and shadow maps, roads, water, placed objects, SpeedTree billboards. [verified README]
- **How it helps:** its decoders are a second, independent reading of world and terrain formats. Use it to check our parsers and as a reference for which resources a world load touches (docs/19 travel worlds). Not a source of engine behaviour (it re-implements gameplay).
- **Reuse:** reference only (no license).

## 10. SimsWiki and MTS wiki technical pages

- SimsWiki "TS3 Advanced Modding Reference": https://simswiki.info/wiki.php?title=Tutorials:TS3_Advanced_Coding_Tutorials [verified, page fetched]
- SimsWiki "Sims 3:PackedFileTypes" (resource type IDs): https://simswiki.info/wiki.php?title=Sims_3:PackedFileTypes [verified, page fetched]
- MTS wiki "Sims 3:Modding": https://modthesims.info/wiki.php?title=Sims_3:Modding [verified, page fetched]
- MTS wiki DBPF page: https://modthesims.info/wiki.php?title=DBPF [snippet]
- Rust `refpack` crate lists TS3 under the "SimEA" RefPack variant: https://docs.rs/refpack [snippet]; license not checked.
- **How it helps:** cite type IDs in `tools/dbpf.ts`; the RefPack variant note is a quick test for our RefPack reader.
- **License:** wiki content; cite, do not copy pages.

---

## Other projects found (lower value)

| Project | URL | What | Activity / license | Use |
|---|---|---|---|---|
| Gibbed.Sims3 (Rick "gibbed") | https://github.com/gibbed/Gibbed.Sims3 | 2009 tools: DBPF, scenegraph, rig, STBL readers; `ReadExecutable/ParseScriptFunctionTables.cs` walks the exe's native script function tables (`{native, name}` pairs, keyed by exe MD5) | Single push 2017-07; zlib-like license with a **non-commercial** clause [verified] | Confirms our internal-call map method; table addresses are for 2009 exes, not 1.67 |
| sims3-rs (kitlith) | https://github.com/kitlith/sims3-rs | Rust DBPF tools: `find_merged_cc`, `geom_tri_count`, `dump_package_pngs` | MIT, push 2025-10 [verified] | `geom_tri_count` is a ready check for heavy CC meshes (docs/17) |
| Tatsh/sims3-tools | https://github.com/Tatsh/sims3-tools | Python 3 utilities | MIT, 2013 [verified metadata] | Reference only |
| Regul Save Cleaner (Onebeld) | https://github.com/Onebeld/RegulSaveCleaner | Save cleaner on s3pi | MIT, push 2025-11 [verified metadata] | Already in docs/09; MIT means we can borrow its cleaning rules |
| Tiny UI Fix (just-harry) | https://github.com/just-harry/tiny-ui-fix-for-ts3 | UI scaling generator (PowerShell) | No license, push 2026-03 [verified] | Author is also the 2nd-largest S3SS contributor |
| LazyDuchess repos | https://github.com/LazyDuchess | New since docs/03: `LotPopulation` (populates community lots), `TS3PatchLauncher`, `TS3BurglarHate`; fork of S3SS | No licenses [verified] | Nothing stutter-related beyond docs/03 |
| SimsOnline (JurjenBouma) | https://github.com/JurjenBouma/SimsOnline | "Proof of concept sims 3 online multiplayer mod" | README only, 2021-01, no license [verified] | No content; no other TS3 multiplayer RE found [snippet searches] |
| Sims3ModLoader (JurjenBouma) | https://github.com/JurjenBouma/Sims3ModLoader | Launcher and package (un)packer | 2021-01 [verified metadata] | None |
| TS3-Revamp (VMPYRC) | https://github.com/VMPYRC/TS3-Revamp | Tuning/XML edits to `GameplayData.package` | 2026-03, no license [verified] | Shows which tuning files a "fixes" package touches |
| Kuree/Sims4Tools | https://github.com/Kuree/Sims4Tools | s4pi (s3pi fork for TS4) and DBPF wiki | Deprecated 2016 [verified metadata] | DBPF 2.x docs |
| OpenTS3-DS, TheSims3Mobile | github luispolis124/OpenTS3-DS, WanKerr/TheSims3Mobile | DS and Windows Phone decompilations | [snippet / verified metadata] | Different games; not useful |
| TheSims3WorldToolBuilder "leaked source" | github Triomonitor/TheSims3WorldToolBuilder | Claims leaked EA source | [verified title only] | **Do not use or open**: leaked EA code is a legal risk to the project |
| Sims 3 Performance Patch (Sonja92yu) | https://www.nexusmods.com/thesims3/mods/235 | Already in docs/01, 07, 20 | Nexus returned HTTP 403 to scripts today | No GitHub account found under that name [verified search] |

Not found: a public Ghidra/IDA database or symbol map for TS3W.exe (same as docs/03 §8), a Simlogical/Pescado routing write-up, and any public Discord with a technical archive (searches returned only generic SEO pages) [snippet searches].

---

## Outreach: who to invite to the crowdsourced repo, and what to ask

Use only each person's public channel (GitHub issue or discussion on their own repo, their MTS profile, or the channel their README names). One short, specific message each; offer something back (our verified addresses, measurements, crash triage). No mass messages, and nothing that asks them to share private research they chose to keep private.

| Who | Channel | Ask | Offer |
|---|---|---|---|
| **loinyx** (Apex Radiance) and **idavidveiga** | Issue on Sims3-ApexRadiance ("Reporting a problem" section names it) | Would they cross-link engine notes? Specifically: do ResourceLookupCache and FastMemory conflict with other resource-manager and allocator hooks, and which byte ranges they patch, so tools can coexist | Our verified routing and route-planner addresses (docs/11, 18), our measurement harness results for their switches |
| **sims3fiend** (S3SS, S3MM) | GitHub issue on S3SS or S3MM | Whether S3SS could expose a list of patched byte ranges for conflict checks; status of #72 patch-only mode; which recurring crash S3MM's vectored handler recovers from | Our crash ledger matches against their open issues (#57, #65) |
| **LazyDuchess** | Her GitHub repos (issues) | Whether MonoPatcher's Mono signatures have Steam 1.67 equivalents she already knows | Our Steam addresses for the same Mono functions |
| **cmedina-dev** | Issue on TS3-64bit-Patch | How the proxy pointer is made usable by the game (mapping, or only for untouched blocks?), and hitch-detector findings | Our docs/08 offload design and shared-memory approach |
| **just-harry**, **swiffyjk** (S3SS contributors) | GitHub | Interest in reviewing native patches | Credit in the repo |
| **NRaas community** | nraas.net forums | Overwatch stuck-Sim reset thresholds, and known bad routing spots per world | Our stuck-Sim repro list (repros.json) |
| **ChrisNonyminus** | GitHub | Permission is already given (MIT); just credit and link | Credit |

Before any outreach: Rodney decides who and sends the messages himself. Our repo is local-only (CLAUDE.md), so "invite to the crowdsourced repo" needs a publishable subset first (notes and our own patch source, never EA files).

---

## Sources

All GitHub facts come from the GitHub REST API and raw READMEs/docs read on 2026-10-13: repos listed above, `loinyx/Sims3-ApexRadiance` `docs/README.md`, `docs/engine/main-loop-and-services.md`, `docs/engine/mono-gc.md`, `docs/engine/game-versions.md`, `docs/features/performance/*.md`; `sims3fiend/Sims3SettingsSetter` issues and contributors; `cmedina-dev/TS3-64bit-Patch` README, tree, commits, issues; `sims3fiend/S3LotLoDThing` README; `ChrisNonyminus/Sims3HookMods` README and tree; `gibbed/Gibbed.Sims3` tree, license and `ParseScriptFunctionTables.cs`; `Eamo5/sims-3-bevy` README. Web pages fetched: SimsWiki and MTS wiki pages listed in section 10. Search summaries (marked [snippet]): GitHub topic and PR pages, sourceforge sims3tools, docs.rs refpack, Nexus 235 review.
