// statres.cpp -- the status console resource panel: minerals, gas, supply and
// the countdown timer. Bodies are in oracle VA order.
//
// /Ob2 can splice a callee defined in this file into a caller here, where 1.17,
// built without WPO, keeps every cross-TU call; noinline on the declaration is
// the lever when such a callee has to live here.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>

#include "../h/platform.h"
#include "../h/statres.h"
#include "../h/rect16.h"
#include "../h/dialog.h"    // ctrl_t, dlgevt_t, PFN_CTRL_EVT / PFN_CTRL_DRAW
#include "../h/bmp.h"       // bmp_t, bmpsrc_t, bmp_copy_rect, bmp_rect_fill
#include "../h/grp.h"       // GROUP
#include "../h/dlg_ctrl.h"  // ctrl_find_ndx, ctrl_show, ctrl_hide, ctrl_text
#include "../h/dlg_dlg.h"   // dlg_load, dlg_activate, dlg_deactivate
#include "../h/dlg_draw.h"  // font_set, font_set_color_tbl
#include "../h/minimap.h"   // timer_create, timer_remove_node, PFN_TIMER_FN
#include "../h/count.h"     // count_get_supplies_used / _available
#include "../h/gamedata.h"  // ReadGroupFile
#include "../h/glues.h"     // alloc_load_bmp
#include "../h/snd.h"       // load_game_data
#include "../h/victory.h"   // psnprintf
#include "../h/CUnit.h"     // CUnit::bPlayer
#include "../h/Storm.h"     // SMemFree, SStrCopy
#include "../h/gds_grptobmp.h"
#include "../h/gds_bmpmas.h"
#include "../gen/globals117.h"
#include "../h/selectlimits.h"  // Hyperion: StatUnits, 12 units or 255 (stage S)

// statres.h spells the control arguments `int`. Cast at each use: binding one
// to a named local pins it in a register and costs statres_resource_ctrl_draw
// three bytes in its last branch.
static ctrl_t *as_ctrl(intptr_t n) { return reinterpret_cast<ctrl_t *>(n); }

// Callees no header of ours declares yet.

// 0x4CDF20, spelled as dlg_draw.cpp spells it and kept out of a header for the
// reason dlg_draw.h gives.
const char *__fastcall font_adjust(int nX, int nY, const char *psz);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it. The
// names are the naming sheet's for this TU.

// The event-handler table statres_dlg_activate registers, six entries: the call
// passes a byte count of 0x18 and ctrl_set_evt_fcns stores `(WORD)(cb >> 2)`.
// All six slots hold 0x4A7790, statres_resource_ctrl_fcn.
#ifdef OC_DLL_MODE
#define sStatresEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x4F0144u)))
#else
extern PFN_CTRL_EVT sStatresEvtFcnTbl_g[6];
#endif

// The player whose resources were last published, or -1 for none.
#ifdef OC_DLL_MODE
#define dword_50D7D8 (*reinterpret_cast<LONG *>(OC_VA(0x50D7D8u)))
#else
extern LONG dword_50D7D8;
#endif

// game\icons.grp, the resource icons and the supply bar.
#ifdef OC_DLL_MODE
#define gpIconsGrp (*reinterpret_cast<GROUP **>(OC_VA(0x6AB3F0u)))
#else
extern GROUP *gpIconsGrp;
#endif

// The resource dialog.
#ifdef OC_DLL_MODE
#define sgpStatResDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x6AB3F4u)))
#else
extern ctrl_t *sgpStatResDlg;
#endif

// The two animated counters the panel walks toward the real balance.
#ifdef OC_DLL_MODE
#define sgnAnimore_g (*reinterpret_cast<LONG *>(OC_VA(0x6AB3F8u)))
#else
extern LONG sgnAnimore_g;
#endif

#ifdef OC_DLL_MODE
#define sgnAnimGas_g (*reinterpret_cast<LONG *>(OC_VA(0x6AB3FCu)))
#else
extern LONG sgnAnimGas_g;
#endif

// The countdown timer's own buffer. 0x20 bytes: SetCountdownText caps the copy
// at 0x20 and the next object starts at 0x6AB420.
#ifdef OC_DLL_MODE
#define sgSlotStrs (reinterpret_cast<char *>(OC_VA(0x6AB400u)))
#else
extern char sgSlotStrs[0x20];
#endif

// The resource slots' text buffers: five rows of 0x20 from 0x6AB420 to
// 0x6AB4C0, indexed by the control's nIndex less one. The base is 0x6AB420,
// not 0x6AB400: 1.17 forms the address as `shl eax, 5` then
// `lea eax, [eax+6AB400h]`, which only the biased index gives.
#ifdef OC_DLL_MODE
#define byte_6AB420 (reinterpret_cast<char *>(OC_VA(0x6AB420u)))
#else
extern char byte_6AB420[0xA0];
#endif

// Spelled as src117/lang/banker.cpp:110 spells it. The gas label's colour code.
#ifdef OC_DLL_MODE
#define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
#else
extern BYTE byte_5264C2;
#endif

// The control's +0x24 user slot is a WORD tag and a DWORD cached value.
// dialog.h leaves 0x24..0x29 unmapped because no two subsystems agree on it, so
// each TU that uses it names its own view the way gluBrfPort.cpp does.
#define ctrl_res_tag(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped24[0]))
#define ctrl_res_val(p) (*reinterpret_cast<dlg_dword_t *>(&(p)->_unmapped24[2]))

// The TU's own owner tag: the .rdata string at 0x4F015C that both the group
// load and the free push.
#define OC_STATRES_FILE "statres.cpp"

// 0x4A74C0 statres_resource_ctrl_draw (198 B). Draw one resource slot: the icon
// centred vertically, then either the supply bar (the slot at index 6) or the
// slot's own text.
// The height is an unsigned short local so the subtract stays 16-bit. The
// bar's X reaches an unsigned short parameter, which drops a widening. The
// control is cast at each use; a named local costs three bytes in the tail.
void statres_resource_ctrl_draw(intptr_t nArg0, int nArg1, int nArg2, int nArg3) {
  bmp_t bmp;
  unsigned short nTop;
  unsigned short nHeight;

  grp_to_bmp(gpIconsGrp, (BYTE)ctrl_res_tag(as_ctrl(nArg0)), &bmp);
  nTop = as_ctrl(nArg0)->rc.top;
  nHeight = as_ctrl(nArg0)->rc.bottom - nTop + 1;
  bmp_mask(as_ctrl(nArg0)->rc.left,
           (unsigned short)(nTop + (nHeight - bmp.nHeight) / 2), &bmp);

  if (as_ctrl(nArg0)->nIndex == 6) {
    statres_draw_bar(nArg0, as_ctrl(nArg0)->rc.left + 2 + bmp.nWidth, nHeight);
    return;
  }

  if (as_ctrl(nArg0)->pData) {
    font_set(sgpFonts_g[1]);
    gbFontJust = 0x21;
    gsFontMargins = as_ctrl(nArg0)->rc;
    font_set_color_tbl(7);
    font_adjust(bmp.nWidth + 2 + as_ctrl(nArg0)->rc.left,
                as_ctrl(nArg0)->rc.top, (const char *)as_ctrl(nArg0)->pData);
    font_set(0);
  }
}

// 0x4A7590 statres_draw_bar (174 B). The supply bar: frame 0x0B of the icon
// group blitted for the filled part and a pen-0 fill for the rest. The cached
// value at +0x26 is the fill fraction in 8.8, so the product is shifted right
// by 8 unsigned.
// nX is an unsigned short although this body spills the whole register. Only
// the caller can see the difference, and it needs the 16-bit form.
void statres_draw_bar(intptr_t nArg0, unsigned short wArg1, int nArg2) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  unsigned short nX = wArg1;
  int nHeight = nArg2;
  bmp_t bmp;
  bmpsrc_t src;
  unsigned short nFilled;
  unsigned short nEmpty;
  unsigned short nY;

  grp_to_bmp(gpIconsGrp, 0x0B, &bmp);
  nFilled = (bmp.nWidth * ctrl_res_val(pCtrl)) >> 8;
  nEmpty = bmp.nWidth - nFilled;
  nY = ((unsigned short)nHeight - bmp.nHeight) / 2 + pCtrl->rc.top;

  if (nFilled) {
    src.rc.left = 0;
    src.rc.top = 0;
    src.nWidth = nFilled;
    src.nHeight = bmp.nHeight;
    src.rc.right = nFilled - 1;
    src.rc.bottom = bmp.nHeight - 1;
    bmp_copy_rect(nX, nY, &bmp, &src);
  }

  if (nEmpty) {
    _bmp_pen = 0;
    bmp_rect_fill(nFilled + nX, nY, nEmpty, bmp.nHeight);
  }
}

// 0x4A7640 statres_draw_supplies (149 B). Rebuild one supply slot's
// "used/available" text, but only when the pair or the tag changed. Both counts
// are halved because supply is stored in halves.
// The halves are unsigned short and the division is signed: the oracle widens
// with movzx, divides with cdq/sar and compares the pair 16-bit through the
// carry flag. An int local emits shr and setge instead.
void statres_draw_supplies(intptr_t nArg0, WORD wArg1, int nUsed, int nAvail) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  WORD wTag = wArg1;
  unsigned short nA;
  unsigned short nB;
  int nPacked;

  ctrl_show(pCtrl);
  nB = ((unsigned short)nAvail + 1) / 2;
  nA = ((unsigned short)nUsed + 1) / 2;
  nPacked = (nA << 16) | nB;

  if (pCtrl->pData == NULL || ctrl_res_val(pCtrl) != (dlg_dword_t)nPacked ||
      ctrl_res_tag(pCtrl) != wTag) {
    ctrl_res_val(pCtrl) = nPacked;
    ctrl_res_tag(pCtrl) = wTag;
    psnprintf(&byte_6AB420[(pCtrl->nIndex - 1) * 0x20], 0x20, "%c%d\x01/%d",
              nB < nA ? 6 : 7, nA, nB);
    pCtrl->pData = &byte_6AB420[(pCtrl->nIndex - 1) * 0x20];
    ctrl_set_update(pCtrl);
  }
}

// 0x4A76E0 statres_draw_resources (175 B). Walk the displayed mineral or gas
// figure one step toward the real balance and republish it. The step is a
// sixteenth of the gap plus one in its direction, or the whole gap when the
// caller asks for a snap.
// The tag store precedes the reload of *pnCurrent so one load feeds both the
// +0x26 store and the format argument.
void statres_draw_resources(intptr_t nArg0, WORD wArg1, void *pArg2, int nTarget,
                            int bSnap) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  WORD wTag = wArg1;
  LONG *pnCurrent = reinterpret_cast<LONG *>(pArg2);
  LONG nCur;
  int nDiff;

  ctrl_show(pCtrl);
  nCur = *pnCurrent;
  nDiff = nTarget - nCur;

  if (pCtrl->pData != NULL && nDiff == 0) {
    if (ctrl_res_val(pCtrl) == (dlg_dword_t)nTarget &&
        ctrl_res_tag(pCtrl) == wTag)
      return;
  } else if (nDiff != 0) {
    if (bSnap)
      *pnCurrent = nCur + nDiff;
    else
      *pnCurrent = nCur + (nDiff > 0 ? 1 : -1) + nDiff / 16;
  }

  ctrl_res_tag(pCtrl) = wTag;
  nCur = *pnCurrent;
  ctrl_res_val(pCtrl) = nCur;
  psnprintf(&byte_6AB420[(pCtrl->nIndex - 1) * 0x20], 0x20, "%d", nCur);
  pCtrl->pData = &byte_6AB420[(pCtrl->nIndex - 1) * 0x20];
  ctrl_set_update(pCtrl);
}

// 0x4A7790 statres_resource_ctrl_fcn (59 B). Claim each slot on the id-0
// initialise event and install the draw handler. The slot at index 6 starts
// with tag 10, the supply bar's icon frame.
BOOL statres_resource_ctrl_fcn(void *pArg0, void *pArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(pArg1);

  if (pEvent->wTag == 0x0E && pEvent->dwId == 0) {
    ctrl_res_val(pCtrl) = 0;
    pCtrl->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(statres_resource_ctrl_draw);
    pCtrl->pfnEvt = ctrl_text;
    ctrl_res_tag(pCtrl) = pCtrl->nIndex == 6 ? 10 : 0;
  }
  return ctrl_text(pCtrl, pEvent);
}

// 0x4A77D0 statres_evn_dlg (86 B). The dialog handler. A jump table over wTag
// 0..15 whose live arms all share one `return 0`, plus the id chain on the
// control event.
BOOL statres_evn_dlg(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  switch (pEvent->wTag) {
    case 14:
      switch (pEvent->dwId) {
        case 4:
          return 0;
        case 1:
          timer_remove_node(pCtrl, 1);
          break;
        case 0:
          statres_dlg_activate(nArg0);
          break;
      }
      break;
    case 0:
    case 1:
    case 3:
    case 4:
    case 6:
    case 7:
    case 9:
    case 15:
      return 0;
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// 0x4A7850 statres_dlg_activate (81 B). Register the slot handlers, start the
// 20 ms animation timer, and reset the supply bar's cached fraction.
//
// statres_animate is cast because it reads the dialog out of ECX while
// minimap.h's PFN_TIMER_FN is spelled nullary.
void statres_dlg_activate(intptr_t nArg0) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);

  pDlg->dwFlags |= 0x30002000;
  ctrl_set_evt_fcns(pDlg, sStatresEvtFcnTbl_g, 0x18);
  timer_create(pDlg, 1, 0x14, reinterpret_cast<PFN_TIMER_FN *>(statres_animate));

  if (sgpStatResDlg != NULL) {
    ctrl_t *pCtrl = ctrl_find_ndx(sgpStatResDlg, 6);
    ctrl_showhide(pCtrl, 0);
    ctrl_res_val(pCtrl) = 0;
  }
}

// 0x4A78B0 statres_deactivate (63 B). Drop the dialog and the icon group. Line
// 507 is the literal the oracle pushes; the null check is inline rather than
// mem_free's.
void statres_deactivate() {
  GROUP *pGrp;

  if (sgpStatResDlg != NULL)
    dlg_deactivate(sgpStatResDlg);
  pGrp = gpIconsGrp;
  sgpStatResDlg = NULL;
  if (pGrp != NULL)
    SMemFree(pGrp, OC_STATRES_FILE, 0x1FB, 0);
  gpIconsGrp = NULL;
}

// 0x4A78F0 statres_init (97 B). Load the icon group and the dialog. Lines 490
// and 497 are the literals the oracle pushes.
void statres_init() {
  gpIconsGrp = ReadGroupFile("game\\icons.grp", 0, OC_STATRES_FILE, 0x1EA);
  sgnAnimore_g = 0;
  sgnAnimGas_g = 0;
  sgpStatResDlg = dlg_load("rez\\statres.bin",
                           reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                           reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                           OC_STATRES_FILE, 0x1F1);
  dlg_activate(sgpStatResDlg, 0,
               reinterpret_cast<PFN_CTRL_EVT>(statres_evn_dlg));
}

// 0x4A7960 SetCountdownText (58 B). Publish the countdown string into slot row
// 0 and show the control the dialog keeps at index -10.
void __fastcall SetCountdownText(const char *pszText) {
  ctrl_t *pCtrl = ctrl_find_ndx(sgpStatResDlg, -10);

  ctrl_show(pCtrl);
  SStrCopy(sgSlotStrs, pszText, 0x20);
  pCtrl->pData = sgSlotStrs;
  ctrl_set_update(pCtrl);
}

// 0x4A79A0 HideCountdownTimer (23 B). Hide the countdown control. A
// shape-insensitive leaf.
void HideCountdownTimer() {
  ctrl_hide(ctrl_find_ndx(sgpStatResDlg, -10));
}

// 0x4A79C0 statres_animate (413 B). The 20 ms timer: republish supply, gas and
// minerals for the observed player, then hide any slot the current view does
// not use.
// ECX carries the dialog although minimap.h spells PFN_TIMER_FN nullary. The
// no-unit exit returns before the nation-id restore, so the hide loop appears
// twice; the goto keeps it at two copies. The supply counts are WORD locals.
void statres_animate(intptr_t nArg0) {
  ctrl_t *pCtrl = ctrl_find_ndx(reinterpret_cast<ctrl_t *>(nArg0), 1);
  int bChanged = 0;
  LONG nSaved = g_LocalNationID;
  WORD nUsed;
  WORD nAvail;

  if (gPlayback) {
    CUnit *pUnit;
    BYTE bPlayer;

    if (byte_6AB4C0 == 0)
      goto hideall;
    pUnit = byte_6AB4C0 == 1 ? gpStatUnit : StatUnits()[0];
    bPlayer = pUnit->bPlayer;
    if (bPlayer == 0x0B)
      goto hideall;
    g_LocalNationID = bPlayer;
    if ((LONG)bPlayer != dword_50D7D8)
      bChanged = 1;
    dword_50D7D8 = bPlayer;
  }

  nUsed = count_get_supplies_used(0, (BYTE)g_LocalNationID);
  nAvail = count_get_supplies_available(0, (BYTE)g_LocalNationID);
  if (nAvail) {
    statres_draw_supplies(reinterpret_cast<intptr_t>(pCtrl), 4, nUsed, nAvail);
    pCtrl = pCtrl->pNext;
  }

  nUsed = count_get_supplies_used(1, (BYTE)g_LocalNationID);
  nAvail = count_get_supplies_available(1, (BYTE)g_LocalNationID);
  if (nAvail) {
    statres_draw_supplies(reinterpret_cast<intptr_t>(pCtrl), 5, nUsed, nAvail);
    pCtrl = pCtrl->pNext;
  }

  nUsed = count_get_supplies_used(2, (BYTE)g_LocalNationID);
  nAvail = count_get_supplies_available(2, (BYTE)g_LocalNationID);
  if (nAvail) {
    statres_draw_supplies(reinterpret_cast<intptr_t>(pCtrl), 6, nUsed, nAvail);
    pCtrl = pCtrl->pNext;
  }

  statres_draw_resources(reinterpret_cast<intptr_t>(pCtrl), (WORD)(byte_5264C2 + 1),
                         &sgnAnimGas_g, gPlayerGas[g_LocalNationID], bChanged);
  pCtrl = pCtrl->pNext;
  statres_draw_resources(reinterpret_cast<intptr_t>(pCtrl), 0, &sgnAnimore_g,
                         gPlayerMinerals[g_LocalNationID], bChanged);
  pCtrl = pCtrl->pNext;

  while (pCtrl != NULL && pCtrl->nIndex <= 5) {
    ctrl_hide(pCtrl);
    pCtrl = pCtrl->pNext;
  }
  g_LocalNationID = gPlayback ? nSaved : g_LocalNationID;
  return;

hideall:
  dword_50D7D8 = -1;
  while (pCtrl != NULL && pCtrl->nIndex <= 5) {
    ctrl_hide(pCtrl);
    pCtrl = pCtrl->pNext;
  }
}
