// dlg_lbox.h -- the interface of src117/lang/dlg_lbox.cpp: the listbox and
// combo controls -- the item and data accessors, the selection and scroll
// bookkeeping, the mouse and timer handlers, and the two draw entries the
// control tables dispatch through
//
// lbox_t is only forward-declared here and its derived layout lives in
// src117/lang/dlg_lbox.cpp: the listbox node is a per-type arm of the dialog
// manager's one node type, and four of its fields sit at the same offsets as
// dialog-arm fields in src117/h/dialog.h (+0x3E, +0x42, +0x46, +0x4A). Folding
// the two into one union is a later job, and a pointer parameter needs the name
// only. Scalars here are plain C types, not dialog.h's typedefs. Include after
// <stddef.h>, which dialog.h's static_asserts need.
#pragma once
#include "platform.h"  // LONG and ULONG

#include "dialog.h"
#include "rect16.h"

// The listbox/combo node. Defined in src117/lang/dlg_lbox.cpp -- see above.
struct lbox_t;

// lbox_draw_combo2 -- oracle 0x4C5B80. lbox_draw_list -- oracle 0x4C5D40.
// draw_combo tail-calls the second and falls through from the first, forwarding
// all four arguments unchanged, so both are PFN_CTRL_DRAW-shaped: ECX + EDX +
// two stack dwords, `ret 8h`.
void __fastcall lbox_draw_combo2(lbox_t *pBox, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall lbox_draw_list(lbox_t *pBox, int nXOfs, int nYOfs, rect16_t *prcClip);

// lbox_calc_pos -- oracle 0x4C62D0. The row under the cursor. ECX = the box,
// EDX = the event, nothing on the stack; both call sites consume the result one
// byte wide, which is what makes the return a byte and not an int.
unsigned char __fastcall lbox_calc_pos(lbox_t *pBox, dlgevt_t *pEvent);

// lbox_recalc -- oracle 0x4C6DB0. The deferred layout pass lbox_set_recalc and
// lbox_remove_all run once the 0x80000000 suppression bit clears. One register
// argument, no stack, plain `ret`.
void __fastcall lbox_recalc(lbox_t *pBox);

// lbox_remove_item -- oracle 0x4C6F60. ECX = the box, EDX = the item index,
// byte-wide at both call sites. Returns the removed item pointer, or NULL once
// the box has freed it.
void *__fastcall lbox_remove_item(lbox_t *pBox, unsigned char nItem);

// scroll_set_page_size -- oracle 0x4C9AD0. scroll_set_range -- oracle 0x4C9B20.
// Both are `ret 4h`: ECX + EDX + one stack dword. lbox_update_scroll is the
// only caller here and supplies (bar, nVisible, 1) and (bar, 0, nScrollRange);
// scroll_set_range's register argument is its second one and the range is the
// pushed one.
void __fastcall scroll_set_page_size(ctrl_t *pScroll, unsigned char nPage, ULONG dwFlag);
// The third argument is 16-bit, not 32: 0x4C9B20 reads the stack slot as a
// word, and `unsigned long` emits the wider load.
void __fastcall scroll_set_range(ctrl_t *pScroll, ULONG dwMin, unsigned short wMax);

// The dialog library's own event handlers, for src117/lang/gluCreat.cpp. Each
// takes the shape every other control-event handler here has: the node and a
// dlgevt_t in ECX and EDX, __fastcall, an int result.

// 0x4C63B0, 170 B. It tail-jumps lbox_ctrl_list four times, so it is the
// combo-box arm of the same handler and takes the same pair; both call sites
// forward their own incoming pair without reloading it.
int __fastcall lbox_ctrl_combo(lbox_t *pBox, dlgevt_t *pEvent);

// 0x4C9630, 258 B. The same shape, plain `ret`, tail-jumping ctrl_evn_dlg at
// four sites and scroll_key_down; its one caller also forwards its own pair.
int __fastcall scroll_ctrl_scroll(ctrl_t *pScroll, dlgevt_t *pEvent);

// 0x4C9920, 5 B. ECX only, and a free function rather than a thiscall member
// despite the single register. The return is a signed 16-bit value: all ten
// call sites consume EAX 16-bit and sign-extend it, so `int` is the wrong
// width -- a difference the byte gate cannot see.
short __fastcall scroll_get_pos(ctrl_t *pScroll);

// The TU's own functions, in the oracle's own .text order, which under /Ox is
// Blizzard's declaration order; each body's address and size are in its .cpp
// banner.
void __fastcall combo_reset_slider(lbox_t *pBox);
void __fastcall combo_lmouse_down(lbox_t *pBox, dlgevt_t *pEvent);
void __fastcall combo_lmouse_up(lbox_t *pBox, dlgevt_t *pEvent);
void __fastcall combo_restore(lbox_t *pBox);
void __fastcall draw_combo(lbox_t *pBox, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall draw_lbox(lbox_t *pBox, int nXOfs, int nYOfs, rect16_t *prcClip);
unsigned char __fastcall lbox_get_count(lbox_t *pBox);
void *__fastcall lbox_get_data(lbox_t *pBox, unsigned char nItem);
void *__fastcall lbox_get_item(lbox_t *pBox, unsigned char nItem);
int __fastcall lbox_get_scroll(lbox_t *pBox);
unsigned char __fastcall lbox_get_selection(lbox_t *pBox);
void __fastcall lbox_hide(lbox_t *pBox);
void __fastcall lbox_lmouse_down(lbox_t *pBox, dlgevt_t *pEvent);
void __fastcall lbox_mouse_moved(lbox_t *pBox, dlgevt_t *pEvent);
void __fastcall lbox_remove_all(lbox_t *pBox);
void __fastcall lbox_remove_current(lbox_t *pBox);
void __fastcall lbox_scroll_timer(lbox_t *pBox, dlgevt_t *pEvent);
void *__fastcall lbox_set_data(lbox_t *pBox, unsigned char nItem, void *pNew);
void __fastcall lbox_set_recalc(lbox_t *pBox, unsigned char bEnable);
void __fastcall lbox_set_selection(lbox_t *pBox, unsigned char nItem);
void __fastcall lbox_update_pos(lbox_t *pBox, unsigned char nItem);
void __fastcall lbox_update_pos_callback(lbox_t *pBox);
void __fastcall lbox_update_scroll(lbox_t *pBox, ctrl_t *pScroll);
void __fastcall lbox_deactivate(lbox_t *pBox);
void __fastcall lbox_init(lbox_t *pBox);
// The default per-item draw callback lbox_init installs at +0x52: `ret 0Ch`,
// ECX + DL + three stack dwords (the row rectangle and the two draw offsets).
void __fastcall draw_lbox_item(lbox_t *pBox, unsigned char nItem, rect16_t *prc,
                           int nXOfs, int nYOfs);
int __fastcall lbox_key_down(lbox_t *pBox, dlgevt_t *pEvent);
// The listbox event handler, spelled without __fastcall, which /Gr supplies,
// and taking ctrl_t * because every consumer hands it a control.
int lbox_ctrl_list(ctrl_t *pCtrl, dlgevt_t *pEvent);

// lbox_insert_item -- oracle 0x4C6A70 (317 B), lbox.cpp. `ret 8h`, so ECX + EDX
// plus two stack words. The widths come from the body: EDX is read as 8 bits,
// the second stack slot only ever one byte, and the EAX at the ret is a
// leftover from lbox_recalc no caller consumes. gluLogin.cpp calls it with the
// item as a string and the flags as a literal 0, and discards the result.
void lbox_insert_item(ctrl_t *pCtrl, BYTE nIndex, const char *pszItem,
                      BYTE nFlags);

// lbox_add_item -- oracle 0x4C61D0 (249 B), `ret 4h`. Appends an item and
// returns its index, or 0xFF when the box is full; every caller reads AL.
// The flags are a stack byte (`test byte ptr [ebp+8h], 10h`).
BYTE lbox_add_item(ctrl_t *pCtrl, const char *pszItem, BYTE nFlags);
int __fastcall pt_inside_border(lbox_t *pBox, dlgevt_t *pEvent);
