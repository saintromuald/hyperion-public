// textbox.h -- declarations for src117/lang/textbox.cpp, oracle
// 0x4B38C0..0x4B3F23: the in-game chat textbox, its dialog and edit callbacks,
// the allied/all send-target mode switch, and the packet that carries a typed
// line to the other players. Free functions are __fastcall through /Gr and are
// declared without the keyword.
//
// targeting_msg, also in this TU, is declared in statbtn.h.

#ifndef OC117_TEXTBOX_H
#define OC117_TEXTBOX_H

#include "platform.h"
#include "dialog.h"

// Storm ordinal 146: __stdcall, two stack words. textbox_mode_switch is its
// only caller.
extern "C" {
BOOL __stdcall SNetGetReplyName(char *pszDest, unsigned int cchDest);
}

// The dialog's keypress hook, not a PFN_CTRL_EVT: it has a direct caller, and
// EAX at the ret is inherited rather than written, so the return is void.
void textbox_dlg_char(ctrl_t *pCtrl, dlgevt_t *pEvent);

// A callback with no direct call site. It returns int because it is a
// PFN_CTRL_EVT (dialog.h), whose dispatch site tests EAX.
int textbox_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// The edit control's event handler, same callback shape.
int textbox_edit_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// Builds the dialog.
void textbox_dlg_init(ctrl_t *pDlg);

// Both nullary; status.cpp is their only caller.
void textbox_free();
void textbox_init();

// Cycles the send target between all, allies and a named player. ECX, EDX and
// one stack word. The one body in this TU that reaches SNetGetReplyName.
void textbox_mode_switch(ctrl_t *pDlg, dlgevt_t *pEvent, BYTE bArg2);

// Sends a typed line to the other players.
void packet_send_string(const char *pszText);

// Refreshes the control's text.
void textbox_ctrl_update(ctrl_t *pCtrl);

// Rebuilds the "to <player>" prompt from the current filter mode.
void sub_4B3EB0(ctrl_t *pCtrl);

#endif  // OC117_TEXTBOX_H
