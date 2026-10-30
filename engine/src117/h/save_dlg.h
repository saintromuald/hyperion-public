// save_dlg.h -- declarations for src117/lang/save_dlg.cpp, the save and load
// game dialogs: the modeless save and load dialogs, their listbox and edit
// control event handlers, the overwrite and delete confirmation modals, and the
// "waiting for other players" pump. Free functions are __fastcall through /Gr
// and are declared without the keyword.

#ifndef OC117_SAVE_DLG_H
#define OC117_SAVE_DLG_H

#include "platform.h"
#include "dialog.h"
#include "modeless.h"   // modeless_dlg_startup, modeless_opt_dlg

// The dialog event callbacks, BOOL fn(ctrl_t *, dlgevt_t *). Each reads the
// event through EDX at dlgevt_t's wConst (+0x0C) and dwId (+0x00) and returns
// an explicit 1 on every handled path; save_edit_fcn also writes its ctrl_t
// through ECX.
BOOL load_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
BOOL save_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
BOOL save_edit_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
BOOL save_listbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// saveload_dlg_activate takes the same pair and returns nothing a caller reads.
// saveload_dlg_deactivate ends in tail jumps to modeless_menu_deactivate and
// modeless_opt_dlg, and its one consuming caller uses the full EAX, so BOOL.
void saveload_dlg_activate(ctrl_t *pCtrl, dlgevt_t *pEvent);
BOOL saveload_dlg_deactivate(ctrl_t *pCtrl, dlgevt_t *pEvent);

// edit_key_evt's only call site is save_edit_fcn's, with the handler's own ECX
// and EDX still live, so it takes the same pair.
void edit_key_evt(ctrl_t *pCtrl, dlgevt_t *pEvent);

// The modal result callbacks, int fn(WORD). ECX is read as 16 bits only, and
// compared against 0xFFFE; an int parameter compiles differently. Both are
// reached only by address, handed in EDX to modal_ok_dlg or modal_okcancel_dlg,
// and every path sets EAX.

int do_delete(WORD wResult);
int do_replace(WORD wResult);

// redo_save is the same family with no argument read at all: it only re-opens
// the save dialog and returns 1. Its address is taken 3 times.
BOOL redo_save();

// One ctrl_t * in ECX. do_load_game, do_delete_game and do_save_game take the
// dialog and look up a control in it with ctrl_find_ndx; no caller reads
// do_save_game's EAX. save_listbox_init's ECX is already the listbox, which it
// normalises to the parent with ctrl_find_ndx(-2) and (3) for the two buttons.

void do_load_game(ctrl_t *pDlg);
void do_save_game(ctrl_t *pDlg);
void do_delete_game(ctrl_t *pDlg);
void save_listbox_init(ctrl_t *pDlg);

// 0x49B360 (63 B), which replay.cpp also calls. ECX is the listbox control: the
// body does ctrl_find_ndx(ecx, 3), lbox_get_count and lbox_get_selection on it,
// then tail-jumps ctrl_ungray or ctrl_gray. All call sites discard EAX.
void gray_delete_btn(ctrl_t *pBox);

// Tests gbMultiPlayerMode, then a Storm time import against sgdwLastSaveTime +
// 0x1D4C0. Every return is 0 or 1 and both callers read the full EAX, so BOOL.
BOOL CanSaveGame();

void do_multi_load_dlg();
void do_multi_save_dlg();

// The dialog passes through ECX untouched. Both bodies read gnDlgReturnVal
// (0x6CE6DC) and dispatch by conditional tail jump to do_load_game,
// do_delete_game, do_save_game or dlg_deactivate with no ECX setup, and each of
// those reads a ctrl_t * from ECX. Without the parameter the compiler would set
// ECX before each jump. No caller reads EAX.
void load_dlg_cmd(ctrl_t *pDlg);
void save_dlg_cmd(ctrl_t *pDlg);

// Pumps RunEvtDispatcher until gnDlgCount (0x6CE658) falls back to its entry
// value.
void wait_dlg_done();

// The one __stdcall: no register input and `ret 4h`. Reached only by address,
// once; its argument is a pointer to something the body does not name.

void __stdcall sub_49ACE0(void *pArg0);


// Callees in other TUs that no other header declares.

// dlg_dlg.cpp / message_box.cpp -- the modal dialog entry points. The two
// modeless ones are in modeless.h, modeless_menu_deactivate in opt_dlgs.h.
void modal_ok_dlg(const char *pszText, void *pfnResult);
void modal_okcancel_dlg(const char *pszText, void *pfnResult);

// dlg_edit.cpp -- edit control operations. lbox_ctrl_list is declared in
// dlg_lbox.h, which every caller includes.
void edit_set_text(ctrl_t *pCtrl, const char *pszText);

// edit_get_text (0x4C7D50, 15 B): ECX the edit control, EDX the destination
// buffer, both forwarded to SStrCopy; all call sites discard EAX.
// edit_ctrl_edit (0x4C7880, 781 B): ECX and EDX; replaydlg_edit_name_fcn and
// save_edit_fcn both tail-jump to it and return its value. replay.cpp gets both
// from this header; lbox_add_item is declared in dlg_lbox.h.
void edit_get_text(ctrl_t *pCtrl, char *pszBuf);
int edit_ctrl_edit(ctrl_t *pCtrl, dlgevt_t *pEvent);

// event_Win.cpp -- the event pump wait_dlg_done spins on.
void RunEvtDispatcher(int nMode);

// do_gamemenu_dlg, which load_dlg_cmd and save_dlg_cmd tail-jump to, is
// declared in opt_dlgs.h.

// The CRT's _mbsicmp, 0x4D2B80, which reads the CRT's MBCS code-page state.
// __cdecl: do_load_game and do_save_game push two arguments and clean up with
// `add esp, 8h`.
extern "C" {
int __cdecl _mbsicmp(const char *psz1, const char *psz2);
}


// 0x4C8100 edit_set_recalc (7 B), in dlg_edit.cpp like the rest of the edit
// family here; gluChat.cpp's ctrl_edit_msgtosend_ctrl_fcn calls it. ECX is the
// edit control and EDX is read as 8 bits; EAX is never set.
void edit_set_recalc(ctrl_t *pEdit, BYTE bRecalc);
#endif  // OC117_SAVE_DLG_H
