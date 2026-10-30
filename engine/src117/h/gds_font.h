// gds_font.h -- declarations for src117/lang/gds_font.cpp: the bitmap fonts,
// text measuring and wrapping, and drawing text into the current bitmap.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_GDS_FONT_H
#define OC117_GDS_FONT_H

#include "platform.h"

// One register argument, a BYTE: the body stores CL.
void font_set_caret(BYTE bValue);        // 0x4CE240 (7 B)

// ECX only. The handle is cleared through the pointer before Storm frees it.
void font_free(void *pFont);         // 0x4CDD10 (22 B)

// The rest of the TU.
void *font_init_bitmapinfo();                              // 0x4CD080 (124 B)
const char *check_wrap(const char *psz);                   // 0x4CD100 (484 B)
void font_adjust_vertical(int *pnY);                       // 0x4CDBF0 (61 B)
int font_char_width(unsigned char bChar);                  // 0x4CDC30 (161 B)
int string_width(const char *pszText, int nMaxWidth);      // 0x4CDD30 (292 B)
intptr_t font_isprint(unsigned char bChar);                     // 0x4CDE90 (39 B)
void *font_load(const char *pszName, void *pfnLoadFile, const char *pszFile, int nLine); // 0x4CDEC0 (95 B)
#if !defined(_MSC_VER)
// clang and gcc: a string literal or a function as the void * (h/compiler.h, OcVoidPtr)
template <typename F> inline void *font_load(const char *pszName, F *pfnLoadFile, const char *pszFile, int nLine)
{
  return font_load(pszName, OcVoidPtr(pfnLoadFile), pszFile, nLine);
}
#endif
const char *font_puts(const char *psz, const char *pszEnd); // 0x4CD670 (855 B)
// draw_char is a hand-written routine outside this TU: ECX is the destination,
// EDX the glyph bits, and it preserves EAX. font_puts is its only caller.
void draw_char(BYTE *pDest, const BYTE *pbBits);           // 0x4CFDF0 (111 B)
int font_putc_wide(BYTE *pDest, const char *pszText); // 0x4CD310 (853 B)
const char *font_adjust_horizontal(const char *pszText, unsigned int *pnLines); // 0x4CD9F0 (505 B)
// ECX and EDX, one stack word, `ret 4h`; every caller discards EAX.
const char *font_adjust(int nX, int nY, const char *psz); // 0x4CDF20 (567 B)

// Nullary.
void font_clear_ext_char();          // 0x4CE520 (8 B)

#endif  // OC117_GDS_FONT_H
