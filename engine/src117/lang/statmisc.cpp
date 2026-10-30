// statmisc.cpp -- the in-game F10 menu button on the status console. Bodies
// are in oracle VA order.
//
// /Ob2 can splice a callee defined in this file into a caller here, where 1.17,
// built without WPO, keeps every cross-TU call; noinline on the declaration is
// the lever when such a callee has to live here.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>

#include "../h/platform.h"
#include "../h/statmisc.h"
#include "../h/rect16.h"
#include "../h/dialog.h"    // ctrl_t, dlgevt_t, PFN_CTRL_EVT / PFN_CTRL_DRAW
#include "../h/dlg_ctrl.h"  // ctrl_set_hot_flag, ctrl_set_evt_fcns, ctrl_find
#include "../h/dlg_dlg.h"   // dlg_load, dlg_activate, dlg_deactivate
#include "../h/dlg_draw.h"  // draw_ctrl_bmp_xy, draw_ctrl_text
#include "../h/minimap.h"   // IsPausedSingleplayer, ctxhelp_hide,
                            // status_get_console_art, btn_ctrl_pushbutton
#include "../h/order.h"     // status_get_str
#include "../h/statdata.h"  // ctxhelp_show
#include "../h/net_cmds.h"  // IsGamePaused
#include "../h/opt_dlgs.h"  // do_gamemenu_dlg
#include "../h/save_dlg.h"  // modeless_opt_dlg
#include "../h/glues.h"     // alloc_load_bmp
#include "../h/snd.h"       // load_game_data
#include "../gen/globals117.h"

// Callees no header of ours declares yet.

// 0x4192A0 (6 B). Nullary; both call sites test the result.
int __fastcall IsCtxHelpDirty();

// 0x46C820. Spelled character for character as src117/lang/save_dlg.cpp:81.
void __fastcall set_btn_size(ctrl_t *pCtrl, WORD wFrame, DWORD dwFlags);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it. The
// names are the naming sheet's for this TU.

// The event-handler table statF10_dlg_fcn registers for the console button.
// One entry: the call passes a byte count of 4 and ctrl_set_evt_fcns stores
// `(WORD)(cb >> 2)`. Its single slot holds 0x4A6500, statF10_btn_ctrl_fcn.
#ifdef OC_DLL_MODE
#define sStatF10EvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x4F00DCu)))
#else
extern PFN_CTRL_EVT sStatF10EvtFcnTbl_g[1];
#endif

// The F10 dialog itself, created by statF10_init and dropped by
// statf10_deactivate.
#ifdef OC_DLL_MODE
#define sgpStatMiscDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x6AB380u)))
#else
extern ctrl_t *sgpStatMiscDlg;
#endif

// The control the context help is currently showing for, or NULL.
#ifdef OC_DLL_MODE
#define sgpLastHoverCtrl_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x6AB384u)))
#else
extern ctrl_t *sgpLastHoverCtrl_g;
#endif

// The TU's own owner tag: the .rdata string at 0x4F00E0 that dlg_load carries.
#define OC_STATMISC_FILE "statmisc.cpp"

// 0x4A62F0 statF10_btn_draw (100 B). The button's draw handler, the
// four-argument PFN_CTRL_DRAW shape; EDX is never read. The frame index stays
// a BYTE (a wider local costs a movzx), and the Y offset goes through a WORD
// cast because 1.17 widens rc.top with movzx while rect16_t::top is signed.
void statF10_btn_draw(intptr_t nArg0, int nArg1, int nArg2, intptr_t nArg3) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  rect16_t *prcClip = reinterpret_cast<rect16_t *>(nArg3);

  ctrl_set_hot_flag(pCtrl);

  BYTE bFrame = pCtrl->_unmapped24[0];
  if (!(pCtrl->dwFlags & 2)) {
    if (pCtrl->dwFlags & 0x40000000)
      bFrame += 2;
    else
      bFrame += 1;
  }
  draw_ctrl_bmp_xy(bFrame, pCtrl->rc.left, (WORD)pCtrl->rc.top, prcClip,
                   pCtrl->dwFlags);

  if (pCtrl->pData) {
    gbFontJust = 0x22;
    int nOfs = (pCtrl->dwFlags & 0x40000000) ? 0 : 3;
    draw_ctrl_text(pCtrl, nOfs, nOfs, 0);
  }
}

// 0x4A6360 statF10_btn_mouse_moved (128 B). Shows the context help for the
// button while the cursor is over it and hides it otherwise. Each hide arm has
// its own store and tail jump; a shared one costs a jmp.
void statF10_btn_mouse_moved(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (IsPausedSingleplayer()) {
    sgpLastHoverCtrl_g = NULL;
    ctxhelp_hide();
    return;
  }
  if (pCtrl != ctrl_find(pCtrl->pParent, pEvent)) {
    sgpLastHoverCtrl_g = NULL;
    ctxhelp_hide();
    return;
  }
  if (pCtrl == sgpLastHoverCtrl_g && IsCtxHelpDirty())
    return;

  ctrl_t *pDlg = pCtrl->pParent;
  sgpLastHoverCtrl_g = pCtrl;
  ctxhelp_show(pDlg->rc.left + pCtrl->rc.right, pDlg->rc.top + pCtrl->rc.top,
               reinterpret_cast<intptr_t>(status_get_str(0x32E)));
}

// 0x4A63E0 statF10_dlg_fcn (110 B). The dialog's own handler: a jump table
// over wTag 0..14 with four arms, case 0 with its own entry at the default's
// address. Written as `break`, that arm folds into the default and three arms
// compile to a compare chain six bytes short. The `!= 0` id test puts the
// `return 1` block before the initialise block, as 1.17 has them.
BOOL statF10_dlg_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  switch (pEvent->wTag) {
    case 14:
      if (pEvent->dwId != 0) {
        if (pEvent->dwId == 4)
          return 1;
      } else {
        pCtrl->dwFlags |= 0x20000000;
        ctrl_set_evt_fcns(pCtrl, sStatF10EvtFcnTbl_g, 4);
        status_get_console_art(pCtrl);
      }
      break;
    case 0:
      goto tail;
    case 3:
      statF10_mouse_moved(nArg0, nArg1);
      break;
    case 1:
      return 0;
  }

tail:
  if (IsGamePaused())
    return modeless_opt_dlg(pCtrl, pEvent);
  return ctrl_dlg(pCtrl, pEvent);
}

// 0x4A6480 statF10_mouse_moved (122 B). The dialog-level hover tracker: finds
// the control under the cursor and shows its help or hides whatever is up. The
// paused arm reaches the hide path with a NULL control, so the hide store is a
// register, not a literal.
void statF10_mouse_moved(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);
  ctrl_t *pCtrl;

  if (IsPausedSingleplayer()) {
    pCtrl = NULL;
  } else {
    pCtrl = ctrl_find(pDlg, pEvent);
    if (pCtrl != NULL && pDlg != pCtrl) {
      if (pCtrl != sgpLastHoverCtrl_g || !IsCtxHelpDirty()) {
        ctrl_t *pParent = pCtrl->pParent;
        sgpLastHoverCtrl_g = pCtrl;
        ctxhelp_show(pParent->rc.left + pCtrl->rc.right,
                     pParent->rc.top + pCtrl->rc.top,
                     reinterpret_cast<intptr_t>(status_get_str(0x32E)));
      }
      return;
    }
  }

  if (pCtrl == sgpLastHoverCtrl_g)
    return;
  sgpLastHoverCtrl_g = pCtrl;
  ctxhelp_hide();
}

// 0x4A6500 statF10_btn_ctrl_fcn (101 B). The button's event handler. The outer
// test is an if/else ladder (two compares, no table) and the inner one a
// switch, which emits the dead `sub eax, 0h`. Every exit tail-jumps into
// btn_ctrl_pushbutton.
BOOL statF10_btn_ctrl_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  switch (pEvent->wTag) {
    case 14:
      switch (pEvent->dwId) {
        case 2:
          if (pCtrl->nIndex == 1)
            do_gamemenu_dlg();
          break;
        case 0:
          set_btn_size(pCtrl, 2, 0);
          pCtrl->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(statF10_btn_draw);
          return btn_ctrl_pushbutton(pCtrl, pEvent);
      }
      break;
    case 3:
      statF10_btn_mouse_moved(nArg0, nArg1);
      break;
  }
  return btn_ctrl_pushbutton(pCtrl, pEvent);
}

// 0x4A6570 statf10_deactivate (26 B). Drops the dialog. The ledger spells this
// row with a lower-case f, unlike its six siblings.
void statf10_deactivate() {
  if (sgpStatMiscDlg != NULL)
    dlg_deactivate(sgpStatMiscDlg);
  sgpStatMiscDlg = NULL;
}

// 0x4A6590 statF10_init (50 B). Loads the dialog and activates it against
// statF10_dlg_fcn. 241 is the line number 1.17 pushes with the owner tag.
void statF10_init() {
  sgpStatMiscDlg = dlg_load("rez\\stat_f10.bin",
                            reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                            reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                            OC_STATMISC_FILE, 0xF1);
  dlg_activate(sgpStatMiscDlg, 0,
               reinterpret_cast<PFN_CTRL_EVT>(statF10_dlg_fcn));
}
