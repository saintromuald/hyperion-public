// mask.cpp -- the fog-of-war and sight mask over the tile grid. Bodies are in
// oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/mask.h"
#include "../h/gamemap.h"      // gamemap_force_redraw
#include "../h/los.h"          // sub_466E40, los_free
#include "../h/CUnitCombat.h"  // sub_46BBB0
#include "../h/TrigEnum.h"     // get_map_height
#include "../h/dlg_dlgdraw.h"  // img_area_invalid
#include "../h/Storm.h"        // SMemAlloc, SMemFree (pulls <windows.h>)
#include <string.h>            // memset, memcpy

// mask_init's memsets are CRT calls in 1.17, which /Oi would expand inline.
// memcpy stays intrinsic, as mask_build_grid's copy is in 1.17.
#pragma function(memset)
#include "../gen/globals117.h"
#include "../port/render/port_scene.h"  // Hyperion: PortSceneFog
#include "../port/render/port_view.h"  // Hyperion: g_portView, PORT_VIEW_RING_TILES_X_MAX, PORT_VIEW_RING_TILES_Y_MAX
#include "../h/img.h"  // Hyperion: _img_list
#include "../h/maplimits.h"  // Hyperion: maps past 256 tiles
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include <vector>

// The globals this TU reaches, dual-spelled, one #ifdef per name.
// gbMultiPlayerMode, glCheatBits, gwMapWidth, gwMapHeight, gpSquareMap,
// gdwLocalMask and byte_6D0BF5 come from ../gen/globals117.h.

// 0x4EEDA0 MaskFnTbl, in .rdata: the ground and air updates, then the clipped
// pair in the same order. sub_46BBB0 picks a slot by adding 1 for air and 2 for
// clipped.
#ifndef OC_MASK_FN_TBL
#define OC_MASK_FN_TBL
#ifdef OC_DLL_MODE
#define MaskFnTbl (*reinterpret_cast<PFN_MASKUPDATE (*)[4]>(OC_VA(0x4EEDA0u)))
#else
extern PFN_MASKUPDATE MaskFnTbl[4] OC_UNALIGNED;
#endif
#endif

// 0x508A50 sgMaskTbls -- the 12 sight patterns, indexed by sight range.
#ifndef OC_SG_MASK_TBLS
#define OC_SG_MASK_TBLS
#ifdef OC_DLL_MODE
#define sgMaskTbls (*reinterpret_cast<mask_tbl_t (*)[12]>(OC_VA(0x508A50u)))
#else
extern mask_tbl_t sgMaskTbls[12] OC_UNALIGNED;
#endif
#endif

// 0x5264B0 and 0x5264B2 -- the screen's top-left tile, two WORDs, under names
// coined here.
#ifndef OC_GW_SCREEN_TILE_X
#define OC_GW_SCREEN_TILE_X
#ifdef OC_DLL_MODE
#define gwScreenTileX (*reinterpret_cast<WORD *>(OC_VA(0x5264B0u)))
#else
extern WORD gwScreenTileX;
#endif
#endif

#ifndef OC_GW_SCREEN_TILE_Y
#define OC_GW_SCREEN_TILE_Y
#ifdef OC_DLL_MODE
#define gwScreenTileY (*reinterpret_cast<WORD *>(OC_VA(0x5264B2u)))
#else
extern WORD gwScreenTileY;
#endif
#endif

// 0x670A70 sgpCurrMaskMap, 0x670A74 and 0x670A78 -- the three mask grids, 24
// columns by 17 rows in 1.17. mask_build_grid copies the first into the second.
#ifndef OC_SGP_CURR_MASK_MAP
#define OC_SGP_CURR_MASK_MAP
#ifdef OC_DLL_MODE
#define sgpCurrMaskMap (*reinterpret_cast<BYTE **>(OC_VA(0x670A70u)))
#else
extern BYTE *sgpCurrMaskMap;
#endif
#endif

#ifndef OC_DWORD_670A74
#define OC_DWORD_670A74
#ifdef OC_DLL_MODE
#define dword_670A74 (*reinterpret_cast<BYTE **>(OC_VA(0x670A74u)))
#else
extern BYTE *dword_670A74;
#endif
#endif

#ifndef OC_DWORD_670A78
#define OC_DWORD_670A78
#ifdef OC_DLL_MODE
#define dword_670A78 (*reinterpret_cast<BYTE **>(OC_VA(0x670A78u)))
#else
extern BYTE *dword_670A78;
#endif
#endif

// 0x670A7C sgpCurrLightMap -- the light map, 0x58 bytes by 60 rows in 1.17.
#ifndef OC_SGP_CURR_LIGHT_MAP
#define OC_SGP_CURR_LIGHT_MAP
#ifdef OC_DLL_MODE
#define sgpCurrLightMap (*reinterpret_cast<BYTE **>(OC_VA(0x670A7Cu)))
#else
extern BYTE *sgpCurrLightMap;
#endif
#endif

// 0x670A80 -- a 32 by 32 table of four-step ramps, one dword per pair of shades,
// which sub_46BFC0 fills and Fog reads. Two dimensions rather than 0x400 entries
// keep Fog's index as 1.17 computes it.
#ifndef OC_DWORD_670A80
#define OC_DWORD_670A80
#ifdef OC_DLL_MODE
#define dword_670A80 (*reinterpret_cast<DWORD (*)[32][32]>(OC_VA(0x670A80u)))
#else
extern DWORD dword_670A80[32][32];
#endif
#endif

// 0x671A80 sgbBitMask, a BYTE. Only mask_init and GenerateFogArr reach it, so
// the plain arm is static, and GenerateFogArr re-reads it per column as 1.17
// does.
#ifndef OC_SGB_BIT_MASK
#define OC_SGB_BIT_MASK
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgbBitMask (*reinterpret_cast<BYTE *>(OC_VA(0x671A80u)))
#else
static BYTE sgbBitMask;
#endif
#endif

// 0x67266C gdwLocalBlackMask, a DWORD of net_data.cpp's. glScrollPixelX/Y,
// shiftDark_g, gdwReplayVisionMask, sgbShowAll_g and gPlayback come from
// ../gen/globals117.h.
#ifndef OC_GDW_LOCAL_BLACK_MASK
#define OC_GDW_LOCAL_BLACK_MASK
#ifdef OC_DLL_MODE
#define gdwLocalBlackMask (*reinterpret_cast<DWORD *>(OC_VA(0x67266Cu)))
#else
extern DWORD gdwLocalBlackMask;
#endif
#endif

// 0x6D06D0 gbInvalMap, image.cpp's: one byte per 16 by 16 screen cell, which
// mask_draw walks 40 wide by 25 tall over a 640 by 400 screen.
// Hyperion: the grid is the view's, g_portView.pbInvalMap (port_view.cpp).

// Hyperion: the fog's grids follow the view's scroll ring (port_view.cpp).
// The mask grid has a megatile a side more than the ring and one more at the far
// edges, 24 by 17 in 1.17; the light map has four 8-pixel blocks a megatile, one
// megatile wider and taller than the ring, 0x58 bytes by 60 rows in 1.17.
static inline int MaskGridWidth() { return g_portView.nRingTilesX + 3; }
static inline int MaskGridHeight() { return g_portView.nRingTilesY + 3; }
static inline int MaskLightPitch() { return (g_portView.nRingTilesX + 1) * 4; }
static inline int MaskLightBytes() { return MaskLightPitch() * (g_portView.nRingTilesY + 1) * 4; }
// The grids are sized when a map loads (mask_init): for the widest view, or for the whole map
// and a margin past it, which a view zoomed out over the map can reach.
enum { MASK_MAP_SPARE = 40 };  // megatiles past the map's width or height, 16 a side and some
static int g_nMaskGridMax;
static int g_nMaskLightMax;

// 0x6D0B8C -- the pointer to the mask overlay's pixels, 640 bytes to the row in
// 1.17.
#ifndef OC_DWORD_6D0B8C
#define OC_DWORD_6D0B8C
// It is _img_draw_buf.pPixels, bmp_t's +4, and is spelled as the field, since
// bmp_t's layout changes at 64 bits.
#define dword_6D0B8C (_img_draw_buf.pPixels)
#endif

// The bodies, in oracle VA order.

// Resamples the 24 by 17 mask grid up into the 0x58-stride light map, four
// light cells per mask cell in each axis, interpolating through dword_670A80.
// Hyperion: the grids' sizes are the view's (MaskGridWidth and the rest);
// 1.17's 0x0E and 0x15 were the ring's megatiles, 0x18 the grid's row, 0x58
// the light map's, and 0x15C and 0x10C the steps back up and on to the next band.
void Fog()
{
  const int nGrid = MaskGridWidth();
  const int nLight = MaskLightPitch();
  DWORD *pOut = reinterpret_cast<DWORD *>(sgpCurrLightMap);
  const BYTE *pSrc = sgpCurrMaskMap + nGrid + 1;
  int nRow, nCol, k;

  nRow = g_portView.nRingTilesY;
  while (nRow != 0) {
    --nRow;
    nCol = g_portView.nRingTilesX;
    while (nCol != 0) {
      int b, a, da, db;
      --nCol;
      a = pSrc[0] << 8;
      b = pSrc[1] << 8;
      da = ((pSrc[nGrid] << 8) - a) / 4;
      db = ((pSrc[nGrid + 1] << 8) - b) / 4;

      for (k = 4; k != 0; --k) {
        *pOut = dword_670A80[a >> 8][b >> 8];
        pOut = reinterpret_cast<DWORD *>(reinterpret_cast<BYTE *>(pOut) + nLight);
        a += da;
        b += db;
      }
      pOut = reinterpret_cast<DWORD *>(reinterpret_cast<BYTE *>(pOut) - (4 * nLight - 4));
      ++pSrc;
    }
    pSrc += nGrid - g_portView.nRingTilesX;
    pOut = reinterpret_cast<DWORD *>(reinterpret_cast<BYTE *>(pOut) +
                                     (4 * nLight - 4 * g_portView.nRingTilesX));
  }
}


// Rebuilds the 24 by 17 mask grid from the fog dwords under the screen, then
// smooths it into the next grid. The tile cursor stops at the last column and
// row, which fills the one-cell overhang; the row start is recomputed, not
// copied from nY0, as in 1.17.
// Hyperion: the grid is the view's (MaskGridWidth, MaskGridHeight), 0x18
// by 0x11 in 1.17, and the smoothing covers all of it but its border.
void GenerateFogArr()
{
  const int nGrid = MaskGridWidth();
  int nX0 = static_cast<int>(gwScreenTileX) - 1;
  if (nX0 < 0)
    nX0 = 0;
  int nY0 = static_cast<int>(gwScreenTileY) - 1;
  if (nY0 < 0)
    nY0 = 0;

  DWORD *pRow = &gpSquareMap[gwMapWidth * nY0 + nX0];
  BYTE *pOutRow = dword_670A78;
  BYTE nHalf = static_cast<BYTE>(sgbBitMask >> 1);
  int nY = static_cast<int>(gwScreenTileY) - 1;
  int nRow;

  nRow = MaskGridHeight();
  while (nRow != 0) {
    DWORD *pTile = pRow;
    BYTE *pOut = pOutRow;
    DWORD dwX;
    int nCol;

    --nRow;
    dwX = static_cast<DWORD>(gwScreenTileX) - 1;
    nCol = nGrid;
    while (nCol != 0) {
      DWORD dwSeen;
      --nCol;
      if (gPlayback != 0) {
        if (sgbShowAll_g != 0) {
          *pOut = sgbBitMask;
        } else {
          DWORD dwMask = gdwReplayVisionMask;
          DWORD dwTile = ~*pTile;
          const int nTile = static_cast<int>(pTile - gpSquareMap);  // Hyperion: the viewer's slots past 7 (stage D)
          if ((dwTile & ((dwMask & 0xFF) << 8)) == 0 && !ViewHxTileExplored(nTile))
            *pOut = 0;
          else
            *pOut = ((dwMask & dwTile & 0xFF) == 0 && !ViewHxTileSeen(nTile)) ? nHalf : sgbBitMask;
        }
      } else {
        dwSeen = *pTile;
        if ((gdwLocalBlackMask & dwSeen) != 0)
          *pOut = 0;
        else
          *pOut = ((gdwLocalMask & dwSeen) != 0) ? nHalf : sgbBitMask;
      }
      if (dwX++ < gwMapWidth - 1u)
        ++pTile;
      ++pOut;
    }
    if (static_cast<DWORD>(nY++) < gwMapHeight - 1u)
      pRow += gwMapWidth;
    pOutRow += nGrid;
  }

  {
    const BYTE *pSrc = dword_670A78 + nGrid + 1;
    BYTE *pDst = sgpCurrMaskMap + nGrid + 1;
    int nR, nC;
    nR = MaskGridHeight() - 2;
    while (nR != 0) {
      --nR;
      nC = nGrid - 2;
      while (nC != 0) {
        --nC;
        unsigned n = pSrc[-nGrid] + pSrc[0] * 2u;
        n += pSrc[nGrid];
        n += pSrc[-1];
        n += pSrc[1];
        n = pSrc[-(nGrid + 1)] + n * 2u;
        n += pSrc[nGrid + 1];
        n += pSrc[-(nGrid - 1)];
        n += pSrc[nGrid - 1];
        *pDst = static_cast<BYTE>(n >> 4);
        ++pDst;
        ++pSrc;
      }
      pDst += 2;
      pSrc += 2;
    }
  }
}

// Applies one unit's sight pattern to the fog map. The clipped pair of
// MaskFnTbl slots is chosen when the pattern would run off any edge.
void sub_46BBB0(int nPixelX, int nPixelY, int bAir, int nPlayer, int nRange,
                int nUnused)
{
  if (static_cast<DWORD>(nPlayer) > 0xFF)
    return;

  mask_tbl_t *pTbl = &sgMaskTbls[nRange];
  PFN_MASKUPDATE *ppfn = &MaskFnTbl[0];
  DWORD dwAnd = ~(static_cast<DWORD>(nPlayer) << 8) & ~static_cast<DWORD>(nPlayer);
  DWORD dwSeen = static_cast<DWORD>(nPlayer);
  int nTileX, nTileY, nHalf;

  if (bAir != 0) {
    ppfn = &MaskFnTbl[1];
  } else {
    int nHeight = get_map_height(nPixelX, nPixelY);
    if (nHeight == 2)
      dwSeen |= 0x1000000;
    else if (nHeight == 1)
      dwSeen |= 0x5000000;
    else
      dwSeen |= 0x7000000;
  }

  nTileX = nPixelX / 32;
  nTileY = nPixelY / 32;

  // One `||` chain, as in 1.17.
  nHalf = pTbl->nWidth / 2;
  if (nTileX - nHalf < 0 || nTileX + nHalf >= gwMapWidth
      || nTileY - (nHalf = pTbl->nHeight / 2) < 0
      || nTileY + nHalf >= gwMapHeight)
    ppfn += 2;

  (*ppfn)(nTileX, nTileY, pTbl, &gpSquareMap[gwMapWidth * nTileY + nTileX],
          dwSeen, dwAnd);
}

// Hyperion: sub_46BBB0's sight pattern for the slots past 7, whose bits are in
// Hyperion's own tile masks (playervision.h). The cells a pattern reveals are
// the ones sub_46BBB0's workers reveal: they follow from the pattern, the map's edges and
// the heights, not from whose sight it is. A ground cell past the direct ones is revealed
// when a cell nearer the centre was, and that cell's height bits (24 to 26) do not block
// the viewer's height. The lit flags are kept here, so the patterns' own dwSeen stay as
// 1.17's workers left them.
void MaskStampHx(int nPixelX, int nPixelY, int bAir, const PlayerSet &q, int nRange)
{
  PlayerSet *pHidden = VisionHxHidden();
  PlayerSet *pUnexplored = VisionHxUnexplored();
  const PlayerSet qSlots = q & PlayerSetHigh();
  if (pHidden == NULL || !qSlots.Any())
    return;
  const PlayerSet qKeep = ~qSlots;
  static BYTE s_abLit[0x4000];
  const mask_tbl_t *pTbl = &sgMaskTbls[nRange];
  const int nTileX = nPixelX / 32, nTileY = nPixelY / 32;
  const int nCentre = gwMapWidth * nTileY + nTileX;
  DWORD dwHeight = 0;
  if (bAir == 0) {
    const int nHeight = get_map_height(nPixelX, nPixelY);
    dwHeight = nHeight == 2 ? 0x1000000 : nHeight == 1 ? 0x5000000 : 0x7000000;
  }
  const int nEnts = bAir != 0 ? pTbl->nShaded : pTbl->nDirect + pTbl->nShaded;
  if (nEnts > static_cast<int>(sizeof s_abLit))
    return;
  for (int i = 0; i < nEnts; i++) {
    const mask_ent_t *pEnt = &pTbl->pEnts[i];
    bool bLit = static_cast<DWORD>(nTileX + pEnt->cX) < gwMapWidth
             && static_cast<DWORD>(nTileY + pEnt->cY) < gwMapHeight;
    if (bLit && bAir == 0 && i >= pTbl->nDirect) {
      bLit = false;
      for (int k = pEnt->nPrev; k-- != 0 && !bLit;) {
        const int nPrev = static_cast<int>(pEnt->pPrev[k] - pTbl->pEnts);
        bLit = s_abLit[nPrev] != 0
            && (gpSquareMap[nCentre + pTbl->pEnts[nPrev].dwTile] & dwHeight) == 0;
      }
    }
    s_abLit[i] = bLit;
    if (bLit) {
      pHidden[nCentre + pEnt->dwTile] &= qKeep;
      pUnexplored[nCentre + pEnt->dwTile] &= qKeep;
    }
  }
}

// Paints the light map into the mask overlay, one 8 by 8 block per light cell.
// A block whose four corners agree is either cleared or shaded flat; otherwise
// it is interpolated. Hyperion: the light map's rows are MaskLightPitch
// apart, 0x58 in 1.17, and the overlay's the view's width, 640 in 1.17.
void mask_apply_light(int nLeft, int nTop, int nRight, int nBottom)
{
  const int nLight = MaskLightPitch();
  BYTE *pLight = sgpCurrLightMap
               + ((static_cast<DWORD>(glScrollPixelX) >> 3) & 3)
               + nLight * (static_cast<int>((static_cast<DWORD>(glScrollPixelY) >> 3) & 3)
                           + (nTop >> 3))
               + (nLeft >> 3);
  BYTE *pDst = dword_6D0B8C + nTop * g_portView.nWidth + nLeft;
  int y, x;

  for (y = nTop; y < nBottom; y += 8) {
    BYTE *pRow = pDst;
    const BYTE *pL = pLight;

    for (x = nLeft; x < nRight; x += 8) {
      int nTL = pL[0];
      int nTR = pL[1];
      int nBL = pL[nLight];
      int nBR = pL[nLight + 1];

      if (nTL == nTR && nTL == nBL && nTL == nBR) {
        if (nTL == 0)
          mask_block_clear(pRow);
        else if (nTL != 0x1F)
          mask_block_flat(pRow, nTL);
      } else {
        mask_block_lerp(pRow, nTL, nTR, nBL, nBR);
      }
      pRow += 8;
      ++pL;
    }
    pLight += nLight;
    pDst += 8 * g_portView.nWidth;
  }
}

#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
// 1.17's body is inline assembly; this C++ is the portable reference
// Clears one 8 by 8 block of the mask overlay. Hyperion: its rows are the
// view's width apart, 160 dwords in 1.17.
__declspec(noinline) void mask_block_clear(BYTE *pDst)
{
  DWORD *p = reinterpret_cast<DWORD *>(pDst);
  const int nRow = g_portView.nWidth / 4;
  DWORD dw = 0;

  p[0 * nRow + 0] = dw;  p[0 * nRow + 1] = dw;
  p[1 * nRow + 0] = dw;  p[1 * nRow + 1] = dw;
  p[2 * nRow + 0] = dw;  p[2 * nRow + 1] = dw;
  p[3 * nRow + 0] = dw;  p[3 * nRow + 1] = dw;
  p[4 * nRow + 0] = dw;  p[4 * nRow + 1] = dw;
  p[5 * nRow + 0] = dw;  p[5 * nRow + 1] = dw;
  p[6 * nRow + 0] = dw;  p[6 * nRow + 1] = dw;
  p[7 * nRow + 0] = dw;  p[7 * nRow + 1] = dw;
}

#endif  // OC_PAYLOAD

#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)

// 1.17's body is an inline __asm block; this C++ is the portable reference
__declspec(noinline) void mask_block_flat(BYTE *pDst, int nShade)
{
  int i, j;

  for (i = 8; i != 0; --i) {
    for (j = 0; j < 8; ++j)
      pDst[j] = shiftDark_g[nShade][pDst[j]];
    pDst += g_portView.nWidth;  // Hyperion: 640 in 1.17
  }
}

// an inline __asm block in 1.17, as mask_block_flat; this C++ is the portable
// reference.
__declspec(noinline) void mask_block_lerp(BYTE *pDst, int nTL, int nTR, int nBL,
                                     int nBR)
{
  int nLeft = nTL << 8;
  int nRight = nTR << 8;
  int nLeftStep = ((nBL << 8) - nLeft) / 8;
  int nRightStep = ((nBR << 8) - nRight) / 8;
  int i, j;

  // Hyperion: a row runs from the right, as the oracle's does: pixel 7
  // takes the right edge's shade and each pixel to its left one step more
  // (`sar edx, 3` of left minus right). Run from the left, as this was, every
  // pixel took its right neighbour's shade and the fog's edges drew a pixel
  // off (docs/upstream-notes.md).
  for (i = 8; i != 0; --i) {
    int nStep = (nLeft - nRight) >> 3;
    int nShade = nRight;
    for (j = 7; j >= 0; --j) {
      pDst[j] = shiftDark_g[(nShade >> 8) & 0x1F][pDst[j]];
      nShade += nStep;
    }
    nLeft += nLeftStep;
    nRight += nRightStep;
    pDst += g_portView.nWidth;  // Hyperion: 640 in 1.17
  }
}

#endif  // OC_PAYLOAD

// Fills dword_670A80 with the four-step ramp between every pair of shades. The
// step is taken with an unsigned shift, so a downhill ramp wraps, as in 1.17.
// The dead initialisers on t and d, and the if/else pairs that are one
// statement twice (MSVC merges them), keep 1.17's register choices.
__declspec(noinline) void sub_46BFC0()
{
  DWORD t = 0;
  DWORD d = 0;
  BYTE *p = reinterpret_cast<BYTE *>(dword_670A80);
  DWORD a;
  int nBase = 0;

  for (a = 0; a < 0x2000; a += 0x100) {
    int nDelta;
    if (a & 0x100) nDelta = nBase; else nDelta = nBase;
    int j;
    for (j = 0x20; j != 0; --j) {
      d = static_cast<DWORD>(nDelta) >> 2;
      BYTE *q = p;
      p += 4;
      nDelta += 0x100;
      if (a & 0x100) t = a; else t = a;
      q[0] = static_cast<BYTE>(t >> 8);
      t = d + t;
      q[1] = static_cast<BYTE>(t >> 8);
      t = d + t;
      q[2] = static_cast<BYTE>(t >> 8);
      t = d + t;
      q[3] = static_cast<BYTE>(t >> 8);
    }
    nBase -= 0x100;
  }
}

// Hyperion: what the grid is made from: the fog dwords and the replay
// viewer's sets past slot 7, which change in a game frame, the view's tiles, the masks the
// player's vision is read with, and the state put back by a load or a restore.
#ifdef OC_DLL_MODE
#define oc_dwReplayFrame (*reinterpret_cast<unsigned int *>(OC_VA(0x52651Cu)))
#else
extern unsigned int oc_dwReplayFrame;  // 0x52651C, the game's frames so far
#endif
struct MaskGridKey {
  unsigned int nFrame, nState;
  DWORD adwView[6];
  DWORD adwMasks[6];
  const void *pSquares;
};

// Rebuilds the mask grid and keeps the previous frame's copy. Hyperion: the grid is made
// again only when what it is made from has changed: every drawn frame takes the
// map's full path with the GPU scene, and a render view zoomed out is tens of thousands of
// tiles a pass, most passes between the game's frames.
void mask_build_grid()
{
  static MaskGridKey s_key;
  MaskGridKey key;
  memset(&key, 0, sizeof key);
  key.nFrame = oc_dwReplayFrame;
  key.nState = PortSceneStateSerial();
  key.adwView[0] = gwScreenTileX;
  key.adwView[1] = gwScreenTileY;
  key.adwView[2] = static_cast<DWORD>(MaskGridWidth());
  key.adwView[3] = static_cast<DWORD>(MaskGridHeight());
  key.adwView[4] = gwMapWidth;
  key.adwView[5] = gwMapHeight;
  key.adwMasks[0] = gPlayback;
  key.adwMasks[1] = sgbShowAll_g;
  key.adwMasks[2] = gdwReplayVisionMask;
  key.adwMasks[3] = gdwLocalMask;
  key.adwMasks[4] = gdwLocalBlackMask;
  key.adwMasks[5] = sgbBitMask;
  key.pSquares = gpSquareMap;
  if (memcmp(&key, &s_key, sizeof key) != 0) {
    memcpy(&s_key, &key, sizeof key);
    GenerateFogArr();
  } else if (PortScenePassKeepCheck()) {
    // HYPERION_PASS_KEEP_CHECK: made again, and compared with what was kept
    const size_t n = static_cast<size_t>(MaskGridWidth()) * MaskGridHeight();
    static std::vector<BYTE> s_raw, s_smooth;
    s_raw.assign(dword_670A78, dword_670A78 + n);
    s_smooth.assign(sgpCurrMaskMap, sgpCurrMaskMap + n);
    GenerateFogArr();
    unsigned int nDiffer = 0;
    for (size_t i = 0; i < n; ++i)
      nDiffer += (s_raw[i] != dword_670A78[i]) + (s_smooth[i] != sgpCurrMaskMap[i]);
    PortScenePassKeepDiffer("the fog's grid", nDiffer);
  }
  memcpy(dword_670A74, sgpCurrMaskMap, MaskGridWidth() * MaskGridHeight());  // Hyperion: 0x198 in 1.17
}

// Two cheats clear fog, and they clear a different width of it: the full-vision
// bit wipes both the explored and the visible halves of each tile dword, the
// map-reveal bit only the low byte. Neither is allowed to persist into a
// multiplayer game, which is what the opening clear is for. With neither bit set
// there is nothing to redraw and the function returns before the sweep.
void mask_disable_fog()
{
  if (gbMultiPlayerMode != 0)
    glCheatBits &= ~0x4001;

  LONG lKeep;
  if ((glCheatBits & 1) != 0)
    lKeep = -0x10000;
  else if ((glCheatBits & 0x4000) != 0)
    lKeep = -0x100;
  else
    return;

  DWORD *pTile = gpSquareMap;
  for (int nLeft = gwMapWidth * gwMapHeight; nLeft != 0; --nLeft) {
    *pTile &= lKeep;
    ++pTile;
  }
  VisionHxReveal((glCheatBits & 1) != 0);  // Hyperion: the slots past 7
  gamemap_force_redraw();
}


// Releases the sight tables and the four grids.
void mask_free()
{
  los_free(sgMaskTbls, 12);

  if (sgpCurrMaskMap != NULL) {
    SMemFree(sgpCurrMaskMap, "mask.cpp", 0x118, 0);
    sgpCurrMaskMap = NULL;
  }
  if (dword_670A74 != NULL) {
    SMemFree(dword_670A74, "mask.cpp", 0x11D, 0);
    dword_670A74 = NULL;
  }
  if (dword_670A78 != NULL) {
    SMemFree(dword_670A78, "mask.cpp", 0x122, 0);
    dword_670A78 = NULL;
  }
  if (sgpCurrLightMap != NULL) {
    SMemFree(sgpCurrLightMap, "mask.cpp", 0x129, 0);
    sgpCurrLightMap = NULL;
  }
}

// The mirror of mask_disable_fog: puts the fog back over every tile unless a
// cheat is still holding it open. 1.17 loads the map width before the height,
// which `gwMapWidth * gwMapHeight` keeps.
void sub_46C180()
{
  if (gbMultiPlayerMode != 0)
    glCheatBits &= ~0x4001;

  BYTE bFlags = byte_6D0BF5;
  if ((bFlags & 1) == 0)
    byte_6D0BF5 = static_cast<BYTE>(bFlags | 2);

  if ((glCheatBits & 0x4001) == 0) {
    gamemap_force_redraw();

    DWORD *pTile = gpSquareMap;
    for (int nLeft = gwMapWidth * gwMapHeight; nLeft != 0; --nLeft) {
      *pTile |= 0xFF;
      ++pTile;
    }
    VisionHxHideAll();  // Hyperion: the slots past 7
  }
}


// Blacks out the whole fog map, builds the shade ramps and the sight tables,
// and allocates the four grids.
void mask_init()
{
  DWORD *pTile = gpSquareMap;
  for (int nLeft = MAP_TILES_MAX; nLeft != 0; --nLeft) {  // Hyperion: 0x10000 in 1.17
    *pTile |= 0xFFFF;
    ++pTile;
  }
  VisionHxInit();  // Hyperion: the slots past 7

  sub_46BFC0();
  sub_466E40(sgMaskTbls, 12, gwMapWidth);

  sgbBitMask = 0x1F;

  // Hyperion: sized for the widest view (port_view.cpp), so the view can
  // change without them, or for the map and a margin past it when that is larger, for a
  // view zoomed out over it; 1.17's were 0x198 and 0x14A0 bytes.
  const int nMapX = static_cast<int>(gwMapWidth) + MASK_MAP_SPARE;
  const int nMapY = static_cast<int>(gwMapHeight) + MASK_MAP_SPARE;
  const int nRingX = nMapX > PORT_VIEW_RING_TILES_X_MAX ? nMapX : PORT_VIEW_RING_TILES_X_MAX;
  const int nRingY = nMapY > PORT_VIEW_RING_TILES_Y_MAX ? nMapY : PORT_VIEW_RING_TILES_Y_MAX;
  g_nMaskGridMax = (nRingX + 3) * (nRingY + 3);
  g_nMaskLightMax = (nRingX + 1) * 4 * (nRingY + 1) * 4;
  sgpCurrMaskMap = static_cast<BYTE *>(SMemAlloc(g_nMaskGridMax, "mask.cpp", 0xFE, 0));
  memset(sgpCurrMaskMap, 0, g_nMaskGridMax);
  dword_670A74 = static_cast<BYTE *>(SMemAlloc(g_nMaskGridMax, "mask.cpp", 0x101, 0));
  memset(dword_670A74, 0, g_nMaskGridMax);
  dword_670A78 = static_cast<BYTE *>(SMemAlloc(g_nMaskGridMax, "mask.cpp", 0x104, 0));
  memset(dword_670A78, 0, g_nMaskGridMax);
  sgpCurrLightMap = static_cast<BYTE *>(SMemAlloc(g_nMaskLightMax, "mask.cpp", 0x10A, 0));
  memset(sgpCurrLightMap, 0, g_nMaskLightMax);
}

// Rebuilds the mask grid, then invalidates every 0x20 by 0x20 screen cell whose
// shade moved since the last frame. Runs of unchanged cells are skipped, so one
// invalidate covers a whole run. Hyperion: it walks the grid inside its
// border, 22 by 15 megatiles (0x2C0 by 0x1E0 pixels) in 1.17.
void mask_invalidate()
{
  GenerateFogArr();

  const int nGrid = MaskGridWidth();
  const int nSpanX = (nGrid - 2) * 0x20;
  const int nSpanY = (MaskGridHeight() - 2) * 0x20;
  BYTE *pPrev = sgpCurrMaskMap + nGrid + 1;
  BYTE *pCurr = dword_670A74 + nGrid + 1;
  int nRight = nSpanX - static_cast<int>(glScrollPixelX & 0x1F);
  int nBottom = nSpanY - static_cast<int>(glScrollPixelY & 0x1F);
  int y, x;

  for (y = nBottom - nSpanY; y < nBottom; y += 0x20) {
    for (x = nRight - nSpanX; x < nRight; x += 0x20) {
      int nRun, nScan;
      BYTE bCurr = *pCurr++;
      BYTE bPrev = *pPrev++;
      if (bCurr == bPrev)
        continue;
      nRun = 0x20;
      pCurr[-1] = pPrev[-1];
      for (nScan = x + 0x20; nScan < nRight; nScan += 0x20) {
        BYTE b = *pPrev;
        if (*pCurr == b)
          break;
        *pCurr = b;
        ++pCurr;
        ++pPrev;
        nRun += 0x20;
      }
      img_area_invalid(x - 0x20, y - 0x20, x + nRun + 0x1F, y + 0x3F);
      x += nRun - 0x20;
    }
    pPrev += 2;
    pCurr += 2;
  }
}

// Repaints the mask overlay. Nonzero repaints all of it; zero walks gbInvalMap
// and repaints only the runs of dirty 16 by 16 cells. Hyperion: over the
// view's width and map rows, 0x280 by 0x190 in 1.17 (port_view.cpp).
void mask_draw(int bAll)
{
  const int nWidth = g_portView.nWidth;
  const int nRows = g_portView.nMapRows;

  if (bAll != 0) {
    // Hyperion: the GPU scene (port_scene.cpp) takes the light map as it is, and
    // when it alone draws the map the software does not apply it.
    const int nFirst = static_cast<int>((static_cast<DWORD>(glScrollPixelX) >> 3) & 3) +
                       MaskLightPitch() * static_cast<int>((static_cast<DWORD>(glScrollPixelY) >> 3) & 3);
    if (!PortSceneFog(sgpCurrLightMap, MaskLightPitch(), MaskLightBytes(), nFirst, &shiftDark_g[0][0],
                      sgpCurrMaskMap, MaskGridWidth(), g_portView.nRingTilesX, g_portView.nRingTilesY,
                      &dword_670A80[0][0]))
      mask_apply_light(0, 0, nWidth, nRows);
    return;
  }

  const BYTE *pInval = g_portView.pbInvalMap;
  int y, x;

  for (y = 0; y < nRows; y += 0x10) {
    for (x = 0; x < nWidth; x += 0x10) {
      int nRun, nScan;
      if (*pInval++ == 0)
        continue;
      nRun = 0x10;
      for (nScan = x + 0x10; nScan < nWidth; nScan += 0x10) {
        if (*pInval == 0)
          break;
        ++pInval;
        nRun += 0x10;
      }
      mask_apply_light(x, y, x + nRun, y + 0x10);
      x += nRun - 0x10;
    }
  }
}

// The per-frame entry point. The argument is forwarded nowhere: Fog
// reloads ECX from gbInvalMap's head before reading it.
void mask_update(int bAll)
{
  // Hyperion: the GPU makes the light map from the grid when it alone draws the map
  // (port_scene.cpp's PortSceneFog)
  if (!PortSceneFogOnGpu())
    Fog();
}

// MaskFnTbl[1]: an air unit sees over terrain, so every cell of the pattern is
// revealed without a blocker test. The count is nShaded, read from the head of
// the array rather than its tail, as 1.17 indexes it.
void mask_update_air(int nTileX, int nTileY, mask_tbl_t *pTbl, DWORD *pTile,
                     DWORD dwSeen, DWORD dwAnd)
{
  mask_ent_t *pEnt = pTbl->pEnts;
  int n = pTbl->nShaded;

  while (n != 0) {
    --n;
    pTile[pEnt->dwTile] &= dwAnd;
    ++pEnt;
  }
}

// MaskFnTbl[3]: mask_update_air with the per-cell bounds test.
void mask_update_air_clipped(int nTileX, int nTileY, mask_tbl_t *pTbl,
                             DWORD *pTile, DWORD dwSeen, DWORD dwAnd)
{
  mask_ent_t *pEnt = pTbl->pEnts;
  int n = pTbl->nShaded;

  while (n != 0) {
    --n;
    if (static_cast<DWORD>(nTileX + pEnt->cX) < gwMapWidth
     && static_cast<DWORD>(nTileY + pEnt->cY) < gwMapHeight)
      pTile[pEnt->dwTile] &= dwAnd;
    ++pEnt;
  }
}

// MaskFnTbl[0]: the direct cells are revealed outright, then each shaded cell
// is revealed only if one of the cells nearer the centre came out lit. The
// reveal reads dwTile twice in one expression, as 1.17 does.
void mask_update_ground(int nTileX, int nTileY, mask_tbl_t *pTbl, DWORD *pTile,
                        DWORD dwSeen, DWORD dwAnd)
{
  mask_ent_t *pEnt = pTbl->pEnts;
  int n = pTbl->nDirect;

  while (n != 0) {
    int dwIdx = pEnt->dwTile;  // Hyperion: signed, DWORD in 1.17
    pTile[dwIdx] &= dwAnd;
    pEnt->dwSeen = pTile[dwIdx];
    ++pEnt;
    --n;
  }

  n = pTbl->nShaded;
  while (n != 0) {
    int i = pEnt->nPrev;
    --n;
    pEnt->dwSeen = 0xFF;
    while (i != 0) {
      --i;
      if ((pEnt->pPrev[i]->dwSeen & dwSeen) == 0) {
        pEnt->dwSeen = (pTile[pEnt->dwTile] &= dwAnd);
        break;
      }
    }
    ++pEnt;
  }
}

// MaskFnTbl[2]: mask_update_ground with the per-cell bounds test.
void mask_update_ground_clipped(int nTileX, int nTileY, mask_tbl_t *pTbl,
                                DWORD *pTile, DWORD dwSeen, DWORD dwAnd)
{
  mask_ent_t *pEnt = pTbl->pEnts;
  int n = pTbl->nDirect;

  while (n != 0) {
    --n;
    pEnt->dwSeen = 0xFF;
    if (static_cast<DWORD>(nTileX + pEnt->cX) < gwMapWidth
     && static_cast<DWORD>(nTileY + pEnt->cY) < gwMapHeight) {
      int dwIdx = pEnt->dwTile;  // Hyperion: signed, DWORD in 1.17
      pTile[dwIdx] &= dwAnd;
      pEnt->dwSeen = pTile[dwIdx];
    }
    ++pEnt;
  }

  n = pTbl->nShaded;
  while (n != 0) {
    --n;
    pEnt->dwSeen = 0xFF;
    if (static_cast<DWORD>(nTileX + pEnt->cX) < gwMapWidth
     && static_cast<DWORD>(nTileY + pEnt->cY) < gwMapHeight) {
      int i = pEnt->nPrev;
      while (i != 0) {
        --i;
        if ((pEnt->pPrev[i]->dwSeen & dwSeen) == 0) {
          int dwIdx = pEnt->dwTile;  // Hyperion: signed, DWORD in 1.17
          pTile[dwIdx] &= dwAnd;
          pEnt->dwSeen = pTile[dwIdx];
          break;
        }
      }
    }
    ++pEnt;
  }
}
