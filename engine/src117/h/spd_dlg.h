// spd_dlg.h -- the game-speed options dialog in src117/lang/spd_dlg.cpp
// (oracle 0x49F060-0x49F410). do_speed_dlg is declared in opt_dlgs.h.
//
// The handlers take (ctrl_t *, dlgevt_t *) in ECX/EDX, a plain two-argument
// free function under /Gr, and return a truth value in EAX.

#ifndef OC117_SPD_DLG_H
#define OC117_SPD_DLG_H

#include "platform.h"
#include "dialog.h"

// The two Storm registry imports this TU reaches, ordinals 423 and 426, which no
// other game header declares. __stdcall with four stack words; init_spd_opts
// tests the result.
extern "C" {
BOOL __stdcall SRegLoadValue(const char *pszKey, const char *pszValue,
                             DWORD dwFlags, DWORD *pdwValue);
BOOL __stdcall SRegSaveValue(const char *pszKey, const char *pszValue,
                             DWORD dwFlags, DWORD dwValue);
}

// Seeds the speed options from the registry. Nullary.
void init_spd_opts();

void speed_dlg_cmd(ctrl_t *pDlg);

BOOL speed_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// ECX is read as a byte: the flag app_add_exit passes its callbacks, which only
// this exit reads.
void exit_spd_opts(BYTE bArg0);

void speed_scrollbar_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

void speed_scrollbar_init(ctrl_t *pCtrl);

// The callback a speed scroll bar parks at +0x36; its argument is the parent
// dialog the bar parks alongside it at +0x26.
void speed_set_options(ctrl_t *pDlg);

BOOL speed_checkbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

#endif  // OC117_SPD_DLG_H
