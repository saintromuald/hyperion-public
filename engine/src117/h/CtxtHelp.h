// CtxtHelp.h -- declarations for src117/lang/CtxtHelp.cpp: the context-help
// bubble -- its two offscreen bitmaps and the show/hide/close cycle over them
//
// Class members belong in the class header; this file owns the free functions,
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_CTXTHELP_H
#define OC117_CTXTHELP_H

#include "platform.h"

// ctxhelp_hide is declared in minimap.h and ctxhelp_show in statdata.h, not
// here: one declaration each.
#include "bmp.h"      // bmp_t / bmpsrc_t cross the boundary in four signatures
#include "rect16.h"

// 0x419090 sub_419090 (105 B). One argument in ECX, the dialog's own bitmap;
// the EDX the dispatch site in sub_4C5630 leaves set is not an input.
void sub_419090(bmp_t *pDest);

// 0x419100 ctxhelp_update (103 B). The +0x10 update hook of image layer 1,
// installed by ctxhelp_init. It reads no register and pops 8, which is
// __stdcall; the engine spells it as img.h's PFN_IMG_UPDATE, the four
// parameters img_update_real passes.
void __fastcall ctxhelp_update(unsigned short nX, unsigned short nY, bmp_t *pBmp, bmpsrc_t *pSrc);

// 0x4191C0 ctxhelp_get_surface (6 B). Returns the address of the bubble's own
// surface object, 0x5C9994.
bmp_t *ctxhelp_get_surface();

// 0x419170 ctxhelp_close (66 B). Nullary, and no caller reads the EAX it
// inherits from bmp_destroy.
void ctxhelp_close();

// 0x419220 ctxhelp_init (116 B). Nullary; EAX at the ret is a leftover zero
// and no caller reads it.
void ctxhelp_init();

// 0x4192A0 IsCtxHelpDirty (6 B). Both callers `test eax, eax` the result.
int IsCtxHelpDirty();

// 0x4192B0 sub_4192B0 (114 B). ECX is the destination; EDX and the one stack
// dword are passed by sub_4C5630 exactly as they are to save_behind_cursor and
// restore_cursor_area beside it, and this body ignores both.
void sub_4192B0(bmp_t *pDest, int nX, int nY);

// 0x419330 sub_419330 (172 B). EDX is read 16-bit, but its one caller,
// sub_4C5630, sign-extends into it, so the parameter is int; the stack dword
// is the matching Y.
void sub_419330(bmp_t *pSrc, int nX, int nY);

// 0x4193E0 img_ctxhelp_invalid (122 B). ECX is read as four words at +0/2/4/6
// against an X,Y,X,Y pattern, which is rect16_t.
void img_ctxhelp_invalid(const rect16_t *prc);

#endif  // OC117_CTXTHELP_H
