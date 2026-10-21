# MTS listing: Sims 3 Modernization v0.2.0

Everything the upload wizard needs, in wizard order. `TO-FILL` marks what waits for the v0.2.0 results (clean night 2026-10-22, `docs/RELEASE.md` decisions, README v0.2.0 table). Rules behind each choice: `RULES.md`.

Before pasting: replace every `TO-FILL`, delete the `[TO-FILL ...]` blocks of features that did not ship, and remove the HTML comments. `node tools/mts-images.ts --final` and `grep -n TO-FILL release/mts/listing.md` must both come back clean.

## Wizard fields

| Field | Value |
|---|---|
| Game | The Sims 3 |
| Section / category | Game Mods > Miscellaneous (fallback: Game Mods > Script & Core Mods; RULES.md "Category") |
| Type (if asked) | Other |
| Title | `Sims 3 Modernization v0.2.0: Less Stutter, Background Route Planning (Script Mod + ASI)` |
| Packs required | **None** (base game). Tick no EP, SP or store pack. |
| Game version (if asked; else in the text) | Steam, patch 1.67 only |
| Main file | `release/mts/out/Sims3Modernization_v0.2.0.zip` (from `tools/mts-zip.ts`) |
| Thumbnail | `release/mts/images/thumbnail.png` (320x240) |
| Screenshots, in order | `1-features.png`, `2-benchmark.png`, `3-install.png` (1200x900 each) |
| Short description / summary (if asked) | see "Summary" |
| Description | see "Description (BBCode)" |
| Additional Credits (if asked) | see "Additional Credits" |
| Tags / keywords (if asked) | see "Tags" |

### Packs: why "None"

`ts3fixes` is compiled only against the script assemblies in the base game's `Game\Bin` (`gameplay.package`, `scripts.package`, `simcore.package`; `tools/setup/build-fixes.ts` `REFS`, `docs/BUILD.md`) [verified]. On patch 1.67 those assemblies carry the code of every expansion, which is why one script mod works with any pack mix [community]. Fixes for pack features (houseboats, Seasons, NRaas helpers) simply never fire when that pack or mod is absent [inferred from the code paths]. `ts3native` patches only `TS3W.exe`, which is the same file whatever packs are installed [verified: it checks the bytes of every site]. **Tested only with all expansion and stuff packs installed**; a base-game-only install has not been run (open question for Rodney: say so in the text, as below).

## Summary

> Less stutter for The Sims 3 (Steam 1.67): a script mod and an ASI plugin that skip work nobody can see and move route planning to a second CPU core. Measured on three towns.

## Description (BBCode)

```
[size=4][b]Sims 3 Modernization v0.2.0[/b][/size]
[i]Less stutter for The Sims 3 on modern PCs: a script mod plus a native ASI plugin. Steam version, patch 1.67 only.[/i]

[b]What it does[/b]
The Sims 3 runs its whole simulation on one CPU core, and a lot of that time goes to work you never see: townies walking across town off screen, route plans that fail and retry, nightly clean-up jobs that run in one go. This mod skips that invisible work or moves it to another core. Your own household and anything you can see behave exactly as before.

[b]What kind of mod it is[/b]
[list]
[*][b]ts3fixes.package[/b] is a [b]script mod[/b] (needs Script Mods enabled). At game start it replaces a few game methods with fixed versions; it is not a core mod and replaces no game files.
[*][b]ts3native.asi[/b] + [b]ts3native.ini[/b] is a [b]native plugin[/b] for the game's exe, loaded by an ASI loader. It checks every byte it changes first and switches that patch off if the exe is not the Steam 1.67 one. Settings are in ts3native.ini.
[/list]
Each part works on its own; together they give the most. Installing or removing it does not change your saves.

[b]Switched on in this release[/b]
Script mod:
[list]
[*]Townies you can't see skip the long walk home, to work, school, shops, parks and venues; they still do the last steps and arrive as normal.
[*]No pointless route retries for unseen townies (no route-fail animation nobody sees, a 30 sim-minute wait before the same trip again).
[*]Stuck Sims get the game's own reset or are sent home, at most once or twice a day per Sim.
[*]No leftover route data from swimming out to sea or boarding houseboats.
[*]Faster saving in big towns (14-24% faster in our tests, same save file).
[*]A lighter Sim panel: needs bars, moodlets and names no longer rebuild on every small change.
[/list]
Native plugin:
[list]
[*][b]Route planning in the background:[/b] when a Sim plans a walk or drive, the game's own planner runs on a second CPU core while the simulation goes on.
[*]Fewer script memory clean-up pauses (forced clean-ups 128 -> 1 in 6 minutes in our test).
[*]Faster boat and swimming route searches, with exactly the same results.
[*]Faster unpacking of game files, byte-identical results.
[*]The backup copy of your old save is made in the background while the game saves.
[/list]
New in v0.2.0:
[list]
[*]TO-FILL: one line per feature that passed its A/B (copy the player wording from docs/PLAYERS.md after `release-features.ts --docs`).
[/list]

[b]Measured results[/b]
10 minutes of play at speed 3 on aged 14-day saves, same seed, alternating order, one laptop (i7-11800H, 32 GB RAM, RTX 3070 Laptop GPU). "No fixes" = the same plugin with all our switches off.
[list]
[*]Stutters per minute (simulation steps of 50 ms or more), no fixes -> v0.1.4 -> v0.2.0: Bridgeport 177 -> 152 -> TO-FILL, Sunset Valley 76 -> 70 -> TO-FILL, Isla Paradiso 146 -> 117 -> TO-FILL.
[*]Route planning on the main thread (ms per second): Bridgeport 129 -> 53 -> TO-FILL, Sunset Valley 79 -> 24 -> TO-FILL, Isla Paradiso 282 -> 92 -> TO-FILL.
[*]Game time per real second: unchanged in Bridgeport and Sunset Valley (already at the speed-3 cap), Isla Paradiso 11.0 -> 12.4 min (+13%) with v0.1.4; v0.2.0 TO-FILL.
[*]Longest single freeze: TO-FILL (v0.1.4 had longer worst freezes than no fixes in two towns, up to about 2.5 s, from route plans that still ran on the main thread; v0.2.0's freeze fix targets exactly these).
[/list]
All numbers come from one machine and one set of saves. Treat them as "this is what we saw", not as a promise; reports from other PCs are welcome.

[b]Requirements[/b]
[list]
[*]The Sims 3 from [b]Steam[/b], patch 1.67, Windows 10 or 11.
[*]No expansion or stuff pack needed. Tested with all packs installed; a base-game-only install has not been tested.
[*][b]Not supported:[/b] the Origin / EA app version and pirated copies. The plugin detects a different exe and does nothing.
[*]An ASI loader for the plugin (not included): if you already use Sims 3 Settings Setter or another .asi mod, you have one. Otherwise get [url=https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases]Ultimate ASI Loader[/url] (Win32 zip) and use its dinput8.dll renamed to wininet.dll.
[/list]

[b]Install[/b]
[list=1]
[*]Close the game and back up Documents\Electronic Arts\The Sims 3\ (saves and mods).
[*][b]Script mod:[/b] put ts3fixes.package in Documents\Electronic Arts\The Sims 3\Mods\Packages\. No Mods folder yet? Create Mods\Packages and a Mods\Resource.cfg (text in README.txt in the download).
[*][b]Native plugin:[/b] in Steam, right-click The Sims 3 > Manage > Browse local files, open Game\Bin, and put ts3native.asi and ts3native.ini there. Add the ASI loader (wininet.dll) if you don't have one yet.
[*]Start the game from Steam and allow script mods when asked.
[*]Check: Game\Bin now has a ts3native.log file.
[/list]
Updating from an older version: replace all files, and use the [b]new[/b] ts3native.ini (an old one keeps the new fixes off).

[b]Uninstall[/b]
Delete ts3fixes.package from Mods\Packages, and ts3native.asi and ts3native.ini from Game\Bin (plus wininet.dll if you added it only for this). Your saves are not changed by installing or removing.

[b]Compatibility[/b]
[list]
[*][b]Tested together with[/b] DXVK, Sims 3 Settings Setter (S3SS), Smooth Patch, Sims 3 Performance Patch (S3PP), NRaas ErrorTrap and Overwatch, and simler90's Gameplay Core Mod.
[*][b]Other ASI plugins:[/b] our plugin checks the original bytes before every patch and skips a patch another plugin already changed; we mapped every address S3SS, S3PP and MonoPatcher patch, and the one default patch next to theirs (S3SS's game file decompressor) is built to work with it.
[*][b]NRaas Overwatch and ErrorTrap:[/b] TO-FILL if shipped: "their nightly inventory clean-up and the check after loading are spread over many frames; a newer Overwatch/ErrorTrap that does this itself switches our version off." We also sent these changes to NRaas: [url=https://github.com/Chain-Reaction/NRaas/pull/45]NRaas pull request #45[/url] (Overwatch) and [url=https://github.com/Chain-Reaction/NRaas/pull/46]#46[/url] (ErrorTrap).
[*][b]Other script mods:[/b] the script mod conflicts only with mods that replace the same game methods (mostly routing, going home and lot visits; full list in the source README). NRaas MasterController, GoHere and StoryProgression have not been tested with it.
[*]DXVK is not part of this mod but strongly recommended: it frees about 1.75 GB of address space and prevents many Error 12 crashes.
[/list]

[b]Known issues[/b]
[list]
[*]TO-FILL from the v0.2.0 release validation (docs/data/results/...-queue-release-validate.md).
[*]An unseen townie that skips the walk can arrive sooner than the full walk would have taken.
[*]Measured on one laptop only; numbers on other PCs may differ.
[/list]

[b]If the game crashes[/b]
First remove our files and see if the crash goes away. Then please [url=https://github.com/rodneypantonial/sims3-modernization/issues]open an issue on GitHub[/url] with what you saw. For a detailed report, set Enabled=1 under [CrashReport] in ts3native.ini: after a crash it writes a text file to Documents\Electronic Arts\The Sims 3\ts3native-crashes\ with your name and folder paths replaced by placeholders. Read it first; the optional [url=https://github.com/rodneypantonial/sims3-modernization/blob/HEAD/tools/report-crash.ts]report-crash tool[/url] shows exactly what it would send and asks before sending anything. Nothing is ever sent automatically, and the mod itself never connects to the internet.

[b]FAQ[/b]
[b]Does this fix Error 12?[/b] It helps a little (less memory churn), but the big win for Error 12 is DXVK. The Sims 3 is a 32-bit program: it can use at most 4 GB of memory no matter how much RAM you have.
[b]Why not a 64-bit version?[/b] That would need EA's source code. We can only make the 32-bit game waste less and do less per frame.
[b]Does it change my saves?[/b] Installing or removing it does not; your saves keep working without it. The optional save clean-up is off.
[b]Will Sims behave differently?[/b] Only Sims you can't see skip work; what they end up doing is the same. Your household always walks.
[b]Origin / EA app?[/b] Not supported; only the Steam exe is mapped.

[b]Credits[/b]
The script mod and the plugin are our own code. No other creator's files are included. Thanks to:
[list]
[*][url=https://github.com/sims3fiend/Sims3SettingsSetter]Sims 3 Settings Setter[/url] by sims3fiend: reference for native hooks.
[*][url=https://modthesims.info/d/658759]Smooth Patch[/url] and [url=https://github.com/LazyDuchess/MonoPatcher]MonoPatcher[/url] by LazyDuchess: the method-patching approach our script mod follows.
[*][url=https://www.nraas.net/]NRaas[/url] (Twallan, Chain-Reaction and the NRaas team): ErrorTrap and Overwatch, studied for compatibility.
[*][url=https://www.nexusmods.com/thesims3/mods/235]Sims 3 Performance Patch[/url] by Sonja92yu: compatibility testing.
[*][url=https://github.com/doitsujin/dxvk]DXVK[/url] (doitsujin and contributors) and [url=https://github.com/ThirteenAG/Ultimate-ASI-Loader]Ultimate ASI Loader[/url] (ThirteenAG): recommended, not included.
[*][url=https://github.com/Critical-Reynolds/Sims3-MCP]Sims3-MCP[/url] by Critical-Reynolds: base of our in-game test agent (not part of this download).
[*]Tools: Ghidra, REA, ILSpy, Frida.
[/list]
Made with the help of an AI coding agent (Claude Code); every change is tested and measured in game before it ships.

[b]Source code and updates[/b]
Source code (MIT), full release notes and every measurement: [url=https://github.com/rodneypantonial/sims3-modernization]github.com/rodneypantonial/sims3-modernization[/url]. The same files are on the [url=https://github.com/rodneypantonial/sims3-modernization/releases]GitHub releases page[/url].

[size=1]Not affiliated with or endorsed by Electronic Arts or Maxis. Use at your own risk and keep backups of your saves.[/size]
```

## Additional Credits

```
sims3fiend (Sims 3 Settings Setter), LazyDuchess (Smooth Patch, MonoPatcher), Twallan, Chain-Reaction and the NRaas team, Sonja92yu (Sims 3 Performance Patch), doitsujin (DXVK), ThirteenAG (Ultimate ASI Loader), Critical-Reynolds (Sims3-MCP). Made with the help of an AI coding agent (Claude Code).
```

## Tags

`performance`, `stutter`, `lag`, `fix`, `routing`, `script mod`, `ASI`, `Steam`, `1.67`, `Error 12`, `townies`, `save speed`

## Notes for Friday

- The "Switched on in this release" lists name only features on in v0.1.4 (docs/PLAYERS.md minus the feature blocks). After `release-features.ts --docs`, add the chosen v0.2.0 features under "New in v0.2.0" in player words, max ~10 lines, and the same short labels to `mts-data.json` `features.v020` (max 6, under ~38 characters each).
- Numbers: copy from the README v0.2.0 table; keep "no fixes -> v0.1.4 -> v0.2.0" order. Drop the "Longest single freeze" line if the freeze fix did not ship.
- "Tested together with": keep in step with docs/PLAYERS.md "Other mods".
