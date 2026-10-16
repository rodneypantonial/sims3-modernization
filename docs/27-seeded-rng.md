# 27. Seeded RNG for repeatable tests

Desk steps 1-3 of the PLAN.md lane "Parallel lane, seeded RNG" (docs/06 §8.1, open question 5). Static work only: Ghidra headless on the analysed copy of `TS3W.exe` (Steam build) and grep over the decompiled script assemblies. Nothing here has run in the game yet; step 4 (N=3 seeded vs unseeded) is a game test for the main session.

Tags: [verified] = read in the disassembly or decompiled source; [inferred] = follows from that but not yet confirmed by a run.

## 1. Native side: where the script RNG is seeded

### 1.1 Call chain [verified]

`RandomUtil` -> `Sims3.SimIFace.RandomGen.NextDouble()` -> cached `IRandom gRandomGen` (from `AppDomain.GetData("RandomGen")`, returns `0.0` if unset) -> `ScriptCore.Random` -> internal call `Random_NextDoubleImpl` `0x0074A780`:

| Step | Address | What it does |
|---|---|---|
| `Random_NextDoubleImpl` | `0x0074A780` | `FUN_007692b0()` returns global `0x011D4BBC`, calls its vtable `+0x17C`, then `+0x28` on the result |
| global `0x011D4BBC` | set in `0x00D7F2B0` | = `MonoScriptHost + 8` (the host is allocated there, `0x17C8` bytes, ctor `0x00D7E010`); cleared in `0x00D71580` (shutdown) |
| vtable `+0x17C` (vtable `0x0108B690`, the `+8` interface) | `0x00D7CFD0` | `LEA EAX,[ECX-8]; RET`: returns the host itself |
| host vtable `0x0108B818` `+0x28` | `0x00D70EE0` | if flag `0x011CDBC5` is set return `0.5`, else `NextDouble` on the RNG at **host `+0xC30`** (`0x004FAA70`) |
| `0x004FAA70` | | `(int32)draw * 2^-32 + 0.5`, so `[0,1)`; a result `>= 1.0` returns `0` |
| `0x004FA9F0` | | **Mersenne Twister** draw: 624-word state at `+0`, read pointer `+0x9C0`, words left `+0x9C4` (starts `0x270` = 624), regenerate `0x004FA8B0`, standard MT19937 tempering |

### 1.2 Seed source: `[Config] SimulatorRandomSeed`, else `RDTSC` [verified]

- **Ctor** `0x00D7E010` (at `0x00D7E29A`): `FUN_004FAAA0(host+0xC30, -1)` -> MT init `0x004FA990` with seed `-1` -> **`RDTSC`** low dword. A second, small LCG at host `+0x15F8` (`0x004FA800`, `x = x*0x41C64E6D + 0x3039`) is also seeded `-1` -> `RDTSC`.
- **Init** (host interface vtable `+4`, code at `0x00D77666..0x00D776A7`, called once from `0x00D7F2B0` when the script host starts): reads config key **`SimulatorRandomSeed`** (wide string `0x0108B480`) through `FUN_0058AD80` (section `Config`, `Services/ConfigRegistry`, `_wtol`, default `0`). If the value is `0`, it fills the seed from `RDTSC` (`0x004FA7C0`). Then it **reseeds the MT** (`0x00D77697`, `0x004FA990`) and the LCG (`0x00D776A7`, `0x004FA800`) with that value.
- MT init detail: `seed |= 1`, then the old Knuth-style `sgenrand` fill (`* 0x10DCD`, `* 0x1C587629 + 0x10DCE`), so seeds `2k` and `2k+1` give the same stream [verified].
- `Default.ini` (`Game\Bin`) already has a `[Config]` section (`Security = 0`, `debugui = 30`). So **`SimulatorRandomSeed = <n>` under `[Config]` in `Default.ini` should seed the script RNG with no code at all** [inferred: the ConfigRegistry is fed from `Default.ini [Config]`; to confirm with one run that prints the first `RandomUtil` draws twice].
- The seed is applied once per process, before the main menu. Every `RandomUtil` draw between start-up and the measured window (main menu, world load, the first sim ticks) advances the same MT, and the number of those draws depends on frame and thread timing [inferred]. A start-up seed alone may therefore not give the same stream at the start of the measured window. Reseeding at the start of the window (§3) removes that.

### 1.3 The built-in `Deterministic` switch is not usable [verified]

`FUN_00EC2820` (a string-variable setter next to `NoRender` and `RenderNoFill`) sets flag `0x011CDBC5` via `0x00588790` when variable `Deterministic` is `true`. With the flag set:
- script `NextDouble` returns a constant `0.5` (every `RandomUtil.RandomChance` and pick gets the same value: gameplay changes, not just becomes repeatable);
- route A* (`Route_SearchAStar`, `0x0081BC4A`) and `0x00813753` reseed their LCG with `0x539` (1337) per search; `0x00B10B03`, `0x00B47BD9` seed statics with 1337 once.

Useful as a hint that EA built determinism tooling, not as our switch. Its caller (`0x00EC2F70`) is not traced.

### 1.4 Other native RNGs [verified count, inferred effect]

`0x004FA800` (the LCG seed function, `-1`/`0` -> `RDTSC`) has about 50 call sites: routing (`0x00819DD2`, `0x0081BC55`), objects, animation and effects subsystems, many as function-local statics. These are not the script RNG; they cause native jitter (routing tie-breaks, animation variation) that a script seed does not touch. A native patch of the `RDTSC` fallback in `0x004FA800` (`0x004FA80D`) and `0x004FA990` would seed them all from a fixed value (§3, option C).

## 2. `new System.Random()` sites in the scripts [verified]

`System.Random()` = `this(Environment.TickCount)` (`mscorlib/System/Random.cs:19`). Grep of `game-copy/decompiled/**.cs`:

- **35** unseeded sites (`new Random()` / `new System.Random()`) in 23 files, and 24 seeded `new Random(x)` sites. docs/06 §8.1 said "at least 59": that was both together.
- No unseeded site is in a per-tick path (autonomy, routing, interaction scoring, `RandomUtil`). They are all one-off events.

**21 can run in live sim play** (all event-driven, none per tick):

| Area | Sites (file:line, method) |
|---|---|
| Births and genetics | `Pregnancy:1383 Run`, `Hospital:1152 MakeBaby`, `AlienUtils:829 GetNewBorns`, `ScientificSample:737 CreateBaby`, `ScientificSample:1028 CreateConedPlantSimBaby`, `ForbiddenFruitTree:100 GetNewBorn`, `ScienceLab:1548`, `ScienceLab:1637` (`InRabbitHole`), `TimeMachine:785 AlarmCallback`, `TimeMachine:1272 CreateChild`, `FutureDescendantService:1271 FixupOccults` |
| New Sims and pets (spawn, immigrants, story progression) | `SimUtils:283 SimCreationSpec.Instantiate`, `Genetics:1502 AssignRandomTraits`, `GeneticsPet:576 MakeRandomPet`, `GeneticsPet:759 AssignRandomTraits`, `PetPoolManager:500 GetRandomWeighedTraits`, `CASFairyData:51 ctor` |
| Rabbit-hole and object events | `EquestrianCenter:497 RaceShuffle`, `ServoBotArena:629 RaceShuffle`, `ShowStage:1093 Run`, `AncientCoinPile:328 UpdateTuningAndVisualState` |

**14 are CAS, UI or loading screen only** (outside the measured live-mode window): `CASLogic` 4553, 4611, 4992, 9120, 9129, 13044; `CASSim:604`; `CASCharacter` 259, 780, 1563; `CASRequiredItemsDialog:781`; `ChangeFavoritesDialog:258`; `LoadingScreenController` 177, 351.

What matters for our tests [inferred]: `spawn_sims` (M11) and story-progression immigrants go through `SimUtils.Instantiate` / `AssignRandomTraits`, so spawned Sims get `TickCount`-seeded traits and looks and then behave differently. Births only matter in runs of several sim days. Everything else is rare.

## 3. Switch design (design only, nothing built)

Off by default. Three layers, cheapest first; A plus B is the recommendation.

**A. Start-up seed, no code:** `SimulatorRandomSeed = <n>` under `[Config]` in the installed `Default.ini` (harness writes it for seeded runs, removes it after). Seeds the native MT and LCG at host start. First check that this key is read at all (§1.2).

**B. `seed_rng {seed}` agent action (main switch).** Agent actions are registered in `mods/ts3agent` (`Dispatcher.Register`, next to `spawn_sims`), so this belongs in `HarnessHandlers.cs`, not ts3fixes:
1. Set private static `Sims3.SimIFace.RandomGen.gRandomGen` by reflection (same as `ReflectHandlers`) to our `SeededRandom : IRandom` (managed MT19937 or xorshift128+ seeded with `seed`; `NextDouble` in `[0,1)`). Keep the old value and restore it on `seed_rng {seed: null}`, so the switch is off by default and reversible in one load.
2. Call it at the start of the measured window (after load, after `set_speed`), not at start-up, so pre-window draws do not matter.
3. Reply with `{seed, previous: "<type of old gRandomGen>", firstDraws: [3 values]}`: two runs with the same seed must print the same first draws (cheap self-check).
4. `System.Random()`: do not patch in the first version. Hooking `Environment.TickCount` would change every timer that uses it, and our repo has no managed method-replacement mechanism (no MonoPatcher in `mods/` or `native/`) [verified]. Instead the scenario avoids the 21 sim-play sites: no `spawn_sims` inside the measured window (spawn before `seed_rng`), story progression off (`-disablestoryprogression`), no pregnancies. If a later scenario needs spawned Sims inside the window, add an optional managed hook of `System.Random::.ctor()` (MonoPatcher-style, `Random(int)` with a counter-based seed) then.

**C. Native seed (optional, later, only if B leaves too much variance):** a ts3native section (off by default) that (1) exports `ts3native_seedRng(seed)` calling the MT init `0x004FA990` on host `+0xC30` (same effect as B but at the native level), and (2) patches the `RDTSC` fallbacks in `0x004FA800`/`0x004FA990`/`0x004FA7C0` to take a fixed value plus a call counter, which makes the ~50 native LCGs (routing tie-breaks, animation) seed the same way each run. Needs a counter registry slot (`docs/registry.md`) and a check that no subsystem depends on distinct seeds per object.

Expected result: B removes `RandomUtil` divergence from the window start; thread timing, native `RDTSC` seeds and frame-rate dependent tick counts still vary, so bit-exact replay remains out of scope (PLAN.md step 4 measures what is left).

## Sources

- Ghidra headless on `game-copy/ghidra` (TS3 project, read-only): `0x0074A780`, `0x007692B0`, `0x00D7F2B0`, `0x00D7E010`, `0x00D71580`, vtables `0x0108B690`/`0x0108B818`, `0x00D70EE0`, `0x004FAA70`, `0x004FA9F0`, `0x004FAAA0`, `0x004FA990`, `0x004FA800`, `0x004FA7C0`, `0x0058AD80`, `0x00D77640..0x00D776C7`, `0x00EC2820`, `0x0081BC41`, `0x00813745`; strings `0x0108B480`, `0x010F2390`.
- `game-copy/decompiled/`: `SimIFace/Sims3.SimIFace/RandomGen.cs`, `IRandom.cs`, `ScriptCore/ScriptCore/Random.cs`, `mscorlib/System/Random.cs`, and the 23 files listed in §2. `game-copy/bin/Default.ini`.
