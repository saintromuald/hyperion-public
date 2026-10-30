// cellscrl.cpp -- the tiled map view: the scroll position in pixels and tiles,
// the map-edge limits, and the repaint of the cell grid. Bodies are in oracle
// VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/cellscrl.h"
#include "../h/game.h"        // cell_init
#include "../h/gamemap.h"     // cell_refresh, gamemap_force_redraw
#include "../h/creep.h"       // cell_draw_creep, tile_is_creep_ptr
#include "../h/replay.h"      // gPlayback, gdwReplayVisionMask
#include "../h/gameloop.h"    // cell_fog_update
#include "../h/minimap.h"     // cell_set_pos, IsAutoScroll
#include "../gen/globals117.h"
#include "../port/render/port_scene.h"  // Hyperion: PortSceneCell
#include "../port/tools/port_trace.h"  // Hyperion: PortTraceViewSet
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include <intrin.h>           // Hyperion: _ReturnAddress, for the camera trace
#include "../h/playervision.h"  // Hyperion: the replay viewer past slot 7 (stage D)

// The globals this TU reaches, dual-spelled, one #ifdef per name. gwMapWidth,
// gwMapHeight, glScrollPixelX, glScrollPixelY, sgMapPixRect_g, gwMapPixWidth
// and gwMapPixHeight come from ../gen/globals117.h.

// 0x5264B0 / 0x5264B2 -- the view's top-left in tiles, spelled as CUnitGUI.cpp
// spells them.
#ifndef OC_GWSCREENTILEX
#define OC_GWSCREENTILEX
#ifdef OC_DLL_MODE
#define gwScreenTileX (*reinterpret_cast<WORD *>(OC_VA(0x5264B0u)))
#else
extern WORD gwScreenTileX;
#endif
#endif

#ifndef OC_GWSCREENTILEY
#define OC_GWSCREENTILEY
#ifdef OC_DLL_MODE
#define gwScreenTileY (*reinterpret_cast<WORD *>(OC_VA(0x5264B2u)))
#else
extern WORD gwScreenTileY;
#endif
#endif

// 0x543208 / 0x54320C -- the last row and column the repaint loops reached, in
// tiles. The scroll routines walk them toward gwScreenTileX / gwScreenTileY.
#ifndef OC_GWLASTSCROLLMTXX
#define OC_GWLASTSCROLLMTXX
#ifdef OC_DLL_MODE
#define gwLastScrollMtxX (*reinterpret_cast<WORD *>(OC_VA(0x543208u)))
#elif defined(OC_PAYLOAD)
extern WORD gwLastScrollMtxX;
#else
// Defined, not extern, for the same reason as gwLastScrollMtxY: cell_scroll_rgt
// reloads it as a dword.
WORD gwLastScrollMtxX;
#endif
#endif

#ifndef OC_GWLASTSCROLLMTXY
#define OC_GWLASTSCROLLMTXY
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define gwLastScrollMtxY (*reinterpret_cast<WORD *>(OC_VA(0x54320Cu)))
#else
// Defined, not extern: cell_scroll_dn reloads it as a dword, which MSVC does
// only for an object this TU places.
WORD gwLastScrollMtxY;
#endif
#endif

// 0x543210 / 0x543214 -- the scroll position as of the last repaint, in pixels.
// game.cpp already carries both spellings.
#ifndef OC_GLLASTSCROLLPIXELX
#define OC_GLLASTSCROLLPIXELX
#ifdef OC_DLL_MODE
#define glLastScrollPixelX (*reinterpret_cast<LONG *>(OC_VA(0x543210u)))
#else
extern LONG glLastScrollPixelX;
#endif
#endif

#ifndef OC_GLLASTSCROLLPIXELY
#define OC_GLLASTSCROLLPIXELY
#ifdef OC_DLL_MODE
#define glLastScrollPixelY (*reinterpret_cast<LONG *>(OC_VA(0x543214u)))
#else
extern LONG glLastScrollPixelY;
#endif
#endif

// 0x543220 / 0x543224 -- the largest scroll position the map allows, in pixels.
// cursor.cpp already carries both spellings and reads both unsigned.
#ifndef OC_GLMAPPIXELRGT
#define OC_GLMAPPIXELRGT
#ifdef OC_DLL_MODE
#define glMapPixelRgt (*reinterpret_cast<DWORD *>(OC_VA(0x543220u)))
#else
extern DWORD glMapPixelRgt;
#endif
#endif

#ifndef OC_GLMAPPIXELBTM
#define OC_GLMAPPIXELBTM
#ifdef OC_DLL_MODE
#define glMapPixelBtm (*reinterpret_cast<DWORD *>(OC_VA(0x543224u)))
#else
extern DWORD glMapPixelBtm;
#endif
#endif

// 0x6CE650 -- the CV5 megatile-group table, 0x1A WORDs per group, the cell ids
// at +10. gamemap.cpp already carries both spellings and the same WORD * type.
#ifndef OC_GPMTXINFO
#define OC_GPMTXINFO
#ifdef OC_DLL_MODE
#define gpMtxInfo (*reinterpret_cast<WORD **>(OC_VA(0x6CE650u)))
#else
extern WORD *gpMtxInfo;
#endif
#endif

// 0x666458 -- one WORD per tile, indexed exactly as gfpCellMap is.
#ifndef OC_GFPMIMAP
#define OC_GFPMIMAP
#ifdef OC_DLL_MODE
#define gfpMIMap (*reinterpret_cast<WORD **>(OC_VA(0x666458u)))
#else
extern WORD *gfpMIMap;
#endif
#endif

// 0x543238 -- the megatile set, 32 bytes per entry, sixteen WORD cell ids.
// minimap.cpp already carries both spellings and the same BYTE * type.
#ifndef OC_GFPMTXSET
#define OC_GFPMTXSET
#ifdef OC_DLL_MODE
#define gfpMtxSet (*reinterpret_cast<BYTE **>(OC_VA(0x543238u)))
#else
extern BYTE *gfpMtxSet;
#endif
#endif

// 0x54323C -- the cell grid, one WORD per tile, row-major over gwMapWidth.
// CUnitPathUtil.cpp already carries both spellings.
#ifndef OC_GFPCELLMAP
#define OC_GFPCELLMAP
#ifdef OC_DLL_MODE
#define gfpCellMap (*reinterpret_cast<WORD **>(OC_VA(0x54323Cu)))
#else
extern WORD *gfpCellMap;
#endif
#endif

// 0x543250 / 0x543254 -- the wrap period of the parallax accumulators below,
// one for each axis. Read once per scroll and used to fold the accumulator back
// into range.
#ifndef OC_DWORD_543250
#define OC_DWORD_543250
#ifdef OC_DLL_MODE
#define dword_543250 (*reinterpret_cast<LONG *>(OC_VA(0x543250u)))
#else
extern LONG dword_543250;
#endif
#endif

#ifndef OC_DWORD_543254
#define OC_DWORD_543254
#ifdef OC_DLL_MODE
#define dword_543254 (*reinterpret_cast<LONG *>(OC_VA(0x543254u)))
#else
extern LONG dword_543254;
#endif
#endif

// 0x543258 and 0x54326C -- five parallax accumulators per axis, stepped by
// dword_502698[i] * pixels on every scroll.
#ifndef OC_DWORD_543258
#define OC_DWORD_543258
#ifdef OC_DLL_MODE
#define dword_543258 (reinterpret_cast<LONG *>(OC_VA(0x543258u)))
#else
extern LONG dword_543258[5] OC_UNALIGNED;
#endif
#endif

#ifndef OC_DWORD_54326C
#define OC_DWORD_54326C
#ifdef OC_DLL_MODE
#define dword_54326C (reinterpret_cast<LONG *>(OC_VA(0x54326Cu)))
#else
extern LONG dword_54326C[5] OC_UNALIGNED;
#endif
#endif

// 0x502698 -- the per-layer scroll rate, five DWORDs in .data, the multiplier
// the accumulators above are stepped by.
#ifndef OC_DWORD_502698
#define OC_DWORD_502698
#ifdef OC_DLL_MODE
#define dword_502698 (reinterpret_cast<LONG *>(OC_VA(0x502698u)))
#else
extern LONG dword_502698[5] OC_UNALIGNED;
#endif
#endif

// The bodies, in oracle VA order.

// 0x40B060. Recomputes every derived view limit from the map dimensions.
// The width is read with its height as one dword, into a local defined first,
// as 1.17 does.
// Hyperion: the view reaches the map's right edge at its own width, 20
// megatiles in 1.17 (port_view.cpp); its bottom reach stays 1.17's.
void cell_init()
{
  DWORD dwWidth = *reinterpret_cast<const DWORD *>(&gwMapWidth);
  // Hyperion: 32,768 at 1,024 tiles. The width alone: 1.17's WORD store
  // dropped the height the dword read brings in its high half, and a Hyperion game's 31
  // bits kept it, which made the map 0x20002000 pixels wide on a 256-tile map.
  gwMapPixWidth = PosU(static_cast<DWORD>(gwMapWidth) << 5);
  gwMapPixHeight = PosU(gwMapHeight << 5);
  sgMapPixRect_g.left = 0;  // Hyperion: a box of map positions
  sgMapPixRect_g.top = 0;
  sgMapPixRect_g.right = gwMapPixWidth;
  sgMapPixRect_g.bottom = gwMapPixHeight;
  glMapPixelBtm = ((DWORD)(gwMapHeight - 12) << 5) + 8;
  {
    // A map narrower than the view does not scroll across.
    const int nRight = (int)(WORD)dwWidth * 32 - g_portView.nWidth;
    glMapPixelRgt = (DWORD)(nRight > 0 ? nRight : 0);
  }
  glScrollPixelX = gwScreenTileX << 5;
  glScrollPixelY = gwScreenTileY << 5;
}

// 0x40B450. Moves the view to a pixel position, clamped to the map and snapped
// to an 8-pixel grid, and repaints if it actually moved. Auto-scroll wins.
void cell_set_pos(int nX, int nY)
{
  PortTraceViewSet(_ReturnAddress(), nX, nY);  // Hyperion: Debug Tools' camera trace
  if (IsAutoScroll())
    return;

  if (nX < 0)
    nX = 0;
  else if ((DWORD)nX >= glMapPixelRgt)
    nX = (int)glMapPixelRgt;
  nX &= -8;
  glScrollPixelX = nX;
  gwScreenTileX = (WORD)(nX >> 5);

  if (nY < 0)
    nY = 0;
  else if ((DWORD)nY >= glMapPixelBtm)
    nY = (int)glMapPixelBtm;
  nY &= -8;
  glScrollPixelY = nY;
  gwScreenTileY = (WORD)(nY >> 5);

  if (nX != glLastScrollPixelX || nY != glLastScrollPixelY)
    cell_draw_background();
}

// 0x40B4D0. Clips a tile rectangle to the map and then to the 21x14 visible
// grid, returning whether anything is left of it. Hyperion: the grid is
// the view's ring (port_view.cpp).
int cell_clip_rect(int *pnX, int *pnY, int *pnW, int *pnH)
{
  const int nTileX = (int)((DWORD)glScrollPixelX >> 5);
  const int nTileY = (int)((DWORD)glScrollPixelY >> 5);
  const int nRingX = g_portView.nRingTilesX;
  const int nRingY = g_portView.nRingTilesY;

  if (*pnX < 0) {
    *pnX = 0;
  } else if (*pnW + *pnX >= gwMapWidth) {
    *pnW = gwMapWidth - *pnX;
  }

  if (*pnY < 0) {
    *pnY = 0;
  } else if (*pnH + *pnY >= gwMapHeight) {
    *pnH = gwMapHeight - *pnY;
  }

  *pnX -= nTileX;
  *pnY -= nTileY;

  if (*pnX < 0) {
    *pnW += *pnX;
    *pnX = 0;
  }
  if (*pnW + *pnX >= nRingX)
    *pnW = nRingX - *pnX;

  if (*pnY < 0) {
    *pnH += *pnY;
    *pnY = 0;
  }
  if (*pnH + *pnY >= nRingY)
    *pnH = nRingY - *pnY;

  if (*pnW <= 0 || *pnH <= 0 || *pnX >= nRingX || *pnY >= nRingY)
    return 0;

  *pnX += nTileX;
  *pnY += nTileY;
  if (*pnX >= gwMapWidth || *pnY >= gwMapHeight)
    return 0;
  return 1;
}

// The redraw loop's done test for cell_scroll_dn. Calling it rather than
// writing the compare keeps cell_draw_row a call there, as in the oracle.
static inline bool RowsDone()
{
  return gwLastScrollMtxY == gwScreenTileY;
}

// The same test for the column loops.
static inline bool ColsDone()
{
  return gwLastScrollMtxX == gwScreenTileX;
}

// The cell map address of one tile. cell_scroll_up calls it rather than
// indexing inline, which keeps cell_draw_row a call there.
static inline WORD *RowPtr(int nX, int nY)
{
  return &gfpCellMap[nY * gwMapWidth + nX];
}

// 0x40B5E0. Scrolls the view down, repainting one row per tile crossed. The
// parallax loop is one index counted down; the redraw loop is entered through
// its bottom test.
BOOL cell_scroll_dn(int nPixels)
{
  LONG lPos = glScrollPixelY + nPixels;
  glScrollPixelY = lPos;
  if ((DWORD)lPos > glMapPixelBtm) {
    nPixels += (int)(glMapPixelBtm - lPos);
    lPos = (LONG)glMapPixelBtm;
    glScrollPixelY = lPos;
  }

  const LONG lTile = (LONG)((DWORD)lPos >> 5);
  gwScreenTileY = (WORD)lTile;
  if (glLastScrollPixelY == lPos)
    return 0;
  glLastScrollPixelY = lPos;

  const LONG lWrap = dword_543254;
  int i = 5;
  do {
    --i;
    dword_54326C[i] -= dword_502698[i] * nPixels;
    if (dword_54326C[i] < 0)
      dword_54326C[i] += lWrap;
  } while (i != 0);

  if (RowsDone())
    return 1;
  do {
    const WORD wDraw = (WORD)(gwLastScrollMtxY + g_portView.nRingTilesY);  // Hyperion: 14 in 1.17
    gwLastScrollMtxY = (WORD)(gwLastScrollMtxY + 1);
    cell_draw_row(&gfpCellMap[wDraw * gwMapWidth + gwScreenTileX], gwScreenTileX, wDraw);
  } while (!RowsDone());
  return 1;
}

// 0x40B6C0. Scrolls the view left, repainting one column per tile crossed.
// The wrap period is read once through the local and once through the global,
// as in 1.17.
BOOL cell_scroll_lft(int nPixels)
{
  LONG lPos = glScrollPixelX - nPixels;
  glScrollPixelX = lPos;
  if (lPos < 0) {
    nPixels += (int)lPos;
    lPos = 0;
    glScrollPixelX = 0;
  }

  const LONG lTile = (LONG)((DWORD)lPos >> 5);
  gwScreenTileX = (WORD)lTile;
  if (glLastScrollPixelX == lPos)
    return 0;
  glLastScrollPixelX = lPos;

  const LONG lWrap = dword_543250;
  int i = 5;
  do {
    --i;
    dword_543258[i] += dword_502698[i] * nPixels;
    if (dword_543258[i] > lWrap)
      dword_543258[i] -= dword_543250;
  } while (i != 0);

  if (ColsDone())
    return 1;
  do {
    gwLastScrollMtxX = (WORD)(gwLastScrollMtxX - 1);
    const int nX = gwLastScrollMtxX;
    const int nY = gwScreenTileY;
    cell_draw_col(&gfpCellMap[nY * gwMapWidth + nX], nX, nY);
  } while (!ColsDone());
  return 1;
}

// 0x40B790. Scrolls the view right, repainting one column per tile crossed.
// The same shape as cell_scroll_dn; the column and row go through int locals.
BOOL cell_scroll_rgt(int nPixels)
{
  LONG lPos = glScrollPixelX + nPixels;
  glScrollPixelX = lPos;
  if ((DWORD)lPos > glMapPixelRgt) {
    nPixels += (int)(glMapPixelRgt - lPos);
    lPos = (LONG)glMapPixelRgt;
    glScrollPixelX = lPos;
  }

  const LONG lTile = (LONG)((DWORD)lPos >> 5);
  gwScreenTileX = (WORD)lTile;
  if (glLastScrollPixelX == lPos)
    return 0;
  glLastScrollPixelX = lPos;

  const LONG lWrap = dword_543250;
  int i = 5;
  do {
    --i;
    dword_543258[i] -= dword_502698[i] * nPixels;
    if (dword_543258[i] < 0)
      dword_543258[i] += lWrap;
  } while (i != 0);

  if (ColsDone())
    return 1;
  do {
    const WORD wDraw = (WORD)(gwLastScrollMtxX + g_portView.nRingTilesX);  // Hyperion: 21 in 1.17
    gwLastScrollMtxX = (WORD)(gwLastScrollMtxX + 1);
    const int nX = wDraw;
    const int nY = gwScreenTileY;
    cell_draw_col(&gfpCellMap[nY * gwMapWidth + nX], nX, nY);
  } while (!ColsDone());
  return 1;
}

// 0x40B870. Scrolls the view up, repainting one row per tile crossed. The
// same shape as cell_scroll_lft.
BOOL cell_scroll_up(int nPixels)
{
  LONG lPos = glScrollPixelY - nPixels;
  glScrollPixelY = lPos;
  if (lPos < 0) {
    nPixels += (int)lPos;
    lPos = 0;
    glScrollPixelY = 0;
  }

  const LONG lTile = (LONG)((DWORD)lPos >> 5);
  gwScreenTileY = (WORD)lTile;
  if (glLastScrollPixelY == lPos)
    return 0;
  glLastScrollPixelY = lPos;

  const LONG lWrap = dword_543254;
  int i = 5;
  do {
    --i;
    dword_54326C[i] += dword_502698[i] * nPixels;
    if (dword_54326C[i] > lWrap)
      dword_54326C[i] -= dword_543254;
  } while (i != 0);

  if (RowsDone())
    return 1;
  do {
    gwLastScrollMtxY = (WORD)(gwLastScrollMtxY - 1);
    const int nX = gwScreenTileX;
    const int nY = gwLastScrollMtxY;
    cell_draw_row(RowPtr(nX, nY), nX, nY);
  } while (!RowsDone());
  return 1;
}

// Reduces a byte offset into the 0x49800-byte scroll buffer by five conditional
// subtractions, largest first. cell_draw_col, cell_draw_row and cell_draw_rect
// all open with it. Hyperion: a remainder by the view's ring
// (port_view.cpp), the same for every offset 1.17's subtractions reached.
static int cell_wrap(int nOfs)
{
  return nOfs % g_portView.nRingSize;
}

// 0x40AA90. Steps the parallax layers by the distance the view moved, then
// repaints the cell grid if the view crossed a tile boundary. The decrement at
// the top, and lDx declared and stored first, are 1.17's order.
void cell_draw_background()
{
  const LONG lDx = glLastScrollPixelX - glScrollPixelX;
  const LONG lDy = glLastScrollPixelY - glScrollPixelY;
  int i = 5;
  do {
    --i;
    const LONG lRate = dword_502698[i];
    dword_543258[i] = (dword_543258[i] + lRate * lDx) % dword_543250;
    dword_54326C[i] = (dword_54326C[i] + lRate * lDy) % dword_543254;
  } while (i != 0);
  glLastScrollPixelX = glScrollPixelX;
  glLastScrollPixelY = glScrollPixelY;
  if (gwScreenTileX != gwLastScrollMtxX || gwScreenTileY != gwLastScrollMtxY) {
    gwLastScrollMtxX = gwScreenTileX;
    gwLastScrollMtxY = gwScreenTileY;
    cell_paint();
  }
  gamemap_force_redraw();
}

// 0x40AC20. Draws one megatile as sixteen cells, four across and four down,
// and lays creep over it when bit 15 of the megatile id is set.
// Hyperion: a row of cells is 8 rows of the view's ring down, 0x1500
// bytes in 1.17's (port_view.cpp); a cell reference is a DWORD (PortMegatilesLoad).
void cell_draw_mtx(WORD wMtx, int nDest, int nX, int nY)
{
  const DWORD *pwMtx =
      reinterpret_cast<const DWORD *>(gfpMtxSet + ((wMtx & 0x7FFF) << 6));
  const int nRow = 8 * g_portView.nRingPitch;
  // Hyperion: the GPU scene's terrain (port_scene.cpp); while the GPU alone
  // draws the map the ring is not drawn, and only the creep's edge is reported.
  if (PortSceneCell(wMtx, nX, nY)) {
    if (wMtx & 0x8000)
      cell_draw_creep(nX, nY, nDest);
    return;
  }
  draw_cell(pwMtx[0], nDest);
  draw_cell(pwMtx[1], nDest + 0x8);
  draw_cell(pwMtx[2], nDest + 0x10);
  draw_cell(pwMtx[3], nDest + 0x18);
  draw_cell(pwMtx[4], nDest + nRow);
  draw_cell(pwMtx[5], nDest + nRow + 0x8);
  draw_cell(pwMtx[6], nDest + nRow + 0x10);
  draw_cell(pwMtx[7], nDest + nRow + 0x18);
  draw_cell(pwMtx[8], nDest + 2 * nRow);
  draw_cell(pwMtx[9], nDest + 2 * nRow + 0x8);
  draw_cell(pwMtx[10], nDest + 2 * nRow + 0x10);
  draw_cell(pwMtx[11], nDest + 2 * nRow + 0x18);
  draw_cell(pwMtx[12], nDest + 3 * nRow);
  draw_cell(pwMtx[13], nDest + 3 * nRow + 0x8);
  draw_cell(pwMtx[14], nDest + 3 * nRow + 0x10);
  draw_cell(pwMtx[15], nDest + 3 * nRow + 0x18);
  if (wMtx & 0x8000)
    cell_draw_creep(nX, nY, nDest);
}
// 0x40AE50. Draws one row of up to 21 megatiles, blank where the row is past
// the bottom edge of the map. Hyperion: a row of the view's ring
// (port_view.cpp).
void cell_draw_row(WORD *pwCell, int nX, int nY)
{
  const int nRingSize = g_portView.nRingSize;
  int nOfs = cell_wrap((nY * g_portView.nRingPitch + nX) * 0x20);
  if ((unsigned)nY >= gwMapHeight) {
    int nLeft = g_portView.nRingTilesX;
    do {
      cell_draw_mtx(0, nOfs, nX, nY);
      nOfs += 0x20;
      { const int t = nOfs - nRingSize; if (t >= 0) nOfs = t; }
      ++nX;
      --nLeft;
    } while (nLeft);
  } else {
    unsigned nLeft = gwMapWidth - nX;
    if (nLeft > (unsigned)g_portView.nRingTilesX)
      nLeft = g_portView.nRingTilesX;
    while (nLeft) {
      --nLeft;
      cell_draw_mtx(*pwCell, nOfs, nX, nY);
      nOfs += 0x20;
      if ((unsigned)nOfs >= (unsigned)nRingSize)
        nOfs -= nRingSize;
      ++pwCell;
      ++nX;
    }
  }
}
// 0x40AB40. Draws one column of up to 14 megatiles, blank where the column
// runs past the bottom edge of the map. Two calls rather than a ternary
// argument, which MSVC merges as 1.17 has them. noinline keeps it a call in
// cell_scroll_lft and cell_scroll_rgt.
// Hyperion: a column of the view's ring (port_view.cpp); a megatile row
// of it was 0x5400 bytes in 1.17.
__declspec(noinline) void cell_draw_col(WORD *pwCell, int nX, int nY)
{
  const int nTileRow = 32 * g_portView.nRingPitch;
  const int nLast = g_portView.nRingSize - nTileRow;
  if ((unsigned)nX >= gwMapWidth)
    return;
  unsigned nLeft = gwMapHeight - nY + 1;
  if (nLeft > (unsigned)g_portView.nRingTilesY)
    nLeft = g_portView.nRingTilesY;
  int nOfs = cell_wrap((nY * g_portView.nRingPitch + nX) * 0x20);
  while (nLeft) {
    --nLeft;
    if ((unsigned)nY >= gwMapHeight)
      cell_draw_mtx(0, nOfs, nX, nY);
    else
      cell_draw_mtx(*pwCell, nOfs, nX, nY);
    ++nY;
    pwCell += gwMapWidth;
    nOfs += nOfs >= nLast ? -nLast : nTileRow;
  }
}
// 0x40AD40. Draws a rectangle of megatiles, clipped to the map and to the
// visible grid first. nOfs is declared before nRows, which keeps 1.17's stack
// slots.
void cell_draw_rect(int nX, int nY, int nW, int nH)
{
  if (!cell_clip_rect(&nX, &nY, &nW, &nH))
    return;
  // Hyperion: the view's ring (port_view.cpp).
  const int nRingSize = g_portView.nRingSize;
  WORD *pwCell = &gfpCellMap[nY * gwMapWidth + nX];
  int nOfs = cell_wrap((nY * g_portView.nRingPitch + nX) * 0x20);
  int nRows = nH;
  while (nRows) {
    --nRows;
    int nCur = nOfs;
    int nCols = nW;
    while (nCols) {
      --nCols;
      cell_draw_mtx(*pwCell, nCur, nX, nY);
      nCur += 0x20;
      { const int t = nCur - nRingSize; if (t >= 0) nCur = t; }
      ++pwCell;
      ++nX;
    }
    nOfs += 32 * g_portView.nRingPitch;
    { const int t = nOfs - nRingSize; if (t >= 0) nOfs = t; }
    pwCell += gwMapWidth - nW;
    nX -= nW;
    ++nY;
  }
}

// 0x40B220. Repaints the whole visible grid, one row at a time.
// Hyperion: the view's ring (port_view.cpp).
void cell_refresh()
{
  const int nTileRow = 32 * g_portView.nRingPitch;
  const int nLast = g_portView.nRingSize - nTileRow;
  const int nIdx = gwMapWidth * gwScreenTileY + gwScreenTileX;
  WORD *pwCell = &gfpCellMap[nIdx];
  WORD *pwMI = gfpMIMap + nIdx;
  DWORD *pdwSquare = gpSquareMap + nIdx;
  int nOfs = cell_wrap((gwScreenTileY * g_portView.nRingPitch + gwScreenTileX) * 0x20);
  int nRow = gwScreenTileY;
  int nLeft = g_portView.nRingTilesY;
  do {
    --nLeft;
    cell_refresh_row(pwCell, pwMI, pdwSquare, nRow, nOfs);
    nOfs += nOfs >= nLast ? -nLast : nTileRow;
    ++nRow;
    pwCell += gwMapWidth;
    pwMI += gwMapWidth;
    pdwSquare += gwMapWidth;
  } while (nLeft);
}
// 0x40B310. Walks one row of the visible grid, re-reads each tile that is not
// fogged, and redraws the ones whose cell id changed. The cell is addressed as
// a byte offset from pwMI, as in 1.17; the mask and the square update follow
// cell_fog_update.
void cell_refresh_row(WORD *pwCell, WORD *pwMI, DWORD *pdwSquare, int nRow,
                      int nOfs)
{
  if (nRow >= (int)gwMapHeight)
    return;
  int nCol = gwScreenTileX;
  int bChanged = 0;
  const int nLast = g_portView.nRingSize - 0x20;  // Hyperion: the view's ring, 0x497E0 in 1.17
  unsigned nLeft = gwMapWidth - nCol;
  if (nLeft > (unsigned)g_portView.nRingTilesX)
    nLeft = g_portView.nRingTilesX;
  if (nLeft) {
    const intptr_t nDelta = (BYTE *)pwCell - (BYTE *)pwMI;
    do {
      --nLeft;
      if (gPlayback) {
        if (!(gdwReplayVisionMask & ~*pdwSquare & 0xFF) &&
            !ViewHxTileSeen(static_cast<int>(pdwSquare - gpSquareMap)))  // Hyperion: the viewer's slots past 7 (stage D)
          goto next;
      } else {
        if (*pdwSquare & gdwLocalMask)
          goto next;
      }
      {
        WORD *const pwAt = (WORD *)((BYTE *)pwMI + nDelta);
        const WORD wMI = *pwMI;
        const unsigned nSub = wMI & 0xF;
        const unsigned nGroup = (wMI >> 4) & 0x7FF;
        const WORD wNew = gpMtxInfo[nGroup * 0x1A + nSub + 10];
        if (wNew != (*pwAt & 0xFFFF7FFF)) {
          *pwAt = wNew;
          DWORD dwSq;
          if (tile_is_creep_ptr(pwMI))
            dwSq = *pdwSquare | 0x40000000;
          else
            dwSq = *pdwSquare & ~0x40000000;
          *pdwSquare = dwSq;
          update_creep_edges_at(nCol, nRow);
          cell_draw_mtx(*pwAt, nOfs, nCol, nRow);
          bChanged = 1;
        }
      }
    next:
      ++nCol;
      ++pwMI;
      ++pdwSquare;
      nOfs += nOfs >= nLast ? -nLast : 0x20;
    } while (nLeft);
  }
  if (bChanged)
    minimap_force_redraw();
}

// 0x40B0E0. Repaints the whole visible grid from the cell map, fourteen rows.
// The row pointer is computed from gwScreenTileY before nRow is declared,
// 1.17's order.
void cell_paint()
{
  WORD *pwRow = &gfpCellMap[gwScreenTileY * gwMapWidth + gwScreenTileX];
  int nRow = gwScreenTileY;
  int nLeft = g_portView.nRingTilesY;  // Hyperion: 14 in 1.17
  do {
    --nLeft;
    cell_draw_row(pwRow, gwScreenTileX, nRow);
    ++nRow;
    pwRow += gwMapWidth;
  } while (nLeft);
}

// 0x40AF30. Re-reads every tile on the map that is not fogged and is outside
// the visible grid, redrawing nothing but marking the minimap dirty. The cell
// mask is an unsigned 0xFFFF7FFF, so the `and` stays 32 bits and only the
// compare narrows.
void cell_fog_update()
{
  WORD *pwCell = gfpCellMap;
  WORD *pwMI = gfpMIMap;
  DWORD *pdwSquare = gpSquareMap;
  int bChanged = 0;
  int nX = 0;
  int nY = 0;
  unsigned nLeft = gwMapWidth * gwMapHeight;
  if (nLeft) {
    do {
      --nLeft;
      if (gPlayback) {
        if (!(~*pdwSquare & gdwReplayVisionMask & 0xFF) &&
            !ViewHxTileSeen(static_cast<int>(pdwSquare - gpSquareMap)))  // Hyperion: the viewer's slots past 7 (stage D)
          goto next;
      } else {
        if (*pdwSquare & gdwLocalMask)
          goto next;
      }
      // Hyperion: the tiles outside the view's ring, 21x14 in 1.17.
      if ((unsigned)(nX - gwScreenTileX) >= (unsigned)g_portView.nRingTilesX ||
          (unsigned)(nY - gwScreenTileY) >= (unsigned)g_portView.nRingTilesY) {
        const WORD wMI = *pwMI;
        const unsigned nSub = wMI & 0xF;
        const unsigned nGroup = (wMI >> 4) & 0x7FF;
        const WORD wNew = gpMtxInfo[nGroup * 0x1A + nSub + 10];
        if (wNew != (*pwCell & 0xFFFF7FFF)) {
          *pwCell = wNew;
          // One load of the square, modified in a register, one store.
          DWORD dwSq;
          if (tile_is_creep_ptr(pwMI))
            dwSq = *pdwSquare | 0x40000000;
          else
            dwSq = *pdwSquare & ~0x40000000;
          *pdwSquare = dwSq;
          update_creep_edges_at(nX, nY);
          bChanged = 1;
        }
      }
    next:
      ++pwCell;
      ++pwMI;
      ++nX;
      ++pdwSquare;
      if (nX >= (int)gwMapWidth) {
        nX = 0;
        ++nY;
      }
    } while (nLeft);
  }
  if (bChanged)
    minimap_force_redraw();
}
