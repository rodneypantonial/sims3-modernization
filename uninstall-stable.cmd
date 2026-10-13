@echo off
rem Restores every file install-stable.cmd (install-fixes.ts) replaced, from %LOCALAPPDATA%\ts3-fixes.
setlocal
cd /d "%~dp0"
tasklist /FI "IMAGENAME eq TS3W.exe" | find /I "TS3W.exe" >nul && (echo Close The Sims 3 first. & exit /b 1)
node tools\setup\install-fixes.ts uninstall && echo Done: previous files restored.
