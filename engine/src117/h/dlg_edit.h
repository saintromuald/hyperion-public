// dlg_edit.h -- declarations for src117/lang/dlg_edit.cpp, the single-line
// edit control: its handler, its text buffer, the caret, and the DBCS helper
// that keeps the caret off a trail byte. The free functions are __fastcall
// through /Gr and are declared without the keyword.
//
// edit_ctrl_edit, edit_get_text, edit_set_recalc and edit_set_text are declared
// in save_dlg.h and edit_draw_text in dlg_draw.h.

#ifndef OC117_DLG_EDIT_H
#define OC117_DLG_EDIT_H

#include "platform.h"

struct ctrl_t;
struct dlgevt_t;

// 0x4C7730. Classifies a byte position in a Korean-locale string: 1 lead byte,
// 2 trail byte, 0 neither.
int sub_4C7730(const char *psz, int nIndex);

// 0x4C77D0. Inserts one character, from DL, at the caret.
void edit_add_char(ctrl_t *pCtrl, BYTE bChar);

// 0x4C7840. The blink timer's callback: flips the caret bit and re-arms itself
// for the other half of the cycle. nId is the timer id it passes on to
// timer_create.
void edit_ctrl_timer_fn(ctrl_t *pCtrl, int nId);

// 0x4C7C00. Frees the text buffer and drops the blink timer and the hooks.
void edit_deactivate(ctrl_t *pCtrl);

// 0x4C7D60. Allocates the 0x100-byte text buffer and clears the caret state.
void edit_init(ctrl_t *pCtrl);

// 0x4C7DA0. Moves the caret to the character the click landed on, from the
// event's nX and nY.
void edit_line_clicked(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4C7E50. Hides the mouse cursor while typing and restores it after. DL is
// stored straight into the flag it guards on.
void edit_obscure_cursor(ctrl_t *pCtrl, BYTE bHide);

// 0x4C7EB0. Recomputes the first visible character so the caret stays inside
// the control's width.
void edit_recalc(ctrl_t *pCtrl);

// 0x4C8090. Deletes the character before or at the caret, by DL.
void edit_remove_char(ctrl_t *pCtrl, BYTE bForward);

#endif  // OC117_DLG_EDIT_H
