# 125. Autonomy worker: answering the three sim-thread ICs (RNG, slot positions, `Query_GetObjects`)

Written 2026-10-20. Static design plus an offline build: no game run, no queue. docs/121 left three internal calls (ICs) of meta scoring that the `[AutShadow]` worker (docs/122) could not answer, so it aborted on them: `Random_NextDoubleImpl` (script RNG, docs/27), `Slots_GetPositionOfSlotImpl` (slot position helper `0x76F0A0`, docs/90) and `Query_GetObjects` (world-wide per-class list, the one class-d IC). This doc picks an answer for each, argues why Mode R (docs/122 §4.3) stays bit-equal, and records the build behind the existing `[AutShadow] Enabled=0` switch. Ledger rows: docs/51 §166 (I-125-1 to I-125-9).

Tags: **[verified]** = read in the disassembly (capstone 5 on the Steam 1.67 exe) or proven by a run named here; **[verified, docs/N]** = verified there; **[inferred]** = reasoning still to confirm; **[design]** = a choice made here. Effort in CLAUDE.md units.

## Summary

| IC | Before | Chosen method | Mode R argument | Cost | Abort rules now |
|---|---|---|---|---|---|
| `Random_NextDoubleImpl` `0x74A780` | abort `rng` | **per-unit MT streams** (§2): the driver marks a unit before each meta score and each pair; a unit's draws come from a Mersenne Twister seeded with a hash of (cut seed, unit), drawn by the game's own init and draw code, on the worker in a private MT object and in the sim-thread reference in the host's MT (saved and restored as before) | both threads run the same code on the same managed state; a unit's k-th draw is a function of (cut, unit, k) only, so a divergence in one unit (abort, tooltip draw) cannot shift any other unit | 4 B per version; 2.5 KB worker MT; per unit one IC call; per unit that draws one MT init (~2-4 µs, both threads) [inferred] | draw before any unit in the run, or no cut seed: `rng`. `Deterministic` flag set: 0.5 as the game |
| `Slots_GetPositionOfSlotImpl` `0x76F2F0` | abort `slotpos` | **learned key, game body at every cut** (§3): key (handle, slot hash, age/gender/species, exit flag), owner kind `AO_CUT` | the cut calls the same body on the sim thread right before the reference reads it live: same thread, same state, same answer; `AO_CUT` needs no change event, so animation-driven slot moves are covered | ~100 B per key; one body call per key per cut (early-out < 1 µs, portal path a few µs, plus any `+0x918` gate wait the game's own call would also take) [inferred] | first sight of a key: `slotpos` + learned; unused 100 cuts: dropped |
| `Query_GetObjects` `0x749630` | abort `query` | **learned type key, game body at every cut**, answers kept up to 256 objects (§4) | as above | ≤ 1 KB payload per type; one walk of that class's host map per cut | first sight: `query` + learned; more than 256 objects: `query` on every call (no payload kept) |
| `Slots_CanAttachToSlotImpl` | abort `attach` | unchanged (docs/62 UNSAFE: fires `World_FireEvent`) | - | - | `attach` |

**Offline result:** selftest passes (new T1/T2 checks for the slot and query keys, new T5 checks for the RNG units); `native/monohost` G-A2 with real Mono, two threads and the game's own MT code: **A0 10,000 of 10,000, rng aborts 0** (was 8,400 A0 + 1,600 `rng` aborts), two clean runs; negative control (worker seed off by one): 1,600 mismatches, as expected [verified]. ts3fixes compiles; its logic selftest passes.

**Effect on the pilot:** the three designed abort reasons that were permanent for their paths (every pet actor's base meta score, the `CommonTests` slot branch, every `Query_GetObjects` type) become one-time warm-up misses per key, except world-wide types over 256 objects. In the host test that moves 16% of compared pairs from A4 to A0. The in-game share is a G-A3 output (I-121-5, I-121-7, I-121-9 now size the cost instead of the abort rate). No new game job: the queued `autshadow-first.json` jobs (G-A3, G-A4) test it; the pet household repro of docs/122 §6.2 now expects A0 instead of `rng` aborts.

## 1. Starting point

- docs/121 §2.7, §4.1: the RNG and slot positions are class b (need the sim thread); `Query_GetObjects` is class d (type set unknown, I-121-7).
- docs/122 §3.2 and §3.4: the worker aborts `rng`, `slotpos` and `query`; Mode R's reference saves and restores the script MT around its run (I-122-11).
- docs/122 §13.2: answers live in one generic table (IC, input dwords) -> the body's answer, filled on the sim thread by calling the IC body found in the provider slot, with owner kinds that decide when an entry is refreshed (`AO_OBJ` when the object is dirty, `AO_LIST` on a list generation, `AO_CUT` at every cut, `AO_WORLD` on the epoch). A worker miss on a non-memo key is learned and answered from the next cut. Slot positions and `Query_GetObjects` fit this table without new machinery; the RNG does not, because a draw changes state.

## 2. Script RNG: per-unit streams

### 2.1 What the game's RNG code touches [verified]

- `Random_NextDoubleImpl` `0x74A780` gets the host through `0x7692B0`, then interface slot `+0x17C` (returns the host) and host slot `+0x28` = `0xD70EE0`.
- `0xD70EE0`: if the `Deterministic` byte `[0x11CDBC5]` (read by `0x588780`) is set it returns the constant 0.5 (`[0xF9CEB8]`); else it tail-jumps to `0x4FAA70` with `this` = host `+0xC30`.
- `0x4FAA70` (thiscall, no stack arguments): one draw from `0x4FA9F0`, converted to a double in [0,1) (a result of 1.0 or more becomes 0).
- `0x4FA9F0`: decrements the words-left count at `+0x9C4`, regenerates through `0x4FA8B0` when it goes negative, reads the word at the **absolute** read pointer `+0x9C0`, advances it by 4, tempers (standard MT19937 constants).
- `0x4FA8B0` (regenerate): rewrites the 624 words in place, then sets the count to 624 and the read pointer to `this`.
- `0x4FA990` (init, thiscall, `ret 4`): seed -1 means `RDTSC`; else `seed | 1`, the Knuth-style fill of 624 words, then the regenerate above.
- So the MT object is exactly 0x9C8 bytes (624 words + pointer + count) and ends where the LCG at host `+0x15F8` starts; the four functions read and write only `this` memory and the one flag byte. A copy of the bytes elsewhere would carry a pointer into the original (it must be rebased), but an object made by `0x4FA990` points at itself. This closes the "[end inferred]" of docs/122 §14.1 and the size part of I-122-11 (I-125-1).

### 2.2 Options

| Option | Worker draws from | Mode R | Rejected because |
|---|---|---|---|
| a. abort (docs/121 §4.1, built in part 1) | nothing | A4 for every object on a drawing path | permanent aborts: every pet actor's meta score |
| b. one forked MT per run (copy of the cut's state, pointer rebased) | the same values the reference draws, in call order | bit-equal only while both threads make the same draw calls in the same order | one divergence shifts every later draw of the run: a worker abort inside an object (the run goes on with neutral values), a tooltip draw the reference makes and the worker's `LocalizeString` stub skips (docs/121 §2.6), a hook branch that differs by thread; one cause shows up as A2 on all later objects |
| c. pre-drawn buffer (the sim draws N values at the cut) | the buffer | same ordering problem as b | plus N unknown up front, and it advances or must restore the sim's stream |
| **d. per-unit streams [design]** | a fresh MT per unit, seeded from (cut seed, unit) | each unit stands alone | chosen |

### 2.3 Design as built

- **Units:** ts3fixes `AutShadow.ScoreMeta` calls `AutShadowNative.RngUnit(o << 16)` before each meta object's `HowMuchItWantsSimToCome` and `RngUnit((o << 16) | (pair + 1))` before each pair's `ScorePair`. The same code runs in the reference and on the worker, so both mark the same units.
- **Cut seed:** right after the cut (`AsSimLeave`), a hash of the live MT's 624 words and count (not the read pointer, an address), stored per published version (`g_asRngSeedV`). The worker uses the seed of the version it holds; the reference uses the version just published. Taking it from the live state keeps the streams tied to the game's seed, so seeded runs (docs/27) repeat them.
- **Unit seed:** a 32-bit mix of (cut seed, unit), shifted left by one so the init's `seed | 1` keeps distinct values apart and never makes -1 (`RDTSC`).
- **Worker:** `RngUnit` on the worker sets the current unit and marks its private MT (`g_asRngMtW`, 0x9C8 bytes) stale; the guard's `AO_RNG` answer seeds it with the game's `0x4FA990` on the unit's first draw and draws with the game's `0x4FAA70`. The `Deterministic` byte is checked first, as in `0xD70EE0`.
- **Reference (sim thread):** `AsReference` swaps the RNG slot's sim-thread target (`g_asOrig[g_asIcRng]`) for `AsRngSimDraw` for the run; `RngUnit` there records the pending unit, and the next draw re-seeds the host's own MT at `+0xC30` with the same `0x4FA990` and calls the game's IC body. The existing save and restore of `+0xC30 .. +0x15F8` puts the host MT back afterwards, so the sim's stream is unchanged by the reference (I-122-11). Outside the reference `RngUnit` does nothing: the live RNG is never touched by the shadow.
- **Contract:** `AutShadowNative.Version` 3 (`RngUnit` added); ts3fixes requires 3, the monohost check too.

### 2.4 Correctness argument (Mode R, I-122-11)

1. Same managed state: the reference and the worker run in the same quiescent window on the same cut (docs/122 §1.2), and every native answer the worker gets equals the live answer at the cut (docs/122 §2, §14).
2. Same stream per unit: both threads seed their MT object with `0x4FA990(mix(cut seed, unit))` and draw with `0x4FAA70`; these touch only `this` and the flag byte (§2.1), so the k-th draw of a unit is a function of (cut seed, unit, k) only.
3. Units isolate divergences: a difference inside one unit (an abort, a draw only one side makes) changes nothing outside it. The one known one-sided draw, the tooltip's `FillInTokens` on the reference, happens while a failing `Test` builds its tooltip; the pair's code is then "Test failed" on both sides and nothing in that pair unit is scored after it (I-125-3 [inferred]).
4. So any A1-A3 on a drawing path is a bug, as for every other IC; the negative control in §6 shows a wrong stream is caught.

### 2.5 Mode W and go-live

In shadow (Modes R and W) the live script stream is untouched: the worker draws only from its own MT, and the reference's draws are restored. A seeded A/B (docs/27) is therefore unaffected by `[AutShadow]` beyond what I-122-11 already tests. Mode W compares with the game's own search, which draws from the live stream; the per-unit draws differ from those values, so on drawing paths the L report shows randomness, not staleness. For a later go-live (docs/118 G6), the worker's results replace draws the sim would have made: either leave the live stream as is (each unit stream is an ordinary MT stream, so the distribution is the same, only the values differ from vanilla), or advance the live MT by the worker's draw count at commit so later sim draws sit at the same stream positions as vanilla. That choice belongs to the go-live determinism note, not to the pilot.

### 2.6 Cost [inferred unless marked]

- Per cut: one hash over 2.5 KB (~1 µs) and 4 B in the published version.
- Per request: one `RngUnit` IC call per meta object and per pair (~150-600 calls for 50-150 objects, interpreter IC calls of well under 1 µs each).
- Per unit that draws: one MT init plus regeneration (~1,250 word steps, ~2-4 µs), on the worker and again in the reference. Pet actors draw in every object's meta score, so a pet request adds ~0.1-0.6 ms to each run.
- Memory: the worker's MT object (2.5 KB, static); nothing per entry.

### 2.7 Abort rules

- A draw before any `RngUnit` in the run (a driver path that forgot a mark): `rng`, logged with the IC name. The game run should show 0 (I-125-2).
- No cut seed for the held version (no host pointer): `rng`.
- `Deterministic` flag set: 0.5, as the game.

## 3. Slot positions

### 3.1 What the body does [verified, docs/90 §1-§2]

`Slots_GetPositionOfSlotImpl(ulong handle, uint slotNameHash, uint ageGenderSpecies, bool isExitSlot)` returns a `Vector3` through a hidden result pointer (5 stack dwords after it, as the descriptor already had) [verified: managed extern signature]. Its body `0x76F2F0` looks the object up and calls the slot position helper `0x76F0A0`, which returns at once for age/gender/species 0 or no object, else reads the routing world, calls `LotRouting_PortalsForObject` (gate 12 `LotTable_Contains` and gate 0 `GetOrInsert`, both under the steering `+0x918` wait when the routing job holds it), takes portal references it releases later (net not zero over the call), and reaches the terrain lock `0x4E16F0` and the lot-grid rebuild at hops 4-5.

### 3.2 Options

| Option | Rejected because |
|---|---|
| Worker runs the body under a QL-shared hold (docs/90 design SH) | SH proves equal answers against the routing job's writers (W4/W5) with the sim thread as the only other caller; the shadow worker would be a third thread, while native animation and locomotion keep moving transforms in the quiescent window (docs/121 §6 item 2); the body also takes portal references, can reach `0x4E16F0` and the grid rebuild (docs/121 §8.3-§8.4), and would put a worker on the routing gates |
| Per-object rows with every slot | unbounded: object x slot x age/gender/species x exit flag (docs/121 §2.7) |
| **Learned key, `AO_CUT` [design]** | chosen |

### 3.3 Design and correctness

- Key = the 5 input dwords (handle low/high, slot hash, age/gender/species, exit flag); answer = the 12-byte vector. The worker's first call for a key misses, aborts `slotpos` and learns the key; from the next cut on the cut calls the game's body for it on the sim thread and the worker answers from the copy.
- `AO_CUT` (re-asked at every cut, like sunrise) and not `AO_OBJ` (re-asked only when an event dirties the owner): slot positions follow animation, which fires no event docs/76 or docs/79 track. Mode R's reference runs on the sim thread right after the cut with nothing in between, so the cut's answer equals the live answer the reference reads (I-125-5 [inferred]).
- The cut's body call is an ordinary sim-thread call: it takes the same gate waits the game's own call takes, and with `[SlotEnabledRO] Mode=1` it goes through SH's detour at `0x76F0A0` like every other sim-thread caller [inferred: the detour sits on the helper itself].

### 3.4 Cost and fallback

~100 B per key (one table entry); per key per cut one body call: < 1 µs on the early-out path, a few µs on the portal path, plus a `+0x918` wait when a routing job holds it (docs/90 measured F-B waits of ~9 ms mean, 854 in 10 min over all callers). Learned keys are few (one `CommonTests` branch, I-121-9) and drop after 100 cuts unused. If G-A3 shows the slot keys in `cutMaxUs` beyond the 0.3 ms p95 budget, switch the descriptor to `AO_OBJ` (one word) and let Mode R show whether events cover the moves (I-125-4).

### 3.5 Abort rules

First sight of a key: `slotpos` (once, then learned). Unused for 100 cuts: dropped, so a later call aborts once more. Epoch: refreshed like every entry.

## 4. `Query_GetObjects`

### 4.1 What the body does [verified, docs/121 §2.3]

It resolves the Mono class of the `Type` argument (`0xE70D50`) and walks the script host's per-class object maps (`+0x1658`, `+0x1678`), returning a managed array. The key is the `MonoReflectionType*`, stable per domain (docs/122 §14.8 [inferred]).

### 4.2 Options

| Option | Rejected because |
|---|---|
| World-wide copy of every script object's (id, class) | thousands of rows per cut (docs/121 §2.3) |
| Per-class lists kept by object events 4/6 | script objects are made and destroyed outside world add/remove too (inventories, objects not placed), so the events may not cover the host maps [inferred]; a missed event would be a silent stale answer |
| **Learned type, `AO_CUT`, keep ≤ 256 objects [design]** | chosen |

### 4.3 Design, correctness, cost

- Same learned-key path as §3: first sight aborts `query` and learns the type, the cut calls the body at every cut, the worker allocates its own array from the copy (as for `Query_GetObjectsOnLot`). Equal to the live answer for the same reason as §3.3, with no reliance on events.
- An answer longer than 256 objects is not kept (`AE_NOPAY`): the worker aborts `query` on it. A world-wide type would cost a long map walk per cut and pool space for every cut; the abort keeps the cut bounded and names the type in the abort log (I-125-6).
- Cost: per learned type per cut one walk of that class's map and a copy of ≤ 1 KB. If the type set is the handful of rare types I-121-7 expects (rabbit holes, meta objects), this is a few µs per cut [inferred].

## 5. Not changed

`Slots_CanAttachToSlotImpl` stays an abort (`attach`): it fires `World_FireEvent` and writes globals (docs/62 UNSAFE), and it is reached only from the eating scoring function, which matters only if tuning names it (I-121-10).

## 6. Build and offline proof (2026-10-20)

Files: `native/ts3native/autshadow.cpp` (owner `AO_RNG`, slot and query rows `AO_CUT`, query keep cap, RNG guard index), `autshadow-run.cpp` (MT layout comment, per-unit RNG, cut seed, reference slot swap, `RngUnit` IC, Version 3), `autshadow-test.h`; `native/monohost/monohost.cpp`, `HostTest.cs`; `mods/ts3fixes/src/AutShadow.cs`. Switches: none new; `[AutShadow] Enabled=0` stays the default, ts3fixes `kAutShadow` stays false. Counters: no new fields (the `ts3native_autshadow` v2 layout is unchanged).

| Check | What | Result |
|---|---|---|
| `native/ts3native/build.cmd` | `/W4 /WX`, all targets, selftest | `selftest ok` (one earlier run tripped the P4 threaded check at `selftest.cpp:8473`, an unrelated timing test on a busy machine; rerun and full rebuild clean) |
| selftest T1/T2 (fake table + 2 entries) | first slot and query calls on the worker miss and abort `slotpos` / `query`; after the next cut they answer bit-equal to the body; a slot change with no event is answered at the following cut; a 300-object type aborts `query`; audit 0 mismatches | pass |
| selftest T5 | reference draws units 3 then 1 (unit 1 one draw more), worker units 1 then 3: equal values per unit; a worker draw before any unit aborts `rng`; `Deterministic` returns 0.5 without touching the MT; the RNG slot target and the host MT are restored after the reference | pass |
| monohost `selftest` (real Mono, `MonoHostTest.dll`, the game's `0x4FA990` / `0x4FAA70` on a host MT) | G-A2: 200 requests x 50 objects, every 7th object draws inside its unit | **A0 10,000 of 10,000**, rng aborts 0, A1-A3 0, 204 reference RNG restores, 36/36 checks, x2 |
| monohost negative control | worker unit seed off by one (temporary edit, reverted) | A0 8,400, 1,600 mismatches: a wrong stream is caught |
| `node tools/setup/build-fixes.ts` | ts3fixes against the game assemblies | builds |
| `node tools/selftest/autshadow/run.ts` | ts3fixes logic | PASS |

**Not proven offline:** the game's own draw paths in meta scoring (pet branch, tooltip tokens) always sit inside a unit (I-125-2), and the tooltip draw ends its unit (I-125-3); slot and query cut cost in the game (I-125-4, I-125-6). Mode 0 and Mode R in game answer all of them.

## 7. Ledger

docs/51 §168: I-125-1 to I-125-9. Updated rows: I-121-5, I-121-7, I-121-9 (now size cost, not aborts), I-122-11 (MT size verified).

## Reproduce

```sh
# §2.1: capstone 5 (32-bit) over game-copy/bin/TS3W.exe at 0x74A780, 0xD70EE0, 0x588780, 0x4FAA70, 0x4FA9F0, 0x4FA8B0, 0x4FA990
cd native/ts3native && build.cmd                                                                     # selftest ok
native\monohost\build.cmd <repo>\game-copy\assemblies
native\monohost\out\monohost.exe <repo>\game-copy\monohost\TS3W.exe <repo>\game-copy\assemblies selftest <repo>\game-copy\monohost\hostdata.txt
node tools/setup/build-fixes.ts && node tools/selftest/autshadow/run.ts
```
