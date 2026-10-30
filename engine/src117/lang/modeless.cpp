// modeless.cpp -- the one in-game modeless dialog: startup and the shared
// handler. Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for, and why moving a
// body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/modeless.h"
#include "../h/opt_dlgs.h"        // modeless_dlg_shutdown
#include "../h/dlg_dlg.h"         // dlg_load, dlg_activate, dlg_deactivate, dlg_focus_free
#include "../h/dlg_ctrl.h"        // ctrl_dlg
#include "../h/TrigAct.h"         // accel_*, unpause_game, pause_game
#include "../h/cursor.h"          // cursor_lock, cursor_unlock, cursor_set
#include "../h/usel.h"            // usel_cancel_drag_evt
#include "../h/statbtn.h"         // placebox_is_on, target_is_on
#include "../h/placebox.h"        // set_placebox
#include "../h/target.h"          // target_off
#include "../h/minimap.h"         // ctxhelp_hide
#include "../h/snd.h"             // load_game_data
#include "../h/glues.h"           // alloc_load_bmp
#include "../gen/globals117.h"
#include "../port/front/port_gamemenu.h"  // Hyperion: PortGameMenuFor, PortGameMenuDrop

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The one modeless dialog the game keeps.
#ifdef OC_DLL_MODE
#define sgpModelessDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x672578u)))
#else
extern ctrl_t *sgpModelessDlg;
#endif

// The bodies, in oracle VA order.

// Replaces the modeless dialog with a new one, pausing a single-player game.
// The oracle inlines modeless_dlg_shutdown(1) at the top; that body lives in
// dlgFatal.cpp, so the sequence is spelled out here.
// Hyperion: in two parts, modeless_dlg_enter and the load. The game menu's own
// dialogs are Hyperion's while its menus are on (port_front_gamemenu.cpp), and 1.17's is not loaded.
void modeless_dlg_startup(void *pTemplate, void *pfnDlg) {
  if (PortGameMenuFor(static_cast<const char *>(pTemplate)))
    return;
  modeless_dlg_enter();
  sgpModelessDlg = dlg_load(pTemplate,
                            reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                            reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                            "modeless.cpp", 0x3F);
  dlg_activate(sgpModelessDlg, 0, reinterpret_cast<PFN_CTRL_EVT>(pfnDlg));
}

// Hyperion: modeless_dlg_startup's first part. The dialog already up is let go of,
// Hyperion's game menu as 1.17's are; then the state a dialog over the game holds: the game paused
// in single player, the cursor locked, the accelerators saved and only the sound's left.
void modeless_dlg_enter(void) {
  if (!gbMultiPlayerMode)
    unpause_game(1);
  if (sgpModelessDlg != NULL) {
    dlg_deactivate(sgpModelessDlg);
    sgpModelessDlg = NULL;
    if (gwGameMode == 3)
      accel_restore();
    cursor_unlock();
  } else if (PortGameMenuDrop()) {
    if (gwGameMode == 3)
      accel_restore();
    cursor_unlock();
  }
  dlg_focus_free();
  usel_cancel_drag_evt();
  cursor_set(0);
  if (!gbMultiPlayerMode)
    pause_game();
  if (gwGameMode == 3) {
    if (placebox_is_on())
      set_placebox(0, NULL, 1);
    if (target_is_on())
      target_off();
  }
  cursor_lock();
  if (gwGameMode == 3)
    accel_save();
  accel_set_sound();
  ctxhelp_hide();
}

// The shared dialog handler: answers 1 for the tags in the case list after
// letting ctrl_dlg see them.
BOOL modeless_opt_dlg(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  switch (pEvent->wTag) {
  case 0x00:
  case 0x01:
  case 0x03:
  case 0x04:
  case 0x05:
  case 0x06:
  case 0x07:
  case 0x08:
  case 0x09:
  case 0x0F:
    ctrl_dlg(pCtrl, pEvent);
    return TRUE;
  }
  return ctrl_dlg(pCtrl, pEvent);
}
