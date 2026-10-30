// port_stormapi.h -- Storm's functions as the port defines them (port_stormerr.cpp,
// port_stormbase.cpp, port_stormfile.cpp), for the port's own files. The game's files declare
// Storm themselves, as 1.17's did, and h/Storm.h holds the ones they share; the names both
// declare are declared alike here. HANDLE is void *, as Windows has it.
#ifndef PORT_STORMAPI_H
#define PORT_STORMAPI_H

#include "../../h/platform.h"  // BOOL, DWORD, size_t

extern "C" {

// Errors (port_stormerr.cpp): ordinal 465.
void __stdcall SErrSetLastError(DWORD dwErrCode);

// Memory, strings and the registry (port_stormbase.cpp): ordinals 401, 403, 501, 503, 578, 423.
void *__stdcall SMemAlloc(DWORD size, const char *ownerTag, int ownerLine, DWORD flags);
BOOL __stdcall SMemFree(const void *ptr, const char *ownerTag, int ownerLine, DWORD flags);
void __stdcall SMemZero(void *pvBuffer, DWORD cbBuffer);
char *__stdcall SStrCopy(char *pszDest, const char *pszSrc, size_t cchDest);
char *__stdcall SStrPack(char *pszDest, const char *pszSrc, size_t cchDest);
int __cdecl SStrPrintf(char *pszDest, size_t cchDest, const char *pszFormat, ...);
BOOL __stdcall SRegLoadValue(const char *pszKey, const char *pszValue, DWORD dwFlags, DWORD *pdwValue);

// Files, in archives or on disk (port_stormfile.cpp): ordinals 267, 268, 269, 265, 253, 280,
// 295 and 296. SFileGetInstallPath answers the length written, which h/Storm.h calls a BOOL.
BOOL __stdcall SFileOpenFile(const char *pszName, void **phFile);
BOOL __stdcall SFileOpenFileEx(void *hMpq, const char *pszName, DWORD dwScope, void **phFile);
BOOL __stdcall SFileReadFile(void *hFile, void *pBuffer, DWORD cb, DWORD *pdwRead, void *pOverlapped);
DWORD __stdcall SFileGetFileSize(void *hFile, DWORD *pdwHigh);
BOOL __stdcall SFileCloseFile(void *hFile);
BOOL __stdcall SFileUnloadFile(void *pData);
BOOL __stdcall SFileGetInstallPath(char *pszBuffer, DWORD cchBuffer, DWORD bSlash);
DWORD __stdcall SFileGetUserDataPath(char *pszBuffer, DWORD cchBuffer, DWORD bSlash);

}  // extern "C"

// A PCX of 8 bits a pixel in memory, as SBmpDecodeImage reads one, into a block from SMemAlloc
// (port_stormbmp.cpp): for images a mod gives, which are not in an archive. False when it is
// not one; the pixels are the game's palette's numbers.
bool PortBmpDecodePcx(const void *pData, size_t cb, unsigned char **ppPixels, int *pnWidth, int *pnHeight);

#endif
