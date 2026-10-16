# 47. UTFWin window tree and window manager (M-UI2a)

Static map of TS3W.exe (Steam 1.67) on a Ghidra copy with our symbols applied. No game runs. Sim-thread cost comes only from the existing `probe-out/profile/*simstack*.jsonl` files (35 files, 10,251 samples, base and fix configs mixed). 1% of sim samples is about 10 ms/s (docs/28).

Tags: **[verified]** = read in a decompile, the disassembly, the call graph or the exe's strings; **[measured]** = from the sample files named above; **[inferred]** = reasoning, still to confirm.

## 1. Summary

- **The window manager is a producer/consumer split, not a shared tree.** The sim thread owns the window tree: message processing, layout, transforms, hit tests, effects and *painting* (recording renderables into display lists) all run on the sim thread inside `UIService_SimUpdate 0x7F1530` and the UI internal calls. The main thread never walks the tree: it adopts the last published display lists and draws them [verified: decompiles of `WindowManager_Draw 0x50B270`, `UTFWin_DrawDisplayLists 0x50EB20`].
- **One lock, the WindowManager lock, joins the two threads** [verified: all 10 call sites]. The lock object is `ecx`: WindowManager `+0x38` in the draw `0x50B270`, `+0x678` in `PostMessage 0x50D660`. `0xFBA5CC` is **not the lock** but the static `-1` that all 10 sites pass as `Lock_Acquire`'s timeout pointer `[esp+4]` [verified asm; correction from the docs/15 [Verify] follow-up and docs/data/results/2026-10-15-verify-findings.md]. The rest of this doc names the lock by that call-site constant. Sim side: queue swap in `ProcessMessages`, publish in `UpdateVisuals`, `PostMessage`. Main side: the whole UI draw, PendingTextures drain included.
- **The 3D scene is drawn inside that lock.** The scene window's renderable (`SceneWindow_Draw 0x7EB6D0`, slot `+0xC` of vtable `0x101D100`) calls `Renderer_BeginFrame`, `Renderer_DrawScene`, `Renderer_DrawMainPass` and `Renderer_EndFrame`, and it is reached from the display-list walk in `0x50EB20`, which `WindowManager_Draw` runs while it holds `0xFBA5CC` [verified: lock and call chain; that the scene window's display list carries this renderable is inferred from the slot]. So the main thread holds the UI lock for the whole scene draw and its render-job waits (docs/46). This confirms docs/41 open item 2.
- **The sim thread could block there, but the samples show no such wait:** 0 of 10,251 sim samples sit in a `Lock_Acquire(0xFBA5CC)` call (5 sites) [measured]. At 5 Hz sampling that puts it below ~0.03% (about 0.3 ms/s), so it is a risk to watch, not a measured cost.
- **`+0x24` = `WindowManager_ProcessMessages 0x50D700`** (message pump plus a per-tick message `0xC` to every registered window). **`+0x28` = `WindowManager_UpdateVisuals 0x50DE70`** (layout and transform pass, repaint of dirty windows into new display lists, then the publish under the lock) [verified].
- **The main-thread slot `UTFWinService_Update 0x723EE0` is only the clipboard pump** (`Services/UTFFoundation/EAClipboard/Clipboard`, lock `0xFB8EAC`). It does no drawing and no input [verified]. UI drawing comes from `Frame_RenderAndPresent 0xEC9F00`, which calls window manager slot `+0x30` (docs/41).
- **PendingTextures is drained on the main thread** at the start of every UI draw (`0x50B270` -> `UTFWin_PendingTexturesDrainOnce 0x50A920` -> `0x50A860`). The drain swaps the list out under `0xFBA4D8`, then calls slot `+0x10` (create) or `+0x14` (release) of each texture object, then `Release` [verified]. This closes docs/43 open item 1.
- **Cost:** UTFWin core is 1.19% leaf-self of sim samples (122/10,251) [measured]. 15% of it comes in through `UpdateVisuals` (`0x7F1553` return), 11% through synchronous `Window_SendMessage` from script-host code (`0xD7F8B0`), and about 70% has no reliable caller frame (managed UI ICs and callbacks, §5). `ProcessMessages` shows 0 real samples: its 62 stack hits are a stale return address.

## 2. Objects and classes

### Window manager (`[0x11CD080]`, getter `0x50AB70`, setter `0x50AB80`)

Constructor `WindowManager_ctor 0x50B6F0` stores vtable `0xFBA698` and creates the queues tagged `UI/UTFWin/MessageQueue`, `...Active`, `...Processing`, `...Finised` (sic), `...Free` and `ModalWindowQueue` [verified]. Fields used below [verified unless marked]:

| Offset | Use |
|---|---|
| `+0x04` | root window |
| `+0x08` | dirty-window list (visual flag `0x1F8` bit 1) |
| `+0x10` | windows that get the per-tick message `0xC` |
| `+0x18` | transform-only list (rewrites the double-buffered display-list transform) |
| `+0x28`, `+0x2C` | render contexts `UpdateVisualsRC` (sim) and `PlotRC` (main) |
| `+0x68`, `+0x69` | "new visuals published", "incremental" flags |
| `+0x6C` | published display lists (sim writes them under the lock, main adopts them) |
| `+0x74` | retired display lists (freed by the sim thread outside the lock, `0x50B640`) |
| `+0xB0` | main-thread draw list (written only under the lock) |
| `+0xD8` | message observer (slot `+0xC` called before and after each dispatch) |
| `+0xE8` / `+0xF4` / `+0x10C` / `+0x118` | active / processing / finished / free message queues; `+0x124` free count, capped at 16 |
| `+0x6A8` | current-dispatch chain (re-entrancy guard for `SendMessage`) |
| `+0x6B0` + `n*0x2C`, 10 entries | deferred message slots (capture/focus style [inferred]) |

Vtable `0xFBA698` (39 slots), the slots that matter:

| Slot | Function | Thread | What it does |
|---|---|---|---|
| `+0x00` | `0x50E3B0` `WindowManager_Shutdown` | any | clears the queues, frees the display lists |
| `+0x08` | `0x50DBD0` `WindowManager_DetachWindow` | sim | recursive detach (`0x50C7E0` sends message `0x11`, clears the deferred slots) |
| `+0x14` | `0x50B8D0` | caller | synchronous send (calls `Window_SendMessage`) [inferred from the callee] |
| `+0x18` | `0x50D660` `WindowManager_PostMessage` | any | copies a 7-dword message into the active queue **under `0xFBA5CC`** |
| `+0x24` | `0x50D700` `WindowManager_ProcessMessages` | sim | see §3 |
| `+0x28` | `0x50DE70` `WindowManager_UpdateVisuals` | sim | see §3 |
| `+0x30` | `0x50B270` `WindowManager_Draw` | main | see §3 |
| `+0x34` | `0x50B300` `WindowManager_AdoptVisualsLocked` | main [inferred] | `0x50B1C0` under the lock |
| `+0x38` | `0x50A990` `WindowManager_PostMessageNoTarget` | any | `PostMessage(0, 0, msg)`; used by input `0x5989C0` and `0x723F00` |

### Windows and controls

There is no RTTI. Class names come from the allocation-tag strings `UI/UTFWin/EA::UTFWinControls::<Class>` and the constructors that store the vtables (vtable dump `0xFBA000`-`0xFBF400`, 179 vtable starts) [verified: strings and constructor stores; class-to-vtable pairing by code locality, inferred for rows without a tag in the constructor]. Each control has several interfaces, so several vtables: a 73-slot window vtable (`IWindow` [inferred name]), a 38-46-slot window-proc vtable and 6-9-slot drawable/effect vtables. Every object is reference counted (`+0` AddRef, `+4` Release on each interface) [verified in `Window_SendMessage`].

| Class | Constructor (or destructor: function storing the vtable) | Main vtables |
|---|---|---|
| Window (base, `UI/UTFWin/Controls/Window`) | `0x515C80` | `0xFBAAB8`, second interface at `+4` `0x101E8F8` |
| WinButton, ButtonDrawableStandard/Radio | `0x51A310`, `0x51A830` | `0xFBB358` (44), `0xFBB1B8` (30), drawables `0xFBB4B8`, `0xFBB534` |
| WinComboBox, ComboBoxDrawable | `0x51CC00`, `0x51C9A0` | `0xFBBC18` (46), `0xFBBB64`, `0xFBB914` |
| WinGrid (list box, sparse matrix) | `0x51CE80`, `0x52B2D0` | `0xFBCD90` (149), `0xFBD078` (149), `0xFBB968` |
| ConsoleWindow | `0x51EE30` | `0xFBBE30` (73), `0xFBBF58` |
| WinDialog, DialogDrawable | `0x521C60` | `0xFBC258` (73), `0xFBC380`, `0xFBC178` |
| WinMessageBox (`msMessageText`) | `0x5343E0` | `0xFBDD60` (73), `0xFBDE88` |
| WinScrollbar, ScrollbarDrawable | `0x538BD0`, `0x538B60` | `0xFBEB68`, `0xFBEAB4`, `0xFBBDC0` |
| WinSlider, SliderDrawable | `0x53B240`, `0x53B1A0` | `0xFBEF90`, `0xFBEEDC`, `0xFBF07C` |
| WinSpinner | `0x53CBE0` | `0xFBF288` (73) |
| WinText, WinTextEdit, WinTreeView, WatchWindow | near `0x53ECD5`, `0x5423F5`, `0x547C25`, `0x548A45` | not paired (outside the dump) |
| Effects: Fade, Glide, Inflate, Modulate, Cascade, Perspective, StaticRotate, BiStateController | `0x5235D0` (Glide), `0x532AC0` (Inflate), `0x534840` (Modulate), `0x51ACC0` (Cascade), `0x516040` (BiState) | 41-slot proc vtables (`0xFBCA78`, `0xFBD900`, `0xFBE0A8`, `0xFBB700`, `0xFBACC0`) |
| Layouts: Simple, Proportional | `0x533130` | `0xFBDB18`, `0xFBDA7C` |
| Navigators: Explicit, Relative | `0x5224D0` | `0xFBC748`, `0xFBE590` |
| ImageDrawable, ImageCursorProvider | `0x531EB0`, `0x532630` | `0xFBD5A8`, `0xFBD778` |
| SceneWindow (Sims side; renderable) | `0x7EBEE0` | renderable `0x101D100`: `+0xC` = `SceneWindow_Draw 0x7EB6D0` |

Window fields [verified in the decompiles]: `+0x2C` parent, `+0x40`-`+0x4C` area rect, `+0x70` style flags (bit 0 visible, `0x10` ignores the mouse, `0x400` clips its children, `0x1000` no child hit test), `+0x150` local transform (4x4), `+0x1A0` world transform, `+0x1F4` current display list, `+0x1F8` dirty flags (bit 1 visuals, bit 2 layout, bit 3 transform), `+0x1F9` bit 3 child dirty, `+0x20C` effect object (reference held by the display list), `+0x210` child list, `+0x218` window-proc list (filtered by a message-type mask, `0x50E5B0`).

The animation types `UI/UTFWin/EA::UTFWin::AnimationCurve` and `AnimationPath2D` (vtables `0xFBA15C`, `0xFBA18C`) and the effects advance on the UI clock `0x11CD088` (`UI_SampleClock`). `UTFWinEffect_Update 0x5172E0` reads that clock and sits in seven effect vtables [verified: references; the "update" role is inferred].

## 3. Per-thread work

| Work | Function | Thread | Lock |
|---|---|---|---|
| UI clock sample | `UI_SampleClock 0x516AC0` | sim | none |
| Message pump: swap active -> processing queue, dispatch each message (target window via `Window_SendMessage 0x50B4C0`, else `WindowManager_HandleSystemMessage 0x50BE70`), recycle the message objects, flush the 10 deferred slots, send message `0xC` to every window in list `+0x10` | `WindowManager_ProcessMessages 0x50D700` | sim | `0xFBA5CC` only around the two queue splices; dispatch runs unlocked |
| System messages: resolution `0x4ECD5687` (screen scale), callback message `0x74D2E9` (calls a function pointer), mouse and keyboard routing with **hit test** `Window_HitTest 0x50AF60` (recursive over children) | `0x50BE70` | sim | none |
| Layout and transforms: on layout dirty, send message `0x14` (layout) to the window, then rebuild the world matrix from the parent, recursively | `Window_UpdateTransforms 0x50CB60` | sim | none |
| Repaint: for each dirty window, set the clip (style `0x400`), send message `0xD` (paint) with `UpdateVisualsRC`; the window procs and drawables record renderables into a new display list | `Window_PaintIntoRC 0x50CF40`, full rebuild `0x50D2F0` | sim | none |
| Publish: copy changed transforms into the back slot of the double-buffered display-list transform (`+0x30`/`+0x90`, flip byte `+0xE`), splice the new lists into `+0x6C`, set `+0x68` | tail of `UpdateVisuals 0x50DE70` | sim | `0xFBA5CC` |
| Free the retired lists | `0x50B640` | sim | none (already unlinked) |
| UI draw: drain PendingTextures, adopt `+0x6C` into the draw list `+0xB0` (`0x50B1C0`), then draw it with `PlotRC` (`0x50EB20`): per entry, effect object slots `+0x10`/`+0x14`/`+0x18`, renderable slot `+0xC`; the scene window's renderable draws the 3D frame | `WindowManager_Draw 0x50B270` from `Frame_RenderAndPresent 0xEC9F00` | main | `0xFBA5CC` for the whole draw (taken twice) |
| Input to UI: OS input events become UTFWin messages that are only posted (`+0x38`), never sent | `UIInput_ToWindowMessage 0x5989C0` (registered as handler 6 by `0x5990C2`) | main [inferred: input pump] | `0xFBA5CC` via `PostMessage` |
| Clipboard | `UTFWinService_Update 0x723EE0` -> `0x4E9800` | main | `0xFB8EAC` |
| Window ICs (`Sims3.UI`): property set, add/remove child, layout load, `GetWindowFromPoint` | `IC_UIManager_*` (docs/43) | sim | none; they call `Window_SendMessage` synchronously |

Messages seen [verified ids]: `0xC` per-tick, `0xD` paint, `0x11` detach, `0x14` layout, `0x4ECD5687` resolution, `0x74D2E9` callback.

**Tooltip:** no native tooltip timer was found. Tooltips are managed (`Sims3.UI.TooltipManager.Simulate`, 0.52% managed self, docs/43) and use the per-tick message and window ICs [inferred].

## 4. Shared state, locks and races

| State | Writers | Readers | Protection | Verdict |
|---|---|---|---|---|
| Message queues `+0xE8`... | any thread (`PostMessage`), sim (pump) | sim | `0xFBA5CC` | safe [verified] |
| Published display lists `+0x6C`, flags `+0x68/+0x69` | sim (publish) | main (adopt) | `0xFBA5CC` | safe [verified] |
| Display-list transform slots | sim (`+0x18` list, back slot) | main (draws the front slot) | `0xFBA5CC` on both sides | safe [verified] |
| Draw list `+0xB0` | main | main | `0xFBA5CC` | safe |
| Effect objects (`window+0x20C`, held by each display-list entry) | sim (effect update on the UI clock, ICs) | main (slots `+0x10`/`+0x14`/`+0x18` during the draw) | reference count only; the sim side writes without the lock | **race R1** [inferred]: effect parameters (alpha, offset, scale) can be read half-updated. Lifetime is safe (refcount); worst case is one torn frame value, not a crash, unless an effect slot frees or reallocates internal buffers |
| Renderables in a display list (vertex, glyph and texture references) | sim (paint), then frozen | main | handed over by the publish | safe while renderables do not point at live window data [inferred: they are built into the RC's own pools, `UI/UTFWin/Renderable2DPool`, `Verts`, `GlyphInfos`] |
| UI textures | sim (`LoadUIImage` queues the create), main (drain creates it) | main (draw) | `0xFBA4D8` for the list | safe for UI images; the ThumbnailManager path that bypasses the list is the docs/43 crash lead, unchanged |
| Window tree (`+0x210`, styles, rects) | sim (ICs, pump, layout) | sim | none needed | single-threaded, **no cross-thread race in the tree itself** [verified: no main-thread reader found; the main-thread callers of `UTFWin_GetWindowManager` are `Frame_RenderAndPresent` (`+0x30`) and the input post `+0x38`] |
| `0x598CC0` (cursor and window calls, from `IC_UIManager_FreezeMouseImpl` and `0x598F90`) | ? | window slots `+0x84`, `+0x9C`, `+0x40` | none | **open:** if `0x598F90`'s callers (`0x96xxxx`, camera/input [inferred]) run on the main thread, this is a direct main-thread touch of window objects. Check the thread before relying on "sim owns the tree" (M-UI4) |

**Sim waits on main (S-UI, like docs/41 S1):** `ProcessMessages` (twice), `UpdateVisuals` (once) and every `PostMessage` take `0xFBA5CC`, which the main thread holds through the whole UI draw, including the 3D scene draw and its render-job waits. When the sim tick and the main draw overlap, the sim thread stalls for up to one scene draw (~10-30 ms on a heavy lot [inferred from docs/41 frame times]). In the existing samples this never shows (0 hits, §1) [measured], so it either almost never overlaps or the wait is short. It is a stutter lead to confirm with a lock-wait counter, not a measured cost.

## 5. Sim-thread cost (existing samples, leaf method)

Method: docs/45 §6. A sample counts for UTFWin when its leaf-most TS3W frame lies in `0x505380`-`0x54B800`. Function entries come from `game-copy/analysis/functions.csv`. Callers are attributed by specific return addresses: `0x7F154A` (after the `+0x24` call) and `0x7F1553` (after `+0x28`), read from the bytes of `0x7F1530`.

- **UTFWin leaf self: 122 / 10,251 = 1.19%** (~12 ms/s) [measured]. Top leaves: `Window_SendMessage 0x50B4C0` 23, `Window_RebuildVisualsRecursive 0x50D2F0` 19, `Window_UpdateTransforms 0x50CB60` 14, `Window_HitTest 0x50AF60` 13, `KeyList_Find16 0x5492E0` 13 (a generic 16-byte key list search with callers outside UTFWin, `0x72DAD0`; it only sits in the range), the rest under 5 each.
- **By caller** (nearest frame outside UTFWin; the IC row overlaps the others, so the rows are approximate):

| Way in | Samples | % of sim | Note |
|---|---|---|---|
| `UpdateVisuals` (`0x7F1553` return in the stack) | 18 | 0.18% | layout, transforms and repaint; real (consistent chain) |
| `0xD7F8B0` (script-host block) -> `Window_SendMessage` | 14 | 0.14% | synchronous sends from managed code (UI ICs, managed window procs) [inferred role of `0xD7F8B0`] |
| `0xF908F0` frame (CRT block) | 20 | 0.20% | no further caller; likely stale or a CRT helper [inferred] |
| named IC frame within 25 slots | 28 | 0.27% | `IC_UIManager_WindowScreenTransformImpl`, `LoadLayoutImpl`, others; several are stale (`IC_Route_Plan`, `IC_Query_GetObjectsOnLot`) |
| no usable frame | ~42 | 0.41% | managed callbacks, which leave no native caller frame |

- **`ProcessMessages` (`+0x24`): 0 real samples.** `0x7F154A` is on 62 stacks (0.60%), but none has a UTFWin leaf, and the frames below it are `UI_SampleClock`, the GC stop function and unrelated code. The address is stale: the `+0x28` call reuses the same stack slot, and the slot is not cleared afterwards. This is the docs/45 caveat again. The managed handlers that the pump triggers show up on the managed sampler instead (UI ICs 1.97% managed self, docs/43).
- **Lock waits on `0xFBA5CC`: 0 samples** [measured].

So the "~1.2% via virtual calls" from docs/43 splits roughly into: ~0.2% the per-tick `UpdateVisuals` pass, ~0.15-0.4% synchronous messages from script (UI ICs and managed window procs), and the rest unattributable (managed callbacks, stale frames). No single native hot spot is above 0.25%.

## 6. Candidates

All of these are switches that default off. The costs are small, so the crash and stall items rank first.

1. **UI-L1: measure `0xFBA5CC` wait per thread** (tooling, cheap). A ts3native counter on `Lock_Acquire` calls whose timeout pointer `[esp+4]` is `0xFBA5CC` (0.10.6 compared `ecx` with it and never matched; 0.10.7 filters on the timeout pointer, docs/15): wait time on the sim thread, and how long the main thread holds the lock in `0x50B270`. Settles the stall lead in §4. Repro: a heavy lot with the HUD open, camera moving.
2. **UI-L2: draw outside the UI lock** (only if UI-L1 shows sim waits). In `0x50B270`, keep the drain and adopt (`0x50A920`, `0x50B1C0`) under the lock and run `0x50EB20` without it. Blocker: the sim thread writes the display-list *back* transform slot under the lock while main reads the front one, and adopt already happened. That looks safe once the flip byte `+0xE` is read at adopt time, but verify that `0x50EB20` reads only the front slot and that the `+0x18` transform path cannot free entries. Alternative: take the scene window's draw out of the locked walk (draw the 3D scene before the UI pass).
3. **UI-T1: throttle `UpdateVisuals` to the render rate.** When no frame was adopted since the last publish (`+0x68` still set), skip the repaint and publish. Saves at most ~0.2% sim [measured upper bound]. Low value. The incremental path (`+0x69`) already merges the lists, so check that skipping does not lose dirty windows.
4. **UI-R1: effect race.** Probe: count frames where a main-thread effect slot runs while the sim thread is in `UTFWinEffect_Update 0x5172E0`. Fix only if a crash in an effect slot shows up in the crash ledger. No change otherwise.
5. **Not candidates:** moving the window tree updates to the main thread (that would put ~1% of sim work on the main thread and still needs every UI IC to be marshalled, for no gain), and a second thread for layout (the tree has no locks and the ICs mutate it synchronously).

## 7. Open items

- Thread of `0x598CC0`/`0x598F90` (window calls outside the IC path), M-UI4.
- Role of `0xD7F8B0` (script-host bridge that sends window messages) and of the 10 deferred slots `+0x6B0`.
- Whether any effect slot (`+0x10`/`+0x14`/`+0x18`) allocates or frees memory (that would make R1 a crash lead).
- WinText, WinTextEdit, TreeView and WatchWindow vtables (outside the dump range), M-UI3 and M-UI5.

## 8. Names

35 rows appended to `tools/ghidra/symbols/ts3w-1.67.symbols.csv`, tagged `<docs/47>`, applied to `game-copy/ghidra-ui2a`.
