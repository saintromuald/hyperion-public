// port_stormlib.h -- the MPQ reading and writing over StormLib 9.40 (port_stormlib.cpp), the
// one file of the port that sees StormLib. port_stormfile.cpp reads archives through it, and
// port_canvas.cpp and port_hxm.cpp write maps. The functions are __cdecl; a handle is StormLib's.
#ifndef PORT_STORMLIB_H
#define PORT_STORMLIB_H

#include "../../h/platform.h"  // BOOL, DWORD

// An archive opened as storm.dll reads one, and closed; NULL with the error when it cannot be.
void *__cdecl StormLibOpenArchive(const char *pszPath, DWORD *pdwError);
void __cdecl StormLibCloseArchive(void *hMpq);
// A file of the archive by name, in dwLocale or else neutral; NULL with the error when it is not
// there.
void *__cdecl StormLibOpenFile(void *hMpq, const char *pszName, DWORD dwLocale, DWORD *pdwError);
DWORD __cdecl StormLibFileSize(void *hFile);
// cb bytes from the file's position on, or as many as are left; FALSE with the error when a
// sector cannot be read. Reaching the end is not an error here.
BOOL __cdecl StormLibReadFile(void *hFile, void *pBuffer, DWORD cb, DWORD *pcbRead, DWORD *pdwError);
DWORD __cdecl StormLibFilePosition(void *hFile);
void __cdecl StormLibSeekFile(void *hFile, DWORD dwPosition);
// The file's MPQ flags and where its data starts in the archive's file, for SFileOpenFileEx's
// scope 4.
void __cdecl StormLibFileLayout(void *hMpq, void *hFile, DWORD *pdwFlags, DWORD *pdwOffset);
void __cdecl StormLibCloseFile(void *hFile);

// A new archive at pszPath, in place of any file there, holding one file stored as it is (a map
// Hyperion makes, port_canvas.cpp); and one holding several, imploded as StarEdit stores a map's
// (a Hyperion map compiled for the game, port_hxm.cpp).
BOOL __cdecl StormLibWriteArchive(const char *pszPath, const char *pszName, const void *pvData, DWORD cbData);
BOOL __cdecl StormLibWriteArchiveFiles(const char *pszPath, int nFiles, const char *const *ppszNames,
                                       const void *const *ppvData, const DWORD *pcbData);

#endif
