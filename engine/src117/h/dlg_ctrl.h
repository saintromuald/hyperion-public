// dlg_ctrl.h -- the interface of src117/lang/dlg_ctrl.cpp: the dialog control
// layer -- node walks, flag accessors, event constructors, and focus, hover
// and hotkey dispatch
//
// The node type, the event record and the two handler ABIs live in dialog.h;
// read its packing note before touching a displacement. Include after
// <stddef.h>, which dialog.h's static_asserts need.
#pragma once

// platform.h first: this header names BOOL, so a TU that reaches dlg_ctrl.h
// without platform.h ahead of it gets C2146.
#include "platform.h"
#include "dialog.h"
#include "rect16.h"

// The tab-order stepping function ctrl_find_tabstop is handed in EDX:
// ctrl_dlg_char picks between ctrl_tab_next and ctrl_tab_prev. One node in,
// one node out.
typedef ctrl_t *(__fastcall *PFN_CTRL_STEP)(ctrl_t *pCtrl);

// dlg_area_invalid -- oracle 0x4C4AE0. Every redraw path in this TU reaches it
// with ECX = the node and nothing else (ctrl_set_text and ctrl_force_update
// tail-jump straight into it).
void __fastcall dlg_area_invalid(ctrl_t *pCtrl);

// The CRT's non-locale uppercase primitive, which ctrl_check_hotkey_upper calls
// as `_toupper` -- the underscore-prefixed entry, not `toupper` -- passing and
// cleaning one dword by hand, so __cdecl with one int parameter. Declaring it
// here keeps the TU header-light; the #undef is needed because VC12's
// <ctype.h> also defines `_toupper` as a macro, which both breaks the
// declaration below and is not equivalent -- it subtracts unconditionally.
#if defined(_MSC_VER)
#ifdef _toupper
#undef _toupper

#endif
extern "C" {
int __cdecl _toupper(int c);
}
#else
#include <ctype.h>  // Hyperion: the C library's own _toupper
#endif

// The three bodies below sit outside the alphabetical list, after the
// `_toupper` block, so that no `#if` can leave them out of a TU.

// 0x4C1860 ctrl_text (36 B). A pfnEvt, which statres installs: ECX is not read
// here but the tail jump hands it to ctrl_evn_dlg unchanged, and EDX is the
// event, dispatched on wTag and dwId. Returns 0 on every path that does not
// tail-jump.
int ctrl_text(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4C17E0 ctrl_rect (87 B). The rectangle control's handler: frees the
// control's +0x10 allocation on event 1, then handles the event as ctrl_text.
int ctrl_rect(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4C1E90 ctrl_evn_dlg (437 B). ECX + EDX, both read as 32 bits and no stack
// parameters; every path to the ret leaves an int all seven callers discard.
int ctrl_evn_dlg(ctrl_t *pCtrl, dlgevt_t *pEvent);


// The TU's own functions; each body's address and size are in its .cpp banner.
void __fastcall ctrl_chain(ctrl_t *pCtrl, dlgevt_t *pEvent, PFN_CTRL_EVT fn);
int __fastcall ctrl_chainr(ctrl_t *pCtrl, dlgevt_t *pEvent, PFN_CTRL_EVT fn);
int __fastcall ctrl_check_hotkey_upper(ctrl_t *pCtrl, const char *pKey);
int __fastcall ctrl_check_hotkey(ctrl_t *pCtrl, const char *pKey);
int __fastcall ctrl_check_virt_hotkey(ctrl_t *pCtrl, const char *pKey);
void __fastcall ctrl_update_cursor(ctrl_t *pDlg, ctrl_t *pCtrl, dlgevt_t *pEvent);
void __fastcall ctrl_clear_mouse_in(ctrl_t *pDlg);
void __fastcall ctrl_activate(ctrl_t *pDlg);
void __fastcall ctrl_check_mouse_in(ctrl_t *pDlg, dlgevt_t *pEvent);
// 0x4C1470, 777 B -- the dialog's own control-event handler, the one every
// screen's `*_dlg_fcn` chains into. ECX the control and EDX the event, and
// replay_dlg_fcn tail-jumps it and returns its value as its own int.
int __fastcall ctrl_dlg(ctrl_t *pCtrl, dlgevt_t *pEvent);
// 0x4C1CB0 ctrl_dlg_char (391 B). ECX the dialog, EDX the key event; its one
// caller, ctrl_dlg, returns the full EAX.
int __fastcall ctrl_dlg_char(ctrl_t *pDlg, dlgevt_t *pEvent);
void __fastcall ctrl_dlg_set_current(ctrl_t *pCtrl, ctrl_t *pCurrent);
int __fastcall ctrl_evt(ctrl_t *pCtrl, dlgevt_t *pEvent);
ctrl_t *__fastcall ctrl_find(ctrl_t *pDlg, dlgevt_t *pEvent);
int __fastcall ctrl_find_key_handler(ctrl_t *pDlg, dlgevt_t *pEvent);
ctrl_t *__fastcall ctrl_find_ndx(ctrl_t *pCtrl, short nIndex);
int __fastcall ctrl_find_tabstop(ctrl_t *pDlg, PFN_CTRL_STEP fnStep);
void __fastcall ctrl_force_update(ctrl_t *pCtrl);
void __fastcall ctrl_gray(ctrl_t *pCtrl);
void __fastcall ctrl_hide(ctrl_t *pCtrl);
int __fastcall ctrl_is_hot(ctrl_t *pCtrl);
ctrl_t *__fastcall ctrl_tab_next(ctrl_t *pCtrl);
ctrl_t *__fastcall ctrl_tab_prev(ctrl_t *pCtrl);
ctrl_t *__fastcall ctrl_find_edit(ctrl_t *pDlg);
int __fastcall ctrl_query_locate(ctrl_t *pCtrl);
int __fastcall ctrl_send_cmd(ctrl_t *pCtrl, dlg_dword_t dwMask);
int __fastcall ctrl_send_cmd_index(ctrl_t *pCtrl, short nIndex);
int __fastcall ctrl_send_msg(ctrl_t *pCtrl, dlg_word_t wId, dlg_dword_t dwParam);
void __fastcall ctrl_set_big_text(ctrl_t *pCtrl);
void __fastcall ctrl_set_evt_fcns(ctrl_t *pCtrl, PFN_CTRL_EVT *pTable, dlg_dword_t cbTable);
void __fastcall ctrl_set_gray(ctrl_t *pCtrl, int bGray);
void __fastcall ctrl_set_hot_flag(ctrl_t *pCtrl);
void __fastcall ctrl_set_med_text(ctrl_t *pCtrl);
void __fastcall ctrl_set_mouse_in(ctrl_t *pDlg, ctrl_t *pCtrl);
void __fastcall ctrl_set_tabstop(ctrl_t *pCtrl);
void __fastcall ctrl_set_text(ctrl_t *pCtrl, void *pText);
void __fastcall ctrl_set_update(ctrl_t *pCtrl);
void __fastcall ctrl_set_val(ctrl_t *pCtrl, dlg_dword_t dwValue);
void __fastcall ctrl_show(ctrl_t *pCtrl);
void __fastcall ctrl_showhide(ctrl_t *pCtrl, int bShow);
void __fastcall ctrl_ungray(ctrl_t *pCtrl);
void __fastcall ctrl_update_defpushbutton(ctrl_t *pDlg);
int __fastcall cursor_in_dlg(ctrl_t *pCtrl, dlgevt_t *pEvent);
void __fastcall global_evt_2_local_point(ctrl_t *pCtrl, dlgevt_t *pEvent, point16_t *pPoint);
int __fastcall pt_in_rect(const rect16_t *pRect, const point16_t *pPoint);
