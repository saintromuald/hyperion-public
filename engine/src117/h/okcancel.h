// okcancel.h -- declarations for src117/lang/okcancel.cpp: the shared OK and
// OK/Cancel message boxes, and the wait dialog they are built from
//
// Free functions are spelled without an explicit __fastcall, which /Gr already
// makes the default. modal_ok_dlg and modal_okcancel_dlg are bodies of this TU
// but stay declared in src117/h/save_dlg.h.

#ifndef OC117_OKCANCEL_H
#define OC117_OKCANCEL_H

#include "platform.h"

#include "dialog.h"   // ctrl_t, rect16_t, PFN_CTRL_DRAW

// 0x47ACB0 okcancel_draw_fcn (27 B). A PFN_CTRL_DRAW, not a PFN_CTRL_EVT. It
// forwards to draw_ctrl_text with zero for both offsets and the clip, never
// reading the three it was passed.
void okcancel_draw_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// The box's completion callback. okcancel_dlg_fcn calls it with gnDlgReturnVal
// in ECX and nothing else set. modal_ok_dlg and modal_okcancel_dlg take it as
// the `void *` save_dlg.h spells and cast it on the way in.
typedef void(__fastcall *PFN_COMPLETE)(LONG nResult);

// 0x47AE20 modal_ok_dlg_aux (131 B). The one stack word is the dialog template
// it hands modeless_dlg_startup; ECX is copied as a C string, and EDX is banked
// in sgpCompleteFcn, not called here.
void modal_ok_dlg_aux(const char *pszText, PFN_COMPLETE pfnComplete,
                      const char *pszDlgFile);

// 0x47AEC0 okcancel_activate (148 B). ECX is the dialog.
void okcancel_activate(ctrl_t *pDlg);

// 0x47AF60 okcancel_deactivate (64 B). Tail-jumps to modeless_menu_deactivate
// with the same argument pair and return type.
BOOL okcancel_deactivate(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x47AFA0 okcancel_dlg_fcn (112 B). The dialog procedure modal_ok_dlg_aux
// installs: a PFN_CTRL_EVT, with two tail jmps to modeless_opt_dlg.
BOOL okcancel_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x47ACD0 modeless_ok_dlg (151 B) and 0x47AD70 modeless_okcancel_dlg (152 B).
// The same shape over two different .bin templates. ECX is the message text,
// EDX the small-bitmap flag, one stack word the tile group. The two-button box
// returns dlg_run_modal's result, which its caller compares against -2; the
// one-button box's result is never read.
void modeless_ok_dlg(const char *pszText, int nSmallBmp, const char *pszTileGrp);
int modeless_okcancel_dlg(const char *pszText, int nSmallBmp,
                          const char *pszTileGrp);

// 0x47B030 do_wait_dlg (89 B). ECX is the message; the body banks it and puts
// the wait template up.
void do_wait_dlg(const char *pszText);

// 0x47B090 sub_47B090 (15 B). Clears the dialog nesting level.
void sub_47B090();

// 0x47B0A0 wait_dlg_fcn (145 B). The wait dialog's procedure; its return type
// is dialog.h's PFN_CTRL_EVT, since the body's EAX does not say.
BOOL wait_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

#endif  // OC117_OKCANCEL_H
