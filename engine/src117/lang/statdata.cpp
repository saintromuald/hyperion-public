// statdata.cpp -- the status panel (oracle 0x4A19E0 to 0x4A30xx): the
// lower-left dialog with the selected unit's portrait, hit points, shields,
// energy, resource counts, build and research progress, transport slots and
// the production queue. Its controls are found by index and shown or hidden in
// sibling runs, so most of these bodies walk ctrl_t::pNext. The signature
// facts are in statdata.h.
//
// /Ob2 inlines an in-TU callee: adding one can splice it into a caller that
// must emit a real call, and removing one breaks a body that relies on it
// being inlined.
#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include "platform.h"
#include "statdata.h"
// The hover and context-help callees come from the headers that own them, so
// each has one declaration.
#include "net_cmds.h"   // IsGamePaused, send_net_cmd_ExitTransport
#include "minimap.h"    // ctxhelp_hide
#include "order.h"      // status_get_str
#include "victory.h"    // psnprintf
#include "CUnit.h"      // CUnit::GetName
#include "../h/playervision.h"  // Hyperion: UnitParasite (stage D)
#include "CImage.h"     // rle_draw_normal
#include "rect32.h"     // rect32_t + the USER32 SetRect import
// grp.h, for GROUP and FRAME, is included here and kept out of statdata.h.
#include "grp.h"        // GROUP, FRAME
#include <string.h>
#include "dlg_dlg.h"    // dlg_load, dlg_activate, dlg_deactivate
#include "snd.h"        // load_game_data
#include "glues.h"      // alloc_load_bmp, mem_free
#include "CUnitMisc.h"  // get_unit_name_from_type
#include "saveload.h"   // get_upgrade_level
#include "statwire.h"   // the wireframe tint helpers
#include "statdraw.h"   // the per-type query and draw layer
#include "bmp.h"        // bmp_t, bmpsrc_t, bmp_copy_rect
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../port/data/port_moddata.h"  // Hyperion: ModUnitArtFrame, ModConsoleGroup
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX
#include "../port/render/port_console.h"  // Hyperion: PortConsoleRace
#include "../port/hud/port_hudinput.h"  // Hyperion: PortHudConsoleCursor

// The globals this TU reaches, dual-spelled: one #ifndef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.
//
// 0x6AAB1C and 0x6AAB24 are not globals: they are the +0x04 pPixels field of
// the two bmp_t objects below, read at +4 and addressed at +0.
#ifdef OC_DLL_MODE
  #ifndef sgBmpStatProgressbarEmpty_g
  #define sgBmpStatProgressbarEmpty_g (*reinterpret_cast<bmp_t *>(OC_VA(0x6AAB18u)))
  #endif
  #ifndef sgBmpStatProgressbarFull_g
  #define sgBmpStatProgressbarFull_g  (*reinterpret_cast<bmp_t *>(OC_VA(0x6AAB20u)))
  #endif
  #ifndef sgbStatDataWantUpdate_g
  #define sgbStatDataWantUpdate_g     (*reinterpret_cast<BYTE *>(OC_VA(0x6AAB30u)))
  #endif
  #ifndef sgbStatType_g
  #define sgbStatType_g               (*reinterpret_cast<BYTE *>(OC_VA(0x6AAB31u)))
  #endif
  #ifndef sgpStatDataHoverCtrl_g
  #define sgpStatDataHoverCtrl_g      (*reinterpret_cast<void **>(OC_VA(0x6AAB38u)))
  #endif
  #ifndef sgpStatDataHoverData_g
  #define sgpStatDataHoverData_g      (*reinterpret_cast<void **>(OC_VA(0x6AAB3Cu)))
  #endif
#else
extern bmp_t sgBmpStatProgressbarEmpty_g;
extern bmp_t sgBmpStatProgressbarFull_g;
extern BYTE  sgbStatDataWantUpdate_g;
extern BYTE  sgbStatType_g;
extern void *sgpStatDataHoverCtrl_g;
extern void *sgpStatDataHoverData_g;
#endif

// gCtrlEvtFcnTbl @0x4F10F8 (.rdata, dlg/ctrl.cpp) -- the per-control-type
// event table, indexed by ctrl_t::wType; 20 entries, up to gCtrlDrawFcnTbl
// @0x4F1148.
#ifdef OC_DLL_MODE
  #ifndef gCtrlEvtFcnTbl
  #define gCtrlEvtFcnTbl (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x4F10F8u)))
  #endif
#else
extern PFN_CTRL_EVT gCtrlEvtFcnTbl[20];
#endif

// sgpFonts_g -- void *[4] @0x6D3024 (dlg/dlg.cpp). The draw handlers use
// element 0.
#ifdef OC_DLL_MODE
  #ifndef sgpFonts_g
  #define sgpFonts_g (reinterpret_cast<void **>(OC_VA(0x6D3024u)))
  #endif
#else
extern void *sgpFonts_g[4];
#endif

// gbFontJust -- BYTE @0x6D300E (gds/font.cpp).
#ifdef OC_DLL_MODE
  #ifndef gbFontJust
  #define gbFontJust (*reinterpret_cast<BYTE *>(OC_VA(0x6D300Eu)))
  #endif
#else
extern BYTE gbFontJust;
#endif

// gsFontMargins -- rect16_t @0x6D3038 (gds/font.cpp). 0x6D303C is its back
// half, not a second global; the oracle copies the 8 bytes as two dwords.
#ifdef OC_DLL_MODE
  #ifndef gsFontMargins
  #define gsFontMargins (*reinterpret_cast<rect16_t *>(OC_VA(0x6D3038u)))
  #endif
#else
extern rect16_t gsFontMargins;
#endif

// font_set -- oracle 0x4CE160, spelled as dlg_draw.h and dlg_dlg.h declare it.
void __fastcall font_set(void *pFont);

// font_adjust -- oracle 0x4CDF20. ECX = x, EDX = y, one stack dword = the
// string, `ret 4h`. Declared here and not in a header, character for character
// as dlg_draw.cpp does; dlg_draw.h records why.
const char *__fastcall font_adjust(int nX, int nY, const char *psz);

// USER32!SetRect through the IAT slot 0x4EB630, declared as dlg_dlgdraw.cpp
// declares it to stay clear of <windows.h>; extern "C" __stdcall with five
// dwords links to `__imp__SetRect@20`.
extern "C" __declspec(dllimport)
int __stdcall SetRect(rect32_t *prc, int xLeft, int yTop, int xRight,
                      int yBottom);

// grh_get_header -- oracle 0x463BD0. `ret 8h`: two register and two stack
// parameters; grhsize_t is in statdata.h. The frame is a WORD: the callee
// compares it in 16 bits against the masked frame count.
void __fastcall grh_get_header(GROUP *pGroup, WORD wFrame, FRAME *pFrame,
                               grhsize_t *pSize);

// gpCmdBtns -- GROUP * @0x6AAA70 (statcmd.cpp), one dword: gpCmdIcons
// @0x6AAA74 follows it.
#ifdef OC_DLL_MODE
  #ifndef gpCmdBtns
  #define gpCmdBtns (*reinterpret_cast<GROUP **>(OC_VA(0x6AAA70u)))
  #endif
#else
extern GROUP *gpCmdBtns;
#endif

// Hyperion: gpCmdIcons -- void * @0x6AAA74 (statcmd.cpp), the command icons, of which a
// mod's own may be left to the GPU
#ifdef OC_DLL_MODE
  #ifndef gpCmdIcons
  #define gpCmdIcons (*reinterpret_cast<void **>(OC_VA(0x6AAA74u)))
  #endif
#else
extern void *gpCmdIcons;
#endif

// sgpStatDataDlg -- ctrl_t * @0x6AAB34, the panel dialog;
// statdata_update_hover hands it to ctrl_find.
#ifdef OC_DLL_MODE
  #ifndef sgpStatDataDlg
  #define sgpStatDataDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x6AAB34u)))
  #endif
#else
extern ctrl_t *sgpStatDataDlg;
#endif

// gCursorPos_g -- long[2] @0x6D3154, spelled as dlg_ctrl.cpp spells it.
// 0x6D3158 is gCursorPos_g[1], not a second global.
#ifdef OC_DLL_MODE
  #ifndef gCursorPos_g
  #define gCursorPos_g (reinterpret_cast<LONG *>(OC_VA(0x6D3154u)))
  #endif
#else
extern LONG gCursorPos_g[2];
#endif

// guwTechStr -- WORD[44] @0x51F9C8 (arr/techdata.cpp), each tech's name
// string id; tech_race @0x51FA20 follows it.
#ifdef OC_DLL_MODE
  #ifndef guwTechStr
  #define guwTechStr (reinterpret_cast<WORD *>(OC_VA(0x51F9C8u)))
  #endif
#else
extern WORD guwTechStr[44] OC_UNALIGNED;
#endif

// guwUpgradeStr -- WORD[62] @0x524CD0 (arr/upgrades.cpp), each upgrade's name
// string id; upg_race @0x524D4C follows it.
#ifdef OC_DLL_MODE
  #ifndef guwUpgradeStr
  #define guwUpgradeStr (reinterpret_cast<WORD *>(OC_VA(0x524CD0u)))
  #endif
#else
extern WORD guwUpgradeStr[62];
#endif

// ctrl_t +0x26 -- a pointer payload. With ctrl_progress_slot's WORD at +0x24 it
// fills dialog.h's `_unmapped24[6]`, reached by cast rather than widened, as
// dlg_draw.cpp's ctrl_bmp() does. It is void *: the icon buttons store a
// 12-byte block here, statdata_ctx_trans reads a CUnit from it, and
// statdata_dlg_mouse_moved only compares it with sgpStatDataHoverData_g.
static inline void *&ctrl_data26(ctrl_t *pCtrl) {
  return *reinterpret_cast<void **>(&pCtrl->_unmapped24[2]);
}

// ctrl_t +0x24 -- a WORD: statdata_set_progress stores it, and
// statdata_progressbar_draw reads it unsigned before the /100.
static inline unsigned short *ctrl_progress_slot(ctrl_t *pCtrl) {
  return reinterpret_cast<unsigned short *>(&pCtrl->_unmapped24[0]);
}

BYTE __fastcall statdata_get_type() {
  return sgbStatType_g;
}

void __fastcall statdata_set_type(BYTE bType) {
  sgbStatType_g = bType;
}

void __fastcall request_statdata_update() {
  sgbStatDataWantUpdate_g = 1;
  sgpStatDataHoverCtrl_g = NULL;
  sgpStatDataHoverData_g = NULL;
}

// The hop to the children happens only when wType is 0: ctrl_find_ndx's own
// first two instructions, inlined, without the index search.
void __fastcall statdata_clr_all(ctrl_t *pCtrl) {
  ctrl_t *p = pCtrl;
  if (p->wType == 0)
    p = p->pChildren;
  while (p) {
    ctrl_hide(p);
    p = p->pNext;
  }
}

// The find result is dereferenced with no null check, as in 1.17.
void __fastcall statdata_clr_stats(ctrl_t *pCtrl) {
  ctrl_t *p = ctrl_find_ndx(pCtrl, -7)->pNext;
  while (p) {
    ctrl_hide(p);
    p = p->pNext;
  }
}

// Neither ECX nor EDX is written before `call ctrl_find_ndx`: both pass
// through, so the second parameter is a short index. The value comes on the
// stack (`ret 4h`); the find result is again used unchecked.
void __fastcall statdata_set_progress(ctrl_t *pCtrl, short nIndex,
                                      short nValue) {
  ctrl_t *p = ctrl_find_ndx(pCtrl, nIndex);
  *ctrl_progress_slot(p) = nValue;
  ctrl_show(p);
  ctrl_set_update(p);
}

// Six bodies of one shape: show every node of the sibling run from the start
// index, stopping after the stop index. The test sits at the loop's bottom, so
// the stop node is shown too.
// 0x4A2450, 39 B
void __fastcall statdata_show_progress(ctrl_t *pCtrl) {
  ctrl_t *p = ctrl_find_ndx(pCtrl, -0x28);
  while (p) {
    ctrl_show(p);
    if (p->nIndex == 0x11)
      break;
    p = p->pNext;
  }
}

// 0x4A2480, 39 B
void __fastcall statdata_show_progress_self(ctrl_t *pCtrl) {
  ctrl_t *p = ctrl_find_ndx(pCtrl, -0x1E);
  while (p) {
    ctrl_show(p);
    if (p->nIndex == 0x0D)
      break;
    p = p->pNext;
  }
}

// 0x4A24B0, 39 B
void __fastcall statdata_show_progress_queue(ctrl_t *pCtrl) {
  ctrl_t *p = ctrl_find_ndx(pCtrl, -0x0F);
  while (p) {
    ctrl_show(p);
    if (p->nIndex == 0x08)
      break;
    p = p->pNext;
  }
}

// 0x4A24E0, 39 B
void __fastcall statdata_show_progress_research(ctrl_t *pCtrl) {
  ctrl_t *p = ctrl_find_ndx(pCtrl, -0x23);
  while (p) {
    ctrl_show(p);
    if (p->nIndex == 0x0F)
      break;
    p = p->pNext;
  }
}

// The negative stop constant needs ctrl_t::nIndex signed: unsigned, the
// compare would be against 0xFFF3.
// 0x4A2510, 39 B
void __fastcall statdata_show_resources(ctrl_t *pCtrl) {
  ctrl_t *p = ctrl_find_ndx(pCtrl, -0x0A);
  while (p) {
    ctrl_show(p);
    if (p->nIndex == -0x0D)
      break;
    p = p->pNext;
  }
}

// 0x4A2540, 39 B
void __fastcall statdata_show_stats(ctrl_t *pCtrl) {
  ctrl_t *p = ctrl_find_ndx(pCtrl, -0x14);
  while (p) {
    ctrl_show(p);
    if (p->nIndex == 0x0C)
      break;
    p = p->pNext;
  }
}

// Both accumulators start at 0 and the parent's rect is skipped when wType is
// 0, so a top-level node adds no offset. The X term is rc.right: the tooltip
// anchors off the right edge.
void __fastcall statdata_ctx_show(ctrl_t *pCtrl, intptr_t nParam) {
  int nParentTop = 0;
  int nParentLeft = 0;
  if (pCtrl->wType != 0) {
    ctrl_t *pParent = pCtrl->pParent;
    nParentLeft = pParent->rc.left;
    nParentTop = pParent->rc.top;
  }
  ctxhelp_show(pCtrl->rc.right + nParentLeft, pCtrl->rc.top + nParentTop,
               nParam);
}

// The guard reads bmp_t::pPixels (+0x04) and the call takes the object (+0x00),
// so 0x6AAB1C and 0x6AAB24 are fields, not globals.
void __fastcall statdata_progressbar_deactivate(ctrl_t *) {
  if (sgBmpStatProgressbarEmpty_g.pPixels) {
    bmp_destroy(&sgBmpStatProgressbarEmpty_g);
    sgBmpStatProgressbarEmpty_g.pPixels = NULL;
  }
  if (sgBmpStatProgressbarFull_g.pPixels) {
    bmp_destroy(&sgBmpStatProgressbarFull_g);
    sgBmpStatProgressbarFull_g.pPixels = NULL;
  }
}

// The icon and queue draw handlers, PFN_CTRL_DRAW (`ret 8h`), ignore nXOfs,
// nYOfs and prcClip. The two differ only in the order of the two icon
// painters. gsFontMargins takes the control's rect, not the clip rect.

// 0x4A2660, 82 B
void __fastcall statdata_icon_info_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                        rect16_t *prcClip) {
  statdata_slot_frame_draw(pCtrl);
  statdata_slot_icon_draw(pCtrl);
  if (pCtrl->pData) {
    PortConsoleSlotTextBegin(pCtrl);  // Hyperion: the text's pixels for the HD console
    font_set(sgpFonts_g[0]);
    gbFontJust = 0x44;
    gsFontMargins = pCtrl->rc;
    font_adjust(pCtrl->rc.left, pCtrl->rc.top,
                reinterpret_cast<const char *>(pCtrl->pData));
    font_set(NULL);
    PortConsoleSlotTextEnd(pCtrl);
  }
}

// 0x4A28E0, 82 B
void __fastcall statdata_btn_queue_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                        rect16_t *prcClip) {
  statdata_slot_icon_draw(pCtrl);
  statdata_slot_frame_draw(pCtrl);
  if (pCtrl->pData) {
    PortConsoleSlotTextBegin(pCtrl);  // Hyperion: the text's pixels for the HD console
    font_set(sgpFonts_g[0]);
    gbFontJust = 0x44;
    gsFontMargins = pCtrl->rc;
    font_adjust(pCtrl->rc.left, pCtrl->rc.top,
                reinterpret_cast<const char *>(pCtrl->pData));
    font_set(NULL);
    PortConsoleSlotTextEnd(pCtrl);
  }
}

// Installs its own draw handler and the type-default event handler on first
// paint, then tail-dispatches through the same table, whose result it returns.
int __fastcall statdata_queue_arrow_ctrl_fcn(ctrl_t *pCtrl,
                                             dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E && pEvent->dwId == 0) {
    pCtrl->pfnDraw = statdata_queue_arrow_draw;
    pCtrl->pfnEvt = gCtrlEvtFcnTbl[pCtrl->wType];
  }
  return gCtrlEvtFcnTbl[pCtrl->wType](pCtrl, pEvent);
}

// The two move handlers return void: every exit is a bare ret or a tail jump
// into a void callee. Neither is in gCtrlEvtFcnTbl; their callers reach them
// from a jump table on the event id and discard EAX.

// The dialog's move handler: re-hit-test, and if the hit node or its payload
// differs from the cache, republish it and refresh the tooltip.
// 0x4A1FA0, 83 B
void __fastcall statdata_dlg_mouse_moved(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  ctrl_t *pHit = IsGamePaused() ? NULL : ctrl_find(pCtrl, pEvent);
  if (pHit != sgpStatDataHoverCtrl_g ||
      (pHit != NULL && ctrl_data26(pHit) != sgpStatDataHoverData_g)) {
    sgpStatDataHoverCtrl_g = pHit;
    if (pHit != NULL)
      sgpStatDataHoverData_g = ctrl_data26(pHit);
    statdata_hover_update(pCtrl, pHit);
  }
}

// The icon button's move handler. It hit-tests against its own parent and
// drops the tooltip when the hit is not itself; both bail-outs cache NULL and a
// -1 payload, where request_statdata_update stores NULL in both. The last arm
// loads the payload and the parent before the stores, as 1.17 does: the stores
// go through a void * that may alias the payload slot.
// 0x4A1AE0, 118 B
void __fastcall statdata_icon_btn_mouse_moved(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (IsGamePaused()) {
    sgpStatDataHoverCtrl_g = NULL;
    sgpStatDataHoverData_g = reinterpret_cast<void *>(-1);
    ctxhelp_hide();
    return;
  }
  if (pCtrl != ctrl_find(pCtrl->pParent, pEvent)) {
    sgpStatDataHoverCtrl_g = NULL;
    sgpStatDataHoverData_g = reinterpret_cast<void *>(-1);
    ctxhelp_hide();
    return;
  }
  if (pCtrl != sgpStatDataHoverCtrl_g) {
    void *pData = ctrl_data26(pCtrl);
    ctrl_t *pDlg = pCtrl->pParent;
    sgpStatDataHoverCtrl_g = pCtrl;
    sgpStatDataHoverData_g = pData;
    statdata_hover_update(pDlg, pCtrl);
  }
}

// The two context-help builders below inline statdata_ctx_show, so it must
// stay in this TU.

// The transport slot's tooltip: one format string and the passenger's name.
// 0x4A3000, 98 B
void __fastcall statdata_ctx_trans(ctrl_t *pCtrl) {
  char szText[0x80];
  psnprintf(szText, sizeof(szText), status_get_str(0x328),
            static_cast<CUnit *>(ctrl_data26(pCtrl))->GetName());
  statdata_ctx_show(pCtrl, reinterpret_cast<intptr_t>(szText));
}

// The research or upgrade slot's tooltip. An icon of any other kind returns
// with no tooltip, as in 1.17.
// 0x4A2F50, 163 B
void __fastcall statdata_ctx_research(ctrl_t *pCtrl) {
  char szText[0x104];
  statdata_icon_t *pItem = static_cast<statdata_icon_t *>(ctrl_data26(pCtrl));
  char *pszLabel;
  WORD wName;
  switch (pItem->wKind) {
  case 4:
    pszLabel = status_get_str(0x326);
    wName = guwTechStr[pItem->wParam];
    break;
  case 5:
    pszLabel = status_get_str(0x325);
    wName = guwUpgradeStr[pItem->wParam];
    break;
  default:
    return;
  }
  psnprintf(szText, sizeof(szText), "%s\n(%s)", pszLabel, status_get_str(wName));
  statdata_ctx_show(pCtrl, reinterpret_cast<intptr_t>(szText));
}

// Frame 11 of the command-button GRP, centred horizontally in the control and
// top-aligned, clipped to the smaller of the frame's and the control's size.
// The clamps are signed compares. The centring term is right - bWidth - left,
// without the +1 the clamp's width carries, as in 1.17.
// 0x4A22B0, 145 B
void __fastcall statdata_queue_arrow_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                          rect16_t *prcClip) {
  rect32_t rc;
  FRAME frame;
  grhsize_t size;
  int nWidth;
  int nHeight;

  grh_get_header(gpCmdBtns, 0x0B, &frame, &size);

  nWidth = pCtrl->rc.right - pCtrl->rc.left + 1;
  if (frame.bWidth < nWidth)
    nWidth = frame.bWidth;
  nHeight = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  if (frame.bHeight < nHeight)
    nHeight = frame.bHeight;
  SetRect(&rc, 0, 0, nWidth, nHeight);

  rle_draw_normal(pCtrl->rc.left +
                      (pCtrl->rc.right - frame.bWidth - pCtrl->rc.left) / 2,
                  pCtrl->rc.top, &frame, &rc, NULL);
}

// Two blits of one shape: the empty bar at full extent, then the full bar
// clipped to width * progress / 100, each guarded on the bitmap's pPixels
// (+0x04). The first block's width arithmetic is 16-bit, the second 32-bit for
// the product with the progress WORD; its /100 is signed.
// 0x4A1C70 (202 B)
void __fastcall statdata_progressbar_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                          rect16_t *prcClip) {
  bmpsrc_t src;

  src.rc.left = 0;
  src.rc.top = 0;
  src.nWidth = pCtrl->rc.right - pCtrl->rc.left + 1;
  src.nHeight = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  src.rc.right = src.rc.left + src.nWidth - 1;
  src.rc.bottom = src.rc.top + src.nHeight - 1;
  if (sgBmpStatProgressbarEmpty_g.pPixels)
    bmp_copy_rect(pCtrl->rc.left, pCtrl->rc.top, &sgBmpStatProgressbarEmpty_g,
                  &src);

  src.nWidth = (short)((pCtrl->rc.right - pCtrl->rc.left + 1) *
                       *ctrl_progress_slot(pCtrl) / 100);
  src.nHeight = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  src.rc.right = src.rc.left + src.nWidth - 1;
  src.rc.bottom = src.rc.top + src.nHeight - 1;
  if (sgBmpStatProgressbarFull_g.pPixels)
    bmp_copy_rect(pCtrl->rc.left, pCtrl->rc.top, &sgBmpStatProgressbarFull_g,
                  &src);
}

// 0x4A1D40, 54 B. The progress bar's event handler: build or tear
// down the bar bitmaps on activate/deactivate, then tail-dispatch through the
// type-default handler.
int __fastcall statdata_progressbar_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 0:
      statdata_progressbar_activate(pCtrl);
      break;
    case 1:
      statdata_progressbar_deactivate(pCtrl);
      break;
    }
  }
  return gCtrlEvtFcnTbl[pCtrl->wType](pCtrl, pEvent);
}

// No parameters. Builds a dlgevt_t carrying only the cursor position, the rest
// left as the stack held it, hit-tests the panel dialog, and republishes the
// hover pair only when it changed. While the game is paused the hit is NULL, so
// a pause still drops the tooltip. The dialog load sits between the two cursor
// stores on purpose: written after both, the DLL-mode compile puts it after the
// second store, unlike 1.17.
// 0x4A2B70, 113 B
void __fastcall statdata_update_hover() {
  dlgevt_t evt;
  ctrl_t *pDlg;
  ctrl_t *pHit;

  {
    LONG anCursor[2];
    PortHudConsoleCursor(anCursor);  // Hyperion: where the HUD shows the selection
    evt.nX = (short)anCursor[0];
    evt.nY = (short)anCursor[1];
  }
  pDlg = sgpStatDataDlg;
  pHit = IsGamePaused() ? NULL : ctrl_find(pDlg, &evt);
  if (pHit != sgpStatDataHoverCtrl_g ||
      (pHit != NULL && ctrl_data26(pHit) != sgpStatDataHoverData_g)) {
    sgpStatDataHoverCtrl_g = pHit;
    if (pHit != NULL)
      sgpStatDataHoverData_g = ctrl_data26(pHit);
    statdata_hover_update(pDlg, pHit);
  }
}

// The transport slot button's event handler. The arm map comes from the jump
// table at 0x4A29C4: seven entries, five of them the shared tail. The wTag 3
// arm is not in it but reached from the outer test, so the outer construct is
// a switch on wTag of its own.
// 0x4A2940, 131 B
int __fastcall statdata_transport_btn_ctrl_fcn(ctrl_t *pCtrl,
                                               dlgevt_t *pEvent) {
  switch (pEvent->wTag) {
  case 3:
    statdata_icon_btn_mouse_moved(pCtrl, pEvent);
    break;
  case 0x0E:
    switch (pEvent->dwId) {
    case 0:
      pCtrl->pfnDraw = statdata_transport_btn_draw;
      *ctrl_progress_slot(pCtrl) = 0;
      ctrl_data26(pCtrl) = NULL;
      break;
    case 2:
      send_net_cmd_ExitTransport(static_cast<CUnit *>(ctrl_data26(pCtrl)));
      return 1;
    case 4:
      if (pCtrl->dwFlags & 8)
        return 1;
      break;
    case 6:
      if (pCtrl == sgpStatDataHoverCtrl_g) {
        ctxhelp_hide();
        sgpStatDataHoverCtrl_g = NULL;
      }
      break;
    }
    break;
  }
  return gCtrlEvtFcnTbl[pCtrl->wType](pCtrl, pEvent);
}

// The two wireframe GRPs and the panel's event table.
#ifdef OC_DLL_MODE
  #ifndef sgpTransWires
  #define sgpTransWires (*reinterpret_cast<GROUP **>(OC_VA(0x6AAB28u)))
  #endif
  #ifndef sgpMultSelWires
  #define sgpMultSelWires (*reinterpret_cast<GROUP **>(OC_VA(0x6AAB2Cu)))
  #endif
  #ifndef sStatDataEvtFcnTbl_g
  #define sStatDataEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x4EFF50u)))
  #endif
#else
extern GROUP *sgpTransWires;
extern GROUP *sgpMultSelWires;
extern PFN_CTRL_EVT sStatDataEvtFcnTbl_g[44];
#endif

// Storm ordinals 401 and 403, spelled as src117/h/Storm.h spells them; that
// header pulls <windows.h>, whose SetRect collides with the one declared above.
extern "C" void *__stdcall SMemAlloc(DWORD size, const char *ownerTag,
                                     int ownerLine, DWORD flags);
extern "C" int __stdcall SMemFree(const void *ptr, const char *ownerTag,
                                  int ownerLine, DWORD flags);

#ifdef OC_DLL_MODE
  #ifndef gpStatUnit
  #define gpStatUnit (*reinterpret_cast<CUnit **>(OC_VA(0x6AB4C4u)))
  #endif
  #ifndef gPlayback
  #define gPlayback (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
  #endif
  #ifndef sgBmpTIcon_g
  #define sgBmpTIcon_g (reinterpret_cast<BYTE (*)[16]>(OC_VA(0x6AAA88u)))
  #endif
  #ifndef _gbRemapColorTbl
  #define _gbRemapColorTbl (reinterpret_cast<BYTE *>(OC_VA(0x501021u)))
  #endif
  #ifndef byte_5264C2
  #define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
  #endif
  #ifndef gszRaceChars_g
  #define gszRaceChars_g (reinterpret_cast<char *>(OC_VA(0x503358u)))
  #endif
  #ifndef byte_665E68
  #define byte_665E68 (*reinterpret_cast<BYTE *>(OC_VA(0x665E68u)))
  #endif
  #ifndef byte_665E69
  #define byte_665E69 (*reinterpret_cast<BYTE *>(OC_VA(0x665E69u)))
  #endif
  #ifndef gulUnitTypeFlags
  #define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
  #endif
  #ifndef byte_6AB4C0
  #define byte_6AB4C0 (*reinterpret_cast<BYTE *>(OC_VA(0x6AB4C0u)))
  #endif
  #ifndef wpn_label_g
  #define wpn_label_g (reinterpret_cast<WORD *>(OC_VA(0x524E10u)))
  #endif
  #ifndef gubUnitArmor
  #define gubUnitArmor (reinterpret_cast<BYTE *>(OC_VA(0x521D90u)))
  #endif
#else
extern WORD wpn_label_g[];
extern BYTE gubUnitArmor[];
extern BYTE byte_6AB4C0;
extern BYTE byte_665E68;
extern BYTE byte_665E69;
extern DWORD gulUnitTypeFlags[228] OC_UNALIGNED;
extern BYTE byte_5264C2;
extern char gszRaceChars_g[4];
extern BYTE sgBmpTIcon_g[6][16] OC_UNALIGNED;
extern BYTE _gbRemapColorTbl[256] OC_UNALIGNED;
extern CUnit *gpStatUnit;
extern DWORD gPlayback;
#endif

// One row of gTypeFcns per unit type: the panel's change test and its drawer.
struct statdata_typefcn_t {
  /*0x00*/ DWORD dwType;
  /*0x04*/ int(__fastcall *pfnQuery)();
  /*0x08*/ void(__fastcall *pfnDraw)(ctrl_t *pDlg);
};
OC_X86_LAYOUT(sizeof(statdata_typefcn_t) == 0x0C, "statdata_typefcn_t size");
static_assert(offsetof(statdata_typefcn_t, dwType) == 0x00, "statdata_typefcn_t::dwType");
OC_X86_LAYOUT(offsetof(statdata_typefcn_t, pfnQuery) == 0x04, "statdata_typefcn_t::pfnQuery");
OC_X86_LAYOUT(offsetof(statdata_typefcn_t, pfnDraw) == 0x08, "statdata_typefcn_t::pfnDraw");

#ifdef OC_DLL_MODE
  #ifndef gTypeFcns
  #define gTypeFcns (reinterpret_cast<statdata_typefcn_t *>(OC_VA(0x50BA50u)))
  #endif
#else
extern statdata_typefcn_t gTypeFcns[228];
#endif

GROUP *__fastcall ReadGroupFile(const char *pszFile, intptr_t nFlags,
                                const char *pszTag, int nLine);

// Frees the panel dialog and both wireframe GRPs.
void __fastcall statdata_deactivate() {
  if (sgpStatDataDlg)
    dlg_deactivate(sgpStatDataDlg);
  GROUP *pGrp = sgpMultSelWires;
  sgpStatDataDlg = NULL;
  mem_free(pGrp, "statdata.cpp", 0x550);
  pGrp = sgpTransWires;
  sgpMultSelWires = NULL;
  mem_free(pGrp, "statdata.cpp", 0x552);
  sgpTransWires = NULL;
}

// Loads the wireframe GRPs and the panel dialog, and activates it.
void __fastcall statdata_init() {
  sgpTransWires = ReadGroupFile("unit\\wirefram\\tranwire.grp", 0, "statdata.cpp", 0x537);
  sgpMultSelWires = ReadGroupFile("unit\\wirefram\\grpwire.grp", 0, "statdata.cpp", 0x539);
  // Hyperion: with a mod's wireframes
  sgpTransWires = ModConsoleGroup(MOD_ART_TRANWIRE, sgpTransWires);
  sgpMultSelWires = ModConsoleGroup(MOD_ART_GRPWIRE, sgpMultSelWires);
  sgbStatDataWantUpdate_g = 1;
  sgbStatType_g = 0;
  sgpStatDataHoverCtrl_g = NULL;
  sgpStatDataHoverData_g = reinterpret_cast<void *>(-1);
  sgpStatDataDlg = dlg_load("rez\\statdata.bin",
                            reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                            reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                            "statdata.cpp", 0x546);
  dlg_activate(sgpStatDataDlg, 0, statdata_dlg_fcn);
}

// The panel dialog's event handler.
int __fastcall statdata_dlg_fcn(ctrl_t *pDlg, dlgevt_t *pEvent) {
  switch (pEvent->wTag) {
  case 0:
  case 1:
  case 15:
    return 0;
  case 3:
    statdata_dlg_mouse_moved(pDlg, pEvent);
    break;
  case 4:
    // A case of its own that folds into the default; the jump table keeps
    // a separate entry for it.
    if (pEvent->dwId == 0)
      break;
    break;
  case 14:
    if (pEvent->dwId == 0) {
      ctrl_set_evt_fcns(pDlg, sStatDataEvtFcnTbl_g, 0xB0);
      status_get_console_art(pDlg);
      sgbStatDataWantUpdate_g = 1;
    }
    break;
  }
  return ctrl_dlg(pDlg, pEvent);
}

// Installs the icon draw handler (queue slots 9..12 get the queue variant) and
// allocates the 12-byte icon payload. The progress slot is zeroed twice, before
// and after the switch; with one store the pfnDraw store lands ahead of `push 0Ch`.
void __fastcall statdata_icon_btn_activate(ctrl_t *pCtrl) {
  *ctrl_progress_slot(pCtrl) = 0;
  switch (pCtrl->nIndex) {
  case 9:
  case 10:
  case 11:
  case 12:
    pCtrl->pfnDraw = statdata_btn_queue_draw;
    break;
  default:
    pCtrl->pfnDraw = statdata_icon_info_draw;
    break;
  }
  *ctrl_progress_slot(pCtrl) = 0;
  ctrl_data26(pCtrl) = SMemAlloc(sizeof(statdata_icon_t), "statdata.cpp", 0x273, 0);  // Hyperion: 0xC in 1.17
}

// Right-click on a panel slot: cancel the queued unit, the research or upgrade,
// the add-on, or an egg's morph. Ignored in a replay.
void __fastcall statdata_slot_cmd(ctrl_t *pCtrl) {
  if (gPlayback)
    return;
  switch (pCtrl->nIndex) {
  case 2:
  case 3:
  case 4:
  case 5:
  case 6:
    order_stop_build(pCtrl->nIndex - 2, 0);
    break;
  case 15:
    switch (statdata_get_type()) {
    case 7:
      order_cancel_research(0, 0);
      break;
    case 8:
      order_cancel_upgrade(0, 0);
      break;
    }
    break;
  case 17:
    switch (gpStatUnit->wType) {
    case 0x24:
    case 0x3B:
    case 0x61:
      if (!gpStatUnit->IsDisabled())
        order_kill(0, 0);
      break;
    default:
      order_cancel_addon(0, 0);
      break;
    }
    break;
  }
}

// Draws the button frame from the command-button GRP, centred in the control;
// the pressed state uses the next frame.
void __fastcall statdata_slot_frame_draw(ctrl_t *pCtrl) {
  rect32_t rc;
  FRAME frame;
  grhsize_t size;
  WORD wFrame = *ctrl_progress_slot(pCtrl);
  if (pCtrl->dwFlags & 0x40000000)
    wFrame++;
  grh_get_header(gpCmdBtns, wFrame, &frame, &size);
  int nWidth = pCtrl->rc.right - pCtrl->rc.left + 1;
  int nHeight = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  int w = frame.bWidth < nWidth ? frame.bWidth : nWidth;
  int h = frame.bHeight < nHeight ? frame.bHeight : nHeight;
  SetRect(&rc, 0, 0, w, h);
  rle_draw_normal(pCtrl->rc.left + (nWidth - w) / 2,
                  pCtrl->rc.top + (nHeight - h) / 2, &frame, &rc, NULL);
}

// Draws the icon in the +0x26 payload, centred, through one of the icon
// palettes, then resets the first 16 remap entries to identity.
void __fastcall statdata_slot_icon_draw(ctrl_t *pCtrl) {
  rect32_t rc;
  FRAME frame;
  grhsize_t size;
  statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(pCtrl));
  BYTE bPal = 3;
  if ((pCtrl->dwFlags & 2) || pCtrl->wType != 2)
    bPal = 4;
  memcpy(_gbRemapColorTbl, sgBmpTIcon_g[bPal], 16);
  grh_get_header(static_cast<GROUP *>(pIcon->pGrp), pIcon->wFrame, &frame, &size);
  SetRect(&rc, 0, 0, frame.bWidth, frame.bHeight);
  // Hyperion: a mod's own icon the GPU draws at the HD scale is left out
  if (pIcon->pGrp != gpCmdIcons || !PortConsoleModUi(MOD_ART_ICON, pIcon->wFrame))
    rle_draw_normal(pCtrl->rc.left + (pCtrl->rc.right - frame.bWidth - pCtrl->rc.left + 1) / 2,
                    pCtrl->rc.top + (pCtrl->rc.bottom - frame.bHeight - pCtrl->rc.top + 1) / 2,
                    &frame, &rc, NULL);
  for (int i = 0; i < 16; i++)
    _gbRemapColorTbl[i] = static_cast<BYTE>(i);
}

// Icon button event handler: allocates and frees the payload, forwards the
// right-click, and keeps the hover state current.
int __fastcall statdata_icon_btn_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  switch (pEvent->wTag) {
  case 3:
    statdata_icon_btn_mouse_moved(pCtrl, pEvent);
    break;
  case 0x0E:
    switch (pEvent->dwId) {
    case 0:
      statdata_icon_btn_activate(pCtrl);
      break;
    case 1:
      if (ctrl_data26(pCtrl))
        SMemFree(ctrl_data26(pCtrl), "statdata.cpp", 0x281, 0);
      break;
    case 2:
      statdata_slot_cmd(pCtrl);
      return 1;
    case 4:
      if (pCtrl->dwFlags & 8)
        return 1;
      break;
    case 6:
      if (pCtrl == sgpStatDataHoverCtrl_g) {
        ctxhelp_hide();
        sgpStatDataHoverCtrl_g = NULL;
      }
      break;
    }
    break;
  }
  return gCtrlEvtFcnTbl[pCtrl->wType](pCtrl, pEvent);
}

// Chooses the context-help text for the control under the cursor. In a replay
// only the queue slots 9..12 get help.
void __fastcall statdata_hover_update(ctrl_t *pDlg, ctrl_t *pCtrl) {
  if (!gpStatUnit || !pCtrl || pDlg == pCtrl) {
    ctxhelp_hide();
    return;
  }
  if (gPlayback) {
    int n = pCtrl->nIndex;
    if (n < 9 || n > 12) {
      ctxhelp_hide();
      return;
    }
  }
  switch (pCtrl->nIndex) {
  case 2:
  case 3:
  case 4:
  case 5:
  case 6:
  case 17:
    statdata_ctx_training(pCtrl);
    return;
  case 9:
  case 10:
  case 11:
  case 12:
    statdata_ctx_status(pCtrl);
    return;
  case 15:
    statdata_ctx_research(pCtrl);
    return;
  }
  if (pCtrl->nIndex >= 0x12 && pCtrl->nIndex <= 0x20) {
    statdata_ctx_trans(pCtrl);
    return;
  }
  if (pCtrl->nIndex >= 0x21 && pCtrl->nIndex <= 0x2C) {
    statdata_ctx_show(pCtrl, reinterpret_cast<intptr_t>(status_get_str(0x329)));
    return;
  }
  ctxhelp_hide();
}

// Installs the progress-bar draw handler and loads the two race-specific bar
// images on first use.
void __fastcall statdata_progressbar_activate(ctrl_t *pCtrl) {
  char szPath[0x104];
  pCtrl->pfnDraw = statdata_progressbar_draw;
  *ctrl_progress_slot(pCtrl) = 0;
  // Hyperion: the console's race, Console skin's (PortConsoleRace), here and below
  if (!sgBmpStatProgressbarEmpty_g.pPixels) {
    psnprintf(szPath, sizeof(szPath), "%s%c%s", "game\\",
              gszRaceChars_g[PortConsoleRace()], "pbrempt.pcx");
    alloc_load_bmp(szPath, &sgBmpStatProgressbarEmpty_g, 0,
                   const_cast<char *>("statdata.cpp"), 0xDE);
  }
  if (!sgBmpStatProgressbarFull_g.pPixels) {
    psnprintf(szPath, sizeof(szPath), "%s%c%s", "game\\",
              gszRaceChars_g[PortConsoleRace()], "pbrfull.pcx");
    alloc_load_bmp(szPath, &sgBmpStatProgressbarFull_g, 0,
                   const_cast<char *>("statdata.cpp"), 0xE2);
  }
}

// Tooltip for a training slot: "<race's train string> <unit name>". Nothing
// for an egg that is disabled or held by a status effect.
void __fastcall statdata_ctx_training(ctrl_t *pCtrl) {
  char szText[0x80];
  statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(pCtrl));
  if (pIcon->wKind == 3) {
    CUnit *u = gpStatUnit;
    WORD wType = u->wType;
    if ((wType != 0x24 && wType != 0x3B && wType != 0x61) ||
        (!(u->dwFlags & 0x400) && !u->bLockdownTimer && !u->bStasisTimer &&
         !u->bParalyzeTimer)) {
      psnprintf(szText, sizeof(szText),
                status_get_str(gpStatUnit->GetRace() + 0x322),
                get_unit_name_from_type(pIcon->wParam));
      statdata_ctx_show(pCtrl, reinterpret_cast<intptr_t>(szText));
      return;
    }
  }
  ctxhelp_hide();
}

// Hyperion: a click on a selected unit's wireframe over the whole selection, which the
// console shows a page of (stage S) and the HUD's grid another
void StatDataSelectAmong(CUnit *pUnit) {
  CUnit *units[SELECTION_WIDE];
  int nCount = 0;
  if (pUnit == NULL)
    return;
  if (!byte_665E68 && !byte_665E69) {
    units[nCount++] = pUnit;
  } else {
    for (int i = 0; i < SelectionMax(); i++) {
      CUnit *u = StatUnits()[i];
      if (u != NULL && (byte_665E68 ? u != pUnit : u->wType == pUnit->wType))
        units[nCount++] = u;
    }
  }
  sub_4BCE70(nCount, units, 0, 1);
}

// Click on a multi-selection button: select just that unit, or with a modifier
// held, every other selected unit or every selected unit of the same type.
void __fastcall statdata_select_btn_command(ctrl_t *pCtrl) {
  CUnit *units[SELECTION_WIDE];
  // Hyperion: in a game of Hyperion's selection the whole selection, of which the grid
  // shows a page of 12 (stage S): with shift every unit but this one, with ctrl every unit of
  // its type
  if (SelectionWide() && (byte_665E68 || byte_665E69)) {
    StatDataSelectAmong(static_cast<statdata_select_t *>(ctrl_data26(pCtrl))->pUnit);
    return;
  }
  if (byte_665E68) {
    int nCount = 0;
    memset(units, 0, sizeof(units));
    ctrl_t *p = ctrl_find_ndx(pCtrl->pParent, 0x21);
    for (int i = SELECTION_MAX; i; i--) {
      if (p->dwFlags & 8) {
        CUnit *u = static_cast<statdata_select_t *>(ctrl_data26(p))->pUnit;
        if (u && p != pCtrl)
          units[nCount++] = u;
      }
      p = p->pNext;
    }
    sub_4BCE70(nCount, units, 0, 1);
    return;
  }
  CUnit *pUnit = static_cast<statdata_select_t *>(ctrl_data26(pCtrl))->pUnit;
  if (byte_665E69) {
    WORD wType = pUnit->wType;
    int nCount = 0;
    memset(units, 0, sizeof(units));
    ctrl_t *p = ctrl_find_ndx(pCtrl->pParent, 0x21);
    for (int i = SELECTION_MAX; i; i--) {
      if (p->dwFlags & 8) {
        CUnit *u = static_cast<statdata_select_t *>(ctrl_data26(p))->pUnit;
        if (u->wType == wType)
          units[nCount++] = u;
      }
      p = p->pNext;
    }
    sub_4BCE70(nCount, units, 0, 1);
    return;
  }
  CUnit *pOne = pUnit;
  sub_4BCE70(1, &pOne, 0, 1);
}

// Hyperion: the box's frame by the unit's state, 1.17's tests in its order
int StatDataBoxFrame(const CUnit *u) {
  if (u->dwFlags & 0x40000000)
    return 0x1F;
  if (gulUnitTypeFlags[u->wType] & 0x40)
    return 0x13;
  if (UnitParasite(u).Any())  // and the parasites past 7 (stage D)
    return 0x19;
  return 0x0D;
}

// Draws a multi-selection button: background frame by unit state, the tinted
// wireframe, centred.
void __fastcall statdata_select_btn_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                         rect16_t *prcClip) {
  rect32_t rc;
  FRAME frame;
  grhsize_t size;
  statdata_select_t *pSel = static_cast<statdata_select_t *>(ctrl_data26(pCtrl));
  CUnit *u = pSel->pUnit;
  *ctrl_progress_slot(pCtrl) = static_cast<WORD>(StatDataBoxFrame(u));  // Hyperion
  // Hyperion: in a game of Hyperion's selection with more than one kind of unit, the box
  // of the console's subgroup, the kind the card is for, is drawn lighter
  if (SelectionWide() && SelectionSubgroups() > 1 && gpStatUnit != NULL && u->wType == gpStatUnit->wType) {
    BYTE abSaved[256];
    memcpy(abSaved, _gbRemapColorTbl, sizeof abSaved);
    memcpy(_gbRemapColorTbl, PortConsoleLightRemap(), sizeof abSaved);
    statdata_slot_frame_draw(pCtrl);
    memcpy(_gbRemapColorTbl, abSaved, sizeof abSaved);
  } else {
    statdata_slot_frame_draw(pCtrl);
  }
  sub_4A7F50(u);
  sub_4A80C0(u);
  sub_4A8030(u);
  // Hyperion: a building, which Hyperion's selection takes, has no frame
  // in grpwire.grp; its wireframe is the large one at half its size (PortConsoleSmallWire)
  if (!PortConsoleSmallWire(pSel->wFrame, &frame))
    grh_get_header(sgpMultSelWires, pSel->wFrame, &frame, &size);
  if (frame.bWidth != 0 && frame.bHeight != 0) {
    SetRect(&rc, 0, 0, frame.bWidth, frame.bHeight);
    rle_draw_normal(pCtrl->rc.left + (pCtrl->rc.right - frame.bWidth - pCtrl->rc.left + 1) / 2,
                    pCtrl->rc.top + (pCtrl->rc.bottom - frame.bHeight - pCtrl->rc.top + 1) / 2,
                    &frame, &rc, NULL);
  }
  sub_4A7F10();
}

// Multi-selection button event handler.
int __fastcall statdata_select_btn_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  switch (pEvent->wTag) {
  case 3:
    statdata_icon_btn_mouse_moved(pCtrl, pEvent);
    break;
  case 0x0E:
    switch (pEvent->dwId) {
    case 0:
      pCtrl->pfnDraw = statdata_select_btn_draw;
      *ctrl_progress_slot(pCtrl) = 0;
      ctrl_data26(pCtrl) = SMemAlloc(sizeof(statdata_select_t), "statdata.cpp", 0x307, 0);
      break;
    case 1:
      if (ctrl_data26(pCtrl))
        SMemFree(ctrl_data26(pCtrl), "statdata.cpp", 0x30C, 0);
      break;
    case 2:
      statdata_select_btn_command(pCtrl);
      return 1;
    case 4:
      if (pCtrl->dwFlags & 8)
        return 1;
      break;
    case 6:
      if (pCtrl == sgpStatDataHoverCtrl_g) {
        ctxhelp_hide();
        sgpStatDataHoverCtrl_g = NULL;
      }
      break;
    }
    break;
  }
  return gCtrlEvtFcnTbl[pCtrl->wType](pCtrl, pEvent);
}

// Draws a transport cargo button: frame, the passenger's tinted wireframe.
void __fastcall statdata_transport_btn_draw(ctrl_t *pCtrl, int nXOfs,
                                            int nYOfs, rect16_t *prcClip) {
  rect32_t rc;
  FRAME frame;
  grhsize_t size;
  statdata_slot_frame_draw(pCtrl);
  CUnit *u = static_cast<CUnit *>(ctrl_data26(pCtrl));
  sub_4A7F50(u);
  sub_4A80C0(u);
  sub_4A8030(u);
  grh_get_header(sgpTransWires, ModUnitArtFrame(MOD_ART_TRANWIRE, u->wType), &frame, &size);  // Hyperion
  SetRect(&rc, 0, 0, frame.bWidth, frame.bHeight);
  rle_draw_normal(pCtrl->rc.left + (pCtrl->rc.right - frame.bWidth - pCtrl->rc.left + 1) / 2,
                  pCtrl->rc.top + (pCtrl->rc.bottom - frame.bHeight - pCtrl->rc.top + 1) / 2,
                  &frame, &rc, NULL);
  sub_4A7F10();
}

// Per-frame panel refresh: clear it with no selection, else redraw through the
// unit type's row of gTypeFcns (one unit) or the multi-selection drawer when
// something changed.
void __fastcall statdata_update() {
  if (!gpStatUnit) {
    if (sgbStatDataWantUpdate_g) {
      statdata_clr_all(sgpStatDataDlg);
      statdata_set_type(0);
      sgbStatDataWantUpdate_g = 0;
      return;
    }
  } else if (byte_6AB4C0 == 1) {
    // Hyperion: a mod's unit has a 1.17 unit's
    if (sgbStatDataWantUpdate_g || gTypeFcns[UnitTypeLooks(gpStatUnit->wType)].pfnQuery()) {
      gTypeFcns[UnitTypeLooks(gpStatUnit->wType)].pfnDraw(sgpStatDataDlg);
      statwire_update();
      statdata_update_hover();
    }
  } else {
    if (sgbStatDataWantUpdate_g || statdraw_query_multi()) {
      statdraw_draw_multi(sgpStatDataDlg);
      statdata_update_hover();
    }
  }
  sgbStatDataWantUpdate_g = 0;
}

// Tooltip for a status icon: a weapon, the armor, the shields, or one of four
// special weapons, each as "<name>\n<label> <base>[+<bonus>]".
void __fastcall statdata_ctx_status(ctrl_t *pCtrl) {
  char szText[0x80];
  statdata_icon_t *pIcon = static_cast<statdata_icon_t *>(ctrl_data26(pCtrl));
  switch (pIcon->wKind) {
  case 0: {
    weapon_id_t bWeapon = static_cast<weapon_id_t>(pIcon->wParam);
    WORD wBonus = gpStatUnit->GetDisplayBonusDamage(bWeapon);
    if (pIcon->wParam != 0x67) {
      psnprintf(szText, sizeof(szText), wBonus ? "%s\n%s %d+%d" : "%s\n%s %d",
                status_get_str(wpn_label_g[bWeapon]), status_get_str(0x309),
                gpStatUnit->GetDisplayBaseDamage(bWeapon), wBonus);
    } else {
      if (wBonus)
        psnprintf(szText, sizeof(szText), "%s\n%s %d+%d %s",
                  status_get_str(wpn_label_g[bWeapon]), status_get_str(0x309),
                  gpStatUnit->GetDisplayBaseDamage(bWeapon), wBonus,
                  status_get_str(0x515));
      else
        psnprintf(szText, sizeof(szText), "%s\n%s %d %s",
                  status_get_str(wpn_label_g[bWeapon]), status_get_str(0x309),
                  gpStatUnit->GetDisplayBaseDamage(bWeapon),
                  status_get_str(0x515));
    }
    break;
  }
  case 1: {
    BYTE bLevel = gpStatUnit->ArmorUpgrades();
    WORD wType = gpStatUnit->wType;
    psnprintf(szText, sizeof(szText), bLevel ? "%s\n%s %d+%d" : "%s\n%s %d",
              status_get_str(guwUpgradeStr[pIcon->wParam]), status_get_str(0x30A),
              gubUnitArmor[wType], bLevel);
    break;
  }
  case 2: {
    BYTE bPlayer = gpStatUnit->bPlayer;
    BYTE bLevel = get_upgrade_level(bPlayer, 0x0F);
    psnprintf(szText, sizeof(szText), bLevel ? "%s\n%s %d+%d" : "%s\n%s %d",
              status_get_str(guwUpgradeStr[pIcon->wParam]), status_get_str(0x30B),
              0, bLevel);
    break;
  }
  case 3: {
    WORD wBonus;
    switch (pIcon->wParam) {
    case 0x49:
      wBonus = gpStatUnit->GetDisplayBonusDamage(0x4F);
      psnprintf(szText, sizeof(szText), wBonus ? "%s\n%s %d+%d" : "%s\n%s %d",
                status_get_str(0x317), status_get_str(0x309),
                gpStatUnit->GetDisplayBaseDamage(0x4F), wBonus);
      break;
    case 0x55:
      wBonus = gpStatUnit->GetDisplayBonusDamage(0x52);
      psnprintf(szText, sizeof(szText), wBonus ? "%s\n%s %d+%d" : "%s\n%s %d",
                status_get_str(0x318), status_get_str(0x309),
                gpStatUnit->GetDisplayBaseDamage(0x52), wBonus);
      break;
    case 0x0E:
      statdata_ctx_show(pCtrl, reinterpret_cast<intptr_t>(status_get_str(0x319)));
      return;
    case 0x0D:
      wBonus = gpStatUnit->GetDisplayBonusDamage(6);
      psnprintf(szText, sizeof(szText), wBonus ? "%s\n%s %d+%d" : "%s\n%s %d",
                status_get_str(0x31A), status_get_str(0x309),
                gpStatUnit->GetDisplayBaseDamage(6), wBonus);
      break;
    default:
      return;
    }
    statdata_ctx_show(pCtrl, reinterpret_cast<intptr_t>(szText));
    return;
  }
  default:
    return;
  }
  statdata_ctx_show(pCtrl, reinterpret_cast<intptr_t>(szText));
}
