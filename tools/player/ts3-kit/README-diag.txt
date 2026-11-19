TS3 diagnostic build (EA app 1.69): where do the frames go?
==========================================================

What it is
  The same ts3native as our published v0.2.12 for the EA app (same fixes, same settings), plus three
  diagnostics that only MEASURE. They change nothing in the game, your save, your mods or your settings.
  - frame timing: frames per second, how long each frame takes, and how long the game waits on the
    graphics side (the RTX Remix bridge) per frame
  - PC check: CPU speed and limits, power plan, battery or plugged in, other programs' CPU use, memory,
    and which graphics card the game and the bridge are set to use
  - mod timing: which script mods use the game's simulation time
  Everything is written to small text files in Game\Bin. The main one is fps-summary.txt.

Install with the kit (close the game first)
  1. Extract the whole zip to a folder (for example your Desktop; not inside Program Files).
  2. Double-click TS3Kit.bat, choose 3 (Install), pick your game folder, answer D (diagnostic build).
     The kit first backs up your current ts3native.asi, ts3native.ini and ts3fixes.package, then writes
     the diagnostic ones. Your other mods and settings stay as they are.
  3. The last lines must say "Install check: OK". If they say MISMATCH, send us a screenshot of that window.

Play
  Start the game the way you always do and play normally for 15-20 minutes, in a world, with your usual
  mods and settings. Nothing special to do: no settings to change, nothing to turn off.
  Then quit the game.

Send it back
  1. Double-click TS3Kit.bat again and choose 1 (Diagnose). It takes a minute or two (it also saves the
     Windows "dxdiag" report) and writes one zip to your Desktop, named TS3Diag-<date>-<code>.zip.
  2. Send us that zip. Your Windows user name, computer name and home folder are masked inside it;
     nothing is uploaded by the kit. You can open the zip and look first.

Going back
  TS3Kit.bat, option 4 (Uninstall) puts your earlier files back from the backup.
