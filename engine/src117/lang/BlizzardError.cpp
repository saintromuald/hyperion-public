// Hyperion: not built. The game's crash reporter is gone from the port and the
// port's own (port_crash.cpp) stays; the file is kept so that later changes to
// the reconstruction still apply. An address of its functions in 1.17's data
// goes to PortOriginalCodeReached.
//
// BlizzardError.cpp -- a thread's stack extent, the inspector's argument buffer
// and the launch itself. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/Storm.h"          // the one <windows.h>: the direct-IAT calls
#include "../h/crashy.h"
#include "../h/BlizzardError.h"
#include "../gen/globals117.h"

extern "C" void *__cdecl memset(void *, int, size_t);

// Globals this TU reaches, dual-spelled.

// 0x6D4510 sgszCrashArgs_g -- the inspector command line PushArg fills and
// SubmitReport reads back, 0x400 bytes.
#ifndef OC_SGSZCRASHARGS_G
#define OC_SGSZCRASHARGS_G
#ifdef OC_DLL_MODE
#define sgszCrashArgs_g (*reinterpret_cast<char (*)[0x400]>(OC_VA(0x6D4510u)))
#else
extern char sgszCrashArgs_g[0x400];
#endif
#endif

// 0x6D44F8 / 0x6DC928 -- the two critical sections NDebugHelp::Initialize
// hands to InitializeCriticalSection.
#ifndef OC_SGDBGHELPLOCK_G
#define OC_SGDBGHELPLOCK_G
#ifdef OC_DLL_MODE
#define sgDbgHelpLock_g (*reinterpret_cast<CRITICAL_SECTION *>(OC_VA(0x6D44F8u)))
#else
extern CRITICAL_SECTION sgDbgHelpLock_g;
#endif
#endif

#ifndef OC_SGMINIDUMPLOCK_G
#define OC_SGMINIDUMPLOCK_G
#ifdef OC_DLL_MODE
#define sgMiniDumpLock_g (*reinterpret_cast<CRITICAL_SECTION *>(OC_VA(0x6DC928u)))
#else
extern CRITICAL_SECTION sgMiniDumpLock_g;
#endif
#endif

// 0x6DC940 / 0x6DC944 -- the two CreateEventW handles, stored in call order.
#ifndef OC_SGHMINIDUMPREQUEST_G
#define OC_SGHMINIDUMPREQUEST_G
#ifdef OC_DLL_MODE
#define sghMiniDumpRequest_g (*reinterpret_cast<HANDLE *>(OC_VA(0x6DC940u)))
#else
extern HANDLE sghMiniDumpRequest_g;
#endif
#endif

#ifndef OC_SGHMINIDUMPDONE_G
#define OC_SGHMINIDUMPDONE_G
#ifdef OC_DLL_MODE
#define sghMiniDumpDone_g (*reinterpret_cast<HANDLE *>(OC_VA(0x6DC944u)))
#else
extern HANDLE sghMiniDumpDone_g;
#endif
#endif

// 0x6D40EC -- CreateThread's lpThreadId out-slot; 0x6D44F0 its handle.
#ifndef OC_SGDWMINIDUMPTHREADID_G
#define OC_SGDWMINIDUMPTHREADID_G
#ifdef OC_DLL_MODE
#define sgdwMiniDumpThreadId_g (*reinterpret_cast<DWORD *>(OC_VA(0x6D40ECu)))
#else
extern DWORD sgdwMiniDumpThreadId_g;
#endif
#endif

#ifndef OC_SGHMINIDUMPTHREAD_G
#define OC_SGHMINIDUMPTHREAD_G
#ifdef OC_DLL_MODE
#define sghMiniDumpThread_g (*reinterpret_cast<HANDLE *>(OC_VA(0x6D44F0u)))
#else
extern HANDLE sghMiniDumpThread_g;
#endif
#endif

// 0x6DC91C -- one byte, set when IsDebuggerPresent() is not TRUE.
#ifndef OC_SGBNODEBUGGER_G
#define OC_SGBNODEBUGGER_G
#ifdef OC_DLL_MODE
#define sgbNoDebugger_g (*reinterpret_cast<bool *>(OC_VA(0x6DC91Cu)))
#else
extern bool sgbNoDebugger_g;
#endif
#endif

// Functions this file calls, declared here; NDebugHelp.h is not included.

namespace NDebugHelp {
void Initialize();
DWORD WINAPI MiniDumpThreadProc(void *pvParam);
}


namespace crashy {
namespace internal {
size_t strlcat(char *pszDst, const char *pszSrc, size_t cbDst);
bool LaunchApplication(const char *pszApp, const char *pszCmdLine);
}
}

namespace crashy {

// TerminateProcess -- oracle 0x402AF0 (15 B).
void TerminateProcess(UINT uExitCode)
{
  ::TerminateProcess(::GetCurrentProcess(), uExitCode);
}

// GetStackAddresses -- oracle 0x402B00 (163 B). The stack extent around dwAddr,
// from the committed private region holding it and the allocation it belongs to.
bool GetStackAddresses(DWORD_PTR dwAddr, DWORD_PTR *pdwStackTop, DWORD_PTR *pdwStackBase)
{
  MEMORY_BASIC_INFORMATION mbi;

  if (!VirtualQuery(reinterpret_cast<void *>(dwAddr), &mbi, sizeof(mbi)))
    return false;
  if (mbi.State != MEM_COMMIT)
    return false;
  if (mbi.Type != MEM_PRIVATE)
    return false;
  if (mbi.Protect != PAGE_READWRITE)
    return false;

  *pdwStackTop = reinterpret_cast<DWORD_PTR>(mbi.BaseAddress) + mbi.RegionSize;
  if (*pdwStackTop <= dwAddr)
    return false;

  *pdwStackBase = reinterpret_cast<DWORD_PTR>(mbi.AllocationBase);
  if (!VirtualQuery(mbi.AllocationBase, &mbi, sizeof(mbi)))
    return false;
  if (mbi.Type != MEM_PRIVATE)
    return false;
  if (reinterpret_cast<DWORD_PTR>(mbi.BaseAddress) != *pdwStackBase)
    return false;

  DWORD_PTR dwEnd = mbi.RegionSize + *pdwStackBase;
  if (mbi.State == MEM_COMMIT) {
    if (dwEnd == *pdwStackTop) {
      *pdwStackBase = reinterpret_cast<DWORD_PTR>(mbi.AllocationBase);
      return true;
    }
  } else if (mbi.State == MEM_RESERVE) {
    if (dwEnd < *pdwStackTop) {
      *pdwStackBase = reinterpret_cast<DWORD_PTR>(mbi.AllocationBase);
      return true;
    }
  }

  return false;
}

// GetThreadStackInfo -- oracle 0x402BB0 (197 B). The stack top goes through
// pdwStackPtr's own slot, as 1.17 reuses that parameter's home, and the cleanup
// after a failed GetThreadContext is repeated rather than shared, as in 1.17.
bool GetThreadStackInfo(DWORD dwThreadId, DWORD_PTR *pcbStackSize,
                        DWORD_PTR *pdwStackTop, DWORD_PTR *pdwStackPtr)
{
  CONTEXT ctx;
  HANDLE hThread = NULL;

  ctx.ContextFlags = CONTEXT_CONTROL;

  if (GetCurrentThreadId() == dwThreadId) {
    RtlCaptureContext(&ctx);
  } else {
    hThread = OpenThread(0x4A, FALSE, dwThreadId);
    if (hThread == NULL)
      return false;
    if (SuspendThread(hThread) == (DWORD)-1)
      return false;
    if (!GetThreadContext(hThread, &ctx)) {
      ResumeThread(hThread);
      CloseHandle(hThread);
      return false;
    }
  }

#if defined(_M_IX86)
  DWORD_PTR dwEsp = ctx.Esp;
#else
  DWORD_PTR dwEsp = ctx.Rsp;
#endif
  *pdwStackPtr = dwEsp;

  DWORD_PTR dwStackBase;
  bool bOk = GetStackAddresses(dwEsp, reinterpret_cast<DWORD_PTR *>(&pdwStackPtr),
                               &dwStackBase);
  if (bOk) {
    *pdwStackTop = reinterpret_cast<DWORD_PTR>(pdwStackPtr);
    *pcbStackSize = reinterpret_cast<DWORD_PTR>(pdwStackPtr) - dwStackBase;
  }

  if (hThread != NULL) {
    ResumeThread(hThread);
    CloseHandle(hThread);
  }

  return bOk;
}

// GetCurrentThreadId -- oracle 0x402C80 (6 B). A tail jump to the import.
DWORD GetCurrentThreadId()
{
  return ::GetCurrentThreadId();
}

// Initialize -- oracle 0x402C90 (5 B).
void Initialize()
{
  NDebugHelp::Initialize();
}

}  // namespace crashy

// PushArg -- oracle 0x402CA0 (75 B). strlcpy inlined with its size folded to
// 0x400; on truncation the first byte is cleared through a select, not a branch.
static __forceinline size_t strlcpy_inl(char *pszDst, const char *pszSrc,
                                       size_t cbDst)
{
  char *d = pszDst;
  const char *s = pszSrc;
  size_t n = cbDst;

  if (n != 0) {
    while (--n != 0) {
      if ((*d++ = *s++) == '\0')
        break;
    }
  }

  if (n == 0) {
    if (cbDst != 0)
      *d = '\0';
    while (*s++)
      ;
  }

  return s - pszSrc - 1;
}

void PushArg(const char *pszArg)
{
  size_t cch = strlcpy_inl(sgszCrashArgs_g, pszArg, sizeof(sgszCrashArgs_g));

  sgszCrashArgs_g[0] = cch < sizeof(sgszCrashArgs_g) ? sgszCrashArgs_g[0] : '\0';
}

namespace crashy {

// SubmitReport -- oracle 0x402CF0 (692 B). The path copy is PushArg's inlined
// strlcpy, and the separator test reads szPath[cch - 1] unguarded: for an empty
// argument string 1.17 reads one byte before the buffer.
#ifndef OC_PAYLOAD
bool SubmitReport(const char *pszReportPath, const ReportInfo *pInfo)
{
  char szPath[0x400];

  size_t cch = strlcpy_inl(szPath, sgszCrashArgs_g, sizeof(szPath));
  if (cch < sizeof(szPath)) {
    if (szPath[cch - 1] != '/' && szPath[cch - 1] != '\\')
      internal::strlcat(szPath, "\\", sizeof(szPath));
    internal::strlcat(szPath, "BlizzardError.exe", sizeof(szPath));

    wchar_t wszPath[0x104];
    if (!MultiByteToWideChar(CP_UTF8, 0, szPath, -1, wszPath, 0x104))
      return false;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(wszPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
      return false;
    FindClose(hFind);

    char szArgs[0x1000] = "";

    if (pInfo->dwFlags & 1)
      internal::strlcat(szArgs, " -auto", sizeof(szArgs));
    if (pInfo->dwFlags & 2)
      internal::strlcat(szArgs, " -nocancel", sizeof(szArgs));
    if (pInfo->dwFlags & 4)
      internal::strlcat(szArgs, " -simple", sizeof(szArgs));

    if (pInfo->pszFrom != NULL) {
      internal::strlcat(szArgs, " -from ", sizeof(szArgs));
      internal::strlcat(szArgs, pInfo->pszFrom, sizeof(szArgs));
    }
    if (pInfo->pszLocale != NULL) {
      internal::strlcat(szArgs, " -locale ", sizeof(szArgs));
      internal::strlcat(szArgs, pInfo->pszLocale, sizeof(szArgs));
    }
    if (pInfo->pszProduct != NULL) {
      internal::strlcat(szArgs, " -product \"", sizeof(szArgs));
      internal::strlcat(szArgs, pInfo->pszProduct, sizeof(szArgs));
      internal::strlcat(szArgs, "\"", sizeof(szArgs));
    }

    for (DWORD i = 0; i < pInfo->cAttachments; ++i) {
      if (pInfo->rgpszAttachments[i] != NULL && pInfo->rgpszAttachments[i][0] != '\0') {
        internal::strlcat(szArgs, " -attachment \"", sizeof(szArgs));
        internal::strlcat(szArgs, pInfo->rgpszAttachments[i], sizeof(szArgs));
        internal::strlcat(szArgs, "\"", sizeof(szArgs));
      }
    }

    internal::strlcat(szArgs, " \"", sizeof(szArgs));
    internal::strlcat(szArgs, pszReportPath, sizeof(szArgs));
    internal::strlcat(szArgs, "\"", sizeof(szArgs));

    return internal::LaunchApplication(szPath, szArgs);
  }

  return false;
}
#endif  // OC_PAYLOAD

}  // namespace crashy

namespace NDebugHelp {

// Initialize -- oracle 0x4049E0 (110 B). The debugger flag is `!= TRUE`, as 1.17
// tests it.
void Initialize()
{
  InitializeCriticalSection(&sgDbgHelpLock_g);
  InitializeCriticalSection(&sgMiniDumpLock_g);

  sghMiniDumpRequest_g = CreateEventW(NULL, FALSE, FALSE, NULL);
  sghMiniDumpDone_g = CreateEventW(NULL, FALSE, FALSE, NULL);

  sghMiniDumpThread_g = CreateThread(NULL, 0x40000, MiniDumpThreadProc, NULL,
                                     STACK_SIZE_PARAM_IS_A_RESERVATION,
                                     &sgdwMiniDumpThreadId_g);

  sgbNoDebugger_g = IsDebuggerPresent() != TRUE;
}

}  // namespace NDebugHelp

namespace crashy {
namespace internal {

// internal::strlcat -- oracle 0x404A50 (130 B).
size_t strlcat(char *pszDst, const char *pszSrc, size_t cbDst)
{
  char *d = pszDst;
  const char *s = pszSrc;
  size_t n = cbDst;
  size_t dlen;

  while (*d != '\0' && n-- != 0)
    ++d;

  dlen = d - pszDst;
  n = cbDst - dlen;

  if (n == 0)
    return dlen + strlen(s);

  while (*s != '\0') {
    if (n != 1) {
      *d++ = *s;
      --n;
    }
    ++s;
  }
  *d = '\0';

  return dlen + (s - pszSrc);
}

// internal::LaunchApplication -- oracle 0x404B10 (214 B). Both names are
// optional.
bool LaunchApplication(const char *pszApp, const char *pszCmdLine)
{
  wchar_t wszCmdLine[0x8000];
  wchar_t wszApp[0x400];

  if ((pszCmdLine == NULL ||
       MultiByteToWideChar(CP_UTF8, 0, pszCmdLine, -1, wszCmdLine, 0x8000)) &&
      (pszApp == NULL ||
       MultiByteToWideChar(CP_UTF8, 0, pszApp, -1, wszApp, 0x400))) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    if (CreateProcessW(pszApp != NULL ? wszApp : NULL,
                       pszCmdLine != NULL ? wszCmdLine : NULL,
                       NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      return true;
    }
  }

  return false;
}

}  // namespace internal
}  // namespace crashy
