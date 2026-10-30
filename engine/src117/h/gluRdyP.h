// gluRdyP.h -- declarations for src117/lang/gluRdyP.cpp: the Protoss ready
// room, its dialog event handler, and the eight portrait frame paths it
// installs. The free functions are __fastcall through /Gr and are declared
// without the keyword.

#ifndef OC117_GLURDYP_H
#define OC117_GLURDYP_H

#include "platform.h"
#include "dialog.h"

// 0x4611E0. Called from RunGlues.
void do_readyp_dlg();

// 0x461320. The dialog's PFN_CTRL_EVT; every exit is a tail jump into ctrl_dlg
// or glurdy_dlg_command.
int glurdyp_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4613C0. Builds the eight-entry frame path table on its own stack and hands
// it to sub_4501F0 in two halves. ECX is the dialog, passed on untouched to
// sub_460A70, although the one caller never sets it.
void glurdyp_init_portrait_frames(ctrl_t *pDlg);

#endif  // OC117_GLURDYP_H
