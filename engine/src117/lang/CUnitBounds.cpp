// CUnitBounds.cpp -- unit collision bounds tracking lists: the two position-sort
// orderings, the six primitives that open, close and shift a slot in them, and
// the CUnit members that enter, move and take a unit out of them. Bodies are in
// oracle address order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>                 // memmove -- the tracking shifts
#include <stdio.h>                  // Hyperion: HYPERION_FINDER_CHECK's log
#include <stdlib.h>                 // Hyperion: getenv
#include <algorithm>                // Hyperion: std::sort

#include "../h/CUnitBounds.h"
#include "../h/CSprite.h"
#include "../h/collide.h"
#include "../h/point16.h"
#include "../h/rect16.h"
#include "../h/CUnit.h"
#include "../gen/globals117.h"

// The globals this TU reaches, each dual-spelled under its own #ifdef rather
// than one guard over the block. CUnit__nSorted is the live entry count and is
// reset with the two arrays; their entry type lives in CUnitBounds.h, next to the
// six tracking functions that take one.
#ifndef OC_DLL_MODE
extern pos_sort_ent_t g_pos_sort_x[POOL_FINDER_STORE];
#endif
#ifdef OC_DLL_MODE
#define g_pos_sort_x (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5C99B8u)))
#endif
#ifndef OC_DLL_MODE
extern pos_sort_ent_t g_pos_sort_y[POOL_FINDER_STORE];
#endif
#ifdef OC_DLL_MODE
#define g_pos_sort_y (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5D03F8u)))
#endif
#ifndef OC_DLL_MODE
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;
#endif
#ifdef OC_DLL_MODE
#define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
#endif
#ifdef OC_DLL_MODE
#define CUnit__nSorted (*reinterpret_cast<DWORD *>(OC_VA(0x5C99B0u)))
#else
extern DWORD CUnit__nSorted;
#endif

// CUnit +0x13C/+0x140/+0x144/+0x148, the four DWORD indices into those arrays,
// reached by offset because CUnit.h still spells the range `_unmapped_138[0x18]`
// (the derivation is recorded on that placeholder). Same spelling as
// collide.cpp so the two TUs read the field the same way.
#define UNIT_BOUND_LEFT(u)   (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x04))
#define UNIT_BOUND_RIGHT(u)  (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x08))
#define UNIT_BOUND_TOP(u)    (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x0C))
#define UNIT_BOUND_BOTTOM(u) (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x10))

// Hyperion: the orderings' sides (CUnitBounds.h)
finder_side_t g_finder_side_x[POOL_FINDER_STORE];
finder_side_t g_finder_side_y[POOL_FINDER_STORE];
bool g_bFinderSideCheck = getenv("HYPERION_FINDER_CHECK") != NULL;

// the grid (FinderGridRect): each unit's extent and square, by one-based id, and each square's
// first unit
enum { FINDER_GRID_SHIFT = 7, FINDER_GRID_SIDE = 256 };
struct finder_cell_t {
  int nLeft, nRight, nTop, nBottom;
  int nCell;  // its square + 1, 0 none
  int nPrev, nNext;
  int nUnused;
};
static finder_cell_t g_finderUnit[POOL_UNITS_STORE + 1];
static int g_finderGrid[FINDER_GRID_SIDE * FINDER_GRID_SIDE];
static int g_nFinderMostW, g_nFinderMostH;  // the widest and tallest unit seen since the map began
static int g_nFinderMostAbs;                 // the largest magnitude of an edge since then

static int FinderGridSquare(int n)
{
  n >>= FINDER_GRID_SHIFT;
  return n < 0 ? 0 : n >= FINDER_GRID_SIDE ? FINDER_GRID_SIDE - 1 : n;
}

static void FinderGridTake(int nId)
{
  finder_cell_t &u = g_finderUnit[nId];
  if (u.nCell == 0)
    return;
  if (u.nPrev != 0)
    g_finderUnit[u.nPrev].nNext = u.nNext;
  else
    g_finderGrid[u.nCell - 1] = u.nNext;
  if (u.nNext != 0)
    g_finderUnit[u.nNext].nPrev = u.nPrev;
  u.nCell = u.nPrev = u.nNext = 0;
}

static void FinderGridPut(int nId, int nLeft, int nRight, int nTop, int nBottom)
{
  finder_cell_t &u = g_finderUnit[nId];
  u.nLeft = nLeft;
  u.nRight = nRight;
  u.nTop = nTop;
  u.nBottom = nBottom;
  if (nRight - nLeft > g_nFinderMostW)
    g_nFinderMostW = nRight - nLeft;
  if (nBottom - nTop > g_nFinderMostH)
    g_nFinderMostH = nBottom - nTop;
  const int anEdge[4] = {nLeft, nRight, nTop, nBottom};
  for (int k = 0; k < 4; ++k) {
    const int nAbs = anEdge[k] < 0 ? -anEdge[k] : anEdge[k];
    if (nAbs > g_nFinderMostAbs)
      g_nFinderMostAbs = nAbs;
  }
  const int nCell = FinderGridSquare(nTop) * FINDER_GRID_SIDE + FinderGridSquare(nLeft) + 1;
  if (u.nCell == nCell)
    return;
  FinderGridTake(nId);
  u.nCell = nCell;
  u.nPrev = 0;
  u.nNext = g_finderGrid[nCell - 1];
  if (u.nNext != 0)
    g_finderUnit[u.nNext].nPrev = nId;
  g_finderGrid[nCell - 1] = nId;
}

static void FinderGridClear()
{
  memset(g_finderGrid, 0, sizeof g_finderGrid);
  memset(g_finderUnit, 0, sizeof g_finderUnit);
  g_nFinderMostW = g_nFinderMostH = g_nFinderMostAbs = 0;
}

int FinderMostAbs()
{
  return g_nFinderMostAbs;
}

int FinderGridEdges(int bY, int bHigh, int nFrom, int nTo, int nOtherLo, int nOtherHi, int *pOut)
{
  int nLeft0, nLeft1, nTop0, nTop1;
  if (bY) {
    nTop0 = bHigh ? nFrom - g_nFinderMostH : nFrom;
    nTop1 = nTo;
    nLeft0 = nOtherLo - g_nFinderMostW;
    nLeft1 = nOtherHi;
  } else {
    nLeft0 = bHigh ? nFrom - g_nFinderMostW : nFrom;
    nLeft1 = nTo;
    nTop0 = nOtherLo - g_nFinderMostH;
    nTop1 = nOtherHi;
  }
  if (nLeft0 > nLeft1 || nTop0 > nTop1)
    return 0;
  int n = 0;
  const int cx0 = FinderGridSquare(nLeft0), cx1 = FinderGridSquare(nLeft1);
  const int cy0 = FinderGridSquare(nTop0), cy1 = FinderGridSquare(nTop1);
  for (int cy = cy0; cy <= cy1; ++cy)
    for (int cx = cx0; cx <= cx1; ++cx)
      for (int nId = g_finderGrid[cy * FINDER_GRID_SIDE + cx]; nId != 0; nId = g_finderUnit[nId].nNext) {
        const finder_cell_t &u = g_finderUnit[nId];
        const int nEdge = bY ? (bHigh ? u.nBottom : u.nTop) : (bHigh ? u.nRight : u.nLeft);
        if (nEdge < nFrom || nEdge > nTo)
          continue;
        CUnit *pUnit = CUnit::Head() + (nId - 1);
        pOut[n++] = bY ? (bHigh ? UNIT_BOUND_BOTTOM(pUnit) : UNIT_BOUND_TOP(pUnit))
                       : (bHigh ? UNIT_BOUND_RIGHT(pUnit) : UNIT_BOUND_LEFT(pUnit));
      }
  std::sort(pOut, pOut + n);
  return n;
}

int FinderGridEntries(int nX0, int nX1, int nY0, int nY1, finder_kept_t *pOut)
{
  // an entry in a span has its coordinate between the span's first and last, the orderings
  // being in order, so the unit's top left corner is within the widest and tallest unit of it
  const int nLeft0 = g_pos_sort_x[nX0].nCoord - g_nFinderMostW, nLeft1 = g_pos_sort_x[nX1 - 1].nCoord;
  const int nTop0 = g_pos_sort_y[nY0].nCoord - g_nFinderMostH, nTop1 = g_pos_sort_y[nY1 - 1].nCoord;
  int n = 0;
  const int cx0 = FinderGridSquare(nLeft0), cx1 = FinderGridSquare(nLeft1);
  const int cy0 = FinderGridSquare(nTop0), cy1 = FinderGridSquare(nTop1);
  for (int cy = cy0; cy <= cy1; ++cy)
    for (int cx = cx0; cx <= cx1; ++cx)
      for (int nId = g_finderGrid[cy * FINDER_GRID_SIDE + cx]; nId != 0; nId = g_finderUnit[nId].nNext) {
        CUnit *pUnit = CUnit::Head() + (nId - 1);
        const int nL = UNIT_BOUND_LEFT(pUnit), nR = UNIT_BOUND_RIGHT(pUnit);
        const bool bL = nL >= nX0 && nL < nX1, bR = nR >= nX0 && nR < nX1;
        if (!bL && !bR)
          continue;
        const int nT = UNIT_BOUND_TOP(pUnit), nB = UNIT_BOUND_BOTTOM(pUnit);
        if (!(nT >= nY0 && nT < nY1) && !(nB >= nY0 && nB < nY1))
          continue;
        pOut[n].nKey = bL && bR ? (nL < nR ? nL : nR) : bL ? nL : nR;
        pOut[n].nId = nId;
        ++n;
      }
  return n;
}

int FinderGridRect(int nLeft, int nTop, int nRight, int nBottom, finder_kept_t *pOut)
{
  int n = 0;
  const int cx0 = FinderGridSquare(nLeft - g_nFinderMostW), cx1 = FinderGridSquare(nRight - 1);
  const int cy0 = FinderGridSquare(nTop - g_nFinderMostH), cy1 = FinderGridSquare(nBottom - 1);
  for (int cy = cy0; cy <= cy1; ++cy)
    for (int cx = cx0; cx <= cx1; ++cx)
      for (int nId = g_finderGrid[cy * FINDER_GRID_SIDE + cx]; nId != 0; nId = g_finderUnit[nId].nNext) {
        const finder_cell_t &u = g_finderUnit[nId];
        const bool bLeft = u.nLeft >= nLeft && u.nLeft < nRight;
        const bool bRight = u.nRight >= nLeft && u.nRight < nRight;
        if ((!bLeft && !bRight) || u.nBottom < nTop || u.nTop >= nBottom)
          continue;
        CUnit *pUnit = CUnit::Head() + (nId - 1);
        int nKey = bLeft ? UNIT_BOUND_LEFT(pUnit) : UNIT_BOUND_RIGHT(pUnit);
        if (bLeft && bRight && UNIT_BOUND_RIGHT(pUnit) < nKey)
          nKey = UNIT_BOUND_RIGHT(pUnit);
        pOut[n].nKey = nKey;
        pOut[n].nId = nId;
        ++n;
      }
  return n;
}

static void FinderSideSwap(finder_side_t *pSide, int a, int b)
{
  const finder_side_t t = pSide[a];
  pSide[a] = pSide[b];
  pSide[b] = t;
}

// The unit's four sides from its coordinates in the orderings now.
void FinderSideUnit(CUnit *pUnit)
{
  if (UNIT_BOUND_LEFT(pUnit) < 0)
    return;
  const int nLeft = g_pos_sort_x[UNIT_BOUND_LEFT(pUnit)].nCoord;
  const int nRight = g_pos_sort_x[UNIT_BOUND_RIGHT(pUnit)].nCoord;
  const int nTop = g_pos_sort_y[UNIT_BOUND_TOP(pUnit)].nCoord;
  const int nBottom = g_pos_sort_y[UNIT_BOUND_BOTTOM(pUnit)].nCoord;
  const finder_side_t x0 = {nTop, nBottom, 0, 0}, x1 = {nTop, nBottom, 1, 0};
  const finder_side_t y0 = {nLeft, nRight, 0, 0}, y1 = {nLeft, nRight, 1, 0};
  g_finder_side_x[UNIT_BOUND_LEFT(pUnit)] = x0;
  g_finder_side_x[UNIT_BOUND_RIGHT(pUnit)] = x1;
  g_finder_side_y[UNIT_BOUND_TOP(pUnit)] = y0;
  g_finder_side_y[UNIT_BOUND_BOTTOM(pUnit)] = y1;
  FinderGridPut(static_cast<int>(pUnit - CUnit::Head()) + 1, nLeft, nRight, nTop, nBottom);
}

// Every side again, from the orderings and the units' indices: after a snapshot's restore,
// which puts the orderings back as they were and not through the functions below.
void FinderSideRebuild()
{
  FinderGridClear();
  for (int i = 0; i < (int)CUnit__nSorted; ++i) {
    if (g_pos_sort_x[i].dwUnitId != 0) {
      CUnit *pUnit = CUnit::Head() + (g_pos_sort_x[i].dwUnitId - 1);
      if (UNIT_BOUND_LEFT(pUnit) == i)
        FinderSideUnit(pUnit);
    }
  }
}

static unsigned long g_nFinderChecked, g_nFinderDiffer;

void FinderSideDiffer(const char *pszWhat, int nIdx)
{
  if (++g_nFinderDiffer > 1000)
    return;
  if (FILE *f = fopen("hyperion_finder_check.log", "a")) {
    fprintf(f, "%s differs at %d\n", pszWhat, nIdx);
    fclose(f);
  }
}

void FinderSideVerify(int bY, int nIdx)
{
  const pos_sort_ent_t &ent = (bY ? g_pos_sort_y : g_pos_sort_x)[nIdx];
  const finder_side_t &side = (bY ? g_finder_side_y : g_finder_side_x)[nIdx];
  if (ent.dwUnitId != 0) {
    CUnit *pUnit = CUnit::Head() + (ent.dwUnitId - 1);
    bool bSame;
    if (bY)
      bSame = side.nLow == g_pos_sort_x[UNIT_BOUND_LEFT(pUnit)].nCoord &&
              side.nHigh == g_pos_sort_x[UNIT_BOUND_RIGHT(pUnit)].nCoord &&
              side.bHigh == (UNIT_BOUND_BOTTOM(pUnit) == nIdx ? 1 : 0) &&
              (UNIT_BOUND_TOP(pUnit) == nIdx || UNIT_BOUND_BOTTOM(pUnit) == nIdx);
    else
      bSame = side.nLow == g_pos_sort_y[UNIT_BOUND_TOP(pUnit)].nCoord &&
              side.nHigh == g_pos_sort_y[UNIT_BOUND_BOTTOM(pUnit)].nCoord &&
              side.bHigh == (UNIT_BOUND_RIGHT(pUnit) == nIdx ? 1 : 0) &&
              (UNIT_BOUND_LEFT(pUnit) == nIdx || UNIT_BOUND_RIGHT(pUnit) == nIdx);
    if (!bSame)
      FinderSideDiffer(bY ? "a side beside g_pos_sort_y" : "a side beside g_pos_sort_x", nIdx);
  }
  if ((++g_nFinderChecked & 0xFFFFF) == 0) {
    if (FILE *f = fopen("hyperion_finder_check.log", "a")) {
      fprintf(f, "%lu checked, %lu differ\n", g_nFinderChecked, g_nFinderDiffer);
      fclose(f);
    }
  }
}

// The test is spelled `== 0`, not `!= 0`: the oracle falls through into the
// zero arm and jumps over it to the call. The `!= 0` spelling compiles the same
// logic with the arms exchanged, a 2-byte delta at +0x25.
static CUnit *unit_from_bound_id(DWORD dwUnitId)
{
  return dwUnitId == 0 ? 0 : CUnit::Head() + dwUnitId - 1;
}

// Opens a slot at nLo in the X ordering: renumbers whichever of the owning
// unit's two X indices still points at an entry about to move, then shifts
// [nLo, nHi) up by one. The renumber is `++`, not `= i + 1` -- the oracle
// reuses the value it just compared. The `if / else if` is the oracle's shape;
// the left arm jumps over the right test rather than falling into it.
void NewTrackingX(pos_sort_ent_t *pArr, int nLo, int nHi)
{
  int i;

  if (nLo >= nHi)
    return;

  for (i = nHi - 1; i >= nLo; --i)
  {
    CUnit *pUnit = unit_from_bound_id(pArr[i].dwUnitId);

    if (UNIT_BOUND_LEFT(pUnit) == i)
      ++UNIT_BOUND_LEFT(pUnit);
    else if (UNIT_BOUND_RIGHT(pUnit) == i)
      ++UNIT_BOUND_RIGHT(pUnit);
  }

  memmove(&pArr[nLo + 1], &pArr[nLo], (nHi - nLo) * sizeof(pos_sort_ent_t));
  memmove(&g_finder_side_x[nLo + 1], &g_finder_side_x[nLo], (nHi - nLo) * sizeof(finder_side_t));  // Hyperion
}

// The X function with the other pair of indices. Same shape, +0x144/+0x148.
void NewTrackingY(pos_sort_ent_t *pArr, int nLo, int nHi)
{
  int i;

  if (nLo >= nHi)
    return;

  for (i = nHi - 1; i >= nLo; --i)
  {
    CUnit *pUnit = unit_from_bound_id(pArr[i].dwUnitId);

    if (UNIT_BOUND_TOP(pUnit) == i)
      ++UNIT_BOUND_TOP(pUnit);
    else if (UNIT_BOUND_BOTTOM(pUnit) == i)
      ++UNIT_BOUND_BOTTOM(pUnit);
  }

  memmove(&pArr[nLo + 1], &pArr[nLo], (nHi - nLo) * sizeof(pos_sort_ent_t));
  memmove(&g_finder_side_y[nLo + 1], &g_finder_side_y[nLo], (nHi - nLo) * sizeof(finder_side_t));  // Hyperion
}

// Closes the slot at nLo: the mirror of NewTrackingX. The walk runs upward and
// the renumber is `--`, and the memmove shifts down rather than up.
void DeleteTrackingX(pos_sort_ent_t *pArr, int nLo, int nHi)
{
  int i;

  if (nLo >= nHi)
    return;

  for (i = nLo + 1; i <= nHi; ++i)
  {
    CUnit *pUnit = unit_from_bound_id(pArr[i].dwUnitId);

    if (UNIT_BOUND_LEFT(pUnit) == i)
      --UNIT_BOUND_LEFT(pUnit);
    else if (UNIT_BOUND_RIGHT(pUnit) == i)
      --UNIT_BOUND_RIGHT(pUnit);
  }

  memmove(&pArr[nLo], &pArr[nLo + 1], (nHi - nLo) * sizeof(pos_sort_ent_t));
  memmove(&g_finder_side_x[nLo], &g_finder_side_x[nLo + 1], (nHi - nLo) * sizeof(finder_side_t));  // Hyperion
}

// The X function with the other pair of indices. Same shape, +0x144/+0x148.
void DeleteTrackingY(pos_sort_ent_t *pArr, int nLo, int nHi)
{
  int i;

  if (nLo >= nHi)
    return;

  for (i = nLo + 1; i <= nHi; ++i)
  {
    CUnit *pUnit = unit_from_bound_id(pArr[i].dwUnitId);

    if (UNIT_BOUND_TOP(pUnit) == i)
      --UNIT_BOUND_TOP(pUnit);
    else if (UNIT_BOUND_BOTTOM(pUnit) == i)
      --UNIT_BOUND_BOTTOM(pUnit);
  }

  memmove(&pArr[nLo], &pArr[nLo + 1], (nHi - nLo) * sizeof(pos_sort_ent_t));
  memmove(&g_finder_side_y[nLo], &g_finder_side_y[nLo + 1], (nHi - nLo) * sizeof(finder_side_t));  // Hyperion
}

// Moves one entry nDelta places and carries the owning unit's index with it,
// then exchanges the two entries. Only the index that still points at nIdx is
// adjusted, and it moves by nDelta rather than by one. pOther is computed
// before the two temporaries are read; reading them first costs the oracle's
// register assignment for the whole exchange.
void TrackXShift(pos_sort_ent_t *pArr, int nIdx, int nDelta)
{
  CUnit *pUnit = unit_from_bound_id(pArr[nIdx].dwUnitId);
  int nCoord;
  DWORD dwUnitId;
  pos_sort_ent_t *pOther;

  if (UNIT_BOUND_LEFT(pUnit) == nIdx)
    UNIT_BOUND_LEFT(pUnit) += nDelta;
  else if (UNIT_BOUND_RIGHT(pUnit) == nIdx)
    UNIT_BOUND_RIGHT(pUnit) += nDelta;
  FinderSideSwap(g_finder_side_x, nIdx, nIdx + nDelta);  // Hyperion

  pOther = &pArr[nIdx + nDelta];
  nCoord = pArr[nIdx].nCoord;
  dwUnitId = pArr[nIdx].dwUnitId;
  pArr[nIdx].dwUnitId = pOther->dwUnitId;
  pArr[nIdx].nCoord = pOther->nCoord;
  pOther->dwUnitId = dwUnitId;
  pOther->nCoord = nCoord;
}

// The X function with the other pair of indices. Same shape, +0x144/+0x148.
void TrackYShift(pos_sort_ent_t *pArr, int nIdx, int nDelta)
{
  CUnit *pUnit = unit_from_bound_id(pArr[nIdx].dwUnitId);
  int nCoord;
  DWORD dwUnitId;
  pos_sort_ent_t *pOther;

  if (UNIT_BOUND_TOP(pUnit) == nIdx)
    UNIT_BOUND_TOP(pUnit) += nDelta;
  else if (UNIT_BOUND_BOTTOM(pUnit) == nIdx)
    UNIT_BOUND_BOTTOM(pUnit) += nDelta;
  FinderSideSwap(g_finder_side_y, nIdx, nIdx + nDelta);  // Hyperion

  pOther = &pArr[nIdx + nDelta];
  nCoord = pArr[nIdx].nCoord;
  dwUnitId = pArr[nIdx].dwUnitId;
  pArr[nIdx].dwUnitId = pOther->dwUnitId;
  pArr[nIdx].nCoord = pOther->nCoord;
  pOther->dwUnitId = dwUnitId;
  pOther->nCoord = nCoord;
}

// Puts the unit into both collision structures. The building arm passes the
// sprite's position, and the flying arm is a tail call.
void CUnit::TrackCollision()
{
  BeginTracking();
  if (dwFlags & UF_Building)
    PlaceBldg(pSprite->wPosX, pSprite->wPosY);
  UpdateUnitCollision();
  if (dwFlags & 4)
    AddToRepulseChunk();
}

// The mirror of TrackCollision: no UpdateUnitCollision leg on the way out.
void CUnit::UntrackCollision()
{
  RemoveFromFinder();
  if (dwFlags & UF_Building)
    UnplaceBldg(pSprite->wPosX, pSprite->wPosY);
  if (dwFlags & 4)
    RemoveFromRepulseChunk();
}

// Marks the building's footprint in the collision matrix. The footprint is the
// placebox in cells, and the position is the box's top-left corner.
void CUnit::PlaceBldg(upos_t wX, upos_t wY)
{
  int nW = gUnitPlaceboxSize[wType].x / 32;
  int nH = gUnitPlaceboxSize[wType].y / 32;

  mtx_place_bldg(PosS(PosS(wX) / 32) - nW / 2,
                   PosS(PosS(wY) / 32) - nH / 2,
                   nW, nH, 0x8000000);
}

// PlaceBldg with the clearing call.
void CUnit::UnplaceBldg(upos_t wX, upos_t wY)
{
  int nW = gUnitPlaceboxSize[wType].x / 32;
  int nH = gUnitPlaceboxSize[wType].y / 32;

  mtx_unplace_bldg(PosS(PosS(wX) / 32) - nW / 2,
                   PosS(PosS(wY) / 32) - nH / 2,
                   nW, nH, 0x8000000);
}

// Clears both orderings to -1 and empties the count, then hands off to the
// finder's own init. The two memsets share one stack cleanup.
void cunit_bounds_init()
{
  CUnit__nSorted = 0;
  FinderGridClear();  // Hyperion
  memset(g_pos_sort_x, -1, g_pools.finder * sizeof(g_pos_sort_x[0]));
  memset(g_pos_sort_y, -1, g_pools.finder * sizeof(g_pos_sort_y[0]));
  cunitfind_init();
}

// Rectangle overlap in the two orderings. Three early refusals and a fourth
// comparison as the result; the senses are the oracle's and are not symmetric.
BOOL CUnit::OverlapsWith(CUnit *pOther)
{
  if (g_pos_sort_x[UNIT_BOUND_RIGHT(this)].nCoord
      < g_pos_sort_x[UNIT_BOUND_LEFT(pOther)].nCoord)
    return 0;

  if (g_pos_sort_x[UNIT_BOUND_LEFT(this)].nCoord
      > g_pos_sort_x[UNIT_BOUND_RIGHT(pOther)].nCoord)
    return 0;

  if (g_pos_sort_y[UNIT_BOUND_BOTTOM(this)].nCoord
      < g_pos_sort_y[UNIT_BOUND_TOP(pOther)].nCoord)
    return 0;

  return g_pos_sort_y[UNIT_BOUND_TOP(this)].nCoord
         <= g_pos_sort_y[UNIT_BOUND_BOTTOM(pOther)].nCoord;
}

// Takes both of the unit's bound pairs out of both orderings, high edge first,
// and marks the four indices free. The count drops once per pair, before the
// pair is deleted, so the deleted index is always the last live one.
void CUnit::RemoveFromFinder()
{
  if (UNIT_BOUND_LEFT(this) < 0)
    return;
  FinderGridTake(static_cast<int>(this - CUnit::Head()) + 1);  // Hyperion

  --CUnit__nSorted;
  DeleteTrackingX(g_pos_sort_x, UNIT_BOUND_RIGHT(this), CUnit__nSorted);
  DeleteTrackingY(g_pos_sort_y, UNIT_BOUND_BOTTOM(this), CUnit__nSorted);
  UNIT_BOUND_RIGHT(this) = -1;
  UNIT_BOUND_BOTTOM(this) = -1;

  --CUnit__nSorted;
  DeleteTrackingX(g_pos_sort_x, UNIT_BOUND_LEFT(this), CUnit__nSorted);
  DeleteTrackingY(g_pos_sort_y, UNIT_BOUND_TOP(this), CUnit__nSorted);
  UNIT_BOUND_LEFT(this) = -1;
  UNIT_BOUND_TOP(this) = -1;
}

// Binary search for the insertion point of a coordinate in one ordering. The
// key is doubled and nudged by one so an exact hit lands on the near or the far
// side of an equal run depending on nFlag; the entries are doubled to match.
// The midpoint is recomputed in both arms, the else arm's half spelled `nMid + 1`
// (hoisting it costs two bytes, nLo three). The empty test reads the global and
// exits through nRes, which BeginTracking's inlined copies need.
int SearchFinder(pos_sort_ent_t *pArr, int nCoord, int nFlag)
{
  DWORD nHi;
  DWORD nLo;
  DWORD nMid;
  int nKey;
  DWORD nRes;

  if (CUnit__nSorted == 0) {
    nRes = 0;
    goto done;
  }
  nHi = CUnit__nSorted;

  nKey = nCoord * 2 + (nFlag == 0 ? 1 : -1);
  nLo = 0;
  nMid = nHi >> 1;

  while (nLo < nHi)
  {
    if (nKey < pArr[nMid].nCoord * 2)
    {
      nHi = nMid;
      nMid = (nLo + nHi) >> 1;
    }
    else
    {
      nLo = nMid + 1;
      nMid = (nMid + 1 + nHi) >> 1;
    }
  }

  nRes = nHi;
done:
  return nRes;
}

// Inserts the four bound edges into the two orderings, low edges first, the
// count rising after each axis pair. Each coordinate is stored through a
// re-read of the index field, not through the local, and the count rises after
// that store.
void CUnit::BeginTracking()
{
  DWORD_PTR nUnitId = this == 0 ? 0 : (this - CUnit::Head()) + 1;
  int nLeft, nTop, nRight, nBottom;
  int nIdx;

  if (gulUnitTypeFlags[wType] & 0x10)
    return;

  nLeft = PosS(pSprite->wPosX) - gUnitBoundBox[wType].left;
  nTop = PosS(pSprite->wPosY) - gUnitBoundBox[wType].top;

  nIdx = SearchFinder(g_pos_sort_x, nLeft, 1);
  NewTrackingX(g_pos_sort_x, nIdx, CUnit__nSorted);
  UNIT_BOUND_LEFT(this) = nIdx;
  g_pos_sort_x[nIdx].dwUnitId = (DWORD)nUnitId;
  g_pos_sort_x[UNIT_BOUND_LEFT(this)].nCoord = nLeft;

  nIdx = SearchFinder(g_pos_sort_y, nTop, 1);
  NewTrackingY(g_pos_sort_y, nIdx, CUnit__nSorted);
  UNIT_BOUND_TOP(this) = nIdx;
  g_pos_sort_y[nIdx].dwUnitId = (DWORD)nUnitId;
  g_pos_sort_y[UNIT_BOUND_TOP(this)].nCoord = nTop;
  ++CUnit__nSorted;

  nRight = gUnitBoundBox[wType].right + PosS(pSprite->wPosX);
  nBottom = gUnitBoundBox[wType].bottom + PosS(pSprite->wPosY);

  nIdx = SearchFinder(g_pos_sort_x, nRight, 1);
  NewTrackingX(g_pos_sort_x, nIdx, CUnit__nSorted);
  UNIT_BOUND_RIGHT(this) = nIdx;
  g_pos_sort_x[nIdx].dwUnitId = (DWORD)nUnitId;
  // Identical arms merge after allocation; the second nRight reference puts it in EBX above the count.
  if (gPlayback) g_pos_sort_x[UNIT_BOUND_RIGHT(this)].nCoord = nRight; else g_pos_sort_x[UNIT_BOUND_RIGHT(this)].nCoord = nRight;

  nIdx = SearchFinder(g_pos_sort_y, nBottom, 1);
  NewTrackingY(g_pos_sort_y, nIdx, CUnit__nSorted);
  UNIT_BOUND_BOTTOM(this) = nIdx;
  g_pos_sort_y[nIdx].dwUnitId = (DWORD)nUnitId;
  g_pos_sort_y[UNIT_BOUND_BOTTOM(this)].nCoord = nBottom;
  ++CUnit__nSorted;
  FinderSideUnit(this);  // Hyperion
}

// Moves the unit's four bound edges by (nX, nY) and bubbles each back into
// order: the leading edge first in the direction of travel. The forward walks
// inline TrackXShift / TrackYShift and the backward walks call them, which is
// the oracle's own split. Each new coordinate is summed in a register and
// stored, not added in place.
void CUnit::CoordSort(int nX, int nY)
{
  if (UNIT_BOUND_LEFT(this) < 0)
    return;
  if (nX == 0 && nY == 0)
    return;
  int nLeft = g_pos_sort_x[UNIT_BOUND_LEFT(this)].nCoord + nX;
  g_pos_sort_x[UNIT_BOUND_LEFT(this)].nCoord = nLeft;
  int nRight = g_pos_sort_x[UNIT_BOUND_RIGHT(this)].nCoord + nX;
  g_pos_sort_x[UNIT_BOUND_RIGHT(this)].nCoord = nRight;
  int nTop = g_pos_sort_y[UNIT_BOUND_TOP(this)].nCoord + nY;
  g_pos_sort_y[UNIT_BOUND_TOP(this)].nCoord = nTop;
  int nBottom = g_pos_sort_y[UNIT_BOUND_BOTTOM(this)].nCoord + nY;
  g_pos_sort_y[UNIT_BOUND_BOTTOM(this)].nCoord = nBottom;
  if (nX > 0) {
    while (UNIT_BOUND_RIGHT(this) + 1 < (int)CUnit__nSorted &&
           nRight > g_pos_sort_x[UNIT_BOUND_RIGHT(this) + 1].nCoord) {
      TrackXShift(g_pos_sort_x, UNIT_BOUND_RIGHT(this) + 1, -1);
      ++UNIT_BOUND_RIGHT(this);
    }
    while (UNIT_BOUND_LEFT(this) + 1 < (int)CUnit__nSorted &&
           nLeft > g_pos_sort_x[UNIT_BOUND_LEFT(this) + 1].nCoord) {
      TrackXShift(g_pos_sort_x, UNIT_BOUND_LEFT(this) + 1, -1);
      ++UNIT_BOUND_LEFT(this);
    }
  } else if (nX < 0) {
    while (UNIT_BOUND_LEFT(this) > 0 &&
           nLeft < g_pos_sort_x[UNIT_BOUND_LEFT(this) - 1].nCoord) {
      TrackXShift(g_pos_sort_x, UNIT_BOUND_LEFT(this) - 1, 1);
      --UNIT_BOUND_LEFT(this);
    }
    while (UNIT_BOUND_RIGHT(this) > 0 &&
           nRight < g_pos_sort_x[UNIT_BOUND_RIGHT(this) - 1].nCoord) {
      TrackXShift(g_pos_sort_x, UNIT_BOUND_RIGHT(this) - 1, 1);
      --UNIT_BOUND_RIGHT(this);
    }
  }
  if (nY > 0) {
    while (UNIT_BOUND_BOTTOM(this) + 1 < (int)CUnit__nSorted &&
           nBottom > g_pos_sort_y[UNIT_BOUND_BOTTOM(this) + 1].nCoord) {
      TrackYShift(g_pos_sort_y, UNIT_BOUND_BOTTOM(this) + 1, -1);
      ++UNIT_BOUND_BOTTOM(this);
    }
    while (UNIT_BOUND_TOP(this) + 1 < (int)CUnit__nSorted &&
           nTop > g_pos_sort_y[UNIT_BOUND_TOP(this) + 1].nCoord) {
      TrackYShift(g_pos_sort_y, UNIT_BOUND_TOP(this) + 1, -1);
      ++UNIT_BOUND_TOP(this);
    }
  } else if (nY < 0) {
    while (UNIT_BOUND_TOP(this) > 0 &&
           nTop < g_pos_sort_y[UNIT_BOUND_TOP(this) - 1].nCoord) {
      TrackYShift(g_pos_sort_y, UNIT_BOUND_TOP(this) - 1, 1);
      --UNIT_BOUND_TOP(this);
    }
    while (UNIT_BOUND_BOTTOM(this) > 0 &&
           nBottom < g_pos_sort_y[UNIT_BOUND_BOTTOM(this) - 1].nCoord) {
      TrackYShift(g_pos_sort_y, UNIT_BOUND_BOTTOM(this) - 1, 1);
      --UNIT_BOUND_BOTTOM(this);
    }
  }
  FinderSideUnit(this);  // Hyperion
}
