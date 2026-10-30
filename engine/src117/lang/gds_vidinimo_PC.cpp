// gds_vidinimo_PC.cpp -- the DirectDraw objects behind the Storm SDraw layer:
// created, freed and handed palette updates. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>
#include <windows.h>
#include <ddraw.h>
#include "../h/gds_vidinimo_PC.h"
#include "../h/gds_appfat.h"
#include "../h/gds_mou_win.h"
#include "../h/rect32.h"
#include "../gen/globals117.h"
#include "../port/render/port_present.h"  // Hyperion

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// ghWnd, 0x66FD28: the main window.
#ifdef OC_DLL_MODE
#define ghWnd (*reinterpret_cast<HWND *>(OC_VA(0x66FD28u)))
#else
extern HWND ghWnd;
#endif

// ddemulate, 0x6D3184: a BYTE flag that selects the emulation-only driver.
#ifdef OC_DLL_MODE
#define ddemulate (*reinterpret_cast<BYTE *>(OC_VA(0x6D3184u)))
#else
extern BYTE ddemulate;
#endif

// PrimarySurface, 0x6D3188.
#ifdef OC_DLL_MODE
#define PrimarySurface (*reinterpret_cast<IDirectDrawSurface **>(OC_VA(0x6D3188u)))
#else
extern IDirectDrawSurface *PrimarySurface;
#endif

// sghDDlib, 0x6D318C: the ddraw.dll module handle.
#ifdef OC_DLL_MODE
#define sghDDlib (*reinterpret_cast<HMODULE *>(OC_VA(0x6D318Cu)))
#else
extern HMODULE sghDDlib;
#endif

// DDInterface, 0x6D3190.
#ifdef OC_DLL_MODE
#define DDInterface (*reinterpret_cast<IDirectDraw **>(OC_VA(0x6D3190u)))
#else
extern IDirectDraw *DDInterface;
#endif

// BackSurface, 0x6D3194.
#ifdef OC_DLL_MODE
#define BackSurface (*reinterpret_cast<IDirectDrawSurface **>(OC_VA(0x6D3194u)))
#else
extern IDirectDrawSurface *BackSurface;
#endif

// Storm imports, the first three spelled exactly as gds_vidblit.cpp and
// gluBN.cpp spell them. SDrawManualInitialize is the import at 0x4D037C.
extern "C" {
BOOL __stdcall SDrawLockSurface(DWORD dwArg0, DWORD dwArg1, void **ppBits,
                                int *pnPitch, DWORD dwFlags);  // 0x4D002E -> [0x4EB550] -> ord 350
BOOL __stdcall SDrawUnlockSurface(DWORD dwArg0, void *pBits, int nRects,
                                  const rect32_t *prc);  // 0x4D0034 -> [0x4EB54C] -> ord 356
BOOL __stdcall SDrawUpdatePalette(DWORD dwFirst, DWORD dwCount,
                                  void *pPalette, DWORD dwUpdate);
BOOL __stdcall SDrawManualInitialize(HWND hWnd, IDirectDraw *pDD,
                                     IDirectDrawSurface *pPrimary,
                                     IDirectDrawSurface *pArg3,
                                     IDirectDrawSurface *pArg4,
                                     IDirectDrawSurface *pBack);
}

typedef HRESULT(WINAPI *PFN_DIRECTDRAWCREATE)(GUID *pGuid, IDirectDraw **ppDD,
                                              IUnknown *pOuter);

// The bodies, in oracle VA order.

// init_directx -- 0x4CF960. Loads ddraw.dll, takes exclusive fullscreen at
// 640x480x32 (falling back to the desktop size), creates the primary surface
// and, when it cannot be locked, a system-memory back surface, then hands the
// set to Storm. Each failure is fatal through ErrorDlg.
void init_directx()
{
  // Hyperion: no DirectDraw and no Storm SDraw. The screen is an 8-bit
  // buffer of Hyperion's own, shown in this window through SDL3
  // (port_present.cpp, docs/renderer.md). The oracle loaded ddraw.dll here,
  // took exclusive fullscreen at 640x480x32, created the primary surface and
  // handed it to SDrawManualInitialize; the cursor clip after it is kept.
  PortPresentInit(ghWnd);
  sub_4CE820(1);
}

// free_directx -- 0x4CFCE0. Detaches Storm, releases the three DirectDraw
// objects (each pointer cleared before its Release) and minimizes the window.
void free_directx()
{
  PortPresentShutdown();  // Hyperion: the oracle detached Storm and released DirectDraw
  if (ghWnd)
    ShowWindow(ghWnd, SW_MINIMIZE);
}

// flip_palette -- 0x4CFD60. Pushes the entries to Storm's palette, then locks
// and unlocks the surface with no work in between.
void flip_palette(palentry_t *pEntries, int nFirst, int nCount)
{
  // Hyperion: the entries go to the screen's palette and the frame is
  // shown again; the oracle called SDrawUpdatePalette and an empty lock and
  // unlock, which made Storm convert the surface with the new palette.
  PortPresentPalette(pEntries, nFirst, nCount);
  PortPresentFrame();
}
