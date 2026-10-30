// statwire.h -- declarations for src117/lang/statwire.cpp: the status console
// unit wireframe and its damage overlay. The TU's name is 1.17's own:
// statwire_init and statwire_deactivate push the .rdata string "statwire.cpp"
// at 0x4F01C0.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_STATWIRE_H
#define OC117_STATWIRE_H

#include "platform.h"
#include "dialog.h"

class CUnit;

// The wireframe is one GRP frame per unit type, tinted by writing six entries
// of _gbRemapColorTbl @0x501021 rather than by recolouring pixels: [0xC0..0xC1]
// shields, [0xD0..0xD3] hit points, [0xD8..0xDB] the third overlay. Those are
// offsets into one BYTE[256], not globals of their own.

// 0x4A7F10 (52 B). Puts all six remap entries back to the identity; void. It
// is also inlined into the tail of statwire_draw_dlg.
void sub_4A7F10();

// 0x4A7F50 (212 B). The hit-point tint. ECX is the unit; `ret`, no stack.
void sub_4A7F50(CUnit *pUnit);

// 0x4A8030 (143 B). The third tint run. ECX is the unit; `ret`, no stack.
void sub_4A8030(CUnit *pUnit);

// 0x4A80C0 (139 B). The shield tint. ECX is the unit; `ret`, no stack.
void sub_4A80C0(CUnit *pUnit);

// 0x4A8150 (231 B). The control's draw handler: `ret 8h` with ECX and EDX, the
// four-argument PFN_CTRL_DRAW shape, though EDX is never read.
void statwire_draw_dlg(intptr_t nArg0, int nArg1, int nArg2, int nArg3);

// 0x4A8240 (32 B). Drop the GRP. `ret`, no arguments.
void statwire_deactivate();

// 0x4A8260 (53 B). Load the GRP and the tint palette. `ret`, no arguments.
void statwire_init();

// 0x4A82A0 (55 B). Push the current selection at the control and mark it dirty.
// `ret`, no arguments; both arms tail jmp into ctrl_set_update, which is what
// makes the return void.
void statwire_update();

// 0x4A82E0 (51 B). The control's event handler, the two-argument PFN_CTRL_EVT
// shape. It returns 1 on the one event it claims and otherwise tail-jumps into
// gCtrlEvtFcnTbl[pCtrl->wType], so it returns that table's int.
BOOL statwire_evn_ctrl(void *pArg0, void *pArg1);

#endif  // OC117_STATWIRE_H
