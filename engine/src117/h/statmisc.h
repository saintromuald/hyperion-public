// statmisc.h -- declarations for src117/lang/statmisc.cpp: the F10 menu button
// on the status console. The TU's name is 1.17's own: the .rdata string
// "statmisc.cpp" at 0x4F00E0 is the owner tag its allocation sites push.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_STATMISC_H
#define OC117_STATMISC_H

#include "platform.h"
#include "dialog.h"

// Two of these are callbacks whose address is taken once with no direct call,
// so their dispatch sites give the arity: statF10_btn_draw has the +0x2E
// PFN_CTRL_DRAW shape and statF10_dlg_fcn the +0x2A PFN_CTRL_EVT one (both in
// dialog.h). The loader finds them in sStatF10EvtFcnTbl_g @0x4F00DC.

// 0x4A62F0 (100 B). Draws the button: `ret 8h` with ECX and EDX, the
// four-argument PFN_CTRL_DRAW shape, though EDX is never read.
void statF10_btn_draw(intptr_t nArg0, int nArg1, int nArg2, intptr_t nArg3);

// 0x4A6360 (128 B). The button's hover tracker. `ret`, ECX + EDX, both read as
// 32 bits. EAX at the ret is an incidental leftover and no caller reads it.
void statF10_btn_mouse_moved(intptr_t nArg0, intptr_t nArg1);

// 0x4A63E0 (110 B). The dialog's event handler, PFN_CTRL_EVT-shaped. Every
// path puts an explicit value in EAX, so it returns one.
BOOL statF10_dlg_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x4A6480 (122 B). The dialog's hover tracker. `ret`, ECX + EDX.
void statF10_mouse_moved(intptr_t nArg0, intptr_t nArg1);

// 0x4A6500 (101 B). The button control's event handler. Its tail jump to
// btn_ctrl_pushbutton gives the return type; nothing calls it directly.
BOOL statF10_btn_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x4A6570 (26 B). Tears the F10 dialog down. The lower-case `f` is the
// ledger's spelling.
void statf10_deactivate();

// 0x4A6590 (50 B). Build the F10 dialog and activate it.
void statF10_init();

#endif  // OC117_STATMISC_H
