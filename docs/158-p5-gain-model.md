# 158. P5 lending gain model: what P5b / P5c can cut from the sim's `+0x918` wait, from measured inputs

Written 2026-10-22. **Static plus existing run data only:** no game run, no build, no queue. Ledger docs/51 §240. Question: before anyone spends clean nights on lending (P5b `ReleaseLock`, P5c `FreeLock`, docs/94 §22.9-22.11), how much sim wait can it cut per world, which inputs are measured, and which cheap probe replaces each inferred one (measure-before-build rule).

Sources: `probe-out/profile/*-ab-native.jsonl` + `*-ab.json` of every run with P5 counters (`snap-p5c-ab-2026-10-20-bp-{first,rep-ab,snap-p5c-ab}`, `-ip-snap-p5c-ab`, `wait-reducers-ab-2026-10-20-bp-wr-*`, `avoidro-2026-10-21-bp-wr-steer-avoid*`) and the release-config clean nights (`clean-night-0.2.0-2026-10-21-{bp,ip,sv}-*`, `clean-night-routing-extra-2026-10-22-bp-*`); `probe-out/queue/snap-p5{b,c}-2026-10-20-{bp,ip}-*-native.json` (whole-run before/after, 10 min); `native/ts3native/{p5,p5b,p5c}.cpp`; docs/94 §22.9-22.11 and §22.29-22.30, docs/136 §2-§3.8, docs/143 §8.14-8.15, docs/150, docs/152. New tool: `tools/p5-gain-model.ts` (§6).

Tags: **[measured]** run counters; **[verified]** read in our source; **[inferred]** derived, method named.

## Summary

- **Who is eligible.** P5 takes only async `ReplanFromPosition` jobs (A10 kind 3; `P5OnStart` arms `j->kind == 3` with `Kinds` bit 8) [verified, `p5.cpp`]. In the release config A10 is off, so **no job is eligible and every lending arm cuts 0 ms/s on every world** [measured: `arStartedReplanFromPosition` 0 in all 0.2.0 clean-night runs]. With A10 ReplanFromPosition on, eligible jobs are 1.0-1.95 jobs/s and **16-32% of the hold** (BP 23-50 ms/s, IP 17-30) [measured].
- **Lent share.** The window covers only the clone run between door asks: **17-23% of an eligible job's hold on BP, 35% on IP** (P5b run), ~5 ms per job, 10-21 windows per plan [measured]. The offline 93% (docs/94 §22.9.3) was a fake job that was all clone run.
- **Admitted takers carry most of the keyed wait** [measured keys, classes per docs/94 §22.10.1]: P5b (steering, gate-0 hits, gate-12 read families) **55-60% BP, 53-65% IP, 62-70% SV** of the 53-92 ms/s keyed wait; P5c adds the plain writers and parked destroys: **86-89% BP, 89-92% IP, 87-90% SV**.
- **But the sim cannot use much of a window** (docs/152 conservation): it progresses inside the window until its first non-admitted touch, then waits to the job's end. Upper bound on the usable share q: **P5b ≤ 0.06-0.17, P5c ≤ 0.22-0.5** [inferred, §3.2]. In the only live A/B (P5 + P5b + P5c vs plain A10, BP, 3 tags) q is not resolved: the implied values span -0.9 to +2.0 [measured, noise-bound].
- **P5 makes the eligible job longer.** Copy, clone run with ~20 door-ask retakes, and the game's plan after the 54-79% stale refusals cost more than the plan a taken answer skips: **+3.8 to +9.6 ms per eligible job** in the A/B (P5c, 0.35.25) [measured]; P5b alone (no A/B, cross-run) +5.6..+8.4 BP, +8.6..+13.1 IP [inferred]. At s = 0.67 this term (+3..+10 ms/s) is larger than the most lending can give back (0.4-3.6 ms/s).
- **Expected cut on the sim's `+0x918` wait (A10 ReplanFromPosition on; negative = less wait).** Lending term alone: BP P5b 0..-0.4, P5c 0..-3.1; IP P5b 0..-1.2, P5c 0..-3.6; SV as IP [inferred]. Whole arm with P5's own job cost: **P5b alone BP +5.0..+9.6, IP/SV +4.6..+8.9** (worse, inferred); **P5c BP -6.1..+6.6** (measured +3.0 mean over 3 tags, -1.2..+6.8), IP/SV -5.3..+3.9 (no live P5c data); **B0 + P5b ≈ P5b alone**; **B1 + P5b (B2) vs B1: +1..+4** (lending term ~0). Release config: 0 everywhere.
- **Dominant uncertainty:** dT (P5's change of the eligible job's hold), then q. **Probe first:** the `snap-p5c-ab` A/B rerun on the current asi with `FreeLock=0` and once with `FreeLock=1`, BP + IP, day jobs (per-job ratios, noise OK), read with `tools/p5-gain-model.ts`. If dT stays above ~+3.5 ms per job, lending cannot pay at any q ≤ 0.5, and below that it pays at most the lending term (≤ ~3 ms/s): P5b/P5c nights should not be spent unless dT ≤ 0. q needs one small counter build (§5).
- **Widening** (§4): own START `Route.Plan` jobs are 86-100% of the release hold; making them eligible (P6) would lend ~23-44 ms/s on BP at today's lent share, worth at most -1.2..-2.8 (P5b) or -5..-10 ms/s (P5c) from lending, and only if Plan jobs get dT ≤ 0. As built the copy alone (5.7 ms per acquire) exceeds a Plan job's mean hold (~5 ms) and the per-START cut costs 9-10 ms of sim time (docs/94 §22.30.4 G2): strongly negative until cut sharing and a cheaper copy exist.

## 1. Which jobs are P5-eligible

| Job kind | Hold BP ms/s (release / A10 on) | Eligible today | Source |
|---|---|---|---|
| Own START `Route.Plan` (kind 0) | 112-125 play, 86-101 crowd | no (P6 design, docs/94 §22.30.4) | [measured] docs/150 §1; [verified] `P5OnStart` |
| Foreign START (FallbackAsync) | +26 when on | no | [measured] docs/150 §1 |
| A10 `Replan` / `PlanFromPoint` | 6.0 / 3.3 (when on) | no (`Kinds` bits 1, 2 logged, not taken) | [verified] `ApplyP5` log lines |
| **A10 `ReplanFromPosition`** | 23-50 (when on), 1.4-1.95 jobs/s, 15-31 ms/job | **yes**, taken jobs and (since 0.36.23 `VerifyLend=1`) verify jobs | [measured] runs above |
| any kind in the release config | - | 0: A10 is off | [measured] |

So a lending arm is only defined in an A10-on config, and its baseline is "A10 ReplanFromPosition on, Mode=2 off". A10 itself moves sync replans off the sim and adds 26-45 ms/s of hold (docs/150 §1); that trade is outside this model.

## 2. Measured inputs per world

All per wall second. "P5c side" = the on side of a P5 + P5b + P5c run; "P5b run" = `snap-p5b` 2026-10-20 (0.34.x, whole run, 600 s assumed from `snRecSec`).

| Input | BP | IP | SV | Tag |
|---|---|---|---|---|
| Keyed sim wait, release config (all keys) | 67.7-92.4, mean 77.2 | 63-84, mean 71.2 | 52-55, mean 53.8 | [measured] off sides |
| Admitted share a, P5b / P5c | 55-60% / 86-89% | 53-65% / 89-92% | 62-70% / 87-90% | [measured keys; class map verified docs/94 §22.10.1; F-B share 69% of `0x62F782` from bp-p1b] |
| Eligible jobs N (A10 RFP on) | 1.42-1.95 | 1.0-1.14 | - | [measured]; SV none |
| Eligible hold E | 23-50 (16-32% of hold) | 17.5-30 (17-24%) | - | [measured] |
| Hold per eligible job, plain A10 | 15.4-18.2 ms | 17.0-21.5 ms | - | [measured] `snap-p5c-ab` off sides, `ip-ar-ab-wait` |
| Hold per eligible job, P5 on | P5c 19.2-30.6 ms; P5b 23.8 ms | P5b 30.1 ms | - | [measured] |
| Lent L | P5c 6.1-10.4; P5b 7.3 | P5b 10.6; P5c none | - | [measured] `p5bWinUs` |
| Lent share λ = L / E | P5c 17-23%; P5b 22% | P5b 35% | - | [measured] |
| Windows per P5 plan n | P5c 10-21; P5b 18.5 | P5b 7.1 | - | [measured] `p5bWins / m2Plans` |
| Take rate | P5b 63%; P5c 8-20% | P5b 53%; P5c - | - | [measured] `m2Taken / m2Plans` |
| Stale refusals (P5c rule) | 54-73% of plans, 29-49% before the acquire | - (Mode=2 off after the 0.35.25 fault) | - | [measured] `p5cStale[0..1]` |
| Held-out acquires per window | P5c 0.03-0.08; P5b 0.13 | P5b 0.98 | - | [measured] `p5bHeldOut / p5bWins` |
| Door-ask wait `m2DoorWaitUs` | 1.2-2.1 | 0.4 | - | [measured] |
| s = sim wait per ms of hold | 0.67 (r² 0.93, 293 slices) | 0.67 assumed | 0.67 assumed | [measured correlation BP; causal reading and IP/SV inferred] |

### 2.1 Where the waits went in the live A/B [measured]

P5 + P5b + P5c on minus plain A10, BP, mean of 3 tags (keyed ms/s): gate 0 -2.9, gate-12 `Contains` -2.3 (both admitted: they drop); steering +4.6 (one tag +9.5), gate 10 +1.6, `0x62FB74` +1.4, unmapped keys +2.4, writers 0.0, destroy -0.2. Summed keyed +10.0 / -2.2 / +5.1; `haUs[0]` +6.8 / -1.2 / +3.3 (mean +3.0). The admitted gate leaves lose wait, the non-admitted keys and the steering key gain it: the conservation pattern of docs/152 §2.

## 3. Model

dW = s × (N × dT − q × L), ms/s, negative = less sim wait.

### 3.1 dT: the eligible job's whole hold change

`asyncUs` counts the lent time too, and a lent window shortens the sim's wait only through q. So the job's whole hold is the right base, not its unlent part. dT = P5 job − plain job:

- **P5c, measured A/B (0.35.25):** +8.6, +9.6, +3.8 ms per eligible job. Parts: copy 5.7 ms per acquire, held until 0.35.26 [measured, docs/94 §22.30.2]; clone run ~7 ms per plan with ~20 door-ask retakes [measured `m2JobUs / m2Plans`]; the game's plan after 54-73% stale refusals [measured]; minus the plan of the 8-20% taken answers.
- **P5c after 0.35.26 [inferred]:** stale-before-acquire jobs (29-49%) skip the copy and the clone run (~13 ms), about -3.8..-6.4 ms per job. dT ≈ **-2.6..+5.8**.
- **P5b alone [inferred, cross-run, day runs]:** 23.8 ms (P5b run) vs 15.4-18.2 plain on BP = **+5.6..+8.4**; IP 30.1 vs 17.0-21.5 = **+8.6..+13.1**. No P5c stale rule, so 0.35.26's skip does not apply; the take rate is high (53-63%), yet the copy plus the clone run with retakes cost more than the game's plan they replace.

### 3.2 q: the share of lent time the sim turns into progress

Conservation (docs/143 §8.14, docs/152 §1): a non-admitted touch during a job waits to the job's end. Per window the sim gets another admitted touch with probability a before a non-admitted one parks it, so of n windows it uses about a / (1 − a), and **q ≤ a / (n (1 − a))**, capped at a [inferred]. With the measured a and n: BP P5b ≤ 0.04-0.08, P5c ≤ 0.22-0.45; IP P5b ≤ 0.17 (n = 7), P5c capped at 0.5 for the forecast (the bound is unstable as a → 1). Lower bound 0. Offline P5c: 84% of acquires did not wait, yet the sim wait per job was unchanged (docs/94 §22.11.4), i.e. q ≈ 0 in that mix [measured offline]. IP's 0.98 held-out acquires per window [measured] also points to q near 0 for P5b there.

Live check [measured, `p5-gain-model.ts` on the 3 BP A/B tags]: implied q = 0.40 / 2.00 / 0.13 from the eligible jobs' hold, 0.05 / 0.21 / -0.94 from the whole hold. One slice pair moves `haUs[0]` by 5-8 ms/s (docs/136 §2), i.e. q by about ±1: **q is not resolved by slice-level waits**. It needs a per-job counter (§5).

### 3.3 Refusal cost

A refused job runs the copy (held before 0.35.26), the clone run (lent) and then the game's plan under the retaken lock: the refusal adds about one clone run + copy to the job, which is most of dT. A sim taker that waits after the window waits through that plan. This cost is in dT; it is why P5c's lent share does not turn into a cut. A taken answer saves the game's plan (~10-15 ms of a 15-18 ms plain RFP job [inferred, plan share of job time]).

### 3.4 Residual waits

Admitted waits move to non-admitted keys in the same job (gate 10, gate-12 writer families, gate-0 misses, unmapped keys) and into the fallback plan's hold (§2.1). Under P5b the per-tick writers (`UpdateSimFootprint` in both animation passes, `0x8334CC`) are never admitted, so most lent windows end with the sim parked at the next animation pass.

## 4. Expected cut ranges

s = 0.67; N, L, a, n, λ as §2; dT and q as §3. Release config (A10 off): **0 for every arm on every world.** Below: A10 ReplanFromPosition on, vs plain A10.

| Arm | BP lending term | BP whole arm | IP lending term | IP whole arm | SV | Inputs that are inferred |
|---|---|---|---|---|---|---|
| P5b alone (`ReleaseLock` + `ReleaseGates`) | 0..-0.4 | **+5.0..+9.6** (worse) | 0..-1.2 | **+4.6..+8.9** | as IP (no SV P5 or A10 data) | dT (cross-run), q |
| P5c (P5b + `FreeLock`, 0.35.26+) | 0..-3.1 | **-6.1..+6.6**; measured on 0.35.25: +3.0 mean (-1.2..+6.8, 3 tags) | 0..-3.6 | **-5.3..+3.9** (no live P5c on IP: Mode=2 faulted) | as IP | dT after 0.35.26, q, all IP/SV |
| B0 + P5b | ~0 (the B0 RO clones already serve steering; a among what is left ~15-20%) | ≈ P5b alone | ~0 | ≈ P5b alone | - | docs/152 B0 ≈ off |
| B1 + P5b (B2 − B1) | ~0 (residual takers are writers and gate 10, not admitted) | **+1..+4** (s in B1 ~0.15-0.25: residual 20-35 ms/s at hold 115-150) | ~0 | similar | - | B1 forecast (docs/152), dT |
| Lending widened to all kinds at today's λ | ≤ -1.2..-2.8 (P5b), ≤ -5..-10 (P5c) | depends on Plan-job dT: copy 5.7 ms > Plan job ~5 ms today | ≤ -5 (P5b) | - | - | λ for Plan jobs, q, cut cost |

Reading: the lending term is real but small (≤ 3.6 ms/s), and it is outweighed by P5's own job lengthening. Lending pays only if dT ≤ 0, which is a property of the take path (copy cost, door-ask round trips, refusal rate), not of the lend.

## 5. Probes and counters that replace the inferred inputs (spec, nothing built)

| Inferred input | Cheapest replacement | Cost |
|---|---|---|
| **dT for P5b** (dominant) | No build: rerun `tools/harness/queues/snap-p5c-ab.json` on the current asi with `RouteSnap.FreeLock=0` (ReleaseLock=1, ReleaseGates=1), `--ab routesnap`, BP + IP, 10 min, day machine OK (per-job means over ~400 jobs per side are robust to load). Read `arJobUsReplanFromPosition / arStartedReplanFromPosition` per side with `node tools/p5-gain-model.ts <tag>` (prints dT and implied q) | 2 day jobs, ~30 min; a job entry |
| **dT for P5c after 0.35.26** | The same job with `FreeLock=1` (the existing queue as is) | 2 day jobs |
| **q** | Small build, existing hook: in `[HitchAttr]`'s `+0x918` wait accounting (`haUs[0]`), attribute each sim wait to the live job: `haJobUs[4]` / `haJobN[4]` (0 eligible job that lent at least once, 1 eligible job held: verify with `VerifyLend=0` or a refused-before-window job, 2 own START, 3 other) and `haJobHoldUs[4]` (hold per class from `ServeAsync`). q ≈ 1 − (wait per ms of hold, lent class) / (wait per ms of hold, held class), within one run; the held class is the natural control (1 in `VerifyEvery` jobs). Reserve the next free counter version in `docs/registry.md` | ~1 h build + 1 day job |
| s on IP / SV | Existing data: regress `haUs[0]` on `asyncUs` per slice over the IP/SV clean-night slices (no run) | ~15 min |
| Gate-0 hit share, F-B share | Already counted: `p5bGateAcq[1]` vs `p5bGateMiss`; F-B from P1b's second frame | none |
| Unmapped keys' class | docs/152 G7 (map the 7 keys) | 1-2 h static |
| SV inputs | A10 RFP on is needed first; one `snap-p5c-ab` SV job after the IP/BP ones | 1 day job |

**Order:** the two `snap-p5c-ab` day jobs first (no build). Stop rule: a net cut needs N × dT < q × L, i.e. dT < q × L / N ≈ 0.5 × 10.4 / 1.5 ≈ 3.5 ms per job at the most generous q. If dT ≥ +3.5 ms per eligible job on both worlds for P5b and P5c, no clean night goes to P5b/P5c lending; between 0 and 3.5 the net cut is at most the lending term (≤ ~3 ms/s), below the BP pair noise, so the same applies; the work moves to the take path (cheaper copy, fewer door-ask retakes, stale rule at the window) or to hold cuts (docs/150 chunks 1-3, TailUnlock). Only if dT ≤ 0: build the q counters.

`[RouteAsync] TailUnlock` (docs/150 §11) is a P5b-style lend with dT ≈ 0 (no extra work), on every job kind: it is the cleanest live test of q. The model predicts its lending term at ≤ 0.67 × 0.07 × ~9 ms/s ≈ -0.4 ms/s with P5b admission, below docs/150 §11's -2..-6 [inferred]; the same q counters read it directly.

## 6. Tool

`node tools/p5-gain-model.ts [--dir <profile dir>] [--dt lo,hi] <tag> [...]` reads `<tag>-ab.json` + `<tag>-ab-native.jsonl` (same slice split as `wait-ab.ts`, warm-up slice skipped) and prints per side: hold, `haUs[0]`, keyed wait, eligible jobs and hold per job, L, λ, windows per plan, take / stale rates, door wait, admitted shares a (P5b, P5c) and the q bounds, the model ranges (lending-only and with `--dt`, default -1..9.5 ms per job) and the all-kinds lending bound. For a P5-on vs P5-off A/B (`routesnap`) it prints the measured dW, dT and the implied q (two readings). `--native <job>-native.json [--secs N]` reads a queue job's before/after dump. `--self-check` covers the class split, the q bound, the arm range, the side split and the per-job inputs. Read-only; no game, no probe-out writes.

## 7. Risks of widening eligibility

| Widening | Hold made eligible (BP) | Blockers and risks |
|---|---|---|
| Own START `Route.Plan` (P6, `TakeKinds` bit 1) | 112-125 ms/s, 22-25 jobs/s | per-START cut 9-10 ms of sim time under `+0x918` (G2; ~200 ms/s without cut sharing); copy 5.7 ms per acquire > the job's ~5 ms mean; stale rate under P5c likely higher at 24 jobs/s (more writes between cut and window); clone-run faults (0.35.24/25 IP, fixed in 0.36.2 with a residual case, docs/94 §22.31); Plan compares M2 0 on BP only so far |
| Foreign / FallbackAsync jobs | +26 ms/s, multi-second tails (docs/134) | no own-Sim task; long clone runs hit lazy stops and door asks; the multi-second tail is cell A\* (docs/150 §2) and would be lent only during the clone run |
| A10 `Replan`, `PlanFromPoint` | 6.0 + 3.3 ms/s when on | no shadow compares yet (docs/94 §22.29.6); small hold |
| START jobs of any kind via a window-time cut | as Plan | needs cut sharing (`PcBorrow` for Mode=2) and the stale test against the window, not the START cut (docs/94 §22.29.6 item 3) |

Widening raises L, but the gain stays bounded by q × L (≤ ~10 ms/s on BP for P5c), and every widened kind brings its own dT. Lending is worth widening only after the take path has dT ≤ 0 on ReplanFromPosition jobs.
