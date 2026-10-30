// port_stormlib.cpp -- the MPQ reading the port's SFile functions use, over
// StormLib 9.40 (File). port_stormfile.cpp defines
// Storm's names for the game and calls these; this is the one file of the port
// that sees StormLib, whose public names are Storm's with an Sl prefix
// (port_stormlib_names.h; tools/build_stormlib.bat builds the library).
//
// An archive is opened the way storm.dll reads one: as MPQ version 1 whatever
// its header says, read only, with no list file or attributes, the header
// searched for at 0x200 steps. A file's name is looked up as Storm looks it
// up, the given locale first and then a neutral entry, which StormLib does the
// same way.
#include "port_stormlib_names.h"
#include <windows.h>
#define __STORMLIB_NO_STATIC_LINK__
// Off Windows StormPort.h declares Windows' types itself; its LPOVERLAPPED is void *.
#if !defined(_WIN32)
#define LPOVERLAPPED SlLPOVERLAPPED
#endif
#include "../../../third_party/StormLib-9.40/src/StormLib.h"
#include "../platform/port_posix.h"  // PortNativePath
#include "port_stormlib.h"
#if !defined(_WIN32)
#undef LPOVERLAPPED
#endif

// The functions are __cdecl, and this file is compiled /Gd with StormLib, apart
// from the port's /Gr group.
//
// StormLib keeps its locale in a global; the port's SFile functions hold their
// lock around every call here, so setting it before a lookup is enough.
//
// StormLib sets Win32's last error as it goes, where storm.dll leaves it alone
// but for its own failures. So each function here leaves it as it found it and
// hands StormLib's error back itself.
struct StormLibKeepError {
  DWORD dwError;
  StormLibKeepError() : dwError(GetLastError()) {}
  ~StormLibKeepError() { SetLastError(dwError); }
};

void *__cdecl StormLibOpenArchive(const char *pszPath, DWORD *pdwError)
{
  StormLibKeepError keep;
  HANDLE hMpq = NULL;
  const DWORD dwFlags =
      MPQ_OPEN_READ_ONLY | MPQ_OPEN_FORCE_MPQ_V1 | MPQ_OPEN_NO_LISTFILE | MPQ_OPEN_NO_ATTRIBUTES;
  if (!SFileOpenArchive(PortNativePath(pszPath), 0, dwFlags, &hMpq)) {
    *pdwError = GetLastError();
    return NULL;
  }
  return hMpq;
}

void __cdecl StormLibCloseArchive(void *hMpq)
{
  StormLibKeepError keep;
  SFileCloseArchive(hMpq);
}

// A file of the archive by name, in dwLocale or else neutral; NULL with the
// error when it is not there.
void *__cdecl StormLibOpenFile(void *hMpq, const char *pszName, DWORD dwLocale, DWORD *pdwError)
{
  StormLibKeepError keep;
  HANDLE hFile = NULL;
  SFileSetLocale(dwLocale);
  if (!SFileOpenFileEx(hMpq, pszName, SFILE_OPEN_FROM_MPQ, &hFile)) {
    *pdwError = GetLastError();
    return NULL;
  }
  return hFile;
}

DWORD __cdecl StormLibFileSize(void *hFile)
{
  StormLibKeepError keep;
  return SFileGetFileSize(hFile, NULL);
}

// cb bytes from the file's position on, or as many as are left; FALSE with the
// error when a sector cannot be read. Reaching the end is not an error here.
BOOL __cdecl StormLibReadFile(void *hFile, void *pBuffer, DWORD cb, DWORD *pcbRead, DWORD *pdwError)
{
  StormLibKeepError keep;
  DWORD cbRead = 0;
  if (!SFileReadFile(hFile, pBuffer, cb, &cbRead, NULL)) {
    const DWORD dwError = GetLastError();
    if (dwError != ERROR_HANDLE_EOF) {
      *pcbRead = cbRead;
      *pdwError = dwError;
      return FALSE;
    }
  }
  *pcbRead = cbRead;
  return TRUE;
}

DWORD __cdecl StormLibFilePosition(void *hFile)
{
  StormLibKeepError keep;
  return SFileSetFilePointer(hFile, 0, NULL, FILE_CURRENT);
}

void __cdecl StormLibSeekFile(void *hFile, DWORD dwPosition)
{
  StormLibKeepError keep;
  SFileSetFilePointer(hFile, static_cast<LONG>(dwPosition), NULL, FILE_BEGIN);
}

// The file's MPQ flags and where its data starts in the archive's file, for
// SFileOpenFileEx's scope 4: StormLib counts from the archive's header, which a
// CD's archive has some way into its file.
void __cdecl StormLibFileLayout(void *hMpq, void *hFile, DWORD *pdwFlags, DWORD *pdwOffset)
{
  StormLibKeepError keep;
  ULONGLONG qwOffset = 0, qwHeader = 0;
  *pdwFlags = 0;
  SFileGetFileInfo(hFile, SFileInfoFlags, pdwFlags, sizeof(DWORD), NULL);
  SFileGetFileInfo(hFile, SFileInfoByteOffset, &qwOffset, sizeof(qwOffset), NULL);
  SFileGetFileInfo(hMpq, SFileMpqHeaderOffset, &qwHeader, sizeof(qwHeader), NULL);
  *pdwOffset = static_cast<DWORD>(qwHeader + qwOffset);
}

void __cdecl StormLibCloseFile(void *hFile)
{
  StormLibKeepError keep;
  SFileCloseFile(hFile);
}

// A new archive at pszPath, in place of any file there, holding one file, pszName, stored
// as it is: a map Hyperion makes (port_canvas.cpp). Storm, StormLib and the editors all
// read a version 1 archive with no compression.
BOOL __cdecl StormLibWriteArchive(const char *pszPath, const char *pszName, const void *pvData,
                                  DWORD cbData)
{
  StormLibKeepError keep;
  HANDLE hMpq = NULL;
  HANDLE hFile = NULL;
  DeleteFileA(pszPath);
  if (!SFileCreateArchive(PortNativePath(pszPath), MPQ_CREATE_ARCHIVE_V1 | MPQ_CREATE_LISTFILE, 16, &hMpq))
    return FALSE;
  bool bOk = SFileCreateFile(hMpq, pszName, 0, cbData, 0, MPQ_FILE_REPLACEEXISTING, &hFile) &&
             SFileWriteFile(hFile, pvData, cbData, 0);
  if (hFile != NULL)
    bOk = SFileFinishFile(hFile) && bOk;
  bOk = SFileCloseArchive(hMpq) && bOk;
  if (!bOk)
    DeleteFileA(pszPath);
  return bOk ? TRUE : FALSE;
}

// The same with several files, imploded as StarEdit stores a map's: a Hyperion map
// compiled for the game (port_hxm.cpp), its scenario.chk first and its sounds after.
BOOL __cdecl StormLibWriteArchiveFiles(const char *pszPath, int nFiles, const char *const *ppszNames,
                                       const void *const *ppvData, const DWORD *pcbData)
{
  StormLibKeepError keep;
  HANDLE hMpq = NULL;
  DeleteFileA(pszPath);
  if (!SFileCreateArchive(PortNativePath(pszPath), MPQ_CREATE_ARCHIVE_V1 | MPQ_CREATE_LISTFILE,
                          static_cast<DWORD>(nFiles * 2 + 16), &hMpq))
    return FALSE;
  bool bOk = true;
  for (int i = 0; i < nFiles && bOk; ++i) {
    HANDLE hFile = NULL;
    bOk = SFileCreateFile(hMpq, ppszNames[i], 0, pcbData[i], 0,
                          MPQ_FILE_IMPLODE | MPQ_FILE_REPLACEEXISTING, &hFile) &&
          (pcbData[i] == 0 || SFileWriteFile(hFile, ppvData[i], pcbData[i], MPQ_COMPRESSION_PKWARE));
    if (hFile != NULL)
      bOk = SFileFinishFile(hFile) && bOk;
  }
  bOk = SFileCloseArchive(hMpq) && bOk;
  if (!bOk)
    DeleteFileA(pszPath);
  return bOk ? TRUE : FALSE;
}
