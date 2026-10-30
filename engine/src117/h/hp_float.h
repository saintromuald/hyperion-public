// hp_float.h -- the free functions src117/lang/hp_float.cpp defines: the floating hit-point
// bar overlay and the geometry struct the drawing helpers share.
//
// Free functions here are spelled without an explicit __fastcall; /Gr already makes it the
// default for this build.

#ifndef OC117_HP_FLOAT_H
#define OC117_HP_FLOAT_H

#include "platform.h"

class CUnit;   // only pointers to it cross this header

// hpbar_t -- one bar's geometry. rle_draw_hpfloat builds one as a local and hands its
// address to every drawing helper, so these four dwords are the whole object.
struct hpbar_t {
  // the value the bar's left edge stands for; the tick run is aligned on it modulo 3
  /*0x00*/ LONG lStart;
  // the bar's top, written by rle_draw_hpfloat and read by no helper in this TU
  /*0x04*/ LONG lTop;
  /*0x08*/ LONG lWidth;
  // the bar's height in rows, which also picks the helper: sub_4649A0 returns unless it
  // is 5, sub_464AB0 unless it is 7
  /*0x0C*/ LONG lHeight;
};

static_assert(sizeof(hpbar_t) == 0x10, "hpbar_t         rle_draw_hpfloat: the local spans [ebp-1Ch]..[ebp-0Dh]");
static_assert(offsetof(hpbar_t, lStart) == 0x00, "hpbar_t::lStart  sub_4649A0: cmp dword ptr [edi], 0h");
static_assert(offsetof(hpbar_t, lTop) == 0x04, "hpbar_t::lTop    rle_draw_hpfloat: mov eax, dword ptr [ebx+4h]");
static_assert(offsetof(hpbar_t, lWidth) == 0x08, "hpbar_t::lWidth  sub_4649A0: mov edx, dword ptr [edi+8h]");
static_assert(offsetof(hpbar_t, lHeight) == 0x0C, "hpbar_t::lHeight sub_4649A0: cmp dword ptr [edi+0Ch], 5h");

// Scale a value to a percentage, then to a pixel run rounded to a multiple of three and
// never below three. ECX + EDX + 2 stack words, `ret 8h`; the percentage is the return.
int sub_4648A0(int nMax, int nCur, int nWidth, int *pnLen);

// sub_4648A0 without the one-pixel width bias.
int sub_464920(int nMax, int nCur, int nWidth, int *pnLen);

// Draw a five-row and a seven-row bar. ECX + EDX + 3 stack words, `ret 0Ch`,
// and EAX is written on no path to a ret.
void sub_4649A0(int nX, int nY, hpbar_t *pBar, int nValue, const BYTE *pbColors);
void sub_464AB0(int nX, int nY, hpbar_t *pBar, int nValue, const BYTE *pbColors);

// The four-row bar. ECX + EDX + 2 stack words, `ret 8h`.
void sub_464BC0(int nX, int nY, hpbar_t *pBar, int nValue);

// The overlay's entry point. ECX + EDX + 3 stack words, `ret 0Ch`; the first
// stack word is never read, so only its 4-byte footprint is proven.
void rle_draw_hpfloat(int nX, int nY, int nUnused, hpbar_t *pBar, CUnit *pUnit);

// Load the bar's colour bytes. Spelled as src117/lang/status.cpp declares it; a different
// spelling would be a second mangled name.
void __fastcall hp_float_init();

#endif  // OC117_HP_FLOAT_H
