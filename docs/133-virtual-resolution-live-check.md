# 133. Virtual-call resolution: live check (managed callvirt caches + native glue sites)

Written 2026-10-20. **Offline build only:** no game run, no queue started. Built and self-checked in an agent worktree; the probe job is ready for Rodney to run. Question (Rodney, after the v0.1.4 release: tests now focus on Mono confidence and virtual-call resolution): do the static resolutions that the Mono offload plan relies on hold at run time?

- **Managed:** the typed override closures of docs/127 (allow-list v0 + tier 2, `docs/data/autonomy-virtual-closure.csv`): is every target a `callvirt` really reaches inside those closures in the static target set?
- **Native:** the docs/131 glue sites that the v2 resolver left unresolved or partial, or resolved only by caller-side typing (`docs/data/mono-glue-closure.csv`, per site now in `docs/data/mono-glue-sites.csv`): which targets do they reach (closes I-131-12)?

One job does both: `tools/harness/queues/virtual-resolution.json` `bp-vres` (Baseline-BP-Spring, 10 min seeded profile, seed 12345, release ini defaults plus `[ProbeSnap] Enabled=1`). Report: `tools/virtual-resolution-report.ts`. Tags as in AGENTS.md; ledger rows in docs/51 §186.

## Summary

- **Managed mechanism: a read-only snapshot, no hook.** Mint's `callvirt` keeps a monomorphic inline cache per call site: a (`MonoClass*`, `RuntimeMethod*`) pair in the calling method's data items, overwritten on every miss [verified, §1]. The probe agent reads every reachable pair every 60 s and once at the end (`/vcache`, profile option `vres`) and keeps the union per site. The cache only holds the last target, so a polymorphic site shows the targets that happened to be last at snapshot times: 10 snapshots give a lower bound on the live target set, never a false target (§1.4).
- **Native mechanism: the existing `[ProbeSnap]` int3 sampler** (docs/94 §7, docs/106) with a new site table: 514 sites (`native/ts3native/vcallsites4.h`, built with `/DTS3N_VCSITES4`); the default build keeps `vcallsites.h`.
- **Overhead estimate** [inferred, to be read from the run]: managed 0 on the sim thread (nothing patched; the agent reads from its own thread), each snapshot a few seconds of one core on the agent thread with a 30 s budget, and ~20-40 MB of JS heap inside the 32-bit process; native at most 16 int3 hits per site per 30 s window = at most ~275 exceptions/s for 514 sites, ~1-3 ms/s spread over all threads (<0.3%), as the 1,182-site routing table of docs/106 ran.
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

Self-checks (no game): `node tools/virtual-resolution-report.ts --self-check`, `node tools/indirect-live-vs-static.ts --self-check`, `node tools/harness/queue.ts --self-check`, `python tools/snap-vcall-sites.py --self-check`, `python tools/mono-glue-closure.py --self-check`; the agent's mint decoder checks itself on a synthetic word stream before its first snapshot (`vcSelfCheck`).

## 4. Effort

Built here: ~3 h agent time. Run: 1 game run (~15 min), no clean night needed. Reading the report and fixing what it finds: ~30-60 min per gap class.
