// rect32.h -- a 32-bit screen rectangle, the dialog invalidation layer's working
// type: the parameter of ClipRect32 (0x4C4800) and CombineRectInRgn (0x4C5490),
// and the type of sgrScreenValid_g (0x50EF44), sgrScreenSize_g (0x50EF54) and
// sgCtxRect (0x6CF788). One definition, so no two files can disagree on the
// field order.
//
// Include after <stddef.h> (the static_asserts use offsetof).
#pragma once
#include "platform.h"  // LONG and ULONG

// Win32's RECT order, left, top, right, bottom: dlg_area_invalid (0x4C4AE0)
// fills one with USER32's SetRect, and CombineRectInRgn clamps +0x00/+0x08
// against the screen's X pair and +0x04/+0x0C against its Y pair. LONG, signed:
// CombineRectInRgn clamps with signed compares, and a rectangle clipped at the
// screen's left edge can have a negative left.
struct rect32_t {
  /*0x00*/ LONG left;
  /*0x04*/ LONG top;
  /*0x08*/ LONG right;
  /*0x0C*/ LONG bottom;
};

static_assert(sizeof(rect32_t)           == 0x10, "rect32_t size    SetRect's LPRECT; sgCtxRect is stored with one 16-byte movdqu");
static_assert(offsetof(rect32_t, left)   == 0x00, "rect32_t::left     dlg_area_invalid: cmp ecx, [ebp-28h]");
static_assert(offsetof(rect32_t, top)    == 0x04, "rect32_t::top      dlg_area_invalid: cmp edx, [ebp-24h]");
static_assert(offsetof(rect32_t, right)  == 0x08, "rect32_t::right    dlg_area_invalid: cmp esi, [ebp-20h]");
static_assert(offsetof(rect32_t, bottom) == 0x0C, "rect32_t::bottom   dlg_area_invalid: cmp ebx, [ebp-1Ch]");
