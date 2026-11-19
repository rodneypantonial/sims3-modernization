TS3 kit 1120.e1: ts3native for the EA app (TS3.exe 1.69) + diagnostic tool
===========================================================================

What is in this kit
- payload\            the published ts3native v0.2.12 for the EA app (ts3native-ea169.asi 0.36.247, its
                      ts3native.ini) and ts3fixes.package (v0.2.12).
- payload\diag\       the diagnostic build: the same v0.2.12 code plus read-only measurements (frame timing,
                      PC check, mod timing; asi 0.36.247-elidiag1), its ini, the same ts3fixes.package.
                      See README-diag.txt.
- TS3Kit.bat          the tool: 1 diagnose (read-only report zip on your Desktop), 2 launch the game your usual
                      way and capture, 3 install (asks N = normal or D = diagnostic; backs up first),
                      4 uninstall (puts the backup back), 5 quit.

The installer only accepts the EA app 1.69 exe (Game\Bin\TS3.exe, PE timestamp 6707155C, SizeOfImage 0xEB5000).
It writes Game\Bin\ts3native.asi, Game\Bin\ts3native.ini and Documents\Electronic Arts\The Sims 3\Mods\Packages\
ts3fixes.package (it picks the user folder the game uses, also when Documents is in OneDrive), after a backup.
TS3.exe and Options.ini are never changed. The diagnose option never changes anything; it masks your Windows user
name, computer name and home folder in the zip.

After an install, Game\Bin\ts3native.log starts with "host build: EA app 1.69.47".

Source and issues: https://github.com/rodneypantonial/sims3-modernization
