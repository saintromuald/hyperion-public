// gluBNOkCancel.h -- declarations for src117/lang/gluBNOkCancel.cpp: the
// battle.net OK / OK-Cancel message box, a real Win32 dialog over the glue.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default. Windows handles are spelled void *, as
// gluBN.h and gluBNProfile.h spell them, so this header reaches no system
// header.

#ifndef OC117_GLUBNOKCANCEL_H
#define OC117_GLUBNOKCANCEL_H

#include "platform.h"

// 0x44E110 (347 B). Storm's message-box callback, installed by gluBN.cpp.
// `ret 10h`, all callers discard EAX. The third argument is the caption: it
// becomes sgTitle and MessageBoxA's caption.
int __stdcall UiMessageBoxCallback(void *hDlg, const char *pszText,
                                   const char *pszCaption, int nFlags);

// 0x44E270 (191 B). Measures the text with DrawTextA's DT_CALCRECT against the
// window's client rectangle and returns whether it still fits. ECX is the
// window and EDX the text; both callers in OkCancelInit take the full EAX.
int IsTextTooLarge(void *hWnd, const char *pszText);

// 0x44E330 (69 B). Releases the two loaded bitmaps. The body reads no register,
// but its one caller sets ECX, so the dialog handle is a parameter it ignores.
void OkCancelDestroy(void *hDlg);

// 0x44E380 (332 B). The box's dialog procedure, a DLGPROC reached only by
// address, spelled as gluBNProfile.h spells SDlgDefDialogProc.
intptr_t __stdcall OkCancelDialogProc(void *hDlg, unsigned int nMsg,
                                 uintptr_t wParam, LONG_PTR lParam);

// 0x44E4D0 (161 B). Builds one of the three fonts in sgOkFonts_g from the
// window's current font. ECX is the window, EDX the font index; returns
// CreateFontIndirectA's handle.
void *OkCancelFontCreate(void *hWnd, int nIndex);

// 0x44E580 (343 B). WM_INITDIALOG: picks the artwork, loads it, sets the button
// bitmaps and fills in the text. ECX is the dialog and EDX the message text;
// the one caller branches on the full EAX, which is 0 or 1.
int OkCancelInit(void *hDlg, const char *pszText);

#endif  // OC117_GLUBNOKCANCEL_H
