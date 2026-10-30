@echo off
REM ---------------------------------------------------------------------------
REM build_porttest.bat -- build the port's differential tests (32-bit, VS2013 and VS 18)
REM into src117\build\porttest. Each test runs one of Hyperion's bodies against
REM the oracle's own code, read from a 1.17 StarCraft.exe at run time:
REM
REM   src117\build\porttest\veclen_test.exe oracle\StarCraft.exe
REM   src117\build\porttest\float_test.exe out.txt
REM   src117\build\porttest\draw_test.exe oracle\StarCraft.exe
REM   src117\build\porttest\mask_test.exe testrun\storm.dll
REM   src117\build\porttest\pklib_test.exe oracle\StarCraft.exe [calls] [replays or folders]
REM   src117\build\porttest\storm_test.exe testrun\storm.dll [rounds]
REM   src117\build\porttest\smk_test.exe testrun [frames]
REM
REM A test builds the port files it checks without the rest of Hyperion, so what
REM those files call on the side comes from tools\porttest\*_stub.cpp: the GPU scene
REM records nothing (the caller draws), the trainer's snapshots and memory probe are
REM off, no path is a .hxm map, the LAN provider is never there, Smacker's sound is
REM the system's DirectSound, not port_dsound.cpp's on SDL audio,
REM settings.json's strings are quoted as port_json.cpp quotes them, no StarCraft: Remastered
REM install is there, and the Storm files' few SDL calls are the C runtime's
REM (tools\porttest\sdl_stub\SDL3\SDL.h). draw_test's
REM also stand in for port_view.cpp's camera, console, presenter, clock, trace,
REM Replay TV and cutscenes, at the game's own view; the test never calls them.
REM draw_test is built with Visual Studio 18 (x86): VS2013 cannot compile
REM src117\h\playerlimits.h (alignas), and the test calls the oracle only by address.
REM The others are built with VS2013.
REM
REM Usage (from PowerShell):  cmd /c "tools\build_porttest.bat"
REM ---------------------------------------------------------------------------
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat"
if not exist "%VCVARS%" ( echo [build_porttest] VS2013 vcvarsall not found & exit /b 1 )
set "VCVARS18=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS18%" ( echo [build_porttest] VS 18 vcvarsall not found & exit /b 1 )

set "REPO=%~dp0.."
set "OUT=%REPO%\src117\build\porttest"
if not exist "%OUT%" mkdir "%OUT%"
REM draw_test runs the oracle's code at its own addresses, so it is laid out like
REM Hyperion.exe: linked at 0x700000, then moved down by hyperion_pe.py, which
REM leaves a blank section over 0x401000..0x700000 (docs\hyperion-exe.md).
REM OC_FIXED_DATA: port_draw.cpp and port_view.cpp reach 1.17's data at its own
REM addresses here, where the test maps the original (src117\h\oc_data.h); the
REM globals they name as externs are bound there by abs_globals.py --fixed.
REM It is built first, in an environment of its own, before VS2013's is set up.
setlocal
call "%VCVARS18%" x86 >nul || exit /b 1
pushd "%OUT%"
echo [build_porttest] cl draw_test.cpp port_draw.cpp port_view.cpp (VS 18)
if not exist "%OUT%\draw" mkdir "%OUT%\draw"
cl /nologo /c /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /DOC_FIXED_DATA /I"%REPO%\src117\h" ^
   "%REPO%\src117\port\render\port_draw.cpp" "%REPO%\src117\port\render\port_view.cpp" /Fo"%OUT%\draw\\" ^
   || (popd & echo build FAILED & exit /b 1)
del /q "%OUT%\draw\abs_globals.obj" 2>nul
python "%REPO%\tools\wave117\abs_globals.py" --fixed --objdir "%OUT%\draw" --bind-only port_draw.obj,port_view.obj ^
   --out "%OUT%\draw\abs_globals.obj" || (popd & echo abs_globals FAILED & exit /b 1)
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /DOC_FIXED_DATA /I"%REPO%\src117\h" ^
   "%REPO%\tools\porttest\draw_test.cpp" "%OUT%\draw\port_draw.obj" "%OUT%\draw\port_view.obj" "%OUT%\draw\abs_globals.obj" ^
   "%REPO%\src117\port\platform\port_settings.cpp" "%REPO%\src117\port\platform\port_json.cpp" "%REPO%\tools\porttest\settings_stub.cpp" ^
   "%REPO%\tools\porttest\scene_stub.cpp" "%REPO%\tools\porttest\camera_stub.cpp" "%REPO%\tools\porttest\console_stub.cpp" ^
   "%REPO%\tools\porttest\present_stub.cpp" "%REPO%\tools\porttest\gpu_stub.cpp" "%REPO%\tools\porttest\clock_stub.cpp" ^
   "%REPO%\tools\porttest\trace_stub.cpp" "%REPO%\tools\porttest\tv_stub.cpp" "%REPO%\tools\porttest\cutscene_stub.cpp" ^
   /Fe"%OUT%\draw_test_linked.exe" /link /MACHINE:X86 /BASE:0x700000 /FIXED /DYNAMICBASE:NO /SAFESEH:NO advapi32.lib user32.lib ^
   || (popd & echo build FAILED & exit /b 1)
python "%REPO%\tools\hyperion_pe.py" "%OUT%\draw_test_linked.exe" "%OUT%\draw_test.exe" || (popd & echo layout FAILED & exit /b 1)
popd
endlocal

call "%VCVARS%" x86 || exit /b 1
pushd "%OUT%"
echo [build_porttest] cl veclen_test.cpp port_veclen.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   "%REPO%\tools\porttest\veclen_test.cpp" "%REPO%\src117\port\gameplay\port_veclen.cpp" ^
   /Fe"%OUT%\veclen_test.exe" /link /MACHINE:X86 advapi32.lib || (popd & echo build FAILED & exit /b 1)
REM float_test's flags are the engine's for floating point (/arch:SSE2 /fp:precise).
echo [build_porttest] cl float_test.cpp
cl /nologo /EHsc /MT /O2 /W3 /arch:SSE2 /fp:precise /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   "%REPO%\tools\porttest\float_test.cpp" /Fe"%OUT%\float_test.exe" /link /MACHINE:X86 || (popd & echo build FAILED & exit /b 1)
REM mask_test runs Storm's STrans, SBltROP3 and SRgn functions from the install's storm.dll.
echo [build_porttest] cl mask_test.cpp port_mask.cpp port_storm.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   "%REPO%\tools\porttest\mask_test.cpp" "%REPO%\src117\port\render\port_mask.cpp" "%REPO%\src117\port\render\port_storm.cpp" ^
   "%REPO%\tools\porttest\scene_stub.cpp" ^
   /Fe"%OUT%\mask_test.exe" /link /MACHINE:X86 || (popd & echo build FAILED & exit /b 1)
REM pklib_test runs the original's PKWARE library at its own addresses, laid out
REM like draw_test, against StormLib's pklib as the payloads build it.
set "PKLIB=%REPO%\third_party\StormLib-9.40\src\pklib"
if not exist "%PKLIB%\pklib.h" ( popd & echo StormLib not found: run python tools\fetch_stormlib.py & exit /b 1 )
echo [build_porttest] cl pklib_test.cpp port_pklib.cpp, pklib
for %%P in (implode explode crc32) do (
  cl /nologo /c /MT /O2 /W3 /DWIN32 /DNDEBUG /Dimplode=PkImplode /Dexplode=PkExplode /Dcrc32_pklib=PkCrc32 ^
     /Fo"%OUT%\pk_%%P.obj" "%PKLIB%\%%P.c" || (popd & echo build FAILED & exit /b 1)
)
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /I"%REPO%\src117\h" /I"%PKLIB%" ^
   "%REPO%\tools\porttest\pklib_test.cpp" "%REPO%\src117\port\data\port_pklib.cpp" "%OUT%\pk_implode.obj" "%OUT%\pk_explode.obj" "%OUT%\pk_crc32.obj" ^
   /Fe"%OUT%\pklib_test_linked.exe" /link /MACHINE:X86 /BASE:0x700000 /FIXED /DYNAMICBASE:NO /SAFESEH:NO advapi32.lib ^
   || (popd & echo build FAILED & exit /b 1)
python "%REPO%\tools\hyperion_pe.py" "%OUT%\pklib_test_linked.exe" "%OUT%\pklib_test.exe" || (popd & echo layout FAILED & exit /b 1)
REM storm_test runs Storm's memory, string, registry, image, file and video functions from the
REM install's storm.dll. Ours read the MPQs through StormLib (build_stormlib.bat) and
REM the pklib objects above.
call "%REPO%\tools\build_stormlib.bat" || (popd & echo build FAILED & exit /b 1)
echo [build_porttest] cl storm_test.cpp port_stormbase.cpp port_stormbmp.cpp port_stormfile.cpp port_stormlib.cpp port_stormvid.cpp port_smacker.cpp port_storm.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /I"%REPO%\tools\porttest\sdl_stub" "%REPO%\tools\porttest\remastered_stub.cpp" "%REPO%\tools\porttest\storm_side_stub.cpp" ^
   "%REPO%\tools\porttest\storm_test.cpp" "%REPO%\src117\port\data\port_stormbase.cpp" "%REPO%\src117\port\platform\port_settings.cpp" "%REPO%\tools\porttest\settings_stub.cpp" "%REPO%\src117\port\data\port_stormbmp.cpp" ^
   "%REPO%\src117\port\data\port_stormfile.cpp" "%REPO%\src117\port\data\port_stormlib.cpp" ^
   "%REPO%\src117\port\data\port_stormvid.cpp" "%REPO%\src117\port\data\port_smacker.cpp" "%REPO%\src117\port\render\port_storm.cpp" ^
   "%REPO%\tools\porttest\snap_stub.cpp" "%REPO%\tools\porttest\snapprobe_stub.cpp" "%REPO%\tools\porttest\json_stub.cpp" ^
   "%REPO%\tools\porttest\hxm_stub.cpp" "%REPO%\tools\porttest\dsound_stub.cpp" "%REPO%\tools\porttest\scene_stub.cpp" ^
   "%REPO%\src117\build\stormlib\stormlib.lib" "%OUT%\pk_implode.obj" "%OUT%\pk_explode.obj" "%OUT%\pk_crc32.obj" ^
   /Fe"%OUT%\storm_test.exe" /link /MACHINE:X86 user32.lib advapi32.lib winmm.lib dsound.lib || (popd & echo build FAILED & exit /b 1)
REM smk_test plays every video the game plays in the install's smackw32.dll and in
REM port_smacker.cpp, the port's own Smacker player, and compares them.
echo [build_porttest] cl smk_test.cpp port_smacker.cpp port_stormbase.cpp port_stormfile.cpp port_stormlib.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /I"%REPO%\tools\porttest\sdl_stub" "%REPO%\tools\porttest\remastered_stub.cpp" "%REPO%\tools\porttest\storm_side_stub.cpp" ^
   "%REPO%\tools\porttest\smk_test.cpp" "%REPO%\src117\port\data\port_smacker.cpp" "%REPO%\src117\port\data\port_stormbase.cpp" "%REPO%\src117\port\platform\port_settings.cpp" "%REPO%\tools\porttest\settings_stub.cpp" ^
   "%REPO%\src117\port\data\port_stormfile.cpp" "%REPO%\src117\port\data\port_stormlib.cpp" ^
   "%REPO%\tools\porttest\snap_stub.cpp" "%REPO%\tools\porttest\snapprobe_stub.cpp" "%REPO%\tools\porttest\json_stub.cpp" ^
   "%REPO%\tools\porttest\hxm_stub.cpp" "%REPO%\tools\porttest\dsound_stub.cpp" ^
   "%REPO%\src117\build\stormlib\stormlib.lib" "%OUT%\pk_implode.obj" "%OUT%\pk_explode.obj" "%OUT%\pk_crc32.obj" ^
   /Fe"%OUT%\smk_test.exe" /link /MACHINE:X86 user32.lib advapi32.lib winmm.lib dsound.lib || (popd & echo build FAILED & exit /b 1)
REM net_test runs the install's storm.dll, with the loopback provider standard.snp
REM serves, and port_stormnet.cpp, the port's own SNet functions, and compares them.
echo [build_porttest] cl net_test.cpp port_stormnet.cpp port_stormbase.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /I"%REPO%\src117\h" ^
   "%REPO%\tools\porttest\net_test.cpp" "%REPO%\src117\port\lan\port_stormnet.cpp" "%REPO%\src117\port\data\port_stormbase.cpp" "%REPO%\src117\port\platform\port_settings.cpp" "%REPO%\tools\porttest\settings_stub.cpp" ^
   "%REPO%\tools\porttest\snap_stub.cpp" "%REPO%\tools\porttest\snapprobe_stub.cpp" "%REPO%\tools\porttest\json_stub.cpp" ^
   "%REPO%\tools\porttest\lan_stub.cpp" "%REPO%\tools\porttest\netcmds_stub.cpp" ^
   /Fe"%OUT%\net_test.exe" /link /MACHINE:X86 user32.lib advapi32.lib || (popd & echo build FAILED & exit /b 1)
REM big_test runs the install's storm.dll and port_stormbig.cpp, the port's own big
REM numbers, on the same numbers and on the signatures of the maps it is given.
echo [build_porttest] cl big_test.cpp port_stormbig.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   "%REPO%\tools\porttest\big_test.cpp" "%REPO%\src117\port\data\port_stormbig.cpp" ^
   /Fe"%OUT%\big_test.exe" /link /MACHINE:X86 || (popd & echo build FAILED & exit /b 1)
REM err_test runs the install's storm.dll and port_stormerr.cpp, the port's own last
REM error and error texts, on the same codes.
echo [build_porttest] cl err_test.cpp port_stormerr.cpp
cl /nologo /EHsc /MT /O2 /W3 /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   "%REPO%\tools\porttest\err_test.cpp" "%REPO%\src117\port\data\port_stormerr.cpp" ^
   /Fe"%OUT%\err_test.exe" /link /MACHINE:X86 || (popd & echo build FAILED & exit /b 1)
popd
echo [build_porttest] OK -^> src117\build\porttest\veclen_test.exe, float_test.exe, draw_test.exe, mask_test.exe, pklib_test.exe, storm_test.exe, smk_test.exe, net_test.exe, big_test.exe, err_test.exe
endlocal & exit /b 0
