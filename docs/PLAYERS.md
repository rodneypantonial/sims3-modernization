# For players: what's switched on and how to install

This part is added to every release. It lists **everything that is switched on** in the current release, not only what changed, and how to install it step by step.

**Works with:** The Sims 3 from **Steam**, patch 1.67, Windows 10 or 11. Any mix of expansion and stuff packs. The Origin / EA app version is **not** supported (the native plugin checks the game file and switches itself off on any other version, so nothing breaks, it just does nothing).

## What's switched on

### `ts3fixes.package` (script mod)

Less stutter and more game time per second, mostly by skipping work nobody can see:

- **Townies you can't see skip the walk.** A townie far from the camera and from your Sims, going home, to work, school, a shop, a park, a venue or another home, arrives without walking or driving the whole way. The last steps and the arrival still happen, so what the Sim does next is unchanged. Your household and anyone you can see always walk. (`kGoHomeShortcut`, `kBuildingTripShortcut`, `kLotTripShortcut`)
- **Homeless townies leave quietly.** Unseen homeless townies leaving town fade out straight away instead of first walking to a random lot. (`kVirtualHomeShortcut`)
- **No pointless route retries.** An unseen townie whose route failed skips the route-fail animation nobody sees, waits 30 sim-minutes before trying the same trip again, and keeps its planned route instead of re-rolling for a subway or hover train detour. (`kNoRouteFailWalkOffscreen`, `kRouteFailureBackoff`, `kNoStationDetourOffscreen`)
- **Stuck Sims get unstuck.** A Sim frozen in place, blocked in a doorway, trapped on a spot it can't walk off, in a hidden room or failing routes over and over gets the game's own reset or is sent home, at most once or twice per Sim per day. (`kStuckWatchdog`, `kStuckWatchdogIdle`)
- **No leftovers from swimming and houseboats.** Swimming out to sea or boarding a houseboat no longer leaves dead route data behind (it piled up in Isla Paradiso saves). (`kPortalAdaptorLeak`)
- **Cheaper background work.** Music, fires and smells far from the camera check for nearby Sims less often, and walk styles are looked up faster. (`kBroadcasterThrottle`, `kWalkStyleComparer`)
- **Faster saving in big towns.** The list of objects already written into the save stays fast past 16,000 objects. Saves came out 14-24% faster in our tests, with the same save file. (`kFastRefTable`)
- **Lighter Sim panel.** The needs bars, the moodlet grid and Sim names no longer rebuild from scratch on every small change. (`kMotiveBarImageCache`, `kBuffGridInPlace`, `kSimNameCache`)

### `ts3native` (native plugin: `ts3native.asi` + `ts3native.ini`)

- **Fewer memory clean-up pauses.** The game's script memory starts bigger and grows in larger steps, so it stops to clean up far less often (128 forced clean-ups down to 1 in 6 minutes in our test). (`[GC]`)
- **Faster boat and swimming routes.** Route searches over water use a faster version of the game's own search, with exactly the same results. (`[WaterJps]`)
- **Faster unpacking of game files.** Textures, objects and other packed data unpack faster, with byte-identical results. (`[Decompress]`)
- **Saving freezes a little less.** The backup copy of your old save is made in the background while the game writes the new one. (`[SaveWrite]`)

Everything else in `ts3native.ini` is **off**: experiments and features still being tested. Only switch them on if you want to help test, and keep backups.

## How to install

You need two downloads from the release page: **`ts3fixes.package`** and **`ts3native-<version>.zip`**. You can install just one of them if you like; they work on their own.

1. **Close the game.**
2. **Back up your saves.** Copy the folder `Documents\Electronic Arts\The Sims 3\` somewhere safe (it holds your saves and mods).
3. **Install the script mod (`ts3fixes.package`):**
   1. Open `Documents\Electronic Arts\The Sims 3\`. If there is no `Mods` folder, create `Mods`, and inside it `Packages`.
   2. If `Mods` has no `Resource.cfg` file, create one (a text file, named exactly `Resource.cfg`) with this text:
      ```
      Priority 500
      PackedFile Packages/*.package
      PackedFile Packages/*/*.package
      PackedFile Packages/*/*/*.package
      ```
      Most mod guides give the same file; if you already have one, keep it.
   3. Put `ts3fixes.package` in `Mods\Packages\`. If you had an older version, replace it.
4. **Install the native plugin (`ts3native`):**
   1. Find the game folder: in Steam, right-click **The Sims 3** > **Manage** > **Browse local files**, then open `Game\Bin`.
   2. **ASI loader:** the plugin needs one. If you already use Sims 3 Settings Setter or another `.asi` mod, you have one: skip this step. Otherwise download [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) (the 32-bit / Win32 zip), take `dinput8.dll` out of it, rename it to `wininet.dll` and put it in `Game\Bin`.
   3. Unzip `ts3native-<version>.zip` and put `ts3native.asi` and `ts3native.ini` in `Game\Bin`. Replace older copies.
5. **Start the game from Steam.** On the first start with the script mod, the game asks whether to enable script mods: say yes (or turn on **Script Mods** in the game options).
6. **Check it works:** after starting the game, `Game\Bin` has a new file `ts3native.log`. For the script mod, the game's "Mod Scripts Found" message at start is a good sign.

### Uninstall

Close the game and delete what you added: `ts3fixes.package` from `Mods\Packages\`, and `ts3native.asi` and `ts3native.ini` from `Game\Bin` (plus `wininet.dll` if you added it only for this). Your saves are not changed by the install or the removal.

### Other mods

Tested together with DXVK, Sims 3 Settings Setter, NRaas (ErrorTrap, Overwatch), the Smooth Patch and the Sims 3 Performance Patch. If something looks wrong, remove our files first and see if it goes away, then [open an issue](https://github.com/rodneypantonial/sims3-modernization/issues) with what you saw.
