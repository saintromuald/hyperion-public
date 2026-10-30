// snd_dlg.h -- the sound options dialog in src117/lang/snd_dlg.cpp (0x49ED20 to
// 0x49F060). do_sound_dlg is declared in opt_dlgs.h and snd_dlg_activate in
// netdlg.h.
//
// The handlers take (ctrl_t *, dlgevt_t *) in ECX and EDX, a plain two-argument
// free function under /Gr, and return a truth value in EAX.

#ifndef OC117_SND_DLG_H
#define OC117_SND_DLG_H

#include "platform.h"
#include "dialog.h"

// ECX is dereferenced twice: the dialog, then the node holding the tick count.
void set_update_timer(ctrl_t *pDlg);

BOOL snd_checkbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

void snd_checkbox_init(ctrl_t *pCtrl);

void snd_dlg_command(ctrl_t *pCtrl);

BOOL snd_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

void snd_dlg_timer(ctrl_t *pCtrl);

void snd_scrollbar_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

void snd_scrollbar_init(ctrl_t *pCtrl);

#endif  // OC117_SND_DLG_H
