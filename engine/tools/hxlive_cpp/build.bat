@echo off
REM ---------------------------------------------------------------------------
REM build.bat: example.exe from example.cpp and hxlive.h, with MSVC x64.
REM
REM Usage (from PowerShell):  & .\build.bat
REM        (from cmd):        build.bat
REM
REM It looks for vcvarsall.bat where Hyperion's own build does (tools\build_engine_x64.py),
REM and failing that asks vswhere, so any edition of Visual Studio 2019 or newer will do.
REM Nothing but Windows' winhttp.lib is linked; the header does that itself with a pragma.
REM ---------------------------------------------------------------------------
setlocal
set "HERE=%~dp0"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%VCVARS%" goto :found

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :missing
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%VCVARS%" goto :found

:missing
echo [hxlive_cpp] no vcvarsall.bat found; install the C++ tools of Visual Studio
exit /b 1

:found
call "%VCVARS%" amd64 >nul || exit /b 1
pushd "%HERE%"
echo [hxlive_cpp] cl example.cpp
cl /nologo /std:c++17 /EHsc /W4 example.cpp /Fe:example.exe
if errorlevel 1 (popd & echo [hxlive_cpp] build FAILED & exit /b 1)
popd
echo [hxlive_cpp] OK -^> %HERE%example.exe
endlocal & exit /b 0
