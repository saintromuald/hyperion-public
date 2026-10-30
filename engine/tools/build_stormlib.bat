@echo off
REM build_stormlib.bat -- builds StormLib's MPQ reading into src117\build\stormlib\stormlib.lib,
REM for the port's SFile functions (src117\port\data\port_stormlib.cpp).
REM
REM The source is StormLib 9.40 (MIT), fetched by tools\fetch_stormlib.py into the
REM gitignored third_party. Every file is compiled with port_stormlib_names.h forced in,
REM which gives StormLib's public names an Sl prefix: they are Storm's names, and the port
REM defines Storm's for the game. StormLib's pklib is left out; its compression calls
REM step 3's pklib objects (pk_implode.obj, pk_explode.obj, pk_crc32.obj), which the
REM engine links and anything else that links this library must link too. All of it is
REM /Gd, the default convention StormLib is written for, and /MT, as the engine is.
REM
REM This is the 32-bit library, which tools\build_porttest.bat's tests link;
REM tools\build_engine_x64.py builds the engine's own. It can be run on its own, from
REM PowerShell or cmd, not Git Bash.
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86 || exit /b 1
)
set "REPO=%~dp0.."
set "SL=%REPO%\third_party\StormLib-9.40\src"
if not exist "%SL%\StormLib.h" (
  echo [build_stormlib] StormLib not found in "%SL%": run python tools\fetch_stormlib.py
  exit /b 1
)
set "OUT=%REPO%\src117\build\stormlib"
set "ARCH=/arch:SSE2 /D_USING_V110_SDK71_"
REM "x64": the x64 build's library, with the x64 compiler
REM the caller has set up (tools\build_engine_x64.py).
if /i "%~1"=="x64" (
  set "OUT=%REPO%\src117\build\x64\stormlib"
  set "ARCH="
)
if not exist "%OUT%" mkdir "%OUT%"
del /q "%OUT%\*.obj" "%OUT%\stormlib.lib" 2>nul

echo [build_stormlib] cl /c StormLib 9.40 (/Gd /MT, names prefixed Sl)
cl /nologo /c /MP /MT /O2 /Oy- /GS- /Gy %ARCH% /fp:precise /Gd /EHsc /W0 ^
   /DWIN32 /DNDEBUG /D_LIB /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE ^
   /D__STORMLIB_SELF__ ^
   /FI"%REPO%\src117\port\data\port_stormlib_names.h" /Fo"%OUT%\\" ^
   "%SL%\FileStream.cpp" "%SL%\LibTomCrypt.c" "%SL%\LibTomMath.c" "%SL%\LibTomMathDesc.c" ^
   "%SL%\SBaseCommon.cpp" "%SL%\SBaseFileTable.cpp" "%SL%\SBaseSubTypes.cpp" "%SL%\SCompression.cpp" ^
   "%SL%\SFileAddFile.cpp" "%SL%\SFileAttributes.cpp" "%SL%\SFileCompactArchive.cpp" ^
   "%SL%\SFileCreateArchive.cpp" "%SL%\SFileExtractFile.cpp" "%SL%\SFileFindFile.cpp" ^
   "%SL%\SFileGetFileInfo.cpp" "%SL%\SFileListFile.cpp" "%SL%\SFileOpenArchive.cpp" ^
   "%SL%\SFileOpenFileEx.cpp" "%SL%\SFilePatchArchives.cpp" "%SL%\SFileReadFile.cpp" ^
   "%SL%\SFileVerify.cpp" "%SL%\SMemUtf8.cpp" "%SL%\adpcm\adpcm.cpp" "%SL%\huffman\huff.cpp" ^
   "%SL%\sparse\sparse.cpp" "%SL%\jenkins\lookup3.c" ^
   "%SL%\bzip2\blocksort.c" "%SL%\bzip2\bzlib.c" "%SL%\bzip2\compress.c" "%SL%\bzip2\crctable.c" ^
   "%SL%\bzip2\decompress.c" "%SL%\bzip2\huffman.c" "%SL%\bzip2\randtable.c" ^
   "%SL%\lzma\C\LzFind.c" "%SL%\lzma\C\LzFindMt.c" "%SL%\lzma\C\LzmaDec.c" "%SL%\lzma\C\LzmaEnc.c" ^
   "%SL%\lzma\C\Threads.c" ^
   "%SL%\zlib\adler32.c" "%SL%\zlib\compress_zlib.c" "%SL%\zlib\crc32.c" "%SL%\zlib\deflate.c" ^
   "%SL%\zlib\inffast.c" "%SL%\zlib\inflate.c" "%SL%\zlib\inftrees.c" "%SL%\zlib\trees.c" ^
   "%SL%\zlib\zutil.c" ^
   || (echo [build_stormlib] compile FAILED & exit /b 1)

lib /nologo /OUT:"%OUT%\stormlib.lib" "%OUT%\*.obj" || (echo [build_stormlib] lib FAILED & exit /b 1)
echo [build_stormlib] OK -^> %OUT%\stormlib.lib
endlocal & exit /b 0
