// Hyperion: not built. The game's crash reporter is gone from the port and the
// port's own (port_crash.cpp) stays; the file is kept so that later changes to
// the reconstruction still apply. An address of its functions in 1.17's data
// goes to PortOriginalCodeReached.
//
// Exception.cpp -- the unhandled-exception filter and the status-code name
// table behind it. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/Storm.h"          // the one <windows.h>: SetUnhandledExceptionFilter
#include "../h/crashy.h"
#include "../h/Exception.h"
#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled.

// 0x6D40E0 -- the installed handler, read as an indirect call target and used
// as the filter's own enable test.
#ifndef OC_SGPFNCRASHYHANDLER_G
#define OC_SGPFNCRASHYHANDLER_G
#ifdef OC_DLL_MODE
#define sgpfnCrashyHandler_g (*reinterpret_cast<crashy::PFN_CRASHY_HANDLER *>(OC_VA(0x6D40E0u)))
#else
extern crashy::PFN_CRASHY_HANDLER sgpfnCrashyHandler_g;
#endif
#endif

// 0x6D40E4 -- SetUnhandledExceptionFilter's previous value, kept so the
// uninstall path can put it back.
#ifndef OC_SGPFNPREVFILTER_G
#define OC_SGPFNPREVFILTER_G
#ifdef OC_DLL_MODE
#define sgpfnPrevFilter_g (*reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER *>(OC_VA(0x6D40E4u)))
#else
extern LPTOP_LEVEL_EXCEPTION_FILTER sgpfnPrevFilter_g;
#endif
#endif

// 0x6D40E8 -- the handler's opaque argument, loaded into EDX at the call.
#ifndef OC_SGPVCRASHYPARAM_G
#define OC_SGPVCRASHYPARAM_G
#ifdef OC_DLL_MODE
#define sgpvCrashyParam_g (*reinterpret_cast<void **>(OC_VA(0x6D40E8u)))
#else
extern void *sgpvCrashyParam_g;
#endif
#endif

// GetExceptionNameWin32 -- oracle 0x4025D0 (742 B). The cases are in winnt.h's
// order, as 1.17's jump tables are. Each case only picks the text for the one
// copy after the switch; the default arm copies and returns on its own. The
// third parameter is never read.
void GetExceptionNameWin32(DWORD dwCode, char *pszName, size_t cbName)
{
  const char *pszText;

  switch (dwCode) {
  case EXCEPTION_ACCESS_VIOLATION:
    pszText = "ACCESS_VIOLATION";
    break;
  case EXCEPTION_DATATYPE_MISALIGNMENT:
    pszText = "DATATYPE_MISALIGNMENT";
    break;
  case EXCEPTION_BREAKPOINT:
    pszText = "BREAKPOINT";
    break;
  case EXCEPTION_SINGLE_STEP:
    pszText = "SINGLE_STEP";
    break;
  case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    pszText = "ARRAY_BOUNDS_EXCEEDED";
    break;
  case EXCEPTION_FLT_DENORMAL_OPERAND:
    pszText = "FLT_DENORMAL_OPERAND";
    break;
  case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    pszText = "FLT_DIVIDE_BY_ZERO";
    break;
  case EXCEPTION_FLT_INEXACT_RESULT:
    pszText = "FLT_INEXACT_RESULT";
    break;
  case EXCEPTION_FLT_INVALID_OPERATION:
    pszText = "FLT_INVALID_OPERATION";
    break;
  case EXCEPTION_FLT_OVERFLOW:
    pszText = "FLT_OVERFLOW";
    break;
  case EXCEPTION_FLT_STACK_CHECK:
    pszText = "FLT_STACK_CHECK";
    break;
  case EXCEPTION_FLT_UNDERFLOW:
    pszText = "FLT_UNDERFLOW";
    break;
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
    pszText = "INT_DIVIDE_BY_ZERO";
    break;
  case EXCEPTION_INT_OVERFLOW:
    pszText = "INT_OVERFLOW";
    break;
  case EXCEPTION_PRIV_INSTRUCTION:
    pszText = "PRIV_INSTRUCTION";
    break;
  case EXCEPTION_IN_PAGE_ERROR:
    pszText = "IN_PAGE_ERROR";
    break;
  case EXCEPTION_ILLEGAL_INSTRUCTION:
    pszText = "ILLEGAL_INSTRUCTION";
    break;
  case EXCEPTION_NONCONTINUABLE_EXCEPTION:
    pszText = "NONCONTINUABLE_EXCEPTION";
    break;
  case EXCEPTION_STACK_OVERFLOW:
    pszText = "STACK_OVERFLOW";
    break;
  case EXCEPTION_INVALID_DISPOSITION:
    pszText = "INVALID_DISPOSITION";
    break;
  case EXCEPTION_GUARD_PAGE:
    pszText = "GUARD_PAGE";
    break;
  case EXCEPTION_INVALID_HANDLE:
    pszText = "INVALID_HANDLE";
    break;
  default: {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");

    if (hNtdll == NULL ||
        !FormatMessageA(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS,
                        hNtdll, dwCode, 0x400, pszName, 0x100, NULL))
      strncpy_s(pszName, 0x100, "unknown exception", _TRUNCATE);
    return;
  }
  }

  strncpy_s(pszName, 0x100, pszText, _TRUNCATE);
}

// CrashyExceptionFilter -- oracle 0x402A10 (147 B). Three parameters, so under
// /Gr the pointer lands on the stack and `ret 4` fits SetUnhandledExceptionFilter's
// one stdcall argument. The first is only forwarded; 1.17 passes whatever ECX held.
LONG CrashyExceptionFilter(size_t cbName, void *pvUnused,
                           EXCEPTION_POINTERS *pep)
{
  if (sgpfnCrashyHandler_g == NULL)
    return 0;

  char szMessage[0x200];
  char szName[0x100];
  crashy::ExceptionInfo info;

  GetExceptionNameWin32(pep->ExceptionRecord->ExceptionCode, szName, cbName);
  szMessage[0] = '\0';
  info.pszName = szName;
  info.pszMessage = szMessage;
  info.dwThreadId = GetCurrentThreadId();
  info.pExceptionPointers = pep;
  info.pFrames = NULL;
  info.nFrames = 0;
  info.nSkip = 0;

  int nAction = sgpfnCrashyHandler_g(&info, sgpvCrashyParam_g);
  if (nAction == 0)
    return 0;

  return nAction == 1 ? -1 : 1;
}

namespace crashy {

// SetExceptionHandler -- oracle 0x402AB0 (64 B). The third parameter is pushed
// by the one caller and never read here.
void SetExceptionHandler(PFN_CRASHY_HANDLER pfnHandler, void *pvParam,
                         DWORD dwUnused)
{
  sgpfnCrashyHandler_g = pfnHandler;
  sgpvCrashyParam_g = pvParam;

  if (pfnHandler == NULL) {
    if (sgpfnPrevFilter_g != NULL) {
      SetUnhandledExceptionFilter(sgpfnPrevFilter_g);
      sgpfnPrevFilter_g = NULL;
    }
  } else {
    sgpfnPrevFilter_g = SetUnhandledExceptionFilter(
        reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(CrashyExceptionFilter));
  }
}

}  // namespace crashy
