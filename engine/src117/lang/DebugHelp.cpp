// Hyperion: not built. The game's crash reporter is gone from the port and the
// port's own (port_crash.cpp) stays; the file is kept so that later changes to
// the reconstruction still apply. An address of its functions in 1.17's data
// goes to PortOriginalCodeReached.
//
// DebugHelp.cpp -- the dbghelp.dll glue Blizzard's crash reporter runs on:
// loading the library and its exports, initialising the symbol handler, and the
// worker thread that writes a minidump. Bodies are in oracle VA order.
//
// GetAddressModule, GetAddressSymbol, GetAddressFileAndLine and
// MiniDumpThreadWrite each carry a C++ exception frame for their CDbgHelpLock and
// stay out of the payload.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/NDebugHelp.h"
#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled. s_dbgHelpLock and
// s_miniDumpLock are a CRITICAL_SECTION's 0x18 bytes, and s_searchPath is the
// 0x8000 bytes LoadSymbols copies it into.

#ifndef OC_dword_510744
#define OC_dword_510744
#ifdef OC_DLL_MODE
#define dword_510744 (*reinterpret_cast<void **>(OC_VA(0x510744u)))
#else
extern void *dword_510744;
#endif
#endif

#ifndef OC_NDebugHelp__s_dbgHelpLock
#define OC_NDebugHelp__s_dbgHelpLock
#ifdef OC_DLL_MODE
#define NDebugHelp__s_dbgHelpLock (reinterpret_cast<BYTE *>(OC_VA(0x6D44F8u)))
#else
extern BYTE NDebugHelp__s_dbgHelpLock[24];
#endif
#endif

#ifndef OC_NDebugHelp__s_searchPath
#define OC_NDebugHelp__s_searchPath
#ifdef OC_DLL_MODE
#define NDebugHelp__s_searchPath (reinterpret_cast<char *>(OC_VA(0x6D4910u)))
#else
extern char NDebugHelp__s_searchPath[0x8000];
#endif
#endif

#ifndef OC_NDebugHelp__s_SymFindFileInPath
#define OC_NDebugHelp__s_SymFindFileInPath
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymFindFileInPath (*reinterpret_cast<void **>(OC_VA(0x6DC910u)))
#else
extern void *NDebugHelp__s_SymFindFileInPath;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymGetLineFromAddr64
#define OC_NDebugHelp__s_SymGetLineFromAddr64
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymGetLineFromAddr64 (*reinterpret_cast<NDebugHelp::PFN_SymGetLineFromAddr64 *>(OC_VA(0x6DC914u)))
#else
extern NDebugHelp::PFN_SymGetLineFromAddr64 NDebugHelp__s_SymGetLineFromAddr64;
#endif
#endif

#ifndef OC_NDebugHelp__s_miniDumpParam
#define OC_NDebugHelp__s_miniDumpParam
#ifdef OC_DLL_MODE
#define NDebugHelp__s_miniDumpParam (*reinterpret_cast<NDebugHelp::MiniDumpParam **>(OC_VA(0x6DC918u)))
#else
extern NDebugHelp::MiniDumpParam *NDebugHelp__s_miniDumpParam;
#endif
#endif

#ifndef OC_NDebugHelp__s_enableMiniDump
#define OC_NDebugHelp__s_enableMiniDump
#ifdef OC_DLL_MODE
#define NDebugHelp__s_enableMiniDump (*reinterpret_cast<bool *>(OC_VA(0x6DC91Cu)))
#else
extern bool NDebugHelp__s_enableMiniDump;
#endif
#endif

#ifndef OC_NDebugHelp__s_symbolsLoaded
#define OC_NDebugHelp__s_symbolsLoaded
#ifdef OC_DLL_MODE
#define NDebugHelp__s_symbolsLoaded (*reinterpret_cast<bool *>(OC_VA(0x6DC91Du)))
#else
extern bool NDebugHelp__s_symbolsLoaded;
#endif
#endif

#ifndef OC_NDebugHelp__s_MiniDumpWriteDump
#define OC_NDebugHelp__s_MiniDumpWriteDump
#ifdef OC_DLL_MODE
#define NDebugHelp__s_MiniDumpWriteDump (*reinterpret_cast<NDebugHelp::PFN_MiniDumpWriteDump *>(OC_VA(0x6DC920u)))
#else
extern NDebugHelp::PFN_MiniDumpWriteDump NDebugHelp__s_MiniDumpWriteDump;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymLoadModule64
#define OC_NDebugHelp__s_SymLoadModule64
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymLoadModule64 (*reinterpret_cast<void **>(OC_VA(0x6DC924u)))
#else
extern void *NDebugHelp__s_SymLoadModule64;
#endif
#endif

#ifndef OC_NDebugHelp__s_miniDumpLock
#define OC_NDebugHelp__s_miniDumpLock
#ifdef OC_DLL_MODE
#define NDebugHelp__s_miniDumpLock (reinterpret_cast<BYTE *>(OC_VA(0x6DC928u)))
#else
extern BYTE NDebugHelp__s_miniDumpLock[24];
#endif
#endif

#ifndef OC_NDebugHelp__s_miniDumpWakeEvent
#define OC_NDebugHelp__s_miniDumpWakeEvent
#ifdef OC_DLL_MODE
#define NDebugHelp__s_miniDumpWakeEvent (*reinterpret_cast<void **>(OC_VA(0x6DC940u)))
#else
extern void *NDebugHelp__s_miniDumpWakeEvent;
#endif
#endif

#ifndef OC_NDebugHelp__s_miniDumpDoneEvent
#define OC_NDebugHelp__s_miniDumpDoneEvent
#ifdef OC_DLL_MODE
#define NDebugHelp__s_miniDumpDoneEvent (*reinterpret_cast<void **>(OC_VA(0x6DC944u)))
#else
extern void *NDebugHelp__s_miniDumpDoneEvent;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymCleanup
#define OC_NDebugHelp__s_SymCleanup
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymCleanup (*reinterpret_cast<void **>(OC_VA(0x6DC948u)))
#else
extern void *NDebugHelp__s_SymCleanup;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymSetOptions
#define OC_NDebugHelp__s_SymSetOptions
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymSetOptions (*reinterpret_cast<NDebugHelp::PFN_SymSetOptions *>(OC_VA(0x6DC94Cu)))
#else
extern NDebugHelp::PFN_SymSetOptions NDebugHelp__s_SymSetOptions;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymGetOptions
#define OC_NDebugHelp__s_SymGetOptions
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymGetOptions (*reinterpret_cast<NDebugHelp::PFN_SymGetOptions *>(OC_VA(0x6DC950u)))
#else
extern NDebugHelp::PFN_SymGetOptions NDebugHelp__s_SymGetOptions;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymInitialize
#define OC_NDebugHelp__s_SymInitialize
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymInitialize (*reinterpret_cast<NDebugHelp::PFN_SymInitialize *>(OC_VA(0x6DC954u)))
#else
extern NDebugHelp::PFN_SymInitialize NDebugHelp__s_SymInitialize;
#endif
#endif

#ifndef OC_NDebugHelp__s_EnumerateLoadedModules64
#define OC_NDebugHelp__s_EnumerateLoadedModules64
#ifdef OC_DLL_MODE
#define NDebugHelp__s_EnumerateLoadedModules64 (*reinterpret_cast<void **>(OC_VA(0x6DC958u)))
#else
extern void *NDebugHelp__s_EnumerateLoadedModules64;
#endif
#endif

#ifndef OC_NDebugHelp__s_StackWalk64
#define OC_NDebugHelp__s_StackWalk64
#ifdef OC_DLL_MODE
#define NDebugHelp__s_StackWalk64 (*reinterpret_cast<void **>(OC_VA(0x6DC95Cu)))
#else
extern void *NDebugHelp__s_StackWalk64;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymGetModuleBase64
#define OC_NDebugHelp__s_SymGetModuleBase64
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymGetModuleBase64 (*reinterpret_cast<void **>(OC_VA(0x6DC960u)))
#else
extern void *NDebugHelp__s_SymGetModuleBase64;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymFunctionTableAccess64
#define OC_NDebugHelp__s_SymFunctionTableAccess64
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymFunctionTableAccess64 (*reinterpret_cast<void **>(OC_VA(0x6DC964u)))
#else
extern void *NDebugHelp__s_SymFunctionTableAccess64;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymUnloadModule64
#define OC_NDebugHelp__s_SymUnloadModule64
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymUnloadModule64 (*reinterpret_cast<void **>(OC_VA(0x6DC968u)))
#else
extern void *NDebugHelp__s_SymUnloadModule64;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymSetSearchPath
#define OC_NDebugHelp__s_SymSetSearchPath
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymSetSearchPath (*reinterpret_cast<void **>(OC_VA(0x6DC96Cu)))
#else
extern void *NDebugHelp__s_SymSetSearchPath;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymGetSymFromAddr64
#define OC_NDebugHelp__s_SymGetSymFromAddr64
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymGetSymFromAddr64 (*reinterpret_cast<NDebugHelp::PFN_SymGetSymFromAddr64 *>(OC_VA(0x6DC970u)))
#else
extern NDebugHelp::PFN_SymGetSymFromAddr64 NDebugHelp__s_SymGetSymFromAddr64;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymEnumerateModules64
#define OC_NDebugHelp__s_SymEnumerateModules64
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymEnumerateModules64 (*reinterpret_cast<void **>(OC_VA(0x6DC974u)))
#else
extern void *NDebugHelp__s_SymEnumerateModules64;
#endif
#endif

#ifndef OC_NDebugHelp__s_SymGetModuleInfo64
#define OC_NDebugHelp__s_SymGetModuleInfo64
#ifdef OC_DLL_MODE
#define NDebugHelp__s_SymGetModuleInfo64 (*reinterpret_cast<NDebugHelp::PFN_SymGetModuleInfo64 *>(OC_VA(0x6DC978u)))
#else
extern NDebugHelp::PFN_SymGetModuleInfo64 NDebugHelp__s_SymGetModuleInfo64;
#endif
#endif

#ifndef OC_NDebugHelp__s_instance
#define OC_NDebugHelp__s_instance
#ifdef OC_DLL_MODE
#define NDebugHelp__s_instance (*reinterpret_cast<void **>(OC_VA(0x6DC97Cu)))
#else
extern void *NDebugHelp__s_instance;
#endif
#endif

namespace NDebugHelp {

// 0x404110 (12 B). Leaves s_dbgHelpLock. Only the destructor is out of line, for
// the unwind tables of the GetAddress* rows that hold the lock.
CDbgHelpLock::~CDbgHelpLock()
{
  LeaveCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(NDebugHelp__s_dbgHelpLock));
}

// DebuggerSetThreadName -- oracle 0x404120 (136 B). Microsoft's debugger
// thread-naming handshake: an exception 0x406D1388 the debugger eats. The filter
// returns -1, EXCEPTION_CONTINUE_EXECUTION.
void DebuggerSetThreadName()
{
  struct THREADNAME_INFO {
    /*0x00*/ DWORD dwType;
    /*0x04*/ const char *pszName;
    /*0x08*/ DWORD dwThreadID;
    /*0x0C*/ DWORD dwFlags;
  } info;

  static_assert(offsetof(THREADNAME_INFO, dwType) == 0x00, "mov dword ptr [ebp-28h], 1000h");
  OC_X86_LAYOUT(offsetof(THREADNAME_INFO, pszName) == 0x04, "mov dword ptr [ebp-24h], 4FDCACh");
  OC_X86_LAYOUT(offsetof(THREADNAME_INFO, dwThreadID) == 0x08, "mov dword ptr [ebp-20h], -1h");
  OC_X86_LAYOUT(offsetof(THREADNAME_INFO, dwFlags) == 0x0C, "mov dword ptr [ebp-1Ch], 0h");
  OC_X86_LAYOUT(sizeof(THREADNAME_INFO) == 0x10, "RaiseException's 4h argument count");

  if (IsDebuggerPresent() != 1)
    return;

  info.dwType = 0x1000;
  info.pszName = "MiniDump Thread";
  info.dwThreadID = (DWORD)-1;
  info.dwFlags = 0;

  __try {
    RaiseException(0x406D1388, 0, sizeof(info) / sizeof(ULONG_PTR),
                   (const ULONG_PTR *)&info);
  } __except (-1) {
  }
}

// LoadLibrary -- oracle 0x4041B0 (381 B). Sixteen separate checks rather than
// one chained condition, which places the failure bodies where 1.17 has them.
// A failure closes the HMODULE with CloseHandle, not FreeLibrary: 1.17's bug.
bool LoadLibrary()
{
  HMODULE hDll;

  if (NDebugHelp__s_instance != NULL)
    return true;

  hDll = LoadLibraryW(L"DbgHelp.dll");
  if (hDll == NULL)
    return false;

  NDebugHelp__s_EnumerateLoadedModules64 = reinterpret_cast<void *>(GetProcAddress(hDll, "EnumerateLoadedModules64"));
  if (NDebugHelp__s_EnumerateLoadedModules64 == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_StackWalk64 = reinterpret_cast<void *>(GetProcAddress(hDll, "StackWalk64"));
  if (NDebugHelp__s_StackWalk64 == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymFunctionTableAccess64 = reinterpret_cast<void *>(GetProcAddress(hDll, "SymFunctionTableAccess64"));
  if (NDebugHelp__s_SymFunctionTableAccess64 == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymGetLineFromAddr64 = reinterpret_cast<PFN_SymGetLineFromAddr64>(GetProcAddress(hDll, "SymGetLineFromAddr64"));
  if (NDebugHelp__s_SymGetLineFromAddr64 == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymGetModuleBase64 = reinterpret_cast<void *>(GetProcAddress(hDll, "SymGetModuleBase64"));
  if (NDebugHelp__s_SymGetModuleBase64 == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymGetModuleInfo64 = reinterpret_cast<PFN_SymGetModuleInfo64>(GetProcAddress(hDll, "SymGetModuleInfo64"));
  if (NDebugHelp__s_SymGetModuleInfo64 == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymGetOptions = (PFN_SymGetOptions)GetProcAddress(hDll, "SymGetOptions");
  if (NDebugHelp__s_SymGetOptions == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymGetSymFromAddr64 = reinterpret_cast<PFN_SymGetSymFromAddr64>(GetProcAddress(hDll, "SymGetSymFromAddr64"));
  if (NDebugHelp__s_SymGetSymFromAddr64 == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymInitialize = (PFN_SymInitialize)GetProcAddress(hDll, "SymInitialize");
  if (NDebugHelp__s_SymInitialize == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymCleanup = reinterpret_cast<void *>(GetProcAddress(hDll, "SymCleanup"));
  if (NDebugHelp__s_SymCleanup == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymSetOptions = (PFN_SymSetOptions)GetProcAddress(hDll, "SymSetOptions");
  if (NDebugHelp__s_SymSetOptions == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymEnumerateModules64 = reinterpret_cast<void *>(GetProcAddress(hDll, "SymEnumerateModules64"));
  if (NDebugHelp__s_SymEnumerateModules64 == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymSetSearchPath = reinterpret_cast<void *>(GetProcAddress(hDll, "SymSetSearchPath"));
  if (NDebugHelp__s_SymSetSearchPath == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymFindFileInPath = reinterpret_cast<void *>(GetProcAddress(hDll, "SymFindFileInPath"));
  if (NDebugHelp__s_SymFindFileInPath == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymLoadModule64 = reinterpret_cast<void *>(GetProcAddress(hDll, "SymLoadModule64"));
  if (NDebugHelp__s_SymLoadModule64 == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_SymUnloadModule64 = reinterpret_cast<void *>(GetProcAddress(hDll, "SymUnloadModule64"));
  if (NDebugHelp__s_SymUnloadModule64 == NULL) {
    CloseHandle(hDll);
    return false;
  }

  NDebugHelp__s_MiniDumpWriteDump = reinterpret_cast<PFN_MiniDumpWriteDump>(GetProcAddress(hDll, "MiniDumpWriteDump"));
  NDebugHelp__s_instance = hDll;
  return true;
}

// LoadSymbols -- oracle 0x404330 (232 B). The search path defaults to the
// executable's own directory. Only SymSetOptions and SymInitialize are
// null-checked; SymGetOptions is called without one.
bool LoadSymbols()
{
  char szPath[0x8000];
  char *pszSlash;

  if (NDebugHelp__s_symbolsLoaded)
    return true;

  if (NDebugHelp__s_instance == NULL)
    return false;

  if (NDebugHelp__s_SymSetOptions == NULL)
    return false;

  if (NDebugHelp__s_SymInitialize == NULL)
    return false;

  NDebugHelp__s_SymSetOptions(NDebugHelp__s_SymGetOptions() | 0x214);

  if (NDebugHelp__s_searchPath[0] == '\0') {
    GetModuleFileNameA(NULL, szPath, sizeof(szPath));
    szPath[sizeof(szPath) - 1] = '\0';

    pszSlash = strrchr(szPath, '\\');
    if (pszSlash != NULL)
      *pszSlash = '\0';
  } else {
    strncpy_s(szPath, sizeof(szPath), NDebugHelp__s_searchPath, (size_t)-1);
  }

  if (dword_510744 == (void *)-1)
    dword_510744 = GetCurrentProcess();

  if (!NDebugHelp__s_SymInitialize(dword_510744, szPath, GetProcessId(dword_510744) != 0))
    return false;

  NDebugHelp__s_symbolsLoaded = true;
  return true;
}

#ifndef OC_PAYLOAD
inline CDbgHelpLock::CDbgHelpLock()
{
  EnterCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(NDebugHelp__s_dbgHelpLock));
}

// GetAddressModule -- oracle 0x404420 (260 B). The module an address falls in,
// and its base when the module is still loaded.
bool GetAddressModule(DWORD dwAddress, char *pszModule, int nUnused, DWORD *pdwBase)
{
  *pszModule = 0;
  *pdwBase = 0;
  CDbgHelpLock lock;
  if (NDebugHelp__s_instance == NULL && !LoadLibrary())
    return false;
  if (!NDebugHelp__s_symbolsLoaded && !LoadSymbols())
    return false;
  if (NDebugHelp__s_SymGetModuleInfo64 == NULL)
    return false;
  ImageHlpModule64 info;
  memset(&info, 0, sizeof(info));
  info.SizeOfStruct = sizeof(info);
  if (!NDebugHelp__s_SymGetModuleInfo64(dword_510744, dwAddress, 0, &info))
    return false;
  if (GetModuleHandleA(info.szLoadedImageName) != NULL)
    *pdwBase = info.dwBaseOfImage;
  strncpy_s(pszModule, 0x100, info.szImageName, (size_t)-1);
  return true;
}

// GetAddressSymbol -- oracle 0x404530 (374 B). The symbol an address falls in
// and the displacement into it; a module with no real symbols gives none.
bool GetAddressSymbol(DWORD dwAddress, char *pszSymbol, int nUnused, DWORD *pdwDisplacement)
{
  *pszSymbol = 0;
  *pdwDisplacement = 0;
  CDbgHelpLock lock;
  if (NDebugHelp__s_instance == NULL && !LoadLibrary())
    return false;
  if (!NDebugHelp__s_symbolsLoaded && !LoadSymbols())
    return false;
  if (NDebugHelp__s_SymGetSymFromAddr64 == NULL)
    return false;
  SymbolBuffer sym;
  memset(&sym, 0, sizeof(sym));
  sym.hdr.SizeOfStruct = sizeof(sym.hdr);
  sym.hdr.Address = dwAddress;
  sym.hdr.MaxNameLength = sizeof(sym.szName);
  DWORD64 qwDisplacement = 0;
  if (!NDebugHelp__s_SymGetSymFromAddr64(dword_510744, dwAddress, 0, &qwDisplacement, &sym.hdr))
    return false;
  if (NDebugHelp__s_SymGetModuleInfo64 == NULL)
    return false;
  ImageHlpModule64 info;
  memset(&info, 0, sizeof(info));
  info.SizeOfStruct = sizeof(info);
  if (!NDebugHelp__s_SymGetModuleInfo64(dword_510744, dwAddress, 0, &info))
    return false;
  if (info.dwSymType == 0 || info.dwSymType == 4 || info.dwSymType == 5)
    return false;
  strncpy_s(pszSymbol, 0x200, sym.hdr.Name, (size_t)-1);
  *pdwDisplacement = (DWORD)qwDisplacement;
  return true;
}

// GetAddressFileAndLine -- oracle 0x4046B0 (220 B). The source file and line of
// an address.
bool GetAddressFileAndLine(DWORD dwAddress, char *pszFile, int nUnused, DWORD *pdwLine)
{
  *pszFile = 0;
  *pdwLine = 0;
  CDbgHelpLock lock;
  if (NDebugHelp__s_instance == NULL && !LoadLibrary())
    return false;
  if (!NDebugHelp__s_symbolsLoaded && !LoadSymbols())
    return false;
  if (NDebugHelp__s_SymGetLineFromAddr64 == NULL)
    return false;
  DWORD dwDisplacement;
  ImageHlpLine64 line;
  memset(&line, 0, sizeof(line));
  line.SizeOfStruct = sizeof(line);
  if (!NDebugHelp__s_SymGetLineFromAddr64(dword_510744, dwAddress, 0, &dwDisplacement, &line))
    return false;
  strncpy_s(pszFile, 0x100, line.FileName, (size_t)-1);
  *pdwLine = line.LineNumber;
  return true;
}

// MiniDumpThreadWrite -- oracle 0x404790 (221 B). Runs the request parked in
// s_miniDumpParam, with the comment as one user stream.
void MiniDumpThreadWrite()
{
  MiniDumpParam *pParam = NDebugHelp__s_miniDumpParam;
  MiniDumpExceptionInfo exception;
  MiniDumpUserStream stream;
  MiniDumpUserStreamInfo streams;

  exception.bClientPointers = FALSE;
  DWORD dwThreadId = pParam->dwThreadId;
  const char *pszComment = pParam->pszComment;
  exception.dwThreadId = dwThreadId;
  exception.pExceptionPointers = pParam->pExceptionPointers;
  DWORD dwCount = 0;
  if (pszComment != NULL) {
    stream.dwType = 0x1000;
    stream.dwBufferSize = strlen(pszComment) + 1;
    stream.pBuffer = pszComment;
    dwCount = 1;
  }
  streams.dwUserStreamCount = dwCount;
  streams.pUserStreamArray = &stream;

  CDbgHelpLock lock;
  if (NDebugHelp__s_MiniDumpWriteDump == NULL)
    pParam->bResult = false;
  else
    pParam->bResult = NDebugHelp__s_MiniDumpWriteDump(
        dword_510744, GetCurrentProcessId(), pParam->hFile, 0x40,
        pParam->pExceptionPointers != NULL ? &exception : NULL, &streams, NULL) != 0;
}
#endif  // OC_PAYLOAD

// MiniDumpThreadProc -- oracle 0x404870 (94 B). 1.17's frame holds a dword it
// writes 0 to and reads once without using; a plain local would be optimized
// away, so it is volatile.
DWORD __stdcall MiniDumpThreadProc(void *pParam)
{
  volatile DWORD dwResult = 0;
  DWORD dwUnused = dwResult;

  DebuggerSetThreadName();

  WaitForSingleObject(NDebugHelp__s_miniDumpWakeEvent, (DWORD)-1);
  while (NDebugHelp__s_miniDumpParam != NULL) {
    MiniDumpThreadWrite();
    SetEvent(NDebugHelp__s_miniDumpDoneEvent);
    WaitForSingleObject(NDebugHelp__s_miniDumpWakeEvent, (DWORD)-1);
  }

  return 0;
}

// WriteMiniDump -- oracle 0x4048D0 (272 B). Hands the request to the minidump
// thread and waits: the worker needs a stack that is not the faulting one. The
// failing load check has its own LeaveCriticalSection.
bool WriteMiniDump(const char *pszPath, const char *pszComment, void *pExceptionPointers,
                   DWORD dwThreadId)
{
  wchar_t wszPath[0x104];
  MiniDumpParam param;
  void *hFile;

  if (!NDebugHelp__s_enableMiniDump)
    return false;

  EnterCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(NDebugHelp__s_dbgHelpLock));
  if (NDebugHelp__s_instance == NULL && !LoadLibrary()) {
    LeaveCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(NDebugHelp__s_dbgHelpLock));
    return false;
  }
  LeaveCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(NDebugHelp__s_dbgHelpLock));

  if (MultiByteToWideChar(0xFDE9, 0, pszPath, -1, wszPath, 0x104) <= 0)
    return false;

  hFile = CreateFileW(wszPath, 0x40000000, 3, NULL, 2, 0x80, NULL);
  if (hFile == (void *)-1)
    return false;

  EnterCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(NDebugHelp__s_miniDumpLock));

  param.hFile = hFile;
  param.dwThreadId = dwThreadId != 0 ? dwThreadId : GetCurrentThreadId();
  param.pszComment = pszComment;
  param.pExceptionPointers = pExceptionPointers;
  param.bResult = false;

  NDebugHelp__s_miniDumpParam = &param;
  SetEvent(NDebugHelp__s_miniDumpWakeEvent);
  WaitForSingleObject(NDebugHelp__s_miniDumpDoneEvent, (DWORD)-1);
  NDebugHelp__s_miniDumpParam = NULL;

  LeaveCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(NDebugHelp__s_miniDumpLock));
  CloseHandle(hFile);

  return param.bResult;
}

}  // namespace NDebugHelp
