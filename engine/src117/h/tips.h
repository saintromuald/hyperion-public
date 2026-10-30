// tips.h -- declarations for src117/lang/tips.cpp, 0x4B4F50 to 0x4B5336: the
// gameplay-tips dialog, its startup checkbox, its next-tip button, and the
// string table the tip text is drawn from. The free functions are __fastcall
// through /Gr and are declared without the keyword.
//
// do_tips_dlg is declared in opt_dlgs.h; run_game calls it with 1 in ECX.

#ifndef OC117_TIPS_H
#define OC117_TIPS_H

#include "platform.h"
#include "dialog.h"

// The control's +0x2E draw handler, not a PFN_CTRL_EVT: ECX, EDX and two stack
// dwords, in the argument order dialog.h gives.
void tips_draw_textbox(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// Loads the tip string for an index and lays it into the textbox control.
void set_tip_text(ctrl_t *pCtrl, int nTipIndex);

// The startup checkbox. Reads the event tag at +0x0C, keeps its bit mask in
// the control's +0x26 slot, and toggles that bit in the options word at
// 0x6D3148, the control idiom snd_dlg.cpp repeats on the same global.
int tips_checkbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// The dialog's event handler, which do_tips_dlg hands to modeless_dlg_startup.
// A PFN_CTRL_EVT returns int, because the dispatch tests EAX.
int tips_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// Builds the dialog.
void tips_dlg_init(ctrl_t *pDlg);

// The next-tip button's event handler, the same callback shape as tips_dlg_fcn.
int tips_next_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

#endif  // OC117_TIPS_H
