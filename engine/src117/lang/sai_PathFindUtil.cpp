// Scratch slice of sai_PathFindUtil.cpp: the bin table the area search uses to
// mask blocked directions. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/sai_PathFindUtil.h"
#include "../h/sai_PathFindArea.h"  // ours: path_search_t
#include "../h/sai.h"               // ours: the region table accessors
#include "../h/sai_PathCreate.h"    // ep_glob_t, for the tile map's offset
#include "../gen/globals117.h"
#include "../h/CUnitBounds.h"    // Hyperion: g_bFinderSideCheck, FinderSideDiffer

#include <string.h>              // memmove, memcpy

// Globals this TU reaches, dual-spelled, with one #ifdef per name.
//
// gwMapPixWidth @0x543230 is registered and arrives through globals117.h.

// 0x69B584 -- the pathing glob. re/globals117.json already names it epGlob;
// sub_492C00 reads the region map at +0x0C, 256 WORD entries per row.
#ifdef OC_DLL_MODE
#define epGlob (*reinterpret_cast<void **>(OC_VA(0x69B584u)))
#else
extern void *epGlob;
#endif

// 0x509400 and 0x509408 -- the four ring steps, {1,0,-1,0} and {0,1,0,-1} read
// out of .data. Both bodies walk them with one byte offset stepping 0,2,4,6.
#ifdef OC_DLL_MODE
#define square_x_g (reinterpret_cast<short*>(OC_VA(0x509400u)))
#else
extern short square_x_g[4];
#endif

#ifdef OC_DLL_MODE
#define square_y_g (reinterpret_cast<short*>(OC_VA(0x509408u)))
#else
extern short square_y_g[4];
#endif

// The bin table: the map is cut into vertical strips at dword_69EC08[i], strip i
// owns the record gBins[gBinIX[i]], and each record holds up to ten
// disjoint y spans sorted by nBottom ascending. sub_492DE0 builds it and
// sub_4930B0 reads it.
//
// All four extents are two-sided, each bracketed by the next name:
// 0x69EC08 + 0x204 = 0x69EE0C, 0x69EE10 + 0x208 = 0x69F018, and
// 0x69F018 + 128 * 0x54 = 0x6A1A18, the registered gScripts_g. The 129 and the
// 130 are extent-derived, not use-derived: only 128 strips are ever indexed,
// and the two slack slots are what the insert's memmove to &a[i+1] needs.
// dword_69EC08's element 1 at 0x69EC0C and gBinIX's at 0x69EE14 are
// elements, not globals: sub_492DE0 walks a running pointer comparing [eax+4].

// The record stride is `imul ecx, [...], 54h` in both bodies, and 0x54 is
// 4 + 10 * 8.
struct bin_t {
  /*0x00*/ int nSpans;
  struct span_t {
    /*0x00*/ int nTop;
    /*0x04*/ int nBottom;
  };
  /*0x04*/ span_t spans[10];
};

static_assert(sizeof(bin_t::span_t) == 8, "bin_t::span_t stride");
static_assert(offsetof(bin_t::span_t, nTop) == 0x00, "bin_t::span_t.nTop");
static_assert(offsetof(bin_t::span_t, nBottom) == 0x04, "bin_t::span_t.nBottom");
static_assert(sizeof(bin_t) == 0x54, "bin_t stride");
static_assert(offsetof(bin_t, nSpans) == 0x00, "bin_t.nSpans");
static_assert(offsetof(bin_t, spans) == 0x04, "bin_t.spans");

#ifdef OC_DLL_MODE
#define dword_69EC08 (reinterpret_cast<int*>(OC_VA(0x69EC08u)))
#else
extern int dword_69EC08[129] OC_UNALIGNED;
#endif

#ifdef OC_DLL_MODE
#define gNumBins (*reinterpret_cast<int*>(OC_VA(0x69EE0Cu)))
#else
extern int gNumBins;
#endif

#ifdef OC_DLL_MODE
#define gBinIX (reinterpret_cast<int*>(OC_VA(0x69EE10u)))
#else
extern int gBinIX[130];
#endif

// gBins has no registered type, so gen never emits it; this is its only spelling.
#ifdef OC_DLL_MODE
#define gBins (reinterpret_cast<bin_t *>(OC_VA(0x69F018u)))
#else
extern bin_t gBins[128] OC_UNALIGNED;
#endif

// Hyperion: a point's strip from the last point's. Both bodies below find the
// strip whose edges hold x by stepping from the first, past up to 128; the first edge past x
// from the second on is the strip's end. A search's points are near each other, so the steps
// start from the strip the last point was in: back while its start is past x, then on while
// the next edge is not. While the edges from the second to the last do not fall, the edges
// passed are all at x or before it and the strip found is the walk's. s_bBinsInOrder says
// they do not: ResetBins, which the local search calls before it reads or writes the table,
// sets it, and an edge a split writes out of order with its neighbours clears it, as does a
// rect starting at or past the last edge, whose walk 1.17 takes past the table's end. A point
// at or past the last edge is walked as 1.17 walks it. HYPERION_FINDER_CHECK=1 walks as well
// and compares.
static bool s_bBinsInOrder = false;
static int s_nBinsLast;  // the strip the last point was in

static int *BinsEdge(int x)
{
  const int nBins = gNumBins;
  int *pEdge = dword_69EC08;
  if (s_bBinsInOrder && nBins >= 1 && nBins <= 0x80 && x < dword_69EC08[nBins]) {
    int i = s_nBinsLast < nBins ? s_nBinsLast : nBins - 1;
    while (i > 0 && dword_69EC08[i] > x)
      --i;
    while (dword_69EC08[i + 1] <= x)
      ++i;
    s_nBinsLast = i;
    if (!g_bFinderSideCheck)
      return &dword_69EC08[i];
    while (x >= pEdge[1])
      ++pEdge;
    if (pEdge != &dword_69EC08[i])
      FinderSideDiffer("a strip's edge", static_cast<int>(pEdge - dword_69EC08));
    return pEdge;
  }
  while (x >= pEdge[1])
    ++pEdge;
  return pEdge;
}

// The edge at nAt written by a split: the edges stay in order if it is between its neighbours.
static void BinsEdgeWritten(int nAt)
{
  const int nBins = gNumBins;
  if (nAt < 1 || nAt > nBins || (nAt > 1 && dword_69EC08[nAt - 1] > dword_69EC08[nAt])
      || (nAt < nBins && dword_69EC08[nAt] > dword_69EC08[nAt + 1]))
    s_bBinsInOrder = false;
}

// The bodies, in oracle VA order.

// Collapse the bin table back to one strip covering the whole map width. The
// width is read into a local because the shipped form schedules that load after
// the first three stores otherwise.
void ResetBins(void)
{
  int nWidth = gwMapPixWidth;

  gNumBins = 1;
  dword_69EC08[0] = 0;
  gBinIX[0] = 0;
  dword_69EC08[1] = nWidth;
  gBinIX[1] = -1;
  gBins[0].nSpans = 0;
  s_bBinsInOrder = true;  // Hyperion
}

// Mask off the directions blocked at this point. A span that ends above the
// point sets the vertical bits, and a point sitting exactly on a strip edge
// also consults the neighbouring strip, which can clear them again.
void sub_4930B0(point16_t *pPt, BYTE *pMask)
{
  int x = pPt->x;
  int y = pPt->y;

  int *pEdge = BinsEdge(x);  // Hyperion: halved

  int i = (int)(pEdge - dword_69EC08);

  bin_t *pBin = &gBins[gBinIX[i]];
  int n = pBin->nSpans;
  if (n == 0)
    return;

  bin_t::span_t *pSpan = pBin->spans;
  while (n > 0 && y > pSpan->nBottom)
  {
    --n;
    ++pSpan;
  }

  if (n == 0)
    return;

  BYTE bMask = 0;
  if (pSpan->nTop >= y)
    bMask = 0x19;
  if (pSpan->nBottom <= y)
    bMask |= 0x46;

  if (pEdge[0] == x)
  {
    bMask |= 0x8C;
    if (i > 0)
    {
      bin_t *pNear = &gBins[gBinIX[i - 1]];
      int m = pNear->nSpans;
      if (m > 0)
      {
        bin_t::span_t *pS = pNear->spans;
        while (m > 0 && y > pS->nBottom)
        {
          --m;
          ++pS;
        }

        if (m > 0 && pS->nTop <= y && pS->nBottom >= y)
        {
          bMask &= 0x7F;
          if (pS->nTop < y)
            bMask &= 0xF7;
          if (pS->nBottom > y)
            bMask &= 0xFB;
        }
      }
    }
  }

  if (pEdge[1] - 1 == x)
  {
    bMask |= 0x23;
    if (i + 1 < gNumBins)
    {
      bin_t *pNear = &gBins[gBinIX[i + 1]];
      int m = pNear->nSpans;
      if (m > 0)
      {
        bin_t::span_t *pS = pNear->spans;
        while (m > 0 && y > pS->nBottom)
        {
          --m;
          ++pS;
        }

        if (m > 0 && pS->nTop <= y && pS->nBottom >= y)
        {
          bMask &= 0xDF;
          if (pS->nTop < y)
            bMask &= 0xFE;
          if (pS->nBottom > y)
            bMask &= 0xFD;
        }
      }
    }
  }

  *pMask &= bMask;
}

// Walk a widening square ring of tiles out from the point clamped into the
// region's bounds until one reports that region, and seat it as the new target.
// The clamped pair must be a point16_t: two short locals take a 4-byte stack
// home each and cost 16 B.
BOOL GetClosestReachable(path_search_t *pSearch, int nRegion)
{
  rect16_t rc;
  GetRegionBounds((WORD)nRegion, &rc);

  point16_t ptTile;
  {
    point16_t ptC;
    if (rc.top > pSearch->ptCur.y)
      ptC.y = rc.top;
    else
      ptC.y = rc.bottom < pSearch->ptCur.y ? static_cast<int>(rc.bottom) : static_cast<int>(pSearch->ptCur.y);
    if (rc.left > pSearch->ptCur.x)
      ptC.x = rc.left;
    else
      ptC.x = rc.right < pSearch->ptCur.x ? static_cast<int>(rc.right) : static_cast<int>(pSearch->ptCur.x);
    ptTile.x = (short)(ptC.x / 32);
    ptTile.y = (short)(ptC.y / 32);
  }

  for (int nRing = 1; nRing < 16; nRing += 2)
  {
    for (int nDir = 0; nDir < 4; ++nDir)
    {
      int i = 0;
      while (i < nRing)
      {
        if ((WORD)ptTile.x < gwMapWidth && (WORD)ptTile.y < gwMapHeight &&
            GetRegionFromPx(ptTile.x * 32, ptTile.y * 32) == (WORD)nRegion)
        {
          point16_t pt;
          pt.x = (short)(ptTile.x * 32 + 16);
          pt.y = (short)(ptTile.y * 32 + 16);
          pSearch->ptCur = pt;
          pSearch->bClosest = 1;
          return 1;
        }

        ++i;
        ptTile.x += square_x_g[nDir];
        ptTile.y += square_y_g[nDir];
      }

      ptTile.x -= square_x_g[nDir];
      ptTile.y -= square_y_g[nDir];
    }

    --ptTile.x;
    --ptTile.y;
  }

  GetRegionPosition((WORD)nRegion, &pSearch->ptCur);
  pSearch->bClosest = 1;
  return 1;
}

// Add one rectangle to the bin table: split the strips it starts and ends in,
// then merge its y span into each strip's list, coalescing any spans the merge
// joined. Returns 0 when a strip, a bin or a span list is full.
// The coalesce reads the next span's bottom through a cursor of its own, and
// its step back is written in both arms of a test, which MSVC merges.
BOOL sub_492DE0(rect16_t *pRect)
{
  int x = pRect->left;
  int nRight = pRect->right;
  int *pEdge;
  int i, n, nBins, nSize;
  bin_t *pB;
  bin_t::span_t *pS;

  // Hyperion: halved; a rect from the last edge on is walked past the table
  if (gNumBins < 1 || gNumBins > 0x80 || x >= dword_69EC08[gNumBins])
    s_bBinsInOrder = false;
  pEdge = BinsEdge(x);
  i = (int)(pEdge - dword_69EC08);
  pB = &gBins[gBinIX[i]];

  if (*pEdge < x) {
    if (gNumBins == 0x80)
      return 0;
    nSize = (gNumBins - i) * 4 + 4;
    memmove(pEdge + 1, pEdge, nSize);
    memmove(&gBinIX[i + 1], &gBinIX[i], nSize);
    bin_t *pNew = &gBins[gNumBins];
    gBinIX[i] = gNumBins;
    nBins = ++gNumBins;
    memcpy(pNew, pB, pB->nSpans * 8 + 4);
    ++i;
    *++pEdge = x;
    BinsEdgeWritten(i);  // Hyperion
  }
  else
    nBins = gNumBins;

  while (x <= nRight) {
    pB = &gBins[gBinIX[i]];
    if (pEdge[1] - 1 > nRight) {
      if (nBins == 0x80)
        return 0;
      nSize = (nBins - i) * 4 + 4;
      memmove(pEdge + 1, pEdge, nSize);
      memmove(&gBinIX[i + 1], &gBinIX[i], nSize);
      gBinIX[i + 1] = gNumBins;
      bin_t *pNew = &gBins[gNumBins];
      nBins = ++gNumBins;
      memcpy(pNew, pB, pB->nSpans * 8 + 4);
      pEdge[1] = nRight + 1;
      BinsEdgeWritten(i + 1);  // Hyperion
    }

    int nTop = pRect->top;
    int nBottom = pRect->bottom;
    pS = pB->spans;
    for (n = pB->nSpans; n > 0; --n, ++pS)
      if (nTop <= pS->nBottom + 1)
        break;

    if (n == 0) {
      if (pB->nSpans == 10)
        return 0;
      pS->nTop = nTop;
      pS->nBottom = nBottom;
      ++pB->nSpans;
    } else if (nBottom >= pS->nTop - 1 && nTop <= pS->nBottom + 1) {
      if (pS->nTop > nTop)
        pS->nTop = nTop;
      if (pS->nBottom < nBottom)
        pS->nBottom = nBottom;
      n = pB->nSpans - 1;
      if (n > 0) {
        pS = pB->spans;
        int *pNextBottom = &pS[1].nBottom;
        for (; n > 0; --n) {
          if (pS[1].nTop - 1 <= pS->nBottom) {
            if (pS->nBottom < *pNextBottom)
              pS->nBottom = *pNextBottom;
            --pB->nSpans;
            if (n > 1)
              memmove(&pS[1], &pS[2], (n - 1) * 8);
            pNextBottom -= 2;
            if (gPlayback) --pS; else --pS;
          }
          ++pS;
          pNextBottom += 2;
        }
        nBins = gNumBins;
      }
    } else {
      if (pB->nSpans == 10)
        return 0;
      memmove(pS + 1, pS, n * 8);
      pS->nTop = nTop;
      pS->nBottom = nBottom;
      ++pB->nSpans;
      nBins = gNumBins;
    }

    x = *++pEdge;
    ++i;
  }
  return 1;
}

// Walk a widening square ring in 8-pixel steps until the unit's collision box
// fits on tiles the region map does not call inaccessible, and leave that
// position in the caller's point. The box is a rect16_t: short locals add a
// movzx per corner. bOk is set after the three divisions, which fixes ESI/EDI.
BOOL sub_492C00(path_search_t *pSearch, point16_t *pPt)
{
  mrect_t  rc = pSearch->rcBound;
  void *pGlob = epGlob;

  for (int nRing = 1; nRing < 10; nRing += 2)
  {
    for (int nDir = 0; nDir < 4; ++nDir)
    {
      int i = 0;
      while (i < nRing)
      {
        rect16_t rcBox;
        rcBox.top = pPt->y + rc.top;
        rcBox.left = pPt->x + rc.left;
        rcBox.bottom = pPt->y + rc.bottom;
        rcBox.right = pPt->x + rc.right;

        if (PosU(rcBox.left) < gwMapPixWidth && PosU(rcBox.top) < gwMapPixHeight &&
            PosU(rcBox.right) < gwMapPixWidth && PosU(rcBox.bottom) < gwMapPixHeight)
        {
          int nTileY = rcBox.top / 32;
          int nTileX = rcBox.left / 32;
          int nRight = rcBox.right / 32;
          BOOL bOk = 1;
          WORD *pRow = reinterpret_cast<WORD *>(static_cast<BYTE *>(pGlob) + offsetof(ep_glob_t, wMap))
                       + nTileY * EP_STRIDE + nTileX;  // Hyperion: 256 in 1.17
          int nRows = rcBox.bottom / 32 - nTileY;

          while (bOk && nRows >= 0)
          {
            int nCols = nRight - nTileX;
            while (bOk && nCols >= 0)
            {
              if (IsValidInaccessibleRegion(pRow[nCols]))
                bOk = 0;
              --nCols;
            }

            --nRows;
            pRow += EP_STRIDE;
          }

          if (bOk)
            return 1;
        }

        ++i;
        pPt->x = (short)(pPt->x + square_x_g[nDir] * 8);
        pPt->y = (short)(pPt->y + square_y_g[nDir] * 8);
      }

      pPt->x = (short)(pPt->x + -square_x_g[nDir] * 8);
      pPt->y = (short)(pPt->y + -square_y_g[nDir] * 8);
    }

    pPt->x -= 8;
    pPt->y -= 8;
  }

  return 0;
}
