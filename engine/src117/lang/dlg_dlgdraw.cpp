// dlg_dlgdraw.cpp -- the dialog manager's invalidation and region layer. Every
// dialog repaint funnels through here: a control asks for its area to be
// invalidated (dlg_area_invalid), the rectangle is normalised into the owning
// dialog's coordinates, clipped to the dialog, then accumulated into a region
// (CombineRectInRgn) that the frame's final blit consumes. The 16-pixel
// alignment in CombineRectInRgn is the blitter's granularity. Bodies are in
// ascending oracle VA order.
#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>

#include "dialog.h"
#include "bmp.h"
#include "img.h"
#include "rect16.h"
#include "rect32.h"
#include "dlg_ctrl.h"      // dlg_area_invalid (DEFINED here), ctrl_set_update
#include "dlg_draw.h"      // draw_bmp_effect
#include "dlg_dlg.h"       // img_update (DEFINED here)
#include "dlg_dlgdraw.h"
#include "gds_image.h"
#include "gamemap.h"       // gamemap_priority_update
#include "dlg_effects.h"
#include "rtti.h"
#include "CtxtHelp.h"      // sub_419330, sub_419090, sub_4192B0
#include "cur.h"           // draw_cursor
#include "../port/render/port_present.h"  // Hyperion: PortPresentFrame
#include "../port/render/port_storm.h"  // Hyperion
#include "../port/render/port_console.h"  // Hyperion: PortConsoleHudHeld

// Storm ordinal 403. Declared here because Storm.h brings <windows.h>, whose
// SetRect collides with the rect32_t spelling below.
extern "C" int __stdcall SMemFree(void *ptr, const char *pszTag, int nLine, DWORD dwFlags);


#ifdef OC_DLL_MODE
#define gTypeInfo_CTRLNODE (*reinterpret_cast<type_info *>(OC_VA(0x510A2Cu)))
#else
extern type_info gTypeInfo_CTRLNODE;
#endif

static void UnlinkCtrlNode(CTRLNODE *p) {
  CTRLNODE *pprev = p->link.prev;
  if (pprev != 0) {
    CTRLNODE *pnext = p->link.next;
    TSLink<CTRLNODE> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<CTRLNODE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)&p->link - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<CTRLNODE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    p->link.prev->link.next = p->link.next;
    p->link.prev = 0;
    p->link.next = 0;
  }
}

// noinline: dlg_update_fcn calls it, and this TU would otherwise inline it.
__declspec(noinline) CTRLNODE *CTRLNODELIST::DeleteNode(CTRLNODE *pNode) {
  TSLink<CTRLNODE> *pl = pNode ? &pNode->link : &link;
  CTRLNODE *pRet = pl->next;
  if ((intptr_t)pRet <= 0)
    pRet = 0;
  UnlinkCtrlNode(pNode);
  SMemFree(pNode, gTypeInfo_CTRLNODE.raw_name(), -2, 0);
  return pRet;
}

void CTRLNODELIST::UnlinkAll(void) {
  for (;;) {
    CTRLNODE *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkCtrlNode(p);
  }
}

inline CTRLNODELIST::TSList() {
  _unmapped_00 = 0;
  link.prev = (CTRLNODE *)&link;
  link.next = (CTRLNODE *)~(intptr_t)&link;
}

inline CTRLNODELIST::~TSList() {
  UnlinkAll();
  UnlinkCtrlNode((CTRLNODE *)&link);
}

inline CTRLNODE::~CTRLNODE() {
  UnlinkCtrlNode(this);
}

inline void CTRLNODE::operator delete(void *p) {
  SMemFree(p, "delete", -1, 0);
}


#include "platform.h"  // DWORD

// The external surface.

// USER32!SetRect, reached through the IAT slot 0x4EB630, which the oracle's
// import directory binds by name. Declared here rather than through
// <windows.h>, for the header-light reason dlg_dlg.cpp declares GetTickCount
// locally; extern "C" + __stdcall + five dword parameters is what produces the
// `__imp__SetRect@20` the linker resolves out of user32.lib.
extern "C" __declspec(dllimport)
int __stdcall SetRect(rect32_t *prc, int xLeft, int yTop, int xRight,
                      int yBottom);

// Storm's regions: the accumulate-dirty-rectangles API. The handle type is
// Storm's HSRGN, spelled `void *` here. The shapes come from the oracle's call
// sites, the only evidence there is:
//   SRgnCreate      2-argument __stdcall, the first the address of the handle
//                   global (CombineRectInRgn).
//   SRgnCombineRect 4-argument __stdcall: handle, &rc, 0, 2.
//   SRgnDelete      1-argument __stdcall (dlg_rgn_free, img_update).
// Not derived: the return types, since every call site discards the result, and
// the meaning of the 0 and the 2. The oracle's SRgnDelete is a 6-byte IAT stub,
// so it is reached through an import library: src117/storm_imports.def binds it
// to Storm.dll ordinal 525, SRgnCreate to 524 and SRgnCombineRect to 523.
extern "C" {
void __stdcall SRgnCreate(void **phRgn, DWORD dwFlags);
void __stdcall SRgnDelete(void *hRgn);
void __stdcall SRgnCombineRect(void *hRgn, const rect32_t *prc, void *pParam,
                               DWORD dwOperation);
// Storm ordinal 529: `push rects / push &count / push handle`, no cleanup.
void __stdcall SRgnGetRects(void *hRgn, DWORD *pdwCount, rect32_t *pRects);
}

// Unported game callees. vid_make_all_invalid is `test eax, eax`-ed by
// img_update, so it returns a truth value.
int __fastcall vid_make_all_invalid(void);       // oracle 0x4CB590

// The game globals, in both spellings.

// gpDlgList -- ctrl_t * @0x6CE6D8 (dlg/dlg.cpp), head of the active-dialog
// chain, walked through ctrl_t::pNext by dlg_set_all_update,
// DlgCombineAllRects and UpdateCursorRect.
#ifdef OC_DLL_MODE
#define gpDlgList (*reinterpret_cast<ctrl_t**>(OC_VA(0x6CE6D8u)))
#else
extern ctrl_t *gpDlgList;
#endif

// sgpDlgTargetBuf_g -- bmp_t * @0x6CE778 (dlg/list.cpp). The type is derived:
// set_dlg_target_bitmap stores ECX here whole, and dlg_area_invalid and
// DlgCombineAllRects compare it against 0x6D0B88, which is _img_draw_buf, a
// bmp_t. A pointer whose only comparison is against a bmp_t's address is a
// bmp_t*.
#ifdef OC_DLL_MODE
#define sgpDlgTargetBuf_g (*reinterpret_cast<bmp_t**>(OC_VA(0x6CE778u)))
#else
extern bmp_t *sgpDlgTargetBuf_g;
#endif

// sgwRenderTypePercentage_g -- rendertype_t @0x6CE77C  (dlg/list.cpp).
// See rendertype_t in src117/h/dlg_dlgdraw.h: three shorts, and 0x6CE780 is its
// third member, not a global of its own.
#ifdef OC_DLL_MODE
#define sgwRenderTypePercentage_g (*reinterpret_cast<rendertype_t*>(OC_VA(0x6CE77Cu)))
#else
extern rendertype_t sgwRenderTypePercentage_g;
#endif

// sghRgnDlg_g -- void * @0x6CE784  (dlg/list.cpp). The accumulated dirty region
// handle; created lazily by CombineRectInRgn and torn down by dlg_rgn_free /
// img_update.
#ifdef OC_DLL_MODE
#define sghRgnDlg_g (*reinterpret_cast<void **>(OC_VA(0x6CE784u)))
#else
extern void *sghRgnDlg_g;
#endif

// sgrScreenValid_g -- rect32_t @0x50EF44 (dlg/dlgdraw.cpp). The extent is
// derived: img_update passes the bare address to ClipRect32, which dereferences
// +0/+4/+8/+0Ch, and UpdateCursorRect clamps against 0x50EF44/48/4Ch/50h in the
// same left/right/top/bottom pattern. The next name is 0x50EF54.
#ifdef OC_DLL_MODE
#define sgrScreenValid_g (*reinterpret_cast<rect32_t*>(OC_VA(0x50EF44u)))
#else
extern rect32_t sgrScreenValid_g;
#endif

// sgrScreenSize_g -- rect32_t @0x50EF54  (dlg/dlgdraw.cpp). Same evidence
// shape, from CombineRectInRgn's four clamps against 0x50EF54/58/5Ch/60h; the
// next name is 0x50EF64.
#ifdef OC_DLL_MODE
#define sgrScreenSize_g (*reinterpret_cast<rect32_t*>(OC_VA(0x50EF54u)))
#else
extern rect32_t sgrScreenSize_g;
#endif

// sgControlNodes_g -- the list of nodes queued for redraw @0x50EF64, the
// sheet's name; unregistered. create_ctrlnode addresses its link at 0x50EF68.
#ifdef OC_DLL_MODE
#define sgControlNodes_g (*reinterpret_cast<CTRLNODELIST *>(OC_VA(0x50EF64u)))
#else
extern CTRLNODELIST sgControlNodes_g;
#endif

// Storm ordinal 401, declared here for the reason SMemFree is.
extern "C" void *__stdcall SMemAlloc(DWORD cbSize, const char *pszTag, int nLine,
                                     DWORD dwFlags);

// dword_6CE788 -- ctrl_t * @0x6CE788 (dlg/list.cpp). Only ever dereferenced at
// +0x36, the dialog bitmap: `mov eax, [0x6CE788]` / `add eax, 36h`.
#ifdef OC_DLL_MODE
#define dword_6CE788 (*reinterpret_cast<ctrl_t **>(OC_VA(0x6CE788u)))
#else
extern ctrl_t *dword_6CE788;
#endif

// gdwNumDlgRects_g @0x6CE78C and gDlgRgnRects_g @0x6CE790 (dlg/list.cpp), the
// sheet's names; unregistered. The count SRgnGetRects fills, capped at 0xFF by
// dlg_update_fcn before the call, and the rectangles it writes.
#ifdef OC_DLL_MODE
#define gdwNumDlgRects_g (*reinterpret_cast<DWORD *>(OC_VA(0x6CE78Cu)))
#define gDlgRgnRects_g (reinterpret_cast<rect32_t *>(OC_VA(0x6CE790u)))
#else
extern DWORD gdwNumDlgRects_g;
extern rect32_t gDlgRgnRects_g[0xFF];
#endif

// s_cursorSurfaceUsed_g -- long @0x50EF70  (dlg/dlgdraw.cpp). UpdateCursorRect
// both tests it (`cmp dword ptr [0x50EF70], 0h`) and stores 0 / 1 into it.
#ifdef OC_DLL_MODE
#define s_cursorSurfaceUsed_g (*reinterpret_cast<LONG*>(OC_VA(0x50EF70u)))
#else
extern LONG s_cursorSurfaceUsed_g;
#endif

// sgCtxRect -- rect32_t @0x6CF788 (dlg/list.cpp). img_update reads and writes
// 0x6CF788/78Ch/790h/794h in the left/top/right/bottom pattern and overwrites
// all sixteen bytes at once with a `movdqu`, which pins the size as well as the
// order. The next name is 0x6CF798.
#ifdef OC_DLL_MODE
#define sgCtxRect (*reinterpret_cast<rect32_t*>(OC_VA(0x6CF788u)))
#else
extern rect32_t sgCtxRect;
#endif

// _img_draw_buf -- bmp_t @0x6D0B88 (gds/image.cpp). Referenced only by address,
// as the sentinel sgpDlgTargetBuf_g is compared against; 8 bytes wide from the
// next name, which is bmp_t's size.
#ifdef OC_DLL_MODE
#define _img_draw_buf (*reinterpret_cast<bmp_t*>(OC_VA(0x6D0B88u)))
#else
extern bmp_t _img_draw_buf;
#endif

// ClipRect16 -- intersect *prc with *prcClip in 16-bit coordinates and return
// non-zero if the result is empty. The two returns are separate on purpose: the
// oracle emits `mov eax, 1h` for the vertical test and `xor eax, eax /
// setnle al` for the horizontal one, which is `if (...) return 1; return (...);`
// and not one `||`, which would if-convert into a single setcc.
int __fastcall ClipRect16(rect16_t *prc, const rect16_t *prcClip)
{
  if (prc->left < prcClip->left)
    prc->left = prcClip->left;
  if (prc->right > prcClip->right)
    prc->right = prcClip->right;
  if (prc->top < prcClip->top)
    prc->top = prcClip->top;
  if (prc->bottom > prcClip->bottom)
    prc->bottom = prcClip->bottom;

  if (prc->top > prc->bottom)
    return 1;
  return prc->left > prc->right;
}

// ClipRect32 -- ClipRect16's 32-bit twin, instruction for instruction.
int __fastcall ClipRect32(rect32_t *prc, const rect32_t *prcClip)
{
  if (prc->left < prcClip->left)
    prc->left = prcClip->left;
  if (prc->right > prcClip->right)
    prc->right = prcClip->right;
  if (prc->top < prcClip->top)
    prc->top = prcClip->top;
  if (prc->bottom > prcClip->bottom)
    prc->bottom = prcClip->bottom;

  if (prc->top > prc->bottom)
    return 1;
  return prc->left > prc->right;
}

// context_in_rect -- does _img_list[1], the context image, overlap *prc? Four
// separating-axis rejections; the extent form (x + w - 1) is what makes the
// comparisons inclusive.
int __fastcall context_in_rect(const rect16_t *prc)
{
  int nX = _img_list[1].nX;
  int nY = _img_list[1].nY;

  if (nX + _img_list[1].nWidth - 1 < prc->left || nX > prc->right
      || nY + _img_list[1].nHeight - 1 < prc->top || nY > prc->bottom)
    return 0;
  return 1;
}

// cursor_in_rect -- the same test against _img_list[0], the cursor image. Byte
// for byte context_in_rect with the element index changed, which is itself
// corroboration that the two records are elements of one array.
int __fastcall cursor_in_rect(const rect16_t *prc)
{
  int nX = _img_list[0].nX;
  int nY = _img_list[0].nY;

  if (nX + _img_list[0].nWidth - 1 < prc->left || nX > prc->right
      || nY + _img_list[0].nHeight - 1 < prc->top || nY > prc->bottom)
    return 0;
  return 1;
}

// Links pNode at the tail of pList, unlinking it first.
static __forceinline void ctrlnode_link(CTRLNODELIST *pList, CTRLNODE *pNode)
{
  TSLink<CTRLNODE> *pLink = pNode != 0 ? &pNode->link : &pList->link;
  TSLink<CTRLNODE> *pTail;

  if (pLink->prev != 0) {
    CTRLNODE *pNext = pLink->next;
    TSLink<CTRLNODE> *pSlot;

    if ((intptr_t)pNext <= 0) {
      pSlot = (TSLink<CTRLNODE> *)~(intptr_t)pNext;
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pNext > 0)
        nOff = (intptr_t)pLink - (intptr_t)pLink->prev->link.next;
      pSlot = (TSLink<CTRLNODE> *)((intptr_t)pNext + nOff);
    }
    pSlot->prev = pLink->prev;
    pLink->prev->link.next = pLink->next;
    pLink->prev = 0;
    pLink->next = 0;
  }
  pTail = (TSLink<CTRLNODE> *)pList->link.prev;
  pLink->prev = (CTRLNODE *)pTail;
  pLink->next = pTail->next;
  pTail->next = pNode;
  pList->link.prev = (CTRLNODE *)pLink;
}

// The node after p, or NULL at the terminator.
static __forceinline CTRLNODE *ctrlnode_next(CTRLNODE *p)
{
  CTRLNODE *pNext = p->link.next;

  if ((intptr_t)pNext <= 0)
    pNext = 0;
  return pNext;
}

// Allocates a redraw node and links it at the tail of pList.
static __forceinline CTRLNODE *ctrlnode_push(CTRLNODELIST *pList)
{
  CTRLNODE *pNode = (CTRLNODE *)SMemAlloc(sizeof(CTRLNODE),
                                          gTypeInfo_CTRLNODE.raw_name(), -2, 8);

  if (pNode != 0) {
    pNode->link.prev = 0;
    pNode->link.next = 0;
  } else {
    pNode = 0;
  }
  ctrlnode_link(pList, pNode);
  return pNode;
}

// 0x4C4910. Queues pDlg for redraw when it overlaps prc, then every visible
// child that overlaps it, clearing each one's redraw flag.
int __fastcall create_ctrlnode(ctrl_t *pDlg, const rect32_t *prc)
{
  ctrl_t *pCtrl;

  if (pDlg->rc.left > prc->right || pDlg->rc.top > prc->bottom ||
      pDlg->rc.right < prc->left || pDlg->rc.bottom < prc->top)
    return 0;
  ctrlnode_push(&sgControlNodes_g)->pCtrl = pDlg;
  for (pCtrl = pDlg->pChildren; pCtrl != 0; pCtrl = pCtrl->pNext) {
    if (pCtrl->dwFlags & 8) {
      int nX = pCtrl->pParent->rc.left;
      int nY = pCtrl->pParent->rc.top;

      if (pCtrl->rc.left + nX <= prc->right && pCtrl->rc.top + nY <= prc->bottom &&
          pCtrl->rc.right + nX >= prc->left && pCtrl->rc.bottom + nY >= prc->top)
        ctrlnode_push(&sgControlNodes_g)->pCtrl = pCtrl;
    }
    pCtrl->dwFlags &= ~1u;
  }
  pDlg->dwFlags &= ~1u;
  return 1;
}

// dlg_area_invalid -- invalidate a control's area. The opening idiom is the one
// dialog.h describes: a node whose wType is 0 is the dialog and contributes no
// origin, anything else is a control and is offset by its owning dialog's
// top-left. Two oracle behaviours are matched as-is rather than repaired: the
// dialog's 0x10000000 flag gates only the img_area_invalid call, where it reads
// as though it should gate CombineRectInRgn too, and the clipped rectangle is
// written back into the local before the emptiness test, so an inverted
// rectangle survives the clamps and is rejected here rather than earlier.
void __fastcall dlg_area_invalid(ctrl_t *pCtrl)
{
  rect32_t rcDlg;
  rect32_t rcCtrl;
  ctrl_t *pDlg;
  LONG nXOfs = 0;
  LONG nYOfs = 0;

  if (pCtrl->wType == 0) {
    pDlg = pCtrl;
  } else {
    pDlg = pCtrl->pParent;
    nXOfs = pDlg->rc.left;
    nYOfs = pDlg->rc.top;
  }

  SetRect(&rcDlg, pDlg->rc.left, pDlg->rc.top, pDlg->rc.right, pDlg->rc.bottom);
  SetRect(&rcCtrl, pCtrl->rc.left, pCtrl->rc.top, pCtrl->rc.right,
          pCtrl->rc.bottom);

  rcCtrl.left += nXOfs;
  rcCtrl.right += nXOfs;
  rcCtrl.top += nYOfs;
  rcCtrl.bottom += nYOfs;

  if (rcCtrl.left < rcDlg.left)
    rcCtrl.left = rcDlg.left;
  if (rcCtrl.right > rcDlg.right)
    rcCtrl.right = rcDlg.right;
  if (rcCtrl.top < rcDlg.top)
    rcCtrl.top = rcDlg.top;
  if (rcCtrl.bottom > rcDlg.bottom)
    rcCtrl.bottom = rcDlg.bottom;

  if (rcCtrl.top <= rcCtrl.bottom && rcCtrl.left <= rcCtrl.right) {
    if ((pDlg->dwFlags & 0x10000000)
        && sgpDlgTargetBuf_g == &_img_draw_buf)
      img_area_invalid(rcCtrl.left, rcCtrl.top, rcCtrl.right, rcCtrl.bottom);
    CombineRectInRgn(&rcCtrl);
  }
}

// UpdateCursorRect -- the cursor image moved, appeared or vanished: work out
// whether any visible dialog overlaps its rectangle and, if one does, add that
// rectangle to the dirty region. s_cursorSurfaceUsed_g mirrors
// _img_list[0].bInUse, and the early return when the cursor is not in use and
// the flag was already clear happens before any callee-saved register is
// pushed, so the oracle has two epilogues. The inner loop clobbers the outer
// loop's registers and the oracle reloads rc from the stack to recover: `rc` has
// its address taken, so it lives in memory and MSVC promotes its members into
// registers only where it can.
void __fastcall UpdateCursorRect(void)
{
  rect32_t rc;
  ctrl_t *pDlg;
  ctrl_t *pCtrl;

  if (_img_list[0].bInUse == 0) {
    if (s_cursorSurfaceUsed_g == 0)
      return;
    s_cursorSurfaceUsed_g = 0;
  } else {
    s_cursorSurfaceUsed_g = 1;
  }

  SetRect(&rc, _img_list[0].nX, _img_list[0].nY,
          _img_list[0].nX + _img_list[0].nWidth - 1,
          _img_list[0].nY + _img_list[0].nHeight - 1);

  if (rc.left < sgrScreenValid_g.left)
    rc.left = sgrScreenValid_g.left;
  if (rc.right > sgrScreenValid_g.right)
    rc.right = sgrScreenValid_g.right;
  if (rc.top < sgrScreenValid_g.top)
    rc.top = sgrScreenValid_g.top;
  if (rc.bottom > sgrScreenValid_g.bottom)
    rc.bottom = sgrScreenValid_g.bottom;

  if (rc.top > rc.bottom)
    return;
  if (rc.left > rc.right)
    return;

  for (pDlg = gpDlgList; pDlg != 0; pDlg = pDlg->pNext) {
    if (!(pDlg->dwFlags & 0x8))
      continue;
    if (pDlg->rc.left > rc.right)
      continue;
    if (pDlg->rc.top > rc.bottom)
      continue;
    if (pDlg->rc.right < rc.left)
      continue;
    if (pDlg->rc.bottom < rc.top)
      continue;

    if ((pDlg->dwFlags & 0x2000) && pDlg->nResIndex == 0) {
      for (pCtrl = pDlg->pChildren; pCtrl != 0; pCtrl = pCtrl->pNext) {
        if (!(pCtrl->dwFlags & 0x8))
          continue;
        // The offsets come off the CHILD's parent pointer, not off pDlg. Same
        // object in practice; the oracle spells it `mov ecx, [eax+32h]` inside
        // the inner loop, so it is re-read per child.
        int nXOfs = pCtrl->pParent->rc.left;
        int nYOfs = pCtrl->pParent->rc.top;
        if (pCtrl->rc.left + nXOfs > rc.right)
          continue;
        if (pCtrl->rc.top + nYOfs > rc.bottom)
          continue;
        if (pCtrl->rc.right + nXOfs < rc.left)
          continue;
        if (pCtrl->rc.bottom + nYOfs < rc.top)
          continue;
        CombineRectInRgn(&rc);
        return;
      }
    } else {
      CombineRectInRgn(&rc);
      return;
    }
  }
}

// set_dlg_target_bitmap -- stores the bitmap the dialog layer draws into.
void __fastcall set_dlg_target_bitmap(bmp_t *pBmp)
{
  sgpDlgTargetBuf_g = pBmp;
}

// sub_4C4D50 -- a whole-object store into the 6-byte
// sgwRenderTypePercentage_g. See rendertype_t in src117/h/dlg_dlgdraw.h: the
// dword+word split is MSVC fusing two adjacent shorts, not evidence of a dword
// field.
void __fastcall sub_4C4D50(const rendertype_t *pSrc)
{
  sgwRenderTypePercentage_g = *pSrc;
}

// dlg_set_all_update -- marks every live dialog for update.
void __fastcall dlg_set_all_update(void)
{
  ctrl_t *pDlg;

  for (pDlg = gpDlgList; pDlg != 0; pDlg = pDlg->pNext)
    ctrl_set_update(pDlg);
}

// 0x4C4D90. Redraws the dirty region: every visible dialog is queued, and for
// each one, back to front and per dirty rectangle, the nodes create_ctrlnode
// queues are drawn at once or deferred to a local list by flag 0x100000; the
// dialogs are then repainted front to back over every rectangle.
void __fastcall dlg_update_fcn(unsigned short, unsigned short, bmp_t *, bmpsrc_t *)
{
  CTRLNODE *pNode;
  ctrl_t *pDlg;
  DWORD i;

  DlgCombineAllRects();
  CTRLNODELIST dirtyDlgs;
  for (pDlg = gpDlgList; pDlg != 0; pDlg = pDlg->pNext) {
    if (pDlg->dwFlags & 8)
      ctrlnode_push(&dirtyDlgs)->pCtrl = pDlg;
  }
  if (sghRgnDlg_g == 0) {
    gdwNumDlgRects_g = 0;
  } else {
    gdwNumDlgRects_g = 0xFF;
    // Hyperion: the port's region (port_storm.cpp), in place of Storm's SRgn functions.
    PortRgnGetRects(sghRgnDlg_g, &gdwNumDlgRects_g, gDlgRgnRects_g);
    for (i = 0; i < gdwNumDlgRects_g; i++) {
      gDlgRgnRects_g[i].right--;
      gDlgRgnRects_g[i].bottom--;
    }
  }
  for (pNode = dirtyDlgs.link.prev->link.prev->link.next; (intptr_t)pNode > 0;
       pNode = pNode->link.prev->link.prev->link.next) {
    pDlg = pNode->pCtrl;
    CTRLNODELIST deferred;
    for (i = 0; i < gdwNumDlgRects_g; i++) {
      if (create_ctrlnode(pDlg, &gDlgRgnRects_g[i])) {
        CTRLNODE *p = sgControlNodes_g.link.next;
        CTRLNODE *pNext;

        if ((intptr_t)p <= 0)
          p = 0;
        // Stepping pNext in the increment is what peels its first read.
        pNext = p != 0 ? ctrlnode_next(p) : 0;
        for (; p != 0; p = pNext, pNext = p != 0 ? ctrlnode_next(p) : 0) {
          UnlinkCtrlNode(p);
          if (p->pCtrl->dwFlags & 0x100000) {
            // Through a pointer, or the unlink's first load is hoisted over the store.
            rect32_t *prcDst = &p->rcDirty;
            *prcDst = gDlgRgnRects_g[i];
            ctrlnode_link(&deferred, p);
          } else {
            sub_4C51C0(p->pCtrl, &gDlgRgnRects_g[i]);
            delete p;
          }
        }
        dword_6CE788 = !(pDlg->dwFlags & 0x8000000) ? pDlg : dword_6CE788;
      }
    }
    for (CTRLNODE *p = deferred.link.next; (intptr_t)p > 0; p = deferred.DeleteNode(p))
      sub_4C51C0(p->pCtrl, &p->rcDirty);
  }
  for (pNode = dirtyDlgs.link.next; (intptr_t)pNode > 0;
       pNode = dirtyDlgs.DeleteNode(pNode)) {
    for (i = 0; i < gdwNumDlgRects_g; i++)
      sub_4C5630(pNode->pCtrl, &gDlgRgnRects_g[i]);
  }
  dword_6CE788 = 0;
}

// dlg_rgn_free -- drop the accumulated dirty region. The oracle inlines this
// whole 26-byte body into img_update's tail, which is the counter-evidence to
// the "/Ob1 would fix this TU" theory; see CombineRectInRgn below.
void __fastcall dlg_rgn_free(void)
{
  if (sghRgnDlg_g != 0) {
    // Hyperion: the port's region (port_storm.cpp), in place of Storm's SRgn functions.
    PortRgnDelete(sghRgnDlg_g);
    sghRgnDlg_g = 0;
  }
}

// _bmp_current -- void * @0x6D0C40 (gds/image.cpp), the bitmap drawing targets.
#ifdef OC_DLL_MODE
#define _bmp_current (*reinterpret_cast<void **>(OC_VA(0x6D0C40u)))
#else
extern void *_bmp_current;
#endif

// Internal copy of ClipRect16, same bytes. sub_4C51C0 keeps EDX across the
// call, which only an internal callee allows; the link folded the two copies
// into one address.
static __declspec(noinline) int __fastcall ClipRect16_local(rect16_t *prc, const rect16_t *prcClip)
{
  if (prc->left < prcClip->left)
    prc->left = prcClip->left;
  if (prc->right > prcClip->right)
    prc->right = prcClip->right;
  if (prc->top < prcClip->top)
    prc->top = prcClip->top;
  if (prc->bottom > prcClip->bottom)
    prc->bottom = prcClip->bottom;

  if (prc->top > prc->bottom)
    return 1;
  return prc->left > prc->right;
}

// Moves a rectangle by (nDX, nDY).
static void OffsetRect32(rect32_t *prc, int nDX, int nDY)
{
  prc->left += nDX;
  prc->right += nDX;
  prc->top += nDY;
  prc->bottom += nDY;
}

// sub_4C51C0 -- redraws one node into its dialog's bitmap: the dirty rectangle
// is clipped to the dialog and the screen, turned into dialog coordinates and
// handed to the node's draw handler. It calls ClipRect16_local, whose EDX
// survives the call.
void __fastcall sub_4C51C0(ctrl_t *pCtrl, const rect32_t *prcDirty)
{
  void *pSave = _bmp_current;
  ctrl_t *pDlg;
  rect32_t rc;
  rect32_t rcDlg;
  rect16_t rcClip;
  int nX, nY;
  if (pCtrl->wType == 0)
    pDlg = pCtrl;
  else
    pDlg = pCtrl->pParent;
  rc = *prcDirty;
  SetRect(&rcDlg, pDlg->rc.left, pDlg->rc.top, pDlg->rc.right, pDlg->rc.bottom);
  if (rc.left < rcDlg.left)
    rc.left = rcDlg.left;
  if (rc.right > rcDlg.right)
    rc.right = rcDlg.right;
  if (rc.top < rcDlg.top)
    rc.top = rcDlg.top;
  if (rc.bottom > rcDlg.bottom)
    rc.bottom = rcDlg.bottom;
  OffsetRect32(&rc, -pDlg->rc.left, -pDlg->rc.top);
  if (rc.left < sgrScreenValid_g.left)
    rc.left = sgrScreenValid_g.left;
  if (rc.right > sgrScreenValid_g.right)
    rc.right = sgrScreenValid_g.right;
  if (rc.top < sgrScreenValid_g.top)
    rc.top = sgrScreenValid_g.top;
  if (rc.bottom > sgrScreenValid_g.bottom)
    rc.bottom = sgrScreenValid_g.bottom;
  rcClip.left = (short)rc.left;
  rcClip.right = (short)rc.right;
  rcClip.top = (short)rc.top;
  rcClip.bottom = (short)rc.bottom;
  nX = rc.left;
  nY = rc.top;
  if (pCtrl != pDlg) {
    const rect16_t *prcCtrl = &pCtrl->rc;
    ClipRect16_local(&rcClip, prcCtrl);
    nX -= prcCtrl->left;
    if (nX < 0)
      nX = 0;
    nY -= pCtrl->rc.top;
    if (nY < 0)
      nY = 0;
  }
  switch (pCtrl->wType) {
  case 5:
    sub_4CA7E0((unsigned short)sgwRenderTypePercentage_g.w0, 0);
    break;
  case 0x0D:
    sub_4CA7E0((unsigned short)sgwRenderTypePercentage_g.w2, 0);
    break;
  }
  _bmp_current = &pDlg->_unmapped36[0];
  pCtrl->pfnDraw(pCtrl, nX, nY, &rcClip);
  _bmp_current = pSave;
}

// dlg_draw_in_rect -- blits the dialog's own bitmap payload, which lives at
// +0x36 rather than in the ctrl_t +0x0C slot the per-control renderers use. The
// offsets handed to draw_bmp_effect are the clip rectangle's origin relative to
// the node, so both are a subtraction and not a sum.
void __fastcall dlg_draw_in_rect(ctrl_t *pDlg, const rect16_t *prc)
{
  draw_bmp_effect(reinterpret_cast<bmp_t *>(&pDlg->_unmapped36[0]),
                  prc->left - pDlg->rc.left, prc->top - pDlg->rc.top,
                  const_cast<rect16_t *>(prc), pDlg->dwFlags);
}

// img_update -- the frame's invalidation pass: refresh the map priority buffer,
// ask the video layer whether anything changed at all, then reconcile the
// context image (_img_list[1]) against the rectangle it occupied last frame.
// Both the old area and the new one have to be repainted, so sgCtxRect is
// combined in and then overwritten with the current rectangle. The `else` arm is
// the case where the context image just disappeared: there is no new rectangle,
// so only the remembered one is dirtied, and sgCtxRect is marked empty by
// setting top = bottom + 1 rather than by zeroing it, which is what makes the
// arm open by testing whether all four members are already zero and do nothing
// if so. dlg_rgn_free is inlined by the oracle at the tail, the 26 bytes
// at +0x10B being byte-identical to its body.
void __fastcall img_update(void)
{
  rect32_t rc;

  gamemap_priority_update();
  if (vid_make_all_invalid()) {
    if (_img_list[1].bInUse) {
      // A `short` local, and the width matters: the oracle keeps it in SI with
      // `mov si, word ptr [0x6D0BAA]` / `test si, si` and re-widens it with
      // `movsx eax, si` for the arithmetic. An `int` local would load `movsx`
      // once and never show the 16-bit register.
      short nWidth = _img_list[1].nWidth;
      if (nWidth != 0) {
        SetRect(&rc, _img_list[1].nX, _img_list[1].nY,
                _img_list[1].nX + nWidth - 1,
                _img_list[1].nY + _img_list[1].nHeight - 1);
        ClipRect32(&rc, &sgrScreenValid_g);
        if (rc.left < rc.right && rc.top < rc.bottom)
          CombineRectInRgn(&rc);
        if (sgCtxRect.left < sgCtxRect.right && sgCtxRect.top < sgCtxRect.bottom)
          CombineRectInRgn(&sgCtxRect);
        sgCtxRect = rc;
      } else if (sgCtxRect.left != 0 || sgCtxRect.right != 0
                 || sgCtxRect.top != 0 || sgCtxRect.bottom != 0) {
        if (sgCtxRect.left < sgCtxRect.right && sgCtxRect.top < sgCtxRect.bottom)
          CombineRectInRgn(&sgCtxRect);
        sgCtxRect.top = sgCtxRect.bottom + 1;
      }
    }
    img_update_real();
  }
  dlg_rgn_free();
  PortPresentFrame();  // Hyperion: the pass is done; show the frame (port_present.cpp)
}

// CombineRectInRgn -- add one rectangle to the frame's dirty region, aligned out
// to the blitter's 16-pixel granularity and clipped to the screen. The region is
// created lazily on first use. The `rc = *prc` copy is a real local copy, not a
// convenience: the caller's rectangle is left untouched by the alignment.
//
// The `__declspec(noinline)` is a steering lever, not a derivation. Without it
// MSVC inlines this body into CombineDlgInRgn's child loop, taking that from
// 153 B to 313 B, because the inliner weights a call site by loop depth.
// Definition order does not change that -- MSVC 2013 defers codegen to the end
// of the TU -- and /Ob1, which does fix this body, breaks img_update instead,
// whose tail is 26 bytes byte-identical to dlg_rgn_free's whole body.
__declspec(noinline) void __fastcall CombineRectInRgn(const rect32_t *prc)
{
  rect32_t rc = *prc;

  rc.left = rc.left & -16;
  rc.right = (rc.right + 16) & -16;
  rc.top = rc.top & -16;
  rc.bottom = (rc.bottom + 16) & -16;

  if (rc.left < sgrScreenSize_g.left)
    rc.left = sgrScreenSize_g.left;
  if (rc.right > sgrScreenSize_g.right)
    rc.right = sgrScreenSize_g.right;
  if (rc.top < sgrScreenSize_g.top)
    rc.top = sgrScreenSize_g.top;
  if (rc.bottom > sgrScreenSize_g.bottom)
    rc.bottom = sgrScreenSize_g.bottom;

  // Hyperion: the port's region (port_storm.cpp), in place of Storm's SRgn functions.
  if (sghRgnDlg_g == 0)
    sghRgnDlg_g = PortRgnCreate();
  PortRgnAddRect(sghRgnDlg_g, &rc);
}

// DlgCombineAllRects -- every visible dialog carrying the 0x10000000 bit
// contributes its rectangles to the frame's region. The img_area_is_invalid
// short-circuit applies only when the dialog layer is drawing into
// _img_draw_buf; against any other target the dialog always contributes.
void __fastcall DlgCombineAllRects(void)
{
  ctrl_t *pDlg;

  for (pDlg = gpDlgList; pDlg != 0; pDlg = pDlg->pNext) {
    if ((pDlg->dwFlags & 0x10000000) && (pDlg->dwFlags & 0x8)) {
      if (sgpDlgTargetBuf_g == &_img_draw_buf
          && img_area_is_invalid(pDlg->rc.left, pDlg->rc.top, pDlg->rc.right,
                                 pDlg->rc.bottom))
        CombineDlgInRgn(pDlg);
    }
  }
}

// CombineDlgInRgn -- the dialog's own rectangle plus every visible child's, each
// child's shifted into screen coordinates by the dialog's top-left. The first
// guard reads oddly and is transcribed as the oracle has it:
// `test dword ptr [edi+18h], 2000h / jz` falls through to the body when the bit
// is clear, and only when it is set does the +0x10 test get to veto. Its size is
// why CombineRectInRgn carries `__declspec(noinline)`; see there.
void __fastcall CombineDlgInRgn(ctrl_t *pDlg)
{
  rect32_t rc;
  ctrl_t *pCtrl;

  if (!(pDlg->dwFlags & 0x2000) || pDlg->nResIndex != 0) {
    SetRect(&rc, pDlg->rc.left, pDlg->rc.top, pDlg->rc.right, pDlg->rc.bottom);
    CombineRectInRgn(&rc);
  }

  for (pCtrl = pDlg->pChildren; pCtrl != 0; pCtrl = pCtrl->pNext) {
    if (pCtrl->dwFlags & 0x8) {
      SetRect(&rc, pCtrl->rc.left, pCtrl->rc.top, pCtrl->rc.right,
              pCtrl->rc.bottom);
      // Both offsets are read into locals before the four adds, and both
      // declarations sit after the SetRect call: the oracle emits the two `movsx`
      // loads back to back and then four memory RMWs, so both are materialised
      // before the first add. Interleaving the reads with the adds, the obvious
      // `rc.top += pDlg->rc.top;` spelling, costs the same bytes but schedules
      // them differently.
      int nXOfs = pDlg->rc.left;
      int nYOfs = pDlg->rc.top;
      rc.left += nXOfs;
      rc.right += nXOfs;
      rc.top += nYOfs;
      rc.bottom += nYOfs;
      CombineRectInRgn(&rc);
    }
  }
}


// ClipRect16 with the emptiness test as one `||`: the oracle inlines this into
// sub_4C5630 and branches on each half, where ClipRect16's own second return
// is materialised with setg.
static __forceinline int ClipRect16Inline(rect16_t *prc, const rect16_t *prcClip)
{
  if (prc->left < prcClip->left)
    prc->left = prcClip->left;
  if (prc->right > prcClip->right)
    prc->right = prcClip->right;
  if (prc->top < prcClip->top)
    prc->top = prcClip->top;
  if (prc->bottom > prcClip->bottom)
    prc->bottom = prcClip->bottom;
  return prc->top > prc->bottom || prc->left > prc->right;
}

// sub_4C5630 -- repaints one dialog's bitmap inside a dirty rectangle. Flags
// 0x10000000 and 0x8000000 blit straight into another target; otherwise the
// context bubble and the cursor are lifted off around the blit. The bitmap
// pointer is taken inside each arm, which is what keeps it in ESI.
void __fastcall sub_4C5630(ctrl_t *pDlg, const rect32_t *prcDirty)
{
  rect16_t rc;
  bmp_t *pBmp;
  int bHidden;

  rc.left = (short)prcDirty->left;
  rc.top = (short)prcDirty->top;
  rc.right = (short)prcDirty->right;
  rc.bottom = (short)prcDirty->bottom;
  if (ClipRect16Inline(&rc, &pDlg->rc))
    return;
  // Hyperion: a dialog the HUD's slots show is not put on the screen (port_console.cpp)
  if (PortConsoleHudHeld(pDlg))
    return;
  if (pDlg->dwFlags & 0x10000000) {
    void *pSave;

    sub_4CA7E0((unsigned short)sgwRenderTypePercentage_g.w4, 0);
    pSave = _bmp_current;
    _bmp_current = sgpDlgTargetBuf_g;
    draw_bmp_effect(reinterpret_cast<bmp_t *>(&pDlg->_unmapped36[0]),
                    rc.left - pDlg->rc.left, rc.top - pDlg->rc.top, &rc,
                    pDlg->dwFlags);
    _bmp_current = pSave;
    return;
  }
  if (pDlg->dwFlags & 0x8000000) {
    void *pSave;

    sub_4CA7E0((unsigned short)sgwRenderTypePercentage_g.w4, 0);
    pSave = _bmp_current;
    _bmp_current = &dword_6CE788->_unmapped36[0];
    draw_bmp_effect(reinterpret_cast<bmp_t *>(&pDlg->_unmapped36[0]),
                    rc.left - pDlg->rc.left, rc.top - pDlg->rc.top, &rc,
                    pDlg->dwFlags);
    _bmp_current = pSave;
    return;
  }
  bHidden = 0;
  if (context_in_rect(&rc)) {
    sub_419330(reinterpret_cast<bmp_t *>(&pDlg->_unmapped36[0]), pDlg->rc.left,
               pDlg->rc.top);
    sub_419090(reinterpret_cast<bmp_t *>(&pDlg->_unmapped36[0]));
    bHidden = 1;
  }
  set_request_render();
  if (cursor_in_rect(&rc) && _img_list[0].bInUse) {
    pBmp = reinterpret_cast<bmp_t *>(&pDlg->_unmapped36[0]);
    save_behind_cursor(pBmp, pDlg->rc.left, pDlg->rc.top);
    draw_cursor(reinterpret_cast<intptr_t>(pBmp), pDlg->rc.left, pDlg->rc.top);
    dlg_draw_in_rect(pDlg, &rc);
    restore_cursor_area(pBmp, pDlg->rc.left, pDlg->rc.top);
  } else {
    pBmp = reinterpret_cast<bmp_t *>(&pDlg->_unmapped36[0]);
    draw_bmp_effect(pBmp, rc.left - pDlg->rc.left, rc.top - pDlg->rc.top, &rc,
                    pDlg->dwFlags);
  }
  clear_request_render();
  if (bHidden)
    sub_4192B0(pBmp, pDlg->rc.left, pDlg->rc.top);
}
