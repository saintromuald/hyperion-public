// gds_mou_win.cpp -- the Win32 mouse: position, and the cursor cage. Bodies are
// in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <windows.h>
#include "../h/gds_mou_win.h"
#include "../h/rect32.h"
#include "../gen/globals117.h"
#include "../port/render/port_present.h"  // Hyperion: PortClipCursor, PortCursorGet, PortCursorSet
#include "../port/render/port_view.h"  // Hyperion: g_portView

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x66FD28 ghWnd, the main window, spelled as event_Win.cpp spells it.
#ifndef OC_GH_WND
#define OC_GH_WND
#ifdef OC_DLL_MODE
#define ghWnd (*reinterpret_cast<void * *>(OC_VA(0x66FD28u)))
#else
extern void * ghWnd;
#endif
#endif

// 0x6D3160 dword_6D3160. The clip flag sub_4CE820 stores and mou_cager tests (dlgFatal.cpp).
#ifndef OC_DWORD_6_D3160
#define OC_DWORD_6_D3160
#ifdef OC_DLL_MODE
#define dword_6D3160 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3160u)))
#else
extern DWORD dword_6D3160;
#endif
#endif

// 0x6D3164 sgbIsCaged_g, a DWORD despite the name.
#ifndef OC_SGB_IS_CAGED_G
#define OC_SGB_IS_CAGED_G
#ifdef OC_DLL_MODE
#define sgbIsCaged_g (*reinterpret_cast<DWORD *>(OC_VA(0x6D3164u)))
#else
extern DWORD sgbIsCaged_g;
#endif
#endif

// 0x6D3168 sgwCage_g. The 16-byte cage rectangle SetRect fills and ClipCursor takes.
#ifndef OC_SGW_CAGE_G
#define OC_SGW_CAGE_G
#ifdef OC_DLL_MODE
#define sgwCage_g (*reinterpret_cast<rect32_t *>(OC_VA(0x6D3168u)))
#else
extern rect32_t sgwCage_g;
#endif
#endif

// The bodies, in oracle VA order.

// 0x4CE820. Records whether the cursor is clipped and applies it: the cage,
// refreshed from the window first when nothing has caged it, or no clip at all.
void sub_4CE820(int bClip)
{
  dword_6D3160 = bClip;
  if (bClip && !sgbIsCaged_g)
    UpdateScreenRect();
  // Hyperion: the cage is in game pixels; the port places it on screen
  // (the oracle called ClipCursor with it directly).
  PortClipCursor(bClip ? reinterpret_cast<RECT *>(&sgwCage_g) : NULL);
}

// 0x4CE890. Reads the cursor position in screen coordinates.
void mou_get_pos(int *pnX, int *pnY)
{
  // Hyperion: the position in game pixels, from the port; the oracle's
  // screen was the frame, so it called GetCursorPos here.
  PortCursorGet(pnX, pnY);
}

// 0x4CE8C0. Moves the cursor and records the new position.
void mou_set_pos(short x, short y)
{
  int nY = y;
  int nX = x;

  PortCursorSet(nX, nY);  // Hyperion: was SetCursorPos; the port maps it to the window
  gCursorPos_g[1] = nY;
  gCursorPos_g[0] = nX;
}

// 0x4CE8E0. Sets the cage to the window's 640 by 480 client area in screen
// coordinates.
void UpdateScreenRect(void)
{
  // Hyperion: the cage is the whole frame in game pixels, the view's
  // screen (port_view.cpp), which PortClipCursor places on screen; the oracle
  // converted the window's 640x480 client area with ClientToScreen here.
  SetRect(reinterpret_cast<RECT *>(&sgwCage_g), 0, 0, g_portView.nWidth, g_portView.nHeight);
}
