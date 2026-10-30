// statres.h -- declarations for src117/lang/statres.cpp: the status console
// resource readout of minerals, gas, supply and the countdown timer. The TU's
// name is 1.17's own: the .rdata string "statres.cpp" at 0x4F015C is the owner
// tag its allocation sites push.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_STATRES_H
#define OC117_STATRES_H

#include "platform.h"

// SetCountdownText (0x4A7960) belongs to this TU but is declared in victory.h,
// whose note also gives sgSlotStrs @0x6AB400 its 32-byte stride.

// statres_draw_supplies and statres_draw_resources read EDX as 16 bits, not 32.

// 0x4A74C0 (198 B). The resource control's draw handler: `ret 8h` with ECX and
// EDX, the four-argument PFN_CTRL_DRAW shape, though neither EDX nor the stack
// slots are read.
// Hyperion: the control pointer-wide, `int` before, which at 64 bits cut a
// control above 4 GB to its low half.
void statres_resource_ctrl_draw(intptr_t nArg0, int nArg1, int nArg2, int nArg3);

// 0x4A7590 (174 B). Draws one resource bar. `ret 4h`, the one stack slot read
// three times. EDX is 16 bits because the one caller needs it: as an int,
// statres_resource_ctrl_draw builds the argument with movsx and 32-bit adds
// where 1.17 uses movzx and `add dx`.
void statres_draw_bar(intptr_t nArg0, unsigned short wArg1, int nArg2);

// 0x4A7640 (149 B). Draw the supply readout. `ret 8h`, and EDX is read as 16
// bits, so its argument is a WORD.
void statres_draw_supplies(intptr_t nArg0, WORD wArg1, int nArg2, int nArg3);

// 0x4A76E0 (175 B). Draws the mineral and gas readouts. `ret Ch`, EDX again 16
// bits, and the first stack slot is dereferenced, so it is a pointer to
// something not yet named.
void statres_draw_resources(intptr_t nArg0, WORD wArg1, void *pArg2, int nArg3,
                            int nArg4);

// 0x4A7790 (59 B). The resource control's event handler. Both ECX and EDX are
// dereferenced, which is the PFN_CTRL_EVT (control, event) pair. It has no
// `ret` of its own: the tail jmp to ctrl_text pins the return type.
BOOL statres_resource_ctrl_fcn(void *pArg0, void *pArg1);

// 0x4A77D0 (86 B). The dialog's event handler. Every path puts an explicit
// zero in EAX, so it returns a value.
BOOL statres_evn_dlg(intptr_t nArg0, intptr_t nArg1);

// 0x4A7850 (81 B). Activate the resource dialog. One register argument.
void statres_dlg_activate(intptr_t nArg0);

// 0x4A78B0 (63 B). Tear the resource dialog down.
void statres_deactivate();

// 0x4A78F0 (97 B). Build the resource dialog. EAX at the ret is dlg_activate's
// return, an incidental leftover no caller reads.
void statres_init();

// 0x4A79A0 (23 B). Hide the countdown control. Tail jmps into ctrl_hide.
void HideCountdownTimer();

// 0x4A79C0 (413 B). Step the mineral and gas icon animations. ECX is only ever
// forwarded to a callee and never read here, so its type is not in these bytes.
void statres_animate(intptr_t nArg0);

#endif  // OC117_STATRES_H
