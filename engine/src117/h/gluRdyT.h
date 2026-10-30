// gluRdyT.h -- declarations for src117/lang/gluRdyT.cpp: the ready room, its
// dialog event handler, and the eight portrait frame paths it installs. Free
// functions are __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_GLURDYT_H
#define OC117_GLURDYT_H

#include "platform.h"
#include "dialog.h"

// 0x461420 (211 B). Nullary; RunGlues, the one caller, discards EAX.
void do_readyt_dlg();

// 0x461560 (147 B). A PFN_CTRL_EVT, reached only by address: ECX and EDX are
// both inputs, and every exit is a tail jmp into ctrl_dlg or glurdy_dlg_command.
// It returns int, as dialog.h's PFN_CTRL_EVT does: the dispatch site tests EAX.
int glurdyt_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x461600 (82 B). Builds the eight-entry frame path table on its own stack
// and hands it to sub_4501F0 as two halves. ECX is a real input though the one
// caller never sets it: the body forwards it untouched to sub_460A70, which
// takes a dialog. glurdyt_dlg_fcn discards EAX.
void glurdyt_init_portrait_frames(ctrl_t *pDlg);

#endif  // OC117_GLURDYT_H
