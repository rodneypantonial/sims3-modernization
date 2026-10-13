@echo off
rem Stable play build: community fixes + Performance Patch + ts3fixes + ts3native (GC heap, faster water routing).
rem Every file it overwrites is backed up first (%LOCALAPPDATA%\ts3-fixes); uninstall-stable.cmd restores them.
rem Close The Sims 3 before running. Needs Node.js 22.18+ (and Visual Studio C++ tools to build ts3native).
setlocal
cd /d "%~dp0"
tasklist /FI "IMAGENAME eq TS3W.exe" | find /I "TS3W.exe" >nul && (echo Close The Sims 3 first. & exit /b 1)
node tools\setup\install-fixes.ts install || (echo Install failed: nothing after the failing step was changed. & exit /b 1)
rem the in-game test agent is for the test harness only: not part of the play build
set "PKG=%USERPROFILE%\Documents\Electronic Arts\The Sims 3\Mods\Packages\ts3agent.package"
if exist "%PKG%" del "%PKG%"
node tools\setup\install-fixes.ts status
echo.
echo Done. Start The Sims 3 from Steam as usual. To undo: uninstall-stable.cmd
