// StatLB.h -- declarations for src117/lang/StatLB.cpp: the in-game status
// listbox dialog -- its draw handlers and its activate/deactivate pair.
//
// Class members belong in the class header; this file owns the free functions,
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_STATLB_H
#define OC117_STATLB_H

#include "platform.h"
#include "dialog.h"   // ctrl_t, dlgevt_t, rect16_t
#include "rect16.h"

// 0x4A5AF0 statlb_playercolor_icon_draw (60 B). 0x4A5B30
// statlb_resource_icon_draw (79 B). 0x4A5B80 statlb_text_draw (77 B).
// All three are installed into ctrl_t::pfnDraw by statlb_ctrl_init_fn, so the
// +0x2E slot's signature is theirs: ECX + EDX + two stack dwords, `ret 8h`.
void statlb_playercolor_icon_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                  rect16_t *prcClip);
void statlb_resource_icon_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                               rect16_t *prcClip);
void statlb_text_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// 0x4A5BD0 statlb_dlg_fcn (122 B). The dialog's own event handler, registered
// by statlb_init through dlg_activate, so it is a PFN_CTRL_EVT. It returns zero
// or ctrl_dlg's result.
int statlb_dlg_fcn(ctrl_t *pDlg, dlgevt_t *pEvent);

// 0x4A5C70 statlb_deactivate (26 B). Nullary and void: its one caller,
// status_free, discards EAX.
void statlb_deactivate();

// 0x4A5E60 sub_4A5E60 (12 B). Handed to ctrl_chain, which calls it with the
// node in ECX and the event in EDX; this body reads only ECX and tail-jumps
// ctrl_hide, so it is void.
void sub_4A5E60(ctrl_t *pCtrl);

// 0x4A5E70 sub_4A5E70 (21 B). 0x4A5F00 sub_4A5F00 (21 B). One argument each;
// both are handed to ctrl_chain by the two refresh bodies and hide the node
// when its index falls in their own range.
void sub_4A5E70(ctrl_t *pCtrl);
void sub_4A5F00(ctrl_t *pCtrl);

// 0x4A5C90 statlb_kills_update (452 B). The alternate refresh: eight rows of kills and
// score read out of glbVals_g. One register argument, the dialog.
void statlb_kills_update(ctrl_t *pDlg);

// 0x4A60B0 statlb_update (321 B). The ordinary refresh: eight rows of one label
// each. One register argument, the dialog.
void statlb_update(ctrl_t *pDlg);

// 0x4A5E90 statlb_refresh (99 B). One register argument, a flag: zero takes the
// refresh path, non-zero the chain-hide path. replay.cpp calls it with 0 and 1.
void statlb_refresh(int nHide);

// 0x4A5FA0 statlb_set_title (221 B). One register argument, the dialog; it has no
// `ret` of its own and the tail jmp to ctrl_set_update pins the return void.
void statlb_set_title(ctrl_t *pDlg);

// 0x4A5F20 statlb_init (113 B). Nullary; loads rez\statlb.bin and activates it
// unless the game mode has no leaderboard.
void statlb_init();

// 0x4A6200 statlb_ctrl_init_fn (93 B). Handed to ctrl_chain, so it is called
// with the node in ECX; it reads only ECX and returns nothing of its own.
void statlb_ctrl_init_fn(ctrl_t *pCtrl);

// 0x4A6260 statlb_view_toggle (68 B). Nullary; flips the 0x6AB278 flag and refreshes.
void statlb_view_toggle();

// 0x4A62B0 statlb_timed_update (51 B). The 1-second timer's callback.
// timer_update passes the owning dialog in ECX; this body only forwards it.
void statlb_timed_update(ctrl_t *pDlg);

#endif  // OC117_STATLB_H
