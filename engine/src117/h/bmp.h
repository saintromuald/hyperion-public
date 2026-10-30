// bmp.h -- the bitmap descriptor, bmp_copy_rect's source block and the drawing
// primitives on the current bitmap. Several TUs reach this layer, and
// gen/hook_table.cpp includes every header in one compile, so the types have
// this one home.
//
// Include after <stddef.h>: the static_asserts use offsetof.
#pragma once

// bmpsrc_t embeds a rect16_t, and the generated TUs do not control header
// order, so this include is explicit.
#include "platform.h"  // OC_X86_LAYOUT
#include "rect16.h"

// The bitmap descriptor: a signed width and height, then the pixels. A ctrl_t
// embeds one at +0x0C, reached through ctrl_bmp() in dlg_draw.cpp.
struct bmp_t {
  /*0x00*/ short nWidth;
  /*0x02*/ short nHeight;
  /*0x04*/ unsigned char *pPixels;
};
OC_X86_LAYOUT(sizeof(bmp_t) == 0x08, "bmp_t size      bmp_clear: two shorts + the +4 pixel base");
static_assert(offsetof(bmp_t, nWidth) == 0x00, "bmp_t::nWidth   bmp_clear: movsx eax, word ptr [esi]");
static_assert(offsetof(bmp_t, nHeight) == 0x02, "bmp_t::nHeight  bmp_clear: movsx edx, word ptr [esi+2h]");
OC_X86_LAYOUT(offsetof(bmp_t, pPixels) == 0x04, "bmp_t::pPixels  bmp_clear: push dword ptr [esi+4h]");

// bmpsrc_t -- bmp_copy_rect's 12-byte source block: the rectangle, then a width
// and height, named for the bitmap's own dimensions bmp_copy_rect overwrites
// them with.
struct bmpsrc_t {
  /*0x00*/ rect16_t rc;
  /*0x08*/ short nWidth;
  /*0x0A*/ short nHeight;
};
static_assert(sizeof(bmpsrc_t) == 0x0C, "bmpsrc_t size      bmp_copy_rect copies 8+4 bytes out of it");
static_assert(offsetof(bmpsrc_t, rc)      == 0x00, "bmpsrc_t::rc       movq xmm0, qword ptr [eax]");
static_assert(offsetof(bmpsrc_t, nWidth)  == 0x08, "bmpsrc_t::nWidth   [ebp-8h] = base+8");
static_assert(offsetof(bmpsrc_t, nHeight) == 0x0A, "bmpsrc_t::nHeight  [ebp-6h] = base+0Ah");

// bmp_copy_rect -- oracle 0x4CE720. ECX and EDX are 16-bit; the caller loads
// them with movzx, so they are unsigned.
void __fastcall bmp_copy_rect(unsigned short nX, unsigned short nY,
                              bmp_t *pBmp, bmpsrc_t *pSrc);

// The drawing primitives, declared here so the TUs that call them agree.

// bmp_clear -- oracle 0x4CE6F0. One BYTE in ECX; it fills _bmp_current whole.
void __fastcall bmp_clear(unsigned char bColour);

// bmp_destroy -- oracle 0x4CB230. ECX is the descriptor.
void __fastcall bmp_destroy(bmp_t *pBmp);

// bmp_hline / bmp_vline -- oracle 0x4CCB30 / 0x4CCB70. ECX is x, EDX y, the one
// stack slot the length, all three sixteen bits.
void __fastcall bmp_hline(unsigned short nX, unsigned short nY, unsigned short nLen);
void __fastcall bmp_vline(unsigned short nX, unsigned short nY, unsigned short nLen);

// bmp_rect / bmp_rect_fill -- oracle 0x4CE7D0 / 0x4CE9C0. (x, y, w, h) in
// ECX, EDX and two stack dwords, every one an unsigned short: the callers do the
// width and height arithmetic in 16 bits and widen it unsigned.
void __fastcall bmp_rect(unsigned short nX, unsigned short nY,
                         unsigned short nWidth, unsigned short nHeight);
void __fastcall bmp_rect_fill(unsigned short nX, unsigned short nY,
                              unsigned short nWidth, unsigned short nHeight);
