// dlg_flc.cpp -- the FLC control: a list of Smacker elements blended over the
// dialog, plus the two event handlers that drive them. Oracle 0x4C8150-0x4C8CD6,
// bodies in VA order.
//
// A wType == 0x0E node's +0x42 slot heads a list of flc_t elements, not of
// ctrl_t children; +0x36 is the control's own payload rect, +0x3E its blend
// level and +0x40 a frame delay. dialog.h leaves all three unnamed because they
// mean something else for every other control type, so they are reached here
// through TU-local macros.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include <string.h>            // memset -- flc_init_flc clears the element bitmap

#include "../h/dlg_flc.h"
#include "../h/dlg_effects.h"
#include "../h/bmp.h"
#include "../h/dialog.h"
#include "../h/rect16.h"
#include "../h/point16.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_draw.h"
#include "../h/glues.h"
#include "../h/gluRdy.h"       // bmp_create
#include "../h/minimap.h"
#include "../h/Storm.h"
#include "../gen/globals117.h"

// The two stack blocks flc_init_flc builds for the video layer, four dwords and
// two. Their roles are not known, so the names stay positional.
struct vidblock4_t { int n00, n04, n08, n0C; };
struct vidblock2_t { int n00, n04; };
static_assert(sizeof(vidblock4_t) == 0x10, "vidblock4_t  lea eax,[ebp-24h] .. lea eax,[ebp-14h]");
static_assert(sizeof(vidblock2_t) == 0x08, "vidblock2_t  lea eax,[ebp-14h] .. [ebp-0Ch]");

// Callees declared here rather than in a header, spelled as
// src117/lang/dlg_draw.cpp spells them.

// font_adjust -- oracle 0x4CDF20. ECX = x, EDX = y, one stack dword = the
// string. The coordinates are signed ints, not shorts: both are sign-extended
// from the control rect and added to a 32-bit offset.
const char * __fastcall font_adjust(int nX, int nY, const char *psz);

// appcritf -- oracle 0x4CC190, __cdecl; here the format is "Could not load "%s"".
void __cdecl appcritf(const char *pszFmt, ...);

// Globals this TU reaches, dual-spelled.

// gCursorPos_g -- LONG[2] at 0x6D3154 (gds/mou_win.cpp); 0x6D3158 is element
// [1], not a global of its own.
#ifdef OC_DLL_MODE
#define gCursorPos_g (reinterpret_cast<LONG*>(OC_VA(0x6D3154u)))
#else
extern LONG gCursorPos_g[2];
#endif

// gpDlgList -- ctrl_t * @0x6CE6D8 (dlg/dlg.cpp), head of the live dialog list.
#ifdef OC_DLL_MODE
#define gpDlgList (*reinterpret_cast<ctrl_t**>(OC_VA(0x6CE6D8u)))
#else
extern ctrl_t *gpDlgList;
#endif

// gnDlgReturnVal -- long @0x6CE6DC (dlg/dlg.cpp). Signed: the sentinels are
// negative.
#ifdef OC_DLL_MODE
#define gnDlgReturnVal (*reinterpret_cast<LONG*>(OC_VA(0x6CE6DCu)))
#else
extern LONG gnDlgReturnVal;
#endif

// gbFontJust -- BYTE @0x6D300E (gds/font.cpp)
#ifdef OC_DLL_MODE
#define gbFontJust (*reinterpret_cast<dlg_byte_t*>(OC_VA(0x6D300Eu)))
#else
extern dlg_byte_t gbFontJust;
#endif

// gbFontCharExtra -- BYTE @0x6D3035 (gds/font.cpp)
#ifdef OC_DLL_MODE
#define gbFontCharExtra (*reinterpret_cast<dlg_byte_t*>(OC_VA(0x6D3035u)))
#else
extern dlg_byte_t gbFontCharExtra;
#endif

// gsFontMargins -- rect16_t @0x6D3038 (gds/font.cpp). gsFontMargins+4 is the
// rect's right/bottom half, not a global of its own.
#ifdef OC_DLL_MODE
#define gsFontMargins (*reinterpret_cast<rect16_t*>(OC_VA(0x6D3038u)))
#else
extern rect16_t gsFontMargins;
#endif

// sgpFonts_g -- void*[4] @0x6D3024 (gds/font.cpp)
#ifdef OC_DLL_MODE
#define sgpFonts_g (reinterpret_cast<void**>(OC_VA(0x6D3024u)))
#else
extern void *sgpFonts_g[4];
#endif

// The FLC arm of the per-type payload dialog.h leaves unnamed. +0x36 is a
// rect16_t, +0x3E the blend level and +0x40 a frame delay, both WORDs inside
// what dialog.h models as the dialog-only pFocus pointer, and +0x4E the
// justification byte draw_flc copies into gbFontJust.
#define flc_rect(p)  (reinterpret_cast<rect16_t *>(&(p)->_unmapped36[0]))
#define flc_level(p) (*reinterpret_cast<short *>(&(p)->pFocus))
#define flc_delay(p) (*(reinterpret_cast<dlg_word_t *>(&(p)->pFocus) + 1))
#define flc_just(p)  ((p)->_unmapped4E[0])

// The two .rdata literals flc_init_flc pushes: 1.17's own TU name at 0x4F11C8
// and appcritf's format at 0x4F11B4.
#define OC_FLC_FILE "flc.cpp"
#define OC_FLC_LOADFAIL "Could not load \"%s\""

#define FLC_PRESSED 0x40000000u
#define FLC_TIMER_ID 0x48

// 0x4C8150 (162 B). The FLC draw handler ctrl_flc_init installs at +0x2E. When
// the mouse is over the payload rect every element draws; otherwise the ones
// carrying bit 3 are skipped.
void draw_flc(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *pClip)
{
  point16_t pt;
  flc_t *pItem;

  pt.x = static_cast<short>(gCursorPos_g[0]);
  pt.y = static_cast<short>(gCursorPos_g[1]);
  pItem = reinterpret_cast<flc_t *>(pCtrl->pChildren);
  if ((pCtrl->dwFlags & 0x80) != 0 && pt_in_rect(flc_rect(pCtrl), &pt) != 0) {
    while (pItem != 0) {
      flc_draw_flc(pCtrl, pItem, nXOfs, nYOfs, pClip);
      pItem = pItem->pNext;
    }
  } else {
    while (pItem != 0) {
      if ((pItem->dwFlags & 8) == 0)
        flc_draw_flc(pCtrl, pItem, nXOfs, nYOfs, pClip);
      pItem = pItem->pNext;
    }
  }
  if (pCtrl->pData != 0) {
    gbFontJust = flc_just(pCtrl);
    flc_draw_text(pCtrl, flc_level(pCtrl), pClip);
  }
}

// 0x4C8200 (175 B). Relocate the payload rect into dialog coordinates, publish
// the centre-derived frame delay, start every element and install draw_flc.
// Only the head element takes its live bit from the control's flags; the rest
// are enabled unconditionally and get the 30 rest level when they ask for it.
void ctrl_flc_init(ctrl_t *pCtrl)
{
  dlg_word_t wLeft = static_cast<dlg_word_t>(pCtrl->rc.left);
  dlg_word_t wTop = static_cast<dlg_word_t>(pCtrl->rc.top);
  int nLeft;
  flc_t *pItem;

  flc_rect(pCtrl)->top += wTop;
  flc_rect(pCtrl)->bottom += wTop;
  flc_rect(pCtrl)->left += wLeft;
  flc_rect(pCtrl)->right += wLeft;

  nLeft = static_cast<short>(wLeft);
  pItem = reinterpret_cast<flc_t *>(pCtrl->pChildren);
  flc_delay(pCtrl) = static_cast<dlg_word_t>(
      ((pCtrl->rc.right - nLeft) / 2 + nLeft - 320) * 1000 / 320);

  if (pItem != 0) {
    pItem->dwFlags |= (pCtrl->dwFlags >> 9) & 0x10;
    for (;;) {
      if (pItem->pszFile != 0)
        flc_init_flc(pItem);
      else
        pItem->hVid = 0;
      pItem = pItem->pNext;
      if (pItem == 0)
        break;
      if (pItem->dwFlags & 2)
        pItem->wLevel = 30;
      pItem->dwFlags |= 0x10;
    }
  }

  flc_just(pCtrl) = static_cast<dlg_byte_t>(pCtrl->dwFlags >> 20);
  pCtrl->pfnDraw = draw_flc;
  ctrl_set_update(pCtrl);
}

// 0x4C82B0 (60 B). The variant installed on an FLC that only tracks hover: it
// starts and stops the elements and leaves everything else to the dialog.
int ctrl_flc_only_mouseover(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag != 0x0E)
    return 0;
  switch (pEvent->dwId) {
  case 10:
    ctrl_flc_init(pCtrl);
    return ctrl_evn_dlg(pCtrl, pEvent);
  case 1:
    ctrl_flc_deactivate(pCtrl);
    return ctrl_evn_dlg(pCtrl, pEvent);
  }
  return 0;
}

// 0x4C82F0 (147 B). The full FLC handler. Mouse events track the payload rect;
// everything else goes to ctrl_flc_evn or is refused.
int ctrl_flc(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  switch (pEvent->wTag) {
  case 0x0E:
    return ctrl_flc_evn(pCtrl, pEvent);

  case 3:
    flc_mouse_update(pCtrl, pEvent, flc_rect(pCtrl));
    return 1;

  case 4:
  case 6:
    dlg_hook(pCtrl, 5);
    dlg_hook(pCtrl, 3);
    flc_mouse_update(pCtrl, pEvent, flc_rect(pCtrl));
    return 1;

  case 5:
    dlg_unhook_all(pCtrl);
    flc_mouse_update(pCtrl, pEvent, flc_rect(pCtrl));
    ctrl_check_mouse_in(pCtrl->pParent, pEvent);
    flc_clicked(pCtrl);
    return 1;
  }
  return 0;
}

// 0x4C83B0 (147 B). ctrl_flc against the control's own rect instead of the
// payload rect, for the button-glow variant.
int ctrl_btnglow_flc(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  switch (pEvent->wTag) {
  case 0x0E:
    return ctrl_btnglow_flc_evn(pCtrl, pEvent);

  case 3:
    flc_mouse_update(pCtrl, pEvent, &pCtrl->rc);
    return 1;

  case 4:
  case 6:
    dlg_hook(pCtrl, 5);
    dlg_hook(pCtrl, 3);
    flc_mouse_update(pCtrl, pEvent, &pCtrl->rc);
    return 1;

  case 5:
    dlg_unhook_all(pCtrl);
    flc_mouse_update(pCtrl, pEvent, &pCtrl->rc);
    ctrl_check_mouse_in(pCtrl->pParent, pEvent);
    flc_clicked(pCtrl);
    return 1;
  }
  return 0;
}

// 0x4C8470 (76 B). Stop every element's video, free its bitmap, and put the
// payload rect back into control coordinates.
void ctrl_flc_deactivate(ctrl_t *pCtrl)
{
  flc_t *pItem = reinterpret_cast<flc_t *>(pCtrl->pChildren);
  dlg_word_t wOfs;

  while (pItem != 0) {
    if (pItem->hVid != 0)
      SVidPlayEnd(pItem->hVid);
    if (pItem->bmp.pPixels != 0)
      bmp_destroy(&pItem->bmp);
    pItem = pItem->pNext;
  }
  wOfs = static_cast<dlg_word_t>(pCtrl->rc.left);
  flc_rect(pCtrl)->left -= wOfs;
  flc_rect(pCtrl)->right -= wOfs;
  wOfs = static_cast<dlg_word_t>(pCtrl->rc.top);
  flc_rect(pCtrl)->top -= wOfs;
  flc_rect(pCtrl)->bottom -= wOfs;
}

// 0x4C84C0 (35 B).
void flc_remove_timers(ctrl_t *pDlg)
{
  ctrl_t *pCtrl = pDlg->pChildren;

  while (pCtrl != 0) {
    if (pCtrl->wType == 0x0E)
      timer_remove_node(pCtrl, FLC_TIMER_ID);
    pCtrl = pCtrl->pNext;
  }
}

// 0x4C84F0 (42 B).
void flc_animate(ctrl_t *pDlg)
{
  ctrl_t *pCtrl = pDlg->pChildren;

  while (pCtrl != 0) {
    if (pCtrl->wType == 0x0E)
      timer_create(pCtrl, FLC_TIMER_ID, 30,
                   reinterpret_cast<PFN_TIMER_FN *>(flc_anim_update_fn));
    pCtrl = pCtrl->pNext;
  }
}

// 0x4C8520 (289 B). Blit one element, clipping the destination rectangle
// against the caller's and the source rectangle against what is left. The
// effect id is 3 when the element blends, 1 when it only masks. Each clamp
// against the caller's rectangle is a conditional expression, and each
// destination edge is stored as soon as it is known.
void flc_draw_flc(ctrl_t *pCtrl, flc_t *pItem, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  if (pItem->hVid == 0) {
    return;
  }
  int aDst[4];
  int aSrc[4];
  int aExt[2];
  int nL = pCtrl->rc.left + pItem->wXOfs > prcClip->left ? pCtrl->rc.left + pItem->wXOfs : prcClip->left;
  int nT = pCtrl->rc.top + pItem->wYOfs > prcClip->top ? pCtrl->rc.top + pItem->wYOfs : prcClip->top;
  aDst[0] = nL;
  aDst[1] = nT;
  int nW = nL + pItem->bmp.nWidth;
  int nR = nW < prcClip->right + 1 ? nW : prcClip->right + 1;
  aDst[2] = nR;
  int nH = nT + pItem->bmp.nHeight;
  int nB = nH < prcClip->bottom + 1 ? nH : prcClip->bottom + 1;
  aDst[3] = nB;
  int nSX = nXOfs - pItem->wXOfs;
  if (nSX < 0) {
    nSX = 0;
  }
  aSrc[0] = nSX;
  int nSY = nYOfs - pItem->wYOfs;
  if (nSY < 0) {
    nSY = 0;
  }
  aSrc[1] = nSY;
  int nSR = nR + (nSX - nL + 1);
  if (pItem->bmp.nWidth < nSR) {
    nSR = pItem->bmp.nWidth;
  }
  aSrc[2] = nSR;
  int nSB = nB + (nSY - nT + 1);
  if (pItem->bmp.nHeight < nSB) {
    nSB = pItem->bmp.nHeight;
  }
  aSrc[3] = nSB;
  aExt[0] = pItem->bmp.nWidth;
  aExt[1] = pItem->bmp.nHeight;
  if (nSB > nSY && nSR > nSX) {
    int nEffect = 0;
    BYTE bLevel = 0;
    if ((pItem->dwFlags & 3) == 0) {
      if (pItem->dwFlags & 0x10) {
        nEffect = 1;
      }
    } else {
      nEffect = 3;
      bLevel = (BYTE)pItem->wLevel;
    }
    effects_draw_surface(pItem->bmp.pPixels, aSrc, aExt, pItem->bmp.nWidth, aDst, nEffect, &bLevel);
  }
}

// 0x4C8650 (224 B). Render the control's caption at the given blend level. The
// third argument is never read; both callers pass their clip rect. Each font
// arm carries its own font_set call, as in 1.17.
void flc_draw_text(ctrl_t *pCtrl, int nLevel, rect16_t *prcClip)
{
  dlg_byte_t bJust = gbFontJust;
  const char *psz;

  switch (pCtrl->dwFlags & 0x4C00) {
    case 0x400:  font_set(sgpFonts_g[0]); break;
    case 0x800:  font_set(sgpFonts_g[2]); break;
    case 0x4000: font_set(sgpFonts_g[3]); break;
    default:     font_set(sgpFonts_g[1]); break;
  }

  gbFontJust = bJust;
  gbFontCharExtra = (pCtrl->dwFlags & 0x10000) ? 0 : gbFontCharExtra;

  psz = static_cast<const char *>(pCtrl->pData);
  if (pCtrl->dwFlags & 0x200)
    ++psz;
  gsFontMargins = pCtrl->rcItems;

  if ((pCtrl->dwFlags & 2) != 0) {
    font_set_color_tbl(5);
    font_adjust(pCtrl->rcItems.left, pCtrl->rcItems.top, psz);
  } else {
    effects_draw_text(psz, pCtrl->rcItems.left, pCtrl->rcItems.top, 1, nLevel);
  }
  font_set(0);
}

// 0x4C8730 (44 B). The draw handler ctrl_btnglow_flc_evn installs at +0x2E.
void flc_draw_text_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  ctrl_set_hot_flag(pCtrl);
  if (pCtrl->pData != 0) {
    gbFontJust = flc_just(pCtrl);
    flc_draw_text(pCtrl, flc_level(pCtrl), prcClip);
  }
}

// 0x4C8760 (247 B). Ramp the control's own blend level and every element's
// toward the hover target, 15 a tick. Only the frontmost dialog hovers. Each
// clamp is one ternary over the stepped value.
void flc_hover_update(ctrl_t *pCtrl)
{
  dlgevt_t evt;
  point16_t pt;
  flc_t *pItem;
  int bIn;
  short nLevel;

  bIn = 0;
  evt.nX = static_cast<short>(gCursorPos_g[0]);
  evt.nY = static_cast<short>(gCursorPos_g[1]);
  pItem = reinterpret_cast<flc_t *>(pCtrl->pChildren);
  global_evt_2_local_point(pCtrl, &evt, &pt);

  if (pCtrl->pParent == gpDlgList && (pCtrl->dwFlags & 0x80) != 0 &&
      pt_in_rect(flc_rect(pCtrl), &pt) != 0) {
    nLevel = flc_level(pCtrl);
    if (nLevel < 100)
      flc_level(pCtrl) =
          static_cast<short>(nLevel + 15 < 100 ? nLevel + 15 : 100);
    bIn = 1;
  } else {
    nLevel = flc_level(pCtrl);
    if (nLevel > 0)
      flc_level(pCtrl) = static_cast<short>(nLevel - 15 > 0 ? nLevel - 15 : 0);
  }

  while (pItem != 0) {
    dlg_dword_t dwFlags = pItem->dwFlags;

    if ((dwFlags & 1) != 0) {
      int nTarget;
      int nCur;

      if (bIn) {
        if (static_cast<dlg_dword_t>(pItem->wLevel) < 100) {
          nCur = pItem->wLevel;
          pItem->wLevel =
              static_cast<dlg_word_t>(nCur + 15 < 100 ? nCur + 15 : 100);
        }
      } else {
        nTarget = ((dwFlags & 2) != 0) ? 30 : 100;
        nCur = pItem->wLevel;
        if (nCur < nTarget)
          pItem->wLevel = static_cast<dlg_word_t>(
              nCur + 15 < nTarget ? nCur + 15 : nTarget);
        else if (nCur > nTarget)
          pItem->wLevel = static_cast<dlg_word_t>(
              nCur - 15 > nTarget ? nCur - 15 : nTarget);
      }
    }
    pItem = pItem->pNext;
  }
  ctrl_set_update(pCtrl);
}

// 0x4C8860 (271 B). Size the element's bitmap from the video, clear it, then
// re-open the video rendering straight into that bitmap and step one frame.
void flc_init_flc(flc_t *pItem)
{
  vidblock4_t blkSrc;
  vidblock2_t blkDst;
  DWORD dwHeight;
  DWORD dwWidth;
  HANDLE hVid;
  int nWidth;
  int nHeight;

  if (pItem->bmp.nWidth == 0 && pItem->bmp.nHeight == 0) {
    SVidPlayBegin(pItem->pszFile, 0, 0, 0, 0, 0, &hVid);
    if (hVid == 0)
      appcritf(OC_FLC_LOADFAIL, pItem->pszFile);
    SVidGetSize(hVid, &dwWidth, &dwHeight, 0);
    SVidPlayEnd(hVid);
    pItem->bmp.nWidth = static_cast<short>(dwWidth);
    pItem->bmp.nHeight = static_cast<short>(dwHeight);
  }

  bmp_create(&pItem->bmp, OC_FLC_FILE, 0x15F);
  memset(pItem->bmp.pPixels, 0, pItem->bmp.nHeight * pItem->bmp.nWidth);

  nWidth = pItem->bmp.nWidth;
  nHeight = pItem->bmp.nHeight;
  blkDst.n00 = nWidth;
  blkSrc.n08 = nWidth - 1;
  blkDst.n04 = nHeight;
  blkSrc.n00 = 0;
  blkSrc.n0C = nHeight - 1;
  blkSrc.n04 = 0;
  SVidPlayBegin(pItem->pszFile,
                reinterpret_cast<DWORD_PTR>(pItem->bmp.pPixels), &blkSrc, &blkDst,
                0, ((pItem->dwFlags & 4) << 16) | 0x21520100, &pItem->hVid);
  if (pItem->hVid == 0)
    appcritf(OC_FLC_LOADFAIL, pItem->pszFile);
  SVidPlayContinueSingle(pItem->hVid, 1, 0);
}

// 0x4C8970 (63 B). The hit test behind event 4. The byte narrowing in the last
// line is the same unexplained one ctrl_query_locate's tail carries.
int flc_locate(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  point16_t pt;
  dlg_dword_t dwFlags;

  global_evt_2_local_point(pCtrl, pEvent, &pt);
  if (pt_in_rect(flc_rect(pCtrl), &pt) == 0)
    return 0;
  dwFlags = pCtrl->dwFlags;
  if (!(dwFlags & 8))
    return 0;
  if (dwFlags & 2)
    return 0;
  return (static_cast<dlg_byte_t>(dwFlags) >> 4) & 1;
}

// 0x4C89B0 (136 B). The 30 ms timer callback. Walk the elements advancing each
// video, unlink the ones that ended, and repaint every FLC sibling if anything
// changed. The cursor is a flc_t ** so the unlink needs no previous pointer.
void flc_anim_update_fn(ctrl_t *pCtrl)
{
  DWORD dwFrame;
  flc_t **ppCur;
  dlg_dword_t dwDirty;
  ctrl_t *pNode;

  if ((pCtrl->dwFlags & 8) == 0)
    return;

  ppCur = reinterpret_cast<flc_t **>(&pCtrl->pChildren);
  dwDirty = 0;
  while (*ppCur != 0) {
    if (SVidPlayContinueSingle((*ppCur)->hVid, 0, &dwFrame) == 0) {
      bmp_destroy(&(*ppCur)->bmp);
      dwDirty = 1;
      *ppCur = (*ppCur)->pNext;
    } else {
      dwDirty |= dwFrame;
      ppCur = &(*ppCur)->pNext;
    }
  }

  if (dwDirty != 0) {
    pNode = pCtrl->pParent->pChildren;
    while (pNode != 0) {
      if (pNode->wType == 0x0E && pNode->pChildren != 0)
        ctrl_set_update(pNode);
      pNode = pNode->pNext;
    }
  }
  flc_hover_update(pCtrl);
}

// 0x4C8A40 (99 B). Track the pressed bit against the given rect: entering
// presses and plays the down click, leaving releases and plays the up one. The
// flag is read before the hit test, as in 1.17.
void flc_mouse_update(ctrl_t *pCtrl, dlgevt_t *pEvent, rect16_t *prc)
{
  point16_t pt;
  dlg_dword_t dwPressed = pCtrl->dwFlags & FLC_PRESSED;

  global_evt_2_local_point(pCtrl, pEvent, &pt);
  if (pt_in_rect(prc, &pt) != 0) {
    if (dwPressed != 0)
      return;
    snd_play_btn(0);
  } else if (dwPressed == 0) {
    return;
  }
  pCtrl->dwFlags ^= FLC_PRESSED;
  ctrl_set_update(pCtrl);
  if ((pCtrl->dwFlags & FLC_PRESSED) != 0)
    snd_play_btn(1);
}

// 0x4C8AB0 (209 B). ctrl_flc's wTag 0x0E arm. Everything the control does not
// handle itself falls out of the switch into ctrl_evn_dlg.
int ctrl_flc_evn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  switch (pEvent->dwId) {
  case 10:
    ctrl_flc_init(pCtrl);
    break;

  case 2:
    gnDlgReturnVal = pCtrl->nIndex;
    ctrl_evt(pCtrl->pParent, pEvent);
    return 1;

  case 6:
    pCtrl->dwFlags &= ~0x40000080u;
    break;

  case 5:
    return 0;

  case 14:
    pCtrl->dwFlags &= ~FLC_PRESSED;
    break;

  case 3:
    ctrl_set_update(pCtrl);
    if ((pCtrl->dwFlags & FLC_PRESSED) != 0) {
      flc_clicked(pCtrl);
      return 1;
    }
    dlg_register_timeout(pCtrl);
    pCtrl->dwFlags ^= FLC_PRESSED;
    if ((pCtrl->dwFlags & FLC_PRESSED) != 0)
      snd_play_btn(1);
    return 1;

  case 9:
    snd_play_btn(0);
    return 1;

  case 1:
    ctrl_flc_deactivate(pCtrl);
    break;

  case 4:
    return flc_locate(pCtrl, pEvent);
  }
  return ctrl_evn_dlg(pCtrl, pEvent);
}

// 0x4C8BC0 (49 B). Release: drop the pressed bit and tell the dialog.
void flc_clicked(ctrl_t *pCtrl)
{
  dlg_unhook_all(pCtrl);
  if ((pCtrl->dwFlags & FLC_PRESSED) != 0) {
    pCtrl->dwFlags &= ~FLC_PRESSED;
    ctrl_set_update(pCtrl);
    ctrl_send_msg(pCtrl, 2, 0);
  }
}

// 0x4C8C00 (214 B). ctrl_btnglow_flc's wTag 0x0E arm. Event 10 relocates the
// payload rect into dialog coordinates and installs the text draw handler.
int ctrl_btnglow_flc_evn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  switch (pEvent->dwId) {
  case 10: {
    dlg_word_t wOfs = static_cast<dlg_word_t>(pCtrl->rc.left);

    flc_rect(pCtrl)->left += wOfs;
    flc_rect(pCtrl)->right += wOfs;
    wOfs = static_cast<dlg_word_t>(pCtrl->rc.top);
    flc_rect(pCtrl)->top += wOfs;
    flc_rect(pCtrl)->bottom += wOfs;
    dlg_byte_t bJust = static_cast<dlg_byte_t>(pCtrl->dwFlags >> 20) & 0x77;

    pCtrl->pfnDraw = flc_draw_text_fcn;
    flc_just(pCtrl) = bJust;
    flc_level(pCtrl) = 0;
    break;
  }

  case 2:
    gnDlgReturnVal = pCtrl->nIndex;
    ctrl_evt(pCtrl->pParent, pEvent);
    return 1;

  case 6:
    pCtrl->dwFlags &= ~0x40000080u;
    break;

  case 5:
    return 0;

  case 14:
    pCtrl->dwFlags &= ~FLC_PRESSED;
    break;

  case 9:
    snd_play_btn(0);
    return 1;

  case 3:
    ctrl_set_update(pCtrl);
    if ((pCtrl->dwFlags & FLC_PRESSED) != 0) {
      flc_clicked(pCtrl);
      return 1;
    }
    dlg_register_timeout(pCtrl);
    pCtrl->dwFlags ^= FLC_PRESSED;
    if ((pCtrl->dwFlags & FLC_PRESSED) != 0)
      snd_play_btn(1);
    return 1;

  case 4:
    return flc_locate(pCtrl, pEvent);
  }
  return ctrl_evn_dlg(pCtrl, pEvent);
}
