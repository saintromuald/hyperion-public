// gluPopup.h -- declarations for src117/lang/gluPopup.cpp: the pop-up dialogs the glue
// screens put over themselves -- the text editor, the history list, the OK and cancel
// boxes and the game-mode chooser.
//
// Member functions live in their class header; this file owns the TU's free functions.
// They are spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GLUPOPUP_H
#define OC117_GLUPOPUP_H

// <stddef.h> comes before dialog.h on purpose: dialog.h's static_asserts use offsetof
// and it does not include <stddef.h> itself.
#include <stddef.h>
#include "platform.h"
#include "dialog.h"
#include "rect16.h"

// The pointees below are the dialog family's own types, which the bytes do not pin: a
// handler is a PFN_CTRL_EVT (`ctrl_t *`, `dlgevt_t *`), a draw callback a PFN_CTRL_DRAW,
// and an activate, init or cmd row takes the owning dialog. All of them are 4 bytes in
// every slot.

// 0x45FA70 (140 B)
void glupedit_dlg_activate(ctrl_t *pDlg);

// 0x45FB00 (249 B)
void sub_45FB00(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x45FC00 (148 B) -- Reached only by address, so the argument list is a lower bound.
int glupedit_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x45FCC0 (96 B) -- EDX is a pointer whose class the bytes do not name.
BOOL glupedit_edit_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x45FD50 (393 B)
BYTE do_pedit_dlg(intptr_t nArg0, intptr_t nArg1, int nArg2, intptr_t nArg3);

// 0x45FEE0 (6 B)
ctrl_t *get_popup_dlg();

// 0x45FEF0 (255 B)
intptr_t do_hist_popup(intptr_t nArg0, int nArg1);

// 0x45FFF0 (268 B)
BYTE do_select_gamemode_dlg();

// 0x460100 (294 B)
BYTE do_pok_split_dlg(intptr_t nArg0, intptr_t nArg1);

// 0x460230 (255 B)
BYTE do_pok_dlg(intptr_t nArg0);

// 0x460330 (259 B)
BYTE do_pok_cancel_dlg(intptr_t nArg0);

// 0x460440 (13 B)
void set_popup_dlg(ctrl_t *pDlg, LONG nResult);

// 0x460450 (132 B) -- ECX is only forwarded to a callee, so its width comes from that
// callee.
void gluHist_dlg_init(ctrl_t *pDlg);

// 0x4604E0 (62 B) -- ECX is only forwarded to a callee, so its width comes from that
// callee.
void gluHist_dlg_cmd(ctrl_t *pDlg);

// 0x460520 (57 B) -- Reached only by address, so the argument list is a lower bound.
int gluHist_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x460560 (26 B) -- ECX is only forwarded to a callee, so its width comes from that
// callee.
void deactivate_popup_dlg(ctrl_t *pDlg);

// 0x460580 (64 B)
void gluPopup_dlg_init(ctrl_t *pDlg);

// 0x4605C0 (27 B) -- Reached only by address, so the argument list is a lower bound.
// ECX is a pointer whose class the bytes do not name. EDX carries an argument the
// body never reads, so `int` is a placeholder. The body never reads arg0 and arg1, so
// only their 4-byte footprints are known.
void popup_txt_update_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// 0x4605E0 (234 B) -- EAX is defined on some paths to a `ret` and not others, so a return
// value would be garbage on the rest; no caller reads it.
void gluPopup_scale_message_size(ctrl_t *pCtrl);

// 0x4606D0 (101 B)
void gluPopup_dlg_activate(ctrl_t *pDlg);

// 0x460740 (118 B) -- Reached only by address, so the argument list is a lower bound.
int gluPopup_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 21 declaration(s); 13 carry an unfalsifiable or note line.

#endif  // OC117_GLUPOPUP_H
