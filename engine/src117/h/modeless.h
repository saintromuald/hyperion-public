// modeless.h -- declarations for src117/lang/modeless.cpp, the one in-game
// modeless dialog. The free functions are __fastcall through /Gr and are
// declared without the keyword.

#ifndef OC117_MODELESS_H
#define OC117_MODELESS_H

#include "platform.h"
#include "dialog.h"

// 0x46ECC0. ECX the dialog template, EDX its event handler, spelled void *
// because every caller passes one.
void modeless_dlg_startup(void *pTemplate, void *pfnDlg);

// Hyperion: modeless_dlg_startup's first part, the state a dialog over the game holds, which
// Hyperion's game menu enters with no dialog of 1.17's.
void modeless_dlg_enter(void);

// 0x46EDA0. Tail-jumps ctrl_dlg except on the event tags it answers 1.
BOOL modeless_opt_dlg(ctrl_t *pCtrl, dlgevt_t *pEvent);

#endif  // OC117_MODELESS_H
