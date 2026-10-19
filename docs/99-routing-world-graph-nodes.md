# 99. Routing I/O study F: world graph R, the world search, the search node, tie order

Written 2026-10-18. Static only: the game, the probe server and test queues were not run, no native code was written. Part F of the routing I/O study (docs/91 inputs, docs/92 portals and parameters, docs/93 outputs, docs/94 snapshot proof). Goal (Rodney): replace the game's route search with our own search off the sim thread, with no gaps in how the game's search works. This doc is the **reimplementation spec** for the world-level part: the waypoint graph R, the world A\*, the hierarchical planner's glue between world and lot legs, the 64-byte search node, and which start or goal wins on ties.

Tools: Ghidra headless decompiles on a private copy `game-copy/ghidra-io-f` (copied from `ghidra-usf`, read-only runs); capstone 5 on `game-copy/bin/TS3W.exe` (Steam 1.67) for call arguments and x87 tails the decompiler lost; constants read from the exe image; `Sunset Valley.world` copied to `game-copy/worlds/` and decoded with the new read-only tool `tools/rtdata-decode.ts` (§1.10).

Tags as in AGENTS.md: **[verified]** = read in the decompile, disassembly, exe data or the package (address or file given); **[inferred]** = reasoning, with the live check that settles it in §7. Pseudocode is ours; field names are ours.

## 0. Summary

- **R is a 2-level packed grid** [verified]: `w x h` chunks of 32 m (Sunset Valley 64 x 64), per chunk a u16 count and u16 record offsets, per waypoint a header, packed u16 neighbour references (10-bit waypoint index, chunk delta -3..+3 on each axis) and one edge byte per neighbour. **The shipped files are version 1** (4-byte record head, no surface byte); the loader converts them to the version-2 in-memory form (5-byte head) and computes the surface byte from terrain at load. Saves write version 2 (§1).
- **Sunset Valley R** [verified, `tools/rtdata-decode.ts`]: 39,265 waypoints, 159,662 directed edges, every edge has its reverse with the same edge byte, no blocked waypoint in the package, 77 flood components. Seven waypoint types and six edge bytes occur; their pairing pins down the roles: type 1 = one hub per lot, type 2 = lot-edge waypoints (first neighbour always the hub, edge byte 1), types `0x10`/`0x7`/`0xD` = walkable network, type `0xE` = a separate network (edge byte 6) that never touches the others (§1.5, §1.6).
- **The world A\*** `0x676260` [verified]: lazy-deletion A\* on a binary min-heap ordered by `f` only, closed bitset per chunk, no decrease-key, goal test on pop, single goal waypoint. Edge cost = segment length x a per-edge multiplier (10 per metre for plain walking; sidewalk, street, trespass, keep-type, lot-group and portal factors), heuristic = straight-line distance x 1, so the heuristic is admissible and weak (a factor of ~6-10 below real cost). A first pass is restricted to a circle around the start-goal midpoint (radius `0.5 d + 48` m, only when `< 600` m); if it fails the search reruns unrestricted (§2).
- **Tie order is the heap's**, exactly EASTL `push_heap`/`pop_heap` on `f` with strict `<`: equal-`f` entries are not ordered by age or `g`; our search must replay the same heap operations in the same push order (edge record order, then the water-link maps in key order) to give identical paths on ties (§2.5).
- **The 64-byte search node** [verified for the fields below]: `+0x00..+0x08` position (y = -100 means "height not set" for world-leg nodes), `+0x0C` level (s16), `+0x0E` flags (u16), `+0x10` portal word, `+0x14` -999 default, `+0x18` 1.0 default, `+0x1C` node type, `+0x20/+0x24` portal object id (u64), `+0x28` portal word, `+0x2C` kind hash (default `0xB828`), `+0x30..+0x34` three u16 zero by default, `+0x38` world waypoint type / leg class, `+0x3C` never initialised by the node constructor (§3).
- **Ties between starts and goals** [verified]: the job picks one goal point by highest priority, then smallest squared distance to the first start; if every goal has the same priority and all distances are within 0.05 m² of the running best, it picks **at random** from an RDTSC-seeded LCG (seed 1337 when a debug flag is set). The planner then orders goal records by `priority x 1e6 - distance²` with a **stable** insertion sort (for up to 28 records) and takes the **first goal in that order whose search succeeds**, not the cheapest. Lot exits and entries are sorted by a portal score with the same stable rule and tried best first. Only the **first** start record feeds the world leg (§4).
- **`W+0xC` and `W+0x14` are different objects** [verified, call at `0x829FC4..0x829FE3`]: `W+0xC` = `[world+0x964]` (an object with the world origin at `+0xB4/+0xB8`, the world manager layout [inferred]); `W+0x14` = the water/cell grid G at `world+0x8C`. This corrects docs/91 §5 q7 and ledger I-91-4 (§5).
- **docs/93 check**: the job's `+0x38` pointer receives the chosen **start** index and `+0x3C` the chosen **goal** index (pre-set by `RoutingJob_Execute`, rewritten by `0x650950`) [verified]; docs/93 §4.3 has them the other way round, its §2 table agrees with this doc.

## 1. The world waypoint graph R

### 1.1 Resource and header

- One resource per world: type `0x05CD4BB3`, group 0, instance 1 in `Sunset Valley.world`, RefPack-compressed (478,997 bytes on disk, 739,164 unpacked) [verified, package index].
- Little-endian u32 `version`, u32 `w`, u32 `h` (chunk counts), then `w*h` u32 chunk offsets relative to the start of the record area, then the record area to the end of the resource [verified: `RTData_Load 0x66D110` reads three u32 through the stream reader, then the offset table; the decoder reads Sunset Valley with these rules and finds every reference in range].
- Loader rule [verified, `0x66D110`]: version `< 2` = old record form, converted record by record (§1.3); version `>= 2` = records copied as they are. `RTData_Save 0x66CC30` writes version 2, `w`, `h`, the offset table and the in-memory record area.
- Chunk geometry: 32 m per chunk side (`0x1020C78` = 32.0), positions inside a chunk in steps of 0.125 m (`0xFE8278`), world position = world origin (`WM+0xB4/+0xB8`) + chunk corner + step offset [verified in `0x66D110`, `0x656550`, the cost functions].

### 1.2 Chunk and record layout

<!-- docs-code: ok -->
```text
chunk c (offset table[c]):   u16 count, u16 recOff[count]   (recOff relative to the chunk start)
record (version >= 2 and in memory):
  +0 u8  n          neighbour count (the builder saturates at 255)
  +1 u8  flags      bits 0-4 type, 0x20 unused in shipped data, 0x40 edge bytes present, 0x80 blocked
  +2 u8  surface    computed at load for version 1 (rule below), stored for version 2
  +3 u8  bx, +4 u8 bz   offset in the chunk, metres = value * 0.125
  +5 u16 ref[n]     packed neighbour references
  +5+2n u8 edge[n]  only when flags & 0x40
record (version 1, the shipped .world files): the same without the surface byte: n, flags, bx, bz, refs, edges
```

Record size: `5 + 2n` (+n) in memory, `4 + 2n` (+n) in version 1 [verified, `0x66D110` copies `n*2` or `n*3` bytes after the head]. The 10-bit index allows 1,024 waypoints per chunk; Sunset Valley's largest chunk has 332 [verified].

- **Surface byte** [verified, `RTData_WaypointSurfaceClass 0x66CB90`]: type 1 gives `0x28`, type 7 gives `0x0A`, type `0xD` gives `0x1F`, any other type gives the terrain type at the waypoint (`0xC6C0C0(x, z, 4, 0)`); 0 if the world manager is null. Our snapshot must either store the version-2 bytes from a running game or sample the same terrain type map [inferred: no search code read it in this study; the cost modes do not].
- **Chunk coordinates quirk** [verified]: loader and builder take the chunk's x from `c mod w` and z from `c div h`; the search, the cost modes and the path converter use `c div w`. Same result for square worlds; Sunset Valley is 64 x 64; the other worlds' shapes were not checked (§7 F-6).

### 1.3 Neighbour references and the edge list

- Packing [verified, builder `0x810720`, A\* `0x676260`, flood `0x66CE10`]: bits 0-9 = waypoint index in the target chunk, bits 10-12 = `dx + 3`, bits 13-15 = `dz + 3`. Target chunk = `c + (dz)*w + dx` with **no bounds or wrap check** (a reference across the map edge would alias; Sunset Valley has none, all refs in range).
- Builder rule [verified, `0x810720`]: only source edges whose valid byte (`+4` of the 8-byte source edge) is non-zero are written; the edge byte comes from source `+5`; the flag byte gets `0x40` always, so a rebuilt R always has edge bytes. Version-1 files may lack `0x40`; Sunset Valley never does [verified].
- Graph shape in Sunset Valley [verified, decoder]: every directed edge has its reverse, and both directions carry the same edge byte, so R is an undirected graph with symmetric edge classes. The search does not rely on this (it reads only forward lists).

### 1.4 In-memory object (`world+0x54`)

| offset | field | writer |
|---|---|---|
| `+0x00` | w (chunks); the A\* reads it as a u16 for the neighbour decode | loader, builder |
| `+0x04` | h | loader, builder |
| `+0x08` | chunk offset table (u32 per chunk, "Routing/RTData/chunkOffset") | loader |
| `+0x0C` | record area ("Routing/RTData/rawData"); null = no R | loader |
| `+0x10` | record area size | loader |
| `+0x14` | components built (byte) | flood `0x66CE10` |
| `+0x18..+0x24` | vector: per chunk, first waypoint's index into the colour array ("Routing/chunkOffset") | flood |
| `+0x28..+0x34` | vector: one colour byte per waypoint ("Routing/colors") | flood |

[verified, `RTData_ctor 0x66CD30`, `RTData_Clear 0x66CD70`, `0x66D110`, `0x66CE10` with `this = R+0x14`.]

### 1.5 Waypoint types (Sunset Valley counts)

| type | count | surface | edges seen | role | tag |
|---|---|---|---|---|---|
| `0x01` | 92 | `0x28` | byte 1 to its lot-edge waypoints only | lot hub, one per lot | counts verified; role inferred (92 hubs, every type-2 waypoint's first neighbour) |
| `0x02` | 6,648 | terrain | byte 1 to the hub (always neighbour 0), byte 2 to other lot-edge waypoints of the same lot, byte 7 to the walk network | lot-edge waypoint | verified pairing; role from `ApplyLotUpdate` (docs/91) |
| `0x07` | 275 | `0x0A` | byte 5 among themselves, byte 7 to `0x10`/`0x02`/`0x0D` | [inferred] paths with their own class (bridges or footpaths) | counts verified |
| `0x0D` | 924 | `0x1F` | byte 3 among themselves, byte 7 to others | street (car) waypoints: modes 2/3 use 1/street when either end is `0x0D`, and the path converter turns them into car nodes (node type 1, kind `0x3B83`, §3) | verified uses; name inferred |
| `0x0E` | 12,151 | terrain | byte 6 among themselves only | separate network, never connected to the others, coloured -1 by the flood | verified; role inferred (traffic lanes or street runs) |
| `0x10` | 19,167 | terrain | byte 7 | general walkable waypoints | counts verified |
| `0x11` | 8 | terrain | byte 7 | unknown | open |
| `0x13`, `0x15` | 0 in SV | terrain | water links through G's maps (§2.4) | water entry points (`0x15` = with the boat test) | verified code paths; no SV data |

Other types the code tests: 5, 6, 8, 9, `0xB` (node type mapping, §3) [verified constants; no SV data].

### 1.6 Edge bytes (Sunset Valley counts, directed)

| byte | count | between | cost treatment (§2.6) |
|---|---|---|---|
| 0 | 0 | — | wall in modes 1, 3, 4 (only reachable with records lacking `0x40`) |
| 1 | 13,296 | hub ↔ lot-edge | lot-connection branch (start/goal lot rules, 5e6, x30, trespass) |
| 2 | 13,296 | lot-edge ↔ lot-edge | trespass: x `trespass` x 10, x 1000 instead of x 10 if the two ends are in different lot-edge connectivity groups; segment obstacle test |
| 3 | 1,866 | `0x0D` ↔ `0x0D` | mode 1: x 10 / sidewalk; mode 2: x 1/street (or 1/riding if neither end is `0x0D`); mode 3: x 10 / street or 10 / riding |
| 4 | 0 | — | x 10 / sidewalk (modes 1, 3) |
| 5 | 536 | `0x07` ↔ `0x07` | x 10 / sidewalk (modes 1, 3) |
| 6 | 24,302 | `0x0E` ↔ `0x0E` | x 10, no class factor |
| 7 | 106,366 | walk network | x 10, segment obstacle test |
| 8 | (virtual) | waypoint ↔ water body | used by the A\* for water links only |

[Counts verified, decoder. Treatment verified per mode in the decompile; the "sidewalk/street/riding" names are the tuning keys the factors come from (`0x673C70`), the physical meaning of each byte is inferred.] Answers docs/91 §5 q6: the per-edge byte is an **edge class** that selects the cost factor, not a cost.

### 1.7 Blocked flag

> **Changes 2026-10-18** (docs/112 §3): the second writer is `0x82A190`, not `0x832890`; when the bit appears after load added.

- Never set in the package (0 of 39,265 in Sunset Valley) [verified]. Set and cleared in play only by `RoutingWorld_ApplyLotUpdate 0x82B590` and `0x832890` on lot-edge waypoints (docs/91 §3.1) [verified there]. (corrected 2026-10-18, docs/112 §3.2: the two writers are `0x82B590` and `RoutingWorld_RestoreLotBlockedBits 0x82A190`, which copies portal word bit 29 back into bit 7 when a lot's routing is (re)built (`LotRouting_FinishBuild 0x63CAF0` from `LotGraph_Build`, `World.LotSetEntranceByTargetPoint`, lot-load message `0xF252DC33`); `0x832890` only reads bit 7 and copies it into the portal word [verified]. The bit appears lazily as lots get their first plan after load (BP: 0 at load, 284 after 10 min; IP: 98) [measured, docs/108 §7].)
- Type bits of the same flag byte are not static either: every G build rewrites `0x10`->`0x13` and `0x02`->`0x15` on shoreline waypoints (`0x66FD00`, docs/112 §3.1, docs/100 §3.2 step 8) [verified]; so §1.5's type counts are the package values, before the load's G build.
- Effect in modes 1, 3 and 4 [verified]: an edge **into** a blocked waypoint is a wall unless both ends are lot-edge types (`0x02` or `0x15`) **and** both records' first neighbour reference is the same u16 (the same lot hub). So a blocked lot edge cannot be entered from the street, but movement between edge waypoints of the same lot stays possible. Mode 2 ignores the flag. The start waypoint's own flag is never tested.
- The gather of lot exits (`0x654CC0`) skips portals whose R waypoint is blocked (§4.4) [verified].

### 1.8 Components

[verified, `RTData_BuildComponents 0x66CE10`] Colours start at -2 ("unassigned"). Every type-`0xE` waypoint is coloured -1 and never seeds or joins a flood. Seeds are taken in chunk order, then waypoint order; each flood is a depth-first stack walk over the forward references, colour = flood number stored in a **signed byte**, starting at 0. The outer loop stops when all waypoints are coloured or the next colour would be -2 (after 254 floods; Sunset Valley needs 76 plus the -1 group). The planner rejects a goal portal whose colour differs from the start's unless water or boat flags are set (§2.7).

### 1.9 Builder input (for completeness)

`WorldRoutingData_LoadChunks 0x810720` [verified]: source chunks are 0x50-byte records in a vector at `+0x2C`, waypoints 0x20 bytes (`+0` x, `+4` z, `+8` flags low 5 bits = type, `+0x10/+0x14` edge vector), edges 8 bytes (`+0` u32: high 16 bits = target chunk index, low 10 bits = waypoint; `+4` valid byte; `+5` edge byte). Record area sized `n_edges*3 + 2*w*h + n_waypoints*7` before compaction. It runs only for `BuildRoutingData` / world save (docs/91).

### 1.10 Decoder

`node tools/rtdata-decode.ts <file.world> [--chunk N]` prints version, size, counts, type and edge-byte histograms, reference checks and the flood count for the `0x05CD4BB3` resource; `--self-check` builds synthetic version-1 and version-2 resources and checks the record parse, reference packing, blocked bit and flood. Read-only: it never writes the package. Sunset Valley output (abridged): `version 1, 64x64 chunks`, `waypoints 39265, directed edges 159662, blocked 0`, `refs out of range 0, one-way edges 0, components 77, in-memory size 762033 bytes`.

## 2. The world planner's search

### 2.1 Objects

- **W** = `[world+0x960]`, "PathPlannerWorld" (ctor `0x657C50`, init `0x654060`) [verified]: `+0x08` P (lot planner, ref-counted), `+0x0C` `[world+0x964]`, `+0x10` R, `+0x14` G, `+0x18` the A\* container (embedded), `+0x88` "R usable" byte (copied from `world+0x97D` each plan), `+0x94/+0x98` stroller tuning, `+0xA4` "LotEdgeConnectivityGroup" hash (waypoint ref → group id, read by `0x653200`), `+0x100` "GoalPtSet" hash, `+0x11C/+0x120` lot-exit option masks built per plan by `0x64FEA0` from `RoutingWorld+0x4A..+0x4F`, `+0x124` = `t*t + t` from `RoutingWorld+0x24`, `+0x128` and `+0x160` the two "BestLotEdgePortals" candidate sets (start lot, goal lot; layout §4.4).
- **A\* container** (`W+0x18`, ctor `0x673C70`) [verified]: `+0x00` R (corrected 2026-10-19, docs/94 §13.3 K6: `+0x00` is the vtable `0xFEF990`, R is at `+0x04`; the closed-list init entry `0x6735C0` is `add ecx,4; jmp 0x673500` [verified], ran in steerro-test K6), `+0x08` closed-set byte size, `+0x0C` per-chunk bit-offset table (index 0 = virtual nodes, 256 bytes = 2,048 bits; chunk c at index `c+1`, `ceil(count/8)` bytes each; built by `0x673500`, "Routing/ClosedList"), `+0x10` closed bits, `+0x14..+0x20` node pool (blocks of 1,000 x 20 bytes, free list at `+0x20`, link at node `+0x0C`), `+0x24` "WR Long Distance Threshold", `+0x28..+0x48` SHORT cost set (9 floats), `+0x4C..+0x6C` LONG cost set.
- **Cost sets** [verified defaults in `0x673C70`, ini values from `game-copy/bin/Sims3.ini`]: order keep-type, trespassing, sidewalk, riding, street, water-lot, then 3 slots overwritten per search. Defaults: threshold 50; SHORT 1.3, 2, 2, 2, 2, 2; LONG 1.3, 2, 4, 10, 10, 10. Sims3.ini `[Routing: World Routing Tuning]` sets threshold 80, keep 1.3, trespass 1.3, LONG sidewalk 1.7, LONG street 6.0, SHORT sidewalk 1.5, SHORT street 3.0 [values verified in the ini; that the ini overrides the defaults is inferred from the registration calls, docs/03].

### 2.2 World search node (20 bytes)

| offset | field |
|---|---|
| `+0x00` | g (float, cost from start) |
| `+0x04` | f = g + h (heap key) |
| `+0x08` | u16 chunk (`0xFFFF` = virtual water node) |
| `+0x0A` | u16 waypoint index (virtual: water body id) |
| `+0x0C` | parent node (pool free-list link when free) |
| `+0x10` | reference count (children + open-list entries) |

A waypoint reference as a u32 is `chunk | wp << 16` throughout the planner [verified].

### 2.3 Search context ("opts", a stack struct in `0x65E0D0`, passed to every cost call)

| dword | offset | content | writer |
|---|---|---|---|
| 0 | `+0x00` | flags: `0x10` start in a lot, `0x20` goal in a lot, `0x40` retry (internal `0x800000`), `0x80` water planning (`0x9008000` mask), `0x100` internal `0x8000`, `0x200` internal `0x40` | planner |
| 1 | `+0x04` | start lot hub ref (`0x650D40`) | planner |
| 2-4 | `+0x08` | start point x, z, level | planner |
| 5-12 | `+0x14` | start lot "edge components" set | planner (`0x652BF0` init) |
| 13 | `+0x34` | goal lot hub ref | planner |
| 14-16 | `+0x38` | goal point x, z, level | planner |
| 17-24 | `+0x44` | goal lot edge components set | planner |
| 25-33 | `+0x64` | active cost set; slots 31-33 = 1/sidewalk, 1/riding, 1/street; slot 26 = FLT_MAX when flag `0x40` | A\* entry |
| 34 | `+0x88` | W | planner |
| 35 | `+0x8C` | job parameter block (polygon set for the segment test, route option word) | planner |
| 36-39 | `+0x90` | circle centre (midpoint start-goal) | `0x673600` |
| 40 | `+0xA0` | circle radius² (0 = no circle) | `0x673600`, A\* |
| 41 | `+0xA4` | start waypoint ref | A\* |
| 42-44 | `+0xA8` | start x, z, level | planner |
| 45 | `+0xB4` | goal waypoint ref | A\* |
| 46-48 | `+0xB8` | goal x, z, level | planner |

[verified frame offsets against `local_1f0` in `0x65E0D0`; set roles inferred where named "edge components".]

### 2.4 Algorithm

<!-- docs-code: ok -->
```text
search(R, G, start, goal, mode, opts) -> path of refs, or none; best := start
  if opts.r2 == 0 and circle_ok(start, goal, opts):        # 0x673600
      opts.r2 = (0.5*d + 48)^2, centre = midpoint            # only when 0.5*d + 48 < 600
      p = search(...)                                        # same call, circle active
      if p: return p
      opts.r2 = 0
  opts.costs = LONG; h0 = cost(start, goal, heuristic)
  if h0 <= threshold: opts.costs = SHORT
  opts.inv = 1/sidewalk, 1/riding, 1/street ; if flags & 0x40: trespass = FLT_MAX
  clear closed bits
  push {g 0, f h0, ref start, parent none}
  while heap not empty:
      n = pop_min_f()                                         # EASTL pop_heap, §2.5
      if closed(n.ref): release(n); continue
      if n.ref == goal: return chain(n)                        # goal test after the closed test
      close(n.ref)
      for each neighbour m of n in push order (§2.5):
          if closed(m): continue
          c = cost(n.ref, m, edge = edge byte or 8, parent = n.parent)
          if c >= 1e8: continue                                # wall
          h = cost(m, goal, heuristic)
          push {g n.g + c, f n.g + c + h, m, parent n}
          if h < best_h: best_h = h; best = m                  # strict <: first wins on ties
      release(n)
  return none   (best kept for the blocker query, §2.7)
```

- Neighbours of a real waypoint: its records' references in record order, edge byte from the record (0 without `0x40`); then, for types `0x13` and `0x15` only, the entries of G's map at `G+0x30` keyed by the u32 `chunk<<16 | wp`, each giving a virtual node `{0xFFFF, body}` (entry `+0x14` byte), admitted when the entry's foot/boat slot (`+0x18` for foot, `+0x1C` when flag `0x100` is set) is not -1 and either the waypoint is type `0x15` or `0x66F890` accepts it. Edge byte 8 [verified].
- Neighbours of a virtual node: entries of `G+0x4C` keyed by the body byte; flag `0x200` admits only type-`0x15` targets; same slot tests; boat flag = option `0x100` and an AGS code in `{0xC00, 0xD00, 0xE00, 0xF00, 0x4100, 0x4200, 0x4300, 0x4500, 0x4600}` (`0x55DA60`). Edge byte 8 [verified].
- Path out: the goal node's parent chain reversed into a vector of u32 refs ("Routing/WorldPath", `0x676130`) [verified].

### 2.5 Open-list order and ties (exact)

[verified, `0x6761F0`, `0x673B60`, `0x5EB920`] The open list is a vector used as a binary min-heap on `f` (`+0x04`):

- **push**: append, then sift up while `parent.f > value.f` (strict); an entry equal to its parent stays below it.
- **pop**: take `[0]`, move the last entry into a temporary, then walk a hole from the root to a leaf, at each level taking the **left** child only if `left.f < right.f` (ties go **right**); if the hole ends with a single left child, take it; then sift the temporary up from the hole with the push rule.
- No `g` or age tie-break, no decrease-key; duplicates of a waypoint coexist and the first one popped closes it.
- **Push order** = record order of references, then G-map entries in key order (red-black tree in-order; equal keys in insertion order [inferred: EASTL multimap]).

A replacement that wants identical paths on equal-cost alternatives must use float32 arithmetic in the same order (g + c, then + h), the same heap procedure and the same push order. With any other order, paths differ only between equal-`f` alternatives (docs/94 mismatch class M1).

### 2.6 Cost functions

All four modes are `cost(R, G, from, to, isEdge, edgeByte, opts, parent)` returning float [verified]. `mode` comes from `RoutingJob_Execute`: 1 by default, 3 with `EnablePlanningAsCar`, `& ~1` with `DisablePlanningAsPedestrian` (1 → 0, 3 → 2), 4 with `IgnoreSidewalkAndLotRestrictions`; any other value uses `RouteCost_Default 0x6734E0` (1 as heuristic, wall as edge: no world leg) [verified, `0x81B2A0` and the switch in `0x676260`].

**Heuristic call** (`isEdge` = 0): positions of `from` and `to` (the goal's own point instead of its waypoint when `opts & 2`, the goal is a hub and equals the goal lot hub; the start's point likewise with `opts & 1`, each also multiplying by keep-type), circle test, then `distance x 1`. Type `0x02`/`0x15` targets must pass the world-manager point test `0xC6BAB0`, else wall [verified]. So `h` = straight-line metres.

**Edge call, mode 1 (pedestrian)** [verified, `0x674E40`]:

1. Walls: `opts & 0x200` and target type 2; target type `0x15` without `0x80`/`0x100`; target blocked (§1.7 rule); edge byte 0.
2. Factor `k` by edge byte: 1 → lot-hub rules (below); 2 → `trespass x 10`, or `trespass x 1000` when `0x653200` gives different lot-edge groups for the two ends; 3, 4, 5 → `10 / sidewalk`; anything else (6, 7, 8) → 10.
3. If the waypoint types differ: `k x keep-type`. If `k <= 0`: cost 0.
4. Circle test on the target (not for edge byte 3); for edge bytes 2 and 7 the segment is tested against the job's polygon set (`RouteCost_Dispatch 0x627780` with `this` = P); a hit is a wall.
5. Cost = `k x |from - to|` (2D).

Lot-hub rules (edge byte 1, both lot-edge ↔ hub directions) [verified branches and `ecx` = target at `0x674F5A`; meaning inferred]: an edge **into** the start lot hub (`opts+0x04`) with `opts & 0x10`, or into the goal lot hub (`opts+0x34`) with `opts & 0x20`, costs a fixed 5,000,000; into the goal lot hub without `0x20` → wall unless the source's lot-edge group is in the goal set, factor 30 unless `opts & 8` and a further ref test; into any other hub → factor 10 (x keep-type, since the types differ); out of a hub with a known parent → trespass factor (x 10, x 1000 across groups), where the **from position uses the parent's in-chunk offsets with the current chunk's corner** (a quirk to copy); out of a hub as the first expansion (no parent) → wall unless the target's group is in the start set, factor 30 unless `opts & 4` and a further ref test. So crossing a third lot through its hub costs about `10 x 1.3` in and `trespass x 10 x 1.3` out per metre.

Virtual (water) edges in mode 1 (only with `opts & 0x80`): same node and same body → 1; heuristic without G → 100; from a virtual node to the start or goal waypoint → 10 x distance; otherwise `10 x bodyValue` (`G+0x3A0 + 8*id`, `0x66D8D0`) with a minimum of 1, or the water-lot factor for type `0x15` ends [verified].

**Mode 2 (car only)** [verified, disassembly `0x6738C8..0x673B55`]: virtual ends → wall; edge byte must be 3, else wall; factor = 1/street if either end is type `0x0D`, else 1/riding (**no x 10**); circle test; cost = factor x distance. No blocked test. In Sunset Valley only the 1,866 `0x0D`-`0x0D` edges carry byte 3, so a car-only search moves on the street waypoints alone.

**Mode 3 (pedestrian + car)** [verified, `0x674250`]: as mode 1, except edge byte 3 → `10 x 1/street` (either end `0x0D`) or `10 x 1/riding`; edge bytes 4, 5 → `10 / sidewalk`; virtual edges additionally allow lot-bound water entry when the start or goal lot `+0x4B0` matches and the point is below sea level (`0xC61670`, minus 0.75 with the boat flag).

**Mode 4 (ignore sidewalk and lot restrictions)** [verified, `0x675800`]: as mode 1 without the sidewalk/street factors and **without the keep-type factor**; edge byte 2 → 10, or 1000 across lot-edge groups, or wall on the retry flag.

### 2.7 Termination, failure and the partial target

- Success: the goal waypoint is popped unclosed. Failure: the heap empties (every reachable node expanded; no node, time or cost budget, docs/11) [verified].
- `best` = the neighbour with the smallest heuristic seen, first one on ties; after a failed world leg with `param_10 & 0xF` the planner sends its position to the obstacle query `0x6593F0` (the blocker, docs/93) [verified call at `0x65FE58..`].
- Component pre-check (goal in a lot, no water/boat flags `0xC9008000`): if the start waypoint's colour differs from the chosen goal portal's colour, the planner **stops trying further goal portals** and returns failure [verified, `0x65F781..`].

### 2.8 How world legs connect to lot legs (`PathPlanner_PlanHierarchical 0x65E0D0`)

[verified flow; branch conditions as listed]

1. Start lot `Ls`, goal lot `Lg` from `World_LotLookup` on the first start and the goal point. Locked lot pairs (`+0x4BC`) on different lots → code 8.
2. No R (`[R+0xC]` null) or `W+0x88` clear → lot-only plan (`LotPlan`).
3. `Ls == Lg` (non-null) → in-lot plan; code 5 with "keep going" falls through to the world path.
4. Start-goal distance² `< 178.76` (13.37 m), no flags `0xC1000280`, `mode & 5` non-zero (every mode except car-only 2), and `0x6443A0` says the straight segment is clear → a 2-node path (start type 6, goal type 7) without any search.
5. Goal lookup order (§4.2); lot-exit option masks (`0x64FEA0`).
6. Start in a lot: `LotSearch_Entry` for the start point (flag `0x10`), gather the start lot's exits into `W+0x128` (`0x654CC0`), pick the best exit (`0x65BC20`), plan the lot leg from the start points to that exit's position (`LotPlan`, goal = exit, record "Routing/lotGoal"). Its failure ends the plan (no second exit is tried) [verified].
7. Goal in a lot: `LotSearch_Entry` for the goal (else for each goal record in lookup order), flag `0x20`, gather the goal lot's entries into `W+0x160`, then loop: pick the best entry, component check, world A\* from the start waypoint (the start exit, or the closest waypoint to the start), on failure erase that candidate and pick again. On success the goal lot leg runs from the entry to the goal points.
8. Goal on open ground: closest waypoint to the goal (`0x65BB90`, up to 3 widening tries of `0x6595B0`); with one goal record a single A\*; with several, per goal in lookup order: closest waypoint, skip if that waypoint was already tried (`W+0x100`), a 2-node direct path if it equals the start waypoint and the points are within 1 m, else A\*; first success wins.
9. Assembly into the 64-byte node vector (§3): start node or the start lot leg's nodes (its last node retyped 9), then one node per world waypoint (intermediate lots crossed by the world path get their own lot legs between the neighbouring world waypoints, first node retyped 8, last 9), water legs between waypoints whose G cell ids differ (`WaterPath_Plan 0x671930`, `0x656A70`), then the goal lot leg (first node retyped 8) or a goal node (type 7, `+0x38` = 16).
10. Post passes: `0x659BC0` (street runs), `0x64E380` (19 node passes: heights, stroller, boats, doors, smoothing) [verified call list; per-pass effect not read], start/goal indices (`0x650950`).

Return codes: 0 ok, 1 trivial, 4 no lot entry for the goal, 5 no route, 6 lot leg failed (retried once with `0x800000` by `PathPlan_WithRetries`), 7 post pass failed, 8 locked lots [verified values; meanings from the branches].

## 3. The 64-byte search node

Built by `0x6412F0` (defaults), appended by `0x62A300` / `0x629140` into the "Routing/Path" object (`0x64E5C0`, 0x18 bytes, nodes at `+0x08..+0x10`), split by `RoutingJob_SplitSubPaths 0x81A340`, copied into 48-byte path nodes by `RoutingPath_CopyNodes 0x819E10` [verified].

| off | size | default | world-leg writer | lot-leg writer | readers | meaning |
|---|---|---|---|---|---|---|
| `+0x00` | f32 | 0 | `0x65E0D0` from the world record (`0x656550`: chunk corner + offset + origin; lot-edge waypoints replaced by the lot's snapped edge position `0x653020`) | lot planners | builder (position), splitter, post passes | x [verified] |
| `+0x04` | f32 | -100 | -100 for waypoint nodes (record y is the constant -100), terrain height for start/goal nodes | lot planners | builder | y; -100 = height not set [verified value; resolved by a post pass, inferred] |
| `+0x08` | f32 | 0 | as x | lot planners | builder | z [verified] |
| `+0x0C` | s16 | 0 | level from `0xAB1F00` for type-`0x15` nodes on lots with `+0x408 == 1 && +0x40C == 0x50` | portal pairs copy it; read as level by `0x62FBE0` | `0x62FBE0` | level [verified read as signed short; name inferred] |
| `+0x0E` | u16 | 0 | 0 | `0x643210`, `0x64A950` (`& ~0x200 | 0x400`), `0x64C580` (`| 1`) | splitter (`0x40`, `0x80`, `0x100`, `0x200`, `0x800`, `0x1000`), builder (bit 0 → path flag `0x10`, bit 1 → `0x20`) | flags [verified bits; meanings open] |
| `+0x10` | u32 | 0 | 0 | portal exit node: portal `+0x2C`; entry: 0 | not read by splitter or builder | portal word [verified writes] |
| `+0x14` | u32 | -999 (`0xFECCFC`) | default | default | none found | unknown |
| `+0x18` | f32 | 1.0 | default | default (no write found) | none found | unknown |
| `+0x1C` | u32 | 0 | 6 start, 7 goal, 8 first node of a lot leg after the world, 9 last node of a lot leg before the world, 10 for waypoint types 6/7/9, 1 for types 5/8/`0xD` (with `+0x2C = 0x3B83`) and `0xB` (`0x347C`), else 0 (`0x64FD10`) | 2 portal entry, 3 portal exit (`0x62FBE0`), 10 (`0x644FD0`, `0x64DD40`), 0 (`0x641BF0`, `0x641DF0`) | splitter (values 0, 1, 2, 3, 6, 7, `0xB`), docs/93 | node type [verified] |
| `+0x20` | u64 | 0 | 0 | portal object id (portal `+0x08/+0x0C`) on both portal nodes | splitter copies it into the subpath's object id | portal object [verified] |
| `+0x28` | u32 | 0 | 0 | portal `+0x30` | not read by splitter/builder | portal word [verified write] |
| `+0x2C` | u32 | `0xB828` | `0x3B83` / `0x347C` for type-1 nodes, else default | portal kind (portal `+0x34`, e.g. `0xB828`, `0xB0C2`), `0xA582` (`0x64B430`), `0xB0C2` (`0x649890`) | splitter (`0x3B83`, `0x347C`, `0xA582`, `0xB828`, `0xB0C2`) | kind hash [verified values] |
| `+0x30` | u16 | 0 | default | default | none found | unknown |
| `+0x32` | u16 | 0 | default | default | none found | unknown |
| `+0x34` | u16 | 0 | default | default | none found | unknown |
| `+0x38` | u32 | 0 | world waypoint type (`0x14` = virtual water node); start 16 (`0x10`); 13 on the extra car-start node (type 1, `0x3B83`, internal flag `0x80` = `BeginAsCar` per docs/11; so `0x3B83` is the car kind and waypoint type `0x0D` maps to it); 20 (`0x14`) on lot-leg joints for the special lot kind | lot legs leave the default unless retyped | none found in splitter or builder | leg class [verified writes; no reader found] |
| `+0x3C` | u32 | not set | — | — | none found | padding or unused [inferred] |

Only `+0x00..+0x0E`, `+0x1C`, `+0x20/+0x24` and `+0x2C` reach the route (builder and splitter); a replacement that emits nodes must fill those exactly as above and may leave the rest at the defaults [inferred: no reader of `+0x10`, `+0x14`, `+0x18`, `+0x28`, `+0x30..+0x3C` was found among the splitter, the builder and the 19 post passes' node-type tests (corrected 2026-10-18, see docs/99 §10.3: `+0x38` has 9 readers in the post passes, so it must be filled; the other listed fields have none in the scanned scope); the post passes were scanned for writes, not fully read].

Intermediate **world path record** (`0x656550`, 20 bytes, "Routing/WorldPath" list): u16 chunk, u16 waypoint, f32 x, f32 y (= -100), f32 z, u32 waypoint type (or `0x14` for a virtual node, whose position copies the previous record's; a leading virtual node copies the next one's) [verified].

## 4. Ties between starts and goals

### 4.1 Goal point chosen by the job (`RoutingJob_Execute 0x81B2A0`)

[verified] Over the job's destination records (64 bytes each, `job+0x28`): highest priority `+0x20` wins; on equal priority the smaller squared distance to the first start point wins (strict `<`: the earlier record stays on exact ties). If there is more than one record, all priorities equal the first and every distance is within 0.05 m² of the running best, the index is replaced by `rng(n)`: an LCG at `job+0xD0` (`s = s*0x41C64E6D + 0x3039`, result = high 32 bits of `(s64 >> 16) * n`), seeded from RDTSC (`0x4FA800(-1)`) and reseeded with 1337 when the byte `0x11CDBC5` is set. The chosen index is written through `job+0x3C` and its position becomes the planner's goal point (on the equal-priority branch the position is copied by the vector helper `0x61BA00` at `0x81BA7E` [inferred: into the same goal-point local]). **Non-deterministic**: a replacement cannot reproduce it bit for bit; the shadow comparison must allow any record of that tie set (docs/94 M1).

### 4.2 Goal lookup order (`0x65E0D0`)

[verified] Score per goal record (28 bytes: x, z, level, ..., `+0x18` priority): `priority x 1,000,000 - dist²(first start, goal)`. Indices are sorted by score, descending, with EASTL `sort`: introsort above 28 records (unstable), a straight insertion sort on the whole range for 28 or fewer, which keeps equal scores in record order. The planner then tries goals in that order (open ground) and stops at the first that succeeds; for a goal lot the goal lot leg receives all records and the lot planner chooses (out of scope here).

### 4.3 Which start

[verified] The world leg uses only the **first** start record (`(*starts)[0]`) for the lot lookup, the exit choice and the closest waypoint. The start lot leg gets the whole start vector and returns the start index it used (`LotPlan`, out of scope). `0x650950` writes the start index through `job+0x38` and the goal index through `job+0x3C`, keeping the planner's goal index if that record's x/z equals the reached goal within 2^-16 m, else the first record at that position, else 0.

### 4.4 Lot exits and entries

[verified, `0x654CC0`, `0x65BC20`, `0x6564D0`, `0x650FD0`] Candidates = the outside room's portals of the lot graph (`room[+0x16C].portals`) that are enabled (`+0x58` bit 1), not blocked in R (`+0x4C` bit 29 clear and R flag `0x80` clear), pass the sidewalk/street option rules and (with locomotion on) a physics range query at the edge point. Each 28-byte candidate: portal, distance to the own point, distance to the other point, world x/z (`+0x50/+0x54`), score, three flag bytes. Score = base distance (to the own point with flag `0x40`, else to the other end) + optional facing term `(1 - cos) x 100` + `2 x` the distance from the portal to the segment own point → other end, then x 2 when the water/height rules reject it, + 100,000 when the option masks reject its class. Sorted ascending by score with the same stable insertion rule (28 or fewer), the first is used; the portal's packed R reference (`+0x4C`: bits 0-13 chunk, 14-23 waypoint) is the world-leg start or goal waypoint. Ties: the earlier portal in the room's portal list.

### 4.5 Closest waypoint

[verified outline, `0x6595B0`; comparator not read] Up to 3 tries with growing parameters; candidates from a cached search area (`0x652D80`, 10 cached "FindClosestParams"), distances to both ends stored per candidate, sorted (`0x6543B0`), the first up to 6 re-scored by direction when more than one, then the first candidate that passes the routability test (`0x659390`) is taken; water bodies short-cut to a virtual node when flag `0x2000` is set and the point is in a water cell.

## 5. `W+0xC` and `W+0x14`

Not the same object [verified]. `PathPlannerWorld_Init 0x654060` stores its four stack arguments as `W+0xC` = `[world+0x964]`, `W+0x08` = P, `W+0x10` = R (`world+0x54`), `W+0x14` = G (`world+0x8C`) (pushes at `0x829FC4..0x829FE3`). `[world+0x964]` is written by `0x829500` from `0xB04BDA` and `0xD41AF3` and read with the world origin at `+0xB4/+0xB8` (`0x656550`, `WaterGrid_CellIndex 0x66D850` takes it as its first stack argument while `ecx` = G), the layout of the world manager `0x11ECBC4` [inferred identity]. Both are used together: the cell index comes from G's width and the manager's origin, the cell bytes from `[W+0x14]+0x1C`.

## 6. What a replacement needs (spec checklist)

1. Load R from `0x05CD4BB3` (version 1 or 2), keep the record order, and replay lot-edge blocked bits from our lot copy (docs/91).
2. Implement the four cost modes with float32 arithmetic in the game's order, the circle pre-pass, the SHORT/LONG choice against the threshold and the reciprocal slots.
3. Implement the heap exactly (§2.5) and the push order.
4. Port the planner glue of §2.8 or call it on the sim thread around our A\*; the lot legs, portal choice and post passes are lot-side work (docs/91 §3.6).
5. Emit 64-byte nodes with the §3 fields.
6. Treat the job's RNG goal choice as a tie set, not an exact answer.

## 7. Open questions and the live check for each

| id | unknown | live check |
|---|---|---|
| F-1 | Do the Sims3.ini tuning values replace the `0x673C70` defaults at runtime? | probe reads `W+0x18+0x24..+0x6C` after load: expect 80 / 1.3 / 1.3 / 1.5 / ... |
| F-2 | Is the version-1 → memory conversion exactly the decoder's (surface bytes aside)? | dump R's two arrays after load and compare with `tools/rtdata-decode.ts` output (ledger I-91-2) |
| F-3 | Meaning of node flags `+0x0E` and of `+0x38` | counters on the splitter per flag bit with the resulting `PathType` |
| F-4 | Which post pass (`0x64E380` chain) sets world-node heights (y = -100) | probe: node y before and after `0x64E380` |
| F-5 | Role of waypoint types `0x0E` and `0x11`, edge byte 6 | dump positions of type `0x0E` waypoints over the street map (`--chunk`) and compare with road meshes; offline |
| F-6 | Non-square worlds (div h vs div w quirk) | run the decoder on every installed world and print `w`, `h` |
| F-7 | Frequency of RNG goal ties (§4.1) | counter at `0x81BC65` per plan |
| F-8 | Lot-hub branch details of mode 1 (start/goal sets, factor 30) | disassemble `0x674F6B..0x675100` and confirm with a probe on edge-byte-1 costs |
| F-9 | `0x6543B0` comparator for closest waypoints | decompile `0x653B70`/`0x6502E0`; static |
| F-10 | `RouteCost_Dispatch` polygon set content (job block `+0x8C`) | dump the set for stroller and normal routes |

## 8. Names added

Appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/99>`), no existing row changed: `WorldAStar_BuildPath 0x676130`, `WorldAStar_NewNode 0x6760D0`, `WorldAStar_CirclePrepass 0x673600`, `WorldAStar_HeapSiftUp 0x5EB920`, `WorldAStar_MakeHeap 0x673BF0`, `WorldAStar_ReleaseChain 0x6735D0`, `WaterLink_SlotFree 0x66F890`, `WorldAStar_ClosedListInit 0x673500`, `WorldAStarContainer_ctor 0x673C70`, `WorldPath_ToRecords 0x656550`, `PathNode_Init 0x6412F0`, `PathNodes_Append 0x62A300`, `PathNodes_Insert 0x629140`, `WorldRecord_NodeType 0x64FD10`, `Plan_ChooseStartGoalIndex 0x650950`, `GoalOrder_Sort 0x6542E0`, `GoalOrder_Greater 0x64FE80`, `PlannerWorld_LotExitMasks 0x64FEA0`, `WaypointRef_Equal 0x64FCE0`, `PlannerWorld_LotEdgeGroup 0x653200`, `WaterGrid_BodyValue 0x66D8D0`, `LotEdge_WaypointPosition 0x653020`, `LotEdgePortals_Gather 0x654CC0`, `LotEdgePortals_Choose 0x65BC20`, `LotEdgePortals_Sort 0x6564D0`, `LotEdgePortals_InsertionSort 0x650FD0`, `ClosestWaypoints_Try 0x6595B0`, `ClosestWaypoints_Find 0x65BB90`, `ClosestWaypoints_Params 0x652D80`, `LotTable_HubRef 0x631BB0`, `PlannerWorld_LotHubRef 0x650D40`, `RTData_Colour 0x650870`, `WaterLink_Body 0x66F540`, `Geom_PointSegmentDist2 0x7CD1A0`, `JobRng_Seed 0x4FA800`, `JobRng_Next 0x4FA840`, `RoutingWorld_SetWorldManager 0x829500`, `WaterLink_Find 0x66F8D0`, `LotPath_AddPortalNodes 0x62FBE0`.

## 10. Follow-ups (2026-10-18)

Second static pass on the open items of §7, same rules as above (no game, no probe, no native code). Tools: capstone 5 on `game-copy/bin/TS3W.exe` 1.67 (no Ghidra run was needed), and `tools/rtdata-decode.ts` with the new `--survey` switch on all 14 `.world` files of the Steam install. Tags as in the rest of this doc.

### 10.1 Cost modes 1-4 in full (F-8 closed)

Shared by all modes [verified]:

- Called only by the A\* `0x676260` through its mode table (`0x676CA0`); the A\* is called only by the planner `0x65E0D0` (`0x65F809`, `0x65FB99`, `0x65FDA5`) and by its own circle rerun (`0x6762B4`). Heuristic calls pass edge flag 0, edge byte 0, parent null; real edges pass the record's edge byte and the expanded node's parent; virtual edges pass byte 8.
- Wall = any cost `>= 1e8` (`0xFEF678`); NaN also fails the A\*'s test. The 5,000,000 hub cost (`0xFEF684`) is **not** a wall.
- Cost slots in the context (`opts+0x64..`): keep, trespass, sidewalk, riding, street, water-lot, then 1/sidewalk, 1/riding, 1/street, each a float32 division done only when the source is non-zero. Flag `0x40` (retry) puts FLT_MAX into trespass. The A\* copies LONG, computes h0, and keeps SHORT when `threshold >= h0` (`0x6762E0..0x6763AA`).
- Positions inside the cost functions are `(c mod w)*32 + bx*0.125`, `(c div w)*32 + bz*0.125`, **without the world origin**; the start and goal points in the context come from the job in world coordinates. The two frames only meet in the start/goal point substitutions, the virtual start/goal distances and the circle centre, so they agree only when the world origin is 0 [inferred risk; the origin per world needs a live read].
- Float order: `dx = from - to`, `s = dz² + dx²`, `d = sqrt(s)` in float32, `cost = d * k`, with `k` built by successive float32 multiplies in the order given below (`0x6753E1..0x67540C`).
- **No cost function reads the surface byte** (`+2` of a record); every `+2` read in them is the body id of a virtual ref.
- Lot-edge group `0x653200`: hash map at `W+0xA0` keyed `chunk<<16 | wp` (the reverse of the planner's u32 ref form), returns the group or -1. Start and goal group sets are EASTL int sets at `opts+0x18` and `opts+0x48` (corrects §2.3, which put them at `+0x14`/`+0x44`), filled by `LotSearch_Entry 0x640E00` (last argument 0 = start set at `0x65EB8E`, 1 = goal set at `0x65ECDE`), looked up with `0x526490`.
- `opts+0x14` and `opts+0x44` are single refs compared for equality; no writer was found in `0x65E0D0`, `0x652BF0`, `0x640E00`, `0x654CC0` or `0x65BC20` [inferred: stale stack values, so the equality is practically never true]. The planner sets only flag bits `0x10`, `0x20`, `0x40`, `0x80`, `0x100` and `0x200` (`0x65E865..0x65E8FB`, `0x65EBEB`, `0x65EE5D`); no store of bits 1, 2, 4 or 8 was found [inferred: the start/goal point substitutions and the factor-30 bypasses are dead for this caller].

**Mode 1, pedestrian `0x674E40`** [verified], in this order:

1. Either end virtual (chunk `0xFFFF`): the virtual rules below, also for heuristic calls (the test comes before the edge flag test).
2. Walls: flag `0x200` with a type-2 target; a type-`0x15` target without flag `0x80` or `0x100`; a blocked target unless both ends are type 2/`0x15` with the same first ref; edge byte 0.
3. `k = 10`. Jump table `0x6757EC` for bytes 1-5; bytes 6, 7 keep 10.
4. Byte 1 (hub):
   a. into the start hub (`opts+0x04`) with flag `0x10`: **return 5e6** at once (no distance, keep or circle step);
   b. into the goal hub (`opts+0x34`): with flag `0x20` return 5e6; else wall unless group(from) is in the goal set; then `k = 10` when flag 8 is set or from equals `opts+0x44`, else `k = 30` (`0xFBB86C`);
   c. into any other hub: `k = 10`;
   d. out of a hub with a parent: the group test compares the **parent's** waypoint with the target (corrects §2.6, which said from vs target): `k = trespass * 1000` across groups, else `trespass * 10`; the parent's record also replaces from for the type test and the in-chunk offsets (the quirk of §2.6, confirmed at `0x6752E2..0x675353`);
   e. out of a hub with no parent: wall unless group(to) is in the start set; then `k = 10` with flag 4 or to equal to `opts+0x14`, else `k = 30`.
5. Byte 2: `k = trespass * 1000` across groups, else `trespass * 10`.
6. Bytes 3-5: `k = (1/sidewalk) * 10`.
7. Types differ: `k = keep * k`. `k <= 0`: cost 0.
8. Tail shared with the heuristic (heuristic `k = 1`): flag 2 with the goal hub as target uses the goal point and `k = keep * k`; otherwise a type-2/`0x15` target must pass `0xC6BAB0`, else wall.
9. Circle: when `r² > 0` and the edge byte is not 3 (heuristic calls included), a target strictly outside the circle is a wall.
10. From position: flag 1 with the start hub uses the start point and `k = keep * k`; otherwise the from chunk with the offsets of the record in use.
11. Edge bytes 2 and 7 only: `RouteCost_Dispatch 0x627780` (this = P) with the two points at y 0 and the polygon set `opts+0x8C`; a hit is a wall.
12. `cost = d * k`.

Mode 1 virtual edges [verified, `0x675417..0x6757DF`]: wall without flag `0x80`; both ends virtual: 1 for the same body, else wall; G null: 100; from a virtual node: if it is the A\* start (`opts+0xA4`), `10 * |start point - target|`; else a type-`0x15` target returns the raw water-lot value (no distance); else `max(10 * bodyValue, 1)` (`0x66D8D0`). To a virtual node: the mirror with the goal ref (`+0xB4`) and goal point (`+0xB8`). No circle, keep or polygon step; a heuristic call from a virtual node returns a body cost, not a distance (corrects §2.6).

**Mode 2, car only `0x673850`** [verified]: any virtual end is a wall (heuristic too); an edge needs byte 3, else wall; `k = 1/street` if either end is type `0xD`, else `1/riding`, no x 10; heuristic `k = 1`; no blocked test, no point test, no substitutions, no keep factor; the circle test reaches only heuristic calls (every edge is byte 3, which skips it) and the polygon branch can never run. **The heuristic is not admissible** here: h costs 1 per metre while street edges cost 1/3 (SHORT) or 1/6 (LONG) per metre with the ini values [inferred from the arithmetic], so the car-only search behaves close to greedy best-first. A port must copy that, not fix it.

**Mode 3, pedestrian + car `0x674250`** [verified, normalised diff against mode 1, jump table `0x674E2C`]: as mode 1 except byte 3 gives `(1/street) * 10` if either end is `0xD`, else `(1/riding) * 10`; and the virtual edges add a lot water rule: for a type-`0x15` end the water-lot value is returned only when the lot at the goal point (or start point) exists, its `+0x4B0` is set, the waypoint belongs to that lot (`0x650030`) and the sea level (`0xC61670`, minus 0.75 with the boat flag) is above the terrain height (`0xC6A250`); otherwise the start/goal or body rules apply. Boat flag = `0x100`; the from-virtual branch also needs the AGS test `0x55DA60(job & 0xCF00)`, the to-virtual branch uses the raw bit.

**Mode 4, ignore sidewalk and lot restrictions `0x675800`** [verified]: walls as mode 1; bytes 3 and up give 10; byte 2 gives 10, or 1000 across groups, **without the trespass factor**, and on the retry flag the product with FLT_MAX makes it a wall; hub rules as mode 1 but case d gives 10/1000 (or infinity on retry), again without trespass; **no keep factor for differing types** (only the flag 1/2 substitutions multiply by keep, `0x6759AD`, `0x675C86`); virtual edges: from a virtual node with G, `bodyValue` without x 10 (0 gives 100); every other case 100; no water-lot or start/goal rules.

**Default `0x6734E0`**: edges are walls, heuristic 1. **Circle prepass `0x673600`**: start and goal waypoint positions (the context points for virtual ends), centre = (s + g) / 2, `r = 0.5 |s - g| + 48`, `opts+0xA0 = r²` when `r < 600`, else 0 [verified].

Not read: what `0x627780` and `0xC6BAB0` test inside (lot-side geometry, docs/91 §3.6).
### 10.2 Closest-waypoint order (F-9 closed)

`ClosestWaypoints_Try 0x6595B0` [verified, capstone]:

1. The cached search area (`0x652D80`, "FindClosestParams") holds the query point A at `+0x00/+0x04`, a direction point B at `+0x08/+0x0C` (B = A unless the caller passes a far end), option flags at `+0x2C` (low 3 bits = mode) and a vector of 32-byte candidates at `+0x30/+0x34`. The candidates come from `ClosestWaypoints_GatherChunk 0x654790`.
2. Candidate filter in `0x654790` [verified, `0x6549A7..0x654A95`]: blocked waypoints are skipped; type `0x11` is **always** skipped; with option `0x4000` only type `0x13` passes; pedestrian modes (mode & 5) skip types 5, 8, `0xB` and `0xE`; car-only (mode 2) keeps only types 5, 8, `0xB` and `0xD`.
3. Candidate record: `+0x00` d²(A), `+0x04` d²(B), `+0x08` sort key, `+0x0C/+0x10` x, z, `+0x14` u16 waypoint, `+0x16` u16 chunk, `+0x18` record pointer, `+0x1C` waypoint type.
4. First key: with option `0x200`, types 1, 2 and `0x15` get key `1e8` (`0xFED3D4`) and are not counted; every other candidate gets key = d²(A). In car-only mode the code walks the record's edge bytes looking for byte 3 but never uses the result (dead loop) [verified].
5. Sort 1: `0x6543B0` over the whole vector, **ascending by `+0x08`**. It is EASTL `sort`: introsort `0x653B70` (median of three of first, middle, last on `+0x08`, partition `0x652890`, heap sort `0x652900` when the depth budget `2*floor(log2 n) - 2` runs out) down to blocks of 28, then `0x6502E0` insertion sort on the first 28 and `0x650360` unguarded insertion on the rest. With 28 or fewer candidates it is a plain insertion sort with strict `>`, so **equal keys keep gather order** (chunk order, then record order); above 28 equal keys can be reordered.
6. Direction re-score, only when more than one candidate counted and `|B - A|² >= 0.01`: for the first `min(count, 6)` candidates (`0xFED3D0` = 6) whose d²(A) and d²(B) are both above 0.01 and whose type is not `0xD` or 7: key = `(1 - dot(u, v)) x d²(B) + d²(A)`, with u = unit(B - A) (x87 `1/sqrt`, then float) and v = unit(candidate - A) (`Vec2_Normalize 0x650F90`). Float order: `vz*uz + vx*ux`, then `1 - dot`, then `x d²(B)`, then `+ d²(A)`. Candidates skipped by the tests keep their d²(A) key.
7. Sort 2: the same `0x6543B0` over the first `min(count, 6)` entries only.
8. Pick: walk in order while key `< 1e8`, run the routability test `0x659390` (unless the caller's skip byte is set; with option `0x100` the skip byte comes from `0x658830`), append each passing ref to the output vector until the caller's limit. `ClosestWaypoints_Find 0x65BB90` calls this up to 3 times with the try index 0, 1, 2 and stops at the first success.

Water short cut [verified]: option `0x1000` with `0x2000` returns the virtual node `{0xFFFF, body}` when A lies in a water cell of G (cell byte not 0 and not `0x7F`).

For a replacement: the order is fully defined by float32 keys and the EASTL procedure. Ties at exactly equal d² are the only place where gather order matters, and with 28 or fewer candidates it is the gather order.

### 10.3 Node field readers (item 3; corrects §3 and I-99-10)

Scan of 101 functions: the 19 passes of `0x64E380`, the street pass `0x659BC0`, the splitter `0x81A340`, `RoutingPath_CopyNodes 0x819E10`, `0x650950`, and one level of their direct callees. Capstone, with every `[reg+off]` hit traced back to a 64-byte node pointer.

| field | readers | verdict |
|---|---|---|
| `+0x10` | none on a node; every `+0x10` hit is the path object's capacity, a list entry (`0x64C580`, `0x64C7C0`), the tuning object or a subpath record | no reader [verified negative, scope above] |
| `+0x14`, `+0x18` | written only (default node built inline from -100, -999, 1.0, `0xB828` in `0x644FD0` and `0x647EB0`) | no reader [verified negative] |
| `+0x28` | the copies in `0x6438D0`, `0x819E10` and `0x6578F0` read `+0x28` of 48-byte path nodes or street-run records, not 64-byte nodes | no reader [verified negative] |
| `+0x30`, `+0x32`, `+0x34` | no access at all | no reader [verified negative] |
| `+0x3C` | the only hit (`0x648409`) is y of the next node, through a pointer biased by -0x38 | no reader [verified negative] |
| **`+0x38`** | **9 functions**: `0x64DD40` (`== 9` with the neighbours' node types), `0x644630` and `0x642880` (level checks only when both nodes have `+0x38 == 0`, the lot-leg nodes), `0x645310` and `0x641B40` (`== 0x14`, virtual water node, with the neighbour `0x13`), `0x647EB0` (`0x14`/`0x15`, then non-zero), `0x643210` (non-zero = world node), `0x6418B0` (non-zero: level from the lot via `0xAB1F00`), `0x659BC0` (copied into its street-run record, runs of `0x0D` found) | **read by the post passes** [verified reads; roles inferred] |

So §3's "no reader" for `+0x38` is wrong: the leg class steers the post passes (street runs, water entry, level checks, lot-crossing check), although the splitter, CopyNodes and `0x650950` never read it. A replacement must fill `+0x38` exactly as §3 lists (world waypoint type, `0x14` for virtual water nodes, 16 for the start node, 13 on the car-start node, 20 on special lot joints, 0 on lot-leg nodes). The fields `+0x10`, `+0x14`, `+0x18`, `+0x28`, `+0x30..+0x36` and `+0x3C` can stay at the defaults within the scanned scope.

Flag word `+0x0E` (bit users; meanings still open) [verified accesses]: bit 0x01 written by `0x64C580`, read by CopyNodes (path flag `0x10` from the previous node); 0x02 read by CopyNodes (path flag `0x20`); 0x10 written by `0x642570`; 0x20 written by `0x641FC0` and `0x659BC0`, read by `0x64DD40`; 0x40 written and read by `0x643210` and `0x64D5B0`, read by the splitter; 0x80 and 0x100 read by the splitter; 0x200 read by `0x64A950` and the splitter; 0x400 written by `0x64A950`; 0x800 read by `0x64BBD0`, `0x64C7C0` and the splitter; 0x1000 read by `0x64C7C0`; 0x2000 read by `0x645310` and `0x64B430`. Masks `0x1180`, `0x900`, `0x600` and `0x1F80` are tested whole.

**The 19 passes** (`0x64E380`, in call order; gate G = byte `0x115824C`; without G only passes 16-19 run; the first pass that returns false fails the plan) [call list and gates verified, roles inferred from constants and gates]:

| # | address | gate | role |
|---|---|---|---|
| 1 | `0x644FD0` | G | inserts default nodes where consecutive nodes change lot; node type 10 |
| 2 | `0x641BF0` | G, not options `0xC0000280`, 4+ nodes | retypes an isolated equal pair of type 1, 10 or `0xB` to 0 [verified] |
| 3 | `0x64DD40` | G | lot-crossing / type-10 check with leg class 9; sets flag `0x20` |
| 4 | `0x6426B0` | G | position cleanup (constant 0.9, writes x) |
| 5 | `0x64C170` | G, options `0x8000` and `0x1000000` | sea level / water |
| 6 | `0x645310` | G | water legs and virtual nodes (`+0x38 == 0x14`, flag `0x2000`), types 6 and `0xB` |
| 7 | `0x642880` | G | level changes between lot-leg nodes |
| 8 | `0x643210` | G, option `0x800` | marks world nodes with flag `0x40` |
| 9 | `0x647EB0` | G | water entry for leg class `0x14`/`0x15` and the special lot kind; inserts nodes |
| 10 | `0x64B430` | G, `0x8000`, sea level | boat; kind `0xA582` |
| 11 | `0x64BBD0` | G, `0x8000` | boat; flag `0x800` |
| 12 | `0x648BE0` | G, `0x1000000`, sea level | wade / ocean |
| 13 | `0x6495E0` | G, `0x1000000` | rewrites the flag word (`0x1180` class) |
| 14 | `0x649890` | G, `0x1000000` | tuning `[obj+0x50]+0x14/+0x18`; kind `0xB0C2` |
| 15 | `0x64A950` | G, `0x8000000` | pond / frozen water: flags `& ~0x200 | 0x400` |
| 16 | `0x64C7C0` | always | smoothing / straightening ("Routing/TempPathListTH"); helper `0x64C580` sets flag `0x01` |
| 17 | `0x64D5B0` | option `0x800` | portal pass (type 3, flag `0x40`) |
| 18 | `0x64A690` | option `0x1000000` | portal entry (type 2, mask `0x1180`) |
| 19 | `0x641DF0` | always | node types 8, 9, 10 become 0 [verified] |

`0x659BC0` ("Routing/AnalyzeStreets", "StreetRuns") runs before the chain and needs 2+ nodes and a non-null `[[ecx+0xC]+0x5C]` [verified].

**F-4 (height of world nodes)**: no pass, CopyNodes or the splitter writes node y, and no code in the scope compares y with -100; -100 appears only as the default in the two inline node builders [verified negative]. So the -100 heights reach the 48-byte route nodes as they are, and the height is resolved later on the route side [inferred]. F-4 moves to the route consumers (docs/93 §6): a static read of the route-side height code or a live read of route node y settles it.

### 10.4 Every installed world (item 4; closes F-6)

Copied the 14 `.world` files of the Steam install (base, EP1-EP11; no SP or store world ships one, and `InstalledWorlds` in Documents is empty) into `game-copy/worlds/` and ran `node tools/rtdata-decode.ts <file> --survey` [verified, package data]:

| world | ver | chunks | waypoints | directed edges | components | type counts (hex:count) |
|---|---|---|---|---|---|---|
| Sunset Valley | 1 | 64x64 | 39,265 | 159,662 | 77 | 1:92 2:6648 7:275 d:924 e:12151 10:19167 11:8 |
| China (EP1) | 2 | 64x64 | 50,104 | 223,864 | 60 | 1:54 2:3286 7:2676 d:625 e:6870 10:36593 |
| Egypt (EP1) | 2 | 64x64 | 55,218 | 254,170 | 75 | 1:50 2:3646 7:1888 d:1126 e:8184 10:40288 11:36 |
| France (EP1) | 2 | 64x64 | 39,427 | 183,294 | 25 | 1:60 2:4073 7:22 d:927 e:5800 10:28002 11:543 |
| Twinbrook (EP2) | 2 | 64x64 | 46,686 | 210,814 | 121 | 1:83 2:5741 7:188 d:1909 e:7748 10:31013 11:4 |
| Bridgeport (EP3) | 2 | 64x64 | 35,313 | 157,482 | 50 | 1:82 2:6485 7:425 d:2325 e:6349 10:19647 |
| Appaloosa Plains (EP5) | 2 | 48x48 | 36,175 | 159,800 | 33 | 1:69 2:5407 7:984 d:1137 e:7716 10:20862 |
| Starlight Shores (EP6) | 2 | 64x64 | 25,588 | 119,534 | 12 | 1:73 2:5620 7:644 d:987 e:3652 10:14612 |
| Moonlight Falls (EP7) | 2 | 48x48 | 49,350 | 216,834 | 37 | 1:81 2:6055 7:451 d:1060 e:11523 10:30180 |
| Sims University (EP9) | 2 | 48x48 | 51,746 | 236,882 | 30 | 1:56 2:4642 7:1543 d:905 e:11528 10:32368 13:704 |
| Isla Paradiso (EP10) | 2 | 64x64 | 43,942 | 179,444 | 115 | 1:109 2:5406 7:868 d:1190 e:9716 10:21365 13:2865 15:2423 |
| Oasis Landing (EP11) | 2 | 48x48 | 64,638 | 278,624 | 38 | 1:65 2:5803 7:1760 d:1303 e:15287 10:39269 11:459 13:658 15:34 |
| GPE8_Future (test) | 2 | 64x64 | 6,344 | 29,230 | 1 | 1:31 2:2208 d:151 10:2195 11:1759 |
| SWE7UniversityTest (test) | 2 | 16x16 | 3,253 | 18,704 | 1 | 1:19 2:1490 d:194 10:1524 11:26 |

Common to all 14 [verified]: 0 refs out of range, 0 one-way edges (every edge has its reverse with the same byte), 0 blocked waypoints, flag `0x20` never set, every record has edge bytes, edge bytes only 1, 2, 3, 5, 6, 7, at most 382 waypoints per chunk (Oasis Landing; the 10-bit index allows 1,024), at most 121 components (well under the 254-flood limit of the signed colour byte).

- **No non-square world** [verified]: every shipped world is square (64x64, 48x48, 16x16), so the loader/builder `c div h` versus search `c div w` quirk (§1.2) never changes a result on shipped data. It stays a trap only for custom (CAW) worlds; our loader should reject `w != h` or follow each consumer's own rule.
- **Only Sunset Valley ships version 1**; the other 13 are version 2 and carry the surface byte. The stored bytes match the `0x66CB90` rule for types 1, 7 and `0xD` (`0x28`, `0x0A`, `0x1F`, 0 mismatches in 13 worlds), and **every other type stores 0**, not a terrain type. With the loader copying version-2 records as they are (§1.1), the in-memory surface byte of a terrain-typed waypoint is computed in Sunset Valley but 0 in every other world [verified file content; the copy rule verified in §1.1]. No cost function reads the byte (§10.1), so the difference has no routing effect found so far.
- **Edge byte pairing holds in every world** [verified]: 1 = hub ↔ lot edge (types 1 ↔ 2 or 1 ↔ `0x15`), 2 = lot edge ↔ lot edge of the same lot (2, `0x15`), 3 = `0xD` ↔ `0xD`, 5 = 7 ↔ 7, 6 = `0xE` ↔ `0xE`, 7 = everything else. Median lengths: byte 2 is 2.0 m in every world (lot-edge spacing), byte 1 is 20 m (hub to edge), byte 6 1.25-2.5 m, byte 3 4.3-9.6 m, byte 7 3-8 m.

**Type `0x0E` and edge byte 6 = world obstacle outlines** [geometry verified; use verified; name inferred]:

- Every type-`0x0E` waypoint has exactly two neighbours, both type `0x0E` with byte 6, and every group is a **closed ring** (Sunset Valley 3,032 rings, all closed; 911 to 3,241 per world). Median ring: 4 corners, 4-8 m perimeter, 1.6-5.6 m² area; the largest go to hundreds of corners and up to 24,800 m². No ring contains a lot hub.
- Each corner sits next to a walk waypoint (median 0.8-0.9 m to the nearest type `0x10`), the pattern of a visibility graph whose walk nodes sit just outside obstacle corners.
- Code: the flood skips them (§1.8); the closest-waypoint gather never returns them in pedestrian modes (`0x654790`); **`0x6580F0` builds a per-chunk cache (hash at `W+0xC0`) of the type-`0x0E` waypoints and their ring edges**, consumed by `Routing_PhysicsRayVertical 0x6583E0` (point test through `PolySet_PointWithinRadius 0x67B130`) and by `0x658A00` (segment crossing `0x7CDAA0`), the latter called from the routability test `0x659390` [verified calls at `0x658754`, `0x6591D4`].
- So R carries two graphs: the walk/street network and, as rings of type `0x0E`, the footprints of fixed world objects (trees, rocks, props, small buildings). The A\* never reaches a ring (no edge joins one to the walk network), but the closest-waypoint step uses the rings to reject candidates whose straight segment from the query point crosses an obstacle. **A replacement must keep the rings** for the closest-waypoint test even though its A\* never visits them.

**Type `0x11` = pass-through walk waypoints** [counts and code verified; name inferred]: every type-`0x11` waypoint has degree 2 with edge byte 7, linked to type `0x10`, 2, `0xD` or other `0x11` waypoints; they form open chains (single nodes in Sunset Valley, Egypt and Twinbrook; chains of up to 31 nodes and 1.7 km in GPE8_Future, France and Oasis Landing, mostly far from streets and lots, e.g. France's 543 lie at x 1,309-1,988 m). They are flooded with the walk network and the A\* expands them like type `0x10`, but `0x654790` **never offers them as a closest waypoint** (unconditional skip at `0x6549C0`), so a route can pass through them and never starts or ends on one. The keep-type factor applies on every edge between a `0x11` and a different type (§2.6 step 3).

**Types `0x13` and `0x15` (water) now have data** [verified]: Isla Paradiso (2,865 / 2,423), Oasis Landing (658 / 34), Sims University (704 / 0). Type `0x15` is a lot-edge type of water lots: byte 1 to the lot hub, byte 2 to other edge waypoints of the same lot, exactly like type 2. Type `0x13` sits in the walk network (byte 7, median 2-5 m from a type `0x10`) and is the waypoint that owns water links in G's map (§2.4). The other worlds have neither type, so water links in the world A\* only occur in those three worlds.

### 10.5 `job+0x38` / `job+0x3C` (item 5; docs/93 §4.3 is wrong)

[verified, capstone] `RoutingJob_Execute` passes `[job+0x38]` and `[job+0x3C]` as arguments 7 and 8 of `PathPlan_WithRetries 0x661B50` (`0x81BF3B..0x81BF48`). `0x661B50` forwards its 13 arguments in the same positions to `PathPlanner_PlanHierarchical 0x65E0D0` (`0x661B5C..0x661B93`, the same for the retry call). All six calls of `Plan_ChooseStartGoalIndex 0x650950` (`0x65E4D5`, `0x65E59D`, `0x66012F`, `0x660348`, `0x66194B`, `0x661ACB`) pass the planner's argument 7 (`[ebp+0x20]`) as their second argument and argument 8 (`[ebp+0x24]`) as their fourth. `0x650950` stores its first argument (the start index from the start lot leg, or 0) through the second and the chosen goal record index through the fourth.

Before planning, Execute stores the goal index from the priority/distance/RNG choice through `[job+0x3C]` (`0x81BCA7`), and reads it back after a success at `0x81C20C`. So **`job+0x38` = start index, `job+0x3C` = goal index**, as §0 says and as docs/93 §2 (start hash from arg 7, goal from arg 8) implies. docs/93 §4.3 has the two the other way round; this doc does not edit docs/93, the ledger row I-99-15 records the correction.

### 10.6 What is closed and what still needs a live value

Closed statically: F-5 (types `0x0E`, `0x11`, edge byte 6), F-6 (no non-square world), F-8 (lot-hub rules), F-9 (closest-waypoint order), the cost rules of modes 2-4, the node-reader question for the 64-byte node (I-99-10, corrected for `+0x38`), the `+0x38`/`+0x3C` disagreement.

Still live (values, not logic): F-1 (ini tuning replaces the defaults), F-2 (memory R equals the decoder's records), F-3 (meaning of the `+0x0E` bits), F-4 (now a route-side question: where world-node y = -100 is resolved), F-7 (RNG tie frequency), F-10 (polygon-set content), and three new ones from §10.1: F-11 the values of `opts+0x14`/`+0x44` at A\* entry and whether context bits 1, 2, 4 or 8 are ever set (a probe at `0x676260` entry), F-12 the world origin (`WM+0xB4/+0xB8`) per world (non-zero means the cost functions mix frames), F-13 a probe of edge-byte-1 and byte-2 costs against §10.1.

## 11. F-3, F-4 and F-13 closed offline (2026-10-18)

The value probe (docs/108 §4) skipped these three because they need node or edge dumps or route-side reads. This pass runs the game's own functions offline under the emulator harness (docs/63) on decoded world data and on built node arrays; no game run, no ts3native change. Tags as in the rest of this doc; **[verified, emu]** = the exe's own bytes produced the stated output in `tools/emu` for every listed case.

Tools added:

- `tools/rtdata-decode.ts`: `--dump nodes|edges` (CSV to stdout: per waypoint chunk, index, type, flags, blocked, x, z, edge count, first ref; per directed edge both ends, edge byte, both types, length in metres, record order kept) and `--mem-image <file>` (the loader's in-memory form: offset table + version-2 records, the input of the cost spec). Both are read-only on the package; write their output outside the repo. `--self-check` now also round-trips the memory image and checks the dumps.
- `tools/emu/emu.py`: the CFG follows MSVC jump tables (`cmp idx, n` ... `jmp [idx*4 + table]`, n + 1 entries), so functions with a `switch` (mode 1's edge-byte table `0x6757EC`) can run under the strict policy. The 8 self-check specs still pass.
- Three specs (not in `--self-check`, they need world data or are characterisations): `world_cost_mode1.py` (F-13), `route_copy_nodes.py` (F-4), `subpath_split.py` (F-3).

<!-- docs-code: ok -->
```sh
node tools/rtdata-decode.ts --self-check
node tools/rtdata-decode.ts "<world>.world" --dump edges > <scratch>/edges.csv      # or --dump nodes, --mem-image <scratch>/r.bin
TS3W_EXE=<main checkout>/game-copy/bin/TS3W.exe python tools/emu/run.py --cases 3000 tools/emu/specs/world_cost_mode1.py
TS3_WORLD="<Steam>/The Sims 3/EP10/GameData/Shared/NonPackaged/Worlds/IslaParadiso.world" python tools/emu/run.py --cases 3000 tools/emu/specs/world_cost_mode1.py
python tools/emu/run.py tools/emu/specs/route_copy_nodes.py
python tools/emu/run.py --cases 1000 tools/emu/specs/subpath_split.py
```

### 11.1 F-3: what the node flags `+0x0E` and node class `+0x38` mean to the splitter (closed)

`RoutingJob_SplitSubPaths 0x81A340` ran on 8-node pedestrian routes (start type 6, inner type 0, goal type 7) with one inner run (nodes 3-4) changed by a flag bit, a node type, a kind hash or a route option; route options sit at `job+0x40` (`RouteOption` at `+0x28`, `RouteOption2` at `+0x30`, both 64-bit, read through `RoutingOptions_Test 0x81C880`; names from SimIFace `Route.cs`). Sweeps: every flag bit alone under each of the 14 option bits the splitter tests at entry (and none), on node types 0, 1, 3, 10, 11 and kinds `0xB828`, `0x3B83`, `0x347C`, `0xA582`, `0xB0C2`, then 6,000 random combinations of options, flag masks, types and kinds [verified, emu]. The pinned result is the table in `subpath_split.py` (31 routes) plus 1,000 random routes that add the inert bits below and any `+0x38` value: 1,031 of 1,031 pass, 182/240 splitter blocks (the rest: portal object lookups, stubbed to "not found", vector growth, real portal pairs).

| node run | route option needed | subpath type of the run |
|---|---|---|
| flag `0x80` | `EnableWaterPlanning` (bit 60) or `RouteOption2.EnablePlanningAsBoat` (`0x2`) | WadePath |
| flag `0x100` | `EnableWaterPlanning` only (not the boat option) | OceanPath |
| flag `0x200` | `EnablePondPlanning` (bit 61) | FrozenPondPath |
| flag `0x800` and kind `0xA582` | `EnablePlanningAsBoat` (not water planning); `BeginAsBoat` (`0x8`) starts the route with it | BoatPath |
| flag `0x1000` or `0x2000` on type-1 car nodes (kind `0x3B83`) | `EnablePlanningAsCar` (`0x10`) and `EnableUFOPlanning` (bit 59); without the UFO option the run stays VehiclePath | UFOPath |
| flag `0x40` | `PlanUsingStroller` (bit 39) | SimPath inside a stroller route: the run is walked without the stroller |
| type 1, kind `0x3B83` or `0x347C` | `EnablePlanningAsCar`; `DisablePlanningAsPedestrian` (`0x20`) or `BeginAsCar` (`0x40`) start the route as a car | VehiclePath |
| type 11 | `RouteWhileMounted` (bit 46) | HorseLanePath |
| type 2 (kind `0xB828` or `0xB0C2`) | none | portal records: here PortalPath, PortalPath, PortalTraversalPath (two lone entry nodes, portal object lookup stubbed to "not found"; real entry/exit pairs were not built) |
| types 1, 5, 8, 9, 11 (not 10) | `PlanUsingStroller` | SimPath run inside the stroller route |

- **No option, no split**: with no route option set, no flag bit and no type except 2 changes the plain route's single SimPath [verified, emu].
- **Inert for the splitter**: flag bits `0x01`, `0x02`, `0x04`, `0x08`, `0x10`, `0x20`, `0x400`, `0x4000`, `0x8000` and every value of `+0x38` (0, `0xD`, `0x10`, `0x13`, `0x14`, `0x15`, 9, 13, 16, 20 tested) never changed a split [verified, emu, within the tested space]. Their users are the post passes and CopyNodes (§10.3; bits 0 and 1 below).
- `BeginAsCar` / `BeginAsBoat` erase the walking nodes before the first car/boat node (`PathNodes_Erase 0x7AA5C0`), so the route starts in the vehicle [verified, emu].
- The subpath record's `+0x00` points at the subpath's **last** node; neighbouring subpaths share the joint node [verified, emu].
- Meanings [inferred from option names and the post-pass writers in §10.3]: `0x80` wade (shallow water on foot), `0x100` ocean swim, `0x200` frozen pond (pass 15 `0x64A950` reads it and turns it into `0x400` under its option gate), `0x800` boat leg (read by pass 11 `0x64BBD0`), `0x1000`/`0x2000` the UFO leg of a car route, `0x40` "no stroller here" (written by pass 8 `0x643210` on world nodes and pass 17 on portals). `+0x38` (leg class) steers only the post passes (§10.3).
- CopyNodes reads bit 0 and bit 1 of the **same** node (route node flags `0x10` / `0x20`), not the previous node as §10.3 says [verified, emu: `route_copy_nodes.py`].

This also answers the offline half of ledger I-93-6 for these inputs: the splitter's `PathType` choice is fully determined by node type, kind, the bits above and the options.

### 11.2 F-4: where world-node y = -100 is resolved (closed for the route build; consumer use is F-4b)

- **Every planner node carries y = -100, not only world nodes** [verified, disassembly]: the world records (`0x656550`), the lot-path writer `LotPath_ToWorldAndEdgeFlags 0x62F140` (stores x, -100, z per node) and the node builder at `0x62A662` (x, -100, z, level = low nibble - 4) all use the constant. Only the start and goal nodes get a terrain height (§3).
- **Nothing between the planner and the route node resolves it**: no post pass, the splitter or CopyNodes writes y (§10.3, re-checked: pass 16 `0x64C7C0` samples terrain height `0xC6A250` per node into its own temporary list "TempPathListTH" and its body writes no node field) [verified, disassembly; pass 16's callees were not re-read].
- **CopyNodes copies y as it is and uses it** [verified, emu: 303 node arrays, 16/19 blocks, the 3 missed are the null-world exit, the same-point helper `0x819680` and vector growth]: each 48-byte route node is `{x, y, z, x}`, the unit direction to the next node and the segment length are computed over **x, y and z**, `+0x24` is the running length, and the path total goes to vtable slot `+0x130`. A segment from a start node at terrain height h to the first planner node at -100 therefore gets a length of at least `100 + h` metres and a direction pointing steeply down.
- So y = -100 is never resolved on the build side; route nodes keep it. Whether a consumer uses the 3D length or direction (follow progress, `Route_UpdateLengths 0x750260`, walk-or-run thresholds, docs/93 §6) is the remaining question, now **F-4b**: read a planned route's first two 48-byte nodes (y, `+0x20`) and the path length against the 2D sum. Two outcomes: lengths include about 2 x (100 + h) m (a real inflation every consumer sees) or the start/goal heights are also -100 at that point (then §3's "terrain height for start/goal" is wrong) [inferred; the static reading of §3 favours the first].

### 11.3 F-13: edge-byte-1 and byte-2 costs against §10.1 (closed)

`RouteCost_Mode1 0x674E40` ran on real edges of three worlds, R built in emulator memory exactly as the loader keeps it (`--mem-image`), against a model written from §10.1 as text (`world_cost_mode1.py`). Each world: 3,010 cases, about 40% byte-1 and 30% byte-2 edges, the rest bytes 3, 5, 6, 7 and heuristic calls; random context flags (bits 1, 2, 4, 8, `0x10`, `0x20`, `0x80`, `0x100`, `0x200`), hubs, lot-edge groups and group sets, parents, blocked targets, type-`0x15` targets, circle, tuning values and point/polygon test answers.

| world | waypoints | byte-1 / byte-2 edges | cases | result |
|---|---|---|---|---|
| Sunset Valley (`game-copy/worlds`) | 39,265 | 13,296 / 13,296 | 3,010 | 3,010 pass |
| Bridgeport (read in place) | 35,313 | 12,970 / 12,970 | 3,010 | 3,010 pass |
| Isla Paradiso (read in place) | 43,942 | 15,658 / 15,658 | 3,010 | 3,010 pass |

[verified, emu] Every real-end block of the function ran (74/110; not reached: the 32 blocks of the virtual water branch from `0x675417`, which need G, and four unreachable unsigned fix-ups after `fild` of a chunk coordinate). Bit-exact float32 results once the tuning values are float32. This confirms §10.1 steps 1-12 for real ends, including: the hub-out group test against the **parent**, the parent's in-chunk offsets with the current chunk, the 5,000,000 return before any distance or circle step, the factor 30 and its flag 4/8 and `+0x14`/`+0x44` bypasses, `k <= 0` returning 0, the type-2/`0x15` point test only with a world manager, and the circle skipped for byte 3 only. What stays live is values, not logic: the real lot-edge groups (`W+0xA0`), the tuning (F-1) and the bits 1, 2, 4, 8 (F-11).

### 11.4 World files

The main checkout's `game-copy/worlds/` now holds only `Sunset Valley.world` (the 13 others of §10.4 are no longer there). The specs read any world in place through `TS3_WORLD`, so nothing has to be copied for this pass. For the default path and the `--survey` rerun of §10.4, Rodney can copy these (never into git; `game-copy/` is ignored), all under `<Steam library>\steamapps\common\The Sims 3\`:

- `EP3\GameData\Shared\NonPackaged\Worlds\Bridgeport.world`
- `EP10\GameData\Shared\NonPackaged\Worlds\IslaParadiso.world`
- the rest of §10.4: `EP1\...\Worlds\China.world`, `Egypt.world`, `France.world`; `EP2\...\Twinbrook.world`; `EP5\...\AppaloosaPlains.world`; `EP6\...\Starlight Shores.world`; `EP7\...\Moonlight Falls.world`; `EP9\...\Sims University.world`; `EP11\...\Oasis Landing.world`; `GameData\Shared\NonPackaged\Worlds\GPE8_Future.world` and `SWE7UniversityTest.world`.

### 11.5 Status of §7 after this pass

Closed: F-3 (splitter meaning of `+0x0E` and `+0x38`), F-13 (edge-byte-1/2 costs), F-4 as asked (no pass resolves y; the route keeps -100). New: F-4b (does a consumer use the 3D length or direction; live read in §11.2). Still live: F-1, F-2, F-7, F-11, F-12; F-10 is answered in docs/107 §3. Ledger: docs/51 §51 (I-99-28 to I-99-33). Names: `PathNodes_Erase 0x7AA5C0`, `SubPaths_PushBack 0x81A0E0`, `IntSet_Find 0x526490`, `RoutePathNodes_Reserve 0x819B60`, appended to the symbols CSV.

## 12. F-4b: who reads the 3D lengths and directions CopyNodes writes (2026-10-18, static + emulator)

Question (§11.2): CopyNodes builds route-node direction `+0x10`, segment length `+0x20`, running length `+0x24` and the path total (`+0x8C`, via vtable `+0x130`) over x, y and z, while planner nodes carry y = -100. Who reads these, is the read 3D, and could about 100 m extra on a segment change a decision? Static reading of the 1.67 image (capstone) plus a new emulator spec; no game run, no native change. Tags as in the rest of this doc.

### 12.1 Verdict

**F-4b is not closed offline; it is narrowed to one live read (§12.6).** Every reader found uses the stored 3D lengths or recomputes in 3D, and none rewrites node y except one last-node case (§12.2) [verified]. What that means depends on the y values a followed path really carries, and the static chain gives a contradiction (§12.4):

- **All nodes at one height** (every node -100, or every node at its real height): lengths, projections and look-ahead points equal the 2D answers; F-4b is harmless [verified, emu].
- **A real-height node next to -100 nodes** (start/goal with terrain height as §3 says, or the Sim position the follower prepends at route start, §12.3): the segment is at least `100 + h` m long, the follower's projected progress on it is about 6% of the real progress, its look-ahead point lies behind the Sim, and the path length is several times the 2D length [verified, emu, for the path functions; the follower's use of them is inferred].
- The second case would make every route start misbehave and every path look long to the walk-style rules, which the game does not visibly do [inferred]. So one static link does not hold live; which one needs the read in §12.6.

### 12.2 Readers [verified unless marked]

`RoutingPathImp` vtable `0x10214D8`; nodes 48 bytes at `path+0x98..+0x9C` as in §11.2.

| # | Reader | Reads | 3D? | Decision it feeds |
|---|---|---|---|---|
| 1 | Path slot `+0x44` `0x81EEB0` (running lengths) | sums stored `+0x20` into each `+0x24` and `path+0x8C`; resets the cursor block | inherits CopyNodes' 3D lengths (no recompute) | every distance-along-path below |
| 2 | Path slot `+0x40` `0x820830` (recompute segments) | recomputes `+0x10`/`+0x20` from node positions over x, y, z (`sqrtps` of the 3-lane sum), then `+0x24`, `+0x8C` | 3D | same; for PathType hash `0xE99B65D5` only (`path+0xB0`), the last segment's end is moved to terrain height (`0xC6A250` with the world manager): the **only y write on the route side** found, last node only |
| 3 | Path slot `+0x100` `0x821E90` (prepend a point) | squared 3D distance of the point to node 0 and to every segment (`0x81D800`), guard 0.00145 m² (`0x1021704`); builds a new first node {point, 3D unit direction, 3D length}, copies the rest (`0x81F820`), slot `+0x44`, node flags (`0x81D680`) | 3D | called by the follower (§12.3) |
| 4 | Path slot `+0x118` `0x821820` -> `0x821120` (project a point) | closest segment by **3D** distance (`0x81D800`: clamped projection over x, y, z), local search from cursor `+0x108`, then a global split search when the local best is more than 1 cm away (`1e-4` m² at `0xF988E8`); result = running length `+0x24` + t | 3D [verified, emu] | follower progress (`0x852700` at `0x852B5B`, `0x852BEE`), steering B (docs/107 §12.2) |
| 5 | Path slot `+0x120` `0x821850` -> `0x821120` | same projection | 3D | `Route_UpdateLengths 0x750260`: route `+0xA8` (distance remaining) = route length (`0x74FEF0`) minus the projection of the Sim's transform position (`0x935770` + `0x959000`) |
| 6 | Path slot `+0x11C` `0x81D9F0` (point at distance) | running lengths `+0x24`/`+0x20`; point = node + (d - run) x direction | 3D [verified, emu] | follower and steering look-ahead (docs/107 §4.2) |
| 7 | Path slot `+0x12C` `0x641360` | `path+0x8C` | inherits 3D | follower: path shorter than 0.15 m (`0x106B4B4`, at `0x852A51`) takes the short-path branch; steering B; `IC_Route_GetPathLength 0x751A20` |
| 8 | `RouteFollow_A 0x846B70` | its point argument | **y replaced** by ground height (`0x829470` -> `0xC6A540` or `0xC6A250`, at `0x846C0D`) before use | steering is y-insensitive for that point |
| 9 | End facing in `RoutingJob_Execute` (`0x81C2A9..0x81C3B7`) | last node minus the job's end point, **y zeroed** before normalising, then path slot `+0x144` | 2D | end orientation |
| 10 | Script ICs: `Route_GetPathLength 0x751A20` (path `+0x12C`), `Route_GetRouteLength 0x751AE0` (`0x74FEF0`), `Route_GetDistanceRemaining 0x74CF70` (route `+0xA8`), `Route_GetPathDistanceRemaining 0x751830`, `Route_GetPointAtDistanceOnRoute/Pathway 0x751CC0/0x751BE0`, segment start/end point and direction ICs, `RouteEvent_GetRouteEventDistanceRemaining/Travelled 0x74DE00/0x74DDC0`, `Route_GetEstimatedTravelTime 0x7656D0` (`docs/data/ic-safety.csv`) | the values of rows 1-7 | inherit 3D [verified slot reads for `0x751A20` and `0x750260`; the others inferred from the CSV reads] | managed decisions below |

Managed users that turn these lengths into a decision (`node tools/managed-callgraph.ts callers ...`) [verified callers; thresholds not read]:

- Walk style (walk, jog or run): `SimWalkStyleRules.GetAdjustedWalkStyleForRoute`, `BoatWalkStyleRules.GetAdjustedWalkStyleForRoute`, `SimScriptAdaptor.OnAboutToStartFollowingRoute` (all `GetPathLength`).
- Route action per subpath and replans: `RoutingComponent.GetRouteActionForSubPath`, `SimRoutingComponent.DoSingleRoute` / `CheckAndUpdateRouteForStations`, `Lot.TryTeleportAndReplan`, `Sim.PushCanePostureIfNecessary` / `PushBackpackPostureIfNecessary`, `CommonDoor.GetRouteThroughDoor`, `Stairs.RouteUpOrDownStairs`, `ToddlerWander.Run` (all `GetDistanceRemaining`).
- Placement and vehicles: `GlobalFunctions.FindGoodLocationAlongRoute`, `Vehicle.OnRouteFinished`, `Terrain.ModifyRouteForSpecies`, `SocialTest.GetClosestAvailableBoxStallForWooHoo` (`GetRouteLength`); chases (`GetEstimatedTravelTime`).

Not found [verified negative within the scan]: no route-side function stores a height getter's result into a node (every call of `0xC6A250`, `0xC6A540` and `0x829470` in `0x740000..0x770000`, `0x81D000..0x830000`, `0x840000..0x870000` and `0x930000..0x960000` stores to the stack); no code in those ranges loads the -100 constant (the only users are the planner builders `0x62A470`, `0x62F140`, `0x6412F0`, `0x644FD0`, `0x647EB0`, `0x64DC70`, `0x6552D0`, `0x656550`, `0x656A70`, and three unrelated sites). So nothing treats -100 as "height unknown" on the route side.

### 12.3 The follower prepends the Sim's position at route start [verified call sites; condition meaning inferred]

`0x852700` reads the agent position through agent slot `+0x1C` (`0x954380`: component lookup `0x935770`, then the transform `0x959000`, so the object's world position) at `0x85289C`. When path slot `+0xA4` returns 0 for it (`|x| <= 1.19e-7`, at `0x852AD5..0x852B05`; also per group member in the loop at `0x852977`), it calls path slot `+0x100` with that position (`0x852B1F`, `0x8529F3`). Row 3's guard is 3D: a position 100 m or more above every node is never "on the path", so on a path whose nodes are all -100 the prepend always runs and creates a first segment from the Sim's real height down to -100 [verified guard; that it fires at every route start is inferred from the slot `+0xA4` value at start].

### 12.4 Emulator result

`tools/emu/specs/route_follow_geometry.py` runs path slots `+0x118` (with `0x821120`, `0x81D800` and the global search `0x81D8D0`/`0x8210C0`) and `+0x11C` on nodes built as CopyNodes builds them, with a Sim on the path's xz track at height h (0-40 m), q metres along the first segment, and a look-ahead of 0.5 m. The model is the 3D rule of rows 4 and 6, written as text. 300 cases pass (100 per layout, `--cases 300`); stubbed only the allocator pair `0x4E3F70`/`0x4E3FB0`.

| Layout | Projected progress / real progress q | Path total / 2D length | Look-ahead point | Search taken |
|---|---|---|---|---|
| real: every node at h | 1.000 | 1.00 | 0.5 m ahead | local |
| flat: every node at -100 | 1.000 | 1.00 | 0.5 m ahead (at y = -100) | global split search, every call (the local best is never within 1 cm) |
| prepended: first node at h, the rest -100 | median 0.058, min 0.009 | median 6.4, max 72.8 | **behind the Sim in 100 of 100 cases** (median 2.9 m from it) | global split search |

[verified, emu] The flat layout gives exact 2D answers because a constant y offset changes neither the closest horizontal segment nor t. Its only cost is the fallback search on every projection call (a few segments of extra work per Sim per tick on long paths) [cost size inferred].

The contradiction: the build side (§11.2) plus row 3 predict the "prepended" layout at the start of every searched route, with a look-ahead behind the Sim and a path length that is many times the walk distance (so the walk-style rules would see long routes everywhere). Normal play does not show Sims turning back at route starts [inferred, not measured]. Candidates, none settled statically: (a) the agent position fed to rows 3 and 4 is not at ground height; (b) a y writer outside every scope scanned; (c) slot `+0xA4` is not 0 at follow start, so the prepend is rare; (d) the lot planner's paths are flat and start/goal keep -100 too (`LotPath_ToWorldAndEdgeFlags 0x62F140` writes -100 to every node of the lot path vector, the first and last included, `0x62F1E0..0x62F331`), and only the direct start-to-goal branches of `0x65E0D0` (`0x65E669..0x65E71F`, `0x65FFB6..0x6600A0`, `0x660222..0x660319`) store real start/goal heights (from `0xC6A250`/`0xC6A540`) [verified writes; which branch a typical route takes is inferred], which would make §3's "terrain height for start/goal" a property of 2-node routes only. Even (d) leaves the prepend of (c).

### 12.5 What it means for the replacement contract

- **Copy y exactly as the game builds it; never "fix" it.** Every consumer (follow progress, look-ahead, steering B, distance remaining, the managed walk-style, route-action and placement rules) reads 3D values. Inner nodes stay at -100 and start/goal keep whatever the branch sets, and the game's CopyNodes computes `+0x10`/`+0x20`/`+0x24`/`+0x8C`. Snapping inner nodes to terrain would turn today's 2D-equivalent behaviour into real slopes (other lengths, other walk styles, other look-ahead) and is a separate, A/B-tested change, not part of a like-for-like replacement. This confirms docs/93 §4.4's caveat with a reader list.
- **The shadow compare (docs/93 §8, §9) must compare node y and `+0x20`,** not only xz and node count, because a y difference changes every reader in §12.2.
- If the live read shows the prepended layout on real routes, that is an existing game defect (route-start look-ahead and inflated lengths), worth its own item; it is not ours to reproduce differently.
- The flat-path fallback search (§12.4) is sim-thread work per follow tick that exists today; a replacement neither adds nor removes it as long as y is copied.

### 12.6 Live read still needed (exact)

Frida, no native build; neither address is a hook site in `native/`, `tools/probe/` or `tools/frida/`. Clean load of a lot with autonomy off, then one in-lot route and one route across the street about 2 minutes.

1. `RoutingPath_CopyNodes 0x819E10`, thiscall, `ret 0x20`: on enter keep stack argument 7 (the path); on leave log the node count, then for nodes 0, 1, n-2 and n-1: y (`+0x04`) and `+0x20`; `path+0x8C`; and the 2D sum of segment lengths.
2. Path slot `+0x100` `0x821E90`, thiscall(path)(point*), `ret 4`: on enter log the point's y, node 0's y and `path+0x8C`; on leave the node count change (+1 = prepended; the return value is not a flag), node 0 and node 1 y and `+0x20`, and `path+0x8C`.
3. The same agent's transform y (the point of item 2) against terrain height at its xz, once per route.

| Outcome | Meaning |
|---|---|
| Item 1: every y -100 on searched routes; item 2 never adds a node | flat paths, F-4b harmless; §3 corrected (start/goal heights only on direct routes) |
| Item 1 flat; item 2 prepends with a real y and `+0x20` >= 100 m | the prepended layout is live: route-start look-ahead and length inflation are real game behaviour; measure walk-style effects next |
| Item 1: start/goal at real height next to -100 nodes | §3 holds; the inflation starts at build time |
| Item 1: inner nodes at real heights | a y writer exists outside the scan (§12.2 negative wrong) |

Ledger: docs/51 §60 (I-99-34 to I-99-39). I-99-33 stays open; its live read is item 1 here.

### 12.7 F-4b live read: probe and plan (2026-10-18, built, not run)

Built for §12.6; nothing here has run in the game. Ledger: docs/51 §64 (I-99-40 to I-99-43).

**Probe** `tools/frida/f4b-reads.js` (read-only; writes no game memory; prologue bytes checked, an existing `jmp rel32` is chained and listed, anything else refused; every list bounded; fail-fast when neither hook fires for 2 checks of 30 s; it detaches its hooks itself 3 s before the window ends):

- Item 1: `0x819E10` (`55 8B EC 83 E4 F0 81 EC`; stack argument 7 = `[ebp+0x20]` = the path [verified, disassembly: `mov edi,[ebp+0x20]` at `0x819E45`, `ret 0x20` at `0x819E38`]). On leave, when `al` is 1: node count, y and `+0x20` (and `+0x24`) of nodes 0, 1, n-2, n-1, `path+0x8C`, and over all nodes the 2D sum, the 3D sum and the sum of `+0x20`. Each path gets a layout: `flat` (every y -100), `real` (no y -100), `ends` (real y only at the first or last node) or `inner` (an inner node at a real height); "searched" = more than 2 nodes, "direct" = 2 nodes.
- Item 2: `0x821E90` (`55 8B EC 83 E4 F0 83 EC`; `ecx` = path, stack argument 1 = the point, `ret 4`). On enter: the point, node count, node 0 y, `path+0x8C`, return address; on leave: node count change, node 0 and node 1 y and `+0x20`, `path+0x8C`.
- Item 3: the point of item 2 is the agent's transform position (stored by agent slot `+0x1C` at `0x85289F`, passed as `[esp+0x30]` at `0x852B1C`; the group loop passes a member's position at `0x8529EC`) [verified, disassembly]. Its terrain height comes from **`World_GetTerrainHeightImpl 0x786200`**, the script `World.GetTerrainHeight` IC: cdecl(x, z, flag), plain `ret`, result in st0; it loads the world manager from `0x11ECBC4` and calls `World_GetHeightAt 0xC6A250` (9 stack arguments, `ret 0x24`; the flag lands in its byte argument at `[ebp+0x18]`) [verified, disassembly]. The probe passes flag 0 [meaning inferred] and calls it only inside item 2's hook when the return address is one of the follower's two call sites (`0x8529F5`, `0x852B21`), so on the sim thread inside `RouteFollow 0x852700`, the same thread scripts call it from; the lot-manager lock it reaches (`+0x228`, docs/18) is taken after `+0x918` as in the game's own lock order. At most 200 calls. Why this read and not another: a direct heightmap read would need the cell shapes and lot floors (`0xC2A570`, `0xAB8EE0`), and the steering ground read (`0x829470`) is for a look-ahead point, not the agent. Frida 17 returns garbage for an x87 `float` return on ia32 (checked on the dummy), so the call goes through a 3-instruction thunk of ours that moves st0 to eax.

**Dummy test** `tools/probe/f4b-reads-test.py` (32-bit ping.exe, never the game): CopyNodes (8 arguments, `ret 0x20`) and slot `+0x100` (`ret 4`, grows the node vector by one node) stubs on two threads with flat, direct and "ends" paths; checks refusal, chaining, every read, the terrain call only from the follower return address (thunk result = the stub's st0), fail-fast at 60 s, clean unhook and the report's outcome line. **PASS** 2026-10-18.

**Report** `node tools/harness/f4b-report.ts probe-out/profile/<tag>-f4b.json` prints the §12.6 outcome row and the I-99-33 / I-99-37 / I-99-39 / item-3 verdicts; `--self-check` (the probe's path reader on fake memory plus every outcome) passes. Rules, checked in this order over searched paths:

| Row | Rule |
|---|---|
| D | any searched path `inner` or `real` |
| C | else any searched path `ends` |
| B | else (every searched path `flat`) a prepend (node count +1) with a point y other than -100 and node 0 `+0x20` >= 100 m |
| A | else no prepend at all (prepends without that layout print "no table row, read the prepend rows") |
| item 3 | every reading within 0.5 m of the terrain = §12.4 candidate (a) refuted; readings further off support it |

**Jobs** `tools/harness/queues/f4b.json` (Baseline-BP-Spring, asi `tested`, `RouteWorker`/`RouteAsync` off as in the live-reads jobs, seed 12345, 3-minute profile window with `--f4b`, about 6 minutes per job with load and teardown, about 12 minutes for the queue):

- `bp-f4b-inlot`: repro `f4b-inlot-route`: household autonomy off (agent `autonomy`), `cancel_interactions`, `go_here` to `@door` / `home` twice (25 s each), one plan-only `sim_route_to @door`, autonomy back on.
- `bp-f4b-street`: repro `f4b-street-route`: same start, `go_here` to (312.4, 1496.7) and back home (70 s each). That this point is across a street is inferred (it is the off-lot target of `detail-priority-cache`).

Run (main checkout): `node tools/harness/queue.ts tools/harness/queues/f4b.json`, then `node tools/harness/f4b-report.ts probe-out/profile/f4b-<date>-bp-f4b-inlot-f4b.json probe-out/profile/f4b-<date>-bp-f4b-street-f4b.json`.

**Results** (fill after the run):

| Job | Reason | CopyNodes calls (searched / direct) | Searched layouts | Prepends (+1 / calls) | Item 3 \|agent y - terrain\| | Outcome row |
|---|---|---|---|---|---|---|
| bp-f4b-inlot | complete, 177 s | 17,172 (9,962 / 7,210), one thread | real 9,962 | 5 / 54 | median 3.32 m, max 10.52 m (45 of 54 > 0.5 m) | D |
| bp-f4b-street | complete, 177 s | 17,927 (10,382 / 7,545), one thread | real 10,382 | 4 / 290 | median 0.00 m, max 6.92 m (6 of 200 > 0.5 m) | D |

Both runs 2026-10-18, files `probe-out/profile/f4b-2026-10-18-bp-f4b-{inlot,street}-f4b.json` (main checkout). The writer is in §12.8.

### 12.8 F-4b outcome D: the y writer (2026-10-18, static, against the live result)

**The writer is pass 16's rebuild helper `PathNodes_ApplyHeights 0x64C580`** (our name), called by post pass 16 `0x64C7C0` [verified, capstone on the Steam 1.67 exe]. §11.2 read pass 16's body and found no node store, and it noted that the callees were not re-read; the store is in this callee. §12.2's negative stays true for its scanned ranges (`0x740000..`, `0x81D000..`, `0x840000..`, `0x930000..`); the writer is in the planner range `0x64xxxx`, outside them.

How it works [verified, disassembly unless marked]:

1. **Pass 16 samples a height per planner node** (64-byte search nodes, x `+0x00`, y `+0x04`, z `+0x08`, level `+0x0C`, flags `+0x0E`) and keeps it in its "Routing/TempPathListTH" list. Each list entry holds the node pointer (`+0x08`), the node's `+0x1C` (`+0x0C`), x, the height and z (`+0x10..+0x18`), and two bytes (`+0x1C` keep, `+0x1D` original node; both 1 for a node of the path). Which height, by node flag:
   - flags `& 0x1080` (wade `0x80`, UFO leg `0x1000`, meanings from §11.1): `World_GetHeightAt 0xC6A250(x, z, level, ...)`, then clamped against the sea height `WorldManager_GetSeaHeight 0xC61670` minus an offset (tuning object `[this+0x50]+0x10`, default 1.5 at `0xFECDA8`) with a sign-mask select (`0x64C88B..0x64C8E5`): y = max(ground, sea height - 1.5) [verified, disassembly], so a wading or boat node never sits deeper than 1.5 m below the surface.
   - flags `& 0x600` (frozen pond `0x200`/`0x400`): `Terrain_GetHeight 0xC6A540` with a byte argument from flag `0x100` (`0x64C8F0..0x64C941`).
   - all other nodes: for a negative level with pass argument 2 set, a basement check first (`World_LotLookup 0xC68990`, `Lot_WorldToLocal2D 0xAB8540`, `0xB015B0`, `0xB01060`), then `0xC6A540` or, in the common case, **`World_GetHeightAt 0xC6A250(x, z, level, 1, ...)`** (`0x64CA26..0x64CA80`).
   So the height is level-aware: `0xC6A250` tries the lot floor first (`0xAB8EE0` -> `Lot+0x290` through `0xA8DF80`, the docs/94 §17 N5 data), then foundations (`0xB06A10` -> `0xB066E0`), then water (`0xB793F0`, `0xB745A0`), then terrain (`WorldCellGrid_Raycast 0xC2A570`, N9) (docs/20, docs/38 W row; the call order re-read at `0xC6A337..0xC6A50A`).
2. The rest of pass 16 (`0x64CB2B..0x64D4CA`) walks the list and inserts extra entries (`0x59B120`) with `+0x1D` = 0 between nodes, at most `0x800` entries; the insert condition was not read in detail [inferred: points where the sampled ground leaves the straight segment].
3. **`0x64C580(nodeVector, list, count, onlyOriginal)` rebuilds the node vector from the list.** For an entry of an existing node it stores the sampled height into the node: `movss [edx+4], xmm0` at **`0x64C620`** (`edx` = the entry's node pointer, `xmm0` = lane 1 of {x, height, z}), sets node flag bit `0x01` unless gate byte `0x115824C` is set (`0x64C625..0x64C631`), and copies the node. For an inserted entry it builds a new 64-byte node {x, height, z}, level and `+0x1C` from the neighbour, flags = (neighbour flags `& 0x1F80`) `| 0x02` (and `| 0x01` unless the gate) (`0x64C668..0x64C757`). Then it swaps the new vector into `nodeVector+0x08..+0x10` and frees the old one (`0x64C76B..0x64C7A1`).
4. Pass 16 calls it on every exit: success with `onlyOriginal` = 0 (`0x64D541`, inserted points kept), and both failure exits (more than `0x800` entries at `0x64D4E3`, `0x64D589`) with `onlyOriginal` = 1 (heights written, inserts dropped; the pass then returns false and the plan fails, §10.3).

Pass 16 runs on every plan (it is not behind gate G, §10.3), so **every** node the planner emits, start, goal, lot and world nodes alike, leaves `0x64E380` with a real height, and the -100 that `0x62F140`, `0x62A662` and `0x656550` store is a placeholder that never reaches CopyNodes. That matches both live runs: real y on 20,344 of 20,344 searched paths and 14,755 of 14,755 direct ones, `path+0x8C` = the 2D sum (median ratio 1.00, so the paths are near-flat, not inflated), and prepends with point y within 0.01 m of node 1 and node 0 `+0x20` around 0.5 m [verified live]. CopyNodes reads the bits 0x01 and 0x02 that `0x64C580` sets (route flags `0x10` = "height sampled by pass 16", `0x20` = "inserted by pass 16") [bits verified; meanings ours, inferred].

**Thread and call chain.** `Route_PlanLocked 0x82C6E0` -> `RoutingJob_Execute 0x81B2A0` (`0x82C7AD`) -> `PathPlan_WithRetries 0x661B50` (`0x81BF79`, and `0x81C67A` for the obstacle replan) -> `PathPlanner_PlanHierarchical 0x65E0D0` -> `0x64E380` (six call sites, four of them in `0x65E0D0`) -> pass 16 `0x64C7C0` (`0x64E4ED`) -> `0x64C580` [verified call sites]. So it runs **inside the `+0x918` hold, on whichever thread runs `Route_PlanLocked`**: the sim thread in vanilla play and in the f4b runs (`RouteWorker`/`RouteAsync` off; CopyNodes, also inside `Route_PlanLocked`, ran on the same thread id as the follower's slot `+0x100` calls from `RouteFollow 0x852700` in both runs [verified live]), our worker under `[RouteAsync]`.

**Item 3 note.** The in-lot run's agent y is 3.32 m (median) above `World_GetTerrainHeightImpl 0x786200` with flag 0, the street run's 0.00 m. Pass 16 asks `0xC6A250` with the node's level, which returns the lot floor; the probe's call returned bare terrain on a lot raised on a foundation [inferred: flag 0 / level 0 skips the floor]. So §12.4 candidate (a) is not a defect: the agent stands on the floor, and the path nodes are on the same floor.

**What this changes**

- §12.1 and §12.4: the contradiction is resolved by candidate (b). Followed paths have the "real" layout, where the emulator gives exact 2D answers (§12.4 row 1). F-4b is harmless in the game as it is; no route-start look-ahead defect, no length inflation.
- §11.2 bullet 2 ("no post pass writes y") and §3's "only start and goal get a terrain height" are wrong: pass 16 sets every node's y.
- §12.5, replacement contract: the replacement must reproduce pass 16 (or run the game's `0x64E380` on its output): level-aware heights from `0xC6A250`/`0xC6A540`/sea height by node flag, the inserted points with flag `0x02`, and flag `0x01` on sampled nodes. "Inner nodes stay at -100" in §12.5 is replaced by this. The shadow compare must still compare y and `+0x20`.
- P4 option G (docs/94 §12): pass 16 is in `Route_PlanLocked`'s closure, so the clone run executes it on the worker. It writes only the plan's own node vector (allocated per plan, no pointer into the routing world, as docs/94 §17.5). It reads non-routing data live: N1 (sea height), N2 (lot lookup, lot grid `0xC688A0` inside the height query), N4 (`Lot_WorldToLocal2D`), N5 (floor heights through `0xAB8EE0`, a second reader next to `Lot_LevelAtPoint`), N9 (terrain), and two reads the §17.2 table did not list: foundations `0xB06A10` and water heights `0xB793F0`/`0xB745A0`, both inside `0xC6A250` (docs/94 §17.8). A terrain LOD swap in the window can change a node's y between the live plan and the clone run; D5's terrain swap counter already marks that as M5.

Ledger: docs/51 §80 (I-99-50 to I-99-55). Not done: an emulator run of `0x64C580` (the store is one instruction, read in the disassembly, and the live data show its effect), and the insert condition of step 2.

| bp-f4b-inlot | | | | | | |
| bp-f4b-street | complete, 177 s | 17927 (10382 / 7545) | real 10382 | 4 / 290 | median 0.00 m, max 6.92 m (6 of 200 readings > 0.5 m) | D |

**bp-f4b-street (2026-10-18 22:31 PHT): data complete, game crashed at the probe's unload.** The probe wrote its JSON as `complete` at 22:31:53.8 (177 s); TS3W.exe died at 22:31:56.779 (empty xcpt file, no fault address), at the end of the `-t 180` window. The in-lot run with the same script survived. Cause evidence (docs/51 §81):

- The suspected race (a terrain call through our thunk, or a hooked call, in flight while the script is freed) did not happen in this run: the terrain call cap (200) was reached before the 90 s line (log: `terrain 200` at 90, 120 and 150 s), and `finish()` had already detached both hooks and run `Interceptor.flush()` 3 s before the crash [verified, log and file times] (I-99-60).
- On the dummy the old script does not crash when it is unloaded mid-call through its own `frida_tools.repl -t 1` host (agent unload, as in the job): 5 of 5 unloads with a thread inside a slow terrain stub, dummy alive each time; the fixed script measured 13-15 ms of call still in flight at every unload, so the race was hit [verified, dummy]. Frida 17 already waits for a JS callback that is inside a `NativeFunction` call before it frees the script (I-99-61).
- What did coincide: the stutter profiler's own unload (its `dispose()` closed `-180-seconds.csv` at 22:31:56.769), 10 ms before the crash and within tens of ms of the f4b host's unload (due at about 22:31:56.79). Two Frida agents unloading together is a known hazard (docs/TESTING.md §6, the end-of-B crashes of 2026-10-13). The in-lot run had the same overlap (csv closed 22:26:15.928, f4b unload due about 22:26:15.94) and survived, and a dummy recheck on 2026-10-18 with two no-op hosts unloading FIFO, near-together and LIFO survived 6 of 6, so this cause is inferred, not reproduced (I-99-62).

**Fix (2026-10-18):** unload guard in `f4b-reads.js` and `live-reads2.js`. `finish()` (timer or dispose) sets `closing` first (no new game call; the check sits right before each call), waits up to 2 s (`Thread.sleep`, which lets go of the JS lock) until no thread is inside a game call, then detaches and flushes; the file records `unloadWait` (`ms`, calls still in flight). The terrain thunk page now comes from `VirtualAlloc` and is never freed (one page per load), so a call that outlives the wait still returns into valid code. The dummy tests gained section 4 (5 unloads mid-call through a `-t 1` host; `--race` runs it alone). This closes the in-flight hazard. It does not address the two-agent overlap, the open lead for this crash: stagger the side session's unload against the profiler's in `profile-world.ts` next.
