// dlgFatal.cpp -- the fatal-error dialog shown over the game before it exits.
// Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for, and why moving a
// body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/dlgFatal.h"
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The bodies, in oracle VA order.

#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_draw.h"
#include "../h/bmp.h"
#include "../h/cursor.h"
#include "../h/minimap.h"    // mou_cager
#include "../h/TrigAct.h"    // accel_save / accel_restore
#include "../h/target.h"
#include "../h/placebox.h"
#include "../h/statbtn.h"    // placebox_is_on / target_is_on
#include "../h/opt_dlgs.h"   // modeless_dlg_shutdown
#include "../port/front/port_gamemenu.h"  // Hyperion: PortGameMenuDrop
#include "../h/snd.h"        // load_game_data
#include "../h/glues.h"      // alloc_load_bmp
#include "../h/rect32.h"
#include "../h/Storm.h"      // SMemAlloc / SMemFree
#include "../h/gds_mou_win.h"
#include "../port/render/port_present.h"  // Hyperion: PortClipCursor
#include "../port/hud/port_hudinput.h"  // Hyperion: PortHudCage

#include <string.h>

// The two heap copies of the message text, and the dialog itself. Both message
// slots are char * and are freed with the owner tag "dlgFatal.cpp".
#ifdef OC_DLL_MODE
#define sgpszMsg1 (*reinterpret_cast<char **>(OC_VA(0x665DD8u)))
#define sgpszMsg2 (*reinterpret_cast<char **>(OC_VA(0x665DDCu)))
#else
extern char *sgpszMsg1;
extern char *sgpszMsg2;
#endif

// 0x665DD4, the modal dialog do_dlgfatal runs.
#ifdef OC_DLL_MODE
#define sgpFatalDlg_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x665DD4u)))
#else
extern ctrl_t *sgpFatalDlg_g;
#endif

// 0x50F0F0, the screen-is-faded latch. A byte: every reach is `cmp byte ptr`.
#ifdef OC_DLL_MODE
#define gbFadedOut (*reinterpret_cast<BYTE *>(OC_VA(0x50F0F0u)))
#else
extern BYTE gbFadedOut;
#endif

// 0x672578, the one modeless dialog the game keeps.
#ifdef OC_DLL_MODE
#define sgpModelessDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x672578u)))
#else
extern ctrl_t *sgpModelessDlg;
#endif

// The mouse cage, three gds/mou_win.cpp slots. sgbIsCaged_g is a DWORD despite
// the name (`mov dword ptr [0x6D3164], 1h`), and sgwCage_g is the 16-byte
// rectangle mou_cager copies with one movdqu and hands to ClipCursor.
#ifdef OC_DLL_MODE
#define dword_6D3160 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3160u)))
#define sgbIsCaged_g (*reinterpret_cast<DWORD *>(OC_VA(0x6D3164u)))
#define sgwCage_g (*reinterpret_cast<rect32_t *>(OC_VA(0x6D3168u)))
#else
extern DWORD dword_6D3160;
extern DWORD sgbIsCaged_g;
extern rect32_t sgwCage_g;
#endif

// 0x43EC00. Raises the fatal dialog over the running game: drops the placement
// box, the target cursor and the modeless dialog, copies both message strings
// onto Storm's heap and runs the dialog modally. The owner lines 0xE1, 0xE5 and
// 0xEA are the oracle's own bytes, not __LINE__.
void do_dlgfatal(const char *pszMsg1, const char *pszMsg2)
{
  if (gbFadedOut)
    return;

  if (placebox_is_on())
    set_placebox(0, NULL, 1);
  if (target_is_on())
    target_off();
  modeless_dlg_shutdown(1);
  if (sgpFatalDlg_g != NULL)
    dlg_deactivate(sgpFatalDlg_g);

  if (pszMsg1 != NULL) {
    char *psz = static_cast<char *>(
        SMemAlloc((DWORD)(strlen(pszMsg1) + 1), "dlgFatal.cpp", 0xE1, 0));

    sgpszMsg1 = psz;
    strcpy(psz, pszMsg1);
  }
  if (pszMsg2 != NULL) {
    char *psz = static_cast<char *>(
        SMemAlloc((DWORD)(strlen(pszMsg2) + 1), "dlgFatal.cpp", 0xE5, 0));

    sgpszMsg2 = psz;
    strcpy(psz, pszMsg2);
  }

  sgpFatalDlg_g = dlg_load("rez\\dlgFatal.bin",
                           reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                           reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                           "dlgFatal.cpp", 0xEA);
  dlg_run_modal(sgpFatalDlg_g, 0, dlgfatal_interact_fcn);
  sgpFatalDlg_g = NULL;
}

// 0x43ED10. The button frame: a filled outer rectangle, a one-pixel inset one,
// then the caption. Pen 0 when flag 0x40000000 is set, pen 16 otherwise.
void dlgfatal_button_draw_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                              rect16_t *prcClip)
{
  _bmp_pen = (pCtrl->dwFlags & 0x40000000) != 0 ? sgbPens_g[0] : sgbPens_g[16];
  bmp_rect(pCtrl->rc.left, pCtrl->rc.top,
           pCtrl->rc.right - pCtrl->rc.left + 1,
           pCtrl->rc.bottom - pCtrl->rc.top + 1);
  bmp_rect(pCtrl->rc.left + 1, pCtrl->rc.top + 1,
           pCtrl->rc.right - pCtrl->rc.left - 1,
           pCtrl->rc.bottom - pCtrl->rc.top - 1);
  if (pCtrl->pData != NULL) {
    gbFontJust = 0x22;
    draw_ctrl_text(pCtrl, 0, 0, &pCtrl->rc);
  }
}

// 0x43EDB0. The dialog background: fill the clip rectangle when there is one,
// else the dialog's own extent anchored at the origin, then two borders.
void dlgfatal_draw_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  _bmp_pen = sgbPens_g[15];
  if (prcClip != NULL)
    bmp_rect_fill(prcClip->left, prcClip->top,
                  prcClip->right - prcClip->left + 1,
                  prcClip->bottom - prcClip->top + 1);
  else
    bmp_rect_fill(0, 0, pCtrl->rc.right - pCtrl->rc.left + 1,
                  pCtrl->rc.bottom - pCtrl->rc.top + 1);

  _bmp_pen = sgbPens_g[16];
  bmp_rect(0, 0, pCtrl->rc.right - pCtrl->rc.left + 1,
           pCtrl->rc.bottom - pCtrl->rc.top + 1);
  bmp_rect(1, 1, pCtrl->rc.right - pCtrl->rc.left - 1,
           pCtrl->rc.bottom - pCtrl->rc.top - 1);
}

// 0x43EE70. The two message controls' draw handler: text only, no frame.
void dlgfatal_msg_draw_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                           rect16_t *prcClip)
{
  if (pCtrl->pData != NULL) {
    gbFontJust = 0x22;
    draw_ctrl_text(pCtrl, 0, 0, &pCtrl->rc);
  }
}

// 0x43EE90. Installs this TU's handlers on the dialog and its controls, hangs
// the two message strings on controls -10 and -20, and takes the cursor and the
// accelerator table. Control -2 is dereferenced unchecked, as the oracle does.
void dlgfatal_activate(ctrl_t *pDlg)
{
  ctrl_t *pCtrl;
  // __restrict puts the sgpszMsg1 compare ahead of the store below, which is
  // the oracle's schedule. re/attempts/dlgfatal_activate.md.
  ctrl_t *__restrict pBtn;

  if (gwGameMode == 3)
    pDlg->dwFlags |= 0x10000000;
  pDlg->pfnDlgProc = reinterpret_cast<void *>(dlgfatal_modal_fcn);
  pDlg->pfnDraw = dlgfatal_draw_fcn;
  pBtn = ctrl_find_ndx(pDlg, -2);
  pBtn->pfnDraw = dlgfatal_button_draw_fcn;

  if (sgpszMsg1 != NULL) {
    pCtrl = ctrl_find_ndx(pDlg, -10);
    if (pCtrl != NULL) {
      pCtrl->pData = sgpszMsg1;
      pCtrl->pfnDraw = dlgfatal_msg_draw_fcn;
    }
  }
  if (sgpszMsg2 != NULL) {
    pCtrl = ctrl_find_ndx(pDlg, -20);
    if (pCtrl != NULL) {
      pCtrl->pData = sgpszMsg2;
      pCtrl->pfnDraw = dlgfatal_msg_draw_fcn;
    }
  }

  cursor_set(0);
  cursor_lock();
  cur_show();
  accel_save();
  accel_set_sound();
  ctxhelp_hide();
}

// 0x43EF40. The oracle's owner lines are 0x99 and 0x9F and they are the bytes,
// not a guess -- SMemFree takes the tag and the line as ordinary arguments.
void dlgfatal_deactivate(ctrl_t *pDlg)
{
  ctrl_t *pCtrl;

  mou_cager(0);

  pCtrl = ctrl_find_ndx(pDlg, -10);
  if (pCtrl != NULL)
    pCtrl->pData = NULL;

  pCtrl = ctrl_find_ndx(pDlg, -20);
  if (pCtrl != NULL)
    pCtrl->pData = NULL;

  if (sgpszMsg1 != NULL) {
    SMemFree(sgpszMsg1, "dlgFatal.cpp", 0x99, 0);
    sgpszMsg1 = NULL;
  }

  if (sgpszMsg2 != NULL) {
    SMemFree(sgpszMsg2, "dlgFatal.cpp", 0x9F, 0);
    sgpszMsg2 = NULL;
  }

  cursor_unlock();
  accel_restore();
}

// 0x43EFD0. The dialog's event handler. Event 10 cages the mouse to the
// dialog's rectangle; the others activate, tear down, or fall to ctrl_dlg.
int dlgfatal_interact_fcn(ctrl_t *pDlg, dlgevt_t *pEvent)
{
  rect32_t rc;

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 10:
      rc.left = pDlg->rc.left;
      rc.top = pDlg->rc.top;
      rc.right = pDlg->rc.right;
      rc.bottom = pDlg->rc.bottom;
      mou_cager((int)reinterpret_cast<intptr_t>(&rc));
      break;
    case 0:
      dlgfatal_activate(pDlg);
      break;
    case 2:
      dlg_deactivate(pDlg);
      return 1;
    case 1:
      dlgfatal_deactivate(pDlg);
      ctrl_dlg(pDlg, pEvent);
      return 1;
    }
  }
  return ctrl_dlg(pDlg, pEvent);
}

// 0x43F090. The dialog's per-frame callback, a bare tail jump.
void dlgfatal_modal_fcn(ctrl_t *pDlg, dlgevt_t *pEvent)
{
  cursor_update();
}

// 0x46EC70. Drops the modeless dialog, unpausing a single-player game first and
// handing the accelerator table and the cursor back when one was up.
__declspec(noinline) void modeless_dlg_shutdown(BYTE bDeactivate)
{
  if (!gbMultiPlayerMode)
    unpause_game(1);

  if (sgpModelessDlg != NULL) {
    if (bDeactivate)
      dlg_deactivate(sgpModelessDlg);
    sgpModelessDlg = NULL;
    if (gwGameMode == 3)
      accel_restore();
    cursor_unlock();
  } else if (PortGameMenuDrop()) {  // Hyperion: Hyperion's game menu was the one up
    if (gwGameMode == 3)
      accel_restore();
    cursor_unlock();
  }
}

// 0x4CE850. Clips the pointer to a rectangle, or releases it and re-reads the
// screen rectangle when the argument is null. The parameter is really a
// rect32_t *; src117/h/minimap.h spells it int and src117/lang/usel.cpp already
// passes a cast pointer, so the spelling is kept rather than split.
__declspec(noinline) void mou_cager(intptr_t nMode)
{
  if (nMode != 0) {
    rect32_t rc = *reinterpret_cast<rect32_t *>(nMode);

    PortHudCage(&rc);  // Hyperion: the minimap's cage where the HUD shows it
    sgbIsCaged_g = 1;
    sgwCage_g = rc;
  } else {
    UpdateScreenRect();
    sgbIsCaged_g = 0;
  }
  if (dword_6D3160 != 0)
    PortClipCursor(reinterpret_cast<RECT *>(&sgwCage_g));  // Hyperion: the cage is in game pixels
}

// 0x4CE9C0. A filled rectangle, one bmp_hline per row.
__declspec(noinline) void bmp_rect_fill(unsigned short nX, unsigned short nY,
                                        unsigned short nWidth,
                                        unsigned short nHeight)
{
  short i;

  for (i = 0; i < nHeight; i++)
    bmp_hline(nX, nY + i, nWidth);
}
