# 35. Audio off the sim thread (M-AUD)

PLAN backlog item 7, M-AUD. Static only: Ghidra headless on a private copy of the H1 project (`game-copy/ghidra-aud`, read-only), plus the sampler files already in `probe-out/profile/`. The game was not run for this note. Steam TS3W.exe 1.67, image base `0x400000`. docs/31 is held for the x64 feasibility note PLAN item 7 already points to, so this note takes 35 (32 is the middleware inventory, 33-34 physics and SpeedTree).

Tags: **[verified]** = read in a decompile, disassembly or the exe's strings; **[measured]** = from the sample files named in §3; **[inferred]** = reasoning, still to confirm.

## Short answer

**Audio is already off the sim thread, by EA's own design. What is left on the sim thread costs about 4 ms/s (0.41% of sim-thread samples), and most of that has to stay. Not worth an offload project.**

- Mixing, decoding, streaming and the DirectSound output run on the engine's own threads: the RenderWare Audio Core **Dac** thread, a device watchdog thread and EA's audio system thread [verified].
- The sim thread only **writes commands into a byte buffer**. A lock-protected triple-buffer handoff carries them to the audio thread, and results (sound-finished callbacks) come back the same way. The sim thread never waits on the audio thread in normal play [verified]. This is the "command queue to the audio thread" design Rodney had in mind; the game already ships it.
- Sound handles are given out by the caller from a pre-filled free-id ring, so `StartSound`-style calls return at once with a handle and never wait for the audio thread [verified].
- What runs on the sim thread [measured, §3]: the per-frame audio `Update` (listener, ambience, an emitter position pass over every object that has sounds, draining results), building the command records, and the sim-side lookups some calls need (object position, lot and room of the sound). The lookups read sim state, so they must stay on the sim thread or be copied first.
- Expected gain from moving the rest: under ~2 ms/s, about 0.2% of the sim thread [inferred]. That is far below what any offload step needs to pay off, and the ~2% loss rule (offload goal) only helps a step that brings a gain. **No design sketch.**

## 1. Threads

| Thread | Created at | Entry | What it does | Source |
|---|---|---|---|---|
| `"RWAudioCore Dac"` | `0xF00D40` (Dac start; also calls `DirectSoundCreate` through `0xEFF300` and sets the cooperative level on the foreground window) | `0xF00170` -> `0xEFFE70` | Drives the RWAC mixer and pushes mix frames to DirectSound. The exe's own RWAC docs: "In realtime mode, the Dac thread is responsible for executing audio commands and producing mix frames" (string `0x1118778`) | [verified] |
| `"RwAudioCore Device WatchDog"` | `0xEDC5C0` (called from the Dac start) | `0xEDB5E0` -> `0xEDA920` | Device watchdog | [verified]; what it checks [inferred: lost/changed device] |
| EA audio system thread | `AudioSystem::Init` `0x8B3C20` (vtable slot +0x0 of base vtable `0x103B018`; game subclass vtable `0x10289E8`, `Init` override `0x867130`) | `0x8B2A00` | Loop: vtable `+0xEC` (`0x866BA0`: ambience `0x872E00`, then `0x868E70` -> `0x8BFAE0`, which takes the next command buffer and runs each command through vtable `+0x15C`), then `+0xF0` (`0x8B6CE0`: hands results back), then sleeps to a fixed period (`Sleep_Wrapper` at `0x8B2A00`). `Init` waits until this thread has finished its own init (`param_1[0x23]` handshake) | [verified] |

All three use the common EA thread wrapper `0x4E2F30` (`_beginthreadex` with entry `0x4E2E30`) [verified]. They start in `AudioSystem::Init`, whatever the `Multithreaded` setting is [verified: the thread start in `0x8B3C20` has no branch on it; that `Multithreaded` has no effect on audio is inferred]. Not audio: `0xF1C800`, started by `0xF1CF50`, is the MovieDecoder video decode thread (string `"MovieDecoder::VideoDecodeThreadFcn"`) [verified].

**Main vs sim thread.** The game-side audio `Update` (`0x865E10`, subclass vtable slot +0x8, calls the base `0x8B4730`) runs on the **sim thread**: a sampled stack reads `0x8627C0 < 0x865E10 < 0xF5B810 < MonoScriptHost::Simulate 0xD81840` [measured], and the sim thread also writes every command from internal calls and animation events (§2). No main-thread samples exist, so main-thread audio work is not measured. The audio thread itself reads one engine global, `[0x11ECBC4]+0x74`, in `0x866BA0` [verified read; inferred to be the world manager]. So the shipped game already reads some sim/world state from the audio thread.

### 1.1 The command path (verified)

The audio system object (global `[0x11DA098]`; the game subclass is also reached as `[0x11D8C40] - 0x27BD0`) exposes a small event-writer interface (slots on vtable `0x10289E8`):

| Slot | Function | Meaning |
|---|---|---|
| +0x44 | `0x8B10D0` -> `0x8A01A0` | Begin a command record (id) in the write buffer at `this+0x114` |
| +0x48 / +0x4C / +0x50 / +0x58 / +0x5C | `0x8B10E0`, `0x8B1100`, `0x8B1110`, `0x8B1140`, `0x8B1150` | Append a float / int / int64 / pointer / blob parameter |
| +0x68 | `0x8B1180` | Close the record (`0x89F710`), run the commit hook `+0x220` (`0x866CE0`: resolves sound-name variants with an FNV hash lookup, on the caller's thread), finish (`0x89FC70`) |
| +0x6C | `0x8B11B0` -> `0x89F6D0` | Cancel the open record |
| +0x28 / +0x2C | `0x8B8BF0`, `0x8B8C60` | Pop a free sound handle from a ring that `Init` pre-fills (ids 1001-2000 and 1-1000). Caller-side, no wait |
| +0x70 | `0x8B11C0` | **Synchronous flush**: wait until the previous batch is taken, swap the buffers under the lock, then spin (`Sleep(0)`) until the audio thread acknowledges (`this+0x328`) |
| +0xDC / +0xE0 | `0x8B16D0` / `0x8B16F0` | Lock / unlock `0x103AAAC` (the mutex at `this+0xB8`) |

The handoff, per frame [verified]:

1. **Sim thread, `Update` `0x8B4730`:** advances the timed fades and emits their commands. If the inbound buffer is empty and the audio thread has posted results (`this+0x325`), it swaps them in under the lock and runs each result through vtable `+0x158` (sound finished and similar). If the write buffer has commands and the last batch was taken (`this+0x324` clear), it swaps write <-> staging under the lock and sets `+0x324`. The lock is held only for the swap; nothing waits.
2. **Audio thread, `0x8BFAE0`:** if its buffer is empty and `+0x324` is set, it swaps staging in under the lock, clears `+0x324`, runs every command (`+0x15C`), counts the batch (`+0x328`), then updates voices and writes results.
3. **Audio thread, `0x8B6CE0`:** if it has results and the last batch was taken (`+0x325` clear), it swaps them out under the lock and sets `+0x325`.

So commands reach the audio thread at most one sim frame plus one audio period late [inferred from the protocol], and results come back one sim frame later. Only the `+0x70` flush blocks. No sim-thread sample was in `0x8B11C0` (0 of 10,251) [measured], and no function that reads the audio globals calls slot +0x70 on them [verified for those functions; a caller through another pointer is not ruled out].

## 2. Sim-thread entry points into audio

"Fire-and-forget" = the caller uses no result produced by the audio thread. Every entry point below is fire-and-forget in that sense: none waits for the audio thread [verified]. The column "what the caller gets" lists the synchronous result and the sim state each call reads.

### 2.1 Script internal calls (`ScriptCore.Audio`, from `game-copy/analysis/internal-calls-map.csv`)

| IC | Address | What the caller gets | Sim state read on the caller | F&F |
|---|---|---|---|---|
| `Audio_StartSoundImpl` | `0x863B60` | **handle** (from +0x28, caller-side) | sound name string | yes (handle is local) |
| `Audio_StartSoundMP3Impl` | `0x863C20` | **handle** | path string | yes |
| `Audio_CreateSoundImpl` | `0x863D00` | **handle** | name string | yes |
| `Audio_StartSoundAtPositionImpl` | `0x863EF0` | **handle** | lot at the position (`0xC68990` on `[0x11ECBC4]`), room and level (`0xADD740`, `0xB01060`, `0xB499D0`) | yes; the lookups must stay on the sim thread |
| `Audio_StartSoundByIdImpl` | `0x863970` | constant 1 | none | yes |
| `Audio_StopSoundImpl` | `0x8639B0` | constant 1 | none | yes |
| `Audio_SetSoundPropertyImpl` | `0x8639D0` | constant 1 | none | yes |
| `Audio_PauseGroupImpl` / `StopGroupImpl` / `EnableGroupImpl` | `0x8639F0` / `0x863A50` / `0x863AB0` | void | none | yes |
| `Audio_StopSoundArrayImpl` / `SetSoundArrayGainImpl` | `0x863DF0` / `0x863E40` | 1, or 0 when the handle is 0 | none | yes |
| `Audio_StartSoundArrayImpl` | `0x864B90` | sound array pointer (built on the caller) | object manager, name strings | yes |
| `Audio_UpdateSoundArrayImpl` | `0x864D90` | 1 or 0 (built on the caller) | object manager | yes |
| `Audio_StartObjectSoundImpl` / `WithFlagsImpl` | `0x8644A0` / `0x864570` | value from the object's audio component (`0xB96150` path) | object lookup `0x939560` | yes [inferred: the decompile shows only a constant return; the register flow was not traced] |
| `Audio_StopObjectSoundImpl` / `StopAllObjectSoundImpl` / `MuteAllObjectSoundsImpl` | `0x864630` / `0x8646B0` / `0x864730` | void | object lookup | yes |
| `Audio_PlayObjectPutSoundImpl` / `GetSoundImpl` | `0x8647B0` / `0x8647E0` | void (`0x8643C0` with `"obj_put"` / `"obj_get"`) | object | yes |
| `Audio_SetEventModifiersImpl` | `0x8641E0` | void; fills modifier maps on the caller (allocates `"Audio/EventModifier*"`) | Sim skills, quality | yes |
| `Audio_SoundFinishedCallbackImpl` | `0x863DC0` | void; registers (handle, callback) through `0x866CA0` -> `0xB34440` | none | yes; the callback **runs later** on the sim thread when `Update` drains the results (`+0x158`) [inferred: the dispatch path from `+0x158` to the script delegate was not traced] |
| `Audio_InitWorldImpl` / `ShutdownWorldImpl` | `0x863B20` / `0x863B40` | InitWorld returns a value (slot +0x248); ShutdownWorld void (+0x24C) | world load and unload only | not per frame |
| `RouteManager_SetRouteSoundSpec` | `0x758320` | void; stores footstep sound names on the route | object | yes |
| `UIManager_GetCustomMusicInfoImpl` | `0x783AC0` | music file info (UI) | file system | UI only, not traced |

The script layer adds no locking or waiting of its own: in the managed samples only 3 of 15,860 had an audio IC as the innermost C# frame (`StartObjectSound` 2, `StopObjectSound` 1) [measured].

### 2.2 Animation sound events

`TrackManager_OnEventSwitch` `0x583AB0` -> sound event `0x583050` (finds the target object from the event's `"object"` / `"surface"` parameters or the actor) -> `0x9442D0` (audio component on the object, component id `[0x11E8E70]`) [verified]. `0x9442D0` drops the event when the object is beyond a squared-distance limit (`[0x1043644]`) from a reference point from `0x96B820` [inferred: the camera or listener], when more than 7 sounds are already queued on it, and in some TV cases. Otherwise it takes a handle (+0x2C) and writes one command with the object id, its position (`0x959640`), lot and room (`0xC62D40`, `0xC89590`, `0xB01060`, `0xB02C20`) [verified]. The handle is kept on the component (`0x943BF0`, `0x944060`) for later stop/position commands. **Fire-and-forget**, but it reads object, lot and room state in event order inside the animation pass (N7, docs/22 §1a step 1a).

### 2.3 Per-frame listener and emitter updates

From `Update` `0x865E10`, on the sim thread every Simulate pass [verified code, measured thread]:

- Listener: copies the camera/listener transform (vtable +0x18 -> `0x8627C0`), then ambience `0x862C20` (calls each ambience emitter's +0x34/+0x30).
- Emitters: walks the object-sound map (`this+0x9EF5*4`), looks each object up (`0x939560`), takes its audio component and calls `0x9436A0`. That reads the object's position (`0x959640`) and, only when it moved past a threshold, writes a position command for each playing sound (`0x9433A0`). **Fire-and-forget**, but it reads every sounding object's transform: that read must stay on the sim thread (or come from a copied snapshot).

### 2.4 Streaming, decoding and the resource lock

- Stream and RAM sample playback, decoding (`"Mixer Decoder Instance Data"` `0xF0F8A0`), resampling and mixing live in RWAC (`0xEC0000`-`0xF22000`) and run on the Dac thread [verified strings and thread; that every decoder call happens on the Dac thread is inferred from the realtime-mode docs string].
- **Resource lock `0xFB2CD0`:** its 22 code references are all in the resource manager (`0x4AFF70`-`0x4B2EC0`); none is in an audio range [verified]. No sim-thread sample had a resource-manager frame under an audio frame (0 of 10,251) [measured]. How sample data is loaded from `.package` files (`"Audio/FactoryBinarySampleData"`, `"Audio/codecbuffer"` in `0x8BE390`) and on which thread was not traced [to find]. On the sim thread it is not visible in the samples.

## 3. Cost on the sim thread [measured]

Sources: all 16 non-empty `probe-out/profile/*simstack*.jsonl` files (10,251 sim-thread samples, 5 Hz, so 1% is about 10 ms/s, as in docs/28) and all 21 non-empty `*managed*.jsonl` files (15,860 samples). Self = the leaf-most TS3W frame's function (`game-copy/analysis/functions.csv`), as in `tools/harness/sample-split.ts`. The script for this count stayed in the session scratchpad.

Audio native code was taken as the functions starting in `0x860000`-`0x873000` (game audio layer: ICs, `Update`, listener, ambience, object-sound map), `0x89F000`-`0x8C5000` (event buffer and EA audio system), `0x943000`-`0x945000` (object audio component) and `0xEC0000`-`0xF22000` (RWAC) [inferred bounds, from the `"Audio/..."` and `rw::audio` string references and the functions read above].

| World | Samples | Audio self | Share | ~ms/s |
|---|---|---|---|---|
| BP | 4,077 | 15 | 0.37% | 3.7 |
| IP | 4,451 | 14 | 0.31% | 3.1 |
| SV | 1,723 | 13 | 0.75% | 7.5 |
| **All** | **10,251** | **42** | **0.41%** (95% interval 0.29-0.53%) | **~4** |

- By part: game layer 22, EA audio system and event buffer 14, object component 4, RWAC 2. Top functions: ambience `0x862C20` 7, `Update` base `0x8B4730` 4, event write `0x8A0210` 2, animation sound `0x9442D0` 2.
- The 2 RWAC samples (`0xEC17A0`, `0xF20C20`) are the only ones in the mixer range, so mixing does not run on the sim thread [measured].
- Inclusive numbers (any of the first 3 TS3W frames in audio: 1.7-2.4%) are inflated by the stack scan (stale return addresses, docs/20 §6) and are not used.
- Managed side: audio ICs are the innermost C# frame in 0.019% of samples. Gameplay code with "Stereo/Music" in its name shows up in 1.8% of managed samples (BP 2.6%), almost all `Stereo.AddEnjoyingMusicCallback` from a `BroadcastCallback` with `System.Object.GetType` as the leaf. That is broadcaster gameplay logic (interaction add/remove by type), not audio output, so it cannot move with audio. It is logged as a script-side hot spot for the broadcaster work (docs/04a), not researched here.
- Side note: the Mono "runtime" range `0xE20000-0xEF0000` in docs/20 and `sample-split.ts` overlaps RWAC from `0xEC0000` on [inferred from the string references]. With 1 sample in `0xEC0000-0xEF0000` the effect on the Mono runtime row (N2) is nil.

## 4. Verdict

| Part | On sim thread now | Movable? | Why |
|---|---|---|---|
| Mixing, decoding, streaming, DirectSound | no | already off | Dac thread [verified] |
| Command delivery, sound-finished results | no, only a lock held for one buffer swap per frame | already off | triple-buffer handoff [verified] |
| Handles | caller-side ring pop, no wait | already deferred | §1.1 |
| Command building (`+0x44`..`+0x68`, name-variant lookup `0x866CE0`) | yes, ~1 ms/s [inferred from the 14 + part of the 22 samples] | no gain | it *is* the queue write; moving it means writing another queue |
| Lot, room and position lookups for positional sounds (§2.1, §2.2) | yes | no | read sim and world state in event order (blockers S and O in docs/28) |
| Emitter position pass in `Update` (§2.3) | yes, part of ~1-2 ms/s | only with a transform snapshot | reads every sounding object's transform; a copy of positions for the audio thread would cost about what it saves [inferred] |
| Ambience `0x862C20` | yes, the biggest single item (7 samples, ~0.7 ms/s) | maybe | not read in enough detail to know what it touches [to find]; too small to chase |

**Gain:** if everything movable left the sim thread, about 1-2 ms/s (0.1-0.2%) [inferred], inside the noise of a bundle run. The offload goal accepts a stable step that costs up to ~2%; it does not make a step worth building when the best case is ~0.2%. **Audio is negligible for sim-thread stutter. No design sketch, no ts3native switch.**

What would change this: a profile where `0x8B11C0` (the blocking flush) or a resource-manager frame under an audio frame shows up on the sim thread, or SV-like worlds where the audio share grows past ~1% with many sounding objects. Both can be checked in any future `/simstack` run with no new probe: count self samples in the ranges in §3.

## Sources

- Ghidra (private copy `game-copy/ghidra-aud`): `AudioSystem::Init` `0x8B3C20`, shutdown `0x8B0AE0`, audio thread `0x8B2A00`, `0x8B4730`, `0x8BFAE0`, `0x8B6CE0`, `0x8B11C0`, vtables `0x10289E8` / `0x103B018`, Dac start `0xF00D40`, watchdog `0xEDC5C0`, thread wrapper `0x4E2F30`, the IC bodies in §2.1, `0x583050`, `0x9442D0`, `0x9436A0`, `0x865E10`.
- Exe strings: RWAC plug-in docs (`0x1113DB8` Dac, `0x1118778` realtime mode, `0x1117910` latency up to 300 ms on PC, `0x1107F68` "RWAudioCore mutex"), `"RWAudioCore Dac"`, `"RwAudioCore Device WatchDog"`, `rw::audio::core::CommandBuffer` (`0xEDC210`).
- Samples: `probe-out/profile/*simstack*.jsonl`, `*managed*.jsonl` (local, not in git).
- Related: docs/22 §1a (clip events), docs/28 N7 and N19, docs/20 §2 and §6.
