// gds_sys_Win.cpp -- Win32 helpers: centring a window, error-code text, and
// the process exit. Bodies are in oracle VA order.

#include "../h/gds_sys_Win.h"
#include "../h/gds_local.h"
#include "../h/Storm.h"
#include <ddraw.h>
#include "../port/data/port_dsound.h"  // Hyperion: DirectSound's error codes
#include "../port/platform/port_posix.h"  // Hyperion: PortWebExit
#include <stdlib.h>
#include <string.h>
#include "../gen/globals117.h"

// Storm ordinal 462, spelled as in net_cmds.cpp.
extern "C" {
BOOL  __stdcall SErrGetErrorStr(DWORD dwErrCode, char *pszBuffer,
                                unsigned int nBufferSize);
}

// center_window -- 0x4CEC50. Moves the window to the centre of the screen
// without resizing it.
void center_window(HWND hWnd)
{
  RECT rc;
  GetWindowRect(hWnd, &rc);
  int cx = rc.right - rc.left;
  int cy = rc.bottom - rc.top;
  HDC hdc = GetDC(hWnd);
  int nScreenX = GetDeviceCaps(hdc, HORZRES);
  int nScreenY = GetDeviceCaps(hdc, VERTRES);
  ReleaseDC(hWnd, hdc);
  SetWindowPos(hWnd, NULL, (nScreenX - cx) / 2, (nScreenY - cy) / 2, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER);
}

// strGetError -- 0x4CECD0. Formats the text for an error code into pszBuf by
// facility, then strips a trailing CR/LF. The strip starts one character short
// of the end and reads pszBuf[-1] on a one-character string, as 1.17 does.
char *strGetError(DWORD dwError, char *pszBuf, int cchBuf)
{
  DWORD dwFacility = (dwError >> 16) & 0x1FFF;
  if (dwFacility == 0x878)
    get_dsound_error(dwError, pszBuf, cchBuf);
  else if (dwFacility == 0x876)
    get_ddraw_error(dwError, pszBuf, cchBuf);
  else if (dwFacility == 0x617)
    psnprintf(pszBuf, cchBuf, "MMSYS error 0x%x", dwError);
  else if (!SErrGetErrorStr(dwError, pszBuf, cchBuf)
           && !FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwError, 0x400, pszBuf, cchBuf, NULL))
    psnprintf(pszBuf, cchBuf, "unknown error 0x%08x", dwError);
  int n = (int)strlen(pszBuf);
  char *p = pszBuf + n - 1;
  while (n > 0) {
    --p;
    --n;
    if (*p != '\r' && *p != '\n')
      break;
    *p = 0;
  }
  return pszBuf;
}

// get_ddraw_error -- 0x4CEDA0. Copies the DDERR_ name of hr into pszBuf.
void get_ddraw_error(HRESULT hr, char *pszBuf, int cchBuf)
{
  const char *psz;
  switch (hr) {
  case DD_OK:
    psz = "DD_OK";
    break;
  case DDERR_ALREADYINITIALIZED:
    psz = "DDERR_ALREADYINITIALIZED";
    break;
  case DDERR_EXCEPTION:
    psz = "DDERR_EXCEPTION";
    break;
  case DDERR_INCOMPATIBLEPRIMARY:
    psz = "DDERR_INCOMPATIBLEPRIMARY";
    break;
  case DDERR_CANNOTATTACHSURFACE:
    psz = "DDERR_CANNOTATTACHSURFACE";
    break;
  case DDERR_CANNOTDETACHSURFACE:
    psz = "DDERR_CANNOTDETACHSURFACE";
    break;
  case DDERR_CURRENTLYNOTAVAIL:
    psz = "DDERR_CURRENTLYNOTAVAIL";
    break;
  case DDERR_INVALIDMODE:
    psz = "DDERR_INVALIDMODE";
    break;
  case DDERR_INVALIDPIXELFORMAT:
    psz = "DDERR_INVALIDPIXELFORMAT";
    break;
  case DDERR_INVALIDRECT:
    psz = "DDERR_INVALIDRECT";
    break;
  case DDERR_LOCKEDSURFACES:
    psz = "DDERR_LOCKEDSURFACES";
    break;
  case DDERR_NODIRECTDRAWSUPPORT:
    psz = "DDERR_NODIRECTDRAWSUPPORT";
    break;
  case DDERR_NOEXCLUSIVEMODE:
    psz = "DDERR_NOEXCLUSIVEMODE";
    break;
  case DDERR_NOTFOUND:
    psz = "DDERR_NOTFOUND";
    break;
  case DDERR_OUTOFVIDEOMEMORY:
    psz = "DDERR_OUTOFVIDEOMEMORY";
    break;
  case DDERR_PALETTEBUSY:
    psz = "DDERR_PALETTEBUSY";
    break;
  case DDERR_SURFACEBUSY:
    psz = "DDERR_SURFACEBUSY";
    break;
  case DDERR_CANTLOCKSURFACE:
    psz = "DDERR_CANTLOCKSURFACE";
    break;
  case DDERR_SURFACELOST:
    psz = "DDERR_SURFACELOST";
    break;
  case DDERR_UNSUPPORTEDFORMAT:
    psz = "DDERR_UNSUPPORTEDFORMAT";
    break;
  case DDERR_VERTICALBLANKINPROGRESS:
    psz = "DDERR_VERTICALBLANKINPROGRESS";
    break;
  case DDERR_WASSTILLDRAWING:
    psz = "DDERR_WASSTILLDRAWING";
    break;
  case DDERR_INVALIDDIRECTDRAWGUID:
    psz = "DDERR_INVALIDDIRECTDRAWGUID";
    break;
  case DDERR_DIRECTDRAWALREADYCREATED:
    psz = "DDERR_DIRECTDRAWALREADYCREATED";
    break;
  case DDERR_NODIRECTDRAWHW:
    psz = "DDERR_NODIRECTDRAWHW";
    break;
  case DDERR_PRIMARYSURFACEALREADYEXISTS:
    psz = "DDERR_PRIMARYSURFACEALREADYEXISTS";
    break;
  case DDERR_NOEMULATION:
    psz = "DDERR_NOEMULATION";
    break;
  case DDERR_NOHWND:
    psz = "DDERR_NOHWND";
    break;
  case DDERR_HWNDSUBCLASSED:
    psz = "DDERR_HWNDSUBCLASSED";
    break;
  case DDERR_HWNDALREADYSET:
    psz = "DDERR_HWNDALREADYSET";
    break;
  case DDERR_NOPALETTEATTACHED:
    psz = "DDERR_NOPALETTEATTACHED";
    break;
  case DDERR_NOPALETTEHW:
    psz = "DDERR_NOPALETTEHW";
    break;
  case DDERR_EXCLUSIVEMODEALREADYSET:
    psz = "DDERR_EXCLUSIVEMODEALREADYSET";
    break;
  case DDERR_CANTCREATEDC:
    psz = "DDERR_CANTCREATEDC";
    break;
  case DDERR_NODC:
    psz = "DDERR_NODC";
    break;
  case DDERR_WRONGMODE:
    psz = "DDERR_WRONGMODE";
    break;
  case DDERR_NOTPALETTIZED:
    psz = "DDERR_NOTPALETTIZED";
    break;
  case DDERR_UNSUPPORTEDMODE:
    psz = "DDERR_UNSUPPORTEDMODE";
    break;
  default:
    psnprintf(pszBuf, cchBuf, "DDERR unknown 0x%x", hr);
    return;
  }
  SStrCopy(pszBuf, psz, cchBuf);
}

// get_dsound_error -- 0x4CF350. Copies the DSERR_ name of hr into pszBuf, in
// the same shape as get_ddraw_error.
void get_dsound_error(HRESULT hr, char *pszBuf, int cchBuf)
{
  const char *psz;
  switch (hr) {
  case DS_OK:
    psz = "DS_OK";
    break;
  case DSERR_ALLOCATED:
    psz = "DSERR_ALLOCATED";
    break;
  case DSERR_CONTROLUNAVAIL:
    psz = "DSERR_CONTROLUNAVAIL";
    break;
  case DSERR_INVALIDCALL:
    psz = "DSERR_INVALIDCALL";
    break;
  case DSERR_PRIOLEVELNEEDED:
    psz = "DSERR_PRIOLEVELNEEDED";
    break;
  case DSERR_BADFORMAT:
    psz = "DSERR_BADFORMAT";
    break;
  case DSERR_NODRIVER:
    psz = "DSERR_NODRIVER";
    break;
  case DSERR_ALREADYINITIALIZED:
    psz = "DSERR_ALREADYINITIALIZED";
    break;
  case DSERR_BUFFERLOST:
    psz = "DSERR_BUFFERLOST";
    break;
  case DSERR_INVALIDPARAM:
    psz = "DSERR_INVALIDPARAM";
    break;
  case DSERR_NOAGGREGATION:
    psz = "DSERR_NOAGGREGATION";
    break;
  case DSERR_OUTOFMEMORY:
    psz = "DSERR_OUTOFMEMORY";
    break;
  case E_NOINTERFACE:
    psz = "E_NOINTERFACE";
    break;
  default:
    psnprintf(pszBuf, cchBuf, "DSERR unknown 0x%x", hr);
    return;
  }
  SStrCopy(pszBuf, psz, cchBuf);
}

// gds_exit -- 0x4CF4E0. Ends the process. Not declared noreturn, so that
// app_exit reaches it by a tail jump.
// Hyperion: the payload has it, and exit is its own runtime's
//. The stdio is ours since step 2, and the
// original's atexit chain holds only its static objects' destructors, which
// free memory and critical sections the process is about to lose anyway.
void gds_exit(int nCode)
{
#if defined(__EMSCRIPTEN__)
  PortWebExit(nCode);  // Hyperion: in a browser the page is told
#endif
  exit(nCode);
}
