@echo off
REM ---------------------------------------------------------------------------
REM build_porttest_x64.bat -- build the differential tests that have an x64 arm
REM with VS 2026 into src117\build\x64\porttest.
REM Their results must be the x86 build's (build_porttest.bat):
REM
REM   src117\build\x64\porttest\veclen_test.exe -   (the digests veclen_test prints)
REM   src117\build\x64\porttest\float_test.exe out.txt
REM   src117\build\x64\porttest\pos_test.exe      (none of the types' results differ from 1.17's)
REM
REM Usage (from PowerShell):  & .\tools\build_porttest_x64.bat
REM ---------------------------------------------------------------------------
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" ( echo [build_porttest_x64] VS 2026 vcvarsall not found & exit /b 1 )
call "%VCVARS%" amd64 >nul || exit /b 1

set "REPO=%~dp0.."
set "OUT=%REPO%\src117\build\x64\porttest"
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"
echo [build_porttest_x64] cl veclen_test.cpp port_veclen.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   "%REPO%\tools\porttest\veclen_test.cpp" "%REPO%\src117\port\gameplay\port_veclen.cpp" ^
   /Fe"%OUT%\veclen_test.exe" /link /MACHINE:X64 advapi32.lib || (popd & echo build FAILED & exit /b 1)
REM float_test's flags are the x64 engine's for floating point (/fp:precise).
echo [build_porttest_x64] cl float_test.cpp
cl /nologo /EHsc /MT /O2 /W3 /fp:precise /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   "%REPO%\tools\porttest\float_test.cpp" /Fe"%OUT%\float_test.exe" /link /MACHINE:X64 || (popd & echo build FAILED & exit /b 1)
REM savedump splits a save into its chunks for comparing saves.
set "PKLIB=%REPO%\third_party\StormLib-9.40\src\pklib"
echo [build_porttest_x64] cl savedump.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /I"%PKLIB%" ^
   "%REPO%\tools\porttest\savedump.cpp" "%PKLIB%\explode.c" "%PKLIB%\crc32.c" ^
   /Fe"%OUT%\savedump.exe" /link /MACHINE:X64 || (popd & echo build FAILED & exit /b 1)
REM pos_test holds the position types (src117\h\pos.h) to 1.17's 16-bit fields.
echo [build_porttest_x64] cl pos_test.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /I"%REPO%\src117\h" ^
   "%REPO%\tools\porttest\pos_test.cpp" /Fe"%OUT%\pos_test.exe" /link /MACHINE:X64 || (popd & echo build FAILED & exit /b 1)
popd
echo [build_porttest_x64] OK -^> src117\build\x64\porttest\veclen_test.exe, float_test.exe, savedump.exe, pos_test.exe
endlocal & exit /b 0
