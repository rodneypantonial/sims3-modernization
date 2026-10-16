# 72. Why ~40% of sim frames never reach the renderer (PLAN gap 7)

PLAN.md "Gaps" item 7. The 2026-10-15 verify run counted 52,454 sim frames published against 31,657 consumes on the main thread and read that as "~40% of sim frames are merged or dropped" (docs/data/results/2026-10-15-verify-findings.md, ledger I-41-4). This doc explains the mechanism from existing data and a static read of the FrameDatabase functions. No game run was made.

Tags: [verified] = read in the decompile (Ghidra, `game-copy/`, not copied here); [measured] = counters or samples from the runs named below; [inferred] = reasoning from both, not checked live.

## 1. Answer

**By design, not lost work and not a throttle.** The sim thread publishes one frame snapshot per sim iteration; the main thread consumes once per main-loop iteration (one rendered frame) and always interpolates between the **two newest** published frames. When the sim runs more iterations per second than the main thread renders, the older snapshots are overwritten unseen. Nothing they carried is lost: every render message (transform updates, scene adds) goes through a separate message buffer that the main thread drains in full. What the renderer skips is only the per-frame interpolation snapshot (the frame header and per-object records), and those are superseded by a newer frame anyway.

- In the 2026-10-16 verify windows the sim published ~61 frames/s and the main loop ran 37-48 iterations/s, so 1.28-1.64 frames were published per consume, and 22-39% of snapshots were never drawn [measured]. The "~40%" is this ratio, measured with heavy instrumentation on the main thread (§3).
- The 10 FPS loading cap never fired (0 hits in both windows) [measured], so no game throttle is involved.
- The docs/41 §5 figure "~8 sim frames per render frame at 480 TPS" is **refuted**: S3SS's 480 TPS is a cap that the sim never reaches; it runs 34-90 iterations/s (median 34 BP, 52 IP) because each iteration costs 10-30 ms of real work [measured].

**Fix-worthy?** Not as "dropped frames": there is nothing to recover. Two side effects are real and already on the candidate list:
- The main thread runs every render message of every sim frame, including frames it never draws (233 messages per published frame in BP, 144 in IP) [measured]. Coalescing superseded transform messages per object is a possible main-thread saving (§4, C1) [inferred].
- The visible-stutter part of this design is docs/41 P1 (render clock clamps when the sim is late, motion freezes, then jumps), not the skipped snapshots. Long sim iterations (per-second max 80-94 ms median) are what the player sees [measured max, inferred link].

Offload value: none for this mechanism itself. It already decouples sim and render; the sim only waits on the frame lock for 0.4-0.7% of its time (§3).

## 2. Mechanism (static) [verified]

FrameDatabase object at `[0x11CDD2C]` (read by `0x5934C0`), fields as in docs/41 §1:

| Step | Function | Thread | What it does |
|---|---|---|---|
| Frame begin | `SimRender_TripleBufferSync 0x593590` | sim | Under lock `+0x48`: if more than 2 frames are queued, normal mode drops the oldest (read index `+0x68` + 1, count `+0x70` - 1) and returns the write slot; fixed-step mode (`+0xFC` set) returns 0 instead, so the sim skips the publish |
| Header | `0x76C8A0` (called at `0xD81DF5`, `0xD81E9E`) | sim | Clears the write slot (`0x5933C0`), writes time, dt, world values and callbacks, marks it ready (`slot+0xC`) |
| Publish | `FrameDb_PublishFrame 0x593600` | sim | Under lock: write index + 1 mod 3, count + 1, cap 2 (3 in fixed-step mode); above the cap the read index moves so that the newest 2 stay queued |
| Messages | `RenderMessageBuffer_Post 0x593E60` | sim | Appends render messages to the back list under its own lock `+0xD0`, independent of the frame ring |
| Consume | `FrameDb_ConsumeAndInterpolate 0x693780` | main | Returns at once if `+0xF8` is set or `TryEnterCriticalSection(+0x48)` fails; returns without drawing if the newer slot is not ready. Else reads the pair at the read index (`0x593440`), advances the render clock (`0x693660`), steps to the next pair only through `0x593480`, which needs 3 or more queued frames (so only in fixed-step mode), then drains messages, sets the lerped render time and interpolates every object from the two slots |
| Drain | `FrameDb_DrainEvents 0x5937B0` | main | Swaps front/back lists when the front is empty and runs every message in order; a frame marker newer than the render time stops the walk, the rest waits for the next consume. Nothing is discarded |
| Render clock | `FrameDb_AdvanceRenderClock 0x693660` | main | Render time += real frame dt, clamped to `[tA, tB]` of the pair; at `tB` it sets the "late" flag (`+8`): motion holds on the newest frame until the sim publishes again (docs/41 P1) |

Consequences:
- In normal mode the queue never holds more than 2 published frames, and the renderer always interpolates between them. A third publish before the next consume overwrites the oldest one [verified]. With 1.3-1.6 publishes per consume, this happens on most render frames.
- The display runs about one sim frame behind the newest sim state (interpolating towards the newest frame, never past it) [verified clamp; latency inferred].
- Every message is applied, so the scene state the main thread draws is always complete; only intermediate poses between two drawn frames are not shown [verified drain; visual effect inferred].
- The 2-frame cap makes the sim never wait for a slow renderer except for the lock itself [verified, as docs/41 S3].

## 3. Measurements

Sources: `probe-out/queue/verify-2026-10-16-{bp,ip}-verify-native.json` (after minus before, the play window after the load), `probe-out/profile/verify-2026-10-16-*-480-seconds.csv`, `probe-out/profile/s3pp-cache-2026-10-16-bp-s3pp-cache-{on,off}-simstack.jsonl`. Windows: BP 16:45-16:53 PHT (490 s, window not focused), IP 16:55-17:03 PHT (486 s, focused at start). Both ran the verify build (`20af6f323fff`) with every `[Verify]` group on and the main-thread sampler.

| | BP | IP |
|---|---|---|
| Frames published on the sim thread (`vfCostFrameDbPublished`) | 30,131 (61.5/s) | 29,798 (61.3/s) |
| Consume calls on main (`vfCost693780Calls`) | 23,486 (47.9/s) | 18,186 (37.4/s) |
| Main-loop iterations (`vfCostECAA65Loops`) | 23,486 | 18,186 |
| 10 FPS loading-cap hits (`vfCostECAA65Calls`) | 0 | 0 |
| Published per consume | 1.28 | 1.64 |
| Snapshots never drawn, lower bound | 22% | 39% |
| Sim iterations/s (seconds CSV, mean / median) | 67 / 34 | 89 / 52 |
| Published per sim iteration (approx., windows differ by a few s) | ~1.0 | ~0.75 |
| Scene message handler `0x703CB0` on main | 7.03M calls, 9.45 s | 4.28M calls, 5.50 s |
| Messages per published frame / per consume | 233 / 299 | 144 / 235 |
| Time in consume (`vfCost693780Us`), mean / max | 32.4 s, 1.38 ms / 13.0 ms | 20.0 s, 1.10 ms / 21.4 ms |
| Sim waits on the frame lock (`0x593590` + `0x593600`), total / max | 2.94 s (0.6%) / 11.2 ms | 2.07 s (0.4%) / 10.4 ms |
| Seconds-CSV `publish` stage (`0x593590`), mean ms/s | 6.0 | 4.6 |

[measured] all rows. Notes:
- **Consume = render frame.** Consume calls equal the main-loop count exactly in both worlds, so the consume runs once per main-loop iteration, which presents once (tools/harness/frame-times.js header) [measured + verified]. The counter counts calls, including early returns (lock busy, slot not ready), so the share of snapshots never drawn is at least the figure above.
- **The ratio depends on how fast main renders**, not on a sim setting. BP (unfocused) rendered faster than IP (focused), so focus and the unfocused `Sleep(10)` do not explain the gap here; IP's heavier scene does [measured rates, inferred cause]. The verify build hooks `0x703CB0`, which runs ~14k times/s on main, plus the main-thread sampler, so main is slower than in normal play and the 22-39% is an upper-side figure for normal play [inferred].
- **2026-10-15 run:** 52,454 / 31,657 = 1.66 published per consume (both worlds), the source of the "~40%" [measured, docs/data/results/2026-10-15-verify-findings.md].
- **Sim-side cost of publishing is small.** In the s3pp-cache simstack samples (877 and 882), message posting `0x593E60` is on 2.3-4.1% of stacks (mostly from `0x692F70`), the lock wait in `0x593590` on 1.1-1.2%, and the header writer `0x76C8A0` on 0.2-0.3% [measured samples; partial stacks, docs/20]. The snapshot work that goes unseen is therefore a fraction of a percent of the sim thread [inferred].
- **Time in consume is inflated** by the hooks inside it (the `0x703CB0` probe alone runs ~300 times per consume). docs/71 found the consume at 0.5-0.9% of main samples [measured there]; here 1.0-2.9% of main stacks [measured]. Treat the µs totals as upper bounds.

## 4. Candidates

| # | Candidate | Value | Note |
|---|---|---|---|
| C1 | Coalesce superseded transform messages per object before the drain | Main-thread time in the message handler is 1.1-1.9% of wall time in these runs (inflated by the probe) [measured]; dropping messages for frames that are never drawn could cut part of it | Only for idempotent transform updates; scene adds/removes and callbacks must keep order. Needs the message type map (docs/22, docs/48) first. Low priority [inferred] |
| C2 | docs/41 P2: drain messages after releasing `+0x48` | The sim waits 0.4-0.7% of its time, max 10-11 ms [measured] | Already listed; small gain |
| C3 | docs/41 P1: count render-clock clamps | The visible stutter of this design | Needs a counter on the `+8` flag after `0x693660`; see §5 |
| - | Raise the ring cap or step through all queued frames | None | The renderer would show older states for no benefit; it already draws the newest two |

## 5. Live check plan (not run)

The claims above rest on counters that exist (v22) except for the overflow and clamp counts. The draft queue `tools/harness/queues/dropped-frames.json` (not run) repeats the window in BP and IP **without** the main-thread sampler and with only `Verify.Costs` on, so main runs closer to normal speed. Expected if §1 holds: published per consume falls below the verify figures (fewer unseen snapshots), `vfCostECAA65Calls` stays 0, consume calls still equal `vfCostECAA65Loops`. Rodney places and focuses the game window before the job (game-window rule); the log line "game window focused" must show it.

For a later counter version (reserve in docs/registry.md first; docs/68 rules apply, `0x593600`/`0x693780` already carry probes):
1. Publish overflow: count `0x593600` calls that find the count at the cap (one snapshot overwritten unseen). Expected: published - consumes, within the early-return slack.
2. Consume early returns: lock busy (`TryEnterCriticalSection` failed) and "newer slot not ready". Expected: rare (main never waits; docs/71 found no wait leaves).
3. Clamp hits: consumes where `0x693660` set the late flag (`+8`), next to the sim's per-second max iteration time. Expected: clamp share rises with long iterations (P1).

Ledger rows I-72-1 to I-72-5 in docs/51 §7 track these.
