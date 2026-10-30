// port_window.cpp -- the game window's corners and icon. Presentation only.
//
// Windows 11 rounds the corners of top-level windows, which in a window clips the 640x480
// frame's, where the main menu right-aligns its version text four pixels from the edge, so the
// game's window asks for square corners. Windows before 11 ignore the attribute; full screen has
// no corners to round. The icon is Hyperion's, the Battlecruiser, resource 1 of Hyperion.exe
// (hyperion/hyperion.rc), set on SDL's window (port_winmsg.cpp); 1.17's was resource 102 of its
// exe, on the window class.
#include "port_window.h"
#if defined(_WIN32)
#include "Storm.h"        // <windows.h>

// dwmapi.h's values, which the VS2013 SDK predates.
static const DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
static const DWORD DWMWCP_DONOTROUND = 1;

static const int HYPERION_ICON = 1;

typedef HRESULT (WINAPI *PFN_DWMSETWINDOWATTRIBUTE)(HWND, DWORD, LPCVOID, DWORD);

static HICON WindowIcon(bool bSmall)
{
  HMODULE hSelf;
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCSTR>(&WindowIcon), &hSelf))
    return NULL;
  return static_cast<HICON>(LoadImageA(hSelf, MAKEINTRESOURCEA(HYPERION_ICON), IMAGE_ICON,
                                       GetSystemMetrics(bSmall ? SM_CXSMICON : SM_CXICON),
                                       GetSystemMetrics(bSmall ? SM_CYSMICON : SM_CYICON), 0));
}

void PortWindowCreated(void *pWnd)
{
  HWND hWnd = static_cast<HWND>(pWnd);
  if (HICON hBig = WindowIcon(false))
    SendMessageA(hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hBig));
  if (HICON hSmall = WindowIcon(true))
    SendMessageA(hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmall));
  HMODULE hDwm = LoadLibraryExA("dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (hDwm == NULL)
    return;
  PFN_DWMSETWINDOWATTRIBUTE pfnSet =
      reinterpret_cast<PFN_DWMSETWINDOWATTRIBUTE>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
  if (pfnSet != NULL) {
    DWORD dwPref = DWMWCP_DONOTROUND;
    pfnSet(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &dwPref, sizeof dwPref);
  }
}
#else
void PortWindowCreated(void *)
{
}
#endif
