// VictDlg.h -- declarations for src117/lang/VictDlg.cpp, 0x4BCF30 to 0x4BD45B:
// the victory and defeat dialogs, their activate handlers and the two command
// handlers that decide what happens after the player dismisses one. The free
// functions are __fastcall through /Gr and are declared without the keyword.
//
// Three functions of the TU are declared elsewhere: victdlg_quit in
// opt_dlgs.h, do_loss_dlg and do_victory_dlg in victory.h.

#ifndef OC117_VICTDLG_H
#define OC117_VICTDLG_H

#include "platform.h"
#include "dialog.h"

// Clears the two module flags that make do_loss_dlg and do_victory_dlg
// idempotent.
void victdlg_init();

// The defeat dialog's activate handler. ECX is the dialog: the body forwards it
// to dfl_load_bmp before anything else touches it.
void loss_activate(ctrl_t *pDlg);

// Runs the choice the player made in the defeat dialog, reading it out of the
// module's own result slot. pCtrl is unused: loss_dlg_fcn still holds it in ECX
// at the call, which keeps pCtrl in ESI in that caller.
void loss_dlg_cmd(ctrl_t *pCtrl);

// The defeat dialog's event handler, which do_loss_dlg hands to
// modeless_dlg_startup.
int loss_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// The victory dialog's activate handler, the same shape as loss_activate.
void victory_activate(ctrl_t *pDlg);

// Runs the choice the player made in the victory dialog. pCtrl is unused, as
// with loss_dlg_cmd, and victory_dlg_fcn needs the use for its allocation.
void victory_dlg_cmd(ctrl_t *pCtrl);

// The victory dialog's event handler; do_victory_dlg hands it to
// modeless_dlg_startup.
int victory_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

#endif  // OC117_VICTDLG_H
