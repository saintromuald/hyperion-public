// find.cpp -- the generic unit-proximity searches: the rectangle walks, the
// nearest-unit scan over the two position-sorted bounds arrays, and the callback
// walk.
//
// Bodies are in the classic game's own order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/find.h"
#include "../h/rect16.h"
#include "../h/CSprite.h"
#include "../h/CUnit.h"
#include "../h/CUnitBounds.h"
#include "../h/CUnitFind.h"
#include "../h/CUnitSearches.h"
#include "../h/CUnitTerran.h"
#include "../h/CBullet.h"
#include "../h/TrigEnum.h"
#include "../h/CTarget.h"
#include "../h/order.h"
#include "../gen/globals117.h"

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload. CUnit__nSorted is `int` here and not the `DWORD`
// CUnitBounds.cpp uses: sub_442D90 compares it against a signed index with
// `jge`, and it is the only body that reads the sign.

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
extern int CUnit__nSorted;
#endif
#ifdef OC_DLL_MODE
#define CUnit__nSorted (*reinterpret_cast<int *>(OC_VA(0x5C99B0u)))
#endif

CUnit *__fastcall sub_442CC0(spos_t x, spos_t y, WORD nRadius, find_filter_t pfnFilter,
                             void *pContext);
CUnit *__fastcall sub_442D90(CUnit *pExclude, spos_t nPosX, spos_t nPosY,
                             int xLo, int yLo, int xHi, int yHi,
                             const mrect_t *pArea, find_filter_t pfnFilter,
                             void *pContext);

// A sort-array entry holds a one-based unit id, so the unit is Head() + id - 1.
// Carrying the id in the pointer keeps the null arm at zero with no store.
static __forceinline CUnit *unit_from_sort_id(DWORD dwUnitId)
{
  CUnit *pUnit = reinterpret_cast<CUnit *>((DWORD_PTR)dwUnitId);

  if (pUnit != NULL)
    pUnit = CUnit::Head() + (dwUnitId - 1);
  return pUnit;
}

// find_first_unit_near.
//
// Search a square of side 2*nRadius centred on (x, y). This one fills the
// rectangle left, top, right, bottom, where the other builders in this file fill
// it left, right, top, bottom.
extern "C" CUnit *__fastcall find_first_unit_near(int x, int y, int nRadius,
                                        pfn_bounce_filter_t pfnFilter,
                                        intptr_t nUnused)
{
  mrect_t rc;  // Hyperion: a box of map positions

  rc.left = PosS(x - nRadius);
  rc.top = PosS(y - nRadius);
  rc.right = PosS(x + nRadius);
  rc.bottom = PosS(y + nRadius);

  return sub_442B60(&rc, reinterpret_cast<CUnitTerranFindCb>(pfnFilter),
                    reinterpret_cast<CUnit *>(nUnused));
}

// sub_442B60.
//
// Return the first unit overlapping pArea that the callback accepts, or the head
// of the list when there is no callback. The `||` guard, rather than an if/else
// over the same load, is what gives the list load its two copies.
CUnit *__fastcall sub_442B60(const mrect_t *pArea, CUnitTerranFindCb pfnCallback,
                             CUnit *pContext)
{
  CUnit **ppList = get_units_in_rect(const_cast<mrect_t *>(pArea));
  CUnit **pp = ppList;
  CUnit *pUnit;

  if (pfnCallback != NULL || (pUnit = *pp) == NULL) {
    pUnit = *pp;
    while (pUnit != NULL) {
      if (pfnCallback(pUnit, pContext))
        break;
      ++pp;
      pUnit = *pp;
    }
  }

  finish_unit_iteration(ppList);
  return pUnit;
}

// CUnit::sub_442BC0.
//
// The nearest-unit search over a square of side 2*nRange centred on this
// unit's sprite.
CUnit *CUnit::sub_442BC0(int nRange, BOOL(__fastcall *pFilter)(CUnit *, CUnit *),
                         CUnit *pUnit)
{
  mrect_t  rc;
  // __restrict lets the wPosY load hoist above the rc stores: sub_442D90 only
  // copies *pArea.
  CSprite *__restrict pSprite = this->pSprite;

  rc.left = PosS(pSprite->wPosX - nRange);
  rc.right = PosS(pSprite->wPosX + nRange);
  rc.top = PosS(pSprite->wPosY - nRange);
  rc.bottom = PosS(pSprite->wPosY + nRange);

  return this->FindNearestInRect(&rc, pFilter, pUnit);
}

// CUnit::FindNearestInRect.
//
// Seed the outward scan from this unit's own bounds indices, or from a fresh
// binary search when sprite flag 0x20 is set. sx and sy are 16-bit locals so the
// WORD load and its sign extension stay separate.
CUnit *CUnit::FindNearestInRect(const mrect_t *pSearchData, BOOL(__fastcall *pFilter)(CUnit *, CUnit *),
                         CUnit *pUnit)
{
  int xLo, yLo, xHi, yHi, nPosX, nPosY;
  spos_t sx, sy;

  if ((this->pSprite->flags.b & 0x20) == 0) {
    xLo = *reinterpret_cast<int *>(reinterpret_cast<BYTE *>(this) + offsetof(CUnit, _unmapped_138) + 0x08) - 1;
    xHi = *reinterpret_cast<int *>(reinterpret_cast<BYTE *>(this) + offsetof(CUnit, _unmapped_138) + 0x04) + 1;
    yLo = *reinterpret_cast<int *>(reinterpret_cast<BYTE *>(this) + offsetof(CUnit, _unmapped_138) + 0x10) - 1;
    yHi = *reinterpret_cast<int *>(reinterpret_cast<BYTE *>(this) + offsetof(CUnit, _unmapped_138) + 0x0C) + 1;
  } else {
    sx = this->pSprite->wPosX;
    xHi = SearchFinder(g_pos_sort_x, sx, 1);
    sy = this->pSprite->wPosY;
    yHi = SearchFinder(g_pos_sort_y, sy, 1);
    xLo = xHi - 1;
    yLo = yHi - 1;
  }

  nPosY = PosU(this->pSprite->wPosY);
  nPosX = this->pSprite->wPosX;
  return sub_442D90(this, nPosX, nPosY, xLo, yLo, xHi, yHi,
                    pSearchData, reinterpret_cast<find_filter_t>(pFilter), pUnit);
}

// oracle 0x442CC0. The free-function nearest-unit search: a square of side
// 2*nRadius around (x, y), with no unit excluded. The radius must be declared
// 16-bit: any 32-bit type, even narrowed in the body, flips both `lea` SIBs.
CUnit *__fastcall sub_442CC0(spos_t x, spos_t y, WORD nRadius, find_filter_t pfnFilter,
                             void *pContext)
{
  mrect_t rc;  // Hyperion: a box of map positions
  int nIdxX, nIdxY;

  rc.left = PosS(x - nRadius);
  rc.right = PosS(x + nRadius);
  rc.top = PosS(y - nRadius);
  rc.bottom = PosS(y + nRadius);

  nIdxX = SearchFinder(g_pos_sort_x, x, 1);
  nIdxY = SearchFinder(g_pos_sort_y, y, 1);

  return sub_442D90(NULL, x, y, nIdxX - 1, nIdxY - 1, nIdxX, nIdxY, &rc,
                    pfnFilter, pContext);
}

// sub_442D40.
//
// sub_442CC0 with the search rectangle supplied by the caller instead of built
// from a radius.
CUnit *__fastcall sub_442D40(int x, int y, const mrect_t *pSearchData,
                             BOOL(__fastcall *pFilter)(CUnit *, int), intptr_t nPlayer)
{
  int nIdxX = SearchFinder(g_pos_sort_x, PosS(x), 1);
  int nIdxY = SearchFinder(g_pos_sort_y, PosS(y), 1);

  return sub_442D90(NULL, x, y, nIdxX - 1, nIdxY - 1, nIdxX, nIdxY,
                    pSearchData,
                    reinterpret_cast<find_filter_t>(pFilter),
                    reinterpret_cast<void *>(nPlayer));
}

// sub_442D90.
// Expand outward through the two position-sorted bounds arrays, keeping the
// accepted candidate closest to the point. The last scan's two identical veclen
// arms are deliberate: they make bFound outrank rc.top for EBX, and the compiler
// merges the calls again.
CUnit *__fastcall sub_442D90(CUnit *pExclude, spos_t nPosX, spos_t nPosY,
                             int xLo, int yLo, int xHi, int yHi,
                             const mrect_t *pArea, find_filter_t pfnFilter,
                             void *pContext)
{
  mrect_t  rc = *pArea;
  CUnit *pBest = NULL;
  int nBest, nSpan, nDy, nDist, t, bMore, bFound;
  spos_t sx, sy;
  CUnit *p;

  nSpan = PosS(nPosX) - rc.left;
  t = rc.right - PosS(nPosX);
  if (t > nSpan)
    nSpan = t;
  nDy = PosS(nPosY) - rc.top;
  t = rc.bottom - PosS(nPosY);
  if (t > nDy)
    nDy = t;
  nBest = veclen(0, nSpan, 0, nDy);

  do {
    bMore = 0;
    bFound = 0;

    if (xLo >= 0) {
      p = unit_from_sort_id(g_pos_sort_x[xLo].dwUnitId);
      if (PosS(p->pSprite->wPosX) < rc.left) {
        xLo = -1;
      } else if (p->pSprite->wPosY >= rc.top && p->pSprite->wPosY < rc.bottom
                 && p != pExclude && pfnFilter(p, pContext)) {
        sy = p->pSprite->wPosY;
        sx = p->pSprite->wPosX;
        nDist = veclen(sx, PosS(nPosX), sy, PosS(nPosY));
        if (nDist < nBest) {
          nBest = nDist;
          pBest = p;
          bFound = 1;
        }
      }
      --xLo;
      bMore = 1;
    }

    if (xHi < CUnit__nSorted) {
      p = unit_from_sort_id(g_pos_sort_x[xHi].dwUnitId);
      if (PosS(p->pSprite->wPosX) >= rc.right) {
        xHi = CUnit__nSorted;
      } else if (p->pSprite->wPosY >= rc.top && p->pSprite->wPosY < rc.bottom
                 && p != pExclude && pfnFilter(p, pContext)) {
        sy = p->pSprite->wPosY;
        sx = p->pSprite->wPosX;
        nDist = veclen(sx, PosS(nPosX), sy, PosS(nPosY));
        if (nDist < nBest) {
          nBest = nDist;
          pBest = p;
          bFound = 1;
        }
      }
      bMore = 1;
      ++xHi;
    }

    if (yLo >= 0) {
      p = unit_from_sort_id(g_pos_sort_y[yLo].dwUnitId);
      if (p->pSprite->wPosY < rc.top) {
        yLo = -1;
      } else if (PosS(p->pSprite->wPosX) >= rc.left
                 && PosS(p->pSprite->wPosX) < rc.right
                 && p != pExclude && pfnFilter(p, pContext)) {
        sy = p->pSprite->wPosY;
        sx = p->pSprite->wPosX;
        nDist = veclen(sx, PosS(nPosX), sy, PosS(nPosY));
        if (nDist < nBest) {
          nBest = nDist;
          pBest = p;
          bFound = 1;
        }
      }
      --yLo;
      bMore = 1;
    }

    if (yHi < CUnit__nSorted) {
      p = unit_from_sort_id(g_pos_sort_y[yHi].dwUnitId);
      if (p->pSprite->wPosY >= rc.bottom) {
        yHi = CUnit__nSorted;
      } else if (PosS(p->pSprite->wPosX) >= rc.left
                 && PosS(p->pSprite->wPosX) < rc.right
                 && p != pExclude && pfnFilter(p, pContext)) {
        sy = p->pSprite->wPosY;
        sx = p->pSprite->wPosX;
        if (bFound)
          nDist = veclen(sx, PosS(nPosX), sy, PosS(nPosY));
        else
          nDist = veclen(sx, PosS(nPosX), sy, PosS(nPosY));
        if (nDist < nBest) {
          nBest = nDist;
          pBest = p;
          bFound = 1;
        }
      }
      bMore = 1;
      ++yHi;
    }

    if (bFound) {
      if (rc.left < PosS(nPosX) - nBest)
        rc.left = PosS(nPosX - nBest);
      if (rc.right > PosS(nPosX) + nBest)
        rc.right = PosS(nPosX + nBest);
      if (rc.top < PosS(nPosY) - nBest)
        rc.top = PosS(nPosY - nBest);
      if (rc.bottom > PosS(nPosY) + nBest)
        rc.bottom = PosS(nPosY + nBest);
    }
  } while (bMore);

  return pBest;
}

// find_foreach_unit.
//
// Walk the units overlapping pArea, stopping at the first the callback accepts;
// NULL when the walk runs out. Each of the three exits returns a different value
// and so carries its own finish_unit_iteration.
CUnit *__fastcall find_foreach_unit(const mrect_t *pArea, FindUnitCb pfnCb,
                                    void *pCtx)
{
  CUnit **ppList = find_units_in_rect(const_cast<mrect_t *>(pArea));
  CUnit **pp = ppList;
  CUnit *pUnit;

  if (pfnCb != NULL && *ppList != NULL) {
    do {
      if (pfnCb(*pp, pCtx)) {
        pUnit = *pp;
        finish_unit_iteration(ppList);
        return pUnit;
      }
      ++pp;
    } while (*pp != NULL);
    finish_unit_iteration(ppList);
    return NULL;
  }

  pUnit = *ppList;
  finish_unit_iteration(ppList);
  return pUnit;
}
