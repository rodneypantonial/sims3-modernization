# The Sims 3 baseline setup: Steam with all expansions, Alienware m15 R6

**Goal:** get the best result from **existing** fixes and measure each step, so we know what our own patches (steps 3 and 4) need to beat.

**Method:** add **one layer at a time**, then run the same test after each one. If something breaks, you'll know which layer caused it.

See `01-existing-fixes-comparison.md` for why each fix is here.

---

## 0. Before you start (10 minutes)

1. **Back up your saves and settings.** Copy the whole `Documents\Electronic Arts\The Sims 3\` folder somewhere safe.
2. **Find the exe the game actually runs.** Start the game. In Task Manager, right-click **TS3W.exe** and choose **Open file location**. That's your **`Bin` folder** for the rest of this guide. On Steam it's normally:
   `C:\Program Files (x86)\Steam\steamapps\common\The Sims 3\Game\Bin\`
   (or the same path under your Steam library drive).
3. **Back up the exe.** Copy `TS3W.exe` to `TS3W.exe.orig`.
4. **Move your Mods folder out of the way** for the vanilla measurement by renaming `Mods` to `Mods_off`. You'll restore it in layer 5.
5. **Update the Nvidia driver.** A Studio or Game Ready driver from nvidia.com is better than the Dell-bundled one.

---

## 1. Pick a fixed test case

Use the **same save, the same lot and the same speed** every time:

- **Save:** your normal heaviest save with all expansions. If you'd rather keep it untouched, use a copy.
- **Test A, load time:** time it from clicking Play in the launcher to being in Live mode with control of your household, using a stopwatch.
- **Test B, idle FPS:** in Live mode at your household lot, set speed 3, keep the camera still for 60 seconds and note the average FPS.
- **Test C, travel and streaming:** pan the camera across town to the busiest community lot and back. Note any hitches.
- **Test D, memory:** after 30 minutes of play, note TS3W.exe's **Commit size** in Task Manager (Details tab → right-click the column header → Select columns → **Commit size**). Process Explorer's **Virtual Size** column is better if you have it. Anything near 3.5–4 GB is the danger zone for Error 12.
- **Test E, saving:** time a save and note whether it succeeds.

Copy this table into a note and fill one row per layer:

| Layer | Load (s) | Idle FPS | Hitches (C) | Memory @30 min | Save OK / time | Crashes / notes |
|---|---|---|---|---|---|---|
| 1 Vanilla | | | | | | |
| 2 GPU + Windows | | | | | | |
| 3 + DXVK | | | | | | |
| 4 + S3SS | | | | | | |
| 5 + Mods/NRaas | | | | | | |
| 6 + Performance Patch (optional) | | | | | | |

---

## 2. Layer 1: vanilla measurement

1. **Clear the caches.** Close the game, then in `Documents\Electronic Arts\The Sims 3\` delete:
   - the files `CASPartCache.package`, `compositorCache.package`, `scriptCache.package`, `simCompositorCache.package` and `socialCache.package`
   - the **contents** of the folders `WorldCaches\`, `Thumbnails\`, `FeaturedItems\` and `DCCache\` (only `dcc.ent` and `missingdeps.idx`)
2. **Check DeviceConfig.** Open `Documents\Electronic Arts\The Sims 3\DeviceConfig.log` and note what it says for the GPU, `Texture Memory` and `Found: …` / `Matched: …`. If it shows **Intel UHD**, the game is on the wrong GPU, which layer 2 fixes.
3. **Check the LAA flag.** Download **CFF Explorer** (NTCore Explorer Suite), open `TS3W.exe`, go to **File Header → Characteristics** and see whether **"App can handle >2gb address space"** is ticked.
   - If it's **ticked**, good. Write down "LAA: yes".
   - If it **isn't ticked**, write down "LAA: no". You'll tick it in layer 2.
4. Run tests A–E and fill in the "1 Vanilla" row.

---

## 3. Layer 2: force the RTX GPU and tune Windows

1. **Alienware Command Center.** Set the thermal profile to **Performance** while you're plugged in. If your model shows a **Hybrid / Discrete graphics** switch (Advanced Optimus or MUX), choose **Discrete** and reboot.
2. **Windows graphics settings.** Go to Settings → System → Display → Graphics → Add desktop app, and choose `TS3W.exe` from your Bin folder. Then go to Options → **High performance (NVIDIA)**. Do the same for `Sims3Launcher.exe` in the same folder.
3. **Nvidia Control Panel.** Go to Manage 3D settings → Program Settings → add `TS3W.exe`, and set:
   - Preferred graphics processor: **High-performance NVIDIA processor**
   - Max Frame Rate: **60**, which saves heat and keeps the sim smooth (you can drop this later once DXVK caps it)
   - Power management mode: **Prefer maximum performance**
   - Vertical sync: **Use the 3D application setting**
4. **Turn off the Steam overlay** for The Sims 3 (Steam → right-click the game → Properties → General).
5. **Set LAA if it's missing.** Only do this if layer 1 showed "LAA: no". In CFF Explorer, tick the box, save, and launch once to make sure the game still starts. *If Steam's "Verify integrity" ever replaces the exe, check this again.*
6. Clear the caches as in layer 1. Check that `DeviceConfig.log` now names your RTX card. Run tests A–E.

---

## 4. Layer 3: DXVK (the biggest fix for Error 12)

1. Download the latest release from https://github.com/doitsujin/dxvk/releases (the `dxvk-x.y.z.tar.gz` file; 7-Zip opens it).
2. From the **`x32`** folder, copy **only `d3d9.dll`** into your Bin folder.
3. Create a text file called `dxvk.conf` in the Bin folder with this content:
   ```ini
   # Cap FPS so the laptop doesn't run flat out (replaces the Nvidia cap)
   d3d9.maxFrameRate = 60
   # Show FPS and memory on screen while we're measuring; delete this line later
   dxvk.hud = fps,memory
   # Better texture filtering at almost no cost
   d3d9.samplerAnisotropy = 16
   ```
   Don't touch `d3d9.textureMemory`. The default is what saves address space.
4. If you set Max Frame Rate in the Nvidia Control Panel, set it back to **Off** so the two caps don't fight.
5. Launch the game. The HUD should appear in the top-left corner. **The first launch and the first few minutes may stutter** while shaders compile, so do one warm-up play session before you measure.
6. `DeviceConfig.log` should now say **GeForce GTX 580**. That's DXVK's built-in Sims 3 profile, and it's expected. In-game, set the graphics options to High or Very High as you like.
7. Run tests A–E and fill in row 3.

**If it doesn't launch:** delete `d3d9.dll` and you're back to layer 2. A `TS3W_d3d9.log` file in the Bin folder will say why. Send it here.
**If DXVK picks the Intel GPU** (the HUD shows Intel), add `dxvk.hideIntegratedGraphics = True` to `dxvk.conf`.

---

## 5. Layer 4: Sims 3 Settings Setter (S3SS)

1. **Install an ASI loader.** Get **Ultimate ASI Loader** from https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases, using the **Win32** (x86) build. Put its DLL in the Bin folder **renamed to `wininet.dll`**. Don't use `d3d9.dll`, because DXVK already has that name.
2. Download `Sims3SettingsSetter.asi` from https://github.com/sims3fiend/Sims3SettingsSetter/releases and put it in the Bin folder.
3. Launch the game and press **Insert** to open the S3SS overlay. Settings are saved to `Documents\Electronic Arts\The Sims 3\S3SS\S3SS.toml`.
4. In the **Patches** tab, turn on the following. Turn on **one group at a time and restart the game after each group**:
   - Group 1, safe: CPU Thread Optimization (on by default), Timer Optimization, Expanded Crash Logs, the **Memory Monitor**
   - Group 2, Smooth Patch: **Smooth Patch (Precise Flavour)**, keeping the default 480 TPS. Also download LazyDuchess's Smooth Patch **`.package`** and put it in `Mods\Packages` later, as S3SS advises. Leave S3SS's 60 FPS limiter **off**, because DXVK is already capping frames.
   - Group 3, memory: Oversized Thread Stack Fix, **Mimalloc Allocator** (needs a restart), RefPack Decompressor Optimization, Lot Streaming Optimizations
   - Group 4, experimental (try last, and keep it only if stable): GC Finalizer Throttle. **Leave "Chunky" off for now** because it raises memory use. **Leave the WorldCache uncap off** unless your world specifically needs it.
5. **If the overlay crashes at startup** next to DXVK, open `S3SS.toml` and set `disable_overlay = true` under `[qol.ui]`. The patches still apply without the overlay.
6. Run tests A–E for row 4. Note which groups were on.

---

## 6. Layer 5: bring your mods back, with NRaas

1. Rename `Mods_off` back to `Mods`, and make sure `Mods\Resource.cfg` exists.
2. Add or update the NRaas mods from nraas.net: **Overwatch** and **ErrorTrap** at minimum, plus **MasterController** if you use it. Get the builds that match patch **1.67/1.69**, which is what Steam is on.
3. Put the **Smooth Patch `.package`** from layer 4 in `Mods\Packages`.
4. Clear the caches, load your save, and let Overwatch run its first cleanup (it shows a notification). Save, then run tests A–E for row 5.

---

## 7. Layer 6 (optional): Sims 3 Performance Patch (Nexus mod 235)

This one is a beta and overlaps with S3SS, so it gets its own row.

1. In S3SS, turn **off** the Oversized Thread Stack Fix, because both mods patch thread stacks.
2. Download it from https://www.nexusmods.com/thesims3/mods/235, then put `Sims3Performance.asi` and `Sims3Performance.ini` in the Bin folder. The `wininet.dll` loader from layer 4 already loads it.
3. Clear the caches and run tests A–E for row 6. Watch for **blank or wrong UI text**, which is a known risk of its text cache.
4. If load time doesn't improve or anything looks wrong, delete both files and turn S3SS's stack fix back on.

---

## 8. Your Bin folder when you're done

```
Game\Bin\
  TS3W.exe              (original, or with LAA set)
  TS3W.exe.orig         (your backup)
  d3d9.dll              DXVK x32
  dxvk.conf
  wininet.dll           Ultimate ASI Loader (Win32)
  Sims3SettingsSetter.asi
  Sims3Performance.asi  (optional)
  Sims3Performance.ini  (optional)
```

**To undo everything:** delete the added files, copy `TS3W.exe.orig` back over `TS3W.exe` (or run Steam → Verify integrity), and restore your `Documents\Electronic Arts\The Sims 3` backup.

---

## 9. What to send back

- The filled-in results table
- `DeviceConfig.log` from layers 1 and 3
- Your LAA yes/no result
- Any `xcpt*.txt` crash logs from `Documents\Electronic Arts\The Sims 3\`
- Your final `S3SS.toml`

These numbers are the baseline our own patches (steps 3 and 4) have to beat. The crash logs and the memory-at-30-minutes figures will tell us where to aim the reverse engineering.
