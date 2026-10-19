# 98. Routing I/O study E: the in-room quadtree A*, as a reimplementation spec

Written 2026-10-18. Static only; the game was not run, no probe, no native code. Part E of the routing I/O study (docs/91 inputs, docs/92 portals and script callbacks, docs/93 outputs and contract, docs/94 snapshot and the G-vs-R gate). Goal: a replacement search over our own snapshot must give the game's answer (docs/93 §8: status, chosen goal and start, subpath signature exact; geometry equivalent). This doc covers the lot-level search inside one level of one lot: `InRoomPlan 0x632E40` and its point-to-point twin `0x632630`, the search `QuadTreeCellAStar 0x61E500`, its three node classes, the quadtree cell decomposition, costs, heuristic, open-list order and ties, termination, path extraction and the 64-byte search nodes it emits. Out of scope: portal and room graph search, portal locks and script callbacks (docs/92), the world graph R and water grid G (docs/91, agent F).

Tools: Ghidra 12.1.4 headless decompiles (read-only) on a private copy `game-copy/ghidra-io-e` of `ghidra-usf`, about 120 functions; capstone 5 over `game-copy/bin/TS3W.exe` (Steam 1.67) for every offset, branch direction and constant below; a raw `E8`/`E9` caller scan of `.text`; constant tables read from the file. Scratch scripts stayed in the session scratchpad.

Tags as in AGENTS.md: **[verified]** = read in the disassembly or decompile (address given); **[inferred]** = reasoning, with a ledger row in docs/51 §31 (I-98-*). Pseudocode is our own description, not decompiled code.

## 1. Summary

- **What it is.** A best-first A\* over the cells of one level's lot quadtree. Leaves of the quadtree are split further on the fly into up to 85 implicit sub-cells (whole, halves, quarters, eighths of the leaf side) by a 21-bit "final" mask per leaf [verified, §3.2]. Cells are created lazily in a hash table that lives until the next reset of the planner's A\* object, which happens at the start of every hierarchical plan (`0x65E345 -> 0x625400 -> 0x61E320`) [verified].
- **Nodes.** Every cell owns one graph node (vtable `0xFEA080`). The wrapper adds one **start node** (vtable `0xFEABD0`, "Routing/AStarStartNode", 0x48 bytes, ctor `0x61EB40`) and one **goal node per goal record** (vtable `0xFE9AD0`, "Routing/AStarGoalNode", 0x50 bytes, ctor `0x61AC40`), each linked both ways to the node of the cell under its point [verified]. **Correction to docs/91 §3.6 and the symbol CSV:** `0x61AC40` is the goal-node constructor and `0x61EB40` the start-node constructor; the CSV rows `AStarStartNode_ctor 0x61AC40` and `AStarGoalNode_ctor 0x61EB40` have the names swapped (CSV is append-only, so this doc and I-98-1 carry the correction).
- **Neighbours.** Four sides in the fixed order −z, +x, +z, −x; per side the edge-adjacent final cells found by climbing to the common ancestor and descending the mirrored path; along a side the cells come high end first [verified, §6].
- **Cost.** Per cell a class byte and a cost multiplier (walkable classes 0, 1, 13; anything else costs FLT_MAX). An edge from cell A to cell B enters B at a point on the shared edge, 0.01 inside B, chosen where the line from A's entry point to the search target crosses that edge, clamped to the shared span; cost = length × (multiplier of A for the part in A, of B for the part in B), +50 when stepping from a normal cell into a penalised one under flag `0x20` [verified, §7].
- **Heuristic.** Plain 2D Euclidean distance (x, z) from the node's entry point to one target point, weight 1 [verified, `0x61ECD0`].
- **Order and ties.** Open list = 50,000 buckets by `trunc(f)`, each bucket kept sorted by exact `f`; a new node goes **before** nodes of equal `f` (newest first); a node whose `f` drops inside its bucket goes **after** equal ones. Pops always take the head of the lowest bucket, so expansion is exact best-first on `f` [verified, §8.3].
- **Termination.** Not at the first goal. The search keeps the goal node with the highest score seen (strictly greater wins, so the first popped among equal scores stays) and stops when that score is within 2^-16 of a threshold (the best same-room goal score, or the caller's primary goal score), when the open list empties, or when the number of inserted nodes reaches 64,000 (262,146 with context flag `0x100000`) [verified, §8.4]. A found goal at the cap is a success.
- **Output.** The node chain start → cells → goal becomes 64-byte search nodes: entry point x/z, y = −100, level, cumulative cost, type 6/0/7, and fixed defaults elsewhere (`+0x2C` = `0xB828`, `+0x3C` stale stack) [verified, §10]. The lot layer then pulls a single directed goal back by 0.125 m (and appends the real goal again), inserts internal-portal node pairs (docs/92) and moves the nodes to world space with lot-edge flags 4/8/0x10 [verified, §10.2].
- **Answers to docs/93 open questions.** Q4 (tie order of equal-cost goals) is answered for the in-room search: §8.3 and §8.4. Q1 (64-byte node fields) is answered for in-room nodes: §10.1. Nothing in this search calls managed code; the only script-reaching step is the portal-lock refresh of the start room (`0x632460`, docs/92) [verified call; script reach per docs/18].
- **Remaining unknowns: none that block the spec;** seven small items are listed in §13 with the offline or live check that closes each. The largest is a quirk at the quadtree root border (§6.3) that a replacement must copy if it occurs in real trees.

## 2. Entry points and call shape

| Caller | Calls | Purpose | Tag |
|---|---|---|---|
| `LotPlan_Inner 0x63F6E0` (lot leg of `PathPlanner_PlanHierarchical 0x65E0D0` via `LotPlan 0x6410B0`) | `InRoomPlan` at `0x63FBFC` (start and goal in the same room) and at `0x64007B`, `0x640754`, `0x640AB5` (portal-to-goal probe inside the portal-failure loop; the no-portal path with any-room = 1; the last leg after portals); `0x632630` at `0x6401FB`, `0x640421` (first and middle legs, point to portal point) | lot leg | verified call sites; site-to-role mapping from the decompile order |
| `0x633FA0` | `InRoomPlan` at `0x6343AB` | called from `0x63DF60` (`0x63DFF8`, `0x63E105`, next to `RoomGraph_ConnectedRooms`) and `LotSearch_Entry 0x640E00` (`0x640F29`); room and goal-step probes | verified callers; purpose inferred |
| `0x635080` | `0x632630` at `0x63513D` | not traced | verified call |

`InRoomPlan` is thiscall on the lot planner `P` (`ecx`), with 11 stack arguments [verified `0x632E40..0x632E5D`, `0x63FBC1..0x63FBFC`]:

| # | Argument | Meaning |
|---|---|---|
| 1 | `X` | lot search context (§3.5), built by `LotPlan_Inner` on its stack |
| 2, 3 | lot key lo/hi | 64-bit lot id |
| 4 | start point | lot-local `{x, z, level:int}` |
| 5 | goal point | lot-local `{x, z, level}`, the caller's primary goal |
| 6 | threshold score | float, the primary goal's score |
| 7 | goal vector | 28-byte records `{x, z, level, dirx, dirz, ?, score}` (lot-local) |
| 8 | path out | "Routing/Path" object; nodes appended to its vector `+0x08..+0x0C` |
| 9 | goal index out | written on success |
| 10 | any-room flag | goals in other rooms may set the target and threshold |
| 11 | no-nudge flag | fail instead of nudging an unwalkable start |

Return codes: 0 success, 1 no lot / lot object / nudge failed, 2 no start node, 4 no usable goal, 5 search failed [verified]. `LotPlan_Inner` returns them upward; `PathPlan_WithRetries` maps them (docs/93 §2.1).

`0x632630` (we name it `InRoomPlanToPoint`) is the same shape with one target point instead of a goal vector: arguments `X`, lot key, start point, target point, path out, no-nudge flag; one goal node with index 0 and score 1.0, threshold 1.0 [verified decompile; call to `0x61E500` at `0x632C1A` with `0x3F800000`].

## 3. Data structures

### 3.1 The level quadtree ("tree")

Per lot and level, `LotTable` entry `+0x74[level − entry+0x40]` [verified `RoomGraph_NodeFor 0x6320D0`]. Fields used by the search [verified reads]:

| Field | Meaning |
|---|---|
| `+0x04, +0x08, +0x0C, +0x10` | bounds minX, minZ, maxX, maxZ (half-open: a point at maxX or maxZ is outside) [`0x663900`] |
| `+0x24` | root node |
| `+0x2C` | default class of empty space |
| `+0x30` | default multiplier of empty space |
| `+0x38` | root cell side length ("scale" of depth 0) |
| `+0x44` | level |
| `+0x48` | pin counter (docs/81) |
| `+0x50` | tree id (part of the cell key) |

Quadtree node [verified `0x66A040`, `0x667960`, `0x669FB0`, `0x6681E0`]: `+0x04` dword: bits 0-1 kind (0 leaf, 1 internal), bits 3-23 "final" mask for sub-cell shapes 0..20 (bit `shape+3`), byte `+0x07` refcount; internal: four children at `+0x0C + 4k`; leaf: footprint entry list at `+0x08` (bit 0 of `+0x08` set: heap array `(+0x08 & ~1)` with count `+0x0C`; clear: one inline entry at `+0x08..+0x13`). Entry = 12 bytes `{FootprintMapEntry*, maskLo, maskHi}`; `FootprintMapEntry` (`0xFEAC7C`): footprint (outer object) at `+0x08`, byte `+0x10` = sub-index passed to the footprint's class and multiplier slots, `+0x10` bits `0x400`, `0x800` used by the masked path (§5.2).

### 3.2 Cells: position record and shapes

A cell position is a 0x24-byte record (vtable `0x107396C`, ctor `0x669BE0`, set `0x669C30`) [verified]:

| Field | Meaning |
|---|---|
| `+0x04` | tree |
| `+0x08` | parent quadtree node of `+0x0C` |
| `+0x0C` | quadtree node (0 = empty quadrant under an internal node) |
| `+0x10` | cost multiplier (cached, §5.4) |
| `+0x14` | side length of the node's square ("scale") |
| `+0x18` | packed: bits 0-11 ix, 12-15 level+4, 16-27 iz, 28-31 depth |
| `+0x1C` | byte: shape (sub-cell 0..84); bits 8-31: cache key |
| `+0x20` | byte: class; bits 8-9 quadrant (child index in parent); bit 10 (`0x400`) "neighbours not yet expanded" |

Child index k: dx = k & 1, dz = k >> 1 (table `0xFEED3C`: (0,0), (1,0), (0,1), (1,1)) [verified]. Descending into an internal node: ix' = 2·ix + dx, iz' = 2·iz + dz, scale' = scale·0.5, depth+1, node' = child k (may be 0), quadrant = k [verified `0x66A040`]. Descending inside a leaf only changes the shape: shape' = `childShape[shape][k]` [verified]. Shape tables (identical copies at `0xFED500/0xFED7A8`, `0xFEE118/0xFEE3C0`, `0xFEED98/0xFEF040`) [verified file read]:

| Shapes | Offset (x, z) in node units | Size |
|---|---|---|
| 0 | (0, 0) | 1 |
| 1-4 | k = shape−1: (0.5·dx, 0.5·dz) | 0.5 |
| 5-20 | 4×4 grid, row-major by parent quadrant pairs (5:(0,0) 6:(.25,0) 7:(.5,0) 8:(.75,0) 9:(0,.25) … 20:(.75,.75)) | 0.25 |
| 21-84 | 8×8 eighths | 0.125 |

`childShape` (`0xFEE518`, stride 16): 0→1,2,3,4; 1→5,6,9,10; 2→7,8,11,12; 3→13,14,17,18; 4→15,16,19,20; 5..20 → the eighths (5→21,22,29,30 … 20→75,76,83,84); 21+ → none. `parentShape` (`0xFEEA68`) and `parentQuadrant` (`0xFEEBC0`) are the inverse [verified].

Cell rectangle: minX = (ix + offX[shape])·scale + tree.minX; maxX = minX + size[shape]·scale; same for z with iz and tree.minZ; centre = min + size·scale·0.5 [verified `0x662BC0`, `0x662E80`, `0x662C00`, `0x662EC0`, `0x662C30`, `0x662C80`].

**Final cell** (a search cell): node = 0, or shape ≥ 21, or (node kind ≠ 1 and the node's mask bit `shape+3` is set) [verified `0x66AC60`, `0x66B6E0`, `0x669D40`]. An internal node is never final.

Cell object (0x3C bytes, "AStarNodeTable" entry, created by `0x61C2C0` -> `0x61BEC0`): the 0x24-byte position at `+0x00`, refcount object `+0x24/+0x28`, hash links `+0x2C`, key `+0x30/+0x34`, graph node `+0x38` [verified]. Key (`0x669FF0`): k0 = ix | iz<<16 | depth<<12 (packed from `+0x18`), k1 = shape | (level+4)<<8 | tree id<<12; bucket = (k0 xor k1) mod 10007 [verified `0x664370`]. On creation the graph node's entry point is the cell centre, its level bits copied from the cell, and `0x400` set [verified `0x66BC30`, `0x61C2C0`].

### 3.3 A\* nodes

Common layout (base ctor `0x61B380`) [verified]:

| Field | Meaning |
|---|---|
| `+0x04` | u16 refcount |
| `+0x08..+0x10` | explicit neighbour vector ("cAStarGraphNode/mNeighborNodes"), insertion order, erase keeps order (`0x61B740` append, `0x61B6D0` erase) |
| `+0x18` | parent |
| `+0x1C` | cell (0 for none) |
| `+0x20` | g |
| `+0x24` | f |
| `+0x28, +0x2C` | entry point x, z |
| `+0x30` | low u16 generation stamp; bits 16-19 level+4; bits 20-21 state (0 new, 1 open, 2 closed); bit 22 fixed position |
| `+0x34` | open-list bucket index |
| `+0x38, +0x3C` | bucket links prev, next |

Start node: fixed position, `+0x40` = −999.0, `+0x44` = 1.0. Goal node: `+0x40` u16 stamp seen, `+0x44` score, `+0x48` goal index, `+0x4C` byte "stamp saved", `+0x4D` byte disabled [verified ctors].

Vtable slots (offsets) [verified file read of the three vtables and the stubs `0x7EAEA0` true, `0x620D60` false, `0xF626F0` this, `0x5BFB90` null]:

| Slot | Graph `0xFEA080` | Start `0xFEABD0` | Goal `0xFE9AD0` |
|---|---|---|---|
| `+0x04` detach | `0x61B440` | `0x61EC90` | `0x61EC90` |
| `+0x08` add neighbour / `+0x0C` remove | `0x61B740` / `0x61B6D0` | same | same |
| `+0x10` is goal | false | false | true |
| `+0x14`, `+0x18` goal self | null | null | this |
| `+0x1C` is start | false | true | false |
| `+0x20`, `+0x24` start self | null | this | null |
| `+0x28` is terminal | false | true | true |
| `+0x2C` expand | `0x61AF40` | same | same |
| `+0x30` edge cost into this node | `0x61AF80` | `0x61AF80` | `0x61AD90` |
| `+0x34`, `+0x38` | `0x61B010` (cell multiplier, else 1.0; not used by the search loop) | same | same |
| `+0x3C` per-search reset | `0x61B170` | `0x61B170` | `0x61ACD0` |

Reset `0x61B170`: state 0, g = f = 0, parent released, bucket fields 0, and sets `0x400` on its cell; position and the fixed bit are kept. Goal reset `0x61ACD0`: base reset, then the first time it saves the stamp; if a later generation resets it again, `+0x4D` = 1 and its cost becomes FLT_MAX (goal nodes are single-search objects) [verified].

### 3.4 Search object Q and open list

Q is the planner's only A\* object, `P+0xBC` (0x9C90 bytes; docs/18): `+0x08..+0x10` target point, `+0x14` result vector (node pointers, start first), `+0x18` open list, `+0x1C` result goal index (−1 on failure), `+0x24` inserted-node counter, `+0x28..+0x9C84` cell hash table [verified `0x61E360`, `0x61E500`, `0x61C020`].

Open list (`Q+0x18`): `+0x04` vector of 12-byte buckets `{head, tail, end}`, `+0x14` lowest non-empty index (−1 empty), `+0x18` highest used index (−1 empty) [verified `0x61DF50`, `0x62BD60`, `0x61BAF0`].

### 3.5 Lot search context X

Built per `LotPlan_Inner` call: `0x6298F0` initialises it, then [verified `0x63F7B6..0x63F828`]: `+0x40` route options (agent), `+0x44` P, `+0x48` → heuristic object at `X+0xA0` (vtable `0xFEBF48`, slot 0 `0x61ECD0`), `+0x4C` = 0, `+0x50` flags = caller flags | `0x10000` (| `0xF` in obstacle mode, docs/93 E4; | `0x2000000` when the start and goal points disagree in the `Lot_RoomLookup` height test against `0xAB3380`, only for lots with byte `+0x4B0` set; decompile of `0x63F6E0`), `+0x54` lot object.

Flag bits of `X+0x50` read by this search [verified reads]:

| Bit | Effect |
|---|---|
| `0x1, 0x2, 0x4, 0x8` | turn blocked classes ≤ 4 / 6 / 8 / 10 (highest set bit wins) into penalised walkable (§5.3) |
| `0x10` | walkable non-1 classes get multiplier 1.0 |
| `0x20` | +50 per extra blocking type in a penalised cell; +50 to enter a penalised cell from a normal one |
| `0x100` | any multiplier > 1.0 makes the cell blocked (class 10) |
| `0x80000` | retry mode: footprint types 0x13 and 0x18 block (class 12) |
| `0x100000` | node cap 0x40002 instead of 64,000 |
| `0x200000` | small-cell clearance test (§5.3) |
| `0x2000000` | footprints of type 0x1E ignored; also defeats the class cache (§5.4) |
| `0x10000` | always set by `LotPlan_Inner`; no reader in this search [verified absence in the functions of this doc] |

## 4. From a point to a cell

`RoomGraph_NodeFor 0x6320D0(lot, point)`: lot must be in the lot table, level tree must exist, then `AStarNode_Get 0x664B10` on the tree: `0x663900` descends from the root [verified]:

1. Point outside [minX, maxX) × [minZ, maxZ) → no cell.
2. Start at root: ix = iz = 0, depth 0, scale = tree `+0x38`, shape 0.
3. Loop: if node = 0 → stop. If node is a leaf and the cell is final → stop. Child k = (x ≥ centreX ? 1 : 0) + (z ≥ centreZ ? 2 : 0); descend.
4. Find-or-create the cell in Q's table.

A point exactly on a split line goes to the higher child [verified decompile of `0x663900`: the child is chosen with "centre ≤ coordinate"].

## 5. Cell class and multiplier

### 5.1 One footprint

Slots of the footprint's outer vtable used here [verified, all 9 classes of docs/82 §3.1]: `+0x0C` type (byte `fp+0x35`, `0x624450`), `+0x08` class(options, subIndex), `+0x04` multiplier(options, subIndex), `+0x28` "ignore" (false except `0xFEAE08`: byte `fp+0x48`), `+0x60` applies to agent (always true, `0x9C1B00`), `+0x84` object id (via `+0x7C`), `+0x34` rectangle overlap test (`0x80F0D0`, `0x80B4F0`).

| Class (outer vtable) | Class slot | Multiplier slot |
|---|---|---|
| base and `0xFEBE38`, `0xFEB5F8`, `0xFEB720`, `0x1020AE8` | `0x807090`: 0 if the agent's ignore list (options `+0x28` object, list `+0x38..+0x3C` of ids) holds this footprint's object id; if `fp+0x20` has allow/deny lists (`0x946E70`): 6 when the agent passes `0x946E90`, else 0; otherwise byte `fp+0x34` | `0x806F70`: 1.0 if the agent's own object id (options `+0x08/+0x0C`) equals `fp+0x14/+0x18`, else float `fp+0x1C` |
| `0xFEBBC0` (object footprints) | `0x80F840`: byte `fp+0x34` only if options `+0x08/+0x0C` equals `fp+0x40/+0x44` and option bit `0x1000000` (word 1) is clear; else 0 | `0x80F7D0`: `fp+0x1C` under the same test, else 1.0 |
| `0xFEBA98` | `0x807090` | `0x620DC0`: `fp+0x1C` |
| `0xFEB848` | `0x80C300`: with options, 0 when (`fp+0x41 & 1`, agent kind `options & 0xCF00` = `0x300`, option `0x10000` clear) or (`fp+0x41 & 2` and option `0x20000`) or (`fp+0x41 & 0xC` = `0xC`); then by sub-index: 0 → 8, 1 → byte `fp+0x34`, 2 → 8, other → 0 | `0x80C3D0`: 1.0 when (`fp+0x41 & 2` and option `0x20000`) or sub-index is 0 or 2, else `fp+0x1C` |
| `0xFEB970` | `0x8066C0`: 0 for the agent's own object, else byte `fp+0x34` | `0x80C3D0` |
| `0xFEBD10` | `0x807B70`: types 0x1A/0x1B → 0 under option `0x20000000`; type 0x1E → 10 unless option test (2,0,1) or `0x10000000`; else base | `0x807C20`: type 0x1E → FLT_MAX under the same test; else base |
| `0xFEAE08` | `0x6119F0`: 0 | `0x620DC0`: `fp+0x1C` |

(Option tests are `RoutingOptions_Test 0x81C880`: words `+0x28/+0x2C`, or `+0x30/+0x34` when the third argument is set.)

### 5.2 One cell from its entries

For a final cell with a leaf node and shape 0 (`0x6681E0`), with an internal node (`0x668C40`: max over children, recursive), or a leaf sub-cell (`0x667DB0`: only entries whose 64-bit mask has bit `bitOfShape[shape]` (table `0xFEDE48`) set) [verified]:

1. class = 0, mult = 1.0, any = false.
2. For each entry with a footprint fp, skip it when the agent's ignore list holds fp's object id, or fp `+0x28` is true; in the generic case (step 3) also when fp is the agent's own object (options `+0x08/+0x0C` = fp id).
3. By type: **0x16** → remember (class, mult) of this entry (last one wins), any = true. **0x0A** → same, second slot. **0x1F** → any = true, and remember "keep class 2". **0x1E** with flag `0x2000000` → skip. Otherwise: c = class slot, m = multiplier slot; in retry mode (`0x80000`) types 0x13/0x18 give c = 12, m = FLT_MAX; type **0x14** gives c = 1, m = 100,000 unless the agent has option `0x1004`; class = max(class, c) (signed), mult = max(mult, m), any = true.
4. Masked path only: an entry with `+0x10` bit `0x800` sets (class, mult) from that entry and ends the scan; bit `0x400` marks any = true without the generic max.
5. After the scan: if a 0x16 entry was seen, class = max(class, c16), mult = max(mult, m16); else if a 0x0A entry was seen, the same with it.
6. If class = 2 and no 0x1F entry: class = 0.
7. If class not in {0, 1, 13}: mult = FLT_MAX.
8. If no entry applied (any = false), or the node is 0: class = tree default (`+0x2C`), mult = tree `+0x30` when that class is walkable, else FLT_MAX; with no tree: class 12.

### 5.3 Context adjustment (`QuadTreeCellPos_C 0x66B090`)

After §5.2 (class starts at 12 with mult FLT_MAX when nothing sets it) [verified]:

1. If `X+0x4C` < 2, or = 13, or < class (the normal case; `LotPlan_Inner` sets `X+0x4C` = 0): if flags `0xF` is non-zero, let t = 10, 8, 6, 4 for the highest of bits 8, 4, 2, 1; if class ≤ t (signed) → **penalise**. Else, if flag `0x10` and class in {0, 13}: mult = 1.0.
   Otherwise (`X+0x4C` ≥ 2, ≠ 13, ≥ class): `X+0x4C` = class; walkable → the `0x10` rule; else penalise.
2. **Penalise:** mult = 1.0 (class ≤ 4), 2.0 (5-6), 3.0 (7-8), 400.0 (≥ 9); class = 13. With flag `0x20` and a node, if the leaf has more than one entry (`0x626360`) and the number of distinct blocking footprint types in this sub-cell (`0x669530`, bit count) is n > 1: mult += 50·(n − 1).
3. **Small-cell clearance** (flag `0x200000`, class walkable, shape ≠ 0, has a parent, size·scale < 0.5): collect the leaf's footprints (`0x669850`/`0x669B90`); a footprint that is the agent's own object or of type 9, 0x12, 0x13 or 0x18 ends the test with "keep"; a footprint whose every sub-class is walkable is skipped; types 1 (unless ignored), 0x0C, 0x0D, 0x10, 0x11, 0x15, 0x17, 0x1D count if their rectangle test overlaps the cell grown upward (`0x66A380`) until its side ≥ 0.5. Any count and no "keep" → class 10, mult FLT_MAX.
4. Flag `0x100` and mult > 1.0 → class 10.
5. Class 1 cells clear their cache key (recomputed on every use).

Walkable = class 0, 1 or 13 (`0x620CA0`) [verified].

### 5.4 Cache

`mult(cell, X)` (`0x66B670`) and `class(cell, X)` (`0x66B600`) recompute when `X+0x50` differs from the key `cell+0x1C >> 8` (24 bits; with no context the key is `0x2000000`) [verified]. Because only 24 bits are stored, a context with any flag above bit 23 (for example `0x2000000`) recomputes on every call: cost only, not answers. Cells die at the Q reset at the start of each hierarchical plan, so a class never leaks across agents [verified `0x65E345`].

## 6. Neighbours

### 6.1 When

A popped node gets its quadtree neighbours only if it is neither start nor goal, has a cell, and the cell's `0x400` bit is set (cleared on use, set again by the next generation's reset) [verified `0x61E777..0x61E7B9`, `0x66BFD0`]. Then, for each neighbour cell in order, if its node differs from this one: `NodeState(neighbour, parent = this)` (§8.2). Afterwards the node's explicit neighbours (start/goal links, `0x61AF40`) are relaxed in vector order, skipping its own parent [verified].

### 6.2 Which (`0x66B9C0`)

For side d = 0, 1, 2, 3 (−z, +x, +z, −x), with climb set S_d, mirror M_d and edge children E_d [verified call arguments `0x66BA6A..0x66BB31`]:

| d | Side | S_d (climb while quadrant in) | M_d (k → k') | E_d (c1, c2) |
|---|---|---|---|---|
| 0 | −z | {0, 1} | 0→2, 1→3, 2→0, 3→1 | (2, 3) |
| 1 | +x | {1, 3} | 0→1, 1→0, 2→3, 3→2 | (0, 2) |
| 2 | +z | {2, 3} | 0→2, 1→3, 2→0, 3→1 | (0, 1) |
| 3 | −x | {0, 2} | 0→1, 1→0, 2→3, 3→2 | (1, 3) |

1. **Climb** (`0x66B8D0`): if the cell has no parent and shape 0, there is no climb (cur = cell, empty path). Else go to the parent (sub-cell: parent shape; shape 0: parent node, ix and iz halved after removing dx/dz, scale doubled) and push the cell's quadrant; while cur has a parent node and the last pushed quadrant is in S_d: push cur's quadrant, go up.
2. **Mirror down** (`0x66AC60`): while the path is not empty and cur is not final: descend into M_d[last pushed quadrant], pop.
3. **Collect** (`0x66B6E0`): stack = [cur]; while the stack is not empty: pop; if final, find-or-create its cell and append it to the output; else push child c1, then child c2 (so the c2 side is visited first).

Resulting order: sides −z, +x, +z, −x; along a side the cells come from the +x end (sides −z, +z) or the +z end (sides +x, −x) first, depth first [verified]. Cells are listed even when blocked; the cost rejects them (§7). No diagonals.

### 6.3 Border quirk

When the climb reaches the root while the last quadrant is still in S_d (the cell lies on the tree's border on side d), the mirror step still runs and lands in the sibling on the opposite side, so the "neighbour" on side d is a cell from side d + 2 (a duplicate of a real neighbour when that sibling is final, otherwise non-adjacent cells of its far row) [verified control flow; effect inferred, I-98-6]. A replacement must reproduce this if real trees put walkable cells on the root border; the offline check in §13 decides.

## 7. Costs

All costs are lot-local units (metres) times multipliers; INF threshold = 1.7014117e38 (`0x11580F4`, `0x11585D4`), FLT_MAX = 3.4028235e38 [verified].

### 7.1 Edge into a cell node (`0x61AF80` -> `0x66C0C0`)

Inputs: target cell C (of node N), parent node P with entry point p, Q's target point T, fixed bit of N. Output: entry point e of N and the cost [verified]:

1. P = none: e = N's point, cost 0.
2. P is terminal (start or goal node): e = N's point if fixed, else p clamped into C's rectangle; cost = |e − p| · mult(C).
3. P has no cell: e = N's point if fixed, else C's centre; cost = |e − p| · mult(C).
4. Otherwise (P's cell D):
   - **Type gate:** if C's node has a footprint of type 0x18 that applies (`0x6685B0`/`0x668DB0`) and overlaps C's rectangle, and D does not have an applying type-0x13 footprint overlapping D's rectangle → cost FLT_MAX (rejected).
   - **Entry point** (if not fixed): zGap = max(minZ(D) − maxZ(C), minZ(C) − maxZ(D)), xGap the same in x. If zGap ≥ xGap (cells stacked in z): ez = minZ(C) + 0.01 if minZ(C) > centreZ(D), else maxZ(C) − 0.01; span = [max(minX(C), minX(D)) + 0.01, min(maxX(C), maxX(D)) − 0.01]; x = T.x; if ez lies between centreZ(D) and T.z (inclusive) and |T.z − p.z| > 1e-4: x = p.x + (T.x − p.x)·(ez − p.z)/(T.z − p.z); ex = lo if x ≤ lo, else hi if x ≥ hi, else x. Else (side by side in x): the same with x and z swapped.
   - **Cost** (`0x66BCE0`, this = C, other = D): mD = mult(D), mC = mult(C); either > INF → FLT_MAX. t = 0 if C = D; else the first of: e.x > maxX(D) and e.x ≠ p.x → t = (e.x − maxX(D))/(e.x − p.x); e.x < minX(D) → (e.x − minX(D))/(e.x − p.x); e.z > maxZ(D) → (e.z − maxZ(D))/(e.z − p.z); e.z < minZ(D) → (e.z − minZ(D))/(e.z − p.z); clamp t to [0, 1]. cost = (t·mC + (1 − t)·mD)·|e − p|; + 50.0 when C's class is 13, D's class < 2 and flag `0x20`.

Constants: 0.01 (`0x106D6CC`), 1e-4 (`0xF988E8`), 50.0 (`0xFEED90`) [verified].

### 7.2 Edge into a goal node (`0x61AD90`)

e = goal point. Disabled goal → FLT_MAX. No parent → 0. Goal with a cell: |e − p| · mult(goal cell); without: |e − p| [verified].

### 7.3 Edge into a start node

Start nodes are fixed; the cost path is §7.1 with N = start (only reached when a cell node re-relaxes an open start; it can never beat g = 0) [verified].

### 7.4 Heuristic

h(N) = sqrt((N.x − T.x)² + (N.z − T.z)²) (`0x61ECD0`, called as `X+0x48` slot 0 with x, z, level, g, &T; level and g unused) [verified]. f = g + h.

## 8. The search (`QuadTreeCellAStar 0x61E500`)

### 8.1 Call

thiscall on Q with (X, start set, target T, threshold, unused, stamp flag). The wrappers pass the stamp flag 0, so `AStarSearch_StampReader 0x61C9B0` never runs here [verified `0x633593`, `0x632C1A`].

### 8.2 NodeState (`AStarSearch_NodeState 0x61E030`): insert or relax N from parent P

1. If N's stamp ≠ the global generation (u16 at `0x11580C4`): N's reset slot, then stamp = generation.
2. State new: if P is given: (c, e) = N's cost slot from P (§7); reject if c ≥ INF; g = c + P.g; reject if g ≥ INF; parent = P; if not fixed, N's point = e; N.g = g. If P is none: parent none, g = 0. Then h at N's (new) point; f = g + h; reject if f > INF (strict); state = open; insert (§8.3); Q`+0x24` += 1.
3. State open and P given: (c, e) from P; reject if c ≥ INF; g = c + P.g; h at N's **current** point (not e); f' = g + h; only if N.f > f' (strict): parent = P, point = e (if not fixed), f = f', g = g, update (§8.3).
4. State closed: nothing (no reopen).

Note the open-state quirk: the new f uses h at the old entry point while the point moves to e [verified `0x61E1FD..0x61E28A`].

### 8.3 Open list order and ties

- Bucket b = trunc(f), clamped to [0, 49999] (`0xC350`) [verified `0x61DF56`].
- **Insert** (`0x61DF50` -> `0x61B880`): walk the bucket from the head while node.f > cur.f; insert before the first cur with cur.f ≥ node.f; so a new node precedes every node of equal f.
- **Update** (`0x61DFB0`): same bucket and not head: walk backward while prev.f > node.f and move N right behind the first prev with prev.f ≤ node.f, so it follows nodes of equal f; head: no move. Different bucket: remove (`0x61BC90`) and insert as above.
- **Pop:** head of bucket min; if the bucket empties, min advances to the next non-empty bucket up to max, else min = max = −1 (`0x61BAF0`).

Result: strict best-first on f; ties go to the most recently inserted node, except nodes moved up inside a bucket, which queue behind equals [verified].

### 8.4 Loop and termination

1. Init (`0x61E360`): generation += 1; T = target; open list emptied; result vector cleared; every start node in the start set, in ascending pointer order (red-black set `0x61C1E0`), gets NodeState(start, none).
2. While open list not empty: pop N; state = closed.
   - If N is a goal node: if best is none or N.score > best.score: best = N, and if |best.score − threshold| ≤ 2^-16 (`0xFEA55C`) → stop.
   - If Q`+0x24` ≥ cap (64,000; 0x40002 with flag `0x100000`) → stop.
   - If N is neither start nor goal and has a cell: quadtree neighbours (§6).
   - Explicit neighbours of N (§6.1).
3. Best goal found → success: Q`+0x1C` = best's goal index, path = best, parent, … up to and including the first start node, reversed to start-first (`0x61DD40`). Else Q`+0x1C` = −1, failure [verified `0x61E6E2..0x61E805`].

The cap counts inserted nodes (state new → open), not pops [verified].

## 9. The wrappers

### 9.1 `InRoomPlan`

1. Lot not in the table or `FindLotById` fails → 1.
2. startRoom = `Lot_RoomIdAtPoint 0x624780` (room id + 16, or level + 4 outside rooms, −1 none). `0x632460`: for each portal of startRoom whose level tree exists: find its footprint; if its class ≠ (locked ? 8 : 0), remove it from the tree, set the class, re-insert (portal lock refresh, docs/92; quadtree write, docs/81).
3. startCell = cell at the start point.
4. Loop (at most twice):
   - Start walkable? (cell exists and class walkable). If not and no-nudge → 1. If not: **nudge**: for i = 0..63: ring r = i/8 + 1, radius = r·0.5, or 2^r·0.5 when `options & 0xCF00` is 0x4100, 0x4200 or 0x4300; angle = (i mod 8)·π/4; candidate = (start.x + sin(angle)·radius, start.z + cos(angle)·radius, start.level); if its cell is walkable: remember the first such cell from any room; if the candidate's room = startRoom, take it and stop. After 64 tries take the same-room cell, else the first any-room one, else → 1.
   - If the cell has a node: unless fixed, set the cell node's point to the start point; make the start node at the **original** start point (fixed), link start ↔ cell node, start's cell = this cell, put it in the start set. No start node → code 2.
   - **Goals**, in vector order with index k: cell at the goal point; if it has a node: unless fixed, the cell node's point = goal point; make goal node (point, index k, score); if the cell is walkable: link goal ↔ cell node, keep the node; and if score > best (strict, best starts at −FLT_MAX) and (goal room = startRoom or any-room flag): best = score, bestPoint = goal point. Goals OK = some walkable goal and best ≠ −FLT_MAX; else code 4.
   - **Target and threshold:** if threshold > best, or the primary goal point's cell is not walkable: target = bestPoint, threshold = best; else target = primary goal point.
   - Search (§8). Success: goal index out, emit nodes (§10.1). Failure: code 5.
   - Detach and release the start and goal nodes (they unlink themselves from the cell nodes).
   - If success, or already retried: restore `X+0x50`, return. Else set `0x80000`; if the (nudged) start cell is walkable under the new flags, return the failure; otherwise run the loop again with `0x80000` (so a start inside a type 0x13/0x18 area is nudged out and searched again) [verified].

### 9.2 `InRoomPlanToPoint 0x632630`

Same as §9.1 with: room from the start point; nudge radius always r·0.5 and only same-room cells (no any-room fallback); one goal node at the target point, index 0, score 1.0, made only if that cell is walkable (else code 4); target = the point; threshold 1.0 [verified].

## 10. Output

### 10.1 The 64-byte search nodes (`0x62A470`, thiscall on P)

One node per path element, start first, appended to the path vector [verified `0x62A5A0..0x62A72A`]:

| Off | Content |
|---|---|
| `+0x00` | entry point x (lot-local here) |
| `+0x04` | −100.0 (`0xFEAFA8`) |
| `+0x08` | entry point z |
| `+0x0C` | s16 level ((`+0x30` >> 16) & 0xF) − 4 |
| `+0x0E` | u16 flags = 0 |
| `+0x10` | g (cumulative cost, multiplier units) |
| `+0x14` | −999.0 (start node: its `+0x40`, also −999.0) |
| `+0x18` | 1.0 (start node: its `+0x44`, also 1.0) |
| `+0x1C` | type: 6 start, 7 goal, 0 cell |
| `+0x20, +0x24` | 0 (globals `0x1158260/4`) |
| `+0x28` | 0 |
| `+0x2C` | `0xB828` (global `0x115826C`, initialised data, no absolute writer) |
| `+0x30, +0x34` | 0 (globals `0x1158270/4`) |
| `+0x38` | 0 |
| `+0x3C` | not written: stale stack |

### 10.2 What the lot layer does with them

- **Same room** (`LotPlan_Inner`, `0x63FBFC..0x63FD05`): nodes go straight into the result path.
- **Single directed goal** (exactly one goal record with a non-zero direction): before the search the goal is moved 0.125 m (`0xFE8278`) back along its direction if the segment test `0x6672E0` from the goal to that point (radius 0.001) is clear; after success the last node's type is set to 0 and a copy moved forward again by 0.125·direction (type 7, same g) is appended [verified `0x63FC70..0x63FC9F`].
- **Internal portals** (`DAT_0115824c` = 1 in the file): `0x632150 -> 0x62FBE0` replaces runs of type-0 nodes near an internal portal of their room with a type-2/type-3 portal pair (docs/92).
- **World space** (`0x62F140`): every node goes through the lot transform (`0x62ECD0`), y set to −100.0; nodes on the lot's level of type ≠ 2, 3 get `+0x0E` |= 0x10 (outside the lot rectangle), else |= 8 (within 1.0 of its edge), else |= 4 (within the routing world margin `world+0x24`).
- **Several rooms:** legs from `0x632630` and the last `InRoomPlan` are stitched with portal nodes (types 2, 3) by `LotPlan_Inner` (docs/92); each leg's own nodes are as in §10.1.

## 11. Reimplementation sketch

<!-- docs-code: ok -->
```text
inroom_plan(lot, level tree, ctx, start, primary, thr, goals, flags):
  room0 = room_at(start)
  refresh_portal_locks(room0)                       # docs/92; sim thread or snapshot answers
  repeat at most twice:
    cell0 = cell_at(start)
    if not walkable(cell0):
      if no_nudge: return 1
      cell0 = nudge(start, room0, ctx)  or return 1   # 64 tries, sin for x, cos for z
    S = start_node(start, fixed) linked to node(cell0)
    G = []; best = -FLT_MAX; bestPt = none
    for k, g in goals:
      c = cell_at(g); n = goal_node(g, k, g.score)
      if c and walkable(c):
        link n to node(c); G.append(n)
        if g.score > best and (room_at(g) == room0 or any_room): best, bestPt = g.score, g.pos
    if no walkable goal or best == -FLT_MAX: code = 4
    else:
      if thr > best or not walkable(cell_at(primary)): T, th = bestPt, best
      else: T, th = primary, thr
      ok, path, gi = astar({S}, T, th, cap(flags))
      code = 0 if ok else 5
    detach S and G
    if code == 0 or retried: return code
    flags |= 0x80000; retried = true
    if walkable(cell0): return code

astar(starts, T, th, cap):
  gen += 1; open = bucket queue; inserted = 0; best = none
  for s in starts by address: open_insert(s, g=0, f=h(s))
  while open not empty:
    n = pop_min(open); closed(n)
    if n is goal and (best is none or n.score > best.score):
      best = n
      if abs(best.score - th) <= 2**-16: break
    if inserted >= cap: break
    if n is cell node and first expansion this generation:
      for c in neighbours(cell(n)) in side order -z,+x,+z,-x, high end first:
        if node(c) != n: relax(node(c), n)
    for m in n.explicit_neighbours in order, except n.parent: relax(m, n)
  return best is not none, chain(best .. first start) reversed, best.index
```

## 12. Mapping to the docs/93 contract

| Contract item | What decides it here | Exact match needs |
|---|---|---|
| Status | §9 codes 1/2/4/5 and the retry rule | the class model (§5) bit for bit, the nudge, the goal filter |
| Chosen goal | best = strictly higher score in pop order; stop at threshold ± 2^-16 | f order with the tie rules of §8.3 and the open-state quirk of §8.2 |
| Start | always the wrapper's start point; the nudge only picks the cell | — |
| Subpath signature | in-room nodes are types 6/0/7 only, flags 0 until §10.2 | §10 post-passes |
| Geometry | entry points of §7.1 (straight-line projection onto shared edges, 0.01 inside) | same decomposition (§3.2) and neighbour order (§6.2); a different cell split gives a different polyline |
| Length | g of the last node is cost, not length; length comes from the builder (docs/93 §4.3) | — |
| Shadow `G-tie` | two goals of equal score reached at equal f | tie rule §8.3 |

Effort (Claude with Rodney): the search, cell model and wrappers as specified, with an offline A/B against the game's functions on dumped trees, about 4-6 days; the footprint class slots (§5.1) are the main risk because they read agent options and per-footprint lists that the snapshot must carry.

## 13. Remaining unknowns and the check that closes each

| # | Unknown | Why it matters | Check |
|---|---|---|---|
| U1 | Root-border mirror (§6.3): do real trees have walkable final cells on the root border? | spurious neighbours change paths and costs near lot edges | offline harness (docs/94 §4.4): dump each level tree after load, run the game's `0x66B9C0` on every border cell, compare with true adjacency |
| U2 | `0xF7EDF6` is an import thunk (IAT `0xF956AC`); with the MSVC `_CIpow` convention (base loaded first, here 2.0, then the exponent r) the radius is 2^r·0.5, i.e. 1 to 128 m, for agent classes 0x4100-0x4300 | nudge radius for those agents | read the IAT name at `0xF956AC`, or run it in `tools/emu` with 2.0 and 3.0 |
| U3 | Allow/deny lists of `fp+0x20` (`0x946E70`/`0x946E90` -> `0x81CEF0`) and the option bits by name | class 6 vs 0 for gated footprints | read `0x81CEF0` and the C# `RouteOption` enum (docs/91 part B list) |
| U4 | `bitOfShape` table `0xFEDE48` beyond shape 23 and the producer of entry masks | masked sub-cell class | read the table to 85 entries and the mask writer in `LotQuadTree_ApplyInsert 0x665C50` |
| U5 | Footprint rectangle tests `+0x34` (`0x80F0D0` -> `0x67CBF0`, polygon cache docs/82) | type gate (§7.1) and clearance (§5.3) | offline: call the game's slot on recorded footprints vs our polygon test |
| U6 | Callers' flag words: which plans set `0x1-0x8`, `0x10`, `0x20`, `0x100`, `0x100000`, `0x200000` | the class model depends on them | count per flag value at `LotPlan_Inner` entry (read-set probe, docs/91 §6) |
| U7 | Is `X+0x4C` ever non-zero for in-room searches (other `0x633FA0` callers)? | §5.3 step 1 else-branch | trace writers of `+0x4C` on the context in `0x633FA0` and `RoomGraph_FirstUse` |

None of U1-U7 changes the algorithm's structure; each fixes a constant, a table or an input field.

## 14. Names and ledger

Appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv` (tag `<docs/98>`): `InRoomPlanToPoint 0x632630`, `InRoom_RefreshPortalLocks 0x632460`, `InRoom_EmitSearchNodes 0x62A470`, `LotPlanCtx_Init 0x6298F0`, `LotPath_ToWorldAndEdgeFlags 0x62F140`, `LotPath_InsertInternalPortals 0x62FBE0`, `Lot_RoomIdAtPoint 0x624780`, `InRoomHeuristic_Euclid 0x61ECD0`, `AStarGoalNode_Cost 0x61AD90`, `AStarGraphNode_Cost 0x61AF80`, `AStarGoalNode_Reset 0x61ACD0`, `AStarNode_Reset 0x61B170`, `AStarNode_Detach 0x61B440`, `AStarTerminalNode_Detach 0x61EC90`, `AStarNode_AddNeighbor 0x61B740`, `AStarNode_RemoveNeighbor 0x61B6D0`, `AStarOpen_Insert 0x61DF50`, `AStarOpen_Update 0x61DFB0`, `AStarBucket_InsertSorted 0x61B880`, `AStarBucket_MoveUp 0x61B910`, `AStarOpen_Remove 0x61BC90`, `AStarOpen_AdvanceMin 0x61BAF0`, `AStarOpen_Grow 0x62BD60`, `AStarSearch_ExtractPath 0x61DD40`, `QuadCell_EdgeCost 0x66C0C0`, `QuadCell_SegmentCost 0x66BCE0`, `QuadCell_Multiplier 0x66B670`, `QuadCell_Class 0x66B600`, `QuadCell_Neighbours 0x66B9C0`, `QuadCell_ClimbToSideAncestor 0x66B8D0`, `QuadCell_MirrorDescend 0x66AC60`, `QuadCell_CollectSideCells 0x66B6E0`, `QuadCell_ExpandNeighbours 0x66BFD0`, `QuadCell_InitNodeAtCentre 0x66BC30`, `QuadCell_Key 0x669FF0`, `AStarCellTable_Find 0x664370`, `QuadCellPos_Parent 0x66A220`, `QuadCellPos_Set 0x669C30`, `QuadCellPos_ctor 0x669BE0`, `LotQuadTree_CellAtPoint 0x663900`, `QuadLeaf_ClassFromEntries 0x6681E0`, `QuadNode_ClassFromChildren 0x668C40`, `QuadLeaf_ClassFromMaskedEntries 0x667DB0`, `QuadLeaf_CountBlockingTypes 0x669530`.

Ledger rows `I-98-1` .. `I-98-12` in docs/51 §31.

## 15. Offline checks (U1-U5)

Added 2026-10-18. No game, probe server or test queue. Tool: `native/ts3native/inroom-test.cpp` (standalone, build and run lines in its header). Like `steerro-test` it maps the Steam 1.67 `TS3W.exe` at its own addresses in a 32-bit process and calls the game's own functions on constructed inputs, on the real CPU. Stand-ins: the GeneralAllocator entry points and the four routing pools (`0x66BB50` cells, `0x61AE80` graph nodes, `0x667920` leaves, `0x667990` internal nodes) allocate from the CRT heap; the IAT slot of `_CIpow` points at the static CRT's `_CIpow` (same x87 convention); two capture jumps sit inside `InRoomPlan` for U2. Every other instruction is the game's. Default run (300 random trees, seed 12345) and a second run (1,000 trees, seed 777): `PASS`, 0 failed checks. **[verified]** below = shown by these runs; **[inferred]** = reasoning, with a ledger row in docs/51 §41.

### 15.1 U1: neighbours at the quadtree root border (`QuadCell_Neighbours 0x66B9C0`)

Setup: random level trees (side 64, internal depth 1-2, leaves split into sub-cells down to eighths by random final masks, empty quadrants included), every final cell found through `LotQuadTree_CellAtPoint 0x663900`, then the game's neighbour list for each cell compared, entry by entry and in order, with a geometric model. 300 trees: 21,189 cells, 98,182 list entries; 1,000 trees: 69,424 cells, 323,133 entries; **0 lists differ from the model** [verified].

The model that matches every list:

| Cell | Side d list |
|---|---|
| no parent node and shape 0 (the root as one whole leaf) | empty: `0x66B9C0` returns before the side loop (`0x66B9DE`..`0x66B9E8`). Correction to §6.2 step 1, which said the cell lists itself |
| not on the border of side d of its box | every final cell sharing a positive-length edge on side d, high end first (x descending for −z/+z, z descending for +x/−x) |
| on the border of side d of its box | **wrap:** every final cell on the box's *opposite* border whose span along the side overlaps the cell's span, same order |

The box is the root square, except for sub-cells of a root that is a leaf: there it is the cell's parent shape, because the climb (`0x66B8D0`) continues only while the current position has a parent node, so inside a root leaf it stops after one shape level. Example from the run, 2 × 2 tree of side 64: cell [0,32]×[0,32] gets, in order, [0,32]×[32,64] (wrap, its −z entry), [32,64]×[0,32] (+x), [0,32]×[32,64] (+z), [32,64]×[0,32] (wrap, its −x entry). Every root-border cell had wrap entries (5,165 and 17,290 cells); 1,006 and 4,040 wrap entries were also a true neighbour on the other side (the duplicate case of §6.3).

So §6.3 is confirmed and sharpened: the root border is treated as a torus, not as a source of arbitrary far cells. A replacement copies it with the rule above (I-98-13, I-98-14).

When it can matter in play. `LotQuadTree_SetBounds 0x6646F0`, called as `LotRouting_QuadTreeBuild` calls it (lot rectangle grown by c on each side, cell size c, where c is the planner's `+0xB8`, docs/101 #14), gives [verified for these inputs]:

| Lot (lot-local, from 0) | c | Bounds x, z | Root side | Levels `+0x40` | +x / +z root border on the bounds edge |
|---|---|---|---|---|---|
| 20 × 30 | 0.5 or 1 | −1..21, −1..31 | 32 | 7 / 6 | no / yes |
| 30 × 30 | 0.5 or 1 | −1..31, −1..31 | 32 | 7 / 6 | yes / yes |
| 40 × 40, 50 × 50, 60 × 60 | 0.5 or 1 | −1..w+1 | 64 | 8 / 7 | no / no |
| 64 × 64 | 0.5 or 1 | −1..65 | 128 | 9 / 8 | no / no |
| 30 × 62 | 0.5 or 1 | −1..31, −1..63 | 64 | 8 / 7 | no / yes |
| 62 × 62 | 0.5 or 1 | −1..63 | 64 | 8 / 7 | yes / yes |

Bounds are rounded outward to whole units; the root starts at the bounds minimum and its side is c·2^(levels−1) ≥ the larger extent; empty space has class 12 (blocked, `+0x2C`). So the −x and −z root borders are always the outer edge of the margin ring about 1 m outside the lot, and the +x/+z borders lie on the bounds edge only when the root side equals the extent; otherwise they lie in empty space beyond the bounds, which no point lookup reaches and whose empty cells are blocked. A wrap edge is relaxed only when a walkable cell on a root border is expanded and the opposite-border cell is walkable too; its cost is the straight distance between the two entry points times the multipliers (§7.1), so it is never cheaper than a clear straight walk but ignores everything between the two borders: a path could jump across the lot through walls [inferred]. Whether real lots have walkable cells on both opposite margin borders is the one part of U1 that needs a live read (I-98-15: dump the border cells of each level tree after load). Sub-cells appear only in leaves at the deepest level (§15.4), so a root that is a split leaf needs a one-level tree, which lots do not have (6-9 levels above) [inferred]; the parent-shape wrap is then out of play.

### 15.2 U2: nudge radius

The import behind thunk `0xF7EDF6` (IAT `0xF956AC`) is `MSVCR80!_CIpow` [verified, import table]. The harness ran the game's own bytes: the class test `0x632F18..0x632F4D` with a fake context, and the radius code `0x632F91..0x633067` for every i and both flag values [verified]:

| Agent class word (options `+0x00`) | Big-nudge flag |
|---|---|
| `& 0xCF00` = `0x4100`, `0x4200`, `0x4300` (also `0x7100`: bits outside `0xCF00` ignored) | 1 |
| `0x4000`, `0x4400`, `0x4500`, `0x0100`, `0x0300`, `0x0800`, `0x0B00`, `0xC100`, no route options | 0 |

| Ring r (candidates i = 8(r−1) .. 8r−1) | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Radius, flag 0 (r·0.5) | 0.5 | 1 | 1.5 | 2 | 2.5 | 3 | 3.5 | 4 m |
| Radius, flag 1 (2^r·0.5) | 1 | 2 | 4 | 8 | 16 | 32 | 64 | 128 m |

The formula of §9.1 holds. `0x4100`-`0x4300` are the houseboat kinds (docs/100 §4, I-100-5), so a houseboat start in a blocked cell is searched for up to 128 m away. I-98-10's open pow order is closed (I-98-16).

### 15.3 U3: per-footprint and per-agent inputs of the class slots

All rows [verified] by calling the slot on constructed footprints and agents (agent object: `+0x00` class word, `+0x08/+0x0C` own object id, `+0x28` route options; options: `+0x08..+0x0C` sorted vector of 8-byte ids, `+0x20/+0x24` own id again, `+0x28/+0x2C` RouteOption, `+0x30/+0x34` RouteOption2, `+0x38..+0x3C` ignore list). `RoutingWorld_Get` only has to be non-null.

**Allow/deny gate** (footprint `+0x20`; list A at `+0x08..+0x0C`, list B at `+0x1C..+0x20`, 8-byte ids). `0x946E70` = "a list is non-empty". `0x946E90(agent)` returns 1 = **excluded** when A is non-empty and (the agent has no options, or some A id is not held), or when B is non-empty, the agent has options and some B id is held. "Held" (`0x81CEF0`): id ≠ 0, and the id is the agent's own id or is in the sorted id vector (lower bound by high word, then low word). So A = ids the agent must all hold, B = ids it must not hold. Base class slot `0x807090` with the result (byte `+0x34` = 3 in the table):

| Gate | no agent | agent, no options | holds {} | holds {X} | holds {X, Y} | own id = X |
|---|---|---|---|---|---|---|
| none | 3 | 3 | 3 | 3 | 3 | 3 |
| A = {X} | 3 | 6 | 6 | 0 | 0 | 0 |
| A = {X, Y} | 3 | 6 | 6 | 6 | 0 | 6 |
| B = {X} | 3 | 0 | 0 | 6 | 6 | 6 |
| A = {X}, B = {Y} | 3 | 6 | 6 | 0 | 6 | 0 |
| A = {X}, B = {X} | 3 | 6 | 6 | 6 | 6 | 6 |
| A = {0} | 3 | 6 | 6 | 6 | 6 | 6 |
| B = {0} | 3 | 0 | 0 | 0 | 0 | 0 |

Correction to §5.1: with a gate the class is 6 when the agent is excluded and 0 when it is admitted (the byte is not used); without an agent the gate is not consulted at all and the byte is the class. The ignore list (checked first) gives 0 in every case. Multiplier `0x806F70`: 1.0 for the agent's own object, else `+0x1C`, also without an agent.

**Other classes:**

| Class (outer vtable) | Rule found |
|---|---|
| `0xFEBBC0` object footprints, `0x80F840` / `0x80F7D0` | class = byte and mult = `+0x1C` only when the agent's id equals `+0x40/+0x44` and RouteOption `IgnorePrivateFootprints` is clear; otherwise class 0, mult 1.0; no agent: 0 and 1.0. These footprints block one agent only |
| `0xFEB848`, `0x80C300` | class 0 when (`+0x41` bit 1, agent class `0x300`, `PreventCatsRoutingThroughPlants` clear) or (bit 2 and `RouteAsFlier`) or (bits 4 and 8 both set); else by sub-index 0 → 8, 1 → byte, 2 → 8, 3 → 0. Bit 4 or bit 8 alone changes nothing |
| `0xFEB848`, `0xFEB970` multiplier `0x80C3D0` | 1.0 for every sub-index with bit 2 and `RouteAsFlier`; else 1.0 for sub-index 0 and 2, `+0x1C` for 1 and 3 |
| `0xFEB970`, `0x8066C0` | 0 for the agent's own object, else the byte |
| `0xFEBD10`, `0x807B70` / `0x807C20` | types 0x1A, 0x1B: class 0 with `EnablePondPlanning`, else base; type 0x1E: class 10 and mult FLT_MAX unless `EnableWaterPlanning` or RouteOption2 `EnablePlanningAsBoat`, then base; other types: base |

**Option bits by name.** `RoutingOptions_Test(lo, hi, which)` tests RouteOption (`which` 0) or RouteOption2 (1) (docs/92 §6); the arguments at each call site [verified, push order] with the names of the C# enums `RouteOption` / `RouteOption2` (SimIFace):

| Test in | Word, bits | Name |
|---|---|---|
| `0x80F840`, `0x80F7D0` | RouteOption hi `0x01000000` (bit 56) | `IgnorePrivateFootprints` |
| `0x80C300` | RouteOption hi `0x00010000` (bit 48) | `PreventCatsRoutingThroughPlants` |
| `0x80C300`, `0x80C3D0` | RouteOption hi `0x00020000` (bit 49) | `RouteAsFlier` |
| `0x807B70`, `0x807C20` | RouteOption hi `0x10000000` (bit 60) | `EnableWaterPlanning` |
| `0x807B70` | RouteOption hi `0x20000000` (bit 61) | `EnablePondPlanning` |
| `0x807B70`, `0x807C20` | RouteOption2 lo `2` | `EnablePlanningAsBoat` |
| `0x6681E0`, `0x667DB0`, `0x669530`, `0x669850` (type 0x14 rule, §5.2) | RouteOption lo `0x1004`, either bit | `BlockedByPeople` (4), `BlockedByDynamicFootprints` (0x1000) |

Agent class `0x300` in `0x80C300` is the cat class by the option's name [inferred].

**What a snapshot must keep per footprint and per agent** (the inputs these slots read): footprint class byte `+0x34`, type `+0x35`, flags dword `+0x34` (bit `0x2000000`), multiplier `+0x1C`, grow `+0x30`, whether the list at `+0x24` is empty, object id `+0x14/+0x18`, the one-agent id `+0x40/+0x44` (`0xFEBBC0`), bits `+0x41` (`0xFEB848`, `0xFEB970`), the gate's two id lists, the ignore byte `+0x48` (`0xFEAE08`), the polygon parts (§15.5) or the box `+0x38..+0x44` (`0xFEAE08`); per agent the class word, own id, both option words, the held-id vector and the ignore list.

### 15.4 U4: sub-cell masks and their writer

All [verified]:

- **`bitOfShape` (`0xFEDE48`), all 85 entries:** shapes 21-84 → bits 0-63 (eighth = 8·row + column, row along z, column along x); shapes 0-20 → the lowest eighth of the shape (0, 0, 4, 32, 36, then 0, 2, 4, 6, 16, 18, 20, 22, 32, 34, 36, 38, 48, 50, 52, 54).
- **Shape masks** `0x11CF180` (21 × 64 bits, built once by `0x667740`, guard byte `0x11CF428`): the eighths each shape covers, e.g. shape 1 `0x0F0F0F0F`, shape 4 `0xF0F0F0F000000000`, shape 5 `0x0303`; all 21 equal the squares from the shape tables of §3.2.
- **Writer of an entry's mask:** `0x662FF0(cell, entry, mask*)`, called by the insert `0x6653A0` when the node is at the deepest level (depth = tree `+0x40` − 1). Bit i is the footprint's rectangle slot `+0x34` on eighth i of the leaf with grow 0, part index = entry byte `+0x10` (400 random polygons: every bit equals the slot; no bit set for an eighth the polygon only touches). Its first test (slot `+0x30`, `0x806FA0` in all nine classes) always returns false. Above the deepest level a footprint that contains the node (slot `+0x38`) gets all ones, otherwise the node is split [static reading of `0x6653A0`].
- **Final bits:** a new leaf has all 21 set (`0x667B10` writes `0xFFFFF8`); `0x667BA0(leaf)(entry, mask*)` appends `{entry, mask}` and clears the bit of shape s exactly when the new mask covers s partly (2,000 random leaves, 4,933 adds, 0 differences).
- Consequence: for every shape that stays final, each entry's mask is all or nothing on it, so the masked class rule of §5.2 (one bit, `bitOfShape[s]`) is exact (checked on every case). A snapshot can keep the per-entry 64-bit masks and the 21 final bits, or rebuild both from the footprint polygons with the slot rule of §15.5.

### 15.5 U5: footprint rectangle tests

All [verified]; 20,000 random cases per test and run (convex polygons in both windings, L-shapes, boxes; one case in four built to touch the polygon's bounding box), compared with exact clipping:

| Slot | Classes | Answer |
|---|---|---|
| `+0x34` `0x80F0D0`(part, minX, minZ, maxX, maxZ, grow) | all except `0xFEAE08` | true iff the rectangle overlaps some polygon of the part with positive area. Edge or corner contact without area is false (the bounding-box pre-test is strict). Part = index into the polygon cache `+0x38` (0x24 bytes per part, rebuilt by `0x80E5B0` when invalid). Then `0x67CBF0` on the global scratch `0x1165D28` (shared state, docs/101) |
| `+0x38` `0x80F200` | same | true iff one polygon contains the whole rectangle: two squares that tile a rectangle do not contain a rectangle across their seam |
| grow, both slots | same | only with flag `0x2000000` in dword `+0x34`; then `+0x30` when the list at `+0x24` is empty (the argument is ignored), else the argument; without the flag no grow at all (`0x8072F0`, `0x807310`) |
| `+0x34` `0x80B4F0` | `0xFEAE08` (box `+0x38..+0x44`) | per axis min(max) − max(min) + grow > 0; strict, grow counted once, part ignored, no flag rule (20,000 of 20,000) |
| `+0x38` `0x80B1C0` | `0xFEAE08` | rectangle inside the box grown by grow on every side |

The entry byte `+0x10` that §3.1 calls the sub-index is the polygon part index: the same byte goes to the class and multiplier slots and to these tests [verified at `0x6631AD`]. Not run here: the callers `0x6685B0`/`0x668DB0` (type gate, §7.1) and `0x66A380` (clearance, §5.3), which pass these slots their rectangles.

### 15.6 Still open

- U1 residual: do real level trees have walkable cells on two opposite root borders (I-98-15)? One read of each level tree after load; until then a replacement copies the wrap rule.
- c (planner `+0xB8`) and the exact lot rectangle passed to `0x6646F0` (lot `+0x84` slot `+0x2C`) were not read; the table in §15.1 assumes lot-local rectangles from 0 and c = 0.5 or 1. **Closed in §15.7:** c = 1, rectangle from (−0.5, −0.5) (dive lots from 0), bounds from (−2, −2); the §15.1 size table is replaced there.
- U6 and U7 are unchanged (live counts).
- All four have a read-only live probe and a queue since 2026-10-18: docs/111 (`tools/harness/queues/live-reads.json`, ledger docs/51 §47).

### 15.7 I-98-21 closed: rectangle and c at `LotQuadTree_SetBounds 0x6646F0`

Added 2026-10-18. Static reading of the Steam 1.67 exe (capstone), the bounds chain run on the game's own bytes under `tools/emu`, and a cross-check against the tree bounds dumped by the 2026-10-18 `live-reads` queue (docs/111; BP and IP both had 0 SetBounds calls in their 5-minute windows; dumps of 280 BP and 480 IP trees). No game run for this section. Ledger: docs/51 §61.

**One caller** [verified]. `0x6646F0` has one direct call (`0x62FA88` in `LotRouting_QuadTreeBuild 0x62F8A0`) and no address reference. The tree constructor `0x664650` also has one call (`0x62F98E`, same function). Every tree is created and sized there.

**What is passed** [verified, disassembly `0x62F9A1..0x62FA88`, `0x6646F0..0x6649BD`, `0x80B3C0`, `0x620E90`]:

| Step | Detail |
|---|---|
| Rectangle source | lot graph (`0x62EAB0` result) `+0x84` = the lot footprint, stored by `0x630CF0` (callers `RoutingSystem_RebuildLotRouting 0x835110`, `RoutingWorld_RemoveLot 0x829D50`, and `0x83575D` in the `RoutingWorld_OnMessage` range). It is a `0x4C`-byte box footprint, vtable `0xFEAE08`, built by `0x80B3C0(lot)`. Slot `+0x2C` = `0x620E90(rect*, 0, 0.0)` copies the box `+0x38..+0x44` |
| Box | min x, z = −0.0 − m; max x = m + lot `+0x174` (x tiles), max z = m + lot `+0x178` (z tiles): **lot-local, origin at the lot corner**. m = 0.5 (`.rdata 0x1020800`), except m = 0 when lot `+0x408` = 1 and `+0x40C` = `0x50`: `LotType` Commercial, subtype 80 = `kEP10_Diving` (enum index in `CommercialLotSubType`; field meanings docs/100) |
| Grow | each side by the static `0x11CF02C` (cached from P `+0xB8` on the first call, docs/101 #14): min − c, max + c |
| c passed | `fld [P+0xB8]`, read directly, as the 6th argument |
| P `+0xB8` | two writers: the planner constructor (0.125, `0x62E93D`) and `0x637650`, which stores its 4th argument × 8 (`0x637666`). Its one caller, `0x829F10` (routing world setup), passes the constant 0.125 (`0xFE8278`). So **c = 1.0** in 1.67, never 0.5. The static would cache 0.125 if a tree were built before `0x829F10` runs; live it reads 1.0 (docs/111 dumps) |
| Call shape | thiscall(tree)(owner P, minX, minZ, maxX, maxZ, c, level, P `+0xBC`, P `+0xC8`), `ret 0x24`. Inside: owner to `+0x20`; min floored, max ceiled to whole units (`+0x04..+0x10`); class 12 to `+0x2C`; FLT_MAX to `+0x30`; c to `+0x3C`; levels = max over the two axes of ceil(log2(ceil(extent / c))) + 1 to `+0x40`; root side c·2^(levels−1) to `+0x38` |

So the lot rectangle is lot-local from (−0.5, −0.5) for ordinary lots and from (0, 0) for dive lots, and the tree bounds are (−2, −2)..(w + 2, d + 2) for ordinary lots and (−1, −1)..(w + 1, d + 1) for dive lots. The −2 is **two cells at c = 1**: the lot footprint's own 0.5 m skirt, plus the grow by c, rounded outward [verified].

**Emulated and matched to the live dumps** [verified]. The chain `0x80B3C0` -> `0x620E90` -> grow -> `0x6646F0` on fake lots under `tools/emu` (scratch script, not committed) reproduces all 89 distinct (bounds, root side, levels) triples of the BP and IP dumps with 0 mismatches. The four IP trees at (−1, −1, 65, 65) are the dive case (64 × 64, levels 0 and 1); every other tree starts at (−2, −2). Corrected size table (c = 1; replaces the §15.1 table):

| Lot (tiles) | Kind | Bounds x, z | Root side | Levels `+0x40` | +x / +z root border on the bounds edge |
|---|---|---|---|---|---|
| 20 × 20 | ordinary | −2..22 | 32 | 6 | no / no |
| 20 × 30 | ordinary | −2..22, −2..32 | 64 | 7 | no / no |
| 28 × 28 | ordinary | −2..30 | 32 | 6 | yes / yes |
| 30 × 30, 40 × 40, 50 × 50 | ordinary | −2..w+2 | 64 | 7 | no / no |
| 60 × 60 | ordinary | −2..62 | 64 | 7 | yes / yes (16 BP trees) |
| 60 × 30, 50 × 60 | ordinary | −2..62 on the 60 side | 64 | 7 | yes on the 60 side only |
| 62 × 62, 64 × 64 | ordinary | −2..64, −2..66 | 128 | 8 | no / no |
| 30 × 30, 62 × 62 | dive | −1..31, −1..63 | 32, 64 | 6, 7 | yes / yes |
| 64 × 64 | dive | −1..65 | 128 | 8 | no / no (IP, 4 trees) |

Rule: the +x (+z) root border lies on the bounds edge when w + 4 (dive: w + 2) is a power of two; otherwise it is in the empty class-12 space beyond the bounds. The −x/−z borders are always the outer edge of the two-cell margin (1.5 m outside the lot footprint box, 2 m outside the lot tiles). §15.1's wrap reasoning stands; only the sizes move: ordinary lots with both opposite borders on the bounds edge are 28 and 60 wide (dive lots 30 and 62), not 30 and 62. docs/111 §3.1 ("30 × 30, 62 × 62") should be read that way.

**When trees are built, and why the window saw none** [verified call graph; the event order at load inferred]:

- `0x62F8A0` is get-or-create: an existing tree at the level is returned at `0x62F901` with no SetBounds; a new one needs the create byte (5th argument) and a lot footprint at `+0x84`. The live `qtb` rows (200 per job, BP and IP) all had create = 1 and found a tree.
- Creators (create = 1): `PathPlanner_AddFootprint 0x6376D0` (20 call sites: floor, wall and object footprint inserts, including `RoutingSystem_RebuildLotRouting`'s), `LotGraph_Build 0x63CB90` (lot first use, and `LotGraph_RebuildRooms 0x63D7B0`), the lot-load path `0x63C8E0` (from `RoutingWorld_LoadLotData 0x82CA10`; it also calls `LotGraph_Reset`), and two sites `0x63A0FD`, `0x63AA23` (functions not named). `RoutingWorld_UpdateLotWalls 0x830940` passes create = 0; `RoutingDebug_DisplayQuadtree` passes a variable.
- Destroyers (`0x665A50`): `LotGraph_Reset 0x62E420` (drops the lot entry and every level tree; from `RoutingWorld_RemoveLot`, the lot-load path, and branches of `LotRouting_SaveLevels 0x63C690` / `0x63C620` not read in full), `PathPlanner_Reset 0x635B70` (all lots), and the tree destructor `0x666350`. `RoutingWorld_RebuildLot 0x82B810` (build/buy and save rebuilds) destroys none; it can create only through `LotGraph_RebuildRooms` -> `LotGraph_Build`, for a level that has no tree.
- So a level tree is sized when its lot is loaded (all lots at world load, before the harness attaches) or when a level gets its first footprint, and lives until the lot is removed or reloaded. Build/buy exit rebuilds rooms and footprints into the existing trees and calls SetBounds only for a level that had none. Lot size does not change in live mode, so stale bounds are not a case [inferred].

**Probe defect** [verified against the probe source]: `tools/frida/inroom-reads.js` reads SetBounds' arguments as `args[0..4]` = minX, minZ, maxX, maxZ, c and `args[5..7]` as words, as docs/111 §2 describes; the owner P comes first, so the floats are `args[1..5]` and the words `args[6..8]`. The tree fields it reads on return (`+0x04..+0x10`, `+0x38`, `+0x40`) are right. Its `lotRect` and the `inroom-report.ts` I-98-21 verdict would be wrong on the first live call, and the dummy stub in `tools/probe/inroom-reads-test.py` has the same wrong shape. Fix both before the confirmation run.

**Live confirmation route** (after that fix): `ip-houseboat-create` in `tools/harness/repros.json`: `IC_World_CreateHouseboatLotImpl` -> `RoutingSystem_RebuildLotRouting` -> new lot entry -> first footprint per level = SetBounds calls for a new lot. Expected per call: owner = P, rectangle min (−1.5, −1.5), c = 1, bounds min (−2, −2) [inferred: a houseboat lot is not a dive lot]. `bp-build-levels` calls SetBounds only if its second-floor walls land on a level with no tree yet, so it is the weaker route. A third route is attaching the probe before the save loads, so every lot passes through `0x63C8E0`.
