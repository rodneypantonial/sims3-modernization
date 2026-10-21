Sims 3 Modernization v{VERSION}
==============================

Less stutter for The Sims 3: a script mod (ts3fixes.package) and a native
plugin (ts3native.asi + ts3native.ini). Each works on its own; install both
for the full effect.

Works with: The Sims 3 from Steam, patch 1.67, Windows 10 or 11, any mix of
expansion and stuff packs. NOT supported: the Origin / EA app version and
pirated copies (the plugin checks the game file and switches itself off on
any other version; it then does nothing).

What's in this download
-----------------------
  Mods\Packages\ts3fixes.package    the script mod
  Game\Bin\ts3native.asi            the native plugin
  Game\Bin\ts3native.ini            its settings (release defaults)
  README.txt                        this file

Install
-------
1. Close the game.
2. Back up your saves: copy the folder
     Documents\Electronic Arts\The Sims 3\
   somewhere safe.
3. Script mod:
   a. Open Documents\Electronic Arts\The Sims 3\. If there is no Mods folder,
      create Mods, and inside it Packages.
   b. If Mods has no Resource.cfg file, create one (a text file named exactly
      Resource.cfg) with these four lines:
        Priority 500
        PackedFile Packages/*.package
        PackedFile Packages/*/*.package
        PackedFile Packages/*/*/*.package
      If you already have one, keep it.
   c. Put ts3fixes.package in Mods\Packages\ (replace an older copy).
4. Native plugin:
   a. Find the game folder: in Steam, right-click The Sims 3 > Manage >
      Browse local files, then open Game\Bin.
   b. ASI loader (not included): if you already use Sims 3 Settings Setter
      or another .asi mod, you have one; skip this step. Otherwise download
      Ultimate ASI Loader (Win32 / 32-bit zip) from
        https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases
      take dinput8.dll out of it, rename it to wininet.dll and put it in
      Game\Bin.
   c. Put ts3native.asi and ts3native.ini in Game\Bin (replace older copies;
      use the new ts3native.ini, an old one keeps the new fixes off).
5. Start the game from Steam. If the game asks whether to enable script mods,
   say yes (or turn on Script Mods in the game options).
6. Check: after the first start, Game\Bin has a new file ts3native.log.

Uninstall
---------
Close the game and delete what you added: ts3fixes.package from
Mods\Packages\, ts3native.asi and ts3native.ini from Game\Bin (and
wininet.dll if you added it only for this). Saves are not changed by
installing or removing.

Settings
--------
Everything in ts3native.ini that the release does not list as on is off
(experiments). To switch background route planning off, set Enabled=0 under
[RouteAsync]. Change other switches only to help test, and keep backups.

Problems and crash reports
--------------------------
Remove our files first and see whether the problem goes away. Then open an
issue: https://github.com/rodneypantonial/sims3-modernization/issues
An opt-in crash report ([CrashReport] Enabled=1 in ts3native.ini) writes a
text file you can read first; nothing is ever sent without you saying yes.
See docs/PLAYERS.md in the repository for the steps.

Source code, full notes and measurements:
  https://github.com/rodneypantonial/sims3-modernization
Code: MIT license. Not affiliated with or endorsed by Electronic Arts or Maxis.
