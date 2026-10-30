// dlgFatal.h -- declarations for src117/lang/dlgFatal.cpp: the fatal-error
// dialog shown over the game before it exits. The free functions are __fastcall
// through /Gr and are declared without the keyword.

#ifndef OC117_DLGFATAL_H
#define OC117_DLGFATAL_H

#include "platform.h"
#include "dialog.h"

// 0x43EC00. Raises the dialog over the running game with up to two message
// lines, in ECX and EDX.
void do_dlgfatal(const char *pszMsg1, const char *pszMsg2);

// 0x43ED10 / 0x43EDB0 / 0x43EE70. The three draw handlers dlgfatal_activate
// installs at +0x2E, with PFN_CTRL_DRAW's shape.
void dlgfatal_button_draw_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                              rect16_t *prcClip);
void dlgfatal_draw_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void dlgfatal_msg_draw_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                           rect16_t *prcClip);

// 0x43EE90. Installs this TU's handlers and takes the cursor.
void dlgfatal_activate(ctrl_t *pDlg);

// 0x43EF40. Tears the dialog down: clears the two message controls' pData
// back-pointers, releases both message copies, and restores the cursor and the
// accelerator table.
void dlgfatal_deactivate(ctrl_t *pDlg);

// 0x43EFD0. The dialog's event handler, a PFN_CTRL_EVT that do_dlgfatal passes
// to dlg_run_modal; returns ctrl_dlg's result or 1.
int dlgfatal_interact_fcn(ctrl_t *pDlg, dlgevt_t *pEvent);

// 0x43F090. Stored at the dialog's +0x4A and reached from dlg_run_modal; a
// single jump to cursor_update, so neither argument is read.
void dlgfatal_modal_fcn(ctrl_t *pDlg, dlgevt_t *pEvent);

#endif  // OC117_DLGFATAL_H
