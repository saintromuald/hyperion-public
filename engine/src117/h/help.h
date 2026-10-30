// help.h -- declarations for src117/lang/help.cpp: the in-game help dialog. Free
// functions are __fastcall through /Gr and are declared without the keyword.
// do_help_dlg is declared in opt_dlgs.h.

#ifndef OC117_HELP_H
#define OC117_HELP_H

#include "platform.h"
#include "dialog.h"   // ctrl_t, dlgevt_t

// 0x464730 (83 B). ECX only; the one caller discards EAX.
void help_activate(ctrl_t *pDlg);

// 0x464790 (120 B). The dialog proc do_help_dlg installs. ECX and EDX; every arm
// returns an explicit 1.
BOOL help_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x464810 and 0x464850. The help listbox's PFN_CTRL_EVT and its activation.
// Neither has a direct caller: the handler's address sits in sgHelpEvtFcnTbl
// (0x5085CC).
BOOL help_listbox_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
void help_listbox_ctrl_activate(ctrl_t *pCtrl);

#endif  // OC117_HELP_H
