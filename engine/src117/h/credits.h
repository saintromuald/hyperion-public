// credits.h -- declarations for src117/lang/credits.cpp: the CREDITS and EPILOG
// screens, the scrolling credit-roll dialog, its per-line tag parser (colour,
// background and timing directives) and the two game states that drive it.
//
// The free functions are __fastcall through /Gr and are declared without the
// keyword.

#ifndef OC117_CREDITS_H
#define OC117_CREDITS_H

#include "platform.h"
#include "dialog.h"    // ctrl_t, dlgevt_t, rect16_t

// 0x4148B0. Re-arms the six text controls and shows the first page.
BOOL credits_activate(ctrl_t *pDlg);

// 0x414910. ECX is never read; EDX is the text and the stack word its length.
// The noinline is here as well as on the definition because credits.cpp
// includes this header first: without it MSVC inlines the body into
// credits_forward, where 1.17 has a call.
__declspec(noinline) void credits_change(ctrl_t *pDlg, const char *pszText, int nLen);

// 0x414990.
void credits_deactivate(ctrl_t *pDlg);

// 0x4149F0. The dialog's event function, switching on pEvent->wTag.
// do_credits_dlg takes its address, so this spelling must match the definition.
int credits_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x414A90.
void credits_edit_background(ctrl_t *pCtrl, char *pszArg);

// 0x414AE0.
void credits_edit_displaytime(char *pszArg);

// 0x414B30.
void credits_edit_fadespeed(char *pszArg);

// 0x414B80. ECX is never read.
void credits_edit_fontcolor(ctrl_t *pCtrl, char *pszArg);

// 0x414BD0.
void credits_end_page(ctrl_t *pCtrl);

// 0x414C20. A control's draw handler: only ECX is read, and EDX and the two
// stack words are ignored.
void credits_fcn_draw_text(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// 0x414C60. Shows the next page; FALSE when the file runs out.
BOOL credits_forward(ctrl_t *pDlg);

// 0x414CE0. Dispatches one tag; TRUE only for the end-of-page tag.
BOOL credits_handle_tag(ctrl_t *pDlg, char *pszTag, int nLen);

// 0x414E20.
BOOL credits_idle(ctrl_t *pDlg);

// 0x414E70.
void GameState_Credits();

// 0x414EF0.
void GameState_Epilog();

// 0x414F70.
void show_campaign_credits();

// 0x414FC0.
void show_epilog_credits();

// 0x415010.
void do_credits_dlg(const char *pszName);

// 0x4150D0. Returns the next token's length and writes its start and a tag flag
// through the two pointers (pnLen is the flag; the definition names it pbIsTag).
// noinline keeps it a call from credits_forward, as in 1.17.
__declspec(noinline) int credits_tokenize(char **ppszCursor, int *pnLen);

#endif  // OC117_CREDITS_H
