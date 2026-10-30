// SystemSurvey.cpp -- the machine-survey report: the stat file it writes, the
// process timestamps it stamps into it, and the file helpers around both.
// Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <windows.h>
#include <shlobj.h>

#include "../h/SystemSurvey.h"
#include "../h/CodeSign.h"
#include "../gen/globals117.h"

// CRT entries declared here rather than in SystemSurvey.h, which main.cpp
// includes beside its own fclose taking a void *.
extern "C" {
errno_t __cdecl fopen_s(FILE **ppFile, const char *pszName, const char *pszMode);
int __cdecl fclose(FILE *pFile);
char *__cdecl fgets(char *pszBuf, int cchBuf, FILE *pFile);
int __cdecl sscanf_s(const char *pszSrc, const char *pszFormat, ...);
}

// Globals this TU reaches, dual-spelled.

#ifndef OC_gszStatGameCode_g
#define OC_gszStatGameCode_g
#ifdef OC_DLL_MODE
#define gszStatGameCode_g (reinterpret_cast<char *>(OC_VA(0x50D858u)))
#else
extern char gszStatGameCode_g[256] OC_UNALIGNED;
#endif
#endif

#ifndef OC_gszStatGameName_g
#define OC_gszStatGameName_g
#ifdef OC_DLL_MODE
#define gszStatGameName_g (reinterpret_cast<char *>(OC_VA(0x50D958u)))
#else
extern char gszStatGameName_g[256] OC_UNALIGNED;
#endif
#endif

#ifndef OC_gpszConnectionName_g
#define OC_gpszConnectionName_g
#ifdef OC_DLL_MODE
#define gpszConnectionName_g (reinterpret_cast<const char **>(OC_VA(0x50DA58u)))
#else
extern const char *gpszConnectionName_g[6];
#endif
#endif

// 0x1000 bytes, LocalAppDataDirectory's StringCchCopyA bound.
#ifndef OC_gszStatAppDataPath_g
#define OC_gszStatAppDataPath_g
#ifdef OC_DLL_MODE
#define gszStatAppDataPath_g (reinterpret_cast<char *>(OC_VA(0x6AB530u)))
#else
extern char gszStatAppDataPath_g[0x1000];
#endif
#endif

// 0x4A8320 (45 B). Copies the characters at or above ' ' and drops the rest.
void get_printable_string(char *pszDst, const char *pszSrc, int cch)
{
  char c;

  if (cch == 0)
    return;

  for (c = *pszSrc; c != '\0' && cch != 0; c = *++pszSrc) {
    if ((unsigned char)c >= ' ') {
      *pszDst++ = c;
      cch--;
    }
  }
  *pszDst = '\0';
}

// CalcChecksum -- oracle 0x4A8390 (150 B). The first fread sits before the loop,
// as in 1.17.
unsigned int CalcChecksum(FILE *pFile)
{
  char buf[0x1000];
  LONG nPos;
  unsigned int nCrc;
  size_t nRead;

  if (pFile == NULL)
    return 0;

  nPos = ftell(pFile);
  rewind(pFile);

  nCrc = crc32(0, NULL, 0);

  nRead = fread(buf, 1, sizeof(buf), pFile);
  while (nRead != 0) {
    nCrc = crc32(nCrc, buf, (unsigned int)nRead);
    nRead = fread(buf, 1, sizeof(buf), pFile);
  }

  fseek(pFile, nPos, SEEK_SET);
  return nCrc;
}

// 0x4A8430 (152 B). Cuts the path at its last backslash and creates what is
// left. A failed create still answers true when the attributes read before it
// have the directory bit, which INVALID_FILE_ATTRIBUTES also has.
bool CreateDirectory(const char *pszPath)
{
  char szDir[0x1000];
  const char *pszSlash;
  DWORD dwAttrs;

  if (pszPath == NULL)
    return false;

  pszSlash = strrchr(pszPath, '\\');
  if (pszSlash == NULL)
    return false;

  StringCchCopyA(szDir, strlen(pszPath) - strlen(pszSlash) + 1, pszPath);
  dwAttrs = GetFileAttributesA(szDir);
  return SHCreateDirectoryExA(NULL, szDir, NULL) == ERROR_SUCCESS ||
         (dwAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// GetEndTime -- oracle 0x4A84D0 (76 B).
bool GetEndTime(struct tm *ptm)
{
  __time64_t t;
  struct tm *ptmNow;

  if (ptm == NULL)
    return false;

  _time64(&t);

  ptmNow = _gmtime64(&t);
  if (ptmNow == NULL)
    return false;

  *ptm = *ptmNow;
  return true;
}

// GetFileSize -- oracle 0x4A8520 (61 B). ftell's length goes through a ULONG, so
// the return widens it by zero extension, not sign extension.
unsigned __int64 GetFileSize(FILE *pFile)
{
  LONG nPos;
  ULONG nSize;

  if (pFile == NULL)
    return 0;

  nPos = ftell(pFile);
  fseek(pFile, 0, SEEK_END);
  nSize = ftell(pFile);
  fseek(pFile, nPos, SEEK_SET);

  return nSize;
}

// GetStartTime -- oracle 0x4A8560 (132 B). The FILETIME is divided as one 64-bit
// value, read through a pun rather than rebuilt from its halves. 11644473600 is
// the 1601-to-1970 offset in seconds.
bool GetStartTime(struct tm *ptm)
{
  FILETIME ftCreate;
  FILETIME ftExit;
  FILETIME ftKernel;
  FILETIME ftUser;
  __time64_t t;
  struct tm *ptmStart;

  if (ptm == NULL)
    return false;

  if (!GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernel, &ftUser))
    return false;

  t = (__time64_t)(*(const unsigned __int64 *)&ftCreate / 10000000ULL - 11644473600ULL);

  ptmStart = _gmtime64(&t);
  if (ptmStart == NULL)
    return false;

  *ptm = *ptmStart;
  return true;
}

// GetSurveyProduct -- oracle 0x4A85F0 (6 B).
char *GetSurveyProduct()
{
  return gszStatGameCode_g;
}

// 0x4A8600 (104 B). The path is built once and kept; a failed create clears
// it again so the next call retries.
char *LocalAppDataDirectory()
{
  size_t nLen;

  if (gszStatAppDataPath_g[0] != '\0')
    return gszStatAppDataPath_g;

  if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, gszStatAppDataPath_g) < 0)
    return NULL;

  nLen = strlen(gszStatAppDataPath_g);
  StringCchCopyA(gszStatAppDataPath_g + nLen, 0x1000 - nLen,
                 "\\Blizzard Entertainment\\System Survey\\ProductData\\");
  if (CreateDirectory(gszStatAppDataPath_g))
    return gszStatAppDataPath_g;

  gszStatAppDataPath_g[0] = '\0';
  return NULL;
}

// 0x4A8670 (181 B). Starts SystemSurvey.exe from the game's own directory,
// but only once its signature checks out.
bool RunSystemSurvey()
{
  char szPath[0x1000];
  PROCESS_INFORMATION pi;
  char *pszName;

  GetModuleFileNameA(NULL, szPath, sizeof(szPath));
  pszName = const_cast<char *>(strrchr(static_cast<const char *>(szPath), '\\'));
  if (pszName == NULL)
    return false;
  pszName++;
  StringCchCopyA(pszName, szPath + sizeof(szPath) - pszName, "SystemSurvey.exe");
  if (VerifyBlizzardSignature(szPath)) {
    STARTUPINFOA si = { sizeof(si) };

    if (CreateProcessA(szPath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      return true;
    }
  }
  return false;
}

// stat_open_file -- oracle 0x4A8730 (25 B).
bool stat_open_file(FILE **ppFile, const char *pszName, const char *pszMode)
{
  return fopen_s(ppFile, pszName, pszMode) ? false : true;
}

// 0x4A8750 (34 B).
bool SetSurveyProduct(const char *pszProduct)
{
  sprintf_s(gszStatGameCode_g, 0x100, "%s", pszProduct);
  gszStatGameCode_g[0xFF] = '\0';
  return true;
}

// StringCchCopyA -- oracle 0x4A8780 (112 B). strsafe.h's own STRSAFEAPI body,
// its two STRSAFEWORKERAPI helpers kept as separate functions, each with its own
// `hr`, as 1.17 tests the validation result before the copy.
#define STRSAFE_MAX_CCH                2147483647
#define STRSAFE_MAX_LENGTH             (STRSAFE_MAX_CCH - 1)
#define STRSAFE_E_INVALID_PARAMETER    ((LONG)0x80070057L)
#define STRSAFE_E_INSUFFICIENT_BUFFER  ((LONG)0x8007007AL)

static __inline LONG __stdcall StringValidateDestA(const char *pszDest, size_t cchDest,
                                                   const size_t cchMax)
{
  LONG hr = 0;

  if ((cchDest == 0) || (cchDest > cchMax))
    hr = STRSAFE_E_INVALID_PARAMETER;

  return hr;
}

static __inline LONG __stdcall StringCopyWorkerA(char *pszDest, size_t cchDest,
                                                 size_t *pcchNewDestLength,
                                                 const char *pszSrc, size_t cchToCopy)
{
  LONG hr = 0;
  size_t cchNewDestLength = 0;

  while (cchDest && cchToCopy && (*pszSrc != '\0')) {
    *pszDest++ = *pszSrc++;
    cchDest--;
    cchToCopy--;

    cchNewDestLength++;
  }

  if (cchDest == 0) {
    pszDest--;
    cchNewDestLength--;

    hr = STRSAFE_E_INSUFFICIENT_BUFFER;
  }

  *pszDest = '\0';

  if (pcchNewDestLength)
    *pcchNewDestLength = cchNewDestLength;

  return hr;
}

LONG __stdcall StringCchCopyA(char *pszDest, size_t cchDest, const char *pszSrc)
{
  LONG hr;

  hr = StringValidateDestA(pszDest, cchDest, STRSAFE_MAX_CCH);

  if (hr >= 0)
    hr = StringCopyWorkerA(pszDest, cchDest, NULL, pszSrc, STRSAFE_MAX_LENGTH);

  return hr;
}

// 0x4A87F0 (673 B). The per-file handle is never closed, and the 64-bit size is
// printed through "%i"; both are 1.17's.
bool WriteSurveyData(const char *pszDir, const char **apszFiles, unsigned int nFiles)
{
  char szFile[0x1000];
  char szIndex[0x1000];
  struct tm tmNow;
  const char *pszAppData;
  unsigned int nCrc;
  unsigned __int64 nSize;
  FILE *pIndex;
  FILE *pData;
  const char *pszName;
  unsigned int i;

  if (nFiles == 0)
    return true;
  if (pszDir == NULL || apszFiles == NULL)
    return false;

  pszAppData = LocalAppDataDirectory();
  if (pszAppData == NULL)
    return false;

  sprintf_s(szIndex, sizeof(szIndex), "%s%s%c%s.index", pszAppData, gszStatGameCode_g, '\\',
            pszDir);
  szIndex[sizeof(szIndex) - 1] = '\0';
  pIndex = NULL;
  if (fopen_s(&pIndex, szIndex, "wb") != 0)
    return false;

  fprintf(pIndex, "{\n");
  fprintf(pIndex, "\t\"product_code\": \"%s\",\n", gszStatGameCode_g);
  fprintf(pIndex, "\t\"application_code\": \"%s\",\n", gszStatGameName_g);
  if (GetStartTime(&tmNow)) {
    fprintf(pIndex, "\t\"last_run_start\": \"");
    WriteTimestamp(&pIndex, &tmNow);
    fprintf(pIndex, "\",\n");
  }
  if (GetEndTime(&tmNow)) {
    fprintf(pIndex, "\t\"last_run_end\": \"");
    WriteTimestamp(&pIndex, &tmNow);
    fprintf(pIndex, "\",\n");
    fprintf(pIndex, "\t\"timestamp\": \"");
    WriteTimestamp(&pIndex, &tmNow);
    fprintf(pIndex, "\",\n");
  }
  fprintf(pIndex, "\t\"files\": [\n");

  for (i = 0; i < nFiles; i++) {
    pszName = apszFiles[i];
    if (*pszName == '\\')
      pszName++;

    sprintf_s(szFile, sizeof(szFile), "%s%s%c%s%c%s", pszAppData, gszStatGameCode_g, '\\', pszDir,
              '\\', pszName);
    szFile[sizeof(szFile) - 1] = '\0';
    pData = NULL;
    if (fopen_s(&pData, szFile, "rb") != 0)
      continue;

    nSize = GetFileSize(pData);
    nCrc = CalcChecksum(pData);
    if (nSize == 0 || nCrc == 0)
      continue;

    fprintf(pIndex, "\t\t{\n");
    fprintf(pIndex, "\t\t\t\"path\": \"%s%c%s\",\n", pszDir, '\\', pszName);
    fprintf(pIndex, "\t\t\t\"checksum\": %u,\n", nCrc);
    fprintf(pIndex, "\t\t\t\"size\": %i\n", nSize);
    fprintf(pIndex, "\t\t}%s\n", i < nFiles - 1 ? "," : "");
  }

  fprintf(pIndex, "\t]\n}\n");
  fclose(pIndex);
  return true;
}

// WriteTimestamp -- oracle 0x4A8AA0 (41 B).
void WriteTimestamp(FILE **ppFile, const struct tm *ptm)
{
  fprintf(*ppFile, "%04u-%02u-%02uT%02u:%02u:%02u",
          ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
          ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
}

// get_provider_index -- oracle 0x4A8AD0 (127 B). The length handed to strncmp
// is a min() macro, so strlen is called twice, as in 1.17.
int get_provider_index(const char *pszName, size_t nLen)
{
  unsigned int i;

  if (pszName == NULL || nLen == 0)
    return 0;

  for (i = 0; i < 6; i++) {
    if (strncmp(pszName, gpszConnectionName_g[i],
                nLen < strlen(gpszConnectionName_g[i]) ? nLen
                                                       : strlen(gpszConnectionName_g[i])) == 0)
      return i;
  }

  return 0;
}
