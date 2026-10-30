// StatLB.cpp -- the in-game status listbox dialog, the leaderboard. Bodies are
// in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>

#include "../h/StatLB.h"
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/dialog.h"    // ctrl_t, dlgevt_t, PFN_CTRL_EVT
#include "../h/bmp.h"       // bmp_t, bmp_rect_fill
#include "../h/grp.h"       // GROUP
#include "../h/dlg_ctrl.h"  // ctrl_chain, ctrl_dlg, ctrl_hide
#include "../h/dlg_dlg.h"   // dlg_load, dlg_activate, dlg_deactivate
#include "../h/dlg_draw.h"  // font_set, font_set_color_tbl
#include "../h/gametype.h"  // IsGreedGame / IsSlaughterGame / IsScenarioGame
#include "../h/glues.h"     // alloc_load_bmp
#include "../h/snd.h"       // load_game_data
#include "../h/minimap.h"   // timer_create, timer_remove_node, PFN_TIMER_FN
#include "../h/TrigLB.h"   // get_lb_values, whose TU opened and defined it
#include "../h/victory.h"   // psnprintf
#include "../h/gds_grptobmp.h"
#include "../h/gds_bmpmas.h"

#include <string.h>        // strlen, inlined by /Oi

// Callees no header of ours declares.

// 0x4CDF20, spelled exactly as src117/lang/dlg_draw.cpp spells it.
const char *__fastcall font_adjust(int nX, int nY, const char *psz);

// 0x445670 and 0x4A7D70, spelled exactly as src117/h/TrigAct.h and
// src117/h/order.h spell them.
const char *__fastcall get_map_string(WORD wStringId);
char *__fastcall status_get_str(WORD wStringId);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The listbox dialog itself, or null while it is torn down.
#ifdef OC_DLL_MODE
#define sgpStatLBDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x6AB270u)))
#else
extern ctrl_t *sgpStatLBDlg;
#endif

// Which refresh last laid the rows out: 0 none, 1 the ordinary one, 2 the
// kills-and-score one. A BYTE.
#ifdef OC_DLL_MODE
#define byte_6AB274 (*reinterpret_cast<BYTE *>(OC_VA(0x6AB274u)))
#else
extern BYTE byte_6AB274;
#endif

// Show every row, not just the first: toggled by statlb_view_toggle and set by
// statlb_init.
#ifdef OC_DLL_MODE
#define dword_6AB278 (*reinterpret_cast<LONG *>(OC_VA(0x6AB278u)))
#else
extern LONG dword_6AB278;
#endif

// game\icons.grp, the resource icons, spelled as src117/lang/statres.cpp
// spells it.
#ifdef OC_DLL_MODE
#define gpIconsGrp (*reinterpret_cast<GROUP **>(OC_VA(0x6AB3F0u)))
#else
extern GROUP *gpIconsGrp;
#endif

// The leaderboard mode, a BYTE: zero hides the rows, 0x28 is the kills-and-score
// board, any other value the ordinary one.
#ifdef OC_DLL_MODE
#define byte_5349EC (*reinterpret_cast<BYTE *>(OC_VA(0x5349ECu)))
#else
extern BYTE byte_5349EC;
#endif

// Spelled as src117/lang/statres.cpp spells it. The gas label's colour code;
// statlb_ctrl_init_fn biases it by one into the resource icon's tag.
#ifdef OC_DLL_MODE
#define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
#else
extern BYTE byte_5264C2;
#endif

// The two 8x16 text buffers the rows' pData points at.
#ifdef OC_DLL_MODE
#define byte_6AB280 (reinterpret_cast<char *>(OC_VA(0x6AB280u)))
#else
extern char byte_6AB280[0x80];
#endif

#ifdef OC_DLL_MODE
#define byte_6AB300 (reinterpret_cast<char *>(OC_VA(0x6AB300u)))
#else
extern char byte_6AB300[0x80];
#endif

// The two per-player counters the rows print, indexed by the player the
// leaderboard record names.
#ifdef OC_DLL_MODE
#define dword_5263D0 (reinterpret_cast<LONG *>(OC_VA(0x5263D0u)))
#else
extern PlayerMineralsTable dword_5263D0;  // Hyperion: and the slots past 11
#endif

#ifdef OC_DLL_MODE
#define dword_526400 (reinterpret_cast<LONG *>(OC_VA(0x526400u)))
#else
extern PlayerGasTable dword_526400;  // Hyperion: and the slots past 11
#endif

// The map's own leaderboard title, as a string id.
#ifdef OC_DLL_MODE
#define dword_5349F4 (*reinterpret_cast<LONG *>(OC_VA(0x5349F4u)))
#else
extern LONG dword_5349F4;
#endif

// A byte table indexed by the control's +0x26 slot, the player, giving the pen
// colour directly.
#ifdef OC_DLL_MODE
#define byte_5290B6 (reinterpret_cast<BYTE *>(OC_VA(0x5290B6u)))
#else
extern PlayerMinimapColorTable byte_5290B6;  // Hyperion: and the slots past 11
#endif

#include "../gen/globals117.h"

// The control's +0x24 user slot is a WORD tag and a DWORD cached value.
// dialog.h leaves 0x24..0x29 unmapped because no two subsystems agree on it, so
// each TU that uses it names its own view the way statres.cpp does.
#define ctrl_res_tag(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped24[0]))
#define ctrl_res_val(p) (*reinterpret_cast<dlg_dword_t *>(&(p)->_unmapped24[2]))

// The TU's own owner tag: the .rdata string at 0x4F00C0 that dlg_load pushes.
#define OC_STATLB_FILE "StatLB.cpp"

// The bodies, in oracle VA order.

// Fill the player's slot with that player's colour.
void statlb_playercolor_icon_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                  rect16_t *prcClip)
{
  _bmp_pen = byte_5290B6[ctrl_res_val(pCtrl)];
  bmp_rect_fill(pCtrl->rc.left, pCtrl->rc.top,
                pCtrl->rc.right - pCtrl->rc.left + 1,
                pCtrl->rc.bottom - pCtrl->rc.top + 1);
}

// Blit the slot's resource icon, centred vertically in the slot.
void statlb_resource_icon_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                               rect16_t *prcClip)
{
  bmp_t bmp;
  unsigned short nTop;
  unsigned short nHeight;

  grp_to_bmp(gpIconsGrp, (BYTE)ctrl_res_tag(pCtrl), &bmp);
  nTop = pCtrl->rc.top;
  nHeight = pCtrl->rc.bottom - nTop + 1;
  bmp_mask(pCtrl->rc.left, (unsigned short)(nTop + (nHeight - bmp.nHeight) / 2),
           &bmp);
}

// Draw the slot's own text inside its rectangle.
void statlb_text_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  if (pCtrl->pData) {
    font_set(sgpFonts_g[1]);
    gbFontJust = 0x21;
    font_set_color_tbl(7);
    gsFontMargins = pCtrl->rc;
    font_adjust(pCtrl->rc.left, pCtrl->rc.top,
                (const char *)pCtrl->pData);
    font_set(0);
  }
}

// The dialog's event handler: install the per-control draw hooks and the
// refresh timer on activate, drop the timer on deactivate.
int statlb_dlg_fcn(ctrl_t *pDlg, dlgevt_t *pEvent)
{
  switch (pEvent->wTag) {
  case 0:
  case 1:
  case 3:
  case 4:
  case 6:
  case 7:
  case 9:
  case 15:
    return 0;
  case 14:
    switch (pEvent->dwId) {
    case 0:
      pDlg->dwFlags |= 0x30002000;
      ctrl_chain(pDlg, NULL, reinterpret_cast<PFN_CTRL_EVT>(statlb_ctrl_init_fn));
      timer_create(pDlg, 1, 0x3E8,
                   reinterpret_cast<PFN_TIMER_FN *>(statlb_timed_update));
      break;
    case 1:
      timer_remove_node(pDlg, 1);
      break;
    case 4:
      return 0;
    }
    break;
  }
  return ctrl_dlg(pDlg, pEvent);
}

// Tears the listbox dialog down. The store runs whether or not the dialog was
// there -- the branch guards only the call.
void statlb_deactivate()
{
  if (sgpStatLBDlg != NULL) {
    dlg_deactivate(sgpStatLBDlg);
  }
  sgpStatLBDlg = NULL;
}

// The alternate refresh: eight leaderboard rows of kills and score, each row a
// pair of 16-byte text slots the controls' pData points at.
void statlb_kills_update(ctrl_t *pDlg)
{
  ctrl_t *pKills;
  ctrl_t *pScore;
  ctrl_t *pIcon;
  LONG *pValues;
  LONG nPlayer;
  int i;
  int nOfs;
  char *psz;

  if (byte_6AB274 != 2) {
    byte_6AB274 = 2;
    ctrl_chain(pDlg, NULL, reinterpret_cast<PFN_CTRL_EVT>(sub_4A5F00));
  }
  ctrl_show(ctrl_find_ndx(pDlg, -0x30));
  ctrl_show(ctrl_find_ndx(pDlg, -0x31));

  pValues = get_lb_values();
  pKills = ctrl_find_ndx(pDlg, -0x1E);
  pScore = ctrl_find_ndx(pDlg, -0x28);
  pIcon = ctrl_find_ndx(pDlg, -0x0A);
  i = 0;
  nOfs = 0;
  do {
    if (pKills == NULL || pScore == NULL || pIcon == NULL)
      break;
    if ((i != 0 && dword_6AB278 == 0) || (nPlayer = pValues[i * 2]) == -1) {
      ctrl_hide(pKills);
      ctrl_hide(pScore);
      ctrl_hide(pIcon);
    } else {
      ctrl_show(pIcon);
      if (ctrl_res_val(pIcon) != (dlg_dword_t)nPlayer) {
        ctrl_res_val(pIcon) = nPlayer;
        ctrl_set_update(pIcon);
      }
      psz = &byte_6AB280[nOfs];
      psnprintf(psz, 0x10, "%d", dword_5263D0[nPlayer]);
      pKills->pData = psz;
      ctrl_show(pKills);
      ctrl_set_update(pKills);
      psz = &byte_6AB300[nOfs];
      psnprintf(psz, 0x10, "%d", dword_526400[nPlayer]);
      pScore->pData = psz;
      ctrl_show(pScore);
      ctrl_set_update(pScore);
    }
    i++;
    nOfs += 0x10;
    pScore = pScore->pNext;
    pKills = pKills->pNext;
    pIcon = pIcon->pNext;
  } while (nOfs < 0x80);

  if (gbMultiPlayerMode != 0 && !IsGreedGame() && !IsSlaughterGame()
      && !IsScenarioGame(NULL) && gPlayback == 0)
    dlg_deactivate(pDlg);
}

// Hide every control of the row being rebuilt.
void sub_4A5E60(ctrl_t *pCtrl)
{
  if (pCtrl->wType != 0) {
    ctrl_hide(pCtrl);
  }
}

// Hide the score columns of the rows the ordinary refresh rebuilds.
void sub_4A5E70(ctrl_t *pCtrl)
{
  if (pCtrl->nIndex <= -0x1E && pCtrl->nIndex >= -0x31) {
    ctrl_hide(pCtrl);
  }
}

// Refresh the board, or just drop its rows when nHide is set.
void statlb_refresh(int nHide)
{
  ctrl_t *pDlg;

  pDlg = sgpStatLBDlg;
  if (pDlg == NULL)
    return;

  if (nHide != 0) {
    if (byte_6AB274 != 0) {
      ctrl_t *pFirst = pDlg->pChildren;
      byte_6AB274 = 0;
      ctrl_chain(pFirst, NULL, reinterpret_cast<PFN_CTRL_EVT>(sub_4A5E60));
    }
    return;
  }

  if (byte_5349EC != 0) {
    if (byte_5349EC != 0x28) {
      statlb_update(pDlg);
      return;
    }
    statlb_kills_update(pDlg);
    return;
  }
  if (byte_6AB274 != 0) {
    byte_6AB274 = 0;
    ctrl_chain(pDlg, NULL, reinterpret_cast<PFN_CTRL_EVT>(sub_4A5E60));
  }
}

// Hide the columns the alternate refresh rebuilds.
void sub_4A5F00(ctrl_t *pCtrl)
{
  if (pCtrl->nIndex <= -0x14 && pCtrl->nIndex >= -0x1C) {
    ctrl_hide(pCtrl);
  }
}

// Position and label the board's title: pull it left of the first row when the
// row has text, and fall back on the mode's own string when the map has none.
void statlb_set_title(ctrl_t *pDlg)
{
  ctrl_t *pTitle;
  ctrl_t *pRow0;
  const char *psz;
  int nWidth;
  int nLeft;
  int nRight;

  pTitle = ctrl_find_ndx(pDlg, -0x1C);
  ctrl_show(pTitle);
  pRow0 = ctrl_find_ndx(pDlg, -0x14);

  if (strlen(byte_6AB280) != 0) {
    font_set(sgpFonts_g[1]);
    nWidth = font_strlen(byte_6AB280);
    font_set(0);
    nLeft = pRow0->rc.left + 4 + nWidth;
    nRight = pRow0->rc.right + 4;
    if (nRight < nLeft)
      nLeft = nRight;
    pTitle->rc.left = nLeft;
  } else {
    pTitle->rc.left = pRow0->rc.right + 4;
    ctrl_hide(pTitle);
  }

  psz = get_map_string((WORD)dword_5349F4);
  if (psz == NULL) {
    switch (byte_5349EC) {
    case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
      psz = status_get_str(byte_5349EC + 0x330);
      break;
    case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
      psz = status_get_str(byte_5349EC + 0x325);
      break;
    }
  }
  pTitle->pData = const_cast<char *>(psz);
  ctrl_set_update(pTitle);
}

// Build the leaderboard dialog, unless the game mode has no board of its own.
void statlb_init()
{
  if (gbMultiPlayerMode != 0 && !IsGreedGame() && !IsSlaughterGame()
      && !IsScenarioGame(NULL) && gPlayback == 0)
    return;

  byte_6AB274 = 0;
  dword_6AB278 = 1;
  sgpStatLBDlg = dlg_load("rez\\statlb.bin",
                          reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                          reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                          OC_STATLB_FILE, 0x18F);
  dlg_activate(sgpStatLBDlg, 0, statlb_dlg_fcn);
}

// Give each control the draw hook its index calls for.
void statlb_ctrl_init_fn(ctrl_t *pCtrl)
{
  // nIndex is read from the field for each test, as 1.17 does; through a local
  // the range test folds into one unsigned compare.
  if (pCtrl->nIndex >= -0x11 && pCtrl->nIndex <= -0x0A) {
    pCtrl->pfnDraw = statlb_playercolor_icon_draw;
    ctrl_res_val(pCtrl) = 0;
    return;
  }
  if (pCtrl->nIndex == -0x30) {
    pCtrl->pfnDraw = statlb_resource_icon_draw;
    ctrl_res_tag(pCtrl) = 0;
    return;
  }
  if (pCtrl->nIndex == -0x31) {
    pCtrl->pfnDraw = statlb_resource_icon_draw;
    ctrl_res_tag(pCtrl) = byte_5264C2 + 1;
    return;
  }
  if (pCtrl->wType == 9) {
    pCtrl->pfnDraw = statlb_text_draw;
  }
}

// The ordinary refresh: eight rows of one label each, the text built in place
// in the 8x16 buffer the row's pData points at. The buffer is indexed by the
// row counter, not walked by a pointer, which keeps 1.17's signed loop test.
void statlb_update(ctrl_t *pDlg)
{
  ctrl_t *pName;
  ctrl_t *pIcon;
  LONG *pValues;
  LONG nValue;
  char *psz;
  int i;

  if (byte_6AB274 != 1) {
    byte_6AB274 = 1;
    ctrl_chain(pDlg, NULL, reinterpret_cast<PFN_CTRL_EVT>(sub_4A5E70));
  }

  pValues = get_lb_values();
  pName = ctrl_find_ndx(pDlg, -0x14);
  pIcon = ctrl_find_ndx(pDlg, -0x0A);
  i = 0;
  do {
    if (pName == NULL || pIcon == NULL)
      break;
    psz = &byte_6AB280[i * 0x10];
    if ((i != 0 && dword_6AB278 == 0) || (nValue = pValues[i * 2]) == -1) {
      ctrl_hide(pName);
      *psz = 0;
      ctrl_hide(pIcon);
    } else {
      ctrl_show(pIcon);
      if (ctrl_res_val(pIcon) != (dlg_dword_t)pValues[i * 2]) {
        ctrl_res_val(pIcon) = pValues[i * 2];
        ctrl_set_update(pIcon);
      }
      psnprintf(psz, 0x10, "%d", pValues[i * 2 + 1]);
      pName->pData = psz;
      ctrl_show(pName);
      ctrl_set_update(pName);
    }
    i++;
    pName = pName->pNext;
    pIcon = pIcon->pNext;
  } while (i < 8);

  statlb_set_title(pDlg);
  if (gbMultiPlayerMode != 0 && !IsGreedGame() && !IsSlaughterGame()
      && !IsScenarioGame(NULL) && gPlayback == 0)
    dlg_deactivate(pDlg);
}


// Flip the board's alternate-layout flag and redraw it.
void statlb_view_toggle()
{
  ctrl_t *pDlg;

  dword_6AB278 ^= 1;
  pDlg = sgpStatLBDlg;
  if (pDlg == NULL)
    return;

  if (byte_5349EC != 0) {
    if (byte_5349EC != 0x28) {
      statlb_update(pDlg);
      return;
    }
    statlb_kills_update(pDlg);
    return;
  }
  if (byte_6AB274 != 0) {
    byte_6AB274 = 0;
    ctrl_chain(pDlg, NULL, reinterpret_cast<PFN_CTRL_EVT>(sub_4A5E60));
  }
}

// The one-second timer: the same refresh the toggle runs.
void statlb_timed_update(ctrl_t *pDlg)
{
  if (byte_5349EC != 0) {
    if (byte_5349EC != 0x28) {
      statlb_update(pDlg);
      return;
    }
    statlb_kills_update(pDlg);
    return;
  }
  if (byte_6AB274 != 0) {
    byte_6AB274 = 0;
    ctrl_chain(pDlg, NULL, reinterpret_cast<PFN_CTRL_EVT>(sub_4A5E60));
  }
}
