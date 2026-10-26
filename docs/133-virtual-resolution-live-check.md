# 133. Virtual-call resolution: live check (managed callvirt caches + native glue sites)

Written 2026-10-20. **Offline build only:** no game run, no queue started. Built and self-checked in an agent worktree; the probe job is ready for Rodney to run. Question (Rodney, after the v0.1.4 release: tests now focus on Mono confidence and virtual-call resolution): do the static resolutions that the Mono offload plan relies on hold at run time?

- **Managed:** the typed override closures of docs/127 (allow-list v0 + tier 2, `docs/data/autonomy-virtual-closure.csv`): is every target a `callvirt` really reaches inside those closures in the static target set?
- **Native:** the docs/131 glue sites that the v2 resolver left unresolved or partial, or resolved only by caller-side typing (`docs/data/mono-glue-closure.csv`, per site now in `docs/data/mono-glue-sites.csv`): which targets do they reach (closes I-131-12)?

One job does both: `tools/harness/queues/virtual-resolution.json` `bp-vres` (Baseline-BP-Spring, 10 min seeded profile, seed 12345, release ini defaults plus `[ProbeSnap] Enabled=1`). Report: `tools/virtual-resolution-report.ts`. Tags as in AGENTS.md; ledger rows in docs/51 §186.

## Summary

- **Managed mechanism: a read-only snapshot, no hook.** Mint's `callvirt` keeps a monomorphic inline cache per call site: a (`MonoClass*`, `RuntimeMethod*`) pair in the calling method's data items, overwritten on every miss [verified, §1]. The probe agent reads every reachable pair every 60 s and once at the end (`/vcache`, profile option `vres`) and keeps the union per site. The cache only holds the last target, so a polymorphic site shows the targets that happened to be last at snapshot times: 10 snapshots give a lower bound on the live target set, never a false target (§1.4).
- **Native mechanism: the existing `[ProbeSnap]` int3 sampler** (docs/94 §7, docs/106) with a new site table: 514 sites (`native/ts3native/vcallsites4.h`, built with `/DTS3N_VCSITES4`); the default build keeps `vcallsites.h`.
- **Overhead estimate** [inferred, to be read from the run]: managed 0 on the sim thread (nothing patched; the agent reads from its own thread), each snapshot a few seconds of one core on the agent thread, now in tick-aligned slices of <= 20 ms per call with a 45 s wall budget (§5: the first version held the agent thread in one call and the harness killed the game), and ~20-40 MB of JS heap inside the 32-bit process; native at most 16 int3 hits per site per 30 s window = at most ~275 exceptions/s for 514 sites, ~1-3 ms/s spread over all threads (<0.3%), as the 1,182-site routing table of docs/106 ran.
- **Static side, unchanged results:** `autonomy-virtual-closure.ts --edges` writes the typed run's callvirt edges (38,789 edges, 28,033 closure methods; 2,952 edges with more than one typed target) without changing the CSV it prints; `mono-glue-closure.py --sites` reproduces `mono-glue-closure.csv` and `mono-glue-races.csv` byte for byte and adds the per-site CSV (1,546 sites) [verified: compared].

## 1. Managed: the callvirt inline caches

### 1.1 Layout [verified, disassembly of the Steam 1.67 exe]

| Item | Fact | Where |
|---|---|---|
| `callvirt` words | `0xEC`: opcode, cache item index, one unused word, declared-method item index (4 words, operand-length table `0x1195088`); `0xED` `con.callvirt` and `0xEE` `con.boxcallvirt` add a constrained-class item before the declared method (5 words) | handler `0xE549EB`-`0xE54A2A` |
| Cache pair | `items[cache]` = receiver `MonoClass*`, `items[cache+1]` = target `RuntimeMethod*`; hit when `obj->vtable->klass` equals it, miss calls `0xE51FC0` and stores both words | `0xE54AF4`-`0xE54B22` |
| Other call items | `call` `0xEA` [op, method], `con.call` `0xEB` [op, class, method]: `RuntimeMethod*` items | `0xE5493E`-`0xE5495B` |
| `switch` `0xE8` | [op, n (u32), n x u32 offsets]: 3 + 2n words | `0xE558B1` |
| Superops `0x1AB`-`0x1C0` | the peephole overwrites only the first opcode word; the other base instructions' words stay in place (docs/130 §9), so a decoder steps over a superop by the length of its first base opcode (entry `0x108FCD8 + 72*(op-0x1AA)`: hash, count, opcodes; all 22 first opcodes have a non-zero length) | docs/130 §9, table read |
| `RuntimeMethod` | `+0` mint code, `+8` `MonoMethod`, `+0x14` data items; bit `0x100` of `+0x44` is set by the transform caller after `generate` returns (`or [ebx+0x44], 0x100`), so a method with the bit set has its code and items complete | `0xE63C8A` code, `0xE63D4E` items, `0xE64AE3` flag |
| Code end | not stored. The data items are allocated from the same domain mempool right after the code (`0xE63C7F` code, `0xE63D3D` items, nothing in between), so `items - code` bounds the code when both sit in one chunk | `0xE63C7F`-`0xE63D3D` |
| Image class cache | `image+0x298` is an eglib `GHashTable` (`g_hash_table_new(NULL, NULL)` at `0xE834B9`; `+8` buckets, `+0xC` size; slot = key, value, next) of typedef token -> `MonoClass*` (insert `0xE6DE08`) | `0xE459F0`, `0xE6DE00` |
| Seeds | corlib image `[0x1220E40]` (`mono_defaults.corlib`, store `0xE75D4C`); `Sims3.SimIFace.Simulator` class `[0x1220F34]` (`0xD7C63D`), its image at class `+0` | as listed |

### 1.2 Snapshot (tools/probe/agent.js `vcache`)

1. Images: the two seeds, then every image a cached receiver class or a target's class belongs to (so the game assemblies are found through the caches themselves).
2. For every class in each image's class cache: its methods (`+0x70` count, `+0x94` array), each `MonoMethod+0x14` `RuntimeMethod`.
3. Breadth-first over `RuntimeMethod`s: decode the mint code once (only with the transformed bit set), keep the callvirt sites whose declared item is a real `RuntimeMethod` (its `MonoMethod+0x14` points back) and follow every call item and every cached target. Methods of generic instances (`List<Sim>`, `InteractionDefinition<Sim, Lot, VisitLot>`) are not in any class cache and are reached only this way.
4. Per site (caller key + mint word offset): the declared method, and per snapshot the cached target and receiver class.
5. All of it in slices (§5): one `/vcache?snap=1` call = one slice; the harness repeats the call until `done`.

Names are `Namespace.Outer+Inner::Method` (no parameter list); the report maps the docs/78 index keys to the same form (drops the parameter list and a generic method's `` `N ``). A decoded method whose items are not right behind its code (another mempool chunk) is read up to 64 KB and decoded to its first invalid opcode; trailing garbage cannot produce a site, because its declared item must pass the back-pointer test. `stats.unbounded` and `stats.badDecode` in the dump count these cases.

### 1.3 Comparison (tools/virtual-resolution-report.ts, managed part)

Static input: `node tools/autonomy-virtual-closure.ts --asm <repo>/game-copy/assemblies --edges <edges.json>` (typed run; per `cv`/`virt`/`iface` edge the targets the closure enters, the CHA overrides and the caller's smallest visit depth). Live sites count when their caller is a closure method. Classes per live target:

| Class | Meaning | Action |
|---|---|---|
| match | in the typed target set | - |
| typed-miss | not typed but a CHA override: receiver typing dropped a real target | soundness gap: fix `managed-callgraph.ts` (receiver rule or hand-reviewed site in `autonomy-virtual-closure.ts`) and rerun the docs/126 verdicts |
| cha-miss | not even in CHA: mod override or a target the index does not expand | soundness gap: add to the hierarchy (docs/127 §5 for mods) |
| bcl-decl | declared on a BCL type (docs/127 §1 does not expand those) | list; a game target here is a gap of the same kind |
| beyond-hops | caller at depth >= 5, where the closure keeps the declared body only | by design (docs/127 §1); counts how much the hop bound hides |
| no-edge | no static edge for (caller, declared) | naming check first, then a gap |

Over-approximation: for each static edge within the hop bound with more than one typed target that was live, the typed targets never seen. A low seen share is expected for a monomorphic cache over 10 snapshots; it is a pointer to receiver rules worth tightening, not a fault.

### 1.4 Limits

- Last-target cache: a site that sees A then B between two snapshots shows only B. Unseen targets are not evidence of over-approximation on their own (§1.3).
- A snapshot races the sim thread: a pair can be read half-updated (new class, old method). The target is still one that this site really resolved to, so it cannot be a false target; only its class column can be wrong for that one sample.
- Overloads with one name share a key; a typed target of another overload counts as a match.
- Only methods transformed by the end of the run and reachable from a class cache or a call item are seen; a closure method that never ran has no sites.

## 2. Native: the docs/131 glue sites

- `python tools/mono-glue-closure.py --sites docs/data/mono-glue-sites.csv` (new option): one row per indirect site, columns as `docs/data/routing-indirect-sites.csv` (`site, caller, kind, slot offset, targets, status, note`, note = `[basis] | hop; groups`), so `tools/indirect-live-vs-static.ts --csv docs/data/mono-glue-sites.csv --sites native/ts3native/vcallsites4.h` works on it too. By basis: 801 imports, 265 slot signature (>6), 110 switch, 93 register, 62 receiver typed (caller), 51 function-pointer field, 39 receiver typed (obj), 28 + 24 global function pointer, 21 partial (caller), 39 docs/102 prior, 13 unknown receiver.
- `python tools/snap-vcall-sites.py --only-sites docs/data/mono-glue-sites.csv --out native/ts3native/vcallsites4.h` (new option): the 533 non-import rows that are unresolved, partial or `receiver typed (caller)`, scanned in their 238 functions with the sampler's rules: **514 sites armed** (511 call, 3 jmp); 5 in a function's first 16 bytes and 1 next to a docs/68 or Frida hook address skipped; 13 `call [disp32]` global-pointer forms are not armable (the sampler decodes register and register-relative forms only). All 62 caller-typed sites are armed.
- Build: `set CL=/DTS3N_VCSITES4`, `native\ts3native\build.cmd out\vres`, clear `CL` (selftest ok; ts3native 0.36.3 from this branch, sha256 `0069fe3c...e11ae`). The `#elif` in `vcall.cpp` is the only code change; without the define the build is unchanged.
- Report classes as docs/106 §2 (`match`, `family`, `new`, `resolves`, `hint`, `no-row`), plus the I-131-12 verdict: **pass** when caller-typed sites fired and none has a `new` pair, **fail** (demote those sites to partial in `mono-glue-closure.py`) otherwise, **not answered** when none fired.

## 3. How to run

1. Park the asi: copy `native/ts3native/out/vres/ts3native.asi` (built in the agent worktree, or rebuild as §2) to `game-copy/ts3native.asi.vres` in the main checkout. `node tools/harness/queue.ts tools/harness/queues/virtual-resolution.json --dry-run` must no longer say MISSING.
2. Run the queue (one job, ~15 min with load; daytime is fine, the job measures no timing).
3. Static edges once per checkout: `node tools/autonomy-virtual-closure.ts --asm game-copy/assemblies --edges probe-out/vres-edges.json > probe-out/vres-closure.csv`.
4. `node tools/virtual-resolution-report.ts --vres probe-out/profile/<tag>-vres.json --edges probe-out/vres-edges.json --log <row.nativeLog>` (prints both comparisons; `--list N` for longer lists).

**Pass rule of the run** (job note): no crash, no watchdog line; `/native` after the run `vcReady` 1, `vcSites` 514, `vcFaults` 0, `vcSkipped` 0; 9 `[vres]` snapshot lines, none `budget hit`, the last one wrote `<tag>-vres.json`; the report shows live sites in closure methods and fired glue sites. The verdicts are separate: managed soundness gaps (`typed-miss` + `cha-miss`, expected 0) and I-131-12.

**Order after the 2026-10-20 failure (§5.3):** `--only bp-vres-native-only`, then `--only bp-vres-snapshot-dry`, then the full snapshot, then `bp-vres`.

Self-checks (no game): `node tools/probe/vcache-check.mjs` (the agent's walker on synthetic memory, §5.2), `node tools/virtual-resolution-report.ts --self-check`, `node tools/indirect-live-vs-static.ts --self-check`, `node tools/harness/queue.ts --self-check`, `python tools/snap-vcall-sites.py --self-check`, `python tools/mono-glue-closure.py --self-check`; the agent's mint decoder checks itself on a synthetic word stream before its first snapshot (`vcSelfCheck`).

## 4. Effort

Built here: ~3 h agent time. Run: 1 game run (~15 min), no clean night needed. Reading the report and fixing what it finds: ~30-60 min per gap class.

## 5. bp-vres 2026-10-20: what ended the run, and the safe snapshot

Written 2026-10-21, offline (no game run). Ledger: docs/51 §189.

### 5.1 Cause

The run was ended by our harness, not by a game crash.

| Fact | Tag | Source |
|---|---|---|
| Profile window started 19:18:26 UTC; the first `/vcache` snapshot is due 60 s later (19:19:26) | verified | `<tag>.log`, `profile-world.ts` loop `s = 60` |
| `FAIL: game died at 19:19:25 (agent not answering for 61 s (TimeoutError ...))`: the time is the last agent heartbeat, so the agent went silent within a second of the snapshot start | verified | `<tag>.log`, `game-alive.ts` prints `lastBeatAt` when TS3W.exe still runs |
| `game-alive.ts` reads the heartbeat through `/agentstatus`, an RPC into the same Frida script; the old `vcache` export was one synchronous call, so every RPC waited behind it | verified | `ts3probe.py` (no lock, but one JS thread per script), old `vcSnapshot` |
| The old budget (30 s) was checked only between methods of the breadth-first walk; the class-cache walk of every image, and each method's decode (up to 64 KB, or 256 KB when bounded), ran unchecked | verified | old `vcSnapshot`, `vcInfo` |
| The game was still running after the "death": the sampler armed at 19:18:25 (process start 19:16:55 + `DelaySec` 90; `/native` at 19:18:25 had `vcWindows` 1) and logs window N 30 s after arming it, so windows 3-5 (~19:19:55, 19:20:25, 19:20:55) are wholly after the agent went silent, with the same hit rate as window 2 (1,724, then 1,807 / 1,660 / 1,625 per 30 s); the machine-load line at 19:20:41 shows TS3W.exe at 17 % CPU | verified | `<tag>-1916-ts3native.log`, `<tag>-1916-machine.jsonl`, `vcall.cpp` `VcMain` |
| No xcpt report and ts3native's exit summary in the log: consistent with `closeGame` (`taskkill /F`) after game-alive exited, not with a fault | verified (no report), inferred (exit path) | crash-ledger triage shows an older report only |
| How long the snapshot would have taken, or whether it would have ended at all | open | the agent never answered; the dry job measures it |

**Most likely cause:** the first snapshot held the agent's script thread for more than 60 s; game-alive read that as a dead game and the queue closed it. The int3 sampler is not implicated: 0 faults, 0 skipped, windows kept closing at the same rate.

### 5.2 Safety changes (tools/probe/agent.js `vcWalker`, profile-world `--vres`)

- **Slices, not one call:** one `/vcache?snap=1` call runs one slice of at most `slice` ms (default 20; at least one unit) and returns. A unit is one hash bucket chain, one image opened, or one method. The harness repeats the call 50 ms apart until `done`, so `/agentstatus` and every other call get answered within ~170 ms. Wall budget per snapshot 45 s (then `partial`); the harness gives up on a snapshot after 60 s and reports `slowest call`.
- **Sim tick alignment:** before its slice, a call waits up to `wait` ms (default 100) for the ts3agent mailbox heartbeat to change (the mod bumps it once per sim tick). Slices start at a fixed point of the tick; the sim thread is never stopped, so reads still race it, which §1.4 already allows. `tickAligned` counts the slices that saw the tick. Without a mailbox the slices run unaligned [inferred: the mailbox is found during the load in every queue run].
- **Every read checked:** the walker reads through a `mem` interface; in game each read first checks the page with `VirtualQuery` (committed, a readable protection, no `PAGE_NOACCESS` or `PAGE_GUARD`), cached per page for one slice, then does Frida's guarded read. A guarded read alone catches the fault but still clears a `PAGE_GUARD` bit, which would leave some thread's stack without its guard page [inferred from Windows guard-page semantics; never observed here]. Code reads are clamped to the end of their region.
- **Caps:** code read <= 64 KB (larger bounded gaps are skipped, `stats.tooBig`); hash chain <= 1,000 slots; class method count <= 20,000; buckets <= 2^20; methods per snapshot <= 60,000; visited set <= 200,000 (then `partial`).
- **Transformed only:** a method whose `+0x44` bit `0x100` is clear is skipped and retried next snapshot (`stats.notTransformed`); it stays a valid target or declared method.
- **No exception escapes:** every read returns null on failure; a bad image, class or method is counted (`imageErrors`, `unreadable`) and skipped.
- **Dry mode:** `dry=N` (profile `--vres-dry N`, queue `profile.vresDry`): walks the seed images' class caches and decodes at most N methods, without following call items or cached targets. It still records the sites it reads.
- **Offline test:** `node tools/probe/vcache-check.mjs` runs the walker from agent.js on synthetic memory: class cache with a self-looping chain and an unmapped class, a transformed method with a `callvirt` and a `call` to a method no class cache lists, a `con.callvirt` whose cache holds an unmapped class, a method without the transformed bit, a method over the read cap, an unmapped method pointer; it checks the site union, slicing (several calls, tick waits), the slice time cap, dry mode, the wall budget and a second snapshot.

### 5.3 Bisect plan

| Step | Job (`--only`) | Tests | Pass |
|---|---|---|---|
| 1 | `bp-vres-native-only` | the int3 sampler alone: same asi, ini, save and seed, no snapshot, samplers off (`noSamplers`), no profiler session | no crash or FAIL line; `vcall window` lines to the end; `vcFaults` 0, `vcSkipped` 0 |
| 2 | `bp-vres-snapshot-dry` | the agent walk alone: `[ProbeSnap]` off, `vresDry` 500 every 60 s, `noProf`, samplers off | no crash or FAIL line; 10 snapshot lines `done`, `slowest call` < 1 s, `tick-aligned` > 0, no budget hit |
| 3 | `bp-vres-snapshot-dry` with `vres: true` instead of `vresDry` (edit the job or a copy) | the full walk, still without int3 | as step 2; read the first snapshot's `ms` and `workMs` (the number the failed run never reported) |
| 4 | `bp-vres` | both together, as designed | §3 pass rule |

A crash in step 1 goes to the sampler: fault address against `vcallsites4.h`, then the function's lock and prologue rules in `snap-vcall-sites.py`. A crash or stall in step 2 or 3 goes to the walker: the last `[vres]` line and the `stats` counters say how far it got.

### 5.4 int3 table review (no change)

The 514 sites ran 5 windows with 0 faults and 0 skipped, 1,600-1,800 hits per 30 s window (~60/s, far below the ~275/s bound of the summary), and kept closing windows after the agent stalled [verified, log]. The handler never blocks on its normal path (`TryAcquireSRWLockExclusive` to disarm a spent site). Residual risks, none observed [inferred]: the fault path takes the poke lock with a blocking acquire, which would wait on a thread that the GC suspended while it held the lock (only on a site whose original instruction faults; `vcFaults` was 0); and sites in Mono functions that run with the loader or GC lock held cost one breakpoint exception each while holding it (a longer hold, not a deadlock, since the handler takes no game lock). Step 1 checks both under load.

## 6. Rerun prepared: glue + routing sites (2026-10-25, offline, not run)

Written 2026-10-25 08:50 PHT. No game run. Native half only; the managed `/vcache` half stays behind the §5.3 bisect in `virtual-resolution.json`.

- **Drift on master 0.36.62** [verified, generator]: with the hook skip list extended to our own native sources (every code address in `native/ts3native/*.cpp|h`, site tables excluded, as `tools/hook-sites.ts` scans) and Apex's `docs/data/apex-hooks.csv`, 4 of the 514 glue sites now sit within 16 bytes of a hook address (`0x829BF7` next to p4clone's `0x829C00`, `0x6FDF34` next to an Apex site, two next to `steerro-test.cpp` addresses: test-only, skipped anyway). `vcallsites4.h` regenerated: **510 sites**. `virtual-resolution.json` now uses `game-copy/ts3native.asi.vres4` (0.36.62 + `/DTS3N_VCSITES4`, builds.json) and says 510; the 0.36.3 `ts3native.asi.vres` stays parked for the 2026-10-20 record.
- **Routing sites added:** `snap-vcall-sites.py --only-sites` takes several CSVs and keeps rows that are not imports and are unresolved, partial, caller-typed or docs/107 rows (the I-107-12 follow/steering rows, docs/107 §13.2). `--only-sites docs/data/mono-glue-sites.csv --only-sites docs/data/routing-indirect-sites.csv --out native/ts3native/vcallsites5.h`: 2,215 rows in 762 functions, **2,115 sites armed** (2,063 call, 52 jmp; 28 skipped in prologues, 79 near hook addresses): **541 glue rows, 1,721 routing rows**, 147 in both. The route planner closure (docs/91-93, `route-plan-callers.csv`) adds no site list of its own: its indirect sites are the docs/102 CSV rows. `call [disp32]` forms stay unarmable, as before.
- **Cost:** the same order as the docs/106 pass-2 table (2,110 sites) that ran clean; sampling caps each site at 16 hits per 30 s window. Correctness run, not timing.
- **Build:** `set CL=/DTS3N_VCSITES5`, `native\ts3native\build.cmd out-vres2`, clear `CL`. Parked as `game-copy/ts3native.asi.vres2` (ts3native 0.36.62, sha256 `97864b40...`, builds.json; the version string is master's, the build tag is the asi name). `build.cmd` now links `selftest.exe` at `/BASE:0x20000000`: at `0x400000` its image covers game addresses, and the `[Verify]` "nothing matches the game's bytes" check failed once the larger table moved its code (plain and probe selftests ok).
- **Job:** `tools/harness/queues/virtual-resolution-2.json` `bp-vres2-routing`: Baseline-BP-Spring, release ini defaults + `[ProbeSnap] Enabled=1`, seed 12345, samplers off, `noiseCheck: false`; six repros in one load (`profile.repros`, ~26 min of windows, ~32 min with load): `rare-route-bits` (Go Here, route option bits, upstairs, Replan), `rare-owned-vehicles` (car), `bp-door-lock` (locked door, portals), `bp-upstairs-route` (stairs, Replan with paths), `replan-buildbuy-crowd` (crowd through doors, build/buy replans), `stuck-check-3am` (stuck checks). No boats (BP has none; `rare-ip-water` / `unseen-boats` on IP would be a second job). Pass: no crash, >= 40 `vcall window` lines with faults 0, `vcSites` 2115.
- **Report:** `node tools/virtual-resolution-report.ts --log <row.nativeLog> --sites-out probe-out/queue/vres2-sites.tsv` (default table `vcallsites5.h`, both CSVs). Per group (glue, routing; imports left out): armed and fired, static % resolved, % resolved with live answers (unresolved sites that fired), % with some target known, partial sites with a target outside the static set, the I-131-12 (caller-typed glue) and I-107-12 (docs/107 rows) verdicts, the `new` pairs; the TSV has one row per armed or fired site: static status and target count, fired / not fired, live targets with counts and class. On the 2026-10-20 `bp-vres` log (150 s of the old table) it already prints glue 150 fired, I-131-12 pass (10 of 62 caller-typed sites fired, 0 new); routing rows fired there only where they overlap the glue table.

## 7. Passive call-site coverage: `[ProbeSnap] CallSites=1` (2026-10-25, built, not run)

Written 2026-10-25 09:45 PHT. No game run. Rodney's decision: call-site resolution logging becomes passive and always on in correctness, verify and soak runs, so every test adds coverage and no dedicated probe run is needed.

- **Native (ts3native 0.36.64, `vcall.cpp`, counters v130 `cp*`):** the normal build carries the §6 table (`vcallsites5.h`, 2,115 sites: 541 glue rows, 1,721 routing rows, 147 in both) under its own name beside the probe table; no `/D` flag. `[ProbeSnap] CallSites=1` (used when `Enabled=0`; `Enabled=1` wins) builds the table at start-up and arms it once `DelaySec` (90 s) later on its own thread, after the same byte check as the probe (a site another patch rewrote is dropped, `vcall skip`). Never re-armed.
- **Disarm rule:** a site restores its own original byte after `CallSitesMinHits` (32) hits when it has fewer than `CallSitesPoly` (4) targets; a site with 4 or more targets keeps sampling to `CallSitesMaxHits` (256) hits or `VC_PAIRS` (now 16, was 8) targets or an overflow. The byte write is one byte (atomic) through the probe's own `VcPokeLocked` under `g_vcPoke`, taken with `TryAcquire` in the handler (busy: the next hit tries again). A hit that raced the disarm (int3 raised just before the byte came back) still finds its site in the table and is emulated; the table, not the byte, decides [verified, selftest `TestCallSites`]. Per-site record: targets (address; module for targets outside the exe in the log line), hits per target; thread class with `Threads=1`. First/last caller left out: the site address names the caller function already, and a caller-of-caller needs a stack walk per hit.
- **Own cost counters:** at arming, 256 int3 round trips on a private `CC C3` page (answered first in the handler, never a table hit) give `cpTrapNs` (ns per trap through the vectored handler list); QPC ticks inside the handler add the measured part. Once a minute: `cpTrapUs` (total), `cpHandlerUs`, `cpLastMinUs`, `cpMaxMinUs`, `cpMinutes`, and `cpLateUs` / `cpLateMin` from minute `CallSitesWarmMin` (10) on, so `cpLateUs / cpLateMin / 60000` = ms per s after warm-up, to hold against the light-tool budget (~2-3%, 20-30 ms/s); log line `callsites min N: hits H, trap X ms (P% of the minute), armed A, done D, poly Q, total T ms` plus the changed `vcall` pairs, only in minutes where something fired. `cpDone` = sites disarmed by the rule, `cpPoly` = sites sampled past 32 hits. Hits, pairs, faults and armed stay in the v43 `vc*` fields.
- **Harness (`tools/harness/queue.ts`):** every job that is not a timing job gets `ProbeSnap.CallSites=1` in its `Bin/ts3native.ini`; timing = a profile job with `noiseCheck` not false (A/B, natural and repro timing runs). Opt-outs: job `callSites: false`, an explicit `ProbeSnap.CallSites` in the job ini, `ProbeSnap.Enabled=1` (the windowed probe), worldimage jobs (memory captures). `callSites: true` forces it on a timing job. The row's `ini` shows the key; `hitch-ab.ts toolsOn` lists `ProbeSnap.CallSites` (tool-costs.json: light, timing `no`, cost not measured yet). Older asi builds ignore the key.
- **Merge database:** after each such job `tools/callsite-db.ts` merges the job's `ts3native.log` copy into **`probe-out/callsites/db.json`** (git-ignored: it changes every run and lives with the logs it came from; addresses and counts only). Keyed by site: runs seen, hits, targets with hits and module, skips; one row per run (the log's first line `ts3native X.Y.Z date time` = one game start) with the jobs, pairs, armed, ns per trap, trap ms and minutes. Copies of one run (session reuse) merge without double counting: per (site, target) the last run and its cumulative count are kept and only the growth is added [self-check]. By hand: `node tools/callsite-db.ts merge <ts3native.log> [--job id]`.
- **Report:** `node tools/virtual-resolution-report.ts --db [file]` (with or without `--log`): the §6 per-group numbers (glue / routing: armed, fired, % resolved static -> with live answers, I-131-12 / I-107-12) over the DB, the runs with their trap cost, and per group the armed sites no run has fired; `--sites-out` as before.
- **Hook rules kept:** the table is the §6 one, generated with the hook skip list (our sources + Apex `apex-hooks.csv`, 79 sites dropped); against 0.36.63's hook list (`node tools/hook-sites.ts --check`, exit 0) and `apex-hooks.csv` no site sits within 16 bytes of a hook [verified, script over both lists]. The runtime byte check still drops a site some patch rewrote after start.
- **Frida guard (2026-10-25):** `queue.ts` never adds the key to a profile job that attaches a second Frida agent (stutter profiler unless `noProf`, or `inroom` / `watchstalk` / `livereads2` / `f4b`; `fridaAttach()`): those 39 correctness profile jobs run without coverage and the queue log says `ProbeSnap.CallSites off: <agent> attaches a Frida agent (docs/67 §7)`. `callSites: true` or an ini `ProbeSnap.CallSites` != 0 on such a job is a parse error. Coverage comes from soak, matrix, repro and `noProf` profile jobs. No native guard (detecting a Frida agent from the asi is not simple or safe).
- **First collection run:** `tools/harness/queues/virtual-resolution-2.json` `bp-vres2-routing` now runs `game-copy/ts3native.asi.callsites` (0.36.64, normal build, builds.json) with `ProbeSnap.CallSites=1`, `noProf`, `minCounters` 130; pass = `OK   call sites` and >= 1 `callsites min` line, no faults; it reports the trap cost. The windowed `/DTS3N_VCSITES5` build `ts3native.asi.vres2` stays parked.
- **Build:** image 863 KB (the vres2 probe build of 0.36.62: 831 KB; the second table adds ~25 KB), selftest at `/BASE:0x20000000` as before; `build.cmd` selftest ok.

## 8. First collection run: `virtual-resolution-2` `bp-vres2-routing` (2026-10-25 11:42-12:16 PHT, PASS)

Asi = `ts3native.asi.callsites` 0.36.64 (passive, self-disarming; every per-repro log starts `ts3native 0.36.64`), BP, six repros, 34.1 min, five game starts (11:42 vehicles + route bits, 11:54 door lock, 11:58 upstairs, 12:03 build/buy crowd, 12:09 stuck check 3 AM), faults 0 in every log, no crash. Timing invalid by design (correctness tool, noise medium).

Report over the five per-run logs (`virtual-resolution-report.ts --log <each per-repro *-ts3native.log>`) [measured]:

| group | armed | fired | % resolved static -> with live answers | some target known | I- verdict |
|---|---|---|---|---|---|
| glue (docs/131) | 541 | 185 (34.2%) | 36.8% -> 55.0% | 57.9% | I-131-12: 62 caller-typed armed, 10 fired, 0 live targets outside the static set -> **pass** |
| routing (docs/102/106/107) | 1,721 | 948 (55.1%) | 17.6% -> 28.2% | 76.7% | I-107-12: 104 armed, 90 fired, 0 outside -> **pass** |

- Never fired: glue 356, routing 773 armed sites (list: `--db` or `--sites-out`).
- New pairs (target not in the static set): glue 98 on `[switch]` rows (jump-table case targets inside the same function: the static row names no case list, not a soundness gap) + 8 on receiver-typed rows with untyped caller sources; routing 47 on partly resolved rows, of them 39 on docs/106/107 live-list rows that say "not an exhaustive list" (expected) and 8 on `closure-built heuristic` rows (the heuristic missed a vtable built outside the closure: those 8 rows are wrong as static answers; the live target stands).
- **DB gap:** `probe-out/callsites/db.json` holds 1 run (972 sites; the job's `nativeLog` = the last game start only). `queue.ts` merges only `row.nativeLog`, so a `profile.repros` job that restarts the game per repro loses the other starts. By hand: `node tools/callsite-db.ts merge <per-repro log> --job bp-vres2-routing` for the 11:42, 11:54, 11:58 and 12:03 logs; fix in `queue.ts`: merge every per-repro `*-ts3native.log` of the row.

**Trap cost** [measured, `callsites min` lines]: ~5.3 us per trap (5,308 ns). First armed minute 185-289 ms of traps (31-38 k hits, 0.30-0.48% of the minute = ~3-5 ms/s); after that 0.3-11 ms per minute (<= 0.02%, < 0.2 ms/s) as ~750-900 sites disarm by minute 2-10; total 204-309 ms per game start. The late counter (minute >= 10) only reached in the vehicles run: 0.34 ms in minute 10. **Verdict: fits the light-tool budget (20-30 ms/s) by two orders of magnitude after the first minute; keep `CallSites=1` on every non-timing job.** The first-minute burst (~3-5 ms/s averaged) is the only part a timing run would see; not measured per frame, so timing jobs keep it off until a cost A/B sets `tool-costs.json`.
