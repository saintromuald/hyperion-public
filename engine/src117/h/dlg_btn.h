// dlg_btn.h -- declarations for src117/lang/dlg_btn.cpp, the check box, radio
// button and push button control handlers. Free functions are __fastcall
// through /Gr and are declared without the keyword.

#ifndef OC117_DLG_BTN_H
#define OC117_DLG_BTN_H

#include "platform.h"
#include "dialog.h"

// btn_ctrl_pushbutton (0x4C7500) and btn_ctrl_radiobutton (0x4C7540) are
// declared in minimap.h and gluScore.h, not here: a second spelling is a second
// mangled name.

// btn_check_state -- 0x4C7440. ECX and EDX are both inputs, EDX only forwarded
// to global_evt_2_local_point. No caller reads EAX.
void btn_check_state(ctrl_t *pCtrl, dlgevt_t *pEvent);

// btn_ctrl_checkbox -- 0x4C74A0. The handler shape: every arm sets an explicit 1
// in EAX or tail-jumps to btn_generic, so it returns int, though all 3 callers
// discard it.
int btn_ctrl_checkbox(ctrl_t *pCtrl, dlgevt_t *pEvent);

// btn_generic -- 0x4C7590. No direct caller; every path returns 0 or 1.
BOOL btn_generic(ctrl_t *pCtrl, dlgevt_t *pEvent);

// btn_unpress -- 0x4C7660. One register argument; it ends in a tail jump to
// snd_play_btn, so void.
void btn_unpress(ctrl_t *pCtrl);

// set_radios -- 0x4C76A0. One register argument, no result.
void set_radios(ctrl_t *pCtrl);

#endif  // OC117_DLG_BTN_H
