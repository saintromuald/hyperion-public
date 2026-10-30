// gds_local.cpp -- the localisation resource module: the satellite DLL the game
// loads its strings, dialogs, accelerators and binary resources out of. Bodies
// are in oracle VA order.
//
// Every lookup that can fall back does, to the executable's own instance, so
// the LoadStringA and LoadAcceleratorsA pairs read as duplicated code.

#include "oc_data.h"  // Hyperion: OC_VA
#include <windows.h>

#include "../h/gds_local.h"
#include "../gen/globals117.h"
#include "../port/platform/port_standalone.h"  // Hyperion: PortGameAccelerators, PortLoadResources
#include "../port/platform/port_winmsg.h"  // Hyperion: the accelerator tables
#include "../port/tools/port_startup.h"  // Hyperion: PortStartupStep, PortStartupWait
#include "../port/data/port_local.h"  // Hyperion: the stand-in for a missing Local.dll

// strtoul and _vsnprintf, declared here rather than through the CRT headers,
// as init.cpp does.
extern "C" {
unsigned long __cdecl strtoul(const char *pszStr, char **ppszEnd, int nBase);
int           __cdecl _vsnprintf(char *pszBuf, size_t cchBuf, const char *pszFormat, va_list args);
}

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// ghLocalDLL, 0x6D0C54: the satellite's handle. InitLocaleModule is the only
// writer.
#ifdef OC_DLL_MODE
#define ghLocalDLL (*reinterpret_cast<HMODULE *>(OC_VA(0x6D0C54u)))
#else
extern HMODULE ghLocalDLL;
#endif

// ghInst, 0x66FD34: the executable's own instance, the fallback for the two
// resource lookups that have one.
#ifdef OC_DLL_MODE
#define ghInst (*reinterpret_cast<HMODULE *>(OC_VA(0x66FD34u)))
#else
extern HMODULE ghInst;
#endif

// ghWnd, 0x66FD28: the main window.
#ifdef OC_DLL_MODE
#define ghWnd (*reinterpret_cast<HWND *>(OC_VA(0x66FD28u)))
#else
extern HWND ghWnd;
#endif

// appcritf, 0x4CC190, __cdecl varargs, spelled as init.cpp spells it; declared
// here, not in a header, for the reason dlg_draw.h gives.
void __cdecl appcritf(const char *pszFmt, ...);

// The bodies, in oracle VA order.

// 0x4CB6F0 (27 B).
void FreeLocaleModule()
{
  HMODULE hDll = ghLocalDLL;

  if (hDll != 0) {
    FreeLibrary(hDll);
    ghLocalDLL = 0;
  }
}

// 0x4CB710 (62 B). The template id is narrowed with movzx on its way into the
// call and pushed whole into appcritf, which is MAKEINTRESOURCE rather than a
// 16-bit parameter.
int GdsDialogBoxParam(int nTemplate, void *pfnDlgProc, void *pInitParam)
{
  // Hyperion: a dialog of the start is the player's to answer, and the start-up log says
  // which it was and what was chosen
  const bool bStarting = PortStartupPending();
  if (bStarting)
    PortStartupStep("dialog", "1.17's dialog %d asks the player", nTemplate);
  PortStartupWait(true);
  // Hyperion: with no Local.dll, the dialog in Hyperion's words (port_local.cpp)
  int nResult = PortLocalOwn() ? PortLocalDialog(nTemplate, static_cast<const char *>(pInitParam))
                               : (int)DialogBoxParamA(ghLocalDLL, MAKEINTRESOURCEA(nTemplate),
                                                      ghWnd, (DLGPROC)pfnDlgProc, (LPARAM)pInitParam);
  PortStartupWait(false);
  if (bStarting)
    PortStartupStep("dialog", "1.17's dialog %d answered %d", nTemplate, nResult);

  if (nResult == -1)
    appcritf("GdsDialogBoxParam: %d", nTemplate);
  return nResult;
}

// 0x4CB750 (89 B). String resource 3 is the locale id, written as hex. The
// satellite is asked first and the executable second; 0x409 is US English.
int GetLocale()
{
  char szId[16];
  char *pszEnd;

  if (ghLocalDLL == 0 || LoadStringA(ghLocalDLL, 3, szId, sizeof(szId)) == 0) {
    // Hyperion: 1.17's exe has no string 3, so the oracle's second ask always
    // came back empty; with no exe read it is not made.
    return 0x409;
  }
  return (int)strtoul(szId, &pszEnd, 16);
}

// 0x4CB7B0 (87 B). The size out-parameter is optional and is written before the
// load, not after.
void *GetResource(const char *pszName, const char *pszType, DWORD *pcbSize)
{
  HRSRC hInfo;
  DWORD cbSize;
  HGLOBAL hRes;

  // Hyperion: with no Local.dll, the stand-in's, which are named (port_local.cpp)
  if (PortLocalOwn()) {
    unsigned long cbLocal = 0;
    const void *pLocal = reinterpret_cast<uintptr_t>(pszName) >> 16 != 0 ? PortLocalResource(pszName, &cbLocal) : 0;
    if (pLocal != 0 && pcbSize != 0)
      *pcbSize = cbLocal;
    return const_cast<void *>(pLocal);
  }
  hInfo = FindResourceA(ghLocalDLL, pszName, pszType);
  if (hInfo == 0)
    return 0;

  cbSize = SizeofResource(ghLocalDLL, hInfo);
  if (cbSize == 0)
    return 0;
  if (pcbSize != 0)
    *pcbSize = cbSize;

  hRes = LoadResource(ghLocalDLL, hInfo);
  if (hRes == 0)
    return 0;
  return LockResource(hRes);
}

// 0x4CB810 (39 B).
void *GetAcceleratorTable(int nResId)
{
  void *hAccel;

  // Hyperion: the table is port_winmsg.cpp's, made from the resource, since the
  // window and its messages are SDL's; the oracle called LoadAcceleratorsA.
  if (ghLocalDLL != 0) {
    hAccel = WinMsgLoadAccelerators(ghLocalDLL, nResId);
    if (hAccel != 0)
      return hAccel;
  }
  // Hyperion: with no Local.dll, its tables from Hyperion's source (port_local.cpp)
  if (PortLocalOwn()) {
    unsigned long cbLocal = 0;
    const void *pLocal = PortLocalAccelerators(nResId, &cbLocal);
    if (pLocal != 0)
      return WinMsgAccelerators(pLocal, cbLocal);
  }
  // Hyperion: the exe's own table, 0x71 (the chat's keys), which Local.dll lacks, is
  // Hyperion's copy (src117/gen/data117.cpp).
  ULONG cb = 0;
  const void *p = PortGameAccelerators(nResId, &cb);
  return p != 0 ? WinMsgAccelerators(p, cb) : 0;
}

// 0x4CB840 (22 B).
BOOL InitLocaleModule(const char *pszPath)
{
  // Hyperion: for its resources only, in the standalone mode as a data file
  // (port_standalone.cpp).
  ghLocalDLL = PortLoadResources(pszPath);
  return ghLocalDLL != 0;
}

// 0x4CB860 (37 B). The forced terminator at cchBuf-1 is the whole point:
// _vsnprintf leaves the buffer unterminated when it truncates.
int __cdecl psnprintf(char *pszBuf, size_t cchBuf, const char *pszFormat, ...)
{
  va_list args;
  int nWritten;

  va_start(args, pszFormat);
  nWritten = _vsnprintf(pszBuf, cchBuf, pszFormat, args);
  pszBuf[cchBuf - 1] = 0;
  return nWritten;
}
