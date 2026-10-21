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
