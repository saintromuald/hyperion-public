// gluCmpgn.h -- declarations for src117/lang/gluCmpgn.cpp: the campaign select screen,
// its mission list and the per-entry FLC animation.
//
// Member functions live in their class header; this file owns the TU's free functions.
// They are spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GLUCMPGN_H
#define OC117_GLUCMPGN_H

#include "platform.h"
#include "dialog.h"
#include "dlg_flc.h"

// The two *_flc_conditional rows, 0x453FE0 and 0x458D80, sit in the screens'
// event-function tables, sCmpgnEvtFcnTbl_g @0x503D1C and sExpCmpgnEvtFcnTbl_g @0x5080D4,
// whose slot dialog.h declares `int(__fastcall *PFN_CTRL_EVT)(ctrl_t *, dlgevt_t *)`; that
// fixes their return type and both pointees. Their siblings in the tables are ctrl_flc
// (0x4C82F0) and 0x4C83B0, and the two tables are not the same permutation: +14 is 453FE0
// in one and 4C82F0 in the other.

// sCmpgnAnims_g @0x503D14 ends where sCmpgnEvtFcnTbl_g @0x503D1C begins, so a pointer walk
// over the anims whose limit lands on 0x503D1C is still inside the anims.

// The pointees below stay `int` where the derived type is `ctrl_t *` or `dlgevt_t *`:
// glues.h's glues_transition_in takes the same argument as `int`, so correcting these ten
// rows is a family-wide change reaching a shared header. The return types are not part of
// that hold.

// 0x453D90 (232 B)
BOOL glucmpgn_dlg_cmd(intptr_t nArg0);

// 0x453E80 (114 B) -- Reached only by address, so the argument list is a lower bound.
int glucmpgn_dlg_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x453F00 (181 B)
void do_cmpgn_dlg();

// 0x453FE0 (220 B)
int ctrl_cmpgn_flc_conditional(intptr_t nArg0, intptr_t nArg1);

// 0x4540E0 (70 B) -- Reached only by address, so the argument list is a lower bound.
void ctrl_cmpgn_flc_activate(intptr_t nArg0);

// 0x454130 (178 B)
void cmpgn_preinit();

// Callees of this TU that no header of ours declares; each belongs to a TU that is not
// open yet (charactr.cpp, gluError.cpp, uiSingle.cpp). ctrl_flc, flc_anim_update_fn and
// draw_flc live in src117/h/dlg_flc.h, which also carries ctrl_btnglow_flc.
//   ReadCharacterFile    returns a 32-bit value; five call sites, two of them this TU's
//                        own preinit rows.
//   do_err_okcancel_dlg  all six callers read AL only, so BYTE.
//   sub_4BB5B0           all six callers use the full EAX, so int.
int ReadCharacterFile(char *pszName, void *pOut);
BYTE do_err_okcancel_dlg(const char *pszText);
int sub_4BB5B0(int nArg0);

#endif  // OC117_GLUCMPGN_H
