// Hyperion: not built. The game's crash reporter is gone from the port and the
// port's own (port_crash.cpp) stays; the file is kept so that later changes to
// the reconstruction still apply. An address of its functions in 1.17's data
// goes to PortOriginalCodeReached.
//
// Logging.cpp -- the crash log's module roster, stack trace and register dump.
// Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/Logging.h"
#include "../h/EnumModuleData.h"
#include "../h/Module.h"        // MODULE_DEBUG_INFO
#include "../h/crashy.h"     // _snprintf_s


#include <string.h>

// The CRT entries this TU calls.
extern "C" {
int __cdecl _snprintf_s(char *pszBuf, size_t cchBuf, size_t nCount, const char *pszFormat, ...);
}

// s_mainThread (0x6D44F4) holds the main thread's id, which LogStackFrames
// prints as "Main Thread". modules (0x6DC980) is the HMODULE[0x400]
// EnumModuleDebugInfo_PSAPI hands to EnumProcessModules.

#ifndef OC_s_mainThread
#define OC_s_mainThread
#ifdef OC_DLL_MODE
#define s_mainThread (*reinterpret_cast<DWORD *>(OC_VA(0x6D44F4u)))
#else
extern DWORD s_mainThread;
#endif
#endif

#ifndef OC_modules
#define OC_modules
#ifdef OC_DLL_MODE
#define modules (*reinterpret_cast<HMODULE (*)[0x400]>(OC_VA(0x6DC980u)))
#else
extern HMODULE modules[0x400];
#endif
#endif

// NDebugHelp's three lookups, declared here exactly as NDebugHelp.h declares
// them.
namespace NDebugHelp {
bool GetAddressModule(DWORD dwAddress, char *pszModule, int nUnused, DWORD *pdwBase);
bool GetAddressSymbol(DWORD dwAddress, char *pszSymbol, int nUnused, DWORD *pdwDisplacement);
bool GetAddressFileAndLine(DWORD dwAddress, char *pszFile, int nUnused, DWORD *pdwLine);
}

// The basename EnumModuleDebugInfo_TH32 and GetSymbol both inline: the last of
// the three separators wins, compared unsigned.
static __forceinline const char *sBaseName(const char *psz)
{
  const char *pLast = nullptr;
  const char *p = strrchr(psz, '/');

  // All three tests are spelled alike, though for the first pLast is known
  // null; 1.17 keeps both the branch and the compare.
  if (p != nullptr && p > pLast)
    pLast = p;

  p = strrchr(psz, '\\');
  if (p != nullptr && p > pLast)
    pLast = p;

  p = strrchr(psz, ':');
  if (p != nullptr && p > pLast)
    pLast = p;

  return pLast != nullptr ? pLast + 1 : psz;
}

// sModuleCompareProc -- oracle 0x402FB0 (28 B). The qsort ordering over the
// roster, by image base.
int __cdecl sModuleCompareProc(const void *pv1, const void *pv2)
{
  DWORD dwBase1 = *static_cast<const DWORD *>(pv1);
  DWORD dwBase2 = *static_cast<const DWORD *>(pv2);

  if (dwBase1 > dwBase2)
    return 1;

  return dwBase1 < dwBase2 ? -1 : 0;
}

// CollectModulesCallback -- oracle 0x402FD0 (45 B). Appends one enumerated
// module and says whether there is room for another. noinline keeps it a call
// in EnumModuleDebugInfo_TH32, as in 1.17.
__declspec(noinline) bool CollectModulesCallback(const MODULE_DEBUG_INFO *pEntry, EnumModuleData *pData)
{
  if (pData == nullptr)
    return false;

  pData->aItems[pData->nCount] = *pEntry;
  pData->nCount++;

  return pData->nCount < 0x100;
}

// FormatModuleDebugInfo -- oracle 0x403000 (226 B). One DBG-MODULE line. The
// third parameter is never read: the destination length is the literal 0x400.
void FormatModuleDebugInfo(const MODULE_DEBUG_INFO *pEntry, char *pszBuf, size_t cchBuf)
{
  char szGuid[0x28];
  char szExtra[0x10];

  szExtra[0] = '\0';
  pszBuf[0] = '\0';

  if (pEntry->dwBuildStamp != 0)
    _snprintf_s(szExtra, sizeof(szExtra), -1, " %8x", pEntry->dwBuildStamp);

  _snprintf_s(szGuid, sizeof(szGuid), -1,
              "{%08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x}",
              pEntry->guidSignature.Data1, pEntry->guidSignature.Data2, pEntry->guidSignature.Data3,
              pEntry->guidSignature.Data4[0], pEntry->guidSignature.Data4[1], pEntry->guidSignature.Data4[2],
              pEntry->guidSignature.Data4[3], pEntry->guidSignature.Data4[4], pEntry->guidSignature.Data4[5],
              pEntry->guidSignature.Data4[6], pEntry->guidSignature.Data4[7]);

  _snprintf_s(pszBuf, 0x400, -1, "DBG-MODULE<%p %08x \"%s\" \"%s\" %u %s %u %u%s>",
              pEntry->pImageBase, pEntry->dwSizeOfImage, pEntry->szModuleName,
              pEntry->szPdbPath, pEntry->dwSignature, szGuid, pEntry->dwAge,
              pEntry->dwTimeDateStamp, szExtra);
}

#ifndef OC_PAYLOAD
// FormatFunctionInfo -- oracle 0x4030F0 (340 B). One DBG-ADDR / symbol line. An
// empty szSymbol takes the short form and returns early; otherwise the line is
// built left to right, each call writing at pszBuf + cch and told the room
// left. The two trailing parameters are never read.
void FormatFunctionInfo(const crashy::StackFrameInfo *pFrame,
                        const crashy::SymbolInfo *pSymbol,
                        char *pszBuf, size_t cbUnused1, size_t cbUnused2)
{
  char szParams[0x50];

  szParams[0] = '\0';
  pszBuf[0] = '\0';

  if (pFrame->bHasParams != 0)
    _snprintf_s(szParams, 0x50, -1, " (%p,%p,%p,%p)",
                pFrame->adwParams[0], pFrame->adwParams[1],
                pFrame->adwParams[2], pFrame->adwParams[3]);

  if (pSymbol->szSymbol[0] == '\0') {
    _snprintf_s(pszBuf, 0x400, -1, "DBG-ADDR<%p>(\"%s\")%s", pFrame->pAddress,
                pSymbol->szModule, szParams);
    return;
  }

  int cch = _snprintf_s(pszBuf, 0x400, -1, "%p %s", pFrame->pAddress,
                        pSymbol->szModule);

  int nPad = 0x15 - cch;
  if (nPad > 0)
    cch += _snprintf_s(pszBuf + cch, 0x400 - cch, -1, "%*s", nPad, " ");

  if (pSymbol->szSymbol[0] != '\0' || pSymbol->dwDisplacement > 0)
    cch += _snprintf_s(pszBuf + cch, 0x400 - cch, -1, " %s+%llu",
                       pSymbol->szSymbol,
                       static_cast<unsigned __int64>(pSymbol->dwDisplacement));

  cch += _snprintf_s(pszBuf + cch, 0x400 - cch, -1, "%s", szParams);

  if (pSymbol->szFile[0] != '\0' || pSymbol->dwLine > 0)
    _snprintf_s(pszBuf + cch, 0x400 - cch, -1, " (%s,%u)", pSymbol->szFile,
                pSymbol->dwLine);
}
#endif  // OC_PAYLOAD

namespace crashy {

#ifndef OC_PAYLOAD
// LogModules -- oracle 0x403250 (227 B). One DBG-MODULE line per loaded module,
// sorted by image base; the three enumerators are tried in turn. cbUnused is
// never set: FormatModuleDebugInfo does not read its third argument.
void LogModules(LOGPROC pfnLog, void *pContext)
{
  EnumModuleData data;
  int n1, n2, n3;

  data.nCount = 0;

  if (!EnumModuleDebugInfo_PSAPI(n1, &data))
    if (!EnumModuleDebugInfo_TH32(n2, &data))
      EnumModuleDebugInfo_Undocumented(n3, &data);

  qsort(data.aItems, data.nCount, sizeof(data.aItems[0]), sModuleCompareProc);

  for (DWORD i = 0; i < data.nCount; i++) {
    char szLine[0x400];
    size_t cbUnused;

    FormatModuleDebugInfo(&data.aItems[i], szLine, cbUnused);

    size_t cch = strlen(szLine);
    if (cch >= sizeof(szLine) - 1) {
      szLine[sizeof(szLine) - 2] = '\n';
      szLine[sizeof(szLine) - 1] = '\0';
    } else {
      szLine[cch] = '\n';
      szLine[cch + 1] = '\0';
    }

    pfnLog(pContext, szLine);
  }
}
#endif  // OC_PAYLOAD

}  // namespace crashy

#ifndef OC_PAYLOAD
// EnumModuleData::EnumModuleData -- oracle 0x403340 (118 B). The body is empty:
// each element's constructor (Module.h) does the clearing, and nCount is left
// alone, as in 1.17.
inline EnumModuleData::EnumModuleData()
{
}
#endif  // OC_PAYLOAD

namespace crashy {

#ifndef OC_PAYLOAD
// LogStackTrace -- oracle 0x4033C0 (142 B). Walks one thread's stack into a
// fixed 128-frame buffer and logs it. EDX is forwarded to StackTrace and never
// read here; StackTrace does not read its fourth argument either.
void LogStackTrace(DWORD dwThreadId, void *pvUnused, LOGPROC pfnLog, void *pContext,
                   bool bWithSymbols, bool bWithHeader, int nSkip)
{
  StackFrameInfo frames[128];

  int nFrames = StackTrace(dwThreadId, pvUnused, frames, dwThreadId, nSkip);
  if (nFrames > 0) {
    if (nFrames > 128)
      nFrames = 128;

    LogStackFrames(dwThreadId, frames, nFrames, pfnLog, pContext, bWithSymbols,
                   bWithHeader);
  }
}

// LogStackFrames -- oracle 0x403450 (437 B). The header line, built left to
// right in one 0x400 buffer, then one line per frame with LogModules' clamp and
// newline tail.
void LogStackFrames(DWORD dwThreadId, const StackFrameInfo *pFrames, int nFrames,
                    LOGPROC pfnLog, void *pContext, bool bWithSymbols, bool bWithHeader)
{
  char szLine[0x400];
  SymbolInfo sym;

  if (bWithHeader) {
    DWORD_PTR dwEsp;
    DWORD_PTR dwBase;
    DWORD_PTR dwUsed;
    char szName[0x80];

    if (s_mainThread == dwThreadId)
      strncpy_s(szName, sizeof(szName), "Main Thread", -1);
    else
      szName[0] = 0;

    bool bStack = GetThreadStackInfo(dwThreadId, &dwUsed, &dwBase, &dwEsp);

    int cch = _snprintf_s(szLine, 0x400, -1, "Thread 0x%08x", dwThreadId);

    if (szName[0] != 0)
      cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, " \"%s\"", szName);

    if (bStack)
      cch += _snprintf_s(szLine + cch, 0x400 - cch, -1,
                         " [Stack Base: 0x%p Used Space: %llu/%llu bytes]", dwBase,
                         static_cast<unsigned __int64>(dwBase - dwEsp),
                         static_cast<unsigned __int64>(dwUsed));

    _snprintf_s(szLine + cch, 0x400 - cch, -1, "\n");
    pfnLog(pContext, szLine);
  }

  for (int i = nFrames; i != 0; i--) {
    GetSymbol(pFrames->pAddress, &sym);
    if (!bWithSymbols)
      sym.szSymbol[0] = 0;

    size_t cbUnused1;
    size_t cbUnused2;

    FormatFunctionInfo(pFrames, &sym, szLine, cbUnused1, cbUnused2);

    size_t cch = strlen(szLine);
    if (cch >= sizeof(szLine) - 1) {
      szLine[sizeof(szLine) - 2] = '\n';
      szLine[sizeof(szLine) - 1] = '\0';
    } else {
      szLine[cch] = '\n';
      szLine[cch + 1] = '\0';
    }

    pfnLog(pContext, szLine);
    pFrames++;
  }
}

#endif  // OC_PAYLOAD

// LogThreadRegisters -- oracle 0x403610 (1776 B). One line per register group,
// into one 0x400 buffer. The XMM group reads FloatSave.RegisterArea past its
// end, as 1.17 does.
void LogThreadRegisters(DWORD dwThreadId, LOGPROC pfnLog, void *pContext)
{
  char szLine[0x400];
  CONTEXT ctx;
  bool bOk;
  int cch;

#if defined(_M_IX86)
  ctx.ContextFlags = 0x1003F;
#else
  ctx.ContextFlags = CONTEXT_ALL;
#endif

  if (::GetCurrentThreadId() == dwThreadId) {
    RtlCaptureContext(&ctx);
    bOk = true;
  } else {
    void *hThread = OpenThread(0x4A, FALSE, dwThreadId);
    if (hThread == nullptr)
      return;

    if (SuspendThread(hThread) == static_cast<DWORD>(-1)) {
      CloseHandle(hThread);
      return;
    }

    BOOL bGot = GetThreadContext(hThread, &ctx);
    ResumeThread(hThread);
    CloseHandle(hThread);
    bOk = bGot != 0;
  }

  if (!bOk)
    return;

#if defined(_M_IX86)
  if ((ctx.ContextFlags & 0x10002) != 0) {
    cch  = _snprintf_s(szLine, 0x400, -1, "%6s:0x%08x", "EAX", ctx.Eax);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "EBX", ctx.Ebx);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "ECX", ctx.Ecx);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "EDX", ctx.Edx);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "\n");
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "ESI", ctx.Esi);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "EDI", ctx.Edi);
    _snprintf_s(szLine + cch, 0x400 - cch, -1, "\n\n");
    pfnLog(pContext, szLine);
  }

  if ((ctx.ContextFlags & 0x10001) != 0) {
    cch  = _snprintf_s(szLine, 0x400, -1, "%6s:0x%08x", "EBP", ctx.Ebp);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "ESP", ctx.Esp);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "EIP", ctx.Eip);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "FLG", ctx.EFlags);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "\n");
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "CS", ctx.SegCs);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "SS", ctx.SegSs);
    _snprintf_s(szLine + cch, 0x400 - cch, -1, "\n\n");
    pfnLog(pContext, szLine);
  }

  if ((ctx.ContextFlags & 0x10004) != 0) {
    cch  = _snprintf_s(szLine, 0x400, -1, "%6s:0x%08x", "DS", ctx.SegDs);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "ES", ctx.SegEs);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "FS", ctx.SegFs);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "GS", ctx.SegGs);
    _snprintf_s(szLine + cch, 0x400 - cch, -1, "\n\n");
    pfnLog(pContext, szLine);
  }

  if ((ctx.ContextFlags & 0x10008) != 0) {
    cch  = _snprintf_s(szLine, 0x400, -1, "%6s:0x%08x", "CTRL", ctx.FloatSave.ControlWord);
    cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "STAT",
                       ctx.FloatSave.StatusWord);
    _snprintf_s(szLine + cch, 0x400 - cch, -1, "\n");
    pfnLog(pContext, szLine);

    for (DWORD i = 0; i < 8; ) {
      DWORD dwTop = 0;
      DWORD dwLow;
      DWORD dwHigh;

      dwLow = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 10]);
      dwHigh = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 10 + 4]);
      *reinterpret_cast<WORD *>(&dwTop) = *reinterpret_cast<const WORD *>(&ctx.FloatSave.RegisterArea[i * 10 + 8]);
      cch = _snprintf_s(szLine, 0x400, -1, "%5s%d:0x%08x 0x%08x 0x%08x", "ST", i,
                        dwLow, dwHigh, dwTop);
      i++;

      dwLow = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 10]);
      dwHigh = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 10 + 4]);
      *reinterpret_cast<WORD *>(&dwTop) = *reinterpret_cast<const WORD *>(&ctx.FloatSave.RegisterArea[i * 10 + 8]);
      cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%5s%d:0x%08x 0x%08x 0x%08x",
                         "ST", i, dwLow, dwHigh, dwTop);
      _snprintf_s(szLine + cch, 0x400 - cch, -1, "\n");
      pfnLog(pContext, szLine);

      i++;
    }

    _snprintf_s(szLine, 0x400, -1, "\n");
    pfnLog(pContext, szLine);
  }

  if ((ctx.ContextFlags & 0x10020) != 0) {
    for (DWORD i = 0; i < 8; ) {
      DWORD d0, d1, d2, d3;

      d0 = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 16]);
      d1 = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 16 + 4]);
      d2 = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 16 + 8]);
      d3 = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 16 + 12]);
      cch = _snprintf_s(szLine, 0x400, -1, "%5s%d:0x%08x 0x%08x 0x%08x 0x%08x", "XMM", i,
                        d0, d1, d2, d3);
      i++;

      d0 = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 16]);
      d1 = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 16 + 4]);
      d2 = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 16 + 8]);
      d3 = *reinterpret_cast<const DWORD *>(&ctx.FloatSave.RegisterArea[i * 16 + 12]);
      cch += _snprintf_s(szLine + cch, 0x400 - cch, -1,
                         "%5s%d:0x%08x 0x%08x 0x%08x 0x%08x", "XMM", i,
                         d0, d1, d2, d3);
      _snprintf_s(szLine + cch, 0x400 - cch, -1, "\n");
      pfnLog(pContext, szLine);

      i++;
    }

    _snprintf_s(szLine, 0x400, -1, "\n");
    pfnLog(pContext, szLine);
  }
#else
  // x64's registers are 64 bits wide and its x87 and XMM state is in FltSave;
  // 1.17 has no such arm, and this one prints the same groups in the same shape.
  const struct {
    const char *pszName;
    DWORD64 qwValue;
  } regs[] = {
    {"RAX", ctx.Rax}, {"RBX", ctx.Rbx}, {"RCX", ctx.Rcx}, {"RDX", ctx.Rdx},
    {"RSI", ctx.Rsi}, {"RDI", ctx.Rdi}, {"R8", ctx.R8},   {"R9", ctx.R9},
    {"R10", ctx.R10}, {"R11", ctx.R11}, {"R12", ctx.R12}, {"R13", ctx.R13},
    {"R14", ctx.R14}, {"R15", ctx.R15}, {"RBP", ctx.Rbp}, {"RSP", ctx.Rsp},
    {"RIP", ctx.Rip}, {"FLG", ctx.EFlags},
  };
  const int nRegs = sizeof(regs) / sizeof(regs[0]);

  for (int i = 0; i < nRegs; i += 4) {
    cch = 0;
    for (int j = i; j < i + 4 && j < nRegs; j++)
      cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%016llx",
                         regs[j].pszName, regs[j].qwValue);
    _snprintf_s(szLine + cch, 0x400 - cch, -1, i + 4 >= nRegs ? "\n\n" : "\n");
    pfnLog(pContext, szLine);
  }

  cch  = _snprintf_s(szLine, 0x400, -1, "%6s:0x%04x", "CS", ctx.SegCs);
  cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%04x", "SS", ctx.SegSs);
  cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%04x", "DS", ctx.SegDs);
  cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%04x", "ES", ctx.SegEs);
  cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%04x", "FS", ctx.SegFs);
  cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%04x", "GS", ctx.SegGs);
  _snprintf_s(szLine + cch, 0x400 - cch, -1, "\n\n");
  pfnLog(pContext, szLine);

  cch  = _snprintf_s(szLine, 0x400, -1, "%6s:0x%08x", "CTRL", ctx.FltSave.ControlWord);
  cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "STAT",
                     ctx.FltSave.StatusWord);
  cch += _snprintf_s(szLine + cch, 0x400 - cch, -1, "%6s:0x%08x", "MXCSR", ctx.MxCsr);
  _snprintf_s(szLine + cch, 0x400 - cch, -1, "\n");
  pfnLog(pContext, szLine);

  for (int i = 0; i < 16; i++) {
    const DWORD *pd = reinterpret_cast<const DWORD *>(&ctx.FltSave.XmmRegisters[i]);
    _snprintf_s(szLine, 0x400, -1, "%5s%d:0x%08x 0x%08x 0x%08x 0x%08x\n", "XMM", i,
                pd[0], pd[1], pd[2], pd[3]);
    pfnLog(pContext, szLine);
  }
  _snprintf_s(szLine, 0x400, -1, "\n");
  pfnLog(pContext, szLine);
#endif
}

// LogAllThreads -- oracle 0x403D00 (157 B). A stack trace for every thread of
// this process except the one the caller already logged. A failed snapshot
// (-1) returns before the loop, without closing it.
void LogAllThreads(LOGPROC pfnLog, void *pContext, DWORD dwSkipThreadId)
{
  THREADENTRY32 te = {};

  te.dwSize = sizeof(te);

  void *hSnapshot = CreateToolhelp32Snapshot(4, 0);
  if (hSnapshot == reinterpret_cast<void *>(-1))
    return;

  DWORD dwProcessId = GetCurrentProcessId();

  if (Thread32First(hSnapshot, &te)) {
    do {
      if (te.th32OwnerProcessID == dwProcessId && te.th32ThreadID != dwSkipThreadId) {
        LogStackTrace(te.th32ThreadID, nullptr, pfnLog, pContext, true, true, 0);
        pfnLog(pContext, "\n");
      }
    } while (Thread32Next(hSnapshot, &te));
  }

  CloseHandle(hSnapshot);
}

}  // namespace crashy

#ifndef OC_PAYLOAD
// EnumModuleDebugInfo_PSAPI -- oracle 0x405170 (295 B). The psapi route, bound
// at run time so the DLL need not be present. A module whose
// GetModuleInformation fails is skipped, a refused callback stops the walk and
// still reports success, and a failed LoadLibraryW returns at once.
bool EnumModuleDebugInfo_PSAPI(int nUnused, EnumModuleData *pData)
{
  // Never set: ExecuteEnumCallback does not read its first argument.
  int nDead;
  PSAPI_MODULEINFO mi;
  wchar_t wszName[260];
  DWORD cbNeeded;
  void *hProcess = GetCurrentProcess();
  HMODULE hPsapi = LoadLibraryW(L"psapi.dll");

  if (hPsapi != nullptr) {
    PFN_ENUMPROCESSMODULES pfnEnum = reinterpret_cast<PFN_ENUMPROCESSMODULES>(
        GetProcAddress(hPsapi, "EnumProcessModules"));
    PFN_GETMODULEINFORMATION pfnInfo = reinterpret_cast<PFN_GETMODULEINFORMATION>(
        GetProcAddress(hPsapi, "GetModuleInformation"));
    PFN_GETMODULEBASENAMEW pfnName = reinterpret_cast<PFN_GETMODULEBASENAMEW>(
        GetProcAddress(hPsapi, "GetModuleBaseNameW"));

    if (pfnEnum != nullptr && pfnInfo != nullptr && pfnName != nullptr) {
      if (pfnEnum(hProcess, modules, sizeof(modules), &cbNeeded)) {
        DWORD nModules = cbNeeded / sizeof(HMODULE);

        for (DWORD i = 0; i < nModules; i++) {
          if (pfnInfo(hProcess, modules[i], &mi, sizeof(mi))) {
            if (pfnName(hProcess, modules[i], wszName, 260) == 0)
              wszName[0] = 0;

            if (!ExecuteEnumCallback(nDead, pData, static_cast<HMODULE>(mi.lpBaseOfDll),
                                     wszName, -1))
              break;
          }
        }

        FreeLibrary(hPsapi);
        return true;
      }
    }
    // Written twice on purpose, for 1.17's register assignment; the two copies
    // merge.
    if (pfnEnum) FreeLibrary(hPsapi); else FreeLibrary(hPsapi);
  }
  return false;
}
#endif  // OC_PAYLOAD

#ifndef OC_PAYLOAD
// EnumModuleDebugInfo_TH32 -- oracle 0x4052A0 (430 B). The toolhelp route.
// WideCharToMultiByte's result is a byte count used as a character index, 1.17's
// bug, and the basename is copied back over its own buffer as a fixed 0x100 bytes.
bool EnumModuleDebugInfo_TH32(int nUnused, EnumModuleData *pData)
{
  void *hSnapshot = CreateToolhelp32Snapshot(8, 0);

  if (hSnapshot == reinterpret_cast<void *>(-1))
    return false;

  MODULEENTRY32W me;

  me.dwSize = sizeof(me);
  if (Module32FirstW(hSnapshot, &me)) {
    do {
      BYTE *pImageBase = me.modBaseAddr;
      MODULE_DEBUG_INFO info;

      int n = WideCharToMultiByte(65001, 0, me.szModule, -1, info.szModuleName,
                                  sizeof(info.szModuleName), nullptr, nullptr);
      if (n < 0)
        info.szModuleName[0] = 0;
      else if ((unsigned)n > 0xFF)
        info.szModuleName[0xFF] = 0;
      else
        info.szModuleName[n] = 0;

      memcpy(info.szModuleName, sBaseName(info.szModuleName), sizeof(info.szModuleName));

      if (GetModuleDebugInfo(pImageBase, &info) && !CollectModulesCallback(&info, pData))
        break;

      me.dwSize = sizeof(me);
    } while (Module32NextW(hSnapshot, &me));
  }

  CloseHandle(hSnapshot);
  return true;
}
#endif  // OC_PAYLOAD

#ifndef OC_PAYLOAD
// EnumModuleDebugInfo_Undocumented -- oracle 0x405450 (163 B). The loader-list
// route, guarded by SEH because nothing it reads is documented.
// FullDllName.Length is a byte count and reaches ExecuteEnumCallback as a
// character count, as in 1.17.
bool EnumModuleDebugInfo_Undocumented(int nUnused, EnumModuleData *pData)
{
  int nDead;

  if (GetModuleHandleW(L"ntdll.dll") == nullptr)
    return false;

  __try {
    const LDR_TEB_MIN *pTeb = reinterpret_cast<const LDR_TEB_MIN *>(NtCurrentTeb());
    const LIST_ENTRY *pHead = &pTeb->ProcessEnvironmentBlock->Ldr->InMemoryOrderModuleList;

    for (const LIST_ENTRY *p = pHead->Flink; p != pHead; p = p->Flink) {
      const LDR_MODULE_ENTRY *pEntry = reinterpret_cast<const LDR_MODULE_ENTRY *>(
          reinterpret_cast<const BYTE *>(p) - offsetof(LDR_MODULE_ENTRY, InMemoryOrderLinks));

      if (!ExecuteEnumCallback(nDead, pData, static_cast<HMODULE>(pEntry->DllBase),
                               pEntry->FullDllName.Buffer, pEntry->FullDllName.Length))
        break;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }

  return true;
}
#endif  // OC_PAYLOAD

namespace crashy {

// 0x4055E0 (421 B). Walks one thread's EBP chain into pFrames, skipping the
// first nSkip frames, and returns how many it stored. The frame count is
// unsigned, as 1.17 compares it. The early SuspendThread failure returns without
// closing the handle, as in 1.17.
int StackTrace(DWORD dwThreadId, void *pvContext, StackFrameInfo *pFrames,
               DWORD dwUnused, int nSkip)
{
  CONTEXT ctx;
  HANDLE hThread = NULL;

  if (pvContext != NULL) {
    ctx = *static_cast<const CONTEXT *>(pvContext);
  } else {
    DWORD dwCurrent = ::GetCurrentThreadId();

#if defined(_M_IX86)
    ctx.ContextFlags = CONTEXT_CONTROL;
#else
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
#endif
    if (dwCurrent == dwThreadId) {
      RtlCaptureContext(&ctx);
    } else {
      hThread = OpenThread(0x4A, FALSE, dwThreadId);
      if (hThread == NULL)
        return 0;

      if (SuspendThread(hThread) == (DWORD)-1)
        return 0;

      if (!GetThreadContext(hThread, &ctx)) {
        ResumeThread(hThread);
        CloseHandle(hThread);
        return 0;
      }
    }
  }

  DWORD nFrames = 0;
#if defined(_M_IX86)
  DWORD dwEip = ctx.Eip;
  DWORD *pFrame = reinterpret_cast<DWORD *>(ctx.Ebp);
  DWORD dwEsp = ctx.Esp;
#endif
  DWORD_PTR dwUsed;
  DWORD_PTR dwBase;
  DWORD_PTR dwTop;

  if (!GetThreadStackInfo(dwThreadId, &dwUsed, &dwBase, &dwTop)) {
    if (hThread != NULL) {
      ResumeThread(hThread);
      CloseHandle(hThread);
    }
    return 0;
  }

  __try {
#if defined(_M_IX86)
    for (DWORD i = 0; i < 0x80; i++) {
      if (i >= (DWORD)nSkip) {
        pFrames[nFrames].pAddress = reinterpret_cast<void *>(dwEip);
        pFrames[nFrames].dwStack = dwEsp;
        pFrames[nFrames].dwFrame = reinterpret_cast<DWORD>(pFrame);
        nFrames++;
        if (nFrames >= 0x80)
          break;
      }

      if (reinterpret_cast<DWORD>(pFrame) <= dwEsp)
        break;
      if (reinterpret_cast<DWORD>(pFrame + 2) > dwBase)
        break;

      DWORD *pNext = reinterpret_cast<DWORD *>(pFrame[0]);
      if ((reinterpret_cast<DWORD>(pNext) & 3) != 0)
        break;
      if (pNext <= pFrame)
        break;

      dwEsp = reinterpret_cast<DWORD>(&pFrame[1]);
      dwEip = pFrame[1];
      pFrame = pNext;
    }
#else
    // x64 keeps no frame-pointer chain, so the walk goes through the unwind
    // tables; a function with no entry is a leaf, its return address on top.
    for (DWORD i = 0; i < 0x80 && ctx.Rip != 0; i++) {
      if (i >= (DWORD)nSkip) {
        pFrames[nFrames].pAddress = reinterpret_cast<void *>(ctx.Rip);
        pFrames[nFrames].dwStack = ctx.Rsp;
        pFrames[nFrames].dwFrame = ctx.Rbp;
        nFrames++;
        if (nFrames >= 0x80)
          break;
      }

      DWORD64 qwImageBase;
      PRUNTIME_FUNCTION pFunction = RtlLookupFunctionEntry(ctx.Rip, &qwImageBase, NULL);
      if (pFunction == NULL) {
        ctx.Rip = *reinterpret_cast<const DWORD64 *>(ctx.Rsp);
        ctx.Rsp += 8;
      } else {
        void *pHandlerData;
        DWORD64 qwEstablisher;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, qwImageBase, ctx.Rip, pFunction, &ctx,
                         &pHandlerData, &qwEstablisher, NULL);
      }
      if (ctx.Rsp >= dwBase)
        break;
    }
#endif
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }

  if (hThread != NULL) {
    ResumeThread(hThread);
    CloseHandle(hThread);
  }
  return nFrames;
}

#ifndef OC_PAYLOAD
// GetSymbol -- oracle 0x405790 (363 B). One address into module, symbol, file
// and line. The address reaches each NDebugHelp entry twice, in ECX and in the
// stack slot none of them reads, as 1.17 passes it.
bool GetSymbol(const void *pAddress, SymbolInfo *pSymbol)
{
  char szPath[0x100];
  DWORD dwBase = 0;
  DWORD dwAddress = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(pAddress));

  pSymbol->szModule[0] = 0;
  pSymbol->szSymbol[0] = 0;
  pSymbol->dwDisplacement = 0;
  pSymbol->szFile[0] = 0;
  pSymbol->dwLine = 0;

  if (NDebugHelp::GetAddressModule(dwAddress, szPath, dwAddress, &dwBase))
    strncpy_s(pSymbol->szModule, sizeof(pSymbol->szModule), sBaseName(szPath), -1);
  else
    pSymbol->szModule[0] = 0;

  NDebugHelp::GetAddressSymbol(dwAddress, pSymbol->szSymbol, dwAddress,
                               &pSymbol->dwDisplacement);
  NDebugHelp::GetAddressFileAndLine(dwAddress, szPath, dwAddress, &pSymbol->dwLine);
  strncpy_s(pSymbol->szFile, sizeof(pSymbol->szFile), sBaseName(szPath), -1);
  return true;
}
#endif  // OC_PAYLOAD

}  // namespace crashy
