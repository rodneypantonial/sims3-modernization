@echo off
rem Builds out\worldimage.exe (32-bit, static CRT), the M3.1 world image capture helper (docs/123 §8), and runs its
rem fake-process selftest (two small test processes captured into out\selftest; exit code = failed checks).
rem ts3native [WorldImage] and monohost imagecap start it; real images go to <repo>\game-copy\worldimage (git-ignored).
setlocal
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio with the C++ tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /O2 /MT /W4 /WX /Foout\ /Feout\worldimage.exe worldimage.cpp kernel32.lib || exit /b 1
del out\*.obj 2>nul
echo built out\worldimage.exe
out\worldimage.exe selftest "%~dp0out\selftest" || exit /b 1
