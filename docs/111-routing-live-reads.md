# 111. Routing in-room live reads (I-98-15, I-98-21, U6, U7)

Written 2026-10-18. Plan and tooling; the game has not been run for this doc. Results go in §5 when the queue has run. Ledger: docs/51 §47 (I-111-1 .. I-111-6).

## 1. What is open and why a live read

docs/98 §15 closed U1-U5 offline. Left open (docs/98 §15.6, §13):

| Item | Question | Why it matters |
|---|---|---|
| I-98-15 (U1 residual) | Does any real lot level tree have a walkable final cell on a root border and another on the opposite border with an overlapping span? | `QuadCell_Neighbours 0x66B9C0` wraps the root border like a torus (I-98-13). A walkable pair would give a cross-lot edge that ignores everything in between; the replacement search would have to copy it |
| I-98-21 | Is the rectangle passed to `LotQuadTree_SetBounds 0x6646F0` lot-local from (0, 0) and grown by c, with c (planner `+0xB8`) 0.5 or 1? | the docs/98 §15.1 root size table assumes it |
| I-101-4 (bonus, same hook) | Is the static `0x11CF02C` equal to `+0xB8` of every object `LotRouting_QuadTreeBuild 0x62F8A0` runs on? | the snapshot can cache c once |
| U6 | Which context flag words `X+0x50` do in-room searches see, per caller? | the class model (docs/98 §5.3) depends on bits `0x1-0x8`, `0x10`, `0x20`, `0x100`, `0x100000`, `0x200000` |
| U7 | Is `X+0x4C` ever non-zero at in-room entry (callers other than `LotPlan_Inner`, e.g. `0x633FA0`)? | docs/98 §5.3 step 1 else-branch |

All five are counts or reads of state the game already has; no native code is needed, so this is a Frida read (ts3native and its counters are left alone).

## 2. The probe: `tools/frida/inroom-reads.js`

Read-only: it never writes game memory and never calls a game function. Every read is guarded; a bad pointer becomes an error entry in the output.

| Hook (prologue bytes checked; a mismatch refuses that hook) | Read | Answers |
|---|---|---|
| `InRoomPlan 0x632E40`, `InRoomPlanToPoint 0x632630` (thiscall on P, X = first stack argument, docs/98 §2) | on entry: `X+0x50`, `X+0x4C`, `X+0x40` (agent or none), the caller by return address (`LotPlan_Inner 0x63F6E0`, `0x633FA0`, `0x635080`, else the raw address); on return: whether `X+0x4C` changed | U6, U7 |
| same, once at the window start and once ~25 s before the end | tree dump (below), inside the hook on the planning thread | I-98-15, bounds part of I-98-21 |
| `LotQuadTree_SetBounds 0x6646F0` (ecx = tree; stack: minX, minZ, maxX, maxZ, c, then three words) | arguments on entry; on return the tree's bounds `+0x04..+0x10`, root side `+0x38`, levels `+0x40`; lot rectangle = arguments shrunk by c | I-98-21 |
| `LotRouting_QuadTreeBuild 0x62F8A0` | ecx, whether it is P (`[[0x11D9128]+0x95C]`), `[ecx+0xB8]` and `[0x11CF02C]` as raw words (first 200 calls) | I-101-4 |

**Tree dump.** Routing world `[0x11D9128]`, planner P `+0x95C`, lot table buckets `P+0x4C`, count `P+0x50`, chain `node+0x190`, lot key `node+0`, lot entry `node+0x10`, level trees in the vector `entry+0x74..+0x78` (docs/60, docs/80 §2, docs/98 §3.1). Per tree: bounds, root side, levels, level, default class and multiplier; then the final cells that touch the root square's border (the walk prunes every child that does not touch it), using the node and shape rules of docs/98 §3.2. Class and multiplier per cell follow docs/98 §5.2 for the plain context (no agent, flags `0x10000`, so §5.3 changes nothing), with the slot rules of docs/98 §5.1 and §15.3 chosen by the slot target address in the footprint's vtable (`0x807090`, `0x80F840`, `0x8066C0`, `0x6119F0`, `0x807B70`, `0x80C300`; multipliers `0x806F70`, `0x620DC0`, `0x80F7D0`, `0x80C3D0`, `0x807C20`). An unknown slot is read as byte `+0x34` / float `+0x1C` and listed. Cells whose entries depend on the agent (gated base footprints, one-agent object footprints) are counted, since a real agent could see them differently. Wrap pairs: a walkable cell on −x and one on +x whose z spans overlap with positive length, and the same for −z / +z.

**Why the dump runs inside the hook.** With `[RouteWorker]` and `[RouteAsync]` off (the jobs set both), every plan and every quadtree write runs on the sim thread. At the entry of an in-room plan no write is in progress, so the walk sees a consistent tree. Cost: one sim-thread stall per dump (all lots, border cells only; expected well under a second). The stack samplers of `profile-world.ts` are off in this mode (they suspend the sim thread).

**Safety (probe-safety rule).** `python tools/probe/inroom-reads-test.py` runs the unchanged script in a 32-bit dummy (`ping.exe`) on a fake routing world with stub functions of the same calling shapes: wrong prologue bytes refuse every hook and the file says so; hooks count on the calling thread; both dumps run there; a bad tree pointer gives an error entry; with no in-room plan for two 30 s checks the run fails with that reason; after the end and after unload the dummy's thread keeps running. PASS on 2026-10-18. `node tools/harness/inroom-report.ts --self-check` runs the walker on fake memory (shape tables, wrap pairs, sub-cell masks, type rules, bad pointer) and the verdict rules.

**Fail-fast.** A progress line every 30 s (`plans, builds, dumps, job N min left`); the start dump needs one in-room plan, and two checks without one end the run with `fail: no in-room plan for 60 s`. `profile-world.ts` kills the Frida process 90 s past its time limit and keeps its own sim-clock guard.

## 3. How to run and read

```
node tools/harness/queue.ts tools/harness/queues/live-reads.json --dry-run
node tools/harness/queue.ts tools/harness/queues/live-reads.json
node tools/harness/inroom-report.ts probe-out/profile/live-reads-<date>-bp-inroom-reads-inroom.json probe-out/profile/live-reads-<date>-ip-inroom-reads-inroom.json
```

Jobs: `bp-inroom-reads` (Baseline-BP-Spring) and `ip-inroom-reads` (Baseline-IP-Spring), asi `tested`, `RouteWorker.Enabled=0`, `RouteAsync.Enabled=0`, profile 5 min seeded 12345 with `inroom: true`; ~8 min each with load and teardown, ~16 min for the queue. Files per job: `<tag>-inroom.json` (all reads) and `<tag>-inroom.log` (the probe's lines).

### 3.1 I-98-15

The report counts trees with wrap pairs (latest dump per tree), border cells and walkable cells per side, agent-dependent border cells and unknown slots. **Expected:** 0 pairs. Reason [inferred]: the −x/−z root borders lie on the margin ring outside the lot (docs/98 §15.1), and the +x/+z borders lie on the bounds edge only when the root side equals the extent, else in empty space (default class 12, blocked). A pair needs both opposite margins walkable, which the margin ring could make possible on lots whose root side equals the bounds (30 × 30, 62 × 62 in the §15.1 table) if the ring cells are empty with a walkable default class. So the deciding fields are `defCls` per tree and the class of empty margin cells. A pair found = I-98-15 refuted; the replacement copies the wrap rule (already modelled, I-98-13).

### 3.2 I-98-21 and I-101-4

**Expected:** lot rectangle minimum (0, 0), c 0.5 or 1, bounds minimum (−1, −1), `+0xB8` = static for every build, ecx = P. SetBounds runs when a lot's routing is (re)built; lots built before the window are covered by the dumped bounds only. If no build happens in 5 min the report says so and falls back to the dumped bounds.

### 3.3 U6 and U7

The report prints, per caller, the flag words with counts, the bits seen, and the `X+0x4C` values. **Expected:** `LotPlan_Inner` rows `0x10000`, some with `0xF` (obstacle mode) or `0x2000000`; `X+0x4C` = 0 everywhere and never changed. Any other bit is listed against the docs/98 §5.3 model; a non-zero `X+0x4C` makes the else-branch live.

## 4. Limits

- Plain context only: an agent can see gated and one-agent footprints differently; the report counts those border cells instead of judging them.
- The class is the probe's copy of docs/98 §5.2, not the game's `0x66B600` (I-111-6). If a pair turns up, re-check those cells with the game's function offline (`native/ts3native/inroom-test.cpp` style) before relying on it.
- Lots enter the lot table lazily; the end dump covers the lots planned on during the 5 minutes, not every lot of the world.
- Two Frida sessions run beside the probe server (stutter profiler and this script); neither hooks the other's functions.

## 5. Results

Runs 2026-10-18 (`queues/live-reads.json`, both PASS, 5 min each, RouteWorker and RouteAsync off). Report: `node tools/harness/inroom-report.ts probe-out/profile/live-reads-2026-10-18-<bp|ip>-inroom-reads-inroom.json`.

| Item | BP (19:35 PHT) | IP (19:44 PHT) | Verdict |
|---|---|---|---|
| I-98-15 wrap pairs | 0 walkable pairs in 330 trees (82 lots); 12,701 border cells, none walkable | 0 in 380 trees (119 lots) | **verified** for the plain context: the replacement need not copy the wrap edge |
| I-98-21 SetBounds rect, c | no SetBounds call in the window; dumped bounds min -2,-2; P+0xB8 = 1 | same; bounds min -2,-2 and -1,-1 | closed statically instead (docs/98 §15.7: lot-local from (-0.5,-0.5), dive lots from 0, c = 1, bounds from -2; trees are sized only on a level's first tree). The probe reads SetBounds args one slot early (owner P is args[0]): fix before any live SetBounds read |
| I-101-4 | ecx = P in all 200 QuadTreeBuild calls; +0xB8 1 = static 1 | same | **verified** |
| U6 option words | 12,062 in-room entries, one thread; per-caller flag words logged | 6,443 entries | recorded (docs/109 §3 bits) |
| U7 X+0x4C | 0 at every entry, never changed by a search | same | **verified**: the §5.3 else-branch is not reached |

| Job | Run | I-98-15 | I-98-21 | I-101-4 | U6 | U7 |
|---|---|---|---|---|---|---|
| bp-inroom-reads | | | | | | |
| ip-inroom-reads | | | | | | |
