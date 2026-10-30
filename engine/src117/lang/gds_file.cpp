// gds_file.cpp -- the stdio wrappers the character, save, replay and log paths
// share: thin forwards to the CRT with the arguments in fastcall registers.
// Bodies are in oracle VA order.

#include <windows.h>

#include "../h/gds_appcrit.h"
#include "../h/gds_file.h"
#include "../gen/globals117.h"

// The CRT entry points, declared here rather than through <stdio.h>, as init.cpp
// and creep.cpp do. FILE is spelled `void *`, as in gds_file.h: the wrappers
// forward the handle and never look inside it.
// Hyperion: with MSVC only; elsewhere the C library's, which compat/windows.h
// gives forms taking void *.
#if defined(_MSC_VER)
extern "C" {
int          __cdecl fclose(void *pFile);
int          __cdecl fgetc(void *pFile);
void *       __cdecl fopen(const char *pszName, const char *pszMode);
// Hyperion: the sizes are size_t, as the C runtime's, which the x64 build
// needs.
size_t       __cdecl fread(void *pBuf, size_t nSize, size_t nCount, void *pFile);
int          __cdecl fseek(void *pFile, long nOffset, int nOrigin);
size_t       __cdecl fwrite(const void *pBuf, size_t nSize, size_t nCount, void *pFile);
int          __cdecl setvbuf(void *pFile, char *pBuf, int nMode, size_t nSize);
}
#endif

// 0x4CC370 (10 B).
int __fastcall pfclose(void *pFile)
{
  return fclose(pFile);
}

// 0x4CC380 (43 B). The attribute read is skipped when the caller does not ask
// for it, and DeleteFileA runs either way.
BOOL __fastcall pfdelete(char *pszPath, BOOL bAlsoReadOnly)
{
  DWORD dwAttrs;

  if (bAlsoReadOnly) {
    dwAttrs = GetFileAttributesA(pszPath);
    if (dwAttrs != (DWORD)-1 && (dwAttrs & FILE_ATTRIBUTE_READONLY) != 0)
      SetFileAttributesA(pszPath, dwAttrs & ~FILE_ATTRIBUTE_READONLY);
  }
  return DeleteFileA(pszPath);
}

// 0x4CC3B0 (10 B).
int __fastcall pfgetc(void *pFile)
{
  return fgetc(pFile);
}

// 0x4CC3C0 (108 B). The default arm's fopen is never reached: appcritf does not
// return.
void *__fastcall pfopen(const char *pszName, short sMode)
{
  const char *pszMode;
  void *pFile;

  switch (sMode) {
  case 1:
    pszMode = "rb";
    break;
  case 14:
  case 22:
    pszMode = "wb";
    break;
  case 23:
    pszMode = "wb+";
    break;
  default:
    pszMode = 0;
    appcritf("bad pfopen mode (is %d)", sMode);
    break;
  }

  pFile = fopen(pszName, pszMode);
  if (pFile == 0)
    return pFile;
  setvbuf(pFile, 0, 0 /*_IOFBF*/, 0x8000);
  return pFile;
}

// 0x4CC460 (28 B).
BOOL __fastcall pfread(void *pBuf, int nSize, void *pFile)
{
  return fread(pBuf, nSize, 1, pFile) == 1;
}

// 0x4CC480 (20 B).
int __fastcall pfseek(void *pFile, int nOffset, int nOrigin)
{
  return fseek(pFile, nOffset, nOrigin);
}

// 0x4CC4A0 (28 B).
BOOL __fastcall pfwrite(const void *pBuf, int nSize, void *pFile)
{
  return fwrite(pBuf, nSize, 1, pFile) == 1;
}
