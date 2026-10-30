// statport.h -- declarations for src117/lang/statport.cpp: the status console
// unit portrait, its talking-head video, its dialog and its controls. The TU's
// name is 1.17's own: the .rdata string "statport.cpp" at 0x4F0108 is the
// owner tag its allocation sites push.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_STATPORT_H
#define OC117_STATPORT_H

#include "platform.h"

class CUnit;

// Four of this TU's functions are declared in other headers and not here:
// get_unit_portrait (0x4A65D0) in gluRdy.h, statport_talking_portrait
// (0x4A7200) in gamesnd.h, show_portrait (0x4A7320) in TrigAct.h and
// statport_trigger_reset (0x4A73E0) in victory.h.

// Four widths below can move a byte: sub_4A6610 reads ECX as 8 bits,
// sub_4A6B40 and sub_4A6C50 read it as 16, and sub_4A6E00 returns a BYTE
// because both consuming callers use only AL.

// 0x4A6610 (239 B). ECX is read as 8 bits and EDX is dereferenced.
void sub_4A6610(BYTE bArg0, void *pArg1);

// 0x4A6700 (94 B). `ret 4h`. Its one consuming caller uses the full EAX and
// does not narrow it.
int sub_4A6700(intptr_t nArg0, int nArg1, intptr_t nArg2);

// 0x4A6760 (57 B). The dialog's character handler. ECX is never read, so the
// argument it carries is ignored; EDX is dereferenced, which is the event.
BOOL statport_dlg_char(intptr_t nArg0, void *pArg1);

// 0x4A67A0 (311 B). Activate the portrait dialog. One register argument.
void statport_dlg_activate(intptr_t nArg0);

// 0x4A68E0 (111 B). The dialog's event handler, PFN_CTRL_EVT-shaped.
BOOL statport_dlg_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x4A6970 (73 B). The portrait control's draw handler: `ret 8h` plus ECX and
// EDX, the four-argument PFN_CTRL_DRAW shape. Neither stack slot is read here
// and EDX is not either, which says nothing about whether they exist.
void statport_portrait_ctrl_draw(intptr_t nArg0, int nArg1, int nArg2, int nArg3);

// 0x4A69C0 (8 B). Set the panel's dirty flag.
void request_statport_update();

// 0x4A69D0 (82 B). Tear the portrait down. EAX at the ret is mem_free's
// return, an incidental leftover no caller reads.
void statport_deactivate();

// 0x4A6A30 (6 B). Read the portrait playback setting. Three consuming callers
// use the full EAX and none narrows it.
int get_playback_option();

// 0x4A6A40 (255 B). Build the portrait dialog.
void statport_init();

// 0x4A6B40 (257 B). ECX is read as 16 bits, so its argument is a WORD.
void sub_4A6B40(WORD wArg0);

// 0x4A6C50 (219 B). ECX is read as 16 bits, so its argument is a WORD.
void sub_4A6C50(WORD wArg0);

// 0x4A6D30 (198 B). The portrait control's event handler, PFN_CTRL_EVT-shaped.
BOOL statport_portrait_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x4A6E00 (92 B). `ret 4h`. Both consuming callers use only AL.
BYTE sub_4A6E00(int nArg0, int nArg1, int nArg2);

// 0x4A6E60 (39 B). One register argument, read as 32 bits.
void statport_portrait_hide(intptr_t nArg0);

// 0x4A6E90 (125 B). Write the portrait playback setting, the sibling of
// get_playback_option. EAX at the ret is statport_portrait_show's return.
void set_playback_option(int nArg0);

// 0x4A6F10 (284 B). `ret 4h`. EDX carries a WORD, not an int: an int costs 19
// bytes over six bodies in this TU, 5 of them here.
void statport_portrait_show(CUnit *pUnit, WORD wPortrait, int nMode);

// 0x4A7030 (344 B). The largest body in this TU.
void statport_portrait_timer(intptr_t nArg0, int nArg1);

// 0x4A7190 (110 B). EDX is read as 16 bits, so its argument is a WORD.
void statport_talk_timer(intptr_t nArg0, WORD wArg1);

// 0x4A7300 (29 B). Both ECX and EDX are only forwarded to a callee and never
// read here, so neither type is in these bytes. Tail call into
// timer_remove_node.
void statport_trigger_done(intptr_t nArg0, int nArg1);

// 0x4A7420 (145 B). The per-frame portrait update.
void statport_update();

#endif  // OC117_STATPORT_H
