// title.h -- declarations for src117/lang/title.cpp, oracle 0x4B5300..0x4B55F5:
// the title screen, the loading dialog it puts up, and the two offscreen
// bitmaps the loading bar is composited from. Free functions are __fastcall
// through /Gr and are declared without the keyword.
//
// close_title_dlg, also in this TU, is declared in glues.h.

#ifndef OC117_TITLE_H
#define OC117_TITLE_H

#include "platform.h"
#include "dialog.h"

// Builds the two offscreen bitmaps the loading bar is drawn into, sized from
// the placeholder control's rect, and starts the timer that steps it. ECX is
// the dialog: the body reads a WORD at +0x0C off it and hands it to
// ctrl_find_ndx.
void init_title_loading(ctrl_t *pDlg);

// The screen's activate handler. Loads the background, the font palette and
// the music, then tails into init_title_loading with the dialog still in ECX.
void title_activate(ctrl_t *pDlg);

// Tears the timer and both bitmaps down and tails into vid_fade_out. ECX, the
// dialog, is only passed on to ctrl_find_ndx.
void title_deactivate(ctrl_t *pDlg);

// The dialog's event handler (0x4B5500). do_title_dlg passes it to dlg_activate
// as its PFN_CTRL_EVT, which fixes the argument pair and the int return.
int title_interact(ctrl_t *pCtrl, dlgevt_t *pEvent);

// True while the title dialog is up; returns 0 or 1.
BOOL is_title_active();

// The ctrl +0x2E draw handler, not a PFN_CTRL_EVT: ECX, EDX and two stack
// words, in the order dialog.h gives. This body reads only ECX, so it does not
// show the arity.
void title_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// Loads the dialog, keeps it in the module's own slot, and activates it.
void do_title_dlg();

// The loading timer's callback. Picks one of the two bitmaps by flipping a
// toggle, copies its first two dwords into the control, and seats title_draw
// in the control's +0x2E slot before tailing into ctrl_set_update.
void title_loading_timer_fcn(ctrl_t *pCtrl);

#endif  // OC117_TITLE_H
