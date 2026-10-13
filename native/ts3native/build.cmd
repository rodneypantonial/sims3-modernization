@echo off
rem Builds out\ts3native.asi (32-bit DLL, static CRT) and runs the helper self-check.
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /LD /O2 /MT /W4 /WX /Foout\ /Feout\ts3native.asi ts3native.cpp /link /DLL kernel32.lib || exit /b 1
cl /nologo /O2 /MT /W4 /WX /wd4505 /Foout\ /Feout\selftest.exe selftest.cpp kernel32.lib || exit /b 1
cl /nologo /O2 /MT /W4 /WX /wd4505 /Foout\ /Feout\decompress-test.exe decompress-test.cpp kernel32.lib || exit /b 1
out\selftest.exe || exit /b 1
copy /y ts3native.ini out\ts3native.ini >nul
del out\*.obj out\*.exp out\*.lib out\selftest.log 2>nul
echo built %~dp0out\ts3native.asi
