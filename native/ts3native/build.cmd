@echo off
rem Builds out\ts3native.asi (32-bit DLL, static CRT) and runs the helper self-check.
setlocal
rem any Visual Studio 2019+ edition with the C++ x86/x64 tools (vswhere ships with the VS installer)
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio with the C++ tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /LD /O2 /MT /W4 /WX /Foout\ /Feout\ts3native.asi ts3native.cpp /link /DLL kernel32.lib || exit /b 1
cl /nologo /O2 /MT /W4 /WX /wd4505 /Foout\ /Feout\selftest.exe selftest.cpp kernel32.lib || exit /b 1
cl /nologo /O2 /MT /W4 /WX /wd4505 /Foout\ /Feout\decompress-test.exe decompress-test.cpp kernel32.lib || exit /b 1
rem waterjps-test maps game code at 0x61F000 and 0x66D000, so its own image sits elsewhere
cl /nologo /O2 /MT /W4 /WX /wd4505 /Foout\ /Feout\waterjps-test.exe waterjps-test.cpp kernel32.lib /link /BASE:0x20000000 || exit /b 1
rem savecompress-test maps game code at 0x4EB000
cl /nologo /O2 /MT /W4 /WX /wd4505 /Foout\ /Feout\savecompress-test.exe savecompress-test.cpp kernel32.lib /link /BASE:0x20000000 || exit /b 1
out\selftest.exe || exit /b 1
copy /y ts3native.ini out\ts3native.ini >nul
del out\*.obj out\*.exp out\*.lib out\selftest.log 2>nul
echo built %~dp0out\ts3native.asi
