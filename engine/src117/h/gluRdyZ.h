// gluRdyZ.h -- declarations for src117/lang/gluRdyZ.cpp: the Zerg ready room,
// its dialog event handler, and the eight portrait frame paths it installs. The
// free functions are __fastcall through /Gr and are declared without the
// keyword.

#ifndef OC117_GLURDYZ_H
#define OC117_GLURDYZ_H

#include "platform.h"
#include "dialog.h"

// 0x461660. Called from RunGlues.
void do_readyz_dlg();

// 0x4617A0. The dialog's PFN_CTRL_EVT; every exit is a tail jump into ctrl_dlg
// or glurdy_dlg_command.
int glurdyz_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x461840. Builds the eight-entry frame path table on its own stack and hands
// it to sub_4501F0 in two halves. ECX is the dialog, passed on untouched to
// sub_460A70, although the one caller never sets it.
void glurdyz_init_portrait_frames(ctrl_t *pDlg);

#endif  // OC117_GLURDYZ_H
