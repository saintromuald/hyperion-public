// dlg_draw.h -- the interface of src117/lang/dlg_draw.cpp: the per-control draw
// handlers gCtrlDrawFcnTbl @0x4F1148 dispatches through, the shared text
// renderer, and the pen-based line primitive behind the dialog frames
//
// Every draw handler below is a PFN_CTRL_DRAW: four arguments, ECX + EDX + two
// stack dwords, `ret 8h`. The dispatch site sub_4C51C0 loads EDX with a clipped
// X offset; src117/h/dialog.h has the rest.
//
// Include after <stddef.h>: bmp.h's static_asserts use offsetof.
#pragma once
#include "platform.h"  // LONG and ULONG

#include "bmp.h"
#include "dialog.h"
#include "rect16.h"

// draw_bmp_effect -- oracle 0x4C3C70. The bitmap blitter the whole family
// funnels into: ECX + EDX + three stack dwords, callee-cleaned. The roles come
// from dlg_draw_in_rect, the only caller that computes all five:
//   ECX  bmp_t*     the FLC control's payload bitmap
//   EDX  int        the x offset, rect->left - rc.left
//   +8   int        the y offset, rect->top - rc.top
//   +0Ch rect16_t*  the incoming rect, read back as four signed words
//   +10h flags      the control's dwFlags
void __fastcall draw_bmp_effect(bmp_t *pBmp, int nXOfs, int nYOfs, rect16_t *prcClip, ULONG dwFlags);

// draw_ctrl_bmp_xy -- oracle 0x4C3D60. ECX is a byte: draw_standard_btn feeds
// it with 8-bit arithmetic and never widens, which only happens for a
// char-typed parameter. EDX is a short, and +8 a dword whose low half is used.
void __fastcall draw_ctrl_bmp_xy(unsigned char nFrame, short nX, int nY, rect16_t *prcClip, ULONG dwFlags);

// draw_custom_btn -- oracle 0x4C3FB0. One of the three renderers
// draw_pushbutton tail-calls; same signature as itself, which is what makes the
// tail calls legal.
void __fastcall draw_custom_btn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// draw_local_ctrl -- oracle 0x4C43E0. draw_local_btn's blitter: ECX + EDX +
// five stack dwords.
//   ECX  int        rc.left, signed
//   EDX  int        rc.top, signed
//   +8   int        the width, in 32-bit arithmetic unlike bmp_rect's 16-bit
//   +0Ch int        gwUnk28_g
//   +10h BYTE       the frame, a byte-wide parameter
//   +14h rect16_t*  the clip rect
//   +18h flags      0x2000
void __fastcall draw_local_ctrl(int nX, int nY, int nWidth, int nUnk, unsigned char bFrame, rect16_t *prcClip, ULONG dwFlags);

// edit_draw_text -- oracle 0x4C7C40. ECX = the control, nothing else.
void __fastcall edit_draw_text(ctrl_t *pCtrl);

// font_set -- oracle 0x4CE160. draw_ctrl_text calls it both with a table entry
// and with zero, the reset at the end, so the parameter is one opaque pointer;
// dlg_set_font (dlg_dlg.cpp) tail-jumps it with the same single argument. This
// header is its only declaration: dlg_dlg.cpp, dlg_lbox.cpp and replay.cpp
// include it for the name.
void __fastcall font_set(void *pFont);

// font_strlen -- oracle 0x4CE3E0 (263 B). ECX is the string, nothing else. The
// return is declared int and the evidence is split: 17 of 19 call sites use the
// full EAX and two narrow it to 16 bits right after the call, which MSVC cannot
// emit for an int return. int is the spelling dlg_edit.cpp and StatLB.cpp
// already match against; `WORD` is the first lever to try if a caller ever
// differs at its font_strlen call, and changing it means re-proving those two.
int font_strlen(const char *psz);

// font_set_color_tbl -- oracle 0x4CE250. `mov cl, al / call` -- a BYTE in ECX,
// nothing on the stack.
void __fastcall font_set_color_tbl(unsigned char bColor);

// font_adjust (oracle 0x4CDF20) and appcritf (oracle 0x4CC190) are declared in
// dlg_draw.cpp and CtxtHelp.cpp rather than here.

// The TU's own functions; each body's address and size are in its .cpp banner.
void __fastcall draw_ctrl_bmp_xy_transparent(unsigned char nFrame, short nX, int nY, rect16_t *prcClip);
void __fastcall draw_rtext(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall draw_ltext(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall draw_ctext(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall draw_rect(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall draw_dlg(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall draw_pushbutton(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall draw_frame_hot(ctrl_t *pCtrl, rect16_t *prc, int nPen);
void __fastcall draw_edit(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall LineTo(short nX, short nY);
void __fastcall draw_ctrl_text(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall draw_local_btn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall draw_standard_btn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
// draw_bar_graph -- oracle 0x4C3BE0. Fills prc's width in proportion
// dwDone / dwTotal (`imul` by the stack word, unsigned `div` by EDX). `ret 4h`.
void __fastcall draw_bar_graph(rect16_t *prc, DWORD dwTotal, DWORD dwDone);

// draw_radiobutton / draw_checkbox -- oracle 0x4C3890 / 0x4C39B0,
// gCtrlDrawFcnTbl entries (PFN_CTRL_DRAW, `ret 8h`).
void __fastcall draw_radiobutton(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall draw_checkbox(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// draw_frame_rect -- oracle 0x4C4250. ECX = the control, DL = the inner
// frame's pen; the outer frame always uses palette pen 12.
void __fastcall draw_frame_rect(ctrl_t *pCtrl, unsigned char bPen);
