// gluExpCmpgn.h -- declarations for src117/lang/gluExpCmpgn.cpp, the expansion
// campaign select screen, gluCmpgn.cpp's twin shape for shape
//
// The handlers below fill sExpCmpgnEvtFcnTbl_g @0x5080D4, and the twin's fill
// sCmpgnEvtFcnTbl_g @0x503D1C, in a different order; dialog.h declares that
// slot `int(__fastcall *)(ctrl_t *, dlgevt_t *)`, which is where the int
// returns come from. The BOOL on the two *_dlg_cmd rows is the `movzx eax, al`
// widening of glues_transition_out, a BYTE in gluRdy.h. sCmpgnAnims_g
// @0x503D14 ends where sCmpgnEvtFcnTbl_g begins, so a walk over the anims that
// stops at 0x503D1C only looks like a read of the table.
//
// Free functions are spelled without an explicit __fastcall, /Gr's default.

#ifndef OC117_GLUEXPCMPGN_H
#define OC117_GLUEXPCMPGN_H

#include "platform.h"
#include "dialog.h"
// gluCmpgn.h, which declares the six external callees both twins reach.
#include "gluCmpgn.h"

// The pointees on the rows below stay `int` where the derived type is ctrl_t *
// and dlgevt_t *: glues_transition_in in glues.h carries the same
// under-derivation on its first parameter, so correcting these ten means
// correcting that shared header and its other callers in the same change.

// 0x458B00 do_xcampaign_dlg (181 B). Nullary; it tail-jumps glues_clear_all.
void do_xcampaign_dlg();

// 0x458BE0 gluExpCmpgn_dlg_cmd (232 B). One register argument; the BOOL is the
// widened glues_transition_out the file header describes.
BOOL gluExpCmpgn_dlg_cmd(intptr_t nArg0);

// 0x458CD0 gluexpcmpgn_dlg_fcn (114 B). The screen's dialog procedure,
// address-taken once: two register arguments, and the tail jmp to ctrl_dlg
// gives the int its one caller reads whole.
int gluexpcmpgn_dlg_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x458D50 ctrl_cmpgn_flc_draw (45 B). A PFN_CTRL_DRAW: ECX, EDX and two stack
// words, forwarded to draw_flc, which is the only reader of EDX.
void ctrl_cmpgn_flc_draw(intptr_t nArg0, int nArg1, int nArg2, intptr_t nArg3);

// 0x458D80 ctrl_xcampaign_flc_conditional (220 B). A table row, so two register
// arguments and an int return; one path calls ctrl_flc.
int ctrl_xcampaign_flc_conditional(intptr_t nArg0, intptr_t nArg1);

// 0x458E80 ctrl_xcmpgn_flc_activate (68 B). One register argument, and no
// caller reads the EAX it inherits.
void ctrl_xcmpgn_flc_activate(intptr_t nArg0);

// 0x458ED0 ctrl_cmpgn_flc_animate (33 B). One register argument; it forwards to
// flc_anim_update_fn.
void ctrl_cmpgn_flc_animate(intptr_t nArg0);

// 0x458F00 xcampaign_preinit (178 B). Nullary. The body touches x87 and SSE, so
// a floating-point return would not show in the EAX evidence.
void xcampaign_preinit();

#endif  // OC117_GLUEXPCMPGN_H
