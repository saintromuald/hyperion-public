// dlg_effects.h -- declarations for src117/lang/dlg_effects.cpp, the dialog
// layer's transparency and color-blend renderer.
//
// The oracle's own TU name is dlg/effects.cpp: every SMemAlloc/SMemFree in this
// slice tags itself with the .rdata string at 0x4F11F8, which is "effects.cpp".
//
// Class members belong in the class header; this file owns the free functions,
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_DLG_EFFECTS_H
#define OC117_DLG_EFFECTS_H

#include "platform.h"
#include "rect32.h"
#include "gds_vidblit.h"   // vidlock_t

// The block effects_init and effects_init_palette read. opts_init builds one on
// its frame and memsets 0x110 bytes of it, which is what fixes the size; every
// field below is a slot that caller writes or effects_init reads.
struct effects_desc_t {
  /*0x000*/ int nFlags;
  /*0x004*/ int nCycleCount;
  /*0x008*/ BYTE aCycleIndexes[256];
  /*0x108*/ int nLevelCount;
  /*0x10C*/ BYTE aPercents[4];
};
static_assert(sizeof(effects_desc_t) == 0x110, "effects_desc_t size          opts_init: memset(&desc, 0, 110h)");
static_assert(offsetof(effects_desc_t, nFlags) == 0x000, "effects_desc_t::nFlags        effects_init: test byte ptr [ebx], 4h");
static_assert(offsetof(effects_desc_t, nCycleCount) == 0x004, "effects_desc_t::nCycleCount   effects_init_palette: mov eax, [ebx+4h]");
static_assert(offsetof(effects_desc_t, aCycleIndexes) == 0x008, "effects_desc_t::aCycleIndexes effects_init_palette: lea eax, [ebx+8h] into memcpy");
static_assert(offsetof(effects_desc_t, nLevelCount) == 0x108, "effects_desc_t::nLevelCount   effects_init: cmp [ebx+108h], edi / jle");
static_assert(offsetof(effects_desc_t, aPercents) == 0x10C, "effects_desc_t::aPercents     effects_init: movzx eax, byte ptr [ebx+edi+10Ch]");

// The parameter block both SCode scripts read. effects_init memsets 0x40 bytes
// of it and then stores 0x40 into its first dword, which is the size field and
// the size. The named members are the seven slots render_transparent,
// sub_4CA8A0 and sub_4CA9B0 write; the rest stay zero from that memset.
struct scodedata_t {
  /*0x00*/ int nSize;
  /*0x04*/ int nReserved4;
  /*0x08*/ int nWidth;
  /*0x0C*/ int nHeight;
  /*0x10*/ int nSkipDest;
  /*0x14*/ int nSkipSrc;
  /*0x18*/ BYTE *pDest;
  /*0x1C*/ BYTE *pSrc;
  /*0x20*/ BYTE *pTable;
  /*0x24*/ int aReserved24[7];
};
OC_X86_LAYOUT(sizeof(scodedata_t) == 0x40, "scodedata_t size       effects_init: memset(0x6CFBE0, 0, 40h)");
static_assert(offsetof(scodedata_t, nReserved4) == 0x04, "scodedata_t::nReserved4 no body in this slice reads or writes it");
OC_X86_LAYOUT(offsetof(scodedata_t, aReserved24) == 0x24, "scodedata_t::aReserved24 the memset covers it and nothing else does");
static_assert(offsetof(scodedata_t, nSize) == 0x00, "scodedata_t::nSize     effects_init: mov [0x6CFBE0], 40h");
static_assert(offsetof(scodedata_t, nWidth) == 0x08, "scodedata_t::nWidth    render_transparent: mov [0x6CFBE8], ecx");
static_assert(offsetof(scodedata_t, nHeight) == 0x0C, "scodedata_t::nHeight   render_transparent: mov [0x6CFBEC], eax");
static_assert(offsetof(scodedata_t, nSkipDest) == 0x10, "scodedata_t::nSkipDest render_transparent: [0x6CFBF0] = arg2 - arg0");
static_assert(offsetof(scodedata_t, nSkipSrc) == 0x14, "scodedata_t::nSkipSrc  render_transparent: [0x6CFBF4] = arg3 - arg0");
static_assert(offsetof(scodedata_t, pDest) == 0x18, "scodedata_t::pDest     render_transparent: mov [0x6CFBF8], ecx");
OC_X86_LAYOUT(offsetof(scodedata_t, pSrc) == 0x1C, "scodedata_t::pSrc      render_transparent: mov [0x6CFBFC], edx");
OC_X86_LAYOUT(offsetof(scodedata_t, pTable) == 0x20, "scodedata_t::pTable    render_transparent: mov [0x6CFC00], eax");

// vidlock_t lives in src117/h/gds_vidblit.h, with vid_lock_surface and
// vid_unlock_surface, the two bodies that fill it.

// 0x4C9C70 effects_init_palette_table (1170 B). Builds one 64K blend table for
// nLevel eighths of pPalette over itself. ECX is the destination and may be
// null, in which case the body allocates one and drops it. Returns 1 always;
// both callers discard it.
int effects_init_palette_table(BYTE *pTable, const BYTE *pPalette, BYTE nLevel);

// 0x4CA110 effects_draw_surface (348 B). Blits prcSrc out of pSrcBits into the
// current destination surface under one of six render modes. pUnused is read by
// neither this body nor the switch arms; draw_bmp_effect passes the address of
// a local, so the slot exists and is dead. The two rect parameters are `int *`
// to match dlg_flc.cpp's declaration, whose call passes the address of a
// TU-local block; the definition casts them to rect32_t *.
void __fastcall effects_draw_surface(unsigned char *pSrcBits, int *prcSrc,
                                     int *pUnused, int nSrcPitch, int *prcDest,
                                     int nMode, unsigned char *pbPercent);

// 0x4CA290 effects_draw_text (234 B). Draws through font_adjust with the font
// color table swapped for one blended at nPercent. Its only caller discards
// EAX and the body's EAX at the ret is font_adjust's, so the return is void.
void __fastcall effects_draw_text(const char *psz, int nX, int nY, int nColor,
                                  int nPercent);

// 0x4CA380 effects_free (159 B). Nullary; frees both SCode handles, the three
// blend tables and the two 64K maps. Spelled as src117/h/opt_dlgs.h spells it.
void effects_free();

// 0x4CA420 effects_init (573 B). pDesc may be null, and null means "every
// level" rather than "no levels" -- it takes the same arm as flag bits 0 and 2.
// Spelled as src117/h/glues.h spells it, so the definition casts pDesc to
// effects_desc_t *; retyping the parameters would put a cast at opt_dlgs.cpp's
// call site, which passes sgGamePalette_g.
void effects_init(BYTE *pPalette, void *pDesc);

// 0x4CA660 effects_init_palette (373 B). Returns early when pPalette already
// equals the cached one, so it is cheap to call per screen.
void effects_init_palette(const DWORD *pPalette, effects_desc_t *pDesc);

// 0x4CA7E0 sub_4CA7E0 (49 B) / 0x4CA820 sub_4CA820 (81 B). Set the current
// blend level (and, for the second, the current shade) of one of the two
// independent effect slots. bAlt picks the slot.
void sub_4CA7E0(int nPercent, int bAlt);
void __fastcall sub_4CA820(int nPercent, int nShade, int bAlt);

// 0x4CA880 set_request_render (11 B) / 0x4CA890 clear_request_render (11 B).
// Both nullary and both `mov dword ptr [0x6CFBDC], imm`; every convention is
// byte-identical here, and fastcall is the TU's.
void set_request_render(void);
void clear_request_render(void);

// 0x4CA8A0 sub_4CA8A0 (176 B). The mode-3 arm of effects_draw_surface: blends
// at the level *pbPercent maps to.
void sub_4CA8A0(BYTE *pDest, BYTE *pSrc, int nWidth, int nHeight,
                int nDestPitch, int nSrcPitch, BYTE *pbPercent);

// 0x4CA950 render_transparent (82 B). The mode-1 arm: a straight SCode run over
// the 64K map at gpbEffectsTbl_g.
void render_transparent(BYTE *pDest, BYTE *pSrc, int nWidth, int nHeight,
                        int nDestPitch, int nSrcPitch);

// 0x4CA9B0 sub_4CA9B0 (462 B). The mode-4/5 arm: rebuilds one row of the shared
// blend map when the level or shade has changed, then runs the SCode.
void sub_4CA9B0(BYTE *pDest, BYTE *pSrc, int nWidth, int nHeight,
                int nDestPitch, int nSrcPitch);

#endif  // OC117_DLG_EFFECTS_H
