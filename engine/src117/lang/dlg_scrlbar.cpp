// dlg_scrlbar.cpp -- the horizontal and vertical scrollbar controls. Bodies are
// in oracle VA order.
//
// A scrollbar tracks a position between a min and a max, notifies its owner
// through the callback at +0x36, and while a button is held runs an auto-repeat
// off the dialog timer. scroll_find_part names the region under the cursor, the
// 0x4F11D0 table turns that into the dwFlags bit the drag records, and the
// 0x50EF78 table turns it into the step each repeat applies.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include <string.h>  // memset -- scroll_create zeroes the node it just allocated

#include "../h/dlg_scrlbar.h"
#include "../h/dialog.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_draw.h"
#include "../h/dlg_lbox.h"
#include "../h/bmp.h"
#include "../h/snd.h"
#include "../h/Storm.h"
#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled.

// The scrollbar half of the 30 theme metrics dlg_stats_init publishes: WORDs,
// though the run is 4-byte spaced.
#ifndef gwVArrowWid
#ifdef OC_DLL_MODE
#define gwVArrowWid (*reinterpret_cast<WORD *>(OC_VA(0x6CE6F4u)))
#define gwVArrowHgt (*reinterpret_cast<WORD *>(OC_VA(0x6CE6F8u)))
#define gwHArrowWid (*reinterpret_cast<WORD *>(OC_VA(0x6CE6FCu)))
#define gwHArrowHgt (*reinterpret_cast<WORD *>(OC_VA(0x6CE700u)))
#define gwUnk6_g (*reinterpret_cast<WORD *>(OC_VA(0x6CE704u)))
#define gwUnk7_g (*reinterpret_cast<WORD *>(OC_VA(0x6CE708u)))
#define gwThumbWid (*reinterpret_cast<WORD *>(OC_VA(0x6CE70Cu)))
#define gwThumbHgt (*reinterpret_cast<WORD *>(OC_VA(0x6CE710u)))
#define gwUnk24_g (*reinterpret_cast<WORD *>(OC_VA(0x6CE74Cu)))
#define gwSliderSlotOnWid_g (*reinterpret_cast<WORD *>(OC_VA(0x6CE750u)))
#define gwUnk26_g (*reinterpret_cast<WORD *>(OC_VA(0x6CE754u)))
#define gwSliderSlotWid_g (*reinterpret_cast<WORD *>(OC_VA(0x6CE758u)))
#else
extern WORD gwVArrowWid;
extern WORD gwVArrowHgt;
extern WORD gwHArrowWid;
extern WORD gwHArrowHgt;
extern WORD gwUnk6_g;
extern WORD gwUnk7_g;
extern WORD gwThumbWid;
extern WORD gwThumbHgt;
extern WORD gwUnk24_g;
extern WORD gwSliderSlotOnWid_g;
extern WORD gwUnk26_g;
extern WORD gwSliderSlotWid_g;
#endif
#endif

// The dialog library's 32 pen indices; this file uses [8] and [16] and spells
// them base+index, never as globals of their own.
#ifndef sgbPens_g
#ifdef OC_DLL_MODE
#define sgbPens_g (reinterpret_cast<BYTE *>(OC_VA(0x6D2368u)))
#else
extern BYTE sgbPens_g[32] OC_UNALIGNED;
#endif
#endif

// The current pen index the bmp_* primitives draw with.
#ifndef _bmp_pen
#ifdef OC_DLL_MODE
#define _bmp_pen (*reinterpret_cast<BYTE *>(OC_VA(0x6D0C50u)))
#else
extern BYTE _bmp_pen;
#endif
#endif

// Part to dwFlags bit (.rdata, dwords) and part to position step (.data,
// words). scroll_lmouse_down rewrites the step table's entries 4 and 5 with the
// page size before every use.
#ifndef sgwPart2ScrollFlagTbl
#ifdef OC_DLL_MODE
#define sgwPart2ScrollFlagTbl (reinterpret_cast<const DWORD *>(OC_VA(0x4F11D0u)))
#else
extern const DWORD sgwPart2ScrollFlagTbl[7];
#endif
#endif

#ifndef sgnPart2ScrollDeltaTbl
#ifdef OC_DLL_MODE
#define sgnPart2ScrollDeltaTbl (reinterpret_cast<short *>(OC_VA(0x50EF78u)))
#else
extern short sgnPart2ScrollDeltaTbl[7];
#endif
#endif

// The scrollbar node: the scrollbar arm of the storage whose shared head and
// dialog arm dialog.h defines. It collides with the dialog arm from +0x3E on, so
// it lives here, as lbox_t does. wType 6 is the horizontal bar, 7 the vertical.
#pragma pack(push, 1)
struct scroll_t {
  /*0x00*/ ctrl_t *pNext;
  /*0x04*/ rect16_t rc;
  /*0x0C*/ dlg_byte_t _unmapped0C[2 * sizeof(void *)];  // Hyperion: ctrl_t's +0x0C and nResIndex
  /*0x14*/ void *pData;
  /*0x18*/ dlg_dword_t dwFlags;
  /*0x1C*/ dlg_byte_t _unmapped1C[4];
  /*0x20*/ short nIndex;
  /*0x22*/ dlg_word_t wType;
  /*0x24*/ dlg_byte_t _unmapped24[2];
  // +0x26 the argument scroll_set_pos calls pfnNotify with. Nothing here writes
  // it.
  /*0x26*/ void *pNotifyArg;
  /*0x2A*/ PFN_CTRL_EVT pfnEvt;
  /*0x2E*/ PFN_CTRL_DRAW pfnDraw;
  /*0x32*/ ctrl_t *pParent;
  // +0x36 the owner's position-changed callback, scroll_create's EDX argument:
  // one register argument, no result.
  /*0x36*/ void *pfnNotify;
  // +0x3A the current position, +0x3C the minimum, +0x3E the maximum, all
  // signed: scroll_set_range clamps with signed compares.
  /*0x3A*/ short nPos;
  /*0x3C*/ short nMin;
#if defined(_WIN64) || defined(__LP64__)
  // Hyperion: at 64 bits the fields from here fall where ctrl_t's slots of the
  // same 32-bit offsets are (src117/h/dialog.h); the
  // asserts below check it.
  dlg_byte_t _pad3E[4];
#endif
  /*0x3E*/ short nMax;
  // +0x40 the step the auto-repeat adds each tick, signed.
  /*0x40*/ short nDelta;
  // +0x42 the part being held, as its 0x4F11D0 flag bit rather than its index;
  // 0x80000000 is the thumb.
#if defined(_WIN64) || defined(__LP64__)
  dlg_byte_t _pad42[4];
#endif
  /*0x42*/ dlg_dword_t dwPart;
#if defined(_WIN64) || defined(__LP64__)
  dlg_byte_t _pad46[4];
#endif
  // +0x46 the page size and +0x47 the auto-repeat countdown scroll_timer
  // decrements.
  /*0x46*/ dlg_byte_t nPage;
  /*0x47*/ dlg_byte_t nTick;
  // +0x48 the thumb's bitmap frame and +0x49 its segment count, which
  // draw_hscroll_native reads.
  /*0x48*/ dlg_byte_t nThumbFrame;
  /*0x49*/ dlg_byte_t nThumbSegs;
  // +0x4A the thumb offset cached while the thumb is held, which is what
  // scroll_calc_thumb_offset returns under dwFlags & 0x80000000.
  /*0x4A*/ short nThumbOfs;
  /*0x4C*/ dlg_byte_t _unmapped4C[0x56 - 0x4C + 3 * (sizeof(void *) - 4)];
};
#pragma pack(pop)

// Hyperion: a scroll bar is a node the dialog code reads as a ctrl_t, and a
// list box reads its position through ctrl_t's +0x36 window, so the two agree on
// every width.
#define OC_AT(f, g) static_assert(offsetof(scroll_t, f) == (g), "scroll_t::" #f " is ctrl_t's")
OC_AT(pData, offsetof(ctrl_t, pData));
OC_AT(dwFlags, offsetof(ctrl_t, dwFlags));
OC_AT(nIndex, offsetof(ctrl_t, nIndex));
OC_AT(wType, offsetof(ctrl_t, wType));
OC_AT(pNotifyArg, offsetof(ctrl_t, _unmapped24) + 2);
OC_AT(pfnEvt, offsetof(ctrl_t, pfnEvt));
OC_AT(pfnDraw, offsetof(ctrl_t, pfnDraw));
OC_AT(pParent, offsetof(ctrl_t, pParent));
OC_AT(pfnNotify, offsetof(ctrl_t, _unmapped36));
OC_AT(nPos, offsetof(ctrl_t, _unmapped36) + CTRL_SLOT2);
OC_AT(nMax, offsetof(ctrl_t, pFocus));
OC_AT(dwPart, offsetof(ctrl_t, pChildren));
OC_AT(nPage, offsetof(ctrl_t, rcItems));
#undef OC_AT
static_assert(sizeof(scroll_t) == sizeof(ctrl_t), "scroll_t is a ctrl_t node");

OC_X86_LAYOUT(sizeof(scroll_t)                == 0x56, "scroll_t size         dialog.h's derived extent for the shared node");
static_assert(offsetof(scroll_t, pNext)       == 0x00, "scroll_t::pNext       dialog.h ctrl_t head; this file never reads it");
OC_X86_LAYOUT(offsetof(scroll_t, rc)          == 0x04, "scroll_t::rc          scroll_find_part: lea ecx, dword ptr [esi+4h]");
OC_X86_LAYOUT(offsetof(scroll_t, pData)       == 0x14, "scroll_t::pData       dialog.h ctrl_t head; this file never reads it");
OC_X86_LAYOUT(offsetof(scroll_t, dwFlags)     == 0x18, "scroll_t::dwFlags     scroll_init: test dword ptr [edx+18h], 2000000h");
OC_X86_LAYOUT(offsetof(scroll_t, nIndex)      == 0x20, "scroll_t::nIndex      scroll_ctrl_scroll: test word ptr [esi+20h], ax");
OC_X86_LAYOUT(offsetof(scroll_t, wType)       == 0x22, "scroll_t::wType       scroll_calc_thumb_run: cmp word ptr [ecx+22h], 6h");
OC_X86_LAYOUT(offsetof(scroll_t, pNotifyArg)  == 0x26, "scroll_t::pNotifyArg  scroll_set_pos: mov ecx, dword ptr [esi+26h]");
OC_X86_LAYOUT(offsetof(scroll_t, pfnEvt)      == 0x2A, "scroll_t::pfnEvt      scroll_create: mov dword ptr [ebx+2Ah], eax");
OC_X86_LAYOUT(offsetof(scroll_t, pfnDraw)     == 0x2E, "scroll_t::pfnDraw     scroll_create: mov dword ptr [ebx+2Eh], eax");
OC_X86_LAYOUT(offsetof(scroll_t, pParent)     == 0x32, "scroll_t::pParent     scroll_create: mov dword ptr [ebx+32h], edi");
OC_X86_LAYOUT(offsetof(scroll_t, pfnNotify)   == 0x36, "scroll_t::pfnNotify   scroll_set_pos: mov eax, dword ptr [esi+36h]");
OC_X86_LAYOUT(offsetof(scroll_t, nPos)        == 0x3A, "scroll_t::nPos        scroll_get_pos: mov ax, word ptr [ecx+3Ah]");
OC_X86_LAYOUT(offsetof(scroll_t, nMin)        == 0x3C, "scroll_t::nMin        scroll_set_range: mov word ptr [esi+3Ch], dx");
OC_X86_LAYOUT(offsetof(scroll_t, nMax)        == 0x3E, "scroll_t::nMax        scroll_set_range: mov word ptr [esi+3Eh], ax");
OC_X86_LAYOUT(offsetof(scroll_t, nDelta)      == 0x40, "scroll_t::nDelta      start_scroll: mov word ptr [esi+40h], ax");
OC_X86_LAYOUT(offsetof(scroll_t, dwPart)      == 0x42, "scroll_t::dwPart      start_scroll: mov dword ptr [esi+42h], eax");
OC_X86_LAYOUT(offsetof(scroll_t, nPage)       == 0x46, "scroll_t::nPage       scroll_set_page_size: mov byte ptr [ecx+46h], dl");
OC_X86_LAYOUT(offsetof(scroll_t, nTick)       == 0x47, "scroll_t::nTick       scroll_timer: dec byte ptr [esi+47h]");
OC_X86_LAYOUT(offsetof(scroll_t, nThumbFrame) == 0x48, "scroll_t::nThumbFrame draw_hscroll_native: mov al, byte ptr [ebx+48h]");
OC_X86_LAYOUT(offsetof(scroll_t, nThumbSegs)  == 0x49, "scroll_t::nThumbSegs  draw_hscroll_native: mov al, byte ptr [ebx+49h]");
OC_X86_LAYOUT(offsetof(scroll_t, nThumbOfs)   == 0x4A, "scroll_t::nThumbOfs   scroll_check_state: mov word ptr [ebx+4Ah], si");

// The owner callback pfnNotify holds: one register argument, no result.
typedef void(__fastcall *PFN_SCROLL_NOTIFY)(void *pArg);

// dwFlags bits this file reads. 0x02000000 picks the native rendering and
// 0x80000000 means the thumb itself is held; the five part bits are the
// 0x4F11D0 table's entries and are cleared together.
#define SCROLL_NATIVE 0x02000000u
#define SCROLL_THUMB 0x80000000u
#define SCROLL_PARTS 0xEC000000u

// The virtual key dlgevt_t carries at +0x08. dialog.h leaves that window
// unnamed because each event id reads it differently; scroll_key_down reads it
// as a WORD, the same way ctrl_dlg_char does.
#define evt_vkey(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped08[0]))

// 0x4C8D10 (448 B). The themed horizontal bar: the left arrow, a run of slot
// tiles wide enough to fill the gap, the right arrow, then the thumb. Too
// narrow to hold both arrow buttons and it draws nothing at all.
void draw_hscroll(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  scroll_t *pScroll = reinterpret_cast<scroll_t *>(pCtrl);
  dlg_dword_t dwFlags;
  unsigned char bLit;
  unsigned char nFrame;
  int nLeft;
  int nTop;
  int nX;
  int nTiles;

  nLeft = (unsigned short)pScroll->rc.left;
  nTop = (unsigned short)pScroll->rc.top;
  if (pScroll->dwFlags & SCROLL_NATIVE) {
    draw_hscroll_native(pCtrl, nXOfs, nYOfs, prcClip);
    return;
  }
  if (pScroll->rc.right - pScroll->rc.left < (gwHArrowWid + gwUnk7_g) * 2)
    return;
  ctrl_set_hot_flag(pCtrl);
  dwFlags = pScroll->dwFlags;
  bLit = (dwFlags & 2) ? 0 : 1;
  nFrame = (unsigned char)(bLit + 0x16);
  if (dwFlags & 0x40000000)
    ++nFrame;
  draw_ctrl_bmp_xy(nFrame, (short)nLeft, nTop, prcClip, dwFlags);
  nX = gwHArrowWid;
  draw_ctrl_bmp_xy(0x20, (short)(nX + nLeft), nTop, prcClip, pScroll->dwFlags);
  nTiles = (pScroll->rc.right - gwHArrowWid * 2 - pScroll->rc.left) / gwUnk7_g - 2;
  while (nTiles != 0) {
    nX += gwUnk7_g;
    draw_ctrl_bmp_xy(0x21, (short)(nX + nLeft), nTop, prcClip, pScroll->dwFlags);
    --nTiles;
  }
  nX += gwUnk7_g;
  draw_ctrl_bmp_xy(0x22, (short)(nX + nLeft), nTop, prcClip, pScroll->dwFlags);
  nX += gwHArrowWid;
  dwFlags = pScroll->dwFlags;
  nFrame = (unsigned char)(bLit + 0x19);
  if (dwFlags & 0x20000000)
    ++nFrame;
  draw_ctrl_bmp_xy(nFrame, (short)(nX + nLeft), nTop, prcClip, dwFlags);
  if (!(pScroll->dwFlags & 2))
    draw_ctrl_bmp_xy_transparent(
        0x1C, (short)(scroll_calc_thumb_offset(pScroll) + gwHArrowWid + nLeft),
        (gwVArrowWid - gwThumbHgt) / 2 + nTop, prcClip);
  if (!(pScroll->dwFlags & 0x20000))
    draw_frame_hot(pCtrl, &pScroll->rc, sgbPens_g[16]);
}

// 0x4C8ED0 (390 B). The undecorated horizontal bar: the slot, its left cap, up
// to six evenly spaced tick marks, its right cap, then the thumb.
void draw_hscroll_native(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  scroll_t *pScroll = reinterpret_cast<scroll_t *>(pCtrl);
  unsigned char nSlot;
  unsigned char nThumb;
  int nStep;
  int nTicks;
  int i;

  ctrl_set_hot_flag(pCtrl);
  draw_local_ctrl(pScroll->rc.left, pScroll->rc.top,
                  pScroll->rc.right - pScroll->rc.left + 1, gwUnk24_g,
                  (unsigned char)((pScroll->dwFlags & 2) ? 0x5B : 0x5E), prcClip,
                  0x2000);
  nSlot = (unsigned char)((pScroll->dwFlags & 2) ? 0x61 : 0x62);
  draw_ctrl_bmp_xy_transparent(
      nSlot, (short)(gwSliderSlotOnWid_g / 2 + pScroll->rc.left),
      (unsigned short)pScroll->rc.top, prcClip);
  nStep = (pScroll->nThumbSegs == 0) ? 1 : pScroll->nThumbSegs;
  nStep = (pScroll->nMax - pScroll->nMin) / nStep;
  nTicks = (nStep < 7) ? nStep : 7;
  for (i = 1; i < nTicks; ++i)
    draw_ctrl_bmp_xy_transparent(
        nSlot,
        (short)((pScroll->rc.right - pScroll->rc.left - gwSliderSlotOnWid_g + 1) *
                    i / nTicks -
                (gwSliderSlotWid_g + 1) / 2 + gwSliderSlotOnWid_g / 2 +
                pScroll->rc.left),
        (unsigned short)pScroll->rc.top, prcClip);
  draw_ctrl_bmp_xy_transparent(
      nSlot,
      (short)(pScroll->rc.right - (gwSliderSlotWid_g + 1) / 2 -
              gwSliderSlotOnWid_g / 2),
      (unsigned short)pScroll->rc.top, prcClip);
  nThumb = (pScroll->dwFlags & 2) ? 0x63 : pScroll->nThumbFrame;
  draw_ctrl_bmp_xy_transparent(
      nThumb, (short)(scroll_calc_thumb_offset(pScroll) + pScroll->rc.left),
      (unsigned short)pScroll->rc.top, prcClip);
  draw_frame_hot(pCtrl, &pScroll->rc, sgbPens_g[16]);
}

// 0x4C9060 (417 B). The themed vertical bar, the mirror of draw_hscroll. The
// thumb's own offset is halved with `>>` here where draw_hscroll divides, which
// is why one gets the sign correction and the other does not.
void draw_vscroll(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  scroll_t *pScroll = reinterpret_cast<scroll_t *>(pCtrl);
  unsigned char bLit;
  unsigned char nFrame;
  int nLeft;
  int nTop;
  int nOfs;
  int nEnd;
  int nTiles;

  nLeft = pScroll->rc.left;
  nTop = pScroll->rc.top;
  if (pScroll->dwFlags & SCROLL_NATIVE) {
    draw_vscroll_native(pCtrl, nXOfs, nYOfs, prcClip);
    return;
  }
  if (pScroll->rc.bottom - pScroll->rc.top < (gwHArrowHgt + gwUnk6_g) * 2)
    return;
  ctrl_set_hot_flag(pCtrl);
  bLit = (pScroll->dwFlags & 2) ? 0 : 1;
  nFrame = (unsigned char)(bLit + 0x10);
  if (pScroll->dwFlags & 0x40000000)
    ++nFrame;
  draw_ctrl_bmp_xy_transparent(nFrame, (short)nLeft, nTop, prcClip);
  nOfs = gwVArrowHgt + 2;
  draw_ctrl_bmp_xy_transparent(0x1D, (short)nLeft, nTop + nOfs, prcClip);
  nTiles = ((short)scroll_calc_thumb_run(pScroll) + gwThumbHgt) / gwUnk6_g - 1;
  while (nTiles != 0) {
    nOfs += gwUnk6_g;
    draw_ctrl_bmp_xy_transparent(0x1E, (short)nLeft, nOfs + nTop, prcClip);
    --nTiles;
  }
  nOfs += gwUnk6_g;
  draw_ctrl_bmp_xy_transparent(0x1F, (short)nLeft, nTop + nOfs, prcClip);
  nEnd = gwUnk6_g + 2 + nOfs;
  nFrame = (unsigned char)(bLit + 0x13);
  if (pScroll->dwFlags & 0x20000000)
    ++nFrame;
  draw_ctrl_bmp_xy_transparent(nFrame, (short)nLeft, nEnd + nTop, prcClip);
  if (!(pScroll->dwFlags & 2))
    draw_ctrl_bmp_xy_transparent(
        0x1C, (short)(nLeft - 1 + ((gwHArrowHgt - gwThumbWid) >> 1)),
        (unsigned short)(scroll_calc_thumb_offset(pScroll) + 2 + gwVArrowHgt + nTop),
        prcClip);
  if (!(pScroll->dwFlags & 0x20000))
    draw_frame_hot(pCtrl, &pScroll->rc, sgbPens_g[16]);
}

// 0x4C9210 (126 B). The undecorated bar: paint the slot, then the thumb on top
// of it unless the control is greyed.
void draw_vscroll_native(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  scroll_t *pScroll = reinterpret_cast<scroll_t *>(pCtrl);

  ctrl_set_hot_flag(pCtrl);
  _bmp_pen = (pScroll->dwFlags & 2) ? sgbPens_g[8] : sgbPens_g[16];
  bmp_rect(pScroll->rc.left, pScroll->rc.top,
           (unsigned short)(pScroll->rc.right - pScroll->rc.left + 1),
           (unsigned short)(pScroll->rc.bottom - pScroll->rc.top + 1));
  if (!(pScroll->dwFlags & 2)) {
    // Read before the call, as 1.17 does.
    short nLeft = pScroll->rc.left;
    bmp_rect(nLeft,
             (unsigned short)(scroll_calc_thumb_offset(pScroll) + pScroll->rc.top),
             (unsigned short)(pScroll->rc.right - nLeft + 1), 6);
  }
}

// 0x4C9290 (130 B). Where along the slot the thumb sits, in pixels. While the
// thumb is held the value scroll_check_state cached is returned unchanged.
unsigned short scroll_calc_thumb_offset(scroll_t *pScroll)
{
  short nRange;
  short nRun;
  short nPos;

  if (pScroll->dwFlags & SCROLL_THUMB)
    return pScroll->nThumbOfs;
  // nMin and nMax are read once, before scroll_calc_thumb_run, as in 1.17.
  short nMin = pScroll->nMin;
  short nMax = pScroll->nMax;
  nRange = (short)(nMax - nMin);
  if (nRange == 0)
    return 0;
  nRun = scroll_calc_thumb_run(pScroll);
  nPos = pScroll->nPos;
  if (nPos == nMin)
    return 0;
  if (nPos == nMax)
    return nRun;
  return (unsigned short)(((nPos - nMin) * nRun + nRange / 2) / nRange);
}

// 0x4C9320 (135 B). The travel the thumb has, in pixels: the long side of the
// bar less the two arrow buttons and the thumb itself. A short accumulator,
// stepped in 1.17's order, and one zero-extending return after the arms.
int scroll_calc_thumb_run(scroll_t *pScroll)
{
  short wRun;

  if (pScroll->wType == 6) {
    if (pScroll->dwFlags & SCROLL_NATIVE) {
      wRun = pScroll->rc.right;
      wRun -= pScroll->rc.left;
      wRun -= gwSliderSlotOnWid_g;
      ++wRun;
    } else {
      wRun = -gwHArrowWid;
      wRun += wRun;
      wRun -= pScroll->rc.left;
      wRun += pScroll->rc.right;
      wRun -= gwThumbWid;
      --wRun;
    }
  } else if (pScroll->dwFlags & SCROLL_NATIVE) {
    wRun = pScroll->rc.bottom;
    wRun -= pScroll->rc.top;
    wRun -= gwUnk26_g;
    ++wRun;
  } else {
    wRun = -gwVArrowHgt;
    wRun += wRun;
    wRun -= pScroll->rc.top;
    wRun += pScroll->rc.bottom;
    wRun -= gwThumbHgt;
    wRun -= 5;
  }
  return (unsigned short)wRun;
}

// 0x4C93B0 (455 B). Re-read the cursor while a button is held. Dragging the
// thumb recomputes the step from where the cursor sits along the slot. The
// native vertical arm measures against rc.right where every other vertical path
// uses rc.bottom, as 1.17 does. The clamp is stored back into the point.
void scroll_check_state(scroll_t *pScroll, dlgevt_t *pEvent)
{
  point16_t pt;
  dlg_dword_t dwPart;
  dlg_dword_t dwFlag;
  dlg_dword_t dwFlags;
  dlg_dword_t dwNative;
  unsigned short wHalf;
  unsigned short wLimit;
  unsigned short wMin;
  unsigned nSize;
  int nPad;
  int nOfs;
  int nRun;

  dwFlag = sgwPart2ScrollFlagTbl[scroll_find_part(pScroll, pEvent)];
  dwPart = pScroll->dwPart;
  if (dwPart == SCROLL_THUMB) {
    global_evt_2_local_point(reinterpret_cast<ctrl_t *>(pScroll), pEvent, &pt);
    if (pScroll->wType == 6) {
      dwNative = pScroll->dwFlags;
      dwNative &= SCROLL_NATIVE;
      if (dwNative) {
        wHalf = gwSliderSlotOnWid_g;
        wLimit = pScroll->rc.right;
        wHalf /= 2;
        wLimit -= wHalf;
      } else {
        wHalf = gwThumbWid;
        wLimit = pScroll->rc.right;
        wHalf /= 2;
        wLimit -= wHalf;
        wLimit -= gwVArrowWid;
        wLimit -= 3;
      }
      pt.x = (pt.x < (short)wLimit) ? pt.x : (short)wLimit;
      nOfs = pt.x - pScroll->rc.left;
      nPad = dwNative ? 0 : gwHArrowWid;
      nSize = dwNative ? gwSliderSlotOnWid_g : gwThumbWid;
    } else {
      dwNative = pScroll->dwFlags;
      dwNative &= SCROLL_NATIVE;
      if (dwNative) {
        wHalf = gwUnk26_g;
        wLimit = pScroll->rc.right;
        wHalf /= 2;
        wLimit -= wHalf;
      } else {
        wHalf = gwThumbHgt;
        wLimit = pScroll->rc.bottom;
        wHalf /= 2;
        wLimit -= wHalf;
        wLimit -= gwVArrowHgt;
        wLimit -= 3;
      }
      pt.y = (pt.y < (short)wLimit) ? pt.y : (short)wLimit;
      nOfs = pt.y - pScroll->rc.top;
      nPad = dwNative ? 0 : gwVArrowHgt + 2;
      nSize = dwNative ? gwUnk26_g : gwThumbHgt;
    }
    nOfs -= nSize / 2 + nPad;
    if (nOfs < 0)
      nOfs = 0;
    pScroll->nThumbOfs = (short)nOfs;
    wMin = pScroll->nMin;
    nRun = (short)scroll_calc_thumb_run(pScroll);
    pScroll->nDelta =
        (short)((pScroll->nMax - (short)wMin + 1) * nOfs / nRun - pScroll->nPos +
                wMin);
    return;
  }
  dwFlags = pScroll->dwFlags;
  if (dwFlags & dwPart) {
    if (dwFlag != dwPart) {
      pScroll->dwFlags = dwFlags ^ dwPart;
      ctrl_set_update(reinterpret_cast<ctrl_t *>(pScroll));
    }
  } else if (dwFlag == dwPart) {
    pScroll->dwFlags = dwFlags ^ dwPart;
    ctrl_set_update(reinterpret_cast<ctrl_t *>(pScroll));
    if (pScroll->dwFlags & pScroll->dwPart)
      snd_play_btn_click();
  }
}

// 0x4C9580 (162 B). Allocate a bare scrollbar node and wire it to its owner.
// A vertical bar also rounds its height down to a whole number of rows.
ctrl_t *scroll_create(ctrl_t *pParent, void *pfnNotify, unsigned short wType, int nSpan)
{
  scroll_t *pScroll =
      reinterpret_cast<scroll_t *>(SMemAlloc(sizeof(scroll_t), "scrlbar.cpp", 0x29D, 0));

  if (pScroll == 0)
    return 0;
  memset(pScroll, 0, sizeof(scroll_t));
  pScroll->wType = wType;
  pScroll->dwFlags = 0x20004;
  pScroll->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(gCtrlEvtFcnTbl[wType]);
  pScroll->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(gCtrlDrawFcnTbl[wType]);
  pScroll->pParent = pParent;
  pScroll->pfnNotify = pfnNotify;
  if (wType == 7) {
    int nEnds = gwVArrowHgt * 2 + 4;

    pScroll->rc.bottom =
        (short)((gwUnk6_g / 2 - nEnds + nSpan) / gwUnk6_g * gwUnk6_g + nEnds);
  }
  return reinterpret_cast<ctrl_t *>(pScroll);
}

// 0x4C9630 (258 B). The scrollbar's control-event handler.
int __fastcall scroll_ctrl_scroll(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  scroll_t *pScroll = reinterpret_cast<scroll_t *>(pCtrl);

  switch (pEvent->wTag) {
  case 14:
    switch (pEvent->dwId) {
    case 10:
      scroll_init(pScroll);
      break;
    case 6:
      pScroll->dwFlags &= ~SCROLL_PARTS;
      break;
    case 13:
    case 14:
      if (pScroll->nIndex & 0x8000)
        return 1;
      break;
    case 15:
      scroll_set_pos(pScroll, (short)(pScroll->nPos - 1));
      break;
    case 16:
      scroll_set_pos(pScroll, (short)(pScroll->nPos + 1));
      break;
    case 11:
      scroll_set_pos(pScroll, (short)pEvent->dwParam);
      return 1;
    }
    return ctrl_evn_dlg(pCtrl, pEvent);
  case 0:
  case 1:
    return scroll_key_down(pScroll, pEvent);
  case 4:
  case 6:
    return scroll_lmouse_down(pScroll, pEvent);
  case 5:
    scroll_check_state(pScroll, pEvent);
    pScroll->dwFlags &= ~SCROLL_PARTS;
    ctrl_set_update(pCtrl);
    dlg_unhook_all(pCtrl);
    return 0;
  case 3:
    scroll_check_state(pScroll, pEvent);
    if (pScroll->dwPart == SCROLL_THUMB) {
      pScroll->nTick = 1;
      scroll_timer(pScroll, pEvent);
    }
    return 1;
  case 13:
    scroll_timer(pScroll, pEvent);
    break;
  }
  return 0;
}

// 0x4C9790 (386 B). Which region of the bar the cursor is in: 0 outside, 1 and
// 2 the arrow buttons, 3 the thumb, 4 and 5 the slot either side of it, 6
// inside the bar but none of those. The second pt_in_rect runs against a copy
// of the same rectangle and so can never disagree with the first.
int scroll_find_part(scroll_t *pScroll, dlgevt_t *pEvent)
{
  dlg_dword_t dwNative;
  unsigned short wSpan;
  unsigned short wStart;
  unsigned short wEnds;
  unsigned short wThumb;
  short nPt;
  int nStart;
  int nEnds;
  int nPos;

  // Scoped, so pt and rc's stack slots can be reused, as in 1.17.
  {
    point16_t pt;
    rect16_t rc;

    global_evt_2_local_point(reinterpret_cast<ctrl_t *>(pScroll), pEvent, &pt);
    if (pt_in_rect(&pScroll->rc, &pt) == 0)
      return 0;
    rc.left = pScroll->rc.left;
    rc.right = pScroll->rc.right;
    rc.top = pScroll->rc.top;
    rc.bottom = pScroll->rc.bottom;
    if (pt_in_rect(&rc, &pt) == 0)
      return 6;
    dwNative = pScroll->dwFlags;
    if (pScroll->wType == 6) {
      wSpan = (unsigned short)(rc.right - rc.left + 1);
      nPt = pt.x;
      wStart = rc.left;
      dwNative &= SCROLL_NATIVE;
      wEnds = (unsigned short)((dwNative ? 0 : gwHArrowWid) + 2);
      wThumb = dwNative ? gwSliderSlotOnWid_g : gwThumbWid;
    } else {
      wSpan = (unsigned short)(rc.bottom - rc.top + 1);
      nPt = pt.y;
      wStart = rc.top;
      dwNative &= SCROLL_NATIVE;
      wEnds = (unsigned short)((dwNative ? 0 : gwVArrowHgt) + 2);
      wThumb = dwNative ? gwSliderSlotOnWid_g : gwThumbHgt;
    }
  }
  nEnds = wEnds;
  nStart = (short)wStart;
  nPos = nPt;
  if (nPos <= nStart + nEnds)
    return 1;
  if (nPos >= (short)wSpan - nEnds + nStart)
    return 2;
  wStart = (unsigned short)(wStart + (scroll_calc_thumb_offset(pScroll) + wEnds));
  if (nPt < (short)wStart)
    return 4;
  return (nPt >= (short)(wStart + wThumb)) ? 5 : 3;
}

// 0x4C9920 (5 B).
short __fastcall scroll_get_pos(ctrl_t *pScroll)
{
  return reinterpret_cast<scroll_t *>(pScroll)->nPos;
}

// 0x4C9930 (124 B). Size the bar to the theme: a horizontal one gets a fixed
// height and a vertical one a fixed width, and a native one gets its thumb
// bitmap instead.
void scroll_init(scroll_t *pScroll)
{
  if (pScroll->wType == 6) {
    if (pScroll->dwFlags & SCROLL_NATIVE) {
      int nBottom;

      pScroll->nThumbFrame = 0x64;
      pScroll->nThumbSegs = 1;
      nBottom = pScroll->rc.top + gwUnk26_g;
      if (pScroll->rc.bottom > nBottom)
        nBottom = pScroll->rc.bottom;
      pScroll->rc.bottom = (short)nBottom;
    } else {
      int nBottom = pScroll->rc.top + gwVArrowWid;

      if (pScroll->rc.bottom > nBottom)
        nBottom = pScroll->rc.bottom;
      pScroll->rc.bottom = (short)nBottom;
    }
  } else if (pScroll->wType == 7) {
    if (pScroll->dwFlags & SCROLL_NATIVE) {
      pScroll->nThumbFrame = 0x64;
      pScroll->nThumbSegs = 1;
    } else {
      int nRight = pScroll->rc.left + gwHArrowHgt;

      if (pScroll->rc.right > nRight)
        nRight = pScroll->rc.right;
      pScroll->rc.right = (short)nRight;
    }
  }
}

// 0x4C99B0 (146 B). The eight navigation keys, each valid on only one
// orientation, all funnelled into one position message.
BOOL scroll_key_down(scroll_t *pScroll, dlgevt_t *pEvent)
{
  unsigned short wPos;

  switch (evt_vkey(pEvent)) {
  case 0x26:
    if (pScroll->wType != 7)
      return 0;
    wPos = (unsigned short)(pScroll->nPos - 1);
    break;
  case 0x25:
    if (pScroll->wType != 6)
      return 0;
    wPos = (unsigned short)(pScroll->nPos - 1);
    break;
  case 0x28:
    if (pScroll->wType != 7)
      return 0;
    wPos = (unsigned short)(pScroll->nPos + 1);
    break;
  case 0x27:
    if (pScroll->wType != 6)
      return 0;
    wPos = (unsigned short)(pScroll->nPos + 1);
    break;
  case 0x24:
    wPos = pScroll->nMin;
    break;
  case 0x23:
    wPos = pScroll->nMax;
    break;
  case 0x21:
    wPos = (unsigned short)(pScroll->nPos - pScroll->nPage);
    break;
  case 0x22:
    wPos = (unsigned short)(pScroll->nPage + pScroll->nPos);
    break;
  default:
    return 0;
  }
  ctrl_send_msg(reinterpret_cast<ctrl_t *>(pScroll), 0x0B, (short)wPos);
  return 1;
}

// 0x4C9A70 (83 B). Begin a drag on whichever part the cursor is over. The two
// page regions carry the page size, so their table entries are rewritten first.
unsigned char scroll_lmouse_down(scroll_t *pScroll, dlgevt_t *pEvent)
{
  int nPage = pScroll->nPage;
  int nPart;

  sgnPart2ScrollDeltaTbl[5] = (short)nPage;
  sgnPart2ScrollDeltaTbl[4] = (short)-nPage;
  nPart = scroll_find_part(pScroll, pEvent);
  if (nPart == 0)
    return 0;
  if (nPart != 6) {
    start_scroll(pScroll, pEvent, sgnPart2ScrollDeltaTbl[nPart],
                 sgwPart2ScrollFlagTbl[nPart]);
    snd_play_btn(2);
  }
  return 1;
}

// 0x4C9AD0 (16 B).
void __fastcall scroll_set_page_size(ctrl_t *pScroll, unsigned char nPage, ULONG dwFlag)
{
  scroll_t *p = reinterpret_cast<scroll_t *>(pScroll);

  p->nPage = nPage;
  p->nThumbSegs = (dlg_byte_t)dwFlag;
}

// 0x4C9AE0 (53 B). Clamp into range, publish, tell the owner, redraw. The
// callback is read into a local before the store so that it is loaded once.
void scroll_set_pos(scroll_t *pScroll, short nPos)
{
  if (nPos < pScroll->nMin)
    nPos = pScroll->nMin;
  else if (nPos > pScroll->nMax)
    nPos = pScroll->nMax;
  if (pScroll->nPos != nPos) {
    void *pfn = pScroll->pfnNotify;

    pScroll->nPos = nPos;
    if (pfn != 0)
      reinterpret_cast<PFN_SCROLL_NOTIFY>(pfn)(pScroll->pNotifyArg);
  }
  ctrl_set_update(reinterpret_cast<ctrl_t *>(pScroll));
}

// 0x4C9B20 (99 B). Move the end stops, then pull the position back inside them
// if either actually moved. The callback is re-read after the store here, not
// cached as in scroll_set_pos.
void __fastcall scroll_set_range(ctrl_t *pScroll, ULONG dwMin, unsigned short wMax)
{
  scroll_t *p = reinterpret_cast<scroll_t *>(pScroll);
  dlg_byte_t bMoved = 0;

  if (p->nMin != (short)dwMin) {
    p->nMin = (short)dwMin;
    bMoved = 1;
  }
  if (p->nMax != (short)wMax) {
    p->nMax = (short)wMax;
    bMoved = 1;
  }
  if (bMoved) {
    short nPos = p->nPos;

    if (nPos < p->nMin)
      nPos = p->nMin;
    else if (nPos > p->nMax)
      nPos = p->nMax;
    if (p->nPos != nPos) {
      p->nPos = nPos;
      if (p->pfnNotify != 0)
        reinterpret_cast<PFN_SCROLL_NOTIFY>(p->pfnNotify)(p->pNotifyArg);
    }
    ctrl_set_update(pScroll);
  }
}

// 0x4C9B90 (66 B). One auto-repeat tick: the first repeat waits three timer
// beats, an arrow press thereafter waits one.
void scroll_timer(scroll_t *pScroll, dlgevt_t *pEvent)
{
  if (--pScroll->nTick == 0) {
    pScroll->nTick = (pScroll->dwPart & 0x60000000) ? 1 : 3;
    scroll_check_state(pScroll, pEvent);
    if (pScroll->dwFlags & pScroll->dwPart)
      ctrl_send_msg(reinterpret_cast<ctrl_t *>(pScroll), 0x0B,
                    pScroll->nDelta + pScroll->nPos);
  }
}

// 0x4C9BE0 (142 B). Take the mouse, record which part is held and how far each
// repeat moves, then run the first tick at once.
void start_scroll(scroll_t *pScroll, dlgevt_t *pEvent, short nDelta, ULONG dwPart)
{
  dlg_hook(reinterpret_cast<ctrl_t *>(pScroll), 5);
  dlg_hook(reinterpret_cast<ctrl_t *>(pScroll), 3);
  dlg_hook(reinterpret_cast<ctrl_t *>(pScroll), 0x0D);
  pScroll->nDelta = nDelta;
  pScroll->dwFlags |= dwPart;
  pScroll->nTick = 1;
  pScroll->dwPart = dwPart;
  ctrl_set_update(reinterpret_cast<ctrl_t *>(pScroll));
  scroll_timer(pScroll, pEvent);
}
