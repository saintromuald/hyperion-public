// gds_vidblit.cpp -- the screen blit layer: it locks the DirectDraw surface,
// copies the back buffer into it, and keeps the list of transparency masks that
// are punched out of the frame. Bodies are in oracle VA order.
//
// The TU's name is 1.17's own: vid_erase_mask's SMemFree owner tag is the
// .rdata string "vidblit.cpp" at 0x4F1330.
//
// The mask list is a Storm TSList, so its terminator is a complemented address,
// not NULL (TSList.h). Every walk ends on a signed `p > 0`; written with
// `p != 0` it runs off the end.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gds_vidblit.h"
#include "../h/Storm.h"
#include "../h/rtti.h"          // type_info::raw_name -- OURS, not <typeinfo>
#include "../h/dlg_dlgdraw.h"   // img_area_invalid, dlg_set_all_update
#include "../gen/globals117.h"
#include "../port/render/port_console.h"  // Hyperion: PortConsoleMaskLeftOut
#include "../port/render/port_mask.h"  // Hyperion
#include "../port/render/port_present.h"  // Hyperion
#include "../port/render/port_storm.h"  // Hyperion: PortBltRop3
#include "../port/render/port_view.h"  // Hyperion: g_portView

// The Storm entries this TU reaches, all __stdcall. Each trailing comment is the
// entry's thunk, the IAT slot it jumps through and its ordinal.
extern "C" {
BOOL __stdcall SDrawLockSurface(DWORD dwArg0, DWORD dwArg1, void **ppBits,
                                int *pnPitch, DWORD dwFlags);  // 0x4D002E -> [0x4EB550] -> ord 350
BOOL __stdcall SDrawUnlockSurface(DWORD dwArg0, void *pBits, int nRects,
                                  const rect32_t *prc);  // 0x4D0034 -> [0x4EB54C] -> ord 356
BOOL __stdcall SBltROP3(void *pDest, const void *pSrc, int nWidth, int nHeight,
                        int nDestPitch, int nSrcPitch, DWORD dwArg6,
                        DWORD dwRop);  // 0x4D0316 -> [0x4EB478] -> ord 313
BOOL __stdcall STransCreateE(const void *pBits, int nWidth, int nHeight,
                             int nArg3, int nArg4, DWORD dwFlags,
                             HSTRANS *phTrans);  // 0x4D012A -> [0x4EB560] -> ord 445
BOOL __stdcall STransDelete(HSTRANS hTrans);  // 0x4D0130 -> [0x4EB4E8] -> ord 434
BOOL __stdcall STransBltUsingMask(void *pDest, const void *pSrc, int nDestPitch,
                                  int nSrcPitch, HSTRANS hMask);  // 0x4D0352 -> [0x4EB4A0] -> ord 432
BOOL __stdcall STransCombineMasks(HSTRANS hBase, HSTRANS hAdd, int nX, int nY,
                                  int nOp, HSTRANS *phOut);  // 0x4D0358 -> [0x4EB4A4] -> ord 443
BOOL __stdcall STransDuplicate(HSTRANS hTrans, HSTRANS *phOut);  // 0x4D035E -> [0x4EB4A8] -> ord 436
BOOL __stdcall STransInvertMask(HSTRANS hTrans, HSTRANS *phOut);  // 0x4D0364 -> [0x4EB4AC] -> ord 438
BOOL __stdcall STransSetDirtyArrayInfo(int nWidth, int nHeight, int nCellWidth,
                                       int nCellHeight);  // 0x4D036A -> [0x4EB4B0] -> ord 440
}  // extern "C"

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// The mask list, 0x50F0D4: its link.prev is at 0x50F0D8 and link.next, where
// every walk starts, at 0x50F0DC.
#ifdef OC_DLL_MODE
#define sgMaskList (*reinterpret_cast<BLTMASKLIST *>(OC_VA(0x50F0D4u)))
#else
extern BLTMASKLIST sgMaskList;
#endif

// The RTTI type descriptor of _BLTMASK at 0x510A44. create_mask hands its
// raw_name() to SMemAlloc as the allocation's owner tag.
#ifdef OC_DLL_MODE
#define gTypeInfo_BLTMASK (*reinterpret_cast<type_info *>(OC_VA(0x510A44u)))
#else
extern type_info gTypeInfo_BLTMASK;
#endif

// 0x6D0B8C -- the pointer to the back buffer's pixels, spelled as mask.cpp
// spells it.
#ifndef OC_DWORD_6D0B8C
#define OC_DWORD_6D0B8C
// It is _img_draw_buf.pPixels, bmp_t's +4, and is spelled as the field, since
// bmp_t's layout changes at 64 bits.
#define dword_6D0B8C (_img_draw_buf.pPixels)
#endif

// The combined mask and its duplicate, 0x6D0C44 and 0x6D0C48, both HSTRANS.
// cursor.cpp spells the first identically.
#ifdef OC_DLL_MODE
  #ifndef sgConsoleMask_g
  #define sgConsoleMask_g (*reinterpret_cast<HSTRANS *>(OC_VA(0x6D0C44u)))
  #endif
#else
extern HSTRANS sgConsoleMask_g;
#endif

#ifdef OC_DLL_MODE
#define dword_6D0C48 (*reinterpret_cast<HSTRANS *>(OC_VA(0x6D0C48u)))
#else
extern HSTRANS dword_6D0C48;
#endif

// 0x6D0C4C -- set while the frame is dirty: vid_paint_invalid sets it, and
// vid_make_all_invalid clears it after repainting the whole screen.
#ifdef OC_DLL_MODE
#define dword_6D0C4C (*reinterpret_cast<DWORD *>(OC_VA(0x6D0C4Cu)))
#else
extern DWORD dword_6D0C4C;
#endif

// The DirectDraw primary surface, 0x6D3188, only tested against zero here, so
// a `void *`.
#ifdef OC_DLL_MODE
#define PrimarySurface (*reinterpret_cast<void **>(OC_VA(0x6D3188u)))
#else
extern void *PrimarySurface;
#endif

// The window state pair, 0x66FD24 and 0x66FD28, spelled as event_Win.cpp spells
// them. ghWnd is HWND because it is handed straight to IsIconic.
#ifdef OC_DLL_MODE
#define sgbWindowActive_l (*reinterpret_cast<DWORD *>(OC_VA(0x66FD24u)))
#else
extern DWORD sgbWindowActive_l;
#endif

#ifdef OC_DLL_MODE
#define ghWnd (*reinterpret_cast<HWND *>(OC_VA(0x66FD28u)))
#else
extern HWND ghWnd;
#endif

// The bodies, in oracle VA order.

// TSList<T,G>::Unlink, which 1.17 inlines into create_mask and vid_erase_mask.
// The link offset it recovers from the data is always zero here but computed
// anyway, and the inner guard is redundant: both are 1.17's shape, as in
// gametype.cpp's UnlinkTemplate.
static void UnlinkMask(BLTMASK *node)
{
  TSLink<BLTMASK> *pl = (node != 0) ? &node->link : &sgMaskList.link;
  BLTMASK *pprev = pl->prev;
  if (pprev != 0) {
    BLTMASK *pnext = pl->next;
    TSLink<BLTMASK> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<BLTMASK> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0) {
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      }
      pnl = reinterpret_cast<TSLink<BLTMASK> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = 0;
    pl->next = 0;
  }
}

// 0x4CB250 (84 B). Unlinks and frees every mask node. Normalising the head to
// null before the test keeps the loop unrotated.
void BLTMASKLIST::Clear(void)
{
  for (;;) {
    BLTMASK *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkMask(p);
    SMemFree(p, gTypeInfo_BLTMASK.raw_name(), -2, 0);
  }
}

// TSList::NewNode's placement new: the node's constructor is its link's.
static __forceinline BLTMASK *InitMask(BLTMASK *p)
{
  p->link.prev = 0;
  p->link.next = 0;
  return p;
}

// 0x4CB2B0 (217 B). Builds a mask out of the bitmap, appends it to the list and
// rebuilds the combined mask. The stores at the bottom run even when SMemAlloc
// returned null, a null dereference 1.17 has and this keeps.
intptr_t __fastcall create_mask(bmp_t *pBmp, int nArg1, int nArg2)
{
  HSTRANS hMask;

  // Hyperion: the port's masks (port_mask.cpp) in place of Storm's
  // STransCreateE(pixels, width, height, 8, 0, 0x1000000, &hMask).
  hMask = PortMaskCreate(pBmp->pPixels, pBmp->nWidth, pBmp->nHeight);
  void *pv = SMemAlloc(sizeof(BLTMASK), gTypeInfo_BLTMASK.raw_name(), -2, 8);
  BLTMASK *node = pv ? InitMask(static_cast<BLTMASK *>(pv)) : 0;
  TSLink<BLTMASK> *nl = (node != 0) ? &node->link : &sgMaskList.link;
  UnlinkMask(reinterpret_cast<BLTMASK *>(nl));
  BLTMASK *ptail = sgMaskList.link.prev;
  nl->prev = ptail;
  nl->next = ptail->link.next;
  ptail->link.next = node;
  sgMaskList.link.prev = reinterpret_cast<BLTMASK *>(nl);

  node->hMask = hMask;
  node->nLeft = nArg1;
  node->nTop = nArg2;
  node->nRight = nArg1 + pBmp->nWidth;
  node->nBottom = nArg2 + pBmp->nHeight;
  vid_mask_initialize();
  return (intptr_t)node;
}

// 0x4CB390 (114 B). Copies one rectangle of the back buffer straight onto the
// locked surface. Hyperion: the back buffer's rows are
// the view's width apart (port_view.cpp).
void __fastcall vid_copy_rect(const rect32_t *prc)
{
  void *pBits;
  int nPitch;

  // Hyperion: the screen is port_present.cpp's buffer, not Storm's
  // SDraw surface (SDrawLockSurface / SDrawUnlockSurface in the oracle).
  if (PortScreenLock(&pBits, &nPitch)) {
    // Hyperion: the port's copy (port_storm.cpp), in place of Storm's SBltROP3.
    PortBltRop3((BYTE *)pBits + prc->top * nPitch + prc->left,
                dword_6D0B8C + prc->top * g_portView.nWidth + prc->left,
                prc->right - prc->left, prc->bottom - prc->top, nPitch, g_portView.nWidth, 0,
                0x00CC0020);
    PortScreenCoverRect(prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top);
    PortScreenUnlock();
  }
}

// 0x4CB410 (75 B). Blits the whole back buffer through the duplicate mask.
void vid_draw_mask(void)
{
  void *pBits;
  int nPitch;

  // Hyperion: port_present.cpp's screen in place of Storm's surface.
  if (PortScreenLock(&pBits, &nPitch)) {
    PortMaskBlt(static_cast<BYTE *>(pBits), dword_6D0B8C, nPitch, g_portView.nWidth,
                dword_6D0C48);  // Hyperion: was STransBltUsingMask, the back buffer 640 wide
    PortScreenUnlock();
  }
}

// 0x4CB460 (150 B). Finds the mask in the list, unlinks it, invalidates the
// rectangle it covered and frees it, then rebuilds the combined mask. The walk
// is TSList's Next spelled out, each step normalising `<= 0` to NULL.
void __fastcall vid_erase_mask(intptr_t nMask)
{
  BLTMASK *p;

  p = sgMaskList.link.next;
  if ((intptr_t)p <= 0) {
    p = 0;
  }
  while (p != 0) {
    if (p == reinterpret_cast<BLTMASK *>(nMask)) {
      UnlinkMask(p);
      PortMaskDelete(p->hMask);  // Hyperion: was STransDelete
      img_area_invalid(p->nLeft, p->nTop, p->nRight, p->nBottom);
      p->hMask = 0;
      SMemFree(p, "vidblit.cpp", 0x8C, 0);
      break;
    }
    p = p->link.next;
    if ((intptr_t)p <= 0) {
      p = 0;
    }
  }
  vid_mask_initialize();
}

// 0x4CB500 (92 B). Drops both combined masks and every list node; the tail jmp
// reaches the list's Clear.
void vid_free(void)
{
  BLTMASK *p;

  // Hyperion: PortMaskDelete in place of Storm's STransDelete.
  if (sgConsoleMask_g != 0) {
    PortMaskDelete(sgConsoleMask_g);
    sgConsoleMask_g = 0;
  }
  if (dword_6D0C48 != 0) {
    PortMaskDelete(dword_6D0C48);
    dword_6D0C48 = 0;
  }

  p = sgMaskList.link.next;
  if ((intptr_t)p <= 0) {
    p = 0;
  }
  while (p != 0) {
    PortMaskDelete(p->hMask);
    p = p->link.next;
    if ((intptr_t)p <= 0) {
      p = 0;
    }
  }
  sgMaskList.Clear();
}


// 0x4CB560 (37 B). The pitch is stored only after the call, so the surface's
// own EAX survives as the return.
int __fastcall vid_lock_surface(vidlock_t *pLock)
{
  int nPitch;
  int nResult;

  nResult = PortScreenLock((void **)&pLock->pBits, &nPitch);  // Hyperion: was SDrawLockSurface
  pLock->nPitch = nPitch;
  return nResult;
}

// 0x4CB590 (89 B). Repaints the whole screen when it is dirty and the window is
// up. The dirty test is nested: a clean screen still returns 1.
int __fastcall vid_make_all_invalid(void)
{
  // Hyperion: the screen being up replaces the DirectDraw surface test,
  // and the frame is drawn whether the window is active or not. The oracle drew
  // nothing while inactive, when its exclusive surface was lost anyway, yet its
  // palette changes still went out: a screen that faded in behind another
  // window showed the previous screen's pixels in its colours.
  if (PortScreenIsUp() != 0 && ghWnd != 0 && IsIconic(ghWnd) == 0) {
    if (dword_6D0C4C != 0) {
      img_area_invalid(0, 0, g_portView.nWidth - 1, g_portView.nHeight - 1);  // Hyperion: 0x27F, 0x1DF
      dlg_set_all_update();
      dword_6D0C4C = 0;
    }
    return 1;
  }
  return 0;
}

// 0x4CB5F0 (200 B). Rebuilds the combined mask by inverting the first node's
// mask and folding every later one into it.
void vid_mask_initialize(void)
{
  BLTMASK *p;
  HSTRANS hCur;
  HSTRANS hNext;

  p = sgMaskList.link.next;
  hCur = 0;
  if ((intptr_t)p <= 0) {
    p = 0;
  }

  // Hyperion: the port's masks (port_mask.cpp) in place of Storm's
  // STrans functions; combining with operation 3 is a subtraction.
  if (sgConsoleMask_g != 0) {
    PortMaskDelete(sgConsoleMask_g);
    sgConsoleMask_g = 0;
  }
  if (dword_6D0C48 != 0) {
    PortMaskDelete(dword_6D0C48);
    dword_6D0C48 = 0;
  }

  if (p != 0) {
    // Hyperion: the view's screen less the first mask (the console's) at
    // its place (port_view.cpp). At 640x480, with the console at (0,0), that is
    // the inversion 1.17 made: PortMaskInvert(p->hMask). A benchmark's run hides
    // the console (port_console.cpp), and then the whole screen is the map's.
    HSTRANS hFull = PortMaskFull(g_portView.nWidth, g_portView.nHeight);
    if (PortConsoleMaskLeftOut(reinterpret_cast<intptr_t>(p)))
      hCur = PortMaskDuplicate(hFull);
    else
      hCur = PortMaskSubtract(hFull, p->hMask, p->nLeft, p->nTop);
    PortMaskDelete(hFull);
    p = p->link.next;
    if ((intptr_t)p <= 0) {
      p = 0;
    }
    while (p != 0) {
      hNext = PortMaskSubtract(hCur, p->hMask, p->nLeft, p->nTop);
      PortMaskDelete(hCur);
      hCur = hNext;
      p = p->link.next;
      if ((intptr_t)p <= 0) {
        p = 0;
      }
    }
  }

  sgConsoleMask_g = hCur;
  if (hCur != 0) {
    dword_6D0C48 = PortMaskDuplicate(hCur);
    PortMaskSetDirtyGrid(g_portView.nWidth, g_portView.nHeight, 16, 16);  // Hyperion: 640, 480
  }
}

// 0x4CB6C0 (11 B).
void vid_paint_invalid(void)
{
  dword_6D0C4C = 1;
}

// 0x4CB6D0 (21 B).
int __fastcall vid_unlock_surface(vidlock_t *pLock, int nFlag, rect32_t *prc)
{
  // Hyperion: was SDrawUnlockSurface(0, pLock->pBits, nFlag, prc); the
  // whole frame is presented once img_update's pass is done. What the dialog wrote is
  // the screen's own, not the GPU scene's (port_present.cpp).
  (void)pLock;
  (void)nFlag;
  if (prc != NULL)
    PortScreenCoverRect(prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top);
  else
    PortScreenCoverClear();
  return PortScreenUnlock();
}
