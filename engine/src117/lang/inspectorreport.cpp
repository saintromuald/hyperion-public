// Hyperion: not built. The game's crash reporter is gone from the port and the
// port's own (port_crash.cpp) stays; the file is kept so that later changes to
// the reconstruction still apply. An address of its functions in 1.17's data
// goes to PortOriginalCodeReached.
//
// inspectorreport.cpp -- the crash report file the Blizzard error inspector reads,
// and the file helpers around it. Bodies are in oracle VA order.

#include "../h/Storm.h"              // the one <windows.h>: MultiByteToWideChar
#include "../h/crashy.h"
#include "../h/inspectorreport.h"
#include "../gen/globals117.h"

namespace crashy {
namespace internal {

// The copy FileBasename inlines: BSD strlcpy, returning the length it wanted.
static size_t strlcpy(char *pszDst, const char *pszSrc, size_t cchDst)
{
  char *pszOut = pszDst;
  const char *pszIn = pszSrc;
  size_t nLeft = cchDst;

  if (nLeft != 0) {
    while (--nLeft != 0) {
      if ((*pszOut++ = *pszIn++) == '\0')
        break;
    }
  }

  if (nLeft == 0) {
    if (cchDst != 0)
      *pszOut = '\0';

    while (*pszIn++ != '\0')
      ;
  }

  return static_cast<size_t>(pszIn - pszSrc - 1);
}

}  // namespace internal
}  // namespace crashy

namespace crashy {

// Report::Report -- oracle 0x403DA0 (37 B). The first store is the vptr; 1.17's
// vftable is at 0x4FDCA8.
Report::Report()
{
  pFile = nullptr;
  dwFlags = 0;
  szName[0] = 0;
  szPath[0] = 0;
}

// 0x403E10 (34 B). Puts the base vtable back and closes the file.
Report::~Report()
{
  if (pFile != nullptr) {
    fclose(pFile);
    pFile = nullptr;
  }
}

// Report::open -- oracle 0x403E40 (332 B). Names the report after the local
// time, creates its directory and opens the file. The `+ 1` makes the two
// length checks unsigned compares against the buffer size.
#ifndef OC_PAYLOAD
bool Report::open(const char *pszFileName)
{
  wchar_t wszPath[0x104];
  struct tm tmNow;
  __time64_t tNow = _time64(nullptr);

  _localtime64_s(&tmNow, &tNow);

  if (errors_dir[0] == 0) {
    bool bOk = false;
    DWORD cchTemp = GetTempPathW(0x104, wszPath);

    if (cchTemp != 0) {
      bOk = WideCharToMultiByte(CP_UTF8, 0, wszPath, cchTemp + 1, errors_dir,
                                sizeof(errors_dir), nullptr, nullptr) != 0;

      if (bOk)
        bOk = PathAppendBlizzardError(errors_dir, sizeof(errors_dir), "BlizzardError");
    }

    if (!bOk)
      return false;
  }

  if (internal::snprintf(szName, sizeof(szName), "%s/%04d-%02d-%02d %02d.%02d.%02d",
                         errors_dir, tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
                         tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec) + 1 <= sizeof(szName)) {
    if (MultiByteToWideChar(CP_UTF8, 0, szName, -1, wszPath, 0x104)) {
      if (internal::MakeDirRecursive(wszPath)) {
        if (internal::snprintf(szPath, sizeof(szPath), "%s/%s", szName, pszFileName) + 1 > sizeof(szPath))
          return false;

        pFile = internal::FileOpen(szPath);
        return pFile != nullptr;
      }
    }
  }

  return false;
}
#endif

// Report::close -- oracle 0x403F90 (30 B). Always true.
bool Report::close()
{
  if (pFile != nullptr) {
    fclose(pFile);
    pFile = nullptr;
  }

  return true;
}

// Report::is_open -- oracle 0x403FB0 (9 B).
bool Report::is_open()
{
  return pFile != nullptr;
}

// Report::add_attachment -- oracle 0x403FC0 (301 B). The rename arm builds
// "<szName>/<basename>" in a local, copies the file there and records that
// path; otherwise the caller's path is recorded as it stands. Both arms end in
// the same strlcpy into the next free slot.
bool Report::add_attachment(const char *pszPath, bool bRename)
{
  char szTemp[0x400];

  if (dwFlags >= 16)
    return false;

  const char *pszSource;

  if (bRename) {
    if (szName[0] == 0)
      return false;

    size_t cchName = internal::strlcpy(szTemp, szName, sizeof(szTemp));

    if (cchName >= sizeof(szTemp))
      return false;

    char *pszTail = &szTemp[cchName];

    *pszTail++ = '/';

    if (!internal::FileBasename(pszPath, pszTail,
                                (DWORD)(&szTemp[sizeof(szTemp)] - pszTail)))
      return false;

    internal::FileCopy(pszPath, szTemp);

    pszSource = szTemp;
  } else {
    if (strlen(pszPath) >= sizeof(szTemp))
      return false;

    pszSource = pszPath;
  }

  internal::strlcpy(aszAttachments[dwFlags], pszSource, sizeof(szTemp));
  dwFlags++;

  return true;
}

// Report::printf -- oracle 0x4040F0 (26 B).
void Report::printf(const char *pszFormat, ...)
{
  va_list args;

  va_start(args, pszFormat);
  vfprintf(pFile, pszFormat, args);
  va_end(args);
}

}  // namespace crashy

namespace crashy {
namespace internal {

// internal::snprintf -- oracle 0x404AE0 (40 B). The terminator is written after
// the call, and the result is _vsnprintf's own.
int __cdecl snprintf(char *pszBuf, size_t cchBuf, const char *pszFormat, ...)
{
  va_list args;

  va_start(args, pszFormat);

  int nResult = _vsnprintf(pszBuf, cchBuf - 1, pszFormat, args);

  va_end(args);

  pszBuf[cchBuf - 1] = 0;

  return nResult;
}

// internal::FileOpen -- oracle 0x404BF0 (100 B). Opens a UTF-8 path for writing
// through the wide CRT.
FILE *FileOpen(const char *pszPath)
{
  wchar_t wszPath[0x104];
  wchar_t wszMode[0x20];

  if (!MultiByteToWideChar(CP_UTF8, 0, pszPath, -1, wszPath, 0x104))
    return nullptr;

  if (!MultiByteToWideChar(CP_UTF8, 0, "w", -1, wszMode, 0x20))
    return nullptr;

  return _wfopen(wszPath, wszMode);
}


// internal::FileCopy -- oracle 0x404C60 (116 B). Copies between two UTF-8
// paths, never over an existing file.
bool FileCopy(const char *pszSource, const char *pszDest)
{
  wchar_t wszSource[0x104];
  wchar_t wszDest[0x104];

  if (!MultiByteToWideChar(CP_UTF8, 0, pszSource, -1, wszSource, 0x104))
    return false;

  if (!MultiByteToWideChar(CP_UTF8, 0, pszDest, -1, wszDest, 0x104))
    return false;

  return CopyFileW(wszSource, wszDest, TRUE) != 0;
}

// internal::MakeDirRecursive -- oracle 0x404CE0 (130 B). The second wcsrchr is
// called twice: 1.17 picks the later separator through a macro that evaluates
// its argument twice. The separator is put back as a backslash whatever it was,
// and only on the path that created the parent.
bool MakeDirRecursive(wchar_t *pwszPath)
{
  if (CreateDirectoryW(pwszPath, NULL))
    return true;

  DWORD dwAttributes = GetFileAttributesW(pwszPath);

  if (dwAttributes != INVALID_FILE_ATTRIBUTES)
    return (dwAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

  wchar_t *pwszSep = const_cast<wchar_t *>(wcsrchr(static_cast<const wchar_t *>(pwszPath), L'\\'));

  if (wcsrchr(static_cast<const wchar_t *>(pwszPath), L'/') > pwszSep)
    pwszSep = const_cast<wchar_t *>(wcsrchr(static_cast<const wchar_t *>(pwszPath), L'/'));

  if (pwszSep == NULL)
    return false;

  *pwszSep = L'\0';

  if (!MakeDirRecursive(pwszPath))
    return false;

  *pwszSep = L'\\';

  return CreateDirectoryW(pwszPath, NULL) != 0;
}

// internal::FileBasename -- oracle 0x405900 (140 B).
bool FileBasename(const char *pszPath, char *pszOut, DWORD cchOut)
{
  const char *pszBase = NULL;
  const char *pszHit;

  if ((pszHit = strrchr(pszPath, '/')) != NULL && pszHit > pszBase)
    pszBase = pszHit;

  if ((pszHit = strrchr(pszPath, '\\')) != NULL && pszHit > pszBase)
    pszBase = pszHit;

  if ((pszHit = strrchr(pszPath, ':')) != NULL && pszHit > pszBase)
    pszBase = pszHit;

  if (pszBase != NULL)
    pszPath = pszBase + 1;

  if (strlcpy(pszOut, pszPath, cchOut) < cchOut)
    return true;

  return false;
}
}  // namespace internal
}  // namespace crashy

namespace crashy {

// Report::PathAppendBlizzardError -- oracle 0x405990 (90 B). Appends a
// separator when the path does not already end in one, then pszMore. The /GL
// link folded open's constant arguments into 1.17's body.
#ifndef OC_PAYLOAD
bool Report::PathAppendBlizzardError(char *pszPath, size_t cchPath, const char *pszMore)
{
  char *pszEnd = pszPath + strlen(pszPath);

  if (pszEnd == pszPath + cchPath)
    return false;

  if (pszEnd != pszPath) {
    char chLast = pszEnd[-1];

    if (chLast != '/' && chLast != '\\')
      *pszEnd = '\\';
  }

  return internal::strlcat(pszPath, pszMore, cchPath) < cchPath;
}
#endif

}  // namespace crashy
