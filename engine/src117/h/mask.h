// mask.h -- declarations for src117/lang/mask.cpp, the fog-of-war and sight
// mask over the tile grid: build it, update it per unit, and draw it.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_MASK_H
#define OC117_MASK_H

#include "platform.h"

// One cell of a sight pattern, 0x14 bytes. pPrev holds the cells nearer the
// centre that must already be lit for this one to be, nPrev of them; two is
// all the 0x14 stride leaves room for.
struct mask_ent_t {
  /*0x00*/ mask_ent_t *pPrev[2];
  /*0x08*/ int   dwTile;  // Hyperion: signed, an offset from the centre, DWORD in 1.17
  /*0x0C*/ DWORD dwSeen;
  /*0x10*/ char cX;
  /*0x11*/ char cY;
  /*0x12*/ BYTE nPrev;
};
OC_X86_LAYOUT(sizeof(mask_ent_t)             == 0x14, "mask_ent_t size");
static_assert(offsetof(mask_ent_t, pPrev)    == 0x00, "mask_ent_t::pPrev");
OC_X86_LAYOUT(offsetof(mask_ent_t, dwTile)   == 0x08, "mask_ent_t::dwTile");
OC_X86_LAYOUT(offsetof(mask_ent_t, dwSeen)   == 0x0C, "mask_ent_t::dwSeen");
OC_X86_LAYOUT(offsetof(mask_ent_t, cX)       == 0x10, "mask_ent_t::cX");
OC_X86_LAYOUT(offsetof(mask_ent_t, cY)       == 0x11, "mask_ent_t::cY");
OC_X86_LAYOUT(offsetof(mask_ent_t, nPrev)    == 0x12, "mask_ent_t::nPrev");

// One sight pattern, indexed by sight range; sgMaskTbls strides these by 0x1C.
// nDirect counts the cells revealed with no blocker test and nShaded the ones
// that need one. mask.cpp never reads nUnk08 or nUnk0C, and los.cpp's builder
// only multiplies them into nDirect.
struct mask_tbl_t {
  /*0x00*/ int nWidth;
  /*0x04*/ int nHeight;
  /*0x08*/ int nUnk08;
  /*0x0C*/ int nUnk0C;
  /*0x10*/ int nDirect;
  /*0x14*/ int nShaded;
  /*0x18*/ mask_ent_t *pEnts;
};
OC_X86_LAYOUT(sizeof(mask_tbl_t)             == 0x1C, "mask_tbl_t size");
static_assert(offsetof(mask_tbl_t, nWidth)   == 0x00, "mask_tbl_t::nWidth");
static_assert(offsetof(mask_tbl_t, nHeight)  == 0x04, "mask_tbl_t::nHeight");
static_assert(offsetof(mask_tbl_t, nUnk08)   == 0x08, "mask_tbl_t::nUnk08");
static_assert(offsetof(mask_tbl_t, nUnk0C)   == 0x0C, "mask_tbl_t::nUnk0C");
static_assert(offsetof(mask_tbl_t, nDirect)  == 0x10, "mask_tbl_t::nDirect");
static_assert(offsetof(mask_tbl_t, nShaded)  == 0x14, "mask_tbl_t::nShaded");
static_assert(offsetof(mask_tbl_t, pEnts)    == 0x18, "mask_tbl_t::pEnts");

// The four MaskFnTbl slots. The clipped pair reads ECX and EDX as the tile
// position; the unclipped pair ignores both, which under /Gr is the same call
// site, so one type serves all four.
typedef void (*PFN_MASKUPDATE)(int nTileX, int nTileY, mask_tbl_t *pTbl,
                               DWORD *pTile, DWORD dwSeen, DWORD dwAnd);

// Nullary; it ends by forcing a redraw of the map.
void mask_disable_fog();

void mask_free();
void mask_init();

// Nonzero redraws the whole 640x400 mask; zero walks gbInvalMap instead. ECX is
// the only input and every caller discards EAX.
void mask_draw(int bAll);

// One register argument: gamemap_update passes 1 at both of its calls, though
// Fog, the tail target, never reads it.
void mask_update(int bAll);

// The MaskFnTbl slots. The unclipped pair reads no register input, but they
// share the table with the clipped pair, so they are spelled alike; the two
// leading arguments are dead in the unclipped bodies.
void mask_update_air(int nTileX, int nTileY, mask_tbl_t *pTbl, DWORD *pTile,
                     DWORD dwSeen, DWORD dwAnd);
void mask_update_air_clipped(int nTileX, int nTileY, mask_tbl_t *pTbl,
                             DWORD *pTile, DWORD dwSeen, DWORD dwAnd);
void mask_update_ground(int nTileX, int nTileY, mask_tbl_t *pTbl, DWORD *pTile,
                        DWORD dwSeen, DWORD dwAnd);
void mask_update_ground_clipped(int nTileX, int nTileY, mask_tbl_t *pTbl,
                                DWORD *pTile, DWORD dwSeen, DWORD dwAnd);

void Fog();
void GenerateFogArr();
void mask_apply_light(int nLeft, int nTop, int nRight, int nBottom);
// noinline on all three: mask_apply_light and mask_draw call them in 1.17, and
// /Ob2 would inline them. The last two are inline asm in 1.17.
__declspec(noinline) void mask_block_clear(BYTE *pDst);
__declspec(noinline) void mask_block_flat(BYTE *pDst, int nShade);
__declspec(noinline) void mask_block_lerp(BYTE *pDst, int nTL, int nTR, int nBL,
                                     int nBR);
// noinline: mask_init's call to this stays a call in 1.17.
__declspec(noinline) void sub_46BFC0();
void mask_build_grid();
void sub_46C180();
void mask_invalidate();

// Hyperion: sub_46BBB0's pattern for the slots past 7, their bits in a mask of
// Hyperion's own (mask.cpp; playervision.h).
struct PlayerSet;
void MaskStampHx(int nPixelX, int nPixelY, int bAir, const PlayerSet &q, int nRange);

#endif  // OC117_MASK_H
