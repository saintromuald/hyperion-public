@echo off
REM ---------------------------------------------------------------------------
REM build_replaytest.bat -- build the oracle's replay-test observer DLL and its
REM launcher (32-bit, VS2013) into src117\build. See docs\replay-test.md. The x64
REM observer the engine loads is built by tools\build_engine_x64.py.
REM
REM Usage (from PowerShell):  cmd /c "tools\build_replaytest.bat"
REM ---------------------------------------------------------------------------
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat"
if not exist "%VCVARS%" ( echo [build_replaytest] VS2013 vcvarsall not found & exit /b 1 )
call "%VCVARS%" x86 || exit /b 1

set "SRC=%~dp0replaytest"
set "OUT=%~dp0..\src117\build"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\rt" mkdir "%OUT%\rt"
pushd "%OUT%\rt"
echo [build_replaytest] cl rt_observer.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /LD ^
   "%SRC%\rt_observer.cpp" /Fe"%OUT%\rt_observer.dll" ^
   /link /MACHINE:X86 /DEF:"%SRC%\rt_observer.def" user32.lib || (popd & echo build FAILED & exit /b 1)
echo [build_replaytest] cl rt_launcher.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   "%SRC%\rt_launcher.cpp" /Fe"%OUT%\rt_launcher.exe" ^
   /link /MACHINE:X86 || (popd & echo build FAILED & exit /b 1)
popd
echo [build_replaytest] OK -^> src117\build\rt_observer.dll, rt_launcher.exe
endlocal & exit /b 0
