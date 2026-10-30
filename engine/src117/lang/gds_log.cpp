// gds_log.cpp -- the crash-log writer. log_printf appends to a 4 KB buffer,
// log_flush writes it to a per-user .ERR file, and log_create builds that
// file's name. Bodies are in oracle VA order.
//
// Every entry point that touches the buffer takes the same critical section at
// 0x6D168C. log_flush's "could not create the file" arm returns without
// releasing it, a leak 1.17 has and this keeps.

#include "oc_data.h"  // Hyperion: OC_VA
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../h/gds_log.h"
#include "../h/gds_local.h"     // psnprintf
#include "../gen/globals117.h"

// SFileGetUserDataPath (Storm ordinal 296, spelled as replay.cpp spells it) and
// SStrCopy (ordinal 501) are declared here, not through Storm.h, which includes
// <windows.h> under NOGDI where this TU needs VS_FIXEDFILEINFO. _mbsrchr is
// declared here, not through <mbstring.h>, as gluBNCreate.cpp does.
extern "C" {
// Hyperion: as port_stormfile.cpp defines it
DWORD  __stdcall SFileGetUserDataPath(char *pszBuf, DWORD cchBuf, DWORD dwFlags);
char * __stdcall SStrCopy(char *pszDest, const char *pszSrc, size_t cchDest);
unsigned char *__cdecl _mbsrchr(const unsigned char *pszStr, unsigned int ch);
}  // extern "C"

// The globals this TU reaches, dual-spelled, one #ifdef per name. Every name
// but sgszErrFile's is coined here.

// 0x50F0E8 -- asks log_create to rebuild the file name. The image initialises
// it to 1 and log_create clears it on every exit, so the name is built once.
#ifdef OC_DLL_MODE
#define sgbLogNameStale_l (*reinterpret_cast<int *>(OC_VA(0x50F0E8u)))
#else
extern int sgbLogNameStale_l;
#endif

// 0x50F0EC -- the open .ERR file. Initialised to -1 in the image, which is what
// log_flush compares against.
#ifdef OC_DLL_MODE
#define shLogFile_l (*reinterpret_cast<HANDLE *>(OC_VA(0x50F0ECu)))
#else
extern HANDLE shLogFile_l;
#endif

// 0x6D1680 / 0x6D1684 -- the pending-text buffer and how many bytes are in it.
// log_printf commits the buffer with VirtualAlloc(0x1000) on first use and
// flushes whenever the new total would pass 0x1000.
#ifdef OC_DLL_MODE
#define sgpLogBuf_l (*reinterpret_cast<BYTE **>(OC_VA(0x6D1680u)))
#else
extern BYTE *sgpLogBuf_l;
#endif

#ifdef OC_DLL_MODE
#define sgcbLogBuf_l (*reinterpret_cast<int *>(OC_VA(0x6D1684u)))
#else
extern int sgcbLogBuf_l;
#endif

// 0x6D1688 -- how many of the ten name slots are used.
#ifdef OC_DLL_MODE
#define sgnLogFiles_l (*reinterpret_cast<int *>(OC_VA(0x6D1688u)))
#else
extern int sgnLogFiles_l;
#endif

// 0x6D168C -- the lock every entry point takes.
#ifdef OC_DLL_MODE
#define sgLogCrit_l (*reinterpret_cast<CRITICAL_SECTION *>(OC_VA(0x6D168Cu)))
#else
extern CRITICAL_SECTION sgLogCrit_l;
#endif

// 0x6D16A8 -- the free-text line the banner ends with ("INFO: %s"). Only ever
// its address is taken, so no extent is claimed.
#ifdef OC_DLL_MODE
#define sgszLogInfo_l (reinterpret_cast<char *>(OC_VA(0x6D16A8u)))
#else
extern char sgszLogInfo_l[];
#endif

// 0x6D1830 -- the names of the .ERR files written this run, ten slots of 0x104
// (log_get_filenames stops at 0xA28). The table ends at 0x6D2258, the next
// global.
#ifdef OC_DLL_MODE
#define sgszLogFiles_l (reinterpret_cast<char (*)[0x104]>(OC_VA(0x6D1830u)))
#else
extern char sgszLogFiles_l[10][0x104];
#endif

// 0x6D2258 -- log_init's once flag, a BYTE.
#ifdef OC_DLL_MODE
#define sgbLogInited_l (*reinterpret_cast<BYTE *>(OC_VA(0x6D2258u)))
#else
extern BYTE sgbLogInited_l;
#endif

// 0x6D2260 -- the current .ERR path. 0x104 bytes, the count psnprintf and
// SStrCopy are both given for it.
#ifdef OC_DLL_MODE
  #ifndef sgszErrFile
  #define sgszErrFile (reinterpret_cast<char *>(OC_VA(0x6D2260u)))
  #endif
#else
extern char sgszErrFile[0x104];
#endif

// The bodies, in oracle VA order.

// 0x4CC4C0 (201 B). Walks the path one component at a time, creating each
// prefix. ERROR_ALREADY_EXISTS and ERROR_ACCESS_DENIED are forgiven only when
// the name really is a directory. szPart is 0x105 bytes, the clamp's constant.
// The success arm first and the clamp as a ternary are 1.17's shape.
BOOL __fastcall CreateDirectoriesRecursive(char *pszPath)
{
  char szPart[0x105];
  char *psz;
  DWORD dwErr;
  DWORD dwAttrs;
  unsigned cch;

  if (pszPath == 0) {
    return 0;
  }
  psz = pszPath;
  while (*psz != 0) {
    while (*psz != 0 && *psz != '\\') {
      psz++;
    }
    cch = (unsigned)(psz - pszPath) + 2;
    if ((uintptr_t)SStrCopy(szPart, pszPath, cch < 0x105 ? cch : 0x105) >= 0x104) {
      return 0;
    }
    if (CreateDirectoryA(szPart, 0) != 0) {
      if (*psz == 0) {
        return 1;
      }
    } else {
      dwErr = GetLastError();
      if (dwErr != ERROR_ALREADY_EXISTS && dwErr != ERROR_ACCESS_DENIED) {
        return 0;
      }
      dwAttrs = GetFileAttributesA(szPart);
      if (dwAttrs == (DWORD)-1 || (dwAttrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return 0;
      }
    }
    psz++;
  }
  return 1;
}

// 0x4CC590 (504 B). Builds "<user path>Errors\<user><minor><build><rev>.ERR"
// once, then opens it; on failure the name is rebuilt at the drive root and
// tried once more. cchUser and the retry counter are separate variables, and
// pszTail's fallback comes before szRoot is initialised, as in 1.17.
void *log_create(void)
{
  int i;
  HANDLE hFile;

  log_init();
  if (sgbLogNameStale_l != 0) {
    char szUserPath[0x104];
    char szDir[0x104];
    VS_FIXEDFILEINFO vi;
    char szUser[0x20];

    SFileGetUserDataPath(szUserPath, 0x104, 1);
    psnprintf(szDir, 0x104, "%sErrors\\", szUserPath);
    CreateDirectoriesRecursive(szDir);
    DWORD cchUser = 0x20;
    if (GetUserNameA(szUser, &cchUser) == 0) {
      szUser[0] = 0;
    }
    log_get_version(&vi);
    psnprintf(sgszErrFile, 0x104, "%s%s%02u%02u%02u.ERR", szDir, szUser,
              (unsigned short)vi.dwProductVersionMS,
              vi.dwProductVersionLS >> 16,
              (unsigned short)vi.dwProductVersionLS);
  }

  hFile = INVALID_HANDLE_VALUE;
  for (i = (sgbLogNameStale_l == 0); i < 2; i++) {
    hFile = CreateFileA(sgszErrFile, GENERIC_WRITE, FILE_SHARE_READ, 0,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (hFile != INVALID_HANDLE_VALUE)
      break;
    {
      char *pszTail =
          (char *)_mbsrchr((const unsigned char *)sgszErrFile, '\\');
      if (pszTail == 0) {
        pszTail = sgszErrFile;
      }
      char szRoot[0x104] = "c:\\";

      strcat(szRoot, pszTail);
      strcpy(sgszErrFile, szRoot);
    }
  }
  if (hFile != INVALID_HANDLE_VALUE) {
    if (GetFileSize(hFile, 0) > 0x10000) {
      SetEndOfFile(hFile);
    }
    SStrCopy(sgszLogFiles_l[sgnLogFiles_l], sgszErrFile, 0x104);
    sgnLogFiles_l++;
  }
  sgbLogNameStale_l = 0;
  return hFile;
}

// 0x4CC790 (166 B). The file is seeked to the end only on the call that just
// created it; an already-open handle is written at its current position.
void __cdecl log_flush(int bCloseFile)
{
  DWORD cbWritten;

  EnterCriticalSection(&sgLogCrit_l);
  if (sgcbLogBuf_l != 0) {
    if (shLogFile_l == INVALID_HANDLE_VALUE) {
      shLogFile_l = log_create();
      if (shLogFile_l == INVALID_HANDLE_VALUE) {
        sgcbLogBuf_l = 0;
        return;
      }
      SetFilePointer(shLogFile_l, 0, 0, FILE_END);
    }
    WriteFile(shLogFile_l, sgpLogBuf_l, sgcbLogBuf_l, &cbWritten, 0);
    sgcbLogBuf_l = 0;
  }
  if (bCloseFile != 0 && shLogFile_l != INVALID_HANDLE_VALUE) {
    CloseHandle(shLogFile_l);
    shLogFile_l = INVALID_HANDLE_VALUE;
  }
  LeaveCriticalSection(&sgLogCrit_l);
}

// 0x4CC840 (47 B). Three induction variables, as in 1.17: the index, set before
// the guard, and the byte offset and row pointer, set inside it. 0xA28 is a
// literal: in the DLL-mode spelling the table is a pointer, its sizeof 4.
void __fastcall log_get_filenames(char **ppszOut, unsigned nMax)
{
  unsigned i = 0;
  if (nMax != 0) {
    char *psz = sgszLogFiles_l[0];
    unsigned nOff = 0;
    do {
      if (nOff >= 0xA28)
        break;
      ppszOut[i] = psz;
      nOff += 0x104;
      i++;
      psz += 0x104;
    } while (i < nMax);
  }
}

// 0x4CC870 (87 B).
void log_init(void)
{
  int i;

  if (sgbLogInited_l == 0) {
    for (i = 0; i < 10; i++) {
      sgszLogFiles_l[i][0] = 0;
    }
    sgbLogInited_l = 1;
  }
}

// 0x4CC8D0 (196 B). The version fields are split into their two halves at the
// call, so the format takes four %d and not two.
void __cdecl log_dump_computer_info(void)
{
  SYSTEMTIME st;
  DWORD cchComputer;
  DWORD cchUser;
  char szComputer[0x40];
  char szUser[0x40];
  VS_FIXEDFILEINFO vi;

  GetLocalTime(&st);
  cchUser = 0x40;
  if (GetUserNameA(szUser, &cchUser) == 0) {
    szUser[0] = 0;
  }
  cchComputer = 0x40;
  if (GetComputerNameA(szComputer, &cchComputer) == 0) {
    szComputer[0] = 0;
  }
  log_get_version(&vi);
  log_printf("\r\n------------------------------------------------------\r\n"
             "PROGRAM VERSION: %d.%d.%d.%d\r\nCOMPUTER NAME: %s\r\n"
             "USER NAME:\t %s\r\nTIME: %02u/%02u/%02u %02u:%02u:%02u.%3d\r\n"
             "INFO: %s\r\n\r\n",
             vi.dwProductVersionMS >> 16, (unsigned short)vi.dwProductVersionMS,
             vi.dwProductVersionLS >> 16, (unsigned short)vi.dwProductVersionLS,
             szComputer, szUser, st.wMonth, st.wDay, st.wYear % 100, st.wHour,
             st.wMinute, st.wSecond, st.wMilliseconds, sgszLogInfo_l);
}

// 0x4CC9A0 (194 B). The length is size_t, so the 0x1000 test is unsigned. The
// byte count is held in a local across the body, as 1.17 does.
void __cdecl log_printf(const char *pszFormat, ...)
{
  char szLine[0x200];
  va_list args;
  size_t cch;
  unsigned cb;

  EnterCriticalSection(&sgLogCrit_l);
  va_start(args, pszFormat);
  _vsnprintf(szLine, 0x200, pszFormat, args);
  szLine[0x1FF] = 0;
  cch = strlen(szLine);
  cb = sgcbLogBuf_l;
  if (cb + cch > 0x1000) {
    log_flush(0);
    cb = sgcbLogBuf_l;
  }
  if (sgpLogBuf_l == 0) {
    sgpLogBuf_l = (BYTE *)VirtualAlloc(0, 0x1000, MEM_COMMIT, PAGE_READWRITE);
    cb = 0;
    sgcbLogBuf_l = cb;
  }
  if (sgpLogBuf_l != 0) {
    memcpy(sgpLogBuf_l + cb, szLine, cch);
    cb += (unsigned int)cch;
    // Two identical arms, which MSVC merges: they keep the add in EDI as 1.17
    // has it.
    if (cb & 1)
      sgcbLogBuf_l = cb;
    else
      sgcbLogBuf_l = cb;
  }
  LeaveCriticalSection(&sgLogCrit_l);
}

// 0x4CCA70 (180 B). The copy is clamped to the caller's 0x34 bytes even though
// VerQueryValueA reports the block's own length.
void __fastcall log_get_version(VS_FIXEDFILEINFO *pInfo)
{
  char szModule[0x104];
  DWORD dwHandle;
  void *pFixed;
  UINT cbFixed;
  DWORD cbInfo;
  void *pBlock;
  UINT cbCopy;

  memset(pInfo, 0, 0x34);
  if (GetModuleFileNameA(0, szModule, 0x104) != 0) {
    cbInfo = GetFileVersionInfoSizeA(szModule, &dwHandle);
    if (cbInfo != 0) {
      pBlock = VirtualAlloc(0, cbInfo, MEM_COMMIT, PAGE_READWRITE);
      if (GetFileVersionInfoA(szModule, 0, cbInfo, pBlock) != 0) {
        if (VerQueryValueA(pBlock, "\\", &pFixed, &cbFixed) != 0) {
          cbCopy = 0x34;
          if (cbFixed < 0x34) {
            cbCopy = cbFixed;
          }
          memcpy(pInfo, pFixed, cbCopy);
        }
      }
      VirtualFree(pBlock, 0, MEM_RELEASE);
    }
  }
}
